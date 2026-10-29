# R16-P2 — Suite profile & reproducible workflow record (2026-10-28)

Task: R16-P2 "Make the full validation workflow reproducible" (Octavox RE
native DSP core 15.0.0). All numbers below were ACTUALLY MEASURED in this
task on this host (see the environment record); nothing is estimated.

Host / build environment (from the JSON `environment` records, captured by
the harnesses themselves):
- os: Linux 5.10.134-013.15.kangaroo.al8.x86_64
- cpu: Intel(R) Xeon(R) Processor, 2 cores online
- compiler: GCC 14.2.0 (`__VERSION__` = "14.2.0"), cmake 4.4.4
- release build: `-O2`, `build_type: "Release"`, sanitizers off
- sanitizer build: Debug + `-fsanitize=address,undefined`, `build_type:
  "Debug"`, `sanitizers: true` (the definitions are passed by CMake as
  `OCTAVOX_BUILD_TYPE="Release"` / `OCTAVOX_SANITIZERS=1`)

Evidence files (all under `evidence/analysis_round16/`):
`r16_acceptance_results.json` + `r16_suite_stdout_release.txt` (release full
run), `r16_acceptance_results_asan.json` + `r16_suite_stdout_asan.txt`
(ASan+UBSan full run), `r16_bench.json` + `r16_bench_stdout.txt` (13-case
bench), `r16_filter_demo.txt` (selection demos), `r16_refcap_check.txt`
(neutrality hashes), `r16_gate_lines_before.txt` /
`r16_gate_lines_after.txt` + `r16_test_main_preedit.cpp.txt` (gate-integrity
proof).

---

## (a) Suite profile — where the 41 s goes (release, full default 85 cases)

**TOTAL_WALL_MS 40998.6** (harness-internal wall clock over the 9 group
functions; external `date`-based wall of the whole process: 41003 ms;
sum of the 85 per-case durations: 40998.2 ms — bookkeeping overhead
< 0.01%).

Per-group subtotals (release):

| group | cases | duration_ms |
|-------|-------|-------------|
| r13 | 20 | 11336.8 |
| stability | 6 | 7164.9 |
| r15acc | 7 | 6974.3 |
| r15p1 | 7 | 6476.6 |
| unity | 30 | 5257.9 |
| rates_formats | 4 | 1802.7 |
| prepare_reset | 6 | 1114.5 |
| voices | 2 | 446.4 |
| stereo | 3 | 423.8 |

Top-15 most expensive cases (release):

| ms | case | group | category |
|----|------|-------|----------|
| 5805.6 | `detector_96000hz_sine_battery` | r13 | long |
| 4468.7 | `longrun_60s_8voice_feedback` | stability | long |
| 2186.7 | `r15acc_robustness_battery` | r15acc | long |
| 1947.9 | `chirp_30s_+700c` | stability | long |
| 1909.1 | `default_neutrality` | r15p1 | unit |
| 1899.8 | `detector_96k_estimate_stability` | r13 | long |
| 1602.9 | `per_voice_ratios_independent` | r15p1 | unit |
| 1468.6 | `rate_96000_+400c` | rates_formats | unit |
| 1445.6 | `detector_estimate_tracking_96000` | r13 | long |
| 1196.3 | `r15acc_ratio_change_isolation` | r15acc | unit |
| 1157.9 | `r15acc_stereo_surface` | r15acc | unit |
| 828.3 | `per_voice_delay_isolation` | r15p1 | unit |
| 819.9 | `r15acc_level_accuracy` | r15acc | unit |
| 809.5 | `r15acc_reset_reprepare_determinism` | r15acc | long |
| 789.3 | `per_voice_gain_ramp_and_mute` | r15p1 | unit |

Unit vs long split (design-intent categories, see (d)):

| category | cases | cumulative ms | share of suite wall |
|----------|-------|---------------|---------------------|
| unit | 67 | 20535.1 | 50.1 % |
| long | 18 | 20463.1 | 49.9 % |

The split is almost exactly 50/50 in wall time: 18 "long" cases cost as
much as the other 67 combined. The single most expensive group is r13
(11.3 s, dominated by the detector batteries at 96 kHz — the R13-b
sample-rate-relative window law makes every detector pass at 96 kHz
~2x the samples of a 48 kHz pass), then the 60 s / 30 s robustness
streams (stability), then the r15acc battery and the r15p1 surface
batteries (each of those cases internally runs 4-8 full engine passes).

## (b) Why a full validation run can take >5 minutes

