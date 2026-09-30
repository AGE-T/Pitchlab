# Task 28 — Pitch Engine Candidate Re-Evaluation
## OLA / WSOLA / TD-PSOLA / FD-PSOLA / Transient-Aware Phase Vocoder

**Status:** IN PROGRESS (checkpoint 1 of 6: framework + OLA complete)
**Date started:** 2026-09-30
**Governing process:** AI Assisted Software Engineering Operating Principles v3.0
**Question being asked (the task's core decision principle):** *not* "which algorithm is the best
general-purpose pitch shifter" but **"is this a technically real, reproducible and sufficiently
distinct transformation that could provide useful sound-design character, specialized
functionality, a creative mode, an experimental engine, or valuable reference behaviour?"*

The old preliminary classifications (research/doppler-whip-pitch-research-report.md §B.1.3–B.1.6,
§B.1.10) were optimised for a *quality-first Doppler-driven engine* decision. This task
re-evaluates the same five candidates under the sound-design lens, with **measured evidence**
from clean-room prototypes, not reputation.

---

## 0. Method (frozen before implementation)

### 0.1 Prototype framework (all under `pitch-lab/prototypes/`, research-only)

| Component | Role |
|---|---|
| `proto_corpus.{h,cpp}` | 12 deterministic materials (Phase F): sine, harmstack, metallic, impulse, drum, pluck, bass, distorted, noise, vocal, pad (stereo), dense. 48 kHz, PCG64 seed 1, peak-normalised −12 dBFS |
| `proto_curves.h` | 10 transforms (Phase F): identity, ±12, ±7, ramp-slow, ramp-fast, reversal-100ms, octave-250ms, ±25 cents @ 5 Hz — input-timeline ratio semantics, matching the production `PitchCurveView` |
| `proto_measure.{h,cpp}` | Shared measurement layer: integrity, dual-estimator dominant frequency (Hann projection + ZC cross-check), pYIN F0, Hilbert-envelope AM depth/rate, comb ripple (spectral ripple vs ±6-bin moving average), onset count/sharpness/tail, spectral centroid, best-lag Pearson vs input, stereo L/R correlation, SHA-256 determinism hash |
| `proto_engine.{h,cpp}` | Prototype engine contract mirroring the production PitchEngine shape (configure → prepare allocates all state → block-streamed process → bounded finish), driven by the offline renderer's essential loop |
| `proto_alloc.{h,cpp}` | Global operator-new counter (the T-A1 technique) — asserts "all allocation in prepare()" with real evidence in every task28 executable |
| `proto_baseline.{h,cpp}` | Phase H differentiation: drives the FIVE frozen production engines (default configs) through their REAL contract — read-only reuse, registry untouched |
| `proto_run.{h,cpp}` | The three runners: full evaluation (12×10 matrix + parameter-effect matrix → canonical JSON + curated WAV renders), ctest selftest (identity exactness, duration, finiteness, determinism, allocation audit, block-split invariance), RT probe (per-block timing, RTF, allocation audit, block-size sweep 128/256/1024) |
| `candidate_*.cpp` | The five clean-room candidates |

**Artifacts:** `pitch-lab/results/research/task28-candidates/<id>/candidate-report.json`,
`rtprobe.json`, `renders/*.wav`, and `baseline/existing-engines.json` (240 records).

### 0.2 Claim discipline (Phase Q)

Every important conclusion in the per-candidate sections is tagged with its evidence source:
**[MEASURED]** (prototype measurement, recorded in the committed JSON), **[MEASURED-BASELINE]**
(existing-engine measurement through the same layer), **[VERIFIED-PAPER]** (primary source read
in Tasks 3/4 — see the doppler report's citations), **[INFERENCE]** (engineering reasoning),
**[UNPROVEN]**, **[OPEN]**. Inference is never presented as measurement.

### 0.3 VST build preservation (Phase K)

Every implementation checkpoint ends with: VST3 build (PITCHLAB_BUILD_VST3=ON), CTest, artifact
verification, worklog entry, commit. Research code lives outside the production registry and
never touches the frozen v0.1 sources (the baseline reuses them read-only through their real
contract).

---

## 1. PHASE A — OLA + resampling (`proto.ola`)

### NAME
proto.ola — fixed-hop overlap-add time-stretch + shared-resampler pitch shift.

### FAMILY
Time-segment (the OLA/SOLA family; DAFx book ch.6 lineage).

### CORE MECHANISM (frozen before coding; clean-room from the published structure)
* **Stage A (stretch ×ρ):** grains of N=2048 frames (Hann, periodic) at the fixed synthesis hop
  Hs=N/2 on the stretch timeline; grain m centred at s_m = N/2 + m·Hs reads input centred at
  a_m = N/2 + Σ Hs/ρ_j. **Window-product normalisation** (y = Σw·x / Σw with 0-guard) — honest
  OLA for any window/overlap/shape: edges fade in/out, non-COLA combinations keep their ripple
  (measured as the COLA diagnostic).
* **Stage B (resample ×ρ):** reads the finalised stretch signal at absolute double position q
  with per-output-frame ratio ρ(t)=curve[t], cutoff `antiAliasCutoff(ρ)`, Standard preset —
  the same §7 primitive and policy as the frozen PV engines.
* **Block-streaming:** one grain schedule, absolute double accumulators, absolute-position
  resampler reads → **bit-identical output for any block split** [MEASURED: 128 vs 256 vs 1024
  bit-identical].

### TECHNICAL REALITY
* **Technically real: YES.** Identity is transparent: dominant 440.06 Hz (0.00 st), RMS ratio
  1.00, Pearson 1.00 vs input, corr exact on every material [MEASURED, sine + all materials].
* **Duration preserved:** frames_out = n_in + (64..1120) flush tail across the matrix; declared
  flush bound honoured, 0 failures over 134 records [MEASURED].
* **Deterministic:** double-run bit-identical (SHA-256) on every record [MEASURED].
* **Duration/output accounting, finiteness, non-silence:** all records PASS [MEASURED].

**VERIFIED:** mechanism, identity transparency, duration preservation, determinism,
block-split invariance, allocation-free process/finish, RTF.
**INFERRED:** the stretch-grid time-warp mechanism behind the FM artifact (see below) — the
measured AM/FM statistics are the evidence; the sawtooth-interference explanation is reasoned
from the normalised-mixture math, not directly measured.
**UNPROVEN:** perceptual characterisation (no listening panel — the WAV renders are committed
for owner listening).
**OPEN QUESTIONS:** see end of section.

### REALTIME FEASIBILITY [MEASURED, local-evidence timing]
* RTF ≈ **0.06** of one core at 48 kHz mono (block 128/256/1024, all transforms).
* **0 allocations** in process/finish (audited).
* Worst block 0.69–2.7 ms at bs=128 (budget 2.67 ms — the reversal-100ms curve is *marginal at
  bs=128* on this sandbox: worst 1.001× budget; comfortable ≤0.26× at bs≥256); bs=1024 worst
  0.23× budget. Timing fields are local evidence, not portable claims.
* Declared latency: input lookahead 2144 frames (~45 ms) + output flush 3424 (6784 at ρ_min=0.5)
  — window-scale latency, as expected for the family.

### DYNAMIC PITCH BEHAVIOUR [MEASURED]
* Ratio updates at **hop rate** (grain centres): 1024 frames ≈ 21.3 ms granularity — the
  documented family limit.
* ramp-slow tracking is correct: measured F0 endpoints match the expected medians of the
  measurement windows (493.6→789.5 Hz vs expected ~525→~770 for 440→880) [MEASURED].
* ramp-fast: 834.8→901.9 Hz vs 880 hold — ±0.4 st flutter around the command (the grain-rate
  FM at work).
* reversal-100ms and octave-250ms render with 0 faults and preserved duration; the ±12
  alternating curve is where the worst-case block time peaks (transient of the schedule).
* ±25-cent @ 5 Hz modulation: dominant 441.7 Hz (tracks the mean), am 0.028 — slow modulation
  is clean.

### MATERIAL RESPONSE [MEASURED]
* **Sine:** dom err +0.56 st (+12) / +0.73 st (−12) — estimator bias from grain-rate FM
  sidebands (ZC cross-check: +0.17/+0.33 st; pYIN: +0.43/+0.23 st — three estimators disagree
  because the output's instantaneous frequency genuinely sawtooths at the grain rate).
  AM depth 0.19 (+12) / 0.32 (−12), AM rate 23.5 Hz ≈ fs/2048. **Asymmetric: downshifts
  flutter more than upshifts** (−7: am 0.21 vs +7: am 0.03).
* **Noise:** identity transparent (corr 1.00); +12: rms 0.58, comb 12.1 dB (the 0.95/2 cutoff
  halves the band — anti-alias behaviour, not a defect), −12: rms 0.86, comb 2.1 dB.
* **Drum (transient-rich):** onsets preserved 4→4 at all settings; sharpness −9.35 dB (+12),
  tail **−0.71 dB** — post-onset energy dips (a "cut" character — see artifact signature).
* **Vocal (formant-rich):** comb ripple 38.7 dB (+12) — the strongest comb of the matrix.
* **Pad (stereo):** per-channel identical processing → inter-channel structure preserved by
  construction; corr_out ≈ corr_in ≈ 0.03 (the material is deliberately decorrelated).
* **Dense/metallic/pluck/bass/distorted:** all records PASS; multi-partial materials show
  partial-image crossings at extreme shifts (metallic −12 dom err 6.0 st = the 423 Hz partial's
  image, not the 300 Hz one — measurement-semantics, the dominant follows energy, not identity).

### ARTIFACT SIGNATURE [MEASURED — the differentiated family]
1. **Comb / notching coloration:** 12.4 dB (drum +12) / 38.7 dB (vocal +12) ripple vs the five
   production engines' 2.4–8.5 dB on the same material [MEASURED-BASELINE] — 1.5–5× stronger
   than anything in the current registry.
2. **Grain-rate periodic FM + AM ("flutter"/"underwater"):** AM depth scales with |ρ−1| and
   direction (identity 0.01 → −12: 0.32), rate = fs/N·overlap — directly parameter-bound.
3. **Transient "cut" (post-onset notch):** tail −0.71 dB vs every production engine's positive
   tail (+1.79…+3.8 dB) [MEASURED-BASELINE] — a rhythm-flattening, gated feel on percussive
   material at these settings.
4. **Onset non-duplication:** onset count preserved 4→4 (no double-attacks at N=2048/Hs=1024).
5. **COLA ripple −300 dB** at the default (perfect Hann/N/2 COLA) — the amplitude modulation
   that remains is *phase cancellation*, not window-sum ripple; non-COLA configs (rect,
   overlap-4/8) trade comb for AM/rms loss (overlap-8: rms 0.17, comb 32 dB) [MEASURED,
   parameter-effect matrix].

### SOUND-DESIGN CHARACTER (Phase G)
**COULD THIS SOUND BE USEFUL EVEN IF IT IS NOT HI-FI PITCH SHIFTING? — YES, as a deliberate
artifact engine.** The character is:
* **deterministic** (bit-identical reruns),
* **controllable through real DSP parameters** — window_frames sets the flutter rate
  (fs/N·overlap), overlap trades AM against comb, window shape changes the interference
  signature (rect: comb 6.6/rms 0.70; bartlett: comb 10.9; hamming: comb 10.5 — all measured),
* **material-dependent** (strongest on formant-rich vocals, mildest on noise),
* **pitch-dependent** (downshift-flutter asymmetry; direction matters),
* **modulation-speed dependent** (slow ±cent modulation is clean; 100 ms reversals excite the
  worst-case schedule transient).
Labels (measured, not assumed): *grainy, phasey, comb-filtered, fluttering, transient-cutting*.

### IMPLEMENTATION COMPLEXITY
Very low: ~500 lines including the block-streaming machinery; reuses the §7 resampler, the
analysis layer for nothing at runtime (pure time-domain). All allocation in prepare(); reset by
fresh instance.

### CPU / MEMORY CHARACTERISTICS
CPU: RTF 0.06 (≈1.5% of one core). Memory: O(N + maxBlock) sliding windows ≈ 6.6k frames +
per-channel accumulators — trivial. [MEASURED]

### REUSABLE PITCH LAB INFRASTRUCTURE
`core/resampler` (interpolateAtAbs + antiAliasCutoff), PCG64, WAV writer, analysis layer
(measurement only). The window-product-normalised stretch core is a candidate shared layer for
**WSOLA** (same machinery + the similarity search).

### ARCHITECTURAL FIT
Fits the existing PitchEngine contract 1:1 (the prototype already implements the production
lifecycle shape: configure/prepare/process/finish, input-timeline curve, block-streaming,
declared latencies). No architectural change required. Registry/parameter decisions are owner
tasks, out of scope here.

### LICENSING [VERIFIED-PAPER/TASK-3]
Algorithm concept unencumbered (decades-old published math; DAFx MATLAB M-files are
educational-only — NOT used; this is a from-the-math clean-room implementation). No external
code linked. The Verhelst-Roelands patent landscape (WSOLA, next section) does not cover plain
OLA.

### CANDIDATE CLASSIFICATION (preliminary; final at the cross-candidate report)
**3 — CREATIVE / CHARACTER ENGINE** (upgraded from the old "Benchmark baseline only" verdict —
the old verdict optimised for quality; the measured artifact family here is distinct from all
five production engines, deterministic, and parameter-controllable). Also valuable as **4 —
REFERENCE ENGINE** (the worst-case-quality reference point for the benchmark lane — the old
role, kept).

### RECOMMENDED NEXT STEP
Keep as the OLA reference/creative candidate; complete WSOLA (Phase B) on the same machinery —
the WSOLA-vs-OLA contrast on the same corpus directly measures what the similarity search buys
and what character it removes.

### KNOWN LIMITATIONS
* Dominant-frequency estimators are biased by the grain-rate FM sidebands on shifted tonal
  material (three estimators disagree by up to 0.5 st) — the *measurement* of "output pitch"
  for OLA-like engines is inherently fuzzy; the artifact IS the pitch instability.
* The selftest's shifted-pitch tolerance is therefore 1.5 st (gross regression net); identity
  exactness (0.1 st) carries the implementation-invariant weight.
* RT probe timing is local-evidence (sandbox machine), not portable.
* Stage-A/stage-B ratio indexing differs on fast curves (documented design note in the header)
  — bounded, window-scale, characterised by the ramp/reversal records.

### OPEN QUESTIONS
* Does a *shorter* window (512) with overlap-8 produce a musically useful "granular shimmer"
  distinct from native.granular's character? (Measured: comb 16.8 dB, rms 0.42, AM rate 187.5
  Hz — parameters exist; perceptual value is an owner-listening question.)
* The transient "cut" (tail −0.71 dB): is this a feature (rhythmic gating) or a defect at
  default settings? Owner listening decision.

---

## 2. PHASE B — WSOLA + resampling (`proto.wsola`)

### NAME
proto.wsola — waveform-similarity overlap-add (Verhelst & Roelands 1993) + shared resampler.

### FAMILY
Time-segment (the WSOLA/SOLA family; SoundTouch's "WSOLA-like" TDStretch is the production
lineage — not used, LGPL, clean-room here).

### CORE MECHANISM (frozen before coding; clean-room from the published description)
* Identical two-stage engine as proto.ola (stretch ×ρ with window-product normalisation +
  §7 resample ×ρ). Only the per-grain analysis-position choice changes.
* Per grain m: choose integer δ ∈ [−tolerance, +tolerance] (default 768 ≈ 16 ms) minimising
  SSE(candidate overlap content, output-built-so-far) over the overlap region
  O = [s−N/2, s−Hs+N/2). Grain 0: δ=0 (no built output).
* **Drift-free by construction:** the tolerance region is centred on the TIME-SCALING LAW's
  nominal position; δ perturbs only the current grain and the next nominal advances from the
  law — the content mapping cannot wander beyond ±tol per grain [MEASURED consequence:
  max|δ| ≤ tolerance in every record].
* **Deterministic tie-break:** δ evaluated in the order 0, +1, −1, +2, −2…; strictly-smaller
  SSE replaces — ties resolve closest-to-zero. (The first implementation used
  ascending-from−tol ties + re-anchored nominals: on near-silent input every SSE ties, the
  tie-break systematically chose −tol, and the re-anchored schedule drifted backwards until
  the input window overflowed — a real, reproduced, root-caused prototype defect, class E,
  fixed with both the tie-break and the law-anchored nominal; drum/plus12 reproduced the
  overflow deterministically before the fix and completes after it.)
* Channel 0 drives the shared scheduling decision for stereo (documented; coherence kept by
  identical per-channel audio).

### TECHNICAL REALITY
* **Technically real: YES — and dramatically cleaner than OLA** [MEASURED, same corpus]:
  * sine ±12/±7: dominant error **0.00 st EXACT** (OLA: 0.56/0.73/−0.23/−0.61);
  * AM depth 0.008–0.012 (OLA: 0.19–0.32) — the flutter family is GONE;
  * rms ratio 1.00 (OLA: 0.75–0.81) — no cancellation loss;
  * harmstack/pluck/bass +12: dominant error 0.00 st (OLA: −1.33);
  * noise +12: comb 1.4 dB = varispeed/vardelay-class cleanliness (OLA: 12.1).
* Identity transparent (dom 0.00 st, rms 1.00, corr 1.00); duration preserved; deterministic
  (bit-identical double-runs); 0 failures over 134 records + 14 sweep records.
* **The search statistics are the behavioural fingerprint** [MEASURED]:
  * sine/plus12: mean|δ| 447, max 763, zero 0% — the search actively locks phase;
  * drum/plus12: mean|δ| 59, **zero 78%** — transients anchor to the nominal schedule;
  * noise: mean|δ| 325 (random-walk alignment); vocal: mean|δ| 157.
  This is **content-adaptive scheduling** — no existing Pitch Lab engine's behaviour depends
  on the waveform content at all (varispeed/vardelay = pure per-sample math; PV/granular =
  fixed grids).

**VERIFIED:** mechanism, cleanliness, identity transparency, determinism, block-split
invariance, allocation-free processing, drift bound.
**INFERRED:** the SSE-tie silence-drift mechanism (reproduced deterministically; the causal
story is from the code path).
**UNPROVEN:** perceptual quality relative to élastique/SoundTouch class shifters (no external
reference rendered — the old research verified their licences; a benchmark link would be an
owner decision).
**OPEN:** see below.

### REALTIME FEASIBILITY [MEASURED, local-evidence timing]
* RTF ≈ **0.23** at 48 kHz mono (3.7× OLA — the search is (2·tol+1)·(N−Hs) MACs per grain);
  still ~4× headroom on one core.
* **0 allocations** in process/finish; worst block 0.26–0.40× budget at bs=1024 (the search
  burst lands inside one block; 0.87–1.0× at bs=128 on reversal — marginal at 128 like OLA,
  comfortable ≥256).
* Latency: window-scale (input lookahead 2144 + the tolerance read reach covered by the
  input back-margin; the driver pad covers the full demand).
* Block-size independence: bit-identical 128 vs 256 vs 1024.

### DYNAMIC PITCH BEHAVIOUR [MEASURED]
* Hop-rate control (same 21.3 ms class as OLA/PV family).
* ramp-fast: 840.8→879.9 Hz vs 880 commanded (OLA: 834.8→901.9 — WSOLA tracks the glide
  tighter); ramp-slow correct; ±25c@5 Hz: dominant 438.8 Hz, am 0.001 — clean slow modulation.
* The search re-locks each grain: under ratio changes the alignment target shifts; no faults,
  duration preserved on all ramp/reversal records.

### MATERIAL RESPONSE [MEASURED]
* **Tonal monophonic (sine/harmstack/pluck/bass/metallic):** essentially clean — the
  family's home turf (matches the audiojs attack-corr 0.995 / f0-err 1.67 Hz findings from
  the old research [VERIFIED-SOURCE]).
* **Vocal (formant-rich):** comb 52.0 dB — WITHIN the production engines' own 41–56 dB range
  on this material (varispeed 55.8, phaselocked 50.5, pv.classic 46.2) — the metric here
  partly reflects the material's spectral character; NOT a differentiator vs engines.
* **Drum:** comb 4.5 (engine range 2.4–8.5); **sharpness −8.8 dB = the sharpest transient
  reproduction of ALL seven engines measured** (varispeed −9.2, vardelay −10.1, pv.classic
  −13.2, phaselocked −11.8, granular −9.9, OLA −9.4); tail −0.8 dB (the OLA-family
  transient "cut" — see below).
* **Direction asymmetry** [MEASURED]: +12 tail −0.8 (cut) vs −12 tail +7.3 (echo-ish decay —
  between varispeed's +9.3 and pv.classic's +2.1). Slow-downs ring, speed-ups cut — the
  documented SoundTouch "echoing when slowing" is measurable here on the same machinery.
* **Noise:** comb 1.4 dB, rms 0.61 (the anti-alias band-halving at ρ=2 — same class as all
  resample-based engines; vardelay's 0.91 shows the non-resampling Doppler family keeps the
  band).
* **Dense polyphony:** comb 9.2 — inside the engine range (7.3–9.6); am 0.50 ≈ engines.

### ARTIFACT SIGNATURE [MEASURED]
1. **What the search REMOVES (vs OLA, same corpus):** grain-rate FM/AM flutter (0.19–0.32 →
   0.008–0.012), cancellation rms loss (0.75–0.81 → 1.00), comb on tonal/noise material
   (8.7/12.1 → 2.4/1.4 dB).
2. **What the search KEEPS:** the OLA-family transient behaviour — sharpness ≈ OLA's, the
   post-onset cut (tail −0.8 vs −0.7) — and the downshift echo (tail +7.3).
3. **What the search ADDS:** content-dependent behaviour (the δ statistics) and a residual
   comb on polyphonic/formant-rich material in the engines' own range.
4. **Tolerance trade** [MEASURED sweeps]: tol=96 → comb 6.3 on drum (search starved) and
   harmstack dom err −0.04; tol=384 ≈ default; tol=2048 → mean|δ| only 69 on drum (transients
   lock early) but the sine search slides freely (mean|δ| 447 already at 768 — period
   multiples). window-8192: comb 2.9 (smoothest) but onset sharp −7.5 (smearest) — the
   window/comb/smear triangle is directly measurable.

### SOUND-DESIGN CHARACTER (Phase G)
**COULD THIS SOUND BE USEFUL EVEN IF IT IS NOT HI-FI PITCH SHIFTING?** — It is close to hi-fi
on its home turf; the sound-design value is subtler than OLA's:
* **material-aware behaviour** (transients anchor, tonal content locks — the engine "reads"
  the signal; deterministic and repeatable);
* **direction-dependent envelope character** (up = cut, down = echo/ring) — a rhythmic
  shaping tool on percussive material at extreme ratios;
* **the sharpest transient reproduction in the lab** (−8.8 dB sharpness);
* at starved tolerance (96) or rect windows, a mild "search-struggle" flutter returns — a
  controllable degradation axis [MEASURED sweeps].
Labels: *clean-on-tonal, transient-sharp, cut/echo directional, content-locking*.

### IMPLEMENTATION COMPLEXITY
Low: ~140 lines over the OLA core (the search + statistics). All state preallocated; the
search scratch is one N-sized member buffer.

### CPU / MEMORY CHARACTERISTICS
RTF 0.23 (search-dominated); memory ≈ OLA + N scratch + tolerance input margin (~30 KB
total at defaults). [MEASURED]

### REUSABLE PITCH LAB INFRASTRUCTURE
Everything OLA reuses; the similarity search itself is a reusable layer for any
segment-splicing engine (the "intelligent splice" of the old harmonizer research).

### ARCHITECTURAL FIT
Same as OLA: the production PitchEngine shape 1:1, no architectural change. The
content-adaptive scheduling is a NEW behavioural class in the lab (the first engine whose
grain placement depends on the signal) — worth noting for the architecture's engine-family
documentation, but expressible within the existing contract.

### LICENSING [VERIFIED-PAPER/TASK-3]
Verhelst & Roelands 1993 concept unencumbered (patents expired). SoundTouch itself is
LGPL-2.1+ — NOT used, not linked, no code read during implementation (clean-room from the
published math; the DAFx M-files educational-only — not used).

### CANDIDATE CLASSIFICATION (preliminary)
**1 — PRODUCT ENGINE CANDIDATE (quality/static-shift lane) + material-adaptive character.**
The old verdict "optional slow/static mode" is CONFIRMED and strengthened: measured
cleanliness matches or beats the PV family on tonal material at hop-rate control and
trivially higher CPU than OLA — and the content-adaptive scheduling + transient sharpness
are behaviours absent from all five production engines. Overlap with varispeed/vardelay
exists (both clean, formant-shifting) — the differentiators are transient sharpness,
directional cut/echo, and material-adaptivity.

### RECOMMENDED NEXT STEP
Proceed to TD-PSOLA (Phase C): the pitch-synchronous lane shares the tracker; the
WSOLA-vs-PSOLA contrast on the voice-like material (both "content-aware") will separate
period-locking from similarity-searching behaviour.

### KNOWN LIMITATIONS
* Search CPU scales with tolerance × overlap length (RTF grows ~8× at window-8192-class
  configs — still < 1 core; sweeps recorded).
* The overlap-region SSE is un-windowed (raw candidate vs built estimate) — the classic
  formulation; a windowed variant would bias the comparison differently (untested).
* Same measurement caveats as OLA (dominant-estimator bias on fluttering outputs — mostly
  moot here since the flutter is gone).

### OPEN QUESTIONS
* Does the ±tolerance content wander produce audible time-smearing on rhythmic material at
  extreme ratios? (corrCoef on drum +12: 0.71 — vs varispeed 0.79, pv.classic 0.58 — inside
  the engines' own range; not a clear outlier.)
* Would a normalised cross-correlation (vs SSE) metric change the lock statistics? (SSE is
  amplitude-sensitive; NCC is the SoundTouch-class choice — a one-parameter follow-up if
  WSOLA proceeds to product candidacy.)

---
*(TD-PSOLA, FD-PSOLA, Transient-aware PV sections follow in the next checkpoints.)*
