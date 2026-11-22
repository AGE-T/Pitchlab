# DELIVERY — PitchLab R24-Windows VST3 Build, Validation and Delivery

**Package:** `PitchLab_R24_Windows_VST3_Research_Package.zip`
**Date (UTC):** 2026-11-09
**Deliverables:** exactly two Windows x64 VST3 plugins —
`PitchLab_Harmonizer_Balanced.vst3` (Lagrange6 + Decimated YIN) and
`PitchLab_Harmonizer_FullRate.vst3` (Lagrange6 + FullRate YIN).

Classification legend: **PASS** (executed and passed) · **FAIL** (executed
and failed) · **NOT TESTED** (not executable in this environment, no claim
made) · **BLOCKED** (requirement cannot be satisfied in this environment;
substitute path documented). The four-state discipline of the previous
deliveries is preserved: no green badge without an executed gate behind it.

---

## 1. Source commit — **PASS**

| item | record |
|---|---|
| documented R24 baseline | `ac6612ca4c2ee6ee8941f80eefde8796686a07d7` — **present and verified** in this repository |
| documented corrective commit | `edcddd5` — **NOT present in this repository** (it was created in a different environment; not assumed, verified by `git log --all`) |
| actual applied equivalent | `a76b26c` — the corrective delta re-applied here, narrowly: `Config::simd` default **Scalar → Auto** (Auto = the bit-exact AVX2 level, silent Scalar fallback; FMA stays opt-in), the `simd_default_auto_neutrality` test expectation follows the actual default, FullRate delivery-status comments |
| Windows portability fix | `5eb002d` — two reproduced defects, narrowly scoped (see §3) |
| dual-test extension | `4efb938` — `octavox_dual_nulltest --fullrate` pair mode |
| **build HEAD** | `4efb938ba21491436749297f04ceb07cc194d5e4` |
| working tree | content-clean vs HEAD (file-mode bits only) |

Inspected before building: R24 SIMD implementation (kernels + dispatch +
per-TU CMake wiring), Auto resolution semantics, Scalar fallback, FMA
opt-in behavior, the full Class ID matrix (incl. the historical
Lagrange6Q shared-ID record — untouched, NOT part of this delivery's two
builds), the VST3 wrapper (38-parameter surface, frozen v2 state blob),
state compatibility paths (v1/v2 restore, variant authority).

## 2. Windows and compiler environment — **BLOCKED (as specified) / PASS (documented substitute)**

This build machine is a **Linux sandbox** (Debian 13.4). Visual Studio
2022, MSVC and the Windows SDK **do not exist in it**, and no wine/PE
execution is possible. **Native Windows x64 compilation with MSVC is
BLOCKED in this environment** — reported as such, not papered over.

The binaries were cross-built with the **Zig 0.13.0** toolchain
(`zig c++ -target x86_64-windows-gnu`: clang/LLVM frontend, lld,
mingw-w64 headers, static libc++) producing **genuine PE32+ x64 DLLs with
the Microsoft x64 ABI** importing only Windows system DLLs
(KERNEL32/USER32/ole32/SHELL32/UCRT). This is the **recorded toolchain
deviation** behind the previously accepted R21/R22 Windows x64 deliveries
(recorded in `PITCHLAB_ROUND21_FINDINGS.md` §Environment and the R22 kit
README). Complete native-MSVC rebuild instructions + the per-TU
`/arch:AVX2` and `<intrin.h>` wiring ship in `build_configuration/`.

Full environment record: `evidence/windows_build/r24w_build_environment.txt`
(host OS/CPU incl. CPUID/XCR0 values, toolchain versions, the pinned VST3
SDK v3.7.12_build_20 component SHAs, exact per-TU compiler flags, link
lines, build directories, the cross-build bridges).

## 3. SIMD compilation and runtime dispatch — **PASS**

Requirements of the brief, one by one:

