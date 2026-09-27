#pragma once

// Pitch Lab — analysis-layer types (implementation specification §10/§10.4
// items 3-4, frozen cycle 5 BEFORE coding).
//
// CONTRACT:
//   * MetricResult = the §4.10 MetricResult made concrete: identity +
//     status + named values + alignment/method metadata + tolerance
//     classification + notes. NEVER an aggregate quality number (§O.3 /
//     cycle-5 hard rule: metrics stay separate dimensions).
//   * Status vocabulary (§10.4 item 4): ok | not-applicable |
//     insufficient-data | reference-unavailable | tracker-unavailable |
//     tainted-input | analysis-error. Missing data is NEVER 0/NaN/empty.
//   * Values are carried as canonical JSON object members (sorted keys =>
//     deterministic serialisation; %.17g doubles round-trip exactly).

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "harness/json_writer.h"

namespace pitchlab::analysis {

enum class Status {
  Ok,
  NotApplicable,
  InsufficientData,
  ReferenceUnavailable,
  TrackerUnavailable,
  TaintedInput,
  AnalysisError,
};

[[nodiscard]] inline const char* statusName(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::NotApplicable: return "not-applicable";
    case Status::InsufficientData: return "insufficient-data";
    case Status::ReferenceUnavailable: return "reference-unavailable";
    case Status::TrackerUnavailable: return "tracker-unavailable";
    case Status::TaintedInput: return "tainted-input";
    case Status::AnalysisError: return "analysis-error";
  }
  return "?";
}

struct ToleranceInfo {
  enum class Kind { Exact, Provisional, None };
  Kind kind = Kind::None;
  bool hasValue = false;
  double value = 0.0;
  std::string source;  // config/tolerances.toml key (provisional entries)

  [[nodiscard]] static ToleranceInfo exact() { return ToleranceInfo{Kind::Exact, false, 0.0, ""}; }
  [[nodiscard]] static ToleranceInfo provisional(double value, const std::string& source) {
    return ToleranceInfo{Kind::Provisional, true, value, source};
  }
  [[nodiscard]] static ToleranceInfo none() { return ToleranceInfo{}; }
};

/// One metric's outcome for one analysed render job. The context-provenance
/// fields (engineId/assetId/curveId/sampleRate/channels/manifest refs) are
/// attached by the Analyzer at serialisation time — metrics fill only the
/// measurement parts.
struct MetricResult {
  std::string metricId;
  int metricVersion = 1;
  Status status = Status::Ok;
  std::string unit = "none";
  json::Object values;      // named values; null members = "undefined for a
                            // recorded reason" (note required)
  json::Object alignment;   // {"axis": ..., "method": ...}
  json::Object method;      // per-metric frozen method metadata
  ToleranceInfo tolerance;
  std::vector<std::string> notes;
  bool hasSampleCount = false;
  int64_t sampleCount = 0;
  bool hasFrameCount = false;
  int64_t frameCount = 0;
  std::string error;  // analysis-error reason (status AnalysisError only)
};

// Small helpers shared by metric modules (deterministic, own arithmetic).

/// Principal-value wrap to (-pi, pi] (the §6.3.1 item 5 frozen operation).
[[nodiscard]] inline double princarg(double x) {
  const double twoPi = 6.283185307179586476925286766559;
  return x - twoPi * static_cast<double>(std::llround(x / twoPi));
}

}  // namespace pitchlab::analysis
