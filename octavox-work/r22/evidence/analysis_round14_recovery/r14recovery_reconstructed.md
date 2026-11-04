# Round-14 recovery — reconstructed evidence summary (parity + interpolation + baseline)

> PROVENANCE: the original harnesses (r14recovery_parity.py/.json/.md,
> r14recovery_interp.py/.json/.md, native/cli/r14recovery_probe.cpp,
> signals/, probe_out/) were destroyed by the second environment reset
> (2026-10-26 ~20:54 UTC; see NOTES_R15P2.md in analysis_round15/). This file
> was reconstructed from the preserved worklog record (entry R15-EV1), which
> contains the measured values reproduced below. The raw machine-readable
> JSON outputs are lost; the numbers here are the record.

## D1 — tracker + rate-limiter parity (native 14.0.0 vs poc/octavox_v2.py)

Setup: byte-identical synthetic inputs on both sides (FNV-1a-64 identity
gate T1 passed); 1 voice, lagrange6, grain 4096, base 256, ctrl 64, fmin 55 /
fmax 1200, no feedback; Python reference instrumented per 64-sample block;
native driven via the public API (lastEstimateHz(), voiceStats()).

Tolerances were fixed before running (T1..T8); one disclosed restatement:
T5 on the step signal (blanket max |ΔS| ≤ 2.0 → post-step ≤ 60) because the
first measurement 49.4067 = exactly |S1−G| was localised to the native
core's DOCUMENTED unity-exit spacing commit at the ratio step (the reference
commits only at its next natural grain wrap) — an intentional improvement,
mechanism named, strict blanket value recorded.

| signal | estimate traj max Δ | glide 1.0→1.5 to ±0.1 c | spacing | commits n/py | output context |
|---|---|---|---|---|---|
| steady 220 Hz | 6.15e-12 cents | — (no step) | final 4145.4080 / 4145.4048; traj max Δ 0.0032 | 1/1; wraps 12/12 | max Δout 0.000514 |
| step 1.0→1.5 | 6.15e-12 cents | 17344 samples BOTH (analytic 17317) | post-step max ΔS 49.4067 (= \|S1−G\|, unity-exit commit) | 1/1; wraps 6/6 | max Δout 0.648 |
| vib 220 Hz ±30 c @ 5 Hz | 4.23e-12 cents | — | final 3995.5268 / 3995.5268; traj max Δ 70.47 | 9/9; wraps 16/16 | max Δout 0.0784 |

Unity behaviour at the pre-step settled region: native parked at delay 2304
→ **bit-exact delayed copy, gain deviation 0.000000000 dB**; Python
reference **−2.4181 dB** (two-tap comb, |cos(π·220·4096/48000)| law) — the
round-12 defect the native core fixed. Native block-size invariance exactly
0 (64/150/single-sample splits). Detector tracking vs true instantaneous f0
(vibrato, after window fill): RMS 10.941 cents, max 15.884 cents (2048-sample
window, estimate sampled at block ends; 32 fill-transient blocks excluded on
both implementations).

## D2 — four-mode interpolation comparison (native 14.0.0)

Estimator: Hann-exact 3-bin magnitude peak (self-test bias 4.29e-11 cents;
the known-biased power-parabola estimator was NOT used; analysis windows
never read before the supplied buffer start). Shared byte-identical signals
(FNV-1a-64 gate G6). Engine: 1 voice, grain 4096, base 256, ratio 2^(5/12).

| mode | pitch err (cents) | OOB/in (dB) | near-Nyq (dB) | below-band (dB) | transient outside support | silence max\|out\| |
|---|---|---|---|---|---|---|
| linear | +0.0287 | −30.30 | −70.32 | −23.99 | 0 (exact) | 0 |
| lagrange6 | +0.0287 | −54.24 | −79.21 | −46.46 | 0 (exact) | 0 |
| lagrange6_q | +0.0287 | −53.24 | −61.38 | −46.27 | 0 (exact) | 0 |
| obs6 | +0.0287 | −60.03 | −61.38 | −39.16 | 0 (exact) | 0 |

Recommendation rule (fixed before running): keep Lagrange6 unless a mode is
a clear net win — pitch within 0.2 cents of best, alias rejection within
3 dB of best, no exact-zero gate regression. **VERDICT: KEEP Lagrange6 as
the default.** All four modes tie on pitch (+0.0287 cents — engine-level,
interpolator-independent) and on the exact-zero gates (transient
kernel-support law, silence floor). The differentiator is alias rejection:
Lagrange6 delivers −54.2 dB out-of-band with the best near-Nyquist
(−79.2 dB) and best below-band (−46.5 dB) residual. ObsKernel6 leads raw
OOB (−60.0 dB) but gives up 7.3 dB below-band and 17.8 dB near-Nyquist
versus Lagrange6 — not a net win across the three-band alias battery;
remains opt-in research (observed-target .rdata data).

Documented engine caveat (all modes): the 9600 Hz alias-probe input's
difference function has minima at multiples of its 5-sample period below
tau_min = 40, so the detector reports a subharmonic (~9600/9 Hz); the
spacing target stays within the 0.5 % hysteresis of the grain so no commit
occurs (S = 4096.0, 0 commits). Impulse contacts: linear spread 0.000/0.071
smp; lagrange6 0.118/0.116; lagrange6_q 0.117; obs6 0.072/0.181 — energy
outside the kernel-law support exactly 0 for all modes; energy outside the
primary contact ≈ 0.94 for all modes (grain re-triggering, an
architecture-level property).

## D3 — 14.0.0 baseline benchmark (verbatim preserved: r14_baseline_bench_stdout.txt)

octavox-native 14.0.0 (this file is the original captured stdout, restored):
mono 1v 1341–1358 ns/frame (15.5× RT), mono 8v 1566–1571 (13.3×), stereo
1v/8v b128 2716/3112 (7.7×/6.7×), mono 8v ctrl512 408 ns (51.1× RT);
RSS 8.1–12.6 MB; allocations 0 in every case. Matches the round-12 recorded
numbers — the R13/R14-recovery changes (SR fixes, cold-start law,
zero-alloc re-prepare) cost nothing measurable.
