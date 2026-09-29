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

## 4. The realtime adapter (frozen design)

### 4.1 Preserving engines (vardelay, granular, pv.classic, pv.phaselocked)

The unbounded realtime stream is presented to the engines as a sequence of
finite VIRTUAL JOBS. Each job is a real, contract-honouring engine
lifecycle: `configure → prepare → process* → finish → destroy` (aborted
jobs destroy without finish — a declared realtime path; complete jobs
honour the full contract).

1. **Job geometry.** Job k covers the absolute input interval
   `[A_k, A_k + N_seg)` with `N_seg = 30 s · fs` (long: seams are rare;
   automation is NOT quantised to jobs — see item 3). Consecutive jobs
   OVERLAP by `X` frames (per-engine: vardelay W, granular G, PV N) — the
   same absolute input is fed to both instances during the overlap
   (rebased), and the output is an equal-power crossfade over exactly X
   frames at the seam. Crossfaded splices are the declared realtime
   adaptation (absent in offline renders; at identity the two branches
   render identical content for the overlap-honouring engines, so the
   seam is numerically transparent there).
2. **Envelope + clamp (the scan-safety rule).** At prepare of job k the
   adapter establishes the pitch envelope
   `env = [r_live·2^(−1/12), r_live·2^(+1/12)]` (±1 st around the live
   ratio) and pre-fills the job's dense curve array with the envelope
   extremes (varispeed-style engines: alternating env-min/env-max; sum- and
   horizon-type scans: env-max throughout) so EVERY prepare-time scan
   bounds the worst case the job will ever see. The runtime curve value is
   `clamp(automation(t) · lfo(t), env)`. Consequences, all documented and
   bounded:
   * sizing (windows, budgets, accumulators, horizons) is correct for any
     automation within the envelope — no exception paths, ever;
   * the pv.classic AA cutoff is the envelope max (≈ +1 st above live):
     at identity the cutoff is `0.95/2^(1/12) ≈ 0.897·Nyquist` (≈19.8 kHz
     at 44.1 kHz — full band to the ear); at the extremes it is ≈5% more
     conservative than the offline engine's exact-curve cutoff;
   * automation that exits the envelope TRIGGERS a re-prepare (item 4)
     rather than being silently clamped forever: the clamp only bridges
     the few milliseconds until the replacement job is live. Static
     settings: no clamping occurs at all.
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
   Re-prepare triggers: envelope exit, engine selection change, engine
   parameter change, sample-rate change, block-size growth beyond the
   prepared max, reset/flush request, job-length cap (N_seg). The audio
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

## 6. Parameter model (ONE authoritative layer)

`src/vst/parameters.h` is the single definition of VST parameter identity
(id, tag, type, range, default, unit, title, short title, flags, grouping);
the processor, the UI and the tests are all generated from it. It maps the
ENGINE selector onto the v0.1 registry iteration order (identity from the
registry, never a second list). Exposed parameters (product surface):

* ENGINE (discrete, registry order, default native.vardelay — the
  realtime-native engine).
* PITCH (−12.0..+12.0 st, default 0, display st with cents) — the
  automation surface; `ratio = 2^(st/12)` per frame.
* LFO RATE (0.1–8 Hz, default 5), LFO DEPTH (0–2 st, default 0 = off) —
  the runtime pitch-curve control appropriate to the realtime model
  (curve = automation + sine LFO, envelope-clamped).
* Per-engine panels (UI shows only the selected engine's controls; the
  non-selected engines' parameters hold their values but are not applied
  — declared, not fake):
  * varispeed: resample quality (small/standard/reference), allow aliasing
    (bool). [adapter window L is fixed at 0.25 s, documented, no fake
    control]
  * vardelay: excursion seconds (0.05–2.0 s, default 0.5 — the v0.1
    default), crossfade frames (0–8192, default 2048 — the v0.1 default).
  * granular: grain seconds (0.02–0.5, default 0.1 — v0.1 default),
    overlap (4–16, default 4 — v0.1 default), jitter frames (0–256,
    default 0 — v0.1 default), window (hann/triangular).
  * pv.classic: FFT size (1024/2048/4096, default 2048 — v0.1 default),
    hop (128/256/512/1024, default 512 — v0.1 default).
  * pv.phaselocked: FFT size + hop (same defaults). [locking_mode is
    fixed "identity" — the only supported value; no fake choice]
* MIX dry/wet (0–1, default 1 wet), BYPASS (VST3 bypass), OUTPUT LEVEL
  (−24..+12 dB, default 0).

Seeds: the granular engine's SeededDeterministic seed is a fixed product
constant (documented; jitter default 0 makes it inert for defaults).

## 7. UI (VSTGUI editor)

A hand-drawn VSTGUI editor (custom `CView`/`CControl` subclasses — not an
XML template screen): dark zinc panels (zinc-950/zinc-900), emerald accent
(active engine, meter peaks, primary), amber for the varispeed adaptation
badge, monospace uppercase micro-labels — the workbench identity (the
workbench itself is untouched). Fixed logical size 640×420, DPI-scaled.
Sections: ENGINE (segmented selector, 5 entries from the registry), PITCH
(large semitone control + LFO row), ENGINE panel (per-engine controls),
OUTPUT (dry/wet, bypass, level), METERS (stereo in/out peak+RMS),
STATUS (engine display name, adaptation label, fs, block size, latency ms,
live job state, RT re-prepare count, faults — real values only, NO quality
score, NO fake DSP indicators). The editor talks to the processor via:
parameter get/set (the normal VST3 path) and two read-only interfaces
(`IPitchLabMeters`, `IPitchLabStatus` — atomically published snapshots
written by the audio thread, polled by a 30 Hz UI timer; the editor NEVER
touches the audio path directly).

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
  latency. An engine switch into a HIGHER-latency engine re-covers the
  last Δ frames (the inherent, in-any-plugin cost of a mid-stream latency
  increase — crossfade-blended, bounded by Δ).
* pv.classic AA cutoff carries the +1 st envelope margin (§4.1 item 2).
* Automation that exits the ±1 st envelope re-prepares the chain (a
  crossfade seam during fast pitch sweeps — masked by the sweep itself).
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
* OD-6/OD-9/OD-18 remain OPEN owner decisions, untouched.

## 13. No-verification-gate statement

This phase adds implementation-quality tests (§9) and CI build lanes, NOT a
project verification gate. v0.1's closed evidence (§17.6 PASS,
results/v0.1) is not re-run, re-audited, or extended by this work.
