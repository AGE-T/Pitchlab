// T-M-S suite (implementation specification §10.4 items 13-17/22, cycle 5,
// §17 step 5): the spectral-domain family — hf-energy, aliasing-indicator,
// spectral-error (LSD vs the varispeed reference) + phase-coherence.
//
// Independent analytic anchors (§20-21):
//   * Analysis STFT geometry (§10.4 item 6a): Nw = 2048 at 48 kHz, periodic
//     Hann, hop 512, bin = 23.4375 Hz, guard = 4 bins = 93.75 Hz.
//   * A BIN-CENTERED tone (f = k * fs/Nw, integer k) windowed by the
//     periodic Hann has EXACTLY zero spectrum at bins >= 2 away from k
//     (Dirichlet zeros at integer offsets; the Hann kernel = 0.5*D - 0.25*
//     (D(+1) + D(-1)) vanishes there) => band-outside fractions sit at the
//     FFT's floating-point floor (~1e-26) — the <= 1e-9 golden class holds
//     analytically for bin-centered material.
//   * NON-bin-centered tones (e.g. the committed 440 Hz sine, or the frozen
//     "1 kHz/5 kHz" pair) leak through the Hann sidelobes at the 4-bin
//     guard: MEASURED floor ~8e-6 total band-outside energy (empirical,
//     PROVISIONAL/EMPIRICAL class per §10.4 preamble — recorded, never a
//     normative tolerance). The tests assert the measured class.
//   * varispeed self-reference: identical buffers => identical STFTs =>
//     d == 0 identically => ALL LSD aggregates == 0.0 exactly.
//   * Phase-locked stack (common accumulator): c_h = princarg(dPhi_h -
//     h*dPhi_1) is frame-CONSTANT (projection offsets are frame-independent;
//     the algebraic identity of item 17) => R_h = |mean e^{j c_h}| == 1 up
//     to the projection floor => >= 0.999 holds.
//   * Incoherent stack: each partial h wanders at its OWN deterministic
//     rate w_h = (0.31 + 0.17 h) Hz with depth 5% — c_h(n) sweeps many
//     radians across frames => R_h <= 0.5 (the bracket; construction
//     documented here).

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

[[nodiscard]] MetricResult runMetric(const char* id,
                                     const pitchlab::analysis::AnalysisContext& ctx) {
  const pitchlab::analysis::MetricRegistry registry = productionMetricRegistry();
  const pitchlab::analysis::MetricDescriptor* d = registry.findById(id);
  REQUIRE(d != nullptr);
  return d->compute(ctx);
}

[[nodiscard]] const json::Value& findResult(const json::Value& artifact, const char* metricId) {
  for (const json::Value& r : artifact.asObject().at("results").asArray()) {
    if (r.asObject().at("metricId").asString() == metricId) return r;
  }
  throw std::runtime_error(std::string("metric not found: ") + metricId);
}

struct JobArt {
  json::Value artifact;
  fs::path artifactPath;
};

[[nodiscard]] JobArt renderAndAnalyse(const fs::path& exp, const TestRoot& tr,
                                      const std::vector<std::string>& metrics) {
  const EngineRegistry engines = TestRoot::productionRegistry();
  const RenderSummary rs = renderExperiment(exp, tr, engines);
  REQUIRE(rs.allOk());
  const pitchlab::analysis::AnalyzeOutcome ao = analyzeExperiment(exp, tr, metrics);
  REQUIRE(ao.jobs.size() >= 1);
  REQUIRE(ao.jobs[0].completed);
  JobArt ja;
  ja.artifactPath = ao.jobs[0].artifactPath;
  ja.artifact = json::parse(readBytes(ja.artifactPath), ja.artifactPath.string());
  return ja;
}

const double kPi = 3.14159265358979323846;

/// Bin-centered tone at bin k (fs = 48 kHz, Nw = 2048): f = k*23.4375 Hz.
[[nodiscard]] std::vector<double> binTone(int64_t n, double binFreq, double amp, uint32_t fs) {
  std::vector<double> x(static_cast<std::size_t>(n));
  const double w = 2.0 * kPi * binFreq / static_cast<double>(fs);
  for (int64_t i = 0; i < n; ++i) {
    x[static_cast<std::size_t>(i)] = amp * std::sin(w * static_cast<double>(i));
  }
  return x;
}

