# Pitch Lab v0.1 — RESULT PRODUCT

**The first tangible output of the completed Pitch Lab v0.1 research system.**
Produced 2026-09-28, immediately after the §17.6 final gate (PASS). This is
not an engineering demo and not a mock-up: every WAV and every number in this
tree was produced by the real v0.1 pipeline — the five registered DSP engines,
the offline render harness, the canonical-JSON manifests, the Analyzer and
the clean-room C++ pYin reference tracker (OD-12, §10.5) — from the committed
authoritative state of this repository.

---

## 1. What this product is (and why it is the v0.1 result)

Pitch Lab v0.1 is a *measurement instrument* for pitch-processing engines:
it renders the same committed input material through every engine under the
same pitch curves and measures, per metric and without any global score,
how each engine actually behaves. The smallest result that demonstrates that
system as a usable research/output tool is exactly what this tree contains:

* a **reproducible experiment configuration** (committed:
  `experiments/suites/result-product-v0.1.toml`),
* **representative processed audio material** — 30 renders, one per
  (material × curve × engine), retained here as float32 listening WAVs,
* **the corresponding analysis evidence** — 30 Analyzer artifacts
  (`pitchlab.analysis.v1` JSON, all 14 metrics each),
* **this human-readable presentation** of the comparative results.

Three committed corpus materials × two curves × all five engines = 30 jobs:

| Material (committed corpus) | Why chosen |
|---|---|
| `syn-harmonic-stack-220-5s-48k` | harmonic stack, constant 220 Hz, carries an analytic `[f0]` recipe — pitch-error / warble / phase-coherence are *measured* (expected values derived analytically, never from the engine) |
| `syn-harmonic-saw-220-vibrato-5s-48k` | saw-equivalent stack with vibrato — isolates how each engine tracks a *time-varying* input pitch under a static shift |
| `syn-transient-percussive-2s-48k` | 8 percussive onsets from the recipe — onset timing and transient preservation |

| Curve | Why chosen |
|---|---|
| `static-plus-12` (constant ratio 2.0) | the steady-state case: pitch accuracy, warble (f0 flutter under a static ratio — §H.1), spectral/phase behaviour |
| `reversal-plus12-minus12-100ms` (+12 st held 400 ms → linear-in-ratio transition to −12 st over 100 ms → hold) | the "killer test" of the research battery: control-rate honesty — *does the engine's output pitch actually follow the curve when it moves fast?* (pitch-lag) |

Engines: the complete v0.1 registry — `native.varispeed` (rate-following
reference), `native.vardelay`, `native.pv.classic`, `native.pv.phaselocked`,
`native.granular`. Identical engine configs and seeds to the §13.6
cross-engine matrix (seeds 7), 48 kHz mono, benchmark mode.

## 2. Where everything lives

```text
results/v0.1/
  README.md                       <- this report
  renders/<engine>/<job>/signal.manifest.json        (provenance: curve hash,
                                    engine params, seed, lengths, master sha256)
  renders/<engine>/<job>/listening/signal.f32.wav    (the audible renders)
  analysis/<engine>/<job>/analysis.json              (the measured evidence)
experiments/suites/result-product-v0.1.toml          (the authoritative input)
artifacts/renders|analysis/result-product-v0.1/...   (regenerated working copies,
                                    incl. the float64 masters — gitignored by design)
```

`<job>` = `<asset-id>__<curve-id>__48000__<paramtag>`. The float64 masters
are NOT retained in git (the v0.1 artifact policy — architecture §B.2:
generated trees regenerate on demand); their SHA-256 provenance is recorded
in every retained manifest and analysis artifact, and regeneration is
byte-exact (see §4).

## 3. How to reproduce (exact commands)

