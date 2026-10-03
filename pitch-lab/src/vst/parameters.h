#pragma once

// Pitch Lab VST3 product layer — THE ONE AUTHORITATIVE PARAMETER MODEL
// (product-phase specification §6).
//
// This header is the single definition of VST parameter identity for the
// Pitch Lab plug-in. Since Task 29 the model is OWNERSHIP-EXPLICIT:
//
//   * SHARED REALTIME CONTROLS (kPitch, kLfoRate, kLfoDepth, kMix, kBypass,
//     kOutputLevel, plus the kEngine selector) are declared ONCE here — the
//     product's musical surface, shared by every engine. PITCH/LFO are the
//     shared realtime pitch-curve surface (the adapter resolves the curve per
//     sample; the selected engine receives it through its capabilities).
//   * ENGINE-OWNED CONFIGURATION PARAMETERS are GENERATED at runtime from
//     the engine registry's EngineParamDescriptor tables (engine_registry.h
//     — the ENGINE declares its own configuration surface; the registry is
//     the single authoritative engine list). The ONLY VST-side table is the
//     stable (engineId, key) -> ParamID binding — VST3 parameter IDs are a
//     VST-layer concept and remain frozen; the engine-parameter MEMBERSHIP
//     is never duplicated here.
//
// The processor's parameter registration, the editor's control binding and
// the tests are ALL generated from this table — no second list exists.
//
// ENGINE IDENTITY comes from the v0.1 research engine registry
// (core/engine_registry.h, registerProductionEngines — the single
// authoritative registration point, architecture §D.6). The ENGINE
// parameter's discrete values are the REGISTRY ITERATION ORDER indices;
// the mapping to engine ids is performed at runtime from the sealed
// registry (never from a duplicated list).
//
// The runtime-facing subset (what the adapter consumes per block) is the
// ParamSnapshot POD; the VST normalised-value mapping is defined here and
// exercised by the parameter + state round-trip tests.

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "core/engine_registry.h"