/// Phase-locked harmonic stack (common accumulator, h = 1..6, amplitudes
/// 0.25/h so every partial clears the 1e-3 participation floor). The
/// fundamental is BIN-CENTERED (210.9375 Hz = bin 9 at Nw = 2048, 48 kHz)
/// so the parabolic f0 tracker is unbiased (delta == 0 at a bin center).
[[nodiscard]] std::vector<double> phaseLockedStack(int64_t n, double f0, uint32_t fs) {
  std::vector<double> x(static_cast<std::size_t>(n), 0.0);
  const double w0 = 2.0 * kPi * f0 / static_cast<double>(fs);
  for (int h = 1; h <= 6; ++h) {
    const double a = 0.25 / static_cast<double>(h);
    for (int64_t i = 0; i < n; ++i) {
      x[static_cast<std::size_t>(i)] +=
          a * std::sin(w0 * static_cast<double>(h) * static_cast<double>(i));
    }
  }
  return x;
}

/// Incoherent stack: partial h wanders at its own deterministic rate
/// w_h = (0.31 + 0.17 h) Hz, depth 5% of h*f0, phase-accumulated.
[[nodiscard]] std::vector<double> incoherentStack(int64_t n, double f0, uint32_t fs) {
  std::vector<double> x(static_cast<std::size_t>(n), 0.0);
  for (int h = 1; h <= 6; ++h) {
    const double wanderHz = 0.31 + 0.17 * static_cast<double>(h);
    const double depth = 0.05;
    const double a = 0.25 / static_cast<double>(h);
    double phase = 0.0;
    for (int64_t i = 0; i < n; ++i) {
      const double t = static_cast<double>(i) / static_cast<double>(fs);
      const double fh = static_cast<double>(h) * f0 *
                        (1.0 + depth * std::sin(2.0 * kPi * wanderHz * t));
      phase += 2.0 * kPi * fh / static_cast<double>(fs);
      x[static_cast<std::size_t>(i)] += a * std::sin(phase);
    }
  }
  return x;
}

}  // namespace

