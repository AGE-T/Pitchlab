// T-INF1c / T-E19 — engine registry mechanism + intended-content assertion.
//
// Verifies the architecture §D.6 rules (one authoritative in-code registry):
//   * production registry content always equals IMPLEMENTED engines (the
//     anti-fake-engine guard). V0.1 END STATE (cycle 4 closure, §17 step 4):
//     exactly FIVE engines — native.varispeed (spec §17 step 3 / §14),
//     native.vardelay (§6.2/§6.2.1), native.pv.classic (§6.3/§6.3.1),
//     native.pv.phaselocked (§6.4/§6.4.1) and native.granular (§6.5/§6.5.1) —
//     the registry EQUALS the §14 "v0.1 end state" table (order aligned to
//     it; the interim order followed the implementation chronology);
//   * registration order is deterministic (registration order);
//   * duplicate ids are rejected; empty ids are rejected; NULL FACTORIES are
//     rejected (§4.2.1 item 5 — an unconstructible engine must not appear);
//   * registration after seal() is rejected;
//   * unknown-id lookup returns nullptr (never an implicit default engine);
//   * test-only dummies are registrable via the public API in test binaries
//     and constructible through their factories.

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      ++g_failures;                                                    \
      std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    }                                                                  \
  } while (false)

// Minimal test-only dummy engine (architecture §L: dummies live in test
// binaries only, flagged "test-only" in origin). Just enough to satisfy the
// contract surface and be constructible through a factory.
class DummyEngine : public pitchlab::PitchEngine {
 public:
  const char* engineId() const override { return "test.dummy"; }
  void configure(const pitchlab::EngineConfiguration&) override {}
  void prepare(const pitchlab::ProcessContext&) override {}
  Latency latency() const override { return Latency{0, 0}; }
  pitchlab::ProcessReport process(const pitchlab::AudioBlockView&, int inFrames,
                                  pitchlab::AudioBlockOut&, int, const pitchlab::PitchCurveView&,
                                  pitchlab::FrameCount) override {
    return pitchlab::ProcessReport{inFrames, 0, false};
  }
  pitchlab::ProcessReport finish(pitchlab::AudioBlockOut&, int) override {
    return pitchlab::ProcessReport{0, 0, false};
  }
  void reset() override {}
};

std::unique_ptr<pitchlab::PitchEngine> makeDummyEngine() {
  return std::make_unique<DummyEngine>();
}

pitchlab::EngineDescriptor makeDummy(const char* id, int order) {
  pitchlab::EngineDescriptor d;
  d.info.id = id;
  d.info.displayName = id;
  d.info.version = "0.0-test";
  d.info.usageClass = pitchlab::UsageClass::Prototype;
  d.info.origin = "test-only";  // architecture §L: dummies are test-only
  d.info.license = "n/a";
  d.capabilities.minRatio = 0.5;
  d.capabilities.maxRatio = 2.0;
  d.capabilities.supportsDynamicRatio = true;
  d.capabilities.controlRate.kind = pitchlab::ControlRateSpec::Kind::FixedBlock;
  d.capabilities.controlRate.blockFrames = order * 100;
  d.capabilities.channelMode = pitchlab::ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.45;
  d.capabilities.bandwidth.notes = "dummy";
  d.capabilities.duration = pitchlab::DurationBehaviour::Preserving;
  d.capabilities.determinism = pitchlab::Determinism::Deterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u};
  d.factory = &makeDummyEngine;
  return d;
}

}  // namespace

