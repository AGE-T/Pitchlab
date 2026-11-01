# OCTAVOX ROUND 20 — FINDINGS

**Complete Feature Reconstruction and Adaptive YIN Pitch Tracking**
Date: 2026-11-01 · Agent: RE track · Continuation of `OCTAVOX_ROUND19_FINDINGS.md`
Core: octavox-native **16.1.0-R20** (additive: the Hybrid detector mode + the
VST3 default-init fixes; every existing behaviour and reference hash unchanged)

---

## 0. Round summary (one screen)

| Priority | Item | Verdict |
|---|---|---|
| **P0** | VST3 default-parameter initialization | **ROOT-CAUSED & FIXED.** The SDK `Parameter()` default ctor initialises `valueNormalized {0.}` — NOT the declared default. All 38 parameters reported 0-based values (Pitch −24.00, Wet/Dry 0.00, Spread off, gains 0.0, offsets −24.00, delays 0 ms) while the engine ran the true defaults — the user's "Generic Editor wrong until touched". Fix (3 parts) + 5 new test cases (S1–S6), 20/20 green; sensitivity proof: fix disabled → exactly the 3 default cases fail. |
| **P1** | Original-UI → DSP feature reconstruction matrix | **DELIVERED** (`docs/R20_FEATURE_RECONSTRUCTION_MATRIX.md`): 5 panels × 40 elements, each with evidence class (OBS/DER/UNK) + exact addresses + our implementation status. **Crossfade = the grain-commit blend width** (R12 FINDING 4 chain; our `commit_crossfade_seconds` is the same mechanism, config-level). **Randomize = the random walk on the per-voice advance rate** (consumer: `record_ring[base+3]` → `rate_v = 4·P(f)·ratio·correction`; `P(0)=0.25` is randomize-off, which our core reproduces bit-exactly). Roadmap: R21-a harmony engine (evidence complete), R21-b Randomize, R21-c host-sync, R21-d host-expose crossfade. |
| **P2** | Three-detector comparison on real material | **DELIVERED.** New `DetectorMode::Hybrid` (coarse-to-fine: decimated coarse stage → candidate mapped to host grid → full-rate difference-function refinement in ±(D+2) → parabolic refine, ±1-lag-clamped). 13-corpus real-material battery × 3 modes × 2 rates (`r20_detcompare.json` + trajectory CSVs). Suite 93→96 cases. Accuracy on pure tones: Hybrid worst 0.02 cent (Decimated 0.19). On real material: modes within ~1 cent of each other; Hybrid jitter matches FullRate (0.316 vs 0.393 cent on fast vibrato); FullRate itself shows 96 kHz instability (172-cent transient max) the decimated family does not. CPU: Hybrid ≈ +8–14% over Decimated, ~3.7× under FullRate @48k, **12.4× under FullRate @96k** (404 vs 5023 ns/sample). |
| **P3** | Dual-mono / stereo integrity (Haas question) | **ANSWERED** (`r20_dualmono.json` + the report below). (a) Deterministic dual-mono is BIT-IDENTICAL L/R in every config (1/2/8 voices × 2 interps × 2 rates) — no hidden stereo widening. (b) The user's "second zero-offset voice doubles the effect" = the default inter-voice base-delay law: **echo lag measured 32.000 samples = 0.667 ms @48k** (0.333 ms @96k) — inside the Haas fusion window; it is a TEMPORAL inter-voice doubling, not an L/R mechanism. (c) True L/R Haas arises only from non-identical channels: 1-sample input offset already yields a 326-sample output lag divergence (per-channel tracker grain-timing), true-stereo decorrelates −6.7 dB. |
| **P4** | VST3 parameter semantics documentation | **DELIVERED** (`docs/R20_PARAMETER_SEMANTICS.md`): the ratio composition law (`ratio_v = GLOBAL·VOICE_OFFSET_v`), the Spread interaction table (offset displaces the spread slot; never replaces global), the naming-conflict resolution (our "Spread" = musical table; the original's = spatial — documented, no silent rename), voice count/mute/gain laws, ms-domain delay rate-dependence. |
| **P5** | Regression gates | **HELD.** 96/96 core cases (93 + 3 new Hybrid) release AND ASan/UBSan zero reports; **all 8 reference hashes exact** (4 legacy 15.0.0 pins + 4 16.0.0 defaults); wrapper 20/20; the R19 click-scan and null-test records unchanged (no behavioural change to existing modes — the Hybrid is purely additive). Zero-allocation laws unchanged. |

---

## 1. P0 — the VST3 default-parameter initialization defect

### 1.1 Root cause (SDK-level, instruction-precise)

`Steinberg::Vst::Parameter`'s default constructor
(`public.sdk/source/vst/vstparameters.cpp:48`, SDK 3.7.12_build_20) leaves the
current value at `valueNormalized {0.}` (`vstparameters.h:107` — the member's
default initialiser). Our `OctavoxParameter` called that default ctor, filled
`ParameterInfo` (including a CORRECT `info.defaultNormalizedValue`), but never
called `setNormalized()` — so the parameter's CURRENT value stayed at 0 while
the metadata was right. Every host reading path shows the CURRENT value:

* `IEditController::getParamNormalized` → `parameter->getNormalized()` (the knob),
* the Generic Editor's plain value → `normalizedParamToPlain(getParamNormalized())`,
* host automation snapshots / state flushes.

Result on a fresh instance (measured by the new test 16 with the fix
disabled): Pitch −24.00, Wet/Dry 0.00, Spread off, every voice gain 0.0,
every voice offset −24.00, every voice delay 0 ms — while the engine ran
`defaultSurface()` (all-wet, unity gains, default delays). The user's report
("values inconsistent until the control is moved") is exactly this: a
`performEdit` writes the CURRENT value through the queue, which then also
drags the engine to the wrong value.

### 1.2 The fix (three coordinated parts)

1. **`OctavoxParameter` ctor** (`octavox_params.cpp`): after `info` is
   complete, `setNormalized(info.defaultNormalizedValue)` — the object is not
   yet in a container, has no listeners, so the `changed()` notification is a
   no-op. Pure value initialisation.
2. **`OctavoxNativeProcessor::initialize()`** (`octavox_native_processor.cpp`):
   fresh-instance semantics on re-initialisation — `surface_ = defaultSurface();
   cfg_ = defaultConfig();` (+ processSetup reset). A host's
   terminate→initialize cycle (project reset / re-insert) previously carried
   the old surface as member state: the re-registered parameters would have
   shown defaults while the engine still ran the previous surface (the same
   divergence class as 1.1).
3. **`IEditController::setComponentState`** override: the SDK base returns
   `kNotImplemented` (`vsteditcontroller.cpp:74`); for a single-component
   effect the controller part IS the component, so the correct semantics is
   the alias to the authoritative `setState` (the same 'OVR1' blob, the same
   surface/cfg overwrite, the same silent parameter-object sync). Idempotent
   if a host calls both `IComponent::setState` and the controller path.

### 1.3 The six-scenario test group (wrapper cases 16–20, no control ever moved)

| Case | Scenarios | Key gates (all == 0) |
|---|---|---|
| 16 `param_defaults_fresh_instance` | S1 + S6 display reads | current==default (all 38), info==spec, displayed plain==default, display strings, live surface == defaultSurface |
| 17 `param_defaults_after_prepare` | S2 + S6 engine truth | values still defaults after setup+activate; **engine bit-exactly runs the default surface** (no events ever delivered — reference: hand-driven core with `applyFullSurface`'s exact application law); host automation-flush of the 38 CURRENT values keeps the surface at defaults and the output bit-exact vs the chain-built reference |
| 18 `param_defaults_state_roundtrip` | S3 | default state save→restore via `setState`, `setEditorState` AND the new `setComponentState` — all 38 stay at defaults |
| 19 `param_state_restore_new_instance` | S4 | NON-default blob → fresh instance via `setState` and `setComponentState`: all 38 current values == saved exactly (worst norm diff 0); resave roundtrip |
| 20 `param_defaults_reinit_cycles` | S5 | 6× initialize/terminate with a non-default activated life in between: every cycle returns to the documented defaults; parameter container stays exactly 38 |

**Sensitivity proof:** with the ctor fix temporarily neutralised, exactly the
three default-checking cases fail (16: current==default / display plain /
strings; 17: prepare values + flush surface; 20: every cycle) — the tests
provably catch the defect class; with the fix, 20/20.

### 1.4 Contract note

The plain↔normalized conversion chain proved EXACTLY idempotent for all 38
defaults at 48 kHz (`flush_engine_vs_raw_reference_maxdiff = 0` — including
the delay-ms grid), so the S6 flush gate is bit-exact, not epsilon-bound.

---

## 2. P2 — the Hybrid detector and the real-material comparison

### 2.1 The Hybrid mode (dsp.hpp `DetectorMode::Hybrid = 2`)

* **Stage 1 (coarse):** the decimated estimator VERBATIM (same anti-alias
  cascade, window, warmed cumulative mean, threshold law, fallback argmin,
  d-based parabolic refine) → candidate `tau_dec` at the ~24 kHz control rate.
* **Mapping:** `tau_c = tau_dec · D` (host stream samples), clamped into the
  raw search domain (edge-clamp law identical to the Decimated's own
  edge behaviour — measured consequence below).
* **Stage 2 (fine):** the ORIGINAL, UNFILTERED signal's difference function
  on the full-rate window (`win_raw = max(2048, ceil(2.5·tau_max))` at the
  HOST rate — the FullRate detector's own window law) over
  `[tau_c − (D+2), tau_c + (D+2)]`, argmin + 3-point parabolic refine with the
  vertex correction CLAMPED to ±1 lag.
* Identical cadence (every 64 stream samples), median history, hysteresis/
  commit law, [60,1200] Hz gate; time-equivalent ~42.7 ms analysis span.
* The FullRate and Decimated code paths are byte-for-byte the R19 code (the
  Hybrid dispatch is a single early return) — **all 8 reference hashes exact**.

### 2.2 Two defects found by the battery (and fixed in-round)

1. **Neighbour read overflow** (ASan, 48 kHz): the refine reads `best+1`
   with the full `m_raw` support; at a domain-edge argmin the last index is
   exactly `win_raw_`. Fix: search upper bound `tau_max_raw_ − 1`.
2. **Estimate drop-outs on out-of-range signals** (the 55 Hz corpus, below
   the 60 Hz gate): an unclamped parabola vertex flew past the sample grid,
   pushed `tau_host > 800`, and the [60,1200] gate rejected → 51–61% drops
   while Decimated held its edge-clamped ~60 Hz behaviour. Fix: clamp the
   vertex correction to ±1 lag + pre-clamp the candidate into the search
   domain. After: the 55 Hz corpus shows drop 0.00% on all three modes with
   median errors 150–155 cent (the honest "saturated at the floor" value).

### 2.3 The real-material corpus (13 signals × 3 modes × 2 rates, 4 s each)

Voice-like stacks (220/110 Hz, vibrato ±35/±25 cent, tremolo), speech-like
(formant-weighted, ±8% drift, voiced/unvoiced gating), bass guitar (55 Hz,
pluck decay), electric guitar (sawtooth-ish, detune), sustained strings with
portamento 293→330 Hz, fast vibrato (6 Hz ±60 cent), portamento 150↔300 Hz,
F0 steps (220→440→330→…, lock-lag probe), transient-rich replucks,
breathy (−20 dB noise), an octave pair (147+294 Hz), low edge (62 Hz).
All phase-continuous (true portamenti), per-period random detune, noise
floors — **no pure-sine selection anywhere**.

Machine-readable results: `evidence/analysis_round20/r20_detcompare.json`
(78 rows), stdout table `logs/r20_detcompare_stdout.txt`, per-corpus
trajectory CSVs `traj/` (ref F0 / raw estimate / engine spacing at 10 ms).

### 2.4 Headline measurements (48 kHz unless noted)

* **Steady accuracy:** the three modes are within ~1 cent of each other on
  every pitched corpus (the corpora carry 2–6 cent performance jitter by
  construction — the detectors track it; the comparison is like-for-like).
* **Jitter (vibrato_fast_330):** FullRate 0.315 / Decimated 0.393 /
  **Hybrid 0.316 cent** — the Hybrid matches the FullRate estimate stability
  at ~1/3.7 of the cost; at 96 kHz: 0.042/0.048/**0.042**.
* **Lock lag (F0 steps):** ~19 ms on ALL three modes (the median-history +
  hysteresis law — engine-side, mode-independent, as designed).
* **FullRate's own 96 kHz instability:** on voice_female_220 @96k the
  FullRate max error is 172 cent (one transient excursion) while
  Decimated/Hybrid stay at ~3 cent — **the full-rate window is NOT
  unconditionally the most stable tracker; the decimated family is
  measurably steadier on this material.**
* **CPU (full detector+voice chain, ns per input sample):**
  48 k: FullRate 1352 / Decimated 355 / Hybrid 374–384;
  96 k: FullRate 5023 / Decimated 355 / **Hybrid 404** → the Hybrid costs
  +14% over Decimated and is **12.4× cheaper than FullRate at 96 kHz**.
* **Octave errors/dropouts:** comparable across modes and driven by the
  material (speech gating 2–4%, pluck re-attacks 1.5%), not the
  architecture; the octave-pair corpus is tracked by all three at the
  deeper member (median error 0.05–0.07 cent).

### 2.5 The quality decision (per the specification's rules)

Decimated REMAINS the shipping default (the real-time target stands; no
host parameter added — the three modes are core-config options). The Hybrid
is the documented quality upgrade path: near-FullRate stability at
Decimated-class cost. The adaptive-quality investigation (stability-gated
mode switching) was NOT implemented — the measured mode deltas on real
material do not yet justify the controller complexity (no demonstrated
benefit: the modes are within ~1 cent of each other except where FullRate
is worse). The FullRate mode stays as the bit-exact reference.

---

## 3. P3 — dual-mono / stereo integrity (the Haas report)

Tool: `cli/r20_dualmono.cpp` (deterministic; 7 scenarios × 2 rates × 2
interpolators + the doubling probe). Records: `r20_dualmono.json`.

### 3.1 The three mechanisms, separated

1. **Deterministic dual-mono transparency (proven):** L≡R byte-identical
   harmonic input (220 Hz stack + jitter + noise, one shared generator
   stream), 1/2/8 voices, equal per-voice delays, feedback 0, spread off,
   wet 100% → **L/R outputs bit-identical in every configuration** (max|L−R|
   = 0, correlation lag 0, side/mid = −∞). The per-channel engine
   architecture is deterministic: identical inputs produce identical
   trackers, commits (1 == 1 at the same instant) and reads. **There is NO
   hidden stereo widening and none was added.**
2. **The user-reported "doubling" (measured directly):** at Global Shift = 0,
   both voice offsets = 0 (the reported configuration), the second voice's
   output is delayed relative to the first by exactly the default
   base-delay law: **echo lag = 32.000 samples = 0.667 ms @48 kHz
   (0.333 ms @96 kHz)** — measured by rendering voice-0-only vs
   voice-1-only (identical engine, per-voice gains swapped) and
   cross-correlating. 0.667 ms sits squarely in the Haas fusion window:
   adding a second zero-offset voice adds a +0.67 ms copy — a TEMPORAL
   doubling of the material, entirely independent of stereo. This is the
   core's documented default (`base_delay = 256 + 32·v` samples); it is
   INTENTIONAL engine behaviour, not a defect, and it is now documented as
   such.
3. **True inter-channel Haas (quantified):** with NON-identical channels the
   per-channel trackers diverge: a 0.1%/1% amplitude imbalance passes
   through proportionally (−60/−40 dB — no tracker divergence), but a
   ONE-SAMPLE input offset already decorrelates the wet paths to −21 dB and
   shifts the output correlation peak by **326 samples** (grain-timing
   divergence of the independent trackers); genuinely decorrelated stereo
   (independent jitter streams) gives −6.7 dB and ~20 ms peak drift. This is
   the structural consequence of per-channel tracking (the same
   architecture the original's channel-isolated engines imply) — on real
   stereo material it is the intended, natural stereo behaviour.

### 3.2 Deliberate non-changes

No artificial stereo widening was introduced to "mimic the original"; the
dual-mono result stays bit-exact; the inter-voice delay law stays the
documented default (a future GUI/UX round may expose it more visibly —
it is already the `Voice v Base Delay` parameter).

---

## 4. P1/P4 — see the two new documents

* `docs/R20_FEATURE_RECONSTRUCTION_MATRIX.md` — the 40-element matrix
  (voice engine / pitch control / harmony / delay-sync / mix-state panels),
  every row: UI element, kind, evidence address, OBS/DER/UNK, our status;
  the two named investigations (Crossfade, Randomize) closed with the
  consumer chains; the R21/R22 roadmap.
* `docs/R20_PARAMETER_SEMANTICS.md` — the shipped 38-parameter interaction
  laws (ratio composition, spread-slot displacement, mute/count/gain
  composition, ms-domain delay law, the naming-conflict documentation).

---

## 5. Validation chain (this round, executed)

* Core suite **96/96** (93 + 3 Hybrid cases: accuracy battery ≤ 1 cent —
  worst 0.0196; vs-FullRate agreement ≤ 0.5 cent steady — worst 0.111 @48k,
  0.026 @96k; 44.1 kHz config) — Release AND ASan/UBSan zero reports.
* **All 8 reference hashes exact:** legacy pins (FullRate + zero crossfades)
  cfg1–cfg4 == `r15_reference_hashes.txt`; 16.0.0 defaults cfg1–cfg4 ==
  `r19_reference_hashes.txt` (the Hybrid is purely additive — FullRate and
  Decimated code paths byte-identical to R19).
* VST3 wrapper suite **20/20** (15 + the 5 P0 cases) incl. the sensitivity
  proof (fix off → 16/17/20 fail with exactly the reported symptom).
* New tools: `octavox_r20_detcompare` (78-row JSON + trajectories),
  `octavox_r20_dualmono` (28-row JSON + the doubling probe) — both clean
  under ASan/UBSan.
* Determinism: the dual-mono bit-identity rows re-verify the engine's
  determinism contract end-to-end.

## 6. Environment

Fifth sandbox reset at round start (cmake lost → reinstalled via pip
cmake-4.4.4; the persistent octavox-work tree survived; llvm-mingw to be
re-acquired for the Windows leg — recorded in the worklog).

## 7. What R20 did NOT do (exclusions, unchanged)

No original-binary execution (runtime observations stay UNK); no claim of
equivalence between our engine and the original; no GUI implementation; no
host-parameter surface change (38 parameters frozen); Decimated stays the
shipping default; no adaptive-quality controller (no measured benefit); no
licensing/anti-copy/security work.
