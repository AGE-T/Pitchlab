# PITCHLAB R23 — Confidence & Voicing Study (Gate 3)

**Question:** what can legitimately be claimed about estimate quality, and
where voicing decisions belong (brief §17.4, §23).

---

## 1. What d'(τ*) is and is NOT

* [DER] At the true period, d'(T) ≈ aperiodic power / total power — an
  **aperiodicity measure**. 1 − d'(T) is therefore a *periodicity score*
  ("clarity"), NOT a probability: it is uncalibrated, corpus-dependent,
  and its distribution is not uniform. The Rust crate's own docs concede
  "clarity is not supplied directly by YIN" — and our source review found
  its clarity arithmetic sign-broken (clean sine → −1.07) [OBS].
* **Naming rule adopted (brief §17.4):** `periodicity_score` /
  `aperiodicity_measure` until a calibration against labelled data exists.
  Our control layer's output field is `confidence` = a documented
  persistence heuristic (1.0/0.9/0.75/0.5/0.4/0.2 by state), never called
  a probability.

## 2. The six-level separation (implemented)

| Level | Owner | Field |
|---|---|---|
| 1 Numeric validity | detector/engine guard | f0 > 0, finite (NaN/Inf guarded at the boundary — Gate 8) |
| 2 Periodicity evidence | detector telemetry | `lastCmndfMin()` (new observer side-channel) |
| 3 Candidate selection | detector | threshold/fallback path taken |
| 4 Range validity | engine gate | [max(fmin, f0_min), fmax] |
| 5 Temporal tracking acceptance | **Pitch Control Layer** | lock/outlier/octave/hold decisions |
| 6 Engine spacing acceptance | engine | `lastEstimateSpacingAccepted()` (R22 API) |

The R22 engine conflated 2–3 (global-min fallback always emits an
estimate) — the layer now separates 5 from 6 (spec §1).

## 3. Measured behaviour of the gates [MEAS]

* **Threshold sweep on the corpus** (Gate 5): d' minima on voiced frames
  are ~0.000–0.01 ⇒ every threshold ≤ 0.20 selects identical minima; the
  engine's 0.20 vs the paper's 0.10 differ only on marginal frames.
  White noise: peak d' minima ~0.6–0.9 — the fallback fires at ~100% of
  frames for EVERY variant (benchmark §2): **a voiced/unvoiced decision
  cannot come from the threshold alone**; it needs the d' magnitude gate
  (2) — exactly what the control layer's periodicity gate (≥0.80) now
  provides ahead of the history.
* Silence: the 1e-6 peak gate rejects (level 1); DC: degenerate
  1200 Hz boundary estimates for ALL variants (level-3 pathology —
  preconditioning question, not a confidence question) [MEAS].
* Gate-1 repair evidence: the measurement states (dead/unvoiced/valid)
  with `peak_r` diagnostics — the same discipline applied to the
  instrument side.

## 4. Calibration path (if a probability is ever required) [DER]

Calibrate 1 − d'(τ*) against labelled voiced/unvoiced frames (PTDB-TUG's
laryngograph reference is the natural labeller), report the mapping's
reliability diagram + ECE on a held-out split, and only then rename. Until
then: scores only. (Dataset access pending — decision record §7.)

## 5. Glissando/vibrato vs continuity rules (brief §23)

The control layer's outlier gate rejects only >200c non-octave deviations
from the TRACKED value; octave-shaped changes get a 3-frame confirmation
(musical octave leaps pass, single-frame estimator flips do not); genuine
glissando passes untouched at the 900 c/s slew limit (47–48 c following
error measured). No hard continuity rule rejects musical motion [MEAS].
