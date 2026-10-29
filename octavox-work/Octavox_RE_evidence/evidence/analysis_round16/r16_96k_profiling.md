# R16-P3 — 96 kHz path profiling and optimisation assessment (2026-10-28)

Task: Priority 3 of the round-16 task book — "Profile the 96 kHz path, where
the recorded realtime factor is approximately 2.0 to 2.1. Identify the main
cost and propose a measured optimisation only if it does not compromise
pitch accuracy or numerical behaviour."

All numbers below are measured on the current 15.0.0 source (no core change
was made in this round — the assessment is measurement + analysis; see the
decision at the end). Measurement host: 2-core Intel Xeon, GCC 14.2.0,
Release -O2 (environment records inside `r16_bench.json`).

## 1. Measured facts (from the R16-P2 bench, all 0 allocations)

Bench rows (mono, 8 voices, block 128; `r16_bench_stdout.txt` /
`r16_bench.json`):

| rate | ctrl 64 | ns/frame | ×RT | ctrl 512 | ns/frame | ×RT |
|------|---------|----------|-----|----------|----------|-----|
| 48 kHz | `mono_8v_b128` | 1562.51 | 13.4 | `mono_8v_b128_ctrl512` | 418.27 | 50.1 |
| 96 kHz | `mono_8v_b128_96k` | 5189.13 | 2.00 | `mono_8v_b128_96k_ctrl512` (NEW R16 isolation row) | 871.58 | 11.90 |

Two-readings detector share (derivation in `r16_suite_profile.md` §e):
direct reading (1 − ctrl512/ctrl64): **73.2 % at 48 kHz, 83.2 % at 96 kHz**;
linear-control model: detector invocation cost **83.7 µs → 315.8 µs**
(3.77× from 48 to 96 kHz) with the non-detector base **254.8 ns/frame at
BOTH rates** (agreement 0.1 ns — the voice/dry/output path itself is
sample-rate-constant per sample; the entire 96 kHz slowdown is in the
detector).

## 2. Main cost, identified in the code (src/dsp.cpp, PitchDetector)

The YIN difference loop is **O(ntau × m)** per invocation
(`for tau in [tau_min, tau_max)` × `for j in [0, m)` accumulate
`(x[seg0+j]−x[seg0+tau+j])²`):

| quantity | 48 kHz | 96 kHz | scaling |
|---|---|---|---|
| tau_min = sr/1200, tau_max = sr/60 | 40 / 800 | 80 / 1600 | ×2 (lag range ∝ sr) |
| win = max(2048, ceil(2.5·tau_max)) | 2048 | 4000 | ×1.95 (the R13-b window law) |
| m = win/2 − tau_max | 224 | 400 | ×1.79 (difference support) |
| ntau = tau_max − tau_min | 760 | 1520 | ×2 |
| inner MACs = ntau × m | ≈170 K | ≈608 K | **×3.57 (work)** |
| measured invocation cost | 83.7 µs | 315.8 µs | **×3.77** |

The residual ×3.77/×3.57 ≈ +5 % superlinearity is a cache effect: the
window working set grows 2048→4000 doubles (16→32 KB, past the L1 line of
this Xeon) and the lag-strided reads span ~25 KB more. So the 96 kHz price
has TWO multiplicative sources, both consequences of correctness decisions:
(a) the R13-b sample-rate-relative window law (the fixed-2048 window was
measured BLIND at 96 kHz — 12.0.0 returned 0.000000 Hz on a 110 Hz tone;
15.0.0 tracks 110.00003 Hz), and (b) the lag range doubling with sr for the
same 60–1200 Hz pitch band.

Per-second budget check: at 96 kHz the detector fires every 64 samples
(=1500 Hz cadence), each 315.8 µs → 0.474 s of detector work per second of
audio ≈ 47 % of one core; plus the 254.8 ns/frame DSP path → total
5189 ns/frame = 2.00× realtime margin, matching the recorded 2.0–2.1×.