1. **Scalar reference compiles without AVX2 requirements** — PASS:
   `dsp.cpp`, `pitch_control.cpp`, `simd_dispatch.cpp` compile with no ISA
   flags; objdump scans show **zero** ymm/vfmadd/VEX-FP instructions in
   those objects.
2. **AVX2 code only in ISA-specific TUs** — PASS: `-mavx2
   -ffp-contract=off` exactly on `simd_avx2.cpp` (never `-mfma` there);
   no global ISA switch (`ninja -t commands` evidence).
3. **FMA separately controlled** — PASS: `-mavx2 -mfma -ffp-contract=off`
   only on `simd_fma.cpp`.
4. **Runtime CPU detection works on Windows** — PASS (substitute-path
   proof): the zig/windows-gnu target lacks the compiler-rt cpu_model
   runtime, so the Windows builds use the **explicit CPUID+XGETBV
   algorithm** (`src/simd_cpuid_explicit.h`; genuine MSVC keeps
   `<intrin.h>` intrinsics). The **exact Windows algorithm** was compiled
   and executed on real hardware via `-DOCTAVOX_FORCE_EXPLICIT_CPUID` +
   `octavox_r24w_cpuid_check`: explicit vs `__builtin_cpu_supports`
   **AGREE** (avx2=1, fma=1). The Linux builds keep the R24-validated
   `__builtin` path unchanged.
5. **AVX2 dispatch accounts for OS YMM support** — PASS: OSXSAVE
   (CPUID.1.ECX bit 27) + XGETBV XCR0 bits 1:2 gate **both** AVX2 and FMA
   (the R24-W fix — the original R24 MSVC branch gated only FMA). The
   shipped DLLs contain the cpuid ×3 (leaves 0/1/7) + xgetbv ×1 sequence.
6. **Unsupported processors safely use Scalar** — PASS by construction +
   suite: `resolve(Auto)` degrades silently to Scalar; forced levels that
   cannot be honoured are rejected loudly at `prepare()` (suite case
   `simd_prepare_mode_validation`).
7. **No unsupported instruction during init or processing** — PASS
   (machine-code level): final-DLL arithmetic — **ymm lines 154 = 86
   (AVX2 unit) + 68 (FMA unit); vfmadd 26 = 26 (FMA unit only)** — every
   256-bit/FMA instruction in the shipped DLLs originates from the two
   ISA-gated kernel units; the baseline code contributes **zero**. Kernel
   pointers resolve at `prepare()` time only (RT-safety contract,
   `simd.hpp`).
   Evidence: `evidence/windows_build/r24w_simd_machinecode_verification.txt`
   + `evidence/simd/`.

**Dispatch behavior (as required):** Auto → AVX2 when supported else
Scalar; Scalar always available; AVX2 explicitly selectable for testing;
FMA opt-in, independently detected. **Auto never selects FMA** (the
measured R24 decision preserved). No new host-facing parameter.

**Two narrowly-scoped Windows portability fixes (defects reproduced
first; kernels untouched):** (1) `__builtin_cpu_supports` fails to LINK
under zig/windows-gnu (no compiler-rt cpu_model) → explicit CPUID+XGETBV
path for Windows, single source of truth, hardware-validated; (2)
missing `<cmath>`/`<algorithm>` includes (libstdc++ transitive, zig
libc++ not). Commits `5eb002d`, `a76b26c`.

## 4. Balanced plugin build result — **PASS (cross-build path)**

`PitchLab_Harmonizer_Balanced.vst3` (build name
`PitchLab_Lagrange6_Balanced.vst3`): PE32+ x86-64, 622 592 bytes,
SHA-256 `38c12bce…` (full value in SHA256SUMS / the verification JSON).
Canonical bundle layout (`Contents/x86_64-win/<binary>.vst3` +
`Contents/Resources/moduleinfo.json` + `PlugIn.ico` + `desktop.ini` +
`.pdb`). GetPluginFactory exported; imports limited to Windows system
DLLs; separate clean build directory; no Linux cache reuse.

