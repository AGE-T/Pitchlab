#include "vst/parameters.h"

#include <algorithm>
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

// ---------------------------------------------------------------------------
// The SHARED realtime control rows (the product's musical surface — Task 29
// makes the shared ownership EXPLICIT; these are NOT engine parameters and
// no engine declares them). PITCH/LFO are the shared realtime pitch-curve
// surface: the adapter resolves the curve per sample and the selected engine
// receives it through its declared dynamic-ratio capability.
//
// Registration order == the order the processor registers parameters in ==
// the order the state stream writes them == the UI's binding reference.
// ---------------------------------------------------------------------------

constexpr std::array<ParamMeta, 7> kSharedTable{{
    {param::kEngine, "engine", "Engine", "ENGINE", "", 0, 4, 1, 4,
     "Engine", "%d", ParamRole::SharedRealtime, /*automatable=*/false},
    {param::kPitch, "pitch", "Pitch", "PITCH", "st", -12.0, 12.0, 0.0, -1,
     "Pitch", "%+.2f", ParamRole::SharedRealtime, true},
    // Task 32: the product-facing rate domain is 0.0..8.0 Hz — 0 Hz is the
    // real OFF state (the LFO contributes nothing and its phase is parked
    // deterministically at the zero crossing; see the adapter's curve loop).
    // The normalised mapping is exact at the boundary: normalise(0) == 0,
    // denormalise(0) == 0.0 (a saved/restored OFF state round-trips exactly).
    {param::kLfoRate, "lfo_rate", "LFO Rate", "LFO RATE", "Hz", 0.0, 8.0, 5.0, -1,
     "Pitch", "%.2f", ParamRole::SharedRealtime, true},
    {param::kLfoDepth, "lfo_depth", "LFO Depth", "LFO DEPTH", "st", 0.0, 2.0, 0.0, -1,
     "Pitch", "%.2f", ParamRole::SharedRealtime, true},
    {param::kMix, "mix", "Dry/Wet", "DRY/WET", "", 0.0, 1.0, 1.0, -1,
     "Output", "%.2f", ParamRole::SharedRealtime, true},
    {param::kBypass, "bypass", "Bypass", "BYPASS", "", 0, 1, 0, 1,
     "Output", "%s", ParamRole::SharedRealtime, true},
    {param::kOutputLevel, "output_level", "Output Level", "LEVEL", "dB", -24.0, 12.0, 0.0, -1,
     "Output", "%+.1f", ParamRole::SharedRealtime, true},
}};

// ---------------------------------------------------------------------------
// The stable (engineId, key) -> VST ParamID binding (Task 29).
//
// VST3 parameter IDs are a VST-LAYER concept: they are frozen here, once,
// and NEVER renumbered (state compatibility). This table does NOT duplicate
// engine-parameter MEMBERSHIP — the membership comes from the registry's
// EngineParamDescriptor tables; a binding row is only USED when the
// registry's engine actually declares an exposed parameter with that key
// (validateEngineParameterModel() enforces both directions; tests assert
// exact coverage). Everything else about an engine parameter (title, unit,
// domain, choices, role, automation capability) is read from the
// engine-owned descriptor.
// ---------------------------------------------------------------------------

struct EngineParamBinding {
  const char* engineId;
  const char* key;
  uint32_t tag;
  const char* id;  // stable product id (status/debug/state docs)
};

constexpr EngineParamBinding kEngineParamBindings[]{
    {"native.varispeed", "resample_quality", param::kVsQuality, "vs_quality"},
    {"native.varispeed", "allow_aliasing", param::kVsAllowAliasing, "vs_allow_aliasing"},
    {"native.vardelay", "excursion_seconds", param::kVdExcursion, "vd_excursion"},
    {"native.vardelay", "crossfade_frames", param::kVdCrossfade, "vd_crossfade"},
    {"native.pv.classic", "fft_size", param::kPvcFft, "pvc_fft"},
    {"native.pv.classic", "hop", param::kPvcHop, "pvc_hop"},
    {"native.pv.phaselocked", "fft_size", param::kPvpFft, "pvp_fft"},
    {"native.pv.phaselocked", "hop", param::kPvpHop, "pvp_hop"},
    {"native.granular", "grain_seconds", param::kGrGrain, "gr_grain"},
    {"native.granular", "overlap", param::kGrOverlap, "gr_overlap"},
    {"native.granular", "window", param::kGrWindow, "gr_window"},
    {"native.granular", "jitter_frames", param::kGrJitter, "gr_jitter"},
};

