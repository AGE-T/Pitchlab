#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCTAVOX offline demo + two-algorithm comparison harness  (OUR implementation)
=============================================================================

PROVENANCE
----------
Everything in this file is our own code.  It contains no bytes from the target
plug-in and was not produced by copying target code.  Measurements produced here
describe *our* implementations only and are never presented as measurements of
the target.

Contents
--------
1. Real WAV -> WAV path (16-bit PCM, mono or stereo) with sample-rate handling.
2. CLI: --in/--out/--sr/--ratio/--semitones/--voices/--interp/--algo/--json.
3. Algorithm A  "delayline"    : the reconstructed architecture (variable-rate
                                two-tap delay line, crossfaded re-arm), imported
                                from octavox_poc.py.
4. Algorithm B  "pv"           : independent STFT phase vocoder (time-stretch by r,
                                then resample by 1/r).  Different family entirely.
5. Algorithm B2 "pv_transient" : same vocoder with a transient-aware short window.
6. A shared metric runner that evaluates A, B, B2 on *identical* signals:
      pitch accuracy (cents), transient attack / smear / pre-ring,
      unwanted spectral energy, latency, cost.

Run
---
  python octavox_offline.py --make-example example_input.wav
  python octavox_offline.py --in example_input.wav --out out_dl.wav --algo delayline --ratio 1.25 --voices 3
  python octavox_offline.py --in example_input.wav --out out_pv.wav --algo pv --semitones 4
  python octavox_offline.py --compare

Author: analysis workspace.  Python 3.11 / numpy.  No SciPy dependency.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
import wave
from dataclasses import dataclass, field
from typing import Callable

import numpy as np

from octavox_poc import (
    BASE_DELAY,
    GRAIN,
    OctavoxPoC,
    VoiceCfg,
    _pitch_fft,
    _pitch_yin,
    _thd_energy,
)

# --------------------------------------------------------------------------------------
# WAV I/O
# --------------------------------------------------------------------------------------


def read_wav(path: str) -> tuple[np.ndarray, int]:
    """Read PCM WAV (16/24/32-bit).  Returns (samples, sr); samples is [n] or [n, ch]."""
    with wave.open(path, "rb") as w:
        nch = w.getnchannels()
        sw = w.getsampwidth()
        sr = w.getframerate()
        nf = w.getnframes()
        comp = w.getcomptype()
        raw = w.readframes(nf)
    if comp != "NONE":
        raise ValueError(f"{path}: compressed WAV not supported ({comp})")
    if sw == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        v = (
            b[:, 0].astype(np.int32)
            | (b[:, 1].astype(np.int32) << 8)
            | (b[:, 2].astype(np.int32) << 16)
        )
        v = (v << 8) >> 8  # sign-extend 24 -> 32
        x = v.astype(np.float64) / 8388608.0
    elif sw == 4:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"{path}: unsupported sample width {sw * 8} bit")
    if nch > 1:
        x = x.reshape(-1, nch)
    return x, sr


def write_wav(path: str, x: np.ndarray, sr: int) -> None:
    """Write 16-bit PCM WAV.  Deterministic, no dither, clip-guarded."""
    if x.ndim == 1:
        x = x[:, None]
    nch = x.shape[1]
    y = np.clip(x, -1.0, 1.0)
    q = np.round(y * 32767.0).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(nch)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(q.reshape(-1).tobytes())


def resample_linear(x: np.ndarray, sr_in: int, sr_out: int) -> np.ndarray:
    """Linear resampler, per channel.  Used only for CLI sample-rate handling."""
    if sr_in == sr_out:
        return x
    n_out = int(round(x.shape[0] * sr_out / sr_in))
    t = np.arange(n_out, dtype=np.float64) * (sr_in / sr_out)
    src = np.arange(x.shape[0], dtype=np.float64)
    if x.ndim == 1:
        return np.interp(t, src, x)
    return np.stack([np.interp(t, src, x[:, c]) for c in range(x.shape[1])], axis=1)