## 5. FullRate plugin build result — **PASS (cross-build path)**

`PitchLab_Harmonizer_FullRate.vst3` (build name
`PitchLab_Lagrange6_FullRate.vst3`): PE32+ x86-64, 622 592 bytes,
SHA-256 `6f1715aa…`. Same canonical layout, separate clean build
directory. **CPU documentation included** (kit README, findings §7, this
report): ~37% of one core at 96 kHz stereo 8-voice with AVX2 (~114%
scalar) — substantially higher than Balanced, not automatically
recommended for realtime use.

## 6. Class ID verification — **PASS**

Balanced `6F4E62589A3142E18F3C5A971D2B4E60` (**preserved lineage** —
R16-R20 sessions/state keep working); FullRate
`C13E1BB55BFB0598F25DDD4A52B0E817` (distinct). Verified in BOTH the
binaries (FUID bytes in Windows COM GUID layout, each DLL containing only
its own) and moduleinfo.json; the two are **distinct** (structural check
`distinct processor CIDs: OK`). The Lagrange6Q historical shared-ID
record is untouched and **not part of this delivery**. Factory metadata
(vendor "PitchLab Project", version 1.2.0, category `Fx|Pitch Shift`,
38 parameters) verified via moduleinfo + the module integration tests.

## 7. Complete regression results — **PASS (Linux side of the identical sources)**

* Core suite: **110/110 Release** (`g2_tests_release.json`) and
  **110/110 ASan/UBSan, zero reports** (`g2_tests_asan.json`).
* The eight historical hash gates (r15 legacy ×4 + r19 default ×4) —
  **bit-exact**, now executing the Auto-resolved AVX2 level end-to-end:
  the strongest proof that the Auto default changes nothing numerically.
* Covered per the brief: YIN FullRate/Decimated/Hybrid reference tests,
  PitchControlLayer, Scalar + AVX2 + Auto dispatch + fallback, FMA
  opt-in, hybrid parabolic refinement regression, `simd_default_auto_
  neutrality`, re-prepare level switching, alloc-free accelerated
  processing.
* Sanitizers under the Windows toolchain: **NOT TESTED** (no sanitizer
  runtime executed on Windows; the Linux/GCC ASan+UBSan run is the
  sanitizer evidence).
* Windows-executed suite: **NOT TESTED** (no PE execution in the sandbox;
  the same sources + defines are suite-validated on Linux; the shipped
  Validation Kit executes real Windows processing through the actual
  DLLs).

## 8. VST3 validator results — **PASS (Linux twins) / NOT TESTED (Windows binaries)**

The Steinberg VST3 SDK v3.7.12_build_20 **validator: 47/47, exit 0, per
variant**, run against the identical-variant Linux builds of the same
sources (`r24w_validator_bal.txt` / `r24w_validator_full.txt`; the
out-of-domain sample-rate probes fail-as-designed and are counted as
passed tests, as in every previous round). Wrapper tests **21/21**
(factory/processor/controller creation, parameter registration, bus
configuration, state serialization/restoration, mono + stereo, dual-mono
identity, no audio-thread allocations, SIMD runtime selection) and
compiled-module integration **12/12** per variant — these drive the
ACTUAL compiled modules through the SDK hosting stack. Running the
validator on the Windows PE binaries was **NOT TESTED** (no PE execution
in this sandbox; `validator.exe` itself is a PE in the cross-build). SDK
validation is explicitly NOT equated to DAW validation.

**Dual-plugin same-process test — PASS:** the extended
`octavox_dual_nulltest --fullrate` loads Balanced + FullRate into ONE
process (two dlopens, both factories live, live re-probe OK): unity
outputs **bit-identical** (parked path is detector-independent),
non-unity outputs **differ** (genuine FullRate detector selection;
bit-identical would have exited 1 as a build-configuration defect).
3/3 cases PASS, identity checks OK. The same tool ships as a Windows
`.exe` for on-target repetition (`Windows_Validation_Kit/`).

