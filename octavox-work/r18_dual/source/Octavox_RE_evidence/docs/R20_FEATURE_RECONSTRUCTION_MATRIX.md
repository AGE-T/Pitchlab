# OCTAVOX R20 — Original-UI → Internal-DSP Feature Reconstruction Matrix

**Round:** 20 (P1) · **Date:** 2026-10-31/11-01 · **Author:** agent (RE track)
**Scope:** the ORIGINAL Eventide Octavox (`Ox.vst3`) UI surface, reconstructed
from the user-supplied original-UI screenshots (feature discovery only — no
visual reproduction) and mapped to (a) the static RE evidence chain
(rounds 8–19: OBS/DER/UNK with exact instruction addresses), and (b) OUR
independent implementation's status for each feature.

**Evidence-class legend** (the three-class separation, unchanged):
* **OBS** — static observation of the original binary (disassembly/records).
* **DER** — derived with high confidence from OBS chains; not directly observed.
* **UNK** — unobservable statically; runtime observation of the original is
  out of scope by contract (the original binary is never executed).

**Independent-implementation legend:**
* **IMPL** — implemented in our own engine (host-visible or core).
* **PARTIAL** — implemented with different semantics/structure (documented).
* **NOT-IMPL** — deliberately not implemented (roadmap item or exclusion).

---

## A. Voice engine (per voice, 8 voices in the original)

| # | UI element | Kind | Evidence (address) | Class | Our implementation |
|---|-----------|------|--------------------|-------|--------------------|
| A1 | Voice enable (on/off per voice) | DSP state | `setVoices`/voice-active loop in prepare FUN_1800E2030; voice state block `+0x4158[v]` (R12 FINDING 2) | OBS | **IMPL** — `Voice v Mute` (id 8+4v, bool) + `voice_fade_seconds` ramp (R19-c). Semantics documented: mute = ramped gain 0, DSP state stays warm. |
| A2 | Voice level (dB) | DSP param | per-voice gain in the voice sample loop; our gain-after-norm documented vs. the target's per-voice coefficient tables `dsp+0xA3CC..0xA41C` (R18 P2) | OBS (tables), DER (mix law) | **IMPL** — `Voice v Gain` (id 7+4v, [0,2] linear). **Delta:** ours is linear [0,2] not dB; documented in P4 semantics doc. |
| A3 | Voice pan | DSP param | output-stage mode flag `[obj+0x8C]` producer at `0x1800DC295` (R18 P2, CLOSED): per-class coefficient tables select the channel mix law; the class's setParam dispatcher (method table `0x180409F60`, dispatch via jump table `0x18013D22C` at `0x18013BE40`) | OBS (structure) | **NOT-IMPL (documented)** — our engine is structurally channel-isolated (one engine per channel; P3 report). The target's pan is a per-voice cross-channel mix inside ONE engine. Our host surface has no pan parameter; stereo behaviour is the documented dual-mono/stereo law (r20_dualmono.json). Roadmap: needs a channel-mix layer, not a per-voice parameter bolt-on. |
| A4 | Voice delay time | DSP param | per-voice delay `+0x4140[v]` (init 280; R12 FINDING 3c) consumed by the re-arm compare `0x1800E2777` vs. `pos − delay < 8.000000000000007` (const `0x180412428`) | OBS | **IMPL** — `Voice v Base Delay` (id 9+4v, ms [0,200] → samples, `msToBaseDelaySamples` law). Our base-delay default table is `256+32v` samples (vs. target init 280 for voice 0 — **not harmonised**; see roadmap R21). Crossfaded changes (R19-d). |
| A5 | Voice delay feedback | DSP param | feedback coefficient in the wet path (global in our reading of the rounds; the per-voice feedback consumer was not isolated as a separate field) | UNK (per-voice split) | **PARTIAL** — one GLOBAL `Feedback` (id 4, [0,0.9]). Per-voice feedback not implemented; no static evidence of a per-voice fb field. Roadmap: re-scan the wet-path arithmetic for per-voice taps. |
| A6 | Voice interval (musical) | DSP param (harmony) | the 29-interval mux tables (record [46], `add=0x18013A8D0`, `int table N`/`int table switch N` records [46]/[47], R18 P4.1) compose the per-voice ratio through the key-ratio law `keyRatio = exp(±cents·ln2/1200)` (R18 P4.2, CLOSED) | OBS (rule), DER (intent) | **PARTIAL** — `Voice v Pitch Offset` (id 6+4v, semitones ±24, continuous). Ours is a continuous semitone offset with spread table; the target's is a DIATONIC mux vocabulary (see C-matrix). Semantics doc: P4. |
| A7 | Voice fine tune (cents) | DSP param | same mux/key-ratio composition (the cents input of the key-ratio law, R18 P4.2) | OBS | **PARTIAL** — the semitone grid (0.01 st = 1 cent steps) gives cent-level resolution; the target's cent input composes with the diatonic snap (ours does not snap). |

