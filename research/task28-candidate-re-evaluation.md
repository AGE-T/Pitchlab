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

## 3. PHASE C — TD-PSOLA (`proto.tdpsola`)

### NAME
proto.tdpsola — pitch-synchronous overlap-add (Moulines & Charpentier 1990), two modes.

### FAMILY
Pitch-synchronous (the PSOLA family; Praat/audiojs lineage — reference behaviour, clean-room
here).

### CORE MECHANISM (frozen before coding; TWO design errors found and corrected by
measurement before any quality conclusion — both are part of the research record)
* **Analysis (offline pre-pass):** the project's clean-room pYIN tracker gives F0+voicing
  (window 2048/hop 512 @ 48k); marks every P (voiced) or P_uv (200 Hz default), each refined
  to the nearest positive-going zero crossing within ±P/4 (minimal epoch alignment).
* **Synthesis (the corrected MC90 semantics):** grain k = 2·P Hann window centred at the
  analysis mark nearest a_k; **raw overlap-add with a per-grain level scale s/P** (the mean-
  restoring classic formulation). Output mark spacing **s = P/β → output pitch = β×F0**.
  * **mode "pitch" (default):** u = P (cycle-accurate consumption) — the classic MC90/psola.m
    PITCH MODIFICATION: **formant-preserving, DURATION-CHANGING (÷β̄ over voiced spans)**.
  * **mode "stretch":** u = s/α (mark reuse/skip = the PSOLA time-scale by α), then the §7
    resampler by α — the conventional duration-preserving shifter (formants shift).
  * Unvoiced: s = u = P_uv regardless of β — **the engine is literally a passthrough on
    material it deems unvoiced** [MEASURED: the drum material's metrics are IDENTICAL across
    all ten transforms — sharpness −14.9 dB, comb 3.3 dB, rms 1.00 in every record].
* **Design error #1 (corrected):** the first version tried a duration-preserving direct pitch
  mode (u = s). With mark reuse the output became **2s-periodic** — measured 444 Hz on BOTH
  +12 and −12 of a 440 Hz sine — the sub-multiple periodicity of the repeated-mark pattern.
  Numerically proven (composite simulation): pure cycle re-tiling cannot shift a continuous
  periodic signal at any u. The published algorithms either change duration (pitch mode) or
  resample (stretch mode); there is no valid duration-preserving pure-PSOLA mark schedule.
* **Design error #2 (corrected):** window-product normalisation (borrowed from the OLA
  engine) **cancels the Hann's neighbour-pulse suppression** at low overlap — dividing by the
  single Hann restored the full 2P grain content and the downshift rendered unshifted. The
  classic raw OLA with the s/P per-grain scale keeps the pulse suppression (the mechanism)
  AND restores the mean level; identity is EXACT by COLA (Hann(2P) at hop P sums to exactly 1).

### TECHNICAL REALITY [MEASURED]
* **Identity: exactly transparent** (dom 0.00 st, rms 1.00 — the COLA property).
* **mode "stretch" — a fully working conventional shifter:** harmstack +12 f0 439.8 (0.01 st),
  dom −0.01 st, rms 1.00; sine ±12 (dbg harness) f0 880.0/219.8 EXACT; duration preserved.
* **mode "pitch" — the voice-grade transform:** vocal +12: **f0 279.9 = 140×2 EXACT**,
  −12: **f0 70.0 EXACT**; bass +12 f0 219.6; pluck +12 f0 439.2 — the F0 tracks the command
  on every pulse-bearing material.
* **FORMANT PRESERVATION (the distinct capability):** vocal +12 pitch-mode centroid 538 Hz vs
  input 653 Hz (≈ preserved) vs stretch-mode 1306 Hz (doubled) — and the dominant partial
  stays at the formant (559.9/700.3 Hz) while the F0 moves ×2/÷2. **No existing Pitch Lab
  engine preserves the spectral envelope while moving the fundamental** [MEASURED-BASELINE:
  all five shift the centroid 1:1 with ratio].
