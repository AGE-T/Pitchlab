// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- VST3 wrapper: class identity (R16-P4b, R18-WDB, R21)
// ============================================================================
// PROVENANCE: our own independent harmonizer implementation around the
// octavox native DSP core. NOT a reproduction of the original Eventide
// Octavox plug-in; no target code; the original binary was never executed.
//
// R21 PRODUCT IDENTITY (the P0 rebrand): the user-facing product is
// "PitchLab Harmonizer" with the neutral vendor "PitchLab Project".
// The historical term "Eventide Octavox" is PRESERVED throughout the
// original-target reverse-engineering documentation (docs/, evidence/)
// where it denotes the RESEARCHED BINARY -- it is removed only from
// user-facing generated product strings (bundle names, display names,
// factory metadata, moduleinfo, validation kits, build reports,
// installer/deployment paths). The internal C++ namespace
// (octavox::vst3wrap) and the historical evidence files are deliberately
// NOT renamed (provenance preservation; see PITCHLAB_ROUND21_FINDINGS.md).
//
// R18-WDB + R21 (two-axis variant matrix): every identity in this header is
// VARIANT-CONDITIONAL and comes from the single central build configuration
// helper octavox_build_config.h:
//
//   * OctavoxNativeProcessorUID / OctavoxNativeFactoryUID -- per-variant
//     FUIDs (see the full compatibility matrix in octavox_build_config.h:
//     the Lagrange6-Decimated and ObsKernel6-Decimated "Balanced" variants
//     keep their R16-R20 lineage FUIDs; the new Lagrange6-Hybrid variant
//     carries a freshly minted deterministic identity).
//   * kPluginName -- the per-variant display name ("PitchLab Harmonizer
//     Lagrange6 Balanced" / "... Lagrange6 Hybrid" / "... ObsKernel6
//     Balanced") which makes the interpolation AND detector choice obvious
//     in any host's plug-in list.
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

// Per-variant FUIDs, declared in octavox_build_config.h (the full matrix
// with compatibility implications is documented there and in
// PITCHLAB_ROUND21_FINDINGS.md "Identity changes"):
//   LAGRANGE6+DECIMATED ("Balanced"): processor {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60}
//                                    factory   {7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44}
//                                    (R16-R20 lineage identity, PRESERVED)
//   LAGRANGE6+HYBRID:    processor {BD4A5DDF-24B1-2C39-DD2B-7F74A7A867CB}
//                                    factory   {5062BAD4-A7A0-F34E-B3A8-1E2EA53A5619}
//                                    (R21 deterministic mint)
//   OBSKERNEL6+DECIMATED ("Balanced"): processor {E3E7FA70-6A38-0D35-B05F-64B91F1E2689}
//                                    factory   {3D4CDBC6-ACAC-BB09-077F-D5B80BD2F550}
//                                    (R18-R20 identity, PRESERVED)
// (DECLARE_UID form: constexpr TUID, usable by the constexpr module factory
// and directly as an FIDString by hosts/tests.)
using build::OctavoxNativeProcessorUID;
using build::OctavoxNativeFactoryUID;

/// Plugin identity strings (factory registration) -- per-variant display
/// name making the interpolation + detector choice obvious. Vendor
/// identity is NEUTRAL and CONSISTENT across all variants; the optional
/// contact fields are deliberately EMPTY (no invented email/URL -- they
/// stay empty unless the user provides real values).
constexpr const char* kPluginName = build::kPluginDisplayName;
constexpr const char* kVendorName = "PitchLab Project";
constexpr const char* kVendorWeb = "";
constexpr const char* kVendorEmail = "";
constexpr const char* kPluginVersion = "1.2.0";  // R21: the 16.1.0 core with
// the PitchLab product identity (three-variant detector matrix); the class
// identities of the two "Balanced" variants are UNCHANGED from 1.0.0/1.1.0
// -- state blobs remain compatible ('OVR1' v1 restores with the build's
// compile-time detector/interpolation selections, which are authoritative).
constexpr const char* kPluginSubcategory = "Fx|Pitch Shift";

}  // namespace vst3wrap
}  // namespace octavox