namespace pitchlab::vst {

// ---------------------------------------------------------------------------
// Parameter tags (VST3 ParamID). Fixed, stable, serialised in state.
// ---------------------------------------------------------------------------

namespace param {

enum Tags : uint32_t {
  // --- engine + pitch (product surface) -----------------------------------
  kEngine = 1,       // discrete: engine registry index
  kPitch = 2,        // -12.0 .. +12.0 semitones
  kLfoRate = 3,      // 0.0 .. 8 Hz — 0 Hz = LFO OFF (Task 32: the sine LFO
                     // contributes nothing and its phase is parked at the
                     // zero crossing; the normalised mapping is exact at 0)
  kLfoDepth = 4,     // 0 .. 2 semitones (0 = LFO off)
  // --- per-engine panels (values persist; applied only to the selected
  //     engine — declared, not fake: the UI shows only the selected
  //     engine's controls) -------------------------------------------------
  kVsQuality = 10,       // varispeed: 0 small / 1 standard / 2 reference
  kVsAllowAliasing = 11, // varispeed: bool
  kVdExcursion = 12,     // vardelay: 0.05 .. 2.0 s
  kVdCrossfade = 13,     // vardelay: 0 .. 8192 frames
  kGrGrain = 14,         // granular: 0.02 .. 0.5 s
  kGrOverlap = 15,       // granular: 4 .. 16
  kGrJitter = 16,        // granular: 0 .. 256 frames
  kGrWindow = 17,        // granular: 0 hann / 1 triangular
  kPvcFft = 18,          // pv.classic: 0 1024 / 1 2048 / 2 4096
  kPvcHop = 19,          // pv.classic: 0 128 / 1 256 / 2 512 / 3 1024
  kPvpFft = 20,          // pv.phaselocked: as kPvcFft
  kPvpHop = 21,          // pv.phaselocked: as kPvcHop
  // --- native.timepitch (task 33; §6.6 table + product spec §6 amendment) --
  // The six frozen tags of the task-33 amendment; kTpTolerance/kTpFormant
  // land with their mode checkpoints (the tags are reserved from the
  // amendment — no other parameter may take 26/27).
  kTpMode = 22,          // timepitch: mode choice index (fixed/adaptive/
                         // pitch_synced/pitch_formant — landed per checkpoint)
  kTpWindow = 23,        // timepitch: 64 .. 16384 frames (Fixed+Adaptive)
  kTpOverlap = 24,       // timepitch: 0 2x / 1 4x / 2 8x (Fixed+Adaptive)
  kTpShape = 25,         // timepitch: 0 hann / 1 hamming / 2 bartlett / 3 rect
  kTpTolerance = 26,     // timepitch: 0 .. 8192 frames (Adaptive; reserved)
  kTpFormant = 27,       // timepitch: 0.25 .. 4.0 (Pitch+Formant; reserved)
  // --- mix / output --------------------------------------------------------
  kMix = 30,         // 0 dry .. 1 wet
  kBypass = 31,      // VST3 bypass (Bypass parameter)
  kOutputLevel = 32, // -24 .. +12 dB
};

}  // namespace param

// ---------------------------------------------------------------------------
// Metadata (drives registration + UI + tests; the ONE table).
//
// The shared realtime rows are the static kSharedTable (parameters.cpp);
// the engine-configuration rows are GENERATED from the registry's
// EngineParamDescriptor tables + the stable binding. `role` makes the
// shared/engine-owned split explicit; `engineId` records the owning engine
// for engine-configuration rows ("" for shared rows).
// ---------------------------------------------------------------------------

/// The product-layer role of one parameter (Task 29: the ownership split).
enum class ParamRole {
  SharedRealtime,       // the shared musical surface (pitch/LFO/mix/level/
                         // bypass) + the engine selector — NOT owned by any
                         // engine
  EngineConfiguration,  // owned by exactly one engine (registry descriptor)
};

struct ParamMeta {
  uint32_t tag;
  const char* id;      // stable string id (status/debug/state docs)
  const char* title;   // UI title
  const char* shortTitle;
  const char* units;   // display units ("" / "st" / "Hz" / "dB" / ...)
  double min;
  double max;
  double defaultPlain;
  int stepCount;       // -1 = continuous, 0 = toggle, n = n+1 steps
  const char* group;   // UI group
  const char* dispFmt; // printf-style for the value, e.g. "%+.2f"
  // --- Task 29 extensions (defaults keep aggregate initialisation working) ---
  ParamRole role = ParamRole::SharedRealtime;
  bool automatable = true;  // host-automation capability (VST kCanAutomate)
  const char* engineId = "";  // owning engine (EngineConfiguration rows)
  // discrete-choice data (engine-declared, registry-derived; nullptr for
  // continuous/bool parameters)
  const char* const* choiceNames = nullptr;  // display + engine-facing names
  const double* choiceValues = nullptr;      // engine-facing values (Int kind)
  int choiceCount = 0;
};

/// The full parameter table in registration order (shared rows first, then
/// engine-owned rows in registry order). Count via parameterCount().
const ParamMeta* parameterTable();
uint32_t parameterCount();

/// Find one parameter's meta by tag (nullptr when unknown). The returned
/// pointer is stable for the process lifetime.
[[nodiscard]] const ParamMeta* findParamMeta(uint32_t tag);

// ---------------------------------------------------------------------------
// Discrete-choice mappings: ENGINE-OWNED since Task 29. The quality names,
// FFT sizes, hop sizes and window shapes live in the engine descriptor
// tables (src/engines/*.cpp) and reach this layer through the generated
// ParamMeta rows (choiceNames / choiceValues). No second copy exists.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Engine-owned parameter enumeration (the UI-facing flow: selected engine ->
// registry lookup -> engine parameter descriptors -> UI control creation).
// The registry is the authority; `tag` resolves each descriptor to its
// stable VST parameter (the controller binding point).
// ---------------------------------------------------------------------------

struct EngineParamView {
  const EngineParamDescriptor* descriptor = nullptr;  // registry-owned
  uint32_t tag = 0;                                    // stable VST ParamID
  const ParamMeta* meta = nullptr;                     // generated model row
};

/// The exposed engine-owned parameters of one engine, in the engine's own
/// declared order (empty for an out-of-range index). Stable for the process
/// lifetime.
[[nodiscard]] const std::vector<EngineParamView>& engineParamsFor(int engineIndex);

/// The stable VST tag for one engine-owned parameter (0 when the engine
/// does not declare an exposed parameter with that key — e.g. fixed
/// engine-internal values).
[[nodiscard]] uint32_t vstTagForEngineParam(const char* engineId, const char* key);

/// Model integrity check (tests + diagnostics): every exposed engine
/// parameter has a binding; every binding matches a declared parameter;
/// the descriptor key set matches parameterKeys; the generated rows are
/// well-formed. Returns the issue list (EMPTY == the model is coherent).
[[nodiscard]] std::vector<std::string> validateEngineParameterModel();

// ---------------------------------------------------------------------------
// Runtime parameter snapshot (the adapter-facing plain values).
// A trivially-copyable POD, published through SeqLock (seqlock.h).
// Written by the parameter owner (main thread setParameter + the
// per-block automation queue merge, see processor.cpp), read by the
// preparation thread (chain decisions) and the audio thread.
// ---------------------------------------------------------------------------

struct ParamSnapshot {
  // engine + pitch
  int engineIndex = 1;        // registry order default: native.vardelay
  double pitchSt = 0.0;
  double lfoRateHz = 5.0;
  double lfoDepthSt = 0.0;
  // varispeed
  int vsQuality = 2;          // "reference" (v0.1 default)
  bool vsAllowAliasing = false;
  // vardelay
  double vdExcursionSec = 0.5;    // v0.1 default
  int64_t vdCrossfadeFrames = 2048;  // v0.1 default
  // granular
  double grGrainSec = 0.1;     // v0.1 default
  int64_t grOverlap = 4;       // v0.1 default
  int64_t grJitterFrames = 0;  // v0.1 default
  int grWindowTriangular = 0;  // 0 = hann
  // pv classic / phaselocked
  int pvcFftSize = 2048;       // v0.1 default
  int pvcHop = 512;            // v0.1 default
  int pvpFftSize = 2048;       // v0.1 default
  int pvpHop = 512;            // v0.1 default
  // native.timepitch (task 33; §6.6 defaults)
  int tpMode = 0;              // choice index: 0 = fixed (the §6.6 default)
  int64_t tpWindowFrames = 2048;
  int64_t tpOverlap = 2;       // ENGINE-FACING value (2/4/8 via choiceValues)
  int tpShape = 0;             // choice index: 0 = hann
  // mix / output
  double mix = 1.0;
  bool bypass = false;
  double outputDb = 0.0;

