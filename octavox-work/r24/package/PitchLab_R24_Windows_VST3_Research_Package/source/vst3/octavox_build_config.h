// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER (formerly the "OctavoxRE" wrapper) -- VST3 wrapper
// BUILD CONFIGURATION (R18-WDB, R21 two-axis variant matrix)
// ============================================================================
// PROVENANCE: our own independent harmonizer implementation around the
// octavox native DSP core. NOT a reproduction of the original Eventide
// Octavox plug-in; no target code; the original binary was never executed.
//
// THE CENTRAL VARIANT HELPER (the one place the build's interpolation AND
// detector selections and the per-variant VST3 identities are defined):
//
//   The CMake cache variables
//     OCTAVOX_VST3_INTERPOLATION  (LAGRANGE6 | LAGRANGE6Q | OBSKERNEL6,
//                                  default LAGRANGE6)
//     OCTAVOX_VST3_DETECTOR       (DECIMATED | HYBRID | FULLRATE,
//                                  default DECIMATED)          [R21]
//   are translated by native/vst3/CMakeLists.txt into EXACTLY ONE of
//     OCTAVOX_VST3_INTERP_LAGRANGE6 / _LAGRANGE6Q / _OBSKERNEL6
//   and EXACTLY ONE of
//     OCTAVOX_VST3_DET_DECIMATED / _DET_HYBRID / _DET_FULLRATE
//   compile definitions, visible to every wrapper consumer (the module,
//   the wrapper reference tests, the compiled-module integration tests,
//   and every state restore / initialization path).
//
// Everything variant-dependent is consumed FROM THIS HEADER ONLY:
//   * build::kDefaultInterpolation / kDefaultDetectorMode -- the wrapper's
//     compile-time default interpolation kernel and pitch detector (the
//     GENERIC DSP CORE DEFAULTS in octavox/dsp.hpp are UNCHANGED and stay
//     Interpolation::Lagrange6 / DetectorMode::Decimated -- the core tree
//     is frozen; only the wrapper's configuration layer selects).
//   * build::referenceConfig()     -- the engine configuration the wrapper
//     tests / integration tests use for their hand-driven core references
//     (core defaults + the variant selections; an OBSKERNEL6 module is
//     NEVER compared against a LAGRANGE6 reference, a HYBRID module never
//     against a DECIMATED one).
//   * build::kInterpolationName / kDetectorModeName / kBundleName /
//     kPluginDisplayName -- the obvious-to-the-user identity strings.
//   * the per-variant processor class FUIDs + factory UIDs (see below).
//
// R21 PRODUCT IDENTITY (the P0 rebrand): the user-facing product is
// "PitchLab Harmonizer" with neutral vendor metadata "PitchLab Project"
// (see octavox_ids.h). The three RECOMMENDED user-facing builds are:
//   * Lagrange6  + Decimated = "PitchLab Harmonizer Lagrange6 Balanced"
//   * Lagrange6  + Hybrid    = "PitchLab Harmonizer Lagrange6 Hybrid"
//   * ObsKernel6 + Decimated = "PitchLab Harmonizer ObsKernel6 Balanced"
// FullRate builds remain the HIGH-COST reference mode (with the R24 AVX2
// kernels ~37% of one core at 96 kHz stereo 8-voice vs ~114% scalar --
// usable, but substantially more expensive than Balanced; not the
// recommended realtime default).
// [R24-Windows delivery: exactly TWO variants ship -- Lagrange6+Decimated
// ("Balanced", the realtime production candidate) and Lagrange6+FullRate
// ("FullRate", the high-cost detector reference for research/comparison).
// The Hybrid/Lagrange6Q/ObsKernel6 sources and identities are PRESERVED
// in the tree; they are simply not part of this delivery's builds.]
//
// FUID COMPATIBILITY MATRIX (R21, all compatibility implications recorded
// in PITCHLAB_ROUND21_FINDINGS.md "Identity changes"):
//   * LAGRANGE6+DECIMATED keeps the R16-R18 lineage identity:
//       processor {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60}
//       factory   {7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44}
//     (a state blob saved by the R16-R20 "OctavoxNative"/"OctavoxRE
//     Lagrange6" module restores into this class -- same class ID,
//     renamed product; sessions referencing it by FUID keep working).
//   * LAGRANGE6Q+DECIMATED keeps the SAME lineage identity (the auxiliary
//     kernel variant of R18-R20 shared it; preserved verbatim).
//   * OBSKERNEL6+DECIMATED keeps its R18-R20 identity:
//       processor {E3E7FA70-6A38-0D35-B05F-64B91F1E2689}
//       factory   {3D4CDBC6-ACAC-BB09-077F-D5B80BD2F550}
//   * LAGRANGE6+HYBRID (the new recommended Hybrid variant) carries
//     freshly minted, deterministically derived UIDs (SHA-256 of a fixed
//     namespace string, first 16 bytes, grouped per the DECLARE_UID
//     convention; derivation recorded in PITCHLAB_ROUND21_FINDINGS.md):
//       processor {BD4A5DDF-24B1-2C39-DD2B-7F74A7A867CB}
//         = sha256("PitchLab R21 deterministic FUID mint v1 (2026-11-02)
//                   :: processor class UID for the PitchLab Harmonizer
//                   Lagrange6 Hybrid variant")[0..15]
//       factory   {5062BAD4-A7A0-F34E-B3A8-1E2EA53A5619}
//         = sha256("... :: module factory UID for the PitchLab Harmonizer
//                   Lagrange6 Hybrid variant")[0..15]
//   * every OTHER (interp x detector) combination (the FullRate benchmark
//     builds, the LAGRANGE6Q auxiliaries, OBSKERNEL6+HYBRID/FullRate)
//     carries its own deterministically minted FUIDs from the same
//     namespace ("... for the PitchLab Harmonizer <Interp> <Detector>
//     variant") -- listed below -- so no two buildable variants ever
//     share a class identity.
//
// The 38-parameter host surface is IDENTICAL in every variant: no host
// parameter is added, removed or renumbered (the interpolation and
// detector choices are COMPILE-TIME properties of the module, never
// host-visible parameters).
// ---------------------------------------------------------------------------
#pragma once

