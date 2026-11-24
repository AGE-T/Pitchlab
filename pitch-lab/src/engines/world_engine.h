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
//     context + worker headroom (80 ms; ~0.217 s in total).
//   * The WORLD analysis allocates and is not realtime-safe, so the chunk
//     work runs on ONE PROCESS-WIDE POOL shared by every native.world
//     instance (poolSize() = hardware - 2, at most 8 threads in total; threads exist only while an
//     engine is attached). Chunks are computed concurrently and committed
//     strictly in order; inside a chunk the per-channel F0, the
//     (channel x 4 fixed frame parts) CheapTrick/D4C tasks and the
//     per-channel synthesis are fork-joined through the same pool (no
//     per-chunk thread creation).
//   * Stereo = DUAL MONO: each channel has its own F0 track (different L/R
//     pitches and anti-phase material are handled; identical channels give
//     identical outputs).
//   * REALTIME (ProcessContext::realtime): process()/finish() NEVER wait —
//     aligned output whose chunk is not finished is reported as
//     ProcessReport::starved and served by the adapter's counted fallback.
//     OFFLINE (the default): emission waits for the chunk (deterministic).
//   * A worker failure never aborts the process: it marks the job failed and
//     the next process()/finish() throws EngineException.
//   * Supported sample rates: exactly 44100/48000/88200/96000 Hz (validated). process() only copies
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
[[nodiscard]] int64_t worldLatencyFrames(double sampleRate, bool harvest = false);

/// The chunk length (frames) — the realtime adapter's job seam length.
[[nodiscard]] int64_t worldChunkFrames(double sampleRate);

namespace testing {
/// Make the computation of chunk `chunk` throw (-1 = off). Tests only.
void setWorldFaultInjectionChunk(int64_t chunk);
/// Delay every chunk computation by `ms` on the pool thread (0 = off). Tests only.
void setWorldChunkDelayMs(int ms);
/// Live threads of the shared WORLD pool (0 when no engine is attached).
int worldPoolLiveThreads();
/// The fixed pool size bound.
int worldPoolSize();
}  // namespace testing

}  // namespace pitchlab
