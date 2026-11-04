# R22 — The Complete Minimum Pitch Signal Path (PRIORITY 1)

**Product:** PitchLab Harmonizer (independent engine, `octavox-native 17.0.0`).
**Scope:** the normative trace of every stage between "the user lowers the
minimum pitch" and "the engine actually shifts that low", the coherent
policy that binds the stages, the measured battery, and the
detectable-vs-safely-shiftable distinction in the API.
**Provenance:** all DER (independent measurement) unless marked OBS (static
observation of the original binary) / UNK. The original Eventide Octavox
binary was never executed.

---

## 0. The problem being solved (as inherited from R21)

R21 introduced `Config::f0_min` (default 60 Hz, configurable 20–200 Hz) and
measured the DETECTOR's quality per candidate floor. But the ENGINE's
spacing logic had an independent, fixed 55 Hz gate (`Config::fmin`, the
spacing-accept floor). Consequence: lowering `f0_min` to, say, 32.703 Hz
made the detector commit 32.7 Hz estimates that the spacing gate silently
discarded — detection improved, but the pitch-shifting engine did NOT
follow. A frequency could be *detected* yet not be *spacing-locked*, with
no way to tell the two capabilities apart from the API.

R22 resolves this with a measured, coherent policy — not by deleting the
55 Hz gate.

## 1. The eight stages, traced

### Stage 1 — Minimum detector frequency (`Config::f0_min`)

`native/src/dsp.cpp`, `PitchDetector::configure()` (R21). `f0_min` sizes the
lag domain (`tau_max = int(rate / f0_min)`, per mode: `sr` for FullRate,
`sr_dec` for Decimated/Hybrid coarse, `sr` again for the Hybrid fine stage)
and lower-bounds the estimate validity gate `[f0_min, 1200] Hz`. Validated
into `[20, 200]` and `< fmax` at `prepare()`. Default 60.0 reproduces the
R12–R20 fixed law bit-exactly.

### Stage 2 — Pitch tracker lag range

* FullRate: `win = max(2048, ceil(2.5·tau_max))`, difference support
  `m = win/2 − tau_max ≥ tau_max/4` (value-mirror lag immunity, R13-b).
* Decimated: `win_dec = max(1024, ceil(2.5·tau_dec))` at `sr_dec`
  (D = max(1, lround(sr/24000)); the anti-alias cascade is a 6th-order
  Butterworth at 0.35·sr_dec) — same ~42.7 ms time span.
* Hybrid: the coarse stage above, plus the fine stage's raw window
  `win_raw = max(2048, ceil(2.5·tau_max_raw))` at the host rate with
  `m_raw` support; the refinement neighbourhood is `W = D + 2` host samples
  around `tau_c = tau_dec·D`, clamped into the lag domain.

