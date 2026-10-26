# OCTAVOX native core -- VST3 wrapper readiness assessment (R14 scope deliverable)

Status date: 2026-10-26 (round 15, after the P1 per-voice parameter surface;
written during the R15-P2 session). Assessed object: the CURRENT public API
(`include/octavox/dsp.hpp`, version 15.0.0) as documented by its normative
header contract. This is an honest readiness note, not a marketing document.

**Context caveat (recorded honestly):** a second environment reset wiped the
live tree during the R15-P2 session AFTER the 78/78-green state was verified
at session start. The 15.0.0 API surface is preserved verbatim
(`include/octavox/dsp.hpp` restored); the implementation was being recovered
at the time of writing. The measurements quoted below are the recorded ones
(14.0.0 baseline `evidence/analysis_round14_recovery/r14_baseline_bench_stdout.txt`
+ the P1 re-measurement of 15.0.0, unchanged vs baseline). See
`evidence/analysis_round15/NOTES_R15P2.md`.

---

## Dimension-by-dimension assessment

### 1. Process-cycle contract — **READY**
- Planar float in/out: `AlignedDelayLine::process(const float*, float*, int)`
  mono; `MultichannelShifter::process(const float* const*, float* const*, int, int)`
  planar N-channel. Matches VST3's planar `AudioBusBuffers` layout directly.
- Block-size agnostic: the control-rate detector fires every
  `detection_interval` STREAM samples, so the output is invariant to how the
  host splits the stream into blocks (gated by `block_size_invariance` over
  splits 1/7/16/64/128/512/1024/4096 + a mixed pattern). Any host block size
  1..anything works; n = 0 calls are no-ops.
- in/out non-aliasing: documented requirement, satisfied by the engine
  (reads complete before the output write per sample).
- In-place processing (in == out) is NOT tested/contracted — a wrapper must
  use separate buffers (VST3 hosts may provide in-place; the wrapper copies).

### 2. Parameter surface — **PARTIAL** (sufficient for bindings; one documented gap)
- Available now (all audio-thread-safe, wait-free, allocation-free):
  global `setRatio/setVoices/setSpread/setFeedback`, per-voice
  `setVoiceRatio/setVoiceGain/setVoiceMute/setVoiceBaseDelay`, `setWetDry`
  (linear law, latency-aligned dry), `setBypass` (click-free, state
  transparent). This is sufficient for a wrapper's parameter bindings:
  every VST3 parameter a harmonizer exposes maps onto these.
- **GAP (documented): block-boundary application, not sample-accurate.**
  A parameter change issued between `process()` calls is picked up at the
  START of the next call; simultaneous updates are atomic at that boundary
  (no partial states). VST3's `IParameterChanges` delivers sample-accurate
  parameter segments within a block. Closing the gap requires EITHER
  (a) a host-side ramp: the wrapper receives the segment and ramps its own
  target toward it (the engine's built-in ramps -- gain/mute over
  `gain_ramp_seconds`, bypass over `bypass_seconds`, ratio over the
  geometric glide -- make the audible result click-free at typical block
  sizes; this is the recommended wrapper strategy today), OR (b) a future
  core extension (sample-indexed parameter queues inside `process()`).
  Not scheduled before the acceptance suite is green.

### 3. State persistence — **PARTIAL** (deterministic reset/re-prepare; no serialization API)
- Deterministic: `reset()` returns the engine to the post-prepare state
  (parameters kept); `prepare()` with the same buffer-determining fields
  reconfigures in place, zero-alloc, bit-identical to a fresh engine
  (counter-gated). A full parameter-event replay reproduces the run
  bit-exactly (the r15acc determinism category; reset/re-prepare/fresh
  engine equivalence).
