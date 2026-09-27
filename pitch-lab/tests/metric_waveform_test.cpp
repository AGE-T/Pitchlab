// T-M-W suite (implementation specification §10.4 items 8/22, cycle 5,
// §17 step 5): peak-rms-crest analytic goldens + edge cases.
//
// Analytic derivation policy (§20-21 of the cycle-5 instruction): the
// expected values below are INDEPENDENT derivations from the frozen
// mathematical definitions (§10.4 item 8) and the signal construction —
// never expected = metric(actual).
//   * DC 0.25 (dyadic value): peak = rms = 0.25 and crest = 1 hold EXACTLY
//     in IEEE-754 (sum of N dyadic squares / N is dyadic; sqrt of a dyadic
//     square is exact).
//   * +-0.25 square, 50% duty: identical exactness argument.
//   * Integer-cycle cosine, sample on the crest, A = 0.25: peak = A exactly
//     (i = 0 hits the crest; 0.25 * 1.0 is exact); rms = A / sqrt(2) to the
//     floating-point summation floor (documented class, §22 — the sum of
//     sin^2 values is not dyadic).
//   * End-to-end: the committed-shape identity render of a 0.25-amplitude
//     sine must reproduce peak/rms within the -80 dBFS identity criterion
//     class (L-5) — a sanity anchor, not an exactness claim.

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

/// Run the peak-rms-crest metric (via the production registry — the single
/// authoritative registration point) on a directly-constructed context.
MetricResult runPeakRmsCrest(const pitchlab::analysis::AnalysisContext& ctx) {
  const pitchlab::analysis::MetricRegistry registry = productionMetricRegistry();
  const pitchlab::analysis::MetricDescriptor* d = registry.findById("peak-rms-crest");
  REQUIRE(d != nullptr);
  return d->compute(ctx);
}

[[nodiscard]] std::vector<double> constant(double v, int64_t n) {
  return std::vector<double>(static_cast<std::size_t>(n), v);
}

/// +-0.25 square wave, exact 50% duty over an even length.
[[nodiscard]] std::vector<double> squareWave(int64_t n) {
  std::vector<double> x(static_cast<std::size_t>(n));
  for (int64_t i = 0; i < n; ++i) {
    x[static_cast<std::size_t>(i)] = (i < n / 2) ? 0.25 : -0.25;
  }
  return x;
}

/// Integer-cycle cosine with a sample exactly on the crest (i = 0).
/// 440 Hz at 48 kHz over exactly 1 s => 440 integer cycles.
[[nodiscard]] std::vector<double> crestCosine(int64_t n, double freq, uint32_t fs) {
  std::vector<double> x(static_cast<std::size_t>(n));
  const double w = 2.0 * 3.14159265358979323846 * freq / static_cast<double>(fs);
  for (int64_t i = 0; i < n; ++i) {
    x[static_cast<std::size_t>(i)] = 0.25 * std::cos(w * static_cast<double>(i));
  }
  return x;
}

const double kSqrt2 = 1.41421356237309504880168872420969808;

}  // namespace

TEST_CASE("T-M-W1: DC 0.25 — peak = rms = 0.25, crest = 1, crestDb = 0 EXACTLY") {
  auto ctx = makeCtx({constant(0.25, 1000)}, 48000);
  const MetricResult r = runPeakRmsCrest(ctx);
  REQUIRE(r.status == Status::Ok);
  CHECK(r.metricId == "peak-rms-crest");
  CHECK(r.unit == "linear-amplitude");
  CHECK(r.values.at("peakCh0").asDouble() == 0.25);   // exact (dyadic)
  CHECK(r.values.at("rmsCh0").asDouble() == 0.25);    // exact (dyadic chain)
  CHECK(r.values.at("crestCh0").asDouble() == 1.0);   // 0.25/0.25 exact
  CHECK(r.values.at("crestDbCh0").asDouble() == 0.0); // log10(1) = 0 exact
  CHECK(r.tolerance.kind == pitchlab::analysis::ToleranceInfo::Kind::Exact);
  CHECK(r.hasSampleCount);
  CHECK(r.sampleCount == 1000);
}

