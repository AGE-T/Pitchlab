# Pitch Lab — VST3 Product Phase Specification (v1.0)

**Status:** FROZEN for implementation before coding (2026-10-01, the §4.4.3.1
freeze-before-coding pattern applied to the product phase).
**Scope authority:** owner directive "REAL VST3 PLUGIN + REQUIRED USER
INTERFACE" (IM handoff, 2026-10-01). v0.1 remains COMPLETE AND CLOSED
(§17.6 PASS; results/v0.1 retained; verification permanently stopped). This
phase adds a PRODUCT LAYER above the untouched research/DSP layer. No v0.1
decision is reopened unless a concrete VST runtime requirement makes an
existing assumption impossible — none found (see §3).

**The question this document answers first (owner directive):** *what is the
smallest correct realtime adapter boundary required to run the existing
engines from VST3 audio blocks, and which engines can / cannot safely run
in the realtime VST process path?*

---

## 1. Product goal

A real, usable VST3 effect plug-in — **Pitch Lab** — that processes audio
through the ACTUAL v0.1 research engines (the five registered
`native.*` engines), with:

* a real VSTGUI editor (engine selector, pitch control, per-engine
  parameters, dry/wet, bypass, output level, input/output meters, honest
  runtime status);
* host-correct behaviour (automation, sample-rate/block-size changes,
  bypass, reset, suspend/resume, state save/restore, latency reporting,
  mono/stereo, any block size);
* a clean product-layer separation: the research pipeline
  (core/engines/harness/analysis/CLI) remains buildable and usable exactly
  as v0.1 closed it.

Primary development target: **Windows x64 VST3**. The sandbox-verified
reference build is **Linux x64 VST3** (the only toolchain available here —
no Windows cross-compiler exists in this environment); the adapter
architecture is platform-portable C++20 over the Steinberg VST3 SDK, and
the Windows build instructions are part of the deliverable (§11).

## 2. Architecture

```
DAW
 → VST3 Processor  (src/vst/processor.cpp — single-component effect)
 → Realtime Adapter (src/vst/realtime_adapter.cpp — the boundary of §3/§4)
 → v0.1 PitchEngine instances (src/engines/* — UNMODIFIED v0.1 code)

DAW ⇄ EditController (= same single component) ⇄ VSTGUI editor
 → parameter model (src/vst/parameters.h — the ONE authoritative definition)
```

Rules:

* The VST3 layer must not move DSP research logic into the UI layer; the
  UI binds to the parameter model and reads status/meters through two
  read-only interfaces (§7). No DSP in the editor.
* **Engine identity remains the v0.1 registry** (`registerProductionEngines`,
  the single authoritative registration point). The plug-in's engine list =
  the registry iteration order, read at initialisation. NO second engine
  registry, no id mapping table, no per-plug-in engine enum that can drift
  from the registry (the parameter value ⇄ registry index mapping is
  generated from the registry itself; §6).
* The existing web workbench is NOT modified.
* The v0.1 offline pipeline targets remain bit-for-bit as they were; the
  plug-in product adds new CMake targets guarded by an option
  (`PITCHLAB_BUILD_VST3`, default ON, OFF does not change the v0.1
  surface at all).

## 3. Realtime boundary analysis (the core decision, frozen)

The v0.1 engine contract (§4.2/§4.3 of the implementation specification)
is a FINITE-JOB, KNOWN-CURVE contract:

1. `prepare()` fixes `(fs, channels, maxBlockFrames, N_in, curve)` for the
   whole job; the curve view is the SAME OBJECT with
   `frames == totalInputFrames`, dense, fully materialised before prepare.
2. Engines SCAN the whole curve at prepare (varispeed: rMin/rMax → FIR
   cutoff + latencies; vardelay: sMax → excursion budget; granular: rMax →
   spread + a schedule simulation → read horizon; pv.classic: rMax + the
   total read advance pEnd → accumulator capacities; pv.phaselocked:
   finiteness only). All runtime sizing is derived from these scans.
3. `process()` requires `inputFrameIndex == cumulative consumed`, consumes
   in order, and produces output gated by each engine's strict-delivery
   rule (production may run ahead of consumed input by an engine-bounded
   amount — e.g. vardelay up to E−K frames, PV by the synthesis lead).
4. `finish()` exactly once, after all real input; `reset()` re-zeroes
   streaming state. No allocation after prepare (§5 rule 1 — verified by
   the v0.1 allocation audit T-A1).

A realtime VST3 process path is an UNBOUNDED input stream arriving in
arbitrary host blocks, with future automation fundamentally unknown, and
1 output frame required per input frame. **The contract itself is not a
realtime contract** — running the engines in realtime therefore requires an
adapter, and the honest boundary is exactly the delta between the two
contracts. Per engine:

| Engine | Duration behaviour | Whole-curve scan deps | Realtime verdict |
|---|---|---|---|
| native.varispeed | **RateFollowing** (output length = N_in/r̄) | rMin, rMax | **Cannot sustain fixed I/O at ratio ≠ 1** (output/input frame counts diverge linearly; no finite latency fixes sustained reading at r>1 or piling at r<1). Runs only under the windowed-splice adaptation (§4.2). |
| native.vardelay | Preserving | sMax | Adaptable (§4.1): the variable-delay machine is the classic realtime shifter family; needs envelope + pacing + jobs. |
| native.granular | Preserving | rMax, horizon-sim | **Runs under the windowed-splice adaptation (§4.2) — the recorded §3 correction (see below).** |
| native.pv.classic | Preserving | rMax, pEnd | Adaptable (§4.1); the AA cutoff tracks the job envelope (§4.1 item 2). |
| native.pv.phaselocked | Preserving | (none — finiteness only) | Adaptable (§4.1); the least envelope-sensitive engine. |

