#pragma once

// Pitch Lab — ExperimentCompiler (implementation specification §4.8 /
// §4.8.1 items 1-4, frozen cycle 3).
//
// CONTRACT: takes a frozen-schema experiment TOML (§15.4), resolves it
// against the sealed engine registry (the ONLY engine authority — never a
// second registry), the corpus, the curve library and the harness defaults,
// and produces fully-resolved RenderJobs + a visible SkipList. Hard
// authoring/validity errors throw ConfigError{file, field, reason} (§4.8);
// per-pairing capability/availability mismatches become visible SKIPS
// (§4.8.1 item 3 — never silent adaptation, never silent drops).
//
// Determinism: job/skip ORDER is the deterministic cross-product order
// (inputs × rates × channels × curves × engines, nested in that order);
// identical inputs ⇒ identical outcome, always.

#include <filesystem>
#include <string>
#include <vector>

#include "core/engine_registry.h"
#include "harness/render_job.h"

namespace pitchlab {

struct CompileOutcome {
  std::vector<RenderJob> jobs;
  std::vector<SkipEntry> skips;
};

/// Compile an experiment file. `pitchlabRoot` = the pitch-lab root
/// (contains assets/, experiments/, config/); `outputRoot` = where the
/// renderer emits (normally <root>/artifacts). `harnessCfg` = parsed
/// config/harness.toml (the default block schedule).
/// Throws ConfigError on any hard error (schema, unknown engine id,
/// missing asset/curve, unknown parameter key, missing seed, corpus
/// integrity mismatch, non-first-class rate/channel).
[[nodiscard]] CompileOutcome compileExperiment(const std::filesystem::path& experimentToml,
                                               const EngineRegistry& registry,
                                               const std::filesystem::path& pitchlabRoot,
                                               const std::filesystem::path& outputRoot,
                                               const HarnessConfig& harnessCfg);

}  // namespace pitchlab