#include "octavox/dsp.hpp"

#include "pluginterfaces/base/funknown.h"

#include <string>

namespace octavox {
namespace vst3wrap {
namespace build {

//--------------------------------------------------------------------------
// The compile-time interpolation selection (axis 1)
//--------------------------------------------------------------------------

#if defined(OCTAVOX_VST3_INTERP_OBSKERNEL6)
constexpr Interpolation kInterpSel = Interpolation::ObsKernel6;
constexpr const char* kInterpolationName = "ObsKernel6";
#elif defined(OCTAVOX_VST3_INTERP_LAGRANGE6Q)
constexpr Interpolation kInterpSel = Interpolation::Lagrange6Q;
constexpr const char* kInterpolationName = "Lagrange6Q";
#else  // default: LAGRANGE6
constexpr Interpolation kInterpSel = Interpolation::Lagrange6;
constexpr const char* kInterpolationName = "Lagrange6";
#endif

//--------------------------------------------------------------------------
// The compile-time detector selection (axis 2, R21)
//--------------------------------------------------------------------------

#if defined(OCTAVOX_VST3_DET_FULLRATE)
constexpr DetectorMode kDetectorSel = DetectorMode::FullRate;
constexpr const char* kDetectorModeName = "FullRate";
#elif defined(OCTAVOX_VST3_DET_HYBRID)
constexpr DetectorMode kDetectorSel = DetectorMode::Hybrid;
constexpr const char* kDetectorModeName = "Hybrid";
#else  // default: DECIMATED
constexpr DetectorMode kDetectorSel = DetectorMode::Decimated;
constexpr const char* kDetectorModeName = "Decimated";
#endif

/// The wrapper's default interpolation for this build.
constexpr Interpolation kDefaultInterpolation = kInterpSel;
/// The wrapper's default pitch detector for this build (R21: selectable;
/// R19/R20 shipped Decimated only; FullRate = offline/benchmark only).
constexpr DetectorMode kDefaultDetectorMode = kDetectorSel;

//--------------------------------------------------------------------------
// The per-variant identity (display name, bundle name, FUIDs)
//--------------------------------------------------------------------------

// ---- LAGRANGE6 ----------------------------------------------------------
#if defined(OCTAVOX_VST3_INTERP_LAGRANGE6) && defined(OCTAVOX_VST3_DET_DECIMATED)

/// Plugin display name (the factory-registered class name).
constexpr const char* kPluginDisplayName = "PitchLab Harmonizer Lagrange6 Balanced";
/// VST3 bundle/package base name (PitchLab_<Variant>.vst3).
constexpr const char* kBundleName = "PitchLab_Lagrange6_Balanced";
/// Processor class FUID (R16-R20 lineage identity, PRESERVED).
DECLARE_UID(OctavoxNativeProcessorUID, 0x6F4E6258, 0x9A3142E1, 0x8F3C5A97, 0x1D2B4E60)
/// Module factory UID (R16-R20 lineage identity, PRESERVED).
DECLARE_UID(OctavoxNativeFactoryUID, 0x7C5F93E2, 0x4B184D7A, 0x9E6C3A2B, 0x8F1D5C44)

#elif defined(OCTAVOX_VST3_INTERP_LAGRANGE6) && defined(OCTAVOX_VST3_DET_HYBRID)

constexpr const char* kPluginDisplayName = "PitchLab Harmonizer Lagrange6 Hybrid";
constexpr const char* kBundleName = "PitchLab_Lagrange6_Hybrid";
/// Processor class FUID (R21 deterministic mint -- see header comment).
DECLARE_UID(OctavoxNativeProcessorUID, 0xBD4A5DDF, 0x24B12C39, 0xDD2B7F74, 0xA7A867CB)
DECLARE_UID(OctavoxNativeFactoryUID, 0x5062BAD4, 0xA7A0F34E, 0xB3A81E2E, 0xA53A5619)

#elif defined(OCTAVOX_VST3_INTERP_LAGRANGE6) && defined(OCTAVOX_VST3_DET_FULLRATE)

constexpr const char* kPluginDisplayName = "PitchLab Harmonizer Lagrange6 FullRate";
constexpr const char* kBundleName = "PitchLab_Lagrange6_FullRate";
/// R24-Windows delivery: the HIGH-COST detector reference variant (research/
/// comparison; substantially higher CPU than Balanced -- see the R24
/// benchmark record; not automatically recommended for realtime use).
/// Distinct deterministically minted identity (never shipped before R24-W).
DECLARE_UID(OctavoxNativeProcessorUID, 0xC13E1BB5, 0x5BFB0598, 0xF25DDD4A, 0x52B0E817)
DECLARE_UID(OctavoxNativeFactoryUID, 0xF47099E7, 0x0B320AE1, 0x086E8325, 0x65140998)

// ---- LAGRANGE6Q (auxiliary kernel variant) -------------------------------
#elif defined(OCTAVOX_VST3_INTERP_LAGRANGE6Q) && defined(OCTAVOX_VST3_DET_DECIMATED)

constexpr const char* kPluginDisplayName = "PitchLab Harmonizer Lagrange6Q";
constexpr const char* kBundleName = "PitchLab_Lagrange6Q";
/// Processor class FUID (R16-R20 lineage identity, shared with the
/// LAGRANGE6 Balanced class exactly as in R18-R20 -- preserved verbatim).
DECLARE_UID(OctavoxNativeProcessorUID, 0x6F4E6258, 0x9A3142E1, 0x8F3C5A97, 0x1D2B4E60)
DECLARE_UID(OctavoxNativeFactoryUID, 0x7C5F93E2, 0x4B184D7A, 0x9E6C3A2B, 0x8F1D5C44)

#elif defined(OCTAVOX_VST3_INTERP_LAGRANGE6Q) && defined(OCTAVOX_VST3_DET_HYBRID)

constexpr const char* kPluginDisplayName = "PitchLab Harmonizer Lagrange6Q Hybrid";
constexpr const char* kBundleName = "PitchLab_Lagrange6Q_Hybrid";
DECLARE_UID(OctavoxNativeProcessorUID, 0xE37DC7BC, 0xA7F66D0D, 0x726A9477, 0x963B2A5D)
DECLARE_UID(OctavoxNativeFactoryUID, 0xA012C97B, 0xCE36B6D0, 0x39F6ABD2, 0x4ADEE5DB)

#elif defined(OCTAVOX_VST3_INTERP_LAGRANGE6Q) && defined(OCTAVOX_VST3_DET_FULLRATE)

constexpr const char* kPluginDisplayName = "PitchLab Harmonizer Lagrange6Q FullRate";
constexpr const char* kBundleName = "PitchLab_Lagrange6Q_FullRate";
DECLARE_UID(OctavoxNativeProcessorUID, 0x2F2E7034, 0x591E46F3, 0x0794E1E1, 0x3DD51B96)
DECLARE_UID(OctavoxNativeFactoryUID, 0xAB32838B, 0xB320718C, 0x9C61FE7E, 0x073D3723)

// ---- OBSKERNEL6 ----------------------------------------------------------
#elif defined(OCTAVOX_VST3_INTERP_OBSKERNEL6) && defined(OCTAVOX_VST3_DET_DECIMATED)

constexpr const char* kPluginDisplayName = "PitchLab Harmonizer ObsKernel6 Balanced";
constexpr const char* kBundleName = "PitchLab_ObsKernel6_Balanced";
/// Processor class FUID (R18-R20 identity, PRESERVED).
DECLARE_UID(OctavoxNativeProcessorUID, 0xE3E7FA70, 0x6A380D35, 0xB05F64B9, 0x1F1E2689)
DECLARE_UID(OctavoxNativeFactoryUID, 0x3D4CDBC6, 0xACACBB09, 0x077FD5B8, 0x0BD2F550)

#elif defined(OCTAVOX_VST3_INTERP_OBSKERNEL6) && defined(OCTAVOX_VST3_DET_HYBRID)

constexpr const char* kPluginDisplayName = "PitchLab Harmonizer ObsKernel6 Hybrid";
constexpr const char* kBundleName = "PitchLab_ObsKernel6_Hybrid";
DECLARE_UID(OctavoxNativeProcessorUID, 0xA9BE5A71, 0xD192F1BA, 0x5A32D5FA, 0x9315FFC0)
DECLARE_UID(OctavoxNativeFactoryUID, 0x1F7DA6FE, 0xB91C0509, 0xF117F422, 0xBD3528D7)

#elif defined(OCTAVOX_VST3_INTERP_OBSKERNEL6) && defined(OCTAVOX_VST3_DET_FULLRATE)

constexpr const char* kPluginDisplayName = "PitchLab Harmonizer ObsKernel6 FullRate";
constexpr const char* kBundleName = "PitchLab_ObsKernel6_FullRate";
/// OFFLINE/BENCHMARK ONLY -- not a shipped mode.
DECLARE_UID(OctavoxNativeProcessorUID, 0x64E9EEBA, 0xF112DDED, 0xABBE2CD9, 0x242695D3)
DECLARE_UID(OctavoxNativeFactoryUID, 0x9F215D3E, 0xDE8ABFCA, 0x3A1423A0, 0xC0FACAE3)

#else
#error "octavox_build_config.h: no (interpolation x detector) variant selected"
#endif

//--------------------------------------------------------------------------
// The single engine-configuration helper every consumer uses
//--------------------------------------------------------------------------

/// The engine configuration for THIS build variant: the pure core defaults
/// (Config{}) with the wrapper's compile-time interpolation AND detector
/// selections pinned EXPLICITLY (both are compile-time properties of the
/// module, never host-visible parameters). Used by:
///   * the processor constructor / initialization paths,
///   * state restore (the variant selections are authoritative),
///   * the wrapper reference tests' hand-driven core references,
///   * the compiled-module integration tests' core references.
inline Config referenceConfig() {
    Config c{};  // core defaults (16.0.0: decimated detector default)
    c.interpolation = kDefaultInterpolation;
    c.detector_mode = kDefaultDetectorMode;
    return c;
}

/// The .vst3 bundle file name of THIS variant (PitchLab_<Variant>.vst3).
inline std::string bundleFileName() { return std::string(kBundleName) + ".vst3"; }

}  // namespace build
}  // namespace vst3wrap
}  // namespace octavox
