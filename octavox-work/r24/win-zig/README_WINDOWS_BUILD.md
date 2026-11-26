# PitchLab R24-Windows — Build Instructions

This package supports two build paths for the two delivered variants:

| variant | interpolation | detector | CMake cache vars |
|---|---|---|---|
| PitchLab Harmonizer Balanced | Lagrange6 | Decimated YIN | `-DOCTAVOX_VST3_INTERPOLATION=LAGRANGE6 -DOCTAVOX_VST3_DETECTOR=DECIMATED` |
| PitchLab Harmonizer FullRate | Lagrange6 | FullRate YIN | `-DOCTAVOX_VST3_INTERPOLATION=LAGRANGE6 -DOCTAVOX_VST3_DETECTOR=FULLRATE` |

Both variants: SIMD **Auto by default** (AVX2 when CPU + OS YMM state
support it, silent Scalar fallback otherwise; FMA opt-in, never selected
by Auto), the 38-parameter host surface, and the frozen v2 state blob.

## 0. Acquire the VST3 SDK (pinned)

The Steinberg VST3 SDK (dual license) is **not** redistributed with this
package. Acquire the pinned revision:

```sh
cd source/tools
./get_vst3_sdk.sh <target_dir>     # fetches + verifies v3.7.12_build_20
                                    # at the pinned component SHAs
```

Component pins: main `cc2adc90…`, base `f0998e7b…`, cmake `2022b265…`,
pluginterfaces `151ecde4…`, public.sdk `3fce096d…` (full SHAs in
`source/vst3/VST3_WRAPPER.md`).

## 1A. Native Windows build — Visual Studio 2022 / MSVC (RECOMMENDED)

This is the toolchain the delivery brief prefers. It was **not** used for
the shipped binaries (see §2 and the delivery report: the build sandbox is
Linux; native MSVC was BLOCKED there) — but the sources are MSVC-ready and
this is the path to a native rebuild:

Requirements: Visual Studio 2022, MSVC x64 toolset, Windows 10/11 SDK,
CMake ≥ 3.19, generator "Visual Studio 17 2022" (or Ninja with the MSVC
environment), the pinned VST3 SDK.

```bat
:: Balanced
cmake -S source\vst3 -B build_bal -G "Visual Studio 17 2022" -A x64 ^
      -DVST3_SDK_ROOT=<path-to>\vst3sdk ^
      -DOCTAVOX_VST3_INTERPOLATION=LAGRANGE6 -DOCTAVOX_VST3_DETECTOR=DECIMATED
cmake --build build_bal --config Release

:: FullRate
cmake -S source\vst3 -B build_full -G "Visual Studio 17 2022" -A x64 ^
      -DVST3_SDK_ROOT=<path-to>\vst3sdk ^
      -DOCTAVOX_VST3_INTERPOLATION=LAGRANGE6 -DOCTAVOX_VST3_DETECTOR=FULLRATE
cmake --build build_full --config Release
```

Outputs: `build_bal\VST3\Release\PitchLab_Lagrange6_Balanced.vst3` and
`build_full\VST3\Release\PitchLab_Lagrange6_FullRate.vst3` (bundle layout
`Contents\x86_64-win\<name>.vst3` + `Contents\Resources\moduleinfo.json`,
generated natively by the SDK packaging).

MSVC notes (already wired in the CMake files):
- per-TU ISA: `simd_avx2.cpp` and `simd_fma.cpp` compile with
  `/arch:AVX2` via `set_source_files_properties` — never globally;
- CPU detection uses the `<intrin.h>` `__cpuid/__cpuidex/_xgetbv` path
  (`src/simd_cpuid_explicit.h`);
- `-Wall/-Wextra` blocks are MSVC-guarded (cl gets its own warning set);
- the state blob and parameter surface are compiler-independent.

