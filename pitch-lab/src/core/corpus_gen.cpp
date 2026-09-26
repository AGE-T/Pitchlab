#include "core/corpus_gen.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include "core/errors.h"
#include "core/rng.h"
#include "core/wav_io.h"

namespace pitchlab {

namespace {

constexpr double kPi = 3.14159265358979323846;

// The -12 dBFS true-peak target (§11.2), computed once from the exact
// definition: 10^(-12/20).
const double kTargetPeakLin = 0.251188643150958;  // 10^(-12/20), double-rounded

[[nodiscard]] bool isSupportedRate(double rate) {
  const double supported[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};
  for (double s : supported) {
    if (rate == s) return true;
  }
  return false;
}

// --- per-class parameter validation + metadata ------------------------------
//
// Each class: REQUIRED param keys (with value checks), ALLOWED channels,
// §11.2 category, bandContentHz derivation. Unknown keys => ConfigError.

struct ClassInfo {
  const char* className;
  const char* category;
  int channels;  // exact channel count for the class (v0.1 instantiation)
};

const ClassInfo kClasses[] = {
    {"sine", "sine", 1},
    {"harmonic-stack", "harmonic", 1},
    {"polyphonic", "polyphonic", 1},
    {"impulse-train", "transient", 1},
    {"percussive-bursts", "transient", 1},
    {"noise-white", "noise", 1},
    {"noise-pink", "noise", 1},
    {"sweep-log", "sweep", 1},
    {"voice-like", "voice-like", 1},
    {"stereo-correlated-noise", "stereo", 2},
    {"stereo-decorrelated-noise", "stereo", 2},
    {"wideband", "wideband", 1},
};

const ClassInfo* findClass(const std::string& name) {
  for (const auto& c : kClasses) {
    if (name == c.className) return &c;
  }
  return nullptr;
}

[[nodiscard]] double requireDouble(const toml::TomlTable& params, const std::string& key,
                                   const std::string& file, bool positive) {
  if (params.count(key) == 0) {
    throw ConfigError(file, key, "params: required key '" + key + "' is missing");
  }
  const double v = params.at(key).asDouble(file, key);
  if (!std::isfinite(v)) {
    throw ConfigError(file, key, "params: value must be finite");
  }
  if (positive && v <= 0.0) {
    throw ConfigError(file, key, "params: value must be > 0");
  }
  return v;
}

[[nodiscard]] int requireInt(const toml::TomlTable& params, const std::string& key,
                             const std::string& file, int minValue) {
  if (params.count(key) == 0) {
    throw ConfigError(file, key, "params: required key '" + key + "' is missing");
  }
  const int64_t v = params.at(key).asInteger(file, key);
  if (v < minValue) {
    throw ConfigError(file, key, "params: value must be >= " + std::to_string(minValue));
  }
  return static_cast<int>(v);
}

[[nodiscard]] std::vector<double> requireDoubleArray(const toml::TomlTable& params,
                                                     const std::string& key,
                                                     const std::string& file, bool positive) {
  if (params.count(key) == 0) {
    throw ConfigError(file, key, "params: required key '" + key + "' is missing");
  }
  const auto& arr = params.at(key).asArray(file, key);
  if (arr.empty()) {
    throw ConfigError(file, key, "params: array must be non-empty");
  }
  std::vector<double> out;
  out.reserve(arr.size());
  for (const auto& v : arr) {
    const double d = v.asDouble(file, key);
    if (!std::isfinite(d) || (positive && d <= 0.0)) {
      throw ConfigError(file, key, "params: array values must be finite" +
                                       std::string(positive ? " and > 0" : ""));
    }
    out.push_back(d);
  }
  return out;
}

/// Direct read of an already-validated double array (validation happened in
/// validateItemParams; the asDouble type check remains as a safety net).
[[nodiscard]] std::vector<double> doubleArrayAt(const toml::TomlTable& params,
                                                const std::string& key,
                                                const std::string& file) {
  const auto& arr = params.at(key).asArray(file, key);
  std::vector<double> out;
  out.reserve(arr.size());
  for (const auto& v : arr) out.push_back(v.asDouble(file, key));
  return out;
}

void allowOnlyKeys(const toml::TomlTable& params, const std::set<std::string>& allowed,
                   const std::string& file) {
  for (const auto& [key, v] : params) {
    (void)v;
    if (allowed.count(key) == 0) {
      throw ConfigError(file, key, "params: unknown key for this class (typo protection)");
    }
  }
}

// --- synthesis helpers --------------------------------------------------------

/// Phase-accumulated sine (§11.2 "phase-accumulated sine (double), exact
/// frequency control"): phase in CYCLES, wrapped per frame via fmod (keeps
/// the sin argument small); returns the wrapped phase.
[[nodiscard]] double advancePhase(double& phase, double freqHz, double fs) {
  phase += freqHz / fs;
  phase = std::fmod(phase, 1.0);
  if (phase < 0.0) phase += 1.0;
  return phase;
}

/// One-pole low-pass coefficient (frozen): alpha = 1 - exp(-2*pi*fc/fs).
[[nodiscard]] double onePoleAlpha(double cutoffHz, double fs) {
  return 1.0 - std::exp(-2.0 * kPi * cutoffHz / fs);
}

/// Two-pole resonator (Rabiner-Schafer formant form, frozen): poles at
/// r*e^(+-j*theta), theta = 2*pi*F/fs, r = exp(-pi*F/(Q*fs)); unity gain at
/// resonance via G = (1-r)*sqrt(1-2r*cos(2*theta)+r^2) (the |denominator|
/// at z=e^j*theta). Deterministic recurrence, per-channel state owned here.
struct Resonator {
  double b0 = 0.0, b1 = 0.0, b2 = 0.0;  // y[n] = b0*x[n] + b1*y[n-1] + b2*y[n-2]
  double y1 = 0.0, y2 = 0.0;

