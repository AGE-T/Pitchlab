# OCTAVOX PoC — independent DSP demonstration (mandate §9)

**This folder is OURS.** It contains no bytes from the target and no Eventide code. It is an
independent implementation of the *architecture* reconstructed in `..\OCTAVOX_*.md`, plus a
measurement harness. Every number it produces describes **this implementation**, never the target.

## Files

| file | role |
|---|---|
| `octavox_poc.py` | the DSP (multi-voice variable-rate delay pitch shifter + YIN-style detector) and the measurement harness |
| `poc_measurements.json` | machine-readable results (generated) |
| `POC_RESULTS.md` | human-readable result table (generated) |
| `poc_run.log` | stdout of the last full run |

## Reproduce

```pwsh
cd f:\VST\OV_DIS\Evdence\poc
python octavox_poc.py
```

Requirements: Python 3.11+, `numpy` (no other dependency). Fixed seeds and a fixed sample rate
(48 kHz) make the run deterministic apart from the wall-clock cost figures.

## Signal path implemented

```
input ─► ring delay line (power-of-two, age-indexed)
      ─► per voice: two fractional taps at d and d+G, 6-point Lagrange interpolation,
                    complementary raised-cosine crossfade, geometric ratio glide,
                    linear gain ramp
      ─► voice sum (mean of the active voices) ─► feedback (loop gain = the `feedback` parameter)
      ─► mix
```

The two taps are `G` samples apart and the sweep wraps every `G/|1−r|` samples, exactly the
structure recovered from `dtonicii9` (D1 §6, D3 §8).

## What is measured

1. **Pitch accuracy** — per voice, nominal vs *applied* interval (diatonic mode snaps to the scale),
   expected vs measured fundamental in cents. Reported three ways because the reconstruction's
   crossfade genuinely produces two different numbers:
   * `measured Hz (YIN)` — cumulative-mean-normalised difference function = grain-average period;
   * `fft Hz` — parabolic-interpolated spectral peak = instantaneous carrier;
   * `predicted Hz` — the closed form derived in `_predicted_shift_hz` (below).
2. **Unity-ratio behaviour** — at 0 semitones the crossfade weight is frozen at 0.5, so *both* taps
   (`G` apart) are read. The unison voice is therefore a two-tap **comb**, not a copy; the measured
   gain is compared with the analytic `|cos(π f G / sr)|` (notch spacing `sr/G`).
3. **Latency** — impulse at sample 1000, first output above −80 dBFS; samples and ms.
4. **Transient behaviour** — amplitude-step burst. Attack is reported both absolutely and *relative to
   the moment the delayed burst appears* (the absolute figure is dominated by latency). The release
   window starts after the **slower** tap has drained (`input_end + first-tap latency + G`), because
   with two taps `G` apart the output legitimately extends past the first tap.
5. **Discontinuities / cleanliness** — `max |Δy|`, the more sensitive second difference
   `max |y[n]−2y[n−1]+y[n−2]|` against the smooth-sine baseline `A(2πf/sr)²`, and the fraction of
   energy outside ±12 Hz of f0 and its first 8 harmonics. Run for three interpolators: 6-point
   Lagrange, 256-phase-quantised Lagrange, and linear (baseline).
6. **Cost** — realtime factor and ms of CPU per second of audio, 1 and 8 voices.
7. **Stability** — 4 s, 8 voices, feedback 0.35: non-finite blocks, peak |y|, bounded verdict.
8. **Grain-length trade-off** — G ∈ {512 … 16384} at +3 and −4 semitones, reporting latency, measured
   error and modelled error side by side.

## Diatonic snapping rule (ours)

`diatonic_interval` converts the detector's f0 to MIDI, adds the nominal interval, and picks the
nearest degree of the C-major set `{0,2,4,5,7,9,11}` (searched over ±2 octaves). A target exactly
halfway between two degrees is a genuine tie: a detector estimate a few 1e-4 semitones away flips it.
Ties are therefore resolved **downwards** for determinism, and the harness reports the interval that
was *actually applied*, not the nominal one. This is why, e.g., a nominal +4 st in C major can come
out as +5: the tritone is exactly equidistant from the perfect fourth and the fifth.

## The artefact this PoC characterises

Writing the crossfade as `y = w·A + (1−w)·B` with `A = x[n−d]`, `B = x[n−d−G]`,
`d = d0 + G·φ`, `w = (1−cos πφ)/2`, the output is exactly

```
y = P(φ)·sin(u) + Q(φ)·cos(u)
u = ω₀n − ω₀d₀ − ω₀Gφ
P(φ) = cos(ω₀G) + (1−cos πφ)(1−cos ω₀G)/2
Q(φ) = −(1+cos πφ)·sin(ω₀G)/2
```

so `sin`/`cos` run at the intended `ω₀·ratio`, but the coefficient rotation adds a phase term
`Φ(φ) = atan2(Q, P)`. Because `Φ` does not return to its starting value over one grain, the output
fundamental shifts by

```
Δf = ΔΦ·(1−ratio)/G · sr/(2π)
```

This is a property of the reconstructed two-tap architecture, not a coding error. It is the reason
the grain-length sweep exists: the shift falls as `1/G` while latency rises linearly, so the design
is a genuine accuracy-versus-latency trade-off.

## A second structural consequence: the unity-ratio comb

At `ratio = 1` the grain phase does not advance (`φ̇ = (1−r)/G = 0`), so the crossfade weight stays
at `w = 0.5` and the output is `0.5·x[n−d] + 0.5·x[n−d−G]` — two equal taps `G` samples apart. That
is a comb filter, not a bypass: gain `|cos(ω₀G/2)|` at the input fundamental, notches every `sr/G`
Hz, and a −6 dB loss on uncorrelated material. The harness measures this and compares it with the
closed form (G = 4096, 220 Hz: measured −2.42 dB, analytic −2.42 dB).

If the shipped plug-in sounds like a clean unison at 0 semitones, then its tap spacing at `r = 1` is
*not* constant — i.e. the real design must move the taps in some other way (or use more than two).
That is a testable prediction this PoC produces, and it is recorded as an open question in
`..\OCTAVOX_OPEN_QUESTIONS.md` rather than assumed either way.

## Why this is not a measurement of the target

The target's own runtime behaviour was never observed (see
`..\OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md` §4). The PoC exists to (a) demonstrate that the
reconstructed signal chain is a coherent, implementable design, and (b) provide a place where the two
runtime questions left open in `..\OCTAVOX_OPEN_QUESTIONS.md` (pitch-direction sign, latency
accounting) can be reasoned about concretely without touching the target binary.
