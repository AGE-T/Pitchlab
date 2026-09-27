// Pitch Lab — tracker-gated metrics (implementation specification §10.4
// item 19, frozen cycle 5 BEFORE coding): pitch-error, pitch-lag,
// warble-instability (interfaces + schema exist; measurement gated behind
// the ReferencePitchTracker — OD-12 resolved to the clean-room C++ pYIN
// route, implementation is LATER WORK) + cpu-cost (measurement-only; the
// renderer-side timing boundary does not exist in v0.1 and is NOT faked).
//
// HARD RULE (§17 step 5): NO substitute estimator. No Goertzel f0, no
// zero-crossing, no hidden Python/library, no one-off hack. The metric
// honestly returns tracker-unavailable until the real tracker exists.

#include <string>

#include "analysis/analysis_context.h"
#include "analysis/analysis_types.h"

namespace pitchlab::analysis {
namespace {

[[nodiscard]] MetricResult trackerGated(const char* metricId,
                                        const std::vector<std::string>& valueFields) {
  MetricResult r;
  r.metricId = metricId;
  r.unit = "cents";
  r.alignment["axis"] = json::Value("input-timeline");
  r.alignment["method"] = json::Value("emission-map (rate-followers) — pending tracker");
  for (const std::string& f : valueFields) {
    r.values[f] = json::Value(nullptr);
  }
  r.notes.push_back("tracker-unavailable: the ReferencePitchTracker (OD-12, resolved "
                    "route: clean-room C++ pYIN) is not implemented — no substitute "
                    "estimator is used (§10.2/§10.4 item 19)");
  r.status = Status::TrackerUnavailable;
  return r;
}

}  // namespace

MetricResult computePitchError(const AnalysisContext& ctx) {
  (void)ctx;
  return trackerGated("pitch-error", {"medianCents", "p95Cents", "excludedVoicedFraction"});
}

MetricResult computePitchLag(const AnalysisContext& ctx) {
  (void)ctx;
  return trackerGated("pitch-lag", {"lagMs", "correlationAtLag"});
}

MetricResult computeWarbleInstability(const AnalysisContext& ctx) {
  (void)ctx;
  return trackerGated("warble-instability", {"f0FlutterStdCents", "f0MeanHz"});
}

MetricResult computeCpuCost(const AnalysisContext& ctx) {
  (void)ctx;
  MetricResult r;
  r.metricId = "cpu-cost";
  r.unit = "none";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("renderer-measured (measurement-only)");
  r.method["declaredInputs"] =
      json::Value("per block size {32..4096}: real-time factor, single thread");
  r.values["realTimeFactor"] = json::Value(nullptr);
  r.values["blockFrames"] = json::Value(nullptr);
  r.values["processingTimeSec"] = json::Value(nullptr);
  r.notes.push_back("not-applicable: renderer-side timing instrumentation is not "
                    "implemented in v0.1; cpu-cost is a measurement-only metric "
                    "(machine-dependent, never a CI gate) — the timing boundary is "
                    "not faked (§10.4 item 19)");
  r.status = Status::NotApplicable;
  return r;
}

}  // namespace pitchlab::analysis