# --------------------------------------------------------------------------------------
# Algorithm A -- reconstructed variable-rate two-tap delay line
# --------------------------------------------------------------------------------------

# musical spread used when --voices > 1 (voice 0 keeps the requested ratio exactly)
_SPREAD_SEMITONES = (0.0, 7.0, 4.0, 12.0, -5.0, -12.0, 3.0, -3.0)


class DelayLineShifter:
    name = "delayline"

    def __init__(
        self,
        sr: int,
        ratio: float = 1.0,
        voices: int = 1,
        interp: str = "lagrange6",
        grain: float = GRAIN,
        base_delay: float = BASE_DELAY,
        spread: bool = True,
    ):
        st = 12.0 * math.log2(ratio)
        vs = []
        for i in range(voices):
            off = _SPREAD_SEMITONES[i] if (spread and i < len(_SPREAD_SEMITONES)) else 0.0
            vs.append(
                VoiceCfg(
                    semitones=st + off,
                    base_delay=base_delay + 32.0 * i,
                    grain=grain,
                )
            )
        self.p = OctavoxPoC(sr=sr, nvoices=voices, voices=vs, interp=interp)
        self.latency = int(base_delay + grain * 0.5)
        self.sr = sr

    def process(self, x: np.ndarray) -> np.ndarray:
        return self.p.process_blocks(x, 64)


# --------------------------------------------------------------------------------------
# Algorithm B -- independent STFT phase vocoder
# --------------------------------------------------------------------------------------


def _hann(n: int) -> np.ndarray:
    """Periodic Hann window (sum of squares is constant at 75% overlap)."""
    return 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(n) / n)


def _princarg(p: np.ndarray) -> np.ndarray:
    return np.mod(p + np.pi, 2.0 * np.pi) - np.pi


