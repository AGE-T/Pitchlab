# R22 — Original UI-to-DSP Feature Reconstruction Matrix (UPDATED)

**Supersedes:** `R21_FEATURE_RECONSTRUCTION_MATRIX.md` (kept verbatim for
provenance). **New evidence this round:**
`evidence/analysis_round22/r22_static_findings.md` (the Randomize polarity
resolution, the global-feedback topology trace, the monoSumWetChans
equal-power data point). Evidence classes: OBS / DER / UNK (legend
unchanged). Independent-implementation legend: IMPL / PARTIAL / NOT-IMPL.
**This round's headline on OUR side:** the host surface grew to 74
parameters (ids 38..73 appended) — pan, per-voice feedback and the harmony
mapper are now IMPL at the HOST level, not just library-level.

## A. Voice engine

| # | UI element | R22 delta (evidence) | Class | Our implementation |
|---|---|---|---|---|
| A1 | Voice enable | unchanged | OBS | IMPL (Voice v Mute) |
| A2 | Voice level (dB) | unchanged (ognN −100..0 dB, default −3) | OBS | IMPL (linear [0,2]; delta documented) |
| A3 | **Voice pan** | **NEW DATA POINT:** `monoSumWetChans` (record [357]) = the equal-power pair **0.707/0.707** mono fold — adjacent evidence for an equal-power family; the `stereomixer3` pan law itself remains UNK (sub-module) | OBS routing + fold / **UNK law** | **IMPL (R22, host-level)** — Voice N Pan (ids 38..45, [−1,+1] default 0): OUR documented law √2·cos/√2·sin, unity-centred (bit-exact legacy), +3.01 dB hard pan, constant total power; the original's law NOT claimed |
| A4 | Voice delay time | unchanged | OBS | IMPL |
| A5 | **Voice delay feedback** | per-voice chain confirmed with module ADDRESSES (`c_divide18..25` ÷20 → `c_power9..16` 10^x); the ROUTING target of the linear gains (the wet-path filters `c_fltr_a*`) re-confirmed | OBS | **IMPL (R22, host-level)** — Voice N Feedback (ids 46..53, −100..0 dB default −100): our independent one-sample-register topology, mute-gated, ±8 bus rail (the original's routing NOT claimed) |
| A6 | Voice interval | unchanged (29-choice mux) | OBS | **IMPL (R22, host-level)** — Voice N Harmony Interval (ids 58..65, 0..28 default 14) via the mapper |
| A7 | Voice fine tune | unchanged (sht ranges, the Q-10 inconsistency stands) | OBS | **IMPL (R22, host-level)** — Voice N Fine Tune (ids 66..73, ±50 ¢; the original's ±2400/±1 ¢ domains are NOT adopted — documented independent choice) |

## B. Pitch control

| # | UI element | R22 delta | Class | Ours |
|---|---|---|---|---|
| B1 | MIDI Control | unchanged (negative result stands) | OBS param / UNK routing | NOT-IMPL |
| B2 | Pitch Tracking | unchanged (R21 closure stands) | OBS | PARTIAL (unity-rate law = our parked/unity behaviour) |
| B3 | Instrument | unchanged (anti-alias tables; R21 closure stands) | OBS | NOT-IMPL |
| B4 | **Minimum Pitch** | unchanged evidence (48 choices C0..B3, default C2); the knob→lag conversion arithmetic still UNK. Note: the original's C0 floor (≈16.35 Hz) is below our validated f0_min domain [20,200] — a documented independent-implementation difference | OBS | **IMPL + UPGRADED (R22)**: the FULL path now follows f0_min (the spacing gate links to the detector floor; default bit-exact); `octavox_r22_minpitch` measures the 10-point matrix end-to-end; `spacingGateHz()` is the detectable/shiftable API line |
| B5 | Detected pitch | unchanged | DER | IMPL (diagnostic API) |
| B6 | Crossfade | unchanged (two-ends OBS) | OBS | IMPL (config-level; host exposure = roadmap) |
| B7 | **Randomize** | **RESOLVED (the polarity contradiction):** the modulation is ONE-SIDED UPWARD — cents ∈ [0,+200], ratio multiplier ∈ [1,+12.24 %] (the R21 "±0..8.33%" wording CORRECTED, correction-log C-R22-1; R9/R10/R11 findings stand) | OBS (closed static chain) | NOT-IMPL (roadmap; the corrected law is fully specified) |

## C. Harmony & tuning system

| # | R22 delta | Class | Ours |
|---|---|---|---|
| C1..C7 | unchanged evidence (5 temperament tables, 19 scale rows, the walk laws — the R21 mapper's 545-vector verification stands) | OBS tables/rules | **IMPL (R22, HOST-EXPOSED)** — Harmony Mapper arm (id 54), Key (55, 17 spellings), Scale (56, the 19 PATTERN ROWS — the original's choice-level scal-mux quirk incl. hungarian stays preserved in the library's mapChoice, documented one API level away), Temperament (57, 5); armed ratios verified over ALL 19 rows × 5 temperaments × 29 intervals; state v3 serialises the block with v1/v2 migration |

## D. Delay & sync

| # | R22 delta | Class | Ours |
|---|---|---|---|
| D1..D4 | unchanged (host-side conversion UNK) | OBS params / UNK | NOT-IMPL |
| D5 | Loop Delay = `fbkd` (0..5000 ms) — **R22: its consumer is traced**: the feedback path's crossfaded delay tap (`delay_xf`, maxdelay 2400, xfadetime 20) via `c_minmax` (vs 50; unit semantics UNK) | OBS | PARTIAL |
| D6 | **Global feedback topology traced (R22-3):** `mfbk` (0..100 %) scales per-CHANNEL taps of the POST-PAN stereo wet mix; each path = softclip → crossfaded fbkd delay → high-cut/subtract cleanup → recirculation | OBS | IMPL (Feedback id 4, wet-path-only, no softclip — documented independent topology; the original's saturating feedback path is NOT claimed) |
| D7 | per-voice fb — see A5 | OBS | **IMPL (R22)** |

## E. Mix / monitor / state

| # | R22 delta | Class | Ours |
|---|---|---|---|
| E1 | unchanged | OBS | NOT-IMPL (roadmap) |
| E3 | Wet/Dry | OBS | IMPL |
| E4/E5 | snapshots unchanged (UNK semantics) | OBS chunks / UNK | NOT-IMPL (host programs) |
| E8 | Load/Save | OBS | **IMPL + v3** — the OVR1 blob v3 (+36 extension doubles) with v1/v2 migration (inert extension; restored legacy sessions behave exactly as saved — verified bit-exact) |
| E9 | Live Mode (+40 samples/voice) | OBS | NOT-IMPL (one-line preset; roadmap) |

## Corrections this round

1. **C-R22-1 (B7):** the R21 static findings' P7(e) "±0..8.33% rate
   modulation" is superseded — the modulation is one-sided upward (see
   `evidence/analysis_round22/r22_static_findings.md` §R22-1 for the full
   re-verified chain with addresses).
2. **Version-string correction:** the R21 findings' "16.1.0-R21" vs the
   shipped "16.0.0" string — logged; R22 ships "16.2.0".
3. (Additions, not corrections: C-R22-2 the monoSumWetChans equal-power
   fold observation; C-R22-3 the global feedback topology trace.)

## Updated roadmap

* R23-a: Randomize (the producer; the corrected one-sided law is fully
  specified — noise → |·| → S&H → slew → f → quartic → rate multiplier).
* R23-b: host-sync delays (needs the host-side tempo arithmetic, UNK).
* R23-c: Crossfade as a host parameter (the xfad law is two-ends OBS).
* R23-d: Live Mode (the +40-sample compensation preset).
* R23-e: f0_min host exposure (a parameter review; the R22 measurements
  support the case — 120/120 shiftable at the open floor, default
  unchanged pending a product decision).
* Later: the stereomixer3 pan coefficient law (needs the sub-module
  dissection).
