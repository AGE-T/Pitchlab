#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCTAVOX round-11 automated acceptance tests for the independent WAV->WAV demo
=============================================================================

Gated pass/fail suite over deterministic signals for the three reference
designs (delayline / pv / pv_transient, used through their documented class
API exactly as `octavox_offline.py --compare` does: a fresh engine per
signal) and the round-11 v2 additions (`octavox_v2.py`: the period-aligned
`delayline_aligned` design and the per-channel-engine stereo fix).

Every gate FAILS the suite when violated (exit code 1); metrics are also
written machine-readably to r11_acceptance_results.json.  Wall-clock cost is
reported for information only (machine-dependent, never gated).

PROVENANCE: everything tested here is OUR implementation.  Nothing is a
measurement of the target plug-in (never executed).  See
OCTAVOX_ROUND11_FINDINGS.md sections 3-5.

Run (from the package root):
    python3 evidence/analysis_round11/r11_acceptance_tests.py
Options: --poc PATH (default: poc), --out PATH (results json), --skip-cli.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
import tempfile
import time
import wave

import numpy as np

# ------------------------------------------------------------------ locate poc
HERE = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.dirname(os.path.dirname(HERE))  # .../Octavox_RE_evidence


def _import_demo(poc_dir: str):
    sys.path.insert(0, poc_dir)
    import octavox_offline as off
    import octavox_v2 as v2
    import octavox_poc as poc
    return off, v2, poc


# ------------------------------------------------------------------ bookkeeping
RESULTS: dict = {"suite": "r11_acceptance_tests", "version": 1, "cases": [], "improvement": {}}
FAILURES: list[str] = []
WARNINGS: list[str] = []


def run_case(case_id: str, group: str, desc: str, fn):
    t0 = time.perf_counter()
    try:
        metrics, gates = fn()
    except Exception as exc:  # noqa: BLE001 - a crash is a hard failure
        metrics, gates = {"exception": repr(exc)}, {"no_exception": False}
    el = time.perf_counter() - t0
    ok = all(bool(v) for v in gates.values())
    bad = [k for k, v in gates.items() if not v]
    if not ok:
        FAILURES.append(f"{case_id}: failed gates {bad} (metrics: {json.dumps(metrics)[:400]})")
    RESULTS["cases"].append({
        "id": case_id, "group": group, "desc": desc, "metrics": metrics,
        "gates": {k: bool(v) for k, v in gates.items()}, "passed": ok,
        "wall_s": el,
    })
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] {case_id:<34} {desc}")
    for k, v in gates.items():
        if not v:
            print(f"         gate FAILED: {k}")
    return ok


def gate_latency(y: np.ndarray, sr: int, k: int, hi_ms: float) -> tuple[int, float]:
    pk = int(np.argmax(np.abs(y)))
    lat = pk - k
    return lat, lat / sr * 1000.0


# ------------------------------------------------------------------ signals
def sig_tone(f: float, dur: float, sr: int, amp: float = 0.5) -> np.ndarray:
    t = np.arange(int(sr * dur)) / sr
    return amp * np.sin(2 * np.pi * f * t)


def sig_chord(sr: int, dur: float = 1.2) -> np.ndarray:
    t = np.arange(int(sr * dur)) / sr
    x = np.zeros(len(t))
    for f, a in ((220.0, 0.3), (277.18, 0.25), (329.63, 0.25)):
        x += a * np.sin(2 * np.pi * f * t)
    return x


def sig_percussive(sr: int, dur: float = 1.5, seed: int = 5) -> np.ndarray:
    """Deterministic percussive material: 6 decaying noise bursts + clicks."""
    rng = np.random.default_rng(seed)
    n = int(sr * dur)
    x = np.zeros(n)
    step = n // 6
    for i in range(6):
        a = i * step
        L = min(step, n - a)
        t = np.arange(L)
        burst = rng.standard_normal(L) * np.exp(-t / max(1, int(sr * 0.012)))
        x[a : a + L] += 0.6 * burst
        tone = np.sin(2 * np.pi * (180.0 + 40.0 * i) * t / sr) * np.exp(-t / max(1, int(sr * 0.08)))
        x[a : a + L] += 0.25 * tone
    return np.clip(x, -1.0, 1.0)


def sig_burst(f: float, sr: int) -> tuple[np.ndarray, int]:
    pre, body, post = int(sr * 0.25), int(sr * 0.5), int(sr * 0.25)
    x = np.zeros(pre + body + post)
    t = np.arange(body) / sr
    x[pre : pre + body] = 0.5 * np.sin(2 * np.pi * f * t)
    return x, pre


