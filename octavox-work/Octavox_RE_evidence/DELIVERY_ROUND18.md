# DELIVERY_ROUND18.md — round-18 courier document

**Package:** `Octavox_RE_evidence_round18.zip` (this file is inside the
zip's `Octavox_RE_evidence/` root, alongside the regenerated
`checksums.txt`).

## Contents (delta vs the round-17 delivery)

* **New (docs):** `docs/OCTAVOX_ROUND18_FINDINGS.md` (the round's findings
  document — read this first).
* **Updated (docs):** `README.md` (title → round 18 + the "What is new in
  round 18" section), `docs/OCTAVOX_OPEN_QUESTIONS.md` (§5 the round-18
  statuses), `docs/OCTAVOX_CONTROL_FLOW_MATRIX.md` (§14 — the NEW
  path & parameter **reachability matrix**: the mode-path, the tracker
  cadence path, the interval-choice grid, the scale→pattern-row
  irregularity, the kernel-row reachability), `native/README.md`
  (the R18 addendum: `--diag`/`--isolate`), `native/VST3_READINESS.md`
  (the round-18 addendum), `native/vst3/VST3_WRAPPER.md` (§7 the
  compiled-module integration harness + the R18 re-verification).
* **New (native harness, R18-P1 — gates untouched):**
  `native/tests/test_main.cpp` gained the `--diag <path>` JSONL forensic
  trace (case start/completion records with per-case wall, process RSS,
  always-on allocation-counter totals and an FP-environment probe) and the
  `--isolate` group-isolated fallback mode (POSIX fork-per-group,
  explicitly NOT equivalent to a single-process run);
  `native/tests/alloc_counter.cpp` gained the always-on new/delete totals
  (the armed counter the gates use is unchanged). Pre-edit sources
  preserved in `evidence/analysis_round18/r18_preedit_*.txt`.
* **New (native/vst3, R18-P6):** `octavox_module_integration_tests.cpp`
  (the compiled-module integration harness — loads the built
  `OctavoxNative.vst3` bundle through the pinned SDK's module loader and
  checks the full host interface contract bit-exactly against the core)
  + its CMake target (platform-aware: `module_linux.cpp` executed here;
  the Windows/macOS paths authored, unexecuted). The wrapper
  implementation sources are **unchanged** (the module hash proves it).
* **Updated (tools):** `native/tools/build_and_test.sh` — LEG 4 (the
  integration harness, 12/12 required; `--skip-vst3`), the r18_* evidence
  filenames, the `--skip-vst3` flag. **Executed end-to-end this round
  with `--skip-asan` (the ASan leg ran standalone: 85/85 in 6.07 min):
  RESULT: ALL GATES PASSED (81 s)** (`r18_build_script_run/`).
  `native/tools/README.md` (the R18 update). `build_and_test.ps1`
  unchanged (authored, NOT executed — no Windows runtime here).
* **New (evidence):** `evidence/analysis_round18/` — 31 files: the P1
  diagnostic suite results (`r18_suite_diagnostics.md` + the release /
  reverse / isolate / stability+r13 / ASan / −O0 diag traces and JSONs +
  the three contention-reproduction captures incl. the
  killed-inside-the-battery trace), the P6 integration results
  (`r18_module_integration.json` + stdout), the build-script run, the
  refcap check (4/4), the P2–P5 static findings
  (`r18_static_findings.md`) and the pre-edit source proofs.
* **Unchanged:** everything else — the DSP core (`native/src`,
  `native/cli` — the four reference hashes re-verified bit-exact), the
  wrapper implementation sources, `poc/`, the prior evidence rounds, the
  decompiles/records/tools, `docs/superseded/`, the prior courier
  documents.

## Verification instructions

```bash
unzip Octavox_RE_evidence_round18.zip
cd <the extraction directory>           # where checksums.txt sits next to
sha256sum -c checksums.txt              # Octavox_RE_evidence/ -- all OK
# cmake >= 3.19 (pip-installed in these environments; put its bin on PATH)
cd native
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
./build/octavox_tests                    # 85 passed, 0 failed (~43 s release)
./build/octavox_tests --diag /tmp/t.jsonl   # the forensic trace (new)
./build/octavox_tests --isolate              # the group-isolated fallback (new)
./build/octavox_r15refcap                # 4/4 hashes match r15_reference_hashes.txt
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON && cmake --build build-asan -j2
./build-asan/octavox_tests               # 85/85, zero sanitizer reports (~6.1 min)
# --- the VST3 module + wrapper (needs the SDK at ../../../vst3sdk or -DVST3_SDK_ROOT) ---
cd ../vst3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_ROOT=<sdk> && cmake --build build -j2
./build/bin/Release/octavox_vst3_wrapper_tests             # 15/15, exit 0
./build/bin/Release/octavox_module_integration_tests       # 12/12 (new: the LOADED bundle)
./build/bin/Release/validator build/VST3/Release/OctavoxNative.vst3   # 47/47
# --- or the scripted full chain (P2) ---
cd ../tools && ./build_and_test.sh --sdk-root <sdk> --evidence-out <dir>
# --- Windows/MSVC: tools/get_vst3_sdk.ps1 + tools/build_and_test.ps1 ---
#     AUTHORED, NOT EXECUTED (no Windows runtime in the dev environment).
```

The target binary is intentionally NOT in the zip (as in every prior
round). The original stays at the delivery source; SHA-256 `7bb260f6…`
(never executed).

## Not included (and why)

* The VST3 SDK itself — NOT redistributed (Steinberg VST3 dual license);
  acquire+verify it with `native/tools/get_vst3_sdk.sh` / `.ps1` (the
  pinned revision set is recorded in `native/vst3/VST3_WRAPPER.md` §2 and
  the tools README).
* The built `OctavoxNative.vst3` bundle and all build trees — build
  products; the commands above reproduce them (the module's SHA-256 at
  delivery time — byte-identical across the R16 delivery, the R17 rebuild
  and the R18 rebuild from the SDK's archived location — is recorded in
  VST3_WRAPPER.md §5/§7: `b293cecb…`).
* Any Windows binary or Windows test result — none exists; the Windows
  scripts remain authored-but-unexecuted and say so. The Windows/macOS
  harness source paths are authored in the CMake but unexecuted.
* A GUI/editor for the wrapper — out of scope by design (see
  VST3_WRAPPER.md §6).
* Runtime verification of the original Eventide Octavox — excluded by the
  standing rule; the target was never executed; all runtime questions stay
  UNK. DAW compatibility is NOT claimed from the module-load/validator/
  integration tests (their scope is stated in each document).

## Round-18 headline results

| gate | result |
|---|---|
| release 85-case suite (single process, `--diag`) | 85/85, 43.5 s |
| reversed order / isolate / stability+r13 combo | 85/85 (41.0 s) / 85/85 (43.3 s) / 26/26 (19.0 s) |
| ASan+UBSan full suite | 85/85, zero reports, 6.07 min |
| Debug −O0 full suite | 85/85, 352.6 s |
| contention reproduction (F≈16, 300 s window) | killed inside the 96 kHz battery after exactly 58 cases (diag-traced) — the reported signature |
| contention no-window / combo | 85/85 in 484 s / 26/26 in 213 s — no stall |
| reference hashes | 4/4 exact |
| wrapper tests / SDK validator / **module integration** | 15/15 / 47/47 / **12/12 bit-exact** |
| module SHA-256 | `b293cecb…` (unchanged from R16/R17) |
| build_and_test.sh (updated, legs 1+3+4) | ALL GATES PASSED (81 s) |
