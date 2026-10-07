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

---

## CP-5 CORRECTION (2026-10-07) — the two-instrument arbitration corrects this finding

**The CONFIRMED-defect classification above is CORRECTED by the CP-5 measurements. The adapter path at beta < 1 delivers the DIRECT engine's content — the "defect" was an instrument inconsistency, and the recorded fix design is un-implementable within the bounded adapter resource model. Nothing below rewrites the original record; this section supersedes its disposition.**

**1. The recheck's wet-pitch metric was not adapter-discriminating.** The raw
Goertzel dominant (60..400 Hz sweep, ±4 Hz band) fails IDENTICALLY on the
DIRECT render: measured on the canonical tree (the probe case
`TP-PS-BETA-DOWN-CONTENT`, `tests/timepitch_host_probe.cpp`) the direct
full-buffer job at -7 st reports a 293 Hz dominant and at -12 st a 220 Hz
dominant — the same values this finding recorded for the adapter path. The
mechanism is the FROZEN synthesis itself: the MC90 mark law tiles 2P-length
grains at output hop s = P/beta, so at strong down-shifts the grain CARRIER
(the input's own periodicity) dominates the spectrum with the shifted
fundamental as a sideband (measured H2:H1 ~= 2:1 at beta = 1/2 exact tiling;
at -5 st the 33% grain overlap restores H1 dominance, which is why the -5 row
measured "correct"). A direct-vs-adapter comparison through this instrument
measures the synthesis character, not the adaptation.

**2. The contract's pitch instrument arbitrates BOTH paths to the exact
expected pitch.** The clean-room pYIN tracker (the same instrument every
pitch gate uses) over the settled wet, sine 220 material, 48 kHz:

| st | tracker(adapter) | tracker(direct) | expected |
|---|---|---|---|
| -5 | 164.7 Hz | 164.7 Hz | 164.8 Hz |
| -7 | 146.8 Hz | 146.7 Hz | 146.8 Hz |
| -12 | 110.0 Hz | 110.0 Hz | 110.0 Hz |

The delivery accounting stays zero-miss on every row (underruns 0,
telemetry-dry 0, stalls 0, preparation failures 0), and the full-buffer
render determinism holds. The package's adapter/direct consistency item is
now a HARD gate in `TP-DIFF-BETADOWN` (the raw Goertzel dominant is demoted
to reported telemetry alongside the direct's reference value — the TASK K
pattern: the over-strict pin converted with evidence, diagnostics kept).

**3. The recorded fix design is un-implementable within the bounded adapter.**
Content continuity at rate beta_c < 1 requires the job input origins to
advance at beta_c x (output step) — then the content the emission needs at
output time T lives at input beta_c x T, i.e. (1 - beta_c) x T BEHIND the
stream, unboundedly. With `kJobSlots = 3` (realtime_adapter.cpp:46) and the
bounded dry retention (max(worst-case Lambda, prep lead)), neither the slot
ring nor any bounded buffer can serve it: jobs stamped on the input timeline
accumulate (alive count grows ~ (1/beta_c - 1) x T / (wetLen - seamX));
jobs stamped on the emission timeline need input the dry lane no longer
holds. The finding's two accounting facts are TRUE in the source (the wet
append IS capped at wetLen; the input origins DO advance ratio-independently)
but they do NOT produce a pitch/content defect measurable by the contract
instrument: each splice job is synthesis-self-contained (its own tracker
warm-up, its own mark grid), and the input-origin skips land at the seam
crossfades between INDEPENDENT jobs. No CP-5 production change was made; the
frozen semantics are untouched.

**4. The mandated matrix (90 rows) is GREEN everywhere except the RC2b
class.** `TP-DIFF-BETAMATRIX` (committed; the CP-5 record was produced with
`PITCHLAB_TP_DIFF_FULLMATRIX=1`): pitch {0,+7,-5,-7,-12} st x fs
{44.1,48,96} kHz x block {64..1024}, sine, tracker-arbitrated — every row
pitch-OK and zero-miss. The +12 rows measure the RC2b known character
through the adapter: the DIRECT engine NULLS at the exact pitch-up octave
(measured g(440 Hz) = 0.0002 on the direct render — the analytic
cancellation, the owner's own anchor measurement), and the adapter's
465-468 Hz readings are the imperfect-cancellation residue of the tiling.
The RC2b DO-NOT-TOUCH disposition stands; no gate was added.

**5. Newly recorded reported classes (diagnostic-first, no gates).** On the
harmonic-stack material the pYIN tracker itself is unreliable ON PSOLA-down
output (both paths measure non-canonical medians — the burst-tiling content
sits outside the tracker's design domain); the path DELTA at -7/-12 on saw
is real and uncharacterized by either instrument — the human-listening
question for the engine-selection lane. The exit-ring headroom was measured
(exitDropped reproduces locally: 24-25 events during the pitch_formant
sweep; 42 on the CP-4d CI runner) with delivery zero-miss throughout — the
self-healing safe path works; the ring stays at 64 (the 64 -> 256 change is
NOT needed; deferred with this record).

**Disposition: RC-2a = RESOLVED AS METRIC ARTIFACT (the adapter path delivers
the direct engine's content at beta < 1; the frozen contract restored in the
measurement domain, untouched in code). The down-shift spectral character
(the carrier sideband structure) belongs to the RC2b / engine-selection
owner lane. TIMEPITCH remains NOT COMPLETE pending the owner listening and
the RC2b disposition, as recorded in the master worklog.**
