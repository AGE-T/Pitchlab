# OCTAVOX ROUND 16 FINDINGS — Evidence Reconciliation, Reproducible Acceptance and VST3 Wrapper Milestone

**Status: DELIVERED.** The round's three goals are closed: (1) the round-15
status contradictions are reconciled (16 dated corrections across 9 files,
historical evidence preserved, correction history recorded); (2) the full
validation workflow is reproducible and profiled (per-test durations,
filters/groups, progress output, named bench selection, machine-readable
outputs with environment records — release 41.0 s / ASan+UBSan 6.16 min
measured, 85/85 both, all gates frozen); (3) the VST3 wrapper is
**genuinely implemented and validated** — the module `OctavoxNative.vst3`
passes the VST3 SDK's own validator 47/47 and the 8-case wrapper test suite
8/8, including the bit-exact sample-accurate-vs-block-boundary equivalence
proof. The original Eventide binary was **never executed**; all runtime
questions stay UNK.

Working tree (persistent since round 15): `octavox-work/Octavox_RE_evidence/`.
The DSP core 15.0.0 is **unchanged** this round (`native/src`, `native/cli`
byte-identical to the round-15 delivery; only the test/bench harnesses were
extended, with the gates proven textually identical). The four
default-neutrality reference hashes still match (re-verified three times:
R16-R0 pre-edit, R16-P2, and the final integration check).

## 1. Verdict table

| Item | Verdict |
|---|---|
| P1 status reconciliation | **DELIVERED** — 16 corrections in 9 files, every superseded statement kept + tagged `[R16-P1 …]`; correction history: `evidence/analysis_round16/R16_P1_RECONCILIATION_LOG.md`; one lost file (`poc/poc_run.log`) restored byte-identical from the round-15 zip |
| P2 reproducible workflow | **DELIVERED** — suite + bench extensions (see §3); measured: release 85/85 in **41.0 s**, ASan+UBSan 85/85 in **6.16 min** (9.01× — resolves the ">5 min independent verification" report); machine-readable JSON with environment records |
| P3 96 kHz profiling | **DELIVERED (measured analysis, deliberate no-change decision)** — main cost = the YIN O(ntau·m) loop (83–95 % share at 96 kHz); every optimisation candidate rejected on bit-neutrality/behaviour grounds; the tested `detection_interval` knob already provides an 11.90× margin at 96 kHz |
| P4 VST3 wrapper | **DELIVERED — IMPLEMENTED, BUILT, VALIDATED** — `native/vst3/` (7 sources + docs): validator **47/47**, wrapper tests **8/8**, segmentation equivalence **maxdiff = 0**, state roundtrips **maxdiff = 0**, **0 allocations** in `process()` |
| P5 documentation & packaging | **DELIVERED** — this document, `VST3_READINESS.md` round-16 addendum, `native/vst3/VST3_WRAPPER.md`, versioned archive + SHA-256 (see §7) |

## 2. P1 — the documentation reconciliation

Audit method: the delivered round-15 package/tree was checked against what
the current source actually contains and does (85/85, 4/4 hashes, **12**
bench rows — three documents claimed "11"), then every stale statement was
corrected **in place, tagged and dated** (nothing deleted, nothing silently
rewritten). The 16 corrections (full table in the reconciliation log):

1. `native/CMakeLists.txt` — obsolete "src/dsp.cpp is MISSING" salvage note
   (superseded: re-implemented R15-REB1, verified bit-neutral) + "round 12"
   title.
2. `native/README.md` — title/intro/layout/results updated from the
   round-12 state (51 cases) to the 15.0.0 state (85 cases, 12 bench rows,
   96 kHz + surface numbers, neutrality-hash line).
3. `native/VST3_READINESS.md` — mid-incident context caveats marked
   historical + reconciliation blocks (the r15acc battery IS wired and
   green; the 96 kHz/surface rows ARE measured).
4. `native/r15_reference_hashes.txt` — "NEXT AGENT: … NOT wired" footer
   superseded (wired, 85/85, re-verified 2026-10-28; regression instruction
   stands).
5. `evidence/analysis_round15/NOTES_R15P2.md` — FINAL STATUS addendum:
   what was lost at the intermediate stage vs what was subsequently
   restored and verified.
6. `native/include/octavox/dsp.hpp` — title → 15.0.0; the salvage note
   reframed as HISTORICAL incident record + the final implementation status
   (comment-only; the contract text untouched).
