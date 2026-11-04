# PitchLab Harmonizer — Windows x64 Validation Kit (Round 22)

**Contents:**
- `octavox_dual_nulltest.exe` — the deterministic A/B/null-test tool (loads
  TWO separately-built bundles, renders identical stimulus, reports the
  unity bit-identity and the non-unity difference; exit 0 on pass).
- `run_validation_lagrange6_balanced.cmd`, `run_validation_lagrange6_hybrid.cmd`,
  `run_validation_obskernel6_balanced.cmd` — run the null-test A/B pairs.

**The four builds** (in `../Builds/`; the first three are the user-facing
variants, the fourth is the offline/benchmark build):
| variant | bundle | detector | interpolation |
|---|---|---|---|
| PitchLab Harmonizer Lagrange6 Balanced | PitchLab_Lagrange6_Balanced.vst3 | Decimated | Lagrange6 |
| PitchLab Harmonizer Lagrange6 Hybrid | PitchLab_Lagrange6_Hybrid.vst3 | Hybrid | Lagrange6 |
| PitchLab Harmonizer ObsKernel6 Balanced | PitchLab_ObsKernel6_Balanced.vst3 | Decimated | ObsKernel6 |
| PitchLab Harmonizer Lagrange6 FullRate | PitchLab_Lagrange6_FullRate.vst3 | FullRate | Lagrange6 |

The FullRate detector is a benchmark/offline mode (96.8% of CPU at 96 kHz) and
is NOT shipped as a recommended real-time build.

**Round 22 changes** (vs R21): the 59-parameter surface (38 legacy ids
unchanged + 21 harmony-mapper musical-model ids 38..58), the state-blob
VERSION 3 (v1/v2 blobs restore bit-neutrally with the harmony fields at
their defaults — migration tested), per-voice PAN (constant-power) and
per-voice FEEDBACK in the core, and the coherent minimum-frequency policy
(Config::f0_min end-to-end). See `PITCHLAB_ROUND22_FINDINGS.md`.

**Toolchain note (honesty):** the modules are cross-built with the Zig
toolchain 0.13.0 (clang/LLVM, mingw-w64 headers, static libc++) — genuine
PE32+ x64 DLLs with the MS x64 ABI, importing only Windows system DLLs
(KERNEL32/USER32/ole32/SHELL32/UCRT). R18-R20 used llvm-mingw; its
Linux-hosted builds are no longer published. Structural verification:
`r22_windows_verification.json` (PE, exports, FUIDs, identity strings,
moduleinfo, SHA-256). The same sources + variant defines are validated
end-to-end on Linux (27/27 wrapper tests, 12/12 integration, validator
47/47 per variant; see the round evidence).

**Usage (on Windows):** run the `.cmd` files (double-click or from cmd);
the tool prints the A/B comparison and exits 0 on pass. The `.pdb` files
are omitted from the delivery (size); the SHA-256 records live in the
verification JSON.

The original Eventide Octavox binary is not part of this delivery and was
never executed.
