#include "core/engine_registry.h"

#include <stdexcept>
#include <string>

#include "engines/granular_engine.h"
#include "engines/pv_classic_engine.h"
#include "engines/pv_phaselocked_engine.h"
#include "engines/timepitch_engine.h"
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
// V0.1 END STATE (cycle 4 closure, spec §17 step 4): ALL FIVE v0.1 engines
// are implemented, tested and registered — the registry content now EQUALS
// the §14 "v0.1 end state" table, and the registration order is aligned to
// that table's order (the interim registration order followed the
// implementation chronology while the table was still incomplete):
//
//   1. "native.varispeed"       Prototype, reference role, RateFollowing, PerSample
//   2. "native.vardelay"        Prototype, Preserving, PerSample (§6.2/§6.2.1)
//   3. "native.pv.classic"       Prototype, baseline, Preserving, hop-rate (§6.3/§6.3.1)
//   4. "native.pv.phaselocked"  Prototype, Preserving, hop-rate, L-D'99 peak shift (§6.4/§6.4.1)
//   5. "native.granular"        Prototype, creative-leaning, Preserving, per-grain hop (§6.5/§6.5.1)
//   6. "native.timepitch"       Prototype, Task28 unification (§6.6/§6.6.1), mode-scoped
//                              Preserving/RateFollowing, per-mode control rate
//
// Registry content == implemented engines, always — asserted by the
// engine_registry_smoke test (T-E19: exactly these six, constructible
// through their factories). The four Task28 algorithms are INTERNAL MODES
// of native.timepitch — never registry entries (the §6.6 sheet's scope
// lock; the proto.* research artefacts stay outside this registry).
// ---------------------------------------------------------------------------
void registerProductionEngines(EngineRegistry& registry) {
  registry.registerEngine(varispeedEngineDescriptor());
  registry.registerEngine(vardelayEngineDescriptor());
  registry.registerEngine(pvClassicEngineDescriptor());
  registry.registerEngine(pvPhaseLockedEngineDescriptor());
  registry.registerEngine(granularEngineDescriptor());
  registry.registerEngine(timePitchEngineDescriptor());
}

}  // namespace pitchlab
