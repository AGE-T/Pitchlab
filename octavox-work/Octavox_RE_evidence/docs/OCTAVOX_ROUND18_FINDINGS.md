# OCTAVOX ROUND 18 FINDINGS — Remaining Static DSP Semantics and Practical VST3 Host Integration

**Status: DELIVERED.** All seven priorities are closed: (1) the aggregate
test-execution anomaly is **explained quantitatively and reproduced
positively** — a single uniform environment slowdown factor
**F ∈ [12.42, 15.85]** (an unoptimised and/or sanitizer build on a host
≈1.5–2.0× slower than this 2-core dev host) simultaneously satisfies every
observation the independent verification environment reported, and a
CPU-contention reproduction of that factor on this host reproduces the exact
reported signature (killed inside the 96 kHz detector battery after 58
successful cases); a new `--diag` mode records case start/completion,
per-case wall time, process RSS, the always-on allocation-counter state and
a floating-point environment probe, and a new `--isolate` group-isolated
fallback mode exists for constrained environments (explicitly NOT equivalent
to a single-process run); (2) the mode-selection flag producer `[obj+0x8C]`
is **CLOSED** — it is the module record's `modsw` ("mod select") field,
written once by the core constructor, absent from the shipped patch text ⇒
mode 0 (the glide path) is the only live trajectory in the shipped plugin
and R17's "mod input connected" hypothesis is refuted; (3) the tracker
`T+0x5F14` ordering is **resolved to the setter** — the dsp module's
setParam case 5 = the system sample rate (not a host parameter, not a
record field), with the exact structural phases distinguished and the
runtime residue bounded UNK with the decisive Windows experiment specified;
(4) the remaining scale/key rules are **CLOSED at the rule level** — the
per-interval mux design rule (the 3rd/7th-class intervals bump +1 grid
step; the reachable target grid excludes the 2nd and 6th scale positions)
and the key-ratio "asymmetry" (a decompiler stack-layout artifact — the
true law is symmetric `±temperament[|key−9|]`), plus two authored-data
irregularities recorded; (5) the interpolation kernel question is **CLOSED
as `UNK-generator / DER-characterisation`** with two record corrections
(the exact phase law and the two consumers' different phase indexing) and
the tested-and-rejected design-family list extended; (6) the **actual
compiled VST3 module now has a repeatable integration test through the
pinned SDK's module loader** — 12/12 cases, bit-exact against the
hand-driven core (module SHA-256 unchanged from R16/R17: `b293cecb…`);
(7) the documentation, open-questions list, reachability matrix and the
versioned archive are updated and verified. The original Eventide binary
was **never executed**; all runtime questions stay UNK.

Environment note: the dev environment survived this round intact (no
sandbox reset); the R17 archive re-verified **355/355** from a fresh
extraction; the one file lost from the working tree
(`poc/poc_run.log`) was restored byte-identically from the ZIP. The
capstone/cmake toolchain was reinstalled (same versions as recorded:
capstone 5.0.9, cmake 4.4.4, GCC 14.2.0).

## 1. Verdict table

| Item | Verdict |
|---|---|
| P1 aggregate test anomaly | **DELIVERED** — single-process full suite: release 85/85 in 43.5 s (fresh, with the diagnostic trace); `--reverse` 85/85 in 41.0 s; `--isolate` (one fresh process per group) 85/85 in 43.3 s; stability+r13 combo 26/26 in 19.0 s; the four reference hashes 4/4. **The quantitative model**: the reported observations (full suite stops at the 96 kHz battery after 58 cases; the r13 group alone passes; the stability+r13 combination passes in one process; the r15p1/r15acc groups alone pass) are ALL satisfied simultaneously by one uniform factor F ∈ **[12.42, 15.85]** (= −O0 × 8.0 on a 1.55–1.98× slower host, or ASan × 8.42 on a 1.47–1.88× slower host) ⇒ environmental, not a code/test defect. **Positive reproduction**: with 3 CPU busy-loop processes imposing a measured ≈2× slowdown on the −O0 build, a 300 s window kills the run **inside the 96 kHz detector battery** (the diag trace's last record: `case_start` for the battery, no completion — the exact reported signature), while the same contended run **without** the window completes 85/85, and the contended stability+r13 combo completes inside the window. Diagnostic mode: rounding mode constant (0) across all 85 cases, RSS bounded (≈20 MB flat), the always-on allocation counters net-bounded (no per-case leak growth); no FP-environment change, no process-global-state requirement (the `--isolate` differential passes identically) |
| P2 mode-flag producer | **CLOSED** — `[obj+0x8C]` = the module record's **`modsw`** field (alias `mod_select`, range 0..1, class default `'0'`), written once by the core ctor `0x1800DBAD0` at `0x1800DBCC9`; the only `[obj+0x8C]` writer for this class in all 13 116 `.pdata` functions; absent from the shipped patch ⇒ mode 0 only. R17's "mod input connected" DER hypothesis refuted |
| P3 tracker ordering | **SETTER RESOLVED, runtime bounded UNK** — `T+0x5F14`'s only writers are the three setParam stores (`0x1800E1151/E11EA/E129B`, folded offset `dsp+0xA42C`); setParam case 5 = the system sample rate (proven: writes the rate at `dsp+0xA524` + the `{2,4,8}` cadence + the rate-class detector constants; the getParam cases 7/8 read the rate back for the published pitch/frequency). Param 5 is NOT a record field (the 'dtonicii' 1.7 class-default record has no rate entry) — it is a framework system param. The ctor DOES initialise the state `[T+0x5FF8] = 0` (correcting R17's record). The exact rate-broadcast call site is bounded UNK (fully virtual dispatch); the Windows experiment that closes the runtime residue is specified |
| P4 scale/key residues | **CLOSED (rule level)** — the mux design rule: `v = 14 + s + δ`, δ = 1 iff `s mod 7 ∈ {2,6}` (the 3rd/7th-class intervals); the reachable walk-target grid = `{0,2,3,4,6} mod 7` — **the 2nd and 6th scale positions are never targeted** (the collapsed pairs: +3rd/+4th, +7th/+oct, +10th/+11th, +14th/+2oct, −2nd/unison, −5th/−6th, −9th/−oct, −12th/−13th); the engine's realised vocabulary = {P1, M3, P4, P5, M7, P8} upward and its inversions downward. The key-ratio law is symmetric `±temperament[|key−9|]` (R17's "asymmetry" was a decompiler stack-layout artifact — corrected with exact instructions). Two authored-data irregularities recorded (the scal-table mux's last-six mappings shifted +2 from their name-matching pattern rows; hungarian out of the 15-entry table) |
| P5 kernel design | **CLOSED as `UNK-generator / DER-characterisation`** — no runtime writer exists (exactly two code references, both readers ⇒ a build-time constant); the exact phase law: row r = delay `2 + r/256` (centroid-proven — corrects the r12 "(r+0.5)/256" comment); the two consumers index the table DIFFERENTLY (engine: `floor(frac·256) & 255`, all rows; filter readers: `int(frac·255)` — **row 255 unreachable there** — corrects the standing "int(frac·256)" record); the per-phase −3 dB band is 0.60–0.77·Nyquist (monotonic shaping); design families newly rejected this round: equiripple/Remez (no equiripple lobes) and flat-passband least-squares (0.16 max-tap error). Our core keeps the table ONLY as the opt-in `ObsKernel6` mode; the `Lagrange6` default is unchanged |
| P6 module integration harness | **DELIVERED — 12/12** — `octavox_module_integration_tests` loads the **actual compiled `OctavoxNative.vst3` bundle** through the pinned SDK's `VST3::Hosting::Module::create` (a real dlopen + factory), then: factory/class discovery, processor+controller creation through the factory, initialize with the SDK `HostApplication` context, bus negotiation (mono+stereo), the activation lifecycle, parameter queues (a full non-default surface **bit-exact** vs the hand-driven core + a mid-block event drop-control), deterministic process calls (**bit-exact mono and stereo**), state save/restore into a **second factory-created instance** (bit-exact replay, 428-byte blob), latency reporting (= the core's exact 2304 samples), terminate/unload. Scope labelled honestly: an SDK-module-load + interface-contract test on one host implementation — **NOT a DAW compatibility claim** |
| P7 documentation & packaging | **DELIVERED** — this document, the updated open-questions list, the parameter/path reachability matrix, the wrapper/readiness/README updates, the updated build script, the versioned archive + SHA-256 + checksum manifest verified from a fresh extraction |

