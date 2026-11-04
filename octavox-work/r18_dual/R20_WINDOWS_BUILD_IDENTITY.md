# OCTAVOX R20 — WINDOWS DUAL VST3 BUILD IDENTITY RECORD
Generated: 2026-10-31 (Europe/Budapest) by tools/r20_dual_build.sh (see
evidence/logs/r20_chain_live.log + r20_* legs for the executed chain).

## The two delivered modules (Builds/OctavoxRE_R20_Windows_x64/)

| | Lagrange6 variant | ObsKernel6 variant |
|---|---|---|
| Display name | OctavoxRE Lagrange6 | OctavoxRE ObsKernel6 |
| Bundle | OctavoxRE_Lagrange6.vst3 | OctavoxRE_ObsKernel6.vst3 |
| Processor FUID | {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60} | {E3E7FA70-6A38-0D35-B05F-64B91F1E2689} |
| Factory UID | {7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44} | {3D4CDBC6-ACAC-BB09-077F-D5B80BD2F550} |
| Vendor / subcategory | Octavox RE Project / Fx\|Pitch Shift | (same) |
| Plugin version | 1.2.0 (R20: the 17.0.0 core — the R20-P0 default-state fix + the Hybrid detector mode selectable via state/config; modules default to Decimated) | 1.2.0 |
| Default interpolation | Lagrange6 (2048-phase 6-tap) | ObsKernel6 (observed 256x6 .rdata kernel) |
| Default detector | Decimated control-rate (~24 kHz, R19 default — UNCHANGED) | Decimated (same) |
| State blob | OVR1 v2 (v1 restores compatibly; detector_mode int32 round-trips all three modes incl. Hybrid) | OVR1 v2 |
| Module SHA-256 | 54f5b392357c5f2334dedfb7c2f71a9abc9815ecaf3f2cb47d3f409314adbd6b | 54caa89201f999ede8e26c45c467ff26d8bf3881e48d84d1a0e804c2efca4e9d |
| Module size | 965632 bytes | 965632 bytes |
| Reproducibility | -Wl,/Brepro links | (LEG 4 re-links verified in the chain) |

Auxiliary compile-definition proof build (NOT delivered):
LAGRANGE6Q variant module SHA-256
c3baa6ea48c4ab31ef7cd13392aa50e1a9289d8c641192b6369b2fb9d768efd5
(965632 bytes) — differs from BOTH delivered modules: the
OCTAVOX_VST3_INTERP_* compile definition provably changes the emitted
code.

## R20-P0 relevance of THIS build
The R20-P0 VST3 default-state fix (OctavoxParameter ctor value seeding +
setComponentState) is IN these modules: the Windows_Validation_Kit's
octavox_vst3_wrapper_tests.exe now contains the 16th case
(param_default_state_sync) — running run_validation.cmd on Windows
verifies that a fresh instance reports the documented defaults for all 38
parameters through the ACTUAL module, without moving any control.

## Toolchain (recorded deviation)
llvm-mingw clang 23.1.2 (release 20260922-ucrt-ubuntu-22.04-x86_64,
sha256 of the distribution tar.xz
bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21 —
re-acquired THIS round after a sandbox reset wiped /home/z/tools; the
R18/R19 builds used clang 23.1.2/23.1.4 from the same release family).
MSVC does not exist on this Linux host and wine is blocked by the
sandbox seccomp policy (standing honesty note): Windows PE binaries are
structurally verified here (PE32+/x86-64 DLL, GetPluginFactory export,
import allow-list, PClassInfo CID in the Windows COM GUID layout,
moduleinfo class version 1.2.0, obs-kernel table bytes present in both)
and functionally validated on the LINUX builds of the SAME source under
the SAME per-variant configuration (wrapper 16/16, integration 12/12,
validator 47/47 per variant; the dual-module null test PASS on the fresh
R20 Linux bundles). User-side Windows validation: Windows_Validation_Kit
(per-variant wrapper + integration test exes, shared validator.exe +
moduleinfotool.exe, octavox_dual_nulltest.exe + run_dual_nulltest.cmd).

## Chain gates (all green; total wall 313 s)
LEG 0 SDK pinned v3.7.12_build_20 verified; LEG 1 core 97/97 + FOUR hash
records (legacy Lagrange6 4/4, legacy ObsKernel6 4/4, the 16.0.0 default
record 4/4 — reproducing on the 17.0.0 core — and the NEW 17.0.0 Hybrid
record 4/4, each double-run reproducible); LEG 2 Linux variants 16/16 +
12/12 + 47/47 each + dual-module null test PASS; LEG 3 Windows builds;
LEG 4 byte-reproducibility; LEG 5 delivery assembly; LEG 6 structural
verification ALL CHECKS PASSED (3 distinct module hashes).
