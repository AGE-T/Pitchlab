#include "vst/parameters.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    // Task 24 (P1.7): crossfade/overlap/jitter are INTEGER-domain parameters
    // (the v0.1 engine contract) — previously declared continuous and
    // TRUNCATED into the snapshot (a metadata/semantics mismatch: hosts saw
    // continuous sliders, the engines received floor(plain)). They are now
    // properly discrete (stepCount = last legal integer) with round-half-up
    // snapping, so plain -> normalized -> plain round-trips exactly and
    // host UIs offer stepped controls.
    {param::kVdCrossfade, "vd_crossfade", "Crossfade", "VD XFADE", "frm", 0, 8192, 2048, 8192,
     "Vardelay", "%d"},
    {param::kGrGrain, "gr_grain", "Grain Length", "GR GRAIN", "s", 0.02, 0.5, 0.1, -1,
     "Granular", "%.3f"},
    {param::kGrOverlap, "gr_overlap", "Overlap", "GR OVLAP", "x", 4, 16, 4, 12,
     "Granular", "%d"},
    {param::kGrJitter, "gr_jitter", "Jitter", "GR JIT", "frm", 0, 256, 0, 256,
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
// Type-safe display formatting + parsing (Task 24, P1.8/P1.9)
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] int clampInt(double v, int lo, int hi) {
  return v < static_cast<double>(lo) ? lo
         : v > static_cast<double>(hi) ? hi
                                       : static_cast<int>(std::llround(v));
}

[[nodiscard]] bool equalsInsensitive(const char* a, const char* b) {
  while (*a != '\0' && *b != '\0') {
    if (std::tolower(static_cast<unsigned char>(*a)) !=
        std::tolower(static_cast<unsigned char>(*b))) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

/// Strict numeric head parse: consumes the whole string as one number.
[[nodiscard]] bool parseNumber(const char* text, double& out) {
  if (text == nullptr || *text == '\0') return false;
  char* end = nullptr;
  const double v = std::strtod(text, &end);
  if (end == text) return false;         // no number consumed
  while (*end != '\0') {                 // trailing junk -> invalid
    if (!std::isspace(static_cast<unsigned char>(*end))) return false;
    ++end;
  }
  if (!std::isfinite(v)) return false;
  out = v;
  return true;
}

const ParamMeta* findMetaByTag(uint32_t tag) {
  for (const auto& m : kTable) {
    if (m.tag == tag) return &m;
  }
  return nullptr;
}

}  // namespace

void formatParamValue(uint32_t tag, double plain, char* buf, std::size_t bufSize) {
  if (buf == nullptr || bufSize == 0) return;
  buf[0] = '\0';
  switch (tag) {
    case param::kVsAllowAliasing:
    case param::kBypass:
      std::snprintf(buf, bufSize, "%s", plain >= 0.5 ? "on" : "off");
      return;
    case param::kGrWindow:
      std::snprintf(buf, bufSize, "%s", plain >= 0.5 ? "triangular" : "hann");
      return;
    case param::kVsQuality:
      std::snprintf(buf, bufSize, "%s",
                    kVsQualityNames[clampInt(plain, 0, 2)]);
      return;
    case param::kEngine:
    case param::kVdCrossfade:
    case param::kGrOverlap:
    case param::kGrJitter:
    case param::kPvcFft:
    case param::kPvcHop:
    case param::kPvpFft:
    case param::kPvpHop:
      std::snprintf(buf, bufSize, "%d", static_cast<int>(std::llround(plain)));
      return;
    default: {
      // real-valued parameters: the table's format (every remaining dispFmt
      // consumes a double — the type mismatch that was P1.8 is structurally
      // impossible now)
      const ParamMeta* meta = findMetaByTag(tag);
      if (meta == nullptr) return;
      std::snprintf(buf, bufSize, meta->dispFmt, plain);
      return;
    }
  }
}

bool parseParamPlain(uint32_t tag, const char* text, double& plainOut) {
  if (text == nullptr) return false;
  // skip leading whitespace
  while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text))) ++text;
  if (*text == '\0') return false;

  double v = 0.0;
  switch (tag) {
    case param::kVsAllowAliasing:
    case param::kBypass:
    case param::kGrWindow:
      if (equalsInsensitive(text, "on") || equalsInsensitive(text, "1")) {
        plainOut = 1.0;
        return true;
      }
      if (equalsInsensitive(text, "off") || equalsInsensitive(text, "0")) {
        plainOut = 0.0;
        return true;
      }
      if (tag == param::kGrWindow &&
          (equalsInsensitive(text, "hann") || equalsInsensitive(text, "triangular"))) {
        plainOut = equalsInsensitive(text, "triangular") ? 1.0 : 0.0;
        return true;
      }
      if (tag == param::kBypass || tag == param::kVsAllowAliasing) {
        if (equalsInsensitive(text, "true") || equalsInsensitive(text, "false")) {
          plainOut = equalsInsensitive(text, "true") ? 1.0 : 0.0;
          return true;
        }
      }
      return false;  // invalid toggle text: rejected, never silently zero
    case param::kVsQuality:
      for (int i = 0; i < 3; ++i) {
        if (equalsInsensitive(text, kVsQualityNames[i])) {
          plainOut = static_cast<double>(i);
          return true;
        }
      }
      if (parseNumber(text, v)) {
        plainOut = v;
        return true;
      }
      return false;
    case param::kEngine: {
      // canonical: the registry index; also accept the engine id or display
      // name (case-insensitive) — identity comes from the registry, never a
      // second list
      if (parseNumber(text, v)) {
        plainOut = v;
        return true;
      }
      const int n = engineCount();
      for (int i = 0; i < n; ++i) {
        const char* id = engineIdForIndex(i);
        const char* name = engineNameForIndex(i);
        if ((id != nullptr && equalsInsensitive(text, id)) ||
            (name != nullptr && equalsInsensitive(text, name))) {
          plainOut = static_cast<double>(i);
          return true;
        }
      }
      return false;
    }
    case param::kPvcFft:
    case param::kPvpFft: {
      // canonical: the choice INDEX; also accept the actual FFT size
      // (1024/2048/4096 — mapped back to the index)
      if (parseNumber(text, v)) {
        for (int i = 0; i < 3; ++i) {
          if (v == static_cast<double>(kFftSizeChoices[i])) {
            plainOut = static_cast<double>(i);
            return true;
          }
        }
        plainOut = v;  // an index (validated by normalise's range clamp)
        return true;
      }
      return false;
    }
    case param::kPvcHop:
    case param::kPvpHop: {
      if (parseNumber(text, v)) {
        for (int i = 0; i < 4; ++i) {
          if (v == static_cast<double>(kHopChoices[i])) {
            plainOut = static_cast<double>(i);
            return true;
          }
        }
        plainOut = v;  // an index (validated by normalise's range clamp)
        return true;
      }
      return false;
    }
    default:
      // real/integer-valued parameters: strict numeric parse (the caller
      // strips whitespace-separated unit suffixes)
      if (parseNumber(text, v)) {
        plainOut = v;
        return true;
      }
      return false;
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