[[nodiscard]] const EngineParamBinding* findBinding(const char* engineId,
                                                    const char* key) {
  for (const EngineParamBinding& b : kEngineParamBindings) {
    if (std::strcmp(b.engineId, engineId) == 0 && std::strcmp(b.key, key) == 0) {
      return &b;
    }
  }
  return nullptr;
}

[[nodiscard]] int clampInt(double v, int lo, int hi) {
  return v < static_cast<double>(lo) ? lo
         : v > static_cast<double>(hi) ? hi
                                       : static_cast<int>(std::llround(v));
}

/// The engine-facing INTEGER value of a discrete Int parameter (the plain
/// domain is the choice-index domain when choiceValues is set). Defensive
/// fallback (lround) when the model row is unavailable (an integrity issue
/// is reported separately; the audio path never crashes).
[[nodiscard]] int64_t discreteIntValue(const ParamMeta* m, double plain) {
  if (m != nullptr && m->choiceValues != nullptr && m->choiceCount > 0) {
    return static_cast<int64_t>(
        std::llround(m->choiceValues[clampInt(plain, 0, m->choiceCount - 1)]));
  }
  return static_cast<int64_t>(std::llround(plain));
}

}  // namespace

// ---------------------------------------------------------------------------
// The generated model table (shared rows + engine-owned rows)
// ---------------------------------------------------------------------------

namespace {

const std::vector<ParamMeta>& modelTable() {
  // Built ONCE, thread-safely (magic static). The registry must be
  // initialised first (engineRegistry() does that); the registry is sealed
  // before any reader can walk it, so the table is immutable afterwards.
  static const std::vector<ParamMeta> table = [] {
    std::vector<ParamMeta> t;
    t.reserve(kSharedTable.size() + 12);
    for (const ParamMeta& m : kSharedTable) t.push_back(m);
    const EngineRegistry& reg = engineRegistry();
    for (std::size_t e = 0; e < reg.size(); ++e) {
      const EngineDescriptor& d = reg.at(e);
      for (const EngineParamDescriptor& p : d.parameters) {
        if (!p.exposed) continue;
        const EngineParamBinding* b = findBinding(d.info.id, p.key);
        if (b == nullptr) continue;  // missing binding — integrity check reports
        ParamMeta m;
        m.tag = b->tag;
        m.id = b->id;
        m.title = p.displayName;
        m.shortTitle = p.displayName;
        m.units = p.unit;
        m.min = p.min;
        m.max = p.max;
        m.defaultPlain = p.defaultPlain;
        m.stepCount = p.stepCount;
        m.group = d.info.displayName;
        m.dispFmt = p.dispFmt;
        m.role = ParamRole::EngineConfiguration;
        m.automatable = p.automatable;
        m.engineId = d.info.id;
        m.choiceNames = p.choiceNames;
        m.choiceValues = p.choiceValues;
        m.choiceCount = p.choiceCount;
        t.push_back(m);
      }
    }
    return t;
  }();
  return table;
}

}  // namespace

const ParamMeta* parameterTable() { return modelTable().data(); }

uint32_t parameterCount() { return static_cast<uint32_t>(modelTable().size()); }

