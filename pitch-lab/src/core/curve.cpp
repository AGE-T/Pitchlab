#include "core/curve.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include "core/rng.h"

namespace pitchlab {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Value/time authoring tables (§4.4.3.1 item 2)
// ---------------------------------------------------------------------------

[[nodiscard]] CurveValue parseValueTable(const toml::TomlValue& v, const std::string& file,
                                         const std::string& field) {
  const auto& t = v.asTable(file, field);
  const bool hasSt = t.count("semitones") != 0;
  const bool hasRatio = t.count("ratio") != 0;
  if (hasSt == hasRatio) {
    throw ConfigError(file, field,
                      "value table must be exactly one of { semitones = x } or { ratio = y }");
  }
  for (const auto& [key, kv] : t) {
    (void)kv;
    if (key != "semitones" && key != "ratio") {
      throw ConfigError(file, key, "unknown key in value table (allowed: semitones, ratio)");
    }
  }
  CurveValue out;
  if (hasSt) {
    out.isRatio = false;
    out.semitones = t.at("semitones").asDouble(file, field);
    if (!std::isfinite(out.semitones)) {
      throw ConfigError(file, field, "semitones must be finite");
    }
  } else {
    out.isRatio = true;
    out.ratio = t.at("ratio").asDouble(file, field);
    if (!std::isfinite(out.ratio) || out.ratio <= 0.0) {
      throw ConfigError(file, field, "ratio must be finite and strictly positive");
    }
  }
  return out;
}

/// { ms = x } XOR { s = y } -> seconds (finite, > 0; §4.4.3.1 item 2).
[[nodiscard]] double parseTimeTable(const toml::TomlValue& v, const std::string& file,
                                    const std::string& field) {
  const auto& t = v.asTable(file, field);
  const bool hasMs = t.count("ms") != 0;
  const bool hasS = t.count("s") != 0;
  if (hasMs == hasS) {
    throw ConfigError(file, field, "time table must be exactly one of { ms = x } or { s = y }");
  }
  for (const auto& [key, kv] : t) {
    (void)kv;
    if (key != "ms" && key != "s") {
      throw ConfigError(file, key, "unknown key in time table (allowed: ms, s)");
    }
  }
  double seconds;
  if (hasMs) {
    seconds = t.at("ms").asDouble(file, field) / 1000.0;
  } else {
    seconds = t.at("s").asDouble(file, field);
  }
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    throw ConfigError(file, field, "time must be finite and > 0");
  }
  return seconds;
}

/// Resolve a value authored as ratio into the semitone domain (log2
/// scaling); semitones-authored values pass through. For centre/min/max.
[[nodiscard]] double valueToSemitones(const CurveValue& v, const std::string& file,
                                      const std::string& field) {
  if (!v.isRatio) {
    return v.semitones;  // finiteness validated at parse
  }
  if (!std::isfinite(v.ratio) || v.ratio <= 0.0) {
    throw ConfigError(file, field, "ratio must be finite and strictly positive");
  }
  return std::log2(v.ratio) * 12.0;
}

[[nodiscard]] double frac01(double x) {
  double f = std::fmod(x, 1.0);
  if (f < 0.0) f += 1.0;
  return f;
}

[[nodiscard]] int64_t secondsToFrames(double seconds, double sampleRate) {
  return static_cast<int64_t>(std::llround(seconds * sampleRate));
}

// ---------------------------------------------------------------------------
// Spec parsing / validation (§4.4.3.1 items 1-10 + §4.4.4)
// ---------------------------------------------------------------------------

const std::set<std::string>& allKnownKeys() {
  static const std::set<std::string> keys = {
      "id",     "kind",       "value",  "from",    "to",           "time",
      "hold",   "rate",       "wave",   "depth",   "centre",       "min",
      "max",    "seed",       "allow_discontinuity", "extend", "points",
      "source", "mode"};
  return keys;
}