Run on the same machine (native, no bridges needed):
```bat
build_bal\bin\Release\octavox_vst3_wrapper_tests.exe
build_bal\bin\Release\octavox_module_integration_tests.exe
build_bal\bin\Release\validator.exe build_bal\VST3\Release\PitchLab_Lagrange6_Balanced.vst3
```
(repeat with `build_full` / the FullRate bundle; expected: wrapper 21/21,
integration 12/12, validator 47/47 — see `evidence/vst3_validation/`).

## 1B. Native Windows test suite (core, MSVC)

```bat
cmake -S source -B build_core -G "Visual Studio 17 2022" -A x64
cmake --build build_core --config Release
build_core\Release\octavox_tests.exe --json results.json
```
Expected: **110/110** (includes the eight historical hash gates, the R24
SIMD battery, `simd_default_auto_neutrality`). Note: the ASan/UBSan suite
(110/110 on Linux/GCC) requires clang-cl or a GCC-family sanitizer
toolchain on Windows — the shipped ASan evidence is Linux-side.

## 2. Cross build (the method used for the shipped binaries)

The shipped binaries were cross-built on the Linux sandbox with Zig 0.13.0
(`zig c++ -target x86_64-windows-gnu`: clang/LLVM frontend, lld,
mingw-w64 headers, static libc++) — genuine PE32+ x64 modules with the MS
x64 ABI importing only Windows system DLLs. This is the recorded toolchain
deviation of the accepted R21/R22 Windows deliveries. Reproduction on a
Linux host:

```sh
# toolchain wrappers (this directory)
./win-zig/zig-cc.sh --version        # -> zig 0.13.0

cmake -S source/vst3 -B build_win_bal -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DOCTAVOX_VST3_INTERPOLATION=LAGRANGE6 -DOCTAVOX_VST3_DETECTOR=DECIMATED \
      -DSMTG_RUN_VST_VALIDATOR=OFF -DSMTG_CREATE_PLUGIN_LINK=OFF \
      -DCMAKE_TOOLCHAIN_FILE=<abs>/win-zig/zig-win-toolchain.cmake \
      -DVST3_SDK_ROOT=<abs>/vst3sdk
cmake --build build_win_bal --target moduleinfotool
cp win-zig/moduleinfotool-host.sh build_win_bal/bin/moduleinfotool.exe
chmod +x build_win_bal/bin/moduleinfotool.exe
PATH="<abs>/win-zig/host-shims:$PATH" cmake --build build_win_bal -j8
```

Host-side bridges (documented deviations, only needed for cross builds):
- `moduleinfotool-host.sh` — planted at `<build>/bin/moduleinfotool.exe`:
  the SDK post-build invokes the PE moduleinfotool on the Linux host;
  the wrapper copies the moduleinfo.json from the **identical-variant
  Linux twin build** (compile-time factory metadata). Requires the Linux
  twin built first (same command minus the toolchain file).
- `host-shims/attrib` — no-op stand-in for the Windows `attrib +s`
  (Explorer decoration only).
- `-DSMTG_RUN_VST_VALIDATOR=OFF` — the validator is a PE exe; validation
  runs on the Linux twins instead.

## 3. What to expect after any rebuild

- Two bundles with **distinct class IDs**:
  Balanced `6F4E62589A3142E18F3C5A971D2B4E60` (preserved lineage),
  FullRate `C13E1BB55BFB0598F25DDD4A52B0E817`;
- wrapper 21/21, module integration 12/12, validator 47/47 (per variant);
- core suite 110/110 (Release; ASan/UBSan 110/110 on the GCC path);
- the dual-module null test PASS (Balanced + FullRate in one process:
  unity bit-identical, non-unity different).

**Byte-identical rebuilds are NOT claimed.** The shipped binaries embed
toolchain- and path-dependent artifacts (PE timestamps/checksums, PDB
paths, link order). Cross-check by SHA-256 against `SHA256SUMS` for the
shipped files; a rebuild with a different compiler (MSVC vs zig) or
different paths WILL differ bytewise while remaining functionally
identical (same sources, same defines, same class IDs, same validator
results). This is documented, not silent.
