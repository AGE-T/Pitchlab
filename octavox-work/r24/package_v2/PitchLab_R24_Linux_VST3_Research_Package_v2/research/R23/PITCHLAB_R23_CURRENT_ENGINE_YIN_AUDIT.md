# PITCHLAB R23 — Current-Engine YIN Audit (Gate 2)

**Scope:** forensic audit of the delivered R22 engine's pitch-detection and
spacing-control path, traced in actual source (not comments), as the factual
baseline for the R23 YIN research and the Pitch Control Layer design.
**Base:** `Octavox_RE_evidence_round22.zip` (SHA-256
`782e7ab8111630518eb99ffb7cb7e7403c7917af410ecc48e1f859936b0a1e61`, verified
2026-11-04), unpacked to `octavox-work/r23/baseline/`; the working tree
`octavox-work/r23/source/` is a byte-verified copy of its `native/` subtree
(639/639 files checked at extraction; clean rebuild passes 100/100 —
`evidence/analysis_round23/logs/g0_baseline_suite.json`).
**Evidence classes:** OBS = read directly from the source lines cited;
DER = mathematically derived from OBS; HYP = hypothesis (to be tested in later
gates); UNK = unresolved. File/line numbers refer to `r23/source`.

---

## 0. Parameter-surface fact (resolves the brief's "IDs 0–73")

* [OBS] The delivered R22 plugin exposes **38 parameters, ids 0..37**
  (`vst3/octavox_params.h` L69–86: `kNumParams = 38`; 6 global ids 0–5 +
  4 per voice × 8 voices, bases 6..9 + 4·v). State blob layout v2 is frozen;
  `Config::f0_min` is deliberately NOT serialized (dsp.hpp L407–410).
* [OBS] The number 73 in the R23 brief matches the **original** Eventide
  Octavox static-RE finding "73 host parameters fully mapped"
  (`OCTAVOX_AGENT_HANDOVER_REPORT.md`), not our surface.
* **R23 compatibility rule adopted:** preserve our ids 0..37 exactly, no
  renumbering, v2 state compatibility; any new R23 control-surface additions
  would extend beyond 37 only via a documented, migration-tested layout —
  and per the R23 brief's "no unnecessary host parameters" rule the new
  detector/control-layer configuration stays engine-internal, not host-facing.

## 1. Component inventory of the audited path

| Component | File:lines | Role |
|---|---|---|
| `PitchDetector` | `src/dsp.cpp` L313–755 | YIN-family estimate, 3 modes |
| `Impl::updateSpacingTarget` | L934–964 | accept gate + period history/median → S_target |
| `Impl::commitSpacing` | L973–996 | wrap-time spacing commit (hysteresis + crossfade) |
| `Impl::exitCommitSpacing` | L1000–1010 | unity-exit commit (read-point preserving) |
| `Impl::readVoiceAt` | L1041–1073 | dual-tap grain read + raised-cosine weights |
| `Impl::processVoiceSample` | L1078–1142 | ratio glide, grain phase ramp/wrap, crossfades |
| `AlignedDelayLine::process` | L1609–1736 | control cadence, ring I/O, feedback, mix |
| `MultichannelShifter` | L1758–1887 | one engine per channel (independent detectors) |
| VST3 `process` | `vst3/octavox_native_processor.cpp` L344–441 | sample-accurate param segmentation |

## 2. PitchDetector — exact behaviour (OBS unless noted)

### 2.1 Configuration law (`configure`, L318–372)

* `f0_min` clamped to [20, 200] Hz, default 60.0 (L321).
* Decimated/Hybrid: `decim_ = lround(sr/24000)` reduced while `sr/d < 18000`
  (`decimationFor`, L684–691) → D = 2/2/4/4/7/8 at 44.1/48/88.2/96/176.4/192 kHz;
  `sr_dec_ = sr / decim_` — **integer division** (L324). Exact at the canonical
  rates (44100/2=22050, 176400/7=25200); [OBS] no truncation-bias audit exists
  for non-canonical rates → carried to the G3 window study.
* Lag domain (decimated): `tau_min = max(2, int(sr_dec/1200))`,
  `tau_max = int(sr_dec/f0_min)`, `win = max(1024, ceil(2.5·tau_max))`,
  `tau_max = min(win−2, tau_max)` (L325–328).
* FullRate: same law at the host rate with `win = max(2048, ceil(2.5·tau_max))`
  (L355–360) and `tau_lo = tau_min` (L361).
* Hybrid fine stage: host-rate window `win_raw = max(2048, ceil(2.5·tau_max_raw))`,
  `m_raw = win_raw/2 − tau_max_raw` (L339–346).