/// Parse the spec from a (already TOML-parsed) table. `file` for errors.
[[nodiscard]] PitchCurveSpec specFromTable(const toml::TomlTable& t,
                                           const std::filesystem::path& path) {
  const std::string file = path.string();
  PitchCurveSpec spec;
  spec.file = file;

  // --- globally known keys (typo protection) ---
  std::set<std::string> present;
  for (const auto& [key, v] : t) {
    (void)v;
    if (allKnownKeys().count(key) == 0) {
      throw ConfigError(file, key, "unknown curve spec key");
    }
    present.insert(key);
  }

  // --- common required ---
  if (present.count("id") == 0) throw ConfigError(file, "id", "id is required");
  spec.id = t.at("id").asString(file, "id");
  if (spec.id.empty()) throw ConfigError(file, "id", "id must be a non-empty string");

  if (present.count("kind") == 0) throw ConfigError(file, "kind", "kind is required");
  spec.kind = t.at("kind").asString(file, "kind");
  const std::set<std::string> kKinds = {"static",      "ramp_lin", "ramp_exp", "lfo",
                                        "random",      "reversal", "breakpoints",
                                        "external"};
  if (kKinds.count(spec.kind) == 0) {
    throw ConfigError(file, "kind", "unknown kind '" + spec.kind + "'");
  }

  // --- common optional ---
  if (present.count("extend") != 0) {
    const std::string extend = t.at("extend").asString(file, "extend");
    if (extend == "hold-last") {
      spec.extendHoldLast = true;
    } else if (extend == "hold-first") {
      spec.extendHoldLast = false;
    } else {
      throw ConfigError(file, "extend", "extend must be \"hold-last\" or \"hold-first\"");
    }
  }
  if (present.count("allow_discontinuity") != 0) {
    spec.allowDiscontinuity = t.at("allow_discontinuity").asBoolean(file, "allow_discontinuity");
  }
  if (present.count("seed") != 0) {
    const int64_t seed = t.at("seed").asInteger(file, "seed");
    if (seed < 0) throw ConfigError(file, "seed", "seed must be an integer >= 0");
    spec.seed = static_cast<uint64_t>(seed);
    spec.hasSeed = true;
  }

  /// Helper: field-set legality per kind (required + optional-allowed).
  struct KindFields {
    std::set<std::string> required;
    std::set<std::string> allowed;
  };
  auto checkFields = [&](const KindFields& kf, const std::string& kindName) {
    for (const auto& key : present) {
      if (key == "id" || key == "kind" || key == "extend" || key == "allow_discontinuity") {
        continue;  // common optional everywhere
      }
      if (kf.required.count(key) == 0 && kf.allowed.count(key) == 0) {
        throw ConfigError(file, key, "field is not valid for kind '" + kindName + "'");
      }
    }
    for (const auto& key : kf.required) {
      if (present.count(key) == 0) {
        throw ConfigError(file, key, "field is required for kind '" + kindName + "'");
      }
    }
  };

  if (spec.kind == "static") {
    checkFields({{"value"}, {}}, "static");
    spec.value = parseValueTable(t.at("value"), file, "value");
  } else if (spec.kind == "ramp_lin" || spec.kind == "reversal") {
    checkFields({{"from", "to", "time"}, {"hold"}}, spec.kind);
    spec.from = parseValueTable(t.at("from"), file, "from");
    spec.to = parseValueTable(t.at("to"), file, "to");
    spec.timeSec = parseTimeTable(t.at("time"), file, "time");
    if (present.count("hold") != 0) {
      spec.holdSec = parseTimeTable(t.at("hold"), file, "hold");
      spec.hasHold = true;
    }
  } else if (spec.kind == "ramp_exp") {
    // EITHER endpoint form (from,to,time[,hold]) OR rate form (from,rate,time[,hold]).
    const bool hasTo = present.count("to") != 0;
    const bool hasRate = present.count("rate") != 0;
    if (hasTo == hasRate) {
      throw ConfigError(file, "to",
                        "ramp_exp requires exactly one parameterization: { from, to, time } "
                        "or { from, rate = { stps }, time }");
    }
    checkFields({{"from", "time"}, {"hold", "to", "rate"}}, "ramp_exp");
    spec.from = parseValueTable(t.at("from"), file, "from");
    spec.timeSec = parseTimeTable(t.at("time"), file, "time");
    if (hasTo) {
      spec.to = parseValueTable(t.at("to"), file, "to");
    } else {
      spec.hasRateForm = true;
      const auto& rt = t.at("rate").asTable(file, "rate");
      if (rt.count("stps") == 0) {
        throw ConfigError(file, "rate", "rate table must be { stps = <semitones/second> }");
      }
      for (const auto& [key, kv] : rt) {
        (void)kv;
        if (key != "stps") {
          throw ConfigError(file, key, "unknown key in rate table (allowed: stps)");
        }
      }
      spec.stps = rt.at("stps").asDouble(file, "rate");
      if (!std::isfinite(spec.stps)) {
        throw ConfigError(file, "rate", "stps must be finite");
      }
    }
    if (present.count("hold") != 0) {
      spec.holdSec = parseTimeTable(t.at("hold"), file, "hold");
      spec.hasHold = true;
    }
  } else if (spec.kind == "lfo") {
    const bool isRandomWalk = present.count("wave") != 0 &&
                              t.at("wave").is(toml::TomlValue::Kind::String) &&
                              t.at("wave").asString(file, "wave") == "random-walk";
    std::set<std::string> required = {"wave", "rate", "depth"};
    std::set<std::string> allowed = {"wave", "rate", "depth", "centre"};
    if (isRandomWalk) {
      required.insert({"min", "max", "seed"});
      allowed.insert({"min", "max", "seed"});
    }
    checkFields({required, allowed}, "lfo");
    spec.wave = t.at("wave").asString(file, "wave");
    if (spec.wave != "sine" && spec.wave != "triangle" && spec.wave != "saw" &&
        spec.wave != "random-walk") {
      throw ConfigError(file, "wave",
                        "wave must be sine|triangle|saw|random-walk; got '" + spec.wave + "'");
    }
    spec.rateHz = t.at("rate").asDouble(file, "rate");
    spec.hasRateHz = true;
    if (!std::isfinite(spec.rateHz) || spec.rateHz <= 0.0) {
      throw ConfigError(file, "rate", "rate must be finite and > 0");
    }
    spec.depth = parseValueTable(t.at("depth"), file, "depth");
    if (spec.depth.isRatio) {
      throw ConfigError(file, "depth", "depth must be authored in semitones ({ semitones = x })");
    }
    if (present.count("centre") != 0) {
      spec.centre = parseValueTable(t.at("centre"), file, "centre");
    } else {
      spec.centre = CurveValue{0.0, 1.0, true};  // default: identity
    }
    if (isRandomWalk) {
      spec.minValue = parseValueTable(t.at("min"), file, "min");
      spec.maxValue = parseValueTable(t.at("max"), file, "max");
      if (present.count("seed") == 0) {
        throw ConfigError(file, "seed", "seed is required for stochastic curves");
      }
    }
    if (spec.wave == "saw" && !spec.allowDiscontinuity) {
      throw ConfigError(file, "allow_discontinuity",
                        "wave 'saw' has a per-cycle value discontinuity and requires "
                        "allow_discontinuity = true (§4.4.4)");
    }
  } else if (spec.kind == "random") {
    checkFields({{"rate", "depth", "centre", "min", "max", "seed"}, {}}, "random");
    spec.rateHz = t.at("rate").asDouble(file, "rate");
    spec.hasRateHz = true;
    if (!std::isfinite(spec.rateHz) || spec.rateHz <= 0.0) {
      throw ConfigError(file, "rate", "rate (steps/second) must be finite and > 0");
    }
    spec.depth = parseValueTable(t.at("depth"), file, "depth");
    if (spec.depth.isRatio) {
      throw ConfigError(file, "depth", "depth must be authored in semitones ({ semitones = x })");
    }
    spec.centre = parseValueTable(t.at("centre"), file, "centre");
    spec.minValue = parseValueTable(t.at("min"), file, "min");
    spec.maxValue = parseValueTable(t.at("max"), file, "max");
    if (valueToSemitones(spec.minValue, file, "min") >
        valueToSemitones(spec.maxValue, file, "max")) {
      throw ConfigError(file, "min", "min must be <= max");
    }
    if (present.count("seed") == 0) {
      throw ConfigError(file, "seed", "seed is required for stochastic curves");
    }
  } else if (spec.kind == "breakpoints") {
    checkFields({{"points"}, {}}, "breakpoints");
    const auto& pts = t.at("points").asArray(file, "points");
    if (pts.empty()) {
      throw ConfigError(file, "points", "empty curve: at least one breakpoint is required");
    }
    double prevTime = -1.0;
    for (const auto& pt : pts) {
      const auto& table = pt.asTable(file, "points");
      if (table.count("t") == 0 || table.count("value") == 0) {
        throw ConfigError(file, "points", "each breakpoint needs { t = ..., value = ... }");
      }
      for (const auto& [key, kv] : table) {
        (void)kv;
        if (key != "t" && key != "value" && key != "law") {
          throw ConfigError(file, key, "unknown key in breakpoint (allowed: t, value, law)");
        }
      }
      CurveBreakPoint bp;
      bp.timeSec = parseTimeTable(table.at("t"), file, "points");
      bp.ratio = parseValueTable(table.at("value"), file, "points").toRatio(file, "points");
      if (table.count("law") != 0) {
        const std::string law = table.at("law").asString(file, "law");
        if (law != "lin" && law != "exp") {
          throw ConfigError(file, "law", "law must be \"lin\" or \"exp\"");
        }
        bp.lawExp = (law == "exp");
      }
      if (bp.timeSec <= prevTime) {
        throw ConfigError(file, "points", "breakpoint times must be strictly increasing");
      }
      if (bp.timeSec < 0.0) {
        throw ConfigError(file, "points", "breakpoint times must be >= 0");
      }
      prevTime = bp.timeSec;
      spec.points.push_back(bp);
    }
  } else if (spec.kind == "external") {
    checkFields({{"source", "mode"}, {"rate"}}, "external");
    const auto& src = t.at("source").asTable(file, "source");
    if (src.count("file") == 0) {
      throw ConfigError(file, "source", "source table must be { file = \"...\" }");
    }
    for (const auto& [key, kv] : src) {
      (void)kv;
      if (key != "file") {
        throw ConfigError(file, key, "unknown key in source table (allowed: file)");
      }
    }
    spec.sourceFile = src.at("file").asString(file, "source");
    if (spec.sourceFile.empty()) {
      throw ConfigError(file, "source", "source file must be a non-empty string");
    }
    spec.sourceMode = t.at("mode").asString(file, "mode");
    if (spec.sourceMode != "dense" && spec.sourceMode != "pairs") {
      throw ConfigError(file, "mode", "mode must be \"dense\" or \"pairs\"");
    }
    if (spec.sourceMode == "dense") {
      if (present.count("rate") == 0) {
        throw ConfigError(file, "rate", "rate (Hz) is required for mode = \"dense\"");
      }
      spec.denseRateHz = t.at("rate").asDouble(file, "rate");
      spec.hasDenseRate = true;
      if (!std::isfinite(spec.denseRateHz) || spec.denseRateHz <= 0.0) {
        throw ConfigError(file, "rate", "rate must be finite and > 0");
      }
    }
  }

  // Seed present but kind is never stochastic => rejected (typo protection).
  const bool stochastic =
      (spec.kind == "random") || (spec.kind == "lfo" && spec.wave == "random-walk");
  if (spec.hasSeed && !stochastic) {
    throw ConfigError(file, "seed", "seed is only valid for stochastic curves (random / lfo "
                                    "wave = random-walk)");
  }

  return spec;
}