- **No save/restore serialization API exists.** A wrapper must snapshot
  itself:
  * the whole `Config` (sample_rate, ratio, voices, max_voices, spread,
    feedback, interpolation, grain, base_delay, fmin, fmax, s_hysteresis,
    f0_history, detection_interval, engagement_seconds,
    gain_ramp_seconds, bypass_seconds);
  * the live parameter surface: per-voice ratio override flags+values
    (an override survives reset; `setRatio/setSpread/prepare` clear them),
    per-voice user gains, per-voice mute flags, per-voice base-delay
    overrides, `setWetDry` value, bypass on/off, active voice count
    (`setVoices`);
  * note: the engine's INTERNAL streaming state (ring contents, glide
    positions, detector history, feedback state) is NOT serializable --
    restore = `prepare(cfg)` + re-apply the parameter surface, which
    reproduces behaviour from stream position 0 (the documented
    determinism contract). Mid-stream resume is out of scope.

### 4. Bus / layout — **READY** (sidechain/aux: not applicable)
- `MultichannelShifter` handles 1..64 planar channels with structural
  isolation (one engine per channel; gated: R output bit-independent of L
  processing, including with the full round-15 surface active). Mono-in /
  stereo-out (or any N-in/N-out) is a wrapper-level routing decision.
- Sidechain / aux buses: not applicable to this DSP (no sidechain
  parameter exists); a wrapper simply does not declare them.

### 5. Sample-rate / block-size changes — **READY**
- Same-size re-prepare is the supported audio-thread fast path (zero
  allocation, zero locks; R13-c, counter-gated).
- Sample-rate change: `prepare()` with a different `sample_rate` takes the
  allocate-and-rebuild path — must be synchronised off-thread or during a
  host pause (documented in the RT contract). The detector window is
  sample-rate relative (R13-b), so 44.1/48/96 kHz are all supported
  (96 kHz gated by the detector battery).

### 6. Allocation contract — **READY**
- Prepare-time only. After `prepare()` returns: `process()`, `reset()` and
  every parameter setter are steady-state allocation-free — verified by the
  global-new counter across the whole suite (including mid-ramp/mid-crossfade
  setter calls and diagnostics). Bench rows record 0 allocations.

### 7. Threading — **READY**
- RT contract in the header: one engine = one thread at a time; all setters
  + `process`/`reset`/diagnostics wait-free, lock-free, non-throwing on the
  audio thread; immutable interpolator tables (thread-safe magic static).
  `MultichannelShifter` engines may be farmed out one-per-thread by the
  host without locks. VoiceStats is returned into thread-local scratch —
  copy out if it must survive the next call (documented).

### 8. Realtime factor — **READY** (ample margin)
- Recorded measurements: mono 1v 1337-1358 ns/frame = 15.5x realtime,
  mono 8v 1566-1571 ns/frame = 13.3x, stereo 8v 3112 ns/frame = 6.7x at
  48 kHz; control-sparse (detection_interval 512) 408 ns/frame = 51x
  (14.0.0 baseline, `r14_baseline_bench_stdout.txt`; P1 re-measured 15.0.0
  unchanged: 1v 15.47x, 8v 13.73x, ctrl512 50.66x, 0 allocs, RSS
  8.1-12.6 MB + ~2 MiB/engine for the R15 dry ring). The default (wet=1)
  path is bit-identical to 14.0.0 and costs the same; the wet path adds one
  dry-ring copy + one multiply-add per sample. 96 kHz and active-surface
  rows are specified in the R15-P2 bench extension (pending re-measurement
  after the tree recovery, see NOTES_R15P2.md).

---

## Overall verdict (honest statement)

The core API is **wrapper-ready EXCEPT** (1) **sample-accurate parameter
automation** — application is block-boundary; a wrapper must ramp host-side
or a future core extension is needed — and (2) **state serialization** —
deterministic reset/re-prepare exists, but the wrapper must snapshot the
Config + live parameter surface itself (list above).

**No wrapper is built, by design.** A VST3 wrapper is a later milestone,
to be started only after the round-15 13-category acceptance battery is
green on a recovered tree (per the round-15 task book; the battery's 7 new
`r15acc_*` cases are designed and salvaged in
`native/tests/r15_acceptance_cases.cpp.inc`, not yet run — see
`evidence/analysis_round15/NOTES_R15P2.md`).
