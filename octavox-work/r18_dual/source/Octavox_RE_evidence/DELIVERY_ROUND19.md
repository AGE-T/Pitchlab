# OCTAVOX ROUND 19 — DELIVERY

Contents delta vs round 18: the 16.0.0 core (decimated control-rate
detector with the FullRate reference mode; click-free spacing commits /
base-delay changes / voice activation ramps; read suppression; stage
profiler; VoiceStats wrap/commit timestamps), the 93-case suite (8 new r19
cases, no gate weakened, the legacy path gated by the unchanged r15 hash
literals), r19_reference_hashes.txt (+ legacy records unchanged), five new
tools (octavox_r19_clickscan, octavox_r19_nulltest,
octavox_r19_variant_metrics, vst3/octavox_dual_nulltest, the instrumented
bench --matrix/--stages modes), evidence/analysis_round19 (212-row
benchmark matrix + stage attribution, 320-render click scan with WAVs,
engine- and module-level null tests with 32-bit float WAVs, variant
metrics, full suite logs), docs/OCTAVOX_ROUND19_FINDINGS.md.

Verification from a fresh extraction:
  cd Octavox_RE_evidence/native
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
  ./build/octavox_tests              # 93/93
  ./build/octavox_r15refcap --legacy # the four 15.0.0 hashes (r15_reference_hashes.txt)
  ./build/octavox_r15refcap          # the four 16.0.0 hashes (r19_reference_hashes.txt)
  sha256sum -c ../checksums.txt      # 529/529

NOT included (by design): the VST3 SDK (pinned-SHA acquisition via
tools/get_vst3_sdk.sh), the Windows dual-build delivery (separate
OctavoxRE_R19_Windows_x64_Dual_Build.zip — built by tools/r19_dual_build.sh
from THIS source; its identity record is in the zip and in
evidence/analysis_round19/logs/r19_win_module_hashes.txt), build
directories.

The original Eventide binary was never executed; runtime behaviour of the
target remains UNK. The ObsKernel6 interpolation mode is the only
target-derived data in this package (the statically observed 256x6 .rdata
kernel table, round 12).
