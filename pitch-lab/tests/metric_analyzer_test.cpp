// T-M-A suite (implementation specification §10.4 items 2/3/5/7/19/20/21/22,
// cycle 5, §17 step 5): the Analyzer end-to-end — canonical-JSON reader
// round-trip, metric-registry semantics, config loading, artifact schema +
// determinism (byte identity), the failure-model matrix, f0-recipe
// statuses, reference resolution.
//
// The end-to-end path is the REAL one: TestRoot -> production engine
// registry -> ExperimentCompiler -> OfflineRenderer -> render manifests ->
// analyzeJobs (the same functions the CLI calls). No second renderer, no
// fake engines, no re-render anywhere.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "test_fixtures.h"

using namespace pitchlab;
using namespace pitchlab::test;
using json::Value;
using json::Object;
using json::Array;

namespace {

[[nodiscard]] const Value& findResult(const Value& artifact, const char* metricId) {
  for (const Value& r : artifact.asObject().at("results").asArray()) {
    if (r.asObject().at("metricId").asString() == metricId) return r;
  }
  throw std::runtime_error(std::string("metric not found: ") + metricId);
}

template <typename Fn>
ConfigError expectConfigError(Fn&& fn) {
  bool threw = false;
  ConfigError err("", "", "");
  try {
    fn();
  } catch (const ConfigError& e) {
    threw = true;
    err = e;
  }
  REQUIRE(threw);
  return err;
}

/// A three-engine experiment on the local sine asset (varispeed reference
/// role + two Preserving engines) — the cross-engine analysis fixture.
struct AnalyzeFixture {
  TestRoot tr{"metric-analyzer"};
  fs::path exp;