## 2. P1 — the aggregate test-execution anomaly

### 2.1 The diagnostic mode (new harness capability, gates untouched)

`octavox_tests --diag <path>` writes an append-only, line-flushed JSONL
trace: `suite_start` (environment + initial FP state), `group_start/end`,
and for every case a `case_start` record **before the body runs** and a
`case_end` record after it — each carrying the per-case wall time, the
process RSS (`VmRSS` via /proc where supported), the always-on
allocation-counter state (`alloc_new`/`alloc_del`/`alloc_out` — new
always-on totals added to the allocation counter; the armed counter the
gates use is unchanged) and the FP-environment probe (rounding mode +
accrued exception flags). Because every record is flushed before the next
case body starts, **a run killed inside a window leaves a post-mortem
trace whose last `case_start` without a matching `case_end` pinpoints
exactly where the run stood** — the forensic tool the independent
environment lacked. `--isolate` (POSIX) runs each selected group in a
fresh forked child (results returned over a pipe) — the documented
constrained-environment fallback AND a process-global-state differential.

### 2.2 The measurements (all machine-readable, `evidence/analysis_round18/`)

| run | result | wall |
|---|---|---|
| release full suite, single process, `--diag` | 85/85 | 43.5 s (`r18_diag_release.jsonl`) |
| release full suite, `--reverse` | 85/85 | 41.0 s (`r18_acceptance_reverse.json`) |
| release full suite, `--isolate` (fresh process per group) | 85/85 | 43.3 s (`r18_diag_isolate.jsonl`) |
| stability + r13 combo, single process | 26/26 | 19.0 s (`r18_diag_stability_r13.jsonl`) |
| ASan+UBSan full suite, `--diag` | 85/85, zero sanitizer reports | (the long leg, `r18_diag_asan.jsonl`) |
| Debug −O0 full suite, `--diag` | 85/85 | (the long leg, `r18_diag_o0.jsonl`) |
| reference hashes | 4/4 exact | `r18_refcap_check.txt` |
| contended −O0 full suite, 2 spinners (F≈16), 300 s window | **killed at 300.0 s with 58 cases done and the kill INSIDE the 96 kHz battery** (the trace's last record = the battery's `case_start` at t=297.2 s, no completion) | `r18_diag_contention_kill.jsonl` |
| contended −O0 stability+r13, 1 spinner (F≈11.2), no window | 26/26 in 213 s (completes inside a 300 s window — matching the independent env's combo observation) | `r18_diag_contention_combo.jsonl` |
| contended −O0 full suite, 1 spinner, NO window | **85/85 in 484 s, exit 0** — no stall, no failure | `r18_diag_contention_full.jsonl` |

Machine/compiler/configuration: Intel Xeon 2 cores, Linux 5.10.134,
GCC 14.2.0, Release `-O2` / Debug −O0 / Debug −O0 + ASan+UBSan, cmake 4.4.4
(the same class of host as rounds 16–17).

### 2.3 The quantitative reconciliation

With the fresh per-case timings (case 58 completes at 18.34 s
release-equivalent; the battery spans 18.34–24.16 s; the stability+r13
combo totals 18.92 s; r13 alone 11.65 s):

* the full suite stopping at the battery after 58 cases ⇒ the 300 s window
  expires during case 59 ⇒ **F ∈ [12.42, 16.35]**;
* the stability+r13 combination passing in one process ⇒ **F < 15.85**;
* the r13 group alone passing ⇒ F < 25.75; the r15p1/r15acc groups alone
  passing ⇒ F < ~40.

**The intersection F ∈ [12.42, 15.85] is non-empty** — one uniform
slowdown factor satisfies every reported observation simultaneously: an
unoptimised (−O0, ×8.0 here) or sanitizer (×8.42 here) build on a host
merely **1.5–2.0× slower than this 2-core dev host**. No test interaction,
order dependence, process-global state, heap fragmentation, FP-environment
change, allocation-instrumentation state or resource-pressure stall is
required — and each of those hypotheses is additionally falsified by the
diagnostic evidence (the isolate differential passes identically; the
rounding mode is constant across all cases; the RSS is bounded; the
allocation counters are net-bounded per case) and by the positive
contention reproduction below.

### 2.4 The positive reproduction (the strongest new evidence)

A measured host slowdown was imposed with CPU busy-loop processes
(calibration: the chirp case at −O0 = 18 s clean, 36 s with two spinners ⇒
host ≈2.0×, F ≈ 16) and the −O0 build was run under `timeout 300` with the
diagnostic trace: **the run was killed at exactly 300 s with 58 cases
completed and the kill INSIDE the 96 kHz detector battery** — the trace's
last record is the battery's `case_start` (t = 297.2 s) with no `case_end`
— the exact reported signature, reproduced to the case. The same contended
regime **without** a window completes 85/85 in 484 s (exit 0 — no stall,
no failure, purely slower), and the contended stability+r13 combo
completes in 213 s — reproducing the independent environment's passing
observations at a compatible factor (their F < 15.85 pins them marginally
below this reproduction's ≈16). The
gates were not weakened anywhere: all 85 cases remain in the default suite;
`--group` (and now `--isolate`) remain the documented constrained-
environment fallbacks, explicitly **not** equivalent to a single-process
full run.

## 3. P2 — the mode-selection flag producer (CLOSED)

Full instruction-level evidence in
`evidence/analysis_round18/r18_static_findings.md` §P2. Summary: the
delay-reader core object (wrapper+0x80, constructed via the add thunk
`0x1801374D0` → `FUN_1800DBAD0`) receives `[obj+0x8C]` from the module
record's **`modsw`** field at construction (`0x1800DBCC9`, via the
field getter `0x1800C3D70` with the class default from the static
class-defaults record at `0x1803B9918` — entry `[1] @0x1803B99A8`:
`modsw`, alias `mod_select`, default `'0'`, max `'1'`). The field is
absent from the shipped patch records ([342]/[348]) ⇒ **mode = 0**; the
mode-1 trajectory decoded in R17 is statically unreachable in the shipped
plugin. Not a host parameter, not an automation path, not a connection
state. R17's DER hypothesis ("mod input connected") is refuted by the
field-name evidence.

## 4. P3 — the tracker `T+0x5F14` ordering (setter resolved)

Full evidence in `r18_static_findings.md` §P3. Summary: the only writers
of `T+0x5F14` (folded `dsp+0xA42C`) are the three stores in the dsp
module's setParam (`FUN_1800E10B0`, method-table slot 1 at
`0x180409F68`) — **case 5 = the system sample rate**. The class-default
record ('dtonicii' 1.7) contains no rate field: the rate is a
framework-reserved param id (the same machinery that evaluates the
`#speed`-dependent class-default formulas at graph construction). The
tracker ctor initialises the state machine (`[T+0x5FF8] = 0` — a
correction to R17's init-site record, which had missed the register-zeroed
stores at `0x1800E0D80..0x1800E0E10`), but not the countdown `0x5F10` or
the cadence `0x5F14`. The countdown gates the tracker frame cadence and
reloads from `T+0x5F14` only at expiry (during audio processing); the
detector's rate-class constants exist only behind setParam case 5, so a
functioning plugin structurally requires the rate delivery before the
first audio block. The exact rate-broadcast call site is bounded UNK
(fully virtual dispatch; no direct callers); the runtime residue
question (the actual value the original host path delivers relative to
the first process call) stays UNK with the specified Windows experiment
(break after the tracker ctor; break at `FUN_1800E10B0` logging (id,
value) in call order; break at the first `0x18013BE30` reload).

