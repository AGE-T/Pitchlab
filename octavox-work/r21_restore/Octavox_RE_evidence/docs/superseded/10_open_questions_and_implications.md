# 10 — Open Questions, Next Steps, and Design Implications

This document collects what is **not** yet resolved, how to resolve it, and what the confirmed architecture implies for designing a *new* pitch-shifting engine (design guidance only; no implementation).

---

## 1. Open questions (prioritized)

| # | Question | Why it matters | How to resolve | Current status |
|---|---|---|---|---|
| Q1 | Engine's internal time/pitch reconstruction mechanism | Core of any comparison; OLA/granular/delay-line judgment | **Resolved at read level** (batch 2): shifter = variable-rate Lagrange-interpolated reads from the 2.4 s ring (256-phase, 6-tap kernel; quartic rate driver; exponential glide multipliers); no OLA/granular layer found in the traced workers. Residual: detector + xfade site (→ Q4, Q14) | CLOSED (read level) |
| Q2 | How exactly `shiftN`/`pcentN`/`modN`/`delayamtN` are consumed (units, signs) | Interval-table units (7/octave grid), sign conventions, glide behavior | `pcentN` → ratio pair (setparam 16) and per-voice binding blocks known (doc 05 §4); the `shiftN` quantization arithmetic is not located | PARTIAL |
| Q3 | Where `add`/`multiply4..11` (voice loop) and wet/dry multiplies are evaluated | Explains execution model ("ghost" nodes) and loop sample-accuracy | Trace `moddelay` process `0x1800D4710` for a fused sum/self-feedback; or find the config-time fold in `[02]` | UNK (localized) |
| Q4 | Internal analysis/detection algorithm | Pitch tracker is half the system | Not located in `[01]`/`[05]`; next: decompile `0x18013BBC0` (`+0x4518` sub-object). NB: `0x1800E7F70` turned out to be the modfilter_t ctor, not the tracker | UNK (candidate located) |
| Q5 | Meaning of `steps=12`, `hysteresis=10`, `correctrate=1`, `mincorrect=0`, `tuning=0`, `quantization=0` in code | Tracking quality parameters | Partially answered: `quantization` = tonality/timbre branch selector; `intervalglide`/`gatelevel`/`pitchtracking` formulas verified (doc 06 §1); the remaining five have mechanics but no located consumer | PARTIAL |
| Q6 | `userscale1..12` (all 0) — reserved feature? | Hidden user-scale support in engine | **Mostly resolved**: ≥8 valid entries required, else ionian fallback; marker convention 99/100+n (doc 06 §5) | MOSTLY CLOSED |
| Q7 | Exact units/derivation of `int table` values (duplicates, 7/octave span) | Interval accuracy for a replacement | Once `shiftN` semantics are known (Q2), recompute from engine arithmetic; plus new domain question: control-side `scal` literals vs engine row codes (Q16) | UNK/CONFLICT |
| Q8 | `add8` (rnd-depth combiner) has no consumer | Possibly a dormant feature (voice-1-only rnd depth path) | Search engine object for subscriptions; check mimo/miso variants | UNK |
| Q9 | `rdm a-c` transform (shares code with `LevMetrAtoC` `0x1801391F0`) | Random modulation feel | Decompile `0x1801391F0` | UNK |
| Q10 | `ouch` semantics beyond mono-wet summing (why mimo has `iswitch9`) | Channel-mode completeness | Compare mimo vs siso wet routing end-to-end | UNK/DER |
| Q11 | `dl` flag of `[05]` and module processes | Possibly transport/reset semantics | **Closed**: `dl ≠ 0` = first block of a run → sample-rate object (id 5) re-publish; not a declick selector | CLOSED |
| Q12 | `0x18013BBC0(+0x4518, 1)` called from engine prepare | Unknown engine subsystem (leading tracker candidate) | Decompile `0x18013BBC0` | UNK (prioritized) |
| Q13 | `dsp_t` setparam 6 constants (`0x12`, `x·0xF3`, `1/x`) | Unknown parameter (candidate: `tune`) | Stored at `+0xA448`/`+0xA450`/`+0xA44C`; correlate with config parser setting order | UNK |
| Q14 | Engine's exact latency | Needed for host compensation alignment | From Q4; cross-check with Live Mode 40-sample comp | UNK |
| Q15 | Per-voice directory pages (ids `0x15`/`0x16`) + restart pulse `0x17`: who consumes them, what triggers a page swap? | Voice-state transition mechanism; needed to reproduce transitions | Trace subscribers (wake bits) of ids `0x15–0x17`; page-select flag is `param_1[0x81]` byte in `[05]` | UNK |
| Q16 | Scale domain: `scal`-table literals vs engine row codes (`==14/15` comparators vs rows 12/13 = chromatic/whole) | Correct scale mapping in any reconstruction | Decompile the `scal` table module; or a runtime value trace | CONFLICT |

## 2. Confirmed facts that should shape a new engine (with evidence anchors)