int main() {
  // ---- V0.1 END STATE + TASK 33 (§6.6/§6.6.1): exactly SIX implemented
  // engines — the five v0.1 engines plus the unified time-pitch engine
  // native.timepitch (6th production engine, selector index 5; the four
  // Task28 algorithms are its internal MODES, never registry entries) ----
  {
    pitchlab::EngineRegistry registry;
    pitchlab::registerProductionEngines(registry);
    registry.seal();
    CHECK(registry.size() == 6);  // the five v0.1 engines + native.timepitch
    CHECK(std::string(registry.at(0).info.id) == "native.varispeed");       // §14 order
    CHECK(std::string(registry.at(1).info.id) == "native.vardelay");
    CHECK(std::string(registry.at(2).info.id) == "native.pv.classic");
    CHECK(std::string(registry.at(3).info.id) == "native.pv.phaselocked");
    CHECK(std::string(registry.at(4).info.id) == "native.granular");
    CHECK(std::string(registry.at(5).info.id) == "native.timepitch");       // task 33, index 5
    const pitchlab::EngineDescriptor* d = registry.findById("native.varispeed");
    CHECK(d != nullptr);
    CHECK(d->factory != nullptr);
    CHECK(d->isReferenceRole);  // harness reference role (§H.2)
    CHECK(d->capabilities.duration == pitchlab::DurationBehaviour::RateFollowing);
    CHECK(d->capabilities.minRatio == 0.0625);
    CHECK(d->capabilities.maxRatio == 16.0);
    // native.vardelay (§6.2/§6.2.1): Preserving, PerSample, [0.5, 2.0],
    // Deterministic, MonoAndStereo, three parameter keys.
    const pitchlab::EngineDescriptor* vd = registry.findById("native.vardelay");
    CHECK(vd != nullptr);
    CHECK(vd->factory != nullptr);
    CHECK(!vd->isReferenceRole);
    CHECK(vd->capabilities.duration == pitchlab::DurationBehaviour::Preserving);
    CHECK(vd->capabilities.minRatio == 0.5);
    CHECK(vd->capabilities.maxRatio == 2.0);
    CHECK(vd->capabilities.controlRate.kind == pitchlab::ControlRateSpec::Kind::PerSample);
    CHECK(vd->capabilities.determinism == pitchlab::Determinism::Deterministic);
    CHECK(vd->capabilities.channelMode == pitchlab::ChannelMode::MonoAndStereo);
    CHECK(vd->parameterKeys.size() == 3);
    // native.granular (§6.5/§6.5.1): Preserving, per-grain hop (FixedBlock,
    // blockFrames = 0 — the honest static declaration), [0.125, 8.0],
    // SeededDeterministic, MonoAndStereo, four parameter keys.
    const pitchlab::EngineDescriptor* gr = registry.findById("native.granular");
    CHECK(gr != nullptr);
    CHECK(gr->factory != nullptr);
    CHECK(!gr->isReferenceRole);
    CHECK(gr->capabilities.duration == pitchlab::DurationBehaviour::Preserving);
    CHECK(gr->capabilities.minRatio == 0.125);
    CHECK(gr->capabilities.maxRatio == 8.0);
    CHECK(gr->capabilities.controlRate.kind == pitchlab::ControlRateSpec::Kind::FixedBlock);
    CHECK(gr->capabilities.controlRate.blockFrames == 0);
    CHECK(gr->capabilities.determinism == pitchlab::Determinism::SeededDeterministic);
    CHECK(gr->capabilities.channelMode == pitchlab::ChannelMode::MonoAndStereo);
    CHECK(gr->parameterKeys.size() == 4);
    // native.pv.classic (§6.3/§6.3.1): Preserving, FixedBlock(512) — the
    // default hop, [0.25, 4.0], Deterministic, MonoAndStereo, three keys.
    const pitchlab::EngineDescriptor* pv = registry.findById("native.pv.classic");
    CHECK(pv != nullptr);
    CHECK(pv->factory != nullptr);
    CHECK(!pv->isReferenceRole);
    CHECK(pv->capabilities.duration == pitchlab::DurationBehaviour::Preserving);
    CHECK(pv->capabilities.minRatio == 0.25);
    CHECK(pv->capabilities.maxRatio == 4.0);
    CHECK(pv->capabilities.controlRate.kind == pitchlab::ControlRateSpec::Kind::FixedBlock);
    CHECK(pv->capabilities.controlRate.blockFrames == 512);
    CHECK(pv->capabilities.determinism == pitchlab::Determinism::Deterministic);
    CHECK(pv->capabilities.channelMode == pitchlab::ChannelMode::MonoAndStereo);
    CHECK(pv->parameterKeys.size() == 3);
    // native.pv.phaselocked (§6.4/§6.4.1): Preserving, FixedBlock(512) —
    // the default hop, [0.25, 4.0], Deterministic, MonoAndStereo, FOUR
    // parameter keys (window, fft_size, hop, locking_mode — the §14 table;
    // "identity" is the only accepted locking mode in v0.1).
    const pitchlab::EngineDescriptor* pl = registry.findById("native.pv.phaselocked");
    CHECK(pl != nullptr);
    CHECK(pl->factory != nullptr);
    CHECK(!pl->isReferenceRole);
    CHECK(pl->capabilities.duration == pitchlab::DurationBehaviour::Preserving);
    CHECK(pl->capabilities.minRatio == 0.25);
    CHECK(pl->capabilities.maxRatio == 4.0);
    CHECK(pl->capabilities.controlRate.kind == pitchlab::ControlRateSpec::Kind::FixedBlock);
    CHECK(pl->capabilities.controlRate.blockFrames == 512);
    CHECK(pl->capabilities.determinism == pitchlab::Determinism::Deterministic);
    CHECK(pl->capabilities.channelMode == pitchlab::ChannelMode::MonoAndStereo);
    CHECK(pl->parameterKeys.size() == 4);
    // The registered engines are constructible through their factories.
    std::unique_ptr<pitchlab::PitchEngine> engine = d->factory();
    CHECK(engine != nullptr);
    CHECK(std::string(engine->engineId()) == "native.varispeed");
    std::unique_ptr<pitchlab::PitchEngine> vengine = vd->factory();
    CHECK(vengine != nullptr);
    CHECK(std::string(vengine->engineId()) == "native.vardelay");
    std::unique_ptr<pitchlab::PitchEngine> cengine = pv->factory();
    CHECK(cengine != nullptr);
    CHECK(std::string(cengine->engineId()) == "native.pv.classic");
    std::unique_ptr<pitchlab::PitchEngine> plengine = pl->factory();
    CHECK(plengine != nullptr);
    CHECK(std::string(plengine->engineId()) == "native.pv.phaselocked");
    std::unique_ptr<pitchlab::PitchEngine> gengine = gr->factory();
    CHECK(gengine != nullptr);
    CHECK(std::string(gengine->engineId()) == "native.granular");
    // native.timepitch (§6.6/§6.6.1, task 33): 6th engine, selector index 5;
    // ratio [0.25, 4.0]; MonoAndStereo; FixedBlock (per-mode hop — the
    // honest blockFrames = 0 declaration); Deterministic; the declared rate
    // set excludes 176.4 kHz (§6.6); mode-scoped parameter keys (the Fixed
    // checkpoint registers mode/window_frames/overlap/window_shape — the
    // §6.6.1 item 1 growth per checkpoint; the tags are stable).
    const pitchlab::EngineDescriptor* tp = registry.findById("native.timepitch");
    CHECK(tp != nullptr);
    CHECK(tp == &registry.at(5));  // registration order == selector index
    CHECK(tp->factory != nullptr);
    CHECK(!tp->isReferenceRole);
    CHECK(tp->capabilities.duration == pitchlab::DurationBehaviour::Preserving);
    CHECK(tp->capabilities.minRatio == 0.25);
    CHECK(tp->capabilities.maxRatio == 4.0);
    CHECK(tp->capabilities.controlRate.kind == pitchlab::ControlRateSpec::Kind::FixedBlock);
    CHECK(tp->capabilities.controlRate.blockFrames == 0);
    CHECK(tp->capabilities.determinism == pitchlab::Determinism::Deterministic);
    CHECK(tp->capabilities.channelMode == pitchlab::ChannelMode::MonoAndStereo);
    CHECK(tp->capabilities.maxChannels == 2);
    CHECK(tp->capabilities.supportedSampleRates.size() == 5);
    CHECK(tp->parameterKeys.size() == 5);  // the Fixed+Adaptive key set
    std::unique_ptr<pitchlab::PitchEngine> tengine = tp->factory();
    CHECK(tengine != nullptr);
    CHECK(std::string(tengine->engineId()) == "native.timepitch");
    CHECK(registry.sealed());
  }

  // ---- mechanism: registration, ordering, lookup, factory construction ----
  {
    pitchlab::EngineRegistry registry;
    registry.registerEngine(makeDummy("test.b", 1));
    registry.registerEngine(makeDummy("test.a", 2));
    registry.registerEngine(makeDummy("test.c", 3));
    registry.seal();
    CHECK(registry.size() == 3);
    CHECK(std::string(registry.at(0).info.id) == "test.b");  // registration order
    CHECK(std::string(registry.at(1).info.id) == "test.a");
    CHECK(std::string(registry.at(2).info.id) == "test.c");
    CHECK(registry.findById("test.a") != nullptr);
    CHECK(registry.findById("test.a")->capabilities.maxRatio == 2.0);
    CHECK(registry.findById("native.pv.classic") == nullptr);  // unknown -> nullptr

    // Factory construction: every registered engine is instantiable through
    // its descriptor factory (§4.2.1 item 5).
    for (std::size_t i = 0; i < registry.size(); ++i) {
      const pitchlab::EngineDescriptor& d = registry.at(i);
      CHECK(d.factory != nullptr);
      std::unique_ptr<pitchlab::PitchEngine> engine = d.factory();
      CHECK(engine != nullptr);
      CHECK(std::string(engine->engineId()) == "test.dummy");
    }
  }

  // ---- failure semantics ----
  {
    pitchlab::EngineRegistry registry;
    bool threw = false;
    try {
      registry.registerEngine(makeDummy("", 1));  // empty id
    } catch (const std::logic_error&) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    {
      pitchlab::EngineDescriptor noFactory = makeDummy("test.nofactory", 1);
      noFactory.factory = nullptr;
      try {
        registry.registerEngine(noFactory);  // §4.2.1 item 5: no factory
      } catch (const std::logic_error&) {
        threw = true;
      }
      CHECK(threw);
    }
    threw = false;
    registry.registerEngine(makeDummy("test.dup", 1));
    try {
      registry.registerEngine(makeDummy("test.dup", 2));  // duplicate id
    } catch (const std::logic_error&) {
      threw = true;
    }
    CHECK(threw);
    registry.seal();
    threw = false;
    try {
      registry.registerEngine(makeDummy("test.late", 3));  // after seal
    } catch (const std::logic_error&) {
      threw = true;
    }
    CHECK(threw);
    CHECK(registry.size() == 1);
  }

  if (g_failures != 0) {
    std::fprintf(stderr, "engine_registry_smoke: %d check(s) FAILED\n", g_failures);
    return 1;
  }
  std::printf("engine_registry_smoke: all checks passed (6 implemented engines — the v0.1 END STATE (§14) + the task-33 6th engine native.timepitch (§6.6): native.varispeed, native.vardelay, native.pv.classic, native.pv.phaselocked, native.granular, native.timepitch; mechanism + factory binding sound)\n");
  return 0;
}
