# R17-P1 — the 85-case suite timing profile and the 300 s window model

Environment (release runs): 14.2.0, Intel(R) Xeon(R) Processor, build_type=Release, sanitizers=False, 2 cores

## Measured runs (this round, all 85/85)

| run | wall | notes |
|---|---|---|
| release forward #1 | 43.1 s | r17_acceptance_results.json |
| release forward #2 | 41.1 s | in-run stability check |
| release REVERSED group order | 42.8 s | r17_acceptance_results_reverse.json (order-independence) |
| Debug -O0 (no sanitizers) | 352.4 s | r17_debug_o0_full.json |
| Debug + ASan/UBSan | 373.0 s | r17_acceptance_results_asan.json, zero reports |

## Per-configuration timing (top cases)

| case | release | reverse | -O0 | -O0/release | ASan | ASan/release |
|---|---|---|---|---|---|---|
| detector_96000hz_sine_battery | 5.9s | 5.8s | 49.1s | 8.36x | 52.5s | 8.94x |
| detector_96k_estimate_stability | 2.0s | 2.0s | 16.5s | 8.45x | 17.5s | 8.96x |
| longrun_60s_8voice_feedback | 4.6s | 4.6s | 37.0s | 8.08x | 40.9s | 8.94x |
| chirp_30s_+700c | 2.0s | 2.0s | 17.6s | 8.66x | 18.2s | 8.95x |
| default_neutrality | 2.0s | 2.0s | 16.5s | 8.42x | 17.4s | 8.89x |
| r15acc_robustness_battery | 2.2s | 2.2s | 17.5s | 7.83x | 18.5s | 8.25x |

Median per-case slowdown: -O0 7.99x (IQR 7.50-8.45), ASan 8.42x (IQR 7.95-8.93) —
**uniform** across cases ⇒ a pure toolchain/host speed factor, no pathological case.

## Where a 300 s window expires (the independent-environment report model)

Release-equivalent stream positions: r13 group starts at 17.4 s; the 96 kHz battery spans [17.8, 23.7] s.

| configuration | total (this host) | 300 s cutoff lands at (release-equiv) | in-progress case |
|---|---|---|---|
| Release (this host) | 41 s | 300.0 s | (suite completes) |
| -O0 Debug (this host) | 352 s | 34.9 s | default_neutrality |
| ASan+UBSan (this host) | 373 s | 33.0 s | wetdry_endpoints |

A verification host H times slower than this dev host shifts the cutoff to
release-equivalent 300/(factor·H) seconds:

| host factor H | -O0 cutoff → | ASan cutoff → |
|---|---|---|
| 1.2x | 29.1 s → per_voice_ratios_independent | 27.5 s → detector_96k_estimate_stability |
| 1.5x | 23.3 s → detector_96000hz_sine_battery | 22.0 s → detector_96000hz_sine_battery |
| 1.8x | 19.4 s → detector_96000hz_sine_battery | 18.3 s → detector_96000hz_sine_battery |
| 2.0x | 17.5 s → cold_start_impulse_alignment_down_8v | 16.5 s → chirp_30s_+700c |

**Reading**: with an unoptimized (Debug -O0 and/or sanitizer) build, a host merely
1.5–2× slower than this 2-core dev host lands the 300 s cutoff **inside the r13
group's 96 kHz detector battery** (which alone costs 49.1 s at -O0 / 52 s under
ASan here) — exactly the reported 'stopping during the Round 13 group near the
96 kHz detector battery'. The filtered runs the independent environment used
(r15acc group: 60 s at -O0; the battery isolated: 49–59 s; the four reference
hashes: seconds) all fit inside 300 s, matching their observation that those
passed. On this dev host the full suite is 85/85 in every configuration;
no stall, no order dependence (reverse run: same set, durations within ±1.1%),
no interaction (battery isolated 5.93 s vs in-suite 5.81 s, +2%), and the harness
flushes progress per case (test_main.cpp pushCase/group-header fflush — no
internal buffering stall).
