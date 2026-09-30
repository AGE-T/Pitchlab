# Pitch Lab — the VST3 plug-in (v0.1 product layer)

**Pitch Lab** is a real VST3 pitch-shift effect built on the completed v0.1
research engine system: the audio you hear is processed by the SAME five
engines the v0.1 research pipeline uses (`native.varispeed`,
`native.vardelay`, `native.pv.classic`, `native.pv.phaselocked`,
`native.granular` — the sealed v0.1 engine registry, in that order).
There is no second engine implementation, no stub, no fake quality score.

* Product-phase specification: `research/pitch-lab-vst3-product-phase-specification.md`
  (architecture, the realtime adaptation boundary, the parameter model, the
  recorded implementation-time corrections).
* Retained evidence: `results/vst3/v0.1/` (audio examples produced through
  the real VST3 process path, UI screenshots, the SDK validator run record).

---

## What the plug-in does

A stereo (or mono) pitch shifter: **PITCH ±12 st** with an optional LFO,
per-engine configuration, dry/wet, bypass and output level — with honest
runtime status (latency, live jobs, re-prepares, clamps, faults) and
input/output meters. Engine switching is live (a short crossfade), pitch
automation is sample-accurate through the host's parameter queue, and the
plug-in survives block-size changes, sample-rate changes, suspend/resume,
state save/load and reset per the VST3 contract (all covered by the test
suites + the official Steinberg VST3 SDK validator: 47/47).

## The engines and their realtime character

| UI entry | Engine | Realtime behaviour |
|---|---|---|
| VARDELAY | native.vardelay | Continuous realtime (the realtime-native variable-delay machine). Latency ≈ 45 ms. |
| PV-CLASSIC | native.pv.classic | Continuous realtime phase vocoder. Latency ≈ 55 ms. |
| PV-LOCKED | native.pv.phaselocked | Continuous realtime phase-locked vocoder. Latency ≈ 55 ms. |
| VARISPEED | native.varispeed | **Windowed-splice adaptation** (labelled amber in the UI): the true varispeed engine run in 0.2 s windows with 15 ms crossfades — the offline continuous render is impossible in sustained fixed-block realtime. Fixed worst-case latency ≈ 226 ms. |
| GRANULAR | native.granular | **Windowed-splice adaptation** (labelled amber): the same measured boundary as varispeed, on the input side (the read grid advances at the pitch ratio per output frame). Fixed worst-case latency ≈ 325 ms at the default grain. |

The ENGINE parameter's index follows the v0.1 **registry order** (§14-aligned,
`engine_registry.cpp`): 0 varispeed, 1 vardelay, 2 pv.classic, 3
pv.phaselocked, 4 granular — the engine identity ALWAYS comes from the
registry (`engineIdForIndex` / `reg.at`); the editor's per-engine control
panel is keyed to the same order. (A latent UI defect — the panel switch
keyed to the implementation-chronology order, showing the granular panel
for pv.classic and vice versa — was found and fixed by the CI `ui-binding`
checks; the audio path was never affected.)

The offline v0.1 pipeline (`pitchlab render`) remains the bit-exact research
reference; the plug-in is the realtime product. Realtime output is
deterministic for a fixed parameter trajectory, input and block schedule.

## Parameters

Musical surface (host-automatable): **PITCH** (±12 st), **LFO RATE**
(0.1–8 Hz), **LFO DEPTH** (0–2 st, 0 = off), **MIX** (dry/wet),
**OUTPUT LEVEL** (−24..+12 dB), **BYPASS**.

