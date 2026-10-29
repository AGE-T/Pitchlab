# OCTAVOX native core -- VST3 wrapper readiness assessment (R14 scope deliverable)

Status date: 2026-10-26 (round 15, after the P1 per-voice parameter surface;
written during the R15-P2 session). Assessed object: the CURRENT public API
(`include/octavox/dsp.hpp`, version 15.0.0) as documented by its normative
header contract. This is an honest readiness note, not a marketing document.

**Context caveat (historical, written mid-incident):** a second environment
reset wiped the live tree during the R15-P2 session AFTER the 78/78-green
state was verified at session start. At the time of writing the 15.0.0
implementation was being recovered; the 15.0.0 API surface was preserved
verbatim (`include/octavox/dsp.hpp` restored) and the measurements quoted
below were the recorded ones (14.0.0 baseline
`evidence/analysis_round14_recovery/r14_baseline_bench_stdout.txt`
+ the P1 re-measurement of 15.0.0, unchanged vs baseline). See
`evidence/analysis_round15/NOTES_R15P2.md` for the incident record.

> **[R16-P1 reconciliation, 2026-10-28]** The intermediate-stage uncertainty
> above is superseded by the FINAL round-15 state: `src/dsp.cpp` was
> re-implemented (worklog R15-REB1) and verified bit-neutral vs 12.0.0 by the
> four reference hashes (`native/r15_reference_hashes.txt`, re-verified
> 2026-10-28 before any round-16 edit); the full 85-case suite — including
> the 7 `r15acc_*` acceptance cases — was wired and delivered **85/85
> passing (release AND ASan/UBSan, zero sanitizer reports)** with the
> machine-readable result file
> `evidence/analysis_round15/r15_acceptance_results.json`; the 96 kHz and
> active-surface benchmark rows were measured and delivered
> (`r15_bench_15.txt`). This note's per-dimension assessments stand as
> written; the round-16 wrapper milestone is tracked separately (see the
> round-16 addendum at the end of this file when delivered).

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
  rows were subsequently measured and delivered (R15-REB2,
  `evidence/analysis_round15/r15_bench_15.txt`): mono_1v_b128_96k
  4998.30 ns/frame = 2.08× realtime, mono_8v_b128_96k 5211.17 = 2.00×,
  mono_8v_b128_surface 1520.40 = 13.70× — 0 allocations everywhere.
  [R16-P1 correction, 2026-10-28: this paragraph previously ended "pending
  re-measurement after the tree recovery" — superseded; see the
  reconciliation log.]

---

## Overall verdict (honest statement)

The core API is **wrapper-ready EXCEPT** (1) **sample-accurate parameter
automation** — application is block-boundary; a wrapper must ramp host-side
or a future core extension is needed — and (2) **state serialization** —
deterministic reset/re-prepare exists, but the wrapper must snapshot the
Config + live parameter surface itself (list above).

**No wrapper is built, by design.** A VST3 wrapper is a later milestone,
to be started only after the round-15 13-category acceptance battery is
green on a recovered tree (per the round-15 task book).

> **[R16-P1 reconciliation, 2026-10-28]** The battery condition is MET: the
> seven `r15acc_*` cases were wired into `tests/test_main.cpp` by R15-REB2
> and the delivered suite is **85/85 passing** (release + ASan/UBSan) with
> the machine-readable result file shipped at
> `evidence/analysis_round15/r15_acceptance_results.json` — the earlier
> "designed and salvaged … not yet run" statement above is superseded
> (kept for the correction history). The wrapper milestone itself is the
> round-16 task book's Priority 4; the outcome (implemented wrapper or
> documented blocker) is recorded in `OCTAVOX_ROUND16_FINDINGS.md` and the
> round-16 addendum to this file.

---

## ROUND-16 ADDENDUM (2026-10-28): the wrapper milestone is DELIVERED

The readiness assessment above was the round-15 state. Round 16 (task
R16-P4b) closed the milestone it pointed to:

* **A real, minimal VST3 wrapper is implemented, built and validated** —
  `native/vst3/` wraps the untouched 15.0.0 core as the module
  `OctavoxNative.vst3` (SingleComponentEffect: processor + controller in
  one component; symmetric mono/stereo buses; 38 parameters incl. the full
  per-voice surface; sample-accurate automation via block segmentation;
  OVR1 state save/restore; latency = `latencySamples()`, click-free bypass
  via `setBypass`; silence/n=0/block-size/reset handling; in-place-safe;
  **zero allocations in `process()`**).
