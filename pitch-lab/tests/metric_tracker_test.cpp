// T-M-TR suite (implementation specification §10.5 item 12, cycle 6, OD-12):
// the tracker-dependent metric modules — pitch-error, pitch-lag,
// warble-instability — through direct makeCtx contexts AND end-to-end
// through the REAL harness (TestRoot -> production registry ->
// ExperimentCompiler -> OfflineRenderer -> render manifests -> analyzeJobs;
// the same functions the CLI calls — no second renderer, no fake engines).
//
// Independent analytic anchors (§10.5 golden discipline — expected values
// derived from the recipe x curve x emission-map arithmetic, NEVER from
// running the tracker on the output):
//   * Identity model (Preserving): f_exp = 1 * 440 at every in-span center.
//   * Static ratio 2.0 (Preserving/RateFollowing): f_exp = 2 * 440 = 880.
//   * Vibrato cancellation: the input's own 5 Hz +-0.5 st modulation is IN
//     the expected model — the warble deviation series isolates the
//     tracker/jitter contribution (a raw 0.5 st modulation would show ~35
//     cents std; the model-removed series is the jitter floor class).
//   * Delayed-curve lag: an output synthesised as f(t) = 440*(1 +
//     0.5*sin(2*pi*0.5*(t - Delta))) under the curve ratio(t) = 1 +
//     0.5*sin(2*pi*0.5*t) lags the expected by EXACTLY Delta = 4 hops
//     (2048 samples @ 48 kHz = 42.67 ms): the corrected (n, n-k) pairing
//     must report lagFrames == +4 with correlation ~ 1.
//   * Warble positive detection: a 10-cent 3 Hz flutter on the OUTPUT
//     (expected model constant) shows ~7 cents std — above the jitter floor.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "test_fixtures.h"

using namespace pitchlab;
using namespace pitchlab::test;
using pitchlab::analysis::MetricResult;
using pitchlab::analysis::Status;
using json::Value;
using doctest::Approx;

namespace {

const double kPi = 3.14159265358979323846;

/// Phase-accumulated sine at an instantaneous frequency schedule (the corpus
/// generator's synthesis form).
[[nodiscard]] std::vector<double> sineWithFInst(
    const std::function<double(double)>& fInst, uint32_t fs, int64_t n) {
  std::vector<double> x(static_cast<std::size_t>(n));
  double phase = 0.0;
  for (int64_t i = 0; i < n; ++i) {
    const double f = fInst(static_cast<double>(i) / static_cast<double>(fs));
    phase += f / static_cast<double>(fs);
    phase = std::fmod(phase, 1.0);
    if (phase < 0.0) phase += 1.0;
    x[static_cast<std::size_t>(i)] = 0.25 * std::sin(2.0 * kPi * phase);
  }
  return x;
}

[[nodiscard]] std::vector<double> constantSine(double fHz, uint32_t fs, int64_t n) {
  return sineWithFInst([fHz](double) { return fHz; }, fs, n);
}

/// Deterministic LCG noise (test-only).
[[nodiscard]] std::vector<double> whiteNoise(uint64_t seed, int64_t n) {
  std::vector<double> x(static_cast<std::size_t>(n));
  uint64_t s = seed;
  for (int64_t i = 0; i < n; ++i) {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    const double u = static_cast<double>(s >> 11) / 9007199254740992.0;
    x[static_cast<std::size_t>(i)] = u * 2.0 - 1.0;
  }
  return x;
}

[[nodiscard]] MetricResult runMetric(const char* metricId,
                                    const pitchlab::analysis::AnalysisContext& ctx) {
  const pitchlab::analysis::MetricRegistry registry = productionMetricRegistry();
  const pitchlab::analysis::MetricDescriptor* d = registry.findById(metricId);
  REQUIRE(d != nullptr);
  return d->compute(ctx);
}

/// A makeCtx context for the tracker metrics: sets the recipe, the duration
/// behaviour, the effective ratio and the emission map (RateFollowing).
[[nodiscard]] pitchlab::analysis::AnalysisContext trackerCtx(
    const std::vector<double>& output, uint32_t fs, int64_t inputFrames,
    const pitchlab::analysis::F0Recipe& recipe, const std::string& durationBehaviour,
    const std::vector<double>& effectiveRatio, bool curveStatic) {
  auto ctx = makeCtx({output}, fs);
  ctx.assetId = "test-asset";
  ctx.asset.assetId = "test-asset";
  ctx.asset.hasF0Recipe = true;
  ctx.asset.f0 = recipe;
  ctx.inputFrames = inputFrames;
  ctx.durationBehaviour = durationBehaviour;
  ctx.effectiveRatio = effectiveRatio;
  ctx.curveStatic = curveStatic;
  ctx.curveKind = curveStatic ? "static" : "lfo";
  ctx.engineId = "test-engine";
  ctx.renderStatus = "ok";
  if (durationBehaviour == "RateFollowing") {
    ctx.emissionMap = std::make_unique<pitchlab::analysis::EmissionMap>(
        effectiveRatio, inputFrames, static_cast<int64_t>(output.size()));
  }
  return ctx;
}

[[nodiscard]] const Value& findResult(const Value& artifact, const char* metricId) {
  for (const Value& r : artifact.asObject().at("results").asArray()) {
    if (r.asObject().at("metricId").asString() == metricId) return r;
  }
  throw std::runtime_error(std::string("metric not found: ") + metricId);
}

const pitchlab::analysis::F0Recipe kSine440 = [] {
  pitchlab::analysis::F0Recipe r;
  r.className = "constant";
  r.freqHz = 440.0;
  return r;
}();

const pitchlab::analysis::F0Recipe kVibrato220 = [] {
  pitchlab::analysis::F0Recipe r;
  r.className = "vibrato";
  r.baseHz = 220.0;
  r.vibratoHz = 5.0;
  r.vibratoSt = 0.5;
  return r;
}();

}  // namespace

