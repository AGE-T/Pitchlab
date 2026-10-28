# OCTAVOX native core — the VST3 wrapper (R16-P4b)

**Status: IMPLEMENTED, BUILT AND VALIDATED** (2026-10-28). A real, minimal
VST3 plug-in around the octavox-native 15.0.0 DSP core — not a placeholder:
the module builds as a proper `OctavoxNative.vst3` bundle, passes the VST3
SDK's own conformance validator (47/47), and passes an 8-case in-process
wrapper test suite including the **bit-exact sample-accurate-vs-block-boundary
equivalence proof**.

PROVENANCE: this wrapper is OUR OWN independent harmonizer implementation
around our own DSP core (`../include/octavox/dsp.hpp`). It is **not** a
reproduction of the original Eventide Octavox plug-in; no target code is
contained here and the original binary was never executed.

---

## 1. What is implemented

| Requirement (task book) | Implementation | Evidence |
|---|---|---|
| Plugin/component/controller lifecycle | `SingleComponentEffect` subclass (one object = IComponent + IAudioProcessor + IEditController; the SDK's supported minimal lifecycle for a non-distributable effect): initialize (buses + parameters + units) / terminate; factory registration with a fresh FUID ({6F4E6258-9A31-42E1-8F3C-5A971D2B4E60}) | wrapper test `lifecycle_smoke` (factory-created, interface-driven); validator category "Termination/Initialization" |
| Mono and stereo bus layouts | symmetric Main In/Out only: mono↔mono, stereo↔stereo (`setBusArrangements` accepts channels ∈ {1,2} with in==out count; asymmetric rejected); the engine is a `MultichannelShifter` (one core engine per channel = structural isolation) | validator bus categories; wrapper test bus negotiation |
| Host sample rate / block size preparation | `setupProcessing` validates the core's documented domain ((0, 1e6] Hz — the R13-b bound) before accepting; scratch preallocated at `maxSamplesPerBlock` + 64; `setActive(true)` prepares the engine (R13-c zero-alloc re-prepare fast path on re-activation) | validator "Process Format" (all rates 22050 Hz…384000 Hz + odd rates); wrapper tests |
| Global + per-voice parameter mapping | 38 parameters (ids 0–37): Bypass, Pitch Shift (±24 st, 1 cent), Voices (1..8), Spread, Feedback, Wet/Dry + per voice v∈0..7: Pitch Offset (±24 st), Gain (0..2), Mute, Base Delay (0..200 ms → samples at the current rate, ring-range-clamped); per-voice absolute ratio = global × 2^(offset/12) re-applied on pitch/spread changes (the core's override-clearing contract) | wrapper test `param_roundtrip`; `moduleinfo.json` |
| Sample-accurate parameter automation | `process()` reads `IParameterChanges`, splits the block into segments at the event sample offsets, applies the parameters at each segment start (the core's tested block-boundary semantics at finer granularity); multiple queues, multiple points per queue, block-end events applied for the next block; per-queue cursors, no allocation | wrapper test `segmentation_equivalence_BITEXACT`: **maxdiff = 0** vs the hand-driven core with the same setter calls at the same stream positions (the required equivalence proof); validator "Accuracy" categories |
| Host-safe parameter transfer / RT | parameters reach the audio thread ONLY through the host's lock-free queues; no shared mutable UI/audio state except the host-managed parameter objects; all core setters wait-free | design + `no_alloc_steady_state` (0 allocations across 100 process calls with events) |
| State save/restore | `IComponent::get/setState`: versioned binary blob `OVR1` v1 = full Config (17 fields) + bus channels + the 38-value live surface (fixed order); restore = prepare(Config at the host's current rate) + re-apply the surface in a canonical order + silent parameter-object sync; the controller-state methods alias the same blob; corrupt state rejected | wrapper test `state_roundtrip_BITEXACT`: live, same-instance and fresh-instance restores all **maxdiff = 0** vs the hand-driven core; validator persistence categories |
| Correct latency reporting | `getLatencySamples()` = the engine's `latencySamples()` (2304 at the default config; the dry path is latency-aligned) | wrapper test `latency_report` (2304 == 2304); validator latency category |
| Bypass behaviour | the Bypass parameter maps to the core's click-free, state-transparent `setBypass` (ramp over `bypass_seconds`); no host-side gating | wrapper test: bypass path bit-identical to the core (maxdiff = 0) |
| Silence / block-size changes / reset | zero input → finite bounded output (silence-exact by the core); n = 0 no-ops without touching buffers; varying host block sizes (1/7/64/512/3/1024) bit-identical to a one-shot run; larger-than-prepared blocks chunked through scratch (never allocated in process()); reset = the setActive(false)→(true) cycle (VST3 has no separate reset) — preserves the surface, replays equal a fresh engine configured with that surface (the cold-start law) | wrapper test `silence_blocks_reset` |
| No allocations / blocking locks in audio processing | nothing in `process()` allocates or locks (fixed cursor table, index arithmetic, preallocated scratch); gated by the project's global-new counter | `no_alloc_steady_state`: **0** allocations |

Additional wrapper-level behaviour: in-place host buffers (`in == out`) are
copied through preallocated per-channel scratch before processing (the core
requires non-aliasing) — bit-identical to separate buffers (`inplace_safety`,
maxdiff = 0). `setProcessing` is a pure suspend notification (engine state
keeps advancing by the core's transparency contract). Tail
`getTailSamples()` = a config-derived safe upper bound (the core's
ring-range law extent + latency; 15080 samples at the defaults — a host
hint, deliberately conservative). 64-bit processing is refused
(`canProcessSampleSize`), matching the float core.

## 2. Build

Requirements: cmake ≥ 3.19, a C++17 compiler (developed with GCC 14.2), the
VST3 SDK **v3.7.12_build_20** at `VST3_SDK_ROOT` (default
`../../../vst3sdk`, i.e. OUTSIDE the evidence tree — the SDK is NOT
redistributed with this package; Steinberg VST3 dual license applies to
anyone rebuilding).

```sh
export PATH=/home/z/.venv/bin:$PATH        # cmake 4.4.4 in this environment
cd native/vst3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4                    # builds octavoxnative (the module
                                           # bundle + moduleinfo.json via the
                                           # SDK packaging machinery),
                                           # octavox_vst3_wrapper_tests and
                                           # validator; the module build
                                           # automatically runs the validator
./build/bin/Release/octavox_vst3_wrapper_tests    # 8/8, exit 0
./build/validator/Release/validator \
    build/VST3/Release/OctavoxNative.vst3         # 47/47 (re-run manually)
```

SDK acquisition record (this environment, 2026-10-28 — the recursive clone
stalled, so the components were fetched individually at the exact pinned
gitlinks of the tag):

| component | repository | pinned SHA |
|---|---|---|
| main | steinbergmedia/vst3sdk @ tag `v3.7.12_build_20` | cc2adc90382dded9e347caf74e4532f1458715db |
| base | steinbergmedia/vst3_base | f0998e7b8424b32ba275cf4218aa56beef29821c |
| cmake | steinbergmedia/vst3_cmake | 2022b265cdbcd90b37294d3fb397f9766dbe4c06 |
| pluginterfaces | steinbergmedia/vst3_pluginterfaces | 151ecde4d6ee1c457dcce848342b162461944fe6 |
| public.sdk | steinbergmedia/vst3_public_sdk | 3fce096d6ee575479753f1aab23033d2e2ffdc6e |

(vstgui4/doc/tutorials are NOT needed: the build sets
`SMTG_ENABLE_VSTGUI_SUPPORT=OFF`, `SMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF`,
`SMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF` — no gtk/X11 dependency, no GUI.)

## 3. Files

```
octavox_ids.h                  fresh FUIDs + identity strings
octavox_params.h/.cpp          the 38-parameter surface: ids, domains,
                               exact normalized<->plain conversions (step-
                               quantized, reversible), the live Surface
                               model, the OVR1 state blob, the VST3
                               Parameter subclass
octavox_native_processor.h/.cpp the SingleComponentEffect: lifecycle, buses,
                               setup/activation, the segmentation process
                               loop, in-place scratch, state, latency/tail
factory.cpp                    the module factory (constexpr form)
octavox_vst3_tests.cpp         the in-process test suite (host mocks = the
                               SDK's own hosting utilities; references =
                               the hand-driven core)
CMakeLists.txt                 the build (SDK external-consumer pattern)
```

## 4. Design notes

* **Segmentation semantics.** The core applies parameter changes at
  `process()` call boundaries (its tested contract: events at stream
  positions, invariant to block splitting). The wrapper's segmentation
  delivers exactly that contract at the event's sample offset: setters are
  invoked between core `process()` calls at the segment boundary = the same
  stream position. The wrapper test proves bit-exactness against a
  hand-driven core issuing the same setter calls at the same positions, and
  separately demonstrates (diff > 0) that sample-accurate delivery genuinely
  differs from a host that only snaps changes to block boundaries.
* **Per-voice ratio composition.** `setVoiceRatio` takes an ABSOLUTE ratio
  and `setRatio`/`setSpread` clear the per-voice overrides (core contract),
  so the wrapper re-applies every non-zero voice offset after any global
  pitch/spread change (cheap, wait-free; clearing one override re-derives
  via the `setRatio`+re-apply dance). A voice with offset 0 stays on the
  core's spread table — spread behaviour is preserved.
* **Threading.** The audio thread reads parameters exclusively via the
  host's queues; `surface_` (the 38-value mirror) is written from
  `process()` (authoritative) and from `setState` (the host guarantees
  processing is paused during state exchange — the documented VST3
  contract, same reliance as the SDK's own samples).
* **State restore at a different rate.** The Config's sample-domain fields
  (grain, base delay) are restored verbatim; the engine re-prepares at the
  host's CURRENT rate (a full re-prepare = deterministic from stream
  position 0 — the documented contract; mid-stream resume is out of scope,
  as the round-15 readiness note already recorded).
* **Bus adaptation on restore.** A restored mono session adapts the bus
  arrangement to mono (while inactive) so the engine is prepared for the
  saved channel count; hosts re-query the layout after `setState`.
* **Why `vstsinglecomponenteffect.cpp` is compiled into both targets:** the
  SDK's `sdk` library does not include it (only the "again" sample compiles
  it into its own target); consumers of the base class must compile it.

## 5. Measured results (2026-10-28, this host: 2-core Xeon, GCC 14.2.0)

* wrapper tests: **8/8** (`r16_vst3_wrapper_tests.txt`): factory/lifecycle
  ok; conversions exactly idempotent; voices 1-vs-8 audibly differ (0.325);
  default run and bypass path bit-identical to the hand-driven core (0/0);
  **segmentation equivalence maxdiff = 0** (and ≠ coarse-host timing by
  0.509); state roundtrips 0/0/0 (428-byte blob); split-invariance 0;
  reset-vs-fresh-surface 0; in-place 0; **0 allocations** in 100 process
  calls with events; latency 2304 == 2304.
* validator: **47 tests passed, 0 failed** (`r16_vst3_validator.txt`) — the
  one iteration during development (46/1) was the "Process Format" test at
  1234567.8 Hz: the core's documented (0, 1e6] Hz domain; the wrapper now
  refuses out-of-domain setups in `setupProcessing` (the validator's
  tolerated path) instead of failing `setActive` later.
* module: `OctavoxNative.vst3/Contents/x86_64-linux/OctavoxNative.so`
  (375 936 B, SHA-256
  b293cecbcd42ce732ccd1789dea39ed82845a75b539084b0b79124ef559ea8ba at the
  time of writing) + `Contents/Resources/moduleinfo.json` (generated by the
  SDK's moduleinfotool; `r16_vst3_moduleinfo.json` copy).

## 6. Known limitations (honest)

* No GUI/editor (`IPlugView` not implemented — the task book asks for a
  minimal processor/controller wrapper; hosts automate the parameters).
* Symmetric mono/stereo buses only; sidechains/aux not applicable (no
  sidechain parameters exist in the core).
* Mid-stream state resume is not attempted (restore = deterministic from
  stream position 0 — the documented core contract).
* The 1-cent pitch step quantization means a host automation curve is
  sampled onto the cent grid (the wrapper's documented conversion; the
  gain/mix/feedback domains are continuous).
* State exchange relies on the VST3 contract that the host does not call
  `getState`/`setState` concurrently with `process()` (standard practice,
  same as the SDK's own samples).
