# G3-a2 — YIN source research: Tonalux article, Rust crate `pitch-detection`, 2002 paper mirrors

Task: fetch three targets via `curl -sL`, document formulas/conventions, and
classify every claim vs the canonical de Cheveigné & Kawahara 2002 JASA
algorithm (DOI 10.1121/1.1458024). Tags: [OBS] = directly observed in
fetched page/source; [DER] = derived/verified by arithmetic or simulation;
[UNK] = not verifiable (all paper mirrors blocked — §3). "Canonical" = the
2002 paper; wording items from the task briefing are marked [UNK] because no
readable copy was fetched this session.

---

## 1. Tonalux article

Fetched `https://tonalux.org/blog/yin-pitch-detection-algorithm-explained`
(HTTP 200, 39,659 B). Title: "Why Pitch Detection Is Hard, and How YIN
Solved It". Citations = line numbers of `/tmp/tonalux.html`.

### 1.1 Formulas as stated
- Difference function (L124): "the squared difference between the signal at time t and the signal at time t+tau,
  summed over a window" — verbal, no index bounds. Canonical: d_t(τ)=Σ_{j=t+1..t+W}(x_j−x_{j+τ})², fixed W [UNK —
  paper not fetched]. The article's own code (L147) uses the SHRINKING convention `for (j = 0; j < bufferSize -
  tau; ++j)`, i.e. d(τ)=Σ_{j=0}^{N−τ−1}(x_j−x_{j+τ})². [OBS L124, L147]
- CMNDF (L126): "each value d'(t,tau) is divided by the running mean of all d values from tau=1 up to the current
  tau. The CMNDF starts at exactly 1 by definition." Matches canonical d'_t(τ)=d_t(τ)/[(1/τ)Σ_{j=1..τ}d_t(j)],
  d'(0)=1; code (L159) is exactly `d_prime = d * tau / runningSum`, runningSum accumulating from τ=1. [OBS L126,
  L159]
- Threshold (L126, L138): "first minimum below a threshold of 0.1"; default 0.1 in code. Matches canonical 0.1
  [UNK for paper wording; corroborated by the Rust crate's comment "The YIN paper uses 0.1 as a threshold",
  yin.rs:63]. [OBS]
- Parabolic interpolation (L139; code L160–164): vertex formula `tau + 0.5*(d(τ+1)−d(τ−1))/(2d(τ)−d(τ−1)−d(τ+1))`.
  [DER] verified it is the correct parabola vertex through (τ−1,d₋),(τ,d₀),(τ+1,d₊) (true vertex +0.25 → returns
  exactly 0.25). The samples come from the function named `cmndf()`, which actually computes RAW d(τ) — misnamed —
  so the code interpolates the RAW difference function; the text never states the interpolation target. [OBS L147,
  L160–164; DER]

### 1.2 Six-steps list vs canonical (L135–140)
1. L135 "Compute autocorrelation over a window of at least two pitch periods." — Canonical Step 1 is the
   autocorrelation itself; window rule per briefing is "W ≥ largest expected period"; the article's "two pitch
   periods" is stricter (2T vs T). [OBS; canonical UNK]
2. L136 "Compute the difference function from the autocorrelation using the identity d(tau) = r(0) + r(0) −
   2*r(tau)" — DEVIATION: holds only when both lagged windows carry equal energy; the general identity is
   d(τ)=pow(x[0..W])+pow(x[τ..τ+W])−2·r(τ), which is what the Rust crate implements (internals.rs:220–227). [OBS;
   DER]
3. L137 "Normalize to CMNDF ... Set d'(1) = 1 by definition." — Canonical sets d'(0)=1 (d'(1)=1 then follows
   automatically); the article also says "starts at exactly 1" (L126) — internally sloppy, substantively fine.
   [OBS; minor]
4. L138 "Find the first tau below threshold (default 0.1). If none exists, use the global minimum." — Matches
   canonical in spirit. BUT the article's own code returns −1/"unvoiced" with NO global-min fallback, and returns
   at the first integer τ where d'<threshold (the crossing sample) — no descent to the dip's local minimum, no re-check of the interpolated ordinate vs the threshold (canonical per briefing). [OBS L138 vs L157–168; DER]
5. L139 "Parabolic interpolation around the minimum to achieve sub-sample frequency resolution without increasing
   the window size." — Concept matches; target unspecified in text (code: raw d, §1.1). [OBS]
6. L140 "Best local estimate: select among candidates using contextual smoothing or return the result directly for
   real-time applications." — The article DOES discuss Step 6, but as one vague sentence: NO temporal neighborhood
   [t−Tmax/2, t+Tmax/2], NO restricted ±20% re-run, no prediction from the previous period. [OBS L140]

### 1.3 Other classified claims
- L110 "cut error rates by roughly a factor of three" — [OBS page states]. Briefing reports paper figures gross error 0.77%→0.50% — not a factor of 3; either a different paper experiment or an exaggeration [UNK — paper blocked; g3b1 could not verify either].
- L126 "threshold = 0.1 is not a tunable parameter ... works across speech, singing voice, guitar ... The original paper reports this finding explicitly" — [OBS claim]; the paper tested speech (title "speech and music"); guitar/singing generality is editorial extension. [DER partial]
- L192 "minimum window ... at least two periods ... 40 Hz requires 50 ms at 44100 Hz, roughly 2205 samples" — [DER] arithmetic verified (40 Hz → 25 ms; 2 periods = 50 ms; 50 ms·44100 = 2205). ✓
- pYIN (L178): Mauch & Dixon 2014, HMM, beta distribution over thresholds, Viterbi, `librosa.pyin()`, Essentia PitchYinProbabilistic — [OBS], consistent with sibling g3b1 (Beta(2,18) 100-threshold grid, 2·n_pitch_bins HMM states, Viterbi). ✓

### 1.4 Tonalux verdict
Steps 1–3 paraphrased correctly (CMNDF convention matches canonical
exactly); but (a) the Step-2 identity is the equal-energy special case, (b)
the code deviates from its own text (shrinking-window d, no global-min
fallback, threshold on the pre-interpolation crossing, dead `tau==0`
branch), (c) Step 6 is mentioned but its mechanism is entirely absent.

---

## 2. Rust crate `pitch-detection` 0.3.0 (alesgenova)

Fetched (all HTTP 200): docs.rs crate root and the `detector/yin`,
`detector/mcleod`, `detector/trait.PitchDetector` API pages, plus the source
browser `/crate/pitch-detection/latest/source/` listings (full URL list in
Appendix A). Note: the task-given URLs `/pitch_detection/yin/` and
`/pitch_detection/mcleod/` DO NOT exist — docs.rs serves "This page does not
exist" because in 0.3.0 the modules live under
`pitch_detection::detector::{yin,mcleod,autocorrelation}`. The rendered
source viewer loads code via JS, so the canonical source was taken as the crates.io tarball
`https://static.crates.io/crates/pitch-detection/pitch-detection-0.3.0.crate` (822,228 B), whose file list matches the source
browser exactly. Citations = file:line in the tarball.
- Version: 0.3.0 = latest [OBS docs.rs sidebar; crates.io API max_version 0.3.0, published 2022-06-30; 0.2.0
  2020-11-13, 0.1.0 2019-03-17; docs.rs page shows a "05 October 2026" docs-build stamp].
