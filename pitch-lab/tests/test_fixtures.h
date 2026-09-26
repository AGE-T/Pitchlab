#pragma once

// Pitch Lab — shared test fixtures for the cycle-3 harness/engine tests
// (T-E*, T-LEN-CAL). Builds throwaway "pitch-lab roots" (assets/curves/
// experiments/config) in a temp directory, renders through the REAL harness
// (ExperimentCompiler + OfflineRenderer) and reads masters back through the
// §9 reader. Test-only code — never linked into runtime components.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"
#include "core/wav_io.h"
#include "harness/experiment_compiler.h"
#include "harness/offline_renderer.h"

namespace pitchlab::test {

namespace fs = std::filesystem;

/// Deterministic sine synthesis (own, no <random>): phase-accumulated,
/// amplitude 0.25 (≈ −12 dBFS).
inline std::vector<double> sineFrames(double freq, uint32_t fs, int64_t n) {
  std::vector<double> x(static_cast<std::size_t>(n));
  const double w = 2.0 * 3.14159265358979323846 * freq / static_cast<double>(fs);
  double phase = 0.0;
  for (int64_t i = 0; i < n; ++i) {
    x[static_cast<std::size_t>(i)] = 0.25 * std::sin(phase);
    phase += w;
  }
  return x;
}

/// Goertzel-based dominant-frequency estimate, two-stage sweep (test-only
/// pitch probe; deterministic; coarse 2 Hz grid then ±3 Hz refinement at
/// 0.05 Hz — adequate resolution for 25-cent checks around 440–1000 Hz).
inline double dominantFrequency(const std::vector<double>& x, uint32_t fs, int64_t skip,
                                int64_t n) {
  const double pi = 3.14159265358979323846;
  const auto goertzelEnergy = [&](double f) {
    const double coeff = 2.0 * std::cos(2.0 * pi * f / static_cast<double>(fs));
    double s = 0.0, s1 = 0.0, s2 = 0.0;
    for (int64_t i = skip; i < skip + n && i < static_cast<int64_t>(x.size()); ++i) {
      s = x[static_cast<std::size_t>(i)] + coeff * s1 - s2;
      s2 = s1;
      s1 = s;
    }
    return s2 * s2 + s1 * s1 - coeff * s1 * s2;
  };
  double best = 0.0;
  double bestE = -1.0;
  for (double f = 20.0; f < 0.45 * fs; f += 2.0) {
    const double energy = goertzelEnergy(f);
    if (energy > bestE) {
      bestE = energy;
      best = f;
    }
  }
  const double lo = std::max(20.0, best - 3.0);
  const double hi = std::min(0.45 * fs, best + 3.0);
  for (double f = lo; f <= hi; f += 0.05) {
    const double energy = goertzelEnergy(f);
    if (energy > bestE) {
      bestE = energy;
      best = f;
    }
  }
  return best;
}

inline double centsBetween(double fMeasured, double fExpected) {
  return 1200.0 * std::log2(fMeasured / fExpected);
}

/// A throwaway pitch-lab root with corpus assets, curves and experiments.
struct TestRoot {
  fs::path root;
  fs::path artifacts;

  explicit TestRoot(const std::string& name) {
    const fs::path base = fs::temp_directory_path() / ("pitchlab-test-" + name);
    root = base;
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "assets" / "corpus", ec);
    fs::create_directories(root / "experiments" / "curves", ec);
    fs::create_directories(root / "experiments" / "suites", ec);
    fs::create_directories(root / "config", ec);
    artifacts = root / "artifacts";
    // Committed-shape harness + tolerances config (provisional values).
    writeFile(root / "config" / "harness.toml",
              "version = 1\nblock_frames = 4096\nmanifest_format = \"json\"\n[export]\nmaster_"
              "format = \"float64\"\nlistening_format = \"float32\"\n");
    writeFile(root / "config" / "tolerances.toml",
              "version = 1\n[tolerances]\nblock_boundary_dbfs = -80.0\nratio1_identity_dbfs = "
              "-80.0\nlength_rate_following_frames = 4096\nlatency_declared_vs_measured_ms = "
              "1.0\npitch_error_median_cents = 25.0\nregression_band_relative = 0.20\n");
  }