* **Material dependence is REAL and measured:** the pure sine in pitch mode does NOT shift
  (numerically proven: continuous periodicity cannot be re-tiling-shifted; the output carries
  sub-harmonic AM — tracker reads 80 Hz garbage); the drum (tracker-unvoiced) passes through
  untouched. **The engine transforms exactly the material its analysis recognises as
  voiced-and-pulsed — the strongest material-gating behaviour in the lab.**
* Deterministic (bit-identical double-runs); 0 allocations in process/finish; block-split
  invariance bit-identical (128/256/1024).

**VERIFIED:** identity transparency, both modes' pitch behaviour, formant preservation,
voicing passthrough, determinism, allocation audit, block invariance, duration semantics.
**INFERRED:** the pulse-event mechanism explanation (the measured f0/centroid/voicing evidence
is direct; the "pulses re-space, continuous tones cannot" causal story is reasoning).
**UNPROVEN:** perceptual voice quality vs Praat-class implementations.
**OPEN:** see below.

### REALTIME FEASIBILITY [MEASURED, local-evidence timing]
* **Synthesis RTF ≈ 0.001** (0.1% of a core — the cheapest measured engine in the lab; worst
  block 0.03 ms at bs=1024).
* **The analysis is the cost driver:** pYIN ≈ 180 ms per signal-second measured (RTF 0.18) —
  still comfortably realtime on one core, but 180× the synthesis cost. In a streaming design
  the analysis adds window+hop ≈ 53 ms lookahead @ 48k.
* Latency: synthesis lookahead 2·P_max+K+128 = 2080 frames (~43 ms) + the analysis lookahead
  in a streaming design; flush bounded (scaled by 1/min ratio in pitch mode — the first fixed
  bound was exceeded on −12, measured and fixed).

### DYNAMIC PITCH BEHAVIOUR [MEASURED]
* β indexed **per output mark** — per-PERIOD control rate: on voice, 2.5–10 ms — the fastest
  control lane of any candidate (the family's documented claim, now measured: the ramp/reversal
  records re-lock every mark with 0 faults).
* Duration follows 1/β̄ over voiced spans (pitch mode): ramp-slow 42413 frames on vocal = the
  integral of 1/ρ(t) over the voiced track ✓ honest rate-following semantics (like varispeed,
  unlike the four preserving engines).

### MATERIAL RESPONSE [MEASURED]
* **Voice-like (vocal):** the home turf — f0 exact, formants preserved, the character the
  family is famous for.
* **Bass/pluck (harmonic, decaying):** f0 exact (219.6/439.2) — works beyond pure voice.
* **Drum (tracker-unvoiced):** complete passthrough — every metric identical across all
  transforms (see above).
* **Sine (event-free):** pitch mode does not shift (proven); the output carries sub-harmonic
  AM and heavy comb (10–12 dB) — the degenerate case, documented not hidden.
* **Noise/dense:** unvoiced → passthrough in pitch mode (the dense polyphony gets gated by the
  voicing decision — the classic "destroys polyphony" now measured as a BYPASS, not
  destruction).
* Stereo: shared mark schedule (channel-0 analysis), per-channel identical synthesis.

### ARTIFACT SIGNATURE [MEASURED]
1. **Downshift AM (pitch mode):** vocal −12 am 0.94 vs +12's 0.20 — the Hann-shape survives
   at s>2P overlap-thin spacings (the documented PSOLA downshift amplitude family, now
   measured).
2. **Grain-rate comb on the degenerate sine** (10–12 dB).
3. **No transient damage on passthrough material** (the drum's onsets survive byte-identically
   — the voicing gate protects them).
