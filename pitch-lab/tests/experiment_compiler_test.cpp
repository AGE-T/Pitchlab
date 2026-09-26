// T-E15 (harness slice) + T-E16/T-E17 semantics + compiler behaviour —
// ExperimentCompiler validation matrix (implementation specification
// §4.8/§4.8.1 items 1-4, cycle 3).
//
// Covers: schema validation (unknown keys, bad kind, non-first-class rates,
// channels), engine resolution against the registry (unknown engine id),
// engine_config rules (missing/duplicate/foreign, missing seed, negative
// seed, unknown parameter key), asset resolution (missing asset, corpus
// integrity mismatch), job expansion + visible skips (rate-mismatch,
// channel-mismatch, sample-rate-unsupported, channels-unsupported,
// ratio-out-of-range), creative saturation (§4.4.6: requested untouched,
// effective clamped, RANGE_SATURATED), determinism of compile outcomes.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <set>

#include "test_fixtures.h"

using namespace pitchlab;
using namespace pitchlab::test;
namespace fs = std::filesystem;

namespace {

struct Fixture {
  TestRoot tr{"compiler"};
  EngineRegistry registry{TestRoot::productionRegistry()};

  Fixture() {
    tr.makeCurve("c-identity", "id = \"c-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
    tr.makeCurve("c-low", "id = \"c-low\"\nkind = \"static\"\nvalue = { ratio = 0.25 }\n");
    tr.makeCurve("c-high", "id = \"c-high\"\nkind = \"static\"\nvalue = { ratio = 2.0 }\n");
    tr.makeCurve("c-vhigh", "id = \"c-vhigh\"\nkind = \"static\"\nvalue = { ratio = 3.0 }\n");
    tr.makeAsset("a-48k", 48000, {sineFrames(440.0, 48000, 12000)});
    tr.makeAsset("a-44k1", 44100, {sineFrames(440.0, 44100, 12000)});
    tr.makeAsset("a-st48k", 48000, {sineFrames(440.0, 48000, 12000),
                                    sineFrames(660.0, 48000, 12000)});
  }

  [[nodiscard]] HarnessConfig cfg() const { return loadHarnessConfig(tr.root); }

  CompileOutcome compileText(const std::string& toml) const {
    const fs::path p = tr.makeExperiment("exp-inline", toml);
    return compileExperiment(p, registry, tr.root, tr.artifacts, cfg());
  }
};

template <typename Fn>
ConfigError expectConfigError(Fn&& fn, const std::string& fieldNeedle = "") {
  bool threw = false;
  ConfigError err("", "", "");
  try {
    fn();
  } catch (const ConfigError& e) {
    threw = true;
    err = e;
  }
  REQUIRE(threw);
  if (!fieldNeedle.empty()) {
    CHECK(err.field().find(fieldNeedle) != std::string::npos);
  }
  return err;
}

std::string validToml(const std::string& engine = "native.varispeed") {
  return "id = \"ok\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\"]\ncurves  = "
         "[\"c-identity\"]\nengines = [\"" + engine +
         "\"]\nsampleRates = [48000]\nchannels = [1]\n[[engine_config]]\nengine = \"" + engine +
         "\"\nparams = { resample_quality = \"reference\" }\nseed = 1\n[output]\nmaster = "
         "true\n";
}

}  // namespace

// Narrow-range + rate/channel-limited test dummy for skip/saturation tests.
namespace {

class NarrowDummy : public PitchEngine {
 public:
  const char* engineId() const override { return "test.narrow"; }
  void configure(const EngineConfiguration&) override {}
  void prepare(const ProcessContext&) override {}
  Latency latency() const override { return Latency{0, 0}; }
  ProcessReport process(const AudioBlockView&, int inFrames, AudioBlockOut&, int,
                        const PitchCurveView&, FrameCount) override {
    return ProcessReport{inFrames, 0, false};
  }
  ProcessReport finish(AudioBlockOut&, int) override { return ProcessReport{0, 0, true}; }
  void reset() override {}
};

EngineDescriptor overRangeDummyDescriptor(double minRatio, double maxRatio) {
  EngineDescriptor d;
  d.info.id = "test.narrow";
  d.info.displayName = "narrow-range dummy";
  d.info.version = "0.0-test";
  d.info.usageClass = UsageClass::Prototype;
  d.info.origin = "test-only";
  d.info.license = "n/a";
  d.capabilities.minRatio = minRatio;
  d.capabilities.maxRatio = maxRatio;
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.duration = DurationBehaviour::RateFollowing;
  d.capabilities.supportedSampleRates = {48000u};
  d.factory = []() -> std::unique_ptr<PitchEngine> { return std::make_unique<NarrowDummy>(); };
  return d;
}

}  // namespace

TEST_CASE_FIXTURE(Fixture, "valid experiment compiles to one job") {
  const CompileOutcome out = compileText(validToml());
  REQUIRE(out.jobs.size() == 1);
  CHECK(out.skips.empty());
  const RenderJob& j = out.jobs[0];
  CHECK(j.experimentId == "ok");
  CHECK(j.kind == ExperimentKind::Benchmark);
  CHECK(j.engineId == "native.varispeed");
  CHECK(j.engineConfig.seed == 1);
  CHECK(j.assetId == "a-48k");
  CHECK(j.sampleRate == 48000);
  CHECK(j.channels == 1);
  CHECK(j.inputFrames == 12000);
  CHECK(j.requestedSpec.id == "c-identity");
  REQUIRE(j.requestedSignal != nullptr);
  REQUIRE(j.effectiveSignal != nullptr);
  CHECK(j.requestedSignal->ratios.size() == 12000);
  CHECK(!j.saturated);
  // Effective aliases requested when in range (content-identical).
  CHECK(j.effectiveSignal->ratios == j.requestedSignal->ratios);
  // Default block schedule = harness.toml block_frames (owner-locked L-3).
  CHECK(j.blockSchedule.kind == BlockSchedule::Kind::Constant);
  CHECK(j.blockSchedule.frames[0] == 4096);
}

TEST_CASE_FIXTURE(Fixture, "compile determinism: same input, same outcome") {
  const CompileOutcome a = compileText(validToml());
  const CompileOutcome b = compileText(validToml());
  REQUIRE(a.jobs.size() == b.jobs.size());
  REQUIRE(a.jobs.size() == 1);
  CHECK(a.jobs[0].engineId == b.jobs[0].engineId);
  CHECK(a.jobs[0].assetId == b.jobs[0].assetId);
  CHECK(a.jobs[0].engineConfig.seed == b.jobs[0].engineConfig.seed);
  CHECK(a.jobs[0].requestedSignal->ratios == b.jobs[0].requestedSignal->ratios);
  CHECK(a.jobs[0].effectiveSignal->ratios == b.jobs[0].effectiveSignal->ratios);
  CHECK(a.skips.size() == b.skips.size());
}

TEST_CASE_FIXTURE(Fixture, "schema validation matrix (T-E15 harness slice)") {
  SUBCASE("unknown top-level key") {
    expectConfigError([&] { (void)compileText(validToml() + "bogus = 1\n"); }, "bogus");
  }
  SUBCASE("bad kind") {
    expectConfigError([&] { compileText("id = \"x\"\nkind = \"science\"\n[suite]\ninputs  = "
                                        "[\"a-48k\"]\ncurves  = [\"c-identity\"]\nengines = "
                                        "[\"native.varispeed\"]\nsampleRates = [48000]\nchannels "
                                        "= [1]\n[[engine_config]]\nengine = "
                                        "\"native.varispeed\"\nparams = { }\nseed = 1\n"); },
                      "kind");
  }
  SUBCASE("missing id") {
    expectConfigError([&] { compileText("kind = \"benchmark\"\n[suite]\ninputs  = "
                                        "[\"a-48k\"]\ncurves  = [\"c-identity\"]\nengines = "
                                        "[\"native.varispeed\"]\nsampleRates = [48000]\nchannels "
                                        "= [1]\n[[engine_config]]\nengine = "
                                        "\"native.varispeed\"\nparams = { }\nseed = 1\n"); },
                      "id");
  }
  SUBCASE("unknown engine id (T-E15)") {
    // A permanently-unknown id (cycle-3 used "native.pv.classic", which
    // became real in cycle 4 — updated together with the registration).
    expectConfigError([&] { compileText(validToml("native.nonexistent")); }, "suite.engines");
  }
  SUBCASE("non-first-class rate") {
    expectConfigError(
        [&] {
          compileText("id = \"x\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\"]\ncurves  "
                      "= [\"c-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = "
                      "[22050]\nchannels = [1]\n[[engine_config]]\nengine = "
                      "\"native.varispeed\"\nparams = { }\nseed = 1\n");
        },
        "sampleRates");
  }
  SUBCASE("channels > 2 rejected (OD-11)") {
    expectConfigError(
        [&] {
          compileText("id = \"x\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\"]\ncurves  "
                      "= [\"c-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = "
                      "[48000]\nchannels = [3]\n[[engine_config]]\nengine = "
                      "\"native.varispeed\"\nparams = { }\nseed = 1\n");
        },
        "channels");
  }
  SUBCASE("missing engine_config for a suite engine") {
    expectConfigError([&] {
      (void)compileText("id = \"x\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\"]\ncurves  = "
                  "[\"c-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = "
                  "[48000]\nchannels = [1]\n");
    }, "engine_config");
  }
  SUBCASE("duplicate engine_config") {
    expectConfigError(
        [&] {
          compileText("id = \"x\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\"]\ncurves  "
                      "= [\"c-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = "
                      "[48000]\nchannels = [1]\n[[engine_config]]\nengine = "
                      "\"native.varispeed\"\nparams = { }\nseed = 1\n[[engine_config]]\nengine "
                      "= \"native.varispeed\"\nparams = { }\nseed = 2\n");
        },
        "engine_config");
  }
  SUBCASE("missing seed (T-E15)") {
    expectConfigError(
        [&] {
          compileText("id = \"x\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\"]\ncurves  "
                      "= [\"c-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = "
                      "[48000]\nchannels = [1]\n[[engine_config]]\nengine = "
                      "\"native.varispeed\"\nparams = { }\n");
        },
        "seed");
  }
  SUBCASE("negative seed") {
    expectConfigError(
        [&] {
          compileText("id = \"x\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\"]\ncurves  "
                      "= [\"c-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = "
                      "[48000]\nchannels = [1]\n[[engine_config]]\nengine = "
                      "\"native.varispeed\"\nparams = { }\nseed = -1\n");
        },
        "seed");
  }
  SUBCASE("unknown parameter key (T-E15, §4.5)") {
    expectConfigError(
        [&] {
          compileText("id = \"x\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\"]\ncurves  "
                      "= [\"c-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = "
                      "[48000]\nchannels = [1]\n[[engine_config]]\nengine = "
                      "\"native.varispeed\"\nparams = { quallity = \"reference\" }\nseed = "
                      "1\n");
        },
        "quallity");
  }
  SUBCASE("missing asset") {
    expectConfigError(
        [&] {
          compileText("id = \"x\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"no-such-asset\"]\n"
                      "curves  = [\"c-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates "
                      "= [48000]\nchannels = [1]\n[[engine_config]]\nengine = "
                      "\"native.varispeed\"\nparams = { }\nseed = 1\n");
        },
        "inputs");
  }
  SUBCASE("corpus integrity mismatch (metadata vs WAV)") {
    // Corrupt the metadata's declared sample rate.
    TestRoot::writeFile(tr.root / "assets" / "corpus" / "a-48k" / "metadata.toml",
                        "id = \"a-48k\"\ncategory = \"sine\"\nsampleRate = 44100\nchannels = "
                        "1\ndurationSec = 1\nsourceDescription = \"x\"\nreferenceStatus = "
                        "\"test-fixture\"\n");
    expectConfigError([&] { compileText(validToml()); }, "inputs");
  }
}

TEST_CASE_FIXTURE(Fixture, "skip matrix (visible, never silent)") {
  SUBCASE("rate-mismatch") {
    const CompileOutcome out = compileText(
        "id = \"x\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\", \"a-44k1\"]\ncurves  = "
        "[\"c-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = [48000]\nchannels = "
        "[1]\n[[engine_config]]\nengine = \"native.varispeed\"\nparams = { }\nseed = 1\n");
    CHECK(out.jobs.size() == 1);  // the 48k asset pairs; the 44.1k asset skips
    REQUIRE(out.skips.size() == 1);
    CHECK(out.skips[0].reason == "rate-mismatch");
    CHECK(out.skips[0].assetId == "a-44k1");
  }
  SUBCASE("channel-mismatch") {
    const CompileOutcome out = compileText(
        "id = \"x\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-st48k\"]\ncurves  = "
        "[\"c-identity\"]\nengines = [\"native.varispeed\"]\nsampleRates = [48000]\nchannels = "
        "[1, 2]\n[[engine_config]]\nengine = \"native.varispeed\"\nparams = { }\nseed = 1\n");
    CHECK(out.jobs.size() == 1);  // (st, ch=2) pairs; (st, ch=1) skips
    REQUIRE(out.skips.size() == 1);
    CHECK(out.skips[0].reason == "channel-mismatch");
  }
  SUBCASE("ratio-out-of-range (benchmark: never rendered, T-E16)") {
    // varispeed declares [0.0625, 16]: 0.25 and 2.0 are in range. Use a
    // narrow-range test dummy to exercise the benchmark skip.
    EngineRegistry testRegistry;
    testRegistry.registerEngine(overRangeDummyDescriptor(0.5, 2.0));
    testRegistry.seal();
    const fs::path p = tr.makeExperiment(
        "x2", "id = \"x2\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\"]\ncurves  = "
                  "[\"c-low\", \"c-identity\"]\nengines = [\"test.narrow\"]\nsampleRates = "
                  "[48000]\nchannels = [1]\n[[engine_config]]\nengine = \"test.narrow\"\nparams "
                  "= { }\nseed = 1\n");
    const CompileOutcome out =
        compileExperiment(p, testRegistry, tr.root, tr.artifacts, cfg());
    CHECK(out.jobs.size() == 1);  // c-identity (1.0) in [0.5, 2]
    REQUIRE(out.skips.size() == 1);
    CHECK(out.skips[0].reason == "ratio-out-of-range");
    CHECK(out.skips[0].curveId == "c-low");
  }
}



TEST_CASE_FIXTURE(Fixture, "creative saturation (T-E17, §4.4.6)") {
  EngineRegistry testRegistry;
  testRegistry.registerEngine(overRangeDummyDescriptor(0.5, 2.0));
  testRegistry.seal();
  const fs::path p = tr.makeExperiment(
      "cr", "id = \"cr\"\nkind = \"creative\"\n[suite]\ninputs  = [\"a-48k\"]\ncurves  = "
                "[\"c-low\", \"c-identity\", \"c-vhigh\"]\nengines = [\"test.narrow\"]\n"
                "sampleRates = [48000]\nchannels = [1]\n[[engine_config]]\nengine = "
                "\"test.narrow\"\nparams = { }\nseed = 4\n");
  const CompileOutcome out = compileExperiment(p, testRegistry, tr.root, tr.artifacts, cfg());
  REQUIRE(out.jobs.size() == 3);  // creative: ALL render (saturated, not skipped)
  CHECK(out.skips.empty());

  bool sawSaturated = false, sawInRange = false;
  for (const RenderJob& j : out.jobs) {
    if (j.requestedSpec.id == "c-low") {
      // Requested signal is NEVER mutated (§4.4.6).
      double mn = 1e300, mx = -1e300;
      for (double v : j.requestedSignal->ratios) {
        mn = std::min(mn, v);
        mx = std::max(mx, v);
      }
      CHECK(mn == 0.25);
      CHECK(mx == 0.25);
      // Effective signal saturated to the engine's [minRatio, maxRatio].
      for (double v : j.effectiveSignal->ratios) {
        CHECK(v == 0.5);
      }
      CHECK(j.saturated);
      sawSaturated = true;
    } else if (j.requestedSpec.id == "c-identity") {
      CHECK(!j.saturated);
      sawInRange = true;
    } else if (j.requestedSpec.id == "c-vhigh") {
      for (double v : j.effectiveSignal->ratios) {
        CHECK(v == 2.0);
      }
      CHECK(j.saturated);
    }
  }
  CHECK(sawSaturated);
  CHECK(sawInRange);
}

TEST_CASE_FIXTURE(Fixture, "engine capability skips: rate and channels") {
  EngineRegistry testRegistry;
  EngineDescriptor d = overRangeDummyDescriptor(0.0625, 16.0);
  d.info.id = "test.mono48";  // mono + 48k-only engine
  d.capabilities.channelMode = ChannelMode::Mono;
  d.factory = []() -> std::unique_ptr<PitchEngine> { return std::make_unique<NarrowDummy>(); };
  testRegistry.registerEngine(d);
  testRegistry.seal();

  const fs::path p = tr.makeExperiment(
      "cap", "id = \"cap\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"a-48k\", \"a-44k1\", "
                   "\"a-st48k\"]\ncurves  = [\"c-identity\"]\nengines = [\"test.mono48\"]\n"
                   "sampleRates = [48000, 44100]\nchannels = [1, 2]\n[[engine_config]]\n"
                   "engine = \"test.mono48\"\nparams = { }\nseed = 9\n");
  const CompileOutcome out = compileExperiment(p, testRegistry, tr.root, tr.artifacts, cfg());
  // Surviving jobs: (a-48k mono @48k, ch1) and (a-44k1 mono @44.1k... wait —
  // rate pairing: a-44k1 @ 48000 skips (rate-mismatch); a-44k1 @ 44100: the
  // engine does not support 44100 -> sample-rate-unsupported. a-st48k: both
  // channel pairings mismatch (asset is 2ch) or unsupported (mono engine).
  CHECK(out.jobs.size() == 1);  // a-48k @ 48k, ch 1
  REQUIRE(out.skips.size() >= 3);
  std::set<std::string> reasons;
  for (const SkipEntry& s : out.skips) reasons.insert(s.reason);
  CHECK(reasons.count("rate-mismatch") == 1);
  CHECK(reasons.count("sample-rate-unsupported") == 1);
  CHECK(reasons.count("channel-mismatch") >= 1);
}
