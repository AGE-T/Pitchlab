// T-M-O suite (implementation specification §10.4 items 11-12/22, cycle 5,
// §17 step 5): onset-timing + transient-preservation goldens + the frozen
// onset detector's determinism.
//
// Independent analytic anchors (§20-21):
//   * The detector definition itself (§10.4 item 6d): envelope = max_c |x_c|,
//     theta = 0.25 * max(e), refractory = round(0.05 * fs); onset = first
//     frame of each maximal run. Synthetic impulse placements are chosen by
//     the TEST, so the expected onsets are read off the construction.
//   * The committed percussive corpus recipe (config/corpus.toml
//     §11.2 "percussive-bursts"): burstPeriodMs = 250 at 48 kHz = 12 000
//     frames, 2 s duration, first burst at frame 0 => exactly 8 bursts
//     (0, 12 000, ..., 84 000). The frozen detector empirically finds all 8
//     (verified; the burst decay tau = 15 ms keeps every burst above
//     theta = 0.25 * max for hundreds of frames).
//   * varispeed identity is timeline-aligned (r(m) = m): expected output
//     position of input onset p is p; the identity output reproduces the
//     input waveform to the identity criterion => the SAME detector output
//     => every matched error == 0 exactly.
//   * varispeed self-reference: the reference IS the analysed render =>
//     identical windows => correlations == 1 (floating-point floor), onset
//     errors vs reference == 0 exactly (integer arithmetic).
//   * The committed impulse-train corpus (spacingMs = 1.0 => 48 frames)
//     documents the detector's resolution limit: all impulses merge into ONE
//     maximal run (48 << refractory 2 400) => 1 onset. This is the honest
//     documented behaviour, not a defect.

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
using pitchlab::analysis::OnsetDetection;

namespace {

[[nodiscard]] std::vector<double> impulseSignal(int64_t n, const std::vector<int64_t>& at) {
  std::vector<double> x(static_cast<std::size_t>(n), 0.0);
  for (int64_t p : at) {
    if (p >= 0 && p < n) x[static_cast<std::size_t>(p)] = 1.0;
  }
  return x;
}

/// Render + analyse one experiment and parse the single job's artifact.
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
  REQUIRE(ao.jobs.size() == 1);
  REQUIRE(ao.jobs[0].completed);
  JobArt ja;
  ja.artifactPath = ao.jobs[0].artifactPath;
  ja.artifact = json::parse(readBytes(ja.artifactPath), ja.artifactPath.string());
  return ja;
}

}  // namespace

// ---------------------------------------------------------------------------
// Detector determinism (§10.4 item 6d, direct calls)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-O1: single impulse — onset at the impulse, theta = 0.25*max") {
  const std::vector<std::vector<double>> ch = {impulseSignal(48000, {1000})};
  const OnsetDetection det = pitchlab::analysis::detectOnsets(ch, 48000.0);
  REQUIRE(det.onsets.size() == 1);
  CHECK(det.onsets[0] == 1000);
  CHECK(det.threshold == doctest::Approx(0.25).epsilon(1e-15));
  CHECK(det.refractoryFrames == 2400);  // round(0.05 * 48000)
}

TEST_CASE("T-M-O2: two impulses 12 000 apart => two onsets (250 ms apart > refractory)") {
  const std::vector<std::vector<double>> ch = {impulseSignal(48000, {1000, 13000})};
  const OnsetDetection det = pitchlab::analysis::detectOnsets(ch, 48000.0);
  REQUIRE(det.onsets.size() == 2);
  CHECK(det.onsets[0] == 1000);
  CHECK(det.onsets[1] == 13000);
}

TEST_CASE("T-M-O3: impulses within the refractory merge into ONE maximal run") {
  const std::vector<std::vector<double>> ch = {impulseSignal(48000, {1000, 1100})};
  const OnsetDetection det = pitchlab::analysis::detectOnsets(ch, 48000.0);
  REQUIRE(det.onsets.size() == 1);  // 100-frame gap << 2 400 refractory
  CHECK(det.onsets[0] == 1000);
}

TEST_CASE("T-M-O4: a run starting at frame 0 counts") {
  const std::vector<std::vector<double>> ch = {impulseSignal(48000, {0, 12000})};
  const OnsetDetection det = pitchlab::analysis::detectOnsets(ch, 48000.0);
  REQUIRE(det.onsets.size() == 2);
  CHECK(det.onsets[0] == 0);
}

TEST_CASE("T-M-O5: detector is deterministic (identical result on repeated call)") {
  const std::vector<std::vector<double>> ch = {impulseSignal(48000, {500, 13000, 25000})};
  const OnsetDetection a = pitchlab::analysis::detectOnsets(ch, 48000.0);
  const OnsetDetection b = pitchlab::analysis::detectOnsets(ch, 48000.0);
  REQUIRE(a.onsets == b.onsets);
  CHECK(a.threshold == b.threshold);
  CHECK(a.refractoryFrames == b.refractoryFrames);
}