  Resonator(double freqHz, double q, double fs) {
    const double theta = 2.0 * kPi * freqHz / fs;
    const double r = std::exp(-kPi * freqHz / (q * fs));
    b1 = 2.0 * r * std::cos(theta);
    b2 = -r * r;
    b0 = (1.0 - r) * std::sqrt(1.0 - 2.0 * r * std::cos(2.0 * theta) + r * r);
  }
  [[nodiscard]] double process(double x) {
    const double y = b0 * x + b1 * y1 + b2 * y2;
    y2 = y1;
    y1 = y;
    return y;
  }
};

/// Voss-McCartney pink noise state (§11.2 "fixed-layer filtered white";
/// frozen: 16 rows, row k updates when i mod 2^k == 0, drawn in ascending k).
struct PinkNoise {
  static constexpr std::size_t kRows = 16;
  std::vector<double> v = std::vector<double>(kRows, 0.0);  // NOT brace-init
                                                             // (initializer-list!)
  [[nodiscard]] double next(Pcg64& rng, int64_t i) {
    for (std::size_t k = 0; k < kRows; ++k) {
      if ((i & ((1LL << k) - 1)) == 0) {
        v[k] = rng.nextDouble01() * 2.0 - 1.0;
      }
    }
    double sum = 0.0;
    for (double x : v) sum += x;
    return sum / 4.0;
  }
};

/// Per-class parameter validation (REQUIRED keys, value checks, unknown-key
/// rejection). Called BOTH at config-parse time (config errors surface at
/// compile time, §4.4.4-style) and at generation time (stand-alone safety).
void validateItemParams(const CorpusItemSpec& spec) {
  const auto& p = spec.params;
  const std::string& file = spec.file;
  if (spec.className == "sine") {
    (void)requireDouble(p, "freqHz", file, true);
    allowOnlyKeys(p, {"freqHz"}, file);
  } else if (spec.className == "harmonic-stack") {
    (void)requireDouble(p, "freqHz", file, true);
    (void)requireInt(p, "partials", file, 1);
    (void)requireDouble(p, "vibratoHz", file, false);
    (void)requireDouble(p, "vibratoSt", file, false);
    allowOnlyKeys(p, {"freqHz", "partials", "vibratoHz", "vibratoSt"}, file);
  } else if (spec.className == "polyphonic") {
    (void)requireDoubleArray(p, "freqHz", file, true);
    (void)requireInt(p, "partials", file, 1);
    (void)requireDoubleArray(p, "stackHz", file, true);
    (void)requireInt(p, "stackPartials", file, 1);
    allowOnlyKeys(p, {"freqHz", "partials", "stackHz", "stackPartials"}, file);
  } else if (spec.className == "impulse-train") {
    (void)requireDouble(p, "spacingMs", file, true);
    allowOnlyKeys(p, {"spacingMs"}, file);
  } else if (spec.className == "percussive-bursts") {
    (void)requireDouble(p, "burstPeriodMs", file, true);
    (void)requireDouble(p, "burstMs", file, true);
    (void)requireDouble(p, "decayTauMs", file, true);
    (void)requireDouble(p, "cutoffHz", file, true);
    allowOnlyKeys(p, {"burstPeriodMs", "burstMs", "decayTauMs", "cutoffHz"}, file);
  } else if (spec.className == "noise-white" || spec.className == "noise-pink" ||
             spec.className == "stereo-correlated-noise") {
    allowOnlyKeys(p, {}, file);
  } else if (spec.className == "sweep-log") {
    const double f0 = requireDouble(p, "startHz", file, true);
    const double f1 = requireDouble(p, "endHz", file, true);
    allowOnlyKeys(p, {"startHz", "endHz"}, file);
    if (f1 <= f0) {
      throw ConfigError(file, "endHz", "params: endHz must be > startHz");
    }
    if (f1 >= spec.sampleRate / 2.0) {
      throw ConfigError(file, "endHz",
                        "params: endHz must stay below Nyquist (fs = " +
                            std::to_string(spec.sampleRate) + ")");
    }
  } else if (spec.className == "voice-like") {
    (void)requireDouble(p, "freqHz", file, true);
    (void)requireInt(p, "partials", file, 1);
    (void)requireDouble(p, "vibratoHz", file, false);
    (void)requireDouble(p, "vibratoSt", file, false);
    const auto formants = requireDoubleArray(p, "formantHz", file, true);
    (void)requireDouble(p, "formantQ", file, true);
    allowOnlyKeys(p, {"freqHz", "partials", "vibratoHz", "vibratoSt", "formantHz", "formantQ"},
                  file);
    if (formants.size() != 3) {
      throw ConfigError(file, "formantHz", "params: exactly three formants (§11.2)");
    }
    for (double fm : formants) {
      if (fm >= spec.sampleRate / 2.0) {
        throw ConfigError(file, "formantHz", "params: formant above Nyquist");
      }
    }
  } else if (spec.className == "stereo-decorrelated-noise") {
    (void)requireDouble(p, "envelopeHz", file, true);
    allowOnlyKeys(p, {"envelopeHz"}, file);
  } else if (spec.className == "wideband") {
    const auto tones = requireDoubleArray(p, "toneHz", file, true);
    const double loHz = requireDouble(p, "noiseLoHz", file, true);
    const double hiHz = requireDouble(p, "noiseHiHz", file, true);
    allowOnlyKeys(p, {"toneHz", "noiseLoHz", "noiseHiHz"}, file);
    if (loHz >= hiHz) {
      throw ConfigError(file, "noiseLoHz", "params: noiseLoHz must be < noiseHiHz");
    }
    if (hiHz >= spec.sampleRate / 2.0) {
      throw ConfigError(file, "noiseHiHz",
                        "params: noiseHiHz must stay below Nyquist (fs = " +
                            std::to_string(spec.sampleRate) + ")");
    }
    for (double f : tones) {
      if (f >= spec.sampleRate / 2.0) {
        throw ConfigError(file, "toneHz", "params: tone above Nyquist");
      }
    }
  }
}

/// Fill a mono channel with the class recipe (deterministic; stochastic
/// classes use the provided stream). `frames` >= 1.
void synthesizeMono(const CorpusItemSpec& spec, double fs, int64_t frames, double* x,
                    Pcg64& rng) {
  const auto& p = spec.params;
  const std::string& file = spec.file;

  if (spec.className == "sine") {
    const double f = p.at("freqHz").asDouble(file, "freqHz");
    double phase = 0.0;
    for (int64_t i = 0; i < frames; ++i) {
      const double ph = advancePhase(phase, f, fs);
      x[i] = std::sin(2.0 * kPi * ph);
    }
  } else if (spec.className == "harmonic-stack") {
    const double f = p.at("freqHz").asDouble(file, "freqHz");
    const int partials = static_cast<int>(p.at("partials").asInteger(file, "partials"));
    const double vibHz = p.at("vibratoHz").asDouble(file, "vibratoHz");
    const double vibSt = p.at("vibratoSt").asDouble(file, "vibratoSt");
    double phase = 0.0;
    for (int64_t i = 0; i < frames; ++i) {
      // vibrato modulates the fundamental frequency (st domain), the phase
      // accumulates the instantaneous frequency (frozen recipe).
      const double t = static_cast<double>(i) / fs;
      const double inst = f * std::exp2(vibSt * std::sin(2.0 * kPi * vibHz * t) / 12.0);
      const double ph = advancePhase(phase, inst, fs);
      double acc = 0.0;
      for (int k = 1; k <= partials; ++k) {
        acc += std::sin(2.0 * kPi * static_cast<double>(k) * ph) / static_cast<double>(k);
      }
      x[i] = acc;
    }
  } else if (spec.className == "polyphonic") {
    const auto freqs = doubleArrayAt(p, "freqHz", file);
    const int partials = static_cast<int>(p.at("partials").asInteger(file, "partials"));
    const auto stack = doubleArrayAt(p, "stackHz", file);
    const int stackPartials = static_cast<int>(p.at("stackPartials").asInteger(file, "stackPartials"));
    std::vector<double> phases(freqs.size() + stack.size(), 0.0);
    for (int64_t i = 0; i < frames; ++i) {
      double acc = 0.0;
      std::size_t idx = 0;
      for (std::size_t j = 0; j < freqs.size(); ++j, ++idx) {
        const double ph = advancePhase(phases[idx], freqs[j], fs);
        for (int k = 1; k <= partials; ++k) {
          acc += std::sin(2.0 * kPi * static_cast<double>(k) * ph) / static_cast<double>(k);
        }
      }
      for (std::size_t j = 0; j < stack.size(); ++j, ++idx) {
        const double ph = advancePhase(phases[idx], stack[j], fs);
        for (int k = 1; k <= stackPartials; ++k) {
          acc += std::sin(2.0 * kPi * static_cast<double>(k) * ph) / static_cast<double>(k);
        }
      }
      x[i] = acc;
    }
  } else if (spec.className == "impulse-train") {
    const double spacingMs = p.at("spacingMs").asDouble(file, "spacingMs");
    double next = 0.0;
    const double spacingFrames = fs * spacingMs / 1000.0;
    for (int64_t i = 0; i < frames; ++i) {
      if (static_cast<double>(i) >= next - 1e-12) {
        x[i] = 1.0;
        next += spacingFrames;
      } else {
        x[i] = 0.0;
      }
    }
  } else if (spec.className == "percussive-bursts") {
    const double periodMs = p.at("burstPeriodMs").asDouble(file, "burstPeriodMs");
    const double burstMs = p.at("burstMs").asDouble(file, "burstMs");
    const double tauMs = p.at("decayTauMs").asDouble(file, "decayTauMs");
    const double cutoffHz = p.at("cutoffHz").asDouble(file, "cutoffHz");
    const double periodFrames = fs * periodMs / 1000.0;
    const double burstFrames = fs * burstMs / 1000.0;
    const double tauFrames = fs * tauMs / 1000.0;
    const double alpha = onePoleAlpha(cutoffHz, fs);
    double lp = 0.0;
    double nextBurst = 0.0;
    int64_t burstStart = -1;
    for (int64_t i = 0; i < frames; ++i) {
      if (static_cast<double>(i) >= nextBurst - 1e-12) {
        burstStart = i;
        nextBurst += periodFrames;
      }
      double raw = 0.0;
      if (burstStart >= 0) {
        const double since = static_cast<double>(i - burstStart);
        if (since < burstFrames) {
          const double u = rng.nextDouble01() * 2.0 - 1.0;
          raw = u * std::exp(-since / tauFrames);
        }
      }
      lp += alpha * (raw - lp);
      x[i] = lp;
    }
  } else if (spec.className == "noise-white") {
    for (int64_t i = 0; i < frames; ++i) {
      x[i] = rng.nextDouble01() * 2.0 - 1.0;
    }
  } else if (spec.className == "noise-pink") {
    PinkNoise pink;
    for (int64_t i = 0; i < frames; ++i) {
      x[i] = pink.next(rng, i);
    }
  } else if (spec.className == "sweep-log") {
    const double f0 = p.at("startHz").asDouble(file, "startHz");
    const double f1 = p.at("endHz").asDouble(file, "endHz");
    if (f1 <= f0) {
      throw ConfigError(file, "endHz", "params: endHz must be > startHz");
    }
    if (f1 >= fs / 2.0) {
      throw ConfigError(file, "endHz", "params: endHz must stay below Nyquist");
    }
    // analytic phase integral of f(t) = f0*(f1/f0)^(t/T):
    //   phi(t) = (f0*T/ln(R)) * (R^(t/T) - 1), R = f1/f0
    const double T = spec.durationSec;
    const double lnR = std::log(f1 / f0);
    const double c = f0 * T / lnR;
    for (int64_t i = 0; i < frames; ++i) {
      const double t = static_cast<double>(i) / fs;
      const double phi = c * (std::pow(f1 / f0, t / T) - 1.0);
      x[i] = std::sin(2.0 * kPi * phi);
    }
  } else if (spec.className == "voice-like") {
    const double f = p.at("freqHz").asDouble(file, "freqHz");
    const int partials = static_cast<int>(p.at("partials").asInteger(file, "partials"));
    const double vibHz = p.at("vibratoHz").asDouble(file, "vibratoHz");
    const double vibSt = p.at("vibratoSt").asDouble(file, "vibratoSt");
    const auto formants = doubleArrayAt(p, "formantHz", file);
    const double q = p.at("formantQ").asDouble(file, "formantQ");
    if (formants.size() != 3) {
      throw ConfigError(file, "formantHz", "params: exactly three formants (§11.2)");
    }
    std::vector<Resonator> res;
    res.reserve(3);
    for (double fm : formants) {
      if (fm >= fs / 2.0) {
        throw ConfigError(file, "formantHz", "params: formant above Nyquist");
      }
      res.emplace_back(fm, q, fs);
    }
    double phase = 0.0;
    for (int64_t i = 0; i < frames; ++i) {
      const double t = static_cast<double>(i) / fs;
      const double inst = f * std::exp2(vibSt * std::sin(2.0 * kPi * vibHz * t) / 12.0);
      const double ph = advancePhase(phase, inst, fs);
      double exc = 0.0;
      for (int k = 1; k <= partials; ++k) {
        exc += std::sin(2.0 * kPi * static_cast<double>(k) * ph) / static_cast<double>(k);
      }
      // three PARALLEL 2-pole resonators summed (formant synthesis shape).
      double acc = 0.0;
      for (auto& r : res) acc += r.process(exc);
      x[i] = acc;
    }
  } else if (spec.className == "wideband") {
    const auto tones = doubleArrayAt(p, "toneHz", file);
    const double loHz = p.at("noiseLoHz").asDouble(file, "noiseLoHz");
    const double hiHz = p.at("noiseHiHz").asDouble(file, "noiseHiHz");
    if (loHz >= hiHz) {
      throw ConfigError(file, "noiseLoHz", "params: noiseLoHz must be < noiseHiHz");
    }
    if (hiHz >= fs / 2.0) {
      throw ConfigError(file, "noiseHiHz", "params: noiseHiHz must stay below Nyquist");
    }
    for (double f : tones) {
      if (f >= fs / 2.0) {
        throw ConfigError(file, "toneHz", "params: tone above Nyquist");
      }
    }
    // one-pole HP at loHz (x - LP(x)) followed by one-pole LP at hiHz.
    const double alphaLo = onePoleAlpha(loHz, fs);
    const double alphaHi = onePoleAlpha(hiHz, fs);
    double lpLo = 0.0, lpHi = 0.0;
    std::vector<double> phases(tones.size(), 0.0);
    for (int64_t i = 0; i < frames; ++i) {
      double acc = 0.0;
      for (std::size_t j = 0; j < tones.size(); ++j) {
        const double ph = advancePhase(phases[j], tones[j], fs);
        acc += std::sin(2.0 * kPi * ph);
      }
      const double n = rng.nextDouble01() * 2.0 - 1.0;
      lpLo += alphaLo * (n - lpLo);
      const double hp = n - lpLo;  // one-pole HP = x - LP(x)
      lpHi += alphaHi * (hp - lpHi);  // then band-limit with the hi LP
      x[i] = acc + lpHi;
    }
  } else {
    throw ConfigError(spec.file, "class", "internal error: unhandled class '" +
                                              spec.className + "'");
  }
}

[[nodiscard]] std::vector<double> bandContentFor(const CorpusItemSpec& spec, double fs) {
  const auto& p = spec.params;
  auto band = [](double lo, double hi) { return std::vector<double>{lo, hi}; };
  if (spec.className == "sine") {
    const double f = p.at("freqHz").asDouble(spec.file, "freqHz");
    return band(f, f);
  }
  if (spec.className == "harmonic-stack") {
    const double f = p.at("freqHz").asDouble(spec.file, "freqHz");
    const double partials = static_cast<double>(
        p.at("partials").asInteger(spec.file, "partials"));
    return band(f, f * partials);
  }
  if (spec.className == "polyphonic") {
    const auto freqs = doubleArrayAt(p, "freqHz", spec.file);
    const auto stack = doubleArrayAt(p, "stackHz", spec.file);
    const double partials = static_cast<double>(
        p.at("partials").asInteger(spec.file, "partials"));
    const double stackPartials = static_cast<double>(
        p.at("stackPartials").asInteger(spec.file, "stackPartials"));
    double lo = 1e300, hi = 0.0;
    for (double f : freqs) {
      lo = std::min(lo, f);
      hi = std::max(hi, f * partials);
    }
    for (double f : stack) {
      lo = std::min(lo, f);
      hi = std::max(hi, f * stackPartials);
    }
    return band(lo, hi);
  }
  if (spec.className == "impulse-train") {
    const double spacingHz = 1000.0 / p.at("spacingMs").asDouble(spec.file, "spacingMs");
    return band(spacingHz, fs / 2.0);
  }
  if (spec.className == "percussive-bursts") {
    return band(1.0, p.at("cutoffHz").asDouble(spec.file, "cutoffHz"));
  }
  if (spec.className == "noise-white" || spec.className == "noise-pink" ||
      spec.className == "stereo-correlated-noise" ||
      spec.className == "stereo-decorrelated-noise") {
    return band(1.0, fs / 2.0);
  }
  if (spec.className == "sweep-log") {
    return band(p.at("startHz").asDouble(spec.file, "startHz"),
                p.at("endHz").asDouble(spec.file, "endHz"));
  }
  if (spec.className == "voice-like") {
    const double f = p.at("freqHz").asDouble(spec.file, "freqHz");
    const double partials =
        static_cast<double>(p.at("partials").asInteger(spec.file, "partials"));
    const auto formants = doubleArrayAt(p, "formantHz", spec.file);
    double hi = f * partials;
    for (double fm : formants) hi = std::max(hi, fm + 500.0);
    return band(f, hi);
  }
  if (spec.className == "wideband") {
    const auto tones = doubleArrayAt(p, "toneHz", spec.file);
    const double hiHz = p.at("noiseHiHz").asDouble(spec.file, "noiseHiHz");
    double lo = 1e300, hi = 0.0;
    for (double f : tones) {
      lo = std::min(lo, f);
      hi = std::max(hi, f);
    }
    hi = std::max(hi, hiHz);
    return band(lo, hi);
  }
  return band(1.0, fs / 2.0);
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

uint64_t corpusItemSeed(uint64_t masterSeed, const std::string& itemId) {
  return splitMix64(masterSeed ^ fnv1a64(itemId.c_str()));
}

CorpusConfig parseCorpusConfig(const std::filesystem::path& tomlFile) {
  const auto t = toml::parseTomlFile(tomlFile);
  CorpusConfig cfg;
  cfg.file = tomlFile.string();

  if (t.count("version") == 0) {
    throw ConfigError(cfg.file, "version", "version is required");
  }
  cfg.version = t.at("version").asString(cfg.file, "version");
  if (cfg.version != "v0.1") {
    throw ConfigError(cfg.file, "version", "unsupported corpus config version '" +
                                               cfg.version + "' (expected \"v0.1\")");
  }
  if (t.count("masterSeed") == 0) {
    throw ConfigError(cfg.file, "masterSeed", "masterSeed is required (§11.4)");
  }
  const int64_t seed = t.at("masterSeed").asInteger(cfg.file, "masterSeed");
  if (seed < 0) {
    throw ConfigError(cfg.file, "masterSeed", "masterSeed must be >= 0");
  }
  cfg.masterSeed = static_cast<uint64_t>(seed);

  if (t.count("item") == 0) {
    throw ConfigError(cfg.file, "item", "at least one [[item]] is required");
  }
  const auto& items = t.at("item").asArray(cfg.file, "item");
  std::set<std::string> seenIds;
  for (const auto& itemValue : items) {
    const auto& tbl = itemValue.asTable(cfg.file, "item");
    CorpusItemSpec item;
    item.file = cfg.file;
    if (tbl.count("id") == 0) {
      throw ConfigError(cfg.file, "id", "[[item]]: id is required");
    }
    item.id = tbl.at("id").asString(cfg.file, "id");
    if (item.id.empty()) {
      throw ConfigError(cfg.file, "id", "[[item]]: id must be non-empty");
    }
    if (seenIds.count(item.id) != 0) {
      throw ConfigError(cfg.file, "id", "duplicate item id '" + item.id + "'");
    }
    seenIds.insert(item.id);

    if (tbl.count("class") == 0) {
      throw ConfigError(cfg.file, "class", "[[item]] '" + item.id + "': class is required");
    }
    item.className = tbl.at("class").asString(cfg.file, "class");
    const ClassInfo* info = findClass(item.className);
    if (info == nullptr) {
      throw ConfigError(cfg.file, "class", "[[item]] '" + item.id + "': unknown class '" +
                                             item.className + "'");
    }
    item.category = info->category;

    if (tbl.count("sampleRate") == 0) {
      throw ConfigError(cfg.file, "sampleRate",
                        "[[item]] '" + item.id + "': sampleRate is required");
    }
    item.sampleRate = tbl.at("sampleRate").asDouble(cfg.file, "sampleRate");
    if (!isSupportedRate(item.sampleRate)) {
      throw ConfigError(cfg.file, "sampleRate",
                        "[[item]] '" + item.id + "': sample rate must be in the §8 supported "
                                                 "set {44100, 48000, 88200, 96000, 176400, "
                                                 "192000}");
    }

    if (tbl.count("durationSec") == 0) {
      throw ConfigError(cfg.file, "durationSec",
                        "[[item]] '" + item.id + "': durationSec is required");
    }
    item.durationSec = tbl.at("durationSec").asDouble(cfg.file, "durationSec");
    if (!std::isfinite(item.durationSec) || item.durationSec <= 0.0) {
      throw ConfigError(cfg.file, "durationSec",
                        "[[item]] '" + item.id + "': durationSec must be finite and > 0");
    }

    if (tbl.count("channels") == 0) {
      throw ConfigError(cfg.file, "channels",
                        "[[item]] '" + item.id + "': channels is required");
    }
    const int64_t ch = tbl.at("channels").asInteger(cfg.file, "channels");
    if (ch != info->channels) {
      throw ConfigError(cfg.file, "channels",
                        "[[item]] '" + item.id + "': class '" + item.className +
                            "' requires channels = " + std::to_string(info->channels) +
                            " (v0.1 instantiation of 'mono and stereo variants where "
                            "meaningful')");
    }
    item.channels = static_cast<int>(ch);

    if (tbl.count("params") == 0) {
      throw ConfigError(cfg.file, "params", "[[item]] '" + item.id + "': params table is required");
    }
    item.params = tbl.at("params").asTable(cfg.file, "params");

    for (const auto& [key, v] : tbl) {
      (void)v;
      if (key != "id" && key != "class" && key != "sampleRate" && key != "durationSec" &&
          key != "channels" && key != "params") {
        throw ConfigError(cfg.file, key, "[[item]] '" + item.id + "': unknown item key");
      }
    }
    validateItemParams(item);  // config errors surface at PARSE time (§K)
    cfg.items.push_back(std::move(item));
  }
  return cfg;
}

GeneratedCorpusItem generateCorpusItem(const CorpusItemSpec& spec, uint64_t itemSeed) {
  if (!std::isfinite(spec.sampleRate) || spec.sampleRate <= 0.0) {
    throw ConfigError(spec.file, "sampleRate", "invalid sample rate");
  }
  const int64_t frames = static_cast<int64_t>(std::llround(spec.durationSec * spec.sampleRate));
  if (frames <= 0) {
    throw ConfigError(spec.file, "durationSec", "duration rounds to zero frames");
  }
  validateItemParams(spec);
  GeneratedCorpusItem out;
  out.spec = spec;
  out.itemSeed = itemSeed;
  out.channels.assign(static_cast<std::size_t>(spec.channels),
                      std::vector<double>(static_cast<std::size_t>(frames), 0.0));
  const double fs = spec.sampleRate;

  if (spec.className == "stereo-correlated-noise") {
    auto rng = makeConsumerStream(itemSeed, "corpus");
    auto& x = out.channels[0];
    for (int64_t i = 0; i < frames; ++i) {
      x[static_cast<std::size_t>(i)] = rng.nextDouble01() * 2.0 - 1.0;
    }
    out.channels[1] = x;  // identical channels = "correlated" (§11.2)
  } else if (spec.className == "stereo-decorrelated-noise") {
    const double envHz = spec.params.at("envelopeHz").asDouble(spec.id, "envelopeHz");
    auto rngL = makeConsumerStream(itemSeed, "corpus.L");
    auto rngR = makeConsumerStream(itemSeed, "corpus.R");
    for (int64_t i = 0; i < frames; ++i) {
      const double t = static_cast<double>(i) / fs;
      const double env = 0.55 + 0.45 * std::sin(2.0 * kPi * envHz * t);
      out.channels[0][static_cast<std::size_t>(i)] = env * (rngL.nextDouble01() * 2.0 - 1.0);
      out.channels[1][static_cast<std::size_t>(i)] = env * (rngR.nextDouble01() * 2.0 - 1.0);
    }
  } else {
    auto rng = makeConsumerStream(itemSeed, "corpus");
    synthesizeMono(spec, fs, frames, out.channels[0].data(), rng);
  }

  // Exact true-peak normalisation (§11.2): max |sample| over ALL channels,
  // shared scaling (deterministic, computed from the exact signal).
  double peak = 0.0;
  for (const auto& ch : out.channels) {
    for (double v : ch) {
      peak = std::max(peak, std::fabs(v));
    }
  }
  if (!(peak > 0.0)) {
    throw ConfigError(spec.file, "class", "recipe produced an all-zero signal");
  }
  out.peakLinear = peak;
  const double scale = kTargetPeakLin / peak;
  for (auto& ch : out.channels) {
    for (double& v : ch) {
      v *= scale;
    }
  }
  out.bandContentHz = bandContentFor(spec, fs);
  out.sourceDescription =
      "generated by tools/corpus_gen v0.1, seed " + std::to_string(itemSeed);
  return out;
}

std::string renderCorpusMetadata(const GeneratedCorpusItem& item) {
  std::ostringstream os;
  os << "# Pitch Lab corpus asset metadata (schema §15.5; §11).\n"
     << "# Generated deterministically by tools/corpus_gen — DO NOT EDIT:\n"
     << "# regenerate with the tool (T-C1 verifies bit-identity).\n";
  os << "id = \"" << item.spec.id << "\"\n";
  os << "category = \"" << item.spec.category << "\"\n";
  os << "sampleRate = " << static_cast<int64_t>(item.spec.sampleRate) << "\n";
  os << "channels = " << item.spec.channels << "\n";
  os << "durationSec = " << item.spec.durationSec << "\n";
  os << "sourceDescription = \"" << item.sourceDescription << "\"\n";
  os << "referenceStatus = \"test-fixture\"\n";
  os << "bandContentHz = [" << item.bandContentHz[0] << ", " << item.bandContentHz[1]
     << "]\n";
  return os.str();
}

std::vector<GeneratedCorpusItem> generateCorpus(const CorpusConfig& config) {
  std::vector<GeneratedCorpusItem> out;
  out.reserve(config.items.size());
  for (const auto& item : config.items) {
    out.push_back(generateCorpusItem(item, corpusItemSeed(config.masterSeed, item.id)));
  }
  return out;
}

void writeCorpusItem(const std::filesystem::path& outDir, const GeneratedCorpusItem& item) {
  const auto dir = outDir / item.spec.id;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    throw ConfigError(dir.string(), "", "cannot create corpus item directory: " + ec.message());
  }
  std::vector<const double*> channelPtrs;
  channelPtrs.reserve(item.channels.size());
  for (const auto& ch : item.channels) channelPtrs.push_back(ch.data());
  writeWav(dir / "signal.wav", channelPtrs.data(),
           static_cast<ChannelCount>(item.spec.channels),
           static_cast<FrameCount>(item.channels[0].size()),
           static_cast<uint32_t>(item.spec.sampleRate), WavSampleFormat::Float64);
  { std::ofstream meta(dir / "metadata.toml", std::ios::binary); meta << renderCorpusMetadata(item); }
}

}  // namespace pitchlab
