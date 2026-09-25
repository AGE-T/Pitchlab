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
// FREEZE STATE: the production list is deliberately EMPTY — registry content
// always equals implemented engines, and no engine exists yet. A test
// (engine_registry_smoke.cpp) asserts this. When the v0.1 engines are
// implemented, registerProductionEngines() grows their descriptors, exactly
// in the order of implementation specification §14.

#include <string_view>
#include <vector>

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
};

/// One registry entry. The construction binding (factory) joins when engine
/// implementations exist; identity and binding must never diverge.
struct EngineDescriptor {
  EngineInfo info;
  Capabilities capabilities;
  bool isReferenceRole = false;                 // harness reference role (§H.2)
  std::vector<const char*> parameterKeys;      // engine-declared parameter names
};

class EngineRegistry {
 public:
  /// Register one engine (before seal() only). Duplicate or empty id throws
  /// std::logic_error.
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
