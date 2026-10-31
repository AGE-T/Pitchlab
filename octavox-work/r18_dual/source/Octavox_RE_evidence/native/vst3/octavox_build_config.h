// ---------------------------------------------------------------------------
// OCTAVOX native core -- VST3 wrapper BUILD CONFIGURATION (R18-WDB)
// ============================================================================
// PROVENANCE: our own independent harmonizer implementation around the
// octavox native DSP core. NOT a reproduction of the original Eventide
// Octavox plug-in; no target code; the original binary was never executed.
//
// THE CENTRAL VARIANT HELPER (the one place the build's interpolation
// selection and the dual VST3 identities are defined):
//
//   The CMake cache variable OCTAVOX_VST3_INTERPOLATION
//     (LAGRANGE6 | LAGRANGE6Q | OBSKERNEL6, default LAGRANGE6)
//   is translated by native/vst3/CMakeLists.txt into EXACTLY ONE of
//     OCTAVOX_VST3_INTERP_LAGRANGE6 / _LAGRANGE6Q / _OBSKERNEL6
//   compile definitions, visible to every wrapper consumer (the module,
//   the wrapper reference tests, the compiled-module integration tests,
//   and every state restore / initialization path).
//
// Everything variant-dependent is consumed FROM THIS HEADER ONLY:
//   * build::kDefaultInterpolation -- the wrapper's compile-time default
//     interpolation (the GENERIC DSP CORE DEFAULT in octavox/dsp.hpp is
//     UNCHANGED and stays Interpolation::Lagrange6 -- the core tree is
//     frozen; only the wrapper's configuration layer selects the kernel).
//   * build::referenceConfig()     -- the engine configuration the wrapper
//     tests / integration tests use for their hand-driven core references
//     (core defaults + the variant interpolation; an OBSKERNEL6 module is
//     NEVER compared against a LAGRANGE6 reference).
//   * build::kInterpolationName / kBundleName / kPluginDisplayName --
//     the obvious-to-the-user identity strings.
//   * the per-variant processor class FUIDs + factory UIDs (see below).
//
// DUAL IDENTITIES (both variants must be loadable simultaneously in one
// DAW): the two variants NEVER share a VST3 class identity.
//   * LAGRANGE6  variant keeps the R16-R18 lineage identity:
//       processor {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60}
//       factory   {7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44}
//     (a state blob saved by the R16-R18 "OctavoxNative" module restores
//     into this class -- same class ID, renamed product).
//   * OBSKERNEL6 variant carries freshly minted, deterministically derived
//     UIDs (SHA-256 of a fixed namespace string, first 16 bytes, grouped
//     per the DECLARE_UID convention of the existing UIDs; derivation
//     recorded in WINDOWS_DUAL_BUILD_REPORT.md):
//       processor {E3E7FA70-6A38-0D35-B05F-64B91F1E2689}
//         = sha256("OctavoxRE R18-Windows-Dual-Build deterministic FUID
//                   mint v1 (2026-10-30) :: processor class UID for the
//                   ObsKernel6 variant")[0..15]
//       factory   {3D4CDBC6-ACAC-BB09-077F-D5B80BD2F550}
//         = sha256("... :: module factory UID for the ObsKernel6
//                   variant")[0..15]
//
// The 38-parameter host surface is IDENTICAL in both variants: no host
// parameter is added, removed or renumbered (the interpolation choice is
// a COMPILE-TIME property of the module, never a host-visible parameter).
// ---------------------------------------------------------------------------
#pragma once

#include "octavox/dsp.hpp"

#include "pluginterfaces/base/funknown.h"

#include <string>

