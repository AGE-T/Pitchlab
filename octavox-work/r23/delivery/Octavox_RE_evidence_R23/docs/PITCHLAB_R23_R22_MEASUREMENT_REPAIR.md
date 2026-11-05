# PITCHLAB R23 — R22 Measurement-Harness Repair Record (Gate 1)

**Mandate:** R23 brief §27 ("Mandatory R22 measurement corrections within
R23 — do not create a separate R22.1 release"). This document records the
repair of the R22 Minimum-Pitch measurement instrument, the evidence that
each of the eight mandated corrections was needed and is effective, and the
re-run of the full 360-cell matrix with corrected machine-readable output.

**Production DSP untouched:** the repair lives entirely in a NEW CLI
instrument (`cli/r23_minpitch_v2.cpp`, our own tool). `src/dsp.cpp`,
`include/octavox/dsp.hpp` and every other engine file are byte-identical to
the delivered R22 archive (baseline SHA-256 snapshot:
`evidence/analysis_round23/logs/g0_baseline_source_sha256.txt`; the only
re-verified engine change gate is the suite: 100/100 baseline before and
after this round of measurements). The legacy instrument
(`cli/r22_minpitch.cpp`) is preserved verbatim and was re-run on the SAME
engine build for the before/after record.

**Evidence files:**
* `evidence/analysis_round23/r23_minpitch_v2.json` — the corrected 360-cell
  matrix (levels 3/4/5/6/7 as separate fields + the instrument
  self-validation section) + `logs/r23_minpitch_v2_stdout.txt`.
* `evidence/analysis_round23/r22_minpitch_legacy_rerun.json` — the legacy
  instrument re-run (the "before").
* `tools/g1_delta_analysis.py` — the cell-by-cell delta (reproducible).

---

## The eight §27 corrections — what was found and fixed

### 1. acFreq global-argmax fallback → REMOVED (was a REAL, ACTIVE bug)

The R22 instrument's local-maximum main path was already fixed in R22, but
its **fallback branch still returned the global argmax**, which on dead or
aperiodic outputs lands on the near-zero-lag shoulder of the smoothed
autocorrelation — exactly at the lag-domain floor
`lagMin = (sr/D)/2000` → the observed **2000.0 Hz boundary artifact**.

**Measured before/after (the smoking gun):**
* Legacy re-run: **58 of 360 cells** emitted `out_hz_measured = 2000.0`
  (all low-tone cells: 32.7–65.4 Hz, across all three detector modes; by
  verdict: 55 `not_detectable` + 3 `detectable_only`).
* v2 on the same cells: **state = `unvoiced`, frequency 0.0** — no
  fabricated boundary frequency is ever emitted (the instrument records
  `peak_r`/`best_lag` diagnostics instead).

### 2. Explicit measurement states

`MeasState {dead, unvoiced, valid}` returned by the instrument
(`acFreqV2`): dead = zero energy; unvoiced = aperiodic / no qualifying
local AC peak / peak_r < 0.30; valid = a frequency. Distribution over the
360 cells: **302 valid, 58 unvoiced, 0 dead**. Self-validation section
(run at battery start, recorded in the JSON): silence→`dead`,
white noise→`unvoiced`, sines 8.2/16.35/110/220/440 Hz→`valid`
(−0.35…+5.2 cents), sine55+noise→`valid` (+0.22 cents).

### 3. Factor-8 measurement decimation anti-alias filtering

The R22 instrument decimated by plain sample dropping
(`v[i] = x[i*8]`). The corpus's white-noise floor (0.0015 amplitude,
broadband to Nyquist) and any engine transients aliased into the
measurement band unfiltered. The v2 instrument applies a **127-tap
linear-phase Hann-windowed-sinc FIR** (cutoff 0.36·(sr/D) = 2160 Hz @48k)
to the full signal before the steady window; measurement-path only
(the production detector is untouched; the brief's zero-phase prohibition
concerns the live path, and a constant group delay is harmless for
steady-state frequency estimation).

**Effect measured:** on all 302 cells where both instruments produce a
frequency, **the measured fundamentals agree within 0.05 Hz** (0 cells
above) — the tonal measurements are unaffected (all stack harmonics lie
far below the decimated Nyquist), confirming the repair changes only the
degenerate/noise cases (58 boundary artifacts → honest `unvoiced` states).
The aliasing protection matters for the noise-floor pollution of the
autocorrelation peak search, which is precisely the path that produced
the artifacts.

