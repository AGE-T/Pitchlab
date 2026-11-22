# PITCHLAB R23 — Window, Lag, Time-Alignment & Latency Study (Gate 3)

**Every number below is either OBS from the audited source or DER/MEASURED
from this round's batteries** (`r23_detector_compare.json`,
`r23_yin_reference.json`, the audit). Frame geometry per the brief §20 —
"window = 1024 samples" statements are insufficient; this is the complete
table.

---

## 1. The production (R22 Decimated default) geometry, 44.1–192 kHz

| Rate | D | sr_dec | τ_min | τ_max (f0_min 60) | win_dec | m | seg0 | window span | cadence | control application |
|---|---|---|---|---|---|---|---|---|---|---|
| 44.1k | 2 | 22050 | 18 | 367 | 1024 | 145 | 512 | 46.4 ms | 64 (1.45 ms) | ≤ +1 cadence |
| 48k | 2 | 24000 | 20 | 400 | 1024 | 112 | 512 | 42.7 ms | 64 (1.33 ms) | ≤ +1 cadence |
| 96k | 4 | 24000 | 20 | 400 | 1024 | 112 | 512 | 42.7 ms | 64 (0.67 ms) | ≤ +1 cadence |
| 192k | 8 | 24000 | 20 | 400 | 1024 | 112 | 512 | 42.7 ms | 64 (0.33 ms) | ≤ +1 cadence |

* Window ENDS at the cadence boundary (causal, one-sided); the estimate's
  effective information centre ≈ the second-half segment
  ([win/2, win/2+m+τ)) — i.e. the newest ~5–9 ms dominate (m=112
  decimated samples + the τ-offset span), which is why vibrato tracking
  beats the canonical fixed-W form (benchmark §2: 26.4 c vs 34.6 c p95).
* **Total end-to-end tracking latency** [DER]: window freshness (the
  segment ends AT the boundary) + ≤1 cadence application (1.33 ms @48k) +
  the engine's spacing-commit pipeline: median history (8 cadences worst
  case before a step moves the median) + commit hysteresis 0.5% +
  commit crossfade 8 ms. **No look-ahead anywhere** (causal at every
  stage). DAW-facing latency report: the dry-alignment constant
  base_delay + grain/2 = 2304 samples @48k (unchanged; module test
  `latency_reporting` PASS).
* librosa comparison [OBS]: `center=True` default adds frame_length/2
  look-ahead (~46 ms @2048/22.05k) — our causal geometry has NO such term;
  any future librosa-validated vector must align timestamps accordingly.

## 2. Buffer-support law vs the brief's §20 numbers [OBS/DER]

* FullRate @48k, f0_min 60: τ_max=800, win=2048, **m=224** (the brief's
  "≈224 terms" confirmed); f0_min 40: τ_max=1200, win=3000, m=300 ✓.
* Decimated halves both (m=112 @48k) at the same TIME span.
* The paper's W ≥ T_max guideline vs the engine's m ≈ 0.28·T_min:
  **measured both ways** (benchmark §2 window study W=1/1.5/2×T_max):
  accuracy +≤0.09 c, tracking −6…−11 c p95 — the harmonizer use case
  (continuous pitch following) selects the short-memory form; the paper's
  guideline is a hypothesis we tested and rejected FOR THIS ENGINE, not a
  rule we ignored [DER].

## 3. Boundary conventions [OBS]

* τ_min = max(2, int(rate/1200)) — the 1200 Hz ceiling; the [f0_min,1200]
  validity gate applied AFTER conversion; boundary minima handled
  (reference battery case 4e; the hybrid fine stage's ASan-motivated
  τ_max−1 clamp is documented in the audit §2.5).
* The decimation grid is the GLOBAL stream position (persistent across
  blocks — arbitrary DAW block sizes are safe by construction; battery C's
  alternating 32/1024 + zero-length calls PASS).
* [UNK] Non-canonical rates: sr/decim_ integer division (e.g. 44056/2 =
  22028 exact — but 44.056k-class legacy rates with D rounding may
  truncate); bounded audit pending (decision record §6). All canonical
  rates (44.1/48/88.2/96/176.4/192) divide exactly.

## 4. Hop/update rates & cost per update

* Engine: one estimate per 64 stream samples (750/s @48k, 3000/s @192k) —
  CPU flat 361–427 ns/SAMPLE across rates (the per-update cost FALLS with
  rate as D rises; the window TIME span is constant).
* Reference canonical: 150 updates/s (8× stride) at 2.75M ns/update
  (48k, W=τ_max) → 11–22 ms/update at 192k — the direct form is 192k-
  viable ONLY via the FFT route (taxonomy §cost).
* Control layer: zero added latency (per-cadence gate, no buffering —
  spec §3).

## 5. Timestamp semantics for any future cross-implementation vectors

The engine's estimate at boundary b reflects the window ending at b; a
librosa-style centered frame at the same b would need samples up to
b+frame_length/2 (look-ahead) and a −frame_length/2 timestamp correction;
the canonical fixed-W frame at t = b − W − τ_max shares the engine's
"window ends at b" alignment (used in the Gate-5 comparison to make the
timestamps identical). Any PTDB-TUG evaluation (protocol doc §25) must fix
ONE convention and compensate the others explicitly, reporting both raw
and compensated alignment [DER].