TEST_CASE("T-M-W2: +-0.25 square, 50% duty — peak = rms = 0.25, crest = 1 EXACTLY") {
  auto ctx = makeCtx({squareWave(1000)}, 48000);
  const MetricResult r = runPeakRmsCrest(ctx);
  REQUIRE(r.status == Status::Ok);
  CHECK(r.values.at("peakCh0").asDouble() == 0.25);
  CHECK(r.values.at("rmsCh0").asDouble() == 0.25);
  CHECK(r.values.at("crestCh0").asDouble() == 1.0);
  CHECK(r.values.at("crestDbCh0").asDouble() == 0.0);
}

TEST_CASE("T-M-W3: integer-cycle cosine on the crest — peak = A exactly; "
          "rms = A/sqrt(2) at the summation floor; crest = sqrt(2) (floor class)") {
  // 440 * 48000/48000 = 440 integer cycles; cos(0) = 1 puts a sample on the
  // crest. The analytic expectations: peak = 0.25 exactly; rms = 0.25/sqrt(2)
  // modulo the floating-point summation floor (1e-12 relative — documented
  // class, NOT a normative tolerance).
  auto ctx = makeCtx({crestCosine(48000, 440.0, 48000)}, 48000);
  const MetricResult r = runPeakRmsCrest(ctx);
  REQUIRE(r.status == Status::Ok);
  const double peak = r.values.at("peakCh0").asDouble();
  const double rms = r.values.at("rmsCh0").asDouble();
  const double crest = r.values.at("crestCh0").asDouble();
  const double crestDb = r.values.at("crestDbCh0").asDouble();
  CHECK(peak == 0.25);  // the i = 0 crest sample: 0.25 * cos(0) = 0.25 exact
  CHECK(rms == doctest::Approx(0.25 / kSqrt2).epsilon(1e-12));
  CHECK(crest == doctest::Approx(kSqrt2).epsilon(1e-12));
  CHECK(crestDb == doctest::Approx(20.0 * std::log10(kSqrt2)).epsilon(1e-12));
  // rms of a pure tone is amplitude-independent in the norm: N = 48000 frames.
  CHECK(r.sampleCount == 48000);
}

TEST_CASE("T-M-W4: stereo — per-channel values stay SEPARATE (no collapsing); "
          "silent channel => crest null + note (never zero-substituted)") {
  const std::vector<double> loud = constant(0.25, 500);
  const std::vector<double> silent(500, 0.0);
  auto ctx = makeCtx({loud, silent}, 48000);
  ctx.channels = 2;
  const MetricResult r = runPeakRmsCrest(ctx);
  REQUIRE(r.status == Status::Ok);
  CHECK(r.values.at("peakCh0").asDouble() == 0.25);
  CHECK(r.values.at("rmsCh0").asDouble() == 0.25);
  CHECK(r.values.at("peakCh1").asDouble() == 0.0);
  CHECK(r.values.at("rmsCh1").asDouble() == 0.0);
  // The silent channel's crest is undefined: null with a note (§10.4 item 4).
  CHECK(r.values.at("crestCh1").isNull());
  CHECK(r.values.at("crestDbCh1").isNull());
  bool noted = false;
  for (const std::string& n : r.notes) {
    if (n.find("silent") != std::string::npos) noted = true;
  }
  CHECK(noted);
  // NO cross-channel aggregation keys exist (no mean/overall member).
  CHECK(r.values.find("peak") == r.values.end());
  CHECK(r.values.find("crest") == r.values.end());
  CHECK(r.values.find("meanCrest") == r.values.end());
}

TEST_CASE("T-M-W5: zero frames => insufficient-data (never zero values)") {
  pitchlab::analysis::AnalysisContext ctx = makeCtx({std::vector<double>{}}, 48000);
  ctx.actualOutputFrames = 0;
  const MetricResult r = runPeakRmsCrest(ctx);
  CHECK(r.status == Status::InsufficientData);
  bool noted = false;
  for (const std::string& n : r.notes) {
    if (n.find("zero frames") != std::string::npos) noted = true;
  }
  CHECK(noted);
  CHECK(r.values.empty());
}

