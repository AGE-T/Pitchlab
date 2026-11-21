#pragma once

// Pitch Lab — native.world: the WORLD vocoder (M. Morise; modified-BSD,
// vendored external/world v1.0.1) wrapped in the v0.1 PitchEngine contract.
//
// Behaviour (frozen with this integration, 2026-11):
//   * Analysis/resynthesis in CHUNKS of kChunkSeconds with kContextSeconds
//     of analysis context on both sides: F0 (DIO+StoneMask, or Harvest),
//     spectral envelope (CheapTrick), aperiodicity (D4C); the F0 contour is
//     multiplied by the ratio curve (voiced frames only), the envelope and
//     aperiodicity are optionally warped by the formant ratio, then WORLD
//     Synthesis renders the chunk. Successive chunks are joined with a
//     kCrossfadeSeconds complementary raised-cosine crossfade.
//   * Duration-preserving and time-aligned (output m <-> input m). Declared
//     input latency = worldLatencyFrames(fs) = chunk + crossfade + right
//     context + worker headroom (~0.175 s).
//   * The WORLD analysis allocates and is not realtime-safe, so the chunk
//     work runs on an engine-owned WORKER THREAD. process() only copies
//     samples into/out of preallocated rings and publishes progress through
//     atomics; it waits for the worker ONLY if a chunk whose output is due
//     is not finished yet (the headroom chunk makes this the exception).
//     The output CONTENT is a pure function of the input (deterministic).
//   * Mono analysis per channel (stereo = two independent analyses).

#include <cstdint>
#include <memory>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"

namespace pitchlab {

[[nodiscard]] EngineDescriptor worldEngineDescriptor();

[[nodiscard]] std::unique_ptr<PitchEngine> makeWorldEngine();

/// The engine's declared input latency at `sampleRate` (frames) — also the
/// realtime adapter's chain-geometry latency term.
[[nodiscard]] int64_t worldLatencyFrames(double sampleRate);

/// The chunk length (frames) — the realtime adapter's job seam length.
[[nodiscard]] int64_t worldChunkFrames(double sampleRate);

}  // namespace pitchlab