4. **Sharpness on shifted vocals:** −10.8 dB (≈ the input's own) — the pulse re-spacing keeps
   attacks coherent on the home-turf material.
5. rms follows the mode: pitch-mode +12 rms 0.42 (duration-halved steady region + the raw-OLA
   level family), −12 rms 1.26.

### SOUND-DESIGN CHARACTER (Phase G)
**COULD THIS SOUND BE USEFUL EVEN IF IT IS NOT HI-FI PITCH SHIFTING? — YES; it is the most
behaviourally distinctive candidate so far:**
* **a voice-gated transformer** — material the analysis accepts is transformed with preserved
  formants; everything else passes through untouched (a deterministic, analysable gate —
  rhythmic material keeps its attacks while a sung line shifts underneath);
* **the formant-preserving pitch move** (the "chipmunk-free" character) — a capability NO
  current engine has;
* **duration follows 1/β̄** (the varispeed-inverse semantics on the voiced spans);
* the downshift AM family (−12 am 0.94) is a strong, repeatable "underwater/reverberant"
  character knob.
Labels: *voice-locked, formant-true, envelope-following, gated-bypass, resonant-on-downshift*.

### IMPLEMENTATION COMPLEXITY
Moderate: ~600 lines including the mark machinery, both modes, the window cache and the
block-streaming state (three real defects found and fixed on the way — see above; the
mark-schedule semantics is where the subtlety lives).

### CPU / MEMORY CHARACTERISTICS
Synthesis RTF 0.001; analysis RTF ≈ 0.18 (the pYIN tracker — reused, not reimplemented).
Memory: O(4·P_max + blocks) sliding windows + the mark list + the window cache (~100 KB at
defaults). [MEASURED]

### REUSABLE PITCH LAB INFRASTRUCTURE
The clean-room pYIN tracker (the analysis backbone — direct reuse), §7 resampler (stretch
mode), the measurement layer. The PSOLA mark/synthesis core is the base for FD-PSOLA (Phase D
subclasses it through the transformGrain hook).

### ARCHITECTURAL FIT
The synthesis stage fits the PitchEngine contract 1:1 (allocation-free, block-streamed,
latency-declared). The analysis pre-pass is the honest research shape; a production engine
would need the streaming-tracker decision (documented as the realtime cost/latency driver).
The duration-changing pitch mode would be a new DurationBehaviour (rate-following, like
varispeed) — expressible in the existing registry capabilities without architectural change.

### LICENSING [VERIFIED-PAPER/TASK-3]
Moulines & Charpentier 1990 concept unencumbered. No Praat (GPL), no sannawag (MIT but
Praat-derived), no maxrmorrison (GPL-3) code read or linked — clean-room from the published
math; the DAFx M-files educational-only, not used.

### CANDIDATE CLASSIFICATION (preliminary)
**2 — EXPERIMENTAL PRODUCT ENGINE / CREATIVE-CHARACTER ENGINE** (upgraded from the old
"Benchmark (voice-only)" verdict). The old verdict's premise — voice-only operation is a
limitation — INVERTS under the sound-design lens: the voicing gate, formant preservation and
per-period control are behaviours absent from all five production engines, measured and
deterministic. The stretch mode additionally provides a clean conventional PSOLA shifter
(f0 exact on tonal material, rms 1.00) as a by-product.

### RECOMMENDED NEXT STEP
FD-PSOLA (Phase D) on this machinery: the grain-spectral envelope-decoupling hook is already
in place; the key question is whether the FD variant's formant control adds anything over the
ALREADY formant-preserving pitch mode (the measured 538-vs-653 centroid drift suggests the
envelope control is imperfect — FD-PSOLA's raison d'être).

### KNOWN LIMITATIONS
* The pitch mode is duration-changing (by design, per the published formulation) — the
  duration-preserving variant is the stretch mode (with formant shift).
* The mark refinement is minimal (zero-crossing); a true epoch detector (energy/peak-picking)
  would improve phase alignment on noisy voice — documented future refinement.
* The pure-sine degenerate case (no shift in pitch mode) is inherent to the mechanism, not a
  defect; the selftest documents it (the F0 net asserts the vocal only; identity exactness
  still covers every material).
* Analysis is offline (the pre-pass contract extension); a streaming tracker is a production
  decision with a measured ~0.18 RTF + 53 ms lookahead cost.

### OPEN QUESTIONS
* The 538-vs-653 centroid drift on +12 (formants preserved imperfectly — the window-length
  trade-off?): would FD-PSOLA's explicit envelope control measure tighter? (Phase D answers
  this directly.)
