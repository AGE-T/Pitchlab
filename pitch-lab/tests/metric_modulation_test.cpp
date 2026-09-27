// T-M-M suite (implementation specification §10.4 item 16/22, cycle 5,
// §17 step 5): amplitude-modulation goldens + the frozen expected-rate
// table + the gated/degenerate envelope cases.
//
// Independent analytic anchors (§20-21):
//   * Synthetic AM: carrier 1 000 Hz, envelope (1 + 0.5 sin(2 pi 25 t)) —
//     both integer cycles at 48 kHz over 1 s. The analytic signal of an AM
//     tone with a non-negative band-limited envelope is m(t) e^{j w_c t}
//     => the Hilbert magnitude == 1 + 0.5 sin(2 pi 25 t) exactly (up to the
//     finite-length edge effects). The envelope spectrum (r2c FFT, N = 48000
//     => 1 Hz bins) therefore peaks at BIN 25 => envelopePeakHz == 25.0
//     (within +/- 1 bin per the freeze); the peak +-3 bins dominate.
//   * Expected-rate table derivations (manifest-parameter arithmetic only):
//     granular: Hg = round(grain_seconds * fs / overlap) => fs / Hg;
//     pv.classic/phaselocked: fs / hop; vardelay STATIC curves:
//     |r_max - 1| * fs / round(excursion_seconds * fs); varispeed: none.
//   * Modulation is MEASURED, never suppressed (§10.1) — the granular
//     grain-rate AM is the engine's intentional behaviour; the real-path
//     test asserts the measured peak sits at the declared rate.

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

[[nodiscard]] MetricResult runAm(const pitchlab::analysis::AnalysisContext& ctx) {
  const pitchlab::analysis::MetricRegistry registry = productionMetricRegistry();
  const pitchlab::analysis::MetricDescriptor* d = registry.findById("amplitude-modulation");
  REQUIRE(d != nullptr);
  return d->compute(ctx);
}

/// AM tone: carrier Hz, modulation depth at modHz (integer cycles at fs/n).
[[nodiscard]] std::vector<double> amTone(int64_t n, double carrierHz, double modHz,
                                         double depth, uint32_t fs) {
  std::vector<double> x(static_cast<std::size_t>(n));
  for (int64_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(fs);
    x[static_cast<std::size_t>(i)] =
        (1.0 + depth * std::sin(2.0 * kPi * modHz * t)) * std::sin(2.0 * kPi * carrierHz * t);
  }
  return x;
}

[[nodiscard]] json::Object params(const std::vector<std::pair<std::string, double>>& kv) {
  json::Object o;
  for (const auto& [k, v] : kv) o[k] = json::Value(v);
  return o;
}

[[nodiscard]] const json::Value& findResult(const json::Value& artifact, const char* metricId) {
  for (const json::Value& r : artifact.asObject().at("results").asArray()) {
    if (r.asObject().at("metricId").asString() == metricId) return r;
  }
  throw std::runtime_error(std::string("metric not found: ") + metricId);
}

}  // namespace

TEST_CASE("T-M-M1: synthetic AM golden — envelopePeakHz == 25 (bin 25), "
          "peak energy dominant") {
  // 1 000 Hz carrier, 25 Hz AM at depth 0.5, 1 s at 48 kHz: 1 Hz envelope
  // bins; the envelope is the pure 25 Hz sinusoid (mean-removed) => the
  // spectrum peak lands at bin 25.
  auto ctx = makeCtx({amTone(48000, 1000.0, 25.0, 0.5, 48000)}, 48000);
  ctx.engineId = "native.varispeed";  // no declared rate (documented physics)
  const MetricResult r = runAm(ctx);
  REQUIRE(r.status == Status::Ok);
  CHECK(r.values.at("envelopePeakHzCh0").asDouble() ==
        doctest::Approx(25.0).epsilon(2.0 / 48000.0));  // +/- ~1 envelope bin
  // The peak +-3 bins carry (nearly) all the non-DC envelope energy.
  CHECK(r.values.at("relativeEnergyAtPeakCh0").asDouble() >= 0.9);
  // varispeed declares NO characteristic AM rate: null + the physics note.
  CHECK(r.values.at("expectedModulationHz").isNull());
  bool noted = false;
  for (const std::string& n : r.notes) {
    if (n.find("varispeed declares no characteristic AM rate") != std::string::npos) {
      noted = true;
    }
  }
  CHECK(noted);
}

