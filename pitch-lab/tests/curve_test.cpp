// T-CV* / T-E15 (curve-library slice) — pitch curve spec parsing, validation
// and deterministic compilation (implementation specification §4.4,
// frozen behaviour §4.4.3.1; §13 T-E15: bad TOML / non-finite params /
// unknown keys / missing seed ⇒ CONFIG ERROR with file+field; §17 step 2
// gate: "curve battery compiles deterministically").
//
// Exact-formula pins per §4.4.3: "the exact formula [is] fixed in the
// curve-compiler unit tests to prevent divergence" — every kind's compiled
// value is checked against the frozen formula (tolerance 1e-12 relative for
// libm-crossing pins; EXACT equality for structural/recurrence pins).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/curve.h"
#include "core/errors.h"
#include "core/toml_lite.h"

using pitchlab::compileCurveSignal;
using pitchlab::loadCurveBattery;
using pitchlab::parseCurveSpec;
using pitchlab::PitchCurveSpec;
using pitchlab::PitchCurveSignal;

#ifndef PITCHLAB_SOURCE_DIR
#error "PITCHLAB_SOURCE_DIR must be defined by CMake for this test"
#endif

namespace {

const std::filesystem::path kBatteryDir =
    std::filesystem::path(PITCHLAB_SOURCE_DIR) / "experiments" / "curves";

const double kPi = 3.14159265358979323846;

int g_tempCounter = 0;

std::filesystem::path writeTemp(const std::string& name, const std::string& content) {
  const auto dir = std::filesystem::temp_directory_path() / "pitchlab-curve-test";
  std::filesystem::create_directories(dir);
  const auto path = dir / (std::to_string(++g_tempCounter) + "-" + name);
  { std::ofstream out(path, std::ios::binary); out << content; }
  return path;
}

struct Captured {
  bool threw = false;
  std::string file, field, reason;
};

Captured tryParse(const std::string& tomlText) {
  Captured cap;
  try {
    (void)parseCurveSpec(writeTemp("spec.toml", tomlText));
  } catch (const pitchlab::ConfigError& e) {
    cap.threw = true;
    cap.file = e.file();
    cap.field = e.field();
    cap.reason = e.reason();
  }
  return cap;
}

Captured tryCompile(const std::string& tomlText, double fs, int64_t frames) {
  Captured cap;
  try {
    const auto spec = parseCurveSpec(writeTemp("spec.toml", tomlText));
    (void)compileCurveSignal(spec, fs, frames);
  } catch (const pitchlab::ConfigError& e) {
    cap.threw = true;
    cap.file = e.file();
    cap.field = e.field();
    cap.reason = e.reason();
  }
  return cap;
}

pitchlab::PitchCurveSpec specFromText(const std::string& tomlText) {
  return parseCurveSpec(writeTemp("spec.toml", tomlText));
}

double relDiff(double a, double b) { return std::fabs(a - b) / std::fabs(b); }

}  // namespace

// ---------------------------------------------------------------------------
// T-CV0: TOML subset parser (toml_lite) — accepted surface + rejections
// ---------------------------------------------------------------------------

TEST_CASE("T-CV0a: toml_lite parses the frozen v0.1 authoring surface") {
  const auto table = pitchlab::toml::parseTomlText(R"(
# comment
version = 1
block_frames = 4096
ratio = 0.25
hexseed = 0x50E5A1AB
neg = -12
pi = 3.14159
sci = 1.0e-3
flag = true
name = "curve with \"escaped\" and \t tab"
inline = { a = 1, b = "x" }
list = [1, 2, 3, ]
nested = { freqHz = 440.0, partials = 40 }
[[item]]
id = "one"
[[item]]
id = "two"
)",
                                                    "mem");
  CHECK(table.at("version").asInteger("mem", "version") == 1);
  CHECK(table.at("block_frames").asInteger("mem", "block_frames") == 4096);
  CHECK(table.at("hexseed").asInteger("mem", "hexseed") == 0x50E5A1AB);
  CHECK(table.at("ratio").asDouble("mem", "ratio") == 0.25);
  CHECK(table.at("neg").asInteger("mem", "neg") == -12);
  CHECK(table.at("sci").asDouble("mem", "sci") == 1.0e-3);
  CHECK(table.at("flag").asBoolean("mem", "flag") == true);
  CHECK(table.at("name").asString("mem", "name") == "curve with \"escaped\" and \t tab");
  CHECK(table.at("list").asArray("mem", "list").size() == 3);
  CHECK(table.at("inline").asTable("mem", "inline").at("a").asInteger("mem", "a") == 1);
  CHECK(table.at("item").asArray("mem", "item").size() == 2);
  CHECK(table.at("item").asArray("mem", "item")[1].asTable("mem", "item").at("id").asString(
            "mem", "id") == "two");
}

