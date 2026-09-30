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
*(WSOLA, TD-PSOLA, FD-PSOLA, Transient-aware PV sections follow in the next checkpoints.)*
