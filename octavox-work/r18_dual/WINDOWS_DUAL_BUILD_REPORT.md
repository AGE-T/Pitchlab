# OCTAVOX R18-WDB — WINDOWS DUAL VST3 BUILD REPORT
## Lagrange6 vs ObsKernel6 — two simultaneously loadable Windows x64 VST3 plugins

Generated: 2026-10-30 (Europe/Budapest), in the development sandbox
(Debian GNU/Linux 13 x86_64, 2 cores). Everything stated below was executed
and captured in this environment; the evidence files are listed in §13.

---

## 0. What this delivery is — and is not

**Is:** two independent, simultaneously loadable Windows x64 VST3 plugins —
`OctavoxRE Lagrange6` and `OctavoxRE ObsKernel6` — built from the exact
Round-18 evidence source with a new, explicitly configured
compile-time interpolation selection. Both are OUR OWN independent
harmonizer implementation around the octavox native DSP core.

**Is not:** a reproduction of the original Eventide Octavox engine. The
ObsKernel6 variant uses the static 256×6 interpolation kernel **observed**
in the original binary's `.rdata` as one comparison mode inside our
independent DSP engine; nothing else about the original is reproduced, and
the original binary was never executed (the standing rule since round 1).
No DAW compatibility is claimed from any test executed here (§10, §12).

Three evidence classes remain strictly separated, per the standing rules:
(1) observations of the original target (static only), (2) measurements of
our own independent DSP/VST3 implementation (this report), (3) runtime
observations of the original target (none exist; the target never ran).

---

## 1. Source verification (task §1)

| check | result |
|---|---|
| archive | `Octavox_RE_evidence_round18.zip` |
| expected SHA-256 | `35a855e77925e51027416b5d2c68ab5a4fc7f0ceb6eb9bea762864c0af02187a` |
| actual SHA-256 | **identical** (verified before extraction) |
| extraction | fresh, to `r18_dual/source/` (archive root `checksums.txt` + `Octavox_RE_evidence/`) |
| checksum manifest | `sha256sum -c checksums.txt` (the documented archive-root command): **398/398 OK, 0 failed** |
| original archive | **unchanged** (read-only use; not rebuilt, not repackaged) |
| pinned VST3 SDK | `get_vst3_sdk.sh --verify` — main `cc2adc90…`, base/cmake/pluginterfaces/public.sdk gitlinks all **match the pins** (v3.7.12_build_20); the SDK tree was **not modified** |

Round 17 was **not** used as the baseline: everything builds from the
verified Round-18 extraction.

---

## 2. Build configuration (task §2) — the central variant helper

