// ---------------------------------------------------------------------------
// OCTAVOX native core -- VST3 wrapper: class identity (R16-P4b, R18-WDB)
// ============================================================================
// PROVENANCE: our own independent harmonizer implementation around the
// octavox native DSP core. NOT a reproduction of the original Eventide
// Octavox plug-in; no target code; the original binary was never executed.
//
// R18-WDB (Windows dual build): every identity in this header is now
// VARIANT-CONDITIONAL and comes from the single central build
// configuration helper octavox_build_config.h:
//
//   * OctavoxNativeProcessorUID / OctavoxNativeFactoryUID -- per-variant
//     FUIDs (the two variants never share a VST3 class identity; the
//     LAGRANGE6 variant keeps the R16-R18 lineage FUIDs, the OBSKERNEL6
//     variant carries freshly minted deterministic ones).
//   * kPluginName -- the per-variant display name ("OctavoxRE Lagrange6" /
//     "OctavoxRE ObsKernel6") which makes the interpolation choice
//     obvious in any host's plug-in list.
//
// The names of the symbols are UNCHANGED (all consumers -- factory.cpp,
// the wrapper tests, the integration harness -- keep compiling against
// octavox::vst3wrap::OctavoxNativeProcessorUID / kPluginName / ... and
// automatically pick up the variant identity of the configured build).
// ---------------------------------------------------------------------------
#pragma once

#include "octavox_build_config.h"

#include "pluginterfaces/base/funknown.h"

namespace octavox {
namespace vst3wrap {

// Fresh UIDs minted for this wrapper (not copied from any SDK sample);
// per-variant values, declared in octavox_build_config.h:
//   LAGRANGE6  variant: processor {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60}
//                        factory   {7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44}
//                        (R16-R18 lineage identity)
//   LAGRANGE6Q variant: same lineage FUIDs (an auxiliary kernel variant)
//   OBSKERNEL6 variant: processor {E3E7FA70-6A38-0D35-B05F-64B91F1E2689}
//                        factory   {3D4CDBC6-ACAC-BB09-077F-D5B80BD2F550}
//                        (deterministically minted for this round)
// (DECLARE_UID form: constexpr TUID, usable by the constexpr module factory
// and directly as an FIDString by hosts/tests.)
using build::OctavoxNativeProcessorUID;
using build::OctavoxNativeFactoryUID;

/// Plugin identity strings (factory registration) -- per-variant display
/// name making the interpolation choice obvious; vendor identity is kept
/// CONSISTENT across both variants (the vendor is the same project).
constexpr const char* kPluginName = build::kPluginDisplayName;
constexpr const char* kVendorName = "Octavox RE Project";
constexpr const char* kVendorWeb = "https://example.invalid/octavox-re";
constexpr const char* kVendorEmail = "octavox-re@example.invalid";
constexpr const char* kPluginVersion = "1.1.0";  // R19: the 16.0.0 core
// (decimated detector + click-free transitions); same class identities as
// 1.0.0 -- state blobs remain compatible (v1 restores with the build's
// compile-time detector/interpolation selections).
constexpr const char* kPluginSubcategory = "Fx|Pitch Shift";

}  // namespace vst3wrap
}  // namespace octavox