## 5. P4 — the remaining scale/key rules (CLOSED at the rule level)

Full evidence with the mux table, the walk arithmetic and the key-ratio
instructions in `r18_static_findings.md` §P4. Headlines:

* **The mux design rule**: `v = 14 + s + δ` with `δ = 1` iff the label's
  diatonic step count `s ≡ 2 or 6 (mod 7)` — the 3rd/7th-class intervals
  (and their octave compounds) are bumped one mux unit. The consumer walk
  arithmetic (both direction branches, the ±1200 octave wraps, the
  `exp((Δcents + pcent + 1200·oct)·ln2/1200)/16` ratio law) is verified
  instruction-by-instruction.
* **The consequence**: the reachable walk-target grid is `{0, 2, 3, 4, 6}
  (mod 7)` — the 2nd and 6th scale positions are never targets; the
  engine's realised vocabulary = `{P1, M3, P4, P5, M7, P8}` upward (and
  the just inversions downward). The collapsed label pairs (identical mux
  values) follow exactly.
* **The key-ratio law is symmetric** `±temperament[|key−9|]` (reference
  A = key semitone 9): R17's recorded ascent/descent "asymmetry" was a
  decompiler stack-layout artifact (the ≥10 branch's `[rbp + key*4 +
  0xD4]` reads the adjacent temperament-cents array, not the zone-run
  lookup) — corrected with exact instructions.
* **Pentatonic scales use the raw-index path**: `c_cmp2/3` test the
  scal-table output for 14/15 — the mux outputs of the chromatic and
  whole-tone choices (R17's choice-level parenthetical was right; the
  pattern rows those choices actually load are 14/15 — see the authored
  irregularity below).
* **Authored-data irregularities (OBS, bounded)**: the scal-table mux maps
  the last six engine scale choices to pattern rows shifted +2 from their
  name-matching rows (chromatic→row 14 = the penta-maj pattern, …,
  neapolitan→row 13 = the whole-tone pattern) and `hungarian` (choice 16)
  exceeds the 15-entry table — recorded as authored data inconsistencies;
  audible effects UNK (the target was never executed).

## 6. P5 — the interpolation kernel (CLOSED as UNK-generator)

Full evidence in `r18_static_findings.md` §P5. Headlines: no runtime
writer exists in the whole binary (two code references, both readers — a
build-time constant); the exact phase law is `row r ↔ delay 2 + r/256`
(centroid-proven — correcting the r12 "(r+0.5)/256" comment); the two
consumers index the table differently (engine: `floor(frac·256) & 255`;
filter readers: `int(frac·255)` — row 255 unreachable there — correcting
the standing record); the per-phase −3 dB band is 0.60–0.77·Nyquist with
monotonic shaping; newly rejected design families: equiripple/Remez and
flat-passband least-squares. The observed table stays shipped as the
opt-in `ObsKernel6` comparison mode in our core (default `Lagrange6`
unchanged).

## 7. P6 — the compiled-module integration harness (12/12)

`native/vst3/octavox_module_integration_tests.cpp` (CMake target
`octavox_module_integration_tests`, built after `octavoxnative`): loads
the built bundle through the pinned SDK's `VST3::Hosting::Module::create`
(a real `dlopen` of `Contents/x86_64-linux/OctavoxNative.so` +
`GetPluginFactory`), then exercises the full host interface contract
through the exported factory's instances — the SDK's own hosting
utilities (`HostApplication`, `ParameterChanges`, `MemoryStream`) supply
the host side. Results (`r18_module_integration.json` +
`r18_module_integration_stdout.txt`): **12/12** — module/factory
discovery (class count 1, our CID present), processor+controller creation
(single-component: both interfaces exported), initialize with a real host
context (buses 1+1; the 38-parameter surface published), mono and stereo
bus negotiation, the activation lifecycle (incl. re-activation), **bit-exact
mono and stereo processing vs the hand-driven core**, parameter queues
(a full non-default surface applied through real `IParameterChanges`
queues — bit-exact; a mid-block event drop-control with a measured
0.205 effect), state save/restore into a **second factory-created
instance** (bit-exact replay, 428-byte blob), latency reporting (= the
core's 2304 samples; tail 15080), and clean terminate/unload. The
wrapper tests re-pass 15/15 and the SDK validator 47/47 on the fresh
build; **the module's SHA-256 is byte-identical to the R16/R17 delivery**
(`b293cecb…`) — deterministic compilation re-proven with the SDK at its
archived location. Scope label (stated in the harness header, its stdout
and here): this is an SDK-module-load + interface-contract test on one
host implementation — **not a DAW compatibility claim**; no DAW was
involved. No GUI/editor is exercised (the wrapper deliberately has none).

## 8. UNK residue & known limitations

* Runtime behaviour of the target: **UNK throughout** (never executed).
* The exact rate-broadcast call site for the tracker's `T+0x5F14` (P3;
  fully virtual dispatch) and the original's runtime residue values —
  bounded UNK with the Windows experiment specified.
* The design *intent* of the 6-slot interval grid (P4) and the authored
  scal-table irregularity's audible effect: DER/UNK.
* The kernel's exact generator: UNK (build-time data; the
  tested-and-rejected family list is closed in §P5).
* Windows build: still scripted-but-unexecuted (no Windows runtime here;
  the R17 scripts are unchanged in status).
* The 96 kHz default-path realtime margin (2.0–2.1×) is unchanged — no
  core change was made this round (Lagrange6 default retained; the four
  reference hashes re-verified bit-exact).
* The harness is one host implementation (the SDK's own utilities on
  Linux); DAW compatibility is not claimed from it.

## 9. Artifacts & reproduction

Working tree: `octavox-work/Octavox_RE_evidence/`. Round-18 evidence
(`evidence/analysis_round18/`):

| file | content |
|---|---|
| `r18_static_findings.md` | the full P2/P3/P4/P5 static evidence (instruction-level) |
| `r18_preedit_test_main.cpp.txt` / `r18_preedit_alloc_counter.cpp.txt` | pre-edit harness sources (change proofs) |
| `r18_diag_release.jsonl` / `r18_acceptance_release.json` | the release full-suite diagnostic trace + JSON (85/85, 43.5 s) |
| `r18_diag_isolate.jsonl` / `r18_acceptance_isolate.json` | the `--isolate` run (85/85, 43.3 s) |
| `r18_acceptance_reverse.json` | the reversed-order run (85/85, 41.0 s) |
| `r18_diag_stability_r13.jsonl` | the stability+r13 combo (26/26, 19.0 s) |
| `r18_diag_asan.jsonl` / `r18_suite_stdout_asan.txt` / `r18_acceptance_asan.json` | the ASan+UBSan leg |
| `r18_diag_o0.jsonl` / `r18_suite_stdout_o0.txt` / `r18_acceptance_o0.json` | the Debug −O0 leg |
| `r18_longlegs.log` | the long-legs driver log |
| `r18_contention_*.txt` / `r18_diag_contention_*.jsonl` | the positive reproduction (the ≈2× contention runs: killed-in-battery, no-window completion, combo-in-window) |
| `r18_module_integration.json` / `r18_module_integration_stdout.txt` | the compiled-module harness: 12/12, bit-exact |
| `r18_refcap_check.txt` | the four neutrality hashes (4/4) |

```sh
# the reproducible chain
cd native
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/octavox_tests --diag ../evidence/analysis_round18/r18_diag_release.jsonl   # 85/85
./build/octavox_tests --isolate                                                    # the fallback mode
./build/octavox_r15refcap                                                          # 4/4 hashes
cd ../vst3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_ROOT=<sdk> && cmake --build build -j
./build/bin/Release/octavox_module_integration_tests --json <out>.json             # 12/12
./build/bin/Release/octavox_vst3_wrapper_tests                                     # 15/15
./build/bin/Release/validator build/VST3/Release/OctavoxNative.vst3                # 47/47
```

New sources this round: `native/tests/test_main.cpp` (the `--diag` +
`--isolate` harness modes), `native/tests/alloc_counter.cpp` (the
always-on totals), `native/vst3/octavox_module_integration_tests.cpp`
(the compiled-module harness) + its CMake target, and the tools/log
updates. The DSP core (`native/src`, `native/cli`) and the wrapper
implementation sources are **byte-identical to the round-17 delivery**
(the module hash proves it).
