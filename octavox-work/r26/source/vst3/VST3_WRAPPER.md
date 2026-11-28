# OCTAVOX native core — the VST3 wrapper (R16-P4b, extended R17-P3, R18-P6, R20-P0)

**Status: IMPLEMENTED, BUILT AND VALIDATED** (2026-10-28; re-validated and
extended 2026-10-29 — round 17; **round 20: the default-parameter
initialisation contract fixed and gated — 20-case suite**). A real, minimal
VST3 plug-in around the octavox-native 16.x DSP core — not a placeholder:
the module builds as a proper `OctavoxRE_<Variant>.vst3` bundle, passes the
VST3 SDK's own conformance validator (47/47), and passes a **20-case**
in-process wrapper test suite (8 cases round 16 + 7 parameter/state cases
round 17 + **5 default-initialisation cases round 20**) including the
**bit-exact sample-accurate-vs-block-boundary equivalence proof**, the full
38-parameter contract audit, and the R20-P0 default/state-sync gates
(fresh instance, after prepare, save/restore, state-to-new-instance via
BOTH `setState` and `setComponentState`, repeated initialize/terminate
cycles — all without moving a control; see §4a and
`docs/OCTAVOX_ROUND20_FINDINGS.md`).

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

**R17-P2: the SDK is acquired and verified by a script** — see
`../tools/get_vst3_sdk.sh` (POSIX) / `../tools/get_vst3_sdk.ps1` (Windows)
and `../tools/README.md`. The scripts fetch exactly the pinned revision set
below, verify every fetched revision (`git rev-parse`), cross-check the
tag's gitlinks, refuse to write inside the evidence tree, and fail clearly
on missing git/network/tar. The full build+test chain (core suite + hashes
+ bench + ASan leg + wrapper + validator) is scripted in
`../tools/build_and_test.sh` (Linux, executed end-to-end this round) and
`../tools/build_and_test.ps1` (Windows/MSVC, **authored but NOT executed —
no Windows runtime in this environment; no Windows binary has been built
or tested**).

```sh
export PATH=/home/z/.venv/bin:$PATH        # cmake 4.4.4 in this environment
cd native/tools
./get_vst3_sdk.sh                           # pinned-SHA SDK acquisition + verification
cd ../vst3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4                    # builds octavoxnative (the module
                                           # bundle + moduleinfo.json via the
                                           # SDK packaging machinery),
                                           # octavox_vst3_wrapper_tests and
                                           # validator; the module build
                                           # automatically runs the validator
./build/bin/Release/octavox_vst3_wrapper_tests    # 15/15, exit 0
./build/bin/Release/validator \
    build/VST3/Release/OctavoxNative.vst3         # 47/47 (re-run manually)
```

SDK acquisition record (this environment, 2026-10-28 — the recursive clone
stalled, so the components were fetched individually at the exact pinned
gitlinks of the tag; **2026-10-29: the pinned set was re-verified against a
fresh fetch and the working tree content-matches all four components** —
`evidence/analysis_round17/`, get_vst3_sdk.sh --verify):

| component | repository | pinned SHA |
|---|---|---|
| main | steinbergmedia/vst3sdk @ tag `v3.7.12_build_20` | cc2adc90382dded9e347caf74e4532f1458715db |
| base | steinbergmedia/vst3_base | f0998e7b8424b32ba275cf4218aa56beef29821c |
| cmake | steinbergmedia/vst3_cmake | 2022b265cdbcd90b37294d3fb397f9766dbe4c06 |
| pluginterfaces | steinbergmedia/vst3_pluginterfaces | 151ecde4d6ee1c457dcce848342b162461944fe6 |
| public.sdk | steinbergmedia/vst3_public_sdk | 3fce096d6ee575479753f1aab23033d2e2ffdc6e |

(vstgui4/doc/tutorials are NOT needed: the build sets
`SMTG_ENABLE_VSTGUI_SUPPORT=OFF`, `SMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF`,
`SMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF` — no gtk/X11 dependency. Since R33
the wrapper HAS an editor — but it is a self-contained software-rendered
view (`octavox_minimal_view.*` + `octavox_ui_renderer.*`), NOT VSTGUI: the
only OS dependency added is core Xlib on Linux / user32+gdi32 on Windows.)

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
                               the hand-driven core; 20 cases, see §5)