Measured release-vs-ASan ratio on this host (identical source, identical
deterministic inputs):

| run | wall |
|-----|------|
| release full 85-case suite | 40998.6 ms (41.0 s) |
| ASan+UBSan (Debug) full 85-case suite | 369541.7 ms (6.16 min) |
| **ratio** | **9.01x** |

The slowdown is uniform across the suite (per-group ASan/release ratios
8.4x-9.3x; e.g. `detector_96000hz_sine_battery` 52517.7 ms vs 5805.6 ms =
9.05x, `longrun_60s_8voice_feedback` 40498.9 ms vs 4468.7 ms = 9.06x).

The round-15 delivery was reported as ">5 min" in an independent
verification environment while the development environment measured
41.2 s (R16-R0). The measurements above resolve the contradiction without
assuming an anomalous host: an ASan+UBSan validation run takes 6.2 min on
this 2-core Xeon, i.e. **a sanitizer verification run alone accounts for
the reported >5 min** (the round-15 verification chain prescribed
release + ASan runs; the >5 min figure is consistent with the sanitizer
leg of that chain, or with a release run on a ~7.5x slower host: 41 s x
7.5 = 5.1 min). The suite itself is dominated by real DSP work on
multi-second streams (60 s noise, 30 s chirp, 4x3 s 96 kHz detector
batteries), not by harness overhead: the harness bookkeeping (timing,
progress lines, table printing) sums to < 0.01% of the wall time.

## (c) Reproducible workflow — exact commands

Run from `native/` (put the cmake bin dir on PATH first, e.g.
`export PATH=/home/z/.venv/bin:$PATH` — the pip-installed cmake location
drifts per environment):

```sh
# one-time builds (Release + ASan+UBSan)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-asan -j

# 1. full default suite (all 85 cases; exit 0 iff all pass)
./build/octavox_tests --json ../evidence/analysis_round16/r16_acceptance_results.json \
    2>&1 | tee ../evidence/analysis_round16/r16_suite_stdout_release.txt
#    stdout ends with: "octavox-native 15.0.0: 85 passed, 0 failed, 85 total"
#    and "TOTAL_WALL_MS <ms>"; the JSON carries per-case duration_ms,
#    group, category, the environment record and per-group subtotals.

# 2. per-case durations in the END table as well
./build/octavox_tests --durations            # adds the DURATION(ms) column

# 3. selection (repeatable; OR within a kind, AND across kinds)
./build/octavox_tests --list                 # 85 cases: name  group  category
./build/octavox_tests --group r15acc         # one group
./build/octavox_tests --group r13 --category long   # 13 long r13 cases
./build/octavox_tests --filter default_neutrality  # substring, case-insensitive
./build/octavox_tests --filter unity --filter stereo   # OR of substrings
# unknown --group/--category or zero selected cases -> usage error, exit 2

# 4. ASan+UBSan full suite (zero sanitizer reports expected)
./build-asan/octavox_tests --json ../evidence/analysis_round16/r16_acceptance_results_asan.json \
    2>&1 | tee ../evidence/analysis_round16/r16_suite_stdout_asan.txt

# 5. bench (13 cases incl. the new 96k_ctrl512 row; 0 allocations)
./build/octavox_bench --json ../evidence/analysis_round16/r16_bench.json \
    2>&1 | tee ../evidence/analysis_round16/r16_bench_stdout.txt
./build/octavox_bench --list                 # 13 case names
./build/octavox_bench --case mono_8v_b128_96k            # one named case
./build/octavox_bench --cases mono_8v_b128,mono_8v_b128_96k_ctrl512   # union
# unknown case name -> usage error, exit 2

# 6. neutrality regression (four reference hashes)
./build/octavox_r15refcap
```

Exit codes: tests 0 = every SELECTED case passed, 1 = any failure, 2 =
usage error (unknown group/category, zero cases selected, unknown option);
bench 2 = unknown case/option. The default run of both tools is the FULL
set (85 cases / 13 bench rows) — no default filtering.

Determinism note: re-running the release suite reproduces the identical
END-of-run table (verified byte-identical against the pre-edit baseline
output in this task); all generators are fixed formulas / the shared
64-bit LCG.

## (d) Category assignment (static, design intent; 18 long / 67 unit)

Rationale: "long" = long-duration robustness by DESIGN (multi-second
stress streams and batteries), assigned per the task-book enumeration —
the stability long-run cases, the R13 cold-start batteries, the detector
batteries at 96/44.1 kHz, estimate-tracking, and the r15acc battery/replay
cases. Everything else is "unit" (fast correctness cases). Borderline
decisions, recorded honestly:
- `dc_input_finite` (stability) -> **long**: a 10 s robustness stream, an
  order of magnitude more audio than the neighbouring unit cases.
