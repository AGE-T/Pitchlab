// Pitch Lab VST3 product layer — T-VP1: parameter model suite.
// The ONE model table's integrity: registry mapping, normalisation round
// trips, defaults, the automation-surface flags (spec §6/§9).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "vst/parameters.h"

using namespace pitchlab::vst;

TEST_CASE("registry binding: engine identity comes from the v0.1 registry") {
  initEngineRegistryOnce();
  CHECK(engineCount() == 5);
  CHECK(std::strcmp(engineIdForIndex(0), "native.varispeed") == 0);
  CHECK(std::strcmp(engineIdForIndex(1), "native.vardelay") == 0);
  CHECK(std::strcmp(engineIdForIndex(2), "native.pv.classic") == 0);
  CHECK(std::strcmp(engineIdForIndex(3), "native.pv.phaselocked") == 0);
  CHECK(std::strcmp(engineIdForIndex(4), "native.granular") == 0);
  CHECK(engineIdForIndex(5) == nullptr);
  CHECK(engineIdForIndex(-1) == nullptr);
  // the display names exist for the selector/UI
  for (int i = 0; i < 5; ++i) {
    CHECK(engineNameForIndex(i) != nullptr);
    CHECK(std::strlen(engineNameForIndex(i)) > 0);
  }
}

TEST_CASE("table integrity: every meta is well-formed and unique") {
  const ParamMeta* table = parameterTable();
  const uint32_t n = parameterCount();
  CHECK(n == 19);
  std::vector<uint32_t> tags;
  for (uint32_t i = 0; i < n; ++i) {
    const ParamMeta& m = table[i];
    CHECK(m.tag != 0);
    CHECK(m.id != nullptr);
    CHECK(m.title != nullptr);
    CHECK(m.max > m.min);
    CHECK(m.defaultPlain >= m.min);
    CHECK(m.defaultPlain <= m.max);
    CHECK(m.dispFmt != nullptr);
    tags.push_back(m.tag);
  }
  std::sort(tags.begin(), tags.end());
  for (std::size_t i = 1; i < tags.size(); ++i) {
    CHECK(tags[i] != tags[i - 1]);  // unique tags
  }
}

TEST_CASE("normalisation: round trips within tolerance for continuous params") {
  const ParamMeta* table = parameterTable();
  const uint32_t n = parameterCount();
  for (uint32_t i = 0; i < n; ++i) {
    const ParamMeta& m = table[i];
    const int steps = 17;
    for (int s = 0; s <= steps; ++s) {
      const double u = static_cast<double>(s) / steps;
      const double plain = denormalise(m.tag, u);
      const double back = normalise(m.tag, plain);
      if (m.stepCount > 0) {
        // discrete (VST3 semantics: stepCount = steps between values): the
        // round trip snaps to the exact step normalisation
        const double snapped = std::floor(u * m.stepCount + 0.5) / m.stepCount;
        CHECK(back == doctest::Approx(snapped).epsilon(1e-12));
        CHECK(plain == doctest::Approx(denormalise(m.tag, snapped)).epsilon(1e-9));
      } else {
        CHECK(std::fabs(back - u) < 1e-9);
      }
    }
  }
}

TEST_CASE("normalisation: clamping outside [0,1]") {
  CHECK(denormalise(param::kPitch, -0.5) == -12.0);
  CHECK(denormalise(param::kPitch, 1.5) == 12.0);
  CHECK(normalise(param::kPitch, -100.0) == 0.0);
  CHECK(normalise(param::kPitch, +100.0) == 1.0);
}

TEST_CASE("applyNormalised: the snapshot fields follow the model") {
  ParamSnapshot s;
  applyNormalised(s, param::kEngine, normalise(param::kEngine, 3.0));
  CHECK(s.engineIndex == 3);
  applyNormalised(s, param::kPitch, normalise(param::kPitch, 7.0));
  CHECK(std::fabs(s.pitchSt - 7.0) < 1e-9);
  applyNormalised(s, param::kMix, 0.25);
  CHECK(std::fabs(s.mix - 0.25) < 1e-9);
  applyNormalised(s, param::kBypass, 1.0);
  CHECK(s.bypass);
  applyNormalised(s, param::kOutputLevel, normalise(param::kOutputLevel, 6.0));
  CHECK(std::fabs(s.outputDb - 6.0) < 1e-9);
  // derived helpers
  CHECK(std::fabs(s.liveRatio() - semitonesToRatio(7.0)) < 1e-12);
  CHECK(s.outputGain() > 1.9);  // +6 dB ~ 1.995
}

TEST_CASE("snapshot defaults mirror the v0.1 engine defaults") {
  const ParamSnapshot s;
  CHECK(s.engineIndex == 1);              // native.vardelay (the realtime-native default)
  CHECK(s.pitchSt == 0.0);
  CHECK(s.vsQuality == 2);                // "reference"
  CHECK(!s.vsAllowAliasing);
  CHECK(std::fabs(s.vdExcursionSec - 0.5) < 1e-12);
  CHECK(s.vdCrossfadeFrames == 2048);
  CHECK(std::fabs(s.grGrainSec - 0.1) < 1e-12);
  CHECK(s.grOverlap == 4);
  CHECK(s.grJitterFrames == 0);
  CHECK(s.pvcFftSize == 2048);
  CHECK(s.pvcHop == 512);
  CHECK(s.pvpFftSize == 2048);
  CHECK(s.pvpHop == 512);
  CHECK(s.mix == 1.0);
  CHECK(!s.bypass);
  CHECK(s.outputDb == 0.0);
}

TEST_CASE("chain signature: musical params do not change it") {
  const ParamSnapshot a;
  ParamSnapshot b = a;
  b.pitchSt = 5.0;
  b.lfoRateHz = 2.0;
  b.lfoDepthSt = 1.0;
  b.mix = 0.3;
  b.outputDb = -6.0;
  b.bypass = true;
  CHECK(a.chainSignature() == b.chainSignature());
  b.engineIndex = 4;
  CHECK(!(a.chainSignature() == b.chainSignature()));
  b = a;
  b.grGrainSec = 0.2;
  CHECK(!(a.chainSignature() == b.chainSignature()));
}
