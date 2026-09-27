// T-M-D suite (implementation specification §10.4 items 9-10/22, cycle 5,
// §17 step 5): realised-duration + latency goldens through the REAL render
// path, plus the contract-linked edge cases.
//
// Independent analytic anchors (§20-21):
//   * native.varispeed (RateFollowing) at CONSTANT ratio r over N_in frames:
//     expected output = ceil-integration + flush where the read recurrence
//     r(m+1) = r(m) + ratio[clamp(floor(r(m)))] advances by exactly r per
//     output frame => expected = N_in/r + ceil(K/r) with K = 24 (the
//     reference resampler kernel half-width taps, §7.3 frozen preset table).
//     Identity (r = 1): 48 000 + 24 = 48 024. Static 2.0 (r | N_in):
//     24 000 + ceil(24/2) = 24 012.
//   * measuredFlush = actualOutputFrames - (frames covering the real input):
//     identity => 48 024 - 48 000 = 24 == declared (ceil(24/1)); 2.0 =>
//     24 012 - 24 000 = 12 == declared (ceil(24/2)).
//   * pv.classic (Preserving, N = 2048): canonical length N_in + L_out
//     (§4.3.3) = 48 000 + 2 048; measuredFlush == declared == 2 048.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "test_fixtures.h"

using namespace pitchlab;
using namespace pitchlab::test;
using pitchlab::analysis::MetricResult;
using pitchlab::analysis::Status;

namespace {

/// Analyse one experiment job for the given metrics and return its results
/// (parsed from the artifact — the artifact is the analysis output).
struct JobArt {
  bool completed = false;
  std::string error;
  fs::path artifactPath;
  json::Value artifact;
};

[[nodiscard]] JobArt analyseOne(const fs::path& exp, const TestRoot& tr,
                                const std::vector<std::string>& metrics) {
  const pitchlab::analysis::AnalyzeOutcome ao = analyzeExperiment(exp, tr, metrics);
  REQUIRE(ao.jobs.size() == 1);
  JobArt ja;
  ja.completed = ao.jobs[0].completed;
  ja.error = ao.jobs[0].error;
  ja.artifactPath = ao.jobs[0].artifactPath;
  if (ja.completed) {
    ja.artifact = json::parse(readBytes(ja.artifactPath), ja.artifactPath.string());
  }
  return ja;
}

[[nodiscard]] const json::Value& findResult(const json::Value& artifact, const char* metricId) {
  const json::Array& results = artifact.asObject().at("results").asArray();
  for (const json::Value& r : results) {
    if (r.asObject().at("metricId").asString() == metricId) return r;
  }
  throw std::runtime_error(std::string("metric not found in artifact: ") + metricId);
}

}  // namespace

