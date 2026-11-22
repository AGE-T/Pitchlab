# DELIVERY ROUND 23 — PitchLab Harmonizer (YIN Research & Validation)

**Date:** 2026-11-05 · **Core:** octavox-native 16.2.0 + the R23 deltas ·
**Original binary: never executed.**

## The sixteen deliverables (docs/)

1. PITCHLAB_R23_YIN_SOURCE_COMPARISON.md — five implementations + paper
2. PITCHLAB_R23_YIN_RESEARCH.md — master synthesis
3. PITCHLAB_R23_YIN_IMPLEMENTATION_SPEC.md — the exact shipped deltas
4. PITCHLAB_R23_TRACKING_CONTROL_SPEC.md — the Pitch Control Layer
5. PITCHLAB_R23_ARTIFACT_ANALYSIS.md — the four user symptoms
6. PITCHLAB_R23_BENCHMARK_RESULTS.md — all measured numbers
7. PITCHLAB_R23_CURRENT_ENGINE_YIN_AUDIT.md — Gate 2 forensics
8. PITCHLAB_R23_YIN_MATHEMATICAL_DERIVATION.md
9. PITCHLAB_R23_YIN_WINDOW_LAG_LATENCY_STUDY.md
10. PITCHLAB_R23_YIN_VARIANT_TAXONOMY.md
11. PITCHLAB_R23_DECIMATION_FILTER_STUDY.md
12. PITCHLAB_R23_YIN_CONFIDENCE_VOICING_STUDY.md
13. PITCHLAB_R23_YIN_STEP6_REALTIME_STUDY.md
14. PITCHLAB_R23_BENCHMARK_PROTOCOL_AND_METRICS.md
15. PITCHLAB_R23_R22_MEASUREMENT_REPAIR.md
16. PITCHLAB_R23_DECISION_RECORD.md

## Gates

* G0 baseline: R22 archive SHA-256 verified; clean rebuild **100/100**
  (Release + ASan/UBSan).
* G1: the R22 measurement harness repaired (§27, all 8 items; the
  global-argmax fallback produced 58 false 2000.0 Hz cells — removed;
  anti-aliased measurement decimation; derived tone-free lock criterion);
  the 360-cell matrix re-run with **0 verdict deltas**.
* G2: full forensic audit of the detector/spacing path (line-cited).
* G3: source comparison + taxonomy from five FETCHED implementations;
  canonical math validated against independent vectors.
* G4: reference implementation **12/12** (incl. the FFT-equivalence proof
  at 3.7e-9 relative).
* G5: 15-signal x 4-rate x 7-variant comparison battery.
* G6: the Pitch Control Layer — **290/290** (lock/hold/release/outlier/
  octave/jitter/glissando/recovery).
* G7: artifacts — negative-shift crackle mechanism measured (commit jumps
  under movement; default crossfade protects); dual-mono bit-exact at
  native AND real-VST3-module level (12/12); NaN/Inf propagation
  REPRODUCED and FIXED; builds differ 3.8-58x CPU, agree within
  audibility on steady material.
* G8: integration — non-finite input guard + CMNDF telemetry +
  Config::tracking_gate (default OFF = bit-exact): **100/100 + 12/12 +
  5/5**.
* G9: synthetic + module levels complete; recorded-audio (PTDB-TUG/
  GuitarSet) and Windows-DAW levels OPEN, honestly recorded (no
  real-audio accuracy claim is made anywhere).

## Compatibility

VST3 parameter ids 0..37 (38 params) unchanged; state blob v2 unchanged
(restored replay bit-exact); default-config DSP output bit-exact vs R22.
(The brief's "0-73" is the ORIGINAL's 73 host parameters from static RE —
documented in the audit.)

## Packaging post-audit (this release, re-verified)

Three delivery-integrity defects found and fixed during the final
fresh-extraction audit, then the archive rebuilt and re-verified
end-to-end:

1. **Stale manifest**: checksums.txt was the R22 cumulative manifest
   (638 records) while this delta package ships 93 files —
   `sha256sum -c` failed on 596 missing paths. REGENERATED for this
   tree (93/93 OK; section headers follow the R22 convention).
2. **README not updated**: the package README was still the round-22
   header. The round-23 delta section is now prepended.
3. **CMake fresh-build link failure** [OBS, reproduced]: `octavox_tests`
   (and every octavox_dsp-linked target) failed to link from a clean
   configure — `src/dsp.cpp` references `PitchControlLayer` (the G8
   integration) while `pitch_control.cpp` built only into the separate
   `octavox_pitch_control` library. FIXED by one idiomatic line:
   `target_link_libraries(octavox_dsp PUBLIC octavox_pitch_control)`.
   No source behaviour change (the working-tree binaries already
   linked via the build ordering); the DELTA package is now
   self-contained. `bench/bench_main.cpp` (referenced by the CMake
   `octavox_bench` target) was also missing from the package — added.

**Final package verification (from the delivered zip, fresh extraction):**
checksums 93/93 · cmake configure + ALL targets build ·
octavox_tests **100/100** (19.9 s) · r23_yin_reference **12/12** ·
r23_control_battery **290/290** · r23_integration_check **5/5** ·
r15refcap hash record reproduced bit-exactly.
The archive's own SHA-256 is recorded OUTSIDE itself (the delivery
dashboard `.sha256` sidecar next to the download) — a zip cannot
contain its own hash.