CMakeLists.txt                 the build (SDK external-consumer pattern)
../tools/get_vst3_sdk.{sh,ps1} R17-P2: pinned-SHA SDK acquisition + verification
../tools/build_and_test.{sh,ps1} R17-P2: the full build+validation chains
```

### 3a. The 38-parameter contract (R17-P3 audit)

The complete machine-readable table (id, title, short title, units, min,
max, step, stepCount, default plain, default normalized, flags, unit) is
dumped by `octavox_vst3_wrapper_tests --dump-params`; the round-17 capture
is `../../evidence/analysis_round17/r17_param_metadata_table.txt` and the
automated audit is the `param_metadata_table` test case (ID uniqueness via
a 38-bit mask, spec-table integrity, processor<->controller
`ParameterInfo` agreement for all 38 ids, boundary/middle conversion
roundtrips — exact on step grids, ≤ 4 ulp continuous). Summary:

| ids | parameter | domain | step | default | notes |
|---|---|---|---|---|---|
| 0 | Bypass | [0,1] bool | 1 | 0 | `kIsBypass|kCanAutomate`, root unit |
| 1 | Pitch Shift | ±24 semitones | 0.01 (4800 steps) | 0 | the 1-cent grid |
| 2 | Voices | 1..8 | 1 (7 steps) | 1 | core default |
| 3 | Spread | [0,1] bool | 1 | 1 | core default on |
| 4 | Feedback | [0, 0.9] | continuous | 0 | subset of the core's clamped (-1,1) |
| 5 | Wet/Dry | [0,1] | continuous | 1 | 1 = bit-neutral all-wet |
| 6+4v | Voice v+1 Pitch Offset | ±24 semitones | 0.01 | 0 | per voice |
| 7+4v | Voice v+1 Gain | [0,2] | continuous | 1 | linear amplitude |
| 8+4v | Voice v+1 Mute | [0,1] bool | 1 | 0 | per voice |
| 9+4v | Voice v+1 Base Delay | [0,200] ms | continuous | (256+32v)/48 ms | ms-domain (see below) |

Conversion laws (`octavox_params.cpp`): normalized→plain clamps [0,1]
(NaN→min), plain = min + round-half-up(norm·span/step)·step on grids
(exactly reversible at grid points), min + norm·span continuous;
plain→normalized clamps into the domain and grid-snaps. The base-delay
parameter converts ms→samples at the CURRENT rate, clamped into the core's
ring-range law (`d + 3·grain + 8 < 2^18`, one sample of margin).

**Cross-rate semantics (verified by `state_cross_rate_layout`)**: the
per-voice base-delay DEFAULTS are milliseconds in the parameter surface
(the spec-table defaults (256+32v)/48 ms) and therefore scale with the host
rate on restore — at 48 kHz they coincide exactly with the core's
sample-domain default table 256+32v; at other rates the ms-domain
interpretation is the documented wrapper behaviour (a "6 ms" delay is 6 ms
at any rate).

### 3b. The OVR1 v1 state blob layout (428 bytes, self-verified in tests)

```
0x000  u32 magic 'OVR1' (0x3152564F)     0x120  i32 channels (bus count at save)
0x004  i32 version = 1                   0x124  38 × f64 plain values, fixed
0x008  Config (17 fields, 112 bytes):         order = parameter id order
       sample_rate@8, ratio@16, voices@24,     (bypass@124, pitch@132,
       max_voices@28, spread@32, feedback@36,  voices@140, spread@148,
       interpolation@44, grain@48,             feedback@156, wetdry@164,
       base_delay@56, fmin@64, fmax@72,        then per voice: offset, gain,
       s_hysteresis@80, f0_history@88,         mute, base-ms ×8)
       detection_interval@92, engagement@96,
       gain_ramp@104, bypass_seconds@112