namespace octavox {
namespace vst3wrap {
namespace build {

//--------------------------------------------------------------------------
// The compile-time interpolation selection
//--------------------------------------------------------------------------

#if defined(OCTAVOX_VST3_INTERP_OBSKERNEL6)

/// The wrapper's default interpolation for this build.
constexpr Interpolation kDefaultInterpolation = Interpolation::ObsKernel6;
/// Short kernel name (identity/display/reporting).
constexpr const char* kInterpolationName = "ObsKernel6";
/// VST3 bundle/package base name (OctavoxRE_<Variant>.vst3).
constexpr const char* kBundleName = "OctavoxRE_ObsKernel6";
/// Plugin display name (the factory-registered class name).
constexpr const char* kPluginDisplayName = "OctavoxRE ObsKernel6";

/// Processor class FUID of THIS variant (freshly minted, deterministic
/// derivation documented in the header comment above + the build report).
DECLARE_UID(OctavoxNativeProcessorUID, 0xE3E7FA70, 0x6A380D35, 0xB05F64B9, 0x1F1E2689)
/// Module factory UID of THIS variant.
DECLARE_UID(OctavoxNativeFactoryUID, 0x3D4CDBC6, 0xACACBB09, 0x077FD5B8, 0x0BD2F550)

#elif defined(OCTAVOX_VST3_INTERP_LAGRANGE6Q)

/// The wrapper's default interpolation for this build.
constexpr Interpolation kDefaultInterpolation = Interpolation::Lagrange6Q;
/// Short kernel name (identity/display/reporting).
constexpr const char* kInterpolationName = "Lagrange6Q";
/// VST3 bundle/package base name.
constexpr const char* kBundleName = "OctavoxRE_Lagrange6Q";
/// Plugin display name (the factory-registered class name).
constexpr const char* kPluginDisplayName = "OctavoxRE Lagrange6Q";

/// Processor class FUID of THIS variant (R16-R18 lineage identity).
DECLARE_UID(OctavoxNativeProcessorUID, 0x6F4E6258, 0x9A3142E1, 0x8F3C5A97, 0x1D2B4E60)
/// Module factory UID of THIS variant.
DECLARE_UID(OctavoxNativeFactoryUID, 0x7C5F93E2, 0x4B184D7A, 0x9E6C3A2B, 0x8F1D5C44)

#else  // default: LAGRANGE6

/// The wrapper's default interpolation for this build.
constexpr Interpolation kDefaultInterpolation = Interpolation::Lagrange6;
/// Short kernel name (identity/display/reporting).
constexpr const char* kInterpolationName = "Lagrange6";
/// VST3 bundle/package base name (OctavoxRE_<Variant>.vst3).
constexpr const char* kBundleName = "OctavoxRE_Lagrange6";
/// Plugin display name (the factory-registered class name).
constexpr const char* kPluginDisplayName = "OctavoxRE Lagrange6";

/// Processor class FUID of THIS variant (R16-R18 lineage identity:
/// the R16/R17/R18 "OctavoxNative" class, renamed with state-blob
/// compatibility preserved).
DECLARE_UID(OctavoxNativeProcessorUID, 0x6F4E6258, 0x9A3142E1, 0x8F3C5A97, 0x1D2B4E60)
/// Module factory UID of THIS variant.
DECLARE_UID(OctavoxNativeFactoryUID, 0x7C5F93E2, 0x4B184D7A, 0x9E6C3A2B, 0x8F1D5C44)

#endif

//--------------------------------------------------------------------------
// The compile-time detector selection (R19)
//--------------------------------------------------------------------------

// Both interpolation variants use the R19 decimated control-rate pitch
// detector (~24 kHz, anti-alias filtered) -- the real-time fix of round 19
// applies to BOTH variants; the interpolation choice remains the ONLY
// variant axis. The FullRate reference mode stays available in the CORE
// (Config::detector_mode, gated by the legacy record) but is NOT a
// host-visible parameter and is never selected by the wrapper.
constexpr DetectorMode kDefaultDetectorMode = DetectorMode::Decimated;

//--------------------------------------------------------------------------
// The single engine-configuration helper every consumer uses
//--------------------------------------------------------------------------

/// The engine configuration for THIS build variant: the pure core defaults
/// (Config{} -- the 16.0.0 core defaults, whose detector default IS the
/// decimated mode) with the wrapper's compile-time interpolation selection
/// substituted and the compile-time detector selection pinned EXPLICITLY
/// (both are compile-time properties of the module, never host-visible
/// parameters). Used by:
///   * the processor constructor / initialization paths,
///   * state restore (the variant selection is authoritative),
///   * the wrapper reference tests' hand-driven core references,
///   * the compiled-module integration tests' core references
///     (an OBSKERNEL6 module is never compared against a LAGRANGE6
///     reference).
inline Config referenceConfig() {
    Config c{};  // core defaults (16.0.0: decimated detector default)
    c.interpolation = kDefaultInterpolation;
    c.detector_mode = kDefaultDetectorMode;
    return c;
}

/// The .vst3 bundle file name of THIS variant (OctavoxRE_<Variant>.vst3).
inline std::string bundleFileName() { return std::string(kBundleName) + ".vst3"; }

}  // namespace build
}  // namespace vst3wrap
}  // namespace octavox