// ---------------------------------------------------------------------------
// Applicability gating
// ---------------------------------------------------------------------------

TEST_CASE("T-M-TR1: recipe gating — no [f0] block => not-applicable for all "
          "three metrics (the honest v0.1 state; noise likewise)") {
  auto ctx = makeCtx({constantSine(440.0, 48000, 48000)}, 48000);
  ctx.assetId = "a-sine";
  ctx.asset.assetId = "a-sine";
  ctx.inputFrames = 48000;
  for (const char* id : {"pitch-error", "pitch-lag", "warble-instability"}) {
    const MetricResult r = runMetric(id, ctx);
    CHECK(r.status == Status::NotApplicable);
    bool noteOk = false;
    for (const std::string& n : r.notes) {
      if (n.find("no analytic f0 recipe") != std::string::npos) noteOk = true;
    }
    CHECK(noteOk);
  }
  // Noise material with a recipe absent: same honest gate.
  auto noiseCtx = makeCtx({whiteNoise(7, 48000)}, 48000);
  noiseCtx.asset.assetId = "a-noise";
  const MetricResult pe = runMetric("pitch-error", noiseCtx);
  CHECK(pe.status == Status::NotApplicable);
}

// ---------------------------------------------------------------------------
// pitch-error (direct contexts)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-TR2: pitch-error — Preserving identity: ok, medianCents in "
          "the empirical sub-bin band, small excluded fraction") {
  // Expected side (INDEPENDENT): f_exp = 1 * 440 = 440 at every in-span
  // center (the analytic recipe x the identity curve).
  const std::vector<double> out = constantSine(440.0, 48000, 48000);
  const std::vector<double> ratio(48000, 1.0);
  auto ctx = trackerCtx(out, 48000, 48000, kSine440, "Preserving", ratio, true);
  const MetricResult r = runMetric("pitch-error", ctx);
  REQUIRE(r.status == Status::Ok);
  const double median = r.values.at("medianCents").asDouble();
  const double p95 = r.values.at("p95Cents").asDouble();
  const double excl = r.values.at("excludedVoicedFraction").asDouble();
  std::printf("T-M-TR2: identity: median %.4f cents, p95 %.4f, excluded %.4f "
              "(compared %lld/%lld)\n",
              median, p95, excl,
              static_cast<long long>(r.values.at("comparedFrameCount").asInt()),
              static_cast<long long>(r.values.at("trackerFrameCount").asInt()));
  CHECK(median <= 15.0);  // PROVISIONAL/EMPIRICAL (the tracker sub-bin class)
  CHECK(p95 <= 30.0);
  CHECK(excl <= 0.10);    // frame-0 init + edges only
  CHECK(r.values.at("comparedFrameCount").asInt() >= 80);
  CHECK(r.unit == "cents");
  CHECK(r.tolerance.kind == pitchlab::analysis::ToleranceInfo::Kind::Provisional);
  CHECK(r.tolerance.source == "pitch_error_median_cents");
}

