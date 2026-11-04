# OCTAVOX_ROUND6_MEASUREMENTS

**Round-6 measurements of OUR three pitch-shift designs — not measurements of the target**

The target `F:\VST\OV_DIS\Ox.vst3` was never executed. Everything below was produced by
`poc/octavox_offline.py` on deterministic signals generated in code. These numbers describe our
implementations only and must never be quoted as target behaviour.

---

## 1. Environment

| item | value |
|---|---|
| OS | Windows 10.0.26200 (Windows 11 kernel), x86-64 |
| CPU | Intel Family 6 Model 158 (Skylake-class) |
| Python | CPython 3.11.9 (MSC v.1938, 64-bit) |
| NumPy | 2.4.4 |
| sample rate | 48 000 Hz (44 100 Hz for one CLI demonstration) |
| block size | 64 samples for the delay line; the vocoders process the whole array in one pass |
| date | 2026-10-20 |

Cost figures are wall-clock on this machine, single-threaded. They are indicative, not a performance
guarantee, and they are **not** real-time claims.

---

## 2. Method

### 2.1 Signals (identical for all three designs)

| id | signal | use |
|---|---|---|
| S1 | 220 Hz sine, 1.2 s, amplitude 0.5 | pitch accuracy, out-of-harmonic energy |
| S2 | 0.25 s silence + 0.50 s 440 Hz sine (hard onset) + 0.25 s silence | transient metrics |
| S3 | single unit impulse at sample 1000, 0.5 s | latency, pre-ring |
| S4 | seeded white noise, 1.0 s | cost at 8 voices |
| S5 | 5 plucked notes (harmonics + click), 2.0 s | example WAV material |

Ratios tested: **r = 1.25** (+3.86 st) and **r = 0.80** (−3.86 st). One voice for the metric runs,
8 voices for the cost column.

### 2.2 Metric definitions

| metric | definition |
|---|---|
| **pitch cents (YIN)** | YIN on the steady tail (last 30 % of S1), error in cents vs `220·r`. |
| **pitch cents (FFT)** | parabolic-interpolated spectral peak on the same tail. |
| **latency ms** | delay of the **main** response to the impulse (argmax of `|y|` minus the impulse index). Each response is essentially a single sample, so this is unambiguous. |
| **first response ms** | first sample of the impulse response above −60 dB re its peak. For the delay line this is interpolation leakage ahead of the main tap, not the audible onset. |
| **onset delay ms** | in S2, when the 1 ms RMS envelope first reaches 1 % of its peak (relative to the input onset). An STFT window that straddles the onset starts earlier than its main response; that difference is its pre-echo length. |
| **attack ms** | 10 %→90 % rise of the 1 ms RMS envelope, measured against the **local** steady level (median envelope 100–300 ms after onset), so slow level drift cannot inflate it. |
| **envelope modulation dB** | 95th/5th percentile spread of the 1 ms RMS envelope over the sustained part (100–450 ms after onset). Exposes comb/crossfade amplitude modulation. |
| **out-of-harmonic** | fraction of spectral energy outside ±12 Hz of the harmonics of the measured f0 (S1 tail). |
| **cost 8 voices** | wall-clock ms to process 1.0 s of S4 with 8 voices. |

The attack metric differs from the round-5 harness (which measured against the global peak): the
round-5 "63 samples ≈ 1.3 ms" figure is not comparable to the table below and is not superseded by
it — both are correct for their own definition. Causality was checked separately: all three designs
produce ≤ −230 dB before the input event.

---

## 3. Results — r = 1.25 (+3.86 semitones)

| design | pitch ¢ (YIN) | pitch ¢ (FFT) | latency ms | first resp. ms | onset delay ms | attack ms | modulation dB | out-of-harm. | cost 8v ms/s |
|---|---|---|---|---|---|---|---|---|---|
| delay line | **+3.58** | +4.80 | 102.48 | 34.17 | 56.21 | 130.42 | **15.68** | 0.0000023 | **6504** |
| phase vocoder | **+0.03** | +0.00 | 34.23 | 34.23 | 14.44 | 8.17 | 1.06 | 0.000127 | 181 |
| transient-aware PV | **+0.02** | +0.00 | 36.38 | 24.23 | 20.44 | **5.52** | **0.79** | 0.000108 | 191 |

## 4. Results — r = 0.80 (−3.86 semitones)

