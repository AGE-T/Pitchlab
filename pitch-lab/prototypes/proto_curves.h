#pragma once

// Pitch Lab — Task 28 candidate re-evaluation: the deterministic pitch
// transform set (task brief Phase F: identity / +12 / -12 / +7 / -7 /
// slow ramp / fast ramp / reversal / repeated octave movement / small
// +-cent modulation).
//
// Semantics match the production PitchCurveView: a DENSE INPUT-TIMELINE
// ratio signal — ratio[f] is the commanded pitch ratio at input frame f,
// strictly positive and finite for every f. Static transforms carry the
// expected ratio (dominant-frequency checks meaningful); the time-varying
// transforms exercise dynamic-pitch behaviour.

#include <cmath>
#include <string>
#include <vector>

#include "core/types.h"

namespace pitchlab::proto {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] inline double semitonesToRatio(double st) {
  return std::pow(2.0, st / 12.0);
}

struct Transform {
  std::string id;
  std::string description;
  bool staticRatio = false;
  double staticValue = 0.0;  // valid when staticRatio

  /// Build the input-timeline ratio curve over `frames` frames at `fs`.
  [[nodiscard]] std::vector<double> build(FrameCount frames, double fs) const {
    std::vector<double> r(static_cast<std::size_t>(frames));
    const double dur = static_cast<double>(frames) / fs;
    for (FrameCount i = 0; i < frames; ++i) {
      const double t = static_cast<double>(i) / fs;
      double v = 1.0;
      if (id == "identity") {
        v = 1.0;
      } else if (id == "plus12" || id == "minus12" || id == "plus7" ||
                 id == "minus7") {
        v = staticValue;
      } else if (id == "ramp-slow") {
        v = semitonesToRatio(12.0 * t / dur);
      } else if (id == "ramp-fast") {
        v = semitonesToRatio(t < 0.15 ? 12.0 * t / 0.15 : 12.0);
      } else if (id == "reversal-100ms") {
        const double step = 0.1;
        const int k = static_cast<int>(std::floor(t / step));
        v = semitonesToRatio(k % 2 == 0 ? 12.0 : -12.0);
      } else if (id == "octave-250ms") {
        const double step = 0.25;
        const int k = static_cast<int>(std::floor(t / step));
        v = semitonesToRatio(k % 2 == 0 ? 12.0 : -12.0);
      } else if (id == "cents25-5hz") {
        v = semitonesToRatio(0.25 * std::sin(2.0 * kPi * 5.0 * t));
      }
      if (!(v > 0.0) || !std::isfinite(v)) v = 1.0;  // guard (never hit)
      r[static_cast<std::size_t>(i)] = v;
    }
    return r;
  }
};

[[nodiscard]] inline const std::vector<Transform>& transforms() {
  static const std::vector<Transform> tr = [] {
    std::vector<Transform> v;
    v.push_back(Transform{"identity", "constant ratio 1.0", true, 1.0});
    v.push_back(Transform{"plus12", "constant +12 st", true,
                          semitonesToRatio(12.0)});
    v.push_back(Transform{"minus12", "constant -12 st", true,
                          semitonesToRatio(-12.0)});
    v.push_back(Transform{"plus7", "constant +7 st", true,
                          semitonesToRatio(7.0)});
    v.push_back(Transform{"minus7", "constant -7 st", true,
                          semitonesToRatio(-7.0)});
    v.push_back(Transform{
        "ramp-slow", "0 -> +12 st linear over the whole duration", false, 0.0});
    v.push_back(Transform{
        "ramp-fast", "0 -> +12 st over 150 ms, then hold", false, 0.0});
    v.push_back(Transform{"reversal-100ms",
                          "alternate +12/-12 st every 100 ms", false, 0.0});
    v.push_back(Transform{"octave-250ms",
                          "alternate +12/-12 st every 250 ms (repeated octave "
                          "movement)", false, 0.0});
    v.push_back(Transform{"cents25-5hz",
                          "+-25 cents around 1.0 at 5 Hz", false, 0.0});
    return v;
  }();
  return tr;
}

[[nodiscard]] inline const Transform* findTransform(const std::string& id) {
  for (const Transform& t : transforms()) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

}  // namespace pitchlab::proto
