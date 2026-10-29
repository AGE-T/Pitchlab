// ---------------------------------------------------------------------------
// OCTAVOX native core -- VST3 wrapper: class identity (R16-P4b)
// ============================================================================
// PROVENANCE: our own independent harmonizer implementation around the
// octavox native DSP core. NOT a reproduction of the original Eventide
// Octavox plug-in; no target code; the original binary was never executed.
//
// Fresh FUIDs minted for this wrapper (not copied from any SDK sample):
//   OctavoxNativeProcessorUID = {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60}
//   OctavoxNativeFactoryUID   = {7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44}
// ---------------------------------------------------------------------------
#pragma once

#include "pluginterfaces/base/funknown.h"

namespace octavox {
namespace vst3wrap {

// Fresh UIDs minted for this wrapper (not copied from any SDK sample):
// processor {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60}
// factory   {7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44}
// (DECLARE_UID form: constexpr TUID, usable by the constexpr module factory
// and directly as an FIDString by hosts/tests.)
DECLARE_UID(OctavoxNativeProcessorUID, 0x6F4E6258, 0x9A3142E1, 0x8F3C5A97, 0x1D2B4E60)
DECLARE_UID(OctavoxNativeFactoryUID, 0x7C5F93E2, 0x4B184D7A, 0x9E6C3A2B, 0x8F1D5C44)

/// Plugin identity strings (factory registration).
constexpr const char* kPluginName = "OctavoxRE Native Harmonizer";
constexpr const char* kVendorName = "Octavox RE Project";
constexpr const char* kVendorWeb = "https://example.invalid/octavox-re";
constexpr const char* kVendorEmail = "octavox-re@example.invalid";
constexpr const char* kPluginVersion = "1.0.0";
constexpr const char* kPluginSubcategory = "Fx|Pitch Shift";

}  // namespace vst3wrap
}  // namespace octavox
