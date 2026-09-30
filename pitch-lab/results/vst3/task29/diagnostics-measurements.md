# Task 29 — Granular 96 kHz investigation (vst_realtime_diagnostics_test)

| drive | block | jobs | lat ms | RTF | cpu ms/call | re-prepares | clamps | faults | underruns | dry-miss | stalls | adopt-fail | prep-fail | out dB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| pitch 0, grain 0.10 | 128 | 3 | 324.8 | 1.161 | 1.548 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -8.8 |
| pitch 0, grain 0.10 | 256 | 3 | 324.8 | 1.145 | 3.053 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -8.8 |
| pitch 0, grain 0.10 | 512 | 3 | 324.8 | 1.137 | 6.064 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -8.8 |
| pitch 0, grain 0.10 | 1024 | 3 | 324.8 | 1.143 | 12.158 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -8.8 |
| pitch +12, grain 0.10 | 128 | 3 | 324.8 | 0.587 | 0.782 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.1 |
| pitch +12, grain 0.10 | 256 | 3 | 324.8 | 0.577 | 1.537 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.1 |
| pitch +12, grain 0.10 | 512 | 3 | 324.8 | 0.581 | 3.101 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.1 |
| pitch +12, grain 0.10 | 1024 | 3 | 324.8 | 0.574 | 6.103 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.1 |
| pitch -12, grain 0.10 | 128 | 3 | 324.8 | 1.333 | 1.777 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.5 |
| pitch -12, grain 0.10 | 256 | 3 | 324.8 | 1.335 | 3.559 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.5 |
| pitch -12, grain 0.10 | 512 | 3 | 324.8 | 1.343 | 7.160 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.5 |
| pitch -12, grain 0.10 | 1024 | 3 | 324.8 | 1.349 | 14.353 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.5 |
| pitch -12, grain 0.50 | 128 | 3 | 724.8 | 1.751 | 2.335 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.5 |
| pitch -12, grain 0.50 | 256 | 3 | 724.8 | 1.733 | 4.622 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.5 |
| pitch -12, grain 0.50 | 512 | 3 | 724.8 | 1.758 | 9.376 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.5 |
| pitch -12, grain 0.50 | 1024 | 3 | 724.8 | 1.750 | 18.620 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -9.5 |
| pitch -12, grain 0.10, lfo | 128 | 1 | 324.8 | 1.067 | 1.423 | 340 | 759 | 0 | 0 | 0 | 0 | 0 | 0 | -9.0 |
| pitch -12, grain 0.10, lfo | 256 | 1 | 324.8 | 1.031 | 2.749 | 244 | 389 | 0 | 0 | 0 | 0 | 0 | 0 | -9.0 |
| pitch -12, grain 0.10, lfo | 512 | 1 | 324.8 | 1.044 | 5.569 | 197 | 199 | 0 | 0 | 0 | 0 | 0 | 0 | -9.0 |
| pitch -12, grain 0.10, lfo | 1024 | 1 | 324.8 | 1.180 | 12.555 | 104 | 104 | 0 | 0 | 0 | 0 | 0 | 0 | -9.0 |

BOUNDARY CHURN (block 128, -12 st + LFO 0.5 st): clamps 759, re-prepares 333, adoptions 332, faults 0 — the envelope is truncated by the parameter range at the ±12 st boundary; every LFO dip clamps and requests a rebuild that still cannot cover the excursion (futile churn; seam every ~6 ms = the audible stutter).

# Task 29 — PV-phaselocked investigation (48 vs 96 kHz x blocks)

| drive | block | jobs | lat ms | RTF | cpu ms/call | re-prepares | clamps | faults | underruns | dry-miss | stalls | adopt-fail | prep-fail | out dB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 48 kHz, pitch +12 | 128 | 1 | 55.3 | 0.016 | 0.043 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -21.2 |
| 48 kHz, pitch +12 | 256 | 1 | 55.3 | 0.015 | 0.078 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -21.2 |
| 48 kHz, pitch +12 | 512 | 1 | 55.3 | 0.014 | 0.154 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -21.2 |
| 48 kHz, pitch +12 | 1024 | 1 | 55.3 | 0.013 | 0.283 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -21.2 |
| 96 kHz, pitch +12 | 128 | 1 | 27.7 | 0.063 | 0.084 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -16.7 |
| 96 kHz, pitch +12 | 256 | 1 | 27.7 | 0.059 | 0.158 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -16.7 |
| 96 kHz, pitch +12 | 512 | 1 | 27.7 | 0.035 | 0.187 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -16.7 |
| 96 kHz, pitch +12 | 1024 | 1 | 27.7 | 0.035 | 0.375 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | -16.7 |
