#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCTAVOX demo v2 -- round-11 improvements over the round-6 independent demo
=========================================================================

PROVENANCE
----------
Everything in this file is our own code.  It contains no bytes from the target
plug-in and was not produced by copying target code.  Measurements produced
here describe *our* implementations only and are never presented as
measurements of the target.  The round-6 reference implementation
(`octavox_poc.py` / `octavox_offline.py`) is intentionally left untouched and
remains the baseline for comparison; this module is the separate v2 design
required by the round-11 mandate ("keep it separate from the reference
version").

What changed relative to the reference, and why (round-11 measured motives)
---------------------------------------------------------------------------
1. `AlignedDelayLineShifter` ("delayline_aligned") -- **period-aligned grain
   splicing**.  The reference two-tap design crossfades between taps spaced
   exactly G samples apart; for a sinusoid the two taps then have a fixed
   phase difference w0*G, which (a) phase-modulates the output carrier by a
   few cents (measured: +4.80 c at r=1.25, -4.27 c at r=0.80) and (b) beats
   the envelope whenever w0*G is near an odd multiple of pi (measured
   crossfade modulation: 2.3 dB with a period-synchronous envelope metric on
   a 220 Hz tone; 15.7/16.8 dB with the shipped 1 ms metric on the burst
   test).  The v2 design instead commits the tap spacing as
   S = k*T_hat (k = round(G/T_hat), T_hat = detected input period), clamped
   to [G/2, 1.5G]: the two taps become phase-aligned for periodic input, the
   mix coefficient stops rotating, and both artifacts disappear (measured:
   0.00 dB modulation, 0.01 cents pitch error with the detector, 0.000 cents
   with the true period).  For non-periodic input (no reliable f0) S falls
   back to G, i.e. exactly the reference behaviour.  Known limitations,
   documented in OCTAVOX_ROUND11_FINDINGS.md: at unison (r = 1) the grain
   phase never wraps, so S never commits and the reference two-tap comb
   remains; very small |1-r| makes wraps (and therefore commits) rare.
2. **Per-channel engines for stateful algorithms.**  The reference CLI
   processed a stereo file's channels through ONE stateful delay-line engine,
   so the right channel received the left channel's delayed ring content
   (measured: right-channel peak only -0.4 dB below the left peak; the docs'
   "channels processed independently" claim was wrong for the delay line).
   The v2 CLI builds a fresh engine per channel for every algorithm.  The
   phase vocoders were already stateless and are unaffected.

Design of the aligned splice (continuity math)
----------------------------------------------
The grain phase `frac` sweeps at (1-r)/S; tap A reads at d = base + frac*S,
tap B at d + S; the complementary raised-cosine weight w(frac) hands over
A<->B at the wraps.  When S is committed at a wrap (S_old -> S_new) the read
point must jump by the spacing of the *handing-off* tap so the output stays
continuous:
  r > 1 (frac crossed 0):  frac_new = (frac_old + 1) * S_old / S_new
      (d_new = d_old + S_old, so A_new == B_old exactly)
  r < 1 (frac crossed 1):  frac_new = (frac_old - 1) * S_old / S_new
      (d_new = d_old - S_new, so B_new == A_old exactly)
The resulting frac is re-normalised into [0,1) with +-1 wraps; because
k = round(G/T_hat) keeps every first commit within half an input period of G
(S_new/S_old in ~[0.97, 1.03]) and later re-commits are hysteresis-gated to
0.5 %, the normalisation wraps also preserve continuity (the residual
discontinuity is the crossfade weight's 2 % bleed, ~-32 dB, only on large
pitch changes).  S is only ever committed AT a wrap, never between wraps, so
d is continuous everywhere else by construction.

Run
---
  python octavox_v2.py --in example_input.wav --out out.wav --algo delayline_aligned --semitones 4 --voices 3
  python octavox_v2.py --in stereo.wav --out out.wav --algo delayline --semitones -5   (stereo-safe)

Author: analysis workspace (round 11).  Python 3.11+ / numpy.  No SciPy.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from collections import deque

import numpy as np

from octavox_poc import BASE_DELAY, GRAIN, DelayLine, PitchDetector
from octavox_offline import (
    DelayLineShifter,
    PhaseVocoderShifter,
    _SPREAD_SEMITONES,
    read_wav,
    resample_linear,
    write_wav,
)

__all__ = ["AlignedDelayLineShifter", "process_multichannel", "build_v2", "main"]


# --------------------------------------------------------------------------------------
# The aligned delay-line shifter (our v2 design)
# --------------------------------------------------------------------------------------


class AlignedDelayLineShifter:
    """Variable-rate two-tap delay shifter with period-aligned grain splicing.

    Same ring, interpolator, voice layout, spread table and complementary
    raised-cosine crossfade as the reference `DelayLineShifter`; the only
    behavioural change is the tap spacing S (committed at wraps from the
    per-block pitch estimate, see the module docstring).
    """

    name = "delayline_aligned"

    def __init__(
        self,
        sr: int,
        ratio: float = 1.0,
        voices: int = 1,
        interp: str = "lagrange6",
        grain: float = GRAIN,
        base_delay: float = BASE_DELAY,
        spread: bool = True,
        feedback: float = 0.0,
        fmin: float = 55.0,
        fmax: float = 1200.0,
        s_hysteresis: float = 0.005,
        s_smoothing: int = 8,
    ):
        self.sr = sr
        self.interp = interp
        self.grain = float(grain)
        self.base_delay = float(base_delay)
        self.feedback = float(feedback)
        self.fmin = fmin
        self.fmax = fmax
        self.s_hysteresis = s_hysteresis
        self.s_smoothing = s_smoothing
        st = 12.0 * math.log2(ratio) if ratio > 0 else 0.0
        self.semitones = [st + (_SPREAD_SEMITONES[i] if (spread and i < len(_SPREAD_SEMITONES)) else 0.0)
                          for i in range(voices)]
        self.base_delays = [base_delay + 32.0 * i for i in range(voices)]
        self.ratio_t = [2.0 ** (s / 12.0) for s in self.semitones]
        self.ratio = list(self.ratio_t)
        self.glide_mult = 0.9995
        self.latency = int(base_delay + grain * 0.5)  # nominal (tap range base..base+2S)
        self.dl = DelayLine(1 << 18)
        self.det = PitchDetector(sr)
        self.frac = [0.5] * voices
        self.S = [float(grain)] * voices
        self._f0_hist: deque = deque(maxlen=max(1, s_smoothing))
        self._S_target = float(grain)
        self._have_estimate = False
        self.fb_state = 0.0
        self.n_commits = [0] * voices
        self.n_wraps = [0] * voices

    # -- spacing management -------------------------------------------------------
    def _update_spacing_target(self, f0: float) -> None:
        """Called once per block with the detector estimate (0.0 = none)."""
        if f0 and self.fmin <= f0 <= self.fmax:
            self._f0_hist.append(self.sr / f0)
            self._have_estimate = True
        if not self._f0_hist:
            self._S_target = float(self.grain)
            return
        T_hat = float(np.median(self._f0_hist))
        k = max(1, int(round(self.grain / T_hat)))
        S = k * T_hat
        self._S_target = min(max(S, 0.5 * self.grain), 1.5 * self.grain)

    def _commit(self, i: int) -> None:
        """Commit the spacing target for voice i at a wrap (hysteresis-gated)."""
        if not self._have_estimate:
            return  # keep reference behaviour until a period is known
        old = self.S[i]
        if abs(self._S_target / old - 1.0) > self.s_hysteresis:
            self.S[i] = self._S_target
            self.n_commits[i] += 1

    # -- processing ----------------------------------------------------------------
    def process(self, x: np.ndarray) -> np.ndarray:
        x = np.asarray(x, dtype=np.float64)
        n = len(x)
        out = np.zeros(n)
        nv = len(self.frac)
        block = 64
        for i0 in range(0, n, block):
            xb = x[i0 : i0 + block]
            self.det.push_block(xb)
            self._update_spacing_target(self.det.estimate())
            for k in range(len(xb)):
                xin = xb[k] + self.feedback * self.fb_state
                self.dl.push(xin)
                acc = 0.0
                for i in range(nv):
                    r = self.ratio[i]
                    # geometric glide toward the target ratio (as the reference)
                    self.ratio[i] = self.ratio_t[i] + (r - self.ratio_t[i]) * self.glide_mult
                    S = self.S[i]
                    frac = self.frac[i] + (1.0 - r) / S
                    if r > 1.0:
                        if frac < 0.0:
                            S_old = S
                            self._commit(i)
                            S = self.S[i]
                            frac = (frac + 1.0) * S_old / S
                            while frac >= 1.0:
                                frac -= 1.0
                            self.n_wraps[i] += 1
                    else:
                        if frac >= 1.0:
                            S_old = S
                            self._commit(i)
                            S = self.S[i]
                            frac = (frac - 1.0) * S_old / S
                            while frac < 0.0:
                                frac += 1.0
                            self.n_wraps[i] += 1
                    self.frac[i] = frac
                    d = self.base_delays[i] + frac * S
                    A = self.dl.read(d, self.interp)
                    B = self.dl.read(d + S, self.interp)
                    w = 0.5 - 0.5 * math.cos(math.pi * frac)
                    acc += w * A + (1.0 - w) * B
                y = acc / nv
                out[i0 + k] = y
                self.fb_state = y if self.feedback else 0.0
        return out


# --------------------------------------------------------------------------------------
# Stereo-safe multichannel processing (the round-11 defect fix)
# --------------------------------------------------------------------------------------


def process_multichannel(make_engine, x: np.ndarray) -> np.ndarray:
    """Process x of shape [n, ch] with ONE FRESH ENGINE PER CHANNEL.

    `make_engine()` is called once per channel and must return a new, reset
    engine.  This is the fix for the round-11 finding: the reference CLI
    pushed all channels through one stateful delay-line engine, so the right
    channel read the left channel's ring content (measured contamination at
    -0.4 dB relative peak).  Stateless algorithms (the vocoders) are
    unaffected either way, but we use the same pattern uniformly.
    """
    if x.ndim == 1:
        return make_engine().process(np.asarray(x, dtype=np.float64))
    chans = [make_engine().process(x[:, c].copy()) for c in range(x.shape[1])]
    return np.stack(chans, axis=1)


def build_v2(algo: str, sr: int, ratio: float, voices: int, interp: str = "lagrange6",
             grain: float = GRAIN, n_fft: int = 2048, spread: bool = True):
    """Engine factory for the v2 CLI (one call = one fresh engine)."""
    if algo == "delayline":
        return lambda: DelayLineShifter(sr, ratio, voices, interp=interp, grain=grain, spread=spread)
    if algo == "delayline_aligned":
        return lambda: AlignedDelayLineShifter(sr, ratio, voices, interp=interp, grain=grain, spread=spread)
    if algo == "pv":
        return lambda: PhaseVocoderShifter(sr, ratio, voices, n_fft=n_fft, transient=False, spread=spread)
    if algo == "pv_transient":
        return lambda: PhaseVocoderShifter(sr, ratio, voices, n_fft=n_fft, transient=True, spread=spread)
    raise ValueError(f"unknown algo {algo!r}")


ALGOS = ("delayline", "pv", "pv_transient", "delayline_aligned")


# --------------------------------------------------------------------------------------
# v2 CLI (mirrors octavox_offline.py, with per-channel engines + the aligned algo)
# --------------------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser(description="Octavox offline demo v2 (our implementation; round 11)")
    ap.add_argument("--in", dest="inp", help="input WAV (16/24/32-bit PCM)")
    ap.add_argument("--out", dest="out", help="output WAV (16-bit PCM)")
    ap.add_argument("--sr", type=int, default=48000, help="internal processing rate (default 48000)")
    ap.add_argument("--keep-rate", action="store_true",
                    help="write the output at the input file's rate (resample back)")
    ap.add_argument("--ratio", type=float, default=1.0, help="pitch ratio (1.0 = no shift)")
    ap.add_argument("--semitones", type=float, default=None, help="pitch shift in semitones (overrides --ratio)")
    ap.add_argument("--voices", type=int, default=1, help="number of voices (default 1)")
    ap.add_argument("--interp", default="lagrange6", choices=["lagrange6", "lagrange6_q", "linear"])
    ap.add_argument("--algo", default="delayline_aligned", choices=list(ALGOS))
    ap.add_argument("--grain", type=float, default=GRAIN, help="delay-line grain length in samples")
    ap.add_argument("--n-fft", type=int, default=2048, help="vocoder FFT size")
    ap.add_argument("--no-spread", action="store_true", help="all voices at the same ratio")
    ap.add_argument("--json", dest="json_out", default=None, help="write a metrics JSON here")
    args = ap.parse_args(argv)

    if not args.inp or not args.out:
        ap.error("--in and --out are required")

    ratio = args.ratio if args.semitones is None else 2.0 ** (args.semitones / 12.0)
    x, sr_in = read_wav(args.inp)
    if x.ndim == 1:
        x = x[:, None]
    x_proc = resample_linear(x, sr_in, args.sr) if sr_in != args.sr else x
    make = build_v2(args.algo, args.sr, ratio, args.voices, args.interp, args.grain,
                    args.n_fft, spread=not args.no_spread)
    t0 = time.perf_counter()
    y = process_multichannel(make, x_proc)  # fresh engine per channel (round-11 fix)
    el = time.perf_counter() - t0
    sr_out = args.sr
    if args.keep_rate and sr_in != args.sr:
        y = resample_linear(y, args.sr, sr_in)
        sr_out = sr_in
    write_wav(args.out, y, sr_out)
    info = {
        "demo": "v2",
        "algo": args.algo,
        "ratio": ratio,
        "voices": args.voices,
        "interp": args.interp,
        "sr_in": sr_in,
        "sr_out": sr_out,
        "channels": int(y.shape[1]),
        "in_samples": int(x.shape[0]),
        "out_samples": int(y.shape[0]),
        "duration_s": float(y.shape[0] / sr_out),
        "cpu_s": el,
        "x_realtime": float((y.shape[0] / sr_out) / max(1e-9, el)),
        "peak": float(np.max(np.abs(y))),
        "per_channel_engines": True,
    }
    print(json.dumps(info, indent=2))
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(info, f, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