- `detector_96k_estimate_stability` -> **long**: a 4 s 96 kHz detector
  run; grouped with the detector batteries (the task book lists
  "detector batteries ... estimate-tracking").
- `cold_start_no_spurious_transient` -> **long**: part of the R13
  cold-start battery (24 sub-runs).
- `default_neutrality`, `per_voice_ratios_independent` -> **unit**:
  correctness/hash cases not in the task-book long enumeration, even
  though each measures ~1.9/1.6 s wall (design intent, not duration).
- The r15p1 surface cases (2-4 s streams, several engine passes each) ->
  **unit**: correctness-focused, not in the long enumeration.

| # | case | group | category | duration_ms (release) |
|---|------|-------|----------|-----------------------|
| 1 | `prepare_default` | prepare_reset | unit | 1.8 |
| 2 | `prepare_invalid_rejected` | prepare_reset | unit | 0.3 |
| 3 | `reset_determinism` | prepare_reset | unit | 215.7 |
| 4 | `repeated_prepare` | prepare_reset | unit | 118.3 |
| 5 | `block_size_invariance` | prepare_reset | unit | 687.3 |
| 6 | `steady_state_no_allocation` | prepare_reset | unit | 91.1 |
| 7 | `unity_tone_220_bitexact` | unity | unit | 67.9 |
| 8 | `unity_tone_1100_bitexact` | unity | unit | 82.0 |
| 9 | `unity_noise_bitexact` | unity | unit | 66.8 |
| 10 | `unity_chord_bitexact` | unity | unit | 66.7 |
| 11 | `unity_silence_bitexact` | unity | unit | 3.0 |
| 12 | `unity_lowlevel_bitexact` | unity | unit | 66.0 |
| 13 | `unity_enter_from_+400c_bitexact` | unity | unit | 216.5 |
| 14 | `unity_gain_exact` | unity | unit | 65.4 |
| 15 | `unity_entry_continuity` | unity | unit | 197.7 |
| 16 | `near_unity_+1c_fresh` | unity | unit | 201.5 |
| 17 | `near_unity_-1c_fresh` | unity | unit | 202.1 |
| 18 | `near_unity_+10c_fresh` | unity | unit | 202.4 |
| 19 | `near_unity_-10c_fresh` | unity | unit | 202.7 |
| 20 | `near_unity_+50c_fresh` | unity | unit | 202.8 |
| 21 | `near_unity_-50c_fresh` | unity | unit | 203.6 |
| 22 | `near_unity_+100c_fresh` | unity | unit | 202.1 |
| 23 | `near_unity_-100c_fresh` | unity | unit | 201.8 |
| 24 | `near_unity_+500c_fresh` | unity | unit | 202.4 |
| 25 | `near_unity_-500c_fresh` | unity | unit | 202.5 |
| 26 | `near_unity_+1200c_fresh` | unity | unit | 205.0 |
| 27 | `near_unity_-1200c_fresh` | unity | unit | 202.0 |
| 28 | `near_unity_+1c_via_unity` | unity | unit | 199.8 |
| 29 | `near_unity_-1c_via_unity` | unity | unit | 199.4 |
| 30 | `near_unity_+10c_via_unity` | unity | unit | 200.0 |
| 31 | `near_unity_-10c_via_unity` | unity | unit | 200.7 |
| 32 | `near_unity_+50c_via_unity` | unity | unit | 200.1 |
| 33 | `near_unity_-50c_via_unity` | unity | unit | 201.0 |
| 34 | `no_bypass_region` | unity | unit | 400.1 |
| 35 | `unity_feedback_stability` | unity | unit | 326.5 |
| 36 | `unity_8voice_unison_exact_mix` | unity | unit | 67.5 |
| 37 | `voices_8_spread_+700c` | voices | unit | 236.9 |
| 38 | `setvoices_midstream` | voices | unit | 209.5 |
| 39 | `stereo_isolation_unity` | stereo | unit | 69.3 |
| 40 | `stereo_isolation_shifted` | stereo | unit | 143.8 |
| 41 | `stereo_independence` | stereo | unit | 210.7 |
| 42 | `rate_44100_+400c` | rates_formats | unit | 224.2 |
| 43 | `rate_96000_+400c` | rates_formats | unit | 1468.6 |
| 44 | `wav_roundtrip_example_input` | rates_formats | unit | 109.8 |
| 45 | `wav_24bit_read` | rates_formats | unit | 0.1 |
| 46 | `longrun_60s_8voice_feedback` | stability | long | 4468.7 |
| 47 | `chirp_30s_+700c` | stability | long | 1947.9 |
| 48 | `impulse_latency` | stability | unit | 7.2 |
| 49 | `impulse_latency_unity` | stability | unit | 6.0 |
| 50 | `dc_input_finite` | stability | long | 659.2 |
| 51 | `clipping_behaviour` | stability | unit | 75.9 |
| 52 | `cold_start_impulse_alignment_unity_1v` | r13 | long | 11.4 |
| 53 | `cold_start_impulse_alignment_unity_8v` | r13 | long | 12.1 |
| 54 | `cold_start_impulse_alignment_up_1v` | r13 | long | 11.4 |
| 55 | `cold_start_impulse_alignment_up_8v` | r13 | long | 16.4 |
| 56 | `cold_start_impulse_alignment_down_1v` | r13 | long | 11.1 |
| 57 | `cold_start_impulse_alignment_down_8v` | r13 | long | 16.6 |
| 58 | `cold_start_no_spurious_transient` | r13 | long | 308.7 |
| 59 | `detector_96000hz_sine_battery` | r13 | long | 5805.6 |
| 60 | `detector_44100hz_sine_battery` | r13 | long | 444.3 |
| 61 | `detector_estimate_tracking_44100` | r13 | long | 214.6 |
| 62 | `detector_estimate_tracking_48000` | r13 | long | 193.5 |
| 63 | `detector_estimate_tracking_96000` | r13 | long | 1445.6 |
| 64 | `detector_96k_estimate_stability` | r13 | long | 1899.8 |
| 65 | `rt_reprepare_zero_allocation` | r13 | unit | 74.7 |
| 66 | `rt_reprepare_param_change_zero_allocation` | r13 | unit | 71.6 |
| 67 | `rt_reprepare_multichannel_zero_allocation` | r13 | unit | 145.1 |
| 68 | `rt_reset_no_allocation` | r13 | unit | 74.5 |
| 69 | `rt_setters_and_process_no_allocation` | r13 | unit | 55.2 |
| 70 | `rt_reprepare_result_identity` | r13 | unit | 454.3 |
| 71 | `voicestats_ratio_diagnostics` | r13 | unit | 70.3 |
| 72 | `per_voice_ratios_independent` | r15p1 | unit | 1602.9 |
| 73 | `per_voice_gain_ramp_and_mute` | r15p1 | unit | 789.3 |
| 74 | `per_voice_delay_isolation` | r15p1 | unit | 828.3 |
| 75 | `wetdry_endpoints` | r15p1 | unit | 611.5 |
| 76 | `bypass_clickfree_and_transparent` | r15p1 | unit | 620.4 |
| 77 | `new_setters_no_allocation` | r15p1 | unit | 115.3 |
| 78 | `default_neutrality` | r15p1 | unit | 1909.1 |
| 79 | `r15acc_ratio_change_isolation` | r15acc | unit | 1196.3 |
| 80 | `r15acc_level_accuracy` | r15acc | unit | 819.9 |
| 81 | `r15acc_block_boundary_semantics` | r15acc | unit | 191.5 |
| 82 | `r15acc_reset_reprepare_determinism` | r15acc | long | 809.5 |
| 83 | `r15acc_stereo_surface` | r15acc | unit | 1157.9 |
| 84 | `r15acc_robustness_battery` | r15acc | long | 2186.7 |
| 85 | `r15acc_invalid_args` | r15acc | unit | 612.5 |

