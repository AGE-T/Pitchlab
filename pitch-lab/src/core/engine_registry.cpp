#include "core/engine_registry.h"

#include <stdexcept>
#include <string>

namespace pitchlab {

void EngineRegistry::registerEngine(EngineDescriptor descriptor) {
  if (sealed_) {
    throw std::logic_error("engine registry is sealed; cannot register '" +
                           std::string(descriptor.info.id ? descriptor.info.id : "<null>") + "'");
  }
  if (descriptor.info.id == nullptr || descriptor.info.id[0] == '\0') {
    throw std::logic_error("engine id must not be empty");
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
// FREEZE STATE: this list is deliberately EMPTY. Registry content always
// equals implemented engines, and none is implemented. During v0.1
// implementation the five descriptors below are added IN THIS ORDER
// (implementation specification §14, "v0.1 end state"):
//
//   1. "native.varispeed"      Prototype, reference role, RateFollowing, PerSample
//   2. "native.vardelay"       Prototype, Preserving, PerSample
//   3. "native.pv.classic"     Prototype, baseline role, Preserving, FixedBlock(hop)
//   4. "native.pv.phaselocked" Prototype, Preserving, FixedBlock(hop)
//   5. "native.granular"       Prototype, creative-leaning, Preserving, FixedBlock(grain hop)
//
// The engine_registry_smoke test asserts the freeze-state emptiness and will
// be updated together with the first real registration.
// ---------------------------------------------------------------------------
void registerProductionEngines(EngineRegistry& registry) {
  (void)registry;  // no production engines exist at the implementation freeze
}

}  // namespace pitchlab
