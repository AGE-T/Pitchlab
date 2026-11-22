# PITCHLAB R23 — Benchmark Protocol & Metric Definitions (Gate 5)

**Fairness rules (brief §26, all honoured in the shipped batteries):**
identical source samples, frame timestamps (the engine's 64-sample cadence
boundaries; canonical frames aligned window-end-to-boundary), identical
lag ranges ([60, 1200] Hz), identical voiced masks; recorded environment
(g++ 14.2.0-19 -O2, Xeon sandbox, Linux 5.10, 2026-11-05); fixed seeds;
raw machine-readable logs in `evidence/analysis_round23/*.json`.

---

## 1. Detector metrics (exact denominators)

| Metric | Definition | Denominator / policy |
|---|---|---|
| valid_ratio | frames with f0 > 0 | ALL frames in the measurement window |
| median/p95 \|cents\| | \|1200·log2(est/gt)\| at the frame boundary | frames where est > 0 AND gt defined (tonal signals; the steady window skips the cold start) |
| gross error rate | \|cents\| > 50 | same as above |
| octave error rate | \|cents\| ∈ (1150, 1250) | same as above |
| jitter | median of \|Δcents\| between consecutive valid frames | steady-tone signals only |
| CPU (engine) | wall-clock of the full engine process / samples | identical block pattern (64) |
| CPU (reference) | wall-clock of the estimate loop / updates | 8×-reduced stride (150/s) — stated on every row |

## 2. Output-quality metrics (artifact battery)

* max inter-sample step over the steady window; click count = steps
  exceeding max(0.02, 6× the median step) — the adaptive threshold's
  known artifact on octave-up outputs is documented where it fires;
* HF band ratio: 8–16 kHz energy / total, 8192-point Hann FFT; reported
  as out-vs-in dB gain (input carries genuine HF by construction);
* finiteness + recovery RMS (last 100 ms) for the stuck battery.

## 3. Control-layer metrics (the spec's measurable response)

acquisition frames/seconds; hold frames → bounded release; recovery
frames after dropout; outlier rejection (tracked-value invariance);
octave acceptance latency; jitter attenuation factor (MAD ratio);
glissando max following error at the slew limit; both smoothing spaces.

## 4. MIREX-style separation (for the future real-audio round)

Raw pitch accuracy (±50 c), raw chroma accuracy, overall accuracy
(incl. voicing), voicing recall/false-alarm, gross/octave rates, median +
p95 cents on correctly-voiced frames — each with its EXACT denominator
declared, voicing decisions scored separately from pitch (a detector may
not look good by emitting pitch on unvoiced frames nor by rejecting hard
voiced frames). The synthetic corpus implements brief §25.5's generator
list (sine/harmonics/missing-fundamental/vibrato/glissando/steps/AM/noise
variants/SNR/transients/DC/clipping); the two-simultaneous-pitch stress
case is labelled out-of-scope-for-F0 and excluded from accuracy
denominators.

## 5. Real-audio protocol (defined now, executed when datasets land)

* PTDB-TUG (48 kHz, laryngograph reference, ODbL database content):
  microphone-derived estimates vs the larynx reference; report RAW and
  explicitly-compensated alignment (the propagation/geometry offset
  measured once per file from cross-correlation of a voiced onset, ONE
  compensation for all detectors — no per-detector tuning); metrics per
  §4; label the corpus `recorded_annotated`.
* GuitarSet (hexaphonic): isolated string channels as monophonic ground
  truth (`recorded_annotated`); mixed excerpts = stress tests only.
* User recordings without trustworthy ground truth: `recorded_unannotated`
  — listening/robustness only, NO cents statistics (brief §25.3).

## 6. What is NOT reported as a score

No single "tracking stability score" (undefined composite) — transparent
tables only (brief §26). No synthetic result presented under a real-audio
label; the R23 numbers are synthetic and say so on every table.
