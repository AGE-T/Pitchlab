# OCTAVOX ROUND 22 — FINDINGS

**Minimum Pitch: the complete 8-level signal path (unified spacing gate + the
10-frequency transposition-safety matrix)**
Date: 2026-11-03 · Agent: RE track · Continuation of `OCTAVOX_ROUND21_FINDINGS.md`
Core: octavox-native **16.2.0-R22** (additive: the unified spacing-accept gate
`max(fmin, f0_min)` + the four Minimum-Pitch diagnostics; bit-exact vs 16.1.0-R21
at every existing configuration — see §2.3; every reference hash unchanged)

---

## 0. Round context — environment loss and the R21 recovery

**Provenance record (material to this round's evidence chain).** The round
started after a full sandbox-environment rollback: every working-tree artifact
of round 21 (the rebuilt `r22/source` base, the R21 Windows kit, all unzipped
R21 trees) was lost; only the published evidence archives (round9–round20) and
the R18/R19/R20 Windows zips in `public/` survived. The R21 baseline was
recovered from the round-21 evidence archive supplied back to us by the
project owner (`Octavox_RE_evidence_round21.zip`, SHA-256
`c77dc1e3469e351b6909c145c13c3084a9e72621bd8633f526b52a086ced8ca9`,
verified against its sidecar before extraction; the archive manifest
`checksums.txt` was then re-verified file-by-file on the extracted tree —
zero stale, zero missing, zero unlisted). All R22 work below is built on that
recovered baseline; `evidence/analysis_round21/` in this archive is the
byte-identical copy from the supplied zip.

**Not recoverable this round:** the R21 Windows three-variant kit
(`PitchLab_R21_Windows_x64_ThreeVariants.zip`) was never published to
`public/` and is gone with the old environment; the cross-build toolchain
(Zig/llvm-mingw) is no longer installed on the rolled-back host. The R22
Windows delivery is therefore **deferred** (recorded in
`DELIVERY_ROUND22.md`) — the Linux core, the full test suite, and the
evidence batteries are unaffected (the VST3 wrapper sources are unchanged
this round).

---

## 1. P1 — the Minimum Pitch signal path, end to end

### 1.1 The question, precisely

The original's static evidence (R21, `docs/R21_FEATURE_RECONSTRUCTION_MATRIX.md`
element 21) bounds a "Low Note" control (the UI string table carries
"Minimum Pitch"), and our engine has carried TWO independent floors since
round 21:

* `Config::f0_min` (default **60 Hz**) — the detector floor: the lag domain is
  `tau_max = sample_rate / f0_min`; the detector zeroes any estimate below it;
* `Config::fmin` (default **55 Hz**) — the ENGINE's spacing-accept gate: the
  delay-line grain spacing `S` only tracks a committed period `T = sr/f0` when
  `fmin <= f0 <= fmax` (the R12 law).

R21 left the interaction unexercised: what happens to a tone that is
*detectable* but below `fmin`; whether lowering `f0_min` below `fmin` changes
any output; and where, in the 8-level chain from the detector lag domain to
the voice pitch ratio, a low tone actually dies. R22-P1 closes this with an
engine-level unification, per-level diagnostics, and a measured matrix.

### 1.2 The engine change (one law, made explicit)

`AlignedDelayLine`'s spacing-accept gate (dsp.cpp `updateSpacingTarget`) is
now the **unified floor**

```
fmin_eff = max(fmin, f0_min)          # accept iff f0 != 0 && fmin_eff <= f0 <= fmax
```

**Bit-exactness proof (why this changes no existing output).** Every NONZERO
committed estimate already satisfies `f0 >= f0_min` (the detector zeroes
anything below its own floor), so for the entire historical regime
`fmin <= f0_min` — which includes the default `fmin 55 < f0_min 60` — the two
gates accept exactly the same estimate stream: lowering `fmin` below the
detector floor has ALWAYS been dead configuration. The unification makes the
detector dominance EXPLICIT: the engine can never lock spacing onto a period
the detector refuses to commit. The surviving distinction is at the OTHER
edge: when `f0_min` is configured BELOW `fmin` (e.g. 40 Hz), the detector
commits tones in `[f0_min, fmin)` that the engine STILL refuses to lock onto —
*a tone may be detectable yet not spacing-trackable* — and that decision is
now recorded per estimate.

**The four diagnostics** (dsp.hpp, levels 1–5 of the chain, observable per
engine):

| Accessor | Level | Meaning |
|---|---|---|
| `spacingGateHz()` | 4 | the effective gate `max(fmin, f0_min)` |
| `lastEstimateSpacingAccepted()` | 3→4 | whether the LAST committed estimate ALSO passed the engine gate (the explicit detectable-vs-trackable distinction) |
| `spacingHistoryCount()` | 4→5 | accepted periods currently in the median history (0..`f0_history`) |
| `spacingTargetSamples()` | 5 | the current spacing target `S_target` (= `grain` while nothing accepted) |