* Does the voicing gate's boundary (the tracker's voicedProbability) create musically useful
  gating artifacts on mixed material (the dense corpus)? Owner listening decision.

---

## 4. PHASE D — FD-PSOLA (`proto.fdpsola`)

### NAME
proto.fdpsola — frequency-domain PSOLA: the TD-PSOLA machinery + per-grain spectral envelope
control (the MC90 FD variant / DAFx psolaF semantics).

### FAMILY
Pitch-synchronous + frequency-domain hybrid (grain-FFT; the PSOLA FD branch).

### CORE MECHANISM (frozen before coding; built ON the TD-PSOLA prototype through its
transformGrain hook — marks, modes, raw-OLA synthesis and block streaming inherited unchanged)
* Per VOICED grain (2P, Hann-windowed): forward r2c FFT (vendored pocketfft, persistent plans
  cached per distinct grain length at prepare), **spectral interpolation of the complex
  half-spectrum by the formant ratio γ: X'(k') = X(k'/γ)** (linear in re/im; zero beyond
  analysis Nyquist), c2r with 1/gLen, written back. The grain's spectrum — harmonic comb AND
  envelope — stretches by γ (the psolaF formant-factor semantics); the OUTPUT PITCH still
  comes from the mark re-spacing. Unvoiced grains: not transformed (the passthrough
  semantics). **γ = 1 (default): the transform is a no-op — FD-PSOLA is BIT-IDENTICAL to
  TD-PSOLA** [MEASURED: identical det-hashes on vocal identity/+12, harmstack +12, drum +12 —
  the superset property is exact, not approximate].

### TECHNICAL REALITY [MEASURED]
* **The pitch and formant axes are DECOUPLED — the distinct capability:**
  * **formant-only shifting at IDENTITY pitch** (a transformation NO existing engine has —
    every production engine either moves nothing at identity or couples the envelope to the
    ratio): vocal identity + γ=2.0: **f0 140.0 EXACT while centroid 653 → 1316.5 (2.02×)**,
    dom 700 → 1400; γ=0.5: centroid 269 (0.41×), dom 279.9, f0 140.0 exact.
  * **combined control:** vocal +7 + γ=2: f0 209.8 (exact ×1.5), centroid 1208.8;
    bass + γ: f0 exact at every γ, centroid 122→240.5 at γ=2.
  * In the +12 pitch mode, γ sweeps move the centroid measurably (591 → 443/1062/720 for
    γ=0.5/1.5/2.0) — **non-monotonically** (the stretched comb interacts with the re-spaced
    marks and the resampler: at γ=1.5 the second comb line dominates, γ=2.0 realigns) — the
    honest composite behaviour, documented.
* Identity exact at γ=1 (inherited COLA transparency); deterministic; 0 allocations in
  process/finish (plans/buffers all prepare-built); block-split invariance bit-identical.

**VERIFIED:** the decoupled control, the γ=1 bit-identity with TD-PSOLA, identity
transparency, determinism, allocation audit, block invariance.
**INFERRED:** the comb/envelope interaction explanation for the non-monotonic centroid
(the measurements are direct; the mechanism story is reasoning).
**UNPROVEN:** perceptual "naturalness" of the γ-moved formants (linear complex
interpolation shears phases — the metallic character hypothesis; committed renders for
owner listening).
**OPEN:** see below.

### REALTIME FEASIBILITY [MEASURED, local-evidence timing]
* RTF ≈ 0.002 at γ=1 (no-op path) — the transform adds ~2 FFTs per voiced grain: the measured
  worst block stays ≤ 0.03 ms at bs=1024; with the transform active the cost remains trivial
  (2·FFT(2P) per grain at per-mark cadence — ~80 MAC/frame at voice pitch).