// ---------------------------------------------------------------------------
// External CSV loading (§4.4.3.1 item 10)
// ---------------------------------------------------------------------------

struct CsvPoint {
  double timeSec = 0.0;
  double ratio = 1.0;
};

[[nodiscard]] std::string trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

[[nodiscard]] double parseCsvNumber(const std::string& token, const std::string& file, int line,
                                    const std::string& field) {
  const std::string t = trim(token);
  if (t.empty()) {
    throw ConfigError(file, field, "line " + std::to_string(line) + ": empty numeric field");
  }
  std::size_t consumed = 0;
  double value = 0.0;
  try {
    value = std::stod(t, &consumed);
  } catch (const std::exception&) {
    throw ConfigError(file, field,
                      "line " + std::to_string(line) + ": invalid number '" + t + "'");
  }
  if (consumed != t.size()) {
    throw ConfigError(file, field,
                      "line " + std::to_string(line) + ": trailing characters in '" + t + "'");
  }
  if (!std::isfinite(value)) {
    throw ConfigError(file, field, "line " + std::to_string(line) + ": non-finite value");
  }
  return value;
}

/// Load the external CSV into (time, ratio) points. Dense mode: row j at
/// time j/rate. Pairs mode: "time,value" rows (time in seconds).
[[nodiscard]] std::vector<CsvPoint> loadExternalPoints(const PitchCurveSpec& spec) {
  const std::filesystem::path csvPath =
      std::filesystem::path(spec.file).parent_path() / spec.sourceFile;
  std::ifstream in(csvPath, std::ios::binary);
  if (!in) {
    throw ConfigError(spec.file, "source", "external curve file not found: " + csvPath.string());
  }
  std::vector<CsvPoint> points;
  std::string line;
  int lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    const std::size_t comma = t.find(',');
    double timeSec, ratio;
    if (spec.sourceMode == "pairs") {
      if (comma == std::string::npos) {
        throw ConfigError(csvPath.string(), "source",
                          "line " + std::to_string(lineNo) +
                              ": pairs mode requires 'time,value' (comma missing)");
      }
      timeSec = parseCsvNumber(t.substr(0, comma), csvPath.string(), lineNo, "source");
      ratio = parseCsvNumber(t.substr(comma + 1), csvPath.string(), lineNo, "source");
      if (timeSec < 0.0) {
        throw ConfigError(csvPath.string(), "source",
                          "line " + std::to_string(lineNo) + ": time must be >= 0");
      }
      if (!points.empty() && timeSec <= points.back().timeSec) {
        throw ConfigError(csvPath.string(), "source",
                          "line " + std::to_string(lineNo) + ": times must be strictly increasing");
      }
    } else {
      if (comma != std::string::npos) {
        throw ConfigError(csvPath.string(), "source",
                          "line " + std::to_string(lineNo) +
                              ": dense mode requires a single ratio value per line");
      }
      ratio = parseCsvNumber(t, csvPath.string(), lineNo, "source");
      timeSec = static_cast<double>(points.size()) / spec.denseRateHz;
    }
    if (!(ratio > 0.0)) {
      throw ConfigError(csvPath.string(), "source",
                        "line " + std::to_string(lineNo) + ": ratio must be strictly positive");
    }
    points.push_back(CsvPoint{timeSec, ratio});
  }
  if (points.empty()) {
    throw ConfigError(spec.file, "source", "empty curve: the external CSV has no data rows");
  }
  return points;
}

