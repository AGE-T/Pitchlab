# G3-b2a — aubio YIN variants (research only, GPL-3.0: behavior described, no code copied)

Task: document aubio `yin`, `yinfast`, `yinfft` behavior with code-level evidence.
Method: `curl -sL` of aubio.org 0.4.7 doxygen page + raw GitHub sources (master; verified
byte-identical on branch `0.4.9` for all five pitch files via diff). Claims tagged
[OBS] (directly observed in fetched text), [DER] (derived by reasoning), [UNK] (unverified).
aubio is GPL-3.0+ — this file contains behavioral descriptions, re-derived formulas,
identifiers and constants only; no aubio code reproduced.

## 1. `yin` (classic) — src/pitch/pitchyin.c (189 L) + pitchyin.h (100 L)

**Difference function — pure time-domain double loop.** [OBS]
- `aubio_pitchyin_do`, for lags tau = 1 .. W-1 (W = bufsize/2, the `yin` fvec length):
  d(tau) = SUM_{j=0..W-1} (x[j] - x[j+tau])^2, accumulated sample-by-sample with SQR — no FFT
  in this file. Max input index touched: 2W-2 < bufsize.
- Window FIXED at W terms for every lag; the YIN paper (de Cheveigne & Kawahara 2002) sums
  j = 0..W-tau-1 (shrinking). aubio's classic yin is a fixed-window variant of paper-YIN.
  [OBS loop bounds] [DER deviation]

**CMNDF — starts at lag 1, no warm-up.** [OBS]
- yin[0] := 1 by definition; running sum accumulates d(1)..d(tau) inline;
  d'(tau) = d(tau)*tau / SUM_{k=1..tau} d(k) (paper's cumulative-mean form, sum from k=1).
  Guard: if the running sum is exactly 0, d'(tau) := 1 (div-by-zero on silence).
- No extra warm-up/trim lags beyond the selection rule's own 3-lag offset (below).

**Default tolerance: 0.15 live; 0.1 only in dead legacy code; 0.6 nowhere.** [OBS]
- Constructor tol = 0.15; pitch.c dispatcher calls set_tolerance(0.15); pitchyin.h docstring
  "[default 0.15]"; 0.4.7 pitch_8h.html: "default is 0.15 for yin".
- A compiled-out `#if 0` block (`aubio_pitchyin_getpitch`) hardcodes 0.1 — the YIN paper's
  default; historical evidence only. 0.6 appears in no fetched aubio pitch source
  (it is a librosa-side constant). [OBS]

**Selection rule — thresholded early-exit, then global-min fallback.** [OBS]
- Inside the same lag loop: once tau > 4, test lag p = tau - 3; accept p if
  d'(p) < tol AND d'(p) < d'(p+1) → peak_pos = p, interpolate, RETURN immediately (higher
  lags never scanned). First eligible p is 2 (at tau = 5).
- Characterization: "first lag under tolerance that is smaller than its RIGHT neighbor" —
  approximates the paper's first-minimum-below-threshold, but NOT a strict local-min test
  (left neighbor unchecked), and acceptance trails the scan by 3 lags. [DER]
- Disabled legacy block instead did: first d' < 0.1, then walk downhill while the next value
  keeps decreasing — closer to the paper's literal rule. [OBS]
- Fallback (no lag qualified): global minimum of the whole CMNDF via `fvec_min_elem`,
  accepted regardless of threshold — classic yin ALWAYS emits an estimate. [OBS]

**Parabolic interpolation target: the CMNDF buffer.** [OBS]
- `fvec_quadratic_peak_pos` (src/mathutils.c), 3-point vertex on CMNDF neighbors:
  p_hat = p + 0.5*(s[p-1] - s[p+1]) / (s[p-1] - 2*s[p] + s[p+1]); boundary lags return the
  raw integer index. `aubio_pitchyin_do` outputs the interpolated PERIOD in samples;
  pitch.c converts f0 = samplerate/period. [OBS formula] [DER]

**Confidence / range / silence.**
- Confidence = 1 - d'(peak_pos) (0.4.6+ semantics: tied to the selected period). [OBS]
- NO explicit min/max frequency config (no fmin/fmax in pitchyin.c/h or pitch.c); range
  implicit: max period W-1 → f_min ~ 2*samplerate/bufsize; min accepted period 2 in the
  thresholded path → theoretical f_max = samplerate/2. Silence detection is NOT in
  pitchyin.c — handled by the dispatcher (section 4). [OBS absence] [DER]