* The analysis (pYIN, RTF ≈ 0.18) is inherited — the same realtime cost driver as TD-PSOLA.
* Same latency/flush contract as TD-PSOLA.

### DYNAMIC PITCH BEHAVIOUR [MEASURED]
Identical to TD-PSOLA (per-mark β re-locking — the transform does not touch scheduling);
γ is a static per-job parameter in this prototype (a per-mark γ curve would be a trivial
extension of the same hook — noted, not built: the minimal-prototype rule).

### MATERIAL RESPONSE [MEASURED]
* The formant control works on voice-like and harmonic material (vocal, bass, harmstack).
* Unvoiced material: passthrough (the γ does not touch drums/noise — consistent gating).
* The degenerate sine case is inherited (pitch mode does not re-space continuous tones);
  the γ axis still moves its spectrum (the interpolation applies to any voiced-classified
  grain).

### ARTIFACT SIGNATURE [MEASURED]
1. **Level follows γ** (the raw-OLA + interpolation energy family): vocal identity γ=0.5 →
   rms 0.42, γ=2.0 → rms 1.26; am 0.94 at γ=2 (the envelope-thin overlap family).
2. **Comb–envelope intermodulation** on the shifted pitch mode (the non-monotonic centroid
   above) — the stretched comb vs the re-spacing creates line-dominance changes (dom moves
   439→879→439 across γ=1/1.5/2 at +12).
3. The interpolation's phase shear (complex-linear) — the metallic character hypothesis,
   committed for listening.

### SOUND-DESIGN CHARACTER (Phase G)
**COULD THIS SOUND BE USEFUL EVEN IF IT IS NOT HI-FI PITCH SHIFTING? — YES; it is the
control-surface candidate:**
* **the formant-only axis** (identity pitch, moving vocal character — the "gender/size"
  transform, gated to voiced material by the PSOLA voicing decision);
* **decoupled pitch+formant** (any β with any γ — a 2-D character space none of the five
  engines or the other candidates expose);
* the composite comb intermodulation at extreme γ = a spectral "detune/crystal" family.
Labels: *formant-free, voice-locked (inherited), two-axis, metallic-sheen*.

### IMPLEMENTATION COMPLEXITY
Low ON TOP of TD-PSOLA: ~140 lines (the transform + plan cache). The inherited complexity
(the mark machinery) is the real cost — already built and validated.

### CPU / MEMORY CHARACTERISTICS
RTF ≈ 0.002–0.01 with the transform active (grain-FFT cadence); memory: TD-PSOLA's + the
plan cache and three pMax-sized scratch buffers (~50 KB). [MEASURED]

### REUSABLE PITCH LAB INFRASTRUCTURE
The TD-PSOLA core (the subclass), pocketfft (the vendored FFT — the same plan pattern as the
frozen PV engines), the window cache.

### ARCHITECTURAL FIT
Same as TD-PSOLA (the hook preserved the engine shape 1:1). The γ parameter is an
engine-level parameter in registry terms — no contract change.

### LICENSING [VERIFIED-PAPER/TASK-3]
The MC90 FD-PSOLA concept and the DAFx psolaF structure are published math (the M-files
educational-only — not used). pocketfft BSD-3 (the project's existing vendored dependency,
ORIGIN.toml-pinned). No external code.

### CANDIDATE CLASSIFICATION (preliminary)
**2 — EXPERIMENTAL PRODUCT ENGINE (control-surface lane)** — the old verdict was SKIP
("the L-D '99 engine supersedes it"). MEASURED ANSWER to the Phase-D critical question:
FD-PSOLA DOES provide a distinct processing character the PV family does not: **explicit,
decoupled formant control on a pitch-synchronous engine** (the PV family's envelope follows
the ratio or requires envelope-estimation surgery; the L-D-style spectral shift moves the
whole spectrum). The superset property (γ=1 ≡ TD-PSOLA bit-identically) makes it a strict
extension, not a duplicate.

