# Round-11 DSP improvement — period-aligned grain splicing (+ stereo-safe engines)

**Everything in this note describes our own independent demo. No target code or data was
reused, and no number below is a measurement of the target plug-in (never executed).**

Reference implementation (unchanged, baseline): `poc/octavox_poc.py` + `poc/octavox_offline.py`.
New implementation (separate file, as required): `poc/octavox_v2.py`.
Gated acceptance suite: `evidence/analysis_round11/r11_acceptance_tests.py`
(45/45 pass; machine-readable results in `r11_acceptance_results.json`).

---

## 1. Selection (from measured weaknesses, not assumption)

The round-6 comparison table (`poc/OFFLINE_COMPARISON.md`) and the round-11 baseline rerun
expose these measured weaknesses of the three designs on identical deterministic signals:

| design | worst measured numbers |
|---|---|
| `delayline` | pitch error **+4.8 ¢ / −4.3 ¢** (FFT, r = 1.25 / 0.80); envelope modulation 15.7 / 16.8 dB (shipped 1 ms metric, burst test); **new round-11 finding: stereo contamination −0.44 dB** (shared stateful engine across channels in the CLI) |
| `pv` | onset delay 14–27 ms (pre-echo), attack 8–26 ms; drops inputs shorter than N/2 to silence |
| `pv_transient` | onset delay 20–29 ms, attack 5.5–8.5 ms |

The **single highest-value DSP improvement** is the one that removes the two largest
artifacts of the delay-line family — the few-cents carrier phase modulation and the
crossfade envelope modulation — because both stem from ONE mechanism (the fixed tap
spacing G), so one change fixes both, and the delay line is the design that wins on
transients and cost. The measured mechanism:

* The reference crossfades between taps spaced **exactly G samples**. For a sinusoid the
  taps then have a fixed phase difference `w0·G`; the mix coefficient
  `w·A + (1−w)·B` rotates its phase over each grain, which (a) shifts the carrier by
  `(Φ(1)−Φ(0))·(1−r)/G·sr/2π` — the analytically predicted and measured few cents — and
  (b) beats the envelope whenever `w0·G` approaches an odd multiple of π.

The stereo contamination is a **defect** (not a DSP-artifact trade-off), so it is fixed as
a correctness correction in the same v2 file (fresh engine per channel); it is not counted
as "the" improvement.

## 2. Design (see `poc/octavox_v2.py` docstring for the full derivation)

**Period-aligned grain splicing.** Commit the tap spacing as `S = k·T̂`
(`k = round(G/T̂)`, `T̂` = median of the last 8 per-block YIN period estimates, clamped
`S ∈ [G/2, 1.5G]`) **only at grain wraps**, so the running read position stays continuous
everywhere else. With `w0·S = 2πk` the two taps are phase-aligned: the mix coefficient
stops rotating, the carrier error and the coherent beating both vanish, and the output of
a periodic input is exactly the single-tap read. If no reliable period exists (noise,
transients), `S` stays at `G` — i.e. **exactly the reference behaviour** (graceful
fallback). Splice continuity when S changes at a wrap:

```
r > 1 (frac crossed 0):  frac_new = (frac_old + 1) · S_old / S_new   (d_new = d_old + S_old ⇒ A_new = B_old)
r < 1 (frac crossed 1):  frac_new = (frac_old − 1) · S_old / S_new   (d_new = d_old − S_new ⇒ B_new = A_old)
```

re-normalised into [0,1) with ±1 wraps. Because `k = round(G/T̂)` keeps every first commit
within half an input period of G (S_new/S_old ≈ 0.97–1.03) and later re-commits are
hysteresis-gated (0.5 %), the residual discontinuity is bounded by the crossfade weight's
~2 % bleed (≈ −32 dB), only on genuine pitch changes.

**Trade-offs (documented, accepted):**

* Unison (r = 1): the grain phase never wraps ⇒ S never commits ⇒ the reference two-tap
  comb remains (measured −2.42 dB at 220 Hz; the analog of open question Q-14 for our demo).
