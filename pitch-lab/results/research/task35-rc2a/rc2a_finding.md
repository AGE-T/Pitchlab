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

## Root cause (refined after the tiling-code analysis; the fix checkpoint verifies)

TWO accounting facts compose, both verified in the source:

1. **The job's wet is capped at `wetLen` while the engine's D.4 production
   is not.** `feedJob` offers the engine the lane's free space
   (`outCap = min(free, pacing)`; splice-mode chains are never paced), but
   the append caps at `job.wetLen - job.wetWritten` — the wet produced
   beyond the window is DISCARDED. At beta < 1 the engine produces
   `span/beta` of wet for the span, so the job's window fills after
   consuming only the FIRST `beta x windowO` of content-bearing input; the
   span's remainder is consumed and discarded.
2. **The input advance per window is ratio-independent.** The tiling sets
   `advance = wetLen - seamX` (one field for both timelines) — the input
   skips `(advance - beta x (wetLen - seamX))` of content per window. At
   beta = 0.5 roughly half the input never reaches any window's wet; the
   output becomes fragments of 2x-slowed content on an average-1x grid —
   the input's own line structure dominates the spectrum (the measured 219
   Hz at -12 st), and the shifted fundamental collapses. The degradation
   ladder (beta 0.749 clean; 2/3 the 2nd harmonic; 0.5 the input pitch)
   matches the growing skip fraction.

The pitch-UP direction survives structurally: the ratio-scaled SHORTFALL
class there warps the duration/timbre (the overlapped content), but every
fragment still carries the correct `f0 x beta` pitch, so the measurable
pitch stays right — the asymmetry the measurements show. (The granular
branch is unaffected: its grain re-reading realizes the rate inside the
window differently — the artifact's granular -12 row measures 117 Hz vs the
expected 110, the documented downshift-envelope-edge limitation, NOT this
collapse.)

**The fix design (CP-5, a real checkpoint):** decouple the input advance
from the output advance for the Pitch-Synced splice grid —
`advance_in = ceil(beta x (wetLen - seamX))` at the chain's centre ratio
(the output grid stays 1x realtime — the host contract), size
`jobInputLen` to guarantee `>= wetLen` of wet across the envelope
(`engineLatIn + ceil(envMax x windowO)` — the existing no-holes guarantee),
and re-derive the emission mapping, the Task-31 retiring limits, the
frontier math, and the T-S-class pins that assume the single `advance`
field. The BETADOWN pitch gates get PROMOTED to hard assertions as the
fix's regression gate, with the direct engine as the content arbiter.

Confidence: HIGH on the phenomenon, the adapter-path exclusivity, and the
two accounting facts (both read in the source and consistent with every
measured row); MEDIUM on the exact fix shape (the CP-5 differential
measurements decide the final form).

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
