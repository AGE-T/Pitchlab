# PITCHLAB R23 — YIN Variant Taxonomy (Gate 3)

**Purpose:** disambiguate "FFT YIN" and separate genuinely different
algorithms (brief §18). Per-implementation evidence:
`evidence/analysis_round23/g3b1_librosa.md` (librosa yin/pyin),
`g3b2a_aubio.md` (aubio yin/yinfast/yinfft); pYIN paper text and the DAGA
2023 comparison PDF remained UNFETCHABLE this round (attempts documented;
the pYIN mechanics below are OBS from the librosa pyin implementation,
which is pYIN's canonical open implementation).

---

## The taxonomy (equivalence classes)

| # | Variant | Mathematical relation to canonical YIN | Real-time shape | License/reuse |
|---|---|---|---|---|
| 1 | **Direct time-domain YIN** (the paper; our reference A) | canonical | causal windows; O(τ_max·W) per frame | public math |
| 2 | **Exact FFT acceleration** (yinfast; aubio yinfast; our variant D) | **equivalent up to FFT round-off** (our proof: ≤3.7e-9 rel; aubio: identical math re-implemented O(B log B) with prefix-sum energies, FFT size = bufsize exactly) | causal; FFT-sized scratch | public math; aubio code GPL |
| 3 | **Spectrally weighted/tapered** (aubio yinfft) | **NOT equivalent**: periodic-Hann-tapered frame → power spectrum × hard-coded 34-point psychoacoustic weight table → Wiener-Khinchin; global-min-only selection, half-period octave guard, unvoiced gate at d'(τ_min) ≥ tol | causal | GPL-3.0 (distinct algorithm) |
| 4 | **pYIN** (Mauch & Dixon 2014; librosa pyin) | YIN candidates + PROBABILISTIC thresholds (Beta(2,18) grid of 100 in librosa [OBS from source]) + Boltzmann trough prior; HMM over 2×n_pitch states (10-cent bins, ±5 semitone transitions, octave jumps only via the voicing switch [OBS]); whole-sequence Viterbi | NON-causal by default (batch DP); fixed-lag approx. exists but is NOT provided by librosa [OBS] | paper; librosa ISC |
| 5 | **MPM / SWIPE** (bounded baselines) | different families (NSDF; matched spectral template) | causal (MPM); SWIPE heavier | comparison only |

**"FFT YIN" disambiguation [OBS + our proof]:** yinfast (class 2) is an
*acceleration* — bit-equivalent estimates up to round-off (proven here on
deterministic vectors at 3.7e-9); yinfft (class 3) is a *different
estimator* (spectral weighting changes the difference function itself).
They must never share a bucket — the R23 brief's caution is confirmed with
code-level evidence (the weighting table application in pitchyinfft.c is
direct proof [OBS]).

**Default thresholds observed [OBS]:** aubio yin/yinfast 0.15 (live);
yinfft 0.85 (energy-domain); librosa yin 0.1; librosa pyin Beta(2,18) grid
mean 0.1; the R22 engine 0.20; our canonical reference 0.10 (paper's range).

**Frame conventions [OBS]:** librosa yin: shrinking-support frame,
`center=True` default = **zero-padded look-ahead ≈ frame_length/2**
(46 ms @2048/22.05k) — a real-time port must either count that latency or
run `center=False`; aubio: causal slideblock (bufsize trailing window,
hopsize stride, no look-ahead); the R22 engine: causal window ending at the
cadence boundary.

**Candidate-probability concept (pYIN) for PitchLab:** our measured
gross/octave error rates on the synthetic corpus are 0.000 for every
variant — the HMM's headline benefit (error-rate reduction) has no room on
our current corpora; its cost (batch Viterbi or undocumented fixed-lag)
and its voicing-decision integration are real complexity. REJECTED for
production; the outlier/octave machinery of our control layer covers the
practical subset (single-frame flips) at zero latency. Revisit only with
real-audio error statistics (decision record §7).

**Cost models [DER from OBS]:** direct: O(τ_max·W) MACs/frame (measured
2.75M ns @48k W=τ_max; 11–22 ms @192k); yinfast: 3 FFTs of next-pow2(W+
τ_max) + O(τ_max) prefix sums (≈40× faster at 48k than our unoptimized
direct loop; aubio reports similar scaling); yinfft: 2 FFTs + weighting;
pYin: ×100 thresholds per frame + Viterbi O(T·n_states²) (pruned band
±5 semitones).
