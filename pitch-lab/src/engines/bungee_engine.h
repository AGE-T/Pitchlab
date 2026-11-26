#pragma once

// Pitch Lab — native.bungee (2026-11, A/B candidate): the Bungee Basic
// phase-vocoder stretcher (MPL-2.0, vendored external/bungee) as a
// duration-preserving, time-aligned pitch shifter (stream_engine_base.h).

#include <cstdint>
#include <memory>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"

namespace pitchlab {

[[nodiscard]] EngineDescriptor bungeeEngineDescriptor();
[[nodiscard]] std::unique_ptr<PitchEngine> makeBungeeEngine();

/// The engine's declared latency bound at `sampleRate` for a granularity
/// choice (0 fine / 1 normal / 2 dense): the measured library latency
/// (impulse at pitch 1, cached per rate) plus a margin. Not for the audio
/// thread (the first call per rate measures).
[[nodiscard]] int64_t bungeeLatencyBoundFrames(double sampleRate, int granularity);

}  // namespace pitchlab
