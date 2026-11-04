# OCTAVOX R20 — VST3 Parameter Semantics (P4)

The 38-parameter host surface (ids 0..37; `octavox_params.h`) — the ACTUAL
DSP semantics, the interaction laws, and the relationship to the original
Eventide Octavox's parameter set (from the R20-P1 feature matrix).

## 1. The surface and its exact semantics

| IDs | Parameter | Domain | Default | Actual DSP effect |
|---|---|---|---|---|
| 0 | Bypass | 0/1 | 0 | Output crossfades to the latency-aligned dry signal over `bypass_seconds` (10 ms). While bypassed the wet engine keeps advancing (state transparency; R19 read-suppression skips only the interpolation reads when fb==0 — bit-identical by test). |
| 1 | Pitch Shift | [-24,+24] st, step 0.01 | 0 | The GLOBAL ratio r = 2^(st/12) applied to every voice that has no non-zero offset override (see §2). Re-applied atomically with the voice overrides at every change (the `retargetRatios()` law). |
| 2 | Voices | 1..8 integer | 1 | The ACTIVE voice count (`active_voices_`): voices >= n are not processed by the audio loop (structurally gated by `proc_hi`; empirically gated by the `active_voices_processing_scope` test: inactive voices' wrap counters freeze). Entering/leaving voices ramp over `voice_fade_seconds` (12 ms; the R19 click fix). |
| 3 | Spread | 0/1 | 1 | The MUSICAL harmony map: when on, voice v (0-based) gets the extra offset kSpreadSemitones[v] = {0, +7, +4, +12, -5, -12, +3, -3} semitones ON TOP of the global shift (and any per-voice offset). When off, every voice without an offset override runs at the global ratio. |
| 4 | Feedback | [0, 0.9] | 0 | Global wet feedback gain applied to the summed wet output (one iteration into the input sum per sample; the [-0.9, 0.9] core domain is intentionally surfaced only on [0, 0.9]). Read suppression never engages when fb != 0. |
| 5 | Wet/Dry | [0, 1] | 1 | Linear amplitude mix: out = w*wet + (1-w)*dry, applied at BLOCK boundaries (the documented R15 contract; an instant full-range jump produces a step of up to w*(|wet|+|dry|) — bounded in R19, NOT slewed: the endpoints are contractually bit-exact). |
| 6+4v | Voice v+1 Pitch Offset | [-24,+24] st, step 0.01 | 0 | Per-voice OVERRIDE (see §2). |
| 7+4v | Voice v+1 Gain | [0, 2] | 1 | Per-voice linear gain with 15 ms ramps (mute/unmute and gain changes ride the same ramp machinery). |
| 8+4v | Voice v+1 Mute | 0/1 | 0 | Per-voice mute (gain ramp to 0). The voice still processes (state stays warm) — gain 0, not bypassed. |
| 9+4v | Voice v+1 Base Delay | [0, 200] ms | (256+32v)/48 ms | Per-voice extra delay in SAMPLES = ms*sr/1000, clamped to [1, 249847] (the core requires delay > 0 STRICTLY: a 0 ms parameter maps to 1 sample). Applied by crossfade over `delay_crossfade_seconds` (12 ms; the R19 click fix). |

## 2. The ratio-composition law (Global Shift / Offset / Spread / Voices)

The EFFECTIVE ratio of voice v is:

    r_v = 2^( (PitchShift + SpreadOffset_v + Offset_v) / 12 )

evaluated with the per-voice override machinery in `applyVoiceRatio` /
`retargetRatios`:

* `SpreadOffset_v` participates ONLY while Spread is on AND no non-zero
  per-voice Offset exists for v — because a non-zero Offset OVERRIDE is
  applied via `setVoiceRatio(v, ...)` which REPLACES the voice's target
  ratio entirely (the core contract: `setRatio`/`setSpread` clear all
  overrides, then every NON-ZERO offset is re-applied on top).
* **Zero-offset semantics (documented explicitly, unchanged since R15):**
  a voice with Offset == 0 is NOT "override with spread disabled" — it
  falls back to the global ratio (including the spread offset when Spread
  is on). There is no way to express "this voice: global ratio, ignore
  spread" other than setting its offset to the NEGATION of the spread
  offset. This is a consequence of the core's clear-on-setRatio contract
  and is deliberately NOT changed (changing it would alter the R15 state
  semantics without a compatibility path).
* Setting Spread re-applies the whole surface (`retargetRatios`), so
  toggling Spread re-derives every voice's target atomically.
* Voices >= the Voices parameter keep their configured surface values
  (state) but are not processed until activated.

## 3. Interaction with voice count and mute

* `Voices` gates PROCESSING scope, not parameter validity: all 8 voices'
  parameters remain host-visible and restorable regardless of the count.
* `Mute` is a gain-domain gate (the voice's delay line keeps advancing);
  the R19 voice-fade ramps make (un)muting click-free.
* Deactivating a voice (Voices count reduction) ramps it out over 12 ms
  and then freezes its grain state (wraps stop; `active_voices_processing_scope`).

## 4. Naming collision with the original (documented, NOT renamed)

The R20-P1 feature matrix (evidence/analysis_round20/r20_feature_matrix.json)
established statically that the ORIGINAL Octavox's "spread" UI concept is a
STEREO WIDTH control (`inspread`/`outspread` nets, both default 0, feeding
input/output stereo-spread modules) — NOT a harmony map.

OUR "Spread" parameter is the musical harmony table (the R12-reconstructed
spread table). The names collide; the semantics do not overlap.

**Decision (R20-P4):** the parameter keeps its name and semantics. A rename
would break saved-session compatibility for every existing user project
(VST3 restores by id + value; a re-titled parameter with changed semantics
would silently alter mixes). The divergence is DOCUMENTED here and in the
feature matrix instead: our host surface reflects OUR DSP (the task-book
requirement: "The host-visible parameter surface must reflect actual DSP
behaviour"), and the original's stereo-spread feature is recorded as a
roadmap item (it is NOT implemented in our engine).

## 5. Original-vs-ours surface mapping (from the R20-P1 matrix)

The original's registered parameter families (statically extracted this
round from `0x1801A0A00..0x1801A3400`): tmpo/stmp/tune/scal/ptrk/inst/minp/
xfad/igan/ogan/mmix/mfbk/fbkd/beal/rdep/live/klck/mlck/vsel/mctl/mtrn/mtrd
+ per-voice Level%d/Feedback%d/Pan%d/Delay%d/Shift%d/Interval%d/Enable%d/
Beat%d. OUR 38-parameter surface implements a SUBSET of the original's
voice-engine concepts (enable/level/delay/shift) with different laws
(dB vs linear; musical intervals vs raw semitones) and implements global
shift/feedback/wet-dry/bypass/voices as first-class parameters. The gaps
(per-voice feedback, per-voice pan, musical intervals, tempo/meter sync,
key/scale/tune, snapshots, live mode, locks, randomize, MIDI control) are
roadmap items in the R20 feature matrix, NOT silently-differing semantics:
every parameter we DO expose matches the DSP we DO run.
