// T-M-T suite (implementation specification §10.4 item 18/22, cycle 5,
// §17 step 5): stereo-coherence goldens + the committed stereo corpus
// brackets.
//
// Independent analytic anchors (§20-21):
//   * L = cos(w i), R = sin(w i) with integer cycles: sum(L*R) = 0 exactly
//     (orthogonality of integer-cycle sin/cos over the common period) =>
//     rho == 0 within the summation floor (1e-9).
//   * L vs -L: rho == -1 (mod the sqrt round-trip floor).
//   * The committed stereo-correlated corpus is generated with
//     out.channels[1] = out.channels[0] (bit-identical channels, §11.2):
//     rhoInput == 1 (mod the fp floor). Every v0.1 engine processes channels
//     independently with identical code paths => bit-identical output
//     channels (T-E5 per-channel bit-identity) => rhoOutput == 1 for ANY of
//     the five engines.
//   * The committed stereo-decorrelated corpus: independent RNG streams
//     (corpus.L / corpus.R consumer tags) => |rhoInput| <= 0.01 (the 5-sigma
//     bracket at N = 480 000; freeze value).

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

const double kPi = 3.14159265358979323846;

[[nodiscard]] MetricResult runStereo(const pitchlab::analysis::AnalysisContext& ctx) {
  const pitchlab::analysis::MetricRegistry registry = productionMetricRegistry();
  const pitchlab::analysis::MetricDescriptor* d = registry.findById("stereo-coherence");
  REQUIRE(d != nullptr);
  return d->compute(ctx);
}

/// Integer-cycle cos/sin pair over n samples at fs (both complete cycles).
[[nodiscard]] std::pair<std::vector<double>, std::vector<double>> cosSinPair(
    int64_t n, double freq, uint32_t fs) {
  std::vector<double> l(static_cast<std::size_t>(n));
  std::vector<double> r(static_cast<std::size_t>(n));
  const double w = 2.0 * kPi * freq / static_cast<double>(fs);
  for (int64_t i = 0; i < n; ++i) {
    l[static_cast<std::size_t>(i)] = 0.25 * std::cos(w * i);
    r[static_cast<std::size_t>(i)] = 0.25 * std::sin(w * i);
  }
  return {l, r};
}

[[nodiscard]] const json::Value& findResult(const json::Value& artifact, const char* metricId) {
  for (const json::Value& r : artifact.asObject().at("results").asArray()) {
    if (r.asObject().at("metricId").asString() == metricId) return r;
  }
  throw std::runtime_error(std::string("metric not found: ") + metricId);
}

/// Render + analyse one (asset, curve, engine) job; returns the parsed
/// artifact of the single job.
[[nodiscard]] json::Value renderAndAnalyseOne(const fs::path& exp, const TestRoot& tr,
                                              const std::vector<std::string>& metrics) {
  const EngineRegistry engines = TestRoot::productionRegistry();
  const RenderSummary rs = renderExperiment(exp, tr, engines);
  REQUIRE(rs.allOk());
  const pitchlab::analysis::AnalyzeOutcome ao = analyzeExperiment(exp, tr, metrics);
  REQUIRE(ao.jobs.size() == 1);
  REQUIRE(ao.jobs[0].completed);
  return json::parse(readBytes(ao.jobs[0].artifactPath), "analysis.json");
}

}  // namespace

TEST_CASE("T-M-T1: L = cos, R = sin (integer cycles) — rho == 0 at the summation floor") {
  const auto [l, r] = cosSinPair(48000, 480.0, 48000);  // 480 integer cycles
  auto ctx = makeCtx({l, r}, 48000, {l, r});
  ctx.channels = 2;
  const MetricResult res = runStereo(ctx);
  REQUIRE(res.status == Status::Ok);
  // Orthogonality: sum(L*R) == 0 for integer-cycle pairs => rho ~ 0.
  CHECK(res.values.at("rhoInput").asDouble() == doctest::Approx(0.0).epsilon(1e-9));
  CHECK(res.values.at("rhoOutput").asDouble() == doctest::Approx(0.0).epsilon(1e-9));
  CHECK(res.values.at("delta").asDouble() == doctest::Approx(0.0).epsilon(1e-9));
  CHECK(res.values.at("inputEnergyCh0").asDouble() > 0.0);
  CHECK(res.values.at("outputEnergyCh0").asDouble() > 0.0);
}

TEST_CASE("T-M-T2: L vs -L — rho == -1 (mod the fp sqrt round-trip floor)") {
  const auto [l, r] = cosSinPair(48000, 480.0, 48000);
  std::vector<double> neg = r;
  for (double& v : neg) v = -v;
  auto ctx = makeCtx({r, neg}, 48000, {r, neg});
  ctx.channels = 2;
  const MetricResult res = runStereo(ctx);
  REQUIRE(res.status == Status::Ok);
  CHECK(res.values.at("rhoInput").asDouble() < -1.0 + 1e-9);
  CHECK(res.values.at("rhoOutput").asDouble() < -1.0 + 1e-9);
}

