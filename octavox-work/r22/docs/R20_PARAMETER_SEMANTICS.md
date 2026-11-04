# OCTAVOX R20 — VST3 Parameter Semantics: the interaction laws (P4)

**Round:** 20 (P4) · **Status:** documentation of the SHIPPED behaviour
(the 38-parameter surface of `octavox_native_params.cpp`, unchanged this
round). This document closes the R20-P4 item: the actual interaction laws
between Global Pitch Shift, per-voice Pitch Offset, Spread, Voice Base
Delay, Voice count and Voice mute — including the naming conflict the
R20 specification flagged — so the host-visible surface provably reflects
the DSP behaviour.

## 1. The pitch-ratio composition law (one formula, whole surface)

For every voice `v` the engine consumes exactly ONE per-voice target ratio
`ratio_v` (the core's `setVoiceRatio` override; `octavox_native_processor.cpp`
`retargetRatios()` / `applyVoiceRatio()`):

```
ratio_v = GLOBAL · VOICE_OFFSET_v        (multiplicative, frequency domain)
GLOBAL        = 2^(PitchShift / 12)                  [param 1]
VOICE_OFFSET_v= 2^(Voice v Pitch Offset / 12)        [param 6+4v]
```

* **Voice Pitch Offset == 0 (plain default):** `ratio_v == GLOBAL` exactly —
  the voice follows the global shift verbatim (bit-exact at 0: `2^0 == 1`).
* **Voice Pitch Offset != 0:** the voice's ratio is the PRODUCT, i.e. the
  offset is RELATIVE to the global shift (a "+3 st" voice at global +2 st
  sounds +5 st). There is NO either/or: the offset NEVER *replaces* the
  global shift, and it NEVER *overrides* Spread (see §2 — it overrides the
  SPREAD TABLE only, not the global factor).
* The product is applied through the core's per-voice geometric glide
  (0.9995/sample), so both factors' changes ride the same glide — the
  composition is order-free and time-consistent.

## 2. Spread: what it is, what it interacts with, and the naming conflict

`Spread` (param 3, bool, default ON) — the core's MUSICAL SPREAD TABLE: when
more than one voice is active, the voices' pitch offsets are spread across a
fixed musical pattern instead of all unisons (the core's two patterns; see
`dsp.hpp` `Config::spread`). The exact interaction law (from
`retargetRatios()`):

1. `setSpread(on)` and `setRatio(GLOBAL)` each CLEAR all per-voice overrides
   (core contract — the spread table is laid down on top of the fresh global
   target).
2. The wrapper then RE-APPLIES every voice whose **Voice v Pitch Offset != 0**
   as `setVoiceRatio(v, GLOBAL · 2^(offset/12))`.

Net law, stated once:

| Spread | Voice offset == 0 | Voice offset != 0 |
|--------|-------------------|--------------------|
| ON  | voice sings the spread-table interval (relative to GLOBAL) | voice sings GLOBAL·2^(offset/12) — **the explicit offset displaces the spread-table slot** |
| OFF | voice sings GLOBAL (unison) | voice sings GLOBAL·2^(offset/12) |

