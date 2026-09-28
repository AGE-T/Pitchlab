// Pitch Lab CLI — implementation-phase state (cycle 5: compile + render +
// analyze are REAL; report/verify/listen-index remain honestly unimplemented,
// §17 step 6).
//
// Architecture §B.1 entry points: compile | render | analyze | report |
// verify | listen-index + registry introspection engines. The corpus_gen
// tool is a separate executable (§11).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "analysis/analyzer.h"
#include "analysis/analysis_context.h"
#include "analysis/metric_registry.h"
#include "core/engine_registry.h"
#include "core/errors.h"
#include "core/version.h"
#include "harness/experiment_compiler.h"
#include "harness/offline_renderer.h"

namespace {

int cmdVersion() {
  std::printf("pitchlab %s (%s)\ncompiler: %s\nstatus: all five v0.1 engines implemented "
              "(varispeed, vardelay, pv.classic, pv.phaselocked, granular) — registry "
              "content equals implemented engines; analysis layer implemented (§17 "
              "step 5, analytic metrics; the OD-12 clean-room pYIN tracker implemented "
              "— the tracker-dependent metrics measure)",
              pitchlab::versionString(), pitchlab::phaseString(), pitchlab::compilerId());
  return 0;
}

int cmdEngines() {
  pitchlab::EngineRegistry registry;
  pitchlab::registerProductionEngines(registry);
  registry.seal();

  std::printf("pitchlab %s (%s) — %zu engine(s) registered\n",
              pitchlab::versionString(), pitchlab::phaseString(), registry.size());
  if (registry.size() == 0) {
    std::printf("no engines implemented yet\n");
    return 0;
  }
  for (std::size_t i = 0; i < registry.size(); ++i) {
    const pitchlab::EngineDescriptor& d = registry.at(i);
    std::printf("  %-22s %-8s reference=%s  duration=%s  ratio=[%g, %g]\n", d.info.id,
                d.info.version, d.isReferenceRole ? "yes" : "no",
                d.capabilities.duration == pitchlab::DurationBehaviour::RateFollowing
                    ? "RateFollowing"
                    : "Preserving",
                d.capabilities.minRatio, d.capabilities.maxRatio);
  }
  return 0;
}

// Build the sealed production registry (the single registration point).
pitchlab::EngineRegistry productionRegistry() {
  pitchlab::EngineRegistry registry;
  pitchlab::registerProductionEngines(registry);
  registry.seal();
  return registry;
}

int cmdCompile(const std::filesystem::path& experimentToml, const std::filesystem::path& root) {
  try {
    const pitchlab::EngineRegistry registry = productionRegistry();
    const pitchlab::HarnessConfig harnessCfg = pitchlab::loadHarnessConfig(root);
    const pitchlab::CompileOutcome outcome =
        pitchlab::compileExperiment(experimentToml, registry, root, root / "artifacts",
                                    harnessCfg);
    std::printf("experiment '%s' compiled: %zu job(s), %zu skip(s)\n",
                experimentToml.string().c_str(), outcome.jobs.size(), outcome.skips.size());
    for (const pitchlab::SkipEntry& skip : outcome.skips) {
      std::printf("  SKIP [%s] %s\n", skip.reason.c_str(), skip.detail.c_str());
    }
    for (const pitchlab::RenderJob& job : outcome.jobs) {
      std::printf("  JOB  %s @ %u Hz/%d ch, curve '%s', engine '%s'\n", job.assetId.c_str(),
                  job.sampleRate, job.channels, job.requestedSpec.id.c_str(),
                  job.engineId.c_str());
    }
    return 0;
  } catch (const pitchlab::ConfigError& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "pitchlab compile: unexpected error: %s\n", e.what());
    return 1;
  }
}

