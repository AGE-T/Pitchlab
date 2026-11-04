#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCTAVOX round 12 -- cross-language metrics + Python reference timing
====================================================================
Runs the round-11 Python reference (``poc/octavox_v2.py``, untouched) and the
round-12 native core (via the ``octavox_cli`` WAV->WAV binary) on IDENTICAL
deterministic inputs and measures both with the same metric code:

  * unity comb depth  (gain deviation across a tone battery; the round-12
    fix under test: the native core must be flat, the Python v2 reference
    keeps its documented two-tap comb at exact unity -- the BEFORE case)
  * pitch accuracy over the cent battery (0, +-1, +-10, +-50, +-100,
    +-500, +-1200 cents)
  * envelope modulation (period-synchronous metric, as round 11)
  * stereo isolation (L tone / R silence)
  * Python reference processing cost on the shared LCG noise input, for the
    native-vs-Python benchmark comparison (the native numbers come from
    octavox_bench's JSON)

Everything measures OUR implementations. Exit code 1 on any gate failure.
Results: r12_native_metrics_results.json (+ .txt next to this script).

usage: python3 r12_native_metrics.py --poc <poc dir> --native-cli <path>
                                     [--bench-json <octavox_bench json>]
                                     [--workdir <scratch dir>]
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import subprocess
import sys
import time

import numpy as np

# ---------------------------------------------------------------------------
# deterministic generators (identical spec to the C++ side)
# ---------------------------------------------------------------------------


class Lcg:
    MUL = 6364136223846793005
    ADD = 1442695040888963407
    MASK53 = (1 << 53) - 1

    def __init__(self, seed: int):
        self.s = (seed * self.MUL + self.ADD) & 0xFFFFFFFFFFFFFFFF

    def next(self) -> float:
        self.s = (self.s * self.MUL + self.ADD) & 0xFFFFFFFFFFFFFFFF
        return ((self.s >> 11) & self.MASK53) / 9007199254740992.0

    def noise(self, n: int, amp: float) -> np.ndarray:
        out = np.empty(n)
        for i in range(n):
            out[i] = (2.0 * self.next() - 1.0) * amp
        return out


def tone(f: float, sr: int, n: int, amp: float) -> np.ndarray:
    t = np.arange(n) / sr
    return amp * np.sin(2 * np.pi * f * t)


# ---------------------------------------------------------------------------
# WAV helpers (16-bit, matching both CLIs' write paths)
# ---------------------------------------------------------------------------


def write_wav16(path: str, x: np.ndarray, sr: int) -> None:
    import wave

    if x.ndim == 1:
        x = x[:, None]
    y = np.clip(x, -1.0, 1.0)
    q = np.round(y * 32767.0).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(q.reshape(-1).tobytes())


def read_wav16(path: str):
    import wave

    with wave.open(path, "rb") as w:
        sr = w.getframerate()
        ch = w.getnchannels()
        raw = w.readframes(w.getnframes())
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if ch > 1:
        x = x.reshape(-1, ch)
    return x, sr, ch


# ---------------------------------------------------------------------------
# metrics (round-11 style)
# ---------------------------------------------------------------------------


def pitch_fft(y: np.ndarray, sr: int, fmin=50.0, fmax=4000.0) -> float:
    n = len(y)
    N = 1
    while N < min(n, 65536):
        N <<= 1
    N = min(N, 65536)
    seg = y[n - N :].astype(np.float64)
    w = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(N) / N)
    sp = np.abs(np.fft.rfft(seg * w))
    kmin = max(1, int(fmin * N / sr))
    kmax = min(len(sp) - 2, int(fmax * N / sr))
    k = kmin + int(np.argmax(sp[kmin:kmax]))
    a, b, c = sp[k - 1], sp[k], sp[k + 1]
    d = 0.0
    den = a - 2 * b + c
    if abs(den) > 1e-300:
        d = 0.5 * (a - c) / den
    return sr * (k + d) / N


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.asarray(x, dtype=np.float64) ** 2)))


def env_mod_period_sync(y: np.ndarray, sr: int, f0: float, skip_s: float) -> float:
    """Period-synchronous envelope modulation (round-11 metric)."""
    per = max(1, int(round(sr / f0)))
    i0 = int(skip_s * sr)
    seg = y[i0:]
    m = len(seg) // per
    if m < 2:
        return 0.0
    env = np.sqrt(np.mean(seg[: m * per].reshape(m, per) ** 2, axis=1))
    lo, hi = float(env.min()), float(env.max())
    if lo <= 0:
        return 999.0 if hi > 0 else 0.0
    return 20 * math.log10(hi / lo)


def gain_db(y: np.ndarray, x: np.ndarray, skip_s: float, sr: int) -> float:
    i0 = int(skip_s * sr)
    return 20 * math.log10(rms(y[i0:]) / max(1e-300, rms(x[i0:])))


# ---------------------------------------------------------------------------
# engine drivers
# ---------------------------------------------------------------------------


