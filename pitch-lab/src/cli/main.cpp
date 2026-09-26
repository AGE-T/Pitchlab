// Pitch Lab CLI — implementation-phase state (cycle 3: compile + render are
// REAL; analyze/report/verify/listen-index remain honestly unimplemented,
// §17 steps 5-6).
//
// Architecture §B.1 entry points: compile | render | analyze | report |
// verify | listen-index + registry introspection engines. The corpus_gen
// tool is a separate executable (§11).

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "core/engine_registry.h"
#include "core/errors.h"
#include "core/version.h"
#include "harness/experiment_compiler.h"
#include "harness/offline_renderer.h"

namespace {

int cmdVersion() {
  std::printf("pitchlab %s (%s)\ncompiler: %s\nstatus: 1 engine implemented (native.varispeed) — "
              "registry content equals implemented engines\n",
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
      "implemented: --version, engines, compile, render\n"
      "  pitchlab --version                    print version, phase, compiler\n"
      "  pitchlab engines                      list registered engines (authoritative registry)\n"
      "  pitchlab compile <experiment.toml>    validate + resolve jobs (dry run)\n"
      "  pitchlab render  <experiment.toml>    compile + render all jobs\n"
      "    [--root <dir>]                      pitch-lab root (default: working directory)\n"
      "not implemented yet (v0.1 steps 5-6): analyze, report, verify, listen-index\n",
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
  if (argc == 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "help") == 0)) {
    printHelp();
    return 0;
  }
  std::fprintf(stderr,
               "pitchlab: '%s' is not implemented yet\n"
               "(v0.1: analyze|report|verify|listen-index are steps 5-6 — see "
               "research/pitch-lab-v0.1-implementation-specification.md §17)\n",
               argc > 1 ? argv[1] : "");
  return 2;
}
