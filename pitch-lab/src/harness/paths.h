#pragma once

// Pitch Lab — harness output-path layout helpers (implementation
// specification §9 naming + §4.8.1 item 7, frozen cycle 3).
//
// Layout (§9 verbatim):
//   <outputRoot>/renders/<run-id>/<engine-id>/<asset-id>__<curve-id>__<fs>__<paramtag>/<basename>.wav
//   <outputRoot>/renders/<run-id>/<engine-id>/<...>/<basename>.manifest.json
//   <outputRoot>/renders/<run-id>/<engine-id>/<...>/listening/<basename>.f32.wav
// with run-id = path-safe experiment id, paramtag = first 12 hex chars of
// SHA-256 over the canonical engine-config JSON, fs = integer Hz, basename
// = the input WAV's stem.

#include <filesystem>
#include <string>

#include "core/hash.h"
#include "core/pitch_engine.h"
#include "harness/json_writer.h"
#include "harness/render_job.h"

namespace pitchlab {

/// Path-safe run id: keep [A-Za-z0-9._-], replace everything else with '_'.
/// Throws ConfigError when nothing path-safe remains.
[[nodiscard]] inline std::string pathSafeRunId(const std::string& experimentId) {
  std::string out;
  out.reserve(experimentId.size());
  for (char c : experimentId) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-';
    out += ok ? c : '_';
  }
  if (out.empty()) {
    throw ConfigError("", "id", "experiment id has no path-safe characters");
  }
  return out;
}

/// §4.5 canonical engine-config JSON: {"parameters": {...sorted...}, "seed": n}.
[[nodiscard]] inline json::Value engineConfigToJson(const EngineConfiguration& cfg) {
  json::Object params;
  for (const auto& kv : cfg.parameters) {
    const auto& v = kv.second;
    if (const auto* s = std::get_if<std::string>(&v)) {
      params[kv.first] = json::Value(*s);
    } else if (const auto* d = std::get_if<double>(&v)) {
      params[kv.first] = json::Value(*d);
    } else if (const auto* b = std::get_if<bool>(&v)) {
      params[kv.first] = json::Value(*b);
    } else {
      const auto* i = std::get_if<int64_t>(&v);
      params[kv.first] = json::Value(*i);
    }
  }
  json::Object root;
  root["parameters"] = json::Value(std::move(params));
  root["seed"] = json::Value(static_cast<int64_t>(cfg.seed));
  return json::Value(std::move(root));
}

/// §4.8.1 item 7 paramtag: first 12 hex chars of SHA-256 over the canonical
/// engine-config JSON.
[[nodiscard]] inline std::string paramTagOf(const EngineConfiguration& cfg) {
  const std::string canonical = json::serialize(engineConfigToJson(cfg));
  const std::string hex = sha256HexString(canonical);
  return hex.substr(0, 12);
}

/// The output directory + file paths for one job (§9 layout). `basename`
/// comes from the input WAV stem.
struct RenderPaths {
  std::filesystem::path directory;                 // .../<asset>__<curve>__<fs>__<tag>
  std::filesystem::path masterWav;                 // directory/<basename>.wav
  std::filesystem::path listeningWav;              // directory/listening/<basename>.f32.wav
  std::filesystem::path manifestJson;              // directory/<basename>.manifest.json
  std::filesystem::path relDirectory;              // pitchlabRoot-relative directory
  std::filesystem::path relMasterWav;              // root-relative (manifest paths)
  std::filesystem::path relListeningWav;
  std::filesystem::path relManifestJson;
};

[[nodiscard]] inline RenderPaths renderPathsFor(const RenderJob& job) {
  const std::string basename = job.inputWavAbs.stem().string();
  const std::string dirName = job.assetId + "__" + job.requestedSpec.id + "__" +
                              std::to_string(job.sampleRate) + "__" +
                              paramTagOf(job.engineConfig);
  RenderPaths p;
  p.directory = job.outputRoot / "renders" / pathSafeRunId(job.experimentId) / job.engineId / dirName;
  p.masterWav = p.directory / (basename + ".wav");
  p.listeningWav = p.directory / "listening" / (basename + ".f32.wav");
  p.manifestJson = p.directory / (basename + ".manifest.json");

  // Lexical (deterministic) root-relative forms for the manifest.
  const std::filesystem::path base = job.pitchlabRoot;
  p.relDirectory = p.directory.lexically_relative(base);
  p.relMasterWav = p.masterWav.lexically_relative(base);
  p.relListeningWav = p.listeningWav.lexically_relative(base);
  p.relManifestJson = p.manifestJson.lexically_relative(base);
  return p;
}

}  // namespace pitchlab
