#pragma once

// Pitch Lab VST3 product layer — THE ONE AUTHORITATIVE PARAMETER MODEL
// (product-phase specification §6).
//
// This header is the single definition of VST parameter identity for the
// Pitch Lab plug-in: tags, types, ranges, defaults, units, groups, titles.
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
  kLfoRate = 3,      // 0.1 .. 8 Hz (sine LFO on the ratio, st domain)
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
  // --- mix / output --------------------------------------------------------
  kMix = 30,         // 0 dry .. 1 wet
  kBypass = 31,      // VST3 bypass (Bypass parameter)
  kOutputLevel = 32, // -24 .. +12 dB
};

}  // namespace param

// ---------------------------------------------------------------------------
// Metadata (drives registration + UI + tests; the ONE table).
// ---------------------------------------------------------------------------

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
};

/// The full parameter table in registration order. Count via kParamCount.
const ParamMeta* parameterTable();
uint32_t parameterCount();

// ---------------------------------------------------------------------------
// Discrete-choice mappings (the only value maps that exist).
// ---------------------------------------------------------------------------

inline constexpr int kFftSizeChoices[] = {1024, 2048, 4096};
inline constexpr int kHopChoices[] = {128, 256, 512, 1024};
inline constexpr const char* kVsQualityNames[] = {"small", "standard", "reference"};

inline int fftChoice(int discreteIdx) {
  return kFftSizeChoices[discreteIdx < 0 ? 0
                      : discreteIdx > 2 ? 2 : discreteIdx];
}
inline int hopChoice(int discreteIdx) {
  return kHopChoices[discreteIdx < 0 ? 0
                    : discreteIdx > 3 ? 3 : discreteIdx];
}

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

    bool operator==(const ChainSignature& o) const = default;
  };

  [[nodiscard]] ChainSignature chainSignature() const {
    return ChainSignature{engineIndex,         vsQuality,
                          vsAllowAliasing,      vdExcursionSec,
                          vdCrossfadeFrames,    grGrainSec,
                          grOverlap,            grJitterFrames,
                          grWindowTriangular,   pvcFftSize,
                          pvcHop,               pvpFftSize,
                          pvpHop};
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