  AnalyzeFixture() {
    tr.makeCurve("a-identity", "id = \"a-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
    tr.makeAsset("a-sine", 48000, {sineFrames(440.0, 48000, 24000)});
    exp = tr.makeExperiment(
        "a-exp",
        "id = \"a-exp\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-sine\"]\ncurves  = "
        "[\"a-identity\"]\nengines = [\"native.varispeed\", \"native.granular\", "
        "\"native.pv.classic\"]\nsampleRates = [48000]\nchannels = [1]\n[[engine_config]]\n"
        "engine = \"native.varispeed\"\nparams = { resample_quality = \"reference\" }\nseed = "
        "7\n[[engine_config]]\nengine = \"native.granular\"\nparams = { grain_seconds = 0.1 }\n"
        "seed = 7\n[[engine_config]]\nengine = \"native.pv.classic\"\nparams = { }\nseed = "
        "7\n[output]\nmaster = true\n[analysis]\nmetrics = [\"spectral-error\"]\n");
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

// ---------------------------------------------------------------------------
// Canonical JSON reader round-trip (§10.4 item 5/21)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-A1: json round-trip — serialize -> parse reproduces the value "
          "tree exactly (doubles %.17g, escapes, nesting)") {
  Object inner;
  inner["a"] = Value(1.0 / 3.0);          // non-dyadic double
  inner["b"] = Value(0.1);                // the classic round-trip probe
  inner["w"] = Value(1.0);                // whole double: %.17g emits "1"
  inner["c"] = Value(static_cast<int64_t>(-42));
  inner["d"] = Value(true);
  inner["e"] = Value(nullptr);
  Array arr;
  arr.push_back(Value("quote\"backslash\\tab\t"));
  arr.push_back(Value(std::string("unicode-esc \x01\x07")));
  arr.push_back(Value(3.14159265358979323846));
  Object root;
  root["inner"] = Value(inner);
  root["list"] = Value(arr);
  root["s"] = Value("plain");

  const std::string text = json::serialize(Value(root));
  const Value parsed = json::parse(text, "roundtrip.json");
  const std::string text2 = json::serialize(parsed);
  CHECK(text == text2);  // canonical: same tree => same bytes
  // Spot-check the values (double exactness via %.17g).
  const Object& in2 = parsed.asObject().at("inner").asObject();
  CHECK(in2.at("a").asDouble() == 1.0 / 3.0);
  CHECK(in2.at("b").asDouble() == 0.1);
  // Whole doubles serialise as integers and re-parse as Int: asDouble() is
  // TOTAL (reads the integer as a double) — the canonical round-trip
  // property the analysis artifacts rely on.
  CHECK(in2.at("w").asDouble() == 1.0);
  CHECK(in2.at("c").asInt() == -42);
  CHECK(in2.at("d").asBool() == true);
  CHECK(in2.at("e").isNull());
  const Array& l2 = parsed.asObject().at("list").asArray();
  CHECK(l2[0].asString() == "quote\"backslash\\tab\t");
  CHECK(l2[2].asDouble() == 3.14159265358979323846);
}

TEST_CASE("T-M-A2: json reader rejects malformed input (CONFIG ERROR)") {
  expectConfigError([] { (void)json::parse("{\"a\":1,\"a\":2}", "d"); });      // duplicate keys
  expectConfigError([] { (void)json::parse("{\"a\":1} trailing", "d"); });     // trailing content
  expectConfigError([] { (void)json::parse("{\"a\": bad}", "d"); });           // bare word
  expectConfigError([] { (void)json::parse("{\"a\": \"unterminated}", "d"); }); // unterminated string
  expectConfigError([] { (void)json::parse("[1, 2", "d"); });                  // unclosed array
  expectConfigError([] { (void)json::parse("{\"ok\": 1}\n{\"more\": 2}", "d"); }); // two roots
}

// ---------------------------------------------------------------------------
// Metric registry semantics (§10.4 item 2)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-A3: the production registry — 15 metrics, §10.1 order, "
          "requiresReference exactly for the reference-dependent pair") {
  const pitchlab::analysis::MetricRegistry registry = productionMetricRegistry();
  REQUIRE(registry.size() == 15);
  // The §10.1 metric module sheet order, verbatim.
  static const char* kOrder[15] = {
      "pitch-error",        "pitch-lag",           "spectral-error",
      "transient-preservation", "onset-timing",    "phase-coherence",
      "stereo-coherence",   "latency",             "realised-duration",
      "cpu-cost",           "peak-rms-crest",      "hf-energy",
      "aliasing-indicator", "amplitude-modulation", "warble-instability",
  };
  for (std::size_t i = 0; i < 15; ++i) {
    INFO("index " << i);
    CHECK(std::string(registry.at(i).id) == kOrder[i]);
    CHECK(registry.at(i).compute != nullptr);  // anti-fake rule
    CHECK(registry.at(i).version == 1);
    CHECK(registry.indexOf(kOrder[i]) == static_cast<int64_t>(i));
  }
  CHECK(registry.findById("spectral-error")->requiresReference);
  CHECK(registry.findById("transient-preservation")->requiresReference);
  for (const char* id : {"pitch-error", "onset-timing", "stereo-coherence", "cpu-cost",
                         "peak-rms-crest", "hf-energy", "aliasing-indicator",
                         "amplitude-modulation", "latency", "realised-duration"}) {
    CHECK(!registry.findById(id)->requiresReference);
  }
  // Unknown id: nullptr / -1 — never a default metric.
  CHECK(registry.findById("overall-quality") == nullptr);
  CHECK(registry.indexOf("overall-quality") == -1);
}

TEST_CASE("T-M-A4: registry defences — duplicate id, null compute, empty id throw") {
  pitchlab::analysis::MetricRegistry registry;
  registry.registerMetric({"m1", 1, [](const pitchlab::analysis::AnalysisContext&) {
                            return pitchlab::analysis::MetricResult{};
                          }, false});
  CHECK_THROWS(registry.registerMetric({"m1", 1, nullptr, false}));
  CHECK_THROWS(registry.registerMetric({"", 1, nullptr, false}));
  CHECK_THROWS(registry.registerMetric({"m2", 1, nullptr, false}));
}

// ---------------------------------------------------------------------------
// Config loading (§10.4 items 2/5)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-A5: loadMetricConfig — the committed shape parses; disabled "
          "metrics; unknown keys are CONFIG ERRORs") {
  TestRoot tr{"metric-analyzer-cfg"};

  // The TestRoot's committed-shape file: all 15 enabled.
  const pitchlab::analysis::MetricConfig cfg = pitchlab::analysis::loadMetricConfig(tr.root);
  for (std::size_t i = 0; i < 15; ++i) {
    CHECK(cfg.enabled[i]);
  }
  // A disabled flag is honoured (selection is the config's ONLY role —
  // identity stays in code).
  tr.writeFile(tr.root / "config" / "metrics.toml",
               "version = 1\n[alignment]\ncommon_axis = \"input-timeline\"\n"
               "rate_following_warp = \"emission-map\"\n"
               "cross_class_warp = \"warp-reference-to-engine-timeline\"\n"
               "[metrics]\nstereo_coherence = false\n");
  const pitchlab::analysis::MetricConfig off = pitchlab::analysis::loadMetricConfig(tr.root);
  const pitchlab::analysis::MetricRegistry reg = productionMetricRegistry();
  CHECK(!off.enabled[static_cast<std::size_t>(reg.indexOf("stereo-coherence"))]);
  CHECK(off.enabled[static_cast<std::size_t>(reg.indexOf("peak-rms-crest"))]);

  // Unknown metric key / unknown alignment key: CONFIG ERROR (identity and
  // the frozen axis policy are code-owned; a typo'd config fails loudly).
  tr.writeFile(tr.root / "config" / "metrics.toml",
               "version = 1\n[alignment]\ncommon_axis = \"input-timeline\"\n"
               "rate_following_warp = \"emission-map\"\n"
               "cross_class_warp = \"warp-reference-to-engine-timeline\"\n"
               "[metrics]\noverall_quality = true\n");
  expectConfigError([&] { (void)pitchlab::analysis::loadMetricConfig(tr.root); });
  tr.writeFile(tr.root / "config" / "metrics.toml",
               "version = 1\n[alignment]\ncommon_axis = \"output-timeline\"\n");
  expectConfigError([&] { (void)pitchlab::analysis::loadMetricConfig(tr.root); });
}

TEST_CASE("T-M-A6: the COMMITTED config/metrics.toml parses with the full "
          "[metrics] table (regression: no phantom 'typo')") {
  // Byte-verified: the committed file's [metrics] header is intact and the
  // parser reads 15 boolean entries. (The earlier session's 'typo' report
  // was a display-pipeline artifact — the file was never wrong; this test
  // pins the real state.)
  const pitchlab::analysis::MetricConfig cfg =
      pitchlab::analysis::loadMetricConfig(fs::path(PITCHLAB_SOURCE_DIR));
  for (std::size_t i = 0; i < 15; ++i) {
    CHECK(cfg.enabled[i]);
  }
}

// ---------------------------------------------------------------------------
// Analyzer end-to-end (§10.4 items 3/20/21)
// ---------------------------------------------------------------------------

TEST_CASE("T-M-A7: end-to-end — schema, result ordering, provenance, f0-recipe "
          "gating, cpu-cost gating, self-reference golden") {
  AnalyzeFixture fx;
  REQUIRE(fx.render().allOk());

  const std::vector<std::string> metrics = {"peak-rms-crest", "spectral-error", "pitch-error",
                                            "cpu-cost", "onset-timing"};
  const pitchlab::analysis::AnalyzeOutcome ao = fx.analyze(metrics);
  REQUIRE(ao.jobs.size() == 3);
  for (const auto& j : ao.jobs) {
    REQUIRE(j.completed);
    CHECK(j.errorCount == 0);
  }

  // The varispeed job: the reference is its own render (self-reference).
  const pitchlab::analysis::JobAnalysisOutcome* varispeedJob = nullptr;
  const pitchlab::analysis::JobAnalysisOutcome* granularJob = nullptr;
  for (const auto& j : ao.jobs) {
    if (j.engineId == "native.varispeed") varispeedJob = &j;
    if (j.engineId == "native.granular") granularJob = &j;
  }
  REQUIRE(varispeedJob != nullptr);
  REQUIRE(granularJob != nullptr);

  const Value art = json::parse(readBytes(varispeedJob->artifactPath), "analysis.json");
  const Object& root = art.asObject();
  CHECK(root.at("schema").asString() == "pitchlab.analysis.v1");
  // Build block: version/phase/compiler (no timestamps, no run-unique data).
  const Object& build = root.at("build").asObject();
  CHECK(build.at("pitchlabVersion").kind() == Value::Kind::String);
  CHECK(build.at("phase").kind() == Value::Kind::String);
  CHECK(build.at("compiler").kind() == Value::Kind::String);
  // Experiment + render provenance.
  CHECK(root.at("experiment").asObject().at("id").asString() == "a-exp");
  CHECK(root.at("render").asObject().at("engineId").asString() == "native.varispeed");
  CHECK(root.at("render").asObject().at("renderStatus").asString() == "ok");
  CHECK(root.at("render").asObject().at("taints").asArray().empty());
  // Requested metrics recorded verbatim.
  const Array& req = root.at("requestedMetrics").asArray();
  REQUIRE(req.size() == 5);
  CHECK(req[0].asString() == "peak-rms-crest");
  // Results in REGISTRATION order (deterministic, §10.4 item 3): the
  // selection {peak-rms-crest, spectral-error, pitch-error, cpu-cost,
  // onset-timing} maps to registry indices {10, 2, 0, 9, 4} => ordered:
  // pitch-error, spectral-error, onset-timing, cpu-cost, peak-rms-crest.
  const Array& results = root.at("results").asArray();
  REQUIRE(results.size() == 5);
  CHECK(results[0].asObject().at("metricId").asString() == "pitch-error");
  CHECK(results[1].asObject().at("metricId").asString() == "spectral-error");
  CHECK(results[2].asObject().at("metricId").asString() == "onset-timing");
  CHECK(results[3].asObject().at("metricId").asString() == "cpu-cost");
  CHECK(results[4].asObject().at("metricId").asString() == "peak-rms-crest");
  // Per-result provenance (§4.10 made concrete) on the spectral-error entry.
  const Object& r0 = results[1].asObject();
  CHECK(r0.at("engineId").asString() == "native.varispeed");
  CHECK(r0.at("inputAssetId").asString() == "a-sine");
  CHECK(r0.at("curveId").asString() == "a-identity");
  CHECK(r0.at("sampleRate").asInt() == 48000);
  CHECK(r0.at("channels").asInt() == 1);
  CHECK(r0.at("renderManifestReference").kind() == Value::Kind::String);
  CHECK(r0.at("unit").kind() == Value::Kind::String);
  CHECK(r0.at("values").kind() == Value::Kind::Object);
  CHECK(r0.at("alignment").kind() == Value::Kind::Object);
  CHECK(r0.at("method").kind() == Value::Kind::Object);
  CHECK(r0.at("tolerance").kind() == Value::Kind::Object);
  CHECK(r0.at("notes").kind() == Value::Kind::Array);
  // Self-reference golden: varispeed spectral-error == 0.0 EXACTLY.
  const Object& se = r0.at("values").asObject();
  CHECK(se.at("meanLsdDbCh0").asDouble() == 0.0);
  CHECK(se.at("maxLsdDbCh0").asDouble() == 0.0);
  CHECK(root.at("reference").asObject().at("manifestFile").kind() == Value::Kind::String);
  // onset-timing on sine material: the ramp-up crosses the frozen
  // threshold => ONE measured onset, error 0 at identity (honest "ok").
  const Object& ot = findResult(art, "onset-timing").asObject();
  CHECK(ot.at("status").asString() == "ok");
  CHECK(ot.at("values").asObject().at("matched").asInt() == 1);
  CHECK(ot.at("values").asObject().at("onsetErrorsFrames").asArray()[0].asInt() == 0);
  // cpu-cost: gated not-applicable with the honest note.
  const Object& cc = findResult(art, "cpu-cost").asObject();
  CHECK(cc.at("status").asString() == "not-applicable");
  CHECK(cc.at("values").asObject().at("realTimeFactor").isNull());
  // Tracker-dependent metric on an asset WITHOUT an analytic f0 recipe:
  // not-applicable with the §10.5 item 10 recipe note (the tracker itself
  // is implemented — the honest gate is the missing analytic expected f0,
  // NOT a tracker-unavailable stub; OD-12 is DONE for this metric family).
  const Object& pe = findResult(art, "pitch-error").asObject();
  CHECK(pe.at("status").asString() == "not-applicable");
  bool recipeNote = false;
  for (const Value& n : pe.at("notes").asArray()) {
    if (n.asString().find("no analytic f0 recipe") != std::string::npos) recipeNote = true;
  }
  CHECK(recipeNote);
  // The tracker identity is recorded in the method metadata.
  CHECK(pe.at("method").asObject().at("tracker").asString().find("pYIN") != std::string::npos);
  // The granular job: cross-class spectral-error measured (status ok,
  // finite; the reference is varispeed's render).
  const Value gart = json::parse(readBytes(granularJob->artifactPath), "analysis.json");
  const Object& gse = findResult(gart, "spectral-error").asObject();
  CHECK(gse.at("status").asString() == "ok");
  CHECK(std::isfinite(gse.at("values").asObject().at("meanLsdDbCh0").asDouble()));
  CHECK(gart.asObject().at("reference").asObject().at("manifestFile").kind() ==
        Value::Kind::String);
}

TEST_CASE("T-M-A8: artifact determinism — delete + re-analyse => byte-identical") {
  AnalyzeFixture fx;
  REQUIRE(fx.render().allOk());
  const pitchlab::analysis::AnalyzeOutcome first =
      fx.analyze({"peak-rms-crest", "spectral-error"});
  REQUIRE(first.allCompleted());
  std::vector<std::pair<std::string, std::string>> before;
  for (const auto& j : first.jobs) {
    before.emplace_back(j.artifactPath.string(), readBytes(j.artifactPath));
  }
  // Delete the whole analysis tree and re-run.
  std::error_code ec;
  fs::remove_all(fx.tr.artifacts / "analysis", ec);
  const pitchlab::analysis::AnalyzeOutcome second =
      fx.analyze({"peak-rms-crest", "spectral-error"});
  REQUIRE(second.allCompleted());
  for (std::size_t i = 0; i < before.size(); ++i) {
    INFO("job " << before[i].first);
    CHECK(fs::exists(before[i].first));
    CHECK(readBytes(before[i].first) == before[i].second);  // byte identity
  }
}

TEST_CASE("T-M-A9: failure model — missing manifest / missing master / hash "
          "mismatch / malformed manifest / curve drift (explicit per-job "
          "analysis-errors; the run continues)") {
  // Case 1: analysis before render — a compiled-but-never-rendered fixture.
  {
    AnalyzeFixture fresh;
    const pitchlab::analysis::AnalyzeOutcome ao = fresh.analyze({"realised-duration"});
    REQUIRE(ao.jobs.size() == 3);
    for (const auto& j : ao.jobs) {
      CHECK(!j.completed);
      CHECK(j.error.find("missing render manifest") != std::string::npos);
      CHECK(j.error.find("never re-renders") != std::string::npos);
    }
  }

  // Cases 2-5 mutate one fixture sequentially (each on a distinct job).
  AnalyzeFixture fx;
  REQUIRE(fx.render().allOk());
  const pitchlab::analysis::MetricRegistry reg = productionMetricRegistry();
  const EngineRegistry engines = TestRoot::productionRegistry();
  const HarnessConfig cfg = loadHarnessConfig(fx.tr.root);
  const CompileOutcome outcome =
      compileExperiment(fx.exp, engines, fx.tr.root, fx.tr.artifacts, cfg);
  REQUIRE(outcome.jobs.size() == 3);
  const std::vector<std::string> metrics = {"realised-duration"};
  const pitchlab::analysis::MetricConfig mc = pitchlab::analysis::loadMetricConfig(fx.tr.root);

  // Case 2: missing master WAV (job 0) — the run continues on the others.
  {
    const fs::path master = renderPathsFor(outcome.jobs[0]).masterWav;
    REQUIRE(fs::exists(master));
    fs::remove(master);
    const pitchlab::analysis::AnalyzeOutcome ao =
        pitchlab::analysis::analyzeJobs(outcome.jobs, metrics, reg, mc);
    REQUIRE(ao.jobs.size() == 3);
    CHECK(!ao.jobs[0].completed);
    CHECK(ao.jobs[0].error.find("missing master WAV") != std::string::npos);
    CHECK(ao.jobs[1].completed);
    CHECK(ao.jobs[2].completed);
  }

  // Case 3: master WAV modified after render (sha256 mismatch, job 1).
  {
    const fs::path master = renderPathsFor(outcome.jobs[1]).masterWav;
    std::ofstream app(master, std::ios::binary | std::ios::app);
    app << "x";
    app.close();
    const pitchlab::analysis::AnalyzeOutcome ao =
        pitchlab::analysis::analyzeJobs(outcome.jobs, metrics, reg, mc);
    CHECK(!ao.jobs[1].completed);
    CHECK(ao.jobs[1].error.find("sha256 mismatch") != std::string::npos);
  }

  // Case 4: malformed manifest JSON (job 2).
  {
    const fs::path manifest = renderPathsFor(outcome.jobs[2]).manifestJson;
    fx.tr.writeFile(manifest, "{ not json");
    const pitchlab::analysis::AnalyzeOutcome ao =
        pitchlab::analysis::analyzeJobs(outcome.jobs, metrics, reg, mc);
    CHECK(!ao.jobs[2].completed);
  }

  // Case 5: curve drift — a FRESH rendered fixture (the shared one carries
  // the damage from cases 2-4 by design); rewrite the authored curve after
  // the render => every job's recompiled hash disagrees with its manifest.
  {
    AnalyzeFixture drifted;
    REQUIRE(drifted.render().allOk());
    drifted.tr.makeCurve(
        "a-identity", "id = \"a-identity\"\nkind = \"static\"\nvalue = { ratio = 0.75 }\n");
    const pitchlab::analysis::AnalyzeOutcome ao = drifted.analyze({"realised-duration"});
    REQUIRE(ao.jobs.size() == 3);
    for (const auto& j : ao.jobs) {
      CHECK(!j.completed);
      CHECK(j.error.find("curve drift") != std::string::npos);
    }
  }
}

TEST_CASE("T-M-A10: unknown metric id => CONFIG ERROR (identity lives in code)") {
  AnalyzeFixture fx;
  REQUIRE(fx.render().allOk());
  const ConfigError err = expectConfigError([&] { (void)fx.analyze({"overall-quality"}); });
  CHECK(err.field().find("overall-quality") != std::string::npos);
}

TEST_CASE("T-M-A11: disabled-in-config metric requested => not-applicable "
          "with the note (never silently dropped)") {
  AnalyzeFixture fx;
  REQUIRE(fx.render().allOk());
  fx.tr.writeFile(fx.tr.root / "config" / "metrics.toml",
                  "version = 1\n[alignment]\ncommon_axis = \"input-timeline\"\n"
                  "rate_following_warp = \"emission-map\"\n"
                  "cross_class_warp = \"warp-reference-to-engine-timeline\"\n"
                  "[metrics]\npeak_rms_crest = false\n");
  const pitchlab::analysis::AnalyzeOutcome ao = fx.analyze({"peak-rms-crest"});
  REQUIRE(ao.jobs.size() == 3);
  for (const auto& j : ao.jobs) {
    REQUIRE(j.completed);
    const Value art = json::parse(readBytes(j.artifactPath), "analysis.json");
    const Object& r = art.asObject().at("results").asArray()[0].asObject();
    CHECK(r.at("status").asString() == "not-applicable");
    bool noted = false;
    for (const Value& n : r.at("notes").asArray()) {
      if (n.asString().find("disabled in config/metrics.toml") != std::string::npos) {
        noted = true;
      }
    }
    CHECK(noted);
  }
}

TEST_CASE("T-M-A12: reference resolution — no varispeed in the run => "
          "reference-unavailable for the reference-dependent metrics") {
  TestRoot tr{"metric-analyzer-12"};
  tr.makeCurve("a-identity", "id = \"a-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  tr.makeAsset("a-sine", 48000, {sineFrames(440.0, 48000, 24000)});
  const fs::path exp = tr.makeExperiment(
      "a-exp12", experimentToml("a-exp12", "a-sine", "a-identity", 48000, 1, "native.vardelay",
                                "benchmark", ""));
  const EngineRegistry engines = TestRoot::productionRegistry();
  REQUIRE(renderExperiment(exp, tr, engines).allOk());
  const pitchlab::analysis::AnalyzeOutcome ao = analyzeExperiment(exp, tr, {"spectral-error"});
  REQUIRE(ao.jobs.size() == 1);
  REQUIRE(ao.jobs[0].completed);
  const Value art = json::parse(readBytes(ao.jobs[0].artifactPath), "analysis.json");
  CHECK(art.asObject().at("reference").isNull());
  CHECK(findResult(art, "spectral-error").asObject().at("status").asString() ==
        "reference-unavailable");
}

TEST_CASE("T-M-A13: the analysis NEVER re-renders — the render artifacts are "
          "immutable through analysis") {
  AnalyzeFixture fx;
  REQUIRE(fx.render().allOk());
  // Snapshot every manifest + master hash before analysis.
  const EngineRegistry engines = TestRoot::productionRegistry();
  const HarnessConfig cfg = loadHarnessConfig(fx.tr.root);
  const CompileOutcome outcome =
      compileExperiment(fx.exp, engines, fx.tr.root, fx.tr.artifacts, cfg);
  std::map<std::string, std::string> before;
  for (const auto& j : outcome.jobs) {
    const RenderPaths rp = renderPathsFor(j);
    before[rp.manifestJson.string()] = readBytes(rp.manifestJson);
    before[rp.masterWav.string()] = readBytes(rp.masterWav);
  }
  REQUIRE(fx.analyze({"peak-rms-crest", "spectral-error", "realised-duration"}).allCompleted());
  for (const auto& [path, bytes] : before) {
    INFO(path);
    CHECK(readBytes(path) == bytes);  // byte-identical: no mutation anywhere
  }
}