  static void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
  }

  /// Write a corpus asset (mono/stereo planar data, float64 WAV + metadata).
  void makeAsset(const std::string& id, uint32_t fs, const std::vector<std::vector<double>>& ch) const {
    const fs::path dir = root / "assets" / "corpus" / id;
    fs::create_directories(dir);
    std::vector<const double*> ptrs;
    for (const auto& c : ch) ptrs.push_back(c.data());
    writeWav(dir / "signal.wav", ptrs.data(), static_cast<ChannelCount>(ch.size()),
             static_cast<FrameCount>(ch[0].size()), fs, WavSampleFormat::Float64);
    std::string meta = "# test asset\nid = \"" + id + "\"\ncategory = \"sine\"\nsampleRate = " +
                       std::to_string(fs) + "\nchannels = " + std::to_string(ch.size()) +
                       "\ndurationSec = " + std::to_string(static_cast<double>(ch[0].size()) / fs) +
                       "\nsourceDescription = \"test fixture\"\nreferenceStatus = \"test-fixture\"\n";
    writeFile(dir / "metadata.toml", meta);
  }

  void makeCurve(const std::string& id, const std::string& tomlText) const {
    writeFile(root / "experiments" / "curves" / (id + ".toml"), tomlText);
  }

  fs::path makeExperiment(const std::string& id, const std::string& tomlText) const {
    const fs::path p = root / "experiments" / "suites" / (id + ".toml");
    writeFile(p, tomlText);
    return p;
  }

  /// The sealed production registry (native.varispeed only, §14).
  [[nodiscard]] static EngineRegistry productionRegistry() {
    EngineRegistry registry;
    registerProductionEngines(registry);
    registry.seal();
    return registry;
  }

  ~TestRoot() {
    std::error_code ec;
    fs::remove_all(root, ec);  // best-effort cleanup
  }
};

/// Standard benchmark experiment TOML for one (asset, curve) pairing.
inline std::string experimentToml(const std::string& id, const std::string& asset,
                                  const std::string& curve, uint32_t fs, int channels,
                                  const std::string& engine = "native.varispeed",
                                  const std::string& kind = "benchmark",
                                  const std::string& params = "resample_quality = \"reference\"",
                                  const std::string& extraOutput = "") {
  std::string chList = channels == 1 ? "[1]" : (channels == 2 ? "[1, 2]" : "[1, 2]");
  return "id = \"" + id + "\"\nkind = \"" + kind + "\"\n[suite]\ninputs  = [\"" + asset +
         "\"]\ncurves  = [\"" + curve + "\"]\nengines = [\"" + engine + "\"]\nsampleRates = [" +
         std::to_string(fs) + "]\nchannels = " + chList +
         "\n[[engine_config]]\nengine = \"" + engine + "\"\nparams = { " + params +
         " }\nseed = 7\n[output]\nmaster = true\nlistening = false\n" + extraOutput +
         "\n[analysis]\nmetrics = [\"spectral-error\"]\n";
}

/// Compile + render one experiment; returns the summary (jobs all use the
/// production registry unless overridden).
inline RenderSummary renderExperiment(const fs::path& experimentToml, const TestRoot& tr,
                                       const EngineRegistry& registry) {
  const HarnessConfig cfg = loadHarnessConfig(tr.root);
  const CompileOutcome outcome =
      compileExperiment(experimentToml, registry, tr.root, tr.artifacts, cfg);
  return renderJobs(outcome.jobs, registry);
}

/// Direct engine drive (renderer-loop equivalent) for contract-level tests
/// (T-A1 allocation audit, T-D3 reset reuse) that instrument process()/
/// finish() call boundaries. Returns the planar output.
struct DriveReport {
  std::vector<std::vector<double>> output;      // per channel
  int64_t producedTotal = 0;
  int64_t flushFrames = 0;
  int64_t exhaustionTransitions = 0;
  int64_t consumedTotal = 0;
  PitchEngine::Latency latency{};
};

