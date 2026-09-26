#pragma once

// Pitch Lab — core audio/frame primitives (implementation specification §4.1).
//
// CONTRACT (v0.1, frozen 2026-09-25; implemented 2026-09-26 — implementation
// cycle 1, spec §17 step 1):
//   * One frame = one sample index across ALL channels. All durations and
//     latencies elsewhere in the system are counted in frames.
//   * Audio is planar, channel-major, non-interleaved, in-memory `double`
//     (Amendment A-1: double bus; owner L-4: float64 compute masters).
//   * Views are NON-OWNING and never outlive the process() call that received
//     them (spec §5 ownership table: the renderer owns all buffers).
//   * Supported sample rates (harness-wide, spec §8): 44100, 48000, 88200,
//     96000, 176400, 192000 — carried per job; no component assumes 44.1/48.
//     The primitive types themselves carry no rate knowledge; the value is a
//     plain number wherever computed (double) and int32/uint32 only in
//     serialised forms (WAV headers, manifests).
//   * ChannelLayout is the v0.1 corpus/harness surface; engines declare the
//     richer ChannelMode set (engine registry, architecture §D.3).

#include <cstdint>

namespace pitchlab {

using FrameCount = int64_t;    // one frame = one sample index across all channels
using ChannelCount = int32_t;
using SampleRate = double;

// Planar audio block view (Amendment A-1: double bus). NON-OWNING.
struct AudioBlockView {
  const double* const* channels = nullptr;  // channel-major, non-interleaved; channels[c][f]
  ChannelCount channelCount = 0;
  FrameCount frameCount = 0;                // frames in THIS block
};

// Mutable planar output region. NON-OWNING.
struct AudioBlockOut {
  double* const* channels = nullptr;
  ChannelCount channelCount = 0;
  FrameCount frameCapacity = 0;             // capacity; produced frames may be fewer
};

enum class ChannelLayout { Mono, Stereo };  // v0.1 corpus/harness surface
// (ChannelMode in capabilities remains the architecture's richer declaration set)

}  // namespace pitchlab
