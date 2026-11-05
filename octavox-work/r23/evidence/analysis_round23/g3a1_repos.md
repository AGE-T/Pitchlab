# G3-a1 — Research: two Python YIN repos (hyuncat/YIN-algorithm, ABSounds/YIN-Pitch)

Task: G3-a1 (research only, no C++). Method: GitHub REST API is rate-limited (403), so all
fetches used raw.githubusercontent.com/<owner>/<repo>/main/<path> plus the github.com HTML
pages for file listings / license metadata. Both repos' default branch is `main`
(embedded JSON `"defaultBranch":"main"` in both HTML pages) [OBS]. hyuncat also carries a
`master` branch with byte-identical content [OBS]. Tag legend: [OBS] = directly observed in
fetched code/text; [DER] = derived interpretation; [UNK] = not verifiable from fetches.

Baseline = de Cheveigné & Kawahara 2002 (JASA 111(4):1917–1930, doi:10.1121/1.1458024):
S1 autocorrelation; S2 d_t(τ)=Σ_{j=1..W}(x_j−x_{j+τ})² (fixed W terms, needs W+τ_max
samples); S3 CMNDF d'(τ)=d(τ)·τ/Σ_{j=1..τ}d(j) (cumulative mean from lag 1); S4 absolute
threshold (~0.1): first τ with d'(τ)<threshold, descend to local minimum, else global-min
fallback; S5 parabolic interpolation of d' around the chosen minimum; S6 temporal
best-local-estimate τ'=argmin_{|τ−τ_prev|≤Δ} d'(τ).

## Repo 1 — hyuncat/YIN-algorithm

Identity [OBS]: https://github.com/hyuncat/YIN-algorithm — root files: README.md +
yin.ipynb ONLY (no .py module). Algorithm lives in the notebook (nbformat 4, python3
kernel; numpy / matplotlib / scipy.signal / IPython.display; librosa and numba `@njit`
mentioned in comments only, not active). Demo: synthetic 440 Hz signal (7 harmonics,
decaying amplitudes, ADSR envelope, noise), fs=44100, ONE frame of FRAME_SIZE=4096.

License verdict: NO LICENSE — raw .../main/LICENSE → HTTP 404, .../main/LICENSE.txt → 404,
HTML page shows no license entry [OBS]. Treat as ALL-RIGHTS-RESERVED, terms [UNK].
Behavior findings only; do not port its code.

### Pipeline walk-through (function → notebook code cell)

(1) FFT autocorrelation `autocorrelation_fft` (cell 10) [OBS]:
- Wiener–Khinchin: r(τ) = irfft(|rfft(x, size_pad)|²)[:τ_max], zero-padded to a "smooth"
  size ≥ W+τ_max (candidate sizes 16,18,20,24,25,27,30,32 × 2^p2).
- Returns autocorrelation[:W−τ_max]; at demo params (W=4096, τ_max=225) that slice is a
  no-op, so autocorr[τ] = biased linear autocorrelation over the W−τ overlap [OBS/DER].
- Comment cites "type II autocorrelation from jasa 2002" [OBS]; what "type II" denotes is
  [UNK] — not verifiable as the paper's own terminology.

(2) Difference function `difference_function` (cell 14) [OBS]:
- d(τ) = r_0 + r_0 − 2·r(τ), with r_0 = Σ_{j=0..W−1} x_j² held CONSTANT for every τ
  (energy array = np.full(autocorr.shape, r_0)); d(0):=0 forced.
- So: constant-energy approximation; cross-term overlap shrinks with τ (N−τ terms), NOT
  the paper's fixed-W sum; needs only W samples (no W+τ_max look-ahead) [DER].
- Docstring claims "without assuming constant energy" — contradicted by the code, which
  does assume constant energy [OBS of both; DER: docstring is wrong].