7–9. "11 cases" → "12 cases" in `DELIVERY_ROUND15.md`,
   `docs/OCTAVOX_ROUND15_FINDINGS.md`, `README.md` (an off-by-one from the
   worklog's "all 11 cases" phrasing).
10. `docs/OCTAVOX_ROUND15_FINDINGS.md` — "≈30 s" suite claim → the measured
    41.2 s.
11. Hard-coded `/home/z/.local/bin` → environment-drift note (cmake is
    pip-installed per environment; round-16: `/home/z/.venv/bin`).
12. `native/ARCHITECTURE.md` — "51/51" → 85/85 (found by the audit, same
    class of stale count).
13. `poc/poc_run.log` — missing from the tree vs the delivered zip;
    **restored byte-identical** (SHA-256 matches the manifest).

All other files in `poc/`, `evidence/analysis_round{14_recovery,15}/` and
the core sources verified byte-identical to the round-15 delivery (except
the two intentional P1 document corrections above).

## 3. P2 — the reproducible validation workflow

### 3.1 What was added (nothing weakened, all 85 tests stay in the default run)

* **Test harness** (`native/tests/test_main.cpp`): per-case wall durations
  (harness overhead < 0.01 %), live progress lines during execution,
  `--durations` optional end-table column, selection by `--list` /
  `--filter <substr>` (repeatable, OR) / `--group <id>` (9 group ids) /
  `--category unit|long` (AND across kinds), exit codes 0/1/2; JSON
  extended backward-compatibly (per-case `group`/`category`/`duration_ms`,
  an `environment` record, per-group subtotals, wall time).
* **Benchmark** (`native/bench/bench_main.cpp`): `--case/--cases` named
  selection, `--list`, per-row `wall_ms`, `environment` block in the JSON,
  and a **new 13th row `mono_8v_b128_96k_ctrl512`** (the 96 kHz
  detector-cost isolation measurement; the delivered round-15 bench had 12
  rows — this is a new R16 row, marked in the output note).
* **Categories** (static, design intent): 18 `long` cases (stability
  long-runs, R13 cold-start + detector batteries, estimate tracking, r15acc
  robustness/replay) / 67 `unit`, borderline decisions recorded in the
  profile document.
* **Gate integrity proven**: the 220 `check(...)` gate calls in the test
  source are textually identical before/after the harness edit (sha256 of
  the extracted gate text `ed57f594…`); pre-edit sources preserved in
  `evidence/analysis_round16/`.

### 3.2 Measured (this host: 2-core Intel Xeon, GCC 14.2.0, Release -O2)

| run | result | wall |
|---|---|---|
| release full default suite (85 cases) | **85/85** | **40 998.6 ms** (independently re-run twice: 41.1 / 41.0 s) |
| ASan+UBSan full default suite | **85/85, zero sanitizer reports** | **369 541.7 ms = 6.16 min** |
| full bench (13 cases) | 0 allocations everywhere | 30.4 s external / 9.42 s best-loop sum |

The 9.01× ASan/release ratio (uniform 8.4–9.3× per group) **resolves the
round-15 ">5 min in an independent verification environment" report** with
no anomalous host assumption: the sanitizer leg of the verification chain
alone takes 6.2 min here (or a release run on a ~7.5× slower host). Where
the 41 s goes: r13 group 11.3 s (96 kHz detector batteries), stability 7.2 s
(60 s/30 s stress streams), r15acc 7.0 s, r15p1 6.5 s, unity 5.3 s …; the
unit/long split is 50.1 %/49.9 % of wall (18 long cases cost as much as the
other 67). Top cases: `detector_96000hz_sine_battery` 5 805.6 ms,
`longrun_60s_8voice_feedback` 4 468.7 ms, `r15acc_robustness_battery`
2 186.7 ms, `chirp_30s_+700c` 1 947.9 ms, `default_neutrality` 1 909.1 ms.

### 3.3 The reproducible commands

```sh
export PATH=/home/z/.venv/bin:$PATH   # cmake 4.4.4 (pip; path drifts per environment)
cd native
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON && cmake --build build-asan -j
./build/octavox_tests                  # 85/85, ~41 s (progress lines; TOTAL_WALL_MS)
./build/octavox_tests --list           # 85 cases: name  group  category
./build/octavox_tests --group r15acc   # isolate one group (7/7 in ~7 s)
./build/octavox_tests --filter default_neutrality           # isolate one case
./build/octavox_tests --json out.json # machine-readable + environment
./build-asan/octavox_tests            # 85/85, ~6.2 min, zero sanitizer reports
./build/octavox_bench                  # 13 cases (incl. 96k_ctrl512), 0 allocs
./build/octavox_bench --case mono_8v_b128_96k_ctrl512       # one named case
./build/octavox_bench --json out.json  # machine-readable + environment
./build/octavox_r15refcap             # the four neutrality hashes
```

Everything above was actually executed on the current source in this round;
the evidence files are the captured outputs (§7).

## 4. P3 — the 96 kHz profile (measured analysis; no core change)

Main cost: the pitch detector's YIN difference loop
`O(ntau × m)` (`for tau in [tau_min,tau_max)` × `for j in [0,m)`). At 48 kHz:
760 lags × 224 support ≈ 170 K MACs/invocation (measured 83.7 µs); at 96 kHz:
1 520 × 400 ≈ 608 K MACs (measured 315.8 µs, 3.77×; the +5 % over the 3.57×
work ratio is the window working set growing 16→32 KB past L1). The
non-detector base is **254.8 ns/frame at both rates** — the entire 96 kHz
slowdown is detector-side, and its share of the ctrl64 cost grows
73.2 % → 83.2 % (direct reading) / 95.1 % (linear-control model). The 2.0–2.1×
realtime margin is the measured price of the R13-b correctness fix (the
12.0.0 core was blind at 96 kHz: 0.000000 Hz on a 110 Hz tone).

Optimisation assessment (all candidates evaluated, none applied):
FFT/autocorrelation YIN, incremental running sums, `-ffast-math`/float/SIMD
reductions — all reorder arithmetic → break bit-neutrality vs the four
reference hashes and the frozen gates' measured literals; scaling the
default `detection_interval` with sr — a behaviour change, not an
optimisation; cache-blocking — ≤ 5 % upside, not worth a core change. The
already-shipped, tested optimisation is `Config::detection_interval`
(**11.90× realtime** at 96 kHz with interval 512, measured, 0 allocations,
estimate values unchanged — only the cadence). Lagrange6 stays the default;
all quality/RT constraints re-verified by the 85-case suite (release +
ASan). Full analysis: `evidence/analysis_round16/r16_96k_profiling.md`.

## 5. P4 — the VST3 wrapper (implemented, built, validated)

**This is the round's centrepiece.** The VST3 SDK v3.7.12_build_20 was
acquired at pinned SHAs (network clone of the tag + per-component tarballs
at the exact gitlinks; the SDK lives outside the evidence tree and is NOT
redistributed — acquisition record in `native/vst3/VST3_WRAPPER.md`), the
toolchain was proven (the SDK's validator builds and passes its own 43
self-tests), and then the wrapper was implemented and iterated until green.

* **What**: `native/vst3/` — a `SingleComponentEffect` (processor +
  controller in one component, the SDK's supported minimal lifecycle)
  around the **untouched** 15.0.0 core (`MultichannelShifter` per channel).
  38 parameters (Bypass, Pitch ±24 st/1 cent, Voices 1..8, Spread,
  Feedback, Wet/Dry + per voice: Pitch Offset, Gain, Mute, Base Delay
  0–200 ms). Sample-accurate automation by block segmentation at the
  parameter-event offsets. `OVR1` v1 binary state = full Config + the live
  surface (+ bus channels, adapted on restore). Latency = the engine's
  `latencySamples()` (2304 at the defaults); tail = the ring-range-law
  upper bound; bypass = the core's click-free `setBypass`; silence/n=0/
  block-size changes/reset handled; in-place host buffers copied through
  preallocated scratch; **no allocations and no blocking locks in
  `process()`**.
* **Validation** (all actually executed, captured to evidence):
  * the VST3 SDK's own **validator: 47 tests passed, 0 failed** against the
    built `OctavoxNative.vst3` bundle (one development iteration: the
    "Process Format" test's 1 234 567.8 Hz leg is above the core's
    documented (0, 1e6] Hz domain — the wrapper now refuses out-of-domain
    setups in `setupProcessing`, the validator's tolerated path);
  * the in-process wrapper tests **8/8**: lifecycle (factory-created,
    interface-driven), parameter conversions (exactly idempotent),
    observable effects (voices 1-vs-8 differ 0.325; default run AND bypass
    path bit-identical to the hand-driven core), **the required
    equivalence proof** — `segmentation_equivalence_BITEXACT`: the
    wrapper's sample-accurate path vs the core's tested block-boundary
    implementation at the same stream positions, **maxdiff = 0,
    bit-identical** (and 0.509 different from coarse block-boundary host
    timing — the segmentation demonstrably moves the application point);
  * state save/restore: live application, same-instance restore and
    fresh-instance restore all **maxdiff = 0** vs the hand-driven
    reference; corrupt state rejected;
  * `no_alloc_steady_state`: **0 allocations** across 100 process calls
    with parameter events (the project's global-new counter);
  * latency 2304 == the core's `latencySamples()`; in-place == separate
    buffers (maxdiff = 0); host-block-split invariance (maxdiff = 0);
    reset-cycle replay == a fresh engine configured with the preserved
    surface (the cold-start law; maxdiff = 0).
* **Honest scope decisions** (documented in VST3_WRAPPER.md §6): no GUI
  editor; symmetric mono/stereo buses only (sidechains not applicable); no
  mid-stream resume (restore = deterministic from stream position 0); state
  exchange relies on the standard VST3 contract that hosts pause
  processing during `setState`/`getState`.

The two PARTIAL dimensions of the round-15 readiness note are closed by the
wrapper itself (see the round-16 addendum in `native/VST3_READINESS.md`):
plain segmentation IS sample-accurate exactly (no host-side ramp needed —
the core's block-agnostic contract makes the segmented stream identical),
and the state snapshot follows precisely the list the readiness note
prescribed.

## 6. UNK residue & known limitations

* Runtime behaviour of the target: **UNK throughout** (never executed —
  unchanged since round 1). All static closures remain OBS/DER at the
  instruction level; nothing in this round touches the target binary.
* Q-D (host candidate table / scale-key) remains **OPEN** with the recorded
  plan (round-15 scope; not attempted this round by design).
* The bounded static residuals from round 15 are unchanged (the mode-1
  phase trajectory; the record-ring first-field scaling; T+0x5F14's
  pre-setParam value; the kernel table's design formula).
* Wrapper scope: no GUI, symmetric buses, no mid-stream resume (above).
* The 1-cent pitch grid quantizes host automation curves onto cent steps
  (documented conversion; the other domains are continuous).
* The VST3 SDK is NOT redistributed in the evidence package (Steinberg dual
  license); rebuilding the wrapper requires acquiring it at the pinned SHAs
  recorded in VST3_WRAPPER.md — the exact commands are in §7.
* The 96 kHz default-path margin remains 2.0–2.1× realtime (the measured
  R13-b price; the `detection_interval` knob is the tested relief).

## 7. Artifacts & reproduction (working tree: `octavox-work/Octavox_RE_evidence/`)

```sh
# --- P2: the reproducible validation chain (see §3.3 for the full set) ---
./build/octavox_tests --json ../evidence/analysis_round16/r16_acceptance_results.json
./build-asan/octavox_tests --json ../evidence/analysis_round16/r16_acceptance_results_asan.json
./build/octavox_bench --json ../evidence/analysis_round16/r16_bench.json
./build/octavox_r15refcap            # 4/4 hashes

# --- P4: the wrapper ---
cd ../vst3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
#    (the octavoxnative target build runs the validator automatically: 47/47)
./build/bin/Release/octavox_vst3_wrapper_tests     # 8/8, exit 0
./build/validator/Release/validator build/VST3/Release/OctavoxNative.vst3
```

Round-16 evidence (`evidence/analysis_round16/`):

| file | content |
|---|---|
| `R16_P1_RECONCILIATION_LOG.md` | the 16-row correction table + consistency verification |
| `r16_acceptance_results.json` / `r16_suite_stdout_release.txt` | 85/85 release, per-case durations, environment |
| `r16_acceptance_results_asan.json` / `r16_suite_stdout_asan.txt` | 85/85 ASan+UBSan, 6.16 min, zero reports |
| `r16_bench.json` / `r16_bench_stdout.txt` | 13 bench cases, 0 allocations, environment |
| `r16_filter_demo.txt` | `--list` / group / filter / category / bench-case demos |
| `r16_suite_profile.md` | the full profile: top cases, group subtotals, unit/long split, the >5-min explanation, category table, 96 kHz isolation |
| `r16_96k_profiling.md` | the P3 analysis (main cost, candidates, no-change decision) |
| `r16_vst3_wrapper_tests.txt` | the 8/8 wrapper test run with all measured values |
| `r16_vst3_validator.txt` | the SDK validator run: 47/47 |
| `r16_vst3_moduleinfo.json` | the module's generated manifest copy |
| `r16_refcap_check.txt` | the four neutrality hashes re-captured |
| `r16_gate_lines_before/after.txt`, `r16_{test,bench}_main_preedit.cpp.txt` | gate-integrity + pre-edit proofs |

New sources: `native/vst3/` (8 files incl. `VST3_WRAPPER.md`). The core
(`native/src`, `native/cli`), `poc/` and all prior-round evidence are
unchanged (verified byte-identical against the round-15 delivery except the
two intentional P1 document corrections and the two P2 harness files).

Delivery: `Octavox_RE_evidence_round16.zip` — SHA-256 and the checksum
manifest in `DELIVERY_ROUND16.md`; verification from a fresh extraction
performed (manifest OK, build clean, 85/85, 4/4 hashes, wrapper 8/8,
validator 47/47).
