# OCTAVOX ROUND 20 — DELIVERY

Contents delta vs round 19: the Hybrid coarse-to-fine detector mode
(`DetectorMode::Hybrid`, dsp.hpp/dsp.cpp — purely additive, the FullRate and
Decimated code paths byte-identical, all reference hashes unchanged); the
VST3 default-parameter initialisation fixes (octavox_params.cpp ctor
`setNormalized(default)`, the processor `initialize()` fresh-instance reset,
`IEditController::setComponentState` implemented — see
docs/OCTAVOX_ROUND20_FINDINGS.md §1); the suite 93→96 (3 new Hybrid
cases) and the wrapper suite 15→20 (5 R20-P0 default/state-sync cases,
no control movement); two new tools (octavox_r20_detcompare — the
13-corpus real-material three-detector battery; octavox_r20_dualmono — the
deterministic dual-mono/stereo integrity + user-report doubling probe);
evidence/analysis_round20 (r20_detcompare.json + 39 trajectory CSVs +
r20_dualmono.json + full suite logs incl. the ASan/UBSan leg); three new
docs (OCTAVOX_ROUND20_FINDINGS.md, R20_FEATURE_RECONSTRUCTION_MATRIX.md,
R20_PARAMETER_SEMANTICS.md); VST3_WRAPPER.md extended with §4a (R20-P0);
tools/windows_dual_build.sh updated for the reset environment (explicit
CMAKE_MAKE_PROGRAM, LEG 3 full-fresh builds, the hash gates pinned to the
legacy mode + the new 16.0.0-default r19-record gate, 96/20-case summaries).

Verification from a fresh extraction:
  cd Octavox_RE_evidence/native
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
  ./build/octavox_tests               # 96/96 (93 R19 + 3 R20 Hybrid)
  ./build/octavox_r15refcap --legacy  # the four 15.0.0 hashes (r15_reference_hashes.txt)
  ./build/octavox_r15refcap           # the four 16.0.0-default hashes (r19_reference_hashes.txt)
  ./build/octavox_r20_detcompare --json det.json --trajdir traj   # 78 rows
  ./build/octavox_r20_dualmono --json dm.json                     # + the doubling probe

  # the VST3 wrapper (needs the pinned SDK, see native/tools/get_vst3_sdk.sh):
  cmake -S vst3 -B buildwrap -DVST3_SDK_ROOT=<sdk> -DCMAKE_BUILD_TYPE=Release
  cmake --build buildwrap -j
  ./buildwrap/bin/Release/octavox_vst3_wrapper_tests   # 20/20 (incl. R20-P0 cases 16-20)

Windows delivery: OctavoxRE_R20_Windows_x64_Dual_Build.zip (the two
byte-reproducible PE32+ x86-64 modules + the validation kit; built with
llvm-mingw release 20260922 = clang 23.1.2 — toolchain deviation vs R19's
23.1.4 recorded in the worklog):
  Lagrange6  module sha256 d25902d6321950da4ddf622293132518768795a6c32ede3fc23a25cbb91aab50
  ObsKernel6 module sha256 0040425897a6aea318c0c18065a169627d350a9dabcef4134aa7e6b73fe4b966

The full chain (native core + wrapper + integrations + validator + Windows
cross-builds + reproducibility): native/tools/windows_dual_build.sh —
ALL GATES PASSED this round (see evidence/logs/r20wdb/).