TEST_CASE("T-M-D1: varispeed identity — realised-duration + latency analytic goldens") {
  TestRoot tr{"metric-duration-1"};
  tr.makeCurve("d-identity", "id = \"d-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  tr.makeAsset("d-sine", 48000, {sineFrames(440.0, 48000, 48000)});
  const fs::path exp = tr.makeExperiment(
      "d-exp1", experimentToml("d-exp1", "d-sine", "d-identity", 48000, 1));
  const EngineRegistry engines = TestRoot::productionRegistry();
  const RenderSummary rs = renderExperiment(exp, tr, engines);
  REQUIRE(rs.allOk());
  REQUIRE(rs.results[0].outputFramesProduced == 48024);  // 48000 + K/1, K = 24

  const JobArt ja = analyseOne(exp, tr, {"realised-duration", "latency"});
  REQUIRE(ja.completed);

  SUBCASE("realised-duration") {
    const json::Value& r = findResult(ja.artifact, "realised-duration");
    CHECK(r.asObject().at("status").asString() == "ok");
    const json::Object& v = r.asObject().at("values").asObject();
    CHECK(v.at("inputFrames").asInt() == 48000);
    CHECK(v.at("outputFrames").asInt() == 48024);   // N_in/r + ceil(24/1)
    CHECK(v.at("expectedOutputFrames").asInt() == 48024);
    CHECK(v.at("lengthDeltaFrames").asInt() == 0);
    CHECK(v.at("realisedRatio").asDouble() == doctest::Approx(48024.0 / 48000.0).epsilon(1e-15));
    CHECK(v.at("expectedRatio").asDouble() == doctest::Approx(48024.0 / 48000.0).epsilon(1e-15));
    CHECK(v.at("durationBehaviour").asString() == "RateFollowing");
    CHECK(v.at("inputDurationSec").asDouble() == doctest::Approx(1.0).epsilon(1e-15));
    CHECK(v.at("outputDurationSec").asDouble() ==
          doctest::Approx(48024.0 / 48000.0).epsilon(1e-15));
    // lengthPolicy verdict reported (the renderer's own), tolerance from
    // tolerances.toml (provisional, OD-6 untouched).
    const json::Object& lp = v.at("lengthPolicy").asObject();
    CHECK(lp.at("status").asString() == "ok");
    CHECK(lp.at("toleranceFrames").asInt() == 4096);
    const json::Object& lat = v.at("declaredLatency").asObject();
    CHECK(lat.at("inputFrames").asInt() == 24);
    CHECK(lat.at("outputFrames").asInt() == 24);
    const json::Object& tol = r.asObject().at("tolerance").asObject();
    CHECK(tol.at("status").asString() == "provisional");
    CHECK(tol.at("source").asString() == "length_rate_following_frames");
  }

  SUBCASE("latency — declared vs measured flush") {
    const json::Value& r = findResult(ja.artifact, "latency");
    CHECK(r.asObject().at("status").asString() == "ok");
    const json::Object& v = r.asObject().at("values").asObject();
    CHECK(v.at("declaredInputLatencyFrames").asInt() == 24);
    CHECK(v.at("declaredOutputLatencyFrames").asInt() == 24);  // ceil(K/r_min)
    CHECK(v.at("measuredFlushFrames").asInt() == 24);          // 48024-48000
    CHECK(v.at("deltaDeclaredVsMeasuredFlushFrames").asInt() == 0);
    CHECK(v.at("deltaDeclaredVsMeasuredFlushMs").asDouble() == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(v.at("conformant").asBool() == true);
    // Sine material: the energy detector fires on the input's amplitude
    // ramp-up (theta = 0.25*max; |sin| crosses it within ~5 frames) — the
    // onset-offset component IS measured, and at identity it is 0 exactly
    // (the identity output reproduces the input => identical detector).
    CHECK(v.at("measuredOnsetOffsetFrames").asInt() == 0);
    CHECK(v.at("expectedOnsetFrame").asInt() == v.at("measuredOnsetFrame").asInt());
    // unit + testSignal provenance.
    CHECK(r.asObject().at("unit").asString() == "frames");
    const json::Object& method = r.asObject().at("method").asObject();
    CHECK(method.at("testSignal").asString() == "d-sine");
  }
}

TEST_CASE("T-M-D2: varispeed static 2.0 — N_in/r + ceil(K/r) analytic golden") {
  TestRoot tr{"metric-duration-2"};
  tr.makeCurve("d-two", "id = \"d-two\"\nkind = \"static\"\nvalue = { ratio = 2.0 }\n");
  tr.makeAsset("d-sine", 48000, {sineFrames(440.0, 48000, 48000)});
  const fs::path exp =
      tr.makeExperiment("d-exp2", experimentToml("d-exp2", "d-sine", "d-two", 48000, 1));
  const EngineRegistry engines = TestRoot::productionRegistry();
  const RenderSummary rs = renderExperiment(exp, tr, engines);
  REQUIRE(rs.allOk());
  // r | N_in: 48000/2 = 24000 exact + ceil(24/2) = 12.
  REQUIRE(rs.results[0].outputFramesProduced == 24012);

  const JobArt ja = analyseOne(exp, tr, {"realised-duration", "latency"});
  REQUIRE(ja.completed);
  const json::Value& rd = findResult(ja.artifact, "realised-duration");
  CHECK(rd.asObject().at("status").asString() == "ok");
  const json::Object& v = rd.asObject().at("values").asObject();
  CHECK(v.at("outputFrames").asInt() == 24012);
  CHECK(v.at("expectedOutputFrames").asInt() == 24012);
  CHECK(v.at("lengthDeltaFrames").asInt() == 0);
  CHECK(v.at("realisedRatio").asDouble() == doctest::Approx(24012.0 / 48000.0).epsilon(1e-15));

  const json::Value& lat = findResult(ja.artifact, "latency");
  const json::Object& lv = lat.asObject().at("values").asObject();
  // measuredFlush: 24012 - #{m : r(m) <= 47999} = 24012 - 24000 = 12; the
  // declared output latency is ceil(K/r_min) = ceil(24/2) = 12.
  CHECK(lv.at("declaredOutputLatencyFrames").asInt() == 12);
  CHECK(lv.at("measuredFlushFrames").asInt() == 12);
  CHECK(lv.at("deltaDeclaredVsMeasuredFlushFrames").asInt() == 0);
}

TEST_CASE("T-M-D3: pv.classic identity — Preserving canonical length N_in + L_out") {
  TestRoot tr{"metric-duration-3"};
  tr.makeCurve("d-identity", "id = \"d-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  tr.makeAsset("d-sine", 48000, {sineFrames(440.0, 48000, 48000)});
  const fs::path exp = tr.makeExperiment(
      "d-exp3", experimentToml("d-exp3", "d-sine", "d-identity", 48000, 1, "native.pv.classic",
                               "benchmark", ""));
  const EngineRegistry engines = TestRoot::productionRegistry();
  const RenderSummary rs = renderExperiment(exp, tr, engines);
  REQUIRE(rs.allOk());
  REQUIRE(rs.results[0].outputFramesProduced == 50048);  // 48000 + 2048 (§4.3.3)

  const JobArt ja = analyseOne(exp, tr, {"realised-duration", "latency"});
  REQUIRE(ja.completed);
  const json::Value& rd = findResult(ja.artifact, "realised-duration");
  CHECK(rd.asObject().at("status").asString() == "ok");
  const json::Object& v = rd.asObject().at("values").asObject();
  CHECK(v.at("outputFrames").asInt() == 50048);
  CHECK(v.at("expectedOutputFrames").asInt() == 50048);
  CHECK(v.at("lengthDeltaFrames").asInt() == 0);
  CHECK(v.at("durationBehaviour").asString() == "Preserving");
  // Preserving: exact contract (§4.3.3), no tolerance band.
  const json::Object& tol = rd.asObject().at("tolerance").asObject();
  CHECK(tol.at("status").asString() == "exact");

  const json::Value& lat = findResult(ja.artifact, "latency");
  const json::Object& lv = lat.asObject().at("values").asObject();
  CHECK(lv.at("measuredFlushFrames").asInt() == 2048);   // 50048 - 48000
  CHECK(lv.at("declaredOutputLatencyFrames").asInt() == 2048);
  CHECK(lv.at("deltaDeclaredVsMeasuredFlushFrames").asInt() == 0);
  CHECK(lv.at("conformant").asBool() == true);
}

TEST_CASE("T-M-D4: tainted-input — values still measured, status flagged (unit)") {
  auto ctx = makeCtx({sineFrames(440.0, 48000, 1000)}, 48000);
  ctx.durationBehaviour = "RateFollowing";
  ctx.lengthPolicyTaint = true;
  ctx.tolerances.lengthRateFollowingFrames = 4096;
  const pitchlab::analysis::MetricRegistry registry = productionMetricRegistry();
  const MetricResult r = registry.findById("realised-duration")->compute(ctx);
  CHECK(r.status == Status::TaintedInput);
  CHECK(r.values.at("outputFrames").asInt() == 1000);  // still measured
  CHECK(r.values.at("lengthPolicy").asObject().at("status").asString() == "taint");
  bool noted = false;
  for (const std::string& n : r.notes) {
    if (n.find("OD-6") != std::string::npos) noted = true;
  }
  CHECK(noted);
}

TEST_CASE("T-M-D5: manifest/WAV frame mismatch => analysis-error (unit)") {
  auto ctx = makeCtx({sineFrames(440.0, 48000, 1000)}, 48000);
  ctx.actualOutputFrames = 999;  // manifest says 999, the WAV has 1000
  const pitchlab::analysis::MetricRegistry registry = productionMetricRegistry();
  const MetricResult r = registry.findById("realised-duration")->compute(ctx);
  CHECK(r.status == Status::AnalysisError);
  CHECK(r.error.find("mismatch") != std::string::npos);
}

TEST_CASE("T-M-D6: RateFollowing without an emission map => latency analysis-error (unit)") {
  auto ctx = makeCtx({sineFrames(440.0, 48000, 1000)}, 48000);
  ctx.durationBehaviour = "RateFollowing";
  ctx.emissionMap.reset();  // no reconstruction available
  ctx.actualOutputFrames = 1000;
  const pitchlab::analysis::MetricRegistry registry = productionMetricRegistry();
  const MetricResult r = registry.findById("latency")->compute(ctx);
  CHECK(r.status == Status::AnalysisError);
  CHECK(r.error.find("emission-map") != std::string::npos);
}

TEST_CASE("T-M-D7: declared latency is never presented as measured evidence (schema)") {
  // The latency result carries BOTH declared fields and the measured flush
  // and their delta; the golden asserts the delta field EXISTS and the
  // conformant flag is a reported boolean, not a pass/fail gate (§10.4
  // item 10). Reuse the T-M-D1 experiment shape at unit level.
  auto ctx = makeCtx({sineFrames(440.0, 48000, 1000)}, 48000);
  ctx.durationBehaviour = "Preserving";
  ctx.declaredLatency = {2048, 2048};
  ctx.inputFrames = 1000;
  ctx.actualOutputFrames = 3048;  // N_in + L_out
  ctx.expectedOutputFrames = 3048;
  ctx.emissionMap.reset();
  const pitchlab::analysis::MetricRegistry registry = productionMetricRegistry();
  const MetricResult r = registry.findById("latency")->compute(ctx);
  REQUIRE(r.status == Status::Ok);
  CHECK(r.values.at("measuredFlushFrames").asInt() == 2048);
  CHECK(r.values.at("deltaDeclaredVsMeasuredFlushFrames").asInt() == 0);
  CHECK(r.values.at("conformant").kind() == json::Value::Kind::Bool);
  // Tolerance is provisional with the named source — never a gate.
  CHECK(r.tolerance.kind == pitchlab::analysis::ToleranceInfo::Kind::Provisional);
  CHECK(r.tolerance.source == "latency_declared_vs_measured_ms");
  // The null-with-reason path: silent input => onset offset null + note
  // (never 0-substituted).
  auto silent = makeCtx({sineFrames(440.0, 48000, 1000)}, 48000,
                        {std::vector<double>(1000, 0.0)});
  silent.durationBehaviour = "Preserving";
  const MetricResult sr = registry.findById("latency")->compute(silent);
  REQUIRE(sr.status == Status::Ok);
  CHECK(sr.values.at("measuredOnsetOffsetFrames").isNull());
  bool silentNoted = false;
  for (const std::string& n : sr.notes) {
    if (n.find("no onset") != std::string::npos) silentNoted = true;
  }
  CHECK(silentNoted);
}