def run_native(cli: str, in_path: str, out_path: str, cents: float, voices: int,
               spread: bool = True, interp: str = "lagrange6") -> dict:
    cmd = [cli, "--in", in_path, "--out", out_path,
           "--cents", f"{cents:.10g}", "--voices", str(voices)]
    if not spread:
        cmd.append("--no-spread")
    if interp != "lagrange6":
        cmd += ["--interp", interp]
    r = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return json.loads(r.stdout)


def run_python_v2(poc_dir: str, in_path: str, out_path: str, cents: float,
                  voices: int, spread: bool = True) -> float:
    """Run the round-11 Python reference; returns wall seconds."""
    # Imported lazily so --help works without the poc tree.
    from octavox_v2 import AlignedDelayLineShifter, process_multichannel  # noqa
    from octavox_offline import read_wav, write_wav  # noqa

    x, sr = read_wav(in_path)
    if x.ndim == 1:
        x = x[:, None]
    ratio = 2.0 ** (cents / 1200.0)

    def make():
        return AlignedDelayLineShifter(sr, ratio, voices, spread=spread)

    t0 = time.perf_counter()
    y = process_multichannel(make, x)
    el = time.perf_counter() - t0
    write_wav(out_path, y, sr)
    return el


def python_v2_timing(sr: int, seconds: float, voices: int) -> dict:
    from octavox_v2 import AlignedDelayLineShifter

    rng = Lcg(2025)
    x = rng.noise(int(sr * seconds), 0.5)
    ratio = 2.0 ** (400.0 / 1200.0)
    eng = AlignedDelayLineShifter(sr, ratio, voices)
    t0 = time.perf_counter()
    eng.process(x)
    el = time.perf_counter() - t0
    return {
        "voices": voices,
        "seconds_audio": seconds,
        "cpu_s": el,
        "x_realtime": seconds / el,
        "ns_per_frame": el * 1e9 / (seconds * sr),
    }


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--poc", required=True, help="path to the poc/ directory")
    ap.add_argument("--native-cli", required=True, help="path to octavox_cli")
    ap.add_argument("--bench-json", default=None,
                    help="optional octavox_bench JSON for the comparison table")
    ap.add_argument("--workdir", default="/tmp/octavox_r12_metrics")
    args = ap.parse_args()

    os.makedirs(args.workdir, exist_ok=True)
    sys.path.insert(0, args.poc)

    sr = 48000
    results = {"meta": {"sr": sr, "poc": args.poc, "native_cli": args.native_cli},
               "unity_comb": [], "pitch_battery": [], "stereo": {},
               "python_timing": [], "native_bench": None, "gates": []}
    failures = []

    def gate(name, ok, detail):
        results["gates"].append({"name": name, "pass": bool(ok), "detail": detail})
        if not ok:
            failures.append(f"{name}: {detail}")

    # ------------------------------------------------------------------
    # 1. unity comb battery: tone gains across frequencies, native vs v2
    # ------------------------------------------------------------------
    comb_freqs = [110.0, 165.0, 220.0, 330.0, 440.0, 660.0, 880.0, 1320.0]
    nat_gains, v2_gains = [], []
    for f in comb_freqs:
        n = int(1.5 * sr)
        x = tone(f, sr, n, 0.5)
        p_in = os.path.join(args.workdir, f"comb_in_{int(f)}.wav")
        p_nat = os.path.join(args.workdir, f"comb_nat_{int(f)}.wav")
        p_v2 = os.path.join(args.workdir, f"comb_v2_{int(f)}.wav")
        write_wav16(p_in, x, sr)
        run_native(args.native_cli, p_in, p_nat, 0.0, 1)
        run_python_v2(args.poc, p_in, p_v2, 0.0, 1)
        ynat, _, _ = read_wav16(p_nat)
        yv2, _, _ = read_wav16(p_v2)
        xin, _, _ = read_wav16(p_in)
        gn = gain_db(ynat, xin, 0.5, sr)
        gv = gain_db(yv2, xin, 0.5, sr)
        nat_gains.append(gn)
        v2_gains.append(gv)
        results["unity_comb"].append(
            {"f_hz": f, "native_gain_db": gn, "v2_gain_db": gv})
    nat_spread = max(nat_gains) - min(nat_gains)
    v2_spread = max(v2_gains) - min(v2_gains)
    gate("native_unity_comb_flat", nat_spread <= 0.05,
         f"native gain spread {nat_spread:.4f} dB across the tone battery")
    gate("v2_unity_comb_measured", v2_spread > 0.5,
         f"v2 reference comb spread {v2_spread:.4f} dB (the documented "
         f"two-tap unity comb -- BEFORE case)")

    # ------------------------------------------------------------------
    # 2. pitch battery (native): cent list from the round-12 mandate
    # ------------------------------------------------------------------
    cent_list = [0.0, 1.0, -1.0, 10.0, -10.0, 50.0, -50.0, 100.0, -100.0,
                 500.0, -500.0, 1200.0, -1200.0]
    f0 = 220.0
    n = int(3.0 * sr)
    x = tone(f0, sr, n, 0.5)
    p_in = os.path.join(args.workdir, "pitch_in.wav")
    write_wav16(p_in, x, sr)
    for cents in cent_list:
        p_out = os.path.join(args.workdir, f"pitch_nat_{cents:+g}.wav")
        run_native(args.native_cli, p_in, p_out, cents, 1)
        y, _, _ = read_wav16(p_out)
        fm = pitch_fft(y[int(1.5 * sr):], sr)
        err = 1200 * math.log2(fm / f0) - cents
        mod = env_mod_period_sync(y, sr, f0 * (2.0 ** (cents / 1200.0)), 1.0)
        results["pitch_battery"].append(
            {"cents": cents, "measured_f0": fm, "error_cents": err,
             "env_mod_db": mod})
        if cents != 0.0:
            gate(f"pitch_{cents:+g}c", abs(err) <= 2.0,
                 f"pitch error {err:+.4f} cents")

    # ------------------------------------------------------------------
    # 3. stereo isolation (native), compared with the v2 per-channel fix
    # ------------------------------------------------------------------
    n = int(2.0 * sr)
    xL = tone(220.0, sr, n, 0.5)
    xR = np.zeros(n)
    p_in = os.path.join(args.workdir, "stereo_in.wav")
    p_nat = os.path.join(args.workdir, "stereo_nat.wav")
    p_v2 = os.path.join(args.workdir, "stereo_v2.wav")
    write_wav16(p_in, np.stack([xL, xR], axis=1), sr)
    run_native(args.native_cli, p_in, p_nat, 400.0, 1)
    run_python_v2(args.poc, p_in, p_v2, 400.0, 1)
    ynat, _, _ = read_wav16(p_nat)
    yv2, _, _ = read_wav16(p_v2)
    nat_r = float(np.max(np.abs(ynat[:, 1])))
    v2_r = float(np.max(np.abs(yv2[:, 1])))
    nat_r_db = 20 * math.log10(max(1e-300, nat_r))
    v2_r_db = 20 * math.log10(max(1e-300, v2_r))
    results["stereo"] = {"native_R_peak": nat_r, "native_R_peak_db": nat_r_db,
                         "v2_R_peak": v2_r, "v2_R_peak_db": v2_r_db}
    gate("native_stereo_isolation", nat_r <= 1e-10,
         f"native R-channel peak {nat_r:.3e} ({nat_r_db:.1f} dB)")

    # ------------------------------------------------------------------
    # 4. Python reference timing on the shared LCG input
    # ------------------------------------------------------------------
    for voices in (1, 8):
        t = python_v2_timing(sr, 5.0, voices)
        results["python_timing"].append(t)

    if args.bench_json and os.path.exists(args.bench_json):
        with open(args.bench_json, "r", encoding="utf-8") as f:
            results["native_bench"] = json.load(f)

    out_base = os.path.splitext(os.path.abspath(__file__))[0]
    with open(out_base + "_results.json", "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)

    # text report
    lines = ["OCTAVOX round 12 -- native vs Python v2 cross-language metrics", ""]
    lines.append("unity comb battery (gain deviation across tones, dB):")
    lines.append(f"  native core : spread {nat_spread:+.4f} dB  "
                 f"[{min(nat_gains):+.4f} .. {max(nat_gains):+.4f}]")
    lines.append(f"  python v2   : spread {v2_spread:+.4f} dB  "
                 f"[{min(v2_gains):+.4f} .. {max(v2_gains):+.4f}]  (BEFORE)")
    lines.append("")
    lines.append("pitch battery (native, 220 Hz):")
    for row in results["pitch_battery"]:
        lines.append("  %+7.1f c -> measured %9.4f Hz, error %+7.4f c, "
                     "env.mod(period-sync) %.3f dB"
                     % (row["cents"], row["measured_f0"], row["error_cents"],
                        row["env_mod_db"]))
    lines.append("")
    lines.append("stereo isolation (L tone, R silence, +400c):")
    lines.append("  native R peak: %.1f dB   v2 R peak: %.1f dB"
                 % (nat_r_db, v2_r_db))
    lines.append("")
    lines.append("python reference timing (LCG noise, 5 s, +400c):")
    for t in results["python_timing"]:
        lines.append("  %d voices: %.3f s (%.2fx realtime, %.0f ns/frame)"
                     % (t["voices"], t["cpu_s"], t["x_realtime"],
                        t["ns_per_frame"]))
    if results["native_bench"]:
        lines.append("native bench rows (see r12_native_bench.json):")
        for row in results["native_bench"]["rows"]:
            lines.append("  %-24s %2d v %d ch: %.0f ns/frame, %.2fx realtime"
                         % (row["name"], row["voices"], row["channels"],
                            row["ns_per_frame"], row["realtime_factor"]))
    lines.append("")
    lines.append("gates: %d/%d pass" % (len(results["gates"]) - len(failures),
                                        len(results["gates"])))
    for g in results["gates"]:
        if not g["pass"]:
            lines.append("  FAIL " + g["detail"])
    txt = "\n".join(lines) + "\n"
    with open(out_base + "_results.txt", "w", encoding="utf-8") as f:
        f.write(txt)
    print(txt)
    print("results: %s_results.json/.txt" % out_base)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