(3) CMNDF `cmndf` (cell 16) [OBS]:
- d'(0):=1; for τ=1..τ_max−1: running prefix sum, d'(τ)=d(τ)/((1/τ)·Σ_{j=1..τ}d(j)).
- Cumulative mean starts at lag 1 — MATCHES paper S3 [OBS].
- No divide-by-zero guard (all-zero prefix → NaN) [OBS].
- Latent bug [DER]: if τ_max > W/2 the diff array (length min(τ_max, W−τ_max)) is shorter
  than τ_max and the τ loop would IndexError; not triggered in the demo.

(4) Threshold `absolute_threshold` (cell 25) [OBS]:
- threshold = 0.1 (default and call site) — matches the paper's ~0.1 [OBS].
- Rule: scan local minima of the RAW d (scipy.signal.argrelmin(diff_fct, order=1), from
  cell 20) and return the first with d'(τ) ≤ 0.1. Deviations: candidates are minima of d,
  not of d' (usually coincide; not guaranteed) [DER]; ≤ vs the paper's strict < [DER minor].
- Fallback: if none below threshold → argmin of d' (global minimum) — paper S4 fallback
  IS implemented [OBS].

(5) Parabolic interpolation `parabolic_interpolation` (cell 28) [OBS]:
- 3-point fit on the RAW difference function d — comment: "to avoid + bias for small lags
  from CMNDF". x* = x + (y₋−y₊)/(2·(y₋−2y₀+y₊)) (plus an unused y* formula).
- Paper S5 interpolates d', not d — deliberate deviation [OBS].
- Guards: boundary (x≤0 or ≥len−1 → no interpolation); denominator==0 → no interpolation [OBS].

(6) Search range [OBS]: τ ∈ [0, 225); τ_max = int(44100/196.0) = 225 (196 Hz floor via
MIN_VIOLIN_FREQ). NO lower-lag bound → no f_max cap (high-frequency false picks possible
on non-synthetic input) [DER]. Notebook output confirms: τ_max 225; d-minima at τ=50/100/
150/200 (882/441/294/220.5 Hz); chosen τ=100 → 441.0 Hz; final 440.06 Hz after parabolic [OBS].

(7) Window/hop [OBS]: W = FRAME_SIZE = 4096 @ 44.1 kHz = 92.88 ms; ONE frame analyzed
(signal[4096:8192]); NO frame loop, NO hop — a single-frame demonstration, not a tracker.
Self-test: np.testing.assert_almost_equal(final_pitch, 440.0, decimal=1) [OBS].

(8) FFT usage: YES — Wiener–Khinchin rfft/irfft (the only FFT-based YIN math of the two
repos) [OBS].

(9) Step 6 (temporal best-local-estimate): ABSENT — no τ_prev tracking, no ±Δ window, no
temporal smoothing; only one frame is ever analyzed [OBS].

### Deviations vs 2002 paper (hyuncat)
1. d(τ) = 2r₀ − 2r(τ) (constant-energy) instead of the fixed-W squared-difference sum [OBS].
2. Shrinking N−τ overlap; uses only W samples (paper needs W+τ_max per frame) [DER].
3. Parabolic interpolation on raw d, not on d' [OBS].
4. Threshold candidates = argrelmin of d, values tested on d' [OBS].
5. ≤ threshold instead of < [DER minor].
6. Single-frame demo; no S6; no streaming/hop loop [OBS].
7. Paper-correct parts: CMNDF cumulative mean from lag 1 [OBS]; threshold 0.1 [OBS];
   global-min fallback [OBS]; parabolic boundary/zero-denominator guards [OBS].

## Repo 2 — ABSounds/YIN-Pitch (AGPL-3.0 — behavior description only)

Identity [OBS]: https://github.com/ABSounds/YIN-Pitch — root files: README.md,
YIN_pitch.py (5,036 B, cleaned-up script), YIN_pitch.ipynb (4.4 MB, embedded outputs),
Figures/, Sounds/ (freesound.org demo audio). Stack: numpy, scipy.signal, scipy.io.wavfile,
matplotlib, tqdm. README credits the 2002 paper (doi:10.1121/1.1458024) and links the
audition.ens.fr PDF [OBS]. README-quoted demo result: "Predicted tau: 159.15 samples /
301.61 Hz" — consistent with a 48 kHz file (48000/159.15 ≈ 301.60 Hz) [OBS/DER].

