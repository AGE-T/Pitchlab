# DELIVERY — Round 22 (PitchLab Harmonizer)

**Evidence archive:** `Octavox_RE_evidence_round22.zip` (manifest
`checksums.txt` in the zip root, regenerated from the actual tree; the
archive's own SHA-256 is recorded in the sidecar
`Octavox_RE_evidence_round22.zip.sha256` and on the delivery dashboard —
a zip cannot contain its own hash).

**Windows delivery:** **DEFERRED this round.** The round started from a full
sandbox-environment rollback: the R21 Windows three-variant kit
(`PitchLab_R21_Windows_x64_ThreeVariants.zip`) was never published to the
public area and is gone with the old environment, and the cross-build
toolchain (Zig / llvm-mingw) is no longer installed on the rolled-back host.
The Linux core, the VST3 wrapper sources (unchanged this round), and the
full verification below are unaffected; the Windows rebuild is the first
item of the next round. See `docs/OCTAVOX_ROUND22_FINDINGS.md` §0.

## Provenance (recovery round)

The R21 baseline was restored from the round-21 evidence archive supplied
back by the project owner before extraction; SHA-256
`c77dc1e3469e351b6909c145c13c3084a9e72621bd8633f526b52a086ced8ca9`
(verified against its sidecar), and the archive's internal manifest was
re-verified file-by-file on the extracted tree (exact file count, zero
stale, zero missing, zero unlisted). `evidence/analysis_round21/` in this
archive is the byte-identical copy from that zip.

## Contents delta vs round 21

* **New (P1 — the Minimum Pitch signal path):** the UNIFIED spacing-accept
  gate `max(fmin, f0_min)` in `native/src/dsp.cpp` + the bit-exactness proof
  (dsp.hpp; provably dead configuration for `fmin <= f0_min` — the default
  is unchanged), the four diagnostics (`spacingGateHz`,
  `lastEstimateSpacingAccepted`, `spacingHistoryCount`,
  `spacingTargetSamples`), 4 new unit cases (suite 96 → 100),
  `native/cli/r22_minpitch.cpp` (the 10-frequency × 4-floor × 3-shift ×
  3-detector = 360-cell battery with the verdict taxonomy), the
  instrument-correction record (two harness measurement defects found,
  root-caused and fixed BEFORE delivery — findings §1.7),
  `docs/OCTAVOX_ROUND22_FINDINGS.md`.
* **New evidence:** `evidence/analysis_round22/` (r22_minpitch.json — the
  fixed instrument's run; logs/r22_core_suite.txt — the 100-case suite log;
  logs/r22_minpitch_stdout.txt — the battery stdout).
* **Unchanged:** every round-21 and earlier file (the manifest diff vs the
  R21 archive shows only the R22 additions + the four intentionally
  modified R22 files: `native/{CMakeLists.txt,
  include/octavox/dsp.hpp, src/dsp.cpp, tests/test_main.cpp}` + the new CLI;
  version string 16.1.0 → 16.2.0).

## Verification (fresh extraction)

1. Unzip; verify the manifest: every file's SHA-256 matches checksums.txt
   (exact file count, zero stale, zero missing, zero unlisted).
2. Linux build: `cmake -DCMAKE_BUILD_TYPE=Release .. && make octavox_tests`
   → **100/100** (all reference hashes unchanged — the unified gate is
   provably output-neutral at every historical configuration).
3. The battery: `./octavox_r22_minpitch --json r22_minpitch.json` → the
   360-cell verdict distribution (not_detectable 141 · detectable_only 18 ·
   below_audible_floor 31 · safely_transposable 170) — see the findings §1.4.

## Not included (with reasons)

* The Windows three-variant kit (toolchain lost in the environment rollback;
  deferred to the next round — see above).
* Genuine recorded audio (none may be supplied by us; the P2 real-recording
  battery at low floors is deferred).
* The original Eventide binary (never redistributed, never executed).
* The VST3 SDK (pinned-SHA acquisition via native/tools/get_vst3_sdk.sh).
* A GUI (excluded, per specification).