## 2. `yinfast` — src/pitch/pitchyinfast.c (201 L) + pitchyinfast.h (102 L)

**Documented as equivalent to yin.** [OBS] 0.4.7 pitch_8h.html: "This algorithm is
equivalent to the YIN algorithm, but computed in the spectral domain for efficiency. See
also python/demos/demo_yin_compare.py." Header: "compute the autocorrelation function
using time domain convolution computed in the spectral domain." ChangeLog: introduced in
0.4.6 (2017-10-02, "new fast method to compute YIN algorithm"); listed in 0.4.7+ docs. [OBS]

**How the FFT accelerates d(tau) = r(0)+r_{t+tau}(0) - 2*r(tau).** [OBS]
- FFT size = bufsize EXACTLY (one `aubio_fft` created with bufsize) — NO next-pow2 rounding
  step in the pitch code. Backend constraints (src/spectral/fft.c validation): fftw3 any
  size >= 2; vDSP needs f*2^n (n>4, f in {1,3,5,15}); Intel IPP and default ooura radix-2
  effectively require power-of-two — users pass pow2 bufsize, but the algorithm never
  rounds up. [OBS] [DER ooura]
- Energy terms: elementwise squares of input; sqdiff[0] = full sum over squares[0..W-1]
  (fvec_sum); each next lag is a SLIDING update
  sqdiff[tau] = sqdiff[tau-1] - squares[tau-1] + squares[W+tau-1] (O(1) per lag; a disabled
  `#if 0` block shows the naive re-sum-per-lag prefix version). Then sqdiff[0] is added to
  every element → per-lag "energy of first window + energy of window starting at tau". [OBS]
- Cross term: kernel = FIRST W samples of input placed at offset 1 in a zero-initialized
  (calloc) bufsize buffer, then reversed; two forward FFTs (input, kernel); manual complex
  product over aubio's split real/imag storage (re' = a*c - b*d, im' = b*c + a*d per bin);
  one inverse FFT; SUM_{j=0..W-1} x[j]*x[j+tau] is read at index tau+W; finally
  d(tau) = energy-sum(tau) - 2*r(tau). [OBS structure]
- Index algebra check: convolving x with the reversed first-half kernel and evaluating at
  tau+W yields exactly the fixed-window cross term; circular-wrap contributions are empty
  for tau <= W-1 → no aliasing in the used range. [DER]

**What changes vs classic yin: nothing behavioral.** [OBS]
- Identical d(tau) target (same fixed W = bufsize/2 window), identical CMNDF shape,
  identical selection rule (p = tau-3, tau > 4, d'(p) < tol AND d'(p) < d'(p+1), early
  return), identical global-min fallback, identical quadratic interpolation on the CMNDF,
  identical confidence 1 - d'(peak_pos). Default tolerance 0.15 (constructor + pitch.c +
  header docstring). [OBS]
- Cost: 2 forward + 1 inverse FFT of size bufsize + O(W) sliding sums vs O(W^2) classic;
  numerical results equal classic yin up to floating-point round-off of FFT arithmetic.
  No window, no taper, no spectral weighting anywhere in yinfast. [DER] [OBS absence]

## 3. `yinfft` — src/pitch/pitchyinfft.c (208 L) + pitchyinfft.h (99 L)

**Doc positioning.** [OBS] pitch_8h.html + pitchyinfft.h: "derived from the YIN algorithm...
a Fourier transform is used to compute a tapered square difference function, which allows
spectral weighting. Because the difference function is tapered, the selection of the period
is simplified." Source: Brossier PhD thesis ch. 3 (QMUL, 2006). `default` method = yinfft
(pitch.c enum; doxygen page). Constructor needs samplerate (weight table maps Hz→bins);
yin/yinfast need only bufsize. [OBS]

**Pipeline (aubio_pitchyinfft_do).** [OBS]
1. Multiply input by window "hanningz" — periodic Hann w[i] = 0.5*(1 - cos(2*pi*i/N)),
   N = bufsize (window table in mathutils.c) → the TAPER.
