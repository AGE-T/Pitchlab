# PITCHLAB R23 — Decimation & Anti-Alias Filter Study (Gate 3)

**Object under study:** the R22 detector's causal decimation chain
(`designAntiAlias`, dsp.cpp L693–710: 3 TDF2 biquads, Butterworth 6th,
fc = 0.35·sr_dec; grid = global stream position). Measured, not assumed
(brief §22). Classes: OBS (source), DER (analysis below), MEAS (this
round's numerics).

---

## 1. The design as shipped [OBS]

* Cascade Q = {0.5176, 0.7071, 1.9319} (the classic 6th-order Butterworth
  biquad split), RBJ low-pass coefficients, TDF2 state — causal, no
  look-ahead, allocation-free, state persistent across arbitrary blocks.
* D = lround(sr/24000) reduced while sr_dec < 18000; fc = 0.35·sr_dec
  (8400 Hz @ sr_dec 24000); decimated Nyquist = 12000 Hz.

## 2. Frequency response & alias rejection [MEAS/DER]

6th-order Butterworth |H(f)| = 1/√(1+(f/fc)^12):

| f (kHz) | f/fc | |H| dB | folding (D=2, sr_dec 24k) |
|---|---|---|---|
| 8.4 | 1.0 | −3.0 | — (passband edge) |
| 10.0 | 1.19 | −8.9 | folds to 14k → out of band |
| 12.0 | 1.43 | **−18.6** | Nyquist fold point |
| 14.0 | 1.67 | −28.9 | folds to 10k |
| 16.0 | 1.90 | −39.1 | folds to 8k |
| 20.0 | 2.38 | −59.6 | folds to 4k |

* [DER] Attenuation AT the decimated Nyquist is **−18.6 dB** — content in
  the 12–16 kHz band folds into 8–12 kHz attenuated only ~19–39 dB. For
  F0 tracking of music (f0 ≤ 1200 Hz, harmonics relevant to YIN's
  difference function well below 8 kHz after the engine's own window) this
  is adequate-but-not-generous: the aliased residue acts as broadband
  noise floor ~2–4 bits down, degrading d(τ)'s dip depth on quiet HF-rich
  material. [MEAS proxy] the Gate-5 corpus (harmonics ≤ 10.6 kHz + noise)
  showed no measurable accuracy loss vs FullRate (0.02 c med both) — the
  practical impact at our SNRs is below measurement resolution.
* [DER] Alternatives if a future corpus shows sensitivity: (a) fc →
  0.25·sr_dec (Nyquist attenuation −34 dB, passband shrinks to 6 kHz —
  safe for F0 ≤ 1200 with 4+ harmonics); (b) 8th order (+12 dB at
  Nyquist, +33% CPU on the chain — ~negligible vs the whole detector);
  (c) linear-phase FIR: zero phase distortion but group delay
  ((N−1)/2)/sr added to the analysis timestamp AND a longer startup
  transient — REJECTED for the causal path (the brief's zero-phase
  prohibition is about offline filtfilt; a causal linear-phase FIR is
  legal but pays latency for a benefit YIN's difference function does not
  need — d(τ) is phase-insensitive to CONSTANT delay [DER]); (d)
  minimum-phase IIR: less group delay at DC, same magnitude response —
  complexity without measured need.

## 3. Startup transient & block behaviour [OBS/MEAS]

* The biquad state starts zeroed; the effective settle time ≈ a few
  hundred samples at fc 8.4 kHz — inside the first window (42.7 ms), so
  the first estimates after prepare() see a partially-charged filter. The
  engine's lock_confirm/history machinery (and the control layer's
  3-frame acquisition) absorbs this by design. The alternating-block
  battery (32/1024 + zero-length) shows no state discontinuity [MEAS].
* The MEASUREMENT path's factor-8 decimation was separately repaired this
  round (127-tap linear-phase FIR — Gate 1) — measurement-only, the
  production chain untouched.

## 4. Conclusions

1. The shipped design is a legitimate causal anti-alias chain; its Nyquist
   attenuation (−18.6 dB) is the honest number, now documented.
2. No measured accuracy deficit on the R23 corpus (engine Decimated ≡
   FullRate to 0.02 c median / 1.5 c p95) — no change warranted by
   evidence (the brief's "do not assume adequate / do not assume
   inadequate" satisfied in both directions).
3. If real-audio (PTDB-TUG) evaluation later shows HF-content degradation,
   the fc→0.25·sr_dec option is the first candidate (documented
   trade-off above).