## B. Pitch control

| # | UI element | Kind | Evidence | Class | Ours |
|---|-----------|------|----------|-------|------|
| B1 | MIDI Control (mode) | host/UI | no static evidence isolated (MIDI event path not dissected) | UNK | **NOT-IMPL** — excluded by scope (no MIDI surface in our VST3 wrapper). Roadmap R22 candidate. |
| B2 | Pitch Tracking (on/off) | DSP param | the tracking flag `+0x4510` gates the record-ring consumption: `if tracking==0: correction=1.0, interval=0` (R12 FINDING 4, `0x1800E25DB` onward) | OBS | **PARTIAL** — no host parameter. The engine always tracks; the R12 law's tracking-off behaviour (correction=1, interval=0 — i.e. freeze the last committed spacing) is our `S` persistence (spacing holds without commits), not a switchable parameter. |
| B3 | Instrument (profile) | DSP param | tracker window/threshold tables per instrument were not isolated; the tracker deep analysis (OCTAVOX_TRACKER_DEEP_ANALYSIS.md) documents the single profile | UNK | **NOT-IMPL** — single tracker profile (60–1200 Hz, 0.20 threshold, 64-sample cadence). Roadmap: profile table if evidence lands. |
| B4 | Low Note (range floor) | DSP param | the validity gate constants 48.0 (`0x180412688`) and 480.0 (`0x180412730`) in the record consumption (R12 FINDING 4): v = clamp(rec·2^-11, ≥48; =480 if ~0) | OBS | **IMPL (fixed)** — our gate is [60,1200] Hz (documented core law; the 48/480 pair is the target's range clamp on the RECORD value, same role). Not host-adjustable in either. |
| B5 | Detected pitch (display) | UI readout | tracker output → record ring → the engine's spacing consumer; the display side is UI-only (no static UI code dissected) | DER (value path OBS) | **NOT-IMPL (host)** — no UI. The equivalent value is `lastEstimateHz()` (core diagnostic API, used by our test tooling). |
| B6 | **Crossfade** | DSP param | **THE candidate law:** the two-tap blend `out = tapA + (tapA − tapB)·state, state ∈ (−1,0]` settling one-shot per grain (R12 FINDING 4, `0x1800E2812–0x1800E2A5D`, mix at `0x1800E29FD–0x1800E2A5D`); tapA at `recidx+wdelta`, tapB at `interval` | OBS (mechanism), DER (parameter binding) | **CONCLUSION (R20):** the original UI's Crossfade control sets the LENGTH of this grain-commit blend (the one-shot fade width between the old-law tap and the new-law tap after a spacing commit). **It is NOT our internal 8 ms commit_crossfade_seconds' default — ours is the same MECHANISM with a fixed default; the target exposes its width as a user control.** Our R19 `commit_crossfade_seconds` is the exact analogue (config-gated, 0 = bit-exact legacy). **Status: IMPL (core, config-level), NOT host-exposed** — exposing it is a P4 roadmap decision (new host parameter requires a surface change → compatibility review). |
| B7 | **Randomize** | DSP param | the record-ring's correction field consumer: `rate_v = 4·P(f)·ratio·correction` with `P(0)=0.25` (rounds 9–11); correction = `record_ring[base+3]` (R12 FINDING 4); the random-modulation source feeding `P(f)` was characterised in the tracker deep analysis (f = the random walk) | OBS (consumer), DER (producer) | **FINDING (R20):** Randomize's consumer is the RATE modulation path (the `P(f)` jitter on the per-voice advance rate — a pitch/delay micro-modulation), NOT a voice-layout or delay randomiser. **Ours: NOT-IMPL** (our rate is deterministic `4·0.25·ratio·correction`-equivalent without the random term; the R12 core reproduced `P(0)=0.25` exactly, i.e. randomize OFF). Roadmap: a seeded random walk on the voice advance rate, amplitude = the Randomize control. |

## C. Harmony & tuning system

| # | UI element | Kind | Evidence | Class | Ours |
|---|-----------|------|----------|-------|------|
| C1 | Key | DSP param (harmony) | key/user/ionian maps + the key-ratio law `keyRatio = exp(±cents·ln2/1200)`, ascent/descent asymmetry CLOSED (R17 §1, R18 P4.2) | OBS | **NOT-IMPL (harmony)** — our `Spread` table is the only musical mapping (2 fixed patterns). The full key/scale engine is the R17/R18 Q-D reconstruction (OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md); implementing it is roadmap R21+ (a scale/key → per-voice ratio mapper on top of the existing per-voice override API — the core already has `setVoiceRatio`, the consumer is ready). |
| C2 | Tuning (reference) | DSP param | the 5 temperament tables at `0x1803BB690` (R17 §1 CLOSED: mux/record-link delivery, engine composition with interval snapping) | OBS | **NOT-IMPL** — 12-TET fixed (semitone grid). The 5-table set + delivery law is fully documented for the R21 implementation. |
| C3 | Scale | DSP param | the 19-row scale-pattern table at `0x1803BB780` (row dump `petk.i32s(0x1803BB780+row·0x30,12)`, R18 P4.1: chromatic choice 14/15 mux, the authored irregularities incl. the `hungarian` out-of-range choice 16 — UNK runtime) | OBS | **NOT-IMPL** (same roadmap as C1). |
| C4 | Musical intervals (per voice) | DSP param | the 29-interval mux tables, the walk-collapsing rule `{0,2,3,4,6} mod 7`, the +1-mux-unit bump rule (R18 P4.1, all OBS) | OBS | **NOT-IMPL** (see A6/C1). |
| C5 | Cent fine-tune (global) | DSP param | the cents input of the key-ratio law (R18 P4.2) | OBS | **PARTIAL** — `Pitch Shift` (id 1) is continuous ±24 st at 0.01 st = 1-cent steps; equivalent resolution, no diatonic composition. |
| C6 | Diatonic mapping | DSP param | the whole C1/C3/C4 chain (scale degree → interval → mux → keyRatio → per-voice ratio table) | DER (composition of OBS links) | **NOT-IMPL** — roadmap R21 (the mapper architecture is already written up in R18 P4.1's closed rule set). |
| C7 | Temperament | DSP param | the 5-table set (C2) + the mux delivery | OBS | **NOT-IMPL** (with C2). |
| C8 | Key Lock | UI state | no static evidence (UI-only) | UNK | **N/A** — UI-state management, out of our wrapper's scope (no GUI this round). |

## D. Delay & sync

| # | UI element | Kind | Evidence | Class | Ours |
|---|-----------|------|----------|-------|------|
| D1 | Sync mode (free/tempo) | host sync | no static evidence isolated (host-sync plumbing is framework-side) | UNK | **NOT-IMPL** — delays are free-running ms. Host tempo sync = roadmap R21 (a `msToBaseDelaySamples` wrapper that reads the host's tempo/meter at parameter-set time; the DSP consumer is unchanged). |
| D2 | Session sync | host sync | as D1 | UNK | **NOT-IMPL** (same). |
| D3 | Tempo (BPM) | host sync | as D1 | UNK | **NOT-IMPL** (same). |
| D4 | Meter (beats/bar) | host sync | as D1 | UNK | **NOT-IMPL** (same). |
| D5 | Loop Delay (length) | DSP param | the record/loop ring at engine+0x98 (4096 floats, `recidx` `+0x42f8`, `wdelta` `+0xa4fc`, mask 0xFFF — R12 FINDING 4) is the target's delay-feedback loop store | OBS | **PARTIAL** — our wet path has the grain ring + feedback (global); no user-facing loop-delay length. The target's loop ring size is fixed 4096 in the dissection (not parameterised in evidence). |
| D6 | Global feedback | DSP param | feedback coefficient in the wet sum (isolated in rounds 6–8 as the coefficient consumed per block; see OCTAVOX_CORE_INTERNALS.md) | OBS | **IMPL** — `Feedback` (id 4, [0,0.9], subset of the core's (−1,1) domain — the positive-only host-facing subset documented in VST3_WRAPPER.md). |
| D7 | Per-voice feedback | DSP param | see A5 | UNK | **NOT-IMPL** (see A5). |

## E. Mix / monitor / state management

| # | UI element | Kind | Evidence | Class | Ours |
|---|-----------|------|----------|-------|------|
| E1 | Input gain + meter | UI/DSP | no static evidence isolated for the input stage | UNK | **NOT-IMPL** — no input gain (the wrapper is unity-in). Metering is out of scope (no GUI). |
| E2 | Output gain + meter | UI/DSP | output-stage mode/coefficients class (`[obj+0x8C]` producer, R18 P2) is the output mix law; the metering side is UI | OBS (mix law), UNK (meter) | **PARTIAL** — per-voice gains + voice-count normalisation implement the mix; no master output-gain parameter (roadmap: one linear output scale in the output stage — trivially additive). |
| E3 | Dry/Wet | DSP param | the output-stage blend (rounds 6–8: the dry-tap latency-aligned copy + wet sum; our core's `wet` law is the reconstructed analogue) | OBS | **IMPL** — `Wet/Dry` (id 5, [0,1], block-boundary mix law, click-scan bounded; bypass is the separate `Bypass` id 0). |
| E4 | Current vs Global state | UI state | the state-blob architecture (our reading: the target's preset/state files) — no static evidence of a dual-state model | UNK | **NOT-IMPL (as UI concept)** — our state model is the single authoritative 'OVR1' blob (full Config + 38-value surface; R20-P0 hardened the default/restore contract). |
| E5 | 16 Snapshot slots | UI state (persistent) | no static evidence (preset storage is resource-side, not in the dissected DSP path) | UNK | **NOT-IMPL** — host-side preset/BPM state is the VST3 host's job; our wrapper exposes the full state blob for host programs. Snapshot banks = future GUI work. |
| E6 | Snapshot Revert | UI command | as E5 | UNK | **NOT-IMPL** (same). |
| E7 | Preset Carousel | UI command | as E5 | UNK | **NOT-IMPL** (same). |
| E8 | Load/Save/Compare | UI command | as E5 | UNK | **PARTIAL** — Load/Save = the host-facing `getState`/`setState` ('OVR1' v1/v2 blob, cross-instance verified in R20-P0 case 19). Compare = no. |
| E9 | Live Mode | UI/DSP | no static evidence | UNK | **N/A** — no GUI. (Candidate semantic: crossfade-times → 0 and engagement snap — our config already exposes exactly these knobs, so a Live Mode is a one-line preset of the existing config, roadmap R21.) |
| E10 | Mix Lock | UI state | no static evidence | UNK | **N/A** — no GUI. |

---

## Crossfade & Randomize — the R20 verdicts (the specification's two named
investigations)

**Crossfade (B6).** The evidence chain is unambiguous: the original engine's
only crossfade is the GRAIN-COMMIT blend — the one-shot two-tap fade
(`out = tapA + (tapA − tapB)·state`, `state ∈ (−1,0]`, settling to single-tap)
that fires when a spacing commit changes the read law (R12 FINDING 4,
addresses above). There is no second, separate crossfade process in the
dissected DSP (no grain-conversion or voice-conversion blend exists — voice
gains are coefficient ramps, the tracker is a value producer, not a blender).
The UI control's parameter binding is DER (the width value's consumer is the
`state` step rate — not directly observed as a host-parameter write), so:
**Crossfade = the width of the grain-commit blend.** Our R19
`commit_crossfade_seconds` is the same mechanism; the R20 delta is the
DOCUMENTATION of this equivalence and the decision to keep it config-level
(not host-exposed) until a GUI round — exposing it would add host parameter
39 and needs the compatibility review of P4.

**Randomize (B7).** Consumer tracked to the instruction level: the
`correction` field from the record ring (`record_ring[base+3]`, R12 FINDING 4)
composes into `rate_v = 4·P(f)·ratio·correction`, and the `P(f)` term is the
random modulation walk (rounds 9–11 tracker analysis; `P(0) = 0.25` exactly —
the zero-randomize value, which our R12 core reproduced bit-exactly).
**Randomize = the amplitude of the random walk on the per-voice advance rate**
(a pitch/delay micro-modulation). It is NOT a voice-layout randomiser, NOT a
delay randomiser, NOT a pan/spread randomiser — those have no consumer in the
dissected dataflow. Ours: not implemented (deterministic advance); the
implementation path is a seeded LCG walk scaled by a control value, summed
into the rate's `P(f)` term. Roadmap R21 with its own accuracy battery.

## Priority roadmap produced by this matrix

* **R21-a (high value, evidence complete):** the harmony/tuning engine — key +
  scale + 19-row pattern table + 29-interval mux + temperament tables + the
  key-ratio law, all mapped onto the existing `setVoiceRatio` per-voice
  override API (the core consumer needs no change). Every table and rule is
  already dumped and CLOSED (R17 §1, R18 P4.1/P4.2).
* **R21-b:** Randomize (the rate-walk producer) + its accuracy battery.
* **R21-c:** host-sync delays (tempo/meter → ms at parameter-set time).
* **R21-d:** expose `commit_crossfade_seconds` (Crossfade) as a host parameter
  (surface change → the P4 compatibility review).
* **R22:** MIDI Control surface (needs a MIDI event path design).
* **Deferred (GUI rounds):** pan (channel-mix layer), snapshots/carousel,
  Live Mode, Mix Lock, meters — all UI-layer features with no static DSP
  evidence.