**IMPLEMENTATION-TIME CORRECTION (recorded, never silent — the v0.1 cycle
pattern): the §3 verdict for native.granular.** The frozen table above
classified granular as adaptable under §4.1. Measured diagnostics with a
REAL ratio ≠ 1 (enabled by the pitch-carry fix) show the granular read grid
r_{k+1} = r_k + Hg·ratio_k advances at the ratio per OUTPUT frame: output n
is gated on input n·ρ + G having arrived, so production sits at (p−G)/ρ
while emission needs p−Λ — the gap (ρ−1)·p grows linearly. Sustained
fixed-I/O at ρ ≠ 1 is structurally impossible (the input-side dual of
varispeed's output-side divergence). native.granular therefore runs under
the SAME windowed-splice adaptation as native.varispeed (§4.2: real
granular jobs over 0.2 s wet windows, 15 ms splice crossfades, declared,
labelled in the UI status; the engine itself is untouched — no v0.1
decision was reopened, the realtime ADAPTATION of the engine changed).

**Explicitly documented engine-level boundaries (no engine was modified):**

* varispeed is the one engine whose *duration behaviour* is impossible in
  sustained fixed-block realtime. The plug-in exposes it through the
  windowed-splice adapter (§4.2) — a declared adaptation, labelled as such
  in the UI status, NOT the offline continuous render.
* The engines' whole-curve scans become whole-JOB scans on the adapter's
  virtual jobs: sizing is correct by construction for the job's declared
  envelope, and the adapter's curve clamp guarantees the runtime curve
  stays inside the scanned envelope (§4.1 item 2). Nothing silently
  exceeds a scanned bound.
* vardelay's fade-before detection reads up to W frames of FUTURE curve.
  In realtime the future curve is unknown; the adapter supplies its
  declared prediction (hold-clamped-envelope, §4.1 item 3) and the
  detection is exact with respect to the curve the engine actually
  consumes. Missed detections fall back to the engine's own instant-wrap
  rule (§6.2.1 item 4c) — an engine behaviour, not an adapter invention.
* Offline bit-exactness does not carry over to realtime by definition
  (unknown future automation). The adapter's determinism guarantee is:
  same parameter trajectory + same input + same block schedule ⇒ identical
  output (tested, §9); the offline pipeline keeps its own stronger
  guarantee untouched.

**THE ONLINE-MODE QUESTION (Task 29, explicitly documented):** the v0.1
core engine contract is FINITE-JOB based (prepare/finish over a known
curve, §4.2/§4.3 of the implementation specification). The VST adapter
provides a VIRTUALISED realtime path: it decomposes the unbounded stream
into overlapping finite virtual jobs, materialises bounded job curves,
crossfades the seams and rebuilds on configuration change. **The core
engines themselves do NOT support an unbounded online stream** — no
engine implements an online/streaming API, and no measurement of the
core contract can be cited as evidence that they do. Any claim of
"online processing" refers to the ADAPTER's virtualisation (its declared
determinism/latency/fault properties, tested); the engines remain
finite-job processors whose whole-curve scans, strict delivery and
finish-exactly-once semantics are the offline contract the adapter
bridges. This distinction is a design boundary, not a defect.

## 4. The realtime adapter (frozen design)

### 4.1 Preserving engines (vardelay, granular, pv.classic, pv.phaselocked)

The unbounded realtime stream is presented to the engines as a sequence of
finite VIRTUAL JOBS. Each job is a real, contract-honouring engine
lifecycle: `configure → prepare → process* → finish → destroy` (aborted
jobs destroy without finish — a declared realtime path; complete jobs
honour the full contract).

1. **Job geometry.** Job k covers the absolute input interval
   `[A_k, A_k + N_seg)` with `N_seg = 10 s · fs` **(recorded correction, task-33 SoT pass
   2026-10-01: this text previously said 30 s — a documentation drift; the implemented
   constant is `kSegmentSeconds = 10` (`realtime_adapter.cpp:24`) and always was, matching
   `docs/vst3-product.md`; the discovery-report §1.10 drift item is closed here. The
   geometry itself is unchanged.)** (long: seams are rare;
   automation is NOT quantised to jobs — see item 3). Consecutive jobs
   OVERLAP by `X` frames (per-engine: vardelay W, granular G, PV N) — the
   same absolute input is fed to both instances during the overlap
   (rebased), and the output is an equal-power crossfade over exactly X
   frames at the seam. Crossfaded splices are the declared realtime
   adaptation (absent in offline renders; at identity the two branches
   render identical content for the overlap-honouring engines, so the
   seam is numerically transparent there).