### RECOMMENDED NEXT STEP
Transient-aware PV (Phase E) completes the candidate set; the cross-candidate report then
faces the FD-PSOLA-vs-TD-PSOLA product question (one engine with γ, or two engines — the
bit-identity makes "TD + optional γ" the natural single-engine shape).

### KNOWN LIMITATIONS
* γ is static per job in the prototype (per-mark γ curves are a trivial extension, not
  built — the minimal-prototype rule).
* The linear complex interpolation shears grain phases (character, not fidelity — measured
  as comb intermodulation; the committed renders carry it).
* The +12-mode γ centroid response is non-monotonic (the comb interaction — documented
  above; a pure-envelope (magnitude-only) variant would behave differently — noted as a
  one-line follow-up if product candidacy proceeds).

### OPEN QUESTIONS
* Does the magnitude-only interpolation (envelope from harmonic peaks, phases preserved)
  sound cleaner than the complex-linear variant? (One-parameter experiment; the current
  variant IS the psolaF semantics.)
* The γ level family (rms 0.42–1.26): normalise per-grain energy? (Honest raw behaviour now;
  a normalisation would change the AM character.)

---

## 5. PHASE E — Transient-Aware Phase Vocoder (`proto.pvtransient`)

### NAME
proto.pvtransient — classic PV (fixed-synthesis-grid formulation) + spectral-flux transient
detection + phase reset (Röbel 2003 / Rubber Band R2 "crisp" semantics).

### FAMILY
Spectral (the phase-vocoder family — an evolution of the classic lane, not the L-D
frequency-domain-shift lane).

