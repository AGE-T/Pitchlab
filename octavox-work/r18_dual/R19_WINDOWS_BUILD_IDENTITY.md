# OCTAVOX R19 — WINDOWS DUAL VST3 BUILD IDENTITY RECORD
Generated: 2026-10-31 (Europe/Budapest) by tools/r19_dual_build.sh (see
evidence/logs/r19_chain_part1.log + part2.log for the executed chain).

## The two delivered modules (OctavoxRE_R19_Windows_x64/)

| | Lagrange6 variant | ObsKernel6 variant |
|---|---|---|
| Display name | OctavoxRE Lagrange6 | OctavoxRE ObsKernel6 |
| Bundle | OctavoxRE_Lagrange6.vst3 | OctavoxRE_ObsKernel6.vst3 |
| Processor FUID | {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60} | {E3E7FA70-6A38-0D35-B05F-64B91F1E2689} |
| Factory UID | {7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44} | {3D4CDBC6-ACAC-BB09-077F-D5B80BD2F550} |
| Vendor / subcategory | Octavox RE Project / Fx\|Pitch Shift | (same) |
| Plugin version | 1.1.0 (R19: the 16.0.0 core) | 1.1.0 |
| Default interpolation | Lagrange6 (2048-phase 6-tap) | ObsKernel6 (observed 256x6 .rdata kernel) |
| Default detector | Decimated control-rate (~24 kHz, R19) | Decimated (same — not a variant axis) |
| State blob | OVR1 v2 (v1 restores compatibly) | OVR1 v2 |
| Module SHA-256 | fefe9faf642f71932c997702572bfd64a24520dd0d2a9c036a8d012b829f19d2 | 353342242a6ad38c7c0ecfe55525375d0a552396fb3bda6fa83b2d85f15aa12e |
| Module size | 962048 bytes | 962048 bytes |
| Reproducibility | 2nd fresh build byte-identical (-Wl,/Brepro) | 2nd fresh build byte-identical |

Auxiliary compile-definition proof build (NOT delivered):
LAGRANGE6Q variant module SHA-256 502afa3997730a6beb75e37560fcaa951109d2d14b151df355b36e81733dd5a3
(962048 bytes) — differs from BOTH delivered modules: the
OCTAVOX_VST3_INTERP_* compile definition provably changes the emitted code.

## Toolchain (recorded deviation)
llvm-mingw clang 23.1.2 (release 20260922-ucrt-x86_64), LLD, static
libc++/winpthread, -Wl,/Brepro. R18 used clang 23.1.4 from a release that
no longer exists on the host. MSVC does not exist on this Linux host and
wine is blocked by the sandbox seccomp policy (standing honesty note since
R18-WDB): Windows PE binaries are structurally verified here
(PE32+/x86-64 DLL, GetPluginFactory export, import allow-list, PClassInfo
CID in Windows GUID layout, moduleinfo class version 1.1.0, obs-kernel
table bytes present in both) and functionally validated on the LINUX
builds of the SAME source under the SAME per-variant configuration
(wrapper 15/15, integration 12/12, validator 47/47 per variant; the
dual-module null test PASS on the fresh R19 Linux bundles). User-side
Windows validation: Windows_Validation_Kit (per-variant wrapper +
integration test exes, shared validator.exe + moduleinfotool.exe, and NEW
octavox_dual_nulltest.exe + run_dual_nulltest.cmd for the user-runnable
module-level A/B null test on real Windows).

## Chain gates (all green)
LEG 0 SDK pinned v3.7.12_build_20 verified; LEG 1 core 93/93 + 4/4 + 4/4 +
4/4 hash records (legacy Lagrange6 / legacy ObsKernel6 / 16.0.0 default,
double-run reproducible); LEG 2 Linux variants 15/15 + 12/12 + 47/47 each
+ dual-module null test PASS; LEG 3 Windows builds; LEG 4 byte-identical
rebuilds; LEG 5 delivery assembly; LEG 6 structural verification ALL
CHECKS PASSED.
