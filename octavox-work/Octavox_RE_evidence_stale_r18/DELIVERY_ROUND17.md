# DELIVERY_ROUND17.md — round-17 courier document

**Package:** `Octavox_RE_evidence_round17.zip` (this file is inside the
zip's `Octavox_RE_evidence/` root, alongside the regenerated
`checksums.txt`).

## Contents (delta vs the round-16 delivery)

* **New (docs):** `docs/OCTAVOX_ROUND17_FINDINGS.md` (the round's findings
  document — read this first).
* **Updated (docs):** `README.md` (title → round 17 + "What is new in
  round 17" section), `native/README.md` (R17 addendum: the `--reverse`
  flag, the tools/ layout entry, the 15/15 wrapper status, MSVC guards),
  `native/VST3_READINESS.md` (round-17 addendum: reproducible build + the
  audited parameter/state contract), `native/vst3/VST3_WRAPPER.md`
  (status → 15/15; §2 the scripted SDK acquisition; §3a the 38-parameter
  contract table; §3b the self-verified OVR1 blob layout; §5 the R17
  measurements incl. the byte-identical module hash; §6 the
  Windows-untested and no-GUI statements), `docs/OCTAVOX_OPEN_QUESTIONS.md`
  (§4 the round-17 statuses; the round-15 Q-D "not attempted" entry tagged
  superseded — kept in place for the correction history).
* **New (native/tools — R17-P2):** `get_vst3_sdk.sh` (pinned-SHA VST3 SDK
  acquisition + verification; executed in `--verify` and fresh-install
  modes this round), `get_vst3_sdk.ps1` (Windows counterpart; authored,
  NOT executed), `build_and_test.sh` (the full Linux build+validation
  chain; **executed end-to-end: ALL GATES PASSED, 454 s**),
  `build_and_test.ps1` (Windows/MSVC; authored, NOT executed),
  `README.md` (usage + the pin table + the honest executed/untested
  statuses).
* **Updated (native harness, R17-P1 — gates untouched):**
  `native/tests/test_main.cpp` (+`--reverse` order-independence flag, the
  JSON `environment.reverse_order` field, `_WIN32` guards for
  gmtime/sysconf), `native/bench/bench_main.cpp` (`_WIN32` guards),
  `native/CMakeLists.txt` + `native/vst3/CMakeLists.txt` (MSVC-neutral
  flag guards — Linux paths proven unchanged).
* **Updated (wrapper tests, R17-P3):** `native/vst3/octavox_vst3_tests.cpp`
  — 8 → **15 cases** (param_metadata_table, events_block_positions,
  simultaneous_multivoice_events, bypass_wetdry_clickless_noalloc,
  state_full_surface_restore, state_cross_rate_layout,
  state_invalid_blobs) + the `--dump-params` metadata dump. The wrapper
  implementation sources (`octavox_params.*`,
  `octavox_native_processor.*`, `factory.cpp`, `octavox_ids.h`) are
  **unchanged** from round 16.
* **New (evidence):** `evidence/analysis_round17/` — 21 files: the static
  findings + petk toolkit, the release/ASan/−O0/reverse acceptance JSONs +
  stdouts, the 300 s window reproduction, the suite profile/timing model,
  the refcap check, the bench, the filter demos, the build-script run, the
  wrapper tests + validator captures, the parameter metadata table, and
  the pre-edit source proofs.
* **Unchanged:** everything else — the DSP core (`native/src`,
  `native/cli` — byte-identical to round 16/15), `poc/` (the Python
  reference, untouched), `evidence/analysis_round{9,10,11,12,14_recovery,
  15,16}`, `evidence/decompiles/`, `evidence/records/`,
  `evidence/tools/`, `docs/superseded/`,
  `OCTAVOX_AGENT_HANDOVER_REPORT.md`, `DELIVERY_ROUND15/16.md` (kept as
  the prior courier documents).

## Verification instructions

```bash
unzip Octavox_RE_evidence_round17.zip
cd <the extraction directory>           # where checksums.txt sits next to
sha256sum -c checksums.txt              # Octavox_RE_evidence/ -- all 355 OK
# cmake >= 3.19 (pip-installed in these environments; put its bin on PATH)
cd native
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
./build/octavox_tests                  # 85 passed, 0 failed (~41 s release)
./build/octavox_tests --reverse        # 85/85, ~42 s (order independence)
./build/octavox_tests --group r13      # 20/20 in ~12 s (the battery group)
./build/octavox_r15refcap              # 4/4 hashes match r15_reference_hashes.txt
./build/octavox_bench                  # 13 cases, 0 allocations
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON && cmake --build build-asan -j2
./build-asan/octavox_tests             # 85/85, zero sanitizer reports (~6.2 min)
# --- or the scripted full chain (P2) ---
cd tools && ./get_vst3_sdk.sh          # pinned-SHA SDK acquisition (needs network)
./build_and_test.sh                    # release + ASan + wrapper: ALL GATES PASSED
# --- the VST3 wrapper (needs the SDK at ../../../vst3sdk or -DVST3_SDK_ROOT) ---
cd ../vst3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
./build/bin/Release/octavox_vst3_wrapper_tests          # 15/15, exit 0
./build/bin/Release/octavox_vst3_wrapper_tests --dump-params
./build/bin/Release/validator build/VST3/Release/OctavoxNative.vst3   # 47/47
# --- Windows/MSVC: tools/get_vst3_sdk.ps1 + tools/build_and_test.ps1 ---
#     AUTHORED, NOT EXECUTED (no Windows runtime in the dev environment).
```

The target binary is intentionally NOT in the zip (as in every prior
round). The original stays at the delivery source; SHA-256 `7bb260f6…`
(never executed).

## Not included (and why)

* The VST3 SDK itself — NOT redistributed (Steinberg VST3 dual license);
  acquire+verify it with `native/tools/get_vst3_sdk.sh` / `.ps1` (the
  exact pinned revision set is recorded in
  `native/vst3/VST3_WRAPPER.md` §2 and the tools README).
* The built `OctavoxNative.vst3` bundle and the SDK build tree — build
  products; the commands above reproduce them (the module's SHA-256 at
  delivery time — byte-identical across the R16 delivery, the post-reset
  rebuild and the R17 guarded rebuild — is recorded in VST3_WRAPPER.md §5).
* Any Windows binary or Windows test result — none exists; the Windows
  scripts are authored-but-unexecuted and say so.
* A GUI/editor for the wrapper — out of scope by design (minimal
  processor/controller wrapper; see VST3_WRAPPER.md §6).
* Runtime verification of the original Eventide Octavox — excluded by the
  standing rule; the target was never executed; all runtime questions stay
  UNK.
