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
