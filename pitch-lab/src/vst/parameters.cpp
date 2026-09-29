#include "vst/parameters.h"

#include <array>
#include <mutex>
#include <string>
#include <vector>

#include "core/pitch_engine.h"

namespace pitchlab::vst {

namespace {

// The ONE parameter table. Registration order == the order the processor
// registers parameters in == the order the state stream writes them ==
// the UI's binding reference.
constexpr std::array<ParamMeta, 19> kTable{{
    {param::kEngine, "engine", "Engine", "ENGINE", "", 0, 4, 1, 4,
     "Engine", "%d"},
    {param::kPitch, "pitch", "Pitch", "PITCH", "st", -12.0, 12.0, 0.0, -1,
     "Pitch", "%+.2f"},
    {param::kLfoRate, "lfo_rate", "LFO Rate", "LFO RATE", "Hz", 0.1, 8.0, 5.0, -1,
     "Pitch", "%.2f"},
    {param::kLfoDepth, "lfo_depth", "LFO Depth", "LFO DEPTH", "st", 0.0, 2.0, 0.0, -1,
     "Pitch", "%.2f"},
    {param::kVsQuality, "vs_quality", "Resample Quality", "VS QUALITY", "", 0, 2, 2, 2,
     "Varispeed", "%d"},
    {param::kVsAllowAliasing, "vs_allow_aliasing", "Allow Aliasing", "VS ALIAS", "", 0, 1, 0, 1,
     "Varispeed", "%s"},
    {param::kVdExcursion, "vd_excursion", "Excursion", "VD EXCUR", "s", 0.05, 2.0, 0.5, -1,
     "Vardelay", "%.3f"},
    {param::kVdCrossfade, "vd_crossfade", "Crossfade", "VD XFADE", "frm", 0, 8192, 2048, -1,
     "Vardelay", "%d"},
    {param::kGrGrain, "gr_grain", "Grain Length", "GR GRAIN", "s", 0.02, 0.5, 0.1, -1,
     "Granular", "%.3f"},
    {param::kGrOverlap, "gr_overlap", "Overlap", "GR OVLAP", "x", 4, 16, 4, -1,
     "Granular", "%d"},
    {param::kGrJitter, "gr_jitter", "Jitter", "GR JIT", "frm", 0, 256, 0, -1,
     "Granular", "%d"},
    {param::kGrWindow, "gr_window", "Window", "GR WNDW", "", 0, 1, 0, 1,
     "Granular", "%s"},
    {param::kPvcFft, "pvc_fft", "FFT Size", "PVC FFT", "", 0, 2, 1, 2,
     "PV Classic", "%d"},
    {param::kPvcHop, "pvc_hop", "Hop", "PVC HOP", "", 0, 3, 2, 3,
     "PV Classic", "%d"},
    {param::kPvpFft, "pvp_fft", "FFT Size", "PVP FFT", "", 0, 2, 1, 2,
     "PV PhaseLocked", "%d"},
    {param::kPvpHop, "pvp_hop", "Hop", "PVP HOP", "", 0, 3, 2, 3,
     "PV PhaseLocked", "%d"},
    {param::kMix, "mix", "Dry/Wet", "DRY/WET", "", 0.0, 1.0, 1.0, -1,
     "Output", "%.2f"},
    {param::kBypass, "bypass", "Bypass", "BYPASS", "", 0, 1, 0, 1,
     "Output", "%s"},
    {param::kOutputLevel, "output_level", "Output Level", "LEVEL", "dB", -24.0, 12.0, 0.0, -1,
     "Output", "%+.1f"},
}};

}  // namespace

const ParamMeta* parameterTable() { return kTable.data(); }

uint32_t parameterCount() { return static_cast<uint32_t>(kTable.size()); }

// ---------------------------------------------------------------------------
// Normalisation
// ---------------------------------------------------------------------------

double normalise(uint32_t tag, double plain) {
  for (const auto& m : kTable) {
    if (m.tag != tag) continue;
    const double span = m.max - m.min;
    if (span <= 0.0) return 0.0;
    double v = (plain - m.min) / span;
    if (m.stepCount > 0) {
      // VST3 step semantics: stepCount = steps BETWEEN values; the plain
      // values are min + k*(span/stepCount), k in [0, stepCount]. A plain
      // value between steps snaps to the nearest step (round-half-up).
      const double k = std::floor(v * m.stepCount + 0.5);
      v = k / m.stepCount;
    }
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return v;
  }
  return 0.0;
}

double denormalise(uint32_t tag, double norm) {
  for (const auto& m : kTable) {
    if (m.tag != tag) continue;
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;
    double v = norm;
    if (m.stepCount > 0) {
      const double k = std::floor(v * m.stepCount + 0.5);
      v = k / m.stepCount;
    }
    return m.min + v * (m.max - m.min);
  }
  return 0.0;
}

void applyNormalised(ParamSnapshot& snap, uint32_t tag, double norm) {
  const double p = denormalise(tag, norm);
  switch (tag) {
    case param::kEngine: snap.engineIndex = static_cast<int>(p); break;
    case param::kPitch: snap.pitchSt = p; break;
    case param::kLfoRate: snap.lfoRateHz = p; break;
    case param::kLfoDepth: snap.lfoDepthSt = p; break;
    case param::kVsQuality: snap.vsQuality = static_cast<int>(p); break;
    case param::kVsAllowAliasing: snap.vsAllowAliasing = (p >= 0.5); break;
    case param::kVdExcursion: snap.vdExcursionSec = p; break;
    case param::kVdCrossfade: snap.vdCrossfadeFrames = static_cast<int64_t>(p); break;
    case param::kGrGrain: snap.grGrainSec = p; break;
    case param::kGrOverlap: snap.grOverlap = static_cast<int64_t>(p); break;
    case param::kGrJitter: snap.grJitterFrames = static_cast<int64_t>(p); break;
    case param::kGrWindow: snap.grWindowTriangular = (p >= 0.5) ? 1 : 0; break;
    case param::kPvcFft: snap.pvcFftSize = fftChoice(static_cast<int>(p)); break;
    case param::kPvcHop: snap.pvcHop = hopChoice(static_cast<int>(p)); break;
    case param::kPvpFft: snap.pvpFftSize = fftChoice(static_cast<int>(p)); break;
    case param::kPvpHop: snap.pvpHop = hopChoice(static_cast<int>(p)); break;
    case param::kMix: snap.mix = p; break;
    case param::kBypass: snap.bypass = (p >= 0.5); break;
    case param::kOutputLevel: snap.outputDb = p; break;
    default: break;  // unknown tags are ignored (never crash the audio path)
  }
}

double plainValue(const ParamSnapshot& snap, uint32_t tag) {
  switch (tag) {
    case param::kEngine: return static_cast<double>(snap.engineIndex);
    case param::kPitch: return snap.pitchSt;
    case param::kLfoRate: return snap.lfoRateHz;
    case param::kLfoDepth: return snap.lfoDepthSt;
    case param::kVsQuality: return static_cast<double>(snap.vsQuality);
    case param::kVsAllowAliasing: return snap.vsAllowAliasing ? 1.0 : 0.0;
    case param::kVdExcursion: return snap.vdExcursionSec;
    case param::kVdCrossfade: return static_cast<double>(snap.vdCrossfadeFrames);
    case param::kGrGrain: return snap.grGrainSec;
    case param::kGrOverlap: return static_cast<double>(snap.grOverlap);
    case param::kGrJitter: return static_cast<double>(snap.grJitterFrames);
    case param::kGrWindow: return snap.grWindowTriangular ? 1.0 : 0.0;
    case param::kPvcFft: {
      for (int i = 0; i < 3; ++i) {
        if (kFftSizeChoices[i] == snap.pvcFftSize) return static_cast<double>(i);
      }
      return 1.0;
    }
    case param::kPvcHop: {
      for (int i = 0; i < 4; ++i) {
        if (kHopChoices[i] == snap.pvcHop) return static_cast<double>(i);
      }
      return 2.0;
    }
    case param::kPvpFft: {
      for (int i = 0; i < 3; ++i) {
        if (kFftSizeChoices[i] == snap.pvpFftSize) return static_cast<double>(i);
      }
      return 1.0;
    }
    case param::kPvpHop: {
      for (int i = 0; i < 4; ++i) {
        if (kHopChoices[i] == snap.pvpHop) return static_cast<double>(i);
      }
      return 2.0;
    }
    case param::kMix: return snap.mix;
    case param::kBypass: return snap.bypass ? 1.0 : 0.0;
    case param::kOutputLevel: return snap.outputDb;
    default: return 0.0;
  }
}

// ---------------------------------------------------------------------------
// Engine registry binding
// ---------------------------------------------------------------------------

namespace {
EngineRegistry& mutableRegistry() {
  static EngineRegistry registry;
  return registry;
}
std::once_flag& registryOnce() {
  static std::once_flag once;
  return once;
}
}  // namespace

void initEngineRegistryOnce() {
  std::call_once(registryOnce(), [] {
    EngineRegistry& r = mutableRegistry();
    registerProductionEngines(r);
    r.seal();
  });
}

const EngineRegistry& engineRegistry() {
  initEngineRegistryOnce();
  return mutableRegistry();
}

const char* engineIdForIndex(int index) {
  const EngineRegistry& r = engineRegistry();
  if (index < 0 || index >= static_cast<int>(r.size())) return nullptr;
  return r.at(static_cast<std::size_t>(index)).info.id;
}

const char* engineNameForIndex(int index) {
  const EngineRegistry& r = engineRegistry();
  if (index < 0 || index >= static_cast<int>(r.size())) return nullptr;
  return r.at(static_cast<std::size_t>(index)).info.displayName;
}

int engineCount() { return static_cast<int>(engineRegistry().size()); }

}  // namespace pitchlab::vst
