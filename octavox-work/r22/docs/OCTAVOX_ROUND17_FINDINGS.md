# OCTAVOX ROUND 17 FINDINGS — Reproducible VST3 Build, Validation Reliability and Remaining Static DSP Questions

**Status: DELIVERED.** All five priorities are closed: (1) the validation
discrepancy reported by the independent verification environment is
**explained quantitatively** — a uniform toolchain/host slowdown (−O0 ≈
8×, ASan+UBSan ≈ 8.4–9×) against a fixed 300 s window, with no stall, no
order dependence, no test interaction and no buffering defect (the full
suite passes 85/85 in every configuration on this host); (2) the VST3
wrapper is **reproducible from a clean extraction** — pinned-SHA SDK
acquisition scripts (executed in verify and fresh-install modes), a Linux
build+validation chain script executed end-to-end (`ALL GATES PASSED`,
454 s), and an authored-but-**unexecuted** Windows/MSVC path clearly
labelled as such; (3) the wrapper's **38-parameter and state contracts are
audited and machine-checked** (15/15 wrapper tests, validator 47/47, the
full metadata table captured); (4) **meaningful static progress** on the
original target: Q-D (candidate tables ↔ scale/key) closed structurally
with new .rdata tables decoded, the mode-1 phase trajectory closed at
instruction level, the record-ring first-field scaling closed (the tracker
doubling floor), T+0x5F14's pre-setParam value proven uninitialised (UNK by
construction), the kernel characterised but its exact generator still UNK;
(5) documentation and the versioned package are consistent and verified.
The original Eventide binary was **never executed**; all runtime questions
stay UNK.

Environment note: a **third sandbox reset** destroyed `/home/z/octavox` and
`/home/z/tools` before this round began. Recovery: the working tree
`octavox-work/Octavox_RE_evidence/` (inside the persistent project
directory) survived; the round-16 ZIP re-verified 325/326 manifest entries
and the one lost file (`poc/poc_run.log`) was restored byte-identically
from the ZIP (all 326 now OK); cmake 4.4.4 reinstalled (same version as
recorded); **every build this round was therefore from scratch**, which
doubles as the clean-rebuild proof for P2.

## 1. Verdict table

| Item | Verdict |
|---|---|
| P1 validation reproducibility | **DELIVERED** — full-suite runs: release 85/85 (43.1 s, 41.1 s, and 42.8 s in reversed group order), ASan+UBSan 85/85 zero reports (373.0 s = 6.22 min), Debug −O0 85/85 (352.4 s); the 300 s window modelled: a −O0 or sanitizer build on a host merely 1.5–2× slower than this 2-core dev host expires **inside the r13 group's 96 kHz detector battery** — the reported signature; per-case slowdown is uniform (median 8.0×/8.4×, IQR 7.5–8.9×) ⇒ pure environment factor; the four reference hashes re-verified (release, ASan and −O0 legs — the hashes are optimisation-level invariant) |
| P2 reproducible VST3 build | **DELIVERED (Linux executed, Windows authored-unexecuted)** — `native/tools/`: `get_vst3_sdk.sh` (pinned-SHA fetch + verify + fresh-install, both modes executed; the working SDK content-matches all four components), `build_and_test.sh` (executed end-to-end: 74 s release leg incl. 85/85 + 4/4 hashes + bench, 376 s ASan leg 85/85, wrapper leg 15/15 + validator 47/47, module hash byte-identical to R16), `get_vst3_sdk.ps1` + `build_and_test.ps1` (authored from the verified chain with MSVC portability guards; **no Windows runtime here — no Windows build is claimed**), `README.md` |
| P3 parameter/state contract | **DELIVERED** — 38-parameter metadata table (`--dump-params`, `r17_param_metadata_table.txt`) + 7 new automated cases (15/15 total): ID uniqueness, spec integrity, processor/controller agreement, boundary/middle roundtrips, block start/middle/last/end events (bit-exact), simultaneous multi-voice events (bit-exact), clickless bypass+wet/dry automation with 0 allocations, full-surface save/restore (38/38 exact + bit-exact replay), cross-rate/cross-layout restore (bit-exact), invalid/truncated blobs (all 428 prefixes rejected, processor stays usable); the OVR1 blob layout documented and self-verified; core neutrality untouched (4/4 hashes) |
| P4 static DSP | **DELIVERED (bounded)** — Q-D **CLOSED structurally** (five temperament tables + the NEW 19-row scale-pattern table + key/user maps + per-voice interval tables, the mux/record-link delivery, the engine composition law); mode-1 phase trajectory **CLOSED** (direct 4-float × rate table-driven phases; mode 0 = 0.25-relaxation glide, constant `0x180412260`); record-ring first-field scaling **CLOSED** (raw live-period copy; the tracker's cadence/2 multiplier + doubling floor `≥ [T+0x6008]·50` with `[T+0x6008]=2048.0` @ `0x1800E0D5B` makes `v ≥ 50` half-grain units); T+0x5F14 pre-setParam **UNK by construction** (full init-site proof: never written in the ctor chain); kernel design formula **UNK generator, DER characterisation** (not Lagrange/Farrow/windowed-sinc: best closed-form fits 2.1–7.4e-3 off; symmetric, DC ripple ≤ 2^-13, cutoff ≈ 0.82–0.84·Nyquist) |
| P5 documentation & packaging | **DELIVERED** — this document, the wrapper/readiness updates, the tools README, the versioned archive + SHA-256 + checksum manifest verified from a fresh extraction |

## 2. P1 — the validation reliability question

### 2.1 What was reported and what was measured

The independent environment: full 85-case suite **did not finish within a
300 s window**, stopping "during the Round 13 group near the 96 kHz
detector battery"; the four reference hashes and the seven `r15acc` cases
passed; the 96 kHz battery passes when isolated.

This round's measurements (all machine-readable with environment records,
`evidence/analysis_round17/`):