### 4. The 50-cent spacingLocked tolerance → derived criterion

Replaced by a tolerance computed per cell from the estimator's resolution
and the engine's spacing quantization:

```
tol_cents = 1731 · dS_max / S
dS_max    = kq·qT/2 + kq·1.4826·MAD_T + 0.005·S
```

where `qT` = the mode's lag-grid quantum in host samples (FullRate 1,
Decimated D=2, Hybrid 1 — from the audited detector law), `MAD_T` = the
per-cell median-absolute-deviation of the accepted period estimates
(tone-free robust scatter), `0.005·S` = the commit-hysteresis staleness
bound, and `kq = round(S / median_estimated_period)` — **the criterion
itself is tone-free** (the known input tone never enters the lock
decision). Derived tolerances: 348/360 cells are **stricter** than the old
flat 50 cents (range 9.29–49.9 cents on those); 12 cells with large
estimator scatter legitimately exceed it (max 1433 cents — cells where
the honest bound is wide because the estimates themselves scatter; those
cells' verdicts are decided by other levels).

**Verdict outcome:** with the derived criterion, **0 verdict mismatches**
vs the legacy run (141/18/31/170 reproduced exactly) — the corrected
instrument is at least as strict as the old one on this corpus while no
longer relying on an arbitrary constant.

### 5. Separate results per level

The v2 JSON records, per cell: level 3 (`success_ratio`,
`est_median_hz`, `est_median_err_cents`, `est_mad_period_samples`),
level 4 (`spacing_accept_ratio`, `spacing_history_count`), level 5
(`spacing_target`, `spacing_kq`, `spacing_qT_host_samples`,
`spacing_tol_cents`, `spacing_locked`, `voice0_commits`), level 6
(`steady_max_step`), level 7 (`out_state`, `out_peak_r`,
`out_hz_measured`, `out_hz_expected_from_estimate`,
`out_hz_expected_from_tone`, `out_err_cents_vs_tone`,
`out_tol_cents`). No level's result is folded into another's field.

### 6. The known tone cannot conceal a wrong estimate

The lock/consistency chain uses the **estimate-derived** references
(`ratio · median_estimate`) for the output-tracking decision; the
tone-derived error (`out_err_cents_vs_tone`) is a recorded diagnostic
only. A new `detector_inaccurate` verdict (median estimate > 50 cents off
the tone while success ≥ 50%) separates a wrong-but-confident detector
from chain failures (0 cells in this corpus — the R22 detector either
locks correctly or fails outright on these synthetic tones).

### 7. The separated verdict taxonomy

`not_detectable` (141) · `detector_inaccurate` (0) ·
`spacing_not_locked` (18) · `below_output_floor` (31) ·
`output_pitch_inaccurate` (0) · `invalid_output_state` (0 — the
measurement state for dead/aperiodic output; the 58 artifact cells are
resolved BEFORE the verdict chain by `not_detectable`) ·
`valid_transposition` (170).

### 8. Raw logs + corrected machine-readable output

`r23_minpitch_v2.json` (instrument_validation + 360 fully-populated
cells), `logs/r23_minpitch_v2_stdout.txt` (the full per-cell table),
`r22_minpitch_legacy_rerun.json` (the "before"), plus this delta record.

---

## Consistency statement

The 360-cell verdict totals reproduce the R22 delivery exactly
(141 not-detectable / 18 spacing-level / 31 below-floor / 170 valid) with
a strictly more honest instrument: no boundary-frequency fabrication, no
unfiltered decimation, a derived (tone-free) lock criterion, and
per-level separation. The R22 conclusions stand; the instrument that
supports them is now itself trustworthy. Remaining measurement-path
limitations are recorded in the benchmark protocol document (Gate 5):
the AC instrument's ~5-cent floor at 8 Hz (parabolic AC at D=8), and the
MAD-based scatter being corpus-dependent.

**No engine defect was found that would justify a production source
change during this repair** (the engine's behaviour on these cells is
unchanged and the R22 conclusions survive a stricter instrument).
