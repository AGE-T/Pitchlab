#include "analysis/analysis_context.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#include "core/errors.h"
#include "core/hash.h"
#include "core/toml_lite.h"

namespace pitchlab::analysis {
namespace fs = std::filesystem;

namespace {

[[nodiscard]] const toml::TomlValue* findTable(const toml::TomlTable& t,
                                               const std::string& key,
                                               const std::string& file) {
  const auto it = t.find(key);
  if (it == t.end() || !it->second.is(toml::TomlValue::Kind::Table)) {
    throw ConfigError(file, key, "expected a [table] section named '" + key + "'");
  }
  return &it->second;
}

}  // namespace

Tolerances loadTolerances(const fs::path& pitchlabRoot) {
  Tolerances t;
  const fs::path file = pitchlabRoot / "config" / "tolerances.toml";
  const toml::TomlTable doc = toml::parseTomlFile(file);
  const toml::TomlValue* tol = findTable(doc, "tolerances", file.string());
  // Allow-list (unknown key => CONFIG ERROR: committed file, typo protection).
  for (const auto& [key, value] : tol->table) {
    if (key == "block_boundary_dbfs") {
      t.blockBoundaryDbfs = value.asDouble(file.string(), key);
    } else if (key == "ratio1_identity_dbfs") {
      t.ratio1IdentityDbfs = value.asDouble(file.string(), key);
    } else if (key == "length_rate_following_frames") {
      t.lengthRateFollowingFrames = value.asInteger(file.string(), key);
    } else if (key == "latency_declared_vs_measured_ms") {
      t.latencyDeclaredVsMeasuredMs = value.asDouble(file.string(), key);
    } else if (key == "pitch_error_median_cents") {
      t.pitchErrorMedianCents = value.asDouble(file.string(), key);
    } else if (key == "regression_band_relative") {
      t.regressionBandRelative = value.asDouble(file.string(), key);
    } else {
      throw ConfigError(file.string(), key, "unknown tolerance key (§15.2 frozen set)");
    }
  }
  return t;
}

MetricConfig loadMetricConfig(const fs::path& pitchlabRoot) {
  MetricConfig cfg;
  const fs::path file = pitchlabRoot / "config" / "metrics.toml";
  const toml::TomlTable doc = toml::parseTomlFile(file);
  // [alignment] — the frozen values are cross-checked (the file declares the
  // axis policy; a drift in the committed file is a CONFIG ERROR, not a
  // silent behaviour change).
  const toml::TomlValue* align = findTable(doc, "alignment", file.string());
  for (const auto& [key, value] : align->table) {
    if (key == "common_axis") {
      const std::string& v = value.asString(file.string(), key);
      if (v != "input-timeline") {
        throw ConfigError(file.string(), key,
                          "common_axis must be 'input-timeline' (architecture §H preamble)");
      }
    } else if (key == "rate_following_warp") {
      const std::string& v = value.asString(file.string(), key);
      if (v != "emission-map") {
        throw ConfigError(file.string(), key, "rate_following_warp must be 'emission-map'");
      }
    } else if (key == "cross_class_warp") {
      const std::string& v = value.asString(file.string(), key);
      if (v != "warp-reference-to-engine-timeline") {
        throw ConfigError(file.string(), key,
                          "cross_class_warp must be 'warp-reference-to-engine-timeline'");
      }
    } else {
      throw ConfigError(file.string(), key, "unknown alignment key (§15.2 frozen set)");
    }
  }
  // [metrics] — underscore spellings of the §10.1 ids, in registry order.
  static const char* kMetricKeys[15] = {
      "pitch_error",       "pitch_lag",      "spectral_error", "transient_preservation",
      "onset_timing",      "phase_coherence", "stereo_coherence", "latency",
      "realised_duration", "cpu_cost",       "peak_rms_crest",  "hf_energy",
      "aliasing_indicator", "amplitude_modulation", "warble_instability",
  };
  const auto it = doc.find("metrics");
  if (it != doc.end()) {
    const toml::TomlValue& m = it->second;
    if (!m.is(toml::TomlValue::Kind::Table)) {
      throw ConfigError(file.string(), "metrics", "expected a [metrics] table");
    }
    for (const auto& [key, value] : m.table) {
      const char* const* found =
          std::find_if(std::begin(kMetricKeys), std::end(kMetricKeys),
                       [&key](const char* mk) { return key == mk; });
      if (found == std::end(kMetricKeys)) {
        throw ConfigError(file.string(), key, "unknown metric enable key (§10.1 id set)");
      }
      const auto index = static_cast<std::size_t>(found - kMetricKeys);
      cfg.enabled[index] = value.asBoolean(file.string(), key);
    }
  }
  return cfg;
}

AssetMeta loadAssetMeta(const fs::path& pitchlabRoot, const std::string& assetId) {
  AssetMeta meta;
  meta.assetId = assetId;
  const fs::path file = pitchlabRoot / "assets" / "corpus" / assetId / "metadata.toml";
  const toml::TomlTable doc = toml::parseTomlFile(file);
  const auto cat = doc.find("category");
  if (cat != doc.end()) {
    meta.category = cat->second.asString(file.string(), "category");
  }
  const auto band = doc.find("bandContentHz");
  if (band != doc.end()) {
    const std::vector<toml::TomlValue>& arr =
        band->second.asArray(file.string(), "bandContentHz");
    if (arr.size() != 2) {
      throw ConfigError(file.string(), "bandContentHz", "expected [lo, hi]");
    }
    meta.bandLo = arr[0].asDouble(file.string(), "bandContentHz");
    meta.bandHi = arr[1].asDouble(file.string(), "bandContentHz");
    if (meta.bandLo < 0.0 || meta.bandHi < meta.bandLo) {
      throw ConfigError(file.string(), "bandContentHz", "expected 0 <= lo <= hi");
    }
    meta.hasBand = true;
  }
  return meta;
}

CurveRecompileResult recompileCurveVerified(const fs::path& pitchlabRoot, const std::string& specId,
                                            double sampleRate, int64_t totalFrames,
                                            double engineMinRatio, double engineMaxRatio,
                                            bool saturate) {
  CurveRecompileResult out;
  const fs::path specFile = pitchlabRoot / "experiments" / "curves" / (specId + ".toml");
  const PitchCurveSpec spec = parseCurveSpec(specFile);
  const PitchCurveSignal requested = compileCurveSignal(spec, sampleRate, totalFrames);
  const std::string requestedHash =
      toHexLower(sha256Doubles(requested.ratios.data(), requested.ratios.size()));

  out.spec = spec;
  out.requestedHash = requestedHash;
  if (!saturate) {
    out.effective = requested;
    out.effectiveHash = requestedHash;
  } else {
    PitchCurveSignal effective = requested;
    double lo = requested.ratios[0];
    double hi = lo;
    for (double& v : effective.ratios) {
      if (v < engineMinRatio) v = engineMinRatio;
      if (v > engineMaxRatio) v = engineMaxRatio;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    effective.specId = requested.specId;
    out.effective = std::move(effective);
    out.effectiveHash = toHexLower(sha256Doubles(out.effective.ratios.data(),
                                                 out.effective.ratios.size()));
  }
  double lo = out.effective.ratios.empty() ? 1.0 : out.effective.ratios[0];
  double hi = lo;
  for (double v : out.effective.ratios) {
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  out.effectiveMin = lo;
  out.effectiveMax = hi;
  return out;
}

}  // namespace pitchlab::analysis