TEST_CASE("T-M-TR3: pitch-error — Preserving static ratio 2.0: the analytic "
          "expected 880 tracked") {
  // Expected side (INDEPENDENT): f_exp = 2 * 440 = 880 (recipe x curve).
  const std::vector<double> out = constantSine(880.0, 48000, 48000);
  const std::vector<double> ratio(48000, 2.0);
  auto ctx = trackerCtx(out, 48000, 48000, kSine440, "Preserving", ratio, true);
  const MetricResult r = runMetric("pitch-error", ctx);
  REQUIRE(r.status == Status::Ok);
  const double median = r.values.at("medianCents").asDouble();
  std::printf("T-M-TR3: static 2.0x: median %.4f cents vs the analytic 880\n", median);
  CHECK(median <= 15.0);
  CHECK(std::fabs(r.values.at("f0MedianMeasuredHz").asDouble() - 880.0) < 8.0);
}

TEST_CASE("T-M-TR4: pitch-error — RateFollowing static 2.0: the read-position "
          "model; flush frames excluded and reported") {
  // varispeed-like: r(m) = 2m; output = the 880 Hz render of a 24000-frame
  // input (24012 frames with the 12-frame flush). In-span: r(center) <=
  // 23999 => center <= 11999 => about half the frames compared, the flush
  // half excluded (honest, reported).
  const std::vector<double> out = constantSine(880.0, 48000, 24012);
  const std::vector<double> ratio(24000, 2.0);
  auto ctx = trackerCtx(out, 48000, 24000, kSine440, "RateFollowing", ratio, true);
  const MetricResult r = runMetric("pitch-error", ctx);
  REQUIRE(r.status == Status::Ok);
  const double median = r.values.at("medianCents").asDouble();
  const double excl = r.values.at("excludedVoicedFraction").asDouble();
  const int64_t compared = r.values.at("comparedFrameCount").asInt();
  std::printf("T-M-TR4: rate-following 2.0x: median %.4f cents, compared %lld, "
              "excluded %.4f\n",
              median, static_cast<long long>(compared), excl);
  CHECK(median <= 15.0);
  // In-span frames: centers 1024 + 512k <= 11999 => k <= 21 => frames 0..21
  // (22 frames, frame 0 unvoiced by init => 21 compared). Total frames: 44.
  CHECK(compared >= 18);
  CHECK(compared <= 24);
  CHECK(excl > 0.3);   // the flush half is excluded (honest reporting)
  CHECK(excl < 0.7);
}

TEST_CASE("T-M-TR5: pitch-error — short signal => insufficient-data; "
          "out-of-band expected note") {
  {
    // Shorter than one tracker window: zero frames.
    const std::vector<double> out = constantSine(440.0, 48000, 1000);
    const std::vector<double> ratio(1000, 1.0);
    auto ctx = trackerCtx(out, 48000, 1000, kSine440, "Preserving", ratio, true);
    const MetricResult r = runMetric("pitch-error", ctx);
    CHECK(r.status == Status::InsufficientData);
    CHECK(r.values.at("medianCents").isNull());
    CHECK(r.values.at("trackerFrameCount").asInt() == 0);
  }
  {
    // Above-band expected (Preserving ratio 4.0 => 1760 Hz, out of band):
    // the tracker locks onto in-band subharmonics (880) — the honest
    // measurement with the documented note (§10.5 item 3's correction).
    const std::vector<double> out = constantSine(1760.0, 48000, 48000);
    const std::vector<double> ratio(48000, 4.0);
    auto ctx = trackerCtx(out, 48000, 48000, kSine440, "Preserving", ratio, true);
    const MetricResult r = runMetric("pitch-error", ctx);
    REQUIRE(r.status == Status::Ok);
    bool subNote = false;
    for (const std::string& n : r.notes) {
      if (n.find("subharmonics") != std::string::npos) subNote = true;
    }
    CHECK(subNote);
    // The measured median sits at the octave-down subharmonic: ~-1200 cents
    // vs the out-of-band expectation — an honest measured RESULT (never a
    // gate); assert the class (within 1200 +- 60 cents of -1200).
    const double median = r.values.at("medianCents").asDouble();
    std::printf("T-M-TR5: out-of-band expected (1760 Hz): median deviation "
                "%.1f cents (the subharmonic-lock class)\n",
                median);
    CHECK(std::fabs(median - 1200.0) <= 60.0);
  }
}

