# G3-b1 — librosa `yin` / `pyin` implementation research (librosa 1.0.0, doc/main)

Task: fetch/inspect librosa's actual yin/pyin; document exact conventions and deviations
vs De Cheveigné & Kawahara (JASA 111.4, 2002). Method: `curl -sL` on librosa.org doc/main
pages + `_modules` source pages, HTML stripped to plain Python, line-level reading.
Line refs = `/tmp/librosa_pitch.py` (from `_modules/librosa/core/pitch.html`). Tags:
[OBS]=observed in fetched source/docs; [DER]=derived/numerically verified; [UNK]=not
verifiable from fetched material. Version "librosa 1.0.0 documentation" (doc/main,
© 2013–2026). librosa <=0.10 pyin lacks `transition_min_prob`.

## 1. `librosa.yin` (pitch.py:492–640)

### 1.1 Signature / defaults [OBS 492–503, 574–581]
`yin(y, *, fmin, fmax, sr=22050, frame_length=2048, hop_length=None, trough_threshold=0.1,
center=True, pad_mode="constant")`. `fmin`/`fmax` REQUIRED (keyword-only; ParameterError
if None, 574); `hop_length=None -> frame_length // 4` (580-581) => 75% overlap (512 @
default). No `win_length` parameter exists at all.

### 1.2 frame_length vs win_length; where the two "windows" are
- [OBS 595–597] `min_period = floor(sr/fmax)`; `max_period = min(ceil(sr/fmin), frame_length-1)`.
- [OBS 586–593] zero-pad by `frame_length//2` both sides when `center=True`, then
  `util.frame(y, frame_length, hop_length)`.
- [OBS 404–416] per frame: `acf = autocorrelate(y_frames, max_size=max_period+1)` (full
  linear ACF via zero-padded FFT, audio.py:1350–1424), then `d(0)=0; d(k) = 2*(ACF(0)-ACF(k))
  - sum_{m<k} y[m]^2`, k=1..max_period.
- [DER, numerically verified — Appendix B] this EQUALS `d(k) = sum_{j=0}^{N-1}(x[j] - x~[j+k])^2`
  with `x~` zero-extended beyond the frame (agreement 4.3e-14, N=64 random). It is NOT the
  paper's `d_t(tau)=sum_{j=1..W}(x_j - x_{j+tau})^2` over two fully-contained windows of a
  W+tau_max observation (differs by the frame's tail energy; 25.9 on the test).
- So librosa has ONE length: effective integration overlap shrinks with lag (N-k samples)
  plus a k-length tail-vs-zeros term; tau_max <= frame_length-1; no "frame_length >=
  2*win_length" rule because win_length does not exist.
- [OBS 950–984 `__check_yin_params`] hard error if `sr/fmin >= frame_length-1` (">=1
  period of fmin must fit"); WARNING if `sr/fmin >= frame_length//2` ("less than two
  periods of fmin fit ... inaccurate pitch detection"; suggests frame_length >=
  ceil(sr/fmin)*2 + 1).

### 1.3 CMNDF construction [OBS 379–428]
Output shape `(max_period - min_period + 1, n_frames)` — CMNDF STORED only for lags in
`[min_period, max_period]` (paper Eq. 8):
- `d(0)=0` set explicitly (412); cumulative mean starts at LAG 1 (`k = slice(1, max_period+1)`,
  411,423) — d(0) excluded, matching the paper's sum_{j=1..tau}.
- denominator for lag tau = `(1/tau)*sum_{j=1..tau} d(j)` over ALL lags from 1 (not from
  min_period) — `cumulative_mean[..., min_period-1 : max_period, :]` (424).
- `+ util.tiny()` guards division by zero (426).

### 1.4 Trough selection rule — paper's Step 6, NOT global argmin
- [OBS 607–609] `is_trough = util.localmin(yin_frames, axis=-2)`; first stored bin
  (lag=min_period) special-cased: `is_trough[...,0,:] = d'[0] < d'[1]`.
  `localmin` (util.py:1160–1219, numba stencil 1069–1071): bin i is a trough iff
  `x[i] < x[i-1] AND x[i] <= x[i+1]`; last bin `x[-1] < x[-2]`; first never (zero pad).
- [OBS 612] `is_threshold_trough = is_trough AND (yin_frames < trough_threshold)` — STRICT
  `<` against the CMNDF value d'.
- [OBS 622] `yin_period = np.argmax(is_threshold_trough, axis=-2)` — argmax of a boolean
  mask = FIRST True = SMALLEST lag >= min_period that is a local min AND below threshold.
  The code quotes the paper (614–617): "choose the smallest value of tau that gives a
  minimum of d' deeper than this threshold. If none is found, the global minimum is
  chosen instead."
- [OBS 621,627–628] fallback: if NO trough below threshold in the restricted range,
  `global_min = argmin(yin_frames)` — global argmin of CMNDF restricted to
  [min_period, max_period].
- VERDICT: "global argmin restricted to lag >= min_period" is FALSE as primary rule —
  it is only the no-qualifying-trough fallback. Docstring (508–509) likewise: "the first
  minimum ... below trough_threshold is selected".

### 1.5 Threshold semantics [OBS]
`trough_threshold=0.1` default ("absolute threshold for peak estimation"), applied to the
CMNDF (d') with strict `<` (612) — matching the paper's recommended absolute threshold
on d' (paper's exact wording ~0.1–0.15 [UNK]).

### 1.6 Parabolic interpolation [OBS 431–487, 604–636]
- Target = the CMNDF (`_parabolic_interpolation(yin_frames)`, 605): librosa interpolates
  d', not raw d (paper's target [UNK]). Stencil at bin i: `a = x[i+1]+x[i-1]-2x[i]`,
  `b = (x[i+1]-x[i-1])/2`, `shift = -b/a`; `|b| >= |a|` => 0; first/last bins => 0 (484–485).