```
Reading rejects: short/truncated input (EVERY prefix length is tested),
wrong magic, unsupported version, non-finite/unfixable config fields
(NaN sample_rate/grain, ring-law violations); fixable fields are
sanitised per the documented defensive-clamp policy (e.g. a non-finite
ratio becomes 1.0 — tested). Restore re-prepares the engine at the host's
CURRENT rate/layout and re-applies the full surface in canonical order.

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

## 4a. R20-P0 — the default-parameter initialisation contract

The round-20 audit found the wrapper's fresh-instance parameter CURRENT
values were **0-based** (the SDK `Parameter()` default ctor initialises
`valueNormalized {0.}`, not the declared default — `vstparameters.cpp:48`,
SDK 3.7.12): the Generic Editor showed Pitch −24.00, Wet/Dry 0.00, Spread
off, voice gains 0.0, voice offsets −24.00, voice delays 0 ms while the
engine ran the true `defaultSurface()` — the user-reported "wrong until the
control is moved". Three coordinated fixes (all in-round, gated by the new
test group):

1. `OctavoxParameter`'s ctor ends with `setNormalized(info.defaultNormalizedValue)`
   (no listeners exist yet — pure value initialisation). All 38 parameters
   now REPORT the declared default as their CURRENT value on registration.
2. `OctavoxNativeProcessor::initialize()` resets `surface_`/`cfg_`/processSetup
   to the documented defaults — fresh-instance semantics for a host's
   terminate→initialize cycle (previously the old surface survived as member
   state: parameters would have shown defaults while the engine ran the
   previous surface).
3. `IEditController::setComponentState` is implemented (the SDK base returns
   `kNotImplemented`): for a single-component effect the controller part IS
   the component, so it aliases the authoritative `setState` (same 'OVR1'
   blob, same silent parameter sync; idempotent if a host calls both paths).

Gates: wrapper cases 16–20 (the S1–S6 scenarios, no control ever moved),
including the engine-truth proof (the DSP bit-exactly runs the default
surface with NO events ever delivered) and the automation-flush proof (a
host sending all 38 CURRENT values through the queues leaves the engine at
the defaults — the conversion chain is exactly idempotent for the default
grid at 48 kHz). Sensitivity proof: with fix 1 disabled, exactly the three
default-checking cases fail with the reported symptom.

## 5. Measured results (2026-10-28 R16; re-measured 2026-10-29 R17, this host: 2-core Xeon, GCC 14.2.0)

* wrapper tests: **15/15** (`r17_vst3_wrapper_tests.txt`): the R16 eight
  (factory/lifecycle ok; conversions exactly idempotent; voices 1-vs-8
  audibly differ 0.325; default run and bypass path bit-identical to the
  hand-driven core 0/0; **segmentation equivalence maxdiff = 0** (and ≠
  coarse-host timing by 0.509); state roundtrips 0/0/0 (428-byte blob);
  split-invariance 0; reset-vs-fresh-surface 0; in-place 0; **0
  allocations** in 100 process calls with events; latency 2304 == 2304)
  plus the R17 seven: **param_metadata_table** (38-id mask, spec
  integrity, ParameterInfo agreement 0 mismatches, boundary roundtrips —
  0 exact failures, 0 ulp failures), **events_block_positions**
  (start/middle/last/block-end events bit-identical, maxdiff = 0, and the
  dropped-block-end control differs 0.059),
  **simultaneous_multivoice_events** (5 queues at one sample position,
  bit-identical, maxdiff = 0), **bypass_wetdry_clickless_noalloc**
  (automation_max_step 0.4980181 ≤ bound 0.5073929; 0 allocations across
  the 120-block automation run), **state_full_surface_restore** (all 38
  parameters non-default: 0 restored-value mismatches, live and
  fresh-restore replays maxdiff = 0), **state_cross_rate_layout**
  (48 kHz stereo → 96 kHz mono and 44.1 kHz stereo replays maxdiff = 0
  vs the hand-driven core at the new rate/layout),
  **state_invalid_blobs** (all 428 truncation prefixes rejected; wrong
  magic/version, NaN sample_rate/grain, ring-law violation rejected; NaN
  ratio accepted-and-sanitised per the documented policy; processor stays
  usable: post-rejection valid restore replay maxdiff = 0).
* validator: **47 tests passed, 0 failed** (`r17_vst3_validator.txt`) — the
  one iteration during development (46/1) was the "Process Format" test at
  1234567.8 Hz: the core's documented (0, 1e6] Hz domain; the wrapper now
  refuses out-of-domain setups in `setupProcessing` (the validator's
  tolerated path) instead of failing `setActive` later.
* module: `OctavoxNative.vst3/Contents/x86_64-linux/OctavoxNative.so`
  (375 936 B, SHA-256
  b293cecbcd42ce732ccd1789dea39ed82845a75b539084b0b79124ef559ea8ba —
  **byte-identical across the R16 delivery, the post-reset R17 rebuild and
  the R17 CMake-guard rebuild**; `r16_vst3_moduleinfo.json` copy +
  `r17_param_metadata_table.txt`).
* the full chain also runs via `../tools/build_and_test.sh`: release leg
  74 s (85/85 + 4/4 hashes + bench), ASan leg 376 s (85/85), wrapper leg
  15/15 + validator 47/47 — `RESULT: ALL GATES PASSED (total wall 454 s)`
  (`r17_build_script_run.txt`).

## 6. Known limitations (honest)

* ~~No GUI/editor~~ — **R33: the minimal editor exists** (see §8). It is a
  fixed-size 784x416 software-rendered `IPlugView` served by
  `createView("Editor")`: all 38 parameters reachable, the R31 vendor
  telemetry displayed live. It is deliberately minimal (no VSTGUI, no font
  stack, no resize, no keyboard handling) — **macOS is not implemented**
  (no hardware; `isPlatformTypeSupported` refuses NSView), and on Linux a
  host without the VST 3.6.8+ `IRunLoop` contract cannot attach it. The
  parameter surface, the state blob and the audio path are untouched.
* **No Windows build has been produced or tested.** The Windows/MSVC path
  exists as reviewed scripts (`../tools/get_vst3_sdk.ps1`,
  `../tools/build_and_test.ps1`) and MSVC portability guards in the
  CMakeLists/harnesses, but this environment has no Windows runtime; the
  Linux chain is the only executed verification.
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
* Cross-compiler caveat: the four reference hashes are the GCC 14.2
  record; MSVC floating-point codegen may legitimately differ, so a hash
  mismatch on Windows is reported, not treated as proof of a defect (the
  85-case suite carries the correctness gates).


---

## 7. Round-18 addendum — the compiled-module integration harness (R18-P6)

A new harness, `octavox_module_integration_tests.cpp` (CMake target
`octavox_module_integration_tests`, built after `octavoxnative`), tests
the **actual compiled `OctavoxNative.vst3` bundle** — not the internal
processor class. It loads the bundle through the pinned SDK's
`VST3::Hosting::Module::create` (a real `dlopen` of
`Contents/x86_64-linux/OctavoxNative.so` + `GetPluginFactory()`), and
drives the exported factory's instances through the standard host
interfaces using the SDK's own hosting utilities
(`HostApplication`, `ParameterChanges`, `MemoryStream`; on Linux the
SDK's `module_linux.cpp` supplies `Module::create` and is compiled into
the harness).

The 12 cases (results: `evidence/analysis_round18/r18_module_integration.json`,
12/12 on 2026-10-30):

1. `module_load_factory` — module load, factory discovery, class-info
   enumeration (1 class, our CID), category sanity;
2. `processor_creation` — `createInstance<IComponent>` + the
   IAudioProcessor/IEditController queries (single-component);
3. `component_initialize_buses` — `initialize(HostApplication)` (a real
   host context), 1+1 audio buses, the 38-parameter surface published;
4. `bus_negotiation_mono` / 5. `bus_negotiation_stereo` — the
   `setBusArrangements` negotiation reflected by `getBusArrangement`;
   `canProcessSampleSize(kSample32)`;
6. `activation_lifecycle` — setup → activate → processing on/off →
   re-activation (the host latency-change cycle);
7. `process_mono_bitexact` / 8. `process_stereo_bitexact` — deterministic
   LCG streams (odd block sizes for stereo) processed by the module vs
   the **hand-driven core reference: bit-identical** (max abs diff 0);
9. `param_queue_events` — a full non-default surface applied through real
   `IParameterChanges` queues = bit-identical to the reference
   (canonical application order) + a mid-block event drop-control
   (measured effect 0.205 — the queue is genuinely consumed);
10. `state_save_restore_second_instance` — `getState` (428-byte blob) →
    a **second factory-created instance** → `setState` → bit-identical
    replay;
11. `latency_reporting` — `getLatencySamples()` == the core's exact
    `latencySamples()` (2304 @ 48 kHz default config); tail 15080;
12. `terminate_unload` — `terminate()` + full release + Module
    destruction (library unload) — clean.

**Scope label (stated here, in the harness header and its stdout):** this
is an SDK-module-load + interface-contract test on ONE host
implementation (the SDK's own utilities on Linux). **It is not a DAW
compatibility claim** — no DAW was involved, and passing here does not
certify behaviour in any specific DAW. The SDK validator run (47/47)
remains the complementary check. (R33: the 17th case `minimal_editor_surface`
exercises the editor through the loaded module — createView + a real X11
attach + a painted-pixel probe; the deep logic proof lives in
`octavox_ui_tests`.)

R18 re-verification on the fresh build (SDK at its archived location
`octavox-work/vst3sdk`): wrapper tests 15/15, validator 47/47,
integration harness 12/12, and **the module's SHA-256 is byte-identical
to the R16/R17 record** (`b293cecbcd42…f559ea8ba`) — deterministic
compilation re-proven from the moved SDK tree.

## 8. The minimal editor (R33)

**What it is.** A fixed-size 784x416 `IPlugView` served by
`OctavoxNativeProcessor::createView("Editor")` (any other view name returns
nullptr). Three strictly separated layers:

1. **`MinimalUiModel`** (`octavox_minimal_view.h`) — platform-free logic:
   the layout, the cached 38 plain parameter values, the cached per-voice
   R31 telemetry frames, the mouse gesture state machine, and the
   full-frame draw through the software renderer. Directly unit-testable
   without any window system (and tested: `octavox_ui_tests`, 11 cases).
2. **The pixel buffer** — one `uint32_t` per pixel, `0x00RRGGBB`, which is
   simultaneously the X11 ZPixmap depth-24 LSBFirst layout AND the Win32
   top-down 32-bpp `BI_RGB` DIB layout: both platform layers blit the SAME
   bytes with no per-pixel conversion (pinned by the pixel-identity test:
   `XGetImage` of the attached window is memcmp-identical to the buffer).
3. **The platform layers** (inside `octavox_minimal_view.cpp`):
   * **Linux** — an X11 child window created inside the host-provided
     `X11EmbedWindowID` parent. The view runs on the **host's UI thread**
     through the VST 3.6.8+ Linux hosting contract: `IRunLoop` (queried
     from the `IPlugFrame`) drives `IEventHandler` (the X11 connection fd)
     and `ITimerHandler` (~30 Hz). **No private thread, no cross-thread
     parameter access.** A host without `IRunLoop` gets
     `attached() == kResultFalse` (documented requirement; every current
     Linux DAW provides it).
   * **Windows** — a `WS_CHILD` window created on the attaching thread;
     `WM_TIMER` (~30 Hz) ticks + `WM_PAINT` blits the DIB; mouse messages
     drive the model. All on the host thread. (Cross-compiled; verified at
     the machine-code level — see the R33 evidence; the identical
     model/renderer logic is executed end-to-end on Linux.)
   * **macOS** — NOT implemented (no hardware in the project);
     `isPlatformTypeSupported("NSView")` returns false, honestly.

**Why no VSTGUI.** The pinned SDK's `vstgui4` submodule is empty in this
repository and the build always compiled with VSTGUI off (no gtk/X11 font
stack). A minimal editor depending on nothing but Xlib/GDI keeps the module
self-contained, keeps every test binary linkable, and — decisively — makes
each rendered frame **bit-reproducible**: the golden-render test pins the
exact pixels (FNV-1a `0x0f80c7f43a8358ad` at the default surface).

**The editor surface.** All 38 host parameters are reachable: global
pitch slider / voices stepper / harmony-map toggle / feedback + mix sliders
in the top panel; per-voice transpose fields (relative drags), gain bars,
delay fields and mute buttons in the voice panel; the header bypass LED is
clickable. Every gesture maps through the wrapper's own
`normalizedFromPlain`/`plainFromNormalized` — UI edits land on the exact
quantized grid the host surface defines. Double-click any control = its
documented default. The edit protocol is `beginEdit` →
`setParamNormalized` + `performEdit` (per change) → `endEdit` through the
public `EditController` API — verified by the mock-handler protocol test.

**The telemetry panel** renders the R31 vendor interface live: per-voice
Hz (derived from id 7's cents-from-C4; the `-65535` sentinel and the
zeroed parked/never-polled cells both display `--`), the raw cents, the
id-11 confidence bar, the id-8 glide ratio, and a per-voice activity LED
driven by `consumeDirtyBits` — the same contract the module tests pin.

**Text rendering.** An embedded 5x7 bitmap font (the classic public-domain
terminal glyph set, retyped; `octavox_ui_font.h`) — no fontconfig, no GDI
font selection, ASCII-only strings, C-locale `snprintf` formatting:
deterministic on every platform.

**Testing** (`octavox_ui_tests`, 11 cases + the 17th module-integration
case): font table sanity; renderer primitives + clipping; hit-map coverage
(38/38 controls); slider drag protocol + exact quantization; relative
value drags + domain clamps; toggles/steppers; double-click defaults;
telemetry display values (the 220 Hz → −300.009 cents pin; the sentinel);
the golden full-frame hash; view lifecycle contract; and — under a real X
server (Xvfb) — the **pixel-identity proof**: attach into a real parent
window with a host-side `IRunLoop` implementation, pump it, and verify the
SERVER-SIDE pixels are bit-identical to the render buffer, that live audio
changes the telemetry pixels, and that `removed()` destroys the window.
The module-level case drives the same flow through the dlopen'ed bundle.

**Honest limits.** No wheel/keyboard handling (`onWheel`/`onKeyDown`/
`onKeyUp` return kResultFalse); no DPI scaling (fixed 784x416); no macOS;
no PE execution in this sandbox (the Windows layer is machine-code
verified only); the editor does not exercise anything the validator tests
(47/47 unchanged).
