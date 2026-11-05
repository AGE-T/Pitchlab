# PITCHLAB R23 — Artifact Analysis (Gate 7)

**Mission:** reproduce the four user-reported issues, separate detector
errors from pitch-shifter defects, and attach measured root causes.
**Evidence:** `evidence/analysis_round23/r23_artifacts.json` (the native
battery, `cli/r23_artifacts.cpp`), `logs/r23_vst3_module_integration.json`
(the real-module VST3 path, 12/12), `r23_integration_check.json` (the fixed
engine), plus the R21 stereo matrix carried forward. Classes: OBS = measured
in this round's output; DER = derived; HYP = hypothesis; UNK = unresolved.
The DAW level (REAPER/Cubase on Windows) is **outside this Linux sandbox** —
honestly recorded as not re-verified here.

---

## 8.1 High-frequency crackling during negative pitch movement

**Reproduction corpus:** 220 Hz harmonic stack (40 harmonics → genuine
8–16 kHz content), static −24/−12 st, −12 st with ±30 cents input jitter,
a 1.0→0.5 ratio sweep, sweep+jitter, and +12 st as the control; all four
interpolation modes; commit-crossfade off/8 ms.

**Findings:**

1. [OBS] **Static negative shifts are clean**: 0 clicks, max inter-sample
   step 0.006–0.012, and the 8–16 kHz output energy is 11–43 dB BELOW the
   input's (downshifting maps harmonics down; no HF is created). Negative
   pitch movement alone does not produce HF crackle in this engine.
