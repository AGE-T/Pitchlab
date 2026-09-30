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
// contains exactly the IMPLEMENTED engines. Current content (cycle 4
// closure, §17 step 4): ALL FIVE v0.1 engines — native.varispeed (§17 step 3
// / §14), native.vardelay (§6.2/§6.2.1), native.pv.classic (§6.3/§6.3.1),
// native.pv.phaselocked (§6.4/§6.4.1) and native.granular (§6.5/§6.5.1) —
// the registry EQUALS the §14 "v0.1 end state" table (order aligned to it).
// The engine_registry_smoke test (T-E19) asserts the exact intended content.

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

// ---------------------------------------------------------------------------
// Engine-owned parameter descriptors (Task 29).
//
// THE ENGINE DECLARES ITS OWN CONFIGURATION SURFACE HERE (registry
// metadata; the engine's configure() key strings are the SAME keys — this
// is a declaration layer, not a contract change). Every product-layer
// consumer (VST parameter registration, the realtime adapter's
// EngineConfiguration mapping, the editor's engine panel) is GENERATED
// from these descriptors — no other component holds engine-parameter
// membership knowledge.
//
// Ownership rule (binding):
//   * The plain domain [min,max] + stepCount is the PRODUCT-FACING value
//     domain (what a VST normalised parameter encodes). For discrete Int
//     parameters with choiceValues, the plain domain is the CHOICE-INDEX
//     domain and choiceValues holds the ENGINE-FACING integers.
//   * For Text parameters the plain domain is the choice-index domain and
//     choiceNames holds the ENGINE-FACING strings.
//   * `exposed == false` marks engine-internal FIXED values (valid keys of
//     the engine contract that the product layer does not expose); the
//     adapter still writes them (the descriptor's default), the VST model
//     and the UI do not see them.
//   * parameterKeys remains the engine-contract key list (harness-side
//     allowed-key validation); the descriptor set must match it (asserted
//     by the product-layer integrity check + tests).
// ---------------------------------------------------------------------------

/// The typed value space of EngineConfiguration (the v0.1 engine contract).
enum class EngineParamKind { Real, Integer, Boolean, Text };

/// The product-layer role: engine build-time configuration vs a realtime
/// musical control on the shared pitch-curve surface. (All v0.1 engine
/// parameters are Configuration; the shared PITCH/LFO/MIX/LEVEL/BYPASS
/// surface lives in the product parameter model, not per-engine.)
enum class EngineParamRole { Configuration, RealtimeCurve };

struct EngineParamDescriptor {
  // identity
  const char* key = "";           // engine-facing key (EngineConfiguration)
  const char* displayName = "";   // product display title, e.g. "Grain Length"
  EngineParamKind kind = EngineParamKind::Real;
  EngineParamRole role = EngineParamRole::Configuration;
  bool exposed = true;            // false: fixed engine-internal value

  // product plain domain
  double min = 0.0;
  double max = 1.0;
  double defaultPlain = 0.0;      // v0.1 default
  const char* unit = "";          // display unit ("s" / "x" / "frames" / "")
  int stepCount = -1;             // -1 continuous, 0 toggle, n = n+1 steps

  // discrete choices (Text: the engine string values; Int: the engine
  // numeric values — see the ownership rule above)
  const char* const* choiceNames = nullptr;
  const double* choiceValues = nullptr;
  int choiceCount = 0;

  // capabilities
  bool automatable = false;       // host-automation capability (VST kCanAutomate)
  bool rebuildsChain = true;      // change requires a chain rebuild

  // engine-domain cross-parameter constraint (engine-declared): the ENGINE
  // value of this parameter must satisfy value <= value(constrainKey) /
  // constrainDivisor (the PV engines' hop <= fft_size/2 domain, §6.3.1). The
  // engine contract REJECTS violations (ConfigError); the product layer
  // CLAMPS to the constraint (the recorded product decision — a rejected
  // configuration would leave the adapter chainless).
  const char* constrainKey = nullptr;
  int constrainDivisor = 0;

  // display
  const char* dispFmt = "%+.2f";  // printf format for real-valued display
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
  // Engine-owned parameter descriptors (Task 29 — see EngineParamDescriptor).
  // The key SET must equal parameterKeys (product-layer integrity check).
  std::vector<EngineParamDescriptor> parameters;
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
