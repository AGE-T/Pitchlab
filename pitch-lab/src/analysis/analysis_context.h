#pragma once

// Pitch Lab — AnalysisContext assembly (implementation specification §10.4
// item 5, frozen cycle 5 BEFORE coding).
//
// The AnalysisContext is EVERYTHING a metric module may see: manifest-derived
// render accounting, corpus asset metadata, the hash-verified recompiled
// effective curve, the emission-map reconstruction inputs, the tolerances and
// the loaded signals (master / input corpus asset / reference master). Metric
// modules are pure functions of this context (§4.10); they never see engines,
// the registry, or harness internals.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "analysis/spectral.h"
#include "core/curve.h"
#include "core/types.h"
#include "core/wav_io.h"
#include "harness/json_writer.h"

namespace pitchlab::analysis {

/// The §15.2 tolerance set (all PROVISIONAL; loaded from
/// config/tolerances.toml; missing entries fall back to the committed
/// defaults with no error — the file may grow, §15.2).
struct Tolerances {
  double blockBoundaryDbfs = -80.0;
  double ratio1IdentityDbfs = -80.0;
  int64_t lengthRateFollowingFrames = 4096;
  double latencyDeclaredVsMeasuredMs = 1.0;
  double pitchErrorMedianCents = 25.0;
  double regressionBandRelative = 0.20;
};

/// config/metrics.toml enable flags (underscore keys ↔ §10.1 dash ids).
struct MetricConfig {
  bool enabled[15] = {true, true, true, true, true, true, true, true,
                      true, true, true, true, true, true, true};
};

/// Corpus asset metadata (§15.5 — category + bandContentHz are the fields
/// the metrics consume).
struct AssetMeta {
  std::string assetId;
  std::string category;
  bool hasBand = false;
  double bandLo = 0.0;
  double bandHi = 0.0;
};

struct DeclaredLatency {
  int64_t inputFrames = 0;
  int64_t outputFrames = 0;
};

/// Result of the hash-verified curve recompilation (§10.4 item 5).
struct CurveRecompileResult {
  PitchCurveSpec spec;
  PitchCurveSignal effective;   // effective signal (saturated or identical)
  std::string requestedHash;   // sha256 of the recompiled requested signal
  std::string effectiveHash;   // sha256 of the effective signal
  double effectiveMin = 1.0;
  double effectiveMax = 1.0;
};

/// Loaders (ConfigError on malformed/unknown keys; missing files are the
/// caller's analysis-error).
[[nodiscard]] Tolerances loadTolerances(const std::filesystem::path& pitchlabRoot);
[[nodiscard]] MetricConfig loadMetricConfig(const std::filesystem::path& pitchlabRoot);
[[nodiscard]] AssetMeta loadAssetMeta(const std::filesystem::path& pitchlabRoot,
                                      const std::string& assetId);

/// Recompile the curve from the authored spec at (fs, N_in) — deterministically
/// — and derive the effective signal (element-wise clamp when `saturate`, per
/// the manifest's derivation note). Hashes are computed with the manifest
/// convention (sha256Doubles, little-endian double array bytes).
[[nodiscard]] CurveRecompileResult recompileCurveVerified(
    const std::filesystem::path& pitchlabRoot, const std::string& specId, double sampleRate,
    int64_t totalFrames, double engineMinRatio, double engineMaxRatio, bool saturate);

/// One loaded signal (master of the analysed job / reference master / the
/// input corpus asset).
struct Signal {
  WavData wav;                        // planar channels + metadata
  std::string relPath;                // pitchlabRoot-relative (provenance)
  bool loaded = false;
};

/// The fully-assembled per-job analysis context (§10.4 item 5).
struct AnalysisContext {
  // Job identity (manifest + experiment).
  std::string experimentId;
  std::string experimentKind;
  std::string experimentFile;
  std::string engineId;
  std::string assetId;
  std::string curveId;
  uint32_t sampleRate = 0;
  int channels = 0;
  int64_t inputFrames = 0;

  // Manifest mirrors (read-only; the manifest is never authority for engine
  // identity — these are provenance fields the metrics legitimately read).
  std::string durationBehaviour;  // "Preserving" | "RateFollowing"
  json::Value engineParameters;   // object mirror (AM expected rates)
  double engineMinRatio = 0.0;
  double engineMaxRatio = 0.0;
  DeclaredLatency declaredLatency;
  int64_t expectedOutputFrames = 0;
  int64_t actualOutputFrames = 0;
  std::string renderStatus;
  std::vector<std::string> taints;
  bool lengthPolicyTaint = false;
  std::string manifestRelPath;
  std::string masterRelPath;

  // Corpus asset metadata.
  AssetMeta asset;

  // Curve: the hash-verified recompiled EFFECTIVE signal + spec kind.
  std::vector<double> effectiveRatio;
  double effectiveMin = 1.0;
  double effectiveMax = 1.0;
  std::string curveKind;
  bool curveStatic = false;

  // Config.
  Tolerances tolerances;

  // Signals.
  Signal output;      // the analysed job's master
  Signal input;       // the corpus input asset
  Signal reference;   // varispeed reference master (loaded = false when none)
  bool hasReference = false;
  std::string referenceManifestRelPath;

  // Emission-map reconstruction (RateFollowing only; empty otherwise).
  std::unique_ptr<EmissionMap> emissionMap;
  // The REFERENCE render's own emission map (varispeed is RateFollowing; its
  // effective curve may differ from the analysed job's when saturation
  // ranges differ — the reference is rendered per its own rules).
  std::unique_ptr<EmissionMap> referenceEmissionMap;

  [[nodiscard]] bool isRateFollowing() const {
    return durationBehaviour == "RateFollowing";
  }
};

}  // namespace pitchlab::analysis