// ---------------------------------------------------------------------------
// pitch-lag (direct contexts)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-TR6: pitch-lag — flat curve => not-applicable; the corrected "
          "sign convention on a synthetic delayed curve (lag EXACTLY 4 hops, "
          "correlation ~ 1)") {
  // (a) Flat curve: the §10.2-reachable not-applicable.
  {
    const std::vector<double> out = constantSine(440.0, 48000, 48000);
    const std::vector<double> ratio(48000, 1.0);
    auto ctx = trackerCtx(out, 48000, 48000, kSine440, "Preserving", ratio, true);
    const MetricResult r = runMetric("pitch-lag", ctx);
    CHECK(r.status == Status::NotApplicable);
    bool flat = false;
    for (const std::string& n : r.notes) {
      if (n.find("flat curve") != std::string::npos) flat = true;
    }
    CHECK(flat);
  }
  // (b) A modulated curve ratio(t) = 1 + 0.5*sin(2*pi*0.5*t) and an output
  // that follows it EXACTLY: lag 0, correlation ~ 1.
  {
    const int64_t n = 96000;  // 2 s: one full 0.5 Hz cycle
    const auto curveF = [](double t) { return 1.0 + 0.5 * std::sin(2.0 * kPi * 0.5 * t); };
    std::vector<double> ratio(static_cast<std::size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
      ratio[static_cast<std::size_t>(i)] = curveF(static_cast<double>(i) / 48000.0);
    }
    const std::vector<double> out = sineWithFInst(
        [&curveF](double t) { return 440.0 * curveF(t); }, 48000, n);
    auto ctx = trackerCtx(out, 48000, n, kSine440, "Preserving", ratio, false);
    const MetricResult r = runMetric("pitch-lag", ctx);
    REQUIRE(r.status == Status::Ok);
    std::printf("T-M-TR6b: exact-follow: lagFrames %lld, r = %.4f\n",
                static_cast<long long>(r.values.at("lagFrames").asInt()),
                r.values.at("correlationAtLag").asDouble());
    CHECK(r.values.at("lagFrames").asInt() == 0);
    CHECK(r.values.at("correlationAtLag").asDouble() > 0.97);
  }
  // (c) The delayed engine: output f(t) = 440*curve(t - Delta) with Delta =
  // 2048 samples = 4 hops EXACTLY. The corrected (n, n-k) pairing must
  // report lagFrames == +4 (positive = LAGS), correlation ~ 1, and
  // lagMs == 4*512/48000*1000 (42.67 ms).
  {
    const int64_t n = 96000;
    const int64_t delay = 2048;  // 4 hops
    const auto curveF = [](double t) { return 1.0 + 0.5 * std::sin(2.0 * kPi * 0.5 * t); };
    std::vector<double> ratio(static_cast<std::size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
      ratio[static_cast<std::size_t>(i)] = curveF(static_cast<double>(i) / 48000.0);
    }
    const std::vector<double> out = sineWithFInst(
        [delay, &curveF](double t) {
          const double td = t - static_cast<double>(delay) / 48000.0;
          return 440.0 * (td > 0.0 ? curveF(td) : 1.0);
        },
        48000, n);
    auto ctx = trackerCtx(out, 48000, n, kSine440, "Preserving", ratio, false);
    const MetricResult r = runMetric("pitch-lag", ctx);
    REQUIRE(r.status == Status::Ok);
    const int64_t lagFrames = r.values.at("lagFrames").asInt();
    const double lagMs = r.values.at("lagMs").asDouble();
    std::printf("T-M-TR6c: delayed 4 hops: lagFrames %lld, lagMs %.3f, r = %.4f\n",
                static_cast<long long>(lagFrames), lagMs,
                r.values.at("correlationAtLag").asDouble());
    CHECK(lagFrames == 4);  // EXACT analytic golden (Delta = 4 hops)
    CHECK(lagMs == Approx(4.0 * 512.0 / 48000.0 * 1000.0).epsilon(1e-9));
    CHECK(r.values.at("correlationAtLag").asDouble() > 0.97);
    CHECK(r.unit == "ms");
  }
}

