# DELIVERY_ROUND15.md — round-15 courier document

**Package:** `Octavox_RE_evidence_round15.zip` (this file is inside the zip's
`Octavox_RE_evidence/` root, alongside the regenerated `checksums.txt`).

## Contents (delta vs the round-12 delivery — the round-13/14 artifacts were destroyed by
environment reset #1 and their scope is rebuilt INSIDE this package)

* **New (docs):** `docs/OCTAVOX_ROUND15_FINDINGS.md` (the round's findings document —
  read this first).
* **Updated (docs):** `README.md` (title → round 15 + "What is new in round 15" section),
  `docs/OCTAVOX_OPEN_QUESTIONS.md` (new Q-16/Q-17 rows + the round-15 closure list),
  `docs/OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md` (§17, rows R-71…R-74 + process note),
  `docs/OCTAVOX_DSP_ARCHITECTURE.md` (S3 correction: `ring[(idx+3)]`, proven by Q-A).
* **New (native):** the whole 15.0.0 state — `native/include/octavox/dsp.hpp` (parameter
  surface contract), `native/src/dsp.cpp`, `native/tests/test_main.cpp` (85 cases),
  `native/tests/r15_acceptance_cases.cpp.inc`, `native/bench/bench_main.cpp` (96 kHz +
  surface cases), `native/cli/r15ref_capture.cpp` + `native/r15_reference_hashes.txt`
  (the neutrality regression record), `native/VST3_READINESS.md`; updated
  `native/CMakeLists.txt`, `native/README.md` (version history + recovery record),
  `native/ARCHITECTURE.md` (round-15 surface mechanisms).
* **New (evidence):** `evidence/analysis_round15/` — `NOTES.md` (Q-A/Q-B/Q-C closures,
  reconstructed record with provenance note), `NOTES_R15P2.md` (incident record),
  `r15_acceptance_results.json` (85/85 machine-readable), `r15_bench_15.txt`,
  `r15_bench_comparison.md` (P5 regression vs baseline);
  `evidence/analysis_round14_recovery/` — `r14recovery_reconstructed.md` (parity +
  interpolation comparison + verdicts, reconstructed record),
  `r14_baseline_bench_stdout.txt` (the 14.0.0 baseline, verbatim preserved).
* **Removed:** `DELIVERY_ROUND12.md` (superseded by this file).
* **Unchanged:** everything else — `poc/` (the Python reference, untouched),
  `evidence/analysis_round{9,10,11,12}/`, `evidence/decompiles/`, `evidence/records/`,
  `evidence/tools/`, `docs/superseded/`, the round-8..12 findings documents,
  `OCTAVOX_AGENT_HANDOVER_REPORT.md`.

## Verification instructions

```bash
unzip Octavox_RE_evidence_round15.zip
cd Octavox_RE_evidence
sha256sum -c ../checksums.txt          # every file must read OK
export PATH=/home/z/.local/bin:$PATH   # cmake 4.x
cd native
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
./build/octavox_tests                  # 85 passed, 0 failed
./build/octavox_bench                  # 11 cases, 0 allocations
./build/octavox_r15refcap              # 4/4 hashes match r15_reference_hashes.txt
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON && cmake --build build-asan -j2
./build-asan/octavox_tests             # 85/85, zero sanitizer reports
```

The target binary is intentionally NOT in the zip (as in every prior round). The original
stays at the delivery source; SHA-256 `7bb260f6…` (never executed).

## Not included (and why)

* The historical `Octavox_RE_evidence_round13.zip` / `round14.zip` — destroyed by
  environment reset #1; their scope is rebuilt and documented inside this package
  (14.0.0 recovery record in `native/README.md`).
* The raw round-15 static scan scripts/outputs and the first 15.0.0 implementation —
  destroyed by reset #2; the verdicts survive as the reconstructed record
  (`evidence/analysis_round15/NOTES.md`) and the core was re-implemented and verified
  bit-neutral (the four reference hashes).
* The parity/interp harness sources (`r14recovery_probe.cpp`, the .py harnesses) — lost
  in reset #2; the measured results survive in `r14recovery_reconstructed.md`.
* Any VST3 wrapper — by design (later milestone; see `native/VST3_READINESS.md`).
