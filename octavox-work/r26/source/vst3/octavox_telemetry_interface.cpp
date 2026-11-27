// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- vendor telemetry interface: the class FUID (R31)
// ============================================================================
// The single definition of IPitchTelemetry::iid, compiled into
// octavox_core_vst3 so EVERY consumer (the module, the wrapper tests, the
// module-integration harness) links the same symbol. See
// octavox_telemetry_interface.h for the provenance note.
// ---------------------------------------------------------------------------
#include "octavox_telemetry_interface.h"

namespace octavox {
namespace vst3wrap {

DEF_CLASS_IID (IPitchTelemetry)

}  // namespace vst3wrap
}  // namespace octavox