Levels 6–8 (read position, voice pitch ratio, low-frequency safety) are
measured by the battery below and asserted by the new unit cases.

### 1.3 The 8-level chain and the 10-frequency matrix

**`cli/r22_minpitch.cpp`** walks the whole chain per cell —
10 tones × 4 floors × 3 transpositions × 3 detectors = **360 cells**
(4 s, 48 kHz, the r21_fmin synthetic generator: harmonic stacks + ±2-cent
jitter + noise floor, honestly labelled SYNTHETIC):

* tones 32.7 (C1) · 41.2 (E1) · 49.0 (G1) · 55.0 (A1) · 58.3 (Bb1) · 62.0 ·
  65.4 (C2) · 82.4 (E2) · 110.0 (A2) · 220.0 (A3, in-range control)
* floors `f0_min` 40 · 55 · 60 (default) · 80
* shifts 0 · −12 · −24 st; modes Decimated · Hybrid · FullRate

**Verdict taxonomy** (the P1 goal — "detectable" vs "safely transposable"):

| verdict | criterion |
|---|---|
| `not_detectable` | detection success < 50% (est within 50 cents of the tone) |
| `detectable_only` | detected but the spacing path never locked (level 4/5 rejected) |
| `below_audible_floor` | locked, output fundamental < 20 Hz (honest record — **the engine never clamps**) |
| `safely_transposable` | locked, output ≥ 20 Hz and within 150 cents of `ratio·f0` |

### 1.4 Results (`evidence/analysis_round22/r22_minpitch.json`, 360 cells)

**Summary: not_detectable 141 · detectable_only 18 · below_audible_floor 31 ·
safely_transposable 170.** The structure is exactly the designed semantics:

* **The distinction band is clean.** All 18 `detectable_only` cells are
  precisely the tones in `[f0_min, fmin) = [40, 55)` — 41.2 and 49 Hz at
  floor 40 (×3 shifts ×3 modes): detected (success 0.94), accepted by the
  gate at ratio ~0.00–0.01, spacing never locked (`S` stays at the
  un-updated 4118–4331), output fundamental nevertheless present (see
  §1.6). No other cell lands in the category.
* **In-range tones are floor-independent.** 82.4, 110, 220 Hz:
  `safely_transposable` in all 36 cells each — at EVERY floor, including
  floor 80 (where the detector still tracks them: the gate rejects nothing
  above the floor).
* **`below_audible_floor` fires exactly at −24 st on 55–65.4 Hz** (11
  tone×floor combinations ×3 modes, minus the 58.3@60 boundary cells, see
  §1.5): outputs 13.75–16.35 Hz — detected, locked, tracked, and honestly
  BELOW the audible floor. The engine applies **no clamp** (the
  `minpitch_lowfreq_transposition_honesty` unit case proves
  `measured_out_hz = 16.25` for 65.4@−24 st); the recommendation lives in
  the docs, not the DSP.
* **`not_detectable` is the R21 near-floor law, not news**: every
  tone-below-its-floor cell fails at level 1–2 (the detector commits wrong
  near-floor estimates rather than zeros — the R21 `r21_fmin.json` measured
  behaviour, unchanged), e.g. 62 Hz @ floor 80: success 0.000 with the
  committed median near 80 Hz.
* **CPU** (median ns/sample over the matrix): Decimated 449 · Hybrid 481 ·
  FullRate 1753 — the R21/R20 ratios hold (Hybrid ≈ +7%, FullRate ≈ 3.9×).

### 1.5 Boundary finding — tones within ~4% above the floor are mode-dependent

58.3 Hz @ floor 60 (the tone only 3.4% above `f0_min`): **Hybrid detects it**
(success 0.62, verdict safely_transposable/below_audible_floor) while
**Decimated and FullRate do not** (success 0.000, committed median 60.1–60.2
Hz — the estimate SNAPS to the floor). The practical reliable detection
floor sits somewhat ABOVE `f0_min` for the decimated family; the hybrid's
coarse-to-fine refinement straddles the boundary (its accepted `S`-implied
frequency lands 49.9 cents off the tone — a knife-edge pass of the 50-cent
lock criterion, recorded as-is). Honest guidance: treat
`f0_min + ~5%` as the reliable floor, and expect mode-dependent verdicts in
the boundary band.

### 1.6 The masked-output phenomenon (recorded, not fixed)

Cells whose spacing never locked can STILL measure a correctly-shifted
output fundamental (e.g. 49 Hz @ floor 40, −12 st: `fout = 24.5` Hz spot-on)
— because the delay-line read advances at `ratio` speed regardless of `S`,
and with sparse misaligned wraps the commit crossfade masks the phase
resets. The verdict taxonomy deliberately requires the level-4/5 lock for
`safely_transposable`: a static-tone window can look clean while the
mechanism is misaligned; the artifacts would surface as rare phase
discontinuities on real material. **Do not read `fout` alone as safety** —
that is the entire point of the detectable/trackable distinction.

