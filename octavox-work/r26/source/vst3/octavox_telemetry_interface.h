// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- vendor telemetry interface (R31)
// ============================================================================
// PROVENANCE: our own design. No code from the original Eventide Octavox
// plug-in; the original binary was never executed.
//
// WHY A VENDOR INTERFACE (the R29 finding applied): the R29 static audit
// established that the original serves its custom functionality through a
// VENDOR interface answered by queryInterface (the "TideOctW..." vendor IID
// at [this+0x48]) -- NOT through extra Steinberg parameters. PitchLab
// follows the same architectural pattern for its m02-style telemetry: the
// 38-parameter automation surface stays byte-frozen (state blob v2), and
// the telemetry is reachable through THIS interface via queryInterface,
// exactly the way advanced hosts/debug tools query vendor extensions.
//
// SEMANTICS: see include/octavox/telemetry.hpp (the id space 7..13 and
// the dirty-bit contract come from the R28/R30 audit results).
// ---------------------------------------------------------------------------
#ifndef OCTAVOX_TELEMETRY_INTERFACE_H
#define OCTAVOX_TELEMETRY_INTERFACE_H

#include "pluginterfaces/base/funknown.h"

namespace octavox {
namespace vst3wrap {

using Steinberg::int32;
using Steinberg::tresult;
using Steinberg::uint32;
using Steinberg::uint64;

/// Vendor telemetry interface served by the processor (queryInterface).
class IPitchTelemetry : public Steinberg::FUnknown
{
public:
    /// Copy voice `voice`'s current frame (14 id cells; indices 0..6 are
    /// reserved and read 0). Any thread. Returns kResultFalse for an out
    /// of-range voice index.
    virtual tresult PLUGIN_API getVoiceFrame(int32 voice, double* frame14) = 0;

    /// The telemetry id of a cell (for self-describing hosts/tools):
    /// writes the id (7..13) for cell `cell` (0..13). Reserved cells
    /// return -1 via the out parameter.
    virtual tresult PLUGIN_API getCellId(int32 cell, int32* idOut) = 0;

    /// Read-and-clear the dirty mask (bit = voice*8 + (id-7); 56 live
    /// bits). The UI-side consume: a set bit means the cell changed at
    /// least once since the previous consume.
    virtual uint64 PLUGIN_API consumeDirtyBits() = 0;

    /// The CURRENT dirty mask without clearing.
    virtual uint64 PLUGIN_API getDirtyBits() = 0;

    static const Steinberg::FUID iid;
    DECLARE_CLASS_IID (IPitchTelemetry, 0xD1A60F31, 0x8C4B4E39, 0x9A6731D1, 0x8E4F5A21)
};

}  // namespace vst3wrap
}  // namespace octavox

#endif  // OCTAVOX_TELEMETRY_INTERFACE_H