TEST_CASE("T-CV0b: toml_lite rejects malformed / out-of-surface input") {
  const char* bad[] = {
      "x = ",                                    // missing value
      "x = 1 y = 2",                             // missing newline
      "x = 'literal'",                           // literal strings not accepted
      "x = [1, 2",                               // unterminated array
      "x = { a = 1, }",                          // trailing comma in inline table
      "x = \"unterminated",                      // unterminated string
      "x = 007",                                 // leading zero
      "x = inf",                                 // non-finite literal
      "x = nan",                                 // non-finite literal
      "x = 1_000",                               // digit separators not accepted
      "[a.b]\nx = 1",                            // dotted header segment
      "x = 1\nx = 2",                            // duplicate key
      "[t]\nx = 1\n[t]\ny = 2",                  // duplicate table
      "x = { a = 1\n}",                          // multi-line inline table
  };
  for (const char* text : bad) {
    bool threw = false;
    try {
      (void)pitchlab::toml::parseTomlText(text, "bad.toml");
    } catch (const pitchlab::ConfigError&) {
      threw = true;
    }
    CHECK_MESSAGE(threw, "expected rejection of: " << text);
  }
}

// ---------------------------------------------------------------------------
// T-E15 (curve slice): validation failure matrix (§4.4.4 hard errors)
// ---------------------------------------------------------------------------

TEST_CASE("T-E15a: curve validation failure matrix — CONFIG ERROR + field") {
  struct Case {
    const char* name;
    std::string toml;
    std::string fieldSub;  // expected substring of the error field
  };
  const Case cases[] = {
      {"unknown kind", "id = \"x\"\nkind = \"wobble\"\n", "kind"},
      {"missing kind", "id = \"x\"\nvalue = { ratio = 1.0 }\n", "kind"},
      {"missing id", "kind = \"static\"\nvalue = { ratio = 1.0 }\n", "id"},
      {"unknown key", "kind = \"static\"\nid = \"x\"\nvalue = { ratio = 1.0 }\nvelue = 1\n",
       "velue"},
      {"static missing value", "id = \"x\"\nkind = \"static\"\n", "value"},
      {"static value both forms",
       "id = \"x\"\nkind = \"static\"\nvalue = { semitones = 1.0, ratio = 2.0 }\n", "value"},
      {"static non-positive ratio", "id = \"x\"\nkind = \"static\"\nvalue = { ratio = -1.0 }\n",
       "value"},
      {"value table unknown key",
       "id = \"x\"\nkind = \"static\"\nvalue = { semitones = 1.0, dbfs = 2.0 }\n",
       "dbfs"},
      {"ramp missing time", "id = \"x\"\nkind = \"ramp_lin\"\nfrom = { ratio = 1.0 }\nto = { "
                            "ratio = 2.0 }\n",
       "time"},
      {"ramp field wrong for kind",
       "id = \"x\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\nfrom = { ratio = 2.0 }\n",
       "from"},
      {"ramp_exp both parameterizations",
       "id = \"x\"\nkind = \"ramp_exp\"\nfrom = { ratio = 1.0 }\nto = { ratio = 2.0 }\nrate = "
       "{ stps = 1.0 }\ntime = { s = 1.0 }\n",
       "to"},
      {"ramp_exp rate table wrong key",
       "id = \"x\"\nkind = \"ramp_exp\"\nfrom = { ratio = 1.0 }\nrate = { perSecond = 1.0 }\n"
       "time = { s = 1.0 }\n",
       "rate"},
      {"lfo missing wave", "id = \"x\"\nkind = \"lfo\"\nrate = 5.0\ndepth = { semitones = 1.0 }\n",
       "wave"},
      {"lfo bad wave", "id = \"x\"\nkind = \"lfo\"\nwave = \"square\"\nrate = 5.0\ndepth = { "
                       "semitones = 1.0 }\n",
       "wave"},
      {"lfo depth in ratio", "id = \"x\"\nkind = \"lfo\"\nwave = \"sine\"\nrate = 5.0\ndepth = { "
                             "ratio = 2.0 }\n",
       "depth"},
      {"lfo rate <= 0", "id = \"x\"\nkind = \"lfo\"\nwave = \"sine\"\nrate = 0.0\ndepth = { "
                        "semitones = 1.0 }\n",
       "rate"},
      {"saw without discontinuity flag",
       "id = \"x\"\nkind = \"lfo\"\nwave = \"saw\"\nrate = 5.0\ndepth = { semitones = 1.0 }\n",
       "allow_discontinuity"},
      {"random missing seed",
       "id = \"x\"\nkind = \"random\"\nrate = 4.0\ndepth = { semitones = 1.0 }\ncentre = { "
       "semitones = 0.0 }\nmin = { semitones = -12.0 }\nmax = { semitones = 12.0 }\n",
       "seed"},
      {"random min > max",
       "id = \"x\"\nkind = \"random\"\nrate = 4.0\ndepth = { semitones = 1.0 }\ncentre = { "
       "semitones = 0.0 }\nmin = { semitones = 12.0 }\nmax = { semitones = -12.0 }\nseed = 1\n",
       "min"},
      {"seed on non-stochastic kind",
       "id = \"x\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\nseed = 5\n", "seed"},
      {"breakpoints empty", "id = \"x\"\nkind = \"breakpoints\"\npoints = []\n", "points"},
      {"breakpoints non-monotone",
       "id = \"x\"\nkind = \"breakpoints\"\npoints = [\n  { t = { ms = 200 }, value = { ratio = "
       "1.0 } },\n  { t = { ms = 100 }, value = { ratio = 2.0 } },\n]\n",
       "points"},
      {"breakpoints bad law",
       "id = \"x\"\nkind = \"breakpoints\"\npoints = [\n  { t = { ms = 100 }, value = { ratio = "
       "1.0 }, law = \"cubic\" },\n]\n",
       "law"},
      {"external bad mode", "id = \"x\"\nkind = \"external\"\nsource = { file = \"c.csv\" }\nmode "
                            "= \"sparse\"\n",
       "mode"},
      {"external dense missing rate",
       "id = \"x\"\nkind = \"external\"\nsource = { file = \"c.csv\" }\nmode = \"dense\"\n",
       "rate"},
      {"extend bad value",
       "id = \"x\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\nextend = \"wrap\"\n", "extend"},
      {"time table zero", "id = \"x\"\nkind = \"ramp_lin\"\nfrom = { ratio = 1.0 }\nto = { ratio "
                          "= 2.0 }\ntime = { ms = 0.0 }\n",
       "time"},
  };
  for (const auto& c : cases) {
    const auto cap = tryParse(c.toml);
    CHECK_MESSAGE(cap.threw, c.name << ": expected CONFIG ERROR");
    if (cap.threw) {
      CHECK_MESSAGE(cap.field.find(c.fieldSub) != std::string::npos,
                    c.name << ": expected field containing '" << c.fieldSub << "', got '"
                           << cap.field << "' (reason: " << cap.reason << ")");
      CHECK_MESSAGE(!cap.file.empty(), c.name << ": error names the file");
    }
  }
}

