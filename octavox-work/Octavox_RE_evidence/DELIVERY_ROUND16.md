# DELIVERY_ROUND16.md — round-16 courier document

**Package:** `Octavox_RE_evidence_round16.zip` (this file is inside the
zip's `Octavox_RE_evidence/` root, alongside the regenerated
`checksums.txt`).

## Contents (delta vs the round-15 delivery)

* **New (docs):** `docs/OCTAVOX_ROUND16_FINDINGS.md` (the round's findings
  document — read this first); `native/vst3/VST3_WRAPPER.md` (the wrapper's
  implementation/build/validation note incl. the pinned-SHA VST3 SDK
  acquisition record).
* **Updated (docs):** `README.md` (title → round 16 + "What is new in round
  16" section), `native/README.md` (round-16 harness + wrapper sections),
  `native/VST3_READINESS.md` (round-16 addendum: the wrapper milestone
  delivered), `native/ARCHITECTURE.md` (build/test counts corrected in
  R16-P1), `docs/OCTAVOX_ROUND15_FINDINGS.md` + `DELIVERY_ROUND15.md` (kept:
  it was explicitly reconciled by R16-P1; superseded as the courier
  document by THIS file) + `README.md`/`native/README.md`/
  `native/CMakeLists.txt`/`native/r15_reference_hashes.txt`/
  `evidence/analysis_round15/NOTES_R15P2.md`/`native/include/octavox/dsp.hpp`
  (the R16-P1 reconciliation corrections — every superseded statement kept
  in place and tagged; history:
  `evidence/analysis_round16/R16_P1_RECONCILIATION_LOG.md`).
* **New (native):** `native/vst3/` — the VST3 wrapper (7 sources +
  VST3_WRAPPER.md): `octavox_ids.h`, `octavox_params.{h,cpp}`,
  `octavox_native_processor.{h,cpp}`, `factory.cpp`,
  `octavox_vst3_tests.cpp`, `CMakeLists.txt`.
* **Updated (native harness, R16-P2 — gates unchanged, proven):**
  `native/tests/test_main.cpp` (durations/filters/groups/progress/JSON),
  `native/bench/bench_main.cpp` (named selection, environment JSON, the new
  `mono_8v_b128_96k_ctrl512` row).
* **New (evidence):** `evidence/analysis_round16/` — 14 files: the
  reconciliation log, the suite profile, the 96 kHz profiling analysis, the
  release/ASan acceptance JSONs + stdouts, the bench JSON + stdout, the
  filter demos, the refcap check, the gate-integrity proofs, the wrapper
  tests + validator runs, the moduleinfo copy, the pre-edit source copies.
* **Restored:** `poc/poc_run.log` (was missing from the working tree vs the
  round-15 zip; byte-identical restore, manifest-verified).
* **Unchanged:** everything else — `poc/` (the Python reference, untouched),
  `evidence/analysis_round{9,10,11,12,14_recovery,15}` (except the tagged
  P1 corrections above), `evidence/decompiles/`, `evidence/records/`,
  `evidence/tools/`, `docs/superseded/`, `native/src/`, `native/cli/`,
  `OCTAVOX_AGENT_HANDOVER_REPORT.md`.

## Verification instructions

```bash
unzip Octavox_RE_evidence_round16.zip
cd Octavox_RE_evidence
sha256sum -c ../checksums.txt          # every file must read OK
# cmake >= 3.19 (pip-installed in these environments; put its bin on PATH)
cd native
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
./build/octavox_tests                  # 85 passed, 0 failed (~41 s release)
./build/octavox_bench                  # 13 cases, 0 allocations
./build/octavox_r15refcap              # 4/4 hashes match r15_reference_hashes.txt
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON && cmake --build build-asan -j2
./build-asan/octavox_tests             # 85/85, zero sanitizer reports (~6.2 min)
# --- the VST3 wrapper (needs the VST3 SDK at ../../../vst3sdk or -DVST3_SDK_ROOT=...;
#     acquisition record with the pinned SHAs: native/vst3/VST3_WRAPPER.md) ---
cd ../vst3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
#    the octavoxnative build runs the validator automatically: 47/47
./build/bin/Release/octavox_vst3_wrapper_tests          # 8/8, exit 0
./build/validator/Release/validator build/VST3/Release/OctavoxNative.vst3
```

The target binary is intentionally NOT in the zip (as in every prior round).
The original stays at the delivery source; SHA-256 `7bb260f6…` (never
executed).

## Not included (and why)

* The VST3 SDK itself — NOT redistributed (Steinberg VST3 dual license);
  the pinned-SHA acquisition record (tag + four component SHAs + the exact
  fetch commands) is in `native/vst3/VST3_WRAPPER.md` §2.
* The built `OctavoxNative.vst3` bundle and the SDK build tree — build
  products; the exact build commands above reproduce them (the module's
  SHA-256 at delivery time is recorded in VST3_WRAPPER.md §5 and in the
  wrapper test evidence).
* A GUI/editor for the wrapper — out of scope by design (minimal
  processor/controller wrapper; see VST3_WRAPPER.md §6).
* Q-D (host candidate table / scale-key) — still OPEN with the recorded
  plan (round-15 scope, untouched this round).
