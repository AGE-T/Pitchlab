#pragma once

// Pitch Lab — metric registry (implementation specification §10.4 item 2,
// frozen cycle 5 BEFORE coding). The engine-registry pattern applied to
// metric identity: ONE authoritative in-code registration point
// (registerProductionMetrics() in metric_registry.cpp); never config. Human
// config (metrics.toml / experiment selections) may only SELECT metrics;
// identity and implementation are authoritative in code. Unknown id ⇒
// CONFIG ERROR — as with engine identity (§4.6 pattern).

#include <string>
#include <vector>

#include "analysis/analysis_context.h"
#include "analysis/analysis_types.h"

namespace pitchlab::analysis {

using MetricFn = MetricResult (*)(const AnalysisContext& ctx);

struct MetricDescriptor {
  const char* id = "";        // the §10.1 module id (dash spelling)
  int version = 1;
  MetricFn compute = nullptr;  // REQUIRED non-null (anti-fake rule)
  bool requiresReference = false;
};

class MetricRegistry {
 public:
  void registerMetric(MetricDescriptor descriptor);  // null/dup/empty id throws

  [[nodiscard]] std::size_t size() const { return metrics_.size(); }
  [[nodiscard]] const MetricDescriptor& at(std::size_t index) const {
    return metrics_.at(index);
  }
  /// nullptr when unknown (never a default metric).
  [[nodiscard]] const MetricDescriptor* findById(const std::string& id) const;

  /// Index of an id in registration order; -1 when unknown.
  [[nodiscard]] int64_t indexOf(const std::string& id) const;

 private:
  std::vector<MetricDescriptor> metrics_;
};

/// THE authoritative production metric registration point (§10.4 item 2;
/// the §10.1 module id set, registration order = the §10.1 table order).
/// Called once per analysis run, then the registry is read-only.
void registerProductionMetrics(MetricRegistry& registry);

}  // namespace pitchlab::analysis