int cmdRender(const std::filesystem::path& experimentToml, const std::filesystem::path& root) {
  pitchlab::RenderSummary summary;
  try {
    const pitchlab::EngineRegistry registry = productionRegistry();
    const pitchlab::HarnessConfig harnessCfg = pitchlab::loadHarnessConfig(root);
    const pitchlab::CompileOutcome outcome =
        pitchlab::compileExperiment(experimentToml, registry, root, root / "artifacts",
                                    harnessCfg);
    for (const pitchlab::SkipEntry& skip : outcome.skips) {
      std::printf("SKIP [%s] %s\n", skip.reason.c_str(), skip.detail.c_str());
    }
    std::printf("rendering %zu job(s)...\n", outcome.jobs.size());
    summary = pitchlab::renderJobs(outcome.jobs, registry);
  } catch (const pitchlab::ConfigError& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "pitchlab render: unexpected error: %s\n", e.what());
    return 1;
  }

  int failures = 0;
  for (const pitchlab::JobResult& r : summary.results) {
    if (r.status == pitchlab::RenderStatus::Failed) {
      ++failures;
      std::printf("FAIL  %s / %s / %s: %s\n", r.experimentId.c_str(), r.engineId.c_str(),
                  r.assetId.c_str(), r.failureReason.c_str());
      continue;
    }
    std::printf("OK    %s / %s / %s / %s: %lld frames (expected %lld, delta %lld, flush %lld, "
                "taints %zu)%s%s\n",
                r.experimentId.c_str(), r.engineId.c_str(), r.assetId.c_str(),
                r.curveId.c_str(), static_cast<long long>(r.outputFramesProduced),
                static_cast<long long>(r.expectedOutputFrames),
                static_cast<long long>(r.lengthDeltaFrames),
                static_cast<long long>(r.flushFrames), r.taints.size(),
                r.masterWav.empty() ? "" : "\n      master: ",
                r.masterWav.empty() ? "" : r.masterWav.string().c_str());
  }
  std::printf("%zu job(s): %d failed, %zu ok\n", summary.results.size(), failures,
              summary.results.size() - static_cast<std::size_t>(failures));
  return failures == 0 ? 0 : 1;
}

void printHelp() {
  std::printf(
      "pitchlab %s (%s)\n"
      "implemented: --version, engines, compile, render, analyze\n"
      "  pitchlab --version                    print version, phase, compiler\n"
      "  pitchlab engines                      list registered engines (authoritative registry)\n"
      "  pitchlab compile <experiment.toml>    validate + resolve jobs (dry run)\n"
      "  pitchlab render  <experiment.toml>    compile + render all jobs\n"
      "  pitchlab analyze <experiment.toml>    compile + analyse existing renders (never re-renders)\n"
      "    [--metrics <id,...>]                metric selection (overrides [analysis].metrics)\n"
      "    [--root <dir>]                      pitch-lab root (default: working directory)\n"
      "not implemented yet (v0.1 step 6): report, verify, listen-index\n",
      pitchlab::versionString(), pitchlab::phaseString());
}

// --root <dir> option extraction; returns the root (default: cwd).
std::filesystem::path extractRoot(int argc, char** argv, int optionIndexLimit) {
  for (int i = 2; i < argc && i < optionIndexLimit + 2; ++i) {
    if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
      return std::filesystem::path(argv[i + 1]);
    }
  }
  return std::filesystem::current_path();
}

