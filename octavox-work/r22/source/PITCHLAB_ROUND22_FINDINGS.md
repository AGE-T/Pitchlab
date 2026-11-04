# PITCHLAB ROUND 22 FINDINGS

> ## ⚠️ SUPERSESSION NOTICE (correction-log C-R22-S1, 2026-11-05)
>
> **This document describes the FIRST R22 attempt (2026-11-02), whose engine
> design was SUPERSEDED before delivery. It is NOT the delivered round 22.**
>
> * This attempt's spacing-gate law was `min(fmin, f0_min)` (the engine gate
>   follows the detector floor DOWN, opening sub-55 Hz tracking); it also
>   claimed the full P1–P7 scope: 74 host parameters (ids 38..73), per-voice
>   pan + feedback, state blob v3, the realaudio v2 harness, and three static
>   RE items.
> * **The delivered round 22** (the recovery round, after a further sandbox
>   rollback) is `docs/OCTAVOX_ROUND22_FINDINGS.md` inside
>   `Octavox_RE_evidence_round22.zip` (SHA-256
>   `782e7ab8111630518eb99ffb7cb7e7403c7917af410ecc48e1f859936b0a1e61`):
>   the UNIFIED gate `max(fmin, f0_min)` (detector dominance made explicit —
>   the historical `fmin < f0_min` regime is provably dead configuration;
>   detectable-vs-spacing-trackable survives at the `f0_min < fmin` edge),
>   the 4 diagnostics, suite 96 → 100, the 360-cell matrix — **P1 only;
>   P2–P5 explicitly deferred** (see the delivered findings §3).
> * The attempt's artifacts (the CLI sources `r22_panfb.cpp`,
>   `r22_realaudio.cpp`, `octavox_r22_state.cpp`, its evidence set under
>   `evidence/analysis_round22/` of the working tree, and its Windows
>   four-variant zip) are NOT in the shipped archive; they remain in the
>   working tree as history. Nothing below was verified against the shipped
>   engine. The two designs agree on the round's core goal — linking the
>   detector floor into the engine gate bit-exactly at defaults — but differ
>   on the open direction and on scope.
>
> **Read the delivered findings for the authoritative round-22 record.**

**Product:** PitchLab Harmonizer (the independent harmonizer; the identity
is PitchLab, never Octavox, in any user-visible surface) · **Core:**
octavox-native **16.2.0** (R22: the f0_min-linked spacing gate, per-voice
pan + per-voice feedback in the DSP core; defaults BIT-EXACT the R21
engine — gated by the unchanged 96-case suite and every reference hash) ·
**Wrapper:** the 38-parameter host contract EXTENDED to **74 parameters
(ids 38..73 appended, never renumbered)** + state blob **v3** with v1/v2
migration · **Original-target research:** the Eventide Octavox static RE
continues (the Randomize polarity contradiction RESOLVED; the global
feedback topology fully traced; a new pan mix-law data point). The original
binary was NEVER executed.

**Environment disclosure (correction-log):** this round began from a fresh
sandbox — the R21 working tree was not present; the round-21 evidence
archive (`Octavox_RE_evidence_round21.zip`, SHA-256
`c77dc1e3469e351b6909c145c13c3084a9e72621bd8633f526b52a086ced8ca9`) was
re-extracted and verified (its 96/96 suite reproduced bit-exactly) before
any R22 change. A second disclosure: the R21 findings described the core
as "16.1.0-R21" while the shipped version string was "16.0.0" — R22 skips
to 16.2.0 and logs the discrepancy. No cmake exists in this environment;
the R22 builds use the direct-g++ scripts
`native/tools/r22_build.sh` + `native/tools/r22_vst3_build.sh` (the
CMakeLists is kept current for future environments). No Windows
cross-compiler exists here — **no Windows binaries are shipped this
round** (the R21 Windows builds are NOT relabeled R22; the sources +
CMake are Windows-build-ready).

## Verdict table