## 3. Optimisation candidates — assessment (why none is applied to the default path)

| candidate | effect | verdict |
|---|---|---|
| FFT / autocorrelation formulation of the difference function (`d(τ) = r(0)+r(τ)−2·corr(τ)`) | O(win log win) instead of O(ntau·m) — the classic YIN speedup | **REJECTED**: reorders the floating-point arithmetic → the estimate bits change → the output stream changes → the four default-neutrality reference hashes (the 12.0.0==15.0.0 bit-identity regression) and the frozen pitch gates' measured literals would all change. Not bit-neutral. |
| Incremental / running-sum difference (reuse overlapping window content between invocations) | Amortises O(win) copies | **REJECTED**: same arithmetic-reordering objection; also the detector window is copied per invocation by design (fresh evidence per estimate — a stale-data bug class the round-13 recovery explicitly avoided). |
| `-ffast-math` / float accumulation / manual SIMD of the inner reduction | Vectorises the reduction, ~4–8× on the loop | **REJECTED**: changes numerics (reduction order), same bit-neutrality objection; also a global build-flag change affecting the whole core, not a DSP improvement. |
| Larger default `detection_interval` at high sr (e.g. scale by sr/48000) | Detector cadence per WALL-CLOCK second stays constant; ≈ ctrl512-class cost | **REJECTED for the default**: this is a behaviour change, not an optimisation — the 96 kHz output stream would differ from the delivered 15.0.0 (the acceptance configs pin their own intervals; the default is part of the tested contract). |
| Cache-blocking the inner loop (same arithmetic, tiled access order) | Maybe the +5 % superlinear part back | **NOT TAKEN**: the loop is already sequential in j (unit stride on both reads); the misses come from the tau stride, which is inherent to the algorithm; expected gain ≤ 5 % — not worth a core change under the bit-neutrality + acceptance discipline. |

**The measured optimisation that already exists and is tested**:
`Config::detection_interval` is a public, documented, acceptance-covered
configuration knob — at 512 the 96 kHz margin is **11.90× realtime**
(measured, 0 allocations; the `mono_8v_b128_96k_ctrl512` row added by
R16-P2 exactly as this isolation measurement). The estimate VALUES are
unchanged by the interval (same window, same math — only the update cadence
changes); hosts that need more margin at 96 kHz set it in `prepare()`.
Changing the DEFAULT was considered and rejected above (behaviour change).

## 4. Quality and realtime-safety constraints — preserved (verified this round)

* Lagrange6 stays the default interpolation mode — untouched (no core edit;
  the round-14 four-mode comparison verdict stands).
* No steady-state allocations: 0 allocs on every bench row incl. the two
  96 kHz rows and the surface row; the allocation counter gates hold
  (85/85 suite).
* No blocking locks on the audio thread: unchanged (RT contract in
  `dsp.hpp`; no locks exist in the core — audited rounds 12–15, re-audited
  by reading the current `process()` path this round).
* Independent state/parameters per voice, latency-aligned dry path,
  click-free bypass + ramps, stereo channel isolation, deterministic reset
  and repeated preparation: all re-verified by the 85-case suite
  (release + ASan/UBSan) on the current source in R16-P2.
* Reference hashes: 4/4 exact (re-captured twice this round: R16-R0 and
  R16-P2).

## 5. Conclusion

The 96 kHz realtime factor 2.0–2.1× is the measured price of the R13-b
correctity fix (sample-rate-relative window) plus the physically doubled
lag range; the cost is 83 % (direct) / 95 % (model) detector, located in
the O(ntau·m) YIN difference loop. **No optimisation is applied to the
default path** because every candidate either reorders arithmetic (breaking
the bit-neutrality regression and the frozen gates' measured literals) or
changes behaviour; the honest, already-shipped alternative is the tested
`detection_interval` configuration (11.90× margin at 96 kHz, measured).
This closes Priority 3 with a measured analysis and a deliberate no-change
decision.