2. [OBS] **The reproducing condition is pitch MOVEMENT + input jitter with
   the commit crossfade disabled**: `sweep+jitter, xfade off` → max step
   0.057 (4–7× the clean level), 2 detected clicks, 8–16 kHz residue
   +11 dB above the same case with the crossfade on. With the engine's
   DEFAULT 8 ms commit crossfade the same input is clean (0.015 step,
   0 clicks, baseline HF).
   [DER] Mechanism: jitter moves the detector's period estimates → the
   spacing median S_target retunes → wrap-time spacing commits jump the
   dominant tap's read delay by ΔS (hysteresis-gated ≥ 0.5%·S) — without
   the crossfade this is a waveform discontinuity (the R19-b "dominant
   click source", re-measured here under movement).
   [HYP] The user's crackling instance plausibly involved (a) a build/path
   with `commit_crossfade_seconds = 0`, or (b) an engine predating R19's
   crossfade, or (c) a DAW-side glitch (not reproducible here). The current
   default is protected.
3. [OBS] **Positive shifts DO add HF**: +12 st raises the 8–16 kHz ratio
   by **+9.4 to +9.6 dB** (all interpolation modes) — the read rate 2× maps
   the stack's 5–10 kHz partials above Nyquist and the engine has NO
   pre-read anti-alias filter (audit §4: read rate = r; aliasing risk for
   r > 1 confirmed by measurement). The 766 "clicks" in the +12 st rows are
   a metric artifact (the adaptive threshold counts the octave-up output's
   naturally larger steps) — documented, not treated as defects.
4. [OBS] Interpolation choice is a second-order effect at negative shifts
   (HF residue −16 to −19 dB for all kernels); ObsKernel6 is consistently
   ~2–3 dB cleaner than Lagrange6. The phase-quantization hypothesis
   (2048-bin table steps under slow read sweeps) produced NO measurable
   discontinuity signature on this corpus — **refuted as a crackle source
   at these levels** [DER from 1].

**Verdict:** the reported symptom is NOT reproduced on the delivered R22
engine at default settings; the mechanism that DOES reproduce it
(estimate-driven spacing commits under movement, crossfade disabled) is
measured, and the default config already contains the mitigation. The
positive-shift HF increase is a genuine, previously-unreported behaviour
(carried to the decision record as a candidate for a future pre-read
anti-alias option).

## 8.2 Single-voice dual-mono stereo offset

* [OBS] Native `MultichannelShifter`, identical L/R input, 1 voice, −12 st:
  **bit-identical L/R output** (maxdiff 0, `r23_artifacts.json` case
  `dual_mono_native`).
* [OBS] **The real VST3 module path** (dlopen → factory → processor →
  `process()` with the SDK's parameter-queue segmentation):
  `process_stereo_bitexact` PASS, `stereo_R_max_abs_diff = 0` (12/12 cases,
  `logs/r23_vst3_module_integration.json`).
* [DER] Combined with the R21 matrix (classes (1)–(4), 0 violations at the
  native level), the offset CANNOT originate in the plugin for bit-identical
  L/R input at either level. The R21 class (4) explanation stands: any
  L/R input difference (even 1 sample of inter-channel skew from the source
  material or the DAW's routing) diverges the independent per-channel
  trackers (measured −23 dB RMS at a 1-sample offset).
* [UNK→DAW] The remaining possibility (host-side channel handling) is
  outside this sandbox. Test protocol for the user: feed the plugin the SAME
  mono file hard-panned to both channels; a residual offset then indicates
  host routing, not the plugin.

## 8.3 Silent or stuck processing while the DAW stays responsive

* [OBS] **REPRODUCED DEFECT (pre-fix)**: NaN or Inf input propagated through
  the engine boundary into the ring/feedback path — the output stayed
  non-finite for the ring's memory (~the delay range) before the clean
  signal flushed it (`nan_input_then_clean` / `inf_input_then_clean`,
  finite=NO in the pre-fix battery run). A DAW fed non-finite samples by an
  upstream plugin would see garbage/stuck output from PitchLab exactly in
  this window.
* [OBS] **FIXED (Gate 8 recorded engine delta)**: a non-finite input guard
  at the `process()` boundary (0.0f substitution) — output all-finite,
  recovery immediate (`r23_integration_check.json`, finite=yes,
  tail-rms 0.395). Bit-exact for finite input (100/100 suite).
* [OBS] Denormal-only input (1e-300): estimates correctly zeroed by the
  detector's peak gate, output finite, full recovery after clean input.
* [OBS] High feedback (0.95) + input stop: bounded exponential decay, no
  divergence, no stuck state.
* [OBS] Alternating 32/1024 block sizes + zero-length calls: state machine
  correct, no stuck condition; all-silence input → honest silence.
* [DER] The engine has no other silent/stuck mechanism on the measured
  path: the detector's peak gate (1e-6), the [f0_min, 1200] validity gate
  and the spacing accept gate all degrade to "hold last state", which
  remains renderable. The remaining [UNK] is host-specific event delivery
  (the wrapper's queue handling is covered by the 12/12 module tests incl.
  `param_queue_events` and `midblock_event_effect_maxdiff`).

## 8.4 No audible/CPU differences between the R21 detector/interpolation builds

* [OBS] **CPU differences are large and real**: engine total cost at
  48 kHz — FullRate 1387 ns/sample, Decimated 361, Hybrid 363 (≈3.8×);
  at 96 kHz 5128 vs 360 vs 393 (14×); at 192 kHz **20938 vs 361 vs 507
  (58×)** (`r23_detector_compare.json`). The compile-time variants execute
  measurably different code paths (estimates differ in the third decimal;
  interpolation outputs differ by up to 1.8e-3 between kernels).
* [OBS] **Why they sound the same**: on steady tonal material the three
  detectors agree to ~0.02 cents median / ≤3 cents p95, and the four
  interpolation kernels agree within ~1e-3 amplitude — below audibility on
  band-limited program material. Audible divergence appears on vibrato
  (p95 26 vs 35 cents — still small), transients, and very high rates
  (FullRate's cost), not on sustained notes. [DER] The user's observation
  is therefore CONSISTENT with correct builds — the variants differ
  measurably but not musically on typical material; the audible
  differentiators are CPU headroom at ≥96 kHz and edge-case tracking, not
  tone.

## Summary table

| Symptom | Reproduced? | Root cause | Status |
|---|---|---|---|
| HF crackle, negative movement | mechanism yes, default no | spacing commits under estimate motion, crossfade disabled | default mitigated; recorded |
| HF residue on +shifts | YES (new) | no pre-read anti-alias for r > 1 | carried to decision record |
| Dual-mono stereo offset | NO (native + VST3 module) | requires non-identical L/R input (R21 class 4) | not a plugin defect; DAW-level test protocol given |
| Silent/stuck | YES (NaN/Inf input) | non-finite input propagation | **FIXED (recorded delta)** |
| Builds sound identical | n/a | variants agree within audibility on steady material; CPU differs 3.8–58× | explained (DER) |
