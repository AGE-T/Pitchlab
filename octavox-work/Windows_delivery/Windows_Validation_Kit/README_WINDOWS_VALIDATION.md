# PitchLab Harmonizer — Windows x64 Validation Kit (Round 21)

**Contents:**
- `octavox_dual_nulltest.exe` — the deterministic A/B/null-test tool (loads
  TWO separately-built bundles, renders identical stimulus, reports the
  unity bit-identity and the non-unity difference; exit 0 on pass).
- `run_validation_lagrange6_balanced.cmd`, `run_validation_lagrange6_hybrid.cmd`,
  `run_validation_obskernel6_balanced.cmd` — run the null-test A/B pairs.

**The three builds** (in `../Builds/`):
| variant | bundle | detector | interpolation |
|---|---|---|---|
| PitchLab Harmonizer Lagrange6 Balanced | PitchLab_Lagrange6_Balanced.vst3 | Decimated | Lagrange6 |
| PitchLab Harmonizer Lagrange6 Hybrid | PitchLab_Lagrange6_Hybrid.vst3 | Hybrid | Lagrange6 |
| PitchLab Harmonizer ObsKernel6 Balanced | PitchLab_ObsKernel6_Balanced.vst3 | Decimated | ObsKernel6 |

The FullRate detector is a benchmark/offline mode (96.8% of CPU at 96 kHz) and
is NOT shipped as a recommended real-time build.

**Toolchain note (honesty):** the modules are cross-built with the Zig
toolchain (clang/LLVM, mingw-w64 headers, static libc++) — genuine PE32+ x64
DLLs with the MS x64 ABI, importing only Windows system DLLs
(KERNEL32/USER32/ole32/SHELL32/UCRT). R18-R20 used llvm-mingw; its
Linux-hosted builds are no longer published. Structural verification:
`r21_windows_verification.json` (PE, exports, FUIDs, identity strings,
moduleinfo, SHA-256). The same sources + variant defines are validated
end-to-end on Linux (21/21 wrapper tests, 12/12 integration, validator 47/47
per variant; see the round evidence).

**Usage (on Windows):**
```
octavox_dual_nulltest.exe --lagrange6 <path\PitchLab_Lagrange6_Balanced.vst3> --obskernel6 <path\PitchLab_ObsKernel6_Balanced.vst3>
```
or run one of the .cmd files (they locate the ../Builds bundles relative to
this kit).

**What the null test proves:** at unity transposition the two interpolation
variants are BIT-IDENTICAL (the parked integer-tap path); at non-unity they
genuinely differ (the different kernels engaged) — a build-configuration
defect would show as bit-identical non-unity output (exit 1).