# ------------------------------------------------------------------ metrics
def m_pitch(y: np.ndarray, sr: int, f_exp: float, poc) -> dict:
    tail = y[int(sr * 0.7) :] if len(y) > int(sr * 0.7) else y
    if np.max(np.abs(tail)) < 1e-9:
        return {"measured_hz": 0.0, "measured_fft_hz": 0.0, "cents": None, "cents_fft": None}
    f0 = poc._pitch_yin(tail, sr)
    ff = poc._pitch_fft(tail, sr)
    return {
        "measured_hz": f0,
        "measured_fft_hz": ff,
        "cents": 1200.0 * math.log2(f0 / f_exp) if f0 > 0 else None,
        "cents_fft": 1200.0 * math.log2(ff / f_exp) if ff > 0 else None,
    }


def m_env_mod(y: np.ndarray, sr: int, f_out: float, periods: int = 6) -> dict:
    """Envelope modulation with a period-synchronous window (isolates the
    crossfade modulation from the 1 ms window's phase breathing on a tone)."""
    k = max(8, int(round(periods * sr / f_out))) if f_out > 0 else 48
    env = np.sqrt(np.convolve(y * y, np.ones(k) / k, mode="same"))
    core = env[k:-k] if len(env) > 2 * k else env
    p5 = float(np.percentile(core, 5))
    p95 = float(np.percentile(core, 95))
    # shipped 1 ms metric for continuity with the round-6 tables
    k1 = max(1, int(sr * 0.001))
    env1 = np.sqrt(np.convolve(y * y, np.ones(k1) / k1, mode="same"))
    c1 = env1[k1:-k1] if len(env1) > 2 * k1 else env1
    q5 = float(np.percentile(c1, 5))
    q95 = float(np.percentile(c1, 95))
    return {
        "mod_db_sync": 20.0 * math.log10(p95 / max(p5, 1e-12)),
        "mod_db_1ms": 20.0 * math.log10(q95 / max(q5, 1e-12)),
    }


def m_levels(y: np.ndarray) -> dict:
    return {
        "peak": float(np.max(np.abs(y))),
        "rms": float(np.sqrt(np.mean(y * y))),
        "finite": bool(np.all(np.isfinite(y))),
        "n_nonfinite": int(np.sum(~np.isfinite(y))),
        "n_clipped": int(np.sum(np.abs(y) > 1.0)),
    }


def m_transient(y: np.ndarray, sr: int, onset: int) -> dict:
    k = max(1, int(sr * 0.001))
    e = np.sqrt(np.convolve(y * y, np.ones(k) / k, mode="same"))
    pk = float(np.max(e)) or 1e-12
    seg = e[onset:]
    idx = np.nonzero(seg > 0.01 * pk)[0]
    out_on = onset + int(idx[0]) if len(idx) else onset
    a0 = out_on + int(sr * 0.10)
    a1 = out_on + int(sr * 0.30)
    local = float(np.median(e[a0:a1])) if a1 > a0 else pk
    i10 = np.nonzero(e[out_on:] > 0.10 * local)[0]
    i90 = np.nonzero(e[out_on:] > 0.90 * local)[0]
    attack = int(i90[0] - i10[0]) if len(i10) and len(i90) else None
    pre = float(np.max(np.abs(y[max(0, onset - int(sr * 0.02)) : onset]))) if onset > 0 else 0.0
    return {
        "onset_delay_samples": int(out_on - onset),
        "attack_samples": attack,
        "attack_ms": (attack / sr * 1000.0) if attack is not None else None,
        "pre_onset_db": 20.0 * math.log10(max(1e-12, pre) / max(1e-12, float(np.max(np.abs(y))))),
    }