TEST_CASE("T-M-W6: manifest channel count disagrees with the WAV => analysis-error") {
  auto ctx = makeCtx({constant(0.25, 100)}, 48000);
  ctx.channels = 2;  // manifest says stereo; the master has one channel
  const MetricResult r = runPeakRmsCrest(ctx);
  CHECK(r.status == Status::AnalysisError);
  CHECK(r.error.find("channel") != std::string::npos);
}

TEST_CASE("T-M-W7: unloaded output => analysis-error") {
  pitchlab::analysis::AnalysisContext ctx;  // nothing loaded
  const MetricResult r = runPeakRmsCrest(ctx);
  CHECK(r.status == Status::AnalysisError);
  CHECK(r.error.find("not loaded") != std::string::npos);
}

TEST_CASE("T-M-W8: end-to-end identity render — peak/rms reproduce the input's "
          "analytic values within the -80 dBFS identity criterion class") {
  // Real harness: varispeed identity over a 0.25-amplitude 440 Hz sine
  // (12 000 frames), analysed through the REAL analyzer. The identity
  // criterion is L-5 audio-equivalence at -80 dBFS (provisional) => the
  // measured peak/rms may deviate from the analytic input values by at most
  // ~1e-4 (documented PROVISIONAL class — the exactness goldens are the
  // unit cases above).
  TestRoot tr{"metric-waveform"};
  tr.makeCurve("w-identity", "id = \"w-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  tr.makeAsset("w-sine", 48000, {sineFrames(440.0, 48000, 12000)});
  const fs::path exp = tr.makeExperiment(
      "w-exp", experimentToml("w-exp", "w-sine", "w-identity", 48000, 1));
  const EngineRegistry engines = TestRoot::productionRegistry();
  const RenderSummary rs = renderExperiment(exp, tr, engines);
  REQUIRE(rs.allOk());
  const pitchlab::analysis::AnalyzeOutcome ao =
      analyzeExperiment(exp, tr, {"peak-rms-crest"});
  REQUIRE(ao.jobs.size() == 1);
  REQUIRE(ao.jobs[0].completed);
  // Read the artifact and check the values (the analyzer attaches provenance).
  const std::string art = readBytes(ao.jobs[0].artifactPath);
  CHECK(art.find("\"schema\": \"pitchlab.analysis.v1\"") != std::string::npos);
  CHECK(art.find("\"metricId\": \"peak-rms-crest\"") != std::string::npos);
  CHECK(art.find("\"status\": \"ok\"") != std::string::npos);
  // Re-parse the artifact through the canonical reader (round-trip) and
  // read the measured values.
  const json::Value parsed = json::parse(art, "analysis.json");
  const json::Value& result = parsed.asObject().at("results").asArray()[0];
  const json::Object& values = result.asObject().at("values").asObject();
  const double peak = values.at("peakCh0").asDouble();
  const double rms = values.at("rmsCh0").asDouble();
  // Analytic anchors: input peak = 0.25 exactly (sineFrames: 0.25 * sin(w i),
  // first crest within 1 sample); rms = 0.25/sqrt(2). The end-to-end
  // deviations are dominated by the identity render's input-edge partial
  // FIR window (first ~K samples attenuated) + the 24-frame flush tail —
  // 48 of 12 024 frames — an energy-level effect of ~1e-3, NOT per-sample
  // error (the -80 dBFS identity class bounds per-sample amplitude). The
  // bound below documents that class honestly.
  CHECK(peak == doctest::Approx(0.25).epsilon(1e-4));
  CHECK(rms == doctest::Approx(0.25 / kSqrt2).epsilon(5e-3));
  CHECK(values.at("crestCh0").asDouble() ==
        doctest::Approx(kSqrt2).epsilon(5e-3));
}