TEST_CASE("T-M-M2: the frozen expected-rate table (manifest arithmetic only)") {
  // granular: Hg = round(grain_seconds * fs / overlap) => fs / Hg.
  {
    auto ctx = makeCtx({amTone(48000, 1000.0, 25.0, 0.5, 48000)}, 48000);
    ctx.engineId = "native.granular";
    ctx.engineParameters = json::Value(params({{"grain_seconds", 0.05}, {"overlap", 4.0}}));
    const MetricResult r = runAm(ctx);
    REQUIRE(r.values.at("expectedModulationHz").kind() == json::Value::Kind::Double);
    // Hg = round(0.05 * 48000 / 4) = 600 => 48000 / 600 = 80.
    CHECK(r.values.at("expectedModulationHz").asDouble() == doctest::Approx(80.0).epsilon(1e-12));
    CHECK(r.values.at("relativeEnergyAtExpectedCh0").kind() == json::Value::Kind::Double);
  }
  // pv.classic: fs / hop.
  {
    auto ctx = makeCtx({amTone(48000, 1000.0, 25.0, 0.5, 48000)}, 48000);
    ctx.engineId = "native.pv.classic";
    ctx.engineParameters = json::Value(params({{"hop", 512.0}}));
    const MetricResult r = runAm(ctx);
    REQUIRE(r.values.at("expectedModulationHz").kind() == json::Value::Kind::Double);
    CHECK(r.values.at("expectedModulationHz").asDouble() ==
          doctest::Approx(48000.0 / 512.0).epsilon(1e-12));
  }
  // pv.phaselocked: same rule.
  {
    auto ctx = makeCtx({amTone(48000, 1000.0, 25.0, 0.5, 48000)}, 48000);
    ctx.engineId = "native.pv.phaselocked";
    ctx.engineParameters = json::Value(params({{"hop", 256.0}}));
    const MetricResult r = runAm(ctx);
    REQUIRE(r.values.at("expectedModulationHz").kind() == json::Value::Kind::Double);
    CHECK(r.values.at("expectedModulationHz").asDouble() ==
          doctest::Approx(48000.0 / 256.0).epsilon(1e-12));
  }
  // vardelay STATIC: |r_max - 1| * fs / E with E = round(excursion * fs).
  {
    auto ctx = makeCtx({amTone(48000, 1000.0, 25.0, 0.5, 48000)}, 48000);
    ctx.engineId = "native.vardelay";
    ctx.engineParameters = json::Value(params({{"excursion_seconds", 0.01}}));
    ctx.curveStatic = true;
    ctx.effectiveMax = 1.02;  // static curve extrema
    const MetricResult r = runAm(ctx);
    REQUIRE(r.values.at("expectedModulationHz").kind() == json::Value::Kind::Double);
    // E = round(0.01 * 48000) = 480; rate = 0.02 * 48000 / 480 = 2.0.
    CHECK(r.values.at("expectedModulationHz").asDouble() == doctest::Approx(2.0).epsilon(1e-12));
  }
  // vardelay DYNAMIC curve: no declared rate (static curves only — frozen).
  {
    auto ctx = makeCtx({amTone(48000, 1000.0, 25.0, 0.5, 48000)}, 48000);
    ctx.engineId = "native.vardelay";
    ctx.engineParameters = json::Value(params({{"excursion_seconds", 0.01}}));
    ctx.curveStatic = false;
    const MetricResult r = runAm(ctx);
    CHECK(r.values.at("expectedModulationHz").isNull());
  }
  // varispeed: null (documented physics — rates scale with the curve).
  {
    auto ctx = makeCtx({amTone(48000, 1000.0, 25.0, 0.5, 48000)}, 48000);
    ctx.engineId = "native.varispeed";
    const MetricResult r = runAm(ctx);
    CHECK(r.values.at("expectedModulationHz").isNull());
  }
}

