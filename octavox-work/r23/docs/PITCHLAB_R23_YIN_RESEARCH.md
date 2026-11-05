# PITCHLAB R23 — YIN Research (master synthesis)

**This document summarizes the round's research programme and indexes the
specialist documents.** Full evidence:
`evidence/analysis_round23/` (JSON batteries + agent research reports
g3a1/g3a2/g3b1/g3b2a with fetched-URL appendices).

---

## 1. Research questions → answers

| Question (brief §1–§5, §17–§26) | Answer | Evidence |
|---|---|---|
| What exactly is canonical YIN? | Fixed-W difference, CMNDF from lag 1, threshold-on-first-local-min with global fallback, raw-d parabola, Step 6 temporal stage | math derivation doc; validated to ≤2e-9 vs independent vectors (reference battery 12/12) |
| Do the five sources implement it faithfully? | None implements Step 6; Rust CMNDF off-by-one + sign-broken clarity; ABSounds 0.4 threshold no-fallback; hyuncat constant-energy assumption; tonalux misnamed CMNDF; librosa faithful except centered frames | source comparison doc + agent reports |
| Is "FFT YIN" one thing? | NO: yinfast = exact acceleration (proven 3.7e-9); yinfft = spectrally weighted, a different estimator | taxonomy doc + equivalence proof |
| Window/support: does W ≥ T_max matter? | Measured both ways: +≤0.09 c accuracy, −6…−11 c tracking — the harmonizer use case selects short-memory tracking | benchmark §2 window study |
| Which detector for production? | Keep the R22 Decimated default (0.02 c med, 361–427 ns/sample flat to 192k, best tracking); FullRate = legacy reference; Hybrid retained; canonical/FFT = research references | decision record §1 |
| How should confidence work? | periodicity score (1 − cmndf_min) with named states, never a probability; six-level separation implemented | confidence/voicing study |
| Step 6 for real time? | Unproven benefit on corpus; causal control layer selected; bounded-look-ahead revisit path documented | Step-6 study |
| Decimation filter adequate? | −18.6 dB at Nyquist measured; no accuracy deficit on corpus; fc 0.25 option documented | filter study |
| The four user symptoms? | crackle mechanism measured (commit jumps under movement, crossfade mitigates; default protected); stereo bit-exact native+VST3-module (cause must be non-identical input or host); NaN/Inf propagation REPRODUCED and FIXED; builds differ 3.8–58× CPU but agree within audibility on steady material | artifact analysis |
| R22 measurement harness trustworthy? | Repaired: 58 boundary-artifact cells removed, derived tone-free lock criterion, verdicts reproduce 141/18/31/170 with 0 deltas | R22 measurement repair doc |

## 2. Specialist document index

1. `PITCHLAB_R23_YIN_SOURCE_COMPARISON.md` — the five sources + paper
2. `PITCHLAB_R23_YIN_VARIANT_TAXONOMY.md` — yin/yinfast/yinfft/pYIN/MPM/SWIPE
3. `PITCHLAB_R23_YIN_MATHEMATICAL_DERIVATION.md` — §§1–8 derivations
4. `PITCHLAB_R23_YIN_WINDOW_LAG_LATENCY_STUDY.md` — full geometry tables
5. `PITCHLAB_R23_DECIMATION_FILTER_STUDY.md` — the anti-alias chain
6. `PITCHLAB_R23_YIN_CONFIDENCE_VOICING_STUDY.md` — scores vs probabilities
7. `PITCHLAB_R23_YIN_STEP6_REALTIME_STUDY.md` — the temporal trade-off
8. `PITCHLAB_R23_CURRENT_ENGINE_YIN_AUDIT.md` — Gate 2 forensics
9. `PITCHLAB_R23_R22_MEASUREMENT_REPAIR.md` — Gate 1 record
10. `PITCHLAB_R23_TRACKING_CONTROL_SPEC.md` — the control layer
11. `PITCHLAB_R23_IMPLEMENTATION_SPEC.md` — the shipped deltas
12. `PITCHLAB_R23_ARTIFACT_ANALYSIS.md` — the four symptoms
13. `PITCHLAB_R23_BENCHMARK_PROTOCOL_AND_METRICS.md` — metric definitions
14. `PITCHLAB_R23_BENCHMARK_RESULTS.md` — all numbers
15. `PITCHLAB_R23_DECISION_RECORD.md` — selections & open items

## 3. Research provenance & licensing discipline

* Every external claim is [OBS] from fetched code/pages (URL appendices in
  the agent evidence files) or [DER] from our own measurements; the paper's
  exact wording remains [UNK] (mirrors blocked) — the mathematics was
  reconstructed and cross-validated by implementation instead.
* Zero AGPL/GPL code entered the proprietary tree; aubio/ABSounds were
  described only; the hyuncat repo is license-less (all-rights-reserved
  treatment); the Rust crate's behaviour was independently verified before
  any trust (and failed two checks).
* All corpus results are SYNTHETIC, labelled as such; no real-audio
  accuracy claim is made anywhere in R23 (dataset access pending).

## 4. The one-paragraph conclusion

The delivered R22 detector architecture survives a full canonical-YIN
cross-examination: its legacy deviations (truncated CMNDF, short-m
support, 0.20 threshold, no Step 6) are measured to cost ≤0.33 cents
accuracy while BUYING 10× CPU flatness across sample rates and superior
moving-pitch tracking; its real weaknesses found this round were the
missing voicing decision (now gated by the Pitch Control Layer), the
missing estimate telemetry (now exposed as an observer side-channel), and
one genuine defect — non-finite input propagation — which is fixed. The
four user symptoms are reproduced/explained at the levels this sandbox can
reach, with the DAW level honestly open.