At `f0_min = 20` and 48 kHz: `tau_max = 2400`, `win = 6000`, `m = 600`
(exactly the law's floor `tau_max/4`). At `sr_dec = 24000`: `tau_dec = 1200`,
`win_dec = 3000`. All buffer sizes derive from these and are allocated at
`prepare()`; `f0_min` is buffer-determining (an in-place re-prepare requires
a matching `f0_min`).

### Stage 3 — Tracker confidence and rejection

The YIN-family estimator (identical law in all three modes): threshold
search with the fixed acceptance constant, cumulative-mean normalisation,
parabolic refinement, then the validity gate `[f0_min, 1200] Hz`. An
estimate outside the gate becomes 0 (rejected) — `lastEstimateHz()`
reports the last committed estimate. Every NON-ZERO estimate passed to the
engine increments `PitchPathStats::detected` (R22; the detectable
capability).

### Stage 4 — Period conversion

`Impl::updateSpacingTarget(f0)` converts an accepted estimate to a period
`t = sample_rate / f0` and appends it to the `f0_history` circular buffer
(default 8 entries; one estimate per `detection_interval = 64` stream
samples). **This is where the R22 coherence law applies** (Stage 5 gate).
The median of the history (`np.median` parity: even counts average the
middle two) becomes `t_hat`.

### Stage 5 — Grain spacing (the previously-fixed 55 Hz gate)

`S = k · t_hat` with `k = max(1, round(G / t_hat))` (half-to-even,
`std::nearbyint`), clamped to `[0.5·G, 1.5·G]` (`G = Config::grain`,
default 4096). `S` must be an exact integer multiple of the input period
for the dual-tap grain law to be periodicity-preserving; the clamp is the
only mechanism that can break that (see the safety floor below).

**The R22 coherent gate (the fix):** the estimate now feeds the history iff

```
spacing_fmin ≤ f0 ≤ cfg.fmax,   spacing_fmin = max( min(cfg.fmin, cfg.f0_min),
                                                    sample_rate / (1.5 · grain) )
```

With every default config (`fmin = 55`, `f0_min = 60`, `grain = 4096`) this
is **exactly 55.0 at every canonical rate** (the safety floor is
7.8125 Hz at 48 kHz, 15.625 at 96 kHz, 31.25 at 192 kHz — all far below
55), so the accepted set is bit-identical to ≤ 16.1.0; the 96-case suite
and the four r15 reference hashes gate this. When `f0_min` is lowered below
`fmin`, the engine follows the detector down — but never below the safety
floor. Estimates below `spacing_fmin` are counted in
`PitchPathStats::spacing_rejected` (**detected but not safely shiftable**);
accepted ones in `spacing_accepted`.

*Deliberate, documented behaviour change* (the only one): a configuration
with `cfg.fmin` ABOVE `cfg.f0_min` (e.g. fmin = 70, f0_min = 60) previously
rejected detector-valid estimates in [60, 70) from the spacing history; the
coherent law now accepts them (`min(70, 60) = 60`). The "engine gate above
the detector gate" state was itself the incoherence this round removes. No
shipped configuration uses it: `fmin` is written only through the engine
Config (the VST3 surface does not expose it; the state blob carries the
default 55 unless a tampered blob is restored).

### Stage 6 — Delay read position

The grain law (R15/R19, unchanged): tap A reads `d = base + frac·S`, tap B
at `d + S`, raised-cosine crossfade over `frac`; `frac` advances
`(1 − r)/S` per sample and wraps into a spacing commit (hysteresis
`s_hysteresis`, R19-b click-free crossfade). The maximum read delay is
`base + 32·(mv−1) + S + 3 kernel taps`, and `prepare()` rejects any config
where `base_delay + 32·(max_voices−1) + 3·grain + 8 ≥ 2^18` — since
`S ≤ 1.5·grain` by the clamp, **ring safety is config-validated for the
whole spacing range, independent of frequency**. Low frequencies add no new
read-delay exposure.

### Stage 7 — Voice pitch ratio

`ratio = 2^((global_shift + voice_offset)/12)` (spread table or per-voice
override; R15-P1 API `setVoiceRatio`). The voice ratio is frequency-blind:
downshifts extend the wrap period (`S/(1−r)`), upshifts shorten it. The
battery measures the OUTPUT pitch (independent autocorrelation estimator,
never the engine's own detector) at ±7/±12/+0/−24 semitones for every
frequency point.

### Stage 8 — Lower frequency safety and buffer constraints

1. **Spacing safety floor** `sr/(1.5·grain)`: a period longer than `1.5·G`
   would clamp `S` below one input period, breaking A/B periodicity — the
   engine refuses to lock (counts as spacing_rejected). At 48 kHz/G=4096:
   7.8125 Hz. This is the *mathematical* shiftable floor for the grain law.
2. **Ring bound**: config-validated at prepare (above), never frequency-
   dependent.
3. **Detector window bound**: `win ≤ 2.5·sr/f0_min ≤ 2.5·10^6/20 = 125000`
   (fits int; `sr ≤ 10^6` and `f0_min ≥ 20` are prepare-validated).
4. **Unity voices park** (`S` irrelevant); shifted voices engage
   identically at any lockable frequency.

## 2. The API distinction (detectable vs safely shiftable)

* `lastEstimateHz()` — the DETECTABLE capability: the last estimate that
  passed the detector gate `[f0_min, 1200]`.
* `minSpacingLockHz()` (R22, `AlignedDelayLine` + `MultichannelShifter`) —
  the effective spacing floor actually enforced: an input is SAFELY
  SHIFTABLE (spacing-lockable) iff its detected frequency is
  `≥ minSpacingLockHz()` and `≤ fmax`.
* `PitchPathStats pitchPathStats()` (R22) — the measured split:
  `detected / spacing_accepted / spacing_rejected` counters, reset by
  `reset()`, per channel on `MultichannelShifter`.

Documentation rule (mirrored in the real-audio harness): "detected" never
implies "shiftable"; the two are reported separately everywhere.

## 3. The measured battery (`octavox_r22_lowfreq`)

Synthetic corpus (honestly labelled — the real-recording category is
`octavox_r21_realaudio`'s): 6 s harmonic stacks with ±2 cent slow jitter
and a noise floor, at the ten specified frequency points
{32.703, 36.708, 41.203, 49.000, 55.000, 60.000, 65.406, 73.416, 82.407,
110.000} Hz; `f0_min = 0.98·f0`; all three detector modes; intervals
{+12, +7, 0, −7, −12, −24} semitones; 186 runs total,
`evidence/analysis_round22/r22_lowfreq.json`.

Headline results (48 kHz, identical settings across modes):

* **Detection**: median estimate within ±0.93 cents of the known F0 at
  every point (the corpus has a ground truth by construction).
* **Spacing lock**: `S` locks to `k·t` (k = round(G/t)) at every shifted
  run; worst relative deviation vs the closed form 0.1 % (median-window
  granularity). Unity runs park (S stays at the grain default — correct).
* **Output pitch** (independent AC estimator on the last 1 s): within
  ±1.9 cents at all points/intervals down to −12 st; at −24 st the worst
  is −3.9 cents (110 Hz → 27.4 Hz; the estimator itself coarsens below
  ~30 Hz). **−24 semitones is supported at every point**: outputs reach
  8.17 Hz (from 32.703 Hz) — below the 20 Hz human-pitch floor but
  mathematically clean (finite, bounded, tracked).
* **All three modes agree** within measurement resolution (the modes share
  the spacing/voice machinery; only the estimate differs).
* **CPU** (ns/input-sample, 6 s runs, includes detector): ~550 ns at
  110 Hz/G=4096 rising to ~1100 ns at 32.7 Hz (the window-growth cost of
  the lower floor: win_dec 2048 → 3750) — the price of the lowered floor,
  consistent with the R21 fmin battery.
* **Special leg, 192 kHz, f0_min = 20, tone 25 Hz** (safety floor 31.25):
  9000 detected, 8912 spacing-rejected, 88 accepted (the rare ≥ 31.25 Hz
  octave-error estimates — legitimately shiftable at that rate). With the
  accept range pinned empty (`fmax = 31.25`): 0 accepted, spacing never
  leaves the grain default — the pure detectable-but-not-shiftable state.
* **Special leg, 48 kHz, f0_min = 20, tone 22 Hz**: 4500 detected =
  4500 accepted (at 48 kHz every detected estimate is shiftable; the
  safety floor only bites above ~131 kHz with G = 4096).
* **Long render**: 30 s, 8 voices, spread ON, feedback 0.25, −12 st at
  41.203 Hz: finite, bounded (rms 0.0088, max step 0.0010).

## 4. Policy summary (normative)

| Stage | Lower bound | Source |
|---|---|---|
| Detector validity | `Config::f0_min` (default 60) | R21 |
| Tracker lag range | derived from `f0_min` (win ≥ 2.5·tau_max) | R13-b/R19 |
| Spacing accept | `max(min(fmin, f0_min), sr/(1.5·grain))` | **R22** |
| Read delay / ring | config-validated (frequency-independent) | R12+ |
| Voice ratio | frequency-blind | R12+ |

**The default is unchanged** (`f0_min = 60`): the R21 static finding (the
original's "Minimum Pitch" control: C0–B3, default C2 ≈ 65.4 Hz) and this
round's measurements are both on record; a default change requires its own
justification. The engine now *honours* a lowered floor end-to-end, which
is what R21's `Config::f0_min` always promised.

**Gates:** suite cases `minpitch_floor_default`,
`minpitch_end_to_end_low_lock`, `minpitch_detectable_not_shiftable`,
`minpitch_extreme_floor_end_to_end`, `minpitch_multichannel_broadcast`,
`minpitch_tinygrain_empty_gate` (group `r22`; 102/102 total this round).