| design | pitch ¢ (YIN) | pitch ¢ (FFT) | latency ms | first resp. ms | onset delay ms | attack ms | modulation dB | out-of-harm. | cost 8v ms/s |
|---|---|---|---|---|---|---|---|---|---|
| delay line | **−4.59** | −4.27 | 65.19 | 65.12 | 15.65 | 98.29 | **16.84** | 0.0000023 | **6566** |
| phase vocoder | **+0.02** | −0.00 | 53.21 | 53.19 | 26.98 | 26.00 | 3.26 | 0.000019 | 174 |
| transient-aware PV | **+0.01** | −0.00 | 50.56 | 25.12 | 28.90 | **8.46** | 3.25 | 0.0000025 | 189 |

Raw data: `poc/offline_measurements.json`. Formatted table: `poc/OFFLINE_COMPARISON.md`.

---

## 5. Reading the numbers

**Pitch accuracy.** The delay line is 3.6–4.6 cents off. This is not a tuning error: the two-tap
crossfade rotates the carrier phase over each grain, and the resulting frequency offset scales as
`(1−r)/G` (derived and measured in `poc/POC_RESULTS.md`; the model and the measurement agree in sign
and magnitude across the whole grain sweep). At `G = 4096` and `r = 1.25` the predicted offset is a
few cents — exactly what is measured. The vocoders resample, so their error is at the measurement
floor (0.01–0.03 cents).

**Latency.** The delay line is **not** a constant-latency design: its main response is 48.0 ms at
unison (round-5 harness), 65.2 ms at `r = 0.80` and 102.5 ms at `r = 1.25`. The read position
sweeps, so the effective tap distance depends on the shift. Its "first response" at `r = 1.25`
(34.2 ms) is interpolation leakage ahead of the main tap at −60 dB, not the onset. The plain
vocoder reports 34.2 ms at `r = 1.25` and 53.2 ms at `r = 0.80` — `N/2 = 1024` samples (21.3 ms)
plus the stretch/resample alignment, which also moves with the ratio. The transient-aware vocoder's
main response is comparable (36.4 / 50.6 ms) but it **starts** earlier (first response 24.2 / 25.1
ms) because the short window at the onset does not need a full `N/2` of context.

**Onset and pre-echo.** In the burst test the plain vocoder's envelope starts 14.4 ms after the
input event — 20 ms before its own main response, because the analysis window straddles the onset
and the stretch smears the attack backwards. The transient-aware version starts at 20.4 ms but
reaches 90 % of level in 5.5 ms, so the perceptual attack is 3–5× sharper. The delay line starts at
56.2 ms and then takes 130 ms to reach 90 %, because the two-tap crossfade modulates the envelope
while it rises. All three designs are causal (≤ −230 dB before the event).

**Modulation.** The delay line modulates the sustained envelope by 15.7–16.8 dB (95th/5th
percentile). That is the audible signature of the two-tap crossfade at a fixed rate: the mix of two
copies `G` apart sweeps a comb through the spectrum. The vocoders are at 0.8–3.3 dB. This is the
single largest quality difference between the designs and it is not a subtle one.

**Attack.** 5.5 ms (transient PV) < 8.2 ms (PV) < 98–130 ms (delay line). The delay-line number is
dominated by the comb modulation, which delays the moment the envelope first reaches 90 % of its
local level.

**Cost.** The Python delay line costs ~6.5 s of CPU per audio second at 8 voices (≈0.15× realtime);
the vocoders cost ~0.18 s (≈5.5× realtime at 8 voices). This is a property of the implementation
language, not of the algorithm family — a C++ delay line would be far cheaper — but it is the
measured fact on this environment and it is reported as such.

**Out-of-harmonic energy.** All three are ≤ 0.013 % on the steady tone. The plain vocoder is slightly
worse at `r = 1.25` (0.0127 %) than the others, consistent with its phase-propagation residual.

---

## 6. Decision for the next direction

**Selected: the transient-aware phase vocoder.**

It wins on every measured axis: pitch accuracy (0.01–0.02 ¢ vs 3.6–4.6 ¢), latency (36–51 ms vs
65–102 ms), attack (5.5–8.5 ms vs 98–130 ms), envelope modulation (0.8–3.3 dB vs 15.7–16.8 dB) and
out-of-harmonic energy, while costing ~30× less CPU than the Python delay line. The one axis where
the plain vocoder is marginally ahead is latency at `r = 1.25` (34.2 vs 36.4 ms), which is inside the
measurement resolution of one FFT hop.

