# PITCHLAB R23 — Decision Record

**Programme:** YIN pitch-tracking research, validation and artifact-free
integration (all ten gates). This record states what was selected, why,
what remains open, and the exact production deltas shipped.

---

## 1. Selected detector configuration

**The delivered R22 detector set is RETAINED unchanged as the production
detectors:**

* Production default: **Decimated** (causal 6th-order Butterworth cascade,
  D per rate, threshold 0.20, first-local-min + argmin fallback, d-based
  parabolic) — CPU 361–427 ns/sample FLAT across 44.1–192 kHz, accuracy
  0.02 c median / 1.5 c p95 on the synthetic corpus, best-in-class
  vibrato/glissando tracking (26.4 c / 7.1 c p95) thanks to the short-m
  memory.
* Reference modes kept: FullRate (legacy project reference) and Hybrid.
* **Why not replace with canonical YIN:** the canonical fixed-W law
  (validated in the reference implementation to ≤2e-9 vs independent
  vectors) costs 2.75M ns/update at 48 kHz and 11–22 ms/update at 192 kHz
  direct (unusable without FFT), tracks vibrato WORSE (34.6 c vs 26.4 c
  p95 — longer memory), and gains nothing measurable on our corpora
  (med 0.31 c vs 0.02 c — the legacy law is MORE accurate on steady
  tones). The paper's W ≥ T_max stability guideline was tested
  (W = 1.0/1.5/2.0 T_max): accuracy improves by ≤0.09 c while tracking
  degrades by 6–11 c — the harmonizer use case favours tracking.
* **Why not FFT-YIN:** equivalence is proven (3.7e-9 rel.) and it is the
  only viable canonical form at 192 kHz, but the engine's Decimated mode
  already delivers flat 361 ns/sample at 192 kHz — an FFT rewrite would
  add complexity (FFT-sized buffers, complex-number paths in the RT
  callback) for no measured end-to-end benefit at our lag ranges.
  Recorded as the designated path IF the lag range ever grows (e.g.
  f0_min → 20 Hz at 192 kHz = 9600-lag domain).
* **yinfft (spectrally weighted) and pYIN: rejected** — different
  algorithms (taxonomy doc); pYIN's Viterbi adds latency/complexity our
  measured error rates do not justify (0.000 gross/octave on corpus).

## 2. Selected control path

**Strategy A: the Pitch Control Layer as a gate BEFORE the existing
8-entry period median** (`Config::tracking_gate`, default false =
bit-exact legacy; battery 290/290; integration 5/5). No double smoothing,
zero added latency. Strategy B (replace the median) documented and
deferred. The per-voice ratio path is untouched (the brief §16.2
correction honoured).

## 3. Exact frame/window/lag definitions (production, audited)

Causal sliding window ending at each 64-sample cadence boundary; second
half is the reference segment; m = win/2 − tau_max difference terms
(112 @48k-decimated / 224 @48k-fullrate with f0_min 60); lag domain
[tau_min = rate/1200, tau_max = rate/f0_min]; truncated CMNDF (multiplier =
accumulated count from tau_lo); threshold 0.20; parabolic on d
(Decimated/Hybrid-fine) or CMNDF (FullRate); validity [f0_min, 1200] Hz.
Full latency budget: window 42.7 ms + ≤64-sample control application
(1.33 ms) + spacing-commit crossfade 8 ms worst case — the DAW-facing
latency report remains the dry-alignment constant (base + grain/2 =
2304 samples @48k), unchanged.

## 4. Production deltas shipped in R23 (the complete list)

1. `src/dsp.cpp`: **non-finite input guard** at the process() boundary
   (0.0f substitution) — fixes the reproduced NaN/Inf propagation defect;
   bit-exact for finite input (100/100).
2. `src/dsp.cpp` + `include/octavox/dsp.hpp`: **CMNDF telemetry
   side-channel** (`PitchDetector::lastCmndfMin()` — observer only) and
   **`Config::tracking_gate`** (default false) wiring the control layer
   (Strategy A).
3. NEW files: `include/octavox/pitch_control.hpp`, `src/pitch_control.cpp`
   (the layer), `cli/r23_minpitch_v2.cpp`, `cli/r23_yin_core.h`,
   `cli/r23_yin_reference.cpp`, `cli/r23_detector_compare.cpp`,
   `cli/r23_control_battery.cpp`, `cli/r23_artifacts.cpp`,
   `cli/r23_integration_check.cpp` (the R23 instruments).
4. **No VST3 parameter change** (38 ids 0..37 preserved; state blob v2
   unchanged; module integration 12/12 incl. state round-trip).
   The brief's "IDs 0–73" is the ORIGINAL Octavox's 73 host parameters
   (static-RE count), not our surface — documented in the audit §0.

## 5. Rejected alternatives (with reasons)

* Canonical fixed-W YIN as production detector (cost, tracking) — §1.
* FFT-YIN now (no measured benefit at our ranges; complexity) — §1.
* yinfft/pYIN (algorithmically distinct; unjustified error rates) — §1.
* Strategy B/C control integration (double-smoothing risk / hash break) — §2.
* A negative-shift anti-alias filter (the brief's caution confirmed:
  downshifts read SLOWER — no new aliasing; measured HF −11…−43 dB) —
  refuted instead of implemented. A POSITIVE-shift (r>1) pre-read filter
  is justified by measurement (+9.4 dB HF @+12 st) and is recorded as the
  top candidate for R24.
* Window enlargement to W ≥ T_max (tracking loss without measurable
  accuracy gain on corpus) — measured, §1.

## 6. Still HYP / UNK

* [HYP] The user-audible crackle instance's exact provenance (the
  mechanism is measured; the default config is protected; DAW-side causes
  remain possible — Windows-host test pending).
* [UNK] DAW-level verification of all four symptom areas (no Windows host
  in this sandbox; test protocol delivered in the artifact analysis).
* [UNK] Non-canonical-rate `sr/decim_` truncation audit (44.056 kHz-class
  rates; bounded study recorded in the window doc).
* [HYP] Engine-side history reset on lock release (Strategy-B semantics)
  — designed, not integrated.
* [HYP] Pre-read anti-alias for r > 1 (positive shifts) — measured
  justification, implementation deferred to R24 with a designed test
  matrix.

## 7. Real-audio evaluation status

PTDB-TUG / GuitarSet were NOT downloaded this round (network fetch of the
DAGA reference PDF was damaged mid-transfer; the dataset landing pages
require interactive access from this sandbox). All R23 accuracy numbers are
synthetic-corpus results, labelled as such everywhere. The benchmark
protocol document defines the exact PTDB-TUG/GuitarSet metrics so R24 can
execute them without re-design. No synthetic result is presented as a
real-audio result anywhere in this round.

## 8. Gate status

G0 baseline 100/100 ✓ · G1 harness repair + 360-cell re-run ✓ (0 verdict
deltas, 58 boundary artifacts removed) · G2 audit ✓ · G3 research ✓
(source comparison + taxonomy from five fetched implementations + paper;
paper mirror text UNK) · G4 reference implementation 12/12 ✓ · G5
comparison battery ✓ · G6 control layer 290/290 ✓ · G7 artifacts ✓
(2 reproduced: one fixed, one mechanism-measured) · G8 integration ✓
(100/100 + 12/12 + 5/5) · G9 real-audio/host — synthetic + module-level
complete, recorded-audio and Windows-DAW levels OPEN (honestly).
