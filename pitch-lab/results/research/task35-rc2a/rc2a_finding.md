# RC-2a — Pitch-Synced beta<1 adapter-path content defect: CONFIRMED

**Task 35 continuation · the master recovery pack TASK E recheck · 2026-10-06**
**Status: CONFIRMED DEFECT on the canonical tree (post CP-3). The fix is the next implementation checkpoint.**

---

## Finding

The realtime adapter's Pitch-Synced path (the windowed-splice adaptation) at
beta < 1 (pitch DOWN) delivers wet content whose fundamental is progressively
weakened/absent as beta decreases, collapsing toward the input's own line
structure — while the DIRECT engine (one continuous offline job) is
contract-correct at the same ratios (the T-PSOLA D.4 pitch gates pass at
beta = 1/2, and the OD-6 length battery covers the 1/beta duration). The
defect is ADAPTER-PATH-ONLY and RATIO-GRADED.

## Status

CONFIRMED (measured; the reproduction is permanent: the differential harness
`TP-DIFF-BETADOWN` case).

## Evidence

Instrument: `tests/timepitch_differential_harness.cpp` (TP-DIFF-BETADOWN,
48 kHz / block 512, saw 220 Hz, 7 s drives, the Goertzel dominant of the
settled output region). Measurements on the canonical tree at `d36dcd9`:

| st | beta | dominant measured | expected (220·beta) | verdict |
|---|---|---|---|---|
| −5 | 0.749 | **165.0 Hz** | 164.8 Hz | CORRECT (within 4 Hz) |
| −7 | 0.667 | **295.0 Hz** | 146.8 Hz | DEFECT: the dominant is the shifted content's 2ND harmonic (2 × 146.8 ≈ 293.6) — the fundamental is weak/absent |
| −12 | 0.500 | **219.0 Hz** | 110.0 Hz | DEFECT: the dominant is the INPUT pitch — heavy content corruption |

The delivery accounting is CLEAN in all three rows (underruns 0, telemetry-dry
0, stalls 0, preparation failures 0, full-buffer render determinism exact) —
the defect is in the CONTENT, not the continuity: a zero-miss but wrongly
pitched wet. The artifact's audio-path lanes contain ZERO
`native.timepitch` windowed-splice records (verified against the committed
`audio-path-report.json`), so no prior gate ever covered this class — the
known audible failure coexisted with a green CI (the TASK K question,
answered YES for this class).

## Impact

Every Pitch-Synced / Pitch + Formant down-shift beyond ~ −5 st through the
realtime adapter produces audibly wrong content (the fundamental collapses,
the input's line structure leaks through) with NO diagnostic signal — the
fault counters stay zero. Down-shifts within the product's −48..+48 st range
are a first-class use case.

## Root cause (primary hypothesis; the fix checkpoint verifies)

`chainGeometry`'s Pitch-Synced splice branch sizes the job's input span as
`jobInputLen = engineLatIn + ceil(windowO × envMax) + margins` with
`worstEnv = max(1, envMax)` flooring the ratio term at 1. Under the D.4
rate-following semantics the engine consumes `beta × windowO` of input per
`windowO` of wet produced: at beta < 1 the span holds `(1 − beta) × windowO`
MORE input than the window's wet can hold, and the excess input is skipped at
each window boundary. The skip fraction grows as beta falls
(−5: 25%, −7: 33%, −12: 50%), which matches the measured degradation ladder
(the −5 row survives; −7/−12 collapse). The pitch-UP direction was never
affected because `max(1, envMax) = envMax` there — the span accidentally
carried the correct ratio scaling. The fix direction: scale the input span by
the CHAIN'S OWN centre-ratio envelope (both directions), re-deriving the
windowed-splice tiling accounting for beta < 1, with the direct engine as the
content arbiter and the full zero-miss/determinism gates re-run.

Confidence: HIGH on the phenomenon and its adapter-path exclusivity; MEDIUM on
the precise span-accounting mechanism (the fix checkpoint's differential
measurements decide).

## Required action

The RC-2a fix checkpoint (CP-5): the ratio-scaled input-span re-derivation +
the BETADOWN pitch gates PROMOTED to hard assertions + the full battery
(CTest 48+, the validator, the artifact comparator, the differential baseline
regeneration, CI).

## Owner decision required

NO (the pack mandates the recheck-then-re-derive path for RC-2a; this is a
content-correctness defect inside the frozen D.4/windowed-splice semantics,
not a semantic amendment — the frozen contract is being RESTORED, not
changed).
