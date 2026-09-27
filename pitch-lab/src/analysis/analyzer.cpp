#include "analysis/analyzer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/errors.h"
#include "core/hash.h"
#include "core/version.h"
#include "core/wav_io.h"
#include "harness/json_reader.h"
#include "harness/json_writer.h"
#include "harness/manifest.h"
#include "harness/paths.h"

namespace pitchlab::analysis {
namespace fs = std::filesystem;

namespace {

// ---- typed manifest access helpers (analysis-error on drift) ----

class ManifestError : public std::runtime_error {
 public:
  explicit ManifestError(const std::string& reason) : std::runtime_error(reason) {}
};

[[nodiscard]] const json::Value& field(const json::Value& v, const char* key,
                                       const char* ctxName) {
  if (v.kind() != json::Value::Kind::Object) {
    throw ManifestError(std::string("manifest node '") + ctxName + "' is not an object");
  }
  const json::Object& o = v.asObject();
  const auto it = o.find(key);
  if (it == o.end()) {
    throw ManifestError(std::string("manifest field missing: ") + ctxName + "." + key);
  }
  return it->second;
}

[[nodiscard]] std::string mfString(const json::Value& v, const char* key, const char* ctxName) {
  const json::Value& f = field(v, key, ctxName);
  if (f.kind() != json::Value::Kind::String) {
    throw ManifestError(std::string("manifest field not a string: ") + ctxName + "." + key);
  }
  return f.asString();
}

[[nodiscard]] int64_t mfInt(const json::Value& v, const char* key, const char* ctxName) {
  const json::Value& f = field(v, key, ctxName);
  if (f.kind() != json::Value::Kind::Int) {
    throw ManifestError(std::string("manifest field not an integer: ") + ctxName + "." + key);
  }
  return f.asInt();
}

[[nodiscard]] double mfDouble(const json::Value& v, const char* key, const char* ctxName) {
  const json::Value& f = field(v, key, ctxName);
  if (f.kind() != json::Value::Kind::Double && f.kind() != json::Value::Kind::Int) {
    throw ManifestError(std::string("manifest field not numeric: ") + ctxName + "." + key);
  }
  return f.kind() == json::Value::Kind::Int ? static_cast<double>(f.asInt()) : f.asDouble();
}

[[nodiscard]] std::vector<std::string> mfStringArray(const json::Value& v, const char* key,
                                                     const char* ctxName) {
  const json::Value& f = field(v, key, ctxName);
  if (f.kind() != json::Value::Kind::Array) {
    throw ManifestError(std::string("manifest field not an array: ") + ctxName + "." + key);
  }
  std::vector<std::string> out;
  for (const json::Value& e : f.asArray()) {
    if (e.kind() != json::Value::Kind::String) {
      throw ManifestError(std::string("manifest array element not a string: ") + ctxName + "." + key);
    }
    out.push_back(e.asString());
  }
  return out;
}

[[nodiscard]] std::string sha256FileHex(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw ManifestError("cannot open file for hashing: " + p.string());
  std::vector<uint8_t> bytes;
  char buf[65536];
  while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
    const std::streamsize got = in.gcount();
    bytes.insert(bytes.end(), reinterpret_cast<const uint8_t*>(buf),
                 reinterpret_cast<const uint8_t*>(buf) + got);
  }
  return sha256Hex(bytes.data(), bytes.size());
}

/// Resolve the reference: a native.varispeed manifest in the same experiment
/// run directory matching (assetId, curve specId, fs, channels) with a
/// successful render (§10.4 item 7).
struct ReferenceFound {
  fs::path manifestPath;
  json::Value manifest;
  fs::path masterWav;
  std::string manifestRel;
  std::string masterRel;
};

[[nodiscard]] bool findReference(const RenderJob& job, ReferenceFound* out) {
  const fs::path refDir =
      job.outputRoot / "renders" / pathSafeRunId(job.experimentId) / "native.varispeed";
  std::error_code ec;
  if (!fs::exists(refDir, ec)) return false;
  // Deterministic scan: collect candidate manifests, lexicographic order.
  std::vector<fs::path> candidates;
  for (const fs::directory_entry& entry : fs::directory_iterator(refDir, ec)) {
    if (!entry.is_directory(ec)) continue;
    for (const fs::directory_entry& f : fs::directory_iterator(entry.path(), ec)) {
      if (f.is_regular_file(ec) && f.path().extension() == ".json" &&
          f.path().string().find(".manifest.json") != std::string::npos) {
        candidates.push_back(f.path());
      }
    }
  }
  std::sort(candidates.begin(), candidates.end());
  for (const fs::path& mp : candidates) {
    json::Value m;
    try {
      m = json::parseFile(mp);
    } catch (const ConfigError&) {
      continue;  // not a parseable manifest — skip (not our reference)
    }
    if (mfString(m, "schema", "root") != "pitchlab.manifest.v1") continue;
    try {
      if (mfString(field(m, "input", "root"), "assetId", "input") != job.assetId) continue;
      if (mfString(field(field(m, "curve", "root"), "requested", "curve"), "specId",
                   "curve.requested") != job.requestedSpec.id) {
        continue;
      }
      if (mfInt(field(m, "render", "root"), "sampleRate", "render") !=
          static_cast<int64_t>(job.sampleRate)) {
        continue;
      }
      if (mfInt(field(m, "render", "root"), "channels", "render") !=
          static_cast<int64_t>(job.channels)) {
        continue;
      }
      if (mfString(field(m, "execution", "root"), "status", "execution") != "ok") continue;
      const json::Value& master = field(field(m, "output", "root"), "master", "output");
      if (master.isNull()) continue;
      const std::string rel = mfString(master, "file", "output.master");
      const fs::path wav = job.pitchlabRoot / rel;
      if (!fs::exists(wav, ec)) continue;
      out->manifestPath = mp;
      out->manifest = std::move(m);
      out->masterWav = wav;
      out->masterRel = rel;
      out->manifestRel = mp.lexically_relative(job.pitchlabRoot).generic_string();
      return true;
    } catch (const ManifestError&) {
      continue;  // malformed candidate — skip
    }
  }
  return false;
}

/// Assemble the per-job context from the manifest + compiled job + files.
/// Throws ManifestError / ConfigError (WAV reader) — the caller maps to a
/// job-level analysis-error.
[[nodiscard]] AnalysisContext assembleContext(const RenderJob& job,
                                              const Tolerances& tolerances) {
  AnalysisContext ctx;
  ctx.tolerances = tolerances;
  ctx.experimentId = job.experimentId;
  ctx.experimentKind = job.kind == ExperimentKind::Benchmark ? "benchmark" : "creative";
  ctx.experimentFile = job.experimentFile;
  ctx.engineId = job.engineId;
  ctx.assetId = job.assetId;
  ctx.curveId = job.requestedSpec.id;
  ctx.manifestRelPath =
      renderPathsFor(job).relManifestJson.generic_string();
  ctx.masterRelPath = renderPathsFor(job).relMasterWav.generic_string();

  const fs::path manifestPath = renderPathsFor(job).manifestJson;
  std::error_code ec;
  if (!fs::exists(manifestPath, ec)) {
    throw ManifestError("missing render manifest: " + manifestPath.string() +
                        " — run 'pitchlab render' first (the analyzer never re-renders)");
  }
  const json::Value m = json::parseFile(manifestPath);
  if (mfString(m, "schema", "root") != "pitchlab.manifest.v1") {
    throw ManifestError("manifest schema is not pitchlab.manifest.v1: " + manifestPath.string());
  }

  // Render must have succeeded (failed jobs write no WAV — nothing to analyse).
  if (mfString(field(m, "execution", "root"), "status", "execution") != "ok") {
    throw ManifestError("render status is not ok (failed render has no master to analyse)");
  }
  ctx.renderStatus = "ok";
  ctx.taints = mfStringArray(field(m, "execution", "root"), "taints", "execution");
  ctx.lengthPolicyTaint =
      std::find(ctx.taints.begin(), ctx.taints.end(), "length-policy") != ctx.taints.end();

  // Engine mirror.
  const json::Value& engine = field(m, "engine", "root");
  ctx.engineId = mfString(engine, "id", "engine");
  ctx.engineParameters = field(engine, "parameters", "engine");
  const json::Value& caps = field(engine, "capabilities", "engine");
  ctx.engineMinRatio = mfDouble(caps, "minRatio", "engine.capabilities");
  ctx.engineMaxRatio = mfDouble(caps, "maxRatio", "engine.capabilities");
  ctx.durationBehaviour = mfString(caps, "duration", "engine.capabilities");

  // Input + render accounting.
  const json::Value& input = field(m, "input", "root");
  ctx.assetId = mfString(input, "assetId", "input");
  ctx.inputFrames = mfInt(input, "frames", "input");
  ctx.sampleRate = static_cast<uint32_t>(mfInt(input, "sampleRate", "input"));
  ctx.channels = static_cast<int>(mfInt(input, "channels", "input"));
  const json::Value& render = field(m, "render", "root");
  ctx.expectedOutputFrames = mfInt(render, "expectedOutputFrames", "render");
  ctx.actualOutputFrames = mfInt(render, "outputFramesProduced", "render");
  {
    const json::Value& lat = field(render, "declaredLatency", "render");
    ctx.declaredLatency.inputFrames = mfInt(lat, "inputFrames", "render.declaredLatency");
    ctx.declaredLatency.outputFrames = mfInt(lat, "outputFrames", "render.declaredLatency");
  }

  // Master WAV: existence + integrity (sha256) + metadata cross-check.
  const json::Value& output = field(m, "output", "root");
  const json::Value& master = field(output, "master", "output");
  if (master.isNull()) {
    throw ManifestError("manifest records no master WAV for an ok render");
  }
  const std::string masterRel = mfString(master, "file", "output.master");
  const fs::path masterWav = job.pitchlabRoot / masterRel;
  if (!fs::exists(masterWav, ec)) {
    throw ManifestError("missing master WAV: " + masterWav.string());
  }
  const std::string masterHash = sha256FileHex(masterWav);
  if (masterHash != mfString(master, "sha256", "output.master")) {
    throw ManifestError("master WAV sha256 mismatch: the artifact changed after render");
  }
  ctx.output.wav = readWav(masterWav);  // §9 reader: full validation + finiteness gate
  ctx.output.relPath = masterRel;
  ctx.output.loaded = true;
  if (ctx.output.wav.meta.sampleRate != ctx.sampleRate ||
      ctx.output.wav.meta.channels != ctx.channels) {
    throw ManifestError("manifest/WAV mismatch: sample rate or channel count differs");
  }
  if (ctx.output.wav.meta.frames != ctx.actualOutputFrames) {
    throw ManifestError("manifest/WAV mismatch: frame count differs (manifest " +
                        std::to_string(ctx.actualOutputFrames) + ", WAV " +
                        std::to_string(ctx.output.wav.meta.frames) + ")");
  }

  // Corpus input asset + metadata.
  ctx.input.wav = readWav(job.inputWavAbs);
  ctx.input.relPath = job.inputWavRel;
  ctx.input.loaded = true;
  if (ctx.input.wav.meta.sampleRate != ctx.sampleRate ||
      ctx.input.wav.meta.channels != ctx.channels) {
    throw ManifestError("input asset rate/channels drift vs the manifest");
  }
  ctx.asset = loadAssetMeta(job.pitchlabRoot, ctx.assetId);

  // Curve recompile + hash verification (§10.4 item 5).
  const json::Value& curve = field(m, "curve", "root");
  const json::Value& requested = field(curve, "requested", "curve");
  const std::string specId = mfString(requested, "specId", "curve.requested");
  if (specId != job.requestedSpec.id) {
    throw ManifestError("manifest curve specId drift: " + specId + " vs compiled " +
                        job.requestedSpec.id);
  }
  const std::string requestedHash = mfString(requested, "sha256", "curve.requested");
  const json::Value& effectiveNode = field(curve, "effective", "curve");
  const std::string effectiveHash = mfString(effectiveNode, "sha256", "curve.effective");
  const std::string derivation = mfString(effectiveNode, "derivation", "curve.effective");
  const bool saturate = derivation != "identity";
  const CurveRecompileResult re = recompileCurveVerified(
      job.pitchlabRoot, specId, static_cast<double>(ctx.sampleRate), ctx.inputFrames,
      ctx.engineMinRatio, ctx.engineMaxRatio, saturate);
  if (re.requestedHash != requestedHash) {
    throw ManifestError("curve drift: recompiled requested signal hash " + re.requestedHash +
                        " != manifest " + requestedHash +
                        " (authored curve or job length changed since the render)");
  }
  if (re.effectiveHash != effectiveHash) {
    throw ManifestError("curve drift: recompiled effective signal hash " + re.effectiveHash +
                        " != manifest " + effectiveHash);
  }
  ctx.effectiveRatio = re.effective.ratios;
  ctx.effectiveMin = re.effectiveMin;
  ctx.effectiveMax = re.effectiveMax;
  ctx.curveKind = re.spec.kind;
  ctx.curveStatic = re.spec.kind == "static";

  // Emission map (RateFollowing only).
  if (ctx.isRateFollowing()) {
    ctx.emissionMap = std::make_unique<EmissionMap>(ctx.effectiveRatio, ctx.inputFrames,
                                                    ctx.actualOutputFrames);
  }

  // Reference resolution (same-experiment varispeed, §10.4 item 7).
  ReferenceFound ref;
  if (findReference(job, &ref)) {
    ctx.hasReference = true;
    ctx.referenceManifestRelPath = ref.manifestRel;
    ctx.reference.wav = readWav(ref.masterWav);
    ctx.reference.relPath = ref.masterRel;
    ctx.reference.loaded = true;
    // The reference's own effective curve + emission map (its saturation
    // range is its own — varispeed's [0.0625, 16]).
    const double refMin = mfDouble(field(field(ref.manifest, "engine", "root"),
                                          "capabilities", "engine"),
                                    "minRatio", "engine.capabilities");
    const double refMax = mfDouble(field(field(ref.manifest, "engine", "root"),
                                          "capabilities", "engine"),
                                    "maxRatio", "engine.capabilities");
    const std::string refDerivation =
        mfString(field(field(ref.manifest, "curve", "root"), "effective", "curve"),
                 "derivation", "curve.effective");
    const CurveRecompileResult refRe = recompileCurveVerified(
        job.pitchlabRoot, specId, static_cast<double>(ctx.sampleRate), ctx.inputFrames,
        refMin, refMax, refDerivation != "identity");
    if (refRe.effectiveHash ==
        mfString(field(field(ref.manifest, "curve", "root"), "effective", "curve"),
                 "sha256", "curve.effective")) {
      const int64_t refFrames = mfInt(field(ref.manifest, "render", "root"),
                                      "outputFramesProduced", "render");
      ctx.referenceEmissionMap = std::make_unique<EmissionMap>(
          refRe.effective.ratios, ctx.inputFrames, refFrames);
    } else {
      // Reference curve drift: keep the reference but without a map — the
      // spectral-error Preserving path degrades honestly (noted by the metric).
      ctx.referenceEmissionMap.reset();
    }
  }
  return ctx;
}

[[nodiscard]] json::Value statusSummaryJson(const std::vector<MetricResult>& results) {
  std::map<std::string, int> counts;
  for (const MetricResult& r : results) {
    ++counts[statusName(r.status)];
  }
  std::string s;
  for (const auto& [name, n] : counts) {
    if (!s.empty()) s += " ";
    s += name + ":" + std::to_string(n);
  }
  return json::Value(s);
}

[[nodiscard]] json::Value resultToJson(const MetricResult& r, const AnalysisContext& ctx) {
  json::Object o;
  o["metricId"] = json::Value(r.metricId);
  o["metricVersion"] = json::Value(static_cast<int64_t>(r.metricVersion));
  o["status"] = json::Value(statusName(r.status));
  o["engineId"] = json::Value(ctx.engineId);
  o["inputAssetId"] = json::Value(ctx.assetId);
  o["curveId"] = json::Value(ctx.curveId);
  o["sampleRate"] = json::Value(static_cast<int64_t>(ctx.sampleRate));
  o["channels"] = json::Value(static_cast<int64_t>(ctx.channels));
  o["renderManifestReference"] = json::Value(ctx.manifestRelPath);
  o["referenceReference"] = ctx.hasReference
      ? json::Value(ctx.referenceManifestRelPath)
      : json::Value(nullptr);
  o["unit"] = json::Value(r.unit);
  o["values"] = json::Value(r.values);
  if (r.hasSampleCount) o["sampleCount"] = json::Value(r.sampleCount);
  if (r.hasFrameCount) o["frameCount"] = json::Value(r.frameCount);
  o["alignment"] = json::Value(r.alignment);
  o["method"] = json::Value(r.method);
  {
    json::Object tol;
    switch (r.tolerance.kind) {
      case ToleranceInfo::Kind::Exact:
        tol["status"] = json::Value("exact");
        break;
      case ToleranceInfo::Kind::Provisional:
        tol["status"] = json::Value("provisional");
        if (r.tolerance.hasValue) tol["value"] = json::Value(r.tolerance.value);
        tol["source"] = json::Value(r.tolerance.source);
        break;
      case ToleranceInfo::Kind::None:
        tol["status"] = json::Value("none");
        break;
    }
    o["tolerance"] = json::Value(std::move(tol));
  }
  {
    json::Array notes;
    for (const std::string& n : r.notes) notes.push_back(json::Value(n));
    o["notes"] = json::Value(std::move(notes));
  }
  if (r.status == Status::AnalysisError && !r.error.empty()) {
    o["error"] = json::Value(r.error);
  }
  return json::Value(std::move(o));
}

}  // namespace