| run | result | wall |
|---|---|---|
| release full suite #1 | 85/85 | 43.1 s |
| release full suite #2 | 85/85 | 41.1 s |
| release full suite, **reversed group order** (`--reverse`, new R17 harness flag) | 85/85 | 42.8 s (per-case durations within ±1.1 % of forward) |
| Debug −O0, no sanitizers | 85/85 | 352.4 s |
| Debug −O0 under the 300 s window (reproduction) | killed at 78/85 | 294.3 s elapsed at kill, during the r15acc group (after r13 completed) |
| Debug + ASan/UBSan | 85/85, zero sanitizer reports | 373.0 s (6.22 min) |
| 96 kHz battery isolated | pass | 5.93 s (vs 5.81 s in-suite: +2 %) |
| r13 group isolated | 20/20 | 11.6 s |
| r15acc group isolated | 7/7 | 7.1 s |
| four reference hashes | 4/4 exact | `r17_refcap_check.txt` — **also bit-exact at −O0** (the in-suite `default_neutrality` run printed all four expected hashes unoptimized) |

Machine/compiler/configuration (from the JSON `environment` records):
Intel Xeon 2 cores, Linux 5.10.134, GCC 14.2.0, Release `-O2` / Debug
`-O0` / Debug `-O0` + ASan+UBSan, cmake 4.4.4.

### 2.2 The explanation (environmental; no code defect)

1. **No stall/order dependence/interaction**: two forward runs and one
   reversed-order run are all 85/85 with matching per-case durations; the
   battery costs the same isolated as in-suite (+2 %); the suite's cases
   share no mutable state (each case builds fresh engines; the harness
   globals are bookkeeping only — structural argument in the profile doc).
2. **No buffering issue**: the harness flushes stdout after every progress
   line and group header (`test_main.cpp` `pushCase`/group-header
   `fflush`) — verified in source this round.