TEST_CASE("T-E15b: non-finite parameters are rejected") {
  // TOML-level: inf/nan literals rejected by the parser (T-CV0b). Value-level:
  // overflow in semitone -> ratio conversion is rejected at COMPILE time
  // (toRatio / post-compilation validation).
  const auto cap = tryCompile(
      "id = \"x\"\nkind = \"static\"\nvalue = { semitones = 1.0e300 }\n", 48000.0, 100);
  CHECK(cap.threw);
  CHECK(cap.field.find("value") != std::string::npos);
}

// ---------------------------------------------------------------------------
// T-CV1..T-CV9: exact compiled semantics per kind (§4.4.3.1 formula pins)
// ---------------------------------------------------------------------------

TEST_CASE("T-CV1: static compilation is the exact authored value everywhere") {
  const double fs = 48000.0;
  const auto sig = compileCurveSignal(
      specFromText("id = \"s\"\nkind = \"static\"\nvalue = { semitones = +7.0 }\n"), fs, 1000);
  REQUIRE(sig.ratios.size() == 1000);
  const double expect = std::exp2(7.0 / 12.0);
  for (double v : sig.ratios) CHECK(relDiff(v, expect) < 1e-12);
  // identity
  const auto id1 = compileCurveSignal(
      specFromText("id = \"s\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n"), fs, 64);
  for (double v : id1.ratios) CHECK(v == 1.0);
}

