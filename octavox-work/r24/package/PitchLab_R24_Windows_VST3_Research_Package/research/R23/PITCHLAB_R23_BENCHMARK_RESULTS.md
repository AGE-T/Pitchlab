# PITCHLAB R23 — Benchmark Results (Gates 4–5, 7–8)

**Conditions:** g++ (Debian 14.2.0-19) -std=c++17 -O2, Xeon (sandbox),
Linux 5.10; deterministic corpora, fixed seeds; all JSON evidence in
`evidence/analysis_round23/`. Every comparison uses identical signals,
frame timestamps (the engine's 64-sample cadence boundaries), lag ranges
([f0_min=60 → 1200] Hz) and voiced masks (brief §26).

---

## 1. Reference implementation (Gate 4) — `r23_yin_reference.json`

12/12 checks: fixed-W difference + canonical CMNDF vs independently
computed (Python) vectors (≤2e-9); shrinking-support variant likewise;
**FFT acceleration (yinfast-type) equivalent to direct within 3.69e-9 max
relative error** (documented tolerance 1e-8 — float round-off at W=512
scales); boundary suite: flat/DC → degenerate tau_min fallback (documented),
DC+AC → DC cancelled by the difference function (220.000 Hz estimated),
silence → zero-energy guard, NaN/Inf inputs → reason=4 without propagation,
1e-12-amplitude signal still estimated (CMNDF scale invariance), tau_max
boundary minimum handled.

Variant accuracy on canonical tones (threshold 0.10, 48 kHz):
paper-fixed-W with raw-d parabola ≤0.03 c (all tones); CMNDF-parabola
+0.02…+0.09 c; shrinking-support ≈ CMNDF-parabola; **legacy R22 law
0.00–0.33 c** (worst at 880 Hz, the truncated-CMNDF cost). The paper's
raw-d-abscissa nuance is measurably the most accurate refinement.

## 2. Detector comparison (Gate 5) — `r23_detector_compare.json`

15-signal corpus × 44.1/48/96/192 kHz × 7 variants (engine FullRate /
Decimated / Hybrid via the public API; canonical paper-W1 / W1.5 / W2;
FFT-W1). Aggregates over tonal signals:

| variant | valid% | med \|cents\| | p95 \|cents\| | gross>50c | octave | jitter (MAD c) | CPU |
|---|---|---|---|---|---|---|---|
| engine Decimated | 100 | **0.02** | **1.54** | 0.000 | 0.000 | 0.031 | **362 ns/sample** |
| engine FullRate | 100 | **0.02** | 3.01 | 0.000 | 0.000 | 0.010 | 1685 ns/sample |
| engine Hybrid | 100 | 0.70 | 6.23 | 0.000 | 0.000 | 0.010 | 396 ns/sample |
| paper W=T_max | 100 | 0.31 | 3.04 | 0.000 | 0.000 | 0.003 | 2 752 510 ns/update |
| paper W=1.5T | 100 | 0.22 | 3.28 | 0.000 | 0.000 | 0.003 | 4 164 053 ns/update |
| paper W=2T | 100 | 0.22 | 2.89 | 0.000 | 0.000 | 0.002 | 5 562 172 ns/update |
| FFT W=T_max | 100 | 0.31 | 3.04 | 0.000 | 0.000 | 0.003 | (equivalence; not timed) |

* Engine CPU by rate (ns/sample): 44.1k 427/1672/433; 48k 361/1387/363;
  96k 360/5128/393; 192k **361 / 20938 / 507** (Decimated/FullRate/Hybrid).
* **Tracking beats canonical on moving pitch** (the short-m support's
  faster memory): vibrato p95 26.4 c (engine) vs 34.6–45.9 c (canonical);
  glissando p95 7.1 vs 9.4–12.7 c; abrupt steps: gross 2.4–2.9% everywhere.
* Stress cases @48k: octave-trap and missing-fundamental — **all variants
  correct, zero octave errors** (the corpus's traps are resolved by every
  implementation); white noise — **100% "valid" estimates for ALL variants**
  (YIN always emits the global-min fallback: a VOICING decision is missing
  everywhere — the confidence/voicing study's motivation); silence — 0%
  valid everywhere; DC — degenerate 1200 Hz boundary estimates everywhere
  (preconditioning question, documented).
* Window study (W = 1.0/1.5/2.0 T_max): med error 0.31 → 0.22 → 0.22 c,
  p95 3.04 → 3.28 → 2.89 c, tracking degrades with W (vibrato p95
  34.6 → 40.2 → 45.9 c) — the paper's stability/temporal-resolution
  trade-off confirmed in both directions; the legacy short-m law
  (m ≈ 0.28 T_max) tracks best on our corpus at no measured accuracy cost.

## 3. Control layer (Gate 6) — `r23_control_battery.json`

290/290: acquisition 3 frames (4 ms); hold 75 frames then bounded release;
recovery 1 frame; +300c outlier rejected; octave accepted after 3 frames;
jitter 4.29c → 0.17c (×0.04, both smoothing spaces); glissando max error
47–48 c @900c/s slew; NaN/Inf/range frames never damage state.

## 4. Artifacts (Gate 7) — `r23_artifacts.json`

See PITCHLAB_R23_ARTIFACT_ANALYSIS.md. Key numbers: negative shifts 0
clicks, HF −11…−43 dB; sweep+jitter+no-crossfade 0.057 step / 2 clicks /
+11 dB HF (default crossfade clean); **+12 st +9.4 dB HF increase**;
dual-mono bit-identical (native + real VST3 module); NaN/Inf propagation
REPRODUCED pre-fix, FIXED post-fix; detector CPU 3.8–58× apart between
modes while their estimates agree to ~0.02 c on steady tones.

## 5. Regression & integration (Gate 8)

* Core suite: **100/100** (Release) with the R23 sources linked
  (default-config bit-exactness preserved; run logs in
  `logs/g0_baseline_suite.json` + the post-integration runs).
* VST3 module integration (real dlopen/module/factory/process path):
  **12/12** — stereo bit-exact, mono bit-exact, param queues, state
  save/restore (456-byte blob, restored replay bit-exact), latency report
  2304 samples, terminate/unload.
* Integration check: 5/5 (`r23_integration_check.json`).

## 6. Honest limitations

* The canonical variants' CPU is measured per update at the 8×-reduced
  frame stride (150/s); the FFT variant was verified for equivalence but
  not timed (its cost model is documented in the taxonomy).
* Real-audio (recorded, annotated) evaluation: NOT completed this round
  (PTDB-TUG/GuitarSet access pending — see the decision record §9);
  all accuracy numbers above are SYNTHETIC-corpus results, honestly
  labelled.
* No Windows rebuild this round (the cross-toolchain is absent from the
  rolled-back sandbox; the R22 four-variant Windows delivery remains the
  shipped binary set — recorded, not silently skipped).
