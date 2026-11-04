# PITCHLAB ROUND 22 FINDINGS

**Product:** PitchLab Harmonizer (the independent harmonizer) · **Core:**
octavox-native **17.0.0** (the version lineage reconciled this round: R21
was documented as "16.1.0-R21" while the string still read 16.0.0 — both
now read 17.0.0) · **Original-target research:** the Eventide Octavox
static RE continues (the Randomize polarity contradiction RESOLVED with a
correction record; the stereo-mixer composite mapped; the Minimum-Pitch
tracker-domain regime found). **The original binary was NEVER executed.**

## Verdict table

| Item | Verdict |
|---|---|
| P1 minimum pitch end-to-end | **DONE** — the complete 8-stage path traced and documented (`docs/R22_MINIMUM_PITCH_PATH.md`); the coherent policy `spacing_fmin = max(min(fmin, f0_min), sr/(1.5·grain))` implemented (bit-exact at every default config — gated by the reference hashes); `PitchPathStats` + `minSpacingLockHz()` expose the DETECTABLE-vs-SHIFTABLE distinction in the API; the 10-point × 6-interval × 3-mode battery (186 runs, all finite, ≤ ±1.9 ¢ output error, −24 st supported at every point) recorded in `evidence/analysis_round22/r22_lowfreq.json`; the 192 kHz not-shiftable demonstration and the empty-gate control leg measured; **the default stays 60 Hz** |
| P2 real-audio harness | **DONE** — `octavox_r22_realaudio`: the category discipline (synthetic / recorded:class / annotated:class, 12 validated classes, hard errors on mislabelling), the manifest format + the labelled test-set sheet (`evidence/analysis_round22/realaudio_r22_demo/realaudio_test_set.csv`), mono AND stereo processing, per-channel trajectories + L/R estimate divergence, pitch TRANSITION events (windowed over the detector's median length), voicing ratio, steady-state allocation count, CPU, output discontinuity, the OUTPUT cross-mode comparison (RMS of the wet-output difference vs FullRate), final output WAVs per mode (--outdir), reference trajectories via --annotate/manifest (the only ground-truth category), Lagrange6 default with ObsKernel6 optional. The workflow demonstrated end-to-end on honestly-labelled SYNTHETIC material (the sandbox holds no real recordings; the demo is never presented as real-recording quality) |
| P3a per-voice pan | **DONE** — constant-power law (`MultichannelShifter::setVoicePan`; gL=cos, gR=sin, θ=(p+1)π/4), exact 1/0 at the hard positions and a bit-symmetric centre constant (dual-mono transparency is BIT-EXACT), click-free (rides the existing gain ramp), mono/other channel counts transparent; 6 gated cases (centre bit-identity, hard isolation, the measured power law, mono no-op, 8-voice, move click-free) |
| P3b per-voice feedback | **DONE** — an explicit INDEPENDENT topology (the original's routing stays UNK): a per-voice overlay ring receives `fb · (the voice's own contribution, post-gain, z⁻¹)`; the voice regenerates through its own shift (the cascade); loop gain ≤ 0.999999 per voice — stable by construction over the complete documented range (−100..0 dB equivalent); mute-gated (OBS-aligned); the base-delay/overlay range guards; 7 gated cases (off bit-exact, 0 dB BIBO over 10 s + decay at 0.5, the cascade tail, mute gating, the guards, reset freshness, combined pan+feedback) |
| P4 harmony integration | **DONE** — the user-facing musical model: 21 APPENDED parameters (ids 38..58; ids 0..37 byte-identical), the composition law `ratio_v = 2^(pitch/12) · mapChoice(...)` applied through the EXISTING per-voice ratio API (the DSP untouched), mapper-OFF bit-neutrality, suspend/resume of the offset surface, **state-blob VERSION 3** with the bit-neutral v1/v2 migration (a hand-built v2 blob renders bit-identically to its v3 equivalent); 6 new wrapper cases; the 545-vector mapper law re-verified this round (worst deviation 0.0) |
| P5 static RE | **DONE** — the Randomize sign/direction contradiction **RESOLVED** instruction-by-instruction (the R21 "±0..8.33%" wording superseded by the correction record: one-sided +0..+12.24% rate, F ∈ [0,1], quartic error ≤ 0.1012 ¢); the stereomixer3 composite structure mapped (sub-functions 0x1800D8230/0x1800D7400 named; .rdata contains NO pan table — the law is computed; arithmetic still UNK, the concrete next-round target); NEW Minimum-Pitch findings: the low-pitch tolerance regime (base index > 0xD2=210 → tolerance 3/32→1/32) and the rate-scaled clamped [2,18] derivation (the T+0x6000 multiplier — the sample-rate interaction) |
| P6 regression | **DONE** — 115/115 core cases (Release AND ASan/UBSan); 27/27 wrapper + 12/12 integration + 47/47 Steinberg validator on ALL FOUR Linux variants (re-run on the final sources); the R20 dual-mono matrix re-run (0 transparency violations); the R21 stereo/voice-delay matrix re-run (0 violations); the R21 harmony vectors re-verified (545/545); the bench re-captured |
| P7 build/packaging | **DONE** — version metadata reconciled everywhere (17.0.0); **the `--filter`/group-selection bug FIXED** (the r20 hybrid cases were registered under group "r20" but executed inside casesR19 — a selection that deselected the r19 cases never ran them and an empty run was reported as passing; reproduced, fixed with the dedicated r20 runner + the empty-selection exit-2 guard); 4 Linux variants + 4 Windows x64 modules (Zig cross-build, genuine PE32+ x64, structurally verified: exports/FUIDs/identity/moduleinfo/SHA-256); the validation kit + delivery assembled; the manifest regenerated from the final tree with a fresh-extraction verification |
| Gates | core 115/115 (release + ASan); wrapper 27/27 ×4; integration 12/12 ×4; validator 47/47 ×4; harmony 545/545; dual-mono/stereo matrices 0 violations; Windows structural verification 4/4 OK |

## P1 — the complete Minimum Pitch signal path (the round's core question)

R21 made the detector's floor configurable but the engine's spacing gate
stayed fixed at 55 Hz: lowering `f0_min` improved DETECTION without making
the ENGINE follow. R22 traces all eight stages (the normative document:
`docs/R22_MINIMUM_PITCH_PATH.md` — detector frequency → tracker lag range →
confidence/rejection → period conversion → grain spacing → delay read
position → voice pitch ratio → safety/buffer constraints) and installs the
**coherent policy**:

```
spacing_fmin = max( min(cfg.fmin, cfg.f0_min), sample_rate / (1.5·grain) )
```

* With every default config this is **exactly 55.0** at every canonical rate
  (the safety floor 48000/6144 = 7.8125 Hz ≪ 55) — the accepted set is
  bit-identical to ≤16.1.0 (gated by the four r15 reference hashes).
* Lowering `f0_min` below `fmin` makes the engine follow — down to the
  **spacing safety floor** `sr/(1.5·grain)`: a period longer than 1.5
  grains cannot be represented by the grain law (the S clamp would break
  A/B periodicity), so such estimates are counted as detected-but-not-
  shiftable and never lock the spacing.
* **The API distinction:** `lastEstimateHz()` = the detectable capability;
  `minSpacingLockHz()` + `PitchPathStats{detected, spacing_accepted,
  spacing_rejected}` = the safely-shiftable capability (reset by reset(),
  per channel on the multichannel surface).
* **The battery** (`octavox_r22_lowfreq`, synthetic corpus honestly
  labelled): the ten specified points
  {32.703, 36.708, 41.203, 49.000, 55.000, 60.000, 65.406, 73.416, 82.407,
  110.000} Hz × intervals {+12, +7, 0, −7, −12, −24} st × all three
  detectors, f0_min = 0.98·f0: **detection ≤ ±0.93 ¢ everywhere; the
  spacing locks to k·period at every shifted run; the OUTPUT pitch
  (independent autocorrelation measurement) is within ±1.9 ¢ at all
  points/intervals down to −12 st and −3.9 ¢ at −24 st** (110→27.4 Hz; the
  estimator itself coarsens below ~30 Hz). −24 semitones is supported at
  every point (outputs reach 8.17 Hz — finite, bounded, tracked). All
  three detector modes agree within measurement resolution. CPU rises
  from ~550 ns/sample (110 Hz floor) to ~1100 (32.7 Hz — the
  window-growth cost). The 192 kHz leg (f0_min=20, tone 25 Hz, safety
  floor 31.25): 8912/9000 estimates spacing-rejected (the detectable-
  not-shiftable state, live), the remainder are legitimately-shiftable
  octave errors; the empty-accept-range control: 0 accepted, spacing
  never leaves the grain default. A 30 s 8-voice spread+feedback render at
  41.2 Hz: finite and bounded.
* **The default is unchanged** (60 Hz): the original's static "Minimum
  Pitch" semantics (C0..B3, default C2 ≈ 65.4 Hz — R21) and these
  measurements are both on record; a default change needs its own
  justification.

## P2 — the real-audio comparison harness

`octavox_r22_realaudio` (R21's tool preserved; this is its successor):
**category discipline first** — every input is labelled `synthetic`,
`recorded:<class>` or `annotated:<class>` (12 validated classes from the
specification's recommended set; an unknown class or an annotated entry
without a reference csv is a HARD error — mislabelled material is worse
than none). The **manifest** declares `path,category[,annotation[,label]]`;
the labelled test-set sheet
(`evidence/analysis_round22/realaudio_r22_demo/realaudio_test_set.csv`)
carries the recommended real classes as commented rows for user-supplied
recordings (the privacy rule: recordings stay at their own paths; only the
metric JSON/CSV is archived). **Per entry × mode** (identical audio +
settings; FullRate/Decimated/Hybrid): raw + median-5-smoothed trajectories
(CSV), voicing ratio (the detector's only confidence output, stated as
such), median/max cent difference **vs the reference** (the FullRate mode
for inter-mode AGREEMENT, the annotation spans for true ACCURACY — the
only ground-truth category; never conflated), octave disagreement,
dropouts, jitter, pitch TRANSITION events (windowed |Δcents| > 100 over
the detector's 8-boundary median length — the R21-style per-step delta
misses real steps spread by the median filter; measured), transient
recovery, relative lock latency, CPU, **steady-state allocation count**
(the harness links the core's allocation counter — 0 everywhere, the R12
contract extended to the real-audio path), output discontinuity scan, the
**output cross-mode comparison** (RMS of the wet-output difference vs the
FullRate output — the processed-audio agreement the specification
requires), L/R estimate divergence for stereo files, and the **final
offline output WAV per mode** (32-bit float). Stereo files run through the
stereo engine at their own channel count. Lagrange6 is the default
interpolation; ObsKernel6 is `--interpolation obskernel6`. The demo set
(3 synthetic WAVs: vibrato-voice, portamento-bass, speech-like with breath
gaps) demonstrates the FULL workflow with honestly-labelled synthetic
material; **the round's comparison results are therefore synthetic-category
demonstrations, not real-recording quality claims** — the sandbox holds no
recorded material, and the sheet + manifest make supplying it a
fill-in-the-rows operation.

## P3 — per-voice pan and feedback (independent designs)

**Pan.** The original's law is UNK (R21; R22 mapped the stereomixer3
composite — see P5). Our documented independent law is **constant-power**
(`gL=cos((p+1)π/4)`, `gR=sin(...)`, gL²+gR²=1; centre = −3.01 dB/side BY
THE LAW — documented, not hidden). Implementation: the coefficient lives
in each mono channel engine and **folds into the existing ramped gain
target** (`user_gain · pan_coeff`) — pan moves are click-free over
`gain_ramp_seconds`, the hot loop is UNCHANGED (bit-exact default), and
the feedback tap sees the channel's panned contribution (documented
interaction). The user API is `MultichannelShifter::setVoicePan`
(stereo-only; other counts are transparent no-ops); the channel
coefficient API is `AlignedDelayLine::setVoicePanGain`. Hard positions are
EXACT 1/0 (cos(π/2) residue pinned away — a −244 dB ghost would break
hard isolation); the centre constant is ONE stored double for both
channels (two libm calls can differ by an ulp — dual-mono bit-identity
requires bit-equal coefficients). Gated: centre bit-identity, hard
isolation (opposite channel exactly 0), the measured power law (power-sum
within 2% at five positions), mono transparency (bit-identical), 8-voice
centre identity + alternating-hards, move click-free (≤1.5× the natural
slope).

**Feedback.** Explicit independent topology (the original's routing is
UNK; the parameter law is OBS — −100..0 dB, linear 10^(dB/20),
mute-gated): **a per-voice overlay ring**. A feedback-armed voice reads
its grain from the shared ring PLUS its own overlay, which receives
`fb · (the voice's own contribution sample, post-gain, one sample earlier
— the same z⁻¹ law as the global feedback)`. The voice therefore
regenerates through **its own shift** (the cascade: each pass re-shifts by
the voice ratio — the musical signature, gated by the cascade-tail case).
Loop gain `fb · gain ≤ 0.999999` per voice — **stable by construction for
the complete documented range** (each loop independent); the combined
global+per-voice loop gain is documented (not clamped — the acceptance
tests exercise each alone and a 0.25+0.3 combination). Mute gates the tap
(OBS-aligned). The overlay is sized at prepare from the config-level read
range (pow2; a LARGER live overlay is kept on in-place re-prepares — only
a range GROWTH takes the allocate path, preserving the R13-c
zero-allocation contract for the historical shrink test). Guards:
`setVoiceFeedback` rejects arming when the voice's effective base delay
exceeds the overlay range; `setVoiceBaseDelay` rejects moves beyond it on
armed voices (both no-ops like every other invalid setter input — gated).
Gated: off bit-exact, 0 dB BIBO (10 s, bounded < 500×) + the 0.5 decay,
the cascade tail (>20× the disarmed tail), mute gating, both guards,
reset freshness, and the combined pan+feedback long render.

## P4 — the harmony mapper integrated into the host surface

21 parameters APPENDED (ids 38..58; **ids 0..37 byte-identical — the
38-parameter contract is immutable**): Harmony Mapper (38), Key (39, 17
spellings, default A), Scale (40, the 15 ENGINE choices — the recovered
scal-mux surface INCLUDING its authored irregularity, deliberately not
normalized away; the underlying 19 pattern rows remain the mapper's
library-level row API), Temperament (41, the five recovered tables),
Global Fine Tune (42), Voice N Interval ×8 (43..50, the 29-choice mux),
Voice N Fine Tune ×8 (51..58). The **composition law**: mapper ON ⇒
`ratio_v = 2^(pitch/12) · mapChoice(key, scale, temperament, interval_v,
fine_v, global_fine)_v` applied through the EXISTING `setVoiceRatio` API
(the pitch-shifting DSP is not modified); the per-voice Transpose
parameters and the spread table are suspended (values preserved; they
resume on OFF — gated). Mapper OFF (the default) ⇒ the ≤R21 law
BIT-EXACTLY (gated). **State blob v3**: the 21 values appended after the
legacy 38 (byte-identical prefix to v2); v1/v2 blobs restore with the
harmony fields at defaults — the migration is **bit-neutral** (a
hand-built v2 blob renders bit-identically to its v3 equivalent —
gated); saves are always v3; version 4+ rejected. The wrapper cases:
defaults/titles/domains/automation, applied ratios vs the mapper law
across 8 configs × 8 voices (every scale-choice class incl. the irregular
rows, every temperament, the 29-interval cycle, global shifts — settled
glide within 1e-9), mapper-OFF bit-neutrality, v3 round-trip
(byte-identical re-save), v1/v2 migration, suspend/resume. The 545-vector
mapper law re-verified this round: **545/545, worst deviation 0.0**.

## P5 — the original-target static RE (evidence:
## `evidence/analysis_round22/r22_static_findings.md`)

**The Randomize resolution (the special requirement).** The R9/R11 law
(noise → |noise| → sample&hold → slew ⇒ F ∈ [0,1] nonnegative ⇒ the
modulation is one-sided upward) vs the R21 wording ("±0..8.33% rate
modulation"). Re-derived link by link with exact instructions: the source
`FUN_180139B40` is signed ([−1,+1] float32 orbit); the **andps 0x7FFFFFFF
abs at 0x180103F33/0x180103F46/0x180103F5A/0x180103F75 (mask at
0x180412C60, verified) applies to the noise samples BEFORE the
sample&hold call at 0x180103F7D**; the slew (0x180139790, called at
0x180103F82) walks toward the nonnegative held target and clamps at it —
F stays nonnegative; the scale (page[10]) multiplies by the verified
1/2400 double; the final chain (f = F·page10; step = 4·P(f)·(ratio/16)
·correction·64) has **no sign reintroduction anywhere**. Therefore: noise
sign = signed at source; modulation input = [0,1]; rate multiplier =
4·P ∈ [1, 1.122412] (**one-sided +0..+12.24%**); cents deviation =
0..+199.92 upward; quartic error ≤ 0.1012 ¢ (negative in the bulk,
re-verified numerically). **The R21 "±" wording is superseded** — the
correction record lives in `r22_randomize_trace.txt`; historical findings
are preserved unedited.

**Pan mix law (bounded progress).** The composite add_fn 0x180137720 is a
thin wrapper over 0x1800D8230 (a constructor-style initializer, module id
0x152) and 0x1800D7400 (the mix); a whole-`.rdata` scan for pan-law table
shapes found NONE (the only structured candidate is a 77-entry monotonic
ramp ≈ a math table) — the law is COMPUTED in the 0x1800D7400-family
sub-modules (the concrete next-round target). OBS structure / UNK
arithmetic.

**Minimum Pitch conversion + rate interaction (NEW).** The tracker
tightens its acceptance tolerance **3/32 → 1/32 when the period-search
base index exceeds 0xD2 = 210** (0x18013CF3C — the low-pitch regime), and
derives a rate-scaled count **clamped to [2,18]** via the T+0x6000
multiplier (0x18013C97C) — the sample-rate interaction inside the domain
math. OBS; we do not claim to reproduce either behaviour.

## P6 — the regression record

115/115 core cases (Release **and** ASan/UBSan — the suite grew 96 → 102
(P1+P7) → 115 (P3)); 27/27 wrapper tests + 12/12 integration + 47/47
Steinberg validator on all four Linux variants, re-run on the final
sources; the R20 dual-mono matrix re-run (0 transparency violations —
`r22_dualmono.json`); the R21 stereo/voice-delay matrix re-run
(`r22_stereo.json`, the four behaviour classes intact; **the new pan and
feedback changes introduce no unintended L/R differences** — the pan
cases prove the centred-position bit-identity and the feedback topology
is per-channel symmetric); the R20-P0 default-synchronization gates
re-passed (the host displays the actual processor defaults without
slider movement); the state v1/v2 migration + fresh restore + repeated
initialize/terminate cycles re-passed; the bench re-captured
(`r22_bench.json`, 13 cases, deterministic).

## P7 — build and packaging quality

* **Version metadata reconciled**: 16.0.0/16.1.0 → **17.0.0** everywhere
  (dsp.hpp title, versionString, CMakeLists project(), the README banner
  with the lineage note).
* **The `--filter` bug FIXED** (reproduced first: `--filter
  detector_hybrid` ran 0 cases and exited 0): the three R20 hybrid cases
  were REGISTERED under group `r20` but EXECUTED inside `casesR19` — a
  selection that deselected every r19-registered case skipped the r19
  runner entirely. They now live in their own `casesR20` runner
  (bodies unchanged); `r22` joins the group registry; a post-run guard
  exits 2 whenever a filtered run executes zero cases (defence in depth
  on top of the existing name-match pre-check).
* **Four unique VST3 identities** (the R21 matrix unchanged; the
  FullRate benchmark build carries its own): Linux
  `PitchLab_{Lagrange6_Balanced, Lagrange6_Hybrid, ObsKernel6_Balanced,
  Lagrange6_FullRate}.vst3` — each fully validated; Windows x64 modules
  cross-built with Zig 0.13.0 (the recorded R21 toolchain deviation —
  genuine PE32+ x64, MS x64 ABI; the validator self-test post-build is
  disabled for the cross builds because a PE cannot execute on the Linux
  host — the RUNTIME validation is carried by the Linux builds of the
  same sources + variant defines, and the Windows modules are
  structurally verified: PE32+ x86-64, GetPluginFactory export, the
  import allow-list, the processor FUID bytes in the Windows COM layout,
  PitchLab-present/Eventide-absent identity strings, moduleinfo identity,
  SHA-256 — `r22_windows_verification.json`, 4/4 OK).
* The Windows delivery + validation kit assembled
  (`octavox-work/r22/Windows_delivery/`: Builds ×4, the A/B null-test
  `.exe` + `.cmd` runners + the README).
* The archive manifest is REGENERATED from the final tree with a
  fresh-extraction verification (exact file count, zero stale, zero
  missing, zero unlisted; SHA-256 of the archive recorded).

## Artifacts & reproduction

* Core: `native/build_r22` (115/115), `native/build_asan_r22` (115/115),
  the R22 tools (`octavox_r22_lowfreq`, `octavox_r22_realaudio`).
* VST3 (Linux ×4): `native/vst3/{build_r22, build_vst3_r22_hyb,
  build_vst3_r22_obs, build_vst3_r22_full}` — logs in
  `evidence/analysis_round22/logs/`.
* Windows ×4 + the kit: `octavox-work/r22/Windows_delivery/`.
* Evidence: `evidence/analysis_round22/` (the low-frequency battery JSON;
  the real-audio demo + trajectories + output WAVs + the test-set sheet;
  the static scans + the randomize trace + the findings; the suite/bench/
  dual-mono/stereo JSONs; the logs).
* Docs: this file, `docs/R22_MINIMUM_PITCH_PATH.md`, the updated
  `docs/R21_FEATURE_RECONSTRUCTION_MATRIX.md` (R22 rows), the updated
  `native/vst3/VST3_WRAPPER.md` (§3a/§3b: the 59-parameter contract + the
  v3 blob), the native README banner.

## Explicit non-claims

No execution of the original binary; no runtime-equivalence claim; the
pan/feedback topologies and the pan law are OUR documented independent
designs (the original's are OBS/UNK); the real-audio demo results are
synthetic-category demonstrations; the default detector (Decimated), the
default f0_min (60 Hz) and every legacy parameter id/default/state
semantics are unchanged.
