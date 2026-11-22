# PitchLab Harmonizer — Windows x64 Validation Kit (R24-Windows delivery)

**Contents:**
- `octavox_dual_nulltest.exe` — the deterministic dual-module A/B/null-test
  tool (loads BOTH delivered bundles simultaneously in one process, renders
  identical stimulus through one processor instance from each factory,
  verifies class identities, and compares outputs sample-for-sample;
  exit 0 on pass). Self-contained: imports only Windows system DLLs
  (KERNEL32/SHELL32/USER32/ole32/UCRT), statically linked libc++.
- `run_validation_balanced_vs_fullrate.cmd` — runs the dual test against the
  two delivered plugin bundles.

**The two delivered builds** (in `../plugins/`):

| delivery bundle | build name | class ID | detector | interpolation |
|---|---|---|---|---|
| PitchLab_Harmonizer_Balanced.vst3 | PitchLab_Lagrange6_Balanced.vst3 | `6F4E62589A3142E18F3C5A971D2B4E60` (preserved lineage) | Decimated YIN | Lagrange6 |
| PitchLab_Harmonizer_FullRate.vst3 | PitchLab_Lagrange6_FullRate.vst3 | `C13E1BB55BFB0598F25DDD4A52B0E817` (R21 mint, distinct) | FullRate YIN | Lagrange6 |

In-DAW display names: "PitchLab Harmonizer Lagrange6 Balanced" /
"PitchLab Harmonizer Lagrange6 FullRate" (the interpolation + detector
choice is visible in the host plugin list). Both keep the 38-parameter
host surface and the frozen v2 state blob (old sessions restore).

**CPU guidance (measured, R24 benchmark record):** Balanced is the realtime
production candidate. FullRate is the high-cost detector reference: at
96 kHz stereo 8-voice it measured ~37% of one core with the AVX2 kernels
vs ~114% scalar — usable on a modern machine but substantially more
expensive than Balanced; not automatically recommended for realtime use.

**SIMD behavior (both builds):** Auto by default — AVX2 when the CPU *and*
OS YMM state support it (CPUID OSXSAVE + XGETBV XCR0 checked), otherwise a
silent Scalar fallback; FMA stays opt-in (never selected by Auto). The
AVX2 lag-axis kernels are bit-identical to the Scalar reference by
construction (hard exact-equality gates), so the plugin output is
bit-exact on every processor. No host parameter was added for SIMD.

**Usage (on Windows x64):** run `run_validation_balanced_vs_fullrate.cmd`
(double-click or from cmd). The tool prints the A/B comparison and exits 0
on pass. Results land in `out_dual\` (JSON + WAVs).

**Toolchain note (honesty):** this sandbox is Linux; Visual Studio 2022 /
MSVC / the Windows SDK are not available in it. The modules are
cross-built with the Zig toolchain 0.13.0 (clang/LLVM frontend, lld,
mingw-w64 headers, static libc++) — genuine PE32+ x64 DLLs with the
Microsoft x64 ABI importing only Windows system DLLs. This is the same
recorded toolchain deviation that produced the accepted R21/R22 Windows
x64 deliveries. The identical sources + variant defines were validated
end-to-end on Linux (110/110 suite Release + ASan/UBSan, wrapper 21/21,
module integration 12/12, Steinberg validator 47/47 per variant, dual
same-process test PASS); the Windows binaries are structurally verified
(PE, exports, imports, FUIDs, moduleinfo, machine-code ISA isolation:
every 256-bit/FMA instruction in the DLL originates from the two
ISA-gated kernel units). Native MSVC rebuild instructions are included in
the package (`build_configuration/`).

**Further verification you can perform:**
1. SHA-256 of the bundles against the package `SHA256SUMS`.
2. The Steinberg `validator.exe` from VST3 SDK v3.7.12_build_20 against
   each bundle (the Linux-side validator runs are in
   `evidence/vst3_validation/`).
3. DAW checks (the recommended minimal set): scan/instantiate, mono +
   stereo processing, parameter automation, pitch shifting, project
   save/reopen (state restoration), continuous-operation stability, CPU
   behavior for both variants.

The original Eventide Octavox binary is not part of this delivery and was
never executed.