License verdict: AGPL-3.0 CONFIRMED [OBS]: raw .../main/LICENSE → HTTP 200, 34,503 B;
header "GNU AFFERO GENERAL PUBLIC LICENSE, Version 3, 19 November 2007"; contains §13
"Remote Network Interaction; Use with the GNU General Public License" (the AGPL-only
network-copyleft clause); GitHub HTML embeds "license":{"spdxId":"AGPL-3.0","name":"GNU
Affero General Public License v3.0"}. Per task rules: describe behavior only; the single
≤2-line formula excerpt below is the only verbatim quote; NOTHING from this repo may be
ported into Octavox.

### Pipeline walk-through (behavior; function → YIN_pitch.py line)

(1) Difference function `difference_vectorized` (L41; notebook cells 14/15) [OBS]:
direct brute-force squared-difference, no FFT. The one verbatim excerpt (crux of the
summation-limits question, ≤2 lines):
    diff[tau] = np.sum((x[1:W-tau] - x[1+tau:W])**2)
- 0-based j runs 1..W−τ−1, i.e. 1-based Σ_{j=2..W−τ}(x_j−x_{j+τ})²: SHRINKING (W−τ−1)
  terms, not fixed-W; also skips the first sample — off-by-one vs both the paper (j=1..W)
  and the author's own axis label Σ_{j=1..W−τ} [OBS/DER]. Needs only W samples [DER].
- A slower nested-loop reference version is kept alongside "because it's easier to
  understand"; both compute the same quantity [OBS].
- No guard: τ ≥ W−1 gives empty slices → silently 0 [DER].

(2) CMNDF `computeCmndf` (L59; cell 18) [OBS]: d'(τ)=d(τ)/((1/τ)Σ_{j=1..τ}d(j)) —
cumulative mean starts at lag 1, MATCHES paper S3. But the output array covers only
τ ∈ [min_tau, max_tau) (indexed τ−min_tau); the `tau==0 → 1.0` branch is dead whenever
min_tau ≥ 1 (always true in the frame pipeline; reachable only in the demo cell where
min_tau=0) [OBS]. Prefix sum is recomputed per τ (O(τ²)); builtin `sum` shadowed; no
divide-by-zero guard [OBS].

(3) Threshold `find_first_local_min_below_threshold` (L5; cell 4) [OBS]: first local
minimum of the d' ARRAY strictly below threshold (scipy.signal.argrelmin, then a linear
scan). Default cmndf_threshold = 0.4 in `pitchDetect`; the demo cell uses 0.2 — both far
above the paper's ~0.1–0.15 [OBS]. NO global-minimum fallback: no candidate below
threshold → None (unvoiced marker); deviates from paper S4's fallback rule [OBS].
- The [min_f0, max_f0] restriction is implemented purely by array slicing (low lags are
  never in the array) [DER].

(4) Parabolic interpolation `parabolic_interp` (L22; cell 5) [OBS]: 3-point fit ON d'
(MATCHES paper S5): x_shift = 0.5·(y₋−y₊)/(y₋−2y₀+y₊); absolute τ = min_tau + idx +
x_shift; f0 = fs_ds/τ. No explicit boundary/zero-denominator guards, but argrelmin only
returns interior indices (1..len−2), so neighbors exist by construction [OBS/DER].

(5) SciPy decimation / offline zero-phase filtering [OBS]: `pitchDetect` first calls
scipy.signal.decimate(audio, decimation_factor=8, zero_phase=True) — anti-aliased
downsampling (default Chebyshev-I IIR) applied forward-backward (filtfilt-style,
zero-phase) → strictly OFFLINE/batch, cannot run causally on a stream. fs_ds = fs//8
(integer floor). Decimation is not part of YIN 2002 at all [DER: efficiency deviation].