TEST_CASE("T-M-O6: stereo envelope is the channel MAX (impulse in one channel)") {
  const std::vector<std::vector<double>> ch = {std::vector<double>(48000, 0.0),
                                               impulseSignal(48000, {2000})};
  const OnsetDetection det = pitchlab::analysis::detectOnsets(ch, 48000.0);
  REQUIRE(det.onsets.size() == 1);
  CHECK(det.onsets[0] == 2000);
}

// ---------------------------------------------------------------------------
// onset-timing goldens through the real path
// ---------------------------------------------------------------------------

TEST_CASE("T-M-O7: varispeed identity on the committed percussive corpus — "
          "8 recipe onsets, every matched error == 0 exactly") {
  TestRoot tr{"metric-onset-7"};
  tr.copyCommittedAsset("syn-transient-percussive-2s-48k");
  tr.makeCurve("o-identity", "id = \"o-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  const fs::path exp = tr.makeExperiment(
      "o-exp7",
      experimentToml("o-exp7", "syn-transient-percussive-2s-48k", "o-identity", 48000, 1));

  const JobArt jaO = renderAndAnalyse(exp, tr, {"onset-timing"});
  const json::Value& r = jaO.artifact.asObject().at("results").asArray()[0];
  CHECK(r.asObject().at("metricId").asString() == "onset-timing");
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();

  // The corpus RECIPE is the expectation source: burst period 250 ms at
  // 48 kHz, 2 s, first burst at frame 0 => 8 bursts. The detector finds all
  // 8 in the input (expected) AND in the identity output (measured).
  CHECK(v.at("expectedOnsetCount").asInt() == 8);
  CHECK(v.at("measuredOnsetCount").asInt() == 8);
  CHECK(v.at("matched").asInt() == 8);
  CHECK(v.at("missed").asInt() == 0);
  CHECK(v.at("extra").asInt() == 0);
  // Identity is timeline-aligned: every matched error is 0 EXACTLY
  // (integer frame arithmetic; the identity render reproduces the input to
  // the identity criterion => the identical detector output).
  const json::Array& errs = v.at("onsetErrorsFrames").asArray();
  REQUIRE(errs.size() == 8);
  for (const json::Value& e : errs) {
    CHECK(e.asInt() == 0);
  }
  CHECK(v.at("meanAbsErrorFrames").asDouble() == 0.0);
  CHECK(v.at("maxAbsErrorFrames").asInt() == 0);
  // Detector constants are frozen in the method metadata.
  const json::Object& method = r.asObject().at("method").asObject();
  CHECK(method.at("refractoryMs").asDouble() == 50.0);
  // Measurement metric: tolerance "none" (no gate).
  CHECK(r.asObject().at("tolerance").asObject().at("status").asString() == "none");
}

TEST_CASE("T-M-O8: the committed impulse-train corpus documents the resolution limit") {
  // spacingMs = 1.0 => impulses every 48 frames — far below the 2 400-frame
  // refractory: the detector reports ONE merged onset. Denser material is
  // resolution-limited BY DESIGN (§10.4 item 6d) — the honest result is 1,
  // not a fabricated per-impulse count.
  TestRoot tr{"metric-onset-8"};
  tr.copyCommittedAsset("syn-transient-impulses-2s-48k");
  tr.makeCurve("o-identity", "id = \"o-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  const fs::path exp = tr.makeExperiment(
      "o-exp8",
      experimentToml("o-exp8", "syn-transient-impulses-2s-48k", "o-identity", 48000, 1));

  const JobArt jaO = renderAndAnalyse(exp, tr, {"onset-timing"});
  const json::Value& r = jaO.artifact.asObject().at("results").asArray()[0];
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  CHECK(v.at("expectedOnsetCount").asInt() == 1);
  CHECK(v.at("measuredOnsetCount").asInt() == 1);
  CHECK(v.at("matched").asInt() == 1);
  CHECK(v.at("onsetErrorsFrames").asArray().size() == 1);
  CHECK(v.at("onsetErrorsFrames").asArray()[0].asInt() == 0);
}