## 9. DAW verification status — **NOT TESTED**

No Windows DAW exists in this sandbox. **No DAV claim is made.** Provided
instead: the Windows Validation Kit (real-processor execution of both
DLLs in one process, exit-code gated) and the recommended on-target
check-list (scan/instantiate, mono + stereo, parameter automation, pitch
shifting, project save/reopen — state restoration, continuous-operation
stability, CPU behaviour per variant) in the kit README.

## 10. State compatibility — **PASS**

The 38 VST3 parameter IDs and defaults are **unchanged**; the state blob
stays the frozen **v2** layout ('OVR1'; simd is engine-internal, not
serialized — a restored session keeps the module default, exactly like
`f0_min`/`tracking_gate`). Wrapper + integration tests re-ran the
live/same-instance/fresh-instance restore paths and the v1-blob restore
(21/21 + 12/12 include them); both variants retain their compile-time
detector/interpolation selections after initialization and state
restoration (the variant selections are authoritative on restore).
Balanced keeps the R16-R20 lineage class ID — pre-existing sessions
reference it by FUID and keep working.

## 11. Package size and SHA-256

`SHA256SUMS` in this package covers every delivered file (the two plugin
binaries, their PDBs, the kit executable, the evidence, the sources —
verified against the working tree at packaging time). The ZIP's own size
and SHA-256 are recorded in the sidecar file
`PitchLab_R24_Windows_VST3_Research_Package.zip.sha256` delivered next to
the ZIP and on the delivery page (a manifest cannot contain its own
checksum).

## 12. Known limitations

1. **Toolchain deviation (recorded):** cross-compiled with Zig 0.13.0 —
   not native MSVC (BLOCKED in this sandbox). Native rebuild instructions
   + MSVC-ready sources shipped.
2. **No DAW verification** (NOT TESTED) — kit + check-list provided.
3. **Validator on the Windows binaries** not executed (NOT TESTED);
   Linux-twin validation + structural verification + kit execution cover
   the same sources/defines.
4. **Byte-identical rebuilds not claimed** (PE timestamps/checksums, PDB
   paths, link order are toolchain/path-dependent; functional equivalence
   is gated by the suite, class IDs and validator).
5. **FullRate CPU cost** is high by design (documented; the reference
   mode, not the recommended realtime default).
6. **The Auto/FMA decision is host-measured on one vCPU family**
   (documented in the R24 decision record): Auto prefers the bit-exact
   AVX2 level; on cores where FMA/add latencies differ, FMA remains an
   explicit opt-in away.
7. `edcddd5` (the other environment's corrective commit) is not in this
   repository; its equivalent delta was re-applied and re-validated here
   (§1).

---

## Acceptance table (summary)

| gate | verdict |
|---|---|
| A. source baseline verified + corrective delta applied | **PASS** |
| B. Windows build environment | **BLOCKED (native MSVC)** / PASS (documented zig substitute, R21/R22 precedent) |
| C. Windows SIMD compilation + dispatch (7 requirements) | **PASS** (substitute-path hardware validation for detection) |
| D. two Windows VST3 plugins built | **PASS** (cross-build path) |
| E. DSP + compatibility regression | **PASS** (Linux: 110/110 Release + ASan; Windows-side execution NOT TESTED) |
| F. VST3 validation (validator/wrapper/integration/dual) | **PASS** on Linux twins; Windows-binary validator NOT TESTED; kit provided |
| G. real Windows DAW verification | **NOT TESTED** (honest) |
| H. delivery package | **PASS** (this package) |

The final objective — a clean, verifiable Windows VST3 delivery containing
exactly the two required variants — is met on every gate executable in
this environment, with the environment-imposed limits classified above
and on-target verification tools shipped. The validated R24 DSP engine is
preserved (kernels untouched); the PitchLab roadmap can resume after
delivery.
