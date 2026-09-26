// T-T1 — core primitives (implementation specification §4.1), unit level.
//
// The §4.1 contract is mostly type shape and view semantics; the tests pin:
//   * exact alias types (frames int64, channels int32, rate double);
//   * AudioBlockView / AudioBlockOut are non-owning views (address identity,
//     not copies) with channel-major indexing channels[c][f];
//   * ChannelLayout v0.1 surface values;
//   * one frame counts across all channels (documented semantics).
//
// Note: these are contract-shape tests (freeze the ABI-level contract);
// numeric DSP behaviour lives in the resampler/WAV/RNG tests.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <type_traits>

#include "core/types.h"

TEST_CASE("T-T1a: §4.1 alias types are exactly as specified") {
  static_assert(std::is_same_v<pitchlab::FrameCount, int64_t>);
  static_assert(std::is_same_v<pitchlab::ChannelCount, int32_t>);
  static_assert(std::is_same_v<pitchlab::SampleRate, double>);
  CHECK(true);  // the static_asserts above are the actual test (compile-time)
}

TEST_CASE("T-T1b: AudioBlockView is a non-owning channel-major view") {
  double ch0[4] = {1.0, 2.0, 3.0, 4.0};
  double ch1[4] = {5.0, 6.0, 7.0, 8.0};
  const double* chans[2] = {ch0, ch1};

  pitchlab::AudioBlockView view;
  view.channels = chans;
  view.channelCount = 2;
  view.frameCount = 4;

  REQUIRE(view.channelCount == 2);
  REQUIRE(view.frameCount == 4);
  // Address identity: the view points at the caller's buffers (no copy).
  CHECK(view.channels[0] == ch0);
  CHECK(view.channels[1] == ch1);
  // channels[c][f] indexing (channel-major, non-interleaved).
  CHECK(view.channels[0][2] == 3.0);
  CHECK(view.channels[1][0] == 5.0);
}

TEST_CASE("T-T1c: AudioBlockOut is a non-owning mutable view") {
  double ch0[2] = {0.0, 0.0};
  double* chans[1] = {ch0};

  pitchlab::AudioBlockOut out;
  out.channels = chans;
  out.channelCount = 1;
  out.frameCapacity = 2;

  REQUIRE(out.frameCapacity == 2);
  out.channels[0][1] = -1.5;  // writable through the view
  CHECK(ch0[1] == -1.5);
}

TEST_CASE("T-T1d: ChannelLayout v0.1 surface") {
  pitchlab::ChannelLayout mono = pitchlab::ChannelLayout::Mono;
  pitchlab::ChannelLayout stereo = pitchlab::ChannelLayout::Stereo;
  CHECK(mono != stereo);
  CHECK(mono == pitchlab::ChannelLayout::Mono);
}