json::Value buildAnalysisArtifact(const AnalysisContext& ctx,
                                  const std::vector<MetricResult>& results,
                                  const std::vector<std::string>& requestedMetrics) {
  json::Object root;
  root["schema"] = json::Value("pitchlab.analysis.v1");
  {
    json::Object build;
    build["pitchlabVersion"] = json::Value(versionString());
    build["phase"] = json::Value(phaseString());
    build["compiler"] = json::Value(compilerId());
    root["build"] = json::Value(std::move(build));
  }
  {
    json::Object experiment;
    experiment["id"] = json::Value(ctx.experimentId);
    experiment["kind"] = json::Value(ctx.experimentKind);
    experiment["file"] = json::Value(ctx.experimentFile);
    root["experiment"] = json::Value(std::move(experiment));
  }
  {
    json::Object render;
    render["manifestFile"] = json::Value(ctx.manifestRelPath);
    render["masterWav"] = json::Value(ctx.masterRelPath);
    render["engineId"] = json::Value(ctx.engineId);
    render["assetId"] = json::Value(ctx.assetId);
    render["curveId"] = json::Value(ctx.curveId);
    render["sampleRate"] = json::Value(static_cast<int64_t>(ctx.sampleRate));
    render["channels"] = json::Value(static_cast<int64_t>(ctx.channels));
    render["renderStatus"] = json::Value(ctx.renderStatus);
    json::Array taints;
    for (const std::string& t : ctx.taints) taints.push_back(json::Value(t));
    render["taints"] = json::Value(std::move(taints));
    root["render"] = json::Value(std::move(render));
  }
  if (ctx.hasReference) {
    json::Object reference;
    reference["manifestFile"] = json::Value(ctx.referenceManifestRelPath);
    reference["masterWav"] = json::Value(ctx.reference.relPath);
    root["reference"] = json::Value(std::move(reference));
  } else {
    root["reference"] = json::Value(nullptr);
  }
  {
    json::Array req;
    for (const std::string& m : requestedMetrics) req.push_back(json::Value(m));
    root["requestedMetrics"] = json::Value(std::move(req));
  }
  {
    json::Array res;
    for (const MetricResult& r : results) res.push_back(resultToJson(r, ctx));
    root["results"] = json::Value(std::move(res));
  }
  root["statusSummary"] = statusSummaryJson(results);
  return json::Value(std::move(root));
}