(Registry source: `kAllCases[]` in `native/tests/test_main.cpp`; the
`--list` output prints exactly this table.)

## (e) Bench record and the 96 kHz detector-cost isolation

Bench environment (from `r16_bench.json`): GCC 14.2.0, build_type
"Release" (-O2), sanitizers false, Linux 5.10.134-013.15.kangaroo.al8.x86_64,
Intel(R) Xeon(R) Processor, 2 cores. 13 cases, 5 s of LCG(2025) noise each,
best of 3 repeats, **0 steady-state allocations on every row**. External
wall of the full 13-case run: 30393 ms; sum of the best-loop wall_ms:
9418.6 ms (the rest is warm-up + the 2 non-best repeats + input
generation). The new row `mono_8v_b128_96k_ctrl512` (detection_interval
512 at 96 kHz, the R16-P3 isolation row) is marked in the stdout header
comment and the JSON `note` field: the delivered round-15 bench had 12
cases, this is the 13th, added by R16-P2.

Measured isolation rows (ns per output frame; mono, 8 voices, block 128):

| rate | ctrl64 row | ns/frame | ctrl512 row | ns/frame |
|------|-----------|----------|-------------|----------|
| 48 kHz | `mono_8v_b128` | 1562.51 | `mono_8v_b128_ctrl512` | 418.27 |
| 96 kHz | `mono_8v_b128_96k` | 5189.13 | `mono_8v_b128_96k_ctrl512` | 871.58 |

