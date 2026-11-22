# PitchLab R24-Windows VST3 Research Package

**The two Windows x64 VST3 plugins for PitchLab Harmonizer**, built from
the corrected R24-SIMD source, with the research record, the full source,
build configuration, and validation evidence.

| delivered plugin | configuration | class ID |
|---|---|---|
| `plugins/PitchLab_Harmonizer_Balanced.vst3` | Lagrange6 + Decimated YIN; SIMD Auto (AVX2 + Scalar fallback; FMA opt-in) | `6F4E62589A3142E18F3C5A971D2B4E60` (preserved lineage) |
| `plugins/PitchLab_Harmonizer_FullRate.vst3` | Lagrange6 + FullRate YIN; SIMD Auto (as above) | `C13E1BB55BFB0598F25DDD4A52B0E817` (distinct) |

In-DAW display names: "PitchLab Harmonizer Lagrange6 Balanced" /
"PitchLab Harmonizer Lagrange6 FullRate". Both share the 38-parameter host
surface and the frozen v2 state blob (older sessions restore).

**Which one should I use?** Balanced is the realtime production candidate.
FullRate is the high-cost detector reference for research/comparison:
at 96 kHz stereo 8-voice it measured ~37% of one core with the AVX2
kernels (~114% scalar) — substantially more expensive than Balanced and
not automatically recommended for realtime use.

**Quick verification on your Windows machine:** run
`Windows_Validation_Kit/run_validation_balanced_vs_fullrate.cmd`
(loads both plugins into one process; exit 0 = PASS; see the kit README).
Bundle checksums: `SHA256SUMS`.

## Honest scope statement (read this first)

* The build machine is a **Linux sandbox** — Visual Studio 2022 / MSVC /
  the Windows SDK are not available there. The shipped binaries are
  **cross-built with Zig 0.13.0** (`x86_64-windows-gnu`: genuine PE32+ x64
  DLLs, Microsoft x64 ABI, static libc++, importing only Windows system
  DLLs). This is the same recorded toolchain deviation behind the accepted
  R21/R22 Windows x64 deliveries. Native **MSVC rebuild instructions are
  included** (`build_configuration/README_WINDOWS_BUILD.md`) — all sources
  are MSVC-ready (per-TU `/arch:AVX2`, `<intrin.h>` detection).
* The identical sources + variant defines were validated **end-to-end on
  Linux**: 110/110 suite (Release + ASan/UBSan), wrapper 21/21, module
  integration 12/12, Steinberg validator 47/47 (per variant), dual-module
  same-process test PASS.
* The Windows binaries are **structurally verified** (PE32+ x86-64,
  GetPluginFactory export, import allow-list, both class IDs in COM
  layout, moduleinfo identity, machine-code ISA isolation — see
  `evidence/windows_build/`).
* **Real DAW verification was NOT performed** (no Windows host exists in
  the build sandbox). The Validation Kit + a recommended check-list are
  provided. The full four-state classification is in
  `DELIVERY_WINDOWS_R24.md`.

## Package tree

```
plugins/                       the two Windows x64 VST3 bundles (+ .pdb)
Windows_Validation_Kit/        dual-module null test (.exe + .cmd + README)
research/
  R23/                         the complete R23 YIN research documentation
                               (16 documents + the R23 round record)
  R24/                         R24 SIMD findings + the measured decision
                               record (Auto = bit-exact AVX2; FMA opt-in)
source/                        the corrected R24 source tree — ONE
                               reproducible project:
                                 CMakeLists.txt, include/, src/, cli/  (native core)
                                 tests/  (the 110-case suite + harness)
                                 bench/  (the benchmark harness)
                                 vst3/   (the VST3 wrapper + its tests,
                                         incl. VST3_WRAPPER.md)
                               (source layout note: the delivery-tree
                               sketch lists native/vst3/tests/benchmarks
                               as siblings; in the actual project they
                               live in this single tree so the wrapper's
                               relative source references keep working —
                               'native' = include+src+cli, 'tests' =
                               tests/, 'benchmarks' = bench/, 'vst3' =
                               vst3/. Nothing was duplicated or moved.)
evidence/
  windows_build/               environment record, per-TU ISA flags,
                               machine-code verification, structural
                               verification JSON
  vst3_validation/             wrapper/integration/validator logs (both
                               variants) + the dual same-process test
  regression/                  the 110/110 Release + ASan/UBSan suite
                               records + the explicit-CPUID cross-check
  simd/                        SIMD-specific evidence (CPUID cross-check,
                               ISA machine-code isolation)
  benchmarks/                  the R24 stage/kernel/engine benchmark
                               records (scalar vs AVX2 vs FMA)
build_configuration/           the Windows build instructions (native
                               MSVC + the zig cross path) + the pinned
                               toolchain files (win-zig/)
README.md                      this file
DELIVERY_WINDOWS_R24.md        the full delivery report (four-state
                               classification, acceptance table)
SHA256SUMS                     checksums of every delivered file
```

## Reproducing the build

See `build_configuration/README_WINDOWS_BUILD.md` (both paths, the pinned
VST3 SDK acquisition script `source/tools/get_vst3_sdk.sh`, expected
gates). Byte-identical rebuilds are **not claimed** (toolchain- and
path-dependent PE artifacts); functional equivalence is gated by the test
suite, the class IDs and the validator.

## Provenance

Our own independent harmonizer implementation around the octavox native
DSP core. NOT a reproduction of the original Eventide Octavox plug-in; no
target code; the original binary was never executed. No "Eventide"
branding anywhere in the product surface. Synthetic test corpora only,
labelled as such.
