#!/usr/bin/env bash
# R24-v2: build + validate ONE VST3 variant (clean dir). Usage:
#   build_one_variant.sh TAG:INTERP:DET:BUNDLE
set -u
export PATH="/home/z/.venv/bin:$PATH"
cd /home/z/my-project/octavox-work/r24/source/vst3
EVID=/home/z/my-project/octavox-work/r24/evidence/corrections_v2/logs
IFS=':' read -r tag interp det bundle <<< "$1"
B=../build_r24v2_vst3_$tag
echo "=== $tag ($interp + $det) start $(date -u '+%H:%M:%S') ==="
rm -rf "$B"
cmake -S . -B "$B" -DCMAKE_BUILD_TYPE=Release \
     -DOCTAVOX_VST3_INTERPOLATION="$interp" -DOCTAVOX_VST3_DETECTOR="$det" \
     > "$EVID/b5_vst3_${tag}_configure.log" 2>&1 \
  && cmake --build "$B" -j2 > "$EVID/b5_vst3_${tag}_build.log" 2>&1 || {
      echo "  BUILD FAILED $tag"; exit 1; }
"$B/bin/Release/octavox_vst3_wrapper_tests" > "$EVID/b5_vst3_${tag}_wrapper.txt" 2>&1
w=$?
"$B/bin/Release/octavox_module_integration_tests" --json "$EVID/b5_vst3_${tag}_integration.json" \
    > "$EVID/b5_vst3_${tag}_integration_stdout.txt" 2>&1
i=$?
"$B/bin/Release/validator" "$B/VST3/Release/$bundle.vst3" > "$EVID/b5_vst3_${tag}_validator.txt" 2>&1
val=$?
SO=$(ls "$B/VST3/Release/$bundle.vst3/Contents/x86_64-linux/"*.so | head -1)
maxglibc=$(objdump -T "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)
maxgxx=$(objdump -T "$SO" | grep -o 'GLIBCXX_[0-9.]*' | sort -Vu | tail -1)
echo "  wrapper=$w [$(grep -o '[0-9]* passed, [0-9]* failed, [0-9]* total' "$EVID/b5_vst3_${tag}_wrapper.txt" | head -1)]"
echo "  integration=$i [$(grep -o '[0-9]* passed, [0-9]* failed, [0-9]* total' "$EVID/b5_vst3_${tag}_integration_stdout.txt" | head -1)]"
echo "  validator=$val [$(grep -o 'Result: [0-9]* tests passed, [0-9]* tests failed' "$EVID/b5_vst3_${tag}_validator.txt" | tail -1)]"
echo "  $SO: maxGLIBC=$maxglibc maxGLIBCXX=$maxgxx"
echo "=== $tag done $(date -u '+%H:%M:%S') ==="