3. **The timing model** (`r17_suite_profile.md`): the per-case slowdown is
   **uniform** (−O0 median 7.99×, IQR 7.50–8.45; ASan median 8.42×, IQR
   7.95–8.93) — the signature of a pure toolchain/host factor. With that
   factor, a 300 s window expires at release-equivalent 300/(factor·H)
   seconds; the r13 group starts at 16.2 s release-equivalent and the 96
   kHz battery spans 16.6–22.4 s. **A −O0 or sanitizer build on a host
   H = 1.5–2× slower than this 2-core dev host expires inside the battery**
   (which alone costs 49.1 s at −O0 / 52.5 s under ASan here) — precisely
   the reported "stopping during the Round 13 group near the 96 kHz
   detector battery". The independent environment's passing runs all fit
   their window: the r15acc group (~60 s at −O0), the isolated battery
   (~49–59 s), the reference hashes (seconds).
4. Conclusion: **environmental** (unoptimized and/or sanitizer build on a
   slower or contended host, against a fixed 300 s window). Nothing was
   weakened: all 85 cases stay in the default suite, the gates are
   textually unchanged, and the four reference hashes still match in every
   configuration. A verification window must budget ≥ 6–7 minutes for the
   sanitizer leg (or ≥ 1 minute for release) on comparable hardware — the
   round-16 record (41 s release / 6.16 min ASan) and this round's numbers
   (43 s / 6.22 min) agree.
5. New harness capability (harness-only, gates untouched; the pre-edit
   sources are preserved in `evidence/analysis_round17/`):
   `--reverse` (group runners back-to-front, recorded in the JSON
   `environment.reverse_order`), plus the R16 `--list/--filter/--group/
   --category/--json` set re-exercised (`r17_filter_demo.txt`).

## 3. P2 — the reproducible VST3 build

Delivered in `native/tools/` (four scripts + README; all inside the
evidence ZIP; the SDK itself stays OUTSIDE — Steinberg dual license):

* **`get_vst3_sdk.sh`** — fetches exactly the pinned revision set (tag
  `v3.7.12_build_20` = `cc2adc90…`, components `base f0998e7b…`, `cmake
  2022b265…`, `pluginterfaces 151ecde4…`, `public.sdk 3fce096d…`),
  verifies every fetched revision (`git rev-parse HEAD` must equal the
  pin), cross-checks the tag's own `git ls-tree` gitlinks against the pins
  (a moved tag or stale pin is a hard error — **no silent version
  upgrades**), installs the components as plain `git archive` extractions
  (the layout the build consumes), refuses targets inside the evidence
  tree, and fails clearly on missing git/network. **Executed this round**
  in `--verify` mode (the round-16 SDK tree content-matches all five
  revisions) and in fresh-install mode into a scratch directory
  (byte-identical to the working tree; the fresh tree configures the
  wrapper build cleanly — vstgui4/doc/tutorials are provably unneeded).
* **`build_and_test.sh`** — the full Linux chain: tool checks → native
  Release build → 85/85 suite + **4/4 reference hashes compared
  programmatically against `r15_reference_hashes.txt`** + bench → native
  ASan/UBSan build → 85/85 → wrapper build (validator runs automatically)
  → 15/15 wrapper tests → 47/47 validator re-run → module SHA-256 →
  machine-readable logs. **Executed end-to-end this round: `RESULT: ALL
  GATES PASSED (total wall 454 s)`** (`r17_build_script_run.txt`: release
  leg 74 s, ASan leg 376 s, wrapper leg 3 s incremental).
* **`get_vst3_sdk.ps1` / `build_and_test.ps1`** — the Windows/MSVC
  counterparts (same git commands; MSVC requires a Developer PowerShell;
  no sanitizer leg — the `ENABLE_SANITIZERS` branch is GCC/Clang-only).
  **Authored but NOT executed: this environment has no Windows runtime.
  No Windows binary has been built or tested, and none is claimed.** The
  scripts carry the untested status in their headers and outputs; the
  first person to run them produces the first real Windows verification.
* **MSVC portability guards** (Linux-neutral, proven): `-Wall/-Wextra` and
  the explicit `-O2` are now `if(NOT MSVC)`-guarded in both CMakeLists;
  the two harnesses gained `_WIN32` branches for `gmtime_s`/`GetSystemInfo`
  (and the bench RSS read returns 0 on Windows). Verified
  Linux-neutral: after the guards the release suite is 85/85, the four
  hashes match, and **the module is byte-identical** (same SHA-256
  `b293cecb…` as the R16 delivery — also reproduced from scratch after the
  environment reset, demonstrating deterministic compilation).
