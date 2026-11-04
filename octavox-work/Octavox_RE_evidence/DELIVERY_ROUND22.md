# DELIVERY ROUND 22 — PitchLab Harmonizer

**Date:** 2026-11-03 · **Core:** octavox-native 17.0.0 · **Original binary:
never executed.**

## What is in this package

The complete research + implementation tree for rounds 6–22 (this round's
deltas listed below). The original Eventide Octavox binary is NOT included
(see the provenance rules; it was analysed statically only).

## Round-22 deltas (the twelve deliverables)

1. `PITCHLAB_ROUND22_FINDINGS.md` — the round findings (this package's root).
2. `docs/R22_MINIMUM_PITCH_PATH.md` + `evidence/analysis_round22/r22_lowfreq.json`
   — the complete Minimum Pitch path specification + the 10-point ×
   6-interval × 3-mode low-frequency battery (186 runs; the
   detectable-vs-shiftable distinction measured).
3. `native/cli/r22_realaudio.cpp` (+ the built `octavox_r22_realaudio`) +
   `evidence/analysis_round22/realaudio_r22_demo/` — the real-WAV
   comparison harness with the synthetic/recorded/annotated category
   discipline, the manifest, the labelled test-set sheet, trajectories,
   output WAVs and the full metric set.
4. `evidence/analysis_round22/realaudio_r22_demo/r22_realaudio_demo.json`
   — the FullRate/Decimated/Hybrid comparison results (synthetic-category
   demonstration, honestly labelled).
5. Per-voice Pan + Feedback: `native/include/octavox/dsp.hpp` +
   `native/src/dsp.cpp` (setVoicePan/setVoicePanGain/setVoiceFeedback,
   the overlay-ring topology) + the 13 acceptance cases in
   `native/tests/test_main.cpp` (group `r22`).
6. The harmony-mapper integration: `native/vst3/octavox_params.{h,cpp}`
   (59 parameters, ids 38..58), `octavox_native_processor.cpp` (the
   composition law), the state-blob v3 + migration, six wrapper cases in
   `octavox_vst3_tests.cpp`.
7. `evidence/analysis_round22/r22_static_findings.md` + the updated
   `docs/R21_FEATURE_RECONSTRUCTION_MATRIX.md` — the OBS/DER/UNK matrix
   with the R22 rows.
8. `evidence/analysis_round22/r22_randomize_trace.txt/.json` — the
   Randomize polarity resolution + the correction record.
9. The PitchLab VST3 variants: four Linux builds (each 27/27 wrapper +
   12/12 integration + 47/47 validator; logs in
   `evidence/analysis_round22/logs/`) + the Windows delivery
   (`octavox-work/r22/Windows_delivery/`, four PE32+ modules + the
   validation kit + `r22_windows_verification.json`).
10. The state-migration tests + parameter metadata:
    `harmony_state_v3_roundtrip`, `harmony_state_migration`,
    `harmony_surface_defaults` (+ the updated parameter tables/help in
    `octavox_params.cpp`).
11. `evidence/analysis_round22/r22_bench.json` + `r22_dualmono.json` +
    `r22_stereo.json` + `r22_suite.json` + `r22_suite_asan.json` — the
    CPU, click (max-output-step + the click gates in the suite) and the
    stereo regression records.
12. The archive manifest (this package's `checksums.txt` R22 section) +
    the SHA-256 of the archive.

## Gates (all re-run on the final sources)

* Core suite: **115/115** (Release; ASan/UBSan 115/115).
* VST3 wrapper: **27/27 ×4 variants**; module integration **12/12 ×4**;
  Steinberg validator **47/47 ×4**.
* Harmony mapper vectors: **545/545** (worst deviation 0.0).
* Dual-mono transparency matrix: **0 violations**; the R21 stereo matrix:
  **0 violations**.
* Windows structural verification: **4/4 OK**.
* `--filter`/selection: `--filter detector_hybrid` now executes 3/3; an
  empty filtered run exits 2.

## Compatibility notes

* ids 0..37, their titles/domains/defaults/automation and the ≤R21 state
  blobs are unchanged (v1/v2 restore bit-neutrally; saves are v3).
* The engine's spacing-accept gate law changed ONLY in its effective-floor
  computation (bit-exact for every default config; the one documented
  behaviour change: a config with `cfg.fmin > cfg.f0_min` now follows the
  detector down — no shipped configuration uses that state).
* The default detector (Decimated), the default f0_min (60 Hz), the
  default interpolation (Lagrange6) and every default parameter value are
  unchanged.