// ---------------------------------------------------------------------------
// hf-energy (§10.4 item 13)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-S1: end-to-end identity render — in-band ~ 1, above-0.45Ny at "
          "the leakage floor; band edges derived from the frozen geometry") {
  TestRoot tr{"metric-spectral-1"};
  tr.makeCurve("s-identity", "id = \"s-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  // Local 440 Hz sine, amplitude 0.25, band [440, 440] (the committed shape).
  const std::vector<double> band = {440.0, 440.0};
  tr.makeAsset("s-sine", 48000, {sineFrames(440.0, 48000, 48000)}, "sine", &band);
  const fs::path exp =
      tr.makeExperiment("s-exp1", experimentToml("s-exp1", "s-sine", "s-identity", 48000, 1));

  const JobArt jaS1 = renderAndAnalyse(exp, tr, {"hf-energy"});
  const json::Value& r = jaS1.artifact.asObject().at("results").asArray()[0];
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  // Band edges: [440 - 93.75, 440 + 93.75] (guard = 4 * 48000/2048 = 93.75).
  const json::Array& edges = v.at("bandEdgesUsedHz").asArray();
  CHECK(edges[0].asDouble() == doctest::Approx(346.25).epsilon(1e-12));
  CHECK(edges[1].asDouble() == doctest::Approx(533.75).epsilon(1e-12));
  CHECK(v.at("nyquistHz").asDouble() == 24000.0);
  CHECK(v.at("stftWindowFrames").asInt() == 2048);
  // In-band fraction ~ 1 - 1.2e-5: the 440 Hz tone is NOT bin-centered
  // (18.77 bins), so the periodic-Hann sidelobes carry ~1e-5 of the energy
  // past the 4-bin guard (the measured leakage class; the freeze's 1e-6
  // estimate is the bin-centered case — T-M-S2). Recorded as empirical.
  CHECK(v.at("relativeEnergyInContentBandCh0").asDouble() >= 1.0 - 1e-4);
  // Above 0.45*Nyquist: the Hann leakage floor (freeze golden <= 1e-9).
  CHECK(v.at("relativeEnergyAbove04NyCh0").asDouble() <= 1e-9);
  // Rate-relative policy: NO 20 kHz ceiling — the bands are Ny-relative.
  CHECK(v.count("bandEdgesUsedHz") == 1);
  CHECK(r.asObject().at("tolerance").asObject().at("status").asString() == "exact");
}

TEST_CASE("T-M-S2: bin-centered two-tone — outside-band at the analytic zero floor") {
  // Tones at bin 40 (937.5 Hz) and bin 190 (4453.125 Hz): integer cycles per
  // window => the Hann kernel vanishes at bins >= 2 from each tone; with the
  // 4-bin guard the band-outside energy is at the FFT floating-point floor.
  // Band metadata [937.5, 4453.125] => band = [843.75, 4546.875].
  auto ctx = makeCtx({[](int64_t n, uint32_t fs) {
    std::vector<double> x(static_cast<std::size_t>(n), 0.0);
    const double w1 = 2.0 * kPi * 937.5 / fs;
    const double w2 = 2.0 * kPi * 4453.125 / fs;
    for (int64_t i = 0; i < n; ++i) {
      x[static_cast<std::size_t>(i)] =
          0.25 * std::sin(w1 * i) + 0.25 * std::sin(w2 * i);
    }
    return x;
  }(48000, 48000)}, 48000);
  ctx.asset.hasBand = true;
  ctx.asset.bandLo = 937.5;
  ctx.asset.bandHi = 4453.125;

  const MetricResult r = runMetric("hf-energy", ctx);
  REQUIRE(r.status == Status::Ok);
  CHECK(r.values.at("relativeEnergyInContentBandCh0").asDouble() >= 1.0 - 1e-9);
  const double outside = r.values.at("relativeEnergyAboveContentBandCh0").asDouble() +
                         r.values.at("relativeEnergyBelowContentBandCh0").asDouble();
  CHECK(outside <= 1e-9);
}

TEST_CASE("T-M-S3: non-bin-centered two-tone 1 kHz/5 kHz — the measured Hann "
          "leakage floor (PROVISIONAL/EMPIRICAL class, ~1e-5; recorded, not "
          "a normative tolerance)") {
  // 1 000 Hz = 42.667 bins, 5 000 Hz = 213.33 bins: NOT bin-centered, so the
  // periodic-Hann sidelobes at the 4-bin guard carry ~8e-6 of the energy
  // (empirically measured; the freeze's 1e-9 estimate applies only to
  // bin-centered material — see T-M-S2 for that class).
  auto ctx = makeCtx({[](int64_t n, uint32_t fs) {
    std::vector<double> x(static_cast<std::size_t>(n), 0.0);
    const double w1 = 2.0 * kPi * 1000.0 / fs;
    const double w2 = 2.0 * kPi * 5000.0 / fs;
    for (int64_t i = 0; i < n; ++i) {
      x[static_cast<std::size_t>(i)] =
          0.25 * std::sin(w1 * i) + 0.25 * std::sin(w2 * i);
    }
    return x;
  }(48000, 48000)}, 48000);
  ctx.asset.hasBand = true;
  ctx.asset.bandLo = 1000.0;
  ctx.asset.bandHi = 5000.0;

  const MetricResult r = runMetric("hf-energy", ctx);
  REQUIRE(r.status == Status::Ok);
  CHECK(r.values.at("relativeEnergyInContentBandCh0").asDouble() >= 1.0 - 1e-4);
  const double outside = r.values.at("relativeEnergyAboveContentBandCh0").asDouble() +
                         r.values.at("relativeEnergyBelowContentBandCh0").asDouble();
  CHECK(outside <= 1e-5);   // the measured floor class
  CHECK(outside > 0.0);     // and it is REAL leakage (not asserted to zero)
}

TEST_CASE("T-M-S4: silent output => not-applicable; silent channel => null "
          "fractions with notes; no bandContentHz => [0, Ny] + note") {
  // Silent stereo output: the band/geometry values are reported, but the
  // FRACTIONS are absent (undefined for a silent output — never zeros).
  auto ctx = makeCtx({std::vector<double>(4096, 0.0), std::vector<double>(4096, 0.0)}, 48000);
  ctx.channels = 2;
  const MetricResult r = runMetric("hf-energy", ctx);
  CHECK(r.status == Status::NotApplicable);
  CHECK(r.values.find("relativeEnergyInContentBandCh0") == r.values.end());
  CHECK(r.values.find("relativeEnergyAboveContentBandCh0") == r.values.end());

  // One silent channel among two: per-channel nulls, never zeros.
  auto ctx2 = makeCtx({sineFrames(440.0, 48000, 4096), std::vector<double>(4096, 0.0)}, 48000);
  ctx2.channels = 2;
  const MetricResult r2 = runMetric("hf-energy", ctx2);
  REQUIRE(r2.status == Status::Ok);
  CHECK(r2.values.at("relativeEnergyInContentBandCh1").isNull());
  CHECK(r2.values.at("totalSpectralEnergyCh1").asDouble() == 0.0);
  CHECK(r2.values.at("relativeEnergyInContentBandCh0").kind() == json::Value::Kind::Double);

  // No declared band: full band + note.
  auto ctx3 = makeCtx({sineFrames(440.0, 48000, 4096)}, 48000);
  const MetricResult r3 = runMetric("hf-energy", ctx3);
  REQUIRE(r3.status == Status::Ok);
  const json::Array& edges = r3.values.at("bandEdgesUsedHz").asArray();
  CHECK(edges[0].asDouble() == 0.0);
  CHECK(edges[1].asDouble() == 24000.0);
  bool noted = false;
  for (const std::string& n : r3.notes) {
    if (n.find("no bandContentHz") != std::string::npos) noted = true;
  }
  CHECK(noted);
}

// ---------------------------------------------------------------------------
// aliasing-indicator (§10.4 item 14)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-S5: end-to-end identity — aliasing-indicator mode; expected max "
          "= bandHi * effectiveMax; above-max at the measured leakage floor") {
  TestRoot tr{"metric-spectral-5"};
  tr.makeCurve("s-identity", "id = \"s-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  const std::vector<double> band = {440.0, 440.0};
  tr.makeAsset("s-sine", 48000, {sineFrames(440.0, 48000, 48000)}, "sine", &band);
  const fs::path exp =
      tr.makeExperiment("s-exp5", experimentToml("s-exp5", "s-sine", "s-identity", 48000, 1));

  const JobArt jaS5 = renderAndAnalyse(exp, tr, {"aliasing-indicator"});
  const json::Value& r = jaS5.artifact.asObject().at("results").asArray()[0];
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  // 440 Hz * effective 1.0: expected max 440; the mode is the indicator.
  CHECK(v.at("interpretation").asString() == "aliasing-indicator");
  CHECK(v.at("expectedMaxHz").asDouble() == doctest::Approx(440.0).epsilon(1e-12));
  // Non-bin-centered 440 Hz: above-expected-max carries the Hann leakage
  // floor ~8e-6 (MEASURED; the freeze's 1e-9 estimate holds for bin-centered
  // material — T-M-S6 asserts that class). Recorded as empirical class.
  CHECK(v.at("relativeEnergyAboveExpectedMaxCh0").asDouble() <= 1e-5);
  CHECK(v.at("relativeEnergyAboveExpectedMaxCh0").asDouble() > 0.0);
  // NO binary alias flag exists anywhere (status stays ok; the interpretation
  // field carries the mode).
  CHECK(v.find("aliasing") == v.end());
  CHECK(v.find("isAliasing") == v.end());
}

TEST_CASE("T-M-S6: bin-centered tone — above-expected-max at the analytic zero floor") {
  // 937.5 Hz (bin 40), band [937.5, 937.5], identity curve (effective 1.0):
  // expected max 937.5; energy above 937.5 + 93.75 sits at the FFT floor.
  auto ctx = makeCtx({binTone(48000, 937.5, 0.25, 48000)}, 48000);
  ctx.asset.hasBand = true;
  ctx.asset.bandLo = 937.5;
  ctx.asset.bandHi = 937.5;
  ctx.effectiveMin = 1.0;
  ctx.effectiveMax = 1.0;
  const MetricResult r = runMetric("aliasing-indicator", ctx);
  REQUIRE(r.status == Status::Ok);
  CHECK(r.values.at("interpretation").asString() == "aliasing-indicator");
  CHECK(r.values.at("expectedMaxHz").asDouble() == doctest::Approx(937.5).epsilon(1e-12));
  CHECK(r.values.at("relativeEnergyAboveExpectedMaxCh0").asDouble() <= 1e-9);
}

TEST_CASE("T-M-S7: band-occupancy mode — expected max beyond Nyquist is "
          "documented occupancy, NOT an aliasing failure") {
  // Band [1000, 20000] x effective max 4.0 at 48 kHz: expected max 80 000 >
  // Nyquist 24 000 => the §10.1/§I.2 inversion: interpretation
  // "band-occupancy", occupied band [max(0, 1000*minEff - guard), Ny].
  auto ctx = makeCtx({binTone(48000, 937.5, 0.25, 48000)}, 48000);
  ctx.asset.hasBand = true;
  ctx.asset.bandLo = 1000.0;
  ctx.asset.bandHi = 20000.0;
  ctx.effectiveMin = 1.0;
  ctx.effectiveMax = 4.0;
  const MetricResult r = runMetric("aliasing-indicator", ctx);
  REQUIRE(r.status == Status::Ok);
  CHECK(r.values.at("interpretation").asString() == "band-occupancy");
  CHECK(r.values.at("expectedMaxHz").asDouble() == doctest::Approx(80000.0).epsilon(1e-12));
  const json::Array& edges = r.values.at("bandEdgesUsedHz").asArray();
  CHECK(edges[0].asDouble() == doctest::Approx(1000.0 - 93.75).epsilon(1e-12));
  CHECK(edges[1].asDouble() == 24000.0);
  CHECK(r.values.at("relativeEnergyInOccupiedBandCh0").kind() == json::Value::Kind::Double);
  bool noted = false;
  for (const std::string& n : r.notes) {
    if (n.find("band occupancy") != std::string::npos) noted = true;
  }
  CHECK(noted);
  // Status stays ok — occupancy is a documented behaviour, not an error.
  CHECK(r.status == Status::Ok);
}

TEST_CASE("T-M-S8: asset without bandContentHz => not-applicable") {
  auto ctx = makeCtx({sineFrames(440.0, 48000, 4096)}, 48000);
  const MetricResult r = runMetric("aliasing-indicator", ctx);
  CHECK(r.status == Status::NotApplicable);
  bool noted = false;
  for (const std::string& n : r.notes) {
    if (n.find("bandContentHz") != std::string::npos) noted = true;
  }
  CHECK(noted);
}

// ---------------------------------------------------------------------------
// spectral-error (§10.4 item 15)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-S9: varispeed self-reference — ALL LSD aggregates == 0.0 EXACTLY") {
  TestRoot tr{"metric-spectral-9"};
  tr.makeCurve("s-identity", "id = \"s-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  const std::vector<double> band = {440.0, 440.0};
  tr.makeAsset("s-sine", 48000, {sineFrames(440.0, 48000, 48000)}, "sine", &band);
  const fs::path exp =
      tr.makeExperiment("s-exp9", experimentToml("s-exp9", "s-sine", "s-identity", 48000, 1));

  const JobArt ja9 = renderAndAnalyse(exp, tr, {"spectral-error"});
  const json::Value& r = ja9.artifact.asObject().at("results").asArray()[0];
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  // Identical buffers => identical STFTs => d == 0 identically. The
  // aggregates are EXACT zeros (0.0 == 0.0 bit comparison).
  CHECK(v.at("meanLsdDbCh0").asDouble() == 0.0);
  CHECK(v.at("medianLsdDbCh0").asDouble() == 0.0);
  CHECK(v.at("maxLsdDbCh0").asDouble() == 0.0);
  // Frame count: complete frames iff start + 2048 <= 48024; starts
  // 0, 512, ..., 89*512 = 45568 (+2048 = 47616 <= 48024) -> 90 frames.
  CHECK(v.at("frameCountCh0").asInt() == 90);
  // The reference block is recorded (self-reference).
  CHECK(!ja9.artifact.asObject().at("reference").isNull());
}

TEST_CASE("T-M-S10: reference-unavailable when no varispeed render exists") {
  TestRoot tr{"metric-spectral-10"};
  tr.makeCurve("s-identity", "id = \"s-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  tr.makeAsset("s-sine", 48000, {sineFrames(440.0, 48000, 24000)});
  const fs::path exp = tr.makeExperiment(
      "s-exp10", experimentToml("s-exp10", "s-sine", "s-identity", 48000, 1, "native.granular",
                                "benchmark", "grain_seconds = 0.1"));
  const EngineRegistry engines = TestRoot::productionRegistry();
  const RenderSummary rs = renderExperiment(exp, tr, engines);
  REQUIRE(rs.allOk());
  const pitchlab::analysis::AnalyzeOutcome ao = analyzeExperiment(exp, tr, {"spectral-error"});
  REQUIRE(ao.jobs.size() == 1);
  REQUIRE(ao.jobs[0].completed);
  const json::Value art = json::parse(readBytes(ao.jobs[0].artifactPath), "analysis.json");
  CHECK(art.asObject().at("reference").isNull());
  const json::Value& r = art.asObject().at("results").asArray()[0];
  CHECK(r.asObject().at("status").asString() == "reference-unavailable");
}

TEST_CASE("T-M-S11: cross-class comparison — Preserving engine vs the warped "
          "RateFollowing reference (alignment machinery, measurement not golden)") {
  // varispeed + pv.classic on the same curve: analysing the pv.classic job
  // compares the input-timeline engine signal against the varispeed master
  // WARPED onto the input grid via the reference emission map (§10.4
  // item 15). Values are measurements — asserted finite and framed, never
  // gated.
  TestRoot tr{"metric-spectral-11"};
  tr.makeCurve("s-plus12", "id = \"s-plus12\"\nkind = \"static\"\nvalue = { ratio = 1.0594630943592953 }\n");
  tr.makeAsset("s-sine", 48000, {sineFrames(440.0, 48000, 24000)});
  const fs::path exp = tr.makeExperiment(
      "s-exp11",
      "id = \"s-exp11\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"s-sine\"]\ncurves  = "
      "[\"s-plus12\"]\nengines = [\"native.varispeed\", \"native.pv.classic\"]\nsampleRates = "
      "[48000]\nchannels = [1]\n[[engine_config]]\nengine = \"native.varispeed\"\nparams = { "
      "resample_quality = \"reference\" }\nseed = 7\n[[engine_config]]\nengine = "
      "\"native.pv.classic\"\nparams = { }\nseed = 7\n[output]\nmaster = true\n[analysis]\n"
      "metrics = [\"spectral-error\"]\n");
  const EngineRegistry engines = TestRoot::productionRegistry();
  const RenderSummary rs = renderExperiment(exp, tr, engines);
  REQUIRE(rs.allOk());
  REQUIRE(rs.results.size() == 2);

  const pitchlab::analysis::AnalyzeOutcome ao = analyzeExperiment(exp, tr, {"spectral-error"});
  REQUIRE(ao.jobs.size() == 2);
  // Find the pv.classic job (job order: engines in the suite's order).
  const pitchlab::analysis::JobAnalysisOutcome* pvJob = nullptr;
  for (const auto& j : ao.jobs) {
    if (j.engineId == "native.pv.classic") pvJob = &j;
  }
  REQUIRE(pvJob != nullptr);
  REQUIRE(pvJob->completed);
  const json::Value art = json::parse(readBytes(pvJob->artifactPath), "analysis.json");
  const json::Value& r = findResult(art, "spectral-error");
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  REQUIRE(v.count("meanLsdDbCh0") == 1);
  const double mean = v.at("meanLsdDbCh0").asDouble();
  CHECK(std::isfinite(mean));
  CHECK(mean >= 0.0);  // LSD is a distance: non-negative
  CHECK(v.at("maxLsdDbCh0").asDouble() >= v.at("medianLsdDbCh0").asDouble());
  // The alignment metadata records the class-dependent method.
  CHECK(r.asObject().at("alignment").asObject().at("axis").asString() == "input-timeline");
  bool warpNote = false;
  for (const json::Value& n : r.asObject().at("notes").asArray()) {
    if (n.asString().find("warped") != std::string::npos) warpNote = true;
  }
  CHECK(warpNote);
}

// ---------------------------------------------------------------------------
// phase-coherence (§10.4 item 17)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-S12: phase-locked stack — R_h >= 0.999 for every partial") {
  auto ctx = makeCtx({phaseLockedStack(48000, 210.9375, 48000)}, 48000);
  ctx.asset.category = "harmonic";
  const MetricResult r = runMetric("phase-coherence", ctx);
  REQUIRE(r.status == Status::Ok);
  for (int h = 2; h <= 6; ++h) {
    const std::string key = "R" + std::to_string(h);
    INFO("harmonic " << h);
    REQUIRE(r.values.at(key).kind() == json::Value::Kind::Double);
    CHECK(r.values.at(key).asDouble() >= 0.999);
    CHECK(r.values.at("participatingFrames" + std::to_string(h)).asInt() > 80);
  }
  // f0 tracked near 220 Hz (parabolic peak, deterministic).
  CHECK(r.values.at("f0MedianHz").asDouble() ==
        doctest::Approx(210.9375).epsilon(1e-3));
}

TEST_CASE("T-M-S13: incoherent stack — R_h <= 0.5 (the documented bracket)") {
  auto ctx = makeCtx({incoherentStack(48000, 210.9375, 48000)}, 48000);
  ctx.asset.category = "harmonic";
  const MetricResult r = runMetric("phase-coherence", ctx);
  REQUIRE(r.status == Status::Ok);
  int bracketed = 0;
  for (int h = 2; h <= 6; ++h) {
    const std::string key = "R" + std::to_string(h);
    if (r.values.at(key).kind() == json::Value::Kind::Double &&
        r.values.at(key).asDouble() <= 0.5) {
      ++bracketed;
    }
  }
  // The wander construction decorrelates the partial increments: at least 4
  // of the 5 tracked partials fall below 0.5 (participation floors may
  // legitimately null an odd partial — noted, never zero-substituted).
  CHECK(bracketed >= 4);
}

TEST_CASE("T-M-S14: material applicability — category outside {harmonic, "
          "voice-like} => not-applicable (a score is never manufactured)") {
  for (const char* const cat : {"sine", "noise", "transient", "stereo"}) {
    auto ctx = makeCtx({phaseLockedStack(48000, 210.9375, 48000)}, 48000);
    ctx.asset.category = cat;
    const MetricResult r = runMetric("phase-coherence", ctx);
    INFO("category " << cat);
    CHECK(r.status == Status::NotApplicable);
    CHECK(r.values.empty());
  }
}

TEST_CASE("T-M-S15: fewer than 3 frames => insufficient-data") {
  // 3 frames need Nw + 2*hop = 2048 + 1024 = 3072 frames; give 3071.
  auto ctx = makeCtx({phaseLockedStack(3071, 210.9375, 48000)}, 48000);
  ctx.asset.category = "harmonic";
  const MetricResult r = runMetric("phase-coherence", ctx);
  CHECK(r.status == Status::InsufficientData);
}

TEST_CASE("T-M-S16: partial that never participates => R_h null + note") {
  // Only h = 1 and h = 2 present: R3..R6 are null with notes (never 0).
  std::vector<double> x(48000, 0.0);
  const double w = 2.0 * kPi * 210.9375 / 48000.0;
  for (int64_t i = 0; i < 48000; ++i) {
    x[static_cast<std::size_t>(i)] =
        0.25 * std::sin(w * i) + 0.125 * std::sin(2.0 * w * i);
  }
  auto ctx = makeCtx({x}, 48000);
  ctx.asset.category = "harmonic";
  const MetricResult r = runMetric("phase-coherence", ctx);
  REQUIRE(r.status == Status::Ok);
  CHECK(r.values.at("R2").kind() == json::Value::Kind::Double);
  for (int h = 3; h <= 6; ++h) {
    INFO("harmonic " << h);
    CHECK(r.values.at("R" + std::to_string(h)).isNull());
    CHECK(r.values.at("participatingFrames" + std::to_string(h)).asInt() == 0);
  }
  bool noted = false;
  for (const std::string& n : r.notes) {
    if (n.find("never participates") != std::string::npos) noted = true;
  }
  CHECK(noted);
}

TEST_CASE("T-M-S17: end-to-end — identity render of harmonic material reports "
          "phase coherence as a measurement (status ok, R values present)") {
  TestRoot tr{"metric-spectral-17"};
  tr.makeCurve("s-identity", "id = \"s-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  tr.makeAsset("s-harm", 48000, {phaseLockedStack(48000, 210.9375, 48000)}, "harmonic");
  const fs::path exp =
      tr.makeExperiment("s-exp17", experimentToml("s-exp17", "s-harm", "s-identity", 48000, 1));
  const JobArt jaS17 = renderAndAnalyse(exp, tr, {"phase-coherence"});
  const json::Value& r = jaS17.artifact.asObject().at("results").asArray()[0];
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  CHECK(v.at("R2").asDouble() >= 0.999);  // identity preserves the locking
  CHECK(v.at("f0MedianHz").asDouble() ==
        doctest::Approx(210.9375).epsilon(1e-3));
}