Engine configuration (settable, not automatable — changing them re-prepares
the chain with a crossfade): VARISPEED quality/aliasing, VARDELAY
excursion/crossfade, GRANULAR grain/overlap/jitter/window, PV-CLASSIC and
PV-LOCKED FFT size/hop. Non-selected engines keep their values (declared,
not fake: only the selected engine's parameters are applied).

The complete mapping between the VST3 parameter IDs and the v0.1 engine
configuration keys lives in ONE authoritative table: `src/vst/parameters.h`
(`parameterTable()`) — the processor, the UI and the tests are all generated
from it.

Integer-domain parameters (VARDELAY crossfade, GRANULAR overlap/jitter) are
declared DISCRETE (`stepCount` = the last legal integer) with exact
round-trips; display formatting and string parsing are tag-aware and
type-safe (`formatParamValue` / `parseParamPlain`): `"on"`/`"off"`,
`"hann"`/`"triangular"`, the resample-quality names and engine ids parse
back to their semantic values, and invalid strings are rejected (never
silently zero).

**UI parameter binding (the editor's write path).** Every editor control
interaction updates the parameter through BOTH responsibilities, in the
VST3-correct order: `setParamNormalized` (the controller's own value — the
single source of truth, which in this single-component design ALSO
publishes the snapshot to the realtime adapter and re-reports latency) and
then `performEdit` (the host notification). `performEdit` alone is NOT a
substitute: the SDK forwards it to `IComponentHandler` and never touches the
local value, so the 33 ms editor sync loop (`syncControlValues`) would write
the OLD controller value back into the control — and because the
engine-configuration parameters above are non-automatable (`kNoFlags`),
hosts do not echo their `performEdit` back, which made them impossible to
change from the UI (the Windows incident: sliders/selector reverted within
one poll tick). Slider interactions also never depend on the redraw dirty
flag: `valueFromMouse` always notifies, and the engine selector initialises
from the authoritative controller value when the editor opens. The
regression proof is the CI `ui-binding` capture: synthetic mouse events
through the real X11/VSTGUI dispatch drive the real controls while the sync
timer polls — every check (controller value, poll stability, engine
adoption, per-engine preservation) must pass.

## Runtime architecture (summary)

```
DAW → VST3 processor (src/vst/processor.cpp, single-component effect)
    → realtime adapter (src/vst/realtime_adapter.{h,cpp})
       · preserving engines: virtual jobs (10 s segments, envelope-bounded
         curve, crossfaded seams, async re-prepare)
       · varispeed + granular: windowed-splice jobs (0.2 s windows)
    → the UNMODIFIED v0.1 PitchEngine instances (src/engines/*)
```

Realtime safety: the `process()` path performs no allocation, no locks, no
I/O, and — since the Task 24 defect audit — no sleeping and no string
formatting either: the startup wait for the first chain lives on the MAIN
thread (`activate()` / the first parameter publish), the status snapshot is
numeric-only (strings are derived on the UI side), and the three-thread
publication protocol (parameter snapshot, meters, status; the chain
hand-off) is memory-model-clean (atomised seqlock + atomic chain/job
fields; TSAN-verified, three runs clean).

Automation is sample-accurate in ONE authoritative coordinate system: host
event offsets are ProcessData-block-relative; the processor converts each
event exactly once into its chunk's local frame (never clamped across chunk
boundaries; boundary-aware slicing reconstructs ramps across internal
chunks). LFO rate/depth, mix, level and bypass automation all reach the
realtime path — bypass resolves per sample (step semantics, multiple
toggles per block legal) with the 10 ms equal-power fade the specification
§4.1 mandates. Automation beyond the per-block capacity (256 points per
parameter) is consolidated (first 255 + the final event) and COUNTED in the
status (`automationDropped`) — never silent.

Latency: `getLatencySamples()` tracks the effective emission latency, and
latency CHANGES are reported to the host through
`IComponentHandler::restartComponent(kLatencyChanged)` from the main-thread
entry points (parameter changes, state restore) — the audio thread never
calls into the host. The in/out meter indicators are independent, with
per-SAMPLE time-based ballistics (identical values at any block schedule).

Suspend/resume (`setProcessing`) implements the specification's "resume =
reset + fresh chain": the reset is audio-thread-safe (an atomic flag; the
retirement/rebuild machinery performs it at the next block), so no
pre-suspend DSP state survives past the standard seam blend.