### CORE MECHANISM (frozen before coding; built ON the OLA prototype's stretch machinery
through the modifyGrain content hook — block streaming, window-product normalisation, the
stretch grid and the §7 resample stage are inherited)
* Classic PV in the fixed-synthesis-grid form: frames at the fixed synthesis hop Hs = N/overlap
  (default N=2048, overlap 4 = 75%), analysis hop Ha_n = Hs/ρ_n (the OLA engine's own stretch
  grid). Per bin and channel:
  Δφ_k = princarg(φ_a(n) − φ_a(n−1) − 2πk·Ha_n/N); ω̂_k = 2πk/N + Δφ_k/Ha_n;
  φ_s_k(n) = φ_s_k(n−1) + ω̂_k·Hs. Grain resynthesised with |X_k| and φ_s (persistent
  pocketfft plan, the frozen PV engines' own pattern). Frame 0: φ_s := φ_a.
* **Transient detection (deterministic, causal):** flux(n) = Σ_k max(0, |X_k(n)| − |X_k(n−1)|)
  on channel 0; transient when flux > sensitivity × median(the previous 8 fluxes) (default
  sensitivity 2.5). **Phase reset at a transient frame:** φ_s_k := φ_a_k over the reset band —
  reset_mode "full" (all bins, default) | "band" (150 Hz–1 kHz, the Rubber Band "mixed"
  band-limited reset) | "off" (the classic-equivalence internal baseline).
* Per-channel phase propagation; the transient decision SHARED (channel 0) — coherent resets,
  classic per-bin drift elsewhere.

### TECHNICAL REALITY [MEASURED]
* **The reset WORKS — the Röbel claim, measured on the shared corpus:**
  * drum +12: onset sharpness **−11.76 dB (full reset) vs −15.11 dB (reset off)** — a
    **3.35 dB sharpening**; the production native.pv.classic measures −13.24 dB (between the
    two, consistent with its classic pipeline) [MEASURED-BASELINE].
  * impulse +12: sharpness **0.00 dB** — the impulse stays an impulse (all energy within
    ±30 frames) with 2/2 onsets preserved — vs the classic PV's window-scale smear.
  * **The resets also HALVE the sustained-material comb:** vocal +12 comb 23.0 dB vs
    native.pv.classic's 46.2 dB — the pulse-train flux triggers periodic re-initialisation
    that prevents the classic PV's accumulated phase-error combing [MEASURED — a finding
    beyond the Röbel claim's scope].
* **The reset trades spectral continuity for transient fidelity:** drum comb 4.2 dB (full)
  vs 2.6 dB (off) — the documented reset-discontinuity cost, measured.
* **band mode is the sharpest on drums (−7.61 dB) but with a post-onset dip (tail −2.63 dB)**
  — the low band keeps continuity while the mids reset: a third, distinct character.
* Pitch behaviour: vocal +12 dom 1400.1 (−0.001 st EXACT), f0 280.0; sine ±12 dom err
  −0.04/−0.28 st; identity exact (0.00 st, rms 1.00). Conventional formant behaviour
  (centroid 1305.8 = ×2 — the classic full-spectrum shift).
* Deterministic (bit-identical double-runs); 0 allocations in process/finish; block-split
  invariance bit-identical; 15 resets on the drum material (4 macro-onsets + the noise-burst
  flux spikes — the detector's honest sensitivity).

**VERIFIED:** the reset's transient sharpening, the impulse preservation, the comb reduction
on pulse-train material, the reset/comb trade, pitch behaviour, determinism, audits.
**INFERRED:** the "periodic resets prevent phase-error accumulation" explanation for the
vocal comb reduction (the measurement is direct; the mechanism story is reasoning).
**UNPROVEN:** perceptual crispness vs Rubber Band's production detector (the compound
percussive/HF/silence curve — more sophisticated than this flux median; not built here).
**OPEN:** see below.

### REALTIME FEASIBILITY [MEASURED, local-evidence timing]
* RTF ≈ **0.07–0.09** at 48 kHz mono (2 FFTs per frame per channel + the flux pass — the
  extra forward pass for the shared detection); worst block ≤ 0.52× budget at bs=128 (the
  reversal curve's ratio transitions), comfortable ≥0.29× elsewhere; 0 allocations.
* Latency: the classic PV window scale + 8 synthesis hops of flux-median lookahead
  (declared: base N/2+Hs+2K+64 + 8·Hs = 9280 frames @ defaults ≈ 193 ms — the honest cost of
  the causal median; a shorter median (4 frames) or a causal percentile would trade
  detection stability for latency — documented, not silently shortened).

### DYNAMIC PITCH BEHAVIOUR [MEASURED]
Hop-rate control (per synthesis frame — 10.7 ms at 75%/2048 — the fastest PV-class lane);
ratio changes re-normalise the analysis hops (the IF estimates use the actual Ha_n); the
reversal/ramp records render 0-fault with duration preserved. Reset boundaries during ratio
changes: the phase re-initialisation restarts the propagation mid-glide — the documented
risk class (the reversal records' worst-case blocks land exactly there, measured 0.52×).

### MATERIAL RESPONSE [MEASURED]
* **Drum/impulse (transient-rich): the reset's home turf** — the sharpening above.
* **Vocal (pulse train):** the comb halving (above) + exact pitch — the resets ride the
  glottal pulses.
* **Sine/harmstack:** clean classic-PV behaviour (dom err ≤ 0.28 st, am 0.2 — the classic
  PV's residual AM family).
* **Noise:** conventional (comb 1.4-class); the flux detector stays quiet on stationary
  noise (resets 0 on the sine/sine-family materials — the median floor).
* Stereo: shared reset decisions, per-channel propagation — the resets are coherent, the
  sustained spans drift per-channel (the classic PV stereo behaviour).

### ARTIFACT SIGNATURE [MEASURED]
1. **Reset-transient interaction family:** the full reset's 3.35 dB sharpening + 1.6 dB comb
   cost; the band mode's sharp/dip character (−7.61/−2.63).
2. **Sensitivity knob (measured):** sens 1.5 → 20 resets (more, smaller resets — sharpness
   −10.50); default 2.5 → 15; window-4096 → 2 resets (the flux smooths over long windows —
   the window/reset interaction axis).
3. The inherited classic-PV residual AM (0.2 on tonal material) and the full-spectrum
   formant shift.

### SOUND-DESIGN CHARACTER (Phase G)
**COULD THIS SOUND BE USEFUL EVEN IF IT IS NOT HI-FI PITCH SHIFTING? — YES: it is the
transient-preservation lane:**
* **percussive material keeps its attacks under spectral pitch shifts** — the drum/impulse
  sharpening is the capability the classic PV family famously lacks (and the reason
  Rubber Band built it);
* **the band-limited reset is a THIRD character** (low-end continuity + mid reset — a
  "punch-preserving" variant measured distinct from both full and off);
* the periodic-reset comb halving on pulse-train material = a cleaner sustained voice lane
  than the classic PV at the same cost;
* the sensitivity knob is a genuine detector-rate control (measured reset counts 2–20 on
  the same material).
Labels: *transient-true, crisp-on-reset, band-punch, pulse-sweeping*.

### IMPLEMENTATION COMPLEXITY
Low ON TOP of the OLA machinery: ~280 lines (the PV pass + the detector + the modes). The
inherited stretch/resample machinery carries the block streaming.

### CPU / MEMORY CHARACTERISTICS
RTF 0.07–0.09 (2 FFTs/frame/channel + the flux pass); memory: the OLA engine's + per-channel
spectral state (3×bins doubles) + one plan (~60 KB at defaults). [MEASURED]

### REUSABLE PITCH LAB INFRASTRUCTURE
The OLA prototype's stretch core (the content hook), pocketfft (the frozen engines' own
pattern), the §7 resampler. **The Phase-E architecture question answered by construction:
the transient-aware treatment IS a reusable layer on the existing stretch machinery** (the
candidate was built as exactly that — a ~280-line content hook, not a standalone engine).

### ARCHITECTURAL FIT
Fits the existing spectral family's shape: as a standalone engine it would be a sibling of
pv.classic/pv.phaselocked with two extra parameters (reset_mode, sensitivity); as a LAYER it
is a content hook on the stretch machinery — the same code demonstrates both. No
architectural change required (the registry parameter surface would be the owner decision).

### LICENSING [VERIFIED-PAPER/TASK-3]
Röbel 2003 concept published; Rubber Band GPL — NOT used, not linked (the "mixed" band
limits 150 Hz–1 kHz are from its published header documentation — parameter values, not
code). Classic PV equations decades-old published math. pocketfft BSD-3 (vendored).

### CANDIDATE CLASSIFICATION (preliminary)
**1 — PRODUCT ENGINE CANDATE (the transient-preserving spectral lane)** — the old verdict
"Prototype" is CONFIRMED and elevated with measurements: the reset demonstrably buys
transient fidelity (+3.35 dB sharpness, impulse-exact) AND a cleaner sustained lane (comb
halved) at trivial cost, with a measured parameter surface (mode/sensitivity/window). The
Phase-E architecture question resolves to **B: a reusable phase-treatment layer** (built as
exactly that) with an optional standalone-engine packaging.

### RECOMMENDED NEXT STEP
The cross-candidate report (Phase F–R): consolidate the five candidates' evidence against
the five production engines, the classification table, and the final research decisions.

### KNOWN LIMITATIONS
* The flux-median detector is the minimal honest version (Röbel's optimal transient
  POSITION within the frame is not implemented — the reset lands at the frame centre; the
  window-4096 sweep shows the interaction).
* The 8-frame median costs 8·Hs lookahead (~85 ms at defaults) — a production design would
  tune the detection window (documented trade).
* The sustained-tone comb reduction is measured on ONE pulse-train material (the vocal) —
  the mechanism (periodic resets) predicts it generalises to quasi-periodic material;
  untested beyond the corpus.
* Per-channel phase propagation drifts inter-channel on sustained spans (the classic PV
  behaviour — the shared resets only pin the transient moments).

### OPEN QUESTIONS
* Would Röbel's transient-position optimisation (reset at the intra-frame transient peak)
  sharpen further? (A within-frame refinement — one more parameter, not built.)
* Does the phaselocked engine + resets stack (the L-D locking AND the reset)? — the natural
  follow-up experiment if the spectral lane proceeds (two independent phase treatments on
  the same grid).