- License: MIT/Apache-2.0 — [OBS] Cargo.toml `license = "MIT/Apache-2.0"`; docs.rs sidebar badge "MIT /
  Apache-2.0". No LICENSE files at tarball root (only tests/samples/LICENSE.txt for bundled WAVs). VERDICT: dual
  MIT or Apache-2.0 at user's choice; permissive.

### 2.1 Difference function — FIXED window (matches paper convention)
[OBS internals.rs:190–228] `windowed_square_error`:
d(τ)=Σ_{i=0}^{w−1}(x_i−x_{i+τ})², asserts `2*window_size <= signal.len()`;
computed as d(τ)=pow(x[0..w])+pow(x[τ..τ+w])−2·windowed_autocorr(τ) via FFT
(rustfft; windowed power updated incrementally). In `get_pitch`, w =
`signal.len()/2` (yin.rs:66); τ searched over [0, N/2); summation length
FIXED at w for every τ — matches the paper's fixed-W convention (W terms per
lag, observation ≥ W+τmax). [OBS yin.rs:66,75,78] BUT the module doc
yin.rs:8–14 states d(t)=Σ_{i=0}^{N−t}(s_i−s_{i+t})² — the SHRINKING
convention — which the code does NOT compute: doc/impl mismatch. [OBS both;
DER]

