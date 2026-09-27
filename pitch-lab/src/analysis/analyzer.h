#pragma once

// Pitch Lab — Analyzer (implementation specification §4.8/§4.10/§10.4 items
// 1/5/20, frozen cycle 5). Reads manifests + master WAVs + corpus context,
// executes the selected metric modules as pure functions, emits immutable
// deterministic analysis artifacts under artifacts/analysis/. NEVER touches
// engines, NEVER re-renders, NEVER mutates render artifacts.

#include <filesystem>
#include <string>
#include <vector>

#include "analysis/analysis_context.h"
#include "analysis/analysis_types.h"
#include "analysis/metric_registry.h"
#include "harness/render_job.h"

namespace pitchlab::analysis {

struct JobAnalysisOutcome {
  std::string experimentId;
  std::string engineId;
  std::string assetId;
  std::string curveId;
  bool completed = false;               // analysis produced an artifact
  std::string error;                   // job-level analysis-error reason
  std::filesystem::path artifactPath;  // absolute
  int resultCount = 0;
  int errorCount = 0;                   // metrics with status analysis-error
  std::vector<std::string> statusSummary;  // "ok:12 not-applicable:2 ..."
};

struct AnalyzeOutcome {
  std::vector<JobAnalysisOutcome> jobs;
  [[nodiscard]] bool allCompleted() const {
    for (const JobAnalysisOutcome& j : jobs) {
      if (!j.completed) return false;
    }
    return true;
  }
  [[nodiscard]] int totalErrors() const {
    int n = 0;
    for (const JobAnalysisOutcome& j : jobs) n += j.errorCount + (j.completed ? 0 : 1);
    return n;
  }
};

/// Analyse all jobs. `selectedMetrics` = the metric ids to execute
/// (experiment [analysis].metrics or the CLI --metrics override; validated
/// by the CALLER against the registry — unknown ids are CONFIG ERRORs
/// before any analysis starts). Throws ConfigError only for
/// global-config problems (tolerances/metrics.toml). Per-job problems are
/// outcomes, not exceptions (the run continues, §10.4 item 21).
[[nodiscard]] AnalyzeOutcome analyzeJobs(const std::vector<RenderJob>& jobs,
                                         const std::vector<std::string>& selectedMetrics,
                                         const MetricRegistry& registry,
                                         const MetricConfig& metricConfig);

/// Serialise one job's analysis artifact (canonical JSON; deterministic).
[[nodiscard]] json::Value buildAnalysisArtifact(const AnalysisContext& ctx,
                                                const std::vector<MetricResult>& results,
                                                const std::vector<std::string>& requestedMetrics);

}  // namespace pitchlab::analysis