// --metrics <id,id,...> extraction (empty when absent).
std::string extractMetricsOption(int argc, char** argv, int optionIndexLimit) {
  for (int i = 2; i < argc && i < optionIndexLimit + 2; ++i) {
    if (std::strcmp(argv[i], "--metrics") == 0 && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return "";
}

[[nodiscard]] std::vector<std::string> splitMetrics(const std::string& s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const std::size_t comma = s.find(',', start);
    const std::string tok = s.substr(start, comma == std::string::npos ? s.size() - start
                                                                       : comma - start);
    // Trim whitespace.
    const std::size_t b = tok.find_first_not_of(" \t");
    const std::size_t e = tok.find_last_not_of(" \t");
    if (b != std::string::npos) out.push_back(tok.substr(b, e - b + 1));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

int cmdAnalyze(const std::filesystem::path& experimentToml, const std::filesystem::path& root,
               const std::string& metricsOverride) {
  pitchlab::CompileOutcome outcome;
  try {
    const pitchlab::EngineRegistry registry = productionRegistry();
    const pitchlab::HarnessConfig harnessCfg = pitchlab::loadHarnessConfig(root);
    outcome = pitchlab::compileExperiment(experimentToml, registry, root, root / "artifacts",
                                          harnessCfg);
  } catch (const pitchlab::ConfigError& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "pitchlab analyze: unexpected error: %s\n", e.what());
    return 1;
  }

  // Metric selection: --metrics overrides the experiment [analysis].metrics
  // (§10.4 item 20). Empty selection => CONFIG ERROR (nothing to analyse).
  std::vector<std::string> selected;
  if (!metricsOverride.empty()) {
    selected = splitMetrics(metricsOverride);
  } else if (!outcome.jobs.empty()) {
    selected = outcome.jobs[0].analysisMetrics;
  }
  if (selected.empty()) {
    std::fprintf(stderr,
                 "CONFIG ERROR: no metrics selected — the experiment needs "
                 "[analysis].metrics or the CLI needs --metrics\n");
    return 2;
  }

  pitchlab::analysis::MetricRegistry metricRegistry;
  pitchlab::analysis::registerProductionMetrics(metricRegistry);
  for (const std::string& id : selected) {
    if (metricRegistry.findById(id) == nullptr) {
      std::fprintf(stderr, "CONFIG ERROR [analysis.metrics] field '%s': unknown metric id "
                           "(identity lives in code, src/analysis/metric_registry.cpp)\n",
                   id.c_str());
      return 2;
    }
  }

  try {
    const pitchlab::analysis::MetricConfig metricConfig =
        pitchlab::analysis::loadMetricConfig(root);
    std::printf("analysing %zu job(s), %zu metric(s)...\n", outcome.jobs.size(), selected.size());
    const pitchlab::analysis::AnalyzeOutcome result =
        pitchlab::analysis::analyzeJobs(outcome.jobs, selected, metricRegistry, metricConfig);
    for (const pitchlab::analysis::JobAnalysisOutcome& j : result.jobs) {
      if (!j.completed) {
        std::printf("ERROR %s / %s / %s / %s: %s\n", j.experimentId.c_str(), j.engineId.c_str(),
                    j.assetId.c_str(), j.curveId.c_str(), j.error.c_str());
        continue;
      }
      std::printf("DONE  %s / %s / %s / %s: %d result(s) [%s]\n      artifact: %s\n",
                  j.experimentId.c_str(), j.engineId.c_str(), j.assetId.c_str(),
                  j.curveId.c_str(), j.resultCount,
                  j.statusSummary.empty() ? "" : j.statusSummary[0].c_str(),
                  j.artifactPath.string().c_str());
    }
    const int errors = result.totalErrors();
    std::printf("%zu job(s): %d with analysis errors, %zu analysed\n", result.jobs.size(), errors,
                result.jobs.size() -
                    static_cast<std::size_t>(
                        std::count_if(result.jobs.begin(), result.jobs.end(),
                                      [](const pitchlab::analysis::JobAnalysisOutcome& j) {
                                        return !j.completed;
                                      })));
    return errors == 0 ? 0 : 1;
  } catch (const pitchlab::ConfigError& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "pitchlab analyze: unexpected error: %s\n", e.what());
    return 1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "version") == 0)) {
    return cmdVersion();
  }
  if (argc == 2 && std::strcmp(argv[1], "engines") == 0) {
    return cmdEngines();
  }
  if (argc >= 3 && std::strcmp(argv[1], "compile") == 0 && argv[2][0] != '-') {
    return cmdCompile(std::filesystem::path(argv[2]), extractRoot(argc, argv, argc - 3));
  }
  if (argc >= 3 && std::strcmp(argv[1], "render") == 0 && argv[2][0] != '-') {
    return cmdRender(std::filesystem::path(argv[2]), extractRoot(argc, argv, argc - 3));
  }
  if (argc >= 3 && std::strcmp(argv[1], "analyze") == 0 && argv[2][0] != '-') {
    return cmdAnalyze(std::filesystem::path(argv[2]), extractRoot(argc, argv, argc - 3),
                      extractMetricsOption(argc, argv, argc - 3));
  }
  if (argc == 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "help") == 0)) {
    printHelp();
    return 0;
  }
  std::fprintf(stderr,
               "pitchlab: '%s' is not implemented yet\n"
               "(v0.1: report|verify|listen-index are step 6 — see "
               "research/pitch-lab-v0.1-implementation-specification.md §17)\n",
               argc > 1 ? argv[1] : "");
  return 2;
}
