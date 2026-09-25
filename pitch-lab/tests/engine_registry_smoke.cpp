// T-INF1c / T-E19 — engine registry mechanism + FREEZE-STATE expectation.
//
// Verifies the architecture §D.6 rules (one authoritative in-code registry):
//   * production registry is EMPTY at the implementation freeze (registry
//     content always equals implemented engines — this assertion is the
//     anti-fake-engine guard and will be updated together with the FIRST
//     real engine registration);
//   * registration order is deterministic (registration order);
//   * duplicate ids are rejected; empty ids are rejected;
//   * registration after seal() is rejected;
//   * unknown-id lookup returns nullptr (never an implicit default engine);
//   * test-only dummies are registrable via the public API in test binaries.

#include <stdexcept>
#include <string>
#include <vector>

#include "core/engine_registry.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      ++g_failures;                                                    \
      std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    }                                                                  \
  } while (false)

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
  return d;
}

}  // namespace

int main() {
  // ---- FREEZE STATE: the production list is empty (anti-fake-engine guard) ----
  {
    pitchlab::EngineRegistry registry;
    pitchlab::registerProductionEngines(registry);
    registry.seal();
    CHECK(registry.size() == 0);  // UPDATE TOGETHER WITH THE FIRST REAL ENGINE
    CHECK(registry.findById("native.varispeed") == nullptr);
    CHECK(registry.sealed());
  }

  // ---- mechanism: registration, ordering, lookup ----
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
  std::printf("engine_registry_smoke: all checks passed (freeze state empty; mechanism sound)\n");
  return 0;
}