const ParamMeta* findParamMeta(uint32_t tag) {
  for (const ParamMeta& m : modelTable()) {
    if (m.tag == tag) return &m;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Engine-owned parameter enumeration + binding resolution
// ---------------------------------------------------------------------------

uint32_t vstTagForEngineParam(const char* engineId, const char* key) {
  if (engineId == nullptr || key == nullptr) return 0;
  const EngineParamBinding* b = findBinding(engineId, key);
  return b != nullptr ? b->tag : 0;
}

const std::vector<EngineParamView>& engineParamsFor(int engineIndex) {
  static const std::vector<std::vector<EngineParamView>> cache = [] {
    const EngineRegistry& reg = engineRegistry();
    std::vector<std::vector<EngineParamView>> v(reg.size());
    for (std::size_t e = 0; e < reg.size(); ++e) {
      const EngineDescriptor& d = reg.at(e);
      for (const EngineParamDescriptor& p : d.parameters) {
        if (!p.exposed) continue;
        const EngineParamBinding* b = findBinding(d.info.id, p.key);
        if (b == nullptr) continue;  // integrity check reports
        const ParamMeta* m = findParamMeta(b->tag);
        if (m == nullptr) continue;
        v[e].push_back(EngineParamView{&p, b->tag, m});
      }
    }
    return v;
  }();
  if (engineIndex < 0 || engineIndex >= static_cast<int>(cache.size())) {
    static const std::vector<EngineParamView> kEmpty;
    return kEmpty;
  }
  return cache[static_cast<std::size_t>(engineIndex)];
}

std::vector<std::string> validateEngineParameterModel() {
  std::vector<std::string> issues;
  const EngineRegistry& reg = engineRegistry();

  // 1) every exposed descriptor has a binding, and the descriptor key set
  //    matches parameterKeys (the engine-contract key list)
  for (std::size_t e = 0; e < reg.size(); ++e) {
    const EngineDescriptor& d = reg.at(e);
    std::vector<std::string> declared;
    for (const EngineParamDescriptor& p : d.parameters) {
      declared.emplace_back(p.key);
      if (p.displayName == nullptr || p.displayName[0] == '\0') {
        issues.push_back(std::string(d.info.id) + "." + p.key +
                         ": empty displayName");
      }
      if (p.exposed) {
        if (findBinding(d.info.id, p.key) == nullptr) {
          issues.push_back(std::string(d.info.id) + "." + p.key +
                           ": exposed but has no stable VST binding");
        }
        if (!(p.max > p.min)) {
          issues.push_back(std::string(d.info.id) + "." + p.key +
                           ": exposed domain max <= min");
        }
        if (p.defaultPlain < p.min || p.defaultPlain > p.max) {
          issues.push_back(std::string(d.info.id) + "." + p.key +
                           ": default outside [min,max]");
        }
        if (p.stepCount > 0) {
          const int choices = p.choiceCount;
          if (choices > 0 && choices != p.stepCount + 1) {
            issues.push_back(std::string(d.info.id) + "." + p.key +
                             ": stepCount != choiceCount-1");
          }
        }
      } else if (p.kind == EngineParamKind::Text &&
                 (p.choiceNames == nullptr || p.choiceCount < 1)) {
        issues.push_back(std::string(d.info.id) + "." + p.key +
                         ": fixed Text parameter without choices");
      }
    }
    std::vector<std::string> keys;
    for (const char* k : d.parameterKeys) keys.emplace_back(k);
    std::sort(declared.begin(), declared.end());
    std::sort(keys.begin(), keys.end());
    if (declared != keys) {
      issues.push_back(std::string(d.info.id) +
                       ": descriptor key set != parameterKeys");
    }
  }

  // 2) every binding matches a declared, exposed parameter of a registered
  //    engine (no stale bindings)
  for (const EngineParamBinding& b : kEngineParamBindings) {
    const EngineDescriptor* d = reg.findById(b.engineId);
    if (d == nullptr) {
      issues.push_back(std::string("binding ") + b.engineId + "." + b.key +
                       ": engine not in registry");
      continue;
    }
    const EngineParamDescriptor* p = nullptr;
    for (const EngineParamDescriptor& cand : d->parameters) {
      if (std::strcmp(cand.key, b.key) == 0) p = &cand;
    }
    if (p == nullptr || !p->exposed) {
      issues.push_back(std::string("binding ") + b.engineId + "." + b.key +
                       ": no exposed descriptor matches");
      continue;
    }
    // the generated row must exist and carry the engine ownership
    const ParamMeta* m = findParamMeta(b.tag);
    if (m == nullptr) {
      issues.push_back(std::string("binding ") + b.engineId + "." + b.key +
                       ": no generated model row");
    } else if (m->role != ParamRole::EngineConfiguration ||
               std::strcmp(m->engineId, b.engineId) != 0) {
      issues.push_back(std::string("binding ") + b.engineId + "." + b.key +
                       ": generated row ownership mismatch");
    }
  }

  // 3) tag uniqueness across the whole table (shared + generated)
  {
    std::vector<uint32_t> tags;
    for (const ParamMeta& m : modelTable()) tags.push_back(m.tag);
    std::sort(tags.begin(), tags.end());
    for (std::size_t i = 1; i < tags.size(); ++i) {
      if (tags[i] == tags[i - 1]) {
        issues.push_back("duplicate parameter tag " + std::to_string(tags[i]));
      }
    }
  }
  return issues;
}

// ---------------------------------------------------------------------------
// Normalisation
// ---------------------------------------------------------------------------

double normalise(uint32_t tag, double plain) {
  for (const auto& m : modelTable()) {
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
  for (const auto& m : modelTable()) {
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
    case param::kVsQuality: snap.vsQuality = clampInt(p, 0, 2); break;
    case param::kVsAllowAliasing: snap.vsAllowAliasing = (p >= 0.5); break;
    case param::kVdExcursion: snap.vdExcursionSec = p; break;
    case param::kVdCrossfade: snap.vdCrossfadeFrames = static_cast<int64_t>(p); break;
    case param::kGrGrain: snap.grGrainSec = p; break;
    case param::kGrOverlap: snap.grOverlap = static_cast<int64_t>(p); break;
    case param::kGrJitter: snap.grJitterFrames = static_cast<int64_t>(p); break;
    case param::kGrWindow: snap.grWindowTriangular = (p >= 0.5) ? 1 : 0; break;
    case param::kPvcFft: snap.pvcFftSize = static_cast<int>(discreteIntValue(findParamMeta(param::kPvcFft), p)); break;
    case param::kPvcHop: snap.pvcHop = static_cast<int>(discreteIntValue(findParamMeta(param::kPvcHop), p)); break;
    case param::kPvpFft: snap.pvpFftSize = static_cast<int>(discreteIntValue(findParamMeta(param::kPvpFft), p)); break;
    case param::kPvpHop: snap.pvpHop = static_cast<int>(discreteIntValue(findParamMeta(param::kPvpHop), p)); break;
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
      const ParamMeta* m = findParamMeta(param::kPvcFft);
      if (m != nullptr) {
        for (int i = 0; i < m->choiceCount; ++i) {
          if (m->choiceValues[i] == static_cast<double>(snap.pvcFftSize)) return static_cast<double>(i);
        }
        return m->defaultPlain;
      }
      return 1.0;
    }
    case param::kPvcHop: {
      const ParamMeta* m = findParamMeta(param::kPvcHop);
      if (m != nullptr) {
        for (int i = 0; i < m->choiceCount; ++i) {
          if (m->choiceValues[i] == static_cast<double>(snap.pvcHop)) return static_cast<double>(i);
        }
        return m->defaultPlain;
      }
      return 2.0;
    }
    case param::kPvpFft: {
      const ParamMeta* m = findParamMeta(param::kPvpFft);
      if (m != nullptr) {
        for (int i = 0; i < m->choiceCount; ++i) {
          if (m->choiceValues[i] == static_cast<double>(snap.pvpFftSize)) return static_cast<double>(i);
        }
        return m->defaultPlain;
      }
      return 1.0;
    }
    case param::kPvpHop: {
      const ParamMeta* m = findParamMeta(param::kPvpHop);
      if (m != nullptr) {
        for (int i = 0; i < m->choiceCount; ++i) {
          if (m->choiceValues[i] == static_cast<double>(snap.pvpHop)) return static_cast<double>(i);
        }
        return m->defaultPlain;
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
//
// Discrete-choice display (Task 29): a parameter with declared choices shows
// the CHOICE TEXT (the engine-owned names — "reference", "hann", "2048"),
// never a bare index. Parsing accepts the choice text, the actual engine
// value (e.g. 2048) and the index.
// ---------------------------------------------------------------------------

namespace {

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

[[nodiscard]] bool isChoiceParam(const ParamMeta& m) {
  return m.choiceCount > 0 && m.choiceNames != nullptr && m.stepCount >= 0;
}

}  // namespace

void formatParamValue(uint32_t tag, double plain, char* buf, std::size_t bufSize) {
  if (buf == nullptr || bufSize == 0) return;
  buf[0] = '\0';
  // toggle parameters (no declared choices): the semantic on/off form
  switch (tag) {
    case param::kVsAllowAliasing:
    case param::kBypass:
      std::snprintf(buf, bufSize, "%s", plain >= 0.5 ? "on" : "off");
      return;
    default:
      break;
  }
  // discrete-choice parameters (engine-declared names): the choice text
  const ParamMeta* meta = findParamMeta(tag);
  if (meta != nullptr && isChoiceParam(*meta)) {
    const int idx = clampInt(plain, 0, meta->choiceCount - 1);
    std::snprintf(buf, bufSize, "%s", meta->choiceNames[idx]);
    return;
  }
  // integer-domain parameters without choices: plain integers
  switch (tag) {
    case param::kEngine:
    case param::kVdCrossfade:
    case param::kGrOverlap:
    case param::kGrJitter:
      std::snprintf(buf, bufSize, "%d", static_cast<int>(std::llround(plain)));
      return;
    default: {
      // real-valued parameters: the table's format (every remaining dispFmt
      // consumes a double — the type mismatch that was P1.8 is structurally
      // impossible now)
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

  // toggle parameters: on/off/true/false/1/0
  switch (tag) {
    case param::kVsAllowAliasing:
    case param::kBypass:
      if (equalsInsensitive(text, "on") || equalsInsensitive(text, "1") ||
          equalsInsensitive(text, "true")) {
        plainOut = 1.0;
        return true;
      }
      if (equalsInsensitive(text, "off") || equalsInsensitive(text, "0") ||
          equalsInsensitive(text, "false")) {
        plainOut = 0.0;
        return true;
      }
      return false;  // invalid toggle text: rejected, never silently zero
    default:
      break;
  }

  // Task 32: the LFO rate's canonical OFF text ("0.00" parses as a number
  // below; "off" is the semantic alias of the 0 Hz boundary). "on" is
  // deliberately NOT accepted — a rate has no single "on" value.
  if (tag == param::kLfoRate && equalsInsensitive(text, "off")) {
    plainOut = 0.0;
    return true;
  }

  // discrete-choice parameters (engine-declared): accept the choice text,
  // the actual engine value (choiceValues, e.g. 2048) and the index
  const ParamMeta* meta = findParamMeta(tag);
  if (meta != nullptr && meta->choiceCount > 0 && meta->choiceNames != nullptr) {
    for (int i = 0; i < meta->choiceCount; ++i) {
      if (equalsInsensitive(text, meta->choiceNames[i])) {
        plainOut = static_cast<double>(i);
        return true;
      }
    }
    double v = 0.0;
    if (!parseNumber(text, v)) return false;
    if (meta->choiceValues != nullptr) {
      for (int i = 0; i < meta->choiceCount; ++i) {
        if (v == meta->choiceValues[i]) {
          plainOut = static_cast<double>(i);
          return true;
        }
      }
    }
    plainOut = v;  // an index (validated by normalise's range clamp)
    return true;
  }

  switch (tag) {
    case param::kEngine: {
      // canonical: the registry index; also accept the engine id or display
      // name (case-insensitive) — identity comes from the registry, never a
      // second list
      double v = 0.0;
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
    default:
      // real/integer-valued parameters: strict numeric parse (the caller
      // strips whitespace-separated unit suffixes)
      double v = 0.0;
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
