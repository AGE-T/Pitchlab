#pragma once

// Pitch Lab — native.signalsmith: the Signalsmith Stretch (MIT, vendored
// external/signalsmith-stretch 1.4.0 + external/signalsmith-linear 0.6.4)
// spectral pitch shifter wrapped in the v0.1 PitchEngine contract.
//
// Behaviour (frozen with this integration, 2026-11):
//   * Duration-preserving: output frame m is time-aligned to input frame m.
//     The library's own latency (inputLatency()+outputLatency()) is
//     discarded at the start; the DECLARED input latency is the preset's
//     closed-form upper bound (signalsmithLatencyBoundFrames), so the
//     renderer's zero padding always covers the real latency.
//   * Control rate: FixedBlock (kControlBlock frames) — the transpose
//     factor is read from the ratio curve at each sub-block start.
//   * SeededDeterministic: the library's random engine is seeded from the
//     EngineConfiguration seed; reset() re-creates the instance so a reset
//     engine is bit-identical to a fresh one (T-D3).
//   * All allocation in prepare()/reset(); process()/finish() do not
//     allocate (the library's per-call scratch resize stays within the
//     capacity reserved by configure()).

#include <cstdint>
#include <memory>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"

namespace pitchlab {

[[nodiscard]] EngineDescriptor signalsmithEngineDescriptor();

[[nodiscard]] std::unique_ptr<PitchEngine> makeSignalsmithEngine();

/// Upper bound of the library's algorithmic latency for a preset
/// (0 = "default": block 0.12 s / interval 0.03 s; 1 = "cheaper": block
/// 0.10 s / interval 0.04 s, split computation) at `sampleRate`. This is the
/// engine's declared input latency and the realtime adapter's geometry term.
[[nodiscard]] int64_t signalsmithLatencyBoundFrames(double sampleRate, int presetIndex);

}  // namespace pitchlab