### 2.2 Difference function and support (L459–485)

* Reference segment = **second half** of the rolling window: `seg0 = win/2`;
  comparison length `m = win − seg0 − tau_max = win/2 − tau_max` (L460–461).
  d(tau) = Σ_{j=0}^{m−1} (x[seg0+j] − x[seg0+tau+j])² over tau ∈ [tau_lo, tau_max).
* [DER] Support sizes @48 kHz, f0_min 60: FullRate tau_max=800, win=2048,
  **m=224** terms (≈ 0.28·T_min; the brief's "≈224" confirmed); Decimated
  (D=2) tau_max=400, win=1024, m=112. At a 40 Hz floor: FullRate tau_max=1200,
  win=3000, m=300.
* [DER] This is a **fixed-m, growing-support convention**: the shifted segment
  [seg0+tau, seg0+tau+m) always fits because seg0+tau_max+m = win. It is NOT
  the canonical paper's fixed-W form over a one-sided frame, and NOT the
  shrinking-support (N−tau) form of librosa/aubio — it is a third convention
  ("second-half segment"). Comparison consequences: G4/G5.
* [DER] **Temporal stability caveat (paper's W ≥ T_max guideline):** with
  m ≈ 0.28·T_min the difference function integrates over well under one
  period of the lowest tone; the estimate's variance vs the canonical
  fixed-W ≥ T_max form is a measurable property (G5 matrix), not assumable.

### 2.3 CMNDF — truncated normalization (L487–491)

`run += d[i]; cmnd_[i] = d[i]·(i+1)/run` for i over [tau_lo, tau_max).
* [OBS] The cumulative mean starts at **tau_lo**, and the multiplier is the
  number of accumulated terms (i+1), not the lag tau. FullRate: tau_lo =
  tau_min (= sr/1200; 40 @48k) — **not** the canonical lag-1 start.
  Decimated/Hybrid: tau_lo = max(2, tau_min−18) ("warms" 18 lags below the
  floor; L329–331, rationale at L466–475: un-warmed normalization biased the
  refined minimum by 3.46 cents at 880 Hz, D=2).
* [DER] Relation to canonical `d'(tau) = d(tau)/((1/tau)·Σ_{j=1..tau}d(j))`:
  different numerator AND denominator population; the deviation magnitude on
  our corpus is quantified in G4 (reference vectors) and G5 (battery).
  [OBS] Preserved deliberately this round as the legacy baseline per the R23
  brief §16.1 ("legacy FullRate reference" label adopted below).

### 2.4 Peak gate, threshold, selection, refinement (L445–527)

* Peak gate: max|x| over the (filtered, in decimated modes) window < 1e-6 → 0
  (L449–454) — an ABSOLUTE level gate (amplitude-scaled, not dB; the
  level-dependence audit is G3-d).
* Threshold **0.20** (L495; same in the Hybrid coarse copy L579) — vs paper
  0.10–0.15, aubio 0.15, librosa 0.1.
* Selection: **first local minimum below threshold** from tau_min+1, with
  non-strict plateau comparisons (`<=` both neighbours, L497); **fallback =
  global argmin from tau_min** (L502–507) — structurally the paper's rule
  (first-below-threshold, else global min) with threshold 0.20 and the
  truncated CMNDF.
* Parabolic refinement: FullRate on **cmnd_** (verbatim legacy); Decimated on
  the **raw d_** (L509–521; measured rationale: CMNDF's local slope skews the
  vertex ~1 cent on the coarse grid); guard |den| > 1e-15, **no vertex clamp**
  in estimate() (the Hybrid fine stage clamps ±1, L671–675, after a measured
  800+ runaway at edge argmins).
* Validity: `f0 = sr_dec/tau` accepted only into [f0_min, 1200] Hz else 0
  (L524–526). **No reason codes** — 0 conflates silence, unvoiced, out-of-range
  and numeric-invalid (the telemetry gap G6 closes).

### 2.5 Hybrid fine stage (L601–682)

tau_c = tau_dec·D clamped into [tau_min_raw, tau_max_raw−1]; neighbourhood
half-width W = D+2; raw-difference argmin over the neighbourhood with m_raw
support; ±1-clamped parabolic on raw d; host-rate f0. [OBS] The clamp laws and
their measured justifications are documented inline (55 Hz corpus drop 51–61%
pre-clamp; ASan boundary at best+1).

### 2.6 Decimation path (L406–443, L693–710)

* Causal 3×TDF2-biquad chain, Butterworth 6th (Q 0.5176/0.7071/1.9319),
  **fc = 0.35·sr_dec** (L695). Decimation grid = global stream position
  ((start+i) % D == 0) — state persists across arbitrary block sizes [OBS].
* [DER] Attenuation at the decimated Nyquist (sr_dec/2 = fc/0.7):
  ω = 1/0.7 = 1.4286·fc → |H| = 1/sqrt(1+ω^12) ≈ **−18.6 dB**. Content within
  ~[fc, Nyq] folds back attenuated only ~19–35 dB; harmonics/noise above 8.4 kHz
  (at D=2) alias into the analysis band. Measured response + alias-rejection
  audit: G3-d filter study. (The 0.35 law is legacy-measured, not assumed
  adequate.)

### 2.7 Control cadence and engine integration (L1624–1645)

* The detector consumes the **RAW input** (pre-feedback), accumulated in
  `det_pending`; an estimate fires every `detection_interval` = 64 stream
  samples (default) — cadence ≈ 750 Hz @48k; the estimate applies from the
  boundary sample onward; documented ≤1-interval (≤64-sample ≈ 1.33 ms)
  additional control lag vs the offline Python reference (L1627–1632).
* [DER] Analysis time span: win/sr_dec ≈ 1024/24000 ≈ **42.7 ms**; the
  estimate's information age at the boundary sample ≈ the second-half-segment
  convention's effective centre — the exact timestamp convention (the window
  ENDS at the boundary; the reference segment starts at win/2 back) is derived
  in the G3 window/lag/latency study.

## 3. updateSpacingTarget — the accept gate and median (L934–964)

* [OBS] R22 unified floor: `accept = (f0 != 0 && max(fmin, f0_min) <= f0 <=
  fmax)` (L936–938); accepted periods (sr/f0) enter a circular history of
  `f0_history` = 8; S_target = clamp(k·median, 0.5G, 1.5G) with
  k = round-half-even(grain/median), kk = max(1,k) (L950–963).
* [OBS] **The only temporal filters between detector and engine are: the
  8-entry median + the 0.005-relative commit hysteresis.** No confidence
  gating, no outlier rejection, no octave-jump detection, no voiced/unvoiced
  state, no lock acquisition/release semantics, no slew limiting of S_target
  beyond the commit hysteresis. A single accepted outlier enters the history
  immediately and can move the median for up to 8 cadences (≈10.7 ms @48k
  per cadence × 8 = 85 ms window). → This is precisely the insertion point
  the R23 Pitch Control Layer is specified to occupy (Gate 6).
* [OBS] Rejected estimates (below the unified floor, above fmax, or 0) are
  simply **not accumulated** — the history HOLDS its old contents (no decay,
  no explicit loss-of-lock policy; an indefinitely static history with
  `have_estimate = true` persists across silence — the "indefinite freeze"
  behaviour the brief flags; measured consequences: G7).

## 4. Grain/read path (for the artifact investigation, Gate 7)

* [OBS] Grain phase law `frac += (1 − r)/S` per output sample (L1084) — the
  brief's expression verified verbatim; wrap at frac ≥ 1 (r ≤ 1) / frac < 0
  (r > 1) with the read-point-preserving rescale `frac_new = (frac∓1)·S_old/S_new`
  (L1086–1103).
* [OBS] Weighting law `w = 0.5 − 0.5·cos(π·frac)` — a **raised cosine**
  (L1056, L1062); the earlier "triangular window" description is refuted for
  this code path (the triangular claim is not in the active source).
* [DER] **Read-rate convention:** tap delay d = base + frac·S advances at
  (1−r) samples per output sample ⇒ read rate = r. Negative shifts (r < 1)
  read SLOWER (upsampling-like; no new aliasing); positive shifts (r > 1) read
  faster (aliasing risk for content above sr/(2r) — the engine has NO
  pre-read anti-alias filter; candidate artifact mechanism for Gate 7, to be
  measured, not assumed). The brief's caution is confirmed: "negative pitch
  downsamples" is backwards under this convention.
* [OBS] Dual-tap crossfade continuity at wrap: tap roles swap (A@base+(1−ε)S
  weight→1 pre-wrap; B@base+S weight 1 post-wrap) — continuous for constant S;
  spacing commits (ΔS ≥ 0.5% by hysteresis) jump the dominant read delay by
  ΔS, **mitigated by the default commit crossfade** (commit_crossfade_seconds
  = 0.008, dsp.hpp L453; shadow-voice blend L976–991, clamped ≤ 40% of the
  wrap period).
* [OBS] Interpolation is **phase-quantized**: Lagrange6 uses a 2048-bin
  table indexed `ph = int(mu·2048)` (L889–895; ObsKernel6/Q use 256 bins) —
  the fractional read position is quantized to 1/2048 (≈ 0.049% of a sample);
  [HYP] periodic interpolation-error steps at bin crossings are a candidate
  high-frequency "crackle" mechanism during slow read-rate sweeps (negative
  shifts) — measured in Gate 7.
* [OBS] Ratio glide: geometric `ratio = ratio_t + (ratio−ratio_t)·0.9995`
  per sample (L1080) — time constant ≈ 2000 samples ≈ 41.7 ms @48k.
* [OBS] Feedback: one-sample global loop `xin = in + fb·y_prev` (L1655–1657,
  L1697–1698); dry path never carries feedback; wet/dry with latency-aligned
  dry ring (read(d) = sample d−1 ago; L859–869).
* [OBS] latencySamples() = base_delay + grain/2 (L1604–1607) — the dry-mix
  alignment constant, not a worst-case read delay.

## 5. Channel architecture and the VST3 path (stereo-offset facts)

* [OBS] `MultichannelShifter` = **one independent AlignedDelayLine per
  channel**, each with its own detector/history/grain state (L1799–1808,
  L1878–1880). No shared tracker, no linked-stereo option (the R21 linked
  tracker remained a design, not code — R21_DUALMONO_STEREO_CORRECTION.md).
* [OBS] VST3: stereo in/out buses by default (`octavox_native_processor.cpp`
  L74–75); `setBusArrangements` re-prepares the engine for the negotiated
  channel count (L93–117); process() does sample-accurate parameter
  segmentation + alias-safe scratch copies (L316–342); unprepared ⇒ honest
  zero output with silenceFlags cleared (L345–355).
* [OBS] Prior evidence carried forward: dual-mono transparency is
  bit-identical at the native engine level in every tested configuration
  (R21 `r21_stereo.json`, 0 violations; classes (1)–(4) documented), BUT per
  the R23 brief a native pass is not proof of the VST3/DAW path — the wrapper
  and module layers are re-verified in Gate 7 with dual-mono input through
  the actual `process()` segmentation path.

## 6. The five-signal separation (brief §16.2) — verified in source

| Signal | Where it lives | R23 boundary |
|---|---|---|
| Input F0 estimate | `PitchDetector::estimate()` return, `last_f0_` (L526) | detector only |
| Tracking decision | `updateSpacingTarget` accept test (L936–938) | engine-side gate — the control layer's insertion point |
| Engine spacing target | `S_target` (L822, L963) | engine |
| Per-voice transposition ratio | `VoiceState::ratio_t/ratio` + glide (L761–762, L1080) | **never** touched by the detector |
| Audio read/grain state | frac/S/ring/interpolators (L1041–1142) | **never** touched by the detector |

[OBS] The detector already only communicates f0 through `last_f0_` → the
engine — the architecture is separable today; the R23 control layer replaces
the accept test + history discipline, not the per-voice ratio path.

## 7. Audit conclusions for the R23 plan

1. The current FullRate path is a **legacy project reference** ("legacy
   FullRate"), NOT canonical YIN: truncated CMNDF (start at tau_min, count
   multiplier ≠ lag), second-half-segment support m = win/2 − tau_max ≪ T_max,
   threshold 0.20, no Step 6. Canonical comparison: Gates 4–5.
2. The engine's temporal protection is a single 8-median + commit hysteresis;
   there is no confidence/lock layer — the G6 control layer is additive at a
   well-defined boundary (accept test → history), with double-smoothing risk
   to be quantified against the existing median (three integration
   strategies per brief §16.2 to be measured, not stacked).
3. Artifact candidates isolated for Gate 7 (all HYP until reproduced):
   (a) phase-quantized interpolation steps under slow read sweeps;
   (b) spacing-commit read-delay jumps (crossfaded by default; the
   crossfade-off legacy path remains testable via config);
   (c) no pre-read anti-alias filter for r > 1;
   (d) outlier-driven S_target motion through the ungated median;
   (e) per-channel detector divergence under non-identical L/R input.
4. Detector telemetry is a bare double (f0) — the G6 layer needs the
   structured estimate (cmndf minimum, reason codes, validity) which requires
   a **detector-side extension** that keeps the legacy estimate bit-exact
   (side-channel outputs only; Gate 8 integration rule).
5. [UNK] `sr/decim_` integer division at non-canonical rates (e.g. 44.056 kHz
   Morse-legacy rates) — bounded-audit item in the G3 window study.
