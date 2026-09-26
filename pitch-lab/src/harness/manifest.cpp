#include "harness/manifest.h"

#include <filesystem>
#include <fstream>

#include "core/version.h"
#include "harness/paths.h"

namespace pitchlab {
namespace {

const char* usageClassName(UsageClass c) {
  switch (c) {
    case UsageClass::Prototype: return "Prototype";
    case UsageClass::BenchmarkOnly: return "BenchmarkOnly";
    case UsageClass::ExternalIntegration: return "ExternalIntegration";
    case UsageClass::ExternalBenchmark: return "ExternalBenchmark";
    case UsageClass::ResearchReference: return "ResearchReference";
    case UsageClass::Rejected: return "Rejected";
    case UsageClass::Future: return "Future";
  }
  return "?";
}

const char* channelModeName(ChannelMode m) {
  switch (m) {
    case ChannelMode::Mono: return "Mono";
    case ChannelMode::Stereo: return "Stereo";
    case ChannelMode::MonoAndStereo: return "MonoAndStereo";
    case ChannelMode::MultiChannel: return "MultiChannel";
  }
  return "?";
}

const char* durationName(DurationBehaviour b) {
  return b == DurationBehaviour::Preserving ? "Preserving" : "RateFollowing";
}

const char* determinismName(Determinism d) {
  return d == Determinism::Deterministic ? "Deterministic" : "SeededDeterministic";
}

const char* controlRateKindName(ControlRateSpec::Kind k) {
  switch (k) {
    case ControlRateSpec::Kind::PerSample: return "PerSample";
    case ControlRateSpec::Kind::FixedBlock: return "FixedBlock";
    case ControlRateSpec::Kind::EngineEvent: return "EngineEvent";
  }
  return "?";
}

json::Value capabilitiesMirror(const Capabilities& cap) {
  json::Object controlRate;
  controlRate["kind"] = json::Value(controlRateKindName(cap.controlRate.kind));
  controlRate["blockFrames"] = json::Value(static_cast<int64_t>(cap.controlRate.blockFrames));

  json::Object bandwidth;
  bandwidth["nyquistFraction"] = json::Value(cap.bandwidth.nyquistFraction);
  bandwidth["notes"] = json::Value(cap.bandwidth.notes);

  json::Array rates;
  for (uint32_t r : cap.supportedSampleRates) {
    rates.push_back(json::Value(static_cast<int64_t>(r)));
  }

  json::Object c;
  c["minRatio"] = json::Value(cap.minRatio);
  c["maxRatio"] = json::Value(cap.maxRatio);
  c["supportsDynamicRatio"] = json::Value(cap.supportsDynamicRatio);
  c["controlRate"] = json::Value(std::move(controlRate));
  c["channelMode"] = json::Value(channelModeName(cap.channelMode));
  c["maxChannels"] = json::Value(static_cast<int64_t>(cap.maxChannels));
  c["bandwidth"] = json::Value(std::move(bandwidth));
  c["duration"] = json::Value(durationName(cap.duration));
  c["determinism"] = json::Value(determinismName(cap.determinism));
  c["supportedSampleRates"] = json::Value(std::move(rates));
  return json::Value(std::move(c));
}

const char* experimentKindName(ExperimentKind k) {
  return k == ExperimentKind::Benchmark ? "benchmark" : "creative";
}

}  // namespace

json::Value buildJobManifest(const RenderJob& job, const EngineDescriptor& descriptor,
                             const JobResult& result, const std::string& inputWavSha256Hex,
                             const std::string& requestedSignalSha256Hex,
                             const std::string& effectiveSignalSha256Hex, double requestedMin,
                             double requestedMax, double effectiveMin, double effectiveMax,
                             bool masterWritten, bool listeningWritten) {
  const RenderPaths paths = renderPathsFor(job);

  // ---- engine block (registry read-only mirror, §4.6) ----
  json::Object engine;
  engine["id"] = json::Value(descriptor.info.id);
  engine["version"] = json::Value(descriptor.info.version);
  engine["displayName"] = json::Value(descriptor.info.displayName);
  engine["usageClass"] = json::Value(usageClassName(descriptor.info.usageClass));
  engine["origin"] = json::Value(descriptor.info.origin);
  engine["license"] = json::Value(descriptor.info.license);
  engine["isReferenceRole"] = json::Value(descriptor.isReferenceRole);
  engine["capabilities"] = capabilitiesMirror(descriptor.capabilities);

  // Canonical engine configuration (§4.5: sorted keys, serialised verbatim).
  const json::Value configJson = engineConfigToJson(job.engineConfig);
  engine["parameters"] = configJson.asObject().at("parameters");
  engine["seed"] = json::Value(static_cast<int64_t>(job.engineConfig.seed));

  // ---- input block ----
  json::Object input;
  input["assetId"] = json::Value(job.assetId);
  input["file"] = json::Value(job.inputWavRel);
  input["sha256"] = json::Value(inputWavSha256Hex);
  input["sampleRate"] = json::Value(static_cast<int64_t>(job.sampleRate));
  input["channels"] = json::Value(static_cast<int64_t>(job.channels));
  input["frames"] = json::Value(job.inputFrames);

  // ---- curve block (requested vs effective, §4.4.6 vocabulary) ----
  json::Object requested;
  requested["specId"] = json::Value(job.requestedSpec.id);
  requested["sha256"] = json::Value(requestedSignalSha256Hex);
  requested["min"] = json::Value(requestedMin);
  requested["max"] = json::Value(requestedMax);

  json::Object effective;
  effective["sha256"] = json::Value(effectiveSignalSha256Hex);
  effective["min"] = json::Value(effectiveMin);
  effective["max"] = json::Value(effectiveMax);
  if (job.saturated) {
    effective["derivation"] = json::Value("saturated to [min,max] of engine '" + job.engineId +
                                          "'");
  } else {
    effective["derivation"] = json::Value("identity");
  }

  json::Object access;
  access["minIndexRequested"] = json::Value(result.curveMinIndexRequested);
  access["maxIndexRequested"] = json::Value(result.curveMaxIndexRequested);
  access["clampCount"] = json::Value(result.curveClampCount);

  json::Object curve;
  curve["requested"] = json::Value(std::move(requested));
  curve["effective"] = json::Value(std::move(effective));
  curve["access"] = json::Value(std::move(access));

  // ---- render block (frame accounting, §4.3) ----
  json::Object blockSchedule;
  blockSchedule["kind"] =
      json::Value(job.blockSchedule.kind == BlockSchedule::Kind::Constant ? "constant" : "pattern");
  json::Array schedFrames;
  for (int64_t f : job.blockSchedule.frames) {
    schedFrames.push_back(json::Value(f));
  }
  blockSchedule["frames"] = json::Value(std::move(schedFrames));

  json::Object declaredLatency;
  declaredLatency["inputFrames"] = json::Value(result.declaredLatency.inputLatencyFrames);
  declaredLatency["outputFrames"] = json::Value(result.declaredLatency.outputLatencyFrames);

  json::Object lengthPolicy;
  lengthPolicy["status"] = json::Value(result.lengthPolicyTaint ? "taint" : "ok");
  lengthPolicy["toleranceFrames"] = json::Value(result.lengthToleranceFrames);

  json::Object render;
  render["sampleRate"] = json::Value(static_cast<int64_t>(job.sampleRate));
  render["channels"] = json::Value(static_cast<int64_t>(job.channels));
  render["blockSchedule"] = json::Value(std::move(blockSchedule));
  render["declaredLatency"] = json::Value(std::move(declaredLatency));
  render["inputFramesActual"] = json::Value(result.inputFramesActual);
  render["inputFramesConsumed"] = json::Value(result.inputFramesConsumed);
  render["inputPaddingFrames"] = json::Value(result.inputPaddingFrames);
  render["inputExhaustionTransitions"] = json::Value(result.inputExhaustionTransitions);
  render["outputFramesProduced"] = json::Value(result.outputFramesProduced);
  render["flushFrames"] = json::Value(result.flushFrames);
  render["expectedOutputFrames"] = json::Value(result.expectedOutputFrames);
  render["actualOutputFrames"] = json::Value(result.outputFramesProduced);
  render["lengthDeltaFrames"] = json::Value(result.lengthDeltaFrames);
  render["lengthPolicy"] = json::Value(std::move(lengthPolicy));

  // ---- output block ----
  json::Object output;
  if (masterWritten && result.status == RenderStatus::Ok) {
    json::Object master;
    master["file"] = json::Value(paths.relMasterWav.generic_string());
    master["sha256"] = json::Value(result.masterSha256);
    master["format"] = json::Value("float64");
    master["frames"] = json::Value(result.outputFramesProduced);
    output["master"] = json::Value(std::move(master));
  } else {
    output["master"] = json::Value(nullptr);
  }
  if (listeningWritten && result.status == RenderStatus::Ok) {
    json::Object listening;
    listening["file"] = json::Value(paths.relListeningWav.generic_string());
    listening["sha256"] = json::Value("");  // computed only for masters (§9: analysis reads masters)
    listening["format"] = json::Value("float32");
    listening["frames"] = json::Value(result.outputFramesProduced);
    output["listening"] = json::Value(std::move(listening));
  } else {
    output["listening"] = json::Value(nullptr);
  }

  // ---- execution block (status vocabulary §4.4.6) ----
  json::Object execution;
  execution["status"] = json::Value(result.status == RenderStatus::Ok ? "ok" : "failed");
  if (!result.failureReason.empty()) {
    execution["failureReason"] = json::Value(result.failureReason);
  }
  json::Array taints;
  for (const std::string& t : result.taints) {
    taints.push_back(json::Value(t));
  }
  execution["taints"] = json::Value(std::move(taints));
  if (job.kind == ExperimentKind::Creative) {
    execution["saturationStatus"] =
        json::Value(job.saturated ? "RANGE_SATURATED" : "IN_RANGE");
  }  // benchmark jobs carry no saturation status (§4.4.6)

  // ---- build block ----
  json::Object build;
  build["pitchlabVersion"] = json::Value(versionString());
  build["phase"] = json::Value(phaseString());
  build["compiler"] = json::Value(compilerId());

  // ---- experiment block ----
  json::Object experiment;
  experiment["id"] = json::Value(job.experimentId);
  experiment["kind"] = json::Value(experimentKindName(job.kind));
  experiment["file"] = json::Value(job.experimentFile);
  if (!job.analysisMetrics.empty()) {
    json::Array metrics;
    for (const std::string& m : job.analysisMetrics) {
      metrics.push_back(json::Value(m));
    }
    experiment["analysisMetrics"] = json::Value(std::move(metrics));
  }

  json::Object root;
  root["schema"] = json::Value("pitchlab.manifest.v1");
  root["build"] = json::Value(std::move(build));
  root["engine"] = json::Value(std::move(engine));
  root["experiment"] = json::Value(std::move(experiment));
  root["execution"] = json::Value(std::move(execution));
  root["input"] = json::Value(std::move(input));
  root["curve"] = json::Value(std::move(curve));
  root["output"] = json::Value(std::move(output));
  root["render"] = json::Value(std::move(render));
  return json::Value(std::move(root));
}

void writeManifestFile(const std::filesystem::path& path, const json::Value& manifest) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    throw ConfigError(path.string(), "", "cannot create manifest directory: " + ec.message());
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw ConfigError(path.string(), "", "cannot open manifest file for writing");
  }
  out << json::serialize(manifest) << "\n";
  out.flush();
  if (!out) {
    throw ConfigError(path.string(), "", "manifest write failed");
  }
}

}  // namespace pitchlab