  // --- derived helpers -----------------------------------------------------

  /// The live linear ratio from the pitch parameter (envelope-independent).
  [[nodiscard]] double liveRatio() const { return std::exp2(pitchSt / 12.0); }

  /// Output gain (linear, from dB).
  [[nodiscard]] double outputGain() const { return std::exp2(outputDb / 6.020599913279624); }

  /// A stable signature of everything that requires a NEW CHAIN when it
  /// changes (engine identity, engine parameters, channel count is handled
  /// separately by the processor). Pitch/LFO/mix/level do NOT require a
  /// chain rebuild (they live inside the envelope / the mix path).
  struct ChainSignature {
    int engineIndex;
    int vsQuality;
    bool vsAllowAliasing;
    double vdExcursionSec;
    int64_t vdCrossfadeFrames;
    double grGrainSec;
    int64_t grOverlap;
    int64_t grJitterFrames;
    int grWindowTriangular;
    int pvcFftSize;
    int pvcHop;
    int pvpFftSize;
    int pvpHop;
    // native.timepitch (task 33): mode ∈ ChainSignature (§6.6.1 item 17 —
    // a mode change is a chain rebuild through the existing adapter
    // lifecycle; NO second lifecycle).
    int tpMode;
    int64_t tpWindowFrames;
    int64_t tpOverlap;
    int tpShape;

