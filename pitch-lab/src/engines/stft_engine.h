#pragma once

// Pitch Lab — native.stft (2026-11, A/B candidate): the stftPitchShift core
// (MIT, vendored external/stftpitchshift) in a streaming STFT, with optional
// cepstral formant preservation (stream_engine_base.h; latency = frame N).

#include <cstdint>
#include <memory>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"

namespace pitchlab {

[[nodiscard]] EngineDescriptor stftEngineDescriptor();
[[nodiscard]] std::unique_ptr<PitchEngine> makeStftEngine();

/// The engine's exact latency for a frame size (1024/2048/4096): N frames.
[[nodiscard]] int64_t stftLatencyFrames(int frameSize);

}  // namespace pitchlab
