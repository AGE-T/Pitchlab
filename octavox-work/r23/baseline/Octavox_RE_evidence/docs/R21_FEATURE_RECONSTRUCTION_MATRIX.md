# R21 — Original UI-to-DSP Feature Reconstruction Matrix (UPDATED)

**Supersedes:** `R20_FEATURE_RECONSTRUCTION_MATRIX.md` (kept verbatim for
provenance). **New evidence this round:**
`evidence/analysis_round21/r21_static_findings.md` (the JUCE host-parameter
table + registration order + the 10 prioritized items) — every delta row below
cites it. Evidence classes: OBS / DER / UNK (unchanged legend; see the R20
matrix). Independent-implementation legend: IMPL / PARTIAL / NOT-IMPL.

## A. Voice engine

| # | UI element | R21 delta (evidence) | Class | Our implementation |
|---|---|---|---|---|
| A1 | Voice enable | unchanged from R20 | OBS | IMPL (Voice v Mute) |
| A2 | Voice level (dB) | unchanged (ognN −100..0 dB, default −3) | OBS | IMPL (linear [0,2]; delta documented) |
| A3 | **Voice pan** | **UPGRADED:** the JUCE parameter `Pan%d` (−1..+1, res 0.01, default 0) + the consumer = `stereomixer3` (8-in, gainN+panN, 25 ms smoothing); exact mix law in a sub-module (UNK) | OBS routing / UNK law | NOT-IMPL (documented; channel-mix layer is the roadmap) |
| A4 | Voice delay time | unchanged (dlyN 0..1250 ms; the R20 report's "init 280" note stands) | OBS | IMPL |
| A5 | **Voice delay feedback** | **CLOSED:** `fbkN` (−100..0 dB, default −100=off) → mute-gated switch → ÷20 → 10^x → per-voice LINEAR fb gain; SEPARATE from the global `mfbk` (0..100 %) | OBS | PARTIAL (global Feedback only; per-voice = roadmap with the now-known law) |
| A6 | Voice interval | unchanged (29-choice mux; the R21 harmony mapper implements the walk) | OBS | **IMPL (independent mapper)** — harmony module (not host-exposed) |
| A7 | Voice fine tune | `sht1` −2400..+2400 / `sht2..8` −1..+1 (the Q-10 inconsistency stands) | OBS | PARTIAL |

## B. Pitch control

| # | UI element | R21 delta | Class | Ours |
|---|---|---|---|---|
| B1 | MIDI Control | parameter OBS (`MIDI Control` + `Voice Select`); **no MIDI modules in the DSP graph** — JUCE-side routing, UNK | OBS param / UNK routing | NOT-IMPL (roadmap R22) |
| B2 | **Pitch Tracking** | **CLOSED + CORRECTED:** `ptrk` (0..1, default ON) → port 30 (+0x4510); OFF ⇒ correction=1.0, interval=0 ⇒ **all voices run at UNITY rate** (not "frozen spacing" — the R20 wording corrected) | OBS | PARTIAL (no host switch; the unity-rate law = our parked/unity behaviour) |
| B3 | **Instrument** | **CLOSED:** `inst` (0..12, default 0) selects ANTI-ALIAS cutoff tables (`MaxCut`/`CutFreq`/`CutMultiplier`/`AntiAliasOnOff`, 16 entries, 13 reachable) — NOT tracker profiles (R20's hypothesis corrected) | OBS | NOT-IMPL (our anti-aliasing is fixed) |
| B4 | **Low Note = "Minimum Pitch"** | **CLOSED:** JUCE integer param (default 0x18=24) + `minp` module: **48 choices C0..B3 (16.35..246.9 Hz), default C2 = 65.4 Hz** → tracker search base (T+0x5F70=x, T+0x5F74=x+2) | OBS | **IMPL (configurable)** — Config::f0_min (default 60 Hz, validated [20,200]); the R21 battery measures the trade-off; default NOT changed |
| B5 | Detected pitch | unchanged | DER | IMPL (diagnostic API) |
| B6 | **Crossfade** | **UPGRADED to OBS at both ends:** JUCE `Crossfade` → `xfad` module (0..100 ms, default 10) → engine record `xfadetime` → the grain-commit blend (R12 chain); internal rate build DER | OBS | IMPL (config-level `commit_crossfade_seconds`; exposing = roadmap) |
| B7 | **Randomize** | **CONFIRMED + producer CLOSED:** `rdep` (0..200, default 50) → modamt1..8; producer chain `rdm noise` → S&H → bounded slew → mod1..8; f = F·(rdep/2400) composes into `rate_v = 4·P(f)·ratio·correction` | OBS | NOT-IMPL (roadmap; the law is now fully specified) |

## C. Harmony & tuning system

| # | R21 delta | Class | Ours |
|---|---|---|---|
| C1..C7 | unchanged evidence; **the independent harmony mapper is now IMPLEMENTED + verified (545 vectors)** per `docs/R21_HARMONY_MAPPER_SPEC.md` | OBS tables/rules | **IMPL (library-level, not host-exposed)** |

## D. Delay & sync

| # | R21 delta | Class | Ours |
|---|---|---|---|
| D1..D4 | **Tempo/Session/Meter parameters OBS** (`Tempo Enable`, `Session Tempo Enable`, `Meter Numerator`, `Meter Denominator`, `Beat Loop`, `Beat%d`) + state chunk tags `tmpo/mtrn/mtrd`; **NO DSP-side tempo modules** — the conversion is host-side (UNK) | OBS params / UNK conversion | NOT-IMPL (roadmap) |
| D5 | Loop Delay = `Loop Delay` (JUCE) / `fbkd` ("fback dly" 0..5000 ms) → the feedback delay tap | OBS | PARTIAL |
| D6 | Global feedback = `mfbk` (0..100 %) | OBS | IMPL (Feedback id 4) |
| D7 | per-voice fb — see A5 (now CLOSED) | OBS | NOT-IMPL |

## E. Mix / monitor / state

| # | R21 delta | Class | Ours |
|---|---|---|---|
| E1 | **Input/Output Level OBS:** `Input Level` (igan −96..+12 dB), `Output Level` (ogan −96..+24 dB) | OBS | NOT-IMPL (roadmap: trivial additive) |
| E2 | unchanged | OBS | PARTIAL |
| E3 | Wet/Dry Mix = `mmix` (0..100 %, default 100) | OBS | IMPL |
| E4/E5 | **Snapshot chunk system OBS:** `Local Snapshots`, `Invalid Snapshot Chunk`, tags `snap`/`pastate`/`pluginID`/`pluginVer`/`MAP_END_KEY`; 16-slot/current-global/compare/revert semantics UNK (JUCE-side) | OBS chunks / UNK semantics | NOT-IMPL (host programs) |
| E8 | Load/Save = the chunk system above | OBS | PARTIAL (OVR1 blob) |
| E9 | **Live Mode CLOSED:** `Live Mode Enable` → `iswitch6/7` + `c_switch` → **+40 samples on every per-voice delayamt when live** | OBS | NOT-IMPL (a one-line preset of existing config: delay compensation) |
| E10 | Mix Lock parameter OBS (`Mix Lock Enable`); semantics UNK | OBS param / UNK semantics | N/A |

## Corrected R20 statements (this round)

1. **B2 wording:** tracking-off = "all voices at unity rate" (correction=1,
   interval=0), not "freeze the last committed spacing".
2. **B3 hypothesis:** Instrument = anti-alias cutoff profiles, not tracker
   profiles.
3. **A5:** per-voice feedback EXISTS (the R20 "no static evidence" verdict is
   superseded by the record-level closure).
4. **The R20 "real-material" corpora are SYNTHETIC** (material-like generators,
   not recordings) — all documentation corrected this round; the R20 results
   remain valid as algorithm tests under the synthetic category.
5. **The R20 dual-mono comment:** the default per-voice base delays are
   DISTINCT (256+32·v), not equal — corrected in r20_dualmono.cpp + the R21
   stereo matrix measures both axes explicitly.

## Updated roadmap

* R22-a: host-expose the harmony mapper (parameter-ID allocation review; ids
  0–37 untouchable), the configurable f0_min, and the per-voice feedback.
* R22-b: Randomize (the rate-walk producer; fully specified by P7).
* R22-c: host-sync delays (the Beat/tempo conversion — needs the host-side
  arithmetic, currently UNK).
* R22-d: Crossfade as a host parameter (the xfad law is now two-ends OBS).
* Later: the pan channel-mix layer (needs the stereomixer3 sub-module law).
