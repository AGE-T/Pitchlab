#include "core/engine_registry.h"

#include <stdexcept>
#include <string>

#include "engines/vardelay_engine.h"
#include "engines/varispeed_engine.h"

namespace pitchlab {

void EngineRegistry::registerEngine(EngineDescriptor descriptor) {
  if (sealed_) {
    throw std::logic_error("engine registry is sealed; cannot register '" +
                           std::string(descriptor.info.id ? descriptor.info.id : "<null>") + "'");
  }
  if (descriptor.info.id == nullptr || descriptor.info.id[0] == '\0') {
    throw std::logic_error("engine id must not be empty");
  }
  if (descriptor.factory == nullptr) {
    // §4.2.1 item 5: registry content == implemented engines; an entry is
    // constructible by definition (the anti-fake-engine rule).
    throw std::logic_error("engine descriptor '" + std::string(descriptor.info.id) +
                           "' has no factory (an unconstructible engine must not be registered)");
  }
  if (findById(descriptor.info.id) != nullptr) {
    throw std::logic_error("duplicate engine id '" + std::string(descriptor.info.id) + "'");
  }
  engines_.push_back(std::move(descriptor));
}

void EngineRegistry::seal() { sealed_ = true; }

const EngineDescriptor* EngineRegistry::findById(std::string_view id) const {
  for (const EngineDescriptor& d : engines_) {
    if (id == d.info.id) return &d;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// THE SINGLE AUTHORITATIVE ENGINE REGISTRATION POINT (architecture §D.6).
//
// Adding an engine = implementing it + adding its descriptor HERE. Never a
// config file (config holds tunable parameter defaults only, keyed by engine
// id). Unknown engine ids referenced by experiments are CONFIG ERRORS.
//
// IMPLEMENTATION PHASE (cycle 4, spec §17 step 4): the registry contains
// EXACTLY the implemented engines, in the order of implementation
// specification §14 ("v0.1 end state"). Currently implemented:
//
//   1. "native.varispeed"   Prototype, reference role, RateFollowing, PerSample
//   2. "native.vardelay"    Prototype, Preserving, PerSample (§6.2/§6.2.1)
//
// The remaining three (native.pv.classic, native.pv.phaselocked,
// native.granular) are NOT implemented and MUST NOT appear here (registry
// content == implemented engines, always — asserted by the
// engine_registry_smoke test, T-E19).
// ---------------------------------------------------------------------------
void registerProductionEngines(EngineRegistry& registry) {
  registry.registerEngine(varispeedEngineDescriptor());
  registry.registerEngine(vardelayEngineDescriptor());
}

}  // namespace pitchlab
