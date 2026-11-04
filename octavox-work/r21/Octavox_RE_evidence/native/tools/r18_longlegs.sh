#!/bin/bash
# R18-P1 long legs: ASan+UBSan full suite and Debug -O0 full suite, both with
# the diagnostic trace. Run in the background; logs under evidence/analysis_round18.
set -u
export PATH="/home/z/.venv/bin:$PATH"
cd /home/z/my-project/octavox-work/Octavox_RE_evidence/native
EV=../evidence/analysis_round18

echo "=== ASan+UBSan leg (build + full suite + diag) ===" > $EV/r18_longlegs.log
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON >> $EV/r18_longlegs.log 2>&1
cmake --build build-asan -j2 >> $EV/r18_longlegs.log 2>&1
./build-asan/octavox_tests --diag $EV/r18_diag_asan.jsonl --json $EV/r18_acceptance_asan.json > $EV/r18_suite_stdout_asan.txt 2>&1
echo "ASAN_EXIT=$?" >> $EV/r18_longlegs.log
echo "asan leg done $(date -u +%H:%M:%S)" >> $EV/r18_longlegs.log

echo "=== Debug -O0 leg (build + full suite + diag) ===" >> $EV/r18_longlegs.log
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug >> $EV/r18_longlegs.log 2>&1
cmake --build build-debug -j2 >> $EV/r18_longlegs.log 2>&1
./build-debug/octavox_tests --diag $EV/r18_diag_o0.jsonl --json $EV/r18_acceptance_o0.json > $EV/r18_suite_stdout_o0.txt 2>&1
echo "O0_EXIT=$?" >> $EV/r18_longlegs.log
echo "o0 leg done $(date -u +%H:%M:%S)" >> $EV/r18_longlegs.log
echo "ALL_LONGLEGS_DONE" >> $EV/r18_longlegs.log