### 2.2 CMNDF — start lag + off-by-one
[OBS internals.rs:235–248] `yin_normalize_square_error`: d'(0)=1
(`square_error[0]=1`), loop from index 1 (`skip(1)`): `sum += d[i]; d'(i) =
d[i]*(i+1)/sum`. So d'(τ)=(τ+1)·d(τ)/Σ_{j=1..τ}d(j); canonical is
τ·d(τ)/Σ_{j=1..τ}d(j) ⇒ d'(1)=1 vs crate d'(1)=2 (unit test
internals.rs:308–315 asserts [0,6,14]→[1,2,2.1], confirming the (i+1)
factor). The cumulative mean uses (τ+1)-term scaling while summing j=1..τ —
an off-by-one vs the paper (factor (τ+1)/τ: 2.0 at τ=1, 1.009 at τ=109;
negligible at typical periods). The doc comment internals.rs:230–234
("d'(t)=d(t)/[(1/t)·Σ_{i=0}^{t}d(i)]") matches neither the code nor the
paper. [OBS+DER; canonical UNK]

### 2.3 Threshold and selection rule
- [OBS yin.rs:63–65] `threshold = 1 − clarity_threshold`, comment: "The YIN paper uses 0.1 as a threshold;
  TarsosDSP uses 0.2. `threshold` is not quite the same thing as 1 - clarity, but it should be close enough." NO
  default inside the crate — callers pass clarity_threshold (docs example 0.7 ⇒ threshold 0.3; tests 0.6 ⇒ 0.4,
  tests/main.rs:303–304). The paper's 0.1 is reachable only by passing clarity 0.9. [OBS]
- Selection: [OBS yin.rs:83–86] "STEP 4: The absolute threshold. We want the first dip below `threshold`. The YIN
  paper looks for minimum peaks. Since `pitch_from_peaks` looks for maximums, we take this opportunity to invert
  the signal." → val ← threshold−d'(τ); then `pitch_from_peaks(result, sample_rate, T::zero(), Quadratic)`.
  `detect_peaks` (peak.rs:25–83) = maxima of positive runs preceded AND succeeded by negative values (initial
  positive runs skipped; run touching array end discarded); `choose_peak` (peak.rs:91–96) = first peak > 0 ⇒
  deepest point of the FIRST excursion of d' below threshold. [DER] vs canonical: (a) within one below-threshold
  excursion with several local minima the deepest (not the first) is taken; (b) NO global-minimum fallback — if d'
  never dips below threshold, `get_pitch` returns None (canonical: use the global minimum); (c) extra voicing gate
  `square_sum(signal) < power_threshold ⇒ None` (yin.rs:70–72), not in the paper's core steps. McLeod instead
  passes clarity_threshold directly as the peak-height threshold (mcleod.rs:52–74). [OBS]

