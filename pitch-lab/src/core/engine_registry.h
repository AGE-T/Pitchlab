#pragma once

// Pitch Lab — engine registry (the SINGLE authoritative engine identity
// source; architecture §D.6, implementation specification §4.6/§14).
//
// Rules (binding):
//   * Engine identity lives in CODE, at ONE compile-time registration point:
//     registerProductionEngines() in engine_registry.cpp. No config file,
//     script or document is an authoritative engine list.
//   * The registry is immutable per build: fill -> seal -> read-only.
//   * Iteration order is the deterministic registration order.
//   * Manifests may COPY registry fields (read-only mirrors); they never
//     become authority.
//   * Test-only dummy engines may be registered via the public API in test
//     binaries only, flagged "test-only" in EngineInfo::origin
//     (architecture §L "Reference comparison" layer).
//
// FREEZE STATE (superseded 2026-09-26, cycles 3-4): the production list now
// contains exactly the IMPLEMENTED engines. Current content: exactly four —
// native.varispeed (spec §17 step 3 / §14), native.vardelay (spec §17 step 4
// / §6.2/§6.2.1, cycle 4), native.granular (spec §17 step 4 / §6.5/§6.5.1,
// cycle 4) and native.pv.classic (spec §17 step 4 / §6.3/§6.3.1, cycle 4).
// The remaining v0.1 engine (native.pv.phaselocked) is NOT implemented and
// MUST NOT appear here until it is; the engine_registry_smoke test (T-E19)
// asserts the exact intended content.

#include <string_view>
#include <vector>

#include "core/pitch_engine.h"

namespace pitchlab {

enum class UsageClass {
  Prototype,
  BenchmarkOnly,
  ExternalIntegration,
  ExternalBenchmark,
  ResearchReference,
  Rejected,
  Future,
};

enum class DurationBehaviour { Preserving, RateFollowing };

enum class ChannelMode { Mono, Stereo, MonoAndStereo, MultiChannel };

enum class Determinism { Deterministic, SeededDeterministic };

struct ControlRateSpec {
  enum class Kind { PerSample, FixedBlock, EngineEvent };
  Kind kind = Kind::PerSample;
  int blockFrames = 0;  // FixedBlock: engine-declared, harness-verified
};

struct BandwidthSpec {
  double nyquistFraction = 0.45;  // declared honest bandwidth (0 < f <= 1.0)
  const char* notes = "";         // honesty notes (aliasing regime, artefacts)
};

struct EngineInfo {
  const char* id = "";             // stable registry id, e.g. "native.varispeed"
  const char* displayName = "";
  const char* version = "";        // engine's own version string
  UsageClass usageClass = UsageClass::Future;
  const char* origin = "";         // "own implementation" | "test-only" | adapter target
  const char* license = "";        // recorded status (re-verify before linking)
};

struct Capabilities {
  double minRatio = 0.0;
  double maxRatio = 0.0;
  bool supportsDynamicRatio = false;
  ControlRateSpec controlRate;
  ChannelMode channelMode = ChannelMode::Mono;
  int maxChannels = 0;
  BandwidthSpec bandwidth;
  DurationBehaviour duration = DurationBehaviour::Preserving;
  Determinism determinism = Determinism::Deterministic;
  // Engine-declared supported sample rates (spec §8 "engine support declared
  // per engine"; frozen shape §4.2.1 item 5). Jobs at rates outside this set
  // are JOB SKIP sample-rate-unsupported (compiler-side check).
  std::vector<uint32_t> supportedSampleRates;
};

/// One registry entry: identity + capabilities + the construction binding
/// (factory). Identity and binding must never diverge (spec §4.6/§4.2.1
/// item 5): registerEngine() REJECTS a null factory — registry content ==
/// implemented engines means an entry is constructible by definition.
struct EngineDescriptor {
  EngineInfo info;
  Capabilities capabilities;
  bool isReferenceRole = false;                 // harness reference role (§H.2)
  std::vector<const char*> parameterKeys;      // engine-declared parameter names
  EngineFactory factory = nullptr;             // REQUIRED non-null (§4.2.1 item 5)
};

class EngineRegistry {
 public:
  /// Register one engine (before seal() only). Duplicate/empty id or a
  /// NULL FACTORY throws std::logic_error (§4.2.1 item 5: an entry is
  /// constructible by definition — the anti-fake-engine rule).
  void registerEngine(EngineDescriptor descriptor);

  /// Freeze the registry (after all registrations). Further registration
  /// throws std::logic_error.
  void seal();

  [[nodiscard]] bool sealed() const { return sealed_; }
  [[nodiscard]] std::size_t size() const { return engines_.size(); }

  /// Deterministic registration order.
  [[nodiscard]] const EngineDescriptor& at(std::size_t index) const { return engines_.at(index); }

  /// nullptr when the id is unknown (never a default engine).
  [[nodiscard]] const EngineDescriptor* findById(std::string_view id) const;

 private:
  std::vector<EngineDescriptor> engines_;
  bool sealed_ = false;
};

/// THE authoritative production engine registration point (architecture
/// §D.6). Called exactly once per process, from the harness/CLI entry, then
/// the registry is sealed. Deliberately NOT static initialisation.
///
/// Freeze state: registers NOTHING (see file header).
void registerProductionEngines(EngineRegistry& registry);

}  // namespace pitchlab
