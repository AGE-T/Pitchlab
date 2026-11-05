# PITCHLAB R23 — YIN Mathematical Derivation (Gate 3)

An implementer's derivation, independent of any library. Evidence classes:
OBS = stated in fetched sources; DER = derived here (verifiable against
`cli/r23_yin_core.h` + `r23_yin_reference.json`); UNK where the paper's own
text was not fetchable this round (mirror failures — the derivation below
reconstructs the mathematics from the implementations and standard signal
theory, cross-validated by our reference implementation's independent
vectors).

---

## 1. What YIN estimates

Signal model: x[n] periodic (or nearly) with period T samples ⇒
x[n+T] ≈ x[n]. YIN estimates **the fundamental period of a monophonic,
( quasi-)periodic frame** — not perceived pitch (missing fundamentals can
still yield T via harmonic spacing; the reference battery's
missing-fundamental case resolves exactly), not a dominant spectral peak,
and nothing meaningful for polyphony (out of scope — the two-pitch stress
case is labelled as such in the benchmark protocol). Where the model
breaks: diplophony/bre fry (two periods), unvoiced consonants/noise
(d(tau) never dips), transients (short-term aperiodicity), strongly
inharmonic partials (piano strings) — all weaken the single-period
assumption [DER].

## 2. From autocorrelation to the difference function

Fixed-window autocorrelation r_t(τ) = Σ_{j=0}^{W−1} x[t+j]·x[t+τ+j] fails
as a period finder because (a) amplitude modulation across the frame
scales the peak, (b) zero-lag energy dominates, (c) a higher-order period
peak (2T) can exceed the T peak when the second half matches better.
Expanding the squared difference:

  d_t(τ) = Σ_{j=0}^{W−1} (x[t+j] − x[t+τ+j])²
         = r_t(0) + r_{t+τ}(0) − 2·r_t(τ)          [DER: binomial expansion]

with r_{t+τ}(0) = Σ_{j} x[t+τ+j]² the SHIFTED window's energy. Dropping
that term (the "constant energy" assumption) is valid only for exactly
periodic signals within the window — for AM/noisy frames it biases d, and
two of our reviewed sources silently make this assumption (hyuncat's
FFT identity; tonalux's presentation) [OBS]. The full three-term form is
the honest one; the FFT acceleration recovers all three terms exactly
(prefix sums + cross-correlation — our variant D, equivalence 3.7e-9).

## 3. Support conventions (the mandatory distinction — brief §17.3)

* **Fixed-W (paper):** W terms per lag; input support W + τ_max
  ([t, t+W+τ_max)); constant integration length; timestamp = frame start t
  (one-sided) — our variant A.
* **Shrinking (librosa family):** N-sample frame, N−τ terms per lag
  (≡ comparing the frame against its zero-extended shift: the tail terms
  Σ_{m≥N−τ}x[m]² appear in librosa's actual formula
  d(k) = 2(ACF(0)−ACF(k)) − Σ_{m<k}x[m]² [OBS, equivalence verified
  4.3e-14 by the agent]); integration length SHRINKS with τ; larger lags
  are noisier — a real statistical asymmetry vs fixed-W [DER].
* **Second-half segment (R22 engine):** m = win/2 − τ_max terms from the
  window's second half at offset τ — support constant (m terms), windows
  inside one causal rolling buffer; m ≈ 0.28·T_min at our defaults —
  far under the paper's W ≥ T_max stability guideline, measured to be
  FASTER-tracking with no accuracy loss on our corpora (benchmark §2).
* **Symmetric alignment:** reference and shifted windows centred on the
  timestamp — changes the effective timestamp/latency; NOT used by any
  production candidate here; recorded for completeness.

## 4. CMNDF

  d'(0) ≡ 1;  d'(τ) = d(τ) / ((1/τ)·Σ_{j=1..τ} d(j))               [canonical]

* Lag 0 is special (d(0)=0 ⇒ 0/0) — set to 1 by definition.
* Cumulative-from-lag-1 normalisation makes d' ~amplitude-invariant (our
  1e-12-amplitude test estimates the same 220 Hz [OBS]) and interpretable
  as an aperiodicity ratio (d'(T) ≈ aperiodic power / total power — NOT a
  probability; see the confidence study).