### 2.4 Parabolic interpolation target — CMNDF (inverted), not raw d
[OBS peak.rs:98–141, yin.rs:88–89] `correct_peak`/`find_quadratic_peak` fits
the parabola through data[idx−1], data[idx], data[idx+1] of the INVERTED
CMNDF (threshold−d'); vertex x*=−b/(2a), y*=c−b²/(4a); concave-up guarded to
±1 endpoints; frequency = sample_rate/(idx + x*). Inversion is affine, so
the interpolated abscissa equals that of interpolating d' directly — the
crate interpolates the CMNDF, NOT the raw d(τ). Canonical per briefing:
"abscissa taken from raw d(tau)" ⇒ DEVIATION (if briefing's reading is right
[UNK]). The interpolated ordinate is NOT re-compared to the threshold (the
peak test is pre-interpolation). [OBS+DER]

### 2.5 Clarity — how it is actually computed (verified numerically)
[OBS yin.rs:92–96] comment: "A `clarity` is not given by the YIN algorithm.
However, we can say a pitch has higher clarity if it's YIN normalized square
error is closer to zero. We can then take 1 - YIN error and report that as
`clarity`." Implementation: `clarity = 1 − threshold +
pitch.clarity/threshold` where `pitch.clarity = peak.1/input[0]`
(internals.rs:87) and `input[0] = threshold − d'(0) = threshold − 1 < 0`.
[DER, Appendix B] For a clean 440 Hz sine (48 kHz, window 1024,
clarity_threshold 0.6): interpolated dip y*=0.4000, inner clarity = 0.4/−0.6
= −0.6667, FINAL reported clarity = **−1.0667** — negative, outside the
documented "number between 0 and 1" (mod.rs:27–29), with inverted
monotonicity (deeper dip ⇒ lower clarity: clarity = (1−thr) −
y*/(thr·(1−thr))); intended paper-style 1−d'(τ*) = 1.0. So the crate's
statement "clarity is not supplied directly by YIN" is true, and the
synthesized formula is sign-broken as written. The FREQUENCY output itself
is excellent (440.015 Hz). [DER]

### 2.6 Step 6 — NOT implemented
[OBS yin.rs:23] module doc: "The current implementation does not perform
*Step 6* of the algorithm specified in the YIN paper." [OBS yin.rs:99–101]
trailing comment: "STEP 6: TODO. Step 6 of the YIN paper can eek out a
little more accuracy/consistency, but it also involves computing over a much
larger window." No temporal neighborhood [t−Tmax/2, t+Tmax/2], no ±20%
restricted re-run, no inter-frame state — `get_pitch` is stateless per call
(the detector holds only reusable buffers). [OBS]

### 2.7 Search range, sample rate, types
- τ range [0, N/2) — `result = real_buffer[..window_size]`, window_size = N/2 (yin.rs:66,75); no fmin/fmax
  parameters, no τmax tied to sample rate; only the fixed-window assert 2w ≤ N. [OBS]
- `padding` (constructor arg) only sizes the BufferPool (size+padding, internals.rs:38–46); it does NOT extend the
  YIN lag search (McLeod uses the full buffer instead). `assert_eq!(signal.len(), self.internals.size)` — signal
  length must equal the constructor size exactly. [OBS yin.rs:68]
- sample_rate passed per call, used solely as frequency = sample_rate/τ* (internals.rs:85–88); no resampling.
  Generic `T: Float + Sum` (f32/f64); FFT-based (rustfft 6.0.1, O(N log N)); no-std category; WASM-friendly
  (lib.rs:1–4). The paper's Step 1 is implicit (autocorrelation inside the FFT path); code labels STEP 2–5, STEP 6
  TODO. [OBS yin.rs:77–101]

---

## 3. 2002 paper — mirror attempts (ALL FAILED → UNK)

Requested canonical items (threshold-on-interpolated-ordinate wording;
abscissa from raw d(τ); Step 6 [t−Tmax/2, t+Tmax/2] search + restricted ±20%
re-run; W ≥ largest expected period; gross error 0.77%→0.50%) are ALL [UNK].
Attempts:
1. `https://studylib.net/doc/27347195/2002-jasa-yin-fund-freq-estim` — 2 attempts (plain + Chrome UA); both return
   a 2,117-B "Studylib Security Check / Verify You Are Human" JS challenge; content never served. [OBS blocked]
2. `https://www.researchgate.net/publication/11357466` — HTTP 403, "Access restricted / unusual activity" block
   page; skipped per instructions. [OBS blocked]
3. `http://auditorysystems.org` (must-try) + `https://auditorysystems.org`, `http://www.auditorysystems.org`,
   `…/research` — ALL curl exit 6 (could not resolve host); DNS dead from this sandbox. [OBS unreachable]
4. Bonus: `http://recherche.ircam.fr/equipes/pcm/cheveign/ps/2002_JASA_YIN_proof.pdf` (the exact URL the crate
   cites, yin.rs:2,51) — HTTP 403. [OBS blocked]
5. Bonus: Semantic Scholar API (DOI 10.1121/1.1458024) — record exists (JASA 2002, PubMed 12002874, de Cheveigné &
   Kawahara) but abstract elided, openAccessPdf CLOSED; Unpaywall `is_oa: false`. [OBS] Indirect corroboration
   only: sibling `g3b1_librosa.md` quotes librosa's docstring citing the paper's Step 4 sentence ("set an absolute
   threshold and choose the smallest value of tau that gives a minimum of d' deeper than this threshold. If none is
   found, the global minimum is chosen instead") — secondary, not a paper fetch. [OBS/UNK]

---

## 4. Biggest deviations summary
1. Tonalux: Step-2 identity is the equal-energy special case; threshold checked at the crossing sample (no local-
   min descent, no global-min fallback); `cmndf()` misnamed (computes raw d); Step 6 = one vague sentence.
2. Crate CMNDF off-by-one: d'(τ)=(τ+1)d(τ)/Σ_{j=1..τ}d(j) ⇒ d'(1)=2 (paper: 1); doc-comment formulas match neither
   code nor paper.
3. Crate threshold = 1−clarity_threshold (callers pass 0.6–0.7 ⇒ 0.3–0.4), not the paper's 0.1; no global-min
   fallback (returns None); deepest (not first) local minimum in a dip; interpolation on the CMNDF, not raw d.
4. Crate clarity formula sign-broken: −1.07 for a pure sine vs documented 0–1 (intended 1−d'(τ*)=1.0); the
   frequency output itself is accurate.
5. Step 6 status: crate openly omits it (doc + TODO); Tonalux waves at it; the paper's own wording is UNK (all
   mirrors blocked).

---

## Appendix A — URLs fetched this session (curl -sL; HTTP 200 unless noted)
- https://tonalux.org/blog/yin-pitch-detection-algorithm-explained (39,659 B)
- https://docs.rs/pitch-detection/latest/pitch_detection/ (21,638 B); `/pitch_detection/yin/`,
  `/pitch_detection/mcleod/…` — "page does not exist" (old task paths)
- https://docs.rs/pitch-detection/latest/pitch_detection/detector/yin/; …/detector/yin/struct.YINDetector.html;
  …/detector/mcleod/struct.McLeodDetector.html; …/detector/trait.PitchDetector.html
- https://docs.rs/crate/pitch-detection/latest/source/ (+ /src/, /src/detector/, /src/lib.rs.html — listings only)
- https://static.crates.io/crates/pitch-detection/pitch-detection-0.3.0.crate (822,228 B; Cargo.toml + src/* read
  from it)
- https://crates.io/api/v1/crates/pitch-detection (version/license metadata)
- https://studylib.net/doc/27347195/2002-jasa-yin-fund-freq-estim — blocked ×2
- https://www.researchgate.net/publication/11357466 — HTTP 403 blocked
- http://auditorysystems.org, https://auditorysystems.org, http://www.auditorysystems.org,
  https://auditorysystems.org/research — DNS failure (curl exit 6) ×4
- http://recherche.ircam.fr/equipes/pcm/cheveign/ps/2002_JASA_YIN_proof.pdf — HTTP 403
- https://api.semanticscholar.org/graph/v1/paper/DOI:10.1121/1.1458024 — metadata only, abstract elided;
  api.unpaywall.org/v2/10.1121/1.1458024 — is_oa false

## Appendix B — numeric verification of the crate's clarity [DER]
Faithful Python re-implementation of yin.rs + internals.rs + peak.rs (direct
d(τ) sums; the crate uses FFT but the algebra is identical), 440 Hz sine,
SAMPLE_RATE=48000, SIZE=1024, CLARITY_THRESHOLD=0.6 (test constants,
tests/main.rs:296–304):
- square_sum = 516.08 > POWER_THRESHOLD 300 ✓ passes the power gate
- fixed-window d(τ), w=512; crate normalization d'(1)=2.0 (paper: 1.0)
- inverted array input[0] = 0.4−1 = −0.6; first positive peak at idx=109 (τ* ≈ 109.087; frequency = 48000/109.0873
  = 440.015 Hz ✓)
- interpolated peak y* = 0.4000 → inner clarity = 0.4/−0.6 = −0.6667
- FINAL clarity = 1 − 0.4 + (−0.6667)/0.4 = −1.0667 (documented 0–1; intended 1−d'(τ*) = 1.0): sign- broken,
  monotonicity inverted (clarity = (1−thr) − y*/(thr·(1−thr))).