TEST_CASE("T-CV2: ramp_lin — linear in the RATIO domain with pre-hold") {
  const double fs = 48000.0;
  // from 0.5 to 2.0 over 0.5 s, hold 0.25 s at from.
  const auto sig = compileCurveSignal(specFromText(R"(
id = "r"
kind = "ramp_lin"
from = { ratio = 0.5 }
to = { ratio = 2.0 }
time = { s = 0.5 }
hold = { s = 0.25 }
)"),
                                       fs, 48000);
  REQUIRE(sig.ratios.size() == 48000);
  const int64_t i1 = 12000;  // 0.25 s
  const int64_t i2 = 36000;  // + 0.5 s
  CHECK(sig.ratios[0] == 0.5);
  CHECK(sig.ratios[i1] == 0.5);   // window start: u = 0
  CHECK(sig.ratios[i2] == 2.0);   // window end: u = 1
  CHECK(sig.ratios[47999] == 2.0);
  const double mid = 0.5 + (2.0 - 0.5) * 0.5;  // u = 0.5
  CHECK(relDiff(sig.ratios[(i1 + i2) / 2], mid) < 1e-12);
  // linearity in the ratio domain: equal increments per frame (exact doubles
  // for the u grid only in expectation — use tolerance).
  const double d1 = sig.ratios[i1 + 1] - sig.ratios[i1];
  const double d2 = sig.ratios[i1 + 101] - sig.ratios[i1 + 100];
  CHECK(std::fabs(d1 - d2) < 1e-9);
}

TEST_CASE("T-CV2b: ramp window shorter than one frame requires the flag") {
  // 1 microsecond window at 48 kHz => 0 frames => instantaneous jump.
  const auto cap = tryCompile(
      "id = \"r\"\nkind = \"ramp_lin\"\nfrom = { ratio = 1.0 }\nto = { ratio = 2.0 }\n"
      "time = { ms = 0.001 }\n",
      48000.0, 100);
  CHECK(cap.threw);
  CHECK(cap.reason.find("allow_discontinuity") != std::string::npos);
  // with the flag it compiles as an instantaneous step AT t = 0 (hold and
  // window both round to zero frames: every frame is post-jump).
  const auto sig = compileCurveSignal(specFromText(R"(
id = "r"
kind = "ramp_lin"
from = { ratio = 1.0 }
to = { ratio = 2.0 }
time = { ms = 0.001 }
allow_discontinuity = true
)"),
                                       48000.0, 100);
  CHECK(sig.ratios[0] == 2.0);
  CHECK(sig.ratios[99] == 2.0);
}

TEST_CASE("T-CV3: ramp_exp endpoint form — geometric in the ratio domain") {
  const double fs = 48000.0;
  const auto sig = compileCurveSignal(specFromText(R"(
id = "g"
kind = "ramp_exp"
from = { ratio = 1.0 }
to = { semitones = +12.0 }
time = { s = 1.0 }
)"),
                                       fs, 96000);
  const int64_t i2 = 48000;
  CHECK(sig.ratios[0] == 1.0);
  CHECK(sig.ratios[i2] == 2.0);  // u = 1 exactly at the window end
  CHECK(sig.ratios[95999] == 2.0);  // tail holds the endpoint value
  // geometric: ratio at u = 0.5 is sqrt(a*b) in the formula
  CHECK(relDiff(sig.ratios[24000], std::sqrt(2.0)) < 1e-12);
  // multiplicative property: equal log2 increments over equal frame steps.
  const double l1 = std::log2(sig.ratios[24000]) - std::log2(sig.ratios[12000]);
  const double l2 = std::log2(sig.ratios[36000]) - std::log2(sig.ratios[24000]);
  CHECK(std::fabs(l1 - l2) < 1e-12);
}

TEST_CASE("T-CV4: ramp_exp rate form — per-frame multiplicative recurrence") {
  const double fs = 48000.0;
  // 2 s ramp inside a 3 s signal: window [0, 96000), tail [96000, 144000).
  const auto sig = compileCurveSignal(specFromText(R"(
id = "r"
kind = "ramp_exp"
from = { ratio = 1.0 }
rate = { stps = 12.0 }
time = { s = 2.0 }
)"),
                                       fs, 144000);
  REQUIRE(sig.ratios.size() == 144000);
  const double g = std::exp2(12.0 / (12.0 * fs));
  // The recurrence is canonical (§4.4.3.1 item 5): r[i+1] == r[i] * g,
  // EXACTLY, for every transition frame.
  for (int64_t i = 0; i + 1 < 96000; ++i) {
    CHECK(sig.ratios[i + 1] == sig.ratios[i] * g);
  }
  // window end: 2 s at +12 st/s => ratio ~2^2 = 4; tail holds it constant.
  CHECK(relDiff(sig.ratios[96000], 4.0) < 1e-9);
  for (int64_t i = 96000; i + 1 < 144000; ++i) {
    CHECK(sig.ratios[i + 1] == sig.ratios[i]);
  }
}

