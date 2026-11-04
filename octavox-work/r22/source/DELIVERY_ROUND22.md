# DELIVERY ROUND 22

**Product identity:** PitchLab Harmonizer (independent). The research
archive name (Octavox_RE_evidence) refers to the studied binary only.

**Entry point for the reader:** `PITCHLAB_ROUND22_FINDINGS.md` (the round
verdicts + all measured results). The round's static-RE detail lives in
`evidence/analysis_round22/r22_static_findings.md`.

## What is in this round

1. **P1 Minimum Pitch, end to end**: the engine spacing gate follows the
   detector floor (`min(fmin, f0_min)`; default bit-exact); the 8-level
   path audit; `octavox_r22_minpitch` — 180 cases (10 frequency points ×
   3 detector modes × 5 intervals + the default-floor control): 120
   shiftable / 45 unity-parked / 15 below-floor / **0 FAIL**. The
   detectable-vs-shiftable distinction is an API (`spacingGateHz()`).
2. **P2 real-audio harness v2**: `octavox_r22_realaudio` — enforced
   categories (synthetic/recorded/annotated), smoothed trajectories +
   conversion timestamps, final-processed-audio cross-mode comparison,
   per-mode WAV output, peak RSS. Personal recordings are never embedded
   without approval.
3. **P3 per-voice pan + feedback**: the documented independent laws
   (√2·cos/sin unity-centred pan; one-sample per-voice feedback registers
   into the common ring, mute-gated, ±8.0 bus rail) + the 14-gate
   acceptance battery.
4. **P4 the harmony mapper host integration**: 36 appended parameters
   (ids 38..73), state blob v3 with v1/v2 migration (inert extension —
   restored legacy sessions behave exactly as saved), armed ratios ==
   mapRow over all 19 scale rows × 5 temperaments × 29 intervals;
   disarmed legacy bit-exact. `octavox_r22_state`: 10/10.
5. **P5 static RE**: the Randomize polarity contradiction RESOLVED (the
   R21 "±" wording corrected; one-sided upward stands); the global
   feedback topology fully traced (post-pan taps, softclip, crossfaded
   fbkd delay, high-cut cleanup); the monoSumWetChans 0.707/0.707
   equal-power data point.
6. **P6 regression**: 96/96 core (bit-exact defaults), 21/21 wrapper,
   10/10 state, 14/14 panfb, R21 stereo/dual-mono and R20 tools
   reproduced, ASan clean on the new engine paths.
7. **P7 packaging**: regenerated manifest, fresh-extraction verification,
   archive SHA-256. **No Windows binaries this round** (no cross-compiler
   in the environment; disclosed; sources are build-ready).

## Gates

96/96 core · 21/21 wrapper · 10/10 state · 14/14 panfb · 180/180
minpitch rows · 0 stereo transparency violations. Logs:
`evidence/analysis_round22/logs/`.

## Reproducing the builds (this environment)

```
cd native && bash tools/r22_build.sh          # core + tests + CLIs
cd native && bash tools/r22_vst3_build.sh     # wrapper + state batteries
```

(the pinned VST3 SDK at `../../../vst3sdk` is required for the second
script; it is NOT redistributed).