TEST_CASE("T-M-T3: mono => not-applicable; silent channel => not-applicable "
          "(rho undefined, never zero-substituted)") {
  // Mono: inter-channel correlation undefined (input AND output mono).
  const std::vector<double> monoSig = sineFrames(440.0, 48000, 4096);
  auto mono = makeCtx({monoSig}, 48000, {monoSig});
  const MetricResult m = runStereo(mono);
  CHECK(m.status == Status::NotApplicable);
  CHECK(m.values.empty());

  // A silent channel: rho undefined.
  const auto [l, r] = cosSinPair(4096, 480.0, 48000);
  const std::vector<double> silent(4096, 0.0);
  auto silentCtx = makeCtx({l, silent}, 48000, {l, silent});
  silentCtx.channels = 2;
  const MetricResult s = runStereo(silentCtx);
  CHECK(s.status == Status::NotApplicable);
  bool noted = false;
  for (const std::string& n : s.notes) {
    if (n.find("silent channel") != std::string::npos) noted = true;
  }
  CHECK(noted);
}

TEST_CASE("T-M-T4: committed correlated corpus through varispeed identity — "
          "rhoInput/rhoOutput == 1 (bit-identical channels; fp floor)") {
  TestRoot tr{"metric-stereo-4"};
  tr.copyCommittedAsset("syn-stereo-correlated-noise-5s-48k");
  tr.makeCurve("t-identity", "id = \"t-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  const fs::path exp = tr.makeExperiment(
      "t-exp4",
      experimentToml("t-exp4", "syn-stereo-correlated-noise-5s-48k", "t-identity", 48000, 2));

  const json::Value art = renderAndAnalyseOne(exp, tr, {"stereo-coherence"});
  const json::Value& r = findResult(art, "stereo-coherence");
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  // Generation: channels[1] = channels[0] (bit-identical) => rho == 1 modulo
  // the sxy/sqrt(sxx*syy) round-trip floor.
  CHECK(v.at("rhoInput").asDouble() > 1.0 - 1e-12);
  // The engine processes identical channels identically (T-E5 per-channel
  // bit-identity) => the output channels are bit-identical too.
  CHECK(v.at("rhoOutput").asDouble() > 1.0 - 1e-12);
  CHECK(std::fabs(v.at("delta").asDouble()) < 1e-12);
  // The metric never asserts engines should preserve coherence (no gate
  // fields, no pass/fail).
  CHECK(v.find("conformant") == v.end());
  CHECK(r.asObject().at("tolerance").asObject().at("status").asString() == "none");
}

TEST_CASE("T-M-T5: committed decorrelated corpus — |rhoInput| <= 0.01; the "
          "output delta is REPORTED (a measurement, never a bug to hide)") {
  TestRoot tr{"metric-stereo-5"};
  tr.copyCommittedAsset("syn-stereo-decorrelated-noise-5s-48k");
  tr.makeCurve("t-identity", "id = \"t-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  const fs::path exp = tr.makeExperiment(
      "t-exp5",
      experimentToml("t-exp5", "syn-stereo-decorrelated-noise-5s-48k", "t-identity", 48000, 2));

  const json::Value art = renderAndAnalyseOne(exp, tr, {"stereo-coherence"});
  const json::Value& r = findResult(art, "stereo-coherence");
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  // Independent deterministic streams: the 5-sigma bracket at N = 480000.
  CHECK(std::fabs(v.at("rhoInput").asDouble()) <= 0.01);
  // Output rho + delta exist as measured values (finite), no golden on the
  // engine's behaviour — the decorrelation is a result to report.
  CHECK(std::isfinite(v.at("rhoOutput").asDouble()));
  CHECK(std::isfinite(v.at("delta").asDouble()));
}

TEST_CASE("T-M-T6: correlated corpus through pv.classic — any engine's "
          "per-channel-identical processing keeps rhoOutput == 1") {
  // The freeze golden: "through ANY of the five engines (all
  // per-channel-identical processors on identical channels) => rhoOutput ==
  // 1.0". pv.classic is the representative STFT engine (its per-channel
  // bit-identity is T-E5-asserted).
  TestRoot tr{"metric-stereo-6"};
  tr.copyCommittedAsset("syn-stereo-correlated-noise-5s-48k");
  tr.makeCurve("t-identity", "id = \"t-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  const fs::path exp = tr.makeExperiment(
      "t-exp6", experimentToml("t-exp6", "syn-stereo-correlated-noise-5s-48k", "t-identity",
                               48000, 2, "native.pv.classic", "benchmark", ""));
  const json::Value art = renderAndAnalyseOne(exp, tr, {"stereo-coherence"});
  const json::Value& r = findResult(art, "stereo-coherence");
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  CHECK(v.at("rhoInput").asDouble() > 1.0 - 1e-12);
  CHECK(v.at("rhoOutput").asDouble() > 1.0 - 1e-12);
}