// ---------------------------------------------------------------------------
// Signal filling helpers
// ---------------------------------------------------------------------------

/// Fill the trailing uncovered region [definedEnd, N) per the extend policy
/// (§4.4.3.1 item 11): hold-last => last defined value; hold-first => first
/// defined value. The leading region [0, firstDefinedFrame) is always filled
/// with the first defined value by the callers (it can only be non-empty for
/// breakpoints/external).
void fillTail(double* ratios, int64_t N, int64_t definedEnd, double firstValue,
              double lastValue, bool holdLast) {
  for (int64_t i = definedEnd; i < N; ++i) {
    ratios[i] = holdLast ? lastValue : firstValue;
  }
}

/// Zero-order-hold random walk in the semitone domain (§4.4.3.1 item 8),
/// shared by kind "random" and lfo wave "random-walk".
void compileRandomWalk(double* ratios, int64_t N, double fs, double stepsPerSecond,
                       double startSt, double depthSt, double minSt, double maxSt,
                       uint64_t seed) {
  auto rng = makeConsumerStream(seed, "curve.random-walk");
  double p = startSt;
  int64_t k = 0;  // next step boundary: round(k * fs / stepsPerSecond)
  int64_t boundary = 0;
  for (int64_t i = 0; i < N; ++i) {
    while (i >= boundary) {
      // advance to the next boundary at or beyond i (first boundary k=0 is
      // frame 0: the walk position IS the centre at frame 0 — the first
      // step applies at the first boundary AFTER frame 0).
      if (k > 0) {
        const double u = rng.nextDouble01() * 2.0 - 1.0;
        p += u * depthSt;
        if (p < minSt) p = minSt;
        if (p > maxSt) p = maxSt;
      }
      ++k;
      boundary = static_cast<int64_t>(std::llround(static_cast<double>(k) * fs / stepsPerSecond));
    }
    ratios[i] = std::exp2(p / 12.0);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

double CurveValue::toRatio(const std::string& file, const std::string& field) const {
  if (isRatio) {
    if (!std::isfinite(ratio) || ratio <= 0.0) {
      throw ConfigError(file, field, "ratio must be finite and strictly positive");
    }
    return ratio;
  }
  if (!std::isfinite(semitones)) {
    throw ConfigError(file, field, "semitones must be finite");
  }
  const double r = std::exp2(semitones / 12.0);
  if (!std::isfinite(r) || !(r > 0.0)) {
    throw ConfigError(file, field, "semitones out of the representable ratio range");
  }
  return r;
}

PitchCurveSpec parseCurveSpec(const std::filesystem::path& tomlFile) {
  const toml::TomlTable t = toml::parseTomlFile(tomlFile);
  return specFromTable(t, tomlFile);
}

PitchCurveSignal compileCurveSignal(const PitchCurveSpec& spec, double sampleRate,
                                    int64_t totalFrames) {
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
    throw ConfigError(spec.file, "sampleRate", "sample rate must be finite and > 0");
  }
  if (totalFrames <= 0) {
    throw ConfigError(spec.file, "totalFrames", "total frames must be > 0");
  }
  PitchCurveSignal out;
  out.specId = spec.id;
  out.sampleRate = sampleRate;
  out.totalFrames = totalFrames;
  out.ratios.assign(static_cast<std::size_t>(totalFrames), 1.0);
  double* r = out.ratios.data();
  const double fs = sampleRate;
  const int64_t N = totalFrames;

  if (spec.kind == "static") {
    const double v = spec.value.toRatio(spec.file, "value");
    for (int64_t i = 0; i < N; ++i) r[i] = v;
  } else if (spec.kind == "ramp_lin" || spec.kind == "reversal") {
    const double a = spec.from.toRatio(spec.file, "from");
    const double b = spec.to.toRatio(spec.file, "to");
    const int64_t i1 = secondsToFrames(spec.holdSec, fs);
    const int64_t i2 = i1 + secondsToFrames(spec.timeSec, fs);
    if (i2 <= i1) {
      // Window shorter than one frame: an instantaneous value jump.
      if (!spec.allowDiscontinuity) {
        throw ConfigError(spec.file, "time",
                          "transition window is shorter than one frame at this sample rate "
                          "(instantaneous jump) — requires allow_discontinuity = true");
      }
      for (int64_t i = 0; i < N; ++i) r[i] = (i < i1) ? a : b;
    } else {
      const double span = static_cast<double>(i2 - i1);
      for (int64_t i = 0; i < N; ++i) {
        double u = static_cast<double>(i - i1) / span;
        if (u < 0.0) u = 0.0;
        if (u > 1.0) u = 1.0;
        r[i] = a + (b - a) * u;
      }
    }
  } else if (spec.kind == "ramp_exp") {
    const double a = spec.from.toRatio(spec.file, "from");
    const int64_t i1 = secondsToFrames(spec.holdSec, fs);
    const int64_t i2 = i1 + secondsToFrames(spec.timeSec, fs);
    if (spec.hasRateForm) {
      // §4.4.3 verbatim: per-frame growth factor computed once, applied
      // multiplicatively (recurrence, NOT the closed form).
      const double g = std::exp2(spec.stps / (12.0 * fs));
      const int64_t m = i2 - i1;
      if (m <= 0) {
        for (int64_t i = 0; i < N; ++i) r[i] = a;
      } else {
        double v = a;
        for (int64_t i = 0; i < N; ++i) {
          const int64_t k = i - i1;
          if (k < 0) {
            r[i] = a;
          } else if (k < m) {
            r[i] = v;
            v *= g;
          } else {
            r[i] = v;
          }
        }
      }
    } else {
      const double b = spec.to.toRatio(spec.file, "to");
      if (i2 <= i1) {
        if (!spec.allowDiscontinuity) {
          throw ConfigError(spec.file, "time",
                            "transition window is shorter than one frame at this sample rate "
                            "(instantaneous jump) — requires allow_discontinuity = true");
        }
        for (int64_t i = 0; i < N; ++i) r[i] = (i < i1) ? a : b;
      } else {
        const double span = static_cast<double>(i2 - i1);
        for (int64_t i = 0; i < N; ++i) {
          double u = static_cast<double>(i - i1) / span;
          if (u < 0.0) u = 0.0;
          if (u > 1.0) u = 1.0;
          r[i] = a * std::pow(b / a, u);
        }
      }
    }
  } else if (spec.kind == "lfo") {
    if (spec.wave == "random-walk") {
      compileRandomWalk(r, N, fs, spec.rateHz, valueToSemitones(spec.centre, spec.file, "centre"),
                        spec.depth.semitones, valueToSemitones(spec.minValue, spec.file, "min"),
                        valueToSemitones(spec.maxValue, spec.file, "max"), spec.seed);
    } else {
      const double centreSt = valueToSemitones(spec.centre, spec.file, "centre");
      const double depthSt = spec.depth.semitones;
      for (int64_t i = 0; i < N; ++i) {
        const double t = static_cast<double>(i) / fs;
        double w;
        if (spec.wave == "sine") {
          w = std::sin(2.0 * kPi * spec.rateHz * t);
        } else {
          const double u = frac01(spec.rateHz * t);
          if (spec.wave == "triangle") {
            w = (u < 0.5) ? (4.0 * u - 1.0) : (3.0 - 4.0 * u);
          } else {  // saw (allow_discontinuity validated at parse)
            w = 2.0 * u - 1.0;
          }
        }
        r[i] = std::exp2((centreSt + depthSt * w) / 12.0);
      }
    }
  } else if (spec.kind == "random") {
    compileRandomWalk(r, N, fs, spec.rateHz,
                      valueToSemitones(spec.centre, spec.file, "centre"), spec.depth.semitones,
                      valueToSemitones(spec.minValue, spec.file, "min"),
                      valueToSemitones(spec.maxValue, spec.file, "max"), spec.seed);
  } else if (spec.kind == "breakpoints" || spec.kind == "external") {
    std::vector<CsvPoint> pts;  // (t, ratio) unified representation
    if (spec.kind == "breakpoints") {
      pts.reserve(spec.points.size());
      for (const auto& bp : spec.points) {
        pts.push_back(CsvPoint{bp.timeSec, bp.ratio});
      }
    } else {
      pts = loadExternalPoints(spec);
    }
    // Frame indices; strictly increasing after rounding (frozen rule).
    std::vector<int64_t> f;
    f.reserve(pts.size());
    for (const auto& p : pts) {
      f.push_back(secondsToFrames(p.timeSec, fs));
    }
    for (std::size_t j = 1; j < f.size(); ++j) {
      if (f[j] <= f[j - 1]) {
        throw ConfigError(spec.file, "points",
                          "curve times map to the same frame at this sample rate (times must "
                          "remain strictly monotone after frame rounding)");
      }
    }
    const double firstValue = pts.front().ratio;
    const double lastValue = pts.back().ratio;
    const int64_t firstFrame = f.front();
    const int64_t lastFrame = f.back();
    // Leading region: always the first defined value (§4.4.3.1 item 11).
    for (int64_t i = 0; i < std::min(firstFrame, N); ++i) r[i] = firstValue;
    // Segments.
    for (std::size_t j = 0; j + 1 < pts.size(); ++j) {
      const int64_t s = f[j];
      const int64_t e = f[j + 1];
      const double va = pts[j].ratio;
      const double vb = pts[j + 1].ratio;
      const bool lawExp = (spec.kind == "breakpoints") ? spec.points[j + 1].lawExp : false;
      const double span = static_cast<double>(e - s);
      for (int64_t i = std::max<int64_t>(s, 0); i < std::min(e, N); ++i) {
        const double u = static_cast<double>(i - s) / span;
        r[i] = lawExp ? va * std::pow(vb / va, u) : va + (vb - va) * u;
      }
    }
    // Final point value at its own frame.
    if (lastFrame < N && lastFrame >= 0) r[lastFrame] = lastValue;
    // Trailing region per policy.
    fillTail(r, N, std::max<int64_t>(lastFrame + 1, 0), firstValue, lastValue,
             spec.extendHoldLast);
  }

  // Post-compilation validation (§4.4.3.1 item 12): finite and strictly
  // positive at every frame, with the offending frame named.
  for (int64_t i = 0; i < N; ++i) {
    if (!std::isfinite(r[i]) || !(r[i] > 0.0)) {
      throw ConfigError(spec.file, spec.kind,
                        "compiled ratio is not finite/positive at frame " + std::to_string(i));
    }
  }
  return out;
}

std::vector<PitchCurveSpec> loadCurveBattery(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".toml") {
      files.push_back(entry.path());
    }
  }
  if (ec) {
    throw ConfigError(dir.string(), "", "curve directory not readable: " + ec.message());
  }
  std::sort(files.begin(), files.end());
  std::vector<PitchCurveSpec> specs;
  specs.reserve(files.size());
  for (const auto& file : files) {
    specs.push_back(parseCurveSpec(file));
  }
  return specs;
}

}  // namespace pitchlab