TEST_CASE("T-CV5: reversal — pre-hold, linear-in-ratio window, hold-last") {
  const double fs = 48000.0;
  const auto sig = compileCurveSignal(specFromText(R"(
id = "rev"
kind = "reversal"
from = { semitones = +12.0 }
to = { semitones = -12.0 }
time = { ms = 100 }
hold = { ms = 400 }
)"),
                                       fs, 48000);
  REQUIRE(sig.ratios.size() == 48000);
  const double a = 2.0, b = 0.5;
  const int64_t i1 = 19200;       // 400 ms
  const int64_t i2 = 19200 + 4800;  // + 100 ms
  CHECK(sig.ratios[0] == a);
  CHECK(sig.ratios[i1 - 1] == a);
  CHECK(sig.ratios[i1] == a);  // u = 0 at the window start
  CHECK(sig.ratios[i2] == b);  // u = 1 at the window end
  CHECK(sig.ratios[47999] == b);
  const double mid = a + (b - a) * 0.5;
  CHECK(relDiff(sig.ratios[(i1 + i2) / 2], mid) < 1e-12);
}

TEST_CASE("T-CV6: lfo sine/triangle/saw — log2-domain formulas") {
  const double fs = 48000.0;
  const int64_t N = 96000;  // 2 s => 10 cycles at 5 Hz
  const auto sig = compileCurveSignal(specFromText(R"(
id = "l"
kind = "lfo"
wave = "sine"
rate = 5.0
depth = { semitones = 1.0 }
centre = { semitones = 0.0 }
)"),
                                       fs, N);
  double maxR = 0.0, minR = 1e9;
  for (int64_t i = 0; i < N; ++i) {
    const double expect = std::exp2(std::sin(2.0 * kPi * 5.0 * i / fs) / 12.0);
    CHECK(relDiff(sig.ratios[static_cast<std::size_t>(i)], expect) < 1e-12);
    maxR = std::max(maxR, sig.ratios[static_cast<std::size_t>(i)]);
    minR = std::min(minR, sig.ratios[static_cast<std::size_t>(i)]);
  }
  CHECK(relDiff(maxR, std::exp2(1.0 / 12.0)) < 1e-12);
  CHECK(relDiff(minR, std::exp2(-1.0 / 12.0)) < 1e-12);

  // triangle: piecewise-linear w in [-1, 1]; corner checks at u = 0, 0.25, 0.5.
  const auto tri = compileCurveSignal(specFromText(R"(
id = "t"
kind = "lfo"
wave = "triangle"
rate = 1.0
depth = { semitones = 3.0 }
)"),
                                       fs, 48000);
  // rate 1 Hz: u = i/fs. w(0) = -1, w(0.25) = 0, w(0.5) = +1.
  CHECK(relDiff(tri.ratios[0], std::exp2(-3.0 / 12.0)) < 1e-12);
  CHECK(relDiff(tri.ratios[12000], std::exp2(0.0 / 12.0)) < 1e-12);
  CHECK(relDiff(tri.ratios[24000], std::exp2(3.0 / 12.0)) < 1e-12);

  // saw (with the required flag): w = 2u - 1, hard reset at each period start.
  const auto saw = compileCurveSignal(specFromText(R"(
id = "s"
kind = "lfo"
wave = "saw"
rate = 1.0
depth = { semitones = 2.0 }
allow_discontinuity = true
)"),
                                       fs, 96000);
  CHECK(relDiff(saw.ratios[0], std::exp2(-2.0 / 12.0)) < 1e-12);
  CHECK(relDiff(saw.ratios[24000], std::exp2(0.0 / 12.0)) < 1e-12);
  // end of period 1: u -> 1 (w -> +1), then the reset at frame 48000 (u = 0).
  CHECK(relDiff(saw.ratios[47999], std::exp2(2.0 * (2.0 * 47999.0 / 48000.0 - 1.0) / 12.0)) <
        1e-12);
  CHECK(relDiff(saw.ratios[48000], std::exp2(-2.0 / 12.0)) < 1e-12);
}

