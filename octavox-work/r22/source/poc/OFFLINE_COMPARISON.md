# Offline comparison -- three independent pitch-shift designs (ours)

All rows use **identical deterministic inputs**, the same sample rate, and the same
metric code (`octavox_offline.py`).  These are measurements of *our* implementations,
never of the target plug-in.

sr = 48000 Hz | delay-line interpolation = `lagrange6` | vocoder n_fft = 2048

## ratio_1.25

| algorithm | pitch cents (YIN) | pitch cents (FFT) | latency ms | first response ms | onset delay ms | attack ms | envelope modulation dB | out-of-harmonic | cost 8v ms/s |
|---|---|---|---|---|---|---|---|---|---|
| delayline | +3.58 | +4.80 | 102.48 | 34.17 | 56.21 | 130.42 | 15.7 | 0.0000 | 6504 |
| pv | +0.03 | +0.00 | 34.23 | 34.23 | 14.44 | 8.17 | 1.1 | 0.0001 | 181 |
| pv_transient | +0.02 | +0.00 | 36.38 | 24.23 | 20.44 | 5.52 | 0.8 | 0.0001 | 191 |

## ratio_0.80

| algorithm | pitch cents (YIN) | pitch cents (FFT) | latency ms | first response ms | onset delay ms | attack ms | envelope modulation dB | out-of-harmonic | cost 8v ms/s |
|---|---|---|---|---|---|---|---|---|---|
| delayline | -4.59 | -4.27 | 65.19 | 65.12 | 15.65 | 98.29 | 16.8 | 0.0000 | 6566 |
| pv | +0.02 | -0.00 | 53.21 | 53.19 | 26.98 | 26.00 | 3.3 | 0.0000 | 174 |
| pv_transient | +0.01 | -0.00 | 50.56 | 25.12 | 28.90 | 8.46 | 3.3 | 0.0000 | 189 |

## Reading the table

* **pitch cents** -- YIN on the steady tail; the delay line shows a few cents because the
  crossfaded two-tap read phase-modulates the carrier (analytically predicted in
  `poc/POC_RESULTS.md`).  The vocoder resamples, so its error is near zero.
* **latency ms** -- delay of the *main* response to an impulse (argmax |y|).  All three
  responses are essentially a single sample at these ratios, so this is unambiguous.  The
  delay line's latency depends on the shift: 48.0 ms at unison (round-5 harness), 65.2 ms at
  r = 0.80 and 102.5 ms at r = 1.25 -- the read position sweeps, so the tap distance is not
  constant.  The vocoders move with the resampling stage (34.2 ms / 53.2 ms).
* **first response ms** -- first sample above -60 dB.  For the delay line this is
  interpolation leakage before the main tap (34.2 ms at r = 1.25 while the main tap is at
  102.5 ms), not the audible onset.
* **onset delay ms** -- in the burst test, when the 1 ms RMS envelope first reaches 1 % of its
  peak.  An STFT design whose analysis window straddles the onset starts earlier than its main
  response; that difference is its pre-echo length.
* **attack ms** -- 10 %->90 % rise of the 1 ms RMS envelope relative to the *local*
  steady level (100-300 ms after onset), so slow level drift does not inflate it.
* **envelope modulation dB** -- 5th/95th percentile spread of the sustained envelope.
  The delay line's two-tap crossfade modulates the amplitude at rate |1-ratio|/G;
  the vocoder does not.
* **causality** -- all three produce <= -230 dB before the input event (checked, not
  tabulated).
* **cost** -- wall clock on the recorded environment; Python/NumPy, not real time.

The comparison is the basis for the next design step: the delay line wins on transients
and cost, the vocoder wins on steady-state pitch accuracy; a production design should
hybridise (transient bypass + phase-locked steady state) rather than pick one.