    bool operator==(const ChainSignature& o) const = default;
  };

  [[nodiscard]] ChainSignature chainSignature() const {
    return ChainSignature{engineIndex,         vsQuality,
                          vsAllowAliasing,      vdExcursionSec,
                          vdCrossfadeFrames,    grGrainSec,
                          grOverlap,            grJitterFrames,
                          grWindowTriangular,   pvcFftSize,
                          pvcHop,               pvpFftSize,
                          pvpHop,               tpMode,
                          tpWindowFrames,       tpOverlap,
                          tpShape};
  }
};

// ---------------------------------------------------------------------------
// VST normalised <-> plain mapping (linear for continuous, step-mapped for
// discrete, bool as 0/1). ONE implementation, used by the processor and the
// tests (deterministic parameter handling requirement).
// ---------------------------------------------------------------------------

[[nodiscard]] double normalise(uint32_t tag, double plain);
[[nodiscard]] double denormalise(uint32_t tag, double norm);

/// Apply a normalised VST value to the snapshot (one field per tag).
void applyNormalised(ParamSnapshot& snap, uint32_t tag, double norm);

/// The plain value of one tag from the snapshot (for state + status).
[[nodiscard]] double plainValue(const ParamSnapshot& snap, uint32_t tag);

// ---------------------------------------------------------------------------
// Type-safe display formatting + parsing (Task 24, P1.8/P1.9)
//
// The previous implementation forwarded a DOUBLE through the model table's
// printf format string: "%d" and even "%s" entries were undefined behaviour
// (varargs type mismatch — a crash-class defect in every host generic UI that
// displayed those values). Formatting is now TAG-AWARE (one place); the
// table's dispFmt is used ONLY for real-valued parameters (every remaining
// entry consumes a double).
//
// Parsing is tag-aware too (the previous std::atof accepted ANY string:
// "on" silently became 0). Invalid input is REJECTED (false), never silently
// zero.
// ---------------------------------------------------------------------------

/// Format a plain value for display (deterministic, type-correct).
void formatParamValue(uint32_t tag, double plain, char* buf, std::size_t bufSize);

/// Parse a display string back to a plain value. Accepts the canonical
/// forms produced by formatParamValue (plus common equivalents: engine
/// id/name, actual FFT/hop sizes, 1/0 for toggles). Whitespace-separated
/// unit suffixes are the CALLER's responsibility to strip. Returns false
/// and leaves `plainOut` untouched when the text is not a valid
/// representation of the parameter's value domain.
[[nodiscard]] bool parseParamPlain(uint32_t tag, const char* text, double& plainOut);

/// Semitone ratio helper (shared by the adapter tests).
[[nodiscard]] inline double semitonesToRatio(double st) { return std::exp2(st / 12.0); }

// ---------------------------------------------------------------------------
// Engine registry binding (the ONLY place the plug-in reads engine
// identity; returns nullptr when the index is out of registry range).
// ---------------------------------------------------------------------------

/// The sealed production registry (process-wide; the plug-in initialises
/// it once at module init — the same single authoritative registration
/// point the CLI uses). NOT thread-safe to call before initRegistry().
const EngineRegistry& engineRegistry();
void initEngineRegistryOnce();  // idempotent, called at module/test startup

/// Registry index -> engine id (nullptr when out of range).
[[nodiscard]] const char* engineIdForIndex(int index);
/// Engine display name from the registry (nullptr when out of range).
[[nodiscard]] const char* engineNameForIndex(int index);
/// The number of engines the selector offers (registry size).
int engineCount();

}  // namespace pitchlab::vst
