#pragma once

// Pitch Lab — Task 28 candidate re-evaluation: the existing-engine
// differentiation baseline (Phase H).
//
// Drives the FIVE frozen production engines through their REAL contract
// (configure -> prepare -> process -> finish) on the task-28 corpus with
// DEFAULT configurations, using the same measurement layer as the
// candidates. This is READ-ONLY reuse of the v0.1 engines: nothing under
// src/engines/ or src/core/ is modified, the production registry order is
// untouched, and the engines are addressed exactly as the harness does
// (registerProductionEngines -> findById -> factory).
//
// Baseline matrix (deliberately reduced vs the candidate matrix — the
// full ten-transform sweep is required for the CANDIDATES; for the
// existing engines the four transforms below already separate their
// artifact families): identity, +12, -12, ramp-fast.

#include <string>
#include <vector>

#include "core/types.h"

namespace pitchlab::proto {

struct BaselineRecord {
  std::string engineId;
  std::string materialId;
  std::string transformId;
  bool ok = false;
  std::string error;
  std::vector<std::vector<double>> out;
  FrameCount frames = 0;
};

/// Render one baseline record (engine default configuration, seed 1).
[[nodiscard]] BaselineRecord renderBaseline(const std::string& engineId,
                                            const std::string& materialId,
                                            const std::string& transformId,
                                            int maxBlockFrames = 1024);

/// The five frozen engine ids (registry order — informational only).
[[nodiscard]] const std::vector<std::string>& baselineEngineIds();

/// The baseline transform ids.
[[nodiscard]] const std::vector<std::string>& baselineTransformIds();

}  // namespace pitchlab::proto