Implied detector-cost share, two readings (measured facts only):

1. Direct reading — share of the ctrl64 per-frame cost removed by
   sparsifying the control interval 64 -> 512
   (`1 - ns(ctrl512)/ns(ctrl64)`):
   - 48 kHz: 1 - 418.27/1562.51 = **73.2 %**
   - 96 kHz: 1 - 871.58/5189.13 = **83.2 %**

2. Linear-control model reading — with per-frame cost
   `c(ctrl) = base + D/ctrl` fitted to the two rows
   (`D = (c64-c512)*512/7`):
   - 48 kHz: D = 83.7 us per detector invocation, base = 254.8 ns/frame;
     detector share of the ctrl64 cost (D/64 / c64) = **83.7 %**
   - 96 kHz: D = 315.8 us per detector invocation, base = 254.8 ns/frame;
     detector share of the ctrl64 cost (D/64 / c64) = **95.1 %**
   - Consistency check: the fitted non-detector base cost agrees to
     0.1 ns/frame between the two rates (254.8 vs 254.8), and the
     detector invocation cost scales 3.77x from 48 kHz to 96 kHz
     (the R13-b sample-rate-relative window law: the analysis window
     doubles in samples at 96 kHz for the same time-domain window).

Interpretation (fact, no proposal): at the default control interval the
detector dominates the per-frame cost of the 8-voice path at both rates,
and its share grows with the sample rate (73.2 % -> 83.2 % direct
reading); the residual ctrl512 cost is ~418/872 ns/frame of which the
model attributes ~163/617 ns/frame to the still-amortized detector and
~255 ns/frame to the non-detector DSP. Optimization is out of scope here
(separate task R16-P3).

---

## Verification performed in this task (all actually executed)

- release full default suite: 85/85 passed, TOTAL_WALL_MS 40998.6
  (`r16_suite_stdout_release.txt`, `r16_acceptance_results.json`).
- ASan+UBSan full default suite: 85/85 passed, TOTAL_WALL_MS 369541.7,
  ZERO sanitizer reports (grep for AddressSanitizer/UndefinedBehavior
  Sanitizer/runtime error/SUMMARY lines in the stdout: 0 matches)
  (`r16_suite_stdout_asan.txt`, `r16_acceptance_results_asan.json`).
- `octavox_r15refcap`: all four neutrality hashes match exactly
  (0x74318b06c182aacb / 0x46a091e98f2a413a / 0x9502cf8827e8782c /
  0x6f7a2ce3ca310c25) (`r16_refcap_check.txt`).
- JSON validity: `python3 json.load` succeeds on all three JSON files;
  85 cases with group/category/duration_ms present; bench 13 rows with
  wall_ms + environment; round-15 schema fields all preserved.
- Filter demos all pass: `--list` (85 rows, exit 0), `--group r15acc`
  (7/7), `--filter default_neutrality` (1/1), `--group r13 --category
  long` (13/13), bench `--case mono_8v_b128_96k` (1 row, 0 allocs)
  (`r16_filter_demo.txt`).
- Gate integrity: the 220 `check(...)` gate calls in `tests/test_main.cpp`
  are textually IDENTICAL before and after the edit (string-aware
  balanced-paren extraction; sha256 of the extracted gate text
  ed57f594a8de6fa4ce8beff1123e3a16a9f3f62317d8c44fdaca62ab31ae35a9 both
  times; `r16_gate_lines_before.txt` == `r16_gate_lines_after.txt`).
  The pre-edit source is preserved as `r16_test_main_preedit.cpp.txt`
  (sha256 9e5b21be91332ff4c88d3082e35ac0510dc9e86fb91ad50d5e241ddc6f47af47).
- Round-15 evidence files are byte-identical before/after this task
  (sha256 of all 5 files in `evidence/analysis_round15/` unchanged).
- The END-of-run result table of the default release run is byte-identical
  to the pre-edit baseline run (determinism + unchanged table format).
