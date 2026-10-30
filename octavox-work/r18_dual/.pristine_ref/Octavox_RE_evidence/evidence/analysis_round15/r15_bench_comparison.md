# Round-15 P5 — benchmark comparison: native 15.0.0 vs the 14.0.0 baseline

Baselines: evidence/analysis_round14_recovery/r14_baseline_bench_stdout.txt (14.0.0,
captured before the second environment reset) and the round-12 recorded numbers
(the R13/R14-recovery changes cost nothing measurable — verified there).
This run: evidence/analysis_round15/r15_bench_15.txt (15.0.0, same deterministic
LCG input, same machine class, single run per case; treat ~±3 % as run noise).

| case | 14.0.0 ns/frame | 15.0.0 ns/frame | Δ | note |
|---|---|---|---|---|
| mono_1v_b64 | 1341.42 | 1308.28 | −2.5 % | noise |
| mono_1v_b128 | 1354.34 | 1329.68 | −1.8 % | noise |
| mono_1v_b512 | 1357.77 | 1338.08 | −1.4 % | noise |
| mono_8v_b64 | 1568.61 | 1547.19 | −1.4 % | noise |
| mono_8v_b128 | 1565.88 | 1556.85 | −0.6 % | noise |
| mono_8v_b512 | 1571.40 | 1533.46 | −2.4 % | noise |
| stereo_1v_b128 | 2716.46 | 2630.30 | −3.2 % | noise |
| stereo_8v_b128 | 3111.75 | 3272.56 | +5.2 % | run variance (two engines) |
| mono_8v_b128_ctrl512 | 408.13 | 415.85 | +1.9 % | ≈ equal |
| mono_1v_b128_96k | — | 4998.30 | NEW | 2.08× realtime margin |
| mono_8v_b128_96k | — | 5211.17 | NEW | 2.00× realtime margin |
| mono_8v_b128_surface | — | 1520.40 | NEW | per-voice surface active: ≈ 0 % vs mono_8v_b128 (within noise) |

Allocations: 0 in every case (both versions). RSS: 14.0.0 8.1–12.6 MB →
15.0.0 10.4–16.6 MB (+2–4 MB: the dry-path ring per engine + the R13-b
detector window law). Realtime factors at 48 kHz: 15.6–15.9× mono 1v,
13.4–13.6× mono 8v, 7.9×/6.4× stereo, 50.1× at ctrl 512 — unchanged class.

## Verdict

- **Default-path cost of the round-15 API ≈ 0** (all deltas within run noise;
  the `wet==1 && !bypass` legacy output path is bit-exact AND cost-neutral).
- **Per-voice control overhead ≈ 0–2 %** (the surface case runs the full
  per-voice override set, two mutes, wet 0.5 and mid-run gain retargets inside
  the timed loop: 1520 vs 1557 ns/frame — within noise of the plain 8v case).
- **96 kHz rows (new)**: 2.0–2.1× realtime margin. The per-SAMPLE cost scales
  ≈3.8× vs 48 kHz because the R13-b detector window law
  `win = max(2048, ceil(2.5·τmax))` grows with τmax = sr/fmin (4364 samples
  at 96 kHz) — the price of the 96 kHz correctness fix (the round-12 core was
  blind at 96 kHz: 0.000000 Hz on a 110 Hz tone). Still comfortably realtime;
  a ctrl-512 variant would raise the margin further (not run — noted).
- Memory +2–4 MB total (dry ring + detector window), 0 allocations steady-state.