* **The two PARTIAL dimensions above are now closed by the wrapper
  itself:**
  * *Sample-accurate automation*: the wrapper applies parameter events at
    their exact in-block sample offsets by splitting the block at the event
    boundaries and invoking the core's setters between the sub-block
    `process()` calls — i.e. it uses the core's OWN tested block-boundary
    contract as the engine of sample accuracy. The equivalence is PROVEN
    bit-exact by the wrapper test `segmentation_equivalence_BITEXACT`
    (maxdiff = 0 vs the hand-driven core with the same setter calls at the
    same stream positions) — the "host-side ramp OR core extension"
    alternative (a) from the assessment turned out unnecessary: plain
    segmentation is exact, because the core is block-agnostic by contract.
  * *State serialization*: the wrapper snapshots exactly the list above
    (whole Config + the live 38-value surface + bus channels) into a
    versioned binary blob; restore = `prepare(cfg)` + re-apply the surface,
    reproducing the run bit-exactly from stream position 0 (gated:
    live/same-instance/fresh-instance restores all maxdiff = 0).
* **Validation:** the VST3 SDK's own validator (v3.7.12_build_20) passes
  **47/47** against the built module; the in-process wrapper test suite
  passes **8/8** (lifecycle, conversions, segmentation equivalence, state
  roundtrips, silence/blocks/reset, in-place, 0-allocations, latency).
  Evidence: `../evidence/analysis_round16/r16_vst3_wrapper_tests.txt`,
  `r16_vst3_validator.txt`; implementation + build + SDK acquisition:
  `native/vst3/VST3_WRAPPER.md`.

The overall verdict is therefore superseded: the core is
**wrapper-ready AND wrapped** — the remaining "limitations" are the
wrapper's documented scope decisions (no GUI editor, symmetric buses only,
no mid-stream resume), listed in VST3_WRAPPER.md §6.

---

## Round-17 addendum (2026-10-29) — reproducible build, audited parameter/state contract

* **Reproducible from a clean extraction (R17-P2).** The pinned-SHA SDK
  acquisition is now scripted (`native/tools/get_vst3_sdk.sh` / `.ps1`:
  exact-revision fetches, `git rev-parse` verification, tag-gitlink
  cross-check, evidence-tree write refusal, clear failures; the POSIX
  variant executed in both `--verify` and fresh-install modes this round —
  the working SDK content-matches all four components). The full
  validation chain is scripted (`native/tools/build_and_test.sh`,
  executed end-to-end: release leg 74 s, ASan leg 376 s, wrapper leg —
  `RESULT: ALL GATES PASSED, total wall 454 s`). The Windows/MSVC
  counterpart (`build_and_test.ps1`) is authored with MSVC portability
  guards in the build files, but **no Windows build has been executed or
  tested** — see VST3_WRAPPER.md §6.
* **The parameter and state contract is audited and machine-checked
  (R17-P3).** The wrapper test suite grew 8 → **15 cases**: the 38-parameter
  metadata table (id uniqueness, spec integrity, processor/controller
  `ParameterInfo` agreement, boundary/middle conversion roundtrips),
  events at block start/middle/last/block-end (bit-exact), simultaneous
  multi-voice events at one sample position (bit-exact), bypass+wet/dry
  automation clickless (bounded step) with **0 allocations**, the full
  38-parameter surface save/restore (0 value mismatches, bit-exact
  replay), cross-rate/cross-layout restore (48 k stereo → 96 k mono and
  44.1 k stereo, bit-exact), and invalid/truncated state blobs (all 428
  truncation prefixes rejected; processor stays usable). The validator
  re-run: **47/47**. The module hash is byte-identical to the round-16
  delivery. No core gate was weakened; the four reference hashes still
  match (re-verified in the release, ASan and -O0 legs this round).
* The readiness verdict is unchanged and strengthened: **wrapper-ready,
  wrapped, and reproducibly re-buildable** (Linux-verified; Windows
  scripted-but-unexecuted). No GUI editor exists — none is faked.