TEST_CASE("T-CV7: random walk — determinism, ZOH steps, clamped bounds") {
  const double fs = 48000.0;
  const std::string toml = R"(
id = "rw"
kind = "random"
rate = 4.0
depth = { semitones = 1.0 }
centre = { semitones = 0.0 }
min = { semitones = -12.0 }
max = { semitones = +12.0 }
seed = 0x5EEDC0DE
)";
  const auto spec = specFromText(toml);
  const auto s1 = compileCurveSignal(spec, fs, 48000);
  const auto s2 = compileCurveSignal(spec, fs, 48000);
  // Determinism: reseeded per compilation (§4.4.3).
  REQUIRE(s1.ratios.size() == s2.ratios.size());
  CHECK(std::memcmp(s1.ratios.data(), s2.ratios.data(),
                    s1.ratios.size() * sizeof(double)) == 0);
  // ZOH at 4 steps/s => 12000-frame holds: frame 0 IS the centre.
  CHECK(s1.ratios[0] == 1.0);
  CHECK(s1.ratios[11999] == s1.ratios[0]);
  CHECK(s1.ratios[12000] != s1.ratios[11999]);  // a step happened at the boundary
  // Bounds respected (clamp, not reflect).
  for (double v : s1.ratios) {
    CHECK(v >= 0.5 * (1.0 - 1e-12));
    CHECK(v <= 2.0 * (1.0 + 1e-12));
  }
  // A different seed gives a different walk (seed actually feeds the stream).
  const auto s3 = compileCurveSignal(specFromText(toml + std::string()), fs, 48000);
  (void)s3;
  const auto other = compileCurveSignal(specFromText(R"(
id = "rw2"
kind = "random"
rate = 4.0
depth = { semitones = 1.0 }
centre = { semitones = 0.0 }
min = { semitones = -12.0 }
max = { semitones = +12.0 }
seed = 99
)"),
                                        fs, 48000);
  bool anyDifferent = false;
  for (std::size_t i = 0; i < s1.ratios.size(); ++i) {
    if (s1.ratios[i] != other.ratios[i]) anyDifferent = true;
  }
  CHECK(anyDifferent);
}

TEST_CASE("T-CV8: breakpoints — laws, monotone times, extension policy") {
  const double fs = 48000.0;
  const auto sig = compileCurveSignal(specFromText(R"(
id = "bp"
kind = "breakpoints"
points = [
  { t = { ms = 100 }, value = { ratio = 1.0 } },
  { t = { ms = 200 }, value = { ratio = 2.0 }, law = "lin" },
  { t = { ms = 300 }, value = { ratio = 1.0 }, law = "exp" },
]
)"),
                                       fs, 48000);
  // Leading region [0, 100 ms): first value.
  CHECK(sig.ratios[0] == 1.0);
  CHECK(sig.ratios[4799] == 1.0);
  CHECK(sig.ratios[4800] == 1.0);  // point 0 at its own frame
  // lin segment [100, 200] ms: midpoint = 1.5.
  CHECK(relDiff(sig.ratios[7200], 1.5) < 1e-12);
  CHECK(sig.ratios[9600] == 2.0);
  // exp segment [200, 300] ms: midpoint = sqrt(2).
  CHECK(relDiff(sig.ratios[12000], std::sqrt(2.0)) < 1e-12);
  CHECK(sig.ratios[14400] == 1.0);
  // Trailing region, default hold-last: 1.0.
  CHECK(sig.ratios[47999] == 1.0);

  // hold-first: trailing region returns to the FIRST value (1.0 here too —
  // use a first value != last to distinguish).
  const auto hf = compileCurveSignal(specFromText(R"(
id = "bp2"
kind = "breakpoints"
points = [
  { t = { ms = 100 }, value = { ratio = 1.5 } },
  { t = { ms = 200 }, value = { ratio = 0.75 } },
]
extend = "hold-first"
)"),
                                       fs, 48000);
  CHECK(hf.ratios[0] == 1.5);
  CHECK(hf.ratios[9600] == 0.75);
  CHECK(hf.ratios[47999] == 1.5);  // returned to the first value
}