AnalyzeOutcome analyzeJobs(const std::vector<RenderJob>& jobs,
                           const std::vector<std::string>& selectedMetrics,
                           const MetricRegistry& registry, const MetricConfig& metricConfig) {
  AnalyzeOutcome outcome;
  // Metric identity is authoritative in code: an unknown id is a CONFIG
  // ERROR here as well (defense in depth — the CLI validates first; the
  // analyzer never silently skips an unknown metric, §10.4 items 2/21).
  for (const std::string& id : selectedMetrics) {
    if (registry.indexOf(id) < 0) {
      throw ConfigError("", id, "unknown metric id (identity lives in code, "
                                "src/analysis/metric_registry.cpp)");
    }
  }
  // Selected metrics resolved in REGISTRATION order (deterministic result
  // ordering, §10.4 item 3).
  std::vector<const MetricDescriptor*> ordered;
  for (std::size_t i = 0; i < registry.size(); ++i) {
    const MetricDescriptor& d = registry.at(i);
    if (std::find(selectedMetrics.begin(), selectedMetrics.end(), std::string(d.id)) !=
        selectedMetrics.end()) {
      ordered.push_back(&d);
    }
  }

  const Tolerances tolerances = loadTolerances(jobs.empty() ? fs::current_path() : jobs[0].pitchlabRoot);

  for (const RenderJob& job : jobs) {
    JobAnalysisOutcome jo;
    jo.experimentId = job.experimentId;
    jo.engineId = job.engineId;
    jo.assetId = job.assetId;
    jo.curveId = job.requestedSpec.id;

    AnalysisContext ctx;
    try {
      ctx = assembleContext(job, tolerances);
    } catch (const ManifestError& e) {
      jo.error = e.what();
      outcome.jobs.push_back(std::move(jo));
      continue;
    } catch (const ConfigError& e) {
      jo.error = e.what();
      outcome.jobs.push_back(std::move(jo));
      continue;
    }

    // Execute the metrics.
    std::vector<MetricResult> results;
    for (const MetricDescriptor* d : ordered) {
      MetricResult r;
      if (!metricConfig.enabled[static_cast<std::size_t>(registry.indexOf(d->id))]) {
        r.metricId = d->id;
        r.metricVersion = d->version;
        r.unit = "none";
        r.status = Status::NotApplicable;
        r.notes.push_back("disabled in config/metrics.toml");
      } else {
        try {
          r = d->compute(ctx);
          r.metricId = d->id;
          if (r.metricVersion == 1) r.metricVersion = d->version;
        } catch (const std::exception& e) {
          r = MetricResult{};
          r.metricId = d->id;
          r.metricVersion = d->version;
          r.status = Status::AnalysisError;
          r.error = e.what();
        }
      }
      if (r.status == Status::AnalysisError) ++jo.errorCount;
      results.push_back(std::move(r));
    }
    jo.resultCount = static_cast<int>(results.size());

    // Artifact path + write (§10.4 item 3: artifacts/analysis/<run-id>/
    // <engine-id>/<asset>__<curve>__<fs>__<paramtag>/analysis.json).
    const RenderPaths rp = renderPathsFor(job);
    const fs::path artifactDir = job.outputRoot / "analysis" /
                                 pathSafeRunId(job.experimentId) / job.engineId /
                                 rp.directory.filename();
    const fs::path artifactPath = artifactDir / "analysis.json";
    const json::Value artifact = buildAnalysisArtifact(ctx, results, selectedMetrics);
    std::error_code ec;
    fs::create_directories(artifactDir, ec);
    if (ec) {
      jo.error = "cannot create analysis directory: " + ec.message();
      outcome.jobs.push_back(std::move(jo));
      continue;
    }
    {
      std::ofstream out(artifactPath, std::ios::binary | std::ios::trunc);
      if (!out) {
        jo.error = "cannot open analysis artifact for writing: " + artifactPath.string();
        outcome.jobs.push_back(std::move(jo));
        continue;
      }
      out << json::serialize(artifact) << "\n";
      out.flush();
      if (!out) {
        jo.error = "analysis artifact write failed";
        outcome.jobs.push_back(std::move(jo));
        continue;
      }
    }
    jo.artifactPath = artifactPath;
    jo.completed = true;
    jo.statusSummary.emplace_back(statusSummaryJson(results).asString());
    outcome.jobs.push_back(std::move(jo));
  }
  return outcome;
}

}  // namespace pitchlab::analysis
