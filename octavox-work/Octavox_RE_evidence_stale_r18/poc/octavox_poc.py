#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCTAVOX PoC  --  INDEPENDENT DSP demonstration (mandate section 9)
=================================================================

IMPORTANT / PROVENANCE
----------------------
This file is *our own* implementation.  It is NOT Eventide code, contains no bytes
from the target, and was not derived by copying the target's instruction stream.
It is an independent realisation of the *architecture* reconstructed in documents
1-6 (a variable-rate delay-line pitch shifter with per-voice gains, a crossfaded
grain re-arm, a geometric interval glide and a diatonic interval chooser).

Every measurement produced by this file is a measurement of THIS implementation.
It must never be presented as a measurement of the target plugin.  The two are kept
in separate folders on purpose (Evdence/ = target evidence, Evdence/poc/ = ours).

DSP structure (our design)
--------------------------
    input -> ring delay line (age-indexed)
          -> per voice:
                 fractional-delay read at  d(t)  with 6-point Lagrange interpolation
                 second read at d(t) + G  (grain length)
                 crossfade weight w(t) in [0,1] selects between the two taps
                 d(t) ramps at (1 - ratio) samples/sample and is re-armed with a
                 crossfaded `grain jump` when it leaves [base, base+G]
                 geometric glide of `ratio` toward `ratio_target`
                 per-voice linear gain ramp
          -> wet mix (sum of voices scaled)
    dry/wet -> out

