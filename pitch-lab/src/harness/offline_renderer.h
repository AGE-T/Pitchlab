#pragma once

// Pitch Lab — OfflineRenderer (implementation specification §4.8 / §4.3.2 /
// §4.8.1 items 5-9, frozen cycle 3).
//
// CONTRACT: executes fully-resolved RenderJobs under the frozen engine
// contract (§4.2) and block-exchange frame accounting (§4.3):
//   * instantiate a FRESH engine per job via the registry factory (§5);
//   * deliver the padded input stream in blocks per the job's schedule;
//   * per-call out capacity per §4.2.1 item 6 (rate-follower starvation-free);
//   * stall guard, consumption guard, flush-bound enforcement (§4.3.2/§4.3.5);
//   * full-output NaN/Inf scan (JOB FAILURE non-finite-output);
//   * length policy per §4.3.3/§4.3.4 with the exact expectation replay;
//   * float64 master WAV + float32 listening copy + canonical manifest;
//   * job failures are caught and recorded; the run continues (§K).
//
// Determinism (§4.8.1 item 11): same (jobs, registry, binary) ⇒ byte-
// identical WAVs and manifests.

#include <filesystem>
#include <vector>

#include "core/engine_registry.h"
#include "harness/render_job.h"

namespace pitchlab {

/// Render all jobs sequentially. `pitchlabRoot` is used for relative-path
/// computation in manifests (jobs also carry it). Throws only on
/// harness-side I/O errors that prevent even failure-recording (e.g. the
/// manifest cannot be written); engine-side failures are per-job results.
[[nodiscard]] RenderSummary renderJobs(const std::vector<RenderJob>& jobs,
                                        const EngineRegistry& registry);

}  // namespace pitchlab
