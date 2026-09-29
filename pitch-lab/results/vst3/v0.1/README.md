# Pitch Lab VST3 — v0.1 product evidence

Retained, reproducible evidence that the VST3 plug-in product layer works
end-to-end: real audio through the real VST3 process path, the real UI
rendered, and the official SDK validation record. Produced by the product
tools from the committed repository state (the same-binary determinism
class of the v0.1 result product).

## contents

* `examples/` — 11 listening examples (f32 WAV): representative corpus
  material (the 220 Hz harmonic stack, the vibrato saw, the percussive set)
  rendered through the REAL VST3 process path — the module factory
  (`GetPluginFactory`), full host lifecycle (initialize → buses → setup →
  active → processing → process with block automation → terminate), the
  actual `PitchLabProcessor` + realtime adapter + v0.1 engines. NOT the
  offline renderer.
* `ui/` — 4 screenshots of the real VSTGUI editor (680×450 logical): the
  bundle loaded the DAW way (VST3::Hosting::Module, dlopen + XEMBED
  handshake under Xvfb), with live meters and status after driving real
  audio through the processor.
* `validator-run.txt` — the official Steinberg VST3 SDK validator against
  the built bundle: **47/47 tests passed, 0 failed**.

## reproduction

```sh
# from the repository root (pinned CMake 3.31.6 + the GUI deps)
cmake -S pitch-lab -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
build/bin/Release/validator build/VST3/Release/PitchLab.vst3 \
  > pitch-lab/results/vst3/v0.1/validator-run.txt
(cd pitch-lab && ../build/bin/Release/vst_host_sim)
Xvfb :99 & DISPLAY=:99 pitch-lab/build/bin/Release/vst_ui_screenshot \
  build-vst/VST3/Release/PitchLab.vst3 1 5.0 0.0 /tmp/ui.ppm
```

## what the examples demonstrate

Each example's status (printed by the driver) is fault-free with the chain
ready; the rendered audio carries the engine's actual pitch behaviour
(spot-verified: the +5 st harmonic-stack examples peak at ≈293–296 Hz vs
the analytic 293.66 Hz; the −5 st vibrato example tracks the modulation):

* **vardelay / pv.classic / pv.phaselocked / varispeed / granular @ +5 st**
  on the harmonic stack — the five engines' realtime characters on steady
  pitched material (the splice engines' window cadence is audible as the
  classic splice texture; the preserving engines are continuous).
* **vardelay / granular / pv.phaselocked @ −5 st + 5 Hz LFO (0.5 st)** on
  the vibrato saw — runtime pitch-curve control on modulated input.
* **vardelay / granular / varispeed @ +7 st** on the percussive set —
  transient behaviour through each family.

## honest scope

* This is product evidence, NOT a verification gate (spec §13): the v0.1
  closed evidence (§17.6 PASS, `results/v0.1/`) is untouched.
* Realtime output is deterministic per (parameter trajectory, input, block
  schedule) with a constant chain set; it is NOT bit-identical to the
  offline renderer for the same curve (a different contract — spec §3/§12).
* The UI screenshots show the real editor with real live values (latency,
  jobs, faults, envelope) — no mock data, no quality score.