| Item | Verdict |
|---|---|
| P1 minimum pitch end-to-end | **DONE** — the engine's spacing-accept gate now FOLLOWS the detector floor (effective gate = `min(fmin, f0_min)`; the default 60/55 law is bit-exact unchanged — the 55 Hz gate is NOT deleted); the 8-level path audited and measured; `octavox_r22_minpitch`: **180 cases — 120 shiftable, 45 unity-parked, 15 below-floor, 0 FAIL**; detectable vs pitch-shiftable distinguished in the API (`spacingGateHz()`) and the verdicts |
| P2 real-audio harness | **DONE** — `octavox_r22_realaudio`: EXPLICIT enforced categories (synthetic / recorded / annotated; annotated requires a reference track, recorded never claims ground truth); smoothed trajectories + conversion timestamps; FINAL-PROCESSED-AUDIO cross-mode comparison; per-mode WAV output; peak RSS. Demonstrated on a synthetic + an annotated case; the user's real recordings enter via `--input` and are NEVER embedded in the archive without approval |
| P3 per-voice pan + feedback | **DONE** — pan law θ=(p+1)π/4, gL=√2·cos, gR=√2·sin (centred = bit-exact legacy; +3.01 dB hard-pan active channel; constant total power gL²+gR²=2; mono fold (gL+gR)/2); feedback: independent one-sample per-voice registers injected into the common input ring, pre-pan, mute-gated, documented ±8.0 bus safety rail. `octavox_r22_panfb`: **14/14 gates** incl. bit-exact centred transparency, hard-pan isolation −234 dB, coherent 8-voice model to 6 decimals, 18 bounded feedback combos, mute/reset/isolation |
| P4 harmony mapper integration | **DONE** — ids 38..73: 8× Pan, 8× Feedback dB (−100..0, the original's OBSERVED conversion 10^(dB/20), mute-gated), Harmony Mapper arm, Key (17), Scale (19 pattern rows), Temperament (5), 8× Interval (29), 8× Fine Tune (±50 ¢); state blob v3 (+36 plain doubles) with **v1/v2 migration (extension inert — restored sessions behave exactly as saved)**; armed ratios = `globalRatio × mapRow(...)` over ALL 19 rows × 5 temperaments × 29 intervals; disarmed = the legacy law bit-exact. `octavox_r22_state`: **10/10** |
| P5 static RE | **3 items advanced** — Randomize polarity RESOLVED (the R21 "±" wording corrected: one-sided upward, cents ∈ [0,+200], ratio multiplier ∈ [1,+12.24 %]; R9/R10/R11 findings stand); the global feedback topology fully traced (post-pan stereo taps, softclip, crossfaded fbkd delay 0..5000 ms, high-cut cleanup); NEW pan mix-law data point (`monoSumWetChans` = equal-power 0.707/0.707 mono fold). See `evidence/analysis_round22/r22_static_findings.md` |
| P6 regression | **ALL GREEN** — 96/96 core (bit-exact defaults), 21/21 wrapper (updated to the 74-param contract), 10/10 state, 14/14 panfb, R21 stereo matrix 0 dual-mono violations, R20 dualmono/detcompare reproduced, R21 fmin reproduced; ASan/UBSan: core fast groups 47/47 + the full panfb battery clean (the minpitch ASan run exceeds the sandbox time budget — non-gated, disclosed) |
| P7 packaging | manifest regenerated from the actual tree; fresh-extraction verification; archive SHA-256 recorded; the web report updated |

Gates (all exit-code gated, logs under `evidence/analysis_round22/logs/`):
96/96 core acceptance (the unchanged R15–R19 suite), 21/21 VST3 wrapper,
10/10 state/mapper, 14/14 pan/feedback, 180/180 minpitch battery rows
(0 FAIL), 0 stereo transparency violations.

---

## P1 — the complete Minimum Pitch signal path

**The R21 gap.** `Config::f0_min` (R21) configured the DETECTOR's lag
domain and validity gate, but the ENGINE's spacing-accept gate kept its
independent fixed 55 Hz floor (`cfg.fmin`): a lowered detection floor
emitted sub-55 Hz estimates that the engine then silently rejected —
spacing stayed at the grain default and the voices ran untracked.

**The R22 fix (one line + contract).** The spacing gate's lower bound is
now `min(cfg.fmin, cfg.f0_min)` (`updateSpacingTarget`,
`native/src/dsp.cpp`). With the default `f0_min = 60` the gate is EXACTLY
the legacy 55 Hz — bit-identical behaviour (96/96, all reference hashes
unchanged). The 55 Hz value is NOT deleted: it remains the engine-side
default gate. The public API gained `spacingGateHz()` — the exact line
between "DETECTABLE" (`lastEstimateHz() != 0`) and "PITCH-SHIFTABLY
TRACKED" (`lastEstimateHz() ≥ spacingGateHz()`).

**The 8-level audit** (all levels verified by the battery):

| level | law | status |
|---|---|---|
| 1. detector lag domain | `tau_max = int(rate / f0_min)` (both window laws) | R21 ✓ |
| 2. estimate validity | `[f0_min, 1200] Hz` | R21 ✓ |
| 3. spacing-accept gate | **`min(fmin, f0_min)`** | **R22 (the fix)** |
| 4. period conversion | `f0_hist = sr / f0` (any f0) | ✓ |
| 5. grain spacing | `S = k·t̂` clamped `[0.5G, 1.5G]` — within the validated f0_min range [20,200] Hz the clamp never engages (k·t̂ ∈ [G−t̂/2, G+t̂/2] ⊂ band) | ✓ measured (lockErr ≤ 0.023 across the matrix) |
| 6. delay read positions | ring bound `base + 32·(mv−1) + 3G + 8 < 2^18` holds for every f0 (S ≤ 1.5G regardless of f0) | ✓ (prepare validation unchanged) |
| 7. voice pitch ratio | wrap period `S/\|1−r\|` — measured output within ≤ 13.6 ¢ worst (≤ 5 ¢ typical) over the whole matrix incl. −24 st | ✓ |
| 8. low-frequency safety | bounded output (peak ≤ 8 with feedback 0), no NaN, wraps advance | ✓ |

**The battery** (`octavox_r22_minpitch`, JSON +
`logs/r22_minpitch_stdout.txt`): the ten spec frequency points
(32.703 / 36.708 / 41.203 / 49 / 55 / 60 / 65.406 / 73.416 / 82.407 /
110 Hz) × 3 detector modes × 5 intervals (unison, +12 st, −12 st, −24 st,
+7 st) with the open floor `f0_min = 32.703`, plus a control sweep at the
shipped default 60 Hz documenting the detectable boundary:

* **120/120 non-unison cases = "shiftable"** — the end-to-end criterion:
  the measured OUTPUT F0 (an independent YIN probe over the wet output,
  5..2000 Hz search) within 50 ¢ of `f0·ratio` (measured worst 13.59 ¢,
  typical < 3 ¢), the spacing committed off the grain default and inside
  the band, wraps advancing, output bounded. `spacing_lock_err` is
  reported as a mechanism diagnostic (S vs the true period), not a gate.
* **45 unison cases = "unity_parked"** — the by-design unity path (exact
  ratio 1.0 parks the voice on an integer delay; wraps = 0; out-cent ≈ 0
  confirms the reproduced input).
* **15 control cases below 60 Hz = "below_floor"** — the shipped default's
  documented boundary (the detector locks harmonics/edges: medCent up to
  +6237 ¢ on a 32.7 Hz tone at the default floor — the garbage-out
  behaviour, recorded honestly).
* **The default is NOT changed** (60 Hz stays; the original's Minimum
  Pitch knob semantics — 48 choices C0..B3, default C2 ≈ 65.4 Hz — remain
  the R21 static finding; host exposure of f0_min is deferred to a future
  parameter review, documented).

## P2 — the real-audio harness v2 (`octavox_r22_realaudio`)

Extends the R21 harness with the R22 spec's requirements:

* **Explicit, enforced categories**: `--category synthetic | recorded |
  annotated`; invalid categories exit 2; `annotated` REQUIRES `--annotate`
  (a reference track: manual annotation or an external tracker — the tool
  never claims ground-truth accuracy it cannot verify); `recorded` never
  presents cent numbers as truth (inter-mode agreement vs the FullRate
  reference, stated in every record's `reference_mode` field).
* **Smoothed trajectory capture**: the harness mirrors the engine's
  period-history law (median of the last ≤ 8 valid estimates); the
  trajectory CSVs carry `est_hz, smoothed_hz, ref_fullrate_hz`.
* **Conversion timestamps**: ≥ 50 ¢ changes of the smoothed track — count
  + first-conversion time per mode.
* **FINAL-PROCESSED-AUDIO comparison**: RMS + max sample diff of each
  mode's wet output vs the FullRate output (identical settings) — the
  comparison is of the processed audio, not just the estimates.
* **WAV output** (`--wavdir`): the final processed audio per mode for
  operator-side auditioning. Personal recordings are never embedded in
  the research archive without explicit approval.
* **Peak RSS** (VmHWM) recorded per file alongside CPU.

Demonstrated on a 6 s synthetic vibrato corpus (synthetic category:
FullRate/Decimated/Hybrid agree to 0.017 ¢ median, CPU 1965 / 574 / 602
ns/sample) and on an annotated run (median TRUE error ≈ 2.9 ¢ vs the
nominal 220 Hz under a ±4 ¢ vibrato). **The user's real test set enters
via `--input`/`--list`**; the recommended set (male/female singing,
speech, bass, guitars, sustained instruments, vibrato, portamento, breath
noise, difficult multi-pitch) is documented in the tool header. Lagrange6
stays the default; ObsKernel6 remains the optional research mode (the
R20/R21 verdicts stand).

## P3 — per-voice pan + per-voice feedback (independent designs)

**Pan** (`MultichannelShifter::setVoicePan`, engine scalar
`setVoicePanGain`): θ = (p+1)·π/4, **gL = √2·cos θ, gR = √2·sin θ**.
Properties (all measured by the 14-gate battery): centred (p=0) → (1,1) =
**bit-exact the no-pan engine** (dual-mono transparency preserved — the
R21 stereo matrix re-run: 0 violations); hard pan → the active channel at
+3.01 dB (measured 1.414214×), the opposite channel at −234 dB; constant
total power gL²+gR² = 2 at every position (measured within 4 %);
mono/>2ch fold (gL+gR)/2 (measured exact); click-free ramped over
gain_ramp_seconds (max inter-sample step ≤ 3× the natural slope,
measured 0.0102 vs 0.0076). The 8-voice panned sum is a COHERENT sum (the
naive amplitude model is wrong — measured 0.609× vs naive 0.883×; the
phase-aware model matches to 6 decimals). The original's stereo mix law
is UNK and is NOT claimed (see R22-2 static: the equal-power mono-fold
data point).

**Per-voice feedback** (`AlignedDelayLine::setVoiceFeedback`, linear
[0,1] — the original's OBSERVED domain −100..0 dB → 10^(dB/20),
mute-gated): each voice's PRE-PAN, POST-GAIN contribution from the
previous sample lives in its OWN one-sample register (independent
per-voice state; muted voices hold exactly 0 — mute-gated like the
original); the registers are scaled by their gains and injected into the
COMMON input ring (the cascade re-enters the shifter and is re-shifted);
a DOCUMENTED safety rail clamps the feedback BUS (never the forward
paths) to ±8.0 and self-heals non-finite values — ANY parameter
combination stays bounded over a long render. Measured: all-zero
bit-exact (explicit-zero == unset engine); 18 level/voice/ratio combos
bounded (worst peak 8.26 — the rail engaged at the extremes, the
documented saturation behaviour); all-8-voices-at-maximum 10 s render
bounded at 8.26; mute gates the register (RMS 0.659 → 0.000); reset()
zeroes the registers (parameters survive); channel isolation structural
(silent L stays silent at 0.000000 with feedback active). The original's
routing is NOT claimed (R22-3 static: the original taps the post-pan
stereo mix through a softclip + crossfaded delay).

## P4 — the harmony mapper host integration

**The extension is append-only**: the original 38 ids, domains, defaults,
flags and serialization order are untouched; the new ids 38..73:

| ids | parameter | domain | default |
|---|---|---|---|
| 38..45 | Voice N Pan | [−1,+1] continuous | 0 (centred) |
| 46..53 | Voice N Feedback | [−100, 0] dB, step 0.1 | −100 (off) |
| 54 | Harmony Mapper arm | bool | OFF |
| 55 | Harmony Key | 0..16 (17 spellings) | 13 (A) |
| 56 | Harmony Scale | 0..18 (**19 pattern rows**) | 1 (ionian) |
| 57 | Harmony Temperament | 0..4 | 0 (Equal) |
| 58..65 | Voice N Harmony Interval | 0..28 (29 selections) | 14 (unison) |
| 66..73 | Voice N Fine Tune | [−50,+50] ¢, step 0.1 | 0 |

**Semantics** (documented in the 74 help texts): armed → every voice's
ratio = `globalRatio × mapRow(key, scale, temperament, interval_v,
fine_v)` (the R21 545-vector-verified mapper; the phase-shift DSP core
itself UNCHANGED); the per-voice Transpose params are ignored while
armed. Disarmed (default) → the legacy spread/transpose paths bit-exact.
The scale parameter exposes the 19 PATTERN ROWS (row-level surface): the
original's choice-level scal-mux irregularity (incl. the hungarian index)
stays preserved in the library's `mapChoice` and documented, NOT routed
through the parameter — the quirk is not erased, it is one API level
away. **Verified**: armed ratios == mapRow over ALL 19 rows × 5
temperaments (95 combos) and ALL 29 intervals × 3 fine positions (87
combos); the disarmed spread law holds with non-default mapper params set
(the core's recovered spread table {0,7,4,12,−5,−12,3,−3} semitones).

**State v3**: the blob appends the 36 extension plain doubles after the
legacy 38 (magic 'OVR1', versions: v1 R16 → v2 R19 (+4 Config fields) →
v3 R22). **Migration**: v1/v2 blobs restore with the extension at the
INERT defaults — pan centred, feedback off, mapper disarmed — so a
restored legacy session behaves EXACTLY as it did when saved (verified:
a hand-built v2 blob restored into a live processor replays bit-identical
audio; the extension explicitly set to inert == untouched defaults,
bit-exact). Forward incompatibility (a v1/v2-only build rejecting v3) is
accepted and documented, exactly as R19 accepted it for v2.

**The bug the battery caught** (disclosed): the first r22_state draft
drove parameters via `setParamNormalized` — which only touches the
host-visible Parameter object; the engine sees values ONLY through the
process() event queue. The spurious passes (a "unison-only" all-scales
case that could not fail) exposed it; the harness now delivers every
parameter through the real offset-0 event cluster. The lesson is recorded
in the tool header.

## P5 — static RE (original target)

See `evidence/analysis_round22/r22_static_findings.md` (the full chains
with addresses). Summary:

1. **Randomize polarity RESOLVED** — the R21 P7(e) "±0..8.33%" wording is
   corrected (correction-log C-R22-1): the chain (noise signed → `andps`
   |·| mask 0x7FFFFFFF at 0x180412C60, sites 0x1800F55…/0x180103F16–F78 →
   sh → slew → `f = F·clamp(depth,0,200)/2400 ≥ 0` → the all-positive
   quartic `4·P` ≥ 1 → the rate multiplier) admits ONLY one-sided upward
   modulation: cents ∈ [0, +200] (≤ 0.101 ¢ fit error), ratio multiplier
   ∈ [1, +12.24 %] at the depth-200 corner. The "±" conflated the
   polynomial's ±2-octave FIT DOMAIN with the driven range; "8.33 %" is
   the f-scale fraction. R9/R10/R11 stand unmodified.
2. **Pan** — new data point: `monoSumWetChans` (record [357]) uses the
   equal-power pair 0.707/0.707 for the mono fold — adjacent evidence for
   an equal-power family; the stereomixer3 pan law itself remains UNK.
   (Coincidence, not evidence: our [−1,+1] default-0 knob domain matches
   the original's knob record exactly.)
3. **Global feedback topology** (new, fully traced at the record level):
   the `mfbk` knob (0..100 %, default 0) scales per-CHANNEL taps of the
   POST-PAN stereo wet mix (`stereomixer3-left/right` → `feedback` /
   `feedback1`, amp2 = mfbk, tc 20 ms), each followed by a **softclip**
   (select 1, drive 0, order 1) → a **crossfaded delay** (`delay_xf`,
   maxdelay 2400, xfadetime 20, tap = `c_minmax` of the `fbkd` knob
   0..5000 ms vs 50) → a **high-cut + subtract cleanup** loop →
   recirculation into the feedback modules' in2. The per-voice `fbkN`
   chain (R21-closed) confirmed with the conversion modules' addresses
   (`c_divide18..25` ÷20 → `c_power9..16` 10^x).
4. Status items (R21 verdicts stand): crossfade two-ends OBS; Minimum
   Pitch knob conversion UNK (and C0 ≈ 16.35 Hz is below our validated
   f0_min domain [20,200] — a documented independent-implementation
   difference); MIDI routing UNK (negative result); tempo conversion UNK
   (host-side); snapshot semantics UNK.

## P6 — regression record

| gate | result |
|---|---|
| core suite (the unchanged R15–R19 96 cases) | **96/96** — the R22 defaults are bit-exact R21 |
| VST3 wrapper tests (updated to the 74-param contract: counts, id bitmap, v3 self-checks, the full-surface case extended with pan/feedback/mapper events + values) | **21/21** |
| state/mapper battery | **10/10** |
| pan/feedback battery | **14/14** |
| minpitch battery | **180 rows, 0 FAIL** |
| R21 stereo matrix re-run | 0 dual-mono transparency violations |
| R20 dualmono + detcompare re-run | reproduced |
| R21 fmin battery re-run | reproduced (93–100 % detection across the low-tone corpus at the open floor) |
| ASan/UBSan | core fast groups 47/47 + the full panfb battery clean; the minpitch ASan run exceeds the sandbox budget (disclosed, non-gated) |
| no-allocation steady state | unchanged (the suite's allocation gates; the pan/feedback additions add NO allocations — registers are pre-allocated in prepare) |

## P7 — packaging

* The manifest is regenerated FROM THE ACTUAL TREE (fresh-extraction
  verified: exact file count, zero stale, zero missing, zero unlisted).
* `Octavox_RE_evidence_round22.zip` + the sidecar `.sha256` (the archive
  root keeps the research name; the PRODUCT identity is PitchLab
  everywhere user-visible).
* **No Windows binaries this round** (no cross-compiler in this
  environment; the R21 Windows builds are not relabeled). The sources +
  the updated CMakeLists (native r22 targets + the vst3 r22_state target)
  are build-ready where a toolchain exists. The Linux g++ build scripts
  are included (`native/tools/r22_build.sh`,
  `native/tools/r22_vst3_build.sh`).
* The web report (this file) + the download table are updated.

## Explicit exclusions (unchanged)

The original binary was never executed; no runtime equivalence is claimed;
Octavox appears only as the research target's name in the RE docs; the
FullRate YIN is not deprecated; synthetic and real recordings are never
conflated; no artificial stereo widening; old parameter ids/defaults/state
compatibility are never silently changed; no GUI this round.