(6) Window/hop [OBS]: W = 256 at fs_ds → 46.44 ms window @ 5512 Hz (44.1 kHz input);
hop = W//2 = 128 → 23.22 ms (50% overlap); frames via range(0, length, W//2), full-frame
check, trailing partial frames dropped. The single-frame demo cell uses W=1024 at full
fs. A Hamming window of length W·8 is used ONLY for spectrogram plotting, not detection [OBS].

(7) Search range [OBS]: min_tau = fs_ds//max_f0, max_tau = fs_ds//min_f0 (integer floor).
Demo ranges: contralto E3–E5 (τ≈7..33 @5512 Hz), baritone E2–E4 (16..67), soprano C4–C6
(5..21), mezzo A3–F5 (7..25) [OBS/DER]. Concern [DER]: with W=256 and max_tau up to 67
the difference sum uses only ~188 terms — short-window CMNDF noise is a plausible reason
the author raised the threshold to 0.4.

(8) FFT usage: NONE in the detection path (brute-force difference); FFT only in the
spectrogram visualization [OBS].

(9) Output [OBS]: per-frame f0 list (object ndarray with None for unvoiced), plotted over
a spectrogram, tqdm progress bar.

(10) Step 6 (temporal best-local-estimate): ABSENT — each frame is decided independently;
no τ_prev, no ±Δ window, no continuity/median/Viterbi post-processing [OBS].

### Deviations vs 2002 paper (ABSounds)
1. Shrinking (W−τ−1)-term difference sum that also skips sample 0 (off-by-one) [OBS].
2. No fixed-W summation; no W+τ_max look-ahead [DER].
3. Threshold 0.4 (pipeline default) / 0.2 (demo) vs the paper's ~0.1–0.15 [OBS].
4. No global-min fallback — returns None instead (paper S4 prescribes the fallback) [OBS].
5. SciPy decimation by 8 with zero-phase filtering — offline-only preprocessing not in
   the paper [OBS].
6. CMNDF stored only over [min_tau, max_tau); the τ=0 branch is dead in the pipeline [OBS].
7. Paper-correct parts: cumulative mean from lag 1 [OBS]; parabolic on d' [OBS]; search
   range bounded by f0 limits [OBS].

## Cross-repo comparison vs canonical YIN steps

| YIN 2002 step | hyuncat/YIN-algorithm | ABSounds/YIN-Pitch |
|---|---|---|
| S2 d(τ), fixed W terms | ✗ 2r₀−2r(τ), N−τ overlap [OBS] | ✗ Σ j=2..W−τ, skips j=1 [OBS] |
| S3 CMNDF mean from lag 1 | ✓ [OBS] | ✓ [OBS] |
| S4 threshold ~0.1 + global-min fallback | ✓ 0.1; ✓ fallback; minima of d [OBS] | ✗ 0.4/0.2; ✗ no fallback (None) [OBS] |
| S5 parabolic on d' | ✗ on raw d (deliberate) [OBS] | ✓ on d' [OBS] |
| S6 best-local-estimate | ✗ absent (single frame) [OBS] | ✗ absent [OBS] |
| FFT acceleration | ✓ rfft/irfft Wiener–Khinchin [OBS] | ✗ brute force [OBS] |
| Streaming-capable | ~ demo only, no loop [OBS] | ✗ decimate(zero_phase) is batch-only [OBS] |
| License | none → all-rights-reserved [UNK] | AGPL-3.0 (verified) [OBS] |

## Takeaways / next actions for the Octavox work

1. NEITHER repo is license-safe to port: hyuncat has no license; ABSounds is AGPL-3.0
   (network copyleft). Use both as behavioral references only; implement Octavox from the
   2002 paper + clean-room notes [DER].
2. Both use the shrinking N−τ difference window needing only W samples. Octavox must
   choose fixed-W (paper-faithful, needs W+τ_max per frame) vs shrinking (cheaper)
   explicitly and document it [DER].