2. Real FFT of the windowed signal; power spectrum per bin (conjugate bins combined).
3. Multiply the power spectrum bin-wise by a psychoacoustic WEIGHT; mirror symmetrically.
4. Second FFT of the weighted power spectrum → weighted autocorrelation R(tau)
   (Wiener–Khinchin route).
5. d(tau) = S - R(tau), S = 2*SUM_{l=0..N/2} weighted power. Nuance: DC and Nyquist bins
   effectively counted twice vs the exact full-band sum (S = full-band + sqrmag[0] +
   sqrmag[N/2]) — minor bias, negligible for windowed audio. [DER]
6. CMNDF identical formula (d'(0)=1, running sum from lag 1, zero-sum guard), lags 1..N/2
   (yin buffer length bufsize/2 + 1 — one MORE lag than yin/yinfast). [OBS]

**Weighting code = proof yinfft is NOT mathematically equivalent to time-domain YIN.** [OBS]
- Two hard-coded 34-entry tables: `freqs[]` = knot frequencies 0..25100 Hz (third-octave-like
  spacing); `weight[]` = dB values from -75.8 dB (0 Hz) rising via -20.9 dB @100 Hz and ~0 dB
  @1 kHz to a peak +8.1 dB @4 kHz, then falling to -17.8 dB above ~15 kHz — a
  loudness/transfer-function-like envelope applied to the POWER SPECTRUM before the 2nd FFT.
- Interpolation piecewise-linear between knots (special first-segment case from 0 Hz; guard
  for coincident knots), then dB→linear via DB2LIN(g) = 10^(g/20) (aubio_priv.h; a
  commented-out variant applied an extra square root — not live). [OBS]
- Derived closed form (up to a positive scale, which the scale-invariant CMNDF removes):
  d_yinfft(tau) ∝ SUM_l W[l]*|FFT(w·x)[l]|^2*(1 - cos(2*pi*l*tau/N))
              = 2*SUM_l W[l]*|FFT(w·x)[l]|^2*sin^2(pi*l*tau/N). [DER]
- Two independent deviations from time-domain YIN: (a) Hann taper w on the frame;
  (b) spectral weight W on the power spectrum. Neither exists in yin/yinfast. Verdict:
  yinfft is a DIFFERENT estimator (windowed + perceptually weighted square difference),
  not a fast rewrite of the same math. [DER]
- Demo script (python/demos/demo_yin_compare.py) labels them distinctly: yinfast = "sum of
  squared diff using complex domain"; yinfft = "tappered/modified squared diff". [OBS]

**Tolerance default 0.85 — and it gates voicing.** [OBS]
- Constructor tol = 0.85; pitch.c dispatcher set_tolerance(0.85); 0.4.7 pitch_8h.html:
  "default is 0.15 for yin and 0.85 for yinfft". NOTE: pitchyinfft.h docstrings still say
  "[default 0.15]" — stale header doc, contradicted by code + main doc. [OBS inconsistency]
- Selection (unlike yin/yinfast): GLOBAL minimum of the CMNDF (`fvec_min_elem`); IF
  d'(tau_min) < tol: octave guard for high frequencies — when tau <= short_period =
  round(samplerate/1300) (f0 above ~1300 Hz), compare half period round(tau/2); if
  d'(half) < tol prefer it, else keep tau; then 3-point quadratic interpolation
  (`fvec_quadratic_peak_pos` on the CMNDF). In-code comment: guards "(unlikely) octave
  doubling in higher frequencies"; a commented-out option returns the raw integer period.
- ELSE (d'(tau_min) >= 0.85): output 0.0, peak_pos = 0 — built-in unvoiced/no-pitch gate;
  yin/yinfast always emit a fallback estimate. Confidence = 1 - d'(peak_pos) → 0 when
  unvoiced (d'[0]=1). No energy term inside yinfft; silence still enforced by dispatcher. [OBS]

## 4. Dispatcher, streaming conventions, silence — src/pitch/pitch.c (530 L)

- `new_aubio_pitch(method, buf_size, hop_size, samplerate)`: mcomb/yinfast/yinfft/yin/
  schmitt/fcomb/specacf/default; default = yinfft. Validation: hopsize >= 1,
  bufsize >= hopsize, samplerate >= 1. Tolerances wired here: yin 0.15, yinfast 0.15,
  yinfft 0.85, specacf 0.85. [OBS]
- Streaming: `aubio_pitch_slideblock` keeps a bufsize buffer; each `aubio_pitch_do` shifts
  out hopsize samples and appends the new hop → analysis window = the LAST bufsize samples
  ending at the current hop. CAUSAL, no lookahead beyond the analysis window; stride =
  hopsize; output timestamp = the just-ingested hop. Same path for yin/yinfft/yinfast/
  schmitt/fcomb/specacf. [OBS] [DER]
- Silence: `DEFAULT_PITCH_SILENCE` = -50 dB; settable within [-200, 0] dB. Level metric:
  aubio_db_spl = 10*log10(mean(x^2)) over the HOP block (ibuf), not the analysis window;
  below threshold → output forced to 0 BEFORE unit conversion (Hz default; midi/cent/bin
  available — "cent" reuses the midi converter, marked not-implemented in a comment). [OBS]
- No min/max frequency parameters in the dispatcher either — limits purely implicit
  (bufsize/2 max lag). [OBS absence]

## 5. License evidence (GPL-3.0)

- COPYING at repo root (674 lines fetched): "GNU GENERAL PUBLIC LICENSE Version 3,
  29 June 2007" — full GPLv3 text. [OBS]
- Every fetched source file (pitchyin.c/.h, pitchyinfast.c/.h, pitchyinfft.c/.h, pitch.c,
  mathutils.c, fft.c, aubio_priv.h, fvec.c) carries the GPLv3-or-later header ("either
  version 3 of the License, or (at your option) any later version"). [OBS]
- README.md License section: GPLv3+ terms. Compliance: description-only research above. [OBS]

## 6. Version cross-checks

- master vs branch `0.4.9`: pitchyin.c, pitchyinfast.c, pitchyinfft.c, pitch.c,
  mathutils.c byte-identical (diff clean) → findings hold for 0.4.9 and master. [OBS]
- [UNK] fvec_min_elem tie-breaking: plain-C loop takes the last minimum on all-ties; the
  macOS vDSP variant's tie behavior is unspecified — edge case only (constant input).

## Appendix — fetched URLs (all HTTP 200 via `curl -sL`; master, with 0.4.9 diff checks)

1. https://aubio.org/doc/0.4.7/pitch_8h.html — methods list, tolerance defaults (0.15/0.85), default=yinfft, yinfast "equivalent" claim, yinfft "tapered ... spectral weighting" claim.
2. https://raw.githubusercontent.com/aubio/aubio/master/src/pitch/pitchyin.c and pitchyin.h — classic yin: difference loop, CMNDF, tol 0.15, selection, confidence.
3. https://raw.githubusercontent.com/aubio/aubio/master/src/pitch/pitchyinfast.c and pitchyinfast.h — FFT-accelerated yin.
4. https://raw.githubusercontent.com/aubio/aubio/master/src/pitch/pitchyinfft.c and pitchyinfft.h — tapered/weighted yinfft, weight tables, tol 0.85.
5. https://raw.githubusercontent.com/aubio/aubio/master/src/pitch/pitch.c — dispatcher: slideblock, silence -50 dB, tolerance wiring, default=yinfft.
6. https://raw.githubusercontent.com/aubio/aubio/master/src/mathutils.c — fvec_quadratic_peak_pos, fvec_min_elem, aubio_silence_detection/db_spl/level_lin, window table (hanningz).
7. https://raw.githubusercontent.com/aubio/aubio/master/src/spectral/fft.c — FFT size validation per backend (no pow2 rounding in API).
8. https://raw.githubusercontent.com/aubio/aubio/master/src/aubio_priv.h — DB2LIN/SQR macros, calloc-backed allocations.
9. https://raw.githubusercontent.com/aubio/aubio/master/src/fvec.c — zero-initialized fvec allocation.
10. https://raw.githubusercontent.com/aubio/aubio/master/COPYING — GPLv3 full text; README.md (same tree) — license section.
11. https://raw.githubusercontent.com/aubio/aubio/master/ChangeLog — yinfast introduced in 0.4.6.
12. https://raw.githubusercontent.com/aubio/aubio/master/python/demos/demo_yin_compare.py — yinfast vs "tappered/modified" yinfft naming.
13. https://raw.githubusercontent.com/aubio/aubio/0.4.9/src/pitch/{pitchyin,pitchyinfast,pitchyinfft,pitch}.c and .../0.4.9/src/mathutils.c — diffed identical to master.