// ---------------------------------------------------------------------------
// warble-instability (direct contexts)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-TR7: warble-instability — the vibrato cancellation (the "
          "expected model removes the input's own modulation)") {
  // Input = the analytic vibrato recipe (220 Hz, 5 Hz, +-0.5 st); static
  // ratio 1.0; output = the SAME signal. Without the expected model the
  // raw 0.5 st modulation would show ~35 cents std; with it, the deviation
  // series isolates the tracker's jitter (the empirical floor class).
  const int64_t n = 48000;
  const std::vector<double> out = sineWithFInst(
      [](double t) { return 220.0 * std::exp2(0.5 * std::sin(2.0 * kPi * 5.0 * t) / 12.0); },
      48000, n);
  const std::vector<double> ratio(static_cast<std::size_t>(n), 1.0);
  auto ctx = trackerCtx(out, 48000, n, kVibrato220, "Preserving", ratio, true);
  const MetricResult r = runMetric("warble-instability", ctx);
  REQUIRE(r.status == Status::Ok);
  const double flutter = r.values.at("f0FlutterStdCents").asDouble();
  std::printf("T-M-TR7: vibrato cancellation: flutter %.4f cents (the raw "
              "modulation would be ~35), meanHz %.2f\n",
              flutter, r.values.at("f0MeanHz").asDouble());
  CHECK(flutter <= 10.0);  // PROVISIONAL/EMPIRICAL jitter-floor class
  CHECK(r.values.at("f0MeanHz").asDouble() == Approx(220.0).epsilon(0.02));
  CHECK(r.unit == "cents");
}

TEST_CASE("T-M-TR8: warble-instability — positive flutter detection + "
          "dynamic-curve gating") {
  // A 10-cent 3 Hz flutter on the OUTPUT under a constant expected model:
  // std ~ 10/sqrt(2) = 7.1 cents — above the jitter floor (the positive
  // control: the metric DETECTS flutter). f(t) = 440 * 2^(0.1*sin(2*pi*3*t)/12)
  // (0.1 SEMITONE depth = 10 cents).
  {
    const int64_t n = 48000;
    const std::vector<double> out = sineWithFInst(
        [](double t) {
          return 440.0 * std::exp2(0.1 * std::sin(2.0 * kPi * 3.0 * t) / 12.0);
        },
        48000, n);
    const std::vector<double> ratio(static_cast<std::size_t>(n), 1.0);
    auto ctx = trackerCtx(out, 48000, n, kSine440, "Preserving", ratio, true);
    const MetricResult r = runMetric("warble-instability", ctx);
    REQUIRE(r.status == Status::Ok);
    const double flutter = r.values.at("f0FlutterStdCents").asDouble();
    std::printf("T-M-TR8: 10-cent 3 Hz flutter: measured %.3f cents (expect ~7)\n",
                flutter);
    CHECK(flutter >= 2.0);
    CHECK(flutter <= 15.0);
  }
  // Dynamic curve: not-applicable (the §10.1 "under static ratio" gate).
  {
    const std::vector<double> out = constantSine(440.0, 48000, 48000);
    std::vector<double> ratio(48000, 1.0);
    for (int64_t i = 0; i < 48000; ++i) {
      ratio[static_cast<std::size_t>(i)] =
          1.0 + 0.01 * std::sin(2.0 * kPi * 0.5 * static_cast<double>(i) / 48000.0);
    }
    auto ctx = trackerCtx(out, 48000, 48000, kSine440, "Preserving", ratio, false);
    const MetricResult r = runMetric("warble-instability", ctx);
    CHECK(r.status == Status::NotApplicable);
    bool dyn = false;
    for (const std::string& note : r.notes) {
      if (note.find("dynamic curve") != std::string::npos) dyn = true;
    }
    CHECK(dyn);
  }
}