- `tau = min_period + idx + shift` via `np.take_along_axis` (632–636); `f0 = sr / tau` (639).

### 1.7 `center=True`, pad_mode, latency [OBS 540–551, 586–590]
- Default `center=True`, `pad_mode="constant"` (zeros): `np.pad(y, (frame_length//2, frame_length//2))`.
  Frame t is CENTERED at `y[t*hop_length]` => the estimate at time t needs samples up to
  `t*hop + frame_length/2` — non-causal look-ahead.
- [DER] latency: look-ahead = frame_length/2 (1024 @ 2048/22050 = ~46.4 ms) on top of the
  frame span (~92.9 ms); estimates every 512 samples (~23.2 ms). Streaming must accept
  +46 ms latency or use `center=False` (causal framing; timestamps shift by
  frame_length/2). No streaming mode exists in librosa.

### 1.8 Voicing / sample rate [OBS]
- `yin` has NO voicing output: every frame returns a value (no NaN, no voiced flag; the
  docstring example shows a plain array); the no-trough fallback always yields an f0.
- `sr` is used ONLY for period<->Hz conversion (`min/max_period`, `f0 = sr/tau`) — the DSP
  is entirely in samples; default sr=22050.

## 2. `librosa.pyin` (pitch.py:646–947)

### 2.1 Threshold prior (beta) [OBS 654–655, 811–819]
- `n_thresholds=100`, `beta_parameters=(2, 18)`: `thresholds = linspace(0,1,101)`;
  `beta_probs = diff(beta.cdf(thresholds, 2, 18))` => 100 masses (sum 1); Beta(2,18)
  mean = 0.1 (resonant with YIN's 0.1). Support [0,1] so no explicit truncation; no
  `n_quantiles` param; effective thresholds are the 100 right edges `thresholds[1:]` (899).
- [OBS 811–813] comment: "The implementation here follows the official pYIN software which
  differs from the method described in the paper" — discrete grid + beta weights instead of
  sampling one threshold per frame (Mauch & Dixon 2014's exact procedure [UNK]).

### 2.2 Trough prior + global-min bonus [OBS 884–925]
- Same `localmin` troughs + first-bin special case (888–890); frame with no troughs =>
  zero observation mass in voiced bins (893–894). Per threshold s, qualifying troughs get
  a Boltzmann prior over RANK among qualifiers: `scipy.stats.boltzmann.pmf(position,
  lambda=2, N)` (908–910), `trough_positions = cumsum(trough_thresholds, axis=0) - 1`
  (903) — smaller periods get exponentially more mass; `probs = trough_prior . beta_probs` (917).
- `no_trough_prob=0.01`: for thresholds where NO trough qualifies, the beta mass is
  partially given to the global-min trough: `probs[global_min] += 0.01 *
  sum(beta_probs[:n_thresholds_below_min])` (919–923) — probabilistic version of yin's
  deterministic fallback. Candidates refined with the SAME parabolic interpolation of
  the CMNDF (929–932), `f0_candidates = sr / (min_period + idx + shift)`.

### 2.3 HMM states and transitions [OBS 821–852]
- `resolution=0.1` => `n_bins_per_semitone = ceil(1/0.1) = 10` (10-cent bins; docstring:
  "0.01 corresponds to cents"). `n_pitch_bins = floor(12*10*log2(fmax/fmin)) + 1` (C2–C7:
  601). HMM = `2 * n_pitch_bins` states (1202 for C2–C7): bins `[0, n_pitch_bins)` voiced,
  `[n_pitch_bins, 2n)` unvoiced mirror (939–945, 860–862).
- Candidate -> bin: `round(12*n_bins_per_semitone*log2(f0/fmin))` clipped to
  `[0, n_pitch_bins]` (935–936) — the upper clip can reach the FIRST UNVOICED row for a
  candidate at the very top of the range (off-by-one quirk at fmax).
- Pitch continuity: `max_semitones_per_frame = round(max_transition_rate*12*hop/sr)`
  (`max_transition_rate=35.92` oct/s = 10 semitones @ 22050/512);
  `transition_local(n_pitch_bins, width=10*10+1=101, window="triangle", wrap=False)`
  (843–848). `transition_local` (sequence.py:2040–2152) is zero outside `|i-j| > width`
  => non-zero band ±(width//2) = ±50 bins = ±5 semitones per frame [DER].
- OCTAVE JUMPS: a direct 12-semitone jump has probability 0 (band ±5 semitones,
  wrap=False kills wraparound). Octave errors are instead resolved by switching voicing:
  `t_switch = transition_loop(2, 1-switch_prob)` = [[0.99,0.01],[0.01,0.99]],
  `transition = kron(t_switch, transition_local)` (851–852) — the switch (1%) preserves
  pitch locality too; true octave discontinuities must traverse several frames or
  unvoiced states. [OBS construction + DER consequence] `p_init` uniform over 2n states (854).

### 2.4 Viterbi decoding [OBS 856–857; sequence.py:1161–1245]
- `states = sequence.viterbi(observation_probs, transition, p_init,
  transition_min_prob=1e-4)` (pyin default `transition_min_prob=1e-4`, 664).
- Core: log-domain forward DP over ALL frames then BACKTRACE from the final argmax
  (1236–1241) — whole-sequence (batch) decoding, not incremental. Pruning: only
  predecessors with transition prob >= 1e-4 considered; docstring (732–737,
  sequence.py:1181–1185): None => exact inference, otherwise "approximate inference,
  but can significantly reduce computation time".
- [DER] real-time consequence: no fixed-lag mode exists; streaming pYIN must approximate
  with fixed-lag Viterbi or per-frame argmax of observation probs (not equivalent —
  loses transition smoothing).
- [DER] costs: exact O(T*n_states²), n_states=1202 (C2–C7) => ~1.4M transitions per
  frame-step; pruned to the ~101-nonzero-per-row band => O(T*n_states*101) plus
  T*n_states memory for `value`/`ptr`. No runtime benchmark reported ([UNK]).

### 2.5 Voicing decision and outputs [OBS 859–867, 942–947]
- `voiced_flag = states < n_pitch_bins` — voicing decided by the DECODED STATE identity,
  not by thresholding a posterior; `voiced_prob` = clipped sum of voiced-bin observation
  probabilities (pre-Viterbi) is returned for consumers to threshold. Unvoiced-bin
  observation = `(1 - voiced_prob)/n_pitch_bins` uniform (945).
- `f0 = fmin * 2**(bin/(12*n_bins_per_semitone))`; unvoiced frames get `fill_na` (default
  `np.nan`; `None` keeps the unvoiced state's best-guess f0) (860–865). Returns
  `(f0, voiced_flag, voiced_prob)`.

## 3. Deviations vs the 2002 YIN paper (and pYIN 2014)
1. Windowing: paper = fixed W inside a W+tau_max observation; librosa = single
   `frame_length` frame with the lagged window implicitly ZERO-EXTENDED (verified,
   Appendix B); overlap shrinks with lag; no 2*win_length rule — instead ">=1 period"
   (error) / ">=2 periods" (warning) of fmin. [DER]
2. Selection rule: librosa implements the PAPER's Step 6 exactly (first local min below
   absolute threshold; global min only as fallback), contrary to the task's hypothesis;
   searches restricted to lags >= min_period. [OBS]
3. Threshold: absolute 0.1 on d', strict `<`, matching the paper. [OBS]
4. Parabolic interpolation on d' (CMNDF) at the selected trough, guarded |shift|<=1;
   paper's target (d vs d') [UNK]. [OBS]
5. Centered frames (center=True default, zero padding): non-causal, ~frame_length/2
   look-ahead; the 2002 paper is a block estimator with no centering convention, and
   injected zeros inflate d(tau) at edge frames. [OBS+DER]
6. Step 6 vs HMM: plain `yin` = the paper's 6 steps, no voicing; `pyin` replaces the fixed
   threshold with a Beta(2,18)-weighted 100-threshold grid + Boltzmann trough prior and a
   2*n_pitch_bins-state HMM with Viterbi (pYIN, Mauch & Dixon 2014) — no HMM in the 2002
   paper; "follows the official pYIN software, not the paper". [OBS]
7. Computation: d(tau) via FFT autocorrelation (O(N log N)/frame), not direct sums. [OBS]

## 4. License [OBS]
librosa is ISC-licensed: "## ISC License — Copyright (c) 2013--2026, librosa development
team." (raw.githubusercontent.com/librosa/librosa/main/LICENSE.md; doc footer: "Copyright
2013--2026, librosa development team"). Permissive, attribution required, no copyleft.

## Appendix A — URLs fetched (all this session, curl -sL)
- https://librosa.org/doc/main/api/generated/librosa.yin.html (70,521 B)
- https://librosa.org/doc/main/api/generated/librosa.pyin.html (79,311 B)
- https://librosa.org/doc/main/_modules/librosa/core/pitch.html (130,139 B -> /tmp/librosa_pitch.py)
- https://librosa.org/doc/main/_modules/librosa/util/utils.html (-> /tmp/librosa_util.py; localmin)
- https://librosa.org/doc/main/_modules/librosa/sequence.html (-> /tmp/librosa_seq.py; viterbi, transitions)
- https://librosa.org/doc/main/_modules/librosa/core/audio.html (-> /tmp/librosa_audio.py; autocorrelate)
- https://librosa.org/doc/main/api/generated/librosa.{sequence.viterbi,autocorrelate}.html (path discovery)
- https://raw.githubusercontent.com/librosa/librosa/main/LICENSE.md (ISC, 766 B)

## Appendix B — numeric verification of the d(tau) convention [DER]
Random x (N=64, max_period=16); librosa formula `d(k)=2(ACF0-ACFk)-sum_{m<k}x[m]^2` vs
(a) zero-extended lagged window and (b) fully-contained windows (W=N-tau_max):
  max |d_lib - d_zeroext| = 4.26e-14   (identical)
  max |d_lib - d_paper|   = 25.86      (differs by the frame tail energy)
  CMNDF d'(tau)=d(tau)*tau/sum_{j=1..tau}d(j) reproduced exactly (consistency: True).
`autocorrelate` = standard zero-padded-FFT full linear ACF (audio.py:1394–1424): ACF(k)=sum_{j=0..N-1-k} x[j]x[j+k].