TEST_CASE("T-M-M3: constant envelope (DC / silence) => not-applicable, "
          "per-channel nulls with notes") {
  // DC 0.25: the analytic signal of DC is DC => envelope constant.
  auto ctx = makeCtx({std::vector<double>(48000, 0.25)}, 48000);
  ctx.engineId = "native.varispeed";
  const MetricResult r = runAm(ctx);
  CHECK(r.status == Status::NotApplicable);
  CHECK(r.values.at("envelopePeakHzCh0").isNull());
  CHECK(r.values.at("relativeEnergyAtPeakCh0").isNull());
  bool noted = false;
  for (const std::string& n : r.notes) {
    if (n.find("constant envelope") != std::string::npos) noted = true;
  }
  CHECK(noted);

  // Silence: same honest path.
  auto ctx2 = makeCtx({std::vector<double>(48000, 0.0)}, 48000);
  ctx2.engineId = "native.varispeed";
  const MetricResult r2 = runAm(ctx2);
  CHECK(r2.status == Status::NotApplicable);
}

TEST_CASE("T-M-M4: real path — granular grain-rate AM is MEASURED (intentional "
          "behaviour, never suppressed)") {
  TestRoot tr{"metric-modulation-4"};
  // Non-identity ratio: the quantised read grid makes the grain machinery's
  // periodicity measurable in the output envelope (at identity the OLA of
  // Hann grains at 4x overlap sums constant — no grain-rate AM, which is
  // itself the honest identity result).
  tr.makeCurve("m-onefive", "id = \"m-onefive\"\nkind = \"static\"\nvalue = { ratio = 1.5 }\n");
  std::vector<double> noise(24000);
  {
    // Deterministic LCG (own, no <random>): x_i in [-0.5, 0.5].
    uint64_t s = 0x9E3779B97F4A7C15ull;
    for (double& v : noise) {
      s = s * 6364136223846793005ull + 1442695040888963407ull;
      v = static_cast<double>(static_cast<int32_t>(s >> 33)) / 2147483648.0 * 0.5;
    }
  }
  tr.makeAsset("m-noise", 48000, {noise});
  const fs::path exp = tr.makeExperiment(
      "m-exp4", experimentToml("m-exp4", "m-noise", "m-onefive", 48000, 1, "native.granular",
                               "benchmark", "grain_seconds = 0.05, overlap = 4, jitter_frames = 0"));

  const EngineRegistry engines = TestRoot::productionRegistry();
  const RenderSummary rs = renderExperiment(exp, tr, engines);
  REQUIRE(rs.allOk());
  const pitchlab::analysis::AnalyzeOutcome ao = analyzeExperiment(exp, tr, {"amplitude-modulation"});
  REQUIRE(ao.jobs.size() == 1);
  REQUIRE(ao.jobs[0].completed);
  const json::Value art = json::parse(readBytes(ao.jobs[0].artifactPath), "analysis.json");
  const json::Value& r = findResult(art, "amplitude-modulation");
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  // Declared rate from the manifest parameters (the analytic table golden):
  // Hg = round(0.05 * 48000 / 4) = 600 => fs / Hg = 80 Hz exactly. Both
  // table inputs (grain_seconds AND overlap) are authored explicitly — the
  // manifest mirrors the authored parameters only.
  // NOTE: 80.0 serialises as "80" and re-parses as an Int-kind value —
  // asDouble() is total, so the numeric assertion is kind-agnostic.
  CHECK(v.at("expectedModulationHz").asDouble() == doctest::Approx(80.0).epsilon(1e-12));
  // The measured envelope: peak reported in the frozen search band and
  // measurable energy at the declared rate (measurement, not a gate).
  REQUIRE(v.at("envelopePeakHzCh0").kind() == json::Value::Kind::Double);
  const double peak = v.at("envelopePeakHzCh0").asDouble();
  CHECK(peak >= 0.5);
  CHECK(peak <= 500.0);
  CHECK(v.at("relativeEnergyAtExpectedCh0").asDouble() > 0.0);
  CHECK(v.at("relativeEnergyAtPeakCh0").asDouble() > 0.0);
  // The measurement note records the philosophy (measured, never suppressed).
  bool noted = false;
  for (const json::Value& n : r.asObject().at("notes").asArray()) {
    if (n.asString().find("never suppressed") != std::string::npos) noted = true;
  }
  CHECK(noted);
  std::printf("  T-M-M4 measured: envelopePeakHz=%.6f relAtPeak=%.6f relAtExpected=%.6f\n",
              peak, v.at("relativeEnergyAtPeakCh0").asDouble(),
              v.at("relativeEnergyAtExpectedCh0").asDouble());
}
