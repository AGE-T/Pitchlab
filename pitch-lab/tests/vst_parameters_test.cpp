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
  // Task 33: the 6th production engine (selector index 5).
  CHECK(engineCount() == 6);
  CHECK(std::strcmp(engineIdForIndex(0), "native.varispeed") == 0);
  CHECK(std::strcmp(engineIdForIndex(1), "native.vardelay") == 0);
  CHECK(std::strcmp(engineIdForIndex(2), "native.pv.classic") == 0);
  CHECK(std::strcmp(engineIdForIndex(3), "native.pv.phaselocked") == 0);
  CHECK(std::strcmp(engineIdForIndex(4), "native.granular") == 0);
  CHECK(std::strcmp(engineIdForIndex(5), "native.timepitch") == 0);
  CHECK(engineIdForIndex(6) == nullptr);
  CHECK(engineIdForIndex(-1) == nullptr);
  // the display names exist for the selector/UI
  for (int i = 0; i < 6; ++i) {
    CHECK(engineNameForIndex(i) != nullptr);
    CHECK(std::strlen(engineNameForIndex(i)) > 0);
  }
}

TEST_CASE("table integrity: every meta is well-formed and unique") {
  const ParamMeta* table = parameterTable();
  const uint32_t n = parameterCount();
  // Task 33: 19 v0.1 rows + 6 native.timepitch rows (Fixed+Adaptive+
  // tolerance+formant; the amendment's FINAL surface) = 25
  CHECK(n == 25);
  std::vector<uint32_t> tags;
  for (uint32_t i = 0; i < n; ++i) {
    const ParamMeta& m = table[i];
    CHECK(m.tag != 0);
    CHECK(m.id != nullptr);
    CHECK(m.title != nullptr);
    // a pinned single-choice row (the task-33 staged mode, domain [0,0]) is
    // the one legal max == min form
    const bool domainOk = m.max > m.min || (m.choiceCount == 1 && m.stepCount == 0);
    CHECK(domainOk);
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
      } else if (m.max > m.min) {
        CHECK(std::fabs(back - u) < 1e-9);
      } else {
        // pinned single-choice (the task-33 staged mode): constant map
        CHECK(back == doctest::Approx(0.0).epsilon(1e-12));
      }
    }
  }
}

