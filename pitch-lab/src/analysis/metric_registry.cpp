#include "analysis/metric_registry.h"

#include <stdexcept>

namespace pitchlab::analysis {

// The metric implementations (src/analysis/metrics_*.cpp). Pure functions of
// the AnalysisContext (§4.10); no engine/harness dependencies.
MetricResult computePeakRmsCrest(const AnalysisContext& ctx);
MetricResult computeRealisedDuration(const AnalysisContext& ctx);
MetricResult computeLatency(const AnalysisContext& ctx);
MetricResult computeOnsetTiming(const AnalysisContext& ctx);
MetricResult computeTransientPreservation(const AnalysisContext& ctx);
MetricResult computeHfEnergy(const AnalysisContext& ctx);
MetricResult computeAliasingIndicator(const AnalysisContext& ctx);
MetricResult computeSpectralError(const AnalysisContext& ctx);
MetricResult computeAmplitudeModulation(const AnalysisContext& ctx);
MetricResult computePhaseCoherence(const AnalysisContext& ctx);
MetricResult computeStereoCoherence(const AnalysisContext& ctx);
MetricResult computePitchError(const AnalysisContext& ctx);
MetricResult computePitchLag(const AnalysisContext& ctx);
MetricResult computeWarbleInstability(const AnalysisContext& ctx);
MetricResult computeCpuCost(const AnalysisContext& ctx);

void MetricRegistry::registerMetric(MetricDescriptor descriptor) {
  if (descriptor.compute == nullptr) {
    throw std::logic_error(std::string("metric '") + descriptor.id +
                           "': null compute function (anti-fake rule, §10.4 item 2)");
  }
  if (descriptor.id == nullptr || descriptor.id[0] == '\0') {
    throw std::logic_error("metric id must be non-empty");
  }
  if (findById(descriptor.id) != nullptr) {
    throw std::logic_error(std::string("duplicate metric id '") + descriptor.id + "'");
  }
  metrics_.push_back(descriptor);
}

const MetricDescriptor* MetricRegistry::findById(const std::string& id) const {
  for (const MetricDescriptor& m : metrics_) {
    if (id == m.id) return &m;
  }
  return nullptr;
}

int64_t MetricRegistry::indexOf(const std::string& id) const {
  for (std::size_t i = 0; i < metrics_.size(); ++i) {
    if (metrics_[i].id == id) return static_cast<int64_t>(i);
  }
  return -1;
}

void registerProductionMetrics(MetricRegistry& registry) {
  // Registration order = the §10.1 metric module sheet order (deterministic
  // result ordering in the analysis artifact, §10.4 item 3).
  registry.registerMetric({"pitch-error", 1, computePitchError, false});
  registry.registerMetric({"pitch-lag", 1, computePitchLag, false});
  registry.registerMetric({"spectral-error", 1, computeSpectralError, true});
  registry.registerMetric({"transient-preservation", 1, computeTransientPreservation, true});
  registry.registerMetric({"onset-timing", 1, computeOnsetTiming, false});
  registry.registerMetric({"phase-coherence", 1, computePhaseCoherence, false});
  registry.registerMetric({"stereo-coherence", 1, computeStereoCoherence, false});
  registry.registerMetric({"latency", 1, computeLatency, false});
  registry.registerMetric({"realised-duration", 1, computeRealisedDuration, false});
  registry.registerMetric({"cpu-cost", 1, computeCpuCost, false});
  registry.registerMetric({"peak-rms-crest", 1, computePeakRmsCrest, false});
  registry.registerMetric({"hf-energy", 1, computeHfEnergy, false});
  registry.registerMetric({"aliasing-indicator", 1, computeAliasingIndicator, false});
  registry.registerMetric({"amplitude-modulation", 1, computeAmplitudeModulation, false});
  registry.registerMetric({"warble-instability", 1, computeWarbleInstability, false});
}

}  // namespace pitchlab::analysis