* Very small |1−r| (a few cents of shift): wraps are rare, so alignment commits are rare —
  but the artifacts scale down with |1−r| anyway.
* One initial commit transient per voice (~170 ms in at r = 1.25) while the pitch detector
  converges; comparable to the reference's own ring-fill start-up.
* Polyphonic input: alignment applies to the detected fundamental only; other partials keep
  partial (attenuated) modulation. The chord test measures/report this (suite passes with
  the same integrity gates).
* Cost: unchanged to slightly faster in practice (the pitch detector already ran per block
  in the reference for its own purposes).

## 3. Before / after (identical deterministic signals, 220 Hz sine, 1.2 s, 48 kHz)

| ratio | pitch error (YIN) | envelope modulation, period-synchronous window | envelope modulation, shipped 1 ms window |
|---|---|---|---|
| | reference → **aligned** | reference → **aligned** | reference → **aligned** |
| 1.2599 (+4 st) | +3.274 ¢ → **+0.030 ¢** | 2.36 dB → **0.00 dB** | 6.59 → 5.48 dB |
| 0.7492 (−5 st) | −5.835 ¢ → **+0.001 ¢** | 2.32 dB → **0.00 dB** | 10.82 → 10.06 dB |
| 2.0000 (+1 oct) | +9.131 ¢ → **+0.082 ¢** | 2.27 dB → **0.01 dB** | 3.02 → 1.15 dB |
| 0.5000 (−1 oct) | −17.757 ¢ → **−0.016 ¢** | 1.99 dB → **0.00 dB** | 13.90 → 13.32 dB |

Notes on the two modulation metrics: the **period-synchronous window** (an integer number
of output periods) isolates the true crossfade modulation — it drops to the float noise
floor (≤ 0.006 dB) with alignment. The **shipped 1 ms window** is dominated by the window's
own phase breathing on a tone (≈ 5.7 dB at 275 Hz, ≈ 10.7 dB at 110 Hz — the metric's
floor, not a signal property), which is why its "improvement" looks small; at +1 octave
(550 Hz, breathing only 1.4 dB) the 1 ms metric shows the real 3.0 → 1.15 dB improvement.

| property | reference → aligned |
|---|---|
| impulse latency | 102.5 ms → 102.5 ms (unchanged, by design) |
| burst attack (1 ms env) | 130.4 ms → 141.3 ms (crossfade phase at onset; same ballpark) |
| pre-onset leakage | −234 dB → −234 dB (causal, unchanged) |
| cost, 8 voices | 4306 → 3811 ms/s audio (same ballpark; detector already ran in the reference) |
| stereo contamination (CLI) | **−0.44 dB → −234 dB** (defect fix: fresh engine per channel) |
| unison gain @ 220 Hz | −2.42 dB → −2.42 dB (documented limitation, unchanged) |
| stability (8 voices, feedback 0.35, 4 s) | bounded → bounded (suite gates) |

Steady-state purity at +4 st (FFT tail, 0.5 s): out-of-harmonic energy 2.3e-6 → 2.2e-6
(both ~pure; the reference's few-cents error is a carrier shift, not added harmonics).

## 4. What was deliberately NOT changed

* The reference implementation files (`octavox_poc.py`, `octavox_offline.py`) are untouched
  and remain the regression baseline (their shipped example WAVs still reproduce
  byte-identically on this environment).
* The vocoder designs (their measured weaknesses — pre-echo onset delay and the short-input
  silence drop — remain open directions; the transient-aware variant already halves the
  attack).
* Unison behaviour (see trade-offs).

## 5. Reproduction

```bash
# acceptance suite (45 gated cases; exit 0 = pass; writes r11_acceptance_results.json)
python3 evidence/analysis_round11/r11_acceptance_tests.py

# quick A/B on any WAV
python3 poc/octavox_offline.py --in poc/example_input.wav --out /tmp/ref.wav --algo delayline --semitones 4 --voices 3
python3 poc/octavox_v2.py     --in poc/example_input.wav --out /tmp/v2.wav  --algo delayline_aligned --semitones 4 --voices 3
```

Deterministic: fixed seeds everywhere; only wall-clock cost rows vary with machine load.