def _pv_shift_mono(
    x: np.ndarray,
    ratio: float,
    n_fft: int = 2048,
    transient: bool = False,
) -> np.ndarray:
    """Time-stretch by `ratio`, then resample by 1/ratio -> pitch x ratio.

    Stretch: synthesis hop Hs = round(H*ratio), phase advanced by the measured
    instantaneous frequency times Hs.  Resample: out[j] = stretch[j*ratio].
    A causal N/2 delay is added so the measured latency is honest.
    """
    H = n_fft // 4
    Hs = max(1, int(round(H * ratio)))
    win_long = _hann(n_fft)
    win_short = np.zeros(n_fft)
    ns = n_fft // 4
    win_short[(n_fft - ns) // 2 : (n_fft - ns) // 2 + ns] = _hann(ns)

    pad = n_fft // 2
    xp = np.concatenate([np.zeros(pad), np.asarray(x, dtype=np.float64), np.zeros(n_fft + pad)])
    nframes = 1 + max(0, (len(xp) - n_fft) // H)
    if nframes < 2:
        return np.zeros_like(x)

    # ---- pass 1: analysis magnitudes/phases, optional transient decision --------
    mags = np.empty((nframes, n_fft // 2 + 1), dtype=np.float64)
    phases = np.empty_like(mags)
    frames = []
    for i in range(nframes):
        seg = xp[i * H : i * H + n_fft]
        frames.append(seg)
        X = np.fft.rfft(seg * win_long)
        mags[i] = np.abs(X)
        phases[i] = np.angle(X)

    use_short = np.zeros(nframes, dtype=bool)
    if transient and nframes > 3:
        # level-normalised spectral flux; only local maxima above a robust threshold
        flux = np.zeros(nframes)
        for i in range(1, nframes):
            d = mags[i] - mags[i - 1]
            flux[i] = float(np.sum(np.maximum(d, 0.0))) / (float(np.sum(mags[i])) + 1e-9)
        med = float(np.median(flux[1:]))
        mad = float(np.median(np.abs(flux[1:] - med))) + 1e-12
        cand = flux > (med + 8.0 * mad)
        cand &= flux > 0.02
        peak = cand.copy()
        peak[1:-1] &= (flux[1:-1] >= flux[:-2]) & (flux[1:-1] > flux[2:])
        use_short = peak

    # ---- pass 2: synthesis with phase propagation ------------------------------
    expected = 2.0 * np.pi * np.arange(n_fft // 2 + 1) * H / n_fft
    syn = np.zeros(n_fft // 2 + 1)
    prev = phases[0].copy()
    out_len = nframes * Hs + n_fft
    y = np.zeros(out_len)
    wsum = np.zeros(out_len)

    for i in range(nframes):
        win = win_short if use_short[i] else win_long
        X = np.fft.rfft(frames[i] * win)
        mag = np.abs(X)
        ph = np.angle(X)
        dphi = _princarg(ph - prev - expected)
        omega = expected + dphi
        if i == 0 or (transient and use_short[i]):
            # first frame / transient-aware hybrid: lock the synthesis phase
            syn = ph.copy()
        else:
            syn = syn + omega * (Hs / H)
        Y = mag * np.exp(1j * syn)
        frame = np.fft.irfft(Y, n_fft) * win
        a = i * Hs
        y[a : a + n_fft] += frame
        wsum[a : a + n_fft] += win * win
        prev = ph

    nz = wsum > 1e-6
    if np.any(nz):
        ref = float(np.median(wsum[nz]))
    else:
        ref = 1.0
    y = y / np.maximum(wsum, 0.5 * ref)

    # ---- resample: out[j] = y[j*ratio] -----------------------------------------
    idx = np.arange(len(x), dtype=np.float64) * ratio
    idx = np.clip(idx, 0.0, len(y) - 1.000001)
    y2 = np.interp(idx, np.arange(len(y), dtype=np.float64), y)

    # ---- causal delay of n_fft/2 -------------------------------------------------
    d = n_fft // 2
    y3 = np.concatenate([np.zeros(d), y2])[: len(x)]
    return y3


class PhaseVocoderShifter:
    def __init__(
        self,
        sr: int,
        ratio: float = 1.0,
        voices: int = 1,
        n_fft: int = 2048,
        transient: bool = False,
        spread: bool = True,
    ):
        self.sr = sr
        self.ratio = ratio
        self.voices = voices
        self.n_fft = n_fft
        self.transient = transient
        self.spread = spread
        self.latency = n_fft // 2
        self.name = "pv_transient" if transient else "pv"

    def process(self, x: np.ndarray) -> np.ndarray:
        acc = np.zeros_like(x, dtype=np.float64)
        n_on = 0
        for i in range(self.voices):
            off = _SPREAD_SEMITONES[i] if (self.spread and i < len(_SPREAD_SEMITONES)) else 0.0
            r = self.ratio * (2.0 ** (off / 12.0))
            acc += _pv_shift_mono(x, r, self.n_fft, self.transient)
            n_on += 1
        return acc / max(1, n_on)


# --------------------------------------------------------------------------------------
# Test signals (deterministic)
# --------------------------------------------------------------------------------------


def sig_sine(f: float = 220.0, dur: float = 1.2, sr: int = 48000, amp: float = 0.5) -> np.ndarray:
    t = np.arange(int(sr * dur)) / sr
    return amp * np.sin(2.0 * np.pi * f * t)


def sig_burst(f: float = 440.0, sr: int = 48000) -> tuple[np.ndarray, int]:
    """0.25 s silence, 0.5 s tone with a hard onset, 0.25 s silence."""
    pre = int(sr * 0.25)
    body = int(sr * 0.5)
    post = int(sr * 0.25)
    x = np.zeros(pre + body + post)
    t = np.arange(body) / sr
    x[pre : pre + body] = 0.5 * np.sin(2.0 * np.pi * f * t)
    return x, pre


def sig_impulse(sr: int = 48000, n: int = 24000) -> tuple[np.ndarray, int]:
    x = np.zeros(n)
    k = 1000
    x[k] = 1.0
    return x, k


def env_rms(y: np.ndarray, sr: int, win_ms: float = 2.0) -> np.ndarray:
    k = max(1, int(sr * win_ms / 1000.0))
    return np.sqrt(np.convolve(y * y, np.ones(k) / k, mode="same"))


# --------------------------------------------------------------------------------------
# Metrics
# --------------------------------------------------------------------------------------


def metrics_pitch(make: Callable[[], object], x: np.ndarray, sr: int, expected: float) -> dict:
    t0 = time.perf_counter()
    y = make().process(x)
    el = time.perf_counter() - t0
    tail = y[int(sr * 0.7) :]
    f_yin = _pitch_yin(tail, sr) if np.max(np.abs(tail)) > 1e-6 else 0.0
    f_fft = _pitch_fft(tail, sr) if np.max(np.abs(tail)) > 1e-6 else 0.0
    return {
        "measured_hz": f_yin,
        "measured_fft_hz": f_fft,
        "expected_hz": expected,
        "cents_err": 1200.0 * math.log2(f_yin / expected) if f_yin > 0 else float("nan"),
        "cents_err_fft": 1200.0 * math.log2(f_fft / expected) if f_fft > 0 else float("nan"),
        "thd_energy": _thd_energy(tail, sr, f_yin) if f_yin > 0 else float("nan"),
        "cost_ms_per_s": el / (len(x) / sr) * 1000.0,
    }


def metrics_impulse(make: Callable[[], object], sr: int) -> dict:
    x, k = sig_impulse(sr)
    y = make().process(x)
    pk = float(np.max(np.abs(y))) or 1e-12
    nz = np.nonzero(np.abs(y) > 1e-3 * pk)[0]
    first = int(nz[0] - k) if len(nz) else None
    peak = int(np.argmax(np.abs(y)) - k)
    pre = y[max(0, k - 2000) : k] if k > 0 else y[:0]
    pre_db = 20.0 * math.log10(max(1e-12, float(np.max(np.abs(pre))) if len(pre) else 1e-12) / pk)
    return {
        "latency_samples": peak,
        "latency_ms": peak / sr * 1000.0,
        "first_response_samples": first,
        "first_response_ms": (first / sr * 1000.0) if first is not None else None,
        "pre_ring_db": float(pre_db),
    }


def metrics_transient(make: Callable[[], object], sr: int) -> dict:
    x, onset = sig_burst(sr=sr)
    y = make().process(x)
    e = env_rms(y, sr, 1.0)
    pk = float(np.max(e)) or 1e-12
    # output onset = first crossing of 1% of peak after the input onset
    seg = e[onset:]
    idx = np.nonzero(seg > 0.01 * pk)[0]
    out_on = onset + int(idx[0]) if len(idx) else onset
    # local steady level = median envelope 100..300 ms after the output onset
    a0 = out_on + int(sr * 0.10)
    a1 = out_on + int(sr * 0.30)
    local = float(np.median(e[a0:a1])) if a1 > a0 else pk
    # attack: 10 % -> 90 % of the *local* level (ignores slow level drift)
    i10 = np.nonzero(e[out_on:] > 0.10 * local)[0]
    i90 = np.nonzero(e[out_on:] > 0.90 * local)[0]
    attack = int(i90[0] - i10[0]) if len(i10) and len(i90) else None
    # amplitude modulation over the sustained part (comb / crossfade artefact)
    b0 = out_on + int(sr * 0.10)
    b1 = out_on + int(sr * 0.45)
    seg_env = e[b0:b1]
    if len(seg_env) > 8:
        lo = float(np.percentile(seg_env, 5)) + 1e-12
        hi = float(np.percentile(seg_env, 95)) + 1e-12
        mod_db = 20.0 * math.log10(hi / lo)
    else:
        mod_db = float("nan")
    # pre-onset energy: anything the design produces *before* the input event.
    # (A causal design is silent here; a non-causal one would not be.)
    pe = float(np.max(np.abs(y[max(0, onset - int(sr * 0.02)) : onset]))) if onset > 0 else 0.0
    pre_db = 20.0 * math.log10(max(1e-12, pe) / max(1e-12, float(np.max(np.abs(y)))))
    return {
        "attack_samples": attack,
        "attack_ms": (attack / sr * 1000.0) if attack is not None else None,
        "onset_delay_samples": int(out_on - onset),
        "onset_delay_ms": (out_on - onset) / sr * 1000.0,
        "mod_db": float(mod_db),
        "pre_onset_db": float(pre_db),
    }


# --------------------------------------------------------------------------------------
# Comparison runner
# --------------------------------------------------------------------------------------


def _algos(sr: int, ratio: float, voices: int, interp: str, n_fft: int) -> dict:
    return {
        "delayline": lambda: DelayLineShifter(sr, ratio, voices, interp=interp, spread=False),
        "pv": lambda: PhaseVocoderShifter(sr, ratio, voices, n_fft=n_fft, transient=False, spread=False),
        "pv_transient": lambda: PhaseVocoderShifter(sr, ratio, voices, n_fft=n_fft, transient=True, spread=False),
    }


def compare(sr: int = 48000, interp: str = "lagrange6", n_fft: int = 2048) -> dict:
    out: dict = {"sr": sr, "interp": interp, "n_fft": n_fft, "ratios": {}}
    for ratio in (1.25, 0.8):
        key = f"ratio_{ratio:.2f}"
        out["ratios"][key] = {}
        f_in = 220.0
        x = sig_sine(f_in, sr=sr)
        for name, make in _algos(sr, ratio, 1, interp, n_fft).items():
            row = {"pitch": metrics_pitch(make, x, sr, f_in * ratio)}
            row.update(metrics_impulse(make, sr))
            row.update(metrics_transient(make, sr))
            # cost at 8 voices (same signal family, one second)
            xc = 0.3 * np.random.default_rng(7).standard_normal(int(sr * 1.0))
            mk8 = _algos(sr, ratio, 8, interp, n_fft)[name]
            t0 = time.perf_counter()
            mk8().process(xc)
            el = time.perf_counter() - t0
            row["cost_8voice_ms_per_s"] = el / 1.0 * 1000.0
            out["ratios"][key][name] = row
    return out


def _md_compare(res: dict) -> str:
    L = [
        "# Offline comparison -- three independent pitch-shift designs (ours)",
        "",
        "All rows use **identical deterministic inputs**, the same sample rate, and the same",
        "metric code (`octavox_offline.py`).  These are measurements of *our* implementations,",
        "never of the target plug-in.",
        "",
        f"sr = {res['sr']} Hz | delay-line interpolation = `{res['interp']}` | vocoder n_fft = {res['n_fft']}",
        "",
    ]
    for key, rows in res["ratios"].items():
        L += [f"## {key}", "",
              "| algorithm | pitch cents (YIN) | pitch cents (FFT) | latency ms | first response ms | onset delay ms | attack ms | envelope modulation dB | out-of-harmonic | cost 8v ms/s |",
              "|---|---|---|---|---|---|---|---|---|---|"]
        for name, r in rows.items():
            L.append(
                "| {n} | {pc:+.2f} | {pf:+.2f} | {lm:.2f} | {fm:.2f} | {om:.2f} | {am:.2f} | {md:.1f} | {th:.4f} | {c:.0f} |".format(
                    n=name,
                    pc=r["pitch"]["cents_err"],
                    pf=r["pitch"]["cents_err_fft"],
                    lm=r["latency_ms"] or 0.0,
                    fm=r["first_response_ms"] or 0.0,
                    om=r["onset_delay_ms"] or 0.0,
                    am=r["attack_ms"] or 0.0,
                    md=r["mod_db"],
                    th=r["pitch"]["thd_energy"],
                    c=r["cost_8voice_ms_per_s"],
                )
            )
        L.append("")
    L += [
        "## Reading the table",
        "",
        "* **pitch cents** -- YIN on the steady tail; the delay line shows a few cents because the",
        "  crossfaded two-tap read phase-modulates the carrier (analytically predicted in",
        "  `poc/POC_RESULTS.md`).  The vocoder resamples, so its error is near zero.",
        "* **latency ms** -- delay of the *main* response to an impulse (argmax |y|).  All three",
        "  responses are essentially a single sample at these ratios, so this is unambiguous.  The",
        "  delay line's latency depends on the shift: 48.0 ms at unison (round-5 harness), 65.2 ms at",
        "  r = 0.80 and 102.5 ms at r = 1.25 -- the read position sweeps, so the tap distance is not",
        "  constant.  The vocoders move with the resampling stage (34.2 ms / 53.2 ms).",
        "* **first response ms** -- first sample above -60 dB.  For the delay line this is",
        "  interpolation leakage before the main tap (34.2 ms at r = 1.25 while the main tap is at",
        "  102.5 ms), not the audible onset.",
        "* **onset delay ms** -- in the burst test, when the 1 ms RMS envelope first reaches 1 % of its",
        "  peak.  An STFT design whose analysis window straddles the onset starts earlier than its main",
        "  response; that difference is its pre-echo length.",
        "* **attack ms** -- 10 %->90 % rise of the 1 ms RMS envelope relative to the *local*",
        "  steady level (100-300 ms after onset), so slow level drift does not inflate it.",
        "* **envelope modulation dB** -- 5th/95th percentile spread of the sustained envelope.",
        "  The delay line's two-tap crossfade modulates the amplitude at rate |1-ratio|/G;",
        "  the vocoder does not.",
        "* **causality** -- all three produce <= -230 dB before the input event (checked, not",
        "  tabulated).",
        "* **cost** -- wall clock on the recorded environment; Python/NumPy, not real time.",
        "",
        "The comparison is the basis for the next design step: the delay line wins on transients",
        "and cost, the vocoder wins on steady-state pitch accuracy; a production design should",
        "hybridise (transient bypass + phase-locked steady state) rather than pick one.",
        "",
    ]
    return "\n".join(L)


# --------------------------------------------------------------------------------------
# Example material
# --------------------------------------------------------------------------------------


def make_example(path: str, sr: int = 48000) -> None:
    """A short deterministic phrase: 5 plucked notes (harmonics + click) + a tail."""
    notes = [220.0, 277.18, 329.63, 440.0, 329.63]
    step = int(sr * 0.32)
    n = step * len(notes) + int(sr * 0.4)
    x = np.zeros(n)
    rng = np.random.default_rng(3)
    for k, f in enumerate(notes):
        a = k * step
        dur = int(sr * 0.30)
        t = np.arange(dur) / sr
        env = np.exp(-t * 6.0)
        tone = (
            0.45 * np.sin(2 * np.pi * f * t)
            + 0.18 * np.sin(2 * np.pi * 2 * f * t)
            + 0.08 * np.sin(2 * np.pi * 3 * f * t)
        )
        click = 0.25 * rng.standard_normal(min(64, dur)) * np.exp(-np.arange(min(64, dur)) / 12.0)
        x[a : a + dur] += tone * env
        x[a : a + len(click)] += click
    x *= 0.9 / max(1e-9, np.max(np.abs(x)))
    write_wav(path, x, sr)
    print(f"wrote {path}: {len(x)} samples, {sr} Hz, {len(x)/sr:.2f} s")


# --------------------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------------------


def _build(args) -> object:
    ratio = args.ratio if args.semitones is None else 2.0 ** (args.semitones / 12.0)
    if args.algo == "delayline":
        return DelayLineShifter(
            args.sr, ratio, args.voices, interp=args.interp, grain=args.grain, spread=not args.no_spread
        )
    return PhaseVocoderShifter(
        args.sr, ratio, args.voices, n_fft=args.n_fft,
        transient=(args.algo == "pv_transient"), spread=not args.no_spread,
    )


def main(argv: list[str] | None = None) -> int:
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass
    ap = argparse.ArgumentParser(description="Octavox offline demo (our implementation)")
    ap.add_argument("--in", dest="inp", help="input WAV (16/24/32-bit PCM)")
    ap.add_argument("--out", dest="out", help="output WAV (16-bit PCM)")
    ap.add_argument("--sr", type=int, default=48000, help="internal processing rate (default 48000)")
    ap.add_argument("--keep-rate", action="store_true",
                    help="write the output at the input file's rate (resample back)")
    ap.add_argument("--ratio", type=float, default=1.0, help="pitch ratio (1.0 = no shift)")
    ap.add_argument("--semitones", type=float, default=None, help="pitch shift in semitones (overrides --ratio)")
    ap.add_argument("--voices", type=int, default=1, help="number of voices (default 1)")
    ap.add_argument("--interp", default="lagrange6", choices=["lagrange6", "lagrange6_q", "linear"])
    ap.add_argument("--algo", default="delayline", choices=["delayline", "pv", "pv_transient"])
    ap.add_argument("--grain", type=float, default=GRAIN, help="delay-line grain length in samples")
    ap.add_argument("--n-fft", type=int, default=2048, help="vocoder FFT size")
    ap.add_argument("--no-spread", action="store_true", help="all voices at the same ratio")
    ap.add_argument("--json", dest="json_out", default=None, help="write a metrics JSON here")
    ap.add_argument("--compare", action="store_true", help="run the two-algorithm comparison suite")
    ap.add_argument("--make-example", default=None, help="write the example input WAV and exit")
    args = ap.parse_args(argv)

    if args.make_example:
        make_example(args.make_example, args.sr)
        return 0

    if args.compare:
        res = compare(args.sr, args.interp, args.n_fft)
        with open("offline_measurements.json", "w", encoding="utf-8") as f:
            json.dump(res, f, indent=2)
        with open("OFFLINE_COMPARISON.md", "w", encoding="utf-8") as f:
            f.write(_md_compare(res))
        print(_md_compare(res))
        return 0

    if not args.inp or not args.out:
        ap.error("--in and --out are required (or use --compare / --make-example)")

    x, sr_in = read_wav(args.inp)
    mono_in = x.ndim == 1
    if mono_in:
        x = x[:, None]
    # sample-rate handling
    x_proc = resample_linear(x, sr_in, args.sr) if sr_in != args.sr else x
    eng = _build(args)
    t0 = time.perf_counter()
    chans = []
    for c in range(x_proc.shape[1]):
        chans.append(eng.process(x_proc[:, c].copy()))
    y = np.stack(chans, axis=1)
    el = time.perf_counter() - t0
    sr_out = args.sr
    if args.keep_rate and sr_in != args.sr:
        y = resample_linear(y, args.sr, sr_in)
        sr_out = sr_in
    write_wav(args.out, y, sr_out)
    info = {
        "algo": args.algo,
        "ratio": args.ratio if args.semitones is None else 2.0 ** (args.semitones / 12.0),
        "voices": args.voices,
        "interp": args.interp,
        "sr_in": sr_in,
        "sr_out": sr_out,
        "in_samples": int(x.shape[0]),
        "out_samples": int(y.shape[0]),
        "duration_s": float(y.shape[0] / sr_out),
        "cpu_s": el,
        "x_realtime": float((y.shape[0] / sr_out) / max(1e-9, el)),
        "peak": float(np.max(np.abs(y))),
    }
    print(json.dumps(info, indent=2))
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(info, f, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