// ---------------------------------------------------------------------------
// Analyzer integration — end-to-end through the REAL harness
// ---------------------------------------------------------------------------

namespace {

/// A one-engine varispeed experiment on a recipe-declared sine asset.
struct TrackerFixture {
  TestRoot tr{"metric-tracker"};
  fs::path exp;

  TrackerFixture() {
    tr.makeCurve("t-identity", "id = \"t-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
    tr.makeCurve("t-plus12", "id = \"t-plus12\"\nkind = \"static\"\nvalue = { semitones = 12.0 }\n");
    tr.makeCurve("t-lfo", "id = \"t-lfo\"\nkind = \"lfo\"\nwave = \"sine\"\nrate = 0.5\ndepth = "
                          "{ semitones = 1.0 }\ncentre = { semitones = 0.0 }\n");
    tr.makeAsset("t-sine", 48000, {constantSine(440.0, 48000, 48000)}, "sine", nullptr,
                 "[f0]\nclass = \"constant\"\nfreqHz = 440.0\n");
    tr.makeAsset("t-vibrato", 48000,
                 {sineWithFInst(
                      [](double t) {
                        return 220.0 * std::exp2(0.5 * std::sin(2.0 * kPi * 5.0 * t) / 12.0);
                      },
                      48000, 48000)},
                 "harmonic", nullptr,
                 "[f0]\nclass = \"vibrato\"\nbaseHz = 220.0\nvibratoHz = 5.0\nvibratoSt = 0.5\n");
    exp = tr.makeExperiment(
        "t-exp",
        "id = \"t-exp\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"t-sine\"]\ncurves  = "
        "[\"t-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = [48000]\nchannels = "
        "[1]\n[[engine_config]]\nengine = \"native.varispeed\"\nparams = { "
        "resample_quality = \"reference\" }\nseed = 7\n[output]\nmaster = true\n[analysis]\n"
        "metrics = [\"pitch-error\", \"pitch-lag\", \"warble-instability\"]\n");
  }

  [[nodiscard]] RenderSummary render() const {
    const EngineRegistry engines = TestRoot::productionRegistry();
    return renderExperiment(exp, tr, engines);
  }

  [[nodiscard]] pitchlab::analysis::AnalyzeOutcome analyze(
      const std::vector<std::string>& metrics) const {
    return analyzeExperiment(exp, tr, metrics);
  }
};

}  // namespace

TEST_CASE("T-M-TR9: end-to-end — varispeed identity: pitch-error ok at the "
          "analytic 440, warble ok at the jitter floor, pitch-lag "
          "not-applicable (flat curve); cpu-cost still honestly gated") {
  TrackerFixture fx;
  REQUIRE(fx.render().allOk());
  const pitchlab::analysis::AnalyzeOutcome ao =
      fx.analyze({"pitch-error", "pitch-lag", "warble-instability", "cpu-cost"});
  REQUIRE(ao.jobs.size() == 1);
  REQUIRE(ao.jobs[0].completed);
  CHECK(ao.jobs[0].errorCount == 0);

  const Value art = json::parse(readBytes(ao.jobs[0].artifactPath), "analysis.json");
  // pitch-error: ok — the identity output carries 440 at the analytic model.
  {
    const Value& pe = findResult(art, "pitch-error");
    CHECK(pe.asObject().at("status").asString() == "ok");
    const double median = pe.asObject().at("values").asObject().at("medianCents").asDouble();
    std::printf("T-M-TR9: varispeed identity pitch-error median %.4f cents "
                "(excluded %.4f)\n",
                median,
                pe.asObject().at("values").asObject().at("excludedVoicedFraction").asDouble());
    CHECK(median <= 20.0);  // PROVISIONAL/EMPIRICAL: tracker class + the
                            // varispeed identity residual (-80 dBFS class)
    CHECK(pe.asObject().at("tolerance").asObject().at("status").asString() == "provisional");
  }
  // warble-instability: ok, static identity — no flutter by construction.
  {
    const Value& w = findResult(art, "warble-instability");
    CHECK(w.asObject().at("status").asString() == "ok");
    const double flutter =
        w.asObject().at("values").asObject().at("f0FlutterStdCents").asDouble();
    std::printf("T-M-TR9: varispeed identity warble flutter %.4f cents\n", flutter);
    CHECK(flutter <= 10.0);
  }
  // pitch-lag: the flat curve not-applicable (reachable now, §10.2).
  {
    const Value& pl = findResult(art, "pitch-lag");
    CHECK(pl.asObject().at("status").asString() == "not-applicable");
  }
  // cpu-cost: unchanged honest gate.
  {
    const Value& cc = findResult(art, "cpu-cost");
    CHECK(cc.asObject().at("status").asString() == "not-applicable");
  }
}

TEST_CASE("T-M-TR10: end-to-end — static +12 st on the recipe sine: the "
          "analytic 880 tracked through the real render path; artifacts "
          "byte-deterministic") {
  TrackerFixture fx;
  // Render with the +12 st curve (ratio 2.0): expected 440*2 = 880 (the
  // analytic expected side INDEPENDENTLY derived).
  {
    const fs::path exp12 = fx.tr.makeExperiment(
        "t-exp12",
        "id = \"t-exp12\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"t-sine\"]\ncurves  = "
        "[\"t-plus12\"]\nengines = [\"native.varispeed\"]\nsampleRates = [48000]\nchannels = "
        "[1]\n[[engine_config]]\nengine = \"native.varispeed\"\nparams = { "
        "resample_quality = \"reference\" }\nseed = 7\n[output]\nmaster = true\n[analysis]\n"
        "metrics = [\"pitch-error\", \"warble-instability\"]\n");
    const EngineRegistry engines = TestRoot::productionRegistry();
    REQUIRE(renderExperiment(exp12, fx.tr, engines).allOk());
    const pitchlab::analysis::AnalyzeOutcome ao =
        analyzeExperiment(exp12, fx.tr, {"pitch-error", "warble-instability"});
    REQUIRE(ao.jobs.size() == 1);
    REQUIRE(ao.jobs[0].completed);
    const Value art = json::parse(readBytes(ao.jobs[0].artifactPath), "analysis.json");
    const Value& pe = findResult(art, "pitch-error");
    CHECK(pe.asObject().at("status").asString() == "ok");
    const double median = pe.asObject().at("values").asObject().at("medianCents").asDouble();
    std::printf("T-M-TR10: +12 st: pitch-error median %.4f cents vs the analytic "
                "880 Hz\n",
                median);
    CHECK(median <= 20.0);
    const Value& w = findResult(art, "warble-instability");
    CHECK(w.asObject().at("status").asString() == "ok");
    CHECK(w.asObject().at("values").asObject().at("f0MeanHz").asDouble() ==
          Approx(880.0).epsilon(0.02));
  }
  // Byte determinism: delete the analysis tree of the identity experiment
  // and re-run — byte-identical artifacts (the tracker path is deterministic).
  {
    REQUIRE(fx.render().allOk());  // the identity experiment must exist first
    const pitchlab::analysis::AnalyzeOutcome first =
        fx.analyze({"pitch-error", "warble-instability", "pitch-lag"});
    REQUIRE(first.allCompleted());
    std::string before = readBytes(first.jobs[0].artifactPath);
    std::error_code ec;
    fs::remove_all(fx.tr.artifacts / "analysis", ec);
    const pitchlab::analysis::AnalyzeOutcome second =
        fx.analyze({"pitch-error", "warble-instability", "pitch-lag"});
    REQUIRE(second.allCompleted());
    CHECK(readBytes(second.jobs[0].artifactPath) == before);
  }
}

TEST_CASE("T-M-TR11: end-to-end — the vibrato asset under varispeed static "
          "identity: the expected model cancels the input modulation; the "
          "lfo curve yields a measured pitch-lag") {
  TrackerFixture fx;
  // The vibrato asset + the LFO curve (0.5 Hz +-1 st) on varispeed: the
  // curve is dynamic => pitch-lag measures (varispeed's per-sample control
  // => the lag is the tracker/HMM smoothing class, small); warble is
  // not-applicable (dynamic curve); pitch-error measures honestly.
  {
    const fs::path expLfo = fx.tr.makeExperiment(
        "t-exp-lfo",
        "id = \"t-exp-lfo\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"t-vibrato\"]\ncurves  = "
        "[\"t-lfo\"]\nengines = [\"native.varispeed\"]\nsampleRates = [48000]\nchannels = "
        "[1]\n[[engine_config]]\nengine = \"native.varispeed\"\nparams = { "
        "resample_quality = \"reference\" }\nseed = 7\n[output]\nmaster = true\n[analysis]\n"
        "metrics = [\"pitch-error\", \"pitch-lag\", \"warble-instability\"]\n");
    const EngineRegistry engines = TestRoot::productionRegistry();
    REQUIRE(renderExperiment(expLfo, fx.tr, engines).allOk());
    const pitchlab::analysis::AnalyzeOutcome ao =
        analyzeExperiment(expLfo, fx.tr, {"pitch-error", "pitch-lag", "warble-instability"});
    REQUIRE(ao.jobs.size() == 1);
    REQUIRE(ao.jobs[0].completed);
    CHECK(ao.jobs[0].errorCount == 0);
    const Value art = json::parse(readBytes(ao.jobs[0].artifactPath), "analysis.json");
    {
      const Value& pe = findResult(art, "pitch-error");
      CHECK(pe.asObject().at("status").asString() == "ok");
      const double median = pe.asObject().at("values").asObject().at("medianCents").asDouble();
      std::printf("T-M-TR11: vibrato + lfo: pitch-error median %.4f cents\n", median);
      CHECK(median <= 40.0);  // the vibrato + LFO tracking class (measured)
    }
    {
      const Value& pl = findResult(art, "pitch-lag");
      CHECK(pl.asObject().at("status").asString() == "ok");
      const double lagMs = pl.asObject().at("values").asObject().at("lagMs").asDouble();
      const double corr = pl.asObject().at("values").asObject().at("correlationAtLag").asDouble();
      std::printf("T-M-TR11: vibrato + lfo: pitch-lag %.3f ms (r = %.4f)\n", lagMs, corr);
      CHECK(corr > 0.9);
      // varispeed is per-sample: the true engine lag is 0; the measured lag
      // is the tracker/HMM smoothing class (a few hops at most).
      CHECK(std::fabs(lagMs) <= 60.0);
    }
    {
      const Value& w = findResult(art, "warble-instability");
      CHECK(w.asObject().at("status").asString() == "not-applicable");  // dynamic curve
    }
  }
}

TEST_CASE("T-M-TR12: failure model — malformed [f0] metadata => the job's "
          "analysis-error (the run would continue)") {
  TrackerFixture fx;
  // An asset with a malformed recipe block: the analysis fails at context
  // assembly with the explicit f0 error.
  fx.tr.makeAsset("t-bad-recipe", 48000, {constantSine(440.0, 48000, 24000)}, "sine", nullptr,
                  "[f0]\nclass = \"wobble\"\nfreqHz = 440.0\n");
  const fs::path expBad = fx.tr.makeExperiment(
      "t-exp-bad",
      "id = \"t-exp-bad\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"t-bad-recipe\"]\ncurves  = "
      "[\"t-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = [48000]\nchannels = "
      "[1]\n[[engine_config]]\nengine = \"native.varispeed\"\nparams = { "
      "resample_quality = \"reference\" }\nseed = 7\n[output]\nmaster = true\n[analysis]\n"
      "metrics = [\"pitch-error\"]\n");
  const EngineRegistry engines = TestRoot::productionRegistry();
  REQUIRE(renderExperiment(expBad, fx.tr, engines).allOk());
  const pitchlab::analysis::AnalyzeOutcome ao = analyzeExperiment(expBad, fx.tr, {"pitch-error"});
  REQUIRE(ao.jobs.size() == 1);
  CHECK_FALSE(ao.jobs[0].completed);
  CHECK(ao.jobs[0].error.find("f0") != std::string::npos);
  CHECK(ao.totalErrors() == 1);
}