The delay-line's 15.7–16.8 dB envelope modulation and 98–130 ms effective attack are disqualifying
for a production shifter, and its 3.6–4.6 cent error is two orders of magnitude above the vocoder's.

The delay line is **not discarded**: it is the faithful reconstruction of the target's architecture
and stays as the reference design, the regression baseline for the synthetic harness, and the object
of the Q-14 question (does the target exhibit the unity-ratio comb?). The two designs answer different
questions and are kept side by side.

A production hybrid would go further than `pv_transient`: phase-locked vocoder in the steady state,
short-window/phase-reset at onsets (already implemented), and a time-domain path for very small
ratios where the vocoder's latency is unnecessary. That is a design proposal, not a measured result.

---

## 7. Example WAV runs (CLI)

All at 48 kHz unless stated; 2.0 s input; 3 voices; spread on; `+4` semitones (ratio 1.259921).

| output | design | cpu s | × realtime | peak |
|---|---|---|---|---|
| `example_output_delayline.wav` | delay line, 3 voices | 7.868 | 0.25 | 0.570 |
| `example_output_pv.wav` | phase vocoder, 3 voices | 0.150 | 13.32 | 0.582 |
| `example_output_pvt.wav` | transient-aware PV, 3 voices | 0.157 | 12.73 | 0.558 |
| `example_output_pvt_44k.wav` | transient-aware PV, 1 voice, −5 st, **44.1 kHz** | 0.048 | 41.87 | 0.736 |

The 44.1 kHz run reads a 48 kHz file, processes at 44.1 kHz and writes 88 200 samples at 44 100 Hz
(2.00 s). Sample-rate handling is explicit; see `OCTAVOX_DSP_DEMO_USAGE.md` §4.1.

Input/output levels: the input peaks at 0.900 (normalised by the generator). The multi-voice outputs
are ≈ 0.56–0.58 because three voices are averaged; the single-voice 44.1 kHz output peaks at 0.736.

---

## 8. Limitations

* **Python.** Every cost number is dominated by the interpreter. Do not extrapolate to C++.
* **Offline only.** The CLI processes whole arrays; there is no streaming or host integration.
* **One stimulus family.** Sines, a hard-onset burst, an impulse and a short plucked phrase. Real
  polyphonic material (chords, vibrato, noise-like attacks) would exercise the tracker differently.
  The synthetic harness (`octavox_poc.py`) is the broader regression suite and is unchanged.
* **No perceptual listening test.** All conclusions are objective metrics. A listening panel could
  weigh the artefacts differently (e.g. prefer the delay line's character).
* **No target comparison.** The target was never run, so none of these numbers can be used to claim
  similarity to, or superiority over, the target.

---

## 9. Reproduce

```powershell
cd f:\VST\OV_DIS\Evdence\poc
python octavox_offline.py --compare
```

Expected: `offline_measurements.json` (4 583 bytes) and `OFFLINE_COMPARISON.md` re-written, the table
printed to stdout. Deterministic audio; cost varies with load.

---

## 10. SHA-256 of the round-6 PoC artefacts

```
696204f0b15207e0ef87800a1d992b2b658f9bbf96384a3923de2262f228a891  octavox_poc.py          (round 5, unchanged)
756fe02a88d074520111b49f02e3d4a362d41fde253c5f5972ff612bbeee68e3  octavox_offline.py
30596b4cca443557b3d98e3ff27a1665c398401c8c9b5ab733c9c3a056d02e47  example_input.wav
8614c94ee701754b556ae752b9dbd1b563cad09572fbffd4d9b1fd936d563839  example_output_delayline.wav
d8c07a2f019253dcb6baca22ef732b40f69c4b161318d8d6095f47d9f7dc11be  example_output_pv.wav
34db838c97b298c2ab29904aae22994b4f587fe89247d916e05a7b77b0e1c2bf  example_output_pvt.wav
88f0f2ac4c28f2e403a3a783c0223da000d72bb50057ac21b6b993afda909ca8  example_output_pvt_44k.wav
8e8f9be5ffdd937d464bf8f4cc75ed21643fc857a9f0833bdecaa5dc481e7408  offline_measurements.json
091cda636a3bc019069b1543cd7fccb679945ded24f27385cd41af5faef23040  OFFLINE_COMPARISON.md
```

`poc_measurements.json` (round-5 harness) is unchanged: `5e5ecf01e84cfed1b540331626fc3850141faec6f8ead0a3ac726339316950b1`.