### 1.7 Instrument corrections (honest record — fixed BEFORE delivery)

The first battery run (same round, pre-delivery) contained two measurement
defects in OUR harness, both root-caused and fixed in
`cli/r22_minpitch.cpp`; the delivered `r22_minpitch.json` is the fixed
instrument's run:

1. **Output-fundamental estimator (`acFreq`)** used the GLOBAL autocorrelation
   argmax. On smooth low-frequency outputs the trivial near-zero-lag
   "shoulder" wins (observed on the 65.4@−24 st cell: `r[3] = 0.9896`
   beating the true fundamental peak `r[367] = 0.9382`, reporting 2000 Hz
   for a 16.35 Hz output; zero-crossing counting over-counts ~2× on the
   harmonic stack for the same reason). Fix: a fundamental is always a LOCAL
   maximum — pick the smallest local peak within 0.25 of the highest
   (≥ 0.30 absolute, lags ≥ 8), parabolic refine; dead/aperiodic outputs
   (peak < 0.30) return 0.
2. **Spacing-lock criterion** compared `S_target` against `k·(CLI-side median
   estimate)` with a 0.51-sample tolerance, conflating the ESTIMATE
   QUANTIZATION (one decimated lag = 4 fullrate samples; up to ~`k·4` ≈ 74
   samples at `k = 19`) with lock failure — perfectly locked cells (output
   within cents of `ratio·f0`) scored `detectable_only` (195 cells in the
   first run vs 18 after the fix). Fix: tie `S` to the KNOWN tone —
   `kq = round(S/T_true)`, lock iff `|cents(kq·sr/S, tone)| ≤ 50` AND the
   engine's own acceptance state (`spacingHistoryCount() > 0`) AND the
   voice-follows gate (`commits > 0` or unity parking).

### 1.8 New unit gates (suite 96 → 100 cases)

| case | asserts |
|---|---|
| `minpitch_gate_unification_bitexact` | the dominance proof in practice: `fmin` 30/40/55 produce BIT-IDENTICAL output to the default at `f0_min 60` (dead configuration), effective gate = 60; `fmin 80` (above the floor) is ALIVE — rejects in-floor-band estimates, output differs |
| `minpitch_distinction_diagnostics` | the three-way distinction through the R22 accessors: 45 Hz @ `f0_min 40` detectable but NOT spacing-trackable (gate 55 governs); 70 Hz trackable; 45 Hz @ `f0_min 80` lost at the detector (near-floor wrong estimates — asserted NOT-within-50-cents, honestly, not `est == 0`) |
| `minpitch_lowfloor_full_path` | 58.3 Hz @ `f0_min 40` walks the whole chain: estimate → gate → history (8/8) → `S = 4116.63` ≈ `k·T` → voice spacing follows to 1e-3 samples → read-position continuity → shifted output |
| `minpitch_lowfreq_transposition_honesty` | 65.4 Hz @ −24 st → measured output 16.25 Hz vs expected 16.35: **no clamp** — the sub-20 Hz result stands, RMS 0.354 |

---

## 2. Regression gates

### 2.1 Suite

**100/100** (`evidence/analysis_round22/logs/r22_core_suite.txt`, release
build): 96 R21 cases + 4 new R22 cases; ASan/UBSan-clean lineage unchanged.

### 2.2 Reference hashes

All reference hash pins (r15 legacy ×4, 16.0.0 defaults ×4, the R18/R19 OBS
records) **unchanged and matching** — the unified gate is provably
output-neutral at every historical configuration (§1.2), confirmed by the
in-suite hash checks (`cfg1..4 hash_match = 1`, replay/reset/prepare
determinism hashes equal).

### 2.3 Compatibility

No parameter added, removed, or renumbered (host contract IDs 0–37 intact);
no state-format change; `versionString()` 16.1.0 → **16.2.0**; the VST3
wrapper sources untouched (the diagnostics are engine-side, additive).

---

## 3. What this round did NOT do (honest scope)

* **P2 (real-recording three-detector comparison at low floors)** — deferred;
  the R21 real-audio harness (`cli/r21_realaudio.cpp`) is available and the
  R22 diagnostics plug into it, but no genuine recorded corpus was measured
  this round.
* **P3 (per-voice Pan/Feedback) and P4 (harmony mapper host exposure,
  IDs 38+)** — deferred to the next round; the independent harmony mapper
  (R21) is in-tree and unchanged.
* **P5 (static RE continuation — the Randomize polarity contradiction
  etc.)** — untouched this round; the open-questions ledger is unchanged.
* **P6 (stereo regression)** — covered transitively by the 100-case suite
  (all dual-mono/stereo integrity cases green); no new stereo battery.
* **Windows delivery** — deferred (toolchain lost in the environment
  rollback, §0).