2. **Envelope + clamp (the scan-safety rule; THE TASK-30 ENVELOPE RULE).**
   At prepare of job k the adapter establishes the pitch envelope
   `env = ±2^(max(1 st, live LFO depth)/12)` around the live ratio,
   intersected with the LEGAL EFFECTIVE-curve domain
   `[paramMin·2^(−depth/12), paramMax·2^(+depth/12)]` (the parameter
   bounds relaxed by the LFO excursion — the effective curve is
   PITCH+LFO, whose legal domain is ±14 st at the depth maximum) and
   clamped to the ENGINE's declared ratio range `[minRatio, maxRatio]`
   (the hard validity limit — an engine never receives a ratio outside
   its declared capability). At LFO depth ≤ 1 st every term degenerates
   EXACTLY to the pre-Task-30 ±1 st formula (bit-identical envelope for
   every pre-existing audio-path case). The adapter pre-fills the job's
   dense curve array with the envelope extremes (varispeed-style engines:
   alternating env-min/env-max; sum- and horizon-type scans: env-max
   throughout) so EVERY prepare-time scan bounds the worst case the job
   will ever see. The runtime curve value is `clamp(automation(t) ·
   lfo(t), env)` — a clamp is a COUNTED BOUNDARY EVENT, never a
   re-prepare trigger (item 4). Consequences, all documented and bounded:
   * sizing (windows, budgets, accumulators, horizons) is correct for any
     automation within the envelope — no exception paths, ever;
   * the pv.classic AA cutoff is the envelope max (≈ +1 st above live):
     at identity the cutoff is `0.95/2^(1/12) ≈ 0.897·Nyquist` (≈19.8 kHz
     at 44.1 kHz — full band to the ear); at the extremes it is ≈5% more
     conservative than the offline engine's exact-curve cutoff;
   * a STATIC PITCH+LFO setting never clamps (the pre-Task-30 ±1 st
     constant violated this for every depth > 1 st — the measured futile
     churn: 759 clamps / 329 re-prepares per 2 s at −12 st + LFO, a seam
     every ~6 ms; Task 30 removed the root, not the counters);
   * the REMAINING clamp is the honest engine-limit boundary: an engine
     whose declared ratio range is narrower than the legal surface
     (native.vardelay [0.5, 2.0] vs the ±14 st surface) clips the
     effective curve at its declared floor/ceiling — accepted-but-clipped
     (the Task-30 LFO boundary decision), counted, never a rebuild (a
     re-prepare cannot move a declared engine capability);
   * automation that exits the envelope is bridged by the clamp for the
     few milliseconds until the re-centred replacement chain is live
     (item 4) — the clamp only ever bridges, it never decides.

   **THE TASK-35 EXIT-HANDOFF AMENDMENT (RC-1 re-derivation; the
   deterministic envelope-exit handoff).** Recorded before implementation
   per the recovery program. The Task-34 record (`bbc25a6`) proved that the
   adapter's bit-determinism guarantee, as it stood, did not cover the
   PREPARATION-THREAD POLL TIMING: the re-centre geometry was derived from
   the preparation thread's poll-moment view of the live ratio
   (`exitLiveRatio` — a single overwritten atom), so the new chain's
   envelope centre (and with it the job input lead Λ and the curve-clamp
   window) depended on wall-clock load, and identical drives could render
   different bytes after a re-centre. The amendment:

   * **The audio thread OWNS the persistent envelope-exit detection.** At
     each block end the audio thread compares the block-end pitch-parameter
     ratio (capability-clamped, the exact comparison the preparation thread
     performed) against the ACTIVE chain's prepared envelope. The
     pitch-parameter timeline is the detection signal — the LFO excursion
     is inside the envelope by construction, so the transient LFO clamp
     class (§4.1 item 2, counted, never a trigger) is structurally
     excluded from the exit path.
   * **The exit handoff is EPOCH-VERSIONED.** Each detected exit publishes
     one `(epoch, ratio)` pair — the exit-moment ratio — to a bounded
     lock-free single-producer/single-consumer event ring (64 events,
     audio-thread producer, preparation-thread consumer, allocation-free,
     no locks). The ring preserves ORDER and CONTENT: the preparation
     thread consumes the events IN ORDER (oldest unconsumed first), one
     re-centre build per consumed event, centred at that event's
     exit-moment ratio. Overrun drops are counted telemetry, never silent.
   * **THE DETERMINISM AMENDMENT (the point of this note).** The re-centre
     ratio is always an AUDIO-THREAD-MEASURED, VERSIONED exit-moment ratio:
     the preparation-side poll-moment read of the live ratio (the
     unbounded load-lag path that the Task-34 CI evidence exposed) is
     REMOVED. The re-centre chain sequence is the in-order consumption of
     the versioned event stream. The remaining run-to-run variance in HOW
     MANY events are consumed per build cycle (the audio thread publishes
     while no swap is pending; the publication windows therefore depend on
     the adoption moments) is BOUNDED by the pending-chain gate and belongs
     to the already-documented preparation-timing class — the exact BLOCK
     of a mid-stream chain swap depends on preparation timing — which the
     bit-determinism contract has always excluded ("deterministic per
     (parameters trajectory, input, block schedule) while the chain set is
     constant"). What the amendment buys: no unbounded load-lag in the
     geometry, an order-preserving exactly-once handoff, and the
     automation-only delivery continuity (RC-1). Two second-order
     consequences are recorded: (a) the parameter/reset-class rebuilds
     centre their envelope at the SNAPSHOT's ratio (the previous
     poll-moment `exitLiveRatio` override — the other load-lag path — is
     removed); the audio-thread exit events carry any in-flight automation
     drift as explicit, versioned re-centres; (b) the re-centre cadence is
     one chain build per consumed exit event (bounded by the 1 ms
     preparation cadence; the lifecycle pins that bound re-prepare counts
     keep their meaning — the counts scale with the automation's
     envelope-exit rate, exactly as the sweep geometry dictates).
   * **Realtime safety is unchanged:** the detection is a bounded
     comparison + ring append on the audio path (no allocation, no lock,
     no blocking); the depth-growth capability exit (§4.1 item 2 / Task 30)
     stays on the preparation side — its `max(snapshot, live)` coverage
     term is monotone-safe under load lag by construction.
3. **Live curve.** The job's dense curve array is owned by the adapter and
   is written per input frame as frames arrive (automation sampled at
   frame granularity with per-block ramps taken from the VST3
   `IParameterChanges` queue — true sample-accurate host automation).
   Not-yet-arrived slots hold the envelope-clamped hold-last prediction
   (consumed only by lookahead-style consumers such as vardelay's
   fade-before simulation, which re-simulates per frame and is
   self-correcting).
4. **Pacing and re-prepare.** The audio thread never allocates: jobs are
   constructed/destroyed ONLY on the dedicated preparation thread (a
   plain `std::jthread` + a lock-free hand-off: the new chain is published
   by an atomic pointer swap at a block boundary; retired instances are
   freed by the preparation thread after a two-block grace epoch).
   Re-prepare triggers (the Task-30 policy — a re-prepare fires ONLY on a
   genuine dependency change; a runtime clamp is NEVER among them):
   pitch-automation envelope exit (the re-centre), LFO-depth growth beyond
   the chain's envelope margin (the capability re-size — ONE rebuild per
   growth, never per block; a depth shrink keeps the wider envelope),
   engine selection change, engine parameter change, sample-rate change,
   block-size growth beyond the prepared max, reset/flush request,
   job-length cap (N_seg). The audio
   thread caps `outCapacity` so an engine's production never runs ahead
   of the curve the adapter has already materialised (the ratio stepping
   always consumes real automation samples).