**Consequence (documented, intended):** a non-zero Voice Pitch Offset takes
the voice OUT of the musical spread pattern (its slot is the overridden
ratio, not the table's interval). It does NOT switch Spread off globally and
does NOT modify other voices' slots.

**The naming conflict (R20-P4 item, resolved as documentation):** the
original Octavox's UI uses "Spread" for its stereo/spacing impression
(a spatial/pan-law control). OUR `Spread` parameter reuses the WORD for the
musical voice-interval table (a pitch law). The two are unrelated DSP
features. No silent rename or semantics change is made this round (the
38-parameter contract is frozen); the surface documents this explicitly:
`Spread` = "musical spread table on/off" (`ParamSpec` title domain,
`octavox_params.cpp`). The spatial-spread feature belongs to the
pan/channel-mix roadmap item (R20 feature matrix A3). A future GUI round may
retitle the host parameter (a metadata-only change, no ID/semantic change)
after the compatibility review.

## 3. Voice count, Voice mute, and the gain laws

* `Voices` (param 2, 1..8, default 1): sets the ACTIVE range `[0, n)`. The
  transition ramps each affected voice's effective gain over
  `voice_fade_seconds` (R19-c); the settled state is bit-identical to the
  legacy instant law. Deactivated voices keep DSP state warm (seamless
  re-entry).
* `Voice v Mute` (param 8+4v, bool): per-voice gain target 0 (ramped the same
  way). Mute is INDEPENDENT of the voice count: muting voice 2 of 3 does not
  change `Voices`, and the voice-count normalisation (1/√n law) still counts
  it. **Documented interaction:** a muted voice still consumes its spread
  slot / offset ratio (its DSP state advances); only its contribution is
  zero.
* `Voice v Gain` (param 7+4v, [0,2] linear, default 1): multiplies AFTER the
  voice-count normalisation (`final gain = norm · user gain`; `gain == 1` is
  bit-neutral).

## 4. Voice Base Delay: the ms-domain law and its rate dependence

`Voice v Base Delay` (param 9+4v, ms [0,200], default (256+32v)/48 ms) is a
TIME-domain control mapped to samples at the CURRENT host rate
(`msToBaseDelaySamples`: `ms·sr/1000`, clamped into [1, maxBaseDelaySamples] —
the core requires delay > 0 strictly, so 0 ms maps to 1 sample).

**Rate dependence (documented contract):** the same ms value yields
different SAMPLE delays at different rates (5.33 ms = 256 samples @48k, 512
@96k) — the audible delay TIME is preserved across rate changes; a saved
state restores the ms value verbatim and re-prepares at the host's rate.
Changes are crossfaded over `delay_crossfade_seconds` (R19-d).

**Interaction with pitch/offset:** none at the parameter level (the delay
law composes with the voice's grain phase/spacing inside the core — the
read position is base + phase-derived; a spacing commit crossfade can run
concurrently with a base-delay crossfade; both are bounded continuous
blends of the READ LAW, and their combination is commutative in the settled
state).

## 5. The remaining global interactions (bounded list)

* `Bypass` (param 0): output-level crossfade to the dry signal over
  `bypass_seconds`; while bypassed the wet engine keeps running (state
  transparency contract). No interaction with the pitch/delay parameters
  beyond that.
* `Wet/Dry` (param 5): block-boundary mix of the (latency-aligned) dry copy
  and the wet sum. The wet/dry jump at block boundaries is the documented
  bounded contract behaviour (R19 click-scan: max step 0.139).
* `Feedback` (param 4, [0,0.9]): global wet-path feedback coefficient
  (the host-facing subset of the core's (−1,1)). Composes with every voice's
  output (the loop is post-mix).

## 6. Host-surface ↔ DSP-state agreement (the R20-P0 gate)

The above laws are only a contract if the host-visible values and the engine
state start from the same 38 numbers. The R20-P0 fix + test group (wrapper
cases 16–20) now gate exactly this: fresh instance, after prepare,
save/restore, state-to-new-instance (both `IComponent::setState` and
`IEditController::setComponentState`), repeated initialize/terminate cycles —
all without moving a single control; plus the engine-bit-exactness proof
that the DSP actually runs the reported defaults. See
`OCTAVOX_ROUND20_FINDINGS.md` (P0 section) for the root cause and the
sensitivity proof.


---

# R21 ADDENDUM — the approved title corrections + help text (P0)

**Round:** 21 (P0) · **Changes:** TITLE-ONLY corrections to the shipped
38-parameter surface (VST3 hosts persist parameter IDS, not titles —
automation and state compatibility are unaffected; ids, ranges, steps,
defaults, flags, units, the state-blob layout and the DSP semantics are
byte-identical):

| id | was | now | reason |
|---|---|---|---|
| 3 | `Spread` / `Spread` | **`Harmony Map`** / `HarmMap` | it is a musical harmony-map switch (the per-voice spread-table intervals), NOT stereo widening — the R20 §2 naming conflict resolved per the R21 specification |
| 1 | `Pitch Shift` / `Pitch` | **`Global Pitch Shift`** / `Glob Pitch` | states the control's role in the §1 composition law (it transposes EVERY voice) |
| 6+4v | `Voice N Pitch Offset` / `VN Pitch` | **`Voice N Transpose`** / `VN Transp` | same law, the other factor |

**Help text (new, static, per-parameter):** `octavox_params.cpp`'s
`kParamHelp` table (exposed via `paramHelp(id)`) documents every parameter —
including the composition law `ratio_v = global_ratio * voice_ratio_v`
("semitones add, ratios multiply") and the Harmony Map semantics (ON: a
zero-Transpose voice follows the spread table — voice 2 = +7 st, voice 3 =
+12 st; a NONZERO Transpose displaces the voice's spread-table slot while
keeping the global transposition; OFF: every voice uses exactly its own
Transpose; stereo output remains strict dual-mono for identical L/R input
either way). Consumed by the validation kit's parameter dump, the wrapper
documentation and future GUI tooltip layers.

**Verification:** wrapper test case 21 (`identity_variant_authority_r21`):
the titles, the otherwise-unchanged specs, the non-empty help texts and the
vendor/variant identity are gated; 21/21 × 4 build variants.

**Cross-reference:** `docs/R21_DUALMONO_STEREO_CORRECTION.md` (the measured
stereo behaviour behind "NOT stereo widening") and the R21 findings'
"Identity changes" section.
