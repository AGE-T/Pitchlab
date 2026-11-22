# PITCHLAB R24-v2 — EXACT BUILD & VALIDATION COMMANDS (reproducible)

Environment used for the delivered binaries (see
`environment.txt`): Debian 13 (trixie), GCC 14.2.0, glibc 2.41 host headers,
CMake 4.4.4, x86_64 with AVX2+FMA. The VST3 SDK v3.7.12_build_20 is expected
at `../../vst3sdk` relative to `source/vst3` (acquire with
`source/tools/get_vst3_sdk.sh` — pinned SHAs; the SDK is not redistributed
in this package).

## 1. Native core (Release + ASan/UBSan) + the eight hash gates

```bash
export PATH="/home/z/.venv/bin:$PATH"        # cmake 4.4.4 (pip install cmake)
cd source/native                             # = the R24 source tree root

# Release
cmake -S . -B build_r24v2 -DCMAKE_BUILD_TYPE=Release
cmake --build build_r24v2 -j
./build_r24v2/octavox_tests                  # 110/110 (suite; JSON: --json out.json)
./build_r24v2/octavox_r15refcap --legacy     # 4/4 legacy 15.0.0 pins (r15_reference_hashes.txt)
./build_r24v2/octavox_r15refcap              # 4/4 16.0.0-default pins
./build_r24v2/octavox_bench --simd auto      # 13-case bench at the production default

# ASan/UBSan
cmake -S . -B build_r24v2_asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build_r24v2_asan -j
./build_r24v2_asan/octavox_tests             # 110/110, zero sanitizer reports
```

## 2. The nine VST3 variants (clean build directories)

The authoritative loop (as executed for this delivery) is
`tools/build_r24v2_variants.sh` in the evidence tree; expanded form:

```bash
cd source/vst3
for V in "LAGRANGE6 DECIMATED   PitchLab_Lagrange6_Balanced" \
         "LAGRANGE6 HYBRID      PitchLab_Lagrange6_Hybrid" \
         "LAGRANGE6 FULLRATE    PitchLab_Lagrange6_FullRate" \
         "LAGRANGE6Q DECIMATED  PitchLab_Lagrange6Q" \
         "LAGRANGE6Q HYBRID     PitchLab_Lagrange6Q_Hybrid" \
         "LAGRANGE6Q FULLRATE   PitchLab_Lagrange6Q_FullRate" \
         "OBSKERNEL6 DECIMATED  PitchLab_ObsKernel6_Balanced" \
         "OBSKERNEL6 HYBRID     PitchLab_ObsKernel6_Hybrid" \
         "OBSKERNEL6 FULLRATE   PitchLab_ObsKernel6_FullRate"; do
  set -- $V
  B="../build_r24v2_vst3_$1_$2"
  cmake -S . -B "$B" -DCMAKE_BUILD_TYPE=Release \
        -DOCTAVOX_VST3_INTERPOLATION="$1" -DOCTAVOX_VST3_DETECTOR="$2"
  cmake --build "$B" -j                                   # module + tests + validator
  "$B/bin/Release/octavox_vst3_wrapper_tests"             # 22/22 per variant
  "$B/bin/Release/octavox_module_integration_tests"       # 12/12 (real dlopen)
  "$B/bin/Release/validator" "$B/VST3/Release/$3.vst3"    # 47/47 per bundle
done
```

The per-TU ISA wiring (AVX2/FMA flags ONLY on `simd_avx2.cpp` /
`simd_fma.cpp`) is part of `source/native/CMakeLists.txt` and
`source/vst3/CMakeLists.txt` — no global `-mavx2`/`-mfma` exists anywhere in
the build (verifiable by inspecting the per-target `flags.make` files in any
build directory).

## 3. Cross-bundle and artifact-level audits

```bash
# dual-module null test (Lagrange6 vs ObsKernel6, two real dlopens in one process):
../build_r24v2_vst3_LAGRANGE6_DECIMATED/bin/Release/octavox_dual_nulltest \
    --lagrange6 <L6B-bundle> --obskernel6 <OK6B-bundle> --out <dir> --json <j>

# class-ID audit across the nine shipped bundles (moduleinfo.json CIDs):
python3 tools/class_id_audit.py plugins/       # pairwise-distinct gate

# binary compatibility audit (per .so):
file <bundle>/Contents/x86_64-linux/*.so
objdump -T <bundle>/Contents/x86_64-linux/*.so | grep -o 'GLIBC_[0-9.]*' | sort -Vu
objdump -T <bundle>/Contents/x86_64-linux/*.so | grep -c isoc23      # must be 0
readelf -d <bundle>/Contents/x86_64-linux/*.so | grep NEEDED
```

## 4. Package verification (clean extraction)

```bash
unzip PitchLab_R24_Linux_VST3_Research_Package_v2.zip -d /tmp/verify_r24v2
cd /tmp/verify_r24v2/PitchLab_R24_Linux_VST3_Research_Package_v2
sha256sum -c SHA256SUMS                        # every file verified
```