The DSP core already provides `Interpolation::Lagrange6`, `Lagrange6Q` and
`ObsKernel6`; the core default was and **remains** `Lagrange6`
(`native/include/octavox/dsp.hpp` is byte-unchanged — see §9's patch).

New: an explicit **compile-time configuration for the VST3 wrapper default
interpolation**, centralized in ONE helper consumed everywhere:

* **`native/vst3/octavox_build_config.h` (new)** — the single place the
  variant is defined. CMake translates the cache variable
  `OCTAVOX_VST3_INTERPOLATION` (`LAGRANGE6` | `LAGRANGE6Q` | `OBSKERNEL6`,
  default `LAGRANGE6`) into exactly one compile definition
  (`OCTAVOX_VST3_INTERP_*`), visible to every wrapper consumer. The header
  provides:
  * `build::kDefaultInterpolation` — the wrapper's compile-time kernel;
  * `build::referenceConfig()` / `defaultConfig()` — the engine
    configuration (core defaults + the variant kernel) used by **processor
    construction** (`cfg_ = defaultConfig()`), **state restore** (see
    below), **wrapper reference tests** and **compiled-module integration
    tests**;
  * the per-variant **identities** (FUIDs, names, bundle names — §3).
* **`native/vst3/CMakeLists.txt`** — validates the option, defines
  `OCTAVOX_VST3_INTERP_<VARIANT>=1` on the module + both test targets, and
  selects the bundle name (`OctavoxRE_<Variant>`).
* **State restore & initialization paths** — the processor constructor uses
  `defaultConfig()`; `setState` restores the blob verbatim **except** the
  interpolation field, which is forced to the module's compile-time
  selection (variant authoritativeness: a state saved by the other variant
  — or by an R16–R18 `OctavoxNative` module — still restores every other
  field, but the kernel stays this build's; the variant difference is a
  compile-time property, never a host-visible or state-switchable
  parameter).
* **No new host parameter.** Both variants preserve the existing
  38-parameter host surface exactly (Bypass, Pitch, Voices, Spread,
  Feedback, Wet/Dry + 4×8 per-voice), verified by the wrapper tests and the
  integration harness in both variants (15/15 and 12/12 respectively).
* The ObsKernel6 variant's integration reference **explicitly uses
  ObsKernel6** (`defaultConfig()` is variant-aware): an ObsKernel6 module
  is never compared against a Lagrange6 reference. Conversely-verified:
  the Lagrange6 variant's integration run compares against the Lagrange6
  reference.

One shared source tree (`r18_dual/source/Octavox_RE_evidence/native`) is
built in separate clean directories per variant — Linux validation builds
and Windows builds alike (§5).

---

## 3. Unique VST3 identities (task §3)

| | Lagrange6 variant | ObsKernel6 variant |
|---|---|---|
| Display name | `OctavoxRE Lagrange6` | `OctavoxRE ObsKernel6` |
| Bundle/package name | `OctavoxRE_Lagrange6` | `OctavoxRE_ObsKernel6` |
| Processor class FUID | `{6F4E6258-9A31-42E1-8F3C-5A971D2B4E60}` | `{E3E7FA70-6A38-0D35-B05F-64B91F1E2689}` |
| Factory UID | `{7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44}` | `{3D4CDBC6-ACAC-BB09-077F-D5B80BD2F550}` |
| Vendor | `Octavox RE Project` (consistent) | `Octavox RE Project` (consistent) |
| Subcategory / version | `Fx|Pitch Shift` / 1.0.0 | `Fx|Pitch Shift` / 1.0.0 |
| Default interpolation | Lagrange6 (2048-phase 6-tap) | ObsKernel6 (observed 256×6 kernel) |

* The two variants **never share a class identity** — distinct processor
  FUIDs, factory UIDs, bundle names and display names — so both load
  simultaneously in one DAW as distinct plugins. The display names and
  bundle names make the interpolation choice obvious.
* The Lagrange6 variant keeps the R16–R18 lineage FUID (the renamed
  `OctavoxNative` class; state blobs from those modules restore into it).
* The ObsKernel6 FUIDs were **deterministically minted** this round
  (SHA-256 of a fixed namespace string, first 16 bytes, grouped per the
  existing `DECLARE_UID` convention — full strings in the header and the
  manifest); the derivation is reproducible.
* Identity verification of the shipped binaries: the `PClassInfo` CID in
  each Windows module was byte-verified to equal its FUID in the **Windows
  COM GUID layout** (Data1/Data2/Data3 little-endian + Data4 raw — the
  pinned SDK's `COM_COMPATIBLE` `INLINE_UID` branch; the same logical UID
  as the Linux builds' UUID-order layout), the display/vendor/subcategory/
  SDK-version strings were byte-verified present, and **no
  cross-contamination** exists (each binary contains only its own class
  ID). See §8.

---

## 4. Toolchain — the honest MSVC note (task §4)

The task asks for *MSVC x64 Release*. This build host is a Linux sandbox
with **no Windows runtime and no MSVC**; installing MSVC is impossible
without Windows (and wine — see below — is blocked). The Windows builds
therefore use:

* **LLVM/Clang 23.1.4** via **llvm-mingw** (release `20261020`,
  ucrt-x86_64 host build; SHA-256 of the toolchain tarball
  `55064b60b991a1e1e8090b8c2f84e82b22cfe5903d12c159d65064cf5e8ae335`),
  targeting `x86_64-w64-mingw32`, linker **LLD**.
* The output is **genuine native Windows PE32+ x86-64 modules** using the
  Microsoft x64 ABI (mingw-w64 follows the MS x64 calling convention; the
  VST3 host boundary is pure COM-style virtual interfaces, ABI-compatible
  with MSVC-built hosts).
* The C++ runtime (libc++/libc++abi/winpthread) is linked **statically**;
  the module imports only Windows system DLLs (`KERNEL32.dll`, `USER32.dll`,
  `ole32.dll`) plus the **UCRT** via standard `api-ms-win-crt-*` API sets —
  exactly the dependency shape of an MSVC `/MD` build; no
  `libstdc++-6.dll`/`libc++.dll`/`libwinpthread-1.dll` runtime requirements.
* `-Wl,/Brepro` gives a deterministic PE timestamp → **byte-reproducible
  modules** (§8).

**DSP logic is untouched:** the intended difference between the variants is
only the interpolation method. The pitch tracker, delay line, feedback,
voice processing, parameter defaults and every other DSP behaviour are the
same source compiled per variant (the whole modified-file list is §9 — no
DSP core file is touched).

**Why the Windows-side tests could not run here (diagnosed, not guessed):**
the sandbox's seccomp policy terminates wine during Windows PE startup —
traced with a purpose-built ptrace tracer to a `SIGSYS` (seccomp
`KILL_PROCESS`) on `set_thread_area` (syscall 243) during wine's thread
setup, while the identical syscall from probe processes survives (the
filter is not a simple per-syscall rule); `wine --version` itself works.
The kill is uncatchable (no signal-delivery stop). Rather than attempt to
defeat a deliberate sandbox control, the Windows-side runtime gates are
delivered as **compiled Windows executables** (the
`Windows_Validation_Kit`) to be run on a real Windows x64 machine — the
same user-managed Windows-runtime workflow every prior round used for the
original target. What WAS executed here: the full functional validation on
**Linux builds of the identical source + configuration** (§6) and the
structural verification of the Windows binaries (§8).

---

## 5. Complete build commands

The full chain is scripted in
`native/tools/windows_dual_build.sh` (new this round; executed end to end —
"ALL GATES PASSED (total wall 112 s)"). Core commands:

```bash
# toolchain (Linux host): cmake 4.4.4 (pip); llvm-mingw at /home/z/tools/llvm-mingw
# 1. pinned SDK (verified): native/tools/get_vst3_sdk.sh --verify <sdk>

# 2. native core (the shared DSP gates -- unchanged):
cmake -S native -B build_lin_core -DCMAKE_BUILD_TYPE=Release
cmake --build build_lin_core -j2
build_lin_core/octavox_tests                          # 85/85
build_lin_core/octavox_r15refcap                      # 4/4 vs r15_reference_hashes.txt
build_lin_core/octavox_r15refcap --interpolation obskernel6   # the new measurements

# 3. LINUX validation builds (per variant; identical source + config as Windows):
cd native/vst3
cmake -S . -B ../../build_lin_lagrange6  -DCMAKE_BUILD_TYPE=Release \
    -DVST3_SDK_ROOT=<sdk> -DOCTAVOX_VST3_INTERPOLATION=LAGRANGE6
cmake --build ../../build_lin_lagrange6 -j2
../../build_lin_lagrange6/bin/Release/octavox_vst3_wrapper_tests        # 15/15
../../build_lin_lagrange6/bin/Release/octavox_module_integration_tests \
    --module ../../build_lin_lagrange6/VST3/Release/OctavoxRE_Lagrange6.vst3 \
    --json <out>.json                                                   # 12/12
../../build_lin_lagrange6/bin/Release/validator \
    ../../build_lin_lagrange6/VST3/Release/OctavoxRE_Lagrange6.vst3     # 47/47
#    (identically for OBSKERNEL6 / build_lin_obskernel6)

# 4. WINDOWS x64 cross-builds (per variant; task-named directories):
cmake -S . -B ../../build_lagrange6 \
    -DCMAKE_TOOLCHAIN_FILE=../../llvm-mingw-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVST3_SDK_ROOT=<sdk> \
    -DOCTAVOX_VST3_INTERPOLATION=LAGRANGE6 \
    -DSMTG_RUN_VST_VALIDATOR=OFF  -DSMTG_CREATE_MODULE_INFO=OFF \
    -DSMTG_CREATE_PLUGIN_LINK=OFF \
    -DCMAKE_MODULE_LINKER_FLAGS="-static -Wl,--no-undefined -Wl,/Brepro" \
    -DCMAKE_SHARED_LINKER_FLAGS="-static -Wl,--no-undefined -Wl,/Brepro" \
    -DCMAKE_EXE_LINKER_FLAGS="-static -Wl,/Brepro"
cmake --build ../../build_lagrange6 -j2          # module + test executables
cmake --build ../../build_lagrange6 --target validator
cmake --build ../../build_lagrange6 --target moduleinfotool
#    (identically for OBSKERNEL6 / build_obskernel6)
```

Cross-build platform notes (all resolved without touching the pinned SDK
tree — the SDK still verifies at its pins):

* the SDK's own `SMTG_PlatformToolset.cmake` has an explicit MinGW branch
  (`-Wno-multichar`, `--no-undefined`);
* the automatic POST_BUILD runs of `validator`/`moduleinfotool` (Windows
  PE executables) are disabled for the cross build (`SMTG_RUN_VST_VALIDATOR=OFF`,
  `SMTG_CREATE_MODULE_INFO=OFF`) because a Linux host cannot execute them;
  the validator instead runs on the **Linux builds of both variants**
  (§6) and both Windows tools are **delivered in the kit** for real-Windows
  runs; `moduleinfo.json` is copied into the Windows bundles from the
  equivalent Linux build where the pinned SDK's own `moduleinfotool`
  generated it from the real factory (the metadata is
  platform-independent; the kit's `moduleinfotool.exe` can regenerate and
  compare it natively — see the kit README);
* `SMTG_CREATE_PLUGIN_LINK=OFF` avoids the SDK's Windows-only
  `\Programs\Common\VST3` admin-folder logic;
* a documented **no-op `attrib` shim** stands in for the SDK's cosmetic
  `attrib +s` (Explorer folder-attribute) POST_BUILD command — the bundle's
  `desktop.ini` + `PlugIn.ico` are still shipped; on a real Windows/MSVC
  build the native attrib runs;
* the SDK's `threadchecker_win32.cpp` (`std::terminate` without
  `<exception>`) and `alignedalloc.h` (`_MSC_VER`-guarded Windows path →
  `std::aligned_alloc`, absent in llvm-mingw's libc++) are handled from our
  build scope via a single force-included compatibility header
  (`mingw_compat_shim.h`) — the aligned-alloc pairing caveat is analysed
  and documented as dormant in that header (the only SDK callers are the
  optional data-exchange feature, which our module/tests/validator never
  exercise);
* the SDK validator's Windows entry point is `wmain` → `-municode` link
  option applied to the validator target from our scope;
* Windows bundle layout follows the pinned SDK's own canonical output:
  the module binary is `Contents/x86_64-win/OctavoxRE_<Variant>.vst3` (a
  PE32+ DLL; `file(1)`-verified) — exactly what Steinberg's
  `SMTG_CREATE_BUNDLE_FOR_WINDOWS` machinery produces.

---

## 6. Validation results (task §5) — everything that EXECUTED

All runs Release; logs under `evidence/` (`wdb_*`).

| gate | Lagrange6 | ObsKernel6 |
|---|---|---|
| clean CMake configure + build | OK (Linux + Windows) | OK (Linux + Windows) |
| 15 VST3 wrapper tests | **15/15** | **15/15** |
| 12 compiled-module integration tests (module loaded through the SDK module loader; **variant-matched reference**) | **12/12**, mono/stereo **bit-exact** (max_abs_diff = 0) | **12/12**, mono/stereo **bit-exact** (max_abs_diff = 0) |
| Steinberg VST3 SDK validator | **47/47** | **47/47** |
| 38 parameter metadata checks | pass (in the wrapper-test suite) | pass |
| state save & restore (2nd-instance replay, bit-exact) | pass | pass |
| mono + stereo processing | pass (bit-exact vs core) | pass (bit-exact vs core) |
| parameter event handling (incl. mid-block events) | pass | pass |
| latency reporting | 2304 samples (= the core's `latencySamples()`), tail 15080 | same |
| module architecture verification | ALL CHECKS PASSED | ALL CHECKS PASSED |

**The shared 85-case native DSP acceptance suite: 85/85** (single process,
Release). The four **existing** neutrality reference hashes:
**4/4 bit-exact** vs `native/r15_reference_hashes.txt` — the historical
Lagrange6 gate is preserved unchanged (the generic DSP core default is
untouched and the default refcap behaviour is byte-identical).

**ObsKernel6 reference measurements (new, separate record — the task
explicitly required NOT to assume the same four hashes apply):**

| config | Lagrange6 (existing gate) | ObsKernel6 (new, `native/r18_obs_reference_hashes.txt`) |
|---|---|---|
| cfg1_default_unity_lcg_noise_b64_150_512 | `0x74318b06c182aacb` | `0x74318b06c182aacb` (**identical**) |
| cfg2_r700c_8v_spread_fb025_sine220 | `0x46a091e98f2a413a` | `0xa73fa640f0fcce9b` (**differs**) |
| cfg3_rm400c_4v_nospread_chirp200_800 | `0x9502cf8827e8782c` | `0x517af72e159b5a53` (**differs**) |
| cfg4_stereo_2ch_4v_unity_then_r400c | `0x6f7a2ce3ca310c25` | `0x87e676a3e9ecc108` (**differs**) |

The new record was captured twice with byte-identical output
(reproducibility) and is checked by the build script against
`native/r18_obs_reference_hashes.txt` (4/4). cfg1 is identical **by
mechanism**: at the default surface the engine takes the special-cased
legacy output path with the bit-neutral `x*1.0 == x` multiplication, which
does not traverse the fractional-delay kernel — so the kernel swap cannot
change its bits; cfg2/3/4 exercise real transposition reads and differ.
**Differences are reported, not gated away**: the Lagrange6 gate stands
exactly as before, the ObsKernel6 hashes are its own separate reference.

Where the Windows-side integration tests (task: "using the Windows
module-loading implementation") could not execute here (§4), the
**identical harness source compiled for Windows** — using
`module_win32.cpp` (`LoadLibraryA`/`GetProcAddress`) — is delivered in the
kit with a per-variant `run_validation.cmd`; on a real Windows machine it
loads the shipped `.vst3` bundle through the real Windows loading path and
checks all 12 cases (bit-exact vs the variant's reference).

---

## 7. Output files (task §6) and the user-side path mapping

Built and delivered (bundle trees complete with
`Contents/x86_64-win/<Name>.vst3` module, `Contents/Resources/moduleinfo.json`,
`PlugIn.ico`, `desktop.ini`):

```
out/Builds/OctavoxRE_R18_Windows_x64/Lagrange6/OctavoxRE_Lagrange6.vst3    # 952320 B, sha256 c5883597…
out/Builds/OctavoxRE_R18_Windows_x64/ObsKernel6/OctavoxRE_ObsKernel6.vst3  # 952320 B, sha256 c47b1010…
```

The delivery archive mirrors the task's Windows layout: extract it at
`F:\VST\OV_DIS\` to obtain exactly

```
F:\VST\OV_DIS\Builds\OctavoxRE_R18_Windows_x64\Lagrange6\OctavoxRE_Lagrange6.vst3
F:\VST\OV_DIS\Builds\OctavoxRE_R18_Windows_x64\ObsKernel6\OctavoxRE_ObsKernel6.vst3
```

Recorded per variant (full table in `WINDOWS_DUAL_BUILD_MANIFEST.txt`):
bundle contents, processor FUID, factory identity, display name, module
architecture (PE32+ x86-64 DLL), file size, SHA-256, default interpolation
mode. Windows-side reproducibility: a **second fresh build in a separate
clean directory produced a byte-identical module for both variants**
(`-Wl,/Brepro`; LLD deterministic timestamp).

---

## 8. Structural verification of the Windows binaries (executed here)

`verify_windows_modules.py` (shipped in
`R18_WDB_build_infrastructure/`) checked, per variant — **ALL PASSED**:

* PE32+ optional-header magic `0x20B`; machine `0x8664` (AMD64); DLL
  characteristic; 14 sections; `file(1)` corroborates "PE32+ executable
  for MS Windows … (DLL), x86-64";
* **`GetPluginFactory` exported** (the VST3 module entry point);
* imports restricted to the allow-list (`KERNEL32.dll`, `USER32.dll`,
  `ole32.dll`, `api-ms-win-crt-*` — no compiler-runtime DLL dependencies);
* identity strings present (display name, vendor, subcategory,
  `VST 3.7.12`);
* **PClassInfo CID == the variant's processor FUID in the Windows COM GUID
  byte layout** (the SDK's `COM_COMPATIBLE` encoding — same logical UID as
  the Linux builds);
* **no cross-variant FUID contamination**; the two modules are **not**
  byte-identical (the interpolation selection genuinely differs);
* bundle completeness incl. `moduleinfo.json` with the correct CID/name;
* sizes + SHA-256 (in the manifest).

Machine-readable result: `evidence/wdb_windows_verification.json`
(`total_failures: 0`).

---

## 9. Source changes (the exact Round-18 source + this round's delta)

The working tree is the fresh, verified Round-18 extraction plus these
changes (full unified diff: `R18_WDB_source_changes.patch`, applies with
`patch -p1` inside `Octavox_RE_evidence/`; pre-edit copies preserved in
`r18_dual/evidence/preedit/`):

| file | change |
|---|---|
| `native/vst3/octavox_build_config.h` | **new** — the central variant helper (kernel selection, identities, `referenceConfig()`) |
| `native/vst3/octavox_ids.h` | identities now variant-conditional via the helper (symbol names unchanged) |
| `native/vst3/octavox_params.h` | `defaultConfig()` = core defaults + variant kernel |
| `native/vst3/octavox_native_processor.cpp` | `setState` forces the compile-time kernel (variant authoritativeness) |
| `native/vst3/octavox_vst3_tests.cpp` | reference engines use `defaultConfig()` (variant-matched references) |
| `native/vst3/octavox_module_integration_tests.cpp` | variant-matched references; variant bundle-name lookup (Windows-separator-aware); variant/interpolation reporting in stdout+JSON |
| `native/vst3/CMakeLists.txt` | `OCTAVOX_VST3_INTERPOLATION` option, per-variant bundle name, variant define on all targets, MinGW compatibility shims (SDK tree untouched) |
| `native/vst3/mingw_compat_shim.h`, `mingw_compat_aligned_alloc.h` | **new** — documented libc++/MinGW compatibility force-includes |
| `native/cli/r15ref_capture.cpp` | optional `--interpolation <kernel>` mode (default behaviour byte-identical) |
| `native/r18_obs_reference_hashes.txt` | **new** — the ObsKernel6 reference record |
| `native/tools/windows_dual_build.sh` | **new** — the full reproducible chain (LEG 0–6) |

The **DSP core** (`native/src/dsp.cpp`, `native/include/octavox/dsp.hpp`,
tests, CLI) is **unchanged** — the patch contains no core file, and the
85-case suite plus the four Lagrange6 neutrality hashes pass bit-exact.

Build infrastructure (outside the evidence tree, shipped in
`R18_WDB_build_infrastructure/`): `llvm-mingw-toolchain.cmake`,
`verify_windows_modules.py`, `kit_templates/` (validation-kit scripts +
README).

---

## 10. DAW smoke test (task §7) — NOT PERFORMED (no DAW)

Probed for installed VST3-capable DAWs/bridges: `reaper`, `carla`,
`yabridge`, `linvst`, `bitwig`, `ardour`, `lmms`, `qtractor` — **none
present**. No DAW was installed (not permitted without the user's consent;
a Linux DAW would additionally not load Windows PE modules, and wine is
blocked — §4). **The modules were built and validated but not tested in a
real DAW.** The kit README carries the concrete smoke-test steps (two
distinct plugins on separate tracks, the 38 parameters in the generic
editor, deterministic audio through both, project save/reopen) for
execution on the user's Windows machine.

---

## 11. Honest scope summary

* No claim of DAW compatibility from anything executed here (the standing
  rule: module-load/validator/integration tests state their own scope).
* No claim that the ObsKernel6 variant reproduces Eventide Octavox — it
  only uses the observed interpolation kernel inside our independent DSP
  engine.
* No Windows/MSVC build was executed with MSVC itself (impossible here);
  the delivered Windows binaries are llvm-mingw/clang builds — genuine
  PE32+ x86-64 DLLs — with the deviation recorded (§4).
* Windows-side runtime execution of the shipped modules/tests did not
  happen in this environment; the Windows_Validation_Kit closes that gap
  on a real Windows machine.
* The original Round-18 archive is unchanged; the pinned SDK tree is
  unchanged (re-verified at its SHAs after the builds).

---

## 12. Failure and skip ledger

**Failures: none** (all gates green; see §6, §8 and the script's
"RESULT: ALL GATES PASSED").

**Skipped/not executed (with reasons):** DAW smoke test (no DAW, §10);
Windows-side runtime execution of validator/wrapper/integration binaries
(sandbox cannot execute PEs — wine blocked by seccomp, §4; kit provided);
MSVC toolchain (unavailable, §4).

---

## 13. Evidence index

```
evidence/logs/wdb_sdk_verify.txt                     pinned SDK verification
evidence/logs/wdb_core_suite.txt                     85/85 suite stdout
evidence/logs/wdb_refcap_lagrange6.txt               4/4 gate check
evidence/logs/wdb_refcap_obskernel6_run{1,2}.txt     ObsKernel6 measurements (run1 == run2)
evidence/logs/wdb_lin_{LAGRANGE6,OBSKERNEL6}_wrapper_tests.txt     15/15 each
evidence/logs/wdb_lin_*_integration.{txt,json}       12/12 each (variant-reported JSON)
evidence/logs/wdb_lin_*_validator.txt                47/47 each
evidence/logs/wdb_win_*_{configure,build,…}.log      Windows cross-builds
evidence/logs/wdb_repro_{LAGRANGE6,OBSKERNEL6}_build.log   fresh-dir reproducibility
evidence/logs/wdb_windows_verification.{txt,json}    structural verification (0 failures)
WINDOWS_DUAL_BUILD_MANIFEST.txt                      file inventory + identities + gates
R18_WDB_source_changes.patch                         the exact source delta
R18_WDB_build_infrastructure/                        toolchain file, verifier, kit templates
Windows_Validation_Kit/                              user-side Windows gates (+ README)
```