* **Truncated warm start** (the R22 engine: cumulative from tau_lo, count
  multiplier): with a fixed offset the multiplier (i+1) ≠ τ, so d' is
  neither the paper's statistic nor scale-comparable across floors; the
  measured cost on our corpus is ≤0.33 cents at 880 Hz (reference table)
  — retained as the legacy baseline per the brief (not silently changed).
* Guards: run ≤ 0 ⇒ d' = 1 (silence/DC); non-finite x ⇒ reject before
  accumulation (reason codes, never NaN propagation — reference battery).

## 5. Threshold & selection (paper Step 4)

Canonical: find LOCAL minima of d'; choose the SMALLEST τ whose minimum is
below the threshold (the paper applies the test to the interpolated dip
ordinate); if none qualifies, fall back to the global minimum in the
allowed range. Distinguish three separate gates (brief §17.5): (1) period
candidate selection, (2) voiced/periodic-enough decision, (3) engine
acceptance into the spacing history — the R22 engine conflates (1)+(2)
(global-min fallback always emits an estimate) and has (3) separately (the
unified floor); our control layer splits (2) from (3). Threshold sweep
0.05/0.10/0.15/0.20/0.40: on our clean corpus every threshold ≤0.2
selects identical minima (d' dips ≈0.000–0.01); the engine's 0.20 and
aubio's 0.15 differ only on marginal frames — the confidence/voicing study
carries the distribution measurements.

## 6. Parabolic interpolation (Step 5) — the nuance

Fit y = a(τ−τ_i)² + b(τ−τ_i) + c through (τ_i−1, τ_i, τ_i+1); vertex
offset = ½(y_{−1} − y_{+1}) / (y_{−1} − 2y_i + y_{+1}) [DER]. Paper's
nuance: the CMNDF abscissa is biased (the running-normalizer's slope
shifts the vertex); use the RAW d minimum's abscissa for the period.
Measured: raw-d abscissa ≤0.03 c vs CMNDF +0.02…+0.09 c across tones —
the bias is real and the paper's remedy is right [OBS from our battery].
Guards (all implemented in the reference): scale-aware denominator
(|den| > 1e-12·Σ|y| — raw d scales with amplitude², a fixed epsilon is
level-dependent), non-convex rejection (|offset| ≥ 1), boundary minima
(no neighbour ⇒ no interpolation), flat valleys (den≈0 ⇒ integer lag).

## 7. Step 6 — best local estimate (offline reference)

For analysis time t: examine estimates in [t − T_max/2, t + T_max/2],
select the best-quality one (lowest d'), re-run the estimator with the lag
range restricted to ±20% of its period, take that result. Our offline
implementation (variant E) matches Step 5 exactly on the clean demo
corpus (median +0.001 c both) — its documented benefit (paper: gross
error 0.77%→0.50% on SPEECH) lives in error-prone corpora we have not yet
measured (real-audio pending). NOT renamed, NOT equated to our control
layer (the brief's rule) — see the Step-6 real-time study for the
causal/latency trade-offs.

## 8. Frequency conversion & ranges

f0 = rate/τ with rate = the ANALYSIS rate (sr_dec for the decimated modes,
sr for FullRate/hybrid-fine — the engine applies this correctly
[OBS/audit]); integer-division rate truncation is exact at canonical rates
and flagged UNK at exotic rates (window study). Lag bounds from the
frequency range: τ_min = ⌈rate/1200⌉, τ_max = ⌊rate/f0_min⌋ with the
window laws sized so τ_max + m fits (audit §2.2). Sub-sample τ from the
parabola keeps the cent-level accuracy; the decimated grid costs ≤1.5 c
p95 (benchmark) — covered by the hybrid fine stage when needed.