TEST_CASE("T-M-O9: silent input => onset-timing not-applicable (the frozen "
          "detector reports no onsets only for silence)") {
  // The frozen detector's theta = 0.25*max: ANY signal with dynamic range
  // above 25% of its own maximum produces at least one run (the sine ramp
  // case is REAL onset behaviour — T-M-D1 measures it); only a silent input
  // yields zero runs => the honest not-applicable.
  TestRoot tr{"metric-onset-9"};
  tr.makeCurve("o-identity", "id = \"o-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  tr.makeAsset("o-silence", 48000, {std::vector<double>(24000, 0.0)});
  const fs::path exp =
      tr.makeExperiment("o-exp9", experimentToml("o-exp9", "o-silence", "o-identity", 48000, 1));
  const JobArt jaO = renderAndAnalyse(exp, tr, {"onset-timing"});
  const json::Value& r = jaO.artifact.asObject().at("results").asArray()[0];
  CHECK(r.asObject().at("status").asString() == "not-applicable");
  bool noted = false;
  for (const json::Value& n : r.asObject().at("notes").asArray()) {
    if (n.asString().find("no detectable onsets") != std::string::npos) noted = true;
  }
  CHECK(noted);
}

// ---------------------------------------------------------------------------
// transient-preservation goldens
// ---------------------------------------------------------------------------

TEST_CASE("T-M-O10: varispeed self-reference — correlations == 1 (fp floor), "
          "onset errors vs reference == 0 exactly") {
  // The reference resolution finds the varispeed render in the same
  // experiment run: for the varispeed job itself the reference IS its own
  // render (the self-comparison anchor, §10.1).
  TestRoot tr{"metric-onset-10"};
  tr.copyCommittedAsset("syn-transient-percussive-2s-48k");
  tr.makeCurve("o-identity", "id = \"o-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  const fs::path exp = tr.makeExperiment(
      "o-exp10",
      experimentToml("o-exp10", "syn-transient-percussive-2s-48k", "o-identity", 48000, 1));

  const JobArt jaO = renderAndAnalyse(exp, tr, {"transient-preservation"});
  const json::Value& r = jaO.artifact.asObject().at("results").asArray()[0];
  CHECK(r.asObject().at("metricId").asString() == "transient-preservation");
  CHECK(r.asObject().at("status").asString() == "ok");
  const json::Object& v = r.asObject().at("values").asObject();
  // Onset-time errors vs the reference: identical onsets => 0 EXACTLY.
  const json::Array& errRef = v.at("onsetTimeErrorVsReferenceFrames").asArray();
  REQUIRE(errRef.size() == 8);
  for (const json::Value& e : errRef) {
    CHECK(e.asInt() == 0);
  }
  // Attack correlations: the reference IS the analysed render => identical
  // windows => normalised correlation == 1 within the floating-point floor
  // (sxy == sxx == syy bit-identically; the sqrt round-trip gives 1 - eps).
  const json::Array& corr = v.at("attackCorrelation").asArray();
  REQUIRE(corr.size() == 8);
  for (const json::Value& c : corr) {
    CHECK(c.asDouble() > 1.0 - 1e-12);
  }
  CHECK(v.at("meanAttackCorrelationCh0").asDouble() > 1.0 - 1e-12);
  // The two components exist SEPARATELY (never merged into one score).
  CHECK(v.count("onsetTimeErrorVsInputFrames") == 1);
  CHECK(v.count("attackCorrelation") == 1);
  CHECK(v.count("onsetTimeErrorVsReferenceFrames") == 1);
  CHECK(v.count("meanAttackCorrelationCh0") == 1);
  CHECK(v.find("transientPreservationScore") == v.end());
  // Framed as distance from reference behaviour (§10.3), noted.
  bool refNote = false;
  for (const json::Value& n : r.asObject().at("notes").asArray()) {
    if (n.asString().find("reference behaviour") != std::string::npos) refNote = true;
  }
  CHECK(refNote);
}

TEST_CASE("T-M-O11: no varispeed render in the run => reference-unavailable "
          "(never a re-render, never a silent skip)") {
  TestRoot tr{"metric-onset-11"};
  tr.copyCommittedAsset("syn-transient-percussive-2s-48k");
  tr.makeCurve("o-identity", "id = \"o-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  const fs::path exp = tr.makeExperiment(
      "o-exp11",
      experimentToml("o-exp11", "syn-transient-percussive-2s-48k", "o-identity", 48000, 1,
                     "native.granular", "benchmark", "grain_seconds = 0.1"));
  const JobArt ja = renderAndAnalyse(exp, tr, {"transient-preservation"});
  const json::Value& r = ja.artifact.asObject().at("results").asArray()[0];
  CHECK(r.asObject().at("status").asString() == "reference-unavailable");
  bool noted = false;
  for (const json::Value& n : r.asObject().at("notes").asArray()) {
    if (n.asString().find("never re-rendered") != std::string::npos) noted = true;
  }
  CHECK(noted);
  // Reference block is null in the artifact (§10.4 item 3).
  CHECK(ja.artifact.asObject().at("reference").isNull());
}
