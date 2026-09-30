// Pitch Lab — Task 28 Phase B candidate: WSOLA + resampling (see
// candidate_wsola.h for the frozen model).

#include "prototypes/candidate_wsola.h"

#include <cmath>

#include "core/errors.h"

namespace pitchlab::proto {

void WsolaPrototype::configure(const ProtoParams& params) {
  // Validate/absorb tolerance_frames here; forward ONLY the OLA parameter
  // subset to the base (it rejects unknown keys).
  ProtoParams baseParams;
  for (const auto& [key, value] : params) {
    if (key == "tolerance_frames") {
      if (const auto* d = std::get_if<double>(&value)) {
        if (!std::isfinite(*d) || *d != std::floor(*d) || *d < 0.0 ||
            *d > 8192.0) {
          throw ConfigError("", "tolerance_frames",
                            "must be an integer in [0, 8192]");
        }
        tolerance_ = static_cast<int>(*d);
      } else {
        throw ConfigError("", "tolerance_frames", "must be a number");
      }
    } else {
      baseParams.emplace(key, value);
    }
  }
  OlaPrototype::configure(baseParams);
}

void WsolaPrototype::prepare(double fs, int channels, int maxBlockFrames,
                             FrameCount totalInputFrames,
                             const double* ratioCurve) {
  // The search reads [a - N/2 - tol, ...]: the input history margin must
  // cover the tolerance BEFORE calling the base prepare (it sizes the
  // sliding input window with inputBackMargin_).
  inputBackMargin_ = tolerance_ + 64;
  OlaPrototype::prepare(fs, channels, maxBlockFrames, totalInputFrames,
                        ratioCurve);
  builtScratch_.assign(static_cast<std::size_t>(windowFrames_) + 16, 0.0);
  searches_ = 0;
  sumDelta_ = 0;
  sumAbsDelta_ = 0;
  maxAbsDelta_ = 0;
  zeroDeltas_ = 0;
}

double WsolaPrototype::grainAnalysisAdjustment(double aNominal) {
  if (grainsPlaced_ == 0 || tolerance_ == 0) {
    return 0.0;
  }
  const double half = static_cast<double>(windowFrames_) / 2.0;
  const double s = sNext_;
  // Overlap region O = [s - N/2, s - Hs + N/2) (positions where the new
  // grain overlaps the already-built output).
  const int64_t ovStart =
      static_cast<int64_t>(std::ceil(s - half));
  const int64_t ovEnd =
      static_cast<int64_t>(std::floor(s - static_cast<double>(hs_) + half));
  const int64_t ovLen = ovEnd - ovStart;
  if (ovLen <= 0 || ovLen + 16 > static_cast<int64_t>(builtScratch_.size())) {
    return 0.0;
  }

  // Normalised built estimate over O (the output-so-far comparison target).
  for (int64_t i = 0; i < ovLen; ++i) {
    const FrameCount rel = (ovStart + i) - stretchBase_;
    double v = 0.0;
    if (rel >= 0 && rel < stretchCapacity_) {
      const double w = wsum_[static_cast<std::size_t>(rel)];
      if (w > 1.0e-12) {
        v = stretch_[0][static_cast<std::size_t>(rel)] / w;
      }
    }
    builtScratch_[static_cast<std::size_t>(i)] = v;
  }

  // Deterministic search over the integer tolerance grid, ordered 0, +1,
  // -1, +2, -2, ...; strictly-smaller SSE replaces the best, so ties
  // resolve to the delta CLOSEST TO ZERO (positive first) — the nominal
  // schedule is preferred among equal candidates. The ascending-from--tol
  // order was a measured defect: on near-silent input every SSE is equal,
  // the tie-break systematically chose -tolerance and the re-anchored
  // schedule drifted backwards until the input window overflowed (the
  // drum/plus12 reproduction; fixed together with the law-anchored
  // nominal in the base engine).
  int64_t bestDelta = 0;
  double bestSse = std::numeric_limits<double>::max();
  const int64_t tol = static_cast<int64_t>(tolerance_);
  for (int64_t radius = 0; radius <= tol; ++radius) {
    const int64_t candidates[2] = {radius, -radius};
    for (int ci = (radius == 0 ? 0 : 1); ci < 2; ++ci) {
      const int64_t d = candidates[ci];
      double sse = 0.0;
      for (int64_t i = 0; i < ovLen; ++i) {
        const double p = static_cast<double>(ovStart + i);
        const double inputPos = aNominal + static_cast<double>(d) + (p - s);
        const double v =
            readInput(0, static_cast<FrameCount>(std::floor(inputPos)));
        const double diff = v - builtScratch_[static_cast<std::size_t>(i)];
        sse += diff * diff;
      }
      if (sse < bestSse) {
        bestSse = sse;
        bestDelta = d;
      }
    }
  }

  ++searches_;
  sumDelta_ += bestDelta;
  sumAbsDelta_ += std::abs(bestDelta);
  maxAbsDelta_ = std::max(maxAbsDelta_, std::abs(bestDelta));
  if (bestDelta == 0) ++zeroDeltas_;
  return static_cast<double>(bestDelta);
}

json::Value WsolaPrototype::diagnostics() const {
  json::Object o;
  json::Value base = OlaPrototype::diagnostics();
  if (base.kind() == json::Value::Kind::Object) {
    for (const auto& [k, v] : base.asObject()) o[k] = v;
  }
  o["wsola_searches"] = json::Value(searches_);
  o["wsola_mean_delta"] =
      json::Value(searches_ > 0
                      ? static_cast<double>(sumDelta_) /
                            static_cast<double>(searches_)
                      : 0.0);
  o["wsola_mean_abs_delta"] =
      json::Value(searches_ > 0
                      ? static_cast<double>(sumAbsDelta_) /
                            static_cast<double>(searches_)
                      : 0.0);
  o["wsola_max_abs_delta"] = json::Value(static_cast<int64_t>(maxAbsDelta_));
  o["wsola_zero_ratio"] =
      json::Value(searches_ > 0
                      ? static_cast<double>(zeroDeltas_) /
                            static_cast<double>(searches_)
                      : 0.0);
  o["wsola_tolerance"] = json::Value(static_cast<int64_t>(tolerance_));
  return json::Value(std::move(o));
}

}  // namespace pitchlab::proto