TEST_CASE("T-CV9: external CSV — dense and pairs, with rejections") {
  const double fs = 48000.0;
  const auto dir = std::filesystem::temp_directory_path() / "pitchlab-curve-test";
  { std::ofstream out(dir / "dense.csv"); out << "# dense ratio rows @ 100 Hz\n0.5\n0.5\n1.0\n"; }

  const auto dense = parseCurveSpec(writeTemp("dense.toml", R"(
id = "d"
kind = "external"
source = { file = "dense.csv" }
mode = "dense"
rate = 100.0
)"));
  const auto sig = compileCurveSignal(dense, fs, 960);  // 20 ms = 2 dense rows
  // row j at t = j/100; frame i at i/48000. Frame 0..479 -> u<0.5 within row
  // pair (0.5, 0.5); frames 480+ interpolate 0.5 -> 1.0.
  CHECK(sig.ratios[0] == 0.5);
  CHECK(sig.ratios[240] == 0.5);
  CHECK(relDiff(sig.ratios[720], 0.75) < 1e-12);
  // beyond the last row: hold-last
  CHECK(sig.ratios[959] > 0.75);
  CHECK(sig.ratios[959] < 1.0);

  { std::ofstream out(dir / "pairs.csv"); out << "0.0,1.0\n0.5,2.0\n# comment\n1.0,1.0\n"; }
  const auto pairs = parseCurveSpec(writeTemp("pairs.toml", R"(
id = "p"
kind = "external"
source = { file = "pairs.csv" }
mode = "pairs"
)"));
  const auto ps = compileCurveSignal(pairs, fs, 96000);
  CHECK(ps.ratios[0] == 1.0);
  CHECK(relDiff(ps.ratios[12000], 1.5) < 1e-12);  // t = 0.25 s: midpoint of (1,2)
  CHECK(ps.ratios[24000] == 2.0);                  // t = 0.5 s: the point itself
  CHECK(relDiff(ps.ratios[36000], 1.5) < 1e-12);  // t = 0.75 s: midpoint of (2,1)
  CHECK(ps.ratios[48000] == 1.0);                  // t = 1.0 s: final point
  CHECK(ps.ratios[95999] == 1.0);                  // beyond: hold-last

  // rejections: missing file / non-monotone pairs / non-positive value
  const auto cap1 = tryCompile("id = \"m\"\nkind = \"external\"\nsource = { file = "
                               "\"missing.csv\" }\nmode = \"pairs\"\n",
                               fs, 100);
  CHECK(cap1.threw);
  { std::ofstream out(dir / "nonmono.csv"); out << "0.5,1.0\n0.25,2.0\n"; }
  const auto cap2 = tryCompile("id = \"m\"\nkind = \"external\"\nsource = { file = "
                               "\"nonmono.csv\" }\nmode = \"pairs\"\n",
                               fs, 100);
  CHECK(cap2.threw);
  CHECK(cap2.reason.find("strictly increasing") != std::string::npos);
  { std::ofstream out(dir / "negval.csv"); out << "0.0,-1.0\n"; }
  const auto cap3 = tryCompile("id = \"m\"\nkind = \"external\"\nsource = { file = "
                               "\"negval.csv\" }\nmode = \"pairs\"\n",
                               fs, 100);
  CHECK(cap3.threw);
  CHECK(cap3.reason.find("strictly positive") != std::string::npos);
  // empty CSV => empty curve => CONFIG ERROR (§4.4.4)
  { std::ofstream out(dir / "empty.csv"); out << "# only comments\n"; }
  const auto cap4 = tryCompile("id = \"m\"\nkind = \"external\"\nsource = { file = "
                               "\"empty.csv\" }\nmode = \"pairs\"\n",
                               fs, 100);
  CHECK(cap4.threw);
  CHECK(cap4.reason.find("empty") != std::string::npos);
}

TEST_CASE("T-CV10: post-compilation validation catches non-finite output") {
  // LFO depth 1e6 st: exp2((0 + 1e6*w)/12) overflows to inf => CONFIG ERROR
  // naming the frame (§4.4.3.1 item 12).
  const auto cap = tryCompile(R"(
id = "big"
kind = "lfo"
wave = "sine"
rate = 5.0
depth = { semitones = 1000000.0 }
)", 48000.0, 1000);
  CHECK(cap.threw);
  CHECK(cap.reason.find("frame") != std::string::npos);
}

TEST_CASE("T-CV11: totalFrames / sampleRate validation") {
  const auto spec = specFromText("id = \"s\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n");
  const auto cap0 = [&] {
    Captured c;
    try {
      (void)compileCurveSignal(spec, 48000.0, 0);
    } catch (const pitchlab::ConfigError& e) {
      c.threw = true;
      c.field = e.field();
    }
    return c;
  }();
  CHECK(cap0.threw);
  CHECK(cap0.field.find("totalFrames") != std::string::npos);
}

// ---------------------------------------------------------------------------
// T-CV12: the §17 step-2 gate — the curve battery compiles deterministically
// ---------------------------------------------------------------------------

TEST_CASE("T-CV12: battery (21 files) compiles deterministically at 2 rates") {
  const auto specs = loadCurveBattery(kBatteryDir);
  REQUIRE(specs.size() == 21);
  // Expected ids (§11.3, deterministic file order).
  const std::vector<std::string> expected = {
      "extreme-0.25x",  "extreme-0.5x",
      "extreme-2x",     "extreme-4x",
      "extreme-8x",     "glide-exp-plus-12",
      "identity-1x",    "lfo-sine-5hz-depth-1st",
      "ramp-fast-plus-12stps", "ramp-negative-1stps",
      "ramp-slow-plus-1stps",  "random-walk-seeded",
      "reversal-plus12-minus12-100ms",
      "static-minus-1", "static-minus-12", "static-minus-24", "static-minus-5",
      "static-plus-1",  "static-plus-12",  "static-plus-24",  "static-plus-7"};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    CHECK(specs[i].id == expected[i]);
  }

  struct RateCase { double fs; int64_t frames; };
  const RateCase rates[] = {{48000.0, 240000}, {44100.0, 220500}};
  for (const auto& rc : rates) {
    for (const auto& spec : specs) {
      const auto s1 = compileCurveSignal(spec, rc.fs, rc.frames);
      const auto s2 = compileCurveSignal(spec, rc.fs, rc.frames);
      REQUIRE(s1.ratios.size() == static_cast<std::size_t>(rc.frames));
      // determinism: bit-identical recompilation
      CHECK(std::memcmp(s1.ratios.data(), s2.ratios.data(),
                        s1.ratios.size() * sizeof(double)) == 0);
      // coverage: every frame finite & strictly positive (post-validation ran)
      double minV = 1e300, maxV = 0.0;
      for (double v : s1.ratios) {
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
      }
      CHECK(minV > 0.0);
      CHECK(std::isfinite(maxV));
    }
  }

  // Semantic anchors (spot checks with the frozen formulas).
  const auto byId = [&](const std::string& id) {
    for (const auto& s : specs) {
      if (s.id == id) return s;
    }
    return specs[0];
  };
  const auto identity = compileCurveSignal(byId("identity-1x"), 48000.0, 240000);
  for (double v : identity.ratios) CHECK(v == 1.0);

  const auto e8 = compileCurveSignal(byId("extreme-8x"), 48000.0, 240000);
  CHECK(e8.ratios[0] == 8.0);
  const auto e025 = compileCurveSignal(byId("extreme-0.25x"), 48000.0, 240000);
  CHECK(e025.ratios[0] == 0.25);

  const auto slow = compileCurveSignal(byId("ramp-slow-plus-1stps"), 48000.0, 240000);
  CHECK(slow.ratios[0] == 1.0);
  // exact-formula pin at the LAST frame (the 5 s window ends exactly at N;
  // the recurrence value at frame 239999 is a*g^239999, within accumulated
  // rounding of the closed form exp2(stps*t/12)).
  CHECK(relDiff(slow.ratios[240000 - 1],
                std::exp2(1.0 * (240000.0 - 1.0) / 48000.0 / 12.0)) < 1e-9);

  const auto fast = compileCurveSignal(byId("ramp-fast-plus-12stps"), 48000.0, 240000);
  CHECK(relDiff(fast.ratios[240000 - 1],
                std::exp2(12.0 * (240000.0 - 1.0) / 48000.0 / 12.0)) < 1e-9);

  const auto neg = compileCurveSignal(byId("ramp-negative-1stps"), 48000.0, 240000);
  CHECK(relDiff(neg.ratios[240000 - 1],
                std::exp2(-1.0 * (240000.0 - 1.0) / 48000.0 / 12.0)) < 1e-9);

  const auto glide = compileCurveSignal(byId("glide-exp-plus-12"), 48000.0, 240000);
  CHECK(glide.ratios[0] == 1.0);
  // endpoint form: a*(b/a)^u with u = 239999/240000.
  CHECK(relDiff(glide.ratios[240000 - 1], std::pow(2.0, 239999.0 / 240000.0)) < 1e-12);

  const auto rev = compileCurveSignal(byId("reversal-plus12-minus12-100ms"), 48000.0, 240000);
  CHECK(rev.ratios[0] == 2.0);
  CHECK(rev.ratios[19199] == 2.0);
  CHECK(rev.ratios[24000] == 0.5);  // 400 ms + 100 ms
  CHECK(rev.ratios[239999] == 0.5);

  const auto lfo = compileCurveSignal(byId("lfo-sine-5hz-depth-1st"), 48000.0, 240000);
  CHECK(relDiff(lfo.ratios[0], std::exp2(0.0)) < 1e-15);  // sin(0) = 0

  const auto rw = compileCurveSignal(byId("random-walk-seeded"), 48000.0, 240000);
  CHECK(rw.ratios[0] == 1.0);
  for (double v : rw.ratios) {
    CHECK(v >= 0.5);
    CHECK(v <= 2.0);
  }
}
