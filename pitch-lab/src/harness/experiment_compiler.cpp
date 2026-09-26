#include "harness/experiment_compiler.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

#include "core/hash.h"
#include "core/toml_lite.h"
#include "core/wav_io.h"
#include "harness/paths.h"

namespace pitchlab {
namespace {

namespace toml = pitchlab::toml;

// §8.1 first-class rates (harness-wide; no other rates accepted).
bool isFirstClassRate(int64_t rate) {
  switch (rate) {
    case 44100:
    case 48000:
    case 88200:
    case 96000:
    case 176400:
    case 192000:
      return true;
    default:
      return false;
  }
}

void checkAllowedKeys(const toml::TomlTable& table, const std::set<std::string>& allowed,
                      const std::string& file, const std::string& section) {
  for (const auto& kv : table) {
    if (allowed.count(kv.first) == 0) {
      throw ConfigError(file, section.empty() ? kv.first : section + "." + kv.first,
                        "unknown key '" + kv.first + "' (typo protection, spec §4.8.1 item 1)");
    }
  }
}

std::vector<std::string> readStringArray(const toml::TomlValue& value, const std::string& file,
                                         const std::string& field) {
  const auto& arr = value.asArray(file, field);
  std::vector<std::string> out;
  for (const toml::TomlValue& v : arr) {
    if (!v.is(toml::TomlValue::Kind::String)) {
      throw ConfigError(file, field, "array elements must be strings");
    }
    if (v.str.empty()) {
      throw ConfigError(file, field, "array elements must be non-empty");
    }
    out.push_back(v.str);
  }
  if (out.empty()) {
    throw ConfigError(file, field, "array must not be empty");
  }
  return out;
}

std::vector<int64_t> readIntegerArray(const toml::TomlValue& value, const std::string& file,
                                      const std::string& field) {
  const auto& arr = value.asArray(file, field);
  std::vector<int64_t> out;
  for (const toml::TomlValue& v : arr) {
    if (!v.is(toml::TomlValue::Kind::Integer)) {
      throw ConfigError(file, field, "array elements must be integers");
    }
    out.push_back(v.integer);
  }
  if (out.empty()) {
    throw ConfigError(file, field, "array must not be empty");
  }
  return out;
}

ParameterValue tomlToParameterValue(const std::string& key, const toml::TomlValue& v,
                                    const std::string& file) {
  switch (v.kind) {
    case toml::TomlValue::Kind::String:
      return ParameterValue(v.str);
    case toml::TomlValue::Kind::Integer:
      return ParameterValue(static_cast<int64_t>(v.integer));
    case toml::TomlValue::Kind::Double:
      if (!std::isfinite(v.real)) {
        throw ConfigError(file, key, "parameter value must be finite (§4.5)");
      }
      return ParameterValue(v.real);
    case toml::TomlValue::Kind::Boolean:
      return ParameterValue(v.boolean);
    default:
      throw ConfigError(file, key, "parameter value must be a scalar (string/number/bool)");
  }
}

struct ResolvedAsset {
  std::string id;
  std::filesystem::path wavAbs;
  std::string wavRel;
  uint32_t sampleRate = 0;
  ChannelCount channels = 0;
  FrameCount frames = 0;
};

ResolvedAsset resolveAsset(const std::string& assetId, const std::filesystem::path& root,
                           const std::string& file) {
  const std::filesystem::path dir = root / "assets" / "corpus" / assetId;
  const std::filesystem::path metadataPath = dir / "metadata.toml";
  const std::filesystem::path wavPath = dir / "signal.wav";
  if (!std::filesystem::exists(metadataPath) || !std::filesystem::exists(wavPath)) {
    throw ConfigError(file, "inputs",
                      "corpus asset '" + assetId + "' not found under assets/corpus/ (expected " +
                          metadataPath.string() + " and signal.wav)");
  }

  // The WAV is AUTHORITATIVE (§4.8.1 item 2); metadata is cross-checked.
  const WavData wav = readWav(wavPath);
  ResolvedAsset asset;
  asset.id = assetId;
  asset.wavAbs = wavPath;
  asset.wavRel = "assets/corpus/" + assetId + "/signal.wav";
  asset.sampleRate = wav.meta.sampleRate;
  asset.channels = wav.meta.channels;
  asset.frames = wav.meta.frames;

  const toml::TomlTable meta = toml::parseTomlFile(metadataPath);
  checkAllowedKeys(meta,
                   {"id", "category", "sampleRate", "channels", "durationSec", "sourceDescription",
                    "referenceStatus", "bandContentHz"},
                   metadataPath.string(), "");
  const int64_t metaRate = meta.at("sampleRate").asInteger(metadataPath.string(), "sampleRate");
  const int64_t metaCh = meta.at("channels").asInteger(metadataPath.string(), "channels");
  if (metaRate != static_cast<int64_t>(asset.sampleRate) || metaCh != asset.channels) {
    std::ostringstream oss;
    oss << "corpus integrity mismatch for asset '" << assetId << "': metadata.toml declares ("
        << metaRate << " Hz, " << metaCh << " ch) but signal.wav is (" << asset.sampleRate
        << " Hz, " << asset.channels << " ch)";
    throw ConfigError(file, "inputs", oss.str());
  }
  const std::string& metaId = meta.at("id").asString(metadataPath.string(), "id");
  if (metaId != assetId) {
    throw ConfigError(file, "inputs", "corpus metadata id '" + metaId +
                                          "' does not match directory name '" + assetId + "'");
  }
  return asset;
}

bool engineSupportsRate(const EngineDescriptor& d, uint32_t rate) {
  for (uint32_t r : d.capabilities.supportedSampleRates) {
    if (r == rate) return true;
  }
  return false;
}

bool engineSupportsChannels(const EngineDescriptor& d, int channels) {
  switch (d.capabilities.channelMode) {
    case ChannelMode::Mono:
      return channels == 1;
    case ChannelMode::Stereo:
      return channels == 2;
    case ChannelMode::MonoAndStereo:
      return channels == 1 || channels == 2;
    case ChannelMode::MultiChannel:
      return channels <= d.capabilities.maxChannels;
  }
  return false;
}

double minOf(const std::vector<double>& v) {
  double m = v.front();
  for (double x : v) m = (x < m) ? x : m;
  return m;
}

double maxOf(const std::vector<double>& v) {
  double m = v.front();
  for (double x : v) m = (x > m) ? x : m;
  return m;
}

}  // namespace

HarnessConfig loadHarnessConfig(const std::filesystem::path& pitchlabRoot) {
  HarnessConfig cfg;
  const std::filesystem::path harnessToml = pitchlabRoot / "config" / "harness.toml";
  if (!std::filesystem::exists(harnessToml)) {
    throw ConfigError(harnessToml.string(), "", "config/harness.toml is missing (committed default)");
  }
  const toml::TomlTable harness = toml::parseTomlFile(harnessToml);
  checkAllowedKeys(harness, {"version", "block_frames", "manifest_format", "export"},
                   harnessToml.string(), "");
  const int64_t version = harness.at("version").asInteger(harnessToml.string(), "version");
  if (version != 1) {
    throw ConfigError(harnessToml.string(), "version", "unsupported harness.toml schema version");
  }
  cfg.blockFrames = harness.at("block_frames").asInteger(harnessToml.string(), "block_frames");
  if (cfg.blockFrames <= 0 || cfg.blockFrames > (1 << 20)) {
    throw ConfigError(harnessToml.string(), "block_frames",
                      "block_frames must be a positive frame count");
  }

  const std::filesystem::path tolerancesToml = pitchlabRoot / "config" / "tolerances.toml";
  if (std::filesystem::exists(tolerancesToml)) {
    const toml::TomlTable tol = toml::parseTomlFile(tolerancesToml);
    checkAllowedKeys(tol, {"version", "tolerances"}, tolerancesToml.string(), "");
    if (tol.count("tolerances") != 0) {
      const auto& t = tol.at("tolerances").asTable(tolerancesToml.string(), "tolerances");
      if (t.count("length_rate_following_frames") != 0) {
        cfg.lengthToleranceFrames = t.at("length_rate_following_frames")
                                        .asInteger(tolerancesToml.string(),
                                                   "tolerances.length_rate_following_frames");
        if (cfg.lengthToleranceFrames < 0) {
          throw ConfigError(tolerancesToml.string(), "tolerances.length_rate_following_frames",
                            "tolerance must be >= 0");
        }
      }
    }
  }
  return cfg;
}

CompileOutcome compileExperiment(const std::filesystem::path& experimentToml,
                                 const EngineRegistry& registry,
                                 const std::filesystem::path& pitchlabRoot,
                                 const std::filesystem::path& outputRoot,
                                 const HarnessConfig& harnessCfg) {
  const std::string file = experimentToml.string();
  if (!std::filesystem::exists(experimentToml)) {
    throw ConfigError(file, "", "experiment file does not exist");
  }

  const toml::TomlTable doc = toml::parseTomlFile(experimentToml);
  checkAllowedKeys(doc, {"id", "kind", "suite", "engine_config", "output", "analysis"}, file, "");

  // ---- id + kind (§4.8.1 item 1) ----
  if (doc.count("id") == 0) throw ConfigError(file, "id", "id is required");
  const std::string experimentId = doc.at("id").asString(file, "id");
  if (experimentId.empty()) throw ConfigError(file, "id", "id must be non-empty");
  if (doc.count("kind") == 0) throw ConfigError(file, "kind", "kind is required (benchmark|creative)");
  const std::string kindStr = doc.at("kind").asString(file, "kind");
  ExperimentKind kind;
  if (kindStr == "benchmark") {
    kind = ExperimentKind::Benchmark;
  } else if (kindStr == "creative") {
    kind = ExperimentKind::Creative;
  } else {
    throw ConfigError(file, "kind", "kind must be 'benchmark' or 'creative'");
  }
  const std::string runId = pathSafeRunId(experimentId);

  // ---- [suite] (required arrays; first-class rates; channels 1|2) ----
  if (doc.count("suite") == 0) throw ConfigError(file, "suite", "[suite] table is required");
  const auto& suite = doc.at("suite").asTable(file, "suite");
  checkAllowedKeys(suite, {"inputs", "curves", "engines", "sampleRates", "channels"}, file, "suite");
  const std::vector<std::string> inputs = readStringArray(suite.at("inputs"), file, "suite.inputs");
  const std::vector<std::string> curves = readStringArray(suite.at("curves"), file, "suite.curves");
  const std::vector<std::string> engines = readStringArray(suite.at("engines"), file, "suite.engines");
  const std::vector<int64_t> rates = readIntegerArray(suite.at("sampleRates"), file, "suite.sampleRates");
  const std::vector<int64_t> channelsList =
      readIntegerArray(suite.at("channels"), file, "suite.channels");
  for (int64_t rate : rates) {
    if (!isFirstClassRate(rate)) {
      throw ConfigError(file, "suite.sampleRates",
                        "rate " + std::to_string(rate) +
                            " is not first-class (spec §8.1: 44100/48000/88200/96000/176400/192000)");
    }
  }
  for (int64_t ch : channelsList) {
    if (ch != 1 && ch != 2) {
      throw ConfigError(file, "suite.channels",
                        "channels must be 1 or 2 in v0.1 (OD-11: >2-channel depth is open)");
    }
  }

  // ---- [[engine_config]] (exactly one per suite engine; §4.8.1 item 1) ----
  std::map<std::string, EngineConfiguration> engineConfigs;
  {
    const toml::TomlValue* configsValue = nullptr;
    if (doc.count("engine_config") != 0) {
      configsValue = &doc.at("engine_config");
    }
    std::vector<toml::TomlTable> configTables;
    if (configsValue != nullptr) {
      const auto& arr = configsValue->asArray(file, "engine_config");
      for (const toml::TomlValue& v : arr) {
        configTables.push_back(v.asTable(file, "engine_config"));
      }
    }
    std::set<std::string> seen;
    for (const toml::TomlTable& t : configTables) {
      checkAllowedKeys(t, {"engine", "params", "seed"}, file, "engine_config");
      const std::string engine = t.at("engine").asString(file, "engine_config.engine");
      if (std::find(engines.begin(), engines.end(), engine) == engines.end()) {
        throw ConfigError(file, "engine_config.engine",
                          "engine_config references '" + engine +
                              "' which is not listed in [suite].engines");
      }
      if (!seen.insert(engine).second) {
        throw ConfigError(file, "engine_config",
                          "duplicate engine_config for engine '" + engine + "'");
      }
      EngineConfiguration cfg;
      if (t.count("params") != 0) {
        const auto& params = t.at("params").asTable(file, "engine_config.params");
        for (const auto& kv : params) {
          cfg.parameters.emplace_back(kv.first, tomlToParameterValue(kv.first, kv.second, file));
        }
      }
      if (t.count("seed") == 0) {
        throw ConfigError(file, "engine_config.seed",
                          "seed is REQUIRED for engine '" + engine + "' (§4.5)");
      }
      const int64_t seed = t.at("seed").asInteger(file, "engine_config.seed");
      if (seed < 0) {
        throw ConfigError(file, "engine_config.seed", "seed must be an integer >= 0 (uint64 range)");
      }
      cfg.seed = static_cast<uint64_t>(seed);
      // §4.5: parameters sorted by key (canonical form).
      std::sort(cfg.parameters.begin(), cfg.parameters.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
      engineConfigs.emplace(engine, std::move(cfg));
    }
    for (const std::string& engine : engines) {
      if (engineConfigs.count(engine) == 0) {
        throw ConfigError(file, "engine_config",
                          "no engine_config for suite engine '" + engine + "' (§4.8.1 item 1)");
      }
    }
  }

  // ---- resolve engines against the registry (the ONLY engine authority) ----
  std::map<std::string, const EngineDescriptor*> descriptors;
  for (const std::string& engine : engines) {
    const EngineDescriptor* d = registry.findById(engine);
    if (d == nullptr) {
      throw ConfigError(file, "suite.engines",
                        "unknown engine id '" + engine + "' (not in the registry; §4.8)");
    }
    // §4.5 key-set validation (typo protection) against the descriptor.
    const EngineConfiguration& cfg = engineConfigs.at(engine);
    std::set<std::string> allowedKeys;
    for (const char* k : d->parameterKeys) allowedKeys.insert(k);
    for (const auto& kv : cfg.parameters) {
      if (allowedKeys.count(kv.first) == 0) {
        throw ConfigError(file, kv.first, "unknown parameter key '" + kv.first +
                                              "' for engine '" + engine + "' (§4.5)");
      }
    }
    descriptors.emplace(engine, d);
  }

  // ---- [output] / [analysis] (defaults per §4.8.1 item 1) ----
  bool wantMaster = true;
  bool wantListening = false;
  if (doc.count("output") != 0) {
    const auto& out = doc.at("output").asTable(file, "output");
    checkAllowedKeys(out, {"master", "listening"}, file, "output");
    if (out.count("master") != 0) wantMaster = out.at("master").asBoolean(file, "output.master");
    if (out.count("listening") != 0) {
      wantListening = out.at("listening").asBoolean(file, "output.listening");
    }
  }
  std::vector<std::string> analysisMetrics;
  if (doc.count("analysis") != 0) {
    const auto& an = doc.at("analysis").asTable(file, "analysis");
    checkAllowedKeys(an, {"metrics"}, file, "analysis");
    if (an.count("metrics") != 0) {
      analysisMetrics = readStringArray(an.at("metrics"), file, "analysis.metrics");
    }
  }

  // ---- resolve assets + curves ----
  std::vector<ResolvedAsset> assets;
  for (const std::string& input : inputs) {
    assets.push_back(resolveAsset(input, pitchlabRoot, file));
  }
  std::vector<PitchCurveSpec> curveSpecs;
  {
    const std::filesystem::path curvesDir = pitchlabRoot / "experiments" / "curves";
    for (const std::string& curveId : curves) {
      const std::filesystem::path curveFile = curvesDir / (curveId + ".toml");
      if (!std::filesystem::exists(curveFile)) {
        throw ConfigError(file, "suite.curves",
                          "curve '" + curveId + "' not found (expected " + curveFile.string() + ")");
      }
      curveSpecs.push_back(parseCurveSpec(curveFile));
    }
  }

  // ---- job expansion (deterministic order; visible skips) ----
  CompileOutcome outcome;
  // Curve-signal cache: identical (specId, fs, N_in) compiled once (immutable
  // shared storage — content-identical by §4.4.3.1 determinism).
  std::map<std::string, std::shared_ptr<const PitchCurveSignal>> requestedCache;

  for (const ResolvedAsset& asset : assets) {
    for (int64_t rate : rates) {
      for (int64_t channels : channelsList) {
        for (const PitchCurveSpec& spec : curveSpecs) {
          for (const std::string& engine : engines) {
            const EngineDescriptor& d = *descriptors.at(engine);
            const std::string pairing = asset.id + " @ " + std::to_string(asset.sampleRate) +
                                        "Hz/" + std::to_string(asset.channels) + "ch, curve '" +
                                        spec.id + "', engine '" + engine + "'";

            if (static_cast<int64_t>(asset.sampleRate) != rate) {
              SkipEntry skip{asset.id,   spec.id, engine, static_cast<uint32_t>(rate),
                             (int)channels, "rate-mismatch",
                             pairing + ": asset is authored at " +
                                 std::to_string(asset.sampleRate) + " Hz (no silent resampling, §8.4)"};
              outcome.skips.push_back(std::move(skip));
              continue;
            }
            if (static_cast<int64_t>(asset.channels) != channels) {
              SkipEntry skip{asset.id, spec.id, engine, static_cast<uint32_t>(rate), (int)channels,
                             "channel-mismatch",
                             pairing + ": asset has " + std::to_string(asset.channels) +
                                 " channels (no silent channel adaptation)"};
              outcome.skips.push_back(std::move(skip));
              continue;
            }
            if (!engineSupportsRate(d, asset.sampleRate)) {
              SkipEntry skip{asset.id, spec.id, engine, static_cast<uint32_t>(rate), (int)channels,
                             "sample-rate-unsupported",
                             pairing + ": engine does not declare support for " +
                                 std::to_string(asset.sampleRate) + " Hz (§8.1)"};
              outcome.skips.push_back(std::move(skip));
              continue;
            }
            if (!engineSupportsChannels(d, channels)) {
              SkipEntry skip{asset.id, spec.id, engine, static_cast<uint32_t>(rate), (int)channels,
                             "channels-unsupported",
                             pairing + ": engine channel mode does not cover " +
                                 std::to_string(channels) + " channels"};
              outcome.skips.push_back(std::move(skip));
              continue;
            }

            // Compile (or reuse) the REQUESTED curve signal for this job.
            const std::string cacheKey = spec.id + "|" + std::to_string(asset.sampleRate) + "|" +
                                         std::to_string(asset.frames);
            std::shared_ptr<const PitchCurveSignal> requested;
            const auto it = requestedCache.find(cacheKey);
            if (it != requestedCache.end()) {
              requested = it->second;
            } else {
              requested = std::make_shared<const PitchCurveSignal>(
                  compileCurveSignal(spec, static_cast<double>(asset.sampleRate), asset.frames));
              requestedCache.emplace(cacheKey, requested);
            }

            // Range check + saturation (§4.4.6).
            const double curveMin = minOf(requested->ratios);
            const double curveMax = maxOf(requested->ratios);
            const bool outOfRange =
                curveMin < d.capabilities.minRatio || curveMax > d.capabilities.maxRatio;
            if (kind == ExperimentKind::Benchmark && outOfRange) {
              SkipEntry skip{asset.id, spec.id, engine, static_cast<uint32_t>(rate), (int)channels,
                             "ratio-out-of-range",
                             pairing + ": curve range [" + std::to_string(curveMin) + ", " +
                                 std::to_string(curveMax) + "] outside engine [" +
                                 std::to_string(d.capabilities.minRatio) + ", " +
                                 std::to_string(d.capabilities.maxRatio) +
                                 "] (benchmark: never rendered, never saturated, §4.4.6)"};
              outcome.skips.push_back(std::move(skip));
              continue;
            }
            std::shared_ptr<const PitchCurveSignal> effective = requested;
            bool saturated = false;
            if (kind == ExperimentKind::Creative && outOfRange) {
              PitchCurveSignal sat;
              sat.specId = requested->specId;
              sat.sampleRate = requested->sampleRate;
              sat.totalFrames = requested->totalFrames;
              sat.ratios.reserve(requested->ratios.size());
              for (double v : requested->ratios) {
                sat.ratios.push_back(std::min(
                    std::max(v, d.capabilities.minRatio), d.capabilities.maxRatio));
              }
              effective = std::make_shared<const PitchCurveSignal>(std::move(sat));
              saturated = true;
            }

            RenderJob job;
            job.experimentId = experimentId;
            job.kind = kind;
            job.experimentFile = experimentToml.string();
            job.engineId = engine;
            job.engineConfig = engineConfigs.at(engine);
            job.assetId = asset.id;
            job.inputWavAbs = asset.wavAbs;
            job.inputWavRel = asset.wavRel;
            job.sampleRate = asset.sampleRate;
            job.channels = asset.channels;
            job.inputFrames = asset.frames;
            job.requestedSpec = spec;
            job.requestedSignal = requested;
            job.effectiveSignal = effective;
            job.saturated = saturated;
            job.wantMaster = wantMaster;
            job.wantListening = wantListening;
            job.analysisMetrics = analysisMetrics;
            job.blockSchedule = BlockSchedule::constant(harnessCfg.blockFrames);
            job.pitchlabRoot = pitchlabRoot;
            job.outputRoot = outputRoot;
            (void)runId;  // the renderer derives it via pathSafeRunId (paths.h)
            outcome.jobs.push_back(std::move(job));
          }
        }
      }
    }
  }
  return outcome;
}

}  // namespace pitchlab