Known limitations (the honest list, including the effective-latency
monotonicity and the splice-adaptation seams) are in the specification §12.
Additional verified characteristics from the Task 24 audit: the v0.1
vardelay engine's §6.2.1 wrap startup produces ~E (excursion) frames of
leading silence for sustained ratios > 1 (the committed
`vardelay_harmstack_p5` example carries 838 ms — accepted v0.1 behaviour,
protected engines); the fresh chain after a resume/reset arrives
asynchronously (~ms — the preparation thread's rebuild).

## Realtime audio-path functional validation (the audio-path suite)

Wiring, processing and pitch transformation are three different claims.
The `vst_audio_path_test` doctest suite (CI-registered; independently
runnable via `ctest -R vst_audio_path_test`) answers them per engine with a
deterministic host simulation: fresh processor, parameters before
activation (the wet-deterministic startup), synthetic material (sustained
sine / harmonic two-tone / transient bursts / onset probe), a fixed block
schedule and a latency-covering zero flush, through the REAL
`IAudioProcessor::process` float32 path, snapshot publication, adapter
chains, the REAL v0.1 engines, and back out the output bus — measured with
the v0.1 analysis layer's own primitives (Hann-windowed exact-frequency
projection + a zero-crossing-median carrier cross-check).

**The measured answer (159 records, machine-readable artifact
`results/vst3/v0.1/audio-path/audio-path-report.json`, byte-deterministic
per binary — the CI regenerates and diffs it):**

| engine | wired | actually processing audio | pitch transformation |
|---|---|---|---|
| native.varispeed | YES | YES (0 faults, finite, non-silent, non-frozen, frame-exact; block matrix 64..4096 + mixed; rates 44.1..192 kHz; mono/stereo) | YES — identity exact, +12/-12 within 0.07 st (dual estimator) |
| native.vardelay | YES | YES (same matrices) | YES — identity exact, ±12 within 0.03 st |
| native.pv.classic | YES | YES (same matrices; FFT/hop latency signatures exact) | YES — exact at identity and ±12 |
| native.pv.phaselocked | YES | YES (same matrices; frequency EXACT) | YES — exact; output LEVEL is engine-inherent at non-identity ratios (see below) |
| native.granular | YES | YES (same matrices) | LIMITED — identity and +12 exact; **-12 renders at the envelope edge** (see below) |

Parameter effectiveness is proven through the ENGINE CONFIGURATION, not
controller state: vardelay crossfade 0/8192 → live-chain latency 96/8288;
PV FFT x hop (11 valid combos each) → latency = fft+hop+96 exactly; grain
0.02/0.5 s → latency changes; excursion, quality, jitter and (with an LFO
drive) overlap/window → measured waveform differences (maxdiff 0.05..1.03);
varispeed allow_aliasing off/on → the 16 kHz alias-band energy ratio 25.7x
(the anti-alias pre-filter's stopband, measured). Reset and engine
switching DURING audio: every destination engine measured while processing
(its own pitch response post-adoption, engine identity asserted per
segment), 0 faults, 5-segment cycles for all five engines.

**Two honest, evidence-backed characteristics (recorded, not hidden):**

* **pv.phaselocked output level at non-identity ratios** — the frequency is
  exact, but the amplitude of a single partial measures 0.12x..0.81x input
  across 44.1..192 kHz (0.24x at +12 st / 48 kHz). This is IN THE FROZEN
  v0.1 ENGINE, not the realtime layer: the direct engine (offline contract,
  constant-ratio 220 Hz sine, no adapter) measures the same 0.086 RMS at
  ratio 2.0, and the committed v0.1-era example
  `pv-locked_harmstack_p5_vst3.wav` carries the same characteristic
  (0.0814 vs ~0.124 for every other engine). The Laroche-Dolson region
  rotation attenuates single partials; richer material behaves differently.
  The suite's RMS floor is an ANTI-SILENCE floor (0.05x, -26 dB) by design.
* **native.granular downshift renders at the envelope edge** — for ratio <
  1 the granular engine's boundary-indexed curve reads (k·Hg, the OUTPUT
  grid, clamped into the input-indexed curve) run ahead of the adapter's
  live-curve-write frontier and see the prepare-time envelope prefill
  (envMax = live·2^(1/12)) instead of the commanded ratio: a -12 st
  realtime drive renders its carrier at +0.5 st (measured exactly 116.541
  Hz vs 110 commanded; the direct engine at the same constant ratio is
  exact at 109.995 Hz; identity and upshifts are exact because their
  boundary positions stay behind the frontier). An adapter-side fix (an
  ahead-write of the live ratio into the not-yet-reached curve region) was
  implemented and MEASURED to deadlock the engine's window/horizon
  consumption — the job never completes (fault storm) — and was REVERTED
  (the fix is riskier than the defect; the granular job lifecycle is ~600
  frames of window margin from that deadlock even in the shipped code).
  The deviation is bounded by the design's own ±1 st job envelope and is
  classified EXPECTED-LIMITATION in the artifact. A proper fix needs a
  dedicated task with the owner's eyes on it.

