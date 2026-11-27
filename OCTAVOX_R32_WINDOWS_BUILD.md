# OCTAVOX R32 — WINDOWS x64 BUILD OF THE TELEMETRY DEMO (R31 ported; the R24-W cross method restored)

**Scope note (standing):** clean-room PitchLab research line (octavox-native
16.3.0). The original Ox.vst3 was **never executed** in this project — every
original-behaviour claim below cites the published static-audit record
(R26–R30). This round **applies** that record; it makes no new RE claims.

## 0. What this round delivers

The R31 telemetry demo VST3 was **Linux-only** (the llvm-mingw/zig cross
toolchain had been lost with the sandbox resets). This round re-acquires the
toolchain (Zig 0.13.0, downloaded from ziglang.org, installed **outside**
the repo), rebuilds the cross pipeline, and delivers
**`PitchLab_Lagrange6_Balanced.vst3` for Windows x64** — the same R31
feature set on the platform the original Octavox RE target runs on:

- the m02-style **pitch telemetry** (ids 7..13: cents from C4 with the
  −65535 sentinel, glide ratio, Q23 n/(n+8), grain phase, labelled
  confidence, gated ∓cents twins) through the **IPitchTelemetry** vendor
  interface (IID D1A60F31-8C4B-4E39-9A67-31D1-8E4F5A21) served by
  queryInterface — the 38-parameter surface and state blob v2 stay
  byte-frozen (wrapper 21/21 unchanged);
- the per-voice **block-rate poller with atomic dirty-bit fanout**
  (56 live bits, consumeDirtyBits() read-and-clear);
- **latency-truth notification** (restartComponent(kLatencyChanged)
  exactly on change);
- the R24 SIMD capability layer: per-TU ISA units with **runtime
  detection**, Auto = bit-exact AVX2 (opt-in FMA) — **now actually present
  in the Windows binary** (see §2: this round caught and fixed a defect
  that would have silently shipped scalar-only).

Package: `public/PitchLab_R32_Telemetry_Demo_Windows_x64.zip` (bundle +
PDB + install/host-author README + this report + the verification logs;
SHA-256 sidecar alongside).

## 1. The toolchain restoration (recorded deviation, unchanged method)

Zig 0.13.0 (`zig c++ -target x86_64-windows-gnu`: clang/LLVM 18.1.6
frontend, lld, mingw-w64 headers, static libc++) — the exact recorded
toolchain deviation behind the **accepted R21/R22/R24-W Windows
deliveries**. No MSVC, no Windows SDK, no wine in this sandbox (native
MSVC/PE execution BLOCKED; same honesty note as R24-W). The zig binary
lives outside the repository; the recreated wrapper scripts
(`octavox-work/r24/win-zig/`: zig-cc.sh, zig-cxx.sh,
zig-win-toolchain.cmake, moduleinfotool-host.sh) are now **env-driven**
(`ZIG_BIN`) instead of hardcoding a host path — this time they are
committed to the branch (the originals were lost with the sandbox resets
and only survived inside the R24-W delivery package, from which the method
was reconstructed and verified against the shipped binaries).

Host-side bridges (documented deviations, cross-builds only):
- `moduleinfotool-host.sh` planted at `<build>/bin/moduleinfotool.exe`
  (the SDK post-build invokes that path; Linux execve honors the shebang)
  copies the **identical-variant Linux twin's** moduleinfo.json — factory
  metadata is a compile-time property of the same sources + defines;
  verified byte-identical.
- `host-shims/attrib` no-op (Explorer decoration only).
- `SMTG_RUN_VST_VALIDATOR=OFF` on the cross-build (the validator is a PE
  exe; validation runs on the Linux twin instead).

## 2. Two Windows-portability defects found and closed (the round's real work)