Author: analysis workspace.  Python 3.11 / numpy.
"""

from __future__ import annotations

import json
import math
import time
from dataclasses import dataclass, field
from typing import Callable, Optional

import numpy as np

# --------------------------------------------------------------------------------------
# Interpolators
# --------------------------------------------------------------------------------------


_LAGRANGE_NODES = np.array([-2.0, -1.0, 0.0, 1.0, 2.0, 3.0])


def lagrange6(mu: float) -> np.ndarray:
    """6-point Lagrange weights for x = mu with nodes at -2,-1,0,1,2,3 (our own derivation)."""
    xs = _LAGRANGE_NODES
    w = np.ones(6)
    for i in range(6):
        for j in range(6):
            if i != j:
                w[i] *= (mu - xs[j]) / (xs[i] - xs[j])
    return w


_TABLES: dict[int, np.ndarray] = {}


def lagrange_table(n: int) -> np.ndarray:
    """Precomputed (n, 6) table of 6-point Lagrange weights, one row per phase."""
    t = _TABLES.get(n)
    if t is None:
        mus = (np.arange(n) + 0.5) / n
        t = np.array([lagrange6(float(m)) for m in mus])
        _TABLES[n] = t
    return t


class DelayLine:
    """Power-of-two ring buffer; `read(delay)` where delay >= 0 is samples ago."""

    def __init__(self, n: int = 1 << 18):
        n = 1 << int(math.ceil(math.log2(max(16, n))))
        self.n = n
        self.mask = n - 1
        self.buf = np.zeros(n, dtype=np.float64)
        self.w = 0  # newest sample index

    def push(self, x: float) -> None:
        self.buf[self.w] = x
        self.w = (self.w - 1) & self.mask  # index decreases with age (as reconstructed)

    def idx(self, delay: float) -> int:
        return (self.w + int(math.floor(delay))) & self.mask

    def read(self, delay: float, interp: str = "lagrange6") -> float:
        if delay < 0.0:
            delay = 0.0
        d0 = int(math.floor(delay))
        mu = delay - d0
        b, m, w = self.buf, self.mask, self.w
        i = (w + d0) & m
        if interp == "linear":
            a = b[i]
            c = b[(i + 1) & m]
            return a + (c - a) * mu
        if interp == "lagrange3":  # 3-point quadratic (never used by the harness)
            y0 = b[(i + 1) & m]
            y1 = b[i]
            y2 = b[(i + m - 1) & m]
            return y1 + 0.5 * mu * (y2 - y0) + 0.5 * mu * mu * (y2 - 2 * y1 + y0)
        # 6-point Lagrange, table driven: 2048 phases, or 256 phases for the quantised variant
        n_ph = 256 if interp == "lagrange6_q" else 2048
        wv = lagrange_table(n_ph)[int(mu * n_ph)]
        s = 0.0
        for k in range(6):
            s += wv[k] * b[(w + d0 + k - 2) & m]
        return s


_LAGRANGE_CACHE: dict[float, np.ndarray] = {}  # kept for API stability; unused


# --------------------------------------------------------------------------------------
# Pitch detector (our own): cumulative-mean-normalised autocorrelation (YIN-style)
# --------------------------------------------------------------------------------------


class PitchDetector:
    def __init__(self, sr: int, win: int = 2048, fmin: float = 60.0, fmax: float = 1200.0):
        self.sr, self.win, self.fmin, self.fmax = sr, win, fmin, fmax
        self.buf = np.zeros(win, dtype=np.float64)
        self.last_f0 = 0.0

    def push_block(self, x: np.ndarray) -> None:
        if len(x) >= self.win:
            self.buf[:] = x[-self.win :]
        else:
            self.buf = np.roll(self.buf, -len(x))
            self.buf[-len(x) :] = x

    def estimate(self) -> float:
        x = self.buf
        if np.max(np.abs(x)) < 1e-6:
            return 0.0
        n = len(x)
        tau_min = max(2, int(self.sr / self.fmax))
        tau_max = min(n - 2, int(self.sr / self.fmin))
        if tau_max <= tau_min:
            return 0.0
        # autocorrelation near the end of the window (causal-ish)
        seg = x[n // 2 :]
        m = len(seg) - tau_max
        if m < 32:
            return 0.0
        d = np.empty(tau_max - tau_min)
        for i, tau in enumerate(range(tau_min, tau_max)):
            a = seg[:m]
            b = seg[tau : tau + m]
            d[i] = np.sum((a - b) ** 2)
        # cumulative mean normalisation
        cmnd = np.empty_like(d)
        run = 0.0
        for i in range(len(d)):
            run += d[i]
            cmnd[i] = d[i] * (i + 1) / run if run > 0 else 1.0
        # first minimum below threshold
        idx = None
        thr = 0.20
        for i in range(1, len(cmnd) - 1):
            if cmnd[i] < thr and cmnd[i] <= cmnd[i - 1] and cmnd[i] <= cmnd[i + 1]:
                idx = i
                break
        if idx is None:
            idx = int(np.argmin(cmnd))
        tau = tau_min + idx
        # parabolic refinement
        if 0 < idx < len(cmnd) - 1:
            y0, y1, y2 = cmnd[idx - 1], cmnd[idx], cmnd[idx + 1]
            den = (y0 - 2 * y1 + y2)
            if abs(den) > 1e-15:
                tau = tau + 0.5 * (y0 - y2) / den
        f0 = self.sr / tau
        if not (self.fmin <= f0 <= self.fmax):
            f0 = 0.0
        self.last_f0 = f0
        return f0


# --------------------------------------------------------------------------------------
# The shifter
# --------------------------------------------------------------------------------------


@dataclass
class VoiceCfg:
    semitones: float = 0.0           # target interval (can be overridden by diatonic mode)
    base_delay: float = 256.0        # grain base delay, samples
    grain: float = 4096.0            # grain length G, samples
    gain: float = 1.0
    gain_ramp: float = 0.25          # per-sample linear ramp limit
    glide_mult: float = 0.9995       # geometric glide multiplier per sample
    enabled: bool = True


class OctavoxPoC:
    """Our multi-voice, diatonic, variable-rate delay pitch shifter."""

    def __init__(
        self,
        sr: int = 48000,
        nvoices: int = 8,
        voices: Optional[list[VoiceCfg]] = None,
        interp: str = "lagrange6",
        diatonic: bool = False,
        scale: tuple[int, ...] = (0, 2, 4, 5, 7, 9, 11),
        mix: float = 1.0,
        feedback: float = 0.0,
    ):
        self.sr = sr
        self.nvoices = nvoices
        self.interp = interp
        self.diatonic = diatonic
        self.scale = scale
        self.mix = mix
        self.feedback = feedback
        self.dl = DelayLine(1 << 18)
        self.det = PitchDetector(sr)
        if voices is None:
            voices = [VoiceCfg(base_delay=256.0 + 32.0 * i) for i in range(nvoices)]
        self.v = voices[:nvoices]
        n = self.nvoices
        self.delay = np.array([v.base_delay + v.grain * 0.5 for v in self.v])
        self.ratio = np.array([2.0 ** (v.semitones / 12.0) for v in self.v])
        self.ratio_t = self.ratio.copy()
        self.gain = np.array([v.gain if v.enabled else 0.0 for v in self.v])
        self.gain_t = self.gain.copy()
        self.applied_st = np.array([v.semitones for v in self.v])  # last interval actually used
        self.xf = np.zeros(n)      # crossfade weight 0..1
        self.xf_dir = np.zeros(n)  # +1 rising, -1 falling
        self.fb_state = 0.0
        self._norm = 1.0
        self._reset_phase()

    # -- helpers -------------------------------------------------------------------
    def _reset_phase(self) -> None:
        # start mid-grain (w == 0.5) so no immediate re-arm
        self.frac = np.full(self.nvoices, 0.5)

    def set_voice_semitones(self, i: int, st: float) -> None:
        self.v[i].semitones = st
        self.ratio_t[i] = 2.0 ** (st / 12.0)

    def set_voice_gain(self, i: int, g: float) -> None:
        self.gain_t[i] = g

    def diatonic_interval(self, f0: float, semis: float) -> float:
        """Snap a nominal interval to the nearest scale degree (our own rule).

        Ties (a target exactly halfway between two degrees) are resolved **downwards** so
        the result is deterministic; without that, a detector estimate a few 1e-4 semitones
        away from nominal flips the chosen degree and the measurement becomes meaningless.
        """
        if f0 <= 0.0:
            return semis
        midi = 69.0 + 12.0 * math.log2(f0 / 440.0)
        target = midi + semis
        best, bd = None, 1e9
        for octv in range(-2, 3):
            for s in self.scale:
                cand = 12.0 * (round((target - s) / 12.0) + octv) + s
                d = abs(cand - target)
                if d < bd - 1e-12 or (abs(d - bd) <= 1e-12 and cand < best):
                    bd, best = d, cand
        return float(best - midi)

    # -- process -------------------------------------------------------------------
    def process(self, x: np.ndarray) -> np.ndarray:
        n = len(x)
        out = np.zeros(n)
        v = self.v
        # one detection per block (control rate), using this block as the newest input
        self.det.push_block(np.asarray(x))
        f0 = self.det.estimate()
        if self.diatonic and f0 > 0.0:
            for i in range(self.nvoices):
                st = self.diatonic_interval(f0, v[i].semitones)
                self.ratio_t[i] = 2.0 ** (st / 12.0)
                self.applied_st[i] = st
        # voice-count normalisation: the wet bus is the *mean* of the active voices, so the
        # feedback loop gain equals `self.feedback` regardless of how many voices are on.
        n_on = 0
        for i in range(self.nvoices):
            if self.gain_t[i] > 0.0:
                n_on += 1
        self._norm = 1.0 / n_on if n_on else 0.0
        for k in range(n):
            xin = x[k] + self.feedback * self.fb_state
            self.dl.push(xin)
            acc = 0.0
            for i in range(self.nvoices):
                # gain ramp
                g = self.gain_t[i] - self.gain[i]
                if g > v[i].gain_ramp:
                    g = v[i].gain_ramp
                elif g < -v[i].gain_ramp:
                    g = -v[i].gain_ramp
                self.gain[i] += g
                if abs(self.gain[i]) < 1e-9 and self.gain_t[i] == 0.0:
                    continue
                # ratio glide (geometric)
                self.ratio[i] = self.ratio_t[i] + (self.ratio[i] - self.ratio_t[i]) * v[i].glide_mult
                r = self.ratio[i]
                # grain phase ramp: the delay sweeps at (1-r) samples/sample
                self.frac[i] += (1.0 - r) / v[i].grain
                if self.frac[i] >= 1.0:
                    self.frac[i] -= 1.0
                elif self.frac[i] < 0.0:
                    self.frac[i] += 1.0
                d = v[i].base_delay + self.frac[i] * v[i].grain
                a = self.dl.read(d, self.interp)
                b = self.dl.read(d + v[i].grain, self.interp)
                # complementary raised-cosine crossfade between the two taps.
                # w(frac) + (1-w(frac)) = 1 ; w(0)=0 -> tap B (== tap A one grain later),
                # w(1)=1 -> tap A.  This makes the tap hand-off continuous.
                w = 0.5 - 0.5 * math.cos(math.pi * self.frac[i])
                acc += self.gain[i] * (a * w + b * (1.0 - w))
            out[k] = self.mix * acc * self._norm
            self.fb_state = out[k] if abs(self.feedback) > 0 else 0.0
        return out

    def process_blocks(self, x: np.ndarray, block: int = 64) -> np.ndarray:
        y = np.empty_like(x)
        for i in range(0, len(x), block):
            y[i : i + block] = self.process(x[i : i + block])
        return y


# --------------------------------------------------------------------------------------
# Measurement harness
# --------------------------------------------------------------------------------------


def _pitch_fft(y: np.ndarray, sr: int, fmin: float = 50.0, fmax: float = 4000.0) -> float:
    """Spectral peak with parabolic refinement = the *instantaneous carrier* of the output.

    The two-tap crossfade phase-modulates the output (see `_predicted_shift_hz`), so this
    peak sits at the modulated carrier, not at the grain-average.
    """
    n = len(y)
    if n < 64:
        return 0.0
    Y = np.abs(np.fft.rfft(y * np.hanning(n)))
    f = np.fft.rfftfreq(n, 1.0 / sr)
    sel = (f >= fmin) & (f <= fmax)
    Ys, fs = Y[sel], f[sel]
    if len(Ys) == 0:
        return 0.0
    j = int(np.argmax(Ys))
    if 0 < j < len(Ys) - 1:
        a, b, c = Ys[j - 1], Ys[j], Ys[j + 1]
        den = a - 2 * b + c
        if abs(den) > 1e-18:
            return float(fs[j] + 0.5 * (a - c) / den * (fs[1] - fs[0]))
    return float(fs[j])


def _pitch_yin(y: np.ndarray, sr: int, fmin: float = 50.0, fmax: float = 4000.0,
               thr: float = 0.15) -> float:
    """Cumulative-mean-normalised squared difference (YIN) fundamental estimate.

    Robust to the amplitude modulation the crossfade introduces, so this is the
    *grain-average* period.
    """
    x = np.asarray(y, dtype=float)
    n = len(x)
    if n < 256:
        return 0.0
    x = x - x.mean()
    if np.max(np.abs(x)) < 1e-9:
        return 0.0
    tmin = max(2, int(sr / fmax))
    tmax = min(n // 2, int(sr / fmin))
    if tmax <= tmin:
        return 0.0
    d = np.zeros(tmax + 2)
    for tau in range(1, tmax + 2):
        diff = x[: n - tau] - x[tau:]
        d[tau] = float(np.dot(diff, diff))
    dp = np.ones(tmax + 2)
    run = 0.0
    for tau in range(2, tmax + 2):
        run += d[tau]
        dp[tau] = d[tau] * (tau - 1) / run if run > 0 else 1.0
    best = None
    tau = tmin
    while tau <= tmax:
        if dp[tau] < thr:
            while tau + 1 <= tmax and dp[tau + 1] < dp[tau]:
                tau += 1
            best = tau
            break
        tau += 1
    if best is None:
        best = tmin + int(np.argmin(dp[tmin : tmax + 1]))
    if 1 < best < tmax:
        a, b, c = dp[best - 1], dp[best], dp[best + 1]
        den = a - 2 * b + c
        if abs(den) > 1e-15:
            best = best + 0.5 * (a - c) / den
    if best <= 0:
        return 0.0
    f0 = sr / float(best)
    return float(f0) if fmin <= f0 <= fmax else 0.0


def _dominant_freq(y: np.ndarray, sr: int, fmin: float = 50.0, fmax: float = 4000.0) -> float:
    """Back-compat alias used by the transient/discontinuity sections."""
    return _pitch_yin(y, sr, fmin, fmax)


def _predicted_shift_hz(f_in: float, ratio: float, grain: float, sr: int) -> float:
    """Closed-form output frequency of the two-tap complementary-crossfade shifter.

    With  A = x[n-d],  B = x[n-d-G],  d = d0 + G*phi,  w = (1-cos(pi*phi))/2  the mix

        y = w*A + (1-w)*B = P(phi)*sin(u) + Q(phi)*cos(u),
        u                = w0*n - w0*d0 - w0*G*phi,
        P(phi)           = cos(w0*G) + (1-cos(pi*phi))*(1-cos(w0*G))/2,
        Q(phi)           = -(1+cos(pi*phi))*sin(w0*G)/2,

    is exact.  sin/cos share the frequency w0*ratio, but the mix coefficient rotates the
    phase by Phi(phi)=atan2(Q,P), so the mean instantaneous frequency is

        f_eff = ratio*f_in + (Phi(1)-Phi(0)) / T * sr/(2*pi),   T = G/|1-ratio|.

    Phi is unwrapped numerically (P crosses zero mid-grain for most G, adding a pi step),
    so this is the *model* the measurement is compared against.  It matches to a few cents
    where the artefact is large and both go to zero as ~1/G.
    """
    w0 = 2.0 * math.pi * f_in / sr
    phis = np.linspace(0.0, 1.0, 8193)
    cpg = math.cos(w0 * grain)
    spg = math.sin(w0 * grain)
    P = cpg + (1.0 - np.cos(math.pi * phis)) * (1.0 - cpg) / 2.0
    Q = -(1.0 + np.cos(math.pi * phis)) * spg / 2.0
    dphi = float(np.unwrap(np.arctan2(Q, P))[-1] - np.unwrap(np.arctan2(Q, P))[0])
    return ratio * f_in + dphi * (1.0 - ratio) / grain * sr / (2.0 * math.pi)




def _thd_energy(y: np.ndarray, sr: int, f0: float, guard_hz: float = 12.0) -> float:
    """Fraction of energy outside +-guard of f0 and its first 8 harmonics."""
    n = len(y)
    Y = np.abs(np.fft.rfft(y * np.hanning(n))) ** 2
    f = np.fft.rfftfreq(n, 1.0 / sr)
    keep = np.zeros(len(f), dtype=bool)
    for h in range(1, 9):
        keep |= np.abs(f - h * f0) < guard_hz
    tot = Y.sum()
    return float(1.0 - Y[keep].sum() / tot) if tot > 0 else 0.0


BASE_DELAY = 256.0
GRAIN = 4096.0


def measure(diatonic: bool = False, interp: str = "lagrange6", nvoices: int = 8) -> dict:
    sr = 48000
    res: dict = {"interp": interp, "nvoices": nvoices, "diatonic": diatonic, "sr": sr,
                 "grain": GRAIN, "base_delay": BASE_DELAY}
    semis = [0, 3, 4, 5, 7, -3, -4, -5][:nvoices]

    # ---- 1. pitch accuracy on a steady sine --------------------------------------
    f_in = 220.0
    dur = 1.2
    t = np.arange(int(sr * dur)) / sr
    x = 0.5 * np.sin(2 * np.pi * f_in * t)
    rows = []
    y0 = None
    for i, s in enumerate(semis):
        v1 = [VoiceCfg(semitones=(s if j == i else 0.0),
                       base_delay=BASE_DELAY + 32.0 * j,
                       grain=GRAIN,
                       enabled=(j == i)) for j in range(nvoices)]
        p1 = OctavoxPoC(sr=sr, nvoices=nvoices, voices=v1, interp=interp, diatonic=diatonic)
        y1 = p1.process_blocks(x, 64)[int(sr * 0.8) :]
        if i == 0:
            y0 = y1
        # expected must use the interval the processor actually applied (in diatonic mode
        # the snapped degree depends on the detector's estimate, not on the nominal value)
        st = float(p1.applied_st[i])
        exp = f_in * 2.0 ** (st / 12.0)
        f_yin = _pitch_yin(y1, sr) if np.max(np.abs(y1)) > 1e-6 else 0.0
        f_fft = _pitch_fft(y1, sr) if np.max(np.abs(y1)) > 1e-6 else 0.0
        f_pred = _predicted_shift_hz(f_in, 2.0 ** (st / 12.0), GRAIN, sr)
        rows.append({
            "voice": i, "nominal_semitones": s, "semitones": st, "expected_hz": exp,
            "measured_hz": f_yin, "measured_fft_hz": f_fft, "predicted_hz": f_pred,
            "cents_err": 1200.0 * math.log2(f_yin / exp) if f_yin > 0 else float("nan"),
            "cents_err_fft": 1200.0 * math.log2(f_fft / exp) if f_fft > 0 else float("nan"),
            "cents_err_predicted": 1200.0 * math.log2(f_pred / exp),
            "thd_energy": _thd_energy(y1, sr, f_yin) if f_yin > 0 else float("nan"),
        })
    res["pitch"] = rows
    res["pitch_max_cents_err"] = max(abs(r["cents_err"]) for r in rows if r["cents_err"] == r["cents_err"])
    # unity-ratio behaviour: with r == 1 the crossfade weight is frozen at 0.5 and the two
    # taps (G apart) are both read, so the "0 semitone" voice is a two-tap comb, not a copy.
    # Analytic gain for a sinusoid: |cos(w0*G/2)|.
    if y0 is not None:
        seg = slice(int(sr * 0.8), None)
        rms_in = float(np.sqrt(np.mean(x[seg] ** 2)))
        rms_out = float(np.sqrt(np.mean(y0 ** 2)))
        res["unity_ratio_gain_db"] = (20.0 * math.log10(rms_out / rms_in)) if rms_in > 0 else None
        res["unity_ratio_gain_db_analytic"] = 20.0 * math.log10(
            abs(math.cos(math.pi * f_in * GRAIN / sr)) or 1e-12)

    # ---- 1b. grain-length trade-off (accuracy vs latency), primary config only ----
    if interp == "lagrange6" and not diatonic:
        sweep = []
        for G in (512.0, 1024.0, 2048.0, 4096.0, 8192.0, 16384.0):
            for st in (3, -4):
                vg = [VoiceCfg(semitones=st, base_delay=BASE_DELAY, grain=G)]
                pg = OctavoxPoC(sr=sr, nvoices=1, voices=vg, interp=interp)
                yg = pg.process_blocks(x, 64)[int(sr * 0.8) :]
                fe = _pitch_fft(yg, sr)
                fp = _predicted_shift_hz(f_in, 2.0 ** (st / 12.0), G, sr)
                sweep.append({
                    "grain": G, "semitones": st,
                    "latency_ms": (BASE_DELAY + G * 0.5) / sr * 1000.0,
                    "measured_fft_hz": fe,
                    "cents_err_fft": 1200.0 * math.log2(fe / (f_in * 2.0 ** (st / 12.0))) if fe > 0 else float("nan"),
                    "predicted_hz": fp,
                    "cents_err_predicted": 1200.0 * math.log2(fp / (f_in * 2.0 ** (st / 12.0))),
                    "thd_energy": _thd_energy(yg, sr, fe) if fe > 0 else float("nan"),
                })
        res["grain_sweep"] = sweep

    # ---- 2. latency (impulse) ---------------------------------------------------
    imp = np.zeros(sr)
    imp[1000] = 1.0
    pl = OctavoxPoC(sr=sr, nvoices=1, voices=[VoiceCfg(semitones=0, base_delay=BASE_DELAY, grain=GRAIN)],
                    interp=interp)
    yi = pl.process_blocks(imp, 64)
    nz = np.nonzero(np.abs(yi) > 1e-4)[0]
    res["latency_samples"] = int(nz[0] - 1000) if len(nz) else None
    res["latency_ms"] = (res["latency_samples"] / sr * 1000.0) if res["latency_samples"] is not None else None

    # ---- 3. transient behaviour (amplitude step) --------------------------------
    burst = np.concatenate([np.zeros(sr // 4), 0.5 * np.sin(2 * np.pi * f_in * np.arange(sr // 2) / sr),
                            np.zeros(sr // 4)])
    pb = OctavoxPoC(sr=sr, nvoices=1, voices=[VoiceCfg(semitones=0, base_delay=BASE_DELAY, grain=GRAIN)],
                    interp=interp)
    yb = pb.process_blocks(burst, 64)
    env = np.abs(yb)
    k = int(sr * 0.005)
    env_s = np.convolve(env, np.ones(k) / k, mode="same")
    onset = sr // 4
    peak = env_s[onset : onset + int(sr * 0.3)].max()
    thr = 0.5 * peak
    idx = np.nonzero(env_s[onset : onset + int(sr * 0.3)] > thr)[0]
    lat = int(res["latency_samples"] or 0)
    res["transient_attack_samples"] = int(idx[0]) if len(idx) else None
    # the absolute number is dominated by the shifter's latency, so also report the ramp
    # measured from the moment the delayed burst starts to appear at the output
    res["transient_attack_rel_samples"] = int(max(0, idx[0] - lat)) if len(idx) else None
    res["transient_attack_rel_ms"] = (res["transient_attack_rel_samples"] / sr * 1000.0
                                      if res["transient_attack_rel_samples"] is not None else None)
    res["transient_pre_ring_db"] = float(
        20 * math.log10(max(1e-12, env_s[onset - 200 : onset].max()) / max(1e-12, peak)))
    # release: the tap pair is G samples apart, so after the input stops the output only
    # flushes once the *slow* tap has drained: input_end + first-tap latency + G.
    end_input = onset + int(sr * 0.5)
    end_all = end_input + lat + int(res["grain"])
    rel0 = end_all + sr // 50
    res["transient_output_end_samples"] = int(end_all - onset)
    res["transient_release_db"] = float(
        20 * math.log10(max(1e-12, env_s[rel0 : rel0 + sr // 100].max()) / max(1e-12, peak)))

    # ---- 4. discontinuities / steadiness (voice 0, from the pitch run) ----------
    tail = y0 if y0 is not None else np.zeros(16)
    pk = max(1e-12, float(np.max(np.abs(tail))))
    d1 = np.max(np.abs(np.diff(tail)))
    res["max_sample_jump"] = float(d1)
    res["max_sample_jump_rel"] = float(d1 / pk)
    # |y[n] - 2y[n-1] + y[n-2]| is ~A*(2*pi*f/sr)^2 for a smooth sine (here 4.1e-4 at
    # 220 Hz / 0.5 amplitude) and ~discontinuity size at a click, so it is a sensitive
    # detector.  Reported both absolutely and relative to the smooth-sine baseline.
    d2 = np.max(np.abs(np.diff(tail, n=2)))
    res["max_second_diff"] = float(d2)
    res["smooth_sine_baseline_2nd_diff"] = float(
        np.max(np.abs(tail)) * (2.0 * math.pi * f_in / sr) ** 2)
    res["discontinuity_db"] = float(20 * math.log10(max(1e-12, d2) / pk))
    res["steady_thd"] = _thd_energy(tail, sr, _dominant_freq(tail, sr) or f_in)

    # ---- 5. cost -----------------------------------------------------------------
    dur2 = 2.0
    x2 = 0.3 * np.random.default_rng(7).standard_normal(int(sr * dur2))
    for nv in (1, 8):
        vs = [VoiceCfg(semitones=semis[i % len(semis)], base_delay=BASE_DELAY + 32.0 * i) for i in range(nv)]
        pc = OctavoxPoC(sr=sr, nvoices=nv, voices=vs, interp=interp)
        t0 = time.perf_counter()
        pc.process_blocks(x2, 64)
        el = time.perf_counter() - t0
        res[f"cost_{nv}voice_xrealtime"] = float(dur2 / el)
        res[f"cost_{nv}voice_ms_per_s_audio"] = float(el / dur2 * 1000.0)

    # ---- 6. stability ------------------------------------------------------------
    rng = np.random.default_rng(11)
    dur3 = 4.0
    x3 = 0.2 * rng.standard_normal(int(sr * dur3))
    ps = OctavoxPoC(sr=sr, nvoices=8,
                    voices=[VoiceCfg(semitones=semis[i], base_delay=BASE_DELAY + 32.0 * i) for i in range(8)],
                    interp=interp, feedback=0.35)
    blk = 64
    bad = 0
    mx = 0.0
    for i in range(0, len(x3), blk):
        yb2 = ps.process(x3[i : i + blk])
        if not np.all(np.isfinite(yb2)):
            bad += 1
        mx = max(mx, float(np.max(np.abs(yb2))) if len(yb2) else 0.0)
    res["stability_nonfinite_blocks"] = bad
    res["stability_max_abs"] = mx
    res["stability_bounded"] = bool(bad == 0 and mx < 100.0)
    return res


def _md_report(out: dict) -> str:
    L = ["# OCTAVOX PoC — measurement results (our implementation)",
         "",
         "Generated by `octavox_poc.py`. These numbers describe **our** independent implementation,",
         "not the target plug-in. Deterministic except for wall-clock cost rows.",
         "",
         "Pitch is reported three ways. `measured Hz` is the YIN (cumulative-mean-normalised",
         "difference) estimate = the grain-average period. `fft Hz` is the interpolated spectral",
         "peak = the *instantaneous carrier*. `predicted Hz` is the closed form derived in",
         "`_predicted_shift_hz`: writing A = x[n-d], B = x[n-d-G], d = d0 + G*phi, w = (1-cos pi*phi)/2,",
         "the two-tap complementary crossfade is exactly",
         "`y = P(phi)*sin(u) + Q(phi)*cos(u)` with `P = cos(w0 G) + (1-cos pi phi)(1-cos w0 G)/2`,",
         "`Q = -(1+cos pi phi) sin(w0 G)/2`, so the carrier runs at `ratio*f` but its coefficient",
         "rotates by `Phi(phi) = atan2(Q,P)`; hence the output sits at",
         "`ratio*f + (Phi(1)-Phi(0))*(1-ratio)/G * sr/(2*pi)`. That artefact is a property of the",
         "reconstructed architecture, not a coding error, and it shrinks ~1/G.",
         ""]
    for name, r in out.items():
        L += [f"## {name}", "",
              f"- interpolation: `{r['interp']}`  ·  voices: {r['nvoices']}  ·  diatonic: {r['diatonic']}  ·  sr: {r['sr']} Hz",
              f"- grain G = {r['grain']:.0f} samples, base delay = {r['base_delay']:.0f} samples",
              "",
              "| voice | nominal st | applied st | expected Hz | measured Hz (YIN) | fft Hz | predicted Hz | cents (YIN) | cents (fft) | cents (pred) | out-of-harmonic |",
              "|---|---|---|---|---|---|---|---|---|---|---|"]
        for row in r["pitch"]:
            L.append("| {voice} | {nominal_semitones} | {semitones} | {expected_hz:.3f} | {measured_hz:.3f} | {measured_fft_hz:.3f} | "
                     "{predicted_hz:.3f} | {cents_err:+.2f} | {cents_err_fft:+.2f} | {cents_err_predicted:+.2f} | "
                     "{thd_energy:.4f} |".format(**row))
        L += ["",
              f"- **pitch**: max |cents error| (YIN) = {r['pitch_max_cents_err']:.2f}",
              f"- **unity ratio (0 st)**: {r['unity_ratio_gain_db']:.2f} dB measured vs "
              f"{r['unity_ratio_gain_db_analytic']:.2f} dB analytic |cos(pi*f*G/sr)| — at r = 1 the "
              f"frozen 0.5/0.5 crossfade reads both taps G apart, so the unison voice is a two-tap comb "
              f"(notch spacing sr/G = {r['sr']/r['grain']:.2f} Hz)",
              f"- **latency**: {r['latency_samples']} samples = {r['latency_ms']:.3f} ms (impulse onset, −80 dBFS)",
              f"- **transient**: attack = {r['transient_attack_samples']} samples to −6 dB "
              f"(= {r['transient_attack_rel_samples']} samples / {r['transient_attack_rel_ms']:.2f} ms after the "
              f"delayed burst appears, the rest is latency); pre-ring = {r['transient_pre_ring_db']:.1f} dB; "
              f"release(+20 ms after the output burst ends) = {r['transient_release_db']:.1f} dB",
              f"- **discontinuity**: max |Δy| = {r['max_sample_jump']:.6f} ({r['max_sample_jump_rel']*100:.2f} % of peak); "
              f"max |Δ²y| = {r['max_second_diff']:.3e} vs smooth-sine baseline {r['smooth_sine_baseline_2nd_diff']:.3e} "
              f"({r['discontinuity_db']:.1f} dB rel. peak); steady out-of-harmonic energy = {r['steady_thd']:.4f}",
              f"- **cost**: {r['cost_1voice_xrealtime']:.2f}x realtime (1 voice), "
              f"{r['cost_8voice_xrealtime']:.2f}x realtime (8 voices); "
              f"{r['cost_8voice_ms_per_s_audio']:.0f} ms CPU per second of audio (8 voices)",
              f"- **stability**: bounded = {r['stability_bounded']}, non-finite blocks = {r['stability_nonfinite_blocks']}, "
              f"max |y| = {r['stability_max_abs']:.3f}",
              ""]
        sw = r.get("grain_sweep")
        if sw:
            L += ["### grain-length trade-off (accuracy vs latency)", "",
                  "| grain | semitones | latency ms | fft Hz | cents (measured) | predicted Hz | cents (predicted) | out-of-harmonic |",
                  "|---|---|---|---|---|---|---|---|"]
            for s in sw:
                L.append("| {grain:.0f} | {semitones} | {latency_ms:.1f} | {measured_fft_hz:.3f} | "
                         "{cents_err_fft:+.2f} | {predicted_hz:.3f} | {cents_err_predicted:+.2f} | {thd_energy:.4f} |".format(**s))
            L += ["",
                  "Measured and predicted columns track each other: the disagreement shrinks as 1/G while the",
                  "latency grows linearly, which is the fundamental two-tap trade-off.", ""]
    return "\n".join(L)


def main() -> None:
    out = {}
    out["lagrange6"] = measure(interp="lagrange6")
    out["lagrange6_quantised_256phase"] = measure(interp="lagrange6_q")
    out["linear_interp_baseline"] = measure(interp="linear")
    out["diatonic_mode"] = measure(interp="lagrange6", diatonic=True)
    with open("poc_measurements.json", "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    with open("POC_RESULTS.md", "w", encoding="utf-8") as f:
        f.write(_md_report(out))
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