1. **The product splits cleanly into tracker → voice matrix → post-processing.** The engine receives a *mono* feed (doc 04 §3) plus per-voice control signals; it returns 8 voices plus analysis products (`pitch`, `tonality`, …). [OBS]
2. **Pitch is expressed in cents relative to C4**, with a no-pitch sentinel and a dB-based magnitude gate; tonality is a 0..1-ish confidence family (id 9/11 products). [OBS: `0x1800E1520`]
3. **Control-rate smoothing is pervasive and explicit** (10/20/25 ms time constants; per-step glide ratios `2^(±1/(12x))`; crossfades up to 100 ms user-visible). [OBS]
4. **Per-voice loops create evolving repeats**: voice = shift → EQ → echo (≤1250 ms) with per-voice feedback up to 0 dB, all inside a far-feedback network (≤5 s, up to 100 %). [OBS]
5. **Anti-aliasing is a first-class subsystem**: instrument table selects EQ cuts + optional 2-section low-pass whose cutoff is derived from the largest shift; plus per-voice EQ tilt. [OBS]
6. **The engine core works on decimated/parallel analysis rates**: SR-class-dependent decimation filters (11/21/41 taps; ×2/×4/×8) and SR-class-dependent analysis sizes (4096/8192/16384) suggest analysis performed at a reduced, SR-normalized rate. [OBS/DER]
7. **Internal processing quantum is 4 samples** (SSE paths, 4-sample priming). [OBS/DER]
8. **Parameter changes propagate by flag bits** (lock-free publish) rather than parameter reads per sample. [OBS]
9. **The reconstruction core is a rate-modulated delay line with high-order fractional interpolation** — 256 phases × 6 taps (Lagrange kernel), rate = quartic polynomial of the modulated per-voice state, with exponential glide multipliers; no granular/OLA layer found in the traced workers. [OBS: `0x1800E2030`, table dump]
10. **Engine outputs form a subscriber contract**: 6 named outputs (`pitch`/`period`/`freq`/`amp`/`tonality`/`timbre`) plus shared product ids; publishing is change-gated and lock-free (`LOCK; *flag |= 1<<bit` per subscriber entry). [OBS: base ctor + `[05]`]
11. **Engine parameters are block-transported**: 13 binding blocks with notify bits + per-voice key blocks (same factory); parameter application happens at block boundaries. [OBS: config parser `0x1800E3350` + `[05]`]

## 3. Design implications for a new pitch engine (non-binding guidance)

These are conclusions a new design should *consider*, each anchored in an observation:

1. **Two products, not one**: a robust pitch/tonality tracker whose outputs are first-class (cents + confidence + gate) is architecturally central, not a side feature. Consumers (visualization, per-voice modulation, quantization) should be able to subscribe to tracker outputs directly. (Anchor: §2.2, doc 06)
2. **Make the time/pitch axis explicit**: Octavox bundles delay, echo, and feedback around each shifted voice; a new engine should expose *shift*, *time offset*, and *formant/length policy* as separate parameters rather than entangling them. (Anchor: doc 08 §1.2)
3. **Booleans and tables for anti-aliasing are not enough** — the `inst`-table approach hard-codes per-instrument AA decisions; a new engine should make aliasing policy automatic and signal-dependent (derive cutoff from actual shift *and* detected content). (Anchor: doc 08 §4)
4. **Smoothing must be designed, not sprinkled**: the observed 10/20/25 ms constants and per-step glide ratios form a coherent transition vocabulary; a new design should specify transition times as part of the DSP contract (and make them modulatable). (Anchor: doc 07)
5. **Live-mode latency policy**: the plug-in compensates live use with an *input* delay equal to its internal analysis latency + 40 samples on delay parameters. A new engine should measure and publish its latency instead of relying on a mode switch. (Anchor: doc 08 §5)
6. **Mono-core / stereo-post topology** is a legitimate and cheap structure for a voice-based shifter; it keeps shifter cost independent of channel count while allowing panned voices. (Anchor: doc 04 §3)
7. **Control-change notification by flag words** works well when parameter updates must reach the audio thread without locks; a new engine can adopt the pattern (with a documented seqlock-like discipline). (Anchor: doc 02 §2.3)
8. **Random modulation as a cross-coupled slew network** (each voice's slewed random value limits the next voice's slew) produces non-periodic modulation cheaply; worth reusing as a *modulation source design* concept. (Anchor: doc 08 §3)
9. **A delay-line rate-modulated core is sufficient for this feature set**: the analyzed product achieves its shifting with variable-rate interpolated reads (no granular/OLA layer found), so a new engine can adopt that core and spend its complexity budget on the tracker and transition policy instead. (Anchor: doc 07 §3, doc 09 D9)

## 4. Recommended next analytical steps

1. **Batch 2 is complete** (docs 05–10 updated). Remaining decompile targets, in priority order:
   - `0x18013BBC0` — the `+0x4518` sub-object (likely the detector; closes Q4/Q12).
   - the `scal` table module — resolves the scale-domain CONFLICT (Q16).
   - `0x1800D4710` (moddelay process) — closes Q3.
   - `0x1800EF810` / `0x1801391F0` (peak/level transforms) — closes Q9 and pins meter semantics.
2. **Wake-bit subscriber trace** for shared ids `0x12–0x1E` (who consumes the per-voice pages/pulse) — closes Q15 and firms up the engine↔kernel contract.
3. **Unit test the interval-table hypothesis**: once `shiftN` units are known (Q2), recompute the 29-entry table from first principles and verify the observed values (Q7).
4. Optional: **time a minimal runtime probe** in a host with loopback measurement to confirm latency (Q14) and the `tonM` id mapping — only if runtime validation becomes permitted; otherwise remain static.

## 5. Explicit non-goals for this phase

- No DSP implementation, prototyping, or code generation for the replacement engine (per project charter).
- No protection/authorization circumvention was performed or will be.
- No "feature exists because it is named so" claims; all naming not backed by record evidence is marked as an analyzer label (doc 01 §6).