**(a) The lost R24-W corrective delta — the link blocker.** zig 0.13.0
targeting x86_64-windows-gnu does **not** provide the compiler-rt
cpu_model runtime: `__builtin_cpu_supports` compiles but **fails to link**
(undefined `__cpu_indicator_init` / `__cpu_model`; reproduced with a
minimal probe this round). The R24-W fix — `src/simd_cpuid_explicit.h`
(the canonical explicit CPUID+XGETBV sequence: leaf 1 ECX FMA bit 12,
OSXSAVE bit 27, XCR0 XMM+YMM state; leaf 7.0 EBX AVX2 bit 5; **both**
features gated on the OS-support check) with the dispatch TU's platform
gates (`_WIN32` → explicit; Linux keeps the R24-validated builtin path
unchanged) — existed only in the R24-W delivery package (the repo delta
was re-applied only narrowly at R24; the R26+ audit line never had it).
Ported verbatim this round, together with its on-hardware validation
harness (`octavox_r24w_cpuid_check`: the exact Windows algorithm vs the
compiler's own resolver on the same silicon — **AGREE**, exit 0).

**(b) The stale capability gates — the silent scalar-only trap.** After
porting (a), the module linked — but the machine-code gate caught that the
final DLL had **ymm=0**: the `isaUnitsCompiled()` / `kernels()` /
read-kernel capability gates still tested the removed `OCTAVOX_X86_MSVC`
macro, so on Windows the dispatch reported "no ISA units", the kernel
references were compiled out, and the linker **dead-stripped the AVX2/FMA
objects** — the plugin would have loaded and run, silently scalar-only,
with every functional test still passing. Fixed by aligning the four gates
with the R24-W corrected form (`OCTAVOX_X86_EXPLICIT`). Final DLL now:
**ymm=275 = 207 (simd_avx2.obj) + 68 (simd_fma.obj), vfmadd=26 (simd_fma
only), cpuid=3 + xgetbv=1 (the explicit detection)** — every 256-bit
instruction in the shipped module originates from the two ISA kernel
units; the baseline code contributes zero mandatory AVX2/FMA (full
accounting + the byte-level kernel-presence check in
`r32w_machinecode_verification.txt`).

Both fixes are provably **Linux-neutral**: the Linux paths compile to the
same code (the Linux twin re-ran every gate from scratch — §3).

## 3. Verification (all gates green; fresh builds, no cache reuse)

| gate | result |
|---|---|
| core suite (Release, fresh build_r32) | **115/115**; all four reference hashes bit-exact; r15acc determinism hash `0x35fe0d51a4372ac8` — identical to the R31 record |
| core suite (ASan/UBSan, fresh build_r32_asan) | **115/115, zero sanitizer reports** |
| explicit-cpuid cross-check (Linux hardware) | **AGREE** (avx2=1 fma=1 both paths) |
| VST3 wrapper tests (Linux twin) | **21/21** (frozen surface untouched) |
| module integration (fresh twin bundle, same sources+defines) | **16/16** — incl. all four R31 telemetry cases (vendor interface, values/sentinel 220 Hz → −300.009 cents, dirty bits, latency notification 2304→1280) |
| Steinberg validator (Linux twin) | **47/47** |
| Windows module machine-code + structure | per-TU ISA flags exact; ymm/vfmadd/cpuid/xgetbv accounting (above); kernel byte-sequences present; imports = 11 Windows system DLLs only (same set as R24-W); single export `GetPluginFactory`; processor FUID + IPitchTelemetry IID GUIDs found; moduleinfo byte-identical to the twin |

## 4. The package

`public/PitchLab_R32_Telemetry_Demo_Windows_x64.zip`:
`PitchLab_Lagrange6_Balanced.vst3` (Windows bundle: `Contents/x86_64-win/`
binary + PDB + `Contents/Resources/moduleinfo.json`) + README (install to
the VST3 folder; the IPitchTelemetry spec for host/tool authors; the
toolchain honesty note) + this report + the verification logs. SHA-256
sidecar alongside. The Linux R31 package remains available unchanged.

## 5. Honest limits

- The Windows binary is **cross-built** (the recorded zig deviation), not
  MSVC-native; the sources remain MSVC-ready (the R24-W MSVC instructions
  apply unchanged). PE execution was not possible in this sandbox — the
  verification method is the documented Linux-twin + machine-code record
  (§3), the same evidence class behind the accepted R24-W delivery. The
  R24-W Windows Validation Kit (on-target runner) is not re-shipped;
  on-target verification remains the owner's step.
- Byte-identical rebuilds are not claimed (PE timestamps/checksums, link
  order); the R32-W module is functionally pinned by the twin gates +
  structural record, like R24-W.
- id 11 (confidence) remains the labelled PitchLab mapping (periodicity ×
  control-state weight); with the default config (tracking gate off) it
  honestly reads 0.
- No DAW was involved (standing scope note): validator + SDK loader +
  machine-code accounting are the compatibility evidence.