* The pinned Linux build and its evidence are preserved (round-16 record +
  this round's re-runs); the absence of the compiled module and the SDK
  from the evidence ZIP is documented in VST3_WRAPPER.md §2/§6 and the
  tools README.

## 4. P3 — the parameter and state contract audit

* **The 38-parameter table** (full dump `r17_param_metadata_table.txt`,
  generated by `octavox_vst3_wrapper_tests --dump-params`; summary table
  and the exact conversion laws in VST3_WRAPPER.md §3a): 6 global + 4 per
  voice × 8 — Bypass (bool, kIsBypass), Pitch ±24 st @ 1 cent (4800
  steps), Voices 1–8 (7 steps), Spread (bool, default on), Feedback
  [0,0.9], Wet/Dry [0,1] (default 1 = bit-neutral), per voice Pitch
  Offset ±24 st, Gain [0,2], Mute (bool), Base Delay [0,200] ms with
  defaults (256+32v)/48 ms.
* **Processor/controller agreement**: the `param_metadata_table` case
  verifies `getParameterCount()==38`, and for every id the host-visible
  `ParameterInfo` (title/shortTitle/units/stepCount/defaultNormalized/
  flags/unitId) equals the spec table the audio path consults — **0
  mismatches**. SingleComponentEffect means one object serves both
  interfaces; the test pins the agreement mechanically.
* **The seven new cases** (15/15 total; measurements in
  `r17_vst3_wrapper_tests.txt`): conversions at boundaries/middles
  (grid-exact, continuous ≤ 4 ulp — including the subtle finding that
  `max − step` is NOT a grid point in binary floating point, only
  `min + (N−1)·step` is); events at block start/middle/last sample and at
  `offset == numSamples` (the block-end drain — bit-exact vs the core,
  with a drop-control proving the event has an effect); five simultaneous
  per-voice + global events at one sample position (bit-exact, the
  reference replicating the wrapper's `retargetRatios` convergence);
  bypass+wet/dry automation with the analytic no-click bound
  (max step 0.4980181 ≤ 0.5073929 = reference step + bypass ramp slope +
  wet/dry event term) and **0 allocations**; the full non-default
  38-parameter surface save/restore (38/38 values exact, replay
  bit-exact, live and fresh-instance); cross-rate/cross-layout restore
  (48 k stereo → 96 k mono, 44.1 k stereo — bit-exact vs the hand-driven
  core at the new rate; the ms-domain default delays scale with rate, the
  documented semantics); invalid/truncated blobs (all 428 prefix lengths
  rejected, wrong magic/version/NaN/ring-law rejected, NaN ratio
  sanitised per the documented policy, processor usable afterwards).
* **The core is untouched**: `native/src`, `native/cli` byte-identical to
  the round-16/15 delivery; the four reference hashes unchanged and
  re-verified; Lagrange6 remains the default; no DSP behaviour was
  changed to accommodate the wrapper.
* **No GUI/editor** exists and none is faked (VST3_WRAPPER.md §6 states it
  in bold so it cannot be mistaken for a completed graphical interface).

## 5. P4 — the remaining static DSP questions

Full evidence with VAs, tables and instruction excerpts:
`evidence/analysis_round17/r17_static_findings.md` (+ `petk.py`, the
read-only PE64/capstone toolkit; `.pdata` re-verified at 13 116
functions). Summary:

1. **Q-D (host candidate tables ↔ scale/key) — CLOSED structurally, bounded
   DER residue.** The candidate tables are .rdata constants: the five
   temperament cent-tables `0x1803BB690` (Equal/Just_Major/Just_Minor/
   Pythagorean/Meantone — order and values match the choices string
   `0x1803BBF60`; meantone = exact 1/4-comma), the **newly decoded
   19-row scale-pattern table `0x1803BB780`** (stride 0x30: rows =
   ionian…locrian, harmonic/melodic minor, chromatic with 99-fillers,
   whole-tone, pentatonics, enigmatic, neapolitan, hungarian), the
   17-entry key map `0x1803BB610` (enharmonic collapse), the ionian
   reference map `0x1803BB660` and the user-scale map `0x1803BB60C`. The
   host parameters (Scale 16 engine-side/19 host-side choices, Key 17
   spellings, tuning/temperament 5 choices, `userscale1..12`, per-voice
   `pitchN` 29 musical intervals −2 oct…+2 oct) reach the engine through
   the module graph (`scal table` 15-entry mux → pattern-row index; `keys`
   → key map → transposition relative to A; `int table N` 29-entry muxes
   with the chromatic/whole-tone switchover `c_or`) and the record links
   (`scale`, `key`, `shift1..8`, `pcent1..8`, `tuning`,
   `userscale1..12`, `quantization`, `tune`). The engine composes the
   published degree/ratio tables with **interval snapping** (pattern
   values = degree-zone boundaries; e.g. a "+3rd" in penta-major snaps to
   the 2nd). Residues: the per-interval mux constants' design rule (why
   e.g. +3rd/+4th share 17) and the key-ratio ascent/descent asymmetry
   stay DER/UNK; runtime values UNK.
2. **Mode-1 phase trajectory — CLOSED.** In the output-stage delay readers
   (`FUN_1800DC430..761`), mode `[obj+0x8C]==1` uses the four port-table
   floats `[obj+0x68]→[0..0xC]` **directly × rate as the four per-frame
   phase values** (externally steered, no internal state); mode 0 glides
   the internal phase toward table[0] with **K = 0.25** per sub-step
   (`0x180412260`), the four outputs being consecutive sub-steps. The mode
   flag's producer is UNK (DER hypothesis: "mod input connected").
3. **Record-ring first-field scaling — CLOSED.** The writer
   (`FUN_1800E0190`) copies the live period `T+0x5FD4` **raw** into
   field 0; the tracker's state 6 multiplies the estimate by the cadence/2
   reload and **doubles it while < `[T+0x6008]·50`** with
   `[T+0x6008] = 2048.0` (immediate store `0x1800E0D5B`; the ctor also
   seeds `T+0x5FD4 = 2048.0`). Hence the crossfade consumer's
   `v = field·2^-11` is guaranteed ≥ 50 (its own ≥ 48 clamp is redundant)
   — the round-12 "what makes v ≥ 48" answered: **the tracker's
   octave-doubling floor, not writer-side scaling**.
4. **T+0x5F14 pre-setParam — UNK by construction.** A complete
   initialisation scan of the tracker ctor (`FUN_1800E0360`), the wrapper
   ctor (`0x1800E2C00`) and the dtonicii9 add_fn (`0x180137E20`) proves
   the field is never written before `setParam` (zero-fill ends at
   `T+0x5EAF`; the immediate stores cover 0x5F18/0x5FD4/0x5FE0/0x6000+
   but not 0x5F10/0x5F14; the dsp_t is an embedded member, so the value
   is allocator residue). Its post-setParam role (the {2,4,8} cadence
   selector, Q-17) is unchanged.
5. **Kernel design formula — UNK generator, DER characterisation.** The
   256×6 table (`0x1803B6170`) has perfect point symmetry
   (`row[i][k] == row[255−i][5−k]`, max diff 0.0) and DC-gain ripple ≤
   `2^-13`; it is **not** Lagrange (row 0 is not a delta), not a Farrow
   polynomial (fits stall at 2.6e-5), and not any tested windowed-sinc /
   root-raised-cosine / Kaiser closed form (best fits 2.1–7.4e-3 — orders
   above float rounding). Verdict: a numerically designed anti-aliased
   6-tap fractional-delay table (cutoff ≈ 0.82–0.84·Nyquist); the exact
   generator stays UNK. Our core's Lagrange-6 remains an independent
   design (documented difference).

No static conclusion was inferred from runtime behaviour; the target was
not executed.

## 6. UNK residue & known limitations

* Runtime behaviour of the target: **UNK throughout** (never executed).
* The Q-D bounded residues (interval-mux design rule; key-ratio asymmetry;
  mode-flag producer) and the kernel's exact generator.
* Windows build: scripted but **unexecuted** (no Windows runtime here).
* The 96 kHz default-path realtime margin (2.0–2.1×) is unchanged — no
  core change was made this round (Lagrange6 default retained; all
  quality/RT constraints re-verified by the suite in three
  configurations).
* The wrapper has no GUI; no mid-stream resume; the 1-cent pitch grid
  quantises automation curves (documented).
* The VST3 SDK is not redistributed (dual license); the pinned-SHA
  acquisition scripts are the documented path to reproduce it.
* Sandbox resets remain an environmental hazard: this round began with
  the third reset; the recovery is documented above and every prior-round
  archive remains unchanged and re-verified.

## 7. Artifacts & reproduction

Working tree: `octavox-work/Octavox_RE_evidence/`. Round-17 evidence
(`evidence/analysis_round17/`):

| file | content |
|---|---|
| `r17_acceptance_results.json` / `r17_suite_stdout_release.txt` | release 85/85 in 43.1 s, per-case durations, environment |
| `r17_acceptance_results_reverse.json` / `r17_suite_stdout_reverse.txt` | reversed-order 85/85 in 42.8 s (order-independence) |
| `r17_acceptance_results_asan.json` / `r17_suite_stdout_asan.txt` | ASan+UBSan 85/85 in 373.0 s, zero reports |
| `r17_debug_o0_full.json` / `r17_debug_o0_full_stdout.txt` | Debug −O0 85/85 in 352.4 s |
| `r17_debug_o0_300s_window.txt` | the 300 s window reproduction (killed at 78/85, 294.3 s) |
| `r17_suite_profile.md` | the timing model: per-config tables, uniform-slowdown analysis, the 300 s cutoff placement vs host factor |
| `r17_refcap_check.txt` | the four neutrality hashes (release; they also match at −O0 in-suite) |
| `r17_bench.json` / `r17_bench_stdout.txt` | 13 bench cases, 0 allocations |
| `r17_filter_demo.txt` | `--list` / group / filter / `--reverse` demos |
| `r17_build_script_run.txt` | `build_and_test.sh` end-to-end: ALL GATES PASSED, 454 s |
| `r17_vst3_wrapper_tests.txt` | the 15/15 wrapper run with all measurements |
| `r17_vst3_validator.txt` | the SDK validator: 47/47 |
| `r17_param_metadata_table.txt` | the 38-parameter table (`--dump-params`) |
| `r17_static_findings.md` + `petk.py` | the P4 static evidence + the read-only PE toolkit |
| `r17_test_main_preedit.cpp.txt` / `r17_wrapper_tests_preedit.cpp.txt` | pre-edit sources (harness/wrapper-test change proofs) |

```sh
# the reproducible chain (or just: native/tools/build_and_test.sh)
cd native
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/octavox_tests --json ../evidence/analysis_round17/r17_acceptance_results.json
./build/octavox_tests --reverse            # order-independence
./build/octavox_tests --filter detector_96000hz_sine_battery   # isolated battery
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON && cmake --build build-asan -j
./build-asan/octavox_tests                # ~6.2 min — budget accordingly
./build/octavox_r15refcap                 # 4/4 hashes
cd ../vst3 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/bin/Release/octavox_vst3_wrapper_tests          # 15/15
./build/bin/Release/octavox_vst3_wrapper_tests --dump-params
./build/bin/Release/validator build/VST3/Release/OctavoxNative.vst3   # 47/47
```

New sources this round: `native/tools/` (4 scripts + README), the
`--reverse` harness flag + `reverse_order` JSON field, MSVC guards in
`native/CMakeLists.txt` + `native/vst3/CMakeLists.txt` + the two harness
sources, and the 7 new wrapper test cases. The DSP core
(`native/src`, `native/cli`) is **byte-identical** to the round-16/15
delivery.

Delivery: `Octavox_RE_evidence_round17.zip` — SHA-256 and the checksum
manifest in `DELIVERY_ROUND17.md`; verification from a fresh extraction
performed (manifest OK, builds clean, 85/85 release + ASan, 4/4 hashes,
wrapper 15/15, validator 47/47).