## Getting the plug-in (the distributable)

The Windows x64 product is published by CI on every push as the GitHub
Actions artefact **`PitchLab-VST3-Windows-x64`**:

1. Open the repository's **Actions** tab → the latest successful `ci` run.
2. Download the **`PitchLab-VST3-Windows-x64`** artefact (any signed-in
   GitHub user can; artefacts are retained for the GitHub default of 90
   days from the run).
3. Extract the zip → you get the real **`PitchLab.vst3`** bundle
   (`Contents/x86_64-win/PitchLab.vst3` is the Windows x64 PE module).
4. Copy `PitchLab.vst3` into `C:\Program Files\Common Files\VST3`, rescan
   your DAW's plug-in cache.

The artefact is produced by the `vst3-windows-x64-product` CI job: the
same sources built by the documented VS 2022 path, validated by the
official Steinberg SDK validator (47/47, asserted before packaging) plus
package-content checks (module present, PE x64, no build junk). The zip
is uploaded **only from a fully validated build** — the SHA-256 of the
package is printed in the run log (build spec §16). The `vst3-evidence`
artefact of the same run carries the Linux validation evidence; the
product artefact is separate and never mixed with evidence.

## Building

Sandbox reference (Linux x64):

```sh
cmake -S pitch-lab -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target PitchLab_vst3     # the .vst3 bundle
cmake --build build --target validator          # the SDK validator
build/bin/Release/validator build/VST3/Release/PitchLab.vst3
```

Dependencies: the vendored VST3 SDK (incl. VSTGUI) under
`external/vst3sdk` (ORIGIN.toml provenance; Steinberg VST3 proprietary
licence path — the documented product-phase exception), plus the system GUI
stack (X11, xcb-util, xkbcommon, cairo, pango, fontconfig, freetype) for
the editor. `PITCHLAB_BUILD_VST3=OFF` restores the exact v0.1 target
surface. The VST3 layer's Linux editor requirements can live in a user
prefix (`~/.local/xcb`, `~/.local/xkb` — set `PKG_CONFIG_PATH` accordingly
when reconfiguring).

Windows x64 (the primary target): Visual Studio 2022, CMake ≥ 3.21,
`cmake -S pitch-lab -B build -G "Visual Studio 17 2022" -A x64`, build the
`PitchLab_vst3` target. Install = copy the bundle into
`C:\Program Files\Common Files\VST3` (Linux: `~/.vst3`).

## Evidence tools

* `tools/vst_host_sim.cpp` — renders corpus material through the REAL VST3
  process path (module factory → full lifecycle → process with automation);
  produces the retained listening examples.
* `tools/vst_ui_screenshot.cpp` — an X11 host that loads the BUILT bundle
  (dlopen, the DAW path), performs the XEMBED handshake, opens the real
  VSTGUI editor, drives audio and captures the window (run under Xvfb).