3. Both start the CMNDF cumulative mean at lag 1 (paper-correct) — cheap to match [OBS].
4. NEITHER implements Step 6; Octavox can differentiate by actually implementing the
   temporal best-local-estimate (±Δ window around previous τ) — the paper's main
   anti-jitter / octave-error mechanism that both repos omit [OBS/DER].
5. ABSounds' 0.4 threshold on ~46 ms windows vs hyuncat's 0.1 on 92.9 ms: threshold
   effectiveness depends on window length vs τ_max; make threshold and window tunable and
   test on real audio, not just synthetics [DER].
6. Parabolic target differs (d vs d'); paper says d', hyuncat argues d avoids small-lag
   CMNDF bias — evaluate both targets in Octavox tests [OBS/DER].
7. Offline zero-phase decimation (ABSounds) is incompatible with real-time design; if
   Octavox downsamples, use causal anti-alias filtering [DER].

## Appendix A — fetched URLs (HTTP 200 unless noted; GitHub API unused due to 403)

hyuncat/YIN-algorithm (branch main; `master` also live and identical):
1. https://raw.githubusercontent.com/hyuncat/YIN-algorithm/main/README.md (1,940 B)
2. https://raw.githubusercontent.com/hyuncat/YIN-algorithm/main/yin.ipynb (882,518 B)
3. https://raw.githubusercontent.com/hyuncat/YIN-algorithm/main/LICENSE → 404 (no license)
4. https://raw.githubusercontent.com/hyuncat/YIN-algorithm/main/LICENSE.txt → 404
5. https://raw.githubusercontent.com/hyuncat/YIN-algorithm/master/README.md (identical)
6. https://raw.githubusercontent.com/hyuncat/YIN-algorithm/master/yin.ipynb (882,518 B)
7. https://github.com/hyuncat/YIN-algorithm (HTML; file list README.md + yin.ipynb;
   defaultBranch "main"; no license entry)

ABSounds/YIN-Pitch (branch main):
8. https://raw.githubusercontent.com/ABSounds/YIN-Pitch/main/README.md (4,001 B)
9. https://raw.githubusercontent.com/ABSounds/YIN-Pitch/main/YIN_pitch.py (5,036 B)
10. https://raw.githubusercontent.com/ABSounds/YIN-Pitch/main/YIN_pitch.ipynb (4,421,734 B)
11. https://raw.githubusercontent.com/ABSounds/YIN-Pitch/main/LICENSE (34,503 B, AGPL-3.0)
12. https://github.com/ABSounds/YIN-Pitch (HTML; file list README.md, YIN_pitch.ipynb,
    YIN_pitch.py, Figures/, Sounds/; license metadata spdxId "AGPL-3.0"; defaultBranch "main")
13. https://github.com/ABSounds/YIN-Pitch/tree/main/Sounds (200)
14. https://github.com/ABSounds/YIN-Pitch/tree/main/Figures (200)

Local fetch artifacts: /tmp/hyuncat_readme_main.md, /tmp/hyuncat_yin.ipynb,
/tmp/hyuncat_code.txt (extracted code cells), /tmp/hyuncat_page.html,
/tmp/absounds_yin.py, /tmp/absounds_readme.md, /tmp/absounds_nb.ipynb,
/tmp/absounds_license.txt, /tmp/absounds_page.html.

## Appendix B — key observed numbers (cross-check sheet)

- hyuncat: fs 44100; FRAME_SIZE 4096 (92.88 ms); τ_max 225 (=44100/196); threshold 0.1;
  d-minima at τ 50/100/150/200 (882/441/294/220.5 Hz); chosen τ=100 → 441.0 Hz; final
  440.06 Hz after parabolic on raw d; assert vs 440.0 @ decimal=1.
- ABSounds: W 256 @ fs_ds=fs//8 (46.44 ms @ 5512 Hz); hop 128 (23.22 ms); threshold 0.4
  (pipeline) / 0.2 (demo cell: W=1024, min_tau=0, max_tau=fs//50); decimation factor 8,
  zero_phase=True; demo f0 301.61 Hz at τ 159.15 (48 kHz file); demo f0 ranges E2–C6.
