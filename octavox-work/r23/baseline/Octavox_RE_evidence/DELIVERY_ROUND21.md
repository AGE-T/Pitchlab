# DELIVERY — Round 21 (PitchLab Harmonizer)

**Evidence archive:** `Octavox_RE_evidence_round21.zip` (see checksums.txt in
the zip root; the archive's own SHA-256 is recorded in the worklog + the
dashboard sidecar — a zip cannot contain its own hash).

**Windows delivery:** `PitchLab_R21_Windows_x64_ThreeVariants.zip` — three
user-facing variants (Lagrange6 Balanced / Lagrange6 Hybrid / ObsKernel6
Balanced) + the validation kit (octavox_dual_nulltest.exe, READMEs, .cmd
runners). Toolchain deviation: Zig cross-build (llvm-mingw's Linux-hosted
builds are no longer published) — genuine PE32+ x64 modules; see
PITCHLAB_ROUND21_FINDINGS.md.

## Contents delta vs round 20

* **New (P0/P1):** the PitchLab identity (octavox_build_config.h /
  octavox_ids.h rewrite; CMake two-axis variant matrix; the wrapper's
  variant-authoritative restore comments), the approved parameter-title
  corrections + 38 static help texts (octavox_params.cpp/h), wrapper test
  case 21 (identity_variant_authority_r21), the dual-nulltest PitchLab paths,
  `Config::f0_min` (dsp.hpp/cpp — default bit-exact), wav_io float32 support,
  the corrected r20_dualmono.cpp comments.
* **New tools:** cli/r21_realaudio.cpp, cli/r21_fmin.cpp, cli/r21_stereo.cpp,
  cli/r21_harmony.cpp.
* **New (P2):** include/octavox/harmony.hpp + src/harmony.cpp (the independent
  harmony mapper), docs/R21_HARMONY_MAPPER_SPEC.md,
  docs/R21_FEATURE_RECONSTRUCTION_MATRIX.md,
  docs/R21_DUALMONO_STEREO_CORRECTION.md, the R20 parameter-semantics R21
  addendum, the R20 findings correction banner.
* **New evidence:** evidence/analysis_round21/ (the static findings + the
  parameter-string scans + r21_harmony_vectors.json + its generator + log +
  r21_stereo.json + r21_fmin.json + r21_realaudio_demo.json + the demo WAVs +
  trajectories + all validation logs + the Windows verification JSON).
* **Manifest reconciliation (P3):** the R20 zip's manifest had 12 stale
  entries (the R20 source modifications), 1 missing file (poc/poc_run.log —
  recorded as DELETED, NOT restored) and 50 unlisted files (the R20
  additions). This round's manifest is REGENERATED from the actual tree; the
  reconciliation is recorded in checksums.txt's R21 section.

## Verification (fresh extraction)

1. Unzip; verify the manifest: every file's SHA-256 matches checksums.txt
   (exact file count, zero stale, zero missing, zero unlisted).
2. Linux build: `cmake -DCMAKE_BUILD_TYPE=Release .. && make octavox_tests`
   → 96/96 (all reference hashes unchanged — f0_min default is bit-exact).
3. VST3 (per variant): wrapper 21/21, integration 12/12, validator 47/47
   (build commands in native/vst3/VST3_WRAPPER.md; the four variants'
   definitions in PITCHLAB_ROUND21_FINDINGS.md).
4. Harmony: `octavox_r21_harmony --verify
   ../../evidence/analysis_round21/r21_harmony_vectors.json` → 545/545,
   worst deviation 0.0.
5. Stereo: `octavox_r21_stereo --json ...` → "dual-mono transparency
   violations: 0".
6. Windows: the kit's .cmd runners (or the structural verification JSON).

## Not included (with reasons)

* Genuine recorded audio (none may be supplied by us — the harness accepts
  user WAVs; the demo WAVs are honestly labelled synthetic).
* The original Eventide binary (never redistributed, never executed).
* The VST3 SDK (pinned-SHA acquisition via native/tools/get_vst3_sdk.sh).
* A GUI (excluded this round, per specification).