From the repository root `pitch-lab/`, with the project built (canonical
environment: GCC 13.3.0 / CMake 3.31.6 / Ninja, or a local C++20 toolchain —
build spec §1/§13; determinism is same-binary, §7.5):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/pitchlab render experiments/suites/result-product-v0.1.toml --root .
./build/pitchlab analyze experiments/suites/result-product-v0.1.toml --root .
```

Expected console result: `30 job(s): 0 failed, 30 ok` and
`30 job(s): 0 with analysis errors, 30 analysed`. The working copies land in
`artifacts/` (gitignored); the retained presentation copies are this tree.
Regeneration was verified at production time: **delete + re-render +
re-analyse ⇒ all 120 files (masters, listening copies, manifests, analysis
artifacts) SHA-256-identical** (executed 2026-09-28, twice end-to-end).

## 4. The measured results — per metric, no global score

Every value below is copied from the committed `analysis/` JSON artifacts
(the generating machine-readable evidence); nothing is hand-derived. Statuses
are honest: `not-applicable` means the metric does not apply to that job
(e.g. pitch metrics on the percussive material — no `[f0]` recipe; warble on
dynamic curves — the metric is defined for static ratios; cpu-cost — not
measured by this offline path in v0.1). All tolerances anywhere in the system
are the owner's provisional values (OD-9) — the metrics *measure*, they do
not grade.

### 4.1 Static +12 st on the constant harmonic stack (the steady-state case)

Pitch error = |measured f0 − expected 440 Hz| in cents over in-span voiced
frames (analytic expected model: input recipe × curve, per emission map).
Warble = std of the f0 deviation series after removing the input's own
modulation (here: none — constant input). Spectral error = median
log-spectral distance vs the varispeed reference render of the same
(input, curve). R2 = 2nd-harmonic phase-coherence ratio. Latency = declared
input latency (manifest).

| Engine | pitch-error median (cents) | pitch-error p95 (cents) | warble f0-flutter std (cents) | spectral error median LSD (dB) | phase coherence R2 | declared latency (ms) |
|---|---|---|---|---|---|---|
| native.varispeed | 0.1997 | 0.1999 | 0.0509 | 0.00 | 1.0000 | 1 |
| native.vardelay | 0.1997 | 0.2054 | 0.0583 | 64.63 | 0.9998 | 500 |
| native.pv.classic | 0.1984 | 0.1993 | 0.0554 | 65.21 | 0.9921 | 53 |
| native.pv.phaselocked | 0.2012 | 0.2024 | 0.0461 | 65.24 | 0.9873 | 53 |
| native.granular | 0.1997 | 0.2000 | 0.1853 | 67.98 | 0.9980 | 50 |

**What this shows:** on constant harmonic material every engine in the
registry lands the +12 st target within ~0.2 cents median (sub-bin tracker
class; the reference's own floor on this material is the same 0.2-cent
class) with cent-level-at-most f0 flutter. The engines differ *spectrally*
(the LSD column: different band/envelope treatment at ratio 2) and in
declared latency (vardelay's 500 ms retention window vs varispeed's 1 ms).

### 4.2 The rapid reversal (control-rate honesty — the pitch-lag discriminant)

The curve moves +12 st → −12 st in 100 ms. Pitch-lag = how many ms the
engine's *measured output pitch* trails the expected pitch curve on the
input timeline (cross-correlation of measured vs expected f0 under the
reversal; correlation-at-lag ≈ 1 means a clean lock).

| Engine | pitch-error median (cents) | pitch-error p95 (cents) | pitch-lag (ms) | correlation at lag | spectral error median LSD (dB) |
|---|---|---|---|---|---|
| native.varispeed | 0.0065 | 0.0369 | 0.0 | 0.9998 | 0.00 |
| native.vardelay | 0.0065 | 0.0694 | 0.0 | 0.9999 | 51.01 |
| native.pv.classic | 0.0067 | 0.1984 | 0.0 | 1.0000 | 49.41 |
| native.pv.phaselocked | 0.0090 | 0.2012 | 21.3 | 1.0000 | 45.94 |
| native.granular | 0.0065 | 0.1997 | 32.0 | 1.0000 | 48.66 |

**What this shows:** the control-rate behaviour the research set out to
measure. The reference, the variable-delay engine and the classic PV track
the reversal with zero measured lag; the phase-locked PV's analysis-frame
advance trails by 21.3 ms and the granular engine's quantised read grid by
32.0 ms (≈ the 50 ms grain period × overlap accounting). These are the
v0.1 registry's honest control-rate signatures — measured, not modelled.

### 4.3 Static +12 st on the vibrato input (tracking a moving input pitch)

The input's own pitch wiggles (saw-equivalent stack, vibrato); the expected
model removes the input's own modulation, so what remains is each engine's
*added* deviation and flutter on a moving pitch.

| Engine | pitch-error median (cents) | pitch-error p95 (cents) | warble f0-flutter std (cents) | spectral error median LSD (dB) |
|---|---|---|---|---|
| native.varispeed | 2.2875 | 4.1726 | 2.525 | 0.00 |
| native.vardelay | 33.4328 | 83.9151 | 49.729 | 74.44 |
| native.pv.classic | 10.2331 | 14.6347 | 10.202 | 72.77 |
| native.pv.phaselocked | 1.3665 | 2.7683 | 1.639 | 73.57 |
| native.granular | 31.9428 | 83.9981 | 48.706 | 80.83 |

**What this shows:** the sharpest engine separation in the battery. The
block-quantised read-rate engines (vardelay, granular) staircase a moving
input pitch — tens of cents of median deviation and ~49 cents of flutter
(granular's 31.9-cent median / 48.7-cent flutter is its quantised read grid
acting on the vibrato; vardelay's numbers are its crossfade/delay
quantisation). The phase vocoders track the vibrato (phaselocked at the
1.4-cent / 1.6-cent class, classic at 10 cents), and the varispeed
reference sits at 2.3 cents / 2.5 cents — the measurement floor of the
tracker+resampler chain on this material.

### 4.4 Static +12 st on the percussive material (transients)

The material has 8 recipe-authored onsets; onset timing compares measured
output onsets against them; transient preservation = attack correlation vs
the varispeed reference render.

| Engine | onset max error | onset mean error | (ms) | attack correlation vs ref | spectral error median LSD (dB) |
|---|---|---|---|---|---|
| native.varispeed | 10 fr | 2.750 fr | 0.208 | 1.0000 | 0.00 |
| native.vardelay | 5 fr | 3.143 fr | 0.104 | −0.0304 | 16.60 |
| native.pv.classic | 1 fr | 1.000 fr | 0.021 | 0.3800 | 12.01 |
| native.pv.phaselocked | 67 fr | 42.375 fr | 1.396 | 0.0743 | 15.10 |
| native.granular | 5 fr | 3.000 fr | 0.104 | 0.8666 | 0.05 |

**What this shows:** onset timing is sub-millisecond for four engines; the
phase-locked PV's window-locked processing shows 1.4 ms of onset jitter.
Attack preservation separates the engines sharply (the reference is 1.0 by
self-construction): granular's OLA passes attacks best (0.87), the classic
PV smears them (0.38), phaselocked's rigid region rotation does not
reproduce transient waveforms (0.07), vardelay's crossfade placement
decorrelates attacks (−0.03). The spectral column adds a surprise worth
having measured: on this *broadband* material granular's OLA output is
spectrally nearly identical to the continuous resampling reference
(0.05 dB median LSD) while differing by 12–17 dB on the engines with
different band treatment.

### 4.5 Duration behaviour (measured, never normalised away — §G.3)

For the same (input, curve), rate-following and preserving engines produce
*different-duration* outputs by contract. Static +12 st on the 5 s stack:

| Engine | behaviour | output duration | length delta vs contract |
|---|---|---|---|
| native.varispeed | RateFollowing | 2.500 s | 0 |
| native.vardelay | Preserving | 5.043 s | 0 |
| native.pv.classic | Preserving | 5.043 s | 0 |
| native.pv.phaselocked | Preserving | 5.043 s | 0 |
| native.granular | Preserving | 5.050 s | 0 |

The varispeed render is 2.5 s because a ratio-2 resampling consumes the
input twice as fast — this is the *realised duration ratio*, a reported
measurement (§H), not an error. Preserving engines realise N_in + declared
latency exactly (length delta 0 in every job of the battery).

### 4.6 Statuses and honesty notes

Across the 30 jobs: 0 render failures, 0 analysis errors. Tracker-dependent
metrics are `ok` exactly on the two `[f0]`-recipe materials (20 jobs) and
`not-applicable` on the percussive material (10 jobs); warble is
`not-applicable` on the reversal (dynamic curve, per definition); cpu-cost
is `not-applicable` in all 30 (v0.1's offline path does not measure RTF).
Per-job values, notes, method metadata (tracker constants, alignment
method, tolerance context) are in each `analysis.json` — start from any
file listed in §2. No aggregate score exists anywhere in the system
(architecture §O).

## 5. Listening guide

The retained WAVs are float32 listening copies of the float64 masters
(`renders/<engine>/<job>/listening/signal.f32.wav`, one per job — plain
WAVs, any DAW/media player). Suggested listening, same material and curve
across engines (the §G.4 listening-index pattern: compare within a group):

* **`syn-harmonic-saw-220-vibrato-5s-48k__static-plus-12__48000__*`** — hear
  the vibrato tracking classes of §4.3: varispeed/phaselocked keep the
  vibrato smooth; classic PV shows PV-quality modulation; vardelay and
  granular exhibit the staircase flutter (the ~49-cent warble).
* **`syn-harmonic-stack-220-5s-48k__reversal-plus12-minus12-100ms__48000__*`**
  — hear the reversal land: varispeed/vardelay/classic snap at the 100 ms
  turn; phaselocked and granular audibly arrive late (21.3 / 32.0 ms —
  §4.2). Also hear the varispeed render's duration behaviour (rate-following).
* **`syn-transient-percussive-2s-48k__static-plus-12__48000__*`** — hear the
  transient classes of §4.4: granular's crisp attacks vs the PVs' smeared
  attacks vs vardelay's crossfaded ones.

## 6. Interpretation (brief, honest)

The v0.1 system's research value is demonstrated by the *separation* it
produces, not by any ranking: the same committed material, rendered and
measured through one deterministic pipeline, yields per-metric engine
signatures — control-rate lag (0 / 0 / 0 / 21.3 / 32.0 ms), vibrato tracking
(2.3 / 33.4 / 10.2 / 1.4 / 31.9 cents median), transient behaviour
(1.0 / −0.03 / 0.38 / 0.07 / 0.87 attack correlation), duration behaviour
by contract, and the shared sub-cent-to-0.2-cent steady-state pitch class.
Which signature is "better" is an owner/research judgement per use case;
Pitch Lab v0.1's job is to make those signatures *measurable, reproducible
and audible* — which is what this tree retains.

---
*Produced by the Pitch Lab v0.1 pipeline (commit recorded in the repository
history at production time; `pitchlab 0.1.0`, implementation phase). See
`research/pitch-lab-v0.1-implementation-specification.md` §17 step 6 (final
gate record) and §13.8 (product evidence) for the authoritative context.*