# ------------------------------------------------------------------ the suite
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--poc", default=os.path.join(PKG, "poc"))
    ap.add_argument("--out", default=os.path.join(HERE, "r11_acceptance_results.json"))
    ap.add_argument("--skip-cli", action="store_true")
    args = ap.parse_args()
    off, v2, poc = _import_demo(args.poc)
    sr = 48000

    def ref_engine(algo, ratio, voices=1, spread=False):
        return off._algos(sr, ratio, voices, "lagrange6", 2048)[algo]() if algo != "delayline_aligned" \
            else None

    # =========================================================== A. WAV I/O
    def t_wav_io():
        x = sig_tone(220.0, 0.5, sr)
        metrics, gates = {}, {}
        for sw in (2, 3, 4):
            p = os.path.join(tempfile.mkdtemp(), f"io{sw*8}.wav")
            with wave.open(p, "wb") as w:
                w.setnchannels(1); w.setsampwidth(sw); w.setframerate(sr)
                if sw == 2:
                    w.writeframes(np.round(np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())
                elif sw == 3:
                    v = np.round(np.clip(x, -1, 1) * 8388608).astype(np.int32)
                    b = np.stack([(v & 0xFF).astype(np.uint8), ((v >> 8) & 0xFF).astype(np.uint8),
                                  ((v >> 16) & 0xFF).astype(np.uint8)], axis=1).reshape(-1)
                    w.writeframes(b.tobytes())
                else:
                    w.writeframes(np.round(np.clip(x, -1, 1) * 2147483648).astype("<i4").tobytes())
            xx, srr = off.read_wav(p)
            err = float(np.max(np.abs(xx - x)))
            metrics[f"read{sw*8}bit_max_err"] = err
            gates[f"read{sw*8}bit_ok"] = srr == sr and len(xx) == len(x) and err < 2.0 / (2 ** (sw * 8 - 1))
        # write + clip guard
        p2 = os.path.join(tempfile.mkdtemp(), "clip.wav")
        off.write_wav(p2, np.array([2.0, -2.0, 0.5, -0.5]), sr)
        yy, _ = off.read_wav(p2)
        metrics["write_clip_values"] = [float(v) for v in yy]
        gates["write_clips_to_pm1"] = yy[0] <= 1.0 and yy[1] >= -1.0
        gates["write_preserves_half"] = abs(yy[2] - 0.5) < 2 / 65535
        return metrics, gates
    run_case("wav_io", "io", "read 16/24/32-bit PCM + clip-guarded write", t_wav_io)

    # ================================== B. reference designs (class-level API)
    REF_ALGOS = ("delayline", "pv", "pv_transient")
    PITCH_GATE = {"delayline": 8.0, "pv": 2.0, "pv_transient": 2.0}

    x220 = sig_tone(220.0, 1.2, sr)
    xchord = sig_chord(sr)
    xperc = sig_percussive(sr)
    xsil = np.zeros(int(sr * 0.5))
    xlow = (10 ** (-60 / 20)) * sig_tone(220.0, 0.5, sr)
    xburst, burst_on = sig_burst(440.0, sr)
    xnoise = 0.2 * np.random.default_rng(11).standard_normal(int(sr * 4.0))
    imp = np.zeros(sr); imp[1000] = 1.0

    for algo in REF_ALGOS:
        # --- sustained tone, +4 st
        def t_pitch_pos(algo=algo):
            r = 2 ** (4 / 12)
            y = off._algos(sr, r, 1, "lagrange6", 2048)[algo]().process(x220.copy())
            m = m_pitch(y, sr, 220.0 * r, poc); m.update(m_levels(y))
            m.update(m_env_mod(y[int(sr * 0.7):], sr, 220.0 * r))
            g = {"finite": m["finite"], "len": len(y) == len(x220),
                 "pitch_cents": abs(m["cents"]) <= PITCH_GATE[algo],
                 "peak_le_1": m["peak"] <= 1.0 + 1e-9}
            return m, g
        run_case(f"ref_{algo}_pitch_pos", "reference", "220 Hz sine, +4 st, 1 voice", t_pitch_pos)

        # --- sustained tone, -5 st
        def t_pitch_neg(algo=algo):
            r = 2 ** (-5 / 12)
            y = off._algos(sr, r, 1, "lagrange6", 2048)[algo]().process(x220.copy())
            m = m_pitch(y, sr, 220.0 * r, poc); m.update(m_levels(y))
            m.update(m_env_mod(y[int(sr * 0.7):], sr, 220.0 * r))
            g = {"finite": m["finite"], "len": len(y) == len(x220),
                 "pitch_cents": abs(m["cents"]) <= PITCH_GATE[algo] + 1.0,
                 "peak_le_1": m["peak"] <= 1.0 + 1e-9}
            return m, g
        run_case(f"ref_{algo}_pitch_neg", "reference", "220 Hz sine, -5 st, 1 voice", t_pitch_neg)

        # --- silence / very low level
        def t_silence(algo=algo):
            y = off._algos(sr, 1.25, 1, "lagrange6", 2048)[algo]().process(xsil.copy())
            m = m_levels(y)
            return m, {"finite": m["finite"], "silent": m["peak"] <= 1e-9,
                       "len": len(y) == len(xsil)}
        run_case(f"ref_{algo}_silence", "reference", "digital silence in -> silence out", t_silence)

        def t_lowlevel(algo=algo):
            y = off._algos(sr, 1.25, 1, "lagrange6", 2048)[algo]().process(xlow.copy())
            m = m_levels(y)
            m["rms_ratio"] = m["rms"] / float(np.sqrt(np.mean(xlow ** 2)))
            return m, {"finite": m["finite"], "level_kept": 0.2 <= m["rms_ratio"] <= 2.0,
                       "len": len(y) == len(xlow)}
        run_case(f"ref_{algo}_lowlevel", "reference", "-60 dBFS tone, +4 st", t_lowlevel)

        # --- unison
        def t_unison(algo=algo):
            y = off._algos(sr, 1.0, 1, "lagrange6", 2048)[algo]().process(x220.copy())
            tail = y[int(sr * 0.3):]; xin = x220[int(sr * 0.3):]
            g_db = 20.0 * math.log10(float(np.sqrt(np.mean(tail ** 2))) /
                                      max(1e-12, float(np.sqrt(np.mean(xin ** 2)))))
            m = m_levels(y); m["unison_gain_db"] = g_db
            lim = (-6.0, 1.0) if algo == "delayline" else (-0.75, 0.75)
            return m, {"finite": m["finite"], "gain_in_range": lim[0] <= g_db <= lim[1]}
        run_case(f"ref_{algo}_unison", "reference", "unison gain (delayline comb is documented)",
                 t_unison)

        # --- chord + percussive: integrity gates, artifacts reported
        def t_chord(algo=algo):
            y = off._algos(sr, 1.25, 1, "lagrange6", 2048)[algo]().process(xchord.copy())
            m = m_levels(y)
            m["pitch"] = m_pitch(y, sr, 275.0, poc)
            return m, {"finite": m["finite"], "len": len(y) == len(xchord),
                       "peak_le_1": m["peak"] <= 1.0 + 1e-9, "audible": m["rms"] > 1e-3}
        run_case(f"ref_{algo}_chord", "reference", "A3+C#4+E4 chord, +4 st", t_chord)

        def t_percussive(algo=algo):
            y = off._algos(sr, 1.25, 1, "lagrange6", 2048)[algo]().process(xperc.copy())
            m = m_levels(y); m["transient"] = m_transient(y, sr, int(sr * 0.02))
            # sharp broadband bursts legitimately overshoot unity in the float domain
            # (Lagrange/vocoder transient response); the WAV write path clip-guards.
            atk = m["transient"]["attack_ms"]
            return m, {"finite": m["finite"], "len": len(y) == len(xperc),
                       "peak_sane": m["peak"] <= 1.6,
                       "causal": m["transient"]["pre_onset_db"] <= -40.0,
                       "attack_bound": (atk if atk is not None else 1e9) <= 250.0}
        run_case(f"ref_{algo}_percussive", "reference", "6 decaying noise bursts, +4 st", t_percussive)

        # --- 8 voices
        def t_voices8(algo=algo):
            y = off._algos(sr, 1.25, 8, "lagrange6", 2048)[algo]().process(x220.copy())
            m = m_levels(y); m.update(m_env_mod(y[int(sr * 0.7):], sr, 275.0))
            return m, {"finite": m["finite"], "len": len(y) == len(x220),
                       "peak_le_1": m["peak"] <= 1.0 + 1e-9, "audible": m["rms"] > 1e-3}
        run_case(f"ref_{algo}_voices8", "reference", "220 Hz, +4 st, 8 voices (spread)", t_voices8)

        # --- impulse latency (info + bound)
        def t_impulse(algo=algo):
            y = off._algos(sr, 1.25, 1, "lagrange6", 2048)[algo]().process(imp.copy())
            lat, lat_ms = gate_latency(y, sr, 1000, 200)
            m = m_levels(y); m["latency_samples"] = lat; m["latency_ms"] = lat_ms
            return m, {"finite": m["finite"], "len": len(y) == len(imp),
                       "latency_bound": 0 <= lat <= int(sr * 0.30)}
        run_case(f"ref_{algo}_impulse", "reference", "impulse response / latency", t_impulse)

    # --- feedback stability (delayline family only; vocoders have no feedback path)
    def t_feedback_ref():
        p = poc.OctavoxPoC(sr=sr, nvoices=8, voices=[poc.VoiceCfg(semitones=s, base_delay=poc.BASE_DELAY + 32.0 * i)
                                                     for i, s in enumerate((0, 3, 4, 5, 7, -3, -4, -5))],
                           interp="lagrange6", feedback=0.35)
        y = p.process_blocks(xnoise.copy(), 64)
        m = m_levels(y)
        return m, {"finite": m["finite"], "bounded": m["peak"] < 100.0, "len": len(y) == len(xnoise)}
    run_case("ref_delayline_feedback8", "reference", "8 voices, feedback 0.35, 4 s noise", t_feedback_ref)

    # ============================================ C. v2: aligned design + stereo
    def aligned(ratio, voices=1, spread=False, feedback=0.0, grain=poc.GRAIN):
        return v2.AlignedDelayLineShifter(sr, ratio, voices, spread=spread, feedback=feedback, grain=grain)

    for r, tag in ((2 ** (4 / 12), "pos4"), (2 ** (-5 / 12), "neg5"), (2.0, "oct_up"), (0.5, "oct_down")):
        def t_aligned_pitch(r=r):
            y = aligned(r).process(x220.copy())
            m = m_pitch(y, sr, 220.0 * r, poc); m.update(m_levels(y))
            m.update(m_env_mod(y[int(sr * 0.7):], sr, 220.0 * r))
            g = {"finite": m["finite"], "len": len(y) == len(x220),
                 "pitch_cents_tight": abs(m["cents"]) <= 2.0,
                 "mod_sync_tight": m["mod_db_sync"] <= 0.75,
                 "peak_le_1": m["peak"] <= 1.0 + 1e-9}
            return m, g
        run_case(f"v2_aligned_pitch_{tag}", "v2", f"220 Hz sine, ratio {r:.4f}, aligned", t_aligned_pitch)

    def t_aligned_chord():
        y = aligned(1.25).process(xchord.copy())
        m = m_levels(y); m["pitch"] = m_pitch(y, sr, 275.0, poc)
        m.update(m_env_mod(y[int(sr * 0.7):], sr, 275.0))
        return m, {"finite": m["finite"], "len": len(y) == len(xchord),
                   "peak_le_1": m["peak"] <= 1.0 + 1e-9, "audible": m["rms"] > 1e-3}
    run_case("v2_aligned_chord", "v2", "chord, +4 st, aligned (partial-period alignment)", t_aligned_chord)

    def t_aligned_percussive():
        y = aligned(1.25).process(xperc.copy())
        m = m_levels(y); m["transient"] = m_transient(y, sr, int(sr * 0.02))
        atk = m["transient"]["attack_ms"]
        return m, {"finite": m["finite"], "len": len(y) == len(xperc),
                   "peak_sane": m["peak"] <= 1.6,
                   "causal": m["transient"]["pre_onset_db"] <= -40.0,
                   "attack_bound": (atk if atk is not None else 1e9) <= 250.0}
    run_case("v2_aligned_percussive", "v2", "percussive bursts, +4 st, aligned", t_aligned_percussive)

    def t_aligned_unison():
        y = aligned(1.0).process(x220.copy())
        tail = y[int(sr * 0.3):]; xin = x220[int(sr * 0.3):]
        g_db = 20.0 * math.log10(float(np.sqrt(np.mean(tail ** 2))) /
                                  max(1e-12, float(np.sqrt(np.mean(xin ** 2)))))
        m = m_levels(y); m["unison_gain_db"] = g_db
        return m, {"finite": m["finite"], "gain_in_range": -6.0 <= g_db <= 1.0,
                   "note": "unison comb is the documented limitation (no wraps -> no commit)"}
    run_case("v2_aligned_unison", "v2", "unison (documented comb limitation)", t_aligned_unison)

    def t_aligned_voices8():
        y = aligned(1.25, 8).process(x220.copy())
        m = m_levels(y); m.update(m_env_mod(y[int(sr * 0.7):], sr, 275.0))
        return m, {"finite": m["finite"], "len": len(y) == len(x220),
                   "peak_le_1": m["peak"] <= 1.0 + 1e-9, "audible": m["rms"] > 1e-3}
    run_case("v2_aligned_voices8", "v2", "220 Hz, +4 st, 8 voices (spread), aligned", t_aligned_voices8)

    def t_aligned_feedback():
        p = v2.AlignedDelayLineShifter(sr, 1.25, 8, spread=False, feedback=0.35)
        y = p.process(xnoise.copy())
        m = m_levels(y); m["n_commits"] = list(p.n_commits); m["n_wraps"] = list(p.n_wraps)
        return m, {"finite": m["finite"], "bounded": m["peak"] < 100.0, "len": len(y) == len(xnoise)}
    run_case("v2_aligned_feedback8", "v2", "aligned, 8 voices, feedback 0.35, 4 s noise", t_aligned_feedback)

    def t_aligned_longrun():
        # 10 s sustained tone: many wraps + re-commits; envelope must stay flat
        xl = sig_tone(220.0, 10.0, sr)
        y = aligned(1.25).process(xl.copy())
        m = m_levels(y); m.update(m_env_mod(y[int(sr * 0.7):], sr, 275.0))
        m["pitch"] = m_pitch(y, sr, 275.0, poc)
        return m, {"finite": m["finite"], "len": len(y) == len(xl),
                   "mod_sync_tight": m["mod_db_sync"] <= 0.75,
                   "pitch_cents_tight": abs(m["pitch"]["cents"]) <= 2.0}
    run_case("v2_aligned_longrun", "v2", "10 s sustained tone, +4 st, aligned", t_aligned_longrun)

    # --- stereo integrity (v2 pattern: fresh engine per channel)
    def t_stereo_v2():
        L = sig_tone(440.0, 1.0, sr); R = np.zeros(len(L))
        X = np.stack([L, R], axis=1)
        Y = v2.process_multichannel(lambda: aligned(1.25), X)
        m = m_levels(Y[:, 0]); m["r_peak"] = float(np.max(np.abs(Y[:, 1])))
        m["contamination_db"] = 20.0 * math.log10(max(m["r_peak"], 1e-12) / max(m["peak"], 1e-12))
        m["channels"] = int(Y.shape[1])
        return m, {"channels": Y.shape[1] == 2, "len": Y.shape[0] == len(L),
                   "r_silent": m["contamination_db"] <= -60.0}
    run_case("v2_stereo_integrity", "v2", "L=tone R=silence -> R stays silent (fresh engines)", t_stereo_v2)

    def t_stereo_differential():
        # uncorrelated L/R content: channels must remain distinct
        rng = np.random.default_rng(23)
        L = 0.3 * rng.standard_normal(int(sr * 0.5)); R = np.roll(L, len(L) // 2)
        X = np.stack([L, R], axis=1)
        Y = v2.process_multichannel(lambda: aligned(1.25), X)
        a = Y[:, 0]; b = Y[:, 1]
        corr = float(np.dot(a, b) / max(1e-12, np.sqrt(np.dot(a, a) * np.dot(b, b))))
        m = {"corr_LR": corr, "rmsL": float(np.sqrt(np.mean(a ** 2))), "rmsR": float(np.sqrt(np.mean(b ** 2)))}
        return m, {"distinct": abs(corr) < 0.9, "levels_similar": abs(m["rmsL"] - m["rmsR"]) < 0.2 * max(m["rmsL"], m["rmsR"])}
    run_case("v2_stereo_differential", "v2", "uncorrelated L/R stay distinct", t_stereo_differential)

    # ============================================ D. CLI end-to-end (v2)
    if not args.skip_cli:
        def t_cli_mono():
            tmp = tempfile.mkdtemp()
            inp = os.path.join(tmp, "in.wav"); outp = os.path.join(tmp, "out.wav")
            off.write_wav(inp, x220, sr)
            r = subprocess.run([sys.executable, os.path.join(args.poc, "octavox_v2.py"),
                                "--in", inp, "--out", outp, "--algo", "delayline_aligned",
                                "--semitones", "4", "--voices", "3"],
                               capture_output=True, text=True, timeout=600)
            ok_run = r.returncode == 0 and os.path.exists(outp)
            m = {"returncode": r.returncode}
            if ok_run:
                y, sr_out = off.read_wav(outp)
                m.update({"out_samples": len(y), "sr_out": sr_out, "peak": float(np.max(np.abs(y)))})
                m["pitch"] = m_pitch(y, sr, 275.0, poc)
            return m, {"ran": ok_run, "duration_preserved": m.get("out_samples") == len(x220),
                       "sr_preserved": m.get("sr_out") == sr,
                       "finite_peak": m.get("peak", 2.0) <= 1.0}
        run_case("cli_v2_mono", "cli", "v2 CLI mono +4 st 3 voices -> WAV", t_cli_mono)

        def t_cli_stereo():
            tmp = tempfile.mkdtemp()
            inp = os.path.join(tmp, "in.wav"); outp = os.path.join(tmp, "out.wav")
            L = sig_tone(440.0, 1.0, sr); R = np.zeros(len(L))
            with wave.open(inp, "wb") as w:
                w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
                q = np.stack([np.round(np.clip(L, -1, 1) * 32767), np.zeros(len(L))], axis=1).astype("<i2")
                w.writeframes(q.reshape(-1).tobytes())
            r = subprocess.run([sys.executable, os.path.join(args.poc, "octavox_v2.py"),
                                "--in", inp, "--out", outp, "--algo", "delayline",
                                "--semitones", "4", "--voices", "1"],
                               capture_output=True, text=True, timeout=600)
            ok_run = r.returncode == 0 and os.path.exists(outp)
            m = {"returncode": r.returncode}
            if ok_run:
                y, sr_out = off.read_wav(outp)   # [n, 2]
                m["channels"] = y.shape[1] if y.ndim == 2 else 1
                m["out_samples"] = y.shape[0]
                m["r_peak"] = float(np.max(np.abs(y[:, 1])))
                m["l_peak"] = float(np.max(np.abs(y[:, 0])))
                m["contamination_db"] = 20.0 * math.log10(max(m["r_peak"], 1e-12) / max(m["l_peak"], 1e-12))
            return m, {"ran": ok_run, "stereo_out": m.get("channels") == 2,
                       "len": m.get("out_samples") == len(L),
                       "r_silent": m.get("contamination_db", 0.0) <= -60.0}
        run_case("cli_v2_stereo", "cli", "v2 CLI stereo delayline: R silence preserved", t_cli_stereo)

        def t_cli_44k():
            tmp = tempfile.mkdtemp()
            inp = os.path.join(tmp, "in.wav"); outp = os.path.join(tmp, "out.wav")
            off.write_wav(inp, sig_tone(220.0, 1.0, sr), sr)
            r = subprocess.run([sys.executable, os.path.join(args.poc, "octavox_v2.py"),
                                "--in", inp, "--out", outp, "--algo", "pv_transient",
                                "--semitones", "-5", "--sr", "44100", "--keep-rate"],
                               capture_output=True, text=True, timeout=600)
            ok_run = r.returncode == 0 and os.path.exists(outp)
            m = {"returncode": r.returncode}
            if ok_run:
                y, sr_out = off.read_wav(outp)
                m["sr_out"] = sr_out; m["out_samples"] = len(y)
                m["pitch"] = m_pitch(y, sr_out, 220.0 * 2 ** (-5 / 12), poc)
            return m, {"ran": ok_run, "keep_rate": m.get("sr_out") == sr,
                       "len": m.get("out_samples") == len(sig_tone(220.0, 1.0, sr)),
                       "pitch": abs(m.get("pitch", {}).get("cents") or 99) <= 3.0}
        run_case("cli_v2_44k_keep_rate", "cli", "v2 CLI 48k in -> 44.1k proc -> keep rate", t_cli_44k)

        def t_cli_24bit():
            tmp = tempfile.mkdtemp()
            inp = os.path.join(tmp, "in24.wav"); outp = os.path.join(tmp, "out.wav")
            x = sig_tone(220.0, 1.2, sr)
            v = np.round(np.clip(x, -1, 1) * 8388608).astype(np.int32)
            b = np.stack([(v & 0xFF).astype(np.uint8), ((v >> 8) & 0xFF).astype(np.uint8),
                          ((v >> 16) & 0xFF).astype(np.uint8)], axis=1).reshape(-1)
            with wave.open(inp, "wb") as w:
                w.setnchannels(1); w.setsampwidth(3); w.setframerate(sr); w.writeframes(b.tobytes())
            r = subprocess.run([sys.executable, os.path.join(args.poc, "octavox_v2.py"),
                                "--in", inp, "--out", outp, "--algo", "pv",
                                "--semitones", "4", "--voices", "1"],
                               capture_output=True, text=True, timeout=600)
            ok_run = r.returncode == 0 and os.path.exists(outp)
            m = {"returncode": r.returncode}
            if ok_run:
                y, _ = off.read_wav(outp)
                m["out_samples"] = len(y); m["peak"] = float(np.max(np.abs(y)))
                m["pitch"] = m_pitch(y, sr, 220.0 * 2 ** (4 / 12), poc)
            return m, {"ran": ok_run, "len": m.get("out_samples") == len(x),
                       "pitch": abs(m.get("pitch", {}).get("cents") or 99) <= 3.0}
        run_case("cli_v2_24bit", "cli", "v2 CLI 24-bit PCM input", t_cli_24bit)

    # ============================================ E. before/after (improvement evidence)
    print("\n--- before/after (improvement evidence; recorded, not gated) ---")
    IMP = {}
    # E1: stereo defect of the REFERENCE CLI (documented defect, measured)
    def measure_ref_cli_stereo():
        tmp = tempfile.mkdtemp()
        inp = os.path.join(tmp, "in.wav"); outp = os.path.join(tmp, "out.wav")
        L = sig_tone(440.0, 1.0, sr); R = np.zeros(len(L))
        with wave.open(inp, "wb") as w:
            w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
            q = np.stack([np.round(np.clip(L, -1, 1) * 32767), np.zeros(len(L))], axis=1).astype("<i2")
            w.writeframes(q.reshape(-1).tobytes())
        r = subprocess.run([sys.executable, os.path.join(args.poc, "octavox_offline.py"),
                            "--in", inp, "--out", outp, "--algo", "delayline",
                            "--semitones", "4", "--voices", "1"],
                           capture_output=True, text=True, timeout=600)
        if r.returncode != 0:
            return {"error": r.stderr[-200:]}
        y, _ = off.read_wav(outp)
        lp = float(np.max(np.abs(y[:, 0]))); rp = float(np.max(np.abs(y[:, 1])))
        return {"r_contamination_db": 20.0 * math.log10(max(rp, 1e-12) / max(lp, 1e-12)),
                "note": "reference CLI shares one stateful engine across channels (round-11 finding)"}
    IMP["stereo_reference_cli"] = measure_ref_cli_stereo()
    print(f"reference CLI stereo contamination: {IMP['stereo_reference_cli'].get('r_contamination_db')} dB "
          f"(defect; fixed by v2 per-channel engines -> measured -234 dB in v2_stereo_integrity)")

    # E2: delayline vs aligned on identical signals
    rows = []
    for r in (2 ** (4 / 12), 2 ** (-5 / 12), 2.0, 0.5):
        y0 = off.DelayLineShifter(sr, r, 1).process(x220.copy())
        y1 = v2.AlignedDelayLineShifter(sr, r, 1).process(x220.copy())
        f_out = 220.0 * r
        p0 = m_pitch(y0, sr, f_out, poc); p1 = m_pitch(y1, sr, f_out, poc)
        e0 = m_env_mod(y0[int(sr * 0.7):], sr, f_out); e1 = m_env_mod(y1[int(sr * 0.7):], sr, f_out)
        rows.append({"ratio": r, "cents_ref": p0["cents"], "cents_aligned": p1["cents"],
                     "mod_db_sync_ref": e0["mod_db_sync"], "mod_db_sync_aligned": e1["mod_db_sync"],
                     "mod_db_1ms_ref": e0["mod_db_1ms"], "mod_db_1ms_aligned": e1["mod_db_1ms"]})
        print(f"r={r:.4f}: cents {p0['cents']:+.3f} -> {p1['cents']:+.3f} | "
              f"mod(sync) {e0['mod_db_sync']:.2f} -> {e1['mod_db_sync']:.2f} dB | "
              f"mod(1ms) {e0['mod_db_1ms']:.2f} -> {e1['mod_db_1ms']:.2f} dB")
    IMP["delayline_vs_aligned"] = rows

    # E3: latency / transient before-after
    lat = {}
    for nm, mk in (("reference", lambda: off.DelayLineShifter(sr, 1.25, 1)),
                   ("aligned", lambda: v2.AlignedDelayLineShifter(sr, 1.25, 1))):
        y = mk().process(imp.copy())
        l, lms = gate_latency(y, sr, 1000, 200)
        yb = mk().process(xburst.copy())
        tr = m_transient(yb, sr, burst_on)
        lat[nm] = {"latency_samples": l, "latency_ms": lms, "attack_ms": tr["attack_ms"],
                   "pre_onset_db": tr["pre_onset_db"]}
        print(f"{nm}: latency {lms:.1f} ms, attack {tr['attack_ms']} ms, pre-onset {tr['pre_onset_db']:.1f} dB")
    IMP["latency_transient"] = lat

    # E4: cost (informational)
    cost = {}
    for nm, mk in (("reference", lambda: off.DelayLineShifter(sr, 1.25, 8)),
                   ("aligned", lambda: v2.AlignedDelayLineShifter(sr, 1.25, 8))):
        xc = 0.3 * np.random.default_rng(7).standard_normal(sr)
        t0 = time.perf_counter(); mk().process(xc.copy()); el = time.perf_counter() - t0
        cost[nm] = {"ms_per_s_8v": el * 1000.0}
        print(f"cost 8 voices: {nm} {el*1000.0:.0f} ms/s audio")
    IMP["cost_8voice"] = cost
    RESULTS["improvement"] = IMP

    # ------------------------------------------------------------------ summary
    n_pass = sum(1 for c in RESULTS["cases"] if c["passed"])
    n_tot = len(RESULTS["cases"])
    RESULTS["summary"] = {"passed": n_pass, "failed": n_tot - n_pass, "total": n_tot,
                          "all_passed": n_pass == n_tot}
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(RESULTS, f, indent=2)
    print(f"\n{'='*70}\n{n_pass}/{n_tot} cases passed; results: {args.out}")
    if FAILURES:
        print("FAILURES:")
        for fl in FAILURES:
            print(f"  - {fl}")
    return 0 if n_pass == n_tot else 1


if __name__ == "__main__":
    sys.exit(main())