inline DriveReport driveEngine(PitchEngine& engine, const EngineConfiguration& cfg,
                               const std::vector<std::vector<double>>& channels, uint32_t fs,
                               const std::vector<double>& ratio, int64_t blockFrames,
                               int64_t outCapacity,
                               const std::function<void()>& beforeCall = nullptr,
                               const std::function<void()>& afterCall = nullptr) {
  DriveReport dr;
  engine.configure(cfg);
  ProcessContext ctx;
  ctx.sampleRate = static_cast<double>(fs);
  ctx.channels = static_cast<ChannelCount>(channels.size());
  ctx.maxBlockFrames = static_cast<int>(blockFrames);
  ctx.totalInputFrames = static_cast<FrameCount>(channels[0].size());
  PitchCurveView curve{ratio.data(), static_cast<FrameCount>(ratio.size()),
                       static_cast<double>(fs)};
  ctx.curve = &curve;
  engine.prepare(ctx);
  dr.latency = engine.latency();

  const int C = static_cast<int>(channels.size());
  const int64_t streamEnd = ctx.totalInputFrames + dr.latency.inputLatencyFrames;
  std::vector<double> zeroPad(static_cast<std::size_t>(dr.latency.inputLatencyFrames), 0.0);
  std::vector<std::vector<double>> staging(static_cast<std::size_t>(C));
  for (auto& s : staging) s.assign(static_cast<std::size_t>(outCapacity), 0.0);
  dr.output.resize(static_cast<std::size_t>(C));

  std::vector<const double*> inPtrs(static_cast<std::size_t>(C));
  std::vector<double*> outPtrs(static_cast<std::size_t>(C));
  for (int c = 0; c < C; ++c) outPtrs[static_cast<std::size_t>(c)] = staging[static_cast<std::size_t>(c)].data();

  int64_t pos = 0;
  bool exhausted = false;
  while (pos < streamEnd) {
    int64_t take = std::min<int64_t>(blockFrames, streamEnd - pos);
    if (pos < ctx.totalInputFrames) {
      take = std::min(take, ctx.totalInputFrames - pos);
    }
    for (int c = 0; c < C; ++c) {
      inPtrs[static_cast<std::size_t>(c)] = (pos < ctx.totalInputFrames)
          ? channels[static_cast<std::size_t>(c)].data() + pos
          : zeroPad.data() + (pos - ctx.totalInputFrames);
    }
    AudioBlockView in{inPtrs.data(), ctx.channels, take};
    AudioBlockOut out{outPtrs.data(), ctx.channels, outCapacity};
    if (beforeCall) beforeCall();
    const ProcessReport rep = engine.process(in, static_cast<int>(take), out,
                                             static_cast<int>(outCapacity), curve, pos);
    if (afterCall) afterCall();
    pos += rep.inputFramesConsumed;
    if (rep.inputExhausted && !exhausted) {
      exhausted = true;
      ++dr.exhaustionTransitions;
    }
    dr.consumedTotal += rep.inputFramesConsumed;
    if (rep.outputFramesProduced > 0) {
      const std::size_t n = static_cast<std::size_t>(rep.outputFramesProduced);
      for (int c = 0; c < C; ++c) {
        dr.output[static_cast<std::size_t>(c)].insert(
            dr.output[static_cast<std::size_t>(c)].end(),
            staging[static_cast<std::size_t>(c)].begin(),
            staging[static_cast<std::size_t>(c)].begin() + static_cast<std::ptrdiff_t>(n));
      }
      dr.producedTotal += rep.outputFramesProduced;
    }
    REQUIRE(rep.inputFramesConsumed > 0);
    REQUIRE(rep.outputFramesProduced >= 0);
  }
  AudioBlockOut out{outPtrs.data(), ctx.channels, outCapacity};
  if (beforeCall) beforeCall();
  const ProcessReport rep = engine.finish(out, static_cast<int>(outCapacity));
  if (afterCall) afterCall();
  if (rep.inputExhausted && !exhausted) {
    exhausted = true;
    ++dr.exhaustionTransitions;
  }
  dr.flushFrames = rep.outputFramesProduced;
  if (rep.outputFramesProduced > 0) {
    const std::size_t n = static_cast<std::size_t>(rep.outputFramesProduced);
    for (int c = 0; c < C; ++c) {
      dr.output[static_cast<std::size_t>(c)].insert(
          dr.output[static_cast<std::size_t>(c)].end(),
          staging[static_cast<std::size_t>(c)].begin(),
          staging[static_cast<std::size_t>(c)].begin() + static_cast<std::ptrdiff_t>(n));
    }
    dr.producedTotal += rep.outputFramesProduced;
  }
  return dr;
}

}  // namespace pitchlab::test

// NOTE: each including test defines DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN and
// includes <doctest/doctest.h> BEFORE this header (driveEngine uses REQUIRE).