TEST_CASE("normalisation: clamping outside [0,1]") {
  // Task-33 continuation (Phase 5): the pitch control domain widened
  // -12..+12 -> -48..+48 st (capability-respecting; the per-engine support
  // is displayed and the chain envelope clamps with counted events).
  CHECK(denormalise(param::kPitch, -0.5) == doctest::Approx(-48.0));
  CHECK(denormalise(param::kPitch, 1.5) == doctest::Approx(48.0));
  CHECK(denormalise(param::kPitch, 0.5) == doctest::Approx(0.0));
  CHECK(denormalise(param::kPitch, 0.625) == doctest::Approx(12.0));
  CHECK(denormalise(param::kPitch, 0.25) == doctest::Approx(-24.0));
  CHECK(normalise(param::kPitch, -100.0) == 0.0);
  CHECK(normalise(param::kPitch, +100.0) == 1.0);
  // the old domain's boundaries map inside the new one (state compat: an
  // old +-12 state loads unchanged): (-12-(-48))/96 = 0.375
  CHECK(normalise(param::kPitch, -12.0) == doctest::Approx(0.375));
  CHECK(normalise(param::kPitch, +12.0) == doctest::Approx(0.625));
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

// ---------------------------------------------------------------------------
// Task 24 defect-audit regressions (P1.7 metadata / P1.8 formatting / P1.9
// parsing)
// ---------------------------------------------------------------------------

TEST_CASE("P1.7 regression: integer-domain parameters are discrete (stepCount set)") {
  // crossfade / overlap / jitter are integer parameters in the v0.1 engine
  // contract; the VST3 metadata now says so (previously continuous with a
  // truncating conversion). Every legal integer must round-trip EXACTLY
  // through plain -> normalized -> plain.
  struct IntParam {
    uint32_t tag;
    double min;
    double max;
  };
  const IntParam intParams[] = {
      {param::kVdCrossfade, 0, 8192},
      {param::kGrOverlap, 4, 16},
      {param::kGrJitter, 0, 256},
  };
  for (const IntParam& ip : intParams) {
    const ParamMeta* m = nullptr;
    for (uint32_t i = 0; i < parameterCount(); ++i) {
      if (parameterTable()[i].tag == ip.tag) m = &parameterTable()[i];
    }
    REQUIRE(m != nullptr);
    CAPTURE(m->id);
    CHECK(m->stepCount == static_cast<int>(ip.max - ip.min));
    // the full legal integer range round-trips exactly
    for (double plain = ip.min; plain <= ip.max; ++plain) {
      const double norm = normalise(ip.tag, plain);
      const double back = denormalise(ip.tag, norm);
      if (std::fabs(back - plain) > 1e-9) {
        CHECK_MESSAGE(false, "integer round-trip mismatch at plain=" << plain);
      }
    }
  }
}

TEST_CASE("P1.8 regression: type-safe formatting for every parameter") {
  // formatParamValue must produce a non-empty, parseable representation
  // for EVERY parameter (the previous snprintf forwarded a double through
  // "%d"/"%s" entries — undefined behaviour, garbage output).
  for (uint32_t i = 0; i < parameterCount(); ++i) {
    const ParamMeta& m = parameterTable()[i];
    CAPTURE(m.id);
    const double plain = (m.min + m.max) * 0.5;
    char buf[48];
    formatParamValue(m.tag, plain, buf, sizeof(buf));
    CHECK(buf[0] != '\0');
    // integer-domain tags format as integers
    switch (m.tag) {
      case param::kEngine:
      case param::kVsQuality:
      case param::kVdCrossfade:
      case param::kGrOverlap:
      case param::kGrJitter:
      case param::kPvcFft:
      case param::kPvcHop:
      case param::kPvpFft:
      case param::kPvpHop: {
        double parsed = -1.0;
        CHECK(parseParamPlain(m.tag, buf, parsed) == true);
        CHECK(std::fabs(parsed - std::round(parsed)) < 1e-9);  // an integer
        break;
      }
      case param::kVsAllowAliasing:
      case param::kBypass:
      case param::kGrWindow: {
        const std::string form(buf);
        const bool semantic = form == "on" || form == "off" || form == "hann" ||
                              form == "triangular";
        CHECK(semantic);
        break;
      }
      default:
        break;
    }
  }
  // the semantic formatters
  char buf[48];
  formatParamValue(param::kVsQuality, 2.0, buf, sizeof(buf));
  CHECK(std::string(buf) == "reference");
  formatParamValue(param::kBypass, 1.0, buf, sizeof(buf));
  CHECK(std::string(buf) == "on");
  formatParamValue(param::kGrWindow, 0.0, buf, sizeof(buf));
  CHECK(std::string(buf) == "hann");
}

TEST_CASE("P1.9 regression: parsing round-trips + rejects invalid input") {
  // formatParamValue output parses back to the same plain value for every
  // parameter; invalid input is rejected (the previous std::atof silently
  // turned "on"/"hann"/garbage into 0).
  for (uint32_t i = 0; i < parameterCount(); ++i) {
    const ParamMeta& m = parameterTable()[i];
    CAPTURE(m.id);
    // discrete parameters round-trip LEGAL (integer-domain) values exactly;
    // a non-integer plain on a discrete parameter is not a legal value (the
    // normalisation snaps it) — the display formats the snapped value
    double mid = (m.min + m.max) * 0.5;
    if (m.stepCount > 0) mid = m.min + std::round(mid - m.min);
    for (const double plain : {mid, m.min, m.max}) {
      char buf[48];
      formatParamValue(m.tag, plain, buf, sizeof(buf));
      double parsed = -999.0;
      CHECK(parseParamPlain(m.tag, buf, parsed) == true);
      const double err = std::fabs(parsed - plain);
      if (m.stepCount > 0) {
        CHECK(err < 1e-9);
      } else {
        const double tol = std::fabs(m.max - m.min) * 0.02 + 1e-9;
        CHECK(err < tol);
      }
    }
  }
  // invalid text is rejected for every parameter
  for (uint32_t i = 0; i < parameterCount(); ++i) {
    const ParamMeta& m = parameterTable()[i];
    CAPTURE(m.id);
    double v = -999.0;
    CHECK(parseParamPlain(m.tag, "garbage", v) == false);
    CHECK(parseParamPlain(m.tag, "", v) == false);
    CHECK(v == -999.0);  // untouched on failure — never silently zero
  }
  // the numeric head parse rejects trailing junk but accepts units after a
  // space only when the caller stripped them (documented contract)
  double v = -999.0;
  CHECK(parseParamPlain(param::kPitch, "+7.5", v) == true);
  CHECK(std::fabs(v - 7.5) < 1e-9);
  CHECK(parseParamPlain(param::kPitch, "7.5x", v) == false);
  CHECK(parseParamPlain(param::kPitch, "  ", v) == false);
  CHECK(parseParamPlain(param::kMix, "0.5", v) == true);
  CHECK(std::fabs(v - 0.5) < 1e-9);
}

// ---------------------------------------------------------------------------
// Task 32 — the LFO rate domain: 0.00 .. 8.00 Hz with 0 Hz = OFF
// ---------------------------------------------------------------------------

TEST_CASE("Task 32: the LFO rate domain is 0.0..8.0 Hz (0 = the OFF state)") {
  const ParamMeta* m = findParamMeta(param::kLfoRate);
  REQUIRE(m != nullptr);
  CHECK(m->min == 0.0);   // the slider's left end IS 0.00 Hz (no implied 0.10 floor)
  CHECK(m->max == 8.0);
  CHECK(m->defaultPlain == 5.0);   // the product default is unchanged
  CHECK(m->automatable);           // host-automation capability unchanged
  // the normalised mapping is EXACT at the OFF boundary: a saved/restored
  // OFF state round-trips bit-exactly (state stores normalised values)
  CHECK(normalise(param::kLfoRate, 0.0) == 0.0);
  CHECK(denormalise(param::kLfoRate, 0.0) == 0.0);
  CHECK(denormalise(param::kLfoRate, 1.0) == 8.0);
  // mid-domain spot check: 5 Hz at the (new) linear mapping
  CHECK(normalise(param::kLfoRate, 5.0) == doctest::Approx(0.625).epsilon(1e-12));
  CHECK(denormalise(param::kLfoRate, 0.625) == doctest::Approx(5.0).epsilon(1e-12));
  // the round trip over the whole domain (incl. the OFF boundary)
  for (int s = 0; s <= 20; ++s) {
    const double u = static_cast<double>(s) / 20.0;
    const double plain = denormalise(param::kLfoRate, u);
    const double back = normalise(param::kLfoRate, plain);
    CHECK(std::fabs(back - u) < 1e-12);
  }
  // applyNormalised carries the OFF state into the snapshot exactly
  ParamSnapshot snap;
  applyNormalised(snap, param::kLfoRate, 0.0);
  CHECK(snap.lfoRateHz == 0.0);
  // the depth is the complementary gate (0 st also disables); the rate
  // domain change does not touch it
  const ParamMeta* d = findParamMeta(param::kLfoDepth);
  CHECK(d->min == 0.0);
  CHECK(d->max == 2.0);
  CHECK(d->defaultPlain == 0.0);
}

TEST_CASE("Task 32: the LFO rate formats/parses the OFF state") {
  char buf[48];
  // the host-facing VALUE REPRESENTATION stays numeric ("0.00" — Part F:
  // 0.00 Hz is the value representation; "OFF" is the editor's label)
  formatParamValue(param::kLfoRate, 0.0, buf, sizeof(buf));
  CHECK(std::strcmp(buf, "0.00") == 0);
  double v = -999.0;
  CHECK(parseParamPlain(param::kLfoRate, buf, v) == true);
  CHECK(v == 0.0);
  // the semantic alias: "off" parses to the 0 Hz boundary (case-insensitive)
  CHECK(parseParamPlain(param::kLfoRate, "off", v) == true);
  CHECK(v == 0.0);
  CHECK(parseParamPlain(param::kLfoRate, "OFF", v) == true);
  CHECK(v == 0.0);
  // "on" is deliberately NOT accepted (a rate has no single "on" value)
  CHECK(parseParamPlain(param::kLfoRate, "on", v) == false);
  CHECK(v == 0.0);  // untouched on failure
  // a normal active value still round-trips
  formatParamValue(param::kLfoRate, 5.0, buf, sizeof(buf));
  CHECK(std::strcmp(buf, "5.00") == 0);
  CHECK(parseParamPlain(param::kLfoRate, "5.00", v) == true);
  CHECK(v == 5.0);
  // below-domain normalised values clamp to the OFF boundary (never invalid)
  CHECK(denormalise(param::kLfoRate, -0.5) == 0.0);
}

TEST_CASE("Task 32: the LFO rate stays OUT of the chain signature (no rebuild on OFF)") {
  // The rate is a live curve control: toggling it must never rebuild the
  // engine chain (the chain signature covers engine identity + engine
  // configuration only). Locked at the MODEL level.
  const ParamSnapshot a;
  ParamSnapshot b = a;
  b.lfoRateHz = 0.0;
  CHECK(a.chainSignature() == b.chainSignature());
  b.lfoRateHz = 8.0;
  CHECK(a.chainSignature() == b.chainSignature());
  b.lfoDepthSt = 2.0;
  CHECK(a.chainSignature() == b.chainSignature());  // depth is live too (Task 30)
}