5. **Latency.** Declared plugin latency (samples) = engine worst-case
   lookahead + seam crossfade + pacing margin, recomputed per chain and
   reported via `setLatencySamples` (hosts are notified; changes are a
   normal VST3 event). Bounds per engine (at 48 kHz, defaults):
   vardelay ≈ W + K + margin ≈ 44 ms; granular ≈ G + margin ≈ 105 ms;
   pv.classic/phaselocked ≈ N + H + margin ≈ 54 ms.

   IMPLEMENTATION-TIME CORRECTION (recorded, never silent): the emission
   runs at the EFFECTIVE latency Λ_eff = the maximum of every adopted
   chain's Λ within one activation — NOT at the live chain's Λ. A latency
   DECREASE (an engine switch into a cheaper engine) must never move the
   emission timeline forward past the retiring chain's production frontier
   (the per-chain model starves the retiring wet mid-blend — the measured
   fault bursts on latency-decreasing switches). Λ_eff therefore never
   decreases within an activation (it resets on reactivation); the job
   lanes and the dry lane retain (and are sized for) the parameter-range
   worst-case history so a mid-stream Λ_eff growth never outruns the
   retained wet/dry. Honest consequence: after using a high-latency engine,
   a lower-latency engine keeps running at the activation's max latency
   until the host reactivates the plug-in; the status reports the true
   Λ_eff. Splice-mode engines declare fixed worst-case latencies over the
   FULL parameter range (stability over optimality — see §12).

   TASK-31 SEAM-COVERAGE RULE (the retention made real, measured then
   fixed — `results/vst3/task31/seam-coverage-measurements.md`): a Λ_eff
   GROWTH moves the emission read position BACKWARD by ΔΛ at adoption —
   "re-covering already-emitted frames". The re-covered range is served by
   the retiring chain (the new chain starts at its own base). Two
   mechanisms make that coverage complete:
   * **the RETAINED WET HISTORY** (a per-chain lane): at each job death the
     produced wet is copied before the preparation thread can recycle the
     job (raw region by memcpy, the final seamX frames through the same
     blend readWet uses — both lanes alive at that moment), and the
     retiring emission read falls back to it when readWet misses (served
     frames counted in the `seamRecoveries` cadence diagnostic, never
     silent, NOT a fault). The retention horizon is the same worst-case
     constant the lanes are sized for — this is the retention this very
     item declared; the pre-Task-31 implementation sized the lanes but the
     job recycling destroyed the data (the measured 13558/14406-frame
     dry-fallback bursts on the extreme granular grain jump 0.1 s → 0.5 s,
     ΔΛ 19200).
   * **the retiring grid continuation**: the retiring chain's job
     scheduling continues while the chain lives (bounded by the last wet
     position the emission reads from it) — the retiring chain remains the
     sole wet source below the new chain's base, so its grid must not
     freeze at adoption (the measured 15680-frame ungenerated span on the
     REVERSE jump, a latency decrease).
   The re-coverage replay is bit-identical to the previously-emitted wet
   (tested at every block size 128..2048 and rate 44.1/48/96 — the repeat
   is the Λ_eff policy's declared cost, rendered as wet, never dry).
6. **Output ring.** Engine output lands in a pre-allocated output ring;
   the host block is emitted from the ring at the declared latency; the
   dry path is taken from the input ring (delay-matched by the same
   latency so dry/wet is phase-aligned at any mix setting). Bypass =
   latency-compensated dry (a 10 ms equal-power fade in/out on the bypass
   branch avoids clicks; the engine chain keeps running — host-typical).

### 4.2 Varispeed + granular — the windowed-splice adapter (the declared deep boundary)

Sustained fixed-I/O varispeed is impossible (§3), and granular carries the
input-side dual of the same divergence (the recorded §3 correction above).
The adapter presents the true engines in crossfaded windows: window k is a
REAL varispeed/granular job over input span `[a_k, a_k + L + X·r_k)` with a
piecewise-live curve (written as frames arrive, §4.1 item 3;
constant-ratio windows are the special case), `L = 0.2 s` input window +
`X = 15 ms` splice crossfade, window advance `a_{k+1} = a_k + L/r̄_k` (the
duration-preserving re-read for r>1 / skip for r<1 — the classic splice
character, documented and labelled in the UI status). Envelope: the
window's curve is clamped to the window's own prepare envelope (the same
±1 st rule; each window re-establishes, so automation tracking ≈ window
cadence). Declared latency (fixed worst-case over the FULL ±12 st
parameter range, stability over optimality):
`Λ = max((r_env − 1)·(L + X·r_env)) + margins` (varispeed ≈ 226 ms,
granular adds its grain ≈ 325 ms at the 0.1 s default grain — documented,
host-visible, and honest: these are worst-case bounds, not typical
operating latencies).

### 4.3 Realtime safety (the process path)

The VST `process()` path contains: parameter snapshot read (atomics),
curve-slot writes, engine `process()` calls (no allocation by the v0.1
contract), ring copies, crossfade arithmetic, meter accumulation. NO:
UI calls, file I/O, blocking, dynamic allocation, locks (the hand-offs
are atomic; the preparation thread never blocks the audio thread — the
audio thread runs the OLD chain until a new one is fully published),
network, hidden services. Exceptions from engines are caught at the chain
boundary → the chain hard-resets to a fresh job on the preparation thread
and the block falls back to latency-compensated dry (never a crash, never
a stuck state; counted in the status snapshot as RT_FAULTS).

## 5. VST3 SDK dependency (provenance)

* Official Steinberg **VST3 SDK**, pinned at tag `v3.7.14_build_55`
  (commit recorded in `external/vst3sdk/ORIGIN.toml`), including its
  VSTGUI 4 submodule (BSD-3-Clause), vendored under `external/vst3sdk/`
  following the v0.1 vendoring pattern (verbatim licence texts +
  ORIGIN.toml with per-tree provenance and sha256 of the pin).
* Licence note (documented exception to the "permissive-only" external/
  rule, recorded in ORIGIN.toml + external/README.md): the VST3 SDK is
  dual-licensed (GPLv3 OR the Steinberg VST3 licence agreement); Pitch Lab
  uses it under the **Steinberg VST3 proprietary licence** (the intended
  licence for closed-source VST3 plug-ins — not the GPLv3 path), so no GPL
  code enters the DSP runtime or the product. VSTGUI is BSD-3-Clause.
  The DSP core (v0.1) remains zero-dependency.
* The SDK tree is vendored verbatim (no patches inside external/; any
  required platform fix lives in the product layer's CMake).

## 6. Parameter model (ONE authoritative layer, OWNERSHIP-EXPLICIT since Task 29)

Two layers with ONE direction of derivation:

* **The ENGINE declares its own configuration parameters** —
  `EngineParamDescriptor` tables on the registry's `EngineDescriptor`
  (core/engine_registry.h; the same `configure()` key strings, a
  declaration layer, not a contract change). Key, display name, typed
  value kind (Real/Integer/Boolean/Text), plain domain, default, unit,
  discrete choices, automation capability, rebuild-chain requirement and
  the cross-parameter engine-domain constraints (the PV hop ≤ fft/2)
  are ALL engine-owned declarations. Engine-internal FIXED values
  (vardelay's read_kernel, the PV windows, locking_mode) are declared
  with `exposed = false` — the adapter still writes them, the product
  surface does not fake a control for them.
* **The VST model is GENERATED from the registry** (vst/parameters.cpp):
  the SHARED REALTIME CONTROLS (ENGINE selector, PITCH, LFO RATE, LFO
  DEPTH, MIX, BYPASS, OUTPUT LEVEL — the product's musical surface, NOT
  owned by any engine) are the static shared table; the ENGINE-OWNED
  CONFIGURATION rows are generated at runtime by walking the sealed
  registry in order and resolving each exposed descriptor through the
  stable `(engineId, key) -> VST ParamID` binding — the ONE VST-side
  table, frozen, never renumbered (state compatibility). The processor's
  parameter registration, the editor's control binding and the tests are
  all generated from this model; `validateEngineParameterModel()`
  (tested) enforces registry↔binding↔model coherence, so no second
  engine-parameter list can drift into existence.

PITCH/LFO are the SHARED REALTIME PITCH-CURVE SURFACE: the adapter
resolves the curve per sample (host automation + sine LFO); the selected
engine receives it through its registry-declared dynamic-ratio

capability (all five v0.1 engines declare support). The UI labels the
surface as shared — it is not an engine-specific parameter.

Exposed parameters (product surface — frozen identity, stable tags):

* ENGINE (discrete, registry order, default native.vardelay — the
  realtime-native engine).
* PITCH (−12.0..+12.0 st, default 0, display st with cents) — the
  automation surface; `ratio = 2^(st/12)` per frame.
* LFO RATE (0.0–8 Hz, default 5; **0 Hz = LFO OFF**, Task 32), LFO DEPTH
  (0–2 st, default 0 = off) — the runtime pitch-curve control appropriate
  to the realtime model (curve = automation + sine LFO,
  envelope-clamped). The OFF semantics: rate 0 gates the sine
  contribution regardless of depth (bit-identical to depth 0, tested) and
  parks the LFO phase at the zero crossing (0 → nonzero is continuous at
  any point — the modulation grows from sin(0); nonzero → 0 is the honest
  hard-off, continuous at a zero crossing); the rate never enters the
  chain signature, the envelope geometry or any re-prepare exit (no
  lifecycle event, no churn), the normalised mapping is exact at 0 (an
  OFF state save/loads bit-exactly; the editor label shows OFF while the
  host-facing value string stays "0.00"), and the normalised state of
  pre-Task-32 sessions shifts the rate by ≤ 0.1 Hz on restore (the domain
  extension is the one deliberate frozen-surface change, recorded in the
  engine-params suite).

**THE GLOBAL LFO ROLE (Task 30 audit, decided):** the LFO is RETAINED as
PRODUCT FUNCTIONALITY — a creative vibrato-style modulation control on
the shared pitch-curve surface — AND, de facto, the realtime stress
instrument of the test suites (the only cyclic curve source the product
has; used as the measurement drive in the audio-path and diagnostics
suites). It is NOT an engine-specific parameter and never was post-Task-29:
the UI positions it under "SHARED PITCH CURVE · LFO" on the pitch panel,
the engines receive the resolved curve through their declared
dynamic-ratio capability. Evidence for the retention: the specification
declares it on the product surface (automatable), every v0.1 engine
declares PerSample/FixedBlock dynamic-ratio support, and removing it
would break the shared-surface architecture to hide a lifecycle bug that
is now fixed at its root.

**THE LFO BOUNDARY SEMANTICS (Task 30, decided):** a PITCH+LFO
combination is ACCEPTED — the UI does not constrain LFO depth against
the pitch (the shared surface is engine-agnostic; the adapter alone owns
the engine-range interaction, so no UI component may invent per-engine
ranges). The legal effective-curve domain is ±14 st (PITCH ±12 + LFO
±2). Each engine's coverage of that domain, from its DECLARED
capabilities (the registry — the single authority):

| engine | declared range | ±14 st coverage |
|---|---|---|
| native.varispeed | [0.0625, 16.0] | full |
| native.vardelay | [0.5, 2.0] | clips beyond ±12 st (the LFO excursion past the parameter boundary) |
| native.pv.classic | [0.25, 4.0] | full |
| native.pv.phaselocked | [0.25, 4.0] | full |
| native.granular | [0.125, 8.0] | full |

Where the engine covers the domain, the chain envelope covers it too (no
clamping, no rebuild — a static setting renders the true curve). Where
it does not (native.vardelay at the extremes), the effective curve is
CLIPPED at the engine's declared limit — accepted-but-clipped, counted in
the clamp-events diagnostic, no rebuild (a rebuild cannot move a
declared capability). The engine NEVER receives an invalid ratio.
* Per-engine panels (UI shows only the selected engine's controls; the
  non-selected engines' parameters hold their values but are not applied
  — declared, not fake):
  * varispeed: Resample Quality (small/standard/reference), Allow
    Aliasing (bool). [adapter window L is fixed at 0.25 s, documented,
    no fake control]
  * vardelay: Excursion (0.05–2.0 s, default 0.5 — the v0.1 default),
    Crossfade (0–8192 frames, default 2048 — the v0.1 default).
  * granular: Grain Length (0.02–0.5 s, default 0.1 — v0.1 default),
    Overlap (4–16, default 4 — v0.1 default), Window
    (hann/triangular), Jitter (0–256 frames, default 0 — v0.1 default).
  * pv.classic: FFT Size (1024/2048/4096, default 2048 — v0.1 default),
    Hop (128/256/512/1024, default 512 — v0.1 default).
  * pv.phaselocked: FFT Size + Hop (same defaults). [locking_mode is
    fixed "identity" — the only supported value; no fake choice]
* MIX dry/wet (0–1, default 1 wet), BYPASS (VST3 bypass), OUTPUT LEVEL
  (−24..+12 dB, default 0).

The adapter's EngineConfiguration mapping is derived from the SAME
engine-owned descriptors (no engine-id if-chain; the PV hop ≤ fft/2 clamp
is the engine-declared constraint). Discrete parameters display their
engine-declared choice text ("2048", "reference", "hann") — never a bare
index; parsing accepts the choice text, the actual engine value and the
index.

Seeds: the granular engine's SeededDeterministic seed is a fixed product
constant (documented; jitter default 0 makes it inert for defaults).

**THE TASK-33 FROZEN-SURFACE AMENDMENT (recorded 2026-10-01, BEFORE coding
— the deliberate parameter-model changes for `native.timepitch`, each
following the Task-32 single-deliberate-change precedent):**

1. **ENGINE row domain: five → six** — the shared table's `kEngine` row
   gains `max` 5, `stepCount` 5 (indices 0..5; default stays 1 =
   vardelay). The ONE deliberate frozen-surface change of this task;
   recorded here + in the frozen-surface test comment. Old states load
   unchanged (engineIndex values 0–4 stable; `native.timepitch` does not
   exist in old states; new states are forward only).
2. **Six new VST tags (frozen, never renumbered):** free IDs 22–27 —
   `kTpMode` (22), `kTpWindow` (23), `kTpOverlap` (24), `kTpShape` (25),
   `kTpTolerance` (26), `kTpFormant` (27) — plus six `kEngineParamBindings`
   rows (`(native.timepitch, mode)`, …) and six `ParamSnapshot` /
   `ChainSignature` POD fields (`mode` and every rebuild-triggering
   parameter are signature members; γ is a signature constant, not a
   curve, in v1). `validateEngineParameterModel()` stays green
   (uniqueness); IDs 28–29 and 5–9 remain free.
3. **parameterCount 19 → 25** (frozen test expectation); per-engine
   visible tag/key sets + hidden counts extended; normalisation
   round-trips for the new rows.
4. **`chainGeometry` + `expectedLatencyFrames`:** a new `native.timepitch`
   branch with the per-mode geometry (spec §6.6.1 item 17: wetLen =
   jobInputLen = 10 s; seamX = N (Fixed/Adaptive) or 2·pMax
   (Pitch-Synced/Pitch + Formant); spliceMode = false) and the per-mode
   declared latency (§6.6.1 item 10; the tracker term Λ_tr first-class,
   ≈122 ms total @48k Pitch-Synced); `worstCaseLatencyFrames` extended
   with the new engine's worst Λ over the FULL parameter surface
   (window 16384, tolerance 8192, tracker at 192 kHz) — sizes
   dryRetention + laneCapacity.
5. **Per-mode parameter visibility (the net-new minimal mechanism):**
   `EngineParamDescriptor` gains optional descriptor-level visibility
   data (`visibleWhenKey`/`visibleWhenValues`; NULL = always visible);
   the editor filters `engineParamsFor(engineIndex)` by evaluating the
   guard against the authoritative `mode` value — descriptor-driven, the
   UI stays dumb, no engine list is reintroduced. The VST parameter
   COUNT does not change with the selected mode (hidden-by-condition
   params remain registered — UI-invisible, not deregistered): no tag
   churn, no state-compat hazard, no parameter-count flicker on mode
   change. Per-mode visibility map (spec §6.6.1 item 18): window/
   overlap/shape ⇒ {fixed, adaptive}; tolerance_frames ⇒ {adaptive};
   formant_ratio ⇒ {pitch_formant}; `puv_hz` ⇒ never (hidden).
6. **State compatibility statement:** states saved by older builds load
   with identical behaviour for the five existing engines; new states
   are forward only. The frozen-surface equality test is updated in the
   SAME commit as the implementation (never before).

## 7. UI (VSTGUI editor)

A hand-drawn VSTGUI editor (custom `CView`/`CControl` subclasses — not an
XML template screen): dark zinc panels (zinc-950/zinc-900), emerald accent
(active engine, meter peaks, primary), amber for the varispeed adaptation
badge, monospace uppercase micro-labels — the workbench identity (the
workbench itself is untouched). Fixed logical size 680×486 (Task 32: the
editor grew from 680×450 to hold the realtime-status lines), DPI-scaled.
Sections: ENGINE (segmented selector, 5 entries from the registry — the
recorded task-33 target state is 6 entries with the panel filtered by the
§6 per-mode visibility mechanism; the selector grows with the
implementation commit, never before), PITCH
(large semitone control + the SHARED pitch-curve/LFO row), ENGINE panel
(per-engine controls — see below), OUTPUT (dry/wet, bypass, level), METERS
(stereo in/out peak+RMS), STATUS (engine display name, adaptation label,
fs, block size, latency ms, live job state, re-prepare count, clamps,
faults + the Task 29 CATEGORIZED fault diagnostics — real values only, NO
quality score, NO fake DSP indicators). The editor talks to the processor
via: parameter get/set (the normal VST3 path) and two read-only interfaces
(`IPitchLabMeters`, `IPitchLabStatus` — atomically published snapshots
written by the audio thread, polled by a 30 Hz UI timer; the editor NEVER
touches the audio path directly).

**THE REALTIME-CAPABILITY STATUS (Task 32):** the status panel's FIRST
line is the classification of the CURRENT configuration's realtime
sustainability — REALTIME OK / REALTIME LIMITED / !! REALTIME NOT
SUSTAINABLE / REALTIME STATUS UNKNOWN — with a warning detail line for
the non-healthy states ("current configuration exceeds measured realtime
capacity — use offline render"). The classification's evidence model
(kept distinct from ENGINE CAPABILITY, which all five engines have):
a MEASURED basis (the audio thread's own steady-clock measurement of the
engines' process/finish cost vs the audio timeline since the current
chain's adoption — the numeric StatusSnapshot fields engineCpuNanos/
rtFrames; thresholds RTF ≤ 0.75 = OK, ≤ 1.0 = LIMITED, > 1.0 = NOT
SUSTAINABLE; the aggregate fault counter escalates one level, never
two), a BENCHMARK prior basis (encoded measured evidence, used ONLY
before the runtime window suffices: exactly one row — native.granular at
≥ 88.2 kHz → LIMITED, citing the Task-30 96 kHz matrix 0.58–1.89
by-configuration spread; a measurement ALWAYS supersedes it), and the
honest UNKNOWN. All string derivation stays on the UI side
(`src/vst/realtime_status.{h,cpp}` — the audio thread stays numeric-only).

**The ENGINE panel is ENGINE-DRIVEN (Task 29):** the editor holds NO
engine-parameter membership knowledge and NO engine-index switch. The
selected engine → registry lookup → the engine's own parameter
descriptors → control creation (title from the descriptor's display name;
value = the model's choice text/formatted value + the engine-declared
unit; the slider binds to the stable VST tag). On engine change: destroy
only the previous engine's controls, create the new engine's, populate
from the authoritative controller values. Each row is title + readable
value on one line, slider below (the 12-pixel value-label defect is
designed out; not merely widened). The controller remains the single
authoritative parameter state — the UI never keeps a second store.

**The fault diagnostics (Task 29):** the status snapshot publishes NUMERIC
COUNTERS ONLY (categorised: engine process faults, engine exceptions,
delivery underruns, dry-history misses, job stalls, chain adoption
failures, preparation failures, plus cadence figures — jobs, preparing,
adoptions, process calls); the audio thread never formats strings; the UI
formats them. The aggregate `faults` keeps its historical composition
(engine faults + underruns + dry-misses) for compatibility; the new
categories identify root classes without retroactively changing what
`faults` means.

## 8. Host behaviour requirements (frozen checklist)

Automation (sample-accurate via IParameterChanges; UI moves via the
normal setParameter path — both merge into the same parameter snapshot);
sample-rate change (full chain re-prepare, state kept); block-size change
(re-prepare if beyond prepared max; any block size ≥ 1 accepted);
bypass (latency-compensated, crossfaded); reset (streaming state zeroed,
job restarted); suspend/resume (resume = reset + fresh chain);
state save/load (full parameter set via the standard VST3 state stream —
round-trip tested); latency reported per §4; mono AND stereo (all five
engines declare MonoAndStereo); Sample32 AND Sample64 processing
(engines are float64-internal — 32-bit converted at the boundary);
transport-independent (documented assumption: the pitch curve is a
function of the processing timeline, not the transport position).

## 9. Testing (implementation quality, NOT a project gate)

Doctest suites in `pitch-lab/tests/` (the v0.1 pattern, but scoped to the
product layer — no new project-wide gate, no reopening of v0.1 evidence):

* parameter model suite: definition table integrity, registry mapping,
  default/normalisation round trip;
* state round trip: full parameter set through the VST3 state stream
  (MemoryStream) → restore → equality;
* processor host-simulation suite: a real lifecycle driver (initialize →
  bus arrangements → setProcessing → process with generated block
  schedules incl. block-size changes mid-stream → terminate), asserting:
  in/out frame balance, bypass behaviour, sample-rate change, reset,
  automation propagation into the curve (asserted via the adapter's
  status snapshots), mono/stereo, determinism (same drive = same output,
  bitwise), engine switching, RT-fault fallback path;
* adapter unit suite: envelope/clamp policy, job lifecycle accounting
  (every complete job honours configure→prepare→process*→finish exactly
  once), seam crossfade sums, pacing rule, varispeed window geometry
  (advance = L/r̄, overlap X, latency bound), curve materialisation
  order; mid-stream chain replacements: EVERY ordered engine pair switch
  in both directions (fault-free + the effective-latency maximum),
  envelope exits on preserving AND splice engines, hard resets;
* UI parameter binding (headless: the editor's parameter-to-control map
  is verified against the parameter model without opening a window).
* The official **VST3 validator** (SDK `validator` target) runs against
  the built `.vst3` bundle in CI and locally (the SDK's own
  host-infrastructure validation).

## 10. Product deliverables

* `pitch-lab/build/.../PitchLab.vst3` (Linux x64 reference build in the
  sandbox; Windows x64 via the documented build path §11);
* source + CMake integration (`PITCHLAB_BUILD_VST3`);
* reproducible build instructions (§11);
* retained screenshots of the final UI (`pitch-lab/results/vst3/v0.1/ui/`);
* representative audio examples produced through the REAL VST3 process
  path (the host-simulation driver rendering corpus material through each
  engine) under `pitch-lab/results/vst3/v0.1/examples/`;
* documentation: this spec (architecture + boundary + parameter model),
  `pitch-lab/docs/vst3-product.md` (user-facing: parameter mapping,
  runtime architecture, known limitations), README updates.

## 11. Build & install (reproducibility)

Sandbox reference (Linux x64): pinned CMake 3.31.6 (`$HOME/.local`),
GCC 14.2 (local) / 13.3 (canonical CI), Ninja; `cmake -S pitch-lab -B
build -G Ninja -DCMAKE_BUILD_TYPE=Release` → the `PitchLab_vst3` target
emits the `.vst3` bundle; `validator <bundle>` runs the SDK validation;
install = copy the bundle into the user VST3 folder
(`~/.vst3` on Linux, `C:\Program Files\Common Files\VST3` on Windows).

Windows x64 (primary target, documented): Visual Studio 2022 (MSVC 19.3x,
x64), CMake ≥ 3.21 + the same pinned VST3 SDK tree, `cmake -S pitch-lab
-B build -G "Visual Studio 17 2022" -A x64`, build the `PitchLab_vst3`
target (the SDK's Windows CMake path handles the COM-style entry point
and .vst3 bundle layout). CI keeps the canonical Linux validation; a
Windows CI lane is a future owner decision, not part of this phase.

## 12. Known limitations (honest, user-visible)

* varispeed AND granular realtime = windowed-splice adaptation (seam
  cadence ≈ L/r̄; labelled in the UI; offline continuous renders remain
  the reference). The granular splice run is the recorded §3 correction —
  the engine's read grid diverges from the output timeline at ratio ≠ 1
  exactly as varispeed's output length does; both run as true engines in
  crossfaded windows.
* The splice engines declare FIXED worst-case latencies over the full
  ±12 st parameter range (varispeed ≈ 226 ms, granular ≈ 325 ms + grain at
  48 kHz — stability over optimality; a per-envelope latency would change
  on every re-prepare, which hosts tolerate poorly).
* The effective emission latency is the activation's maximum (§4.1 item 5,
  the Λ_eff correction): engine switches never lower the latency
  mid-stream; reactivate (toggle processing) to return to the engine's own
  latency. An engine switch into a HIGHER-latency engine — or any
  same-engine configuration change that grows Λ (the granular grain
  length) — re-covers the last Δ frames (the inherent, in-any-plugin cost
  of a mid-stream latency increase; Task 31: the re-coverage is served
  from the retained wet history as a bit-identical replay, then
  crossfade-blended at the seam — never a dry burst; see §4.1 item 5's
  Task-31 seam-coverage rule).
* pv.classic AA cutoff carries the +1 st envelope margin (§4.1 item 2).
* Automation that exits the ±1 st envelope re-prepares the chain (a
  crossfade seam during fast pitch sweeps — masked by the sweep itself).
* **The `native.timepitch` Fixed-mode `window_shape=rect` synthesis
  character (the TASK G recheck record, measured at `d8b226a`):** at any
  NON-ZERO pitch the rect window's OLA synthesis produces audible
  grain-boundary content discontinuities — the rect window has NO taper,
  so the stretched-content mismatch at every OLA grain boundary passes
  through the window-product normalisation unattenuated (the tapered
  shapes hann/hamming/bartlett mask the same discontinuity to zero).
  MEASURED: direct offline render AND production adapter path, saw 220,
  48k/512, the harness click threshold: rect +7 = 81/79 clicks (direct /
  adapter — the two paths agree), −7 = 154/150, +12 = 515/502, −12 =
  137/134, 0 st = EXACT identity (0 clicks, −240 dB) on both paths; ZERO
  delivery faults, ZERO re-centres, ZERO seam correlation on the static
  rows (the crackle is the synthesis, not the delivery and not the
  re-centre seam). The production engine is BIT-IDENTICAL to the
  validated Task-28 OLA reference on every rect configuration (the
  committed `timepitch_ola_reference_test`: e.g. +12/ov2 rect 717/717
  clicks on BOTH sides) — the reference behaviour itself, not a migration
  defect. Adjacent measured class: at +12 st on 44.1 kHz the hann shape's
  own content steps measure maxΔ 0.129 — marginally above the 0.12
  harness threshold (48 kHz measures 0.119, just below it; 96 kHz 0.059)
  — the same content class, fs-scaled, below the click threshold at
  every other (fs, pitch) point. Evidence:
  `results/vst3/task35/rc3-rect-recheck/` (the 420-row matrix + the
  listening WAVs + the click zoom) and the worklog task-35 TASK G entry.
  This is FROZEN-design behaviour (§6.6.1 window arithmetic verbatim from
  the validated prototype) — an owner-decision item, never silently
  changed.
* Realtime output is deterministic per (parameters trajectory, input,
  block schedule) while the chain set is constant — the single-chain drive
  is bit-identical across runs (tested); the exact BLOCK of a mid-stream
  chain swap depends on preparation timing (the async prep hand-off is
  part of the design; the offline pipeline remains the bit-exact research
  reference). It is NOT bit-identical to the offline renderer's output for
  the "same" curve (different contract, §3).
* Finite drives (offline bouncing through the process path) may leave
  the last splice window's read reach unfulfilled — the bounded tail
  falls back to dry; realtime streams never end.
* **Measured 96 kHz findings (Task 29; the instrumented drives live in
  `results/vst3/task29/diagnostics-measurements.md`):**
  * **native.granular @ 96 kHz is CPU-bound in the windowed-splice
    adaptation** (RTF ≈ 1.1 at identity, ≈ 1.33 at −12 st, ≈ 1.75 at
    grain 0.5 s on the reference machine) while every FAULT class
    measures ZERO in isolation (engine faults, underruns, dry-misses,
    stalls, adoption failures, preparation failures) — the stutter's
    primary cause is the legitimate splice cost itself, not a lane/lifecycle
    defect. Task 32: this is no longer silent — the editor's
    realtime-capability status classifies the CURRENT configuration from
    the audio thread's own measurement (the boundary-spanning family gets
    a benchmark prior LIMITED within ~0.1 s of adoption, then the measured
    verdict: the −12 st/grain-0.5 configurations surface the NOT
    SUSTAINABLE warning "use offline render"; the +12 st configurations
    measure ~0.6 and classify OK — per configuration, never a blanket
    engine verdict). The previously recorded granular downshift limitation
    (envelope-edge curve materialisation) remains documented and
    untouched.
  * **A secondary, measured churn amplifier:** at the ±12 st parameter
    boundary with LFO, the ±1 st envelope is truncated by the parameter
    range (envMin floors at ratio 0.5), so every LFO dip below the
    boundary clamps and requests a rebuild whose re-centred envelope
    STILL cannot cover the excursion — a futile churn loop (measured:
    759 clamps / 329 re-prepares / 328 adoptions per 2 s ≈ a seam every
    6 ms), fault-free but audible as stutter. Bounded-unresolved: the
    host-side fault counts (e.g. the reported 1021 faults) were not
    reproduced in isolation; they are consistent with host CPU
    starvation downstream of the two measured pre-conditions. A next-task
    candidate (suppress futile re-prepare requests when the envelope is
    structurally unable to cover the excursion) is recorded in the
    worklog, NOT implemented here (this task's mandate is the parameter
    model + diagnostics; the audio-path behaviour is untouched and the
    byte-identical artifact evidence is preserved).
  * **native.pv.phaselocked @ 96 kHz is healthy on every measured axis**
    (faults 0 at 48 AND 96 kHz across blocks 128–1024; RTF 0.026–0.063;
    one chain, one adoption, no churn) — the reported 96 kHz stutter is
    NOT reproducible in the adapter path in isolation; its ownership
    lies outside the plugin's realtime path as measured (host-side load
    remains the unmeasured residual).
* OD-6/OD-9/OD-18 remain OPEN owner decisions, untouched.

## 13. No-verification-gate statement

This phase adds implementation-quality tests (§9) and CI build lanes, NOT a
project verification gate. v0.1's closed evidence (§17.6 PASS,
results/v0.1) is not re-run, re-audited, or extended by this work.
