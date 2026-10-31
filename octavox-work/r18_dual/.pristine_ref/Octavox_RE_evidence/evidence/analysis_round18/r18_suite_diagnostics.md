# R18-P1 — the aggregate-suite diagnostic results and the anomaly reconciliation

## 1. The diagnostic mode (new capability; gates untouched)

`octavox_tests --diag <path>` (JSONL, line-flushed before each case body):
`suite_start` (environment + initial FP state), `group_start/end`, per-case
`case_start` (before the body) + `case_end` (after), each carrying the
per-case wall time, `rss_kb` (VmRSS), the always-on allocation-counter
state (`alloc_new/alloc_del/alloc_out`) and the FP-environment probe
(rounding mode + accrued exception flags). `--isolate` runs each group in
a fresh forked child (the constrained-environment fallback; NOT equivalent
to a single-process run). The 85-case default suite and every gate are
unchanged; the armed allocation counter the gates use is unchanged.

## 2. The single-process runs (this round, this dev host)

| run | result | wall | trace |
|---|---|---|---|
| release, single process, `--diag` | 85/85 | 43.5 s | `r18_diag_release.jsonl` |
| release, `--reverse` | 85/85 | 41.0 s | `r18_acceptance_reverse.json` |
| release, `--isolate` (fresh process per group) | 85/85 | 43.3 s | `r18_diag_isolate.jsonl` |
| stability + r13 combo, single process | 26/26 | 19.0 s | `r18_diag_stability_r13.jsonl` |
| Debug + ASan/UBSan, single process, `--diag` | 85/85, zero sanitizer reports | 363.9 s (6.07 min) | `r18_diag_asan.jsonl` |
| Debug −O0, single process, `--diag` | 85/85 | 352.6 s | `r18_diag_o0.jsonl` |
| reference hashes (`octavox_r15refcap`) | 4/4 exact | ~2 s | `r18_refcap_check.txt` |

Host: Intel Xeon 2 cores, Linux 5.10.134, GCC 14.2.0, cmake 4.4.4 —
the same host class as the R16/R17 records (release 41–43 s, −O0 352–353 s,
ASan 363–373 s: consistent across rounds).

## 3. The hypothesis elimination (the task's list)

| hypothesis | evidence against it |
|---|---|
| earlier test group execution | the reversed-order run passes 85/85 with the same per-case durations; the stability+r13 combo (the independent env's passing prefix) passes; the battery costs the same isolated vs in-suite (R17) |
| persistent process-global state | `--isolate` (fresh process per group) passes identically to the single-process run — the differential is negative |
| heap / memory fragmentation | RSS is bounded and flat across the whole suite (≈20 MB after the first cases; no growth trend); the always-on allocation counters are net-bounded per case (alloc_out ≈ 220 at the end; no per-case leak) |
| floating point environment changes | the rounding mode is **constant (0 = round-to-nearest) across all 85 cases** in every trace; the exception flags accumulate (sticky, as designed — FE_INEXACT=32) but never affect arithmetic |
| allocation instrumentation state | the always-on counters show balanced new/delete per case; the armed counter the gates use is reset/armed per case and unchanged |
| resource pressure / host scheduling | the positive reproduction (§5): an imposed ≈2× CPU slowdown reproduces the exact reported signature, and the same contended run WITHOUT a window completes 85/85 — a pure slowdown, no stall |
| a genuine order-dependent behaviour | no combination of the above; the uniform-factor model (§4) explains every observation with ONE scalar |

## 4. The quantitative reconciliation (one uniform factor)

Fresh per-case timings (release-equivalent): case 58 completes at
**18.34 s**; the 96 kHz battery (case 59) spans **[18.34, 24.16] s**
(5.82 s); the stability+r13 combo totals **18.92 s**; r13 alone
**11.65 s**. The independent verification environment reported: the full
suite stopped at the battery after 58 successful cases; the complete r13
group passed independently; the stability+r13 combination passed in one
process; all remaining r15p1/r15acc cases passed independently. With a
300 s window and a uniform slowdown factor F:

* full suite stops at the battery after 58 cases ⇒ **F ∈ [12.42, 16.35]**;
* stability+r13 combo passes ⇒ **F < 15.85**;
* r13 alone passes ⇒ F < 25.75;
* r15p1/r15acc alone pass ⇒ F < ~40.

**The intersection F ∈ [12.42, 15.85] is non-empty**: a −O0 build
(×8.0 on this host) on a host 1.55–1.98× slower, or an ASan build (×8.42)
on a host 1.47–1.88× slower, satisfies ALL FOUR observations
simultaneously. Nothing about the tests, their order, their interaction or
the gates needs to change — the anomaly is environmental (the R17
conclusion, now with an exact feasible interval and a positive
reproduction).

## 5. The positive reproduction (the exact signature, to the case)

A measured host slowdown was imposed with CPU busy-loop processes
(calibration: the `chirp_30s_+700c` case at −O0 = 18 s clean, 36 s with
two spinners ⇒ host factor ≈ 2.0×, F ≈ 16). Then:

| experiment | result | evidence |
|---|---|---|
| **−O0 full suite, 2 spinners, `timeout 300`, `--diag`** | **killed at exactly 300 s with 58 cases completed and the kill INSIDE `detector_96000hz_sine_battery`** — the trace's last record is the battery's `case_start` (t = 297.18 s) with no `case_end`. **This is the exact reported signature: "stopped at the 96 kHz detector battery after 58 successful cases."** | `r18_diag_contention_kill.jsonl`, `r18_contention_kill_stdout.txt` |
| stability + r13 combo, 1 spinner (F ≈ 11.2), no window | 26/26 in 213 s — completes inside a 300 s window (matching the independent env's combo observation; their F < 15.85) | `r18_diag_contention_combo.jsonl`, `r18_contention_combo_stdout.txt` |
| **−O0 full suite, 1 spinner, NO window** | **85/85 in 484 s, exit 0** — contention only slows the suite; no stall, no failure, no case regression | `r18_diag_contention_full.jsonl`, `r18_contention_full_stdout.txt` |

The post-mortem value of the diagnostic trace is demonstrated by the kill
run: even though stdout only shows the progress lines up to case 58, the
trace's final `case_start`-without-`case_end` record pinpoints the in-flight
case exactly — the forensic capability the independent environment lacked.

## 6. The fallback for constrained environments

The full 85-case single-process suite remains THE canonical validation
(all cases enabled; no gate weakened). For environments that cannot
budget its wall time, the documented fallbacks are:
`octavox_tests --group <id>` (per-group processes, R16+) and the new
`octavox_tests --isolate` (one fresh forked process per group, aggregated
results). **Neither is equivalent to a single-process full run and neither
is claimed to be**; a verification window must budget ≥ ~1 min (release)
or ≥ ~6–7 min (−O0/ASan) per process on comparable hardware, plus any
host slowdown factor.
