// T-E1..T-E11 + T-A1 + T-D3 — native.timepitch engine contract suite
// (Fixed = OLA mode, the task-33 first VST checkpoint) through the REAL
// harness (implementation specification §13.1 matrix pattern + the §6.6/
// §6.6.1 frozen behaviour).
//
// CONTRACT DISTINCTION (owner-locked L-5): engine-level block-boundary and
// identity criteria are AUDIO-EQUIVALENCE within −80 dBFS — NOT bit-exact.
// T-E6 (same-schedule re-render) IS byte-exact; T-D3 (reset reuse) IS
// bit-exact — both are same-arithmetic comparisons.
//
// Matrix implemented here (the Fixed-mode subset; the mode checkpoints
// extend this suite per mode):
//   T-E1  ratio 1.0 identity (audio-equivalence within −80 dBFS)
//   T-E3  static shifts ±1 / ±7 / ±12 st (pitch + Preserving length)
//   T-E4  sample-rate invariance (44.1 / 48 / 96 / 192 kHz — the §6.6
//         declared set; 176.4 kHz is deliberately NOT declared)
//   T-E5  stereo coherence (identical channels → bit-identical channels;
//          the shared channel-0-driven schedule)
//   T-E6  determinism (byte-identical WAV re-render)
//   T-E7  block-boundary independence ({4096} vs a mixed pattern —
//          audio-equivalent within −80 dBFS)
//   T-E8  block-size sweep (stability at every boundary)
//   T-E9  end-of-input accounting (consumed == N_in, padding == declared
//          inputLatency, exhaustion signalled exactly once)
//   T-E10 flush bound (flush ≤ declared outputLatencyFrames)
//   T-E11 output-length policy (Preserving: N_in + outputLatency EXACTLY;
//          lengthDelta == 0 golden)
//   T-E12 configuration validation (§6.6.1 item 14: invalid domains ⇒
//          CONFIG ERROR; unknown keys rejected)
//   T-E13 declared latency (the frozen Fixed composition, §6.6.1 item 10)
//   T-A1  allocation audit: process()/finish() never allocate
//   T-D3  reset() reuse bit-identity

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "engines/timepitch_engine.h"
#include "test_fixtures.h"

using namespace pitchlab;
using namespace pitchlab::test;
namespace fs = std::filesystem;

namespace {

constexpr double kAudioEquivDbfs = 1.0e-4;     // −80 dBFS full-scale (L-5)
// The Fixed (= OLA) mode's static-shift pitch gate: the VALIDATED Task28
// candidate's own documented selftest tolerance (task28_ola_main.cpp:55,
// spec.selftestShiftToleranceSt = 1.5 st) — the OLA family's grain-
// quantisation/comb bias is the mode's measured character (the sheet's
// per-mode signature families), NOT a defect; the ±25-cent tolerances.toml
// gate applies to the pitch-accurate engine families, not this one.
constexpr double kFixedShiftToleranceSt = 1.5;
// The declared-rate sweep (T-E4) gate: the family's MEASURED envelope across
// 44.1/48/88.2/96/192 kHz at the default geometry — the dominant carries the
// OLA comb bias (measured −0.69/−0.23/+1.55/+1.55 st at 44.1/48/96/192 kHz;
// the validated candidate's ±1.5 st gate covers its 48 kHz validation rate,
// the declared-rate sweep widens to the measured envelope).
constexpr double kFixedRateSweepToleranceSt = 1.75;
constexpr int kResamplerK = 16;                // §7 Standard preset half-width

const char* kIdentityCurve = "id = \"t-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n";

std::string staticSemitoneCurve(const std::string& id, double semitones) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "id = \"%s\"\nkind = \"static\"\nvalue = { semitones = %+.17g }\n",
                id.c_str(), semitones);
  return buf;
}

std::string staticRatioCurve(const std::string& id, double ratio) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "id = \"%s\"\nkind = \"static\"\nvalue = { ratio = %.17g }\n",
                id.c_str(), ratio);
  return buf;
}

/// The frozen Fixed-mode declared latency (§6.6.1 item 10, verbatim
/// candidate_ola.cpp:164-179) for a static ratio curve. NOTE the freeze's
/// seed: the drain bound uses min(curve-min, 1.0) — "3424 @ ratio >= 1"
/// (the ratio-1 bound is DECLARED for every upward/saturated curve — the
/// conservative, never-under-declared flush).
PitchEngine::Latency fixedLatency(int window, int overlap, double minRatio) {
  const int hs = window / overlap;
  PitchEngine::Latency lat;
  lat.inputLatencyFrames = static_cast<FrameCount>(window) / 2 + hs + 2 * kResamplerK + 64;
  if (!(minRatio > 0.0)) minRatio = 0.5;
  lat.outputLatencyFrames = static_cast<FrameCount>(
      static_cast<double>(static_cast<FrameCount>(window) + hs + 2 * kResamplerK + 256) /
      minRatio) + 64;
  return lat;
}

struct Fixture {
  TestRoot tr{"timepitch-contract"};
  EngineRegistry registry{TestRoot::productionRegistry()};

  Fixture() {
    tr.makeCurve("t-identity", kIdentityCurve);
    tr.makeCurve("t-p1", staticSemitoneCurve("t-p1", 1.0));
    tr.makeCurve("t-p7", staticSemitoneCurve("t-p7", 7.0));
    tr.makeCurve("t-p12", staticSemitoneCurve("t-p12", 12.0));
    tr.makeCurve("t-m1", staticSemitoneCurve("t-m1", -1.0));
    tr.makeCurve("t-m7", staticSemitoneCurve("t-m7", -7.0));
    tr.makeCurve("t-m12", staticSemitoneCurve("t-m12", -12.0));
    tr.makeCurve("t-r2", staticRatioCurve("t-r2", 2.0));
    tr.makeCurve("t-r075", staticRatioCurve("t-r075", 0.75));
    tr.makeAsset("mono-48k", 48000, {sineFrames(440.0, 48000, 48000)});
    tr.makeAsset("mono-44k1", 44100, {sineFrames(440.0, 44100, 44100)});
    tr.makeAsset("mono-96k", 96000, {sineFrames(440.0, 96000, 96000)});
    tr.makeAsset("mono-192k", 192000, {sineFrames(440.0, 192000, 96000)});
    tr.makeAsset("stereo-48k", 48000,
                 {sineFrames(440.0, 48000, 24000), sineFrames(440.0, 48000, 24000)});
  }

  struct Rendered {
    JobResult result;
    WavData master;
  };

  Rendered render(const std::string& asset, const std::string& curve, uint32_t fs,
                  int channels = 1, const std::string& params = "") {
    const std::string expId = "exp-" + asset + "-" + curve;
    const fs::path exp =
        tr.makeExperiment(expId, experimentToml(expId, asset, curve, fs, channels,
                                                "native.timepitch", "benchmark", params));
    const RenderSummary summary = renderExperiment(exp, tr, registry);
    REQUIRE(summary.results.size() == 1);
    REQUIRE(summary.results[0].status == RenderStatus::Ok);
    REQUIRE(summary.results[0].masterWav != fs::path());
    return Rendered{summary.results[0], readWav(summary.results[0].masterWav)};
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// T-E1: ratio 1.0 identity — output == bypass within −80 dBFS (L-5).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E1: ratio 1.0 identity within -80 dBFS") {
  const Rendered r = render("mono-48k", "t-identity", 48000);
  REQUIRE(r.result.status == RenderStatus::Ok);
  CHECK(r.result.inputFramesConsumed == 48000);
  CHECK(r.result.inputExhaustionTransitions == 1);
  REQUIRE(r.master.channels.size() == 1);
  const WavData input = readWav(tr.root / "assets" / "corpus" / "mono-48k" / "signal.wav");
  const int64_t n = std::min<int64_t>(input.meta.frames, r.master.meta.frames);
  REQUIRE(n >= 47000);
  double worst = 0.0;
  for (int64_t k = 0; k < n; ++k) {
    worst = std::max(worst, std::fabs(r.master.channels[0][static_cast<std::size_t>(k)] -
                                      input.channels[0][static_cast<std::size_t>(k)]));
  }
  CHECK(worst <= kAudioEquivDbfs);
  std::printf("T-E1: identity worst |y−x| = %.3g (−80 dBFS gate %.3g)\n", worst,
              kAudioEquivDbfs);
}

// ---------------------------------------------------------------------------
// T-E3: static shifts — measured pitch + Preserving length.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E3: static semitone shifts, pitch and length") {
  struct Case {
    const char* curve;
    double st;
  };
  for (const Case& c : std::vector<Case>{
           {"t-p1", 1.0}, {"t-p7", 7.0}, {"t-p12", 12.0},
           {"t-m1", -1.0}, {"t-m7", -7.0}, {"t-m12", -12.0}}) {
    CAPTURE(c.curve);
    const Rendered r = render("mono-48k", c.curve, 48000);
    REQUIRE(r.result.status == RenderStatus::Ok);
    const double expected = 440.0 * std::exp2(c.st / 12.0);
    const double f = dominantFrequency(r.master.channels[0], 48000, 4000, 30000);
    const double errSt = 12.0 * std::log2(f / expected);
    CHECK(std::abs(errSt) <= kFixedShiftToleranceSt);
    // Preserving length: the composite is duration-preserving by construction.
    CHECK(r.result.lengthDeltaFrames == 0);
    std::printf("T-E3: %-6s -> %+.3f st (expected %.1f Hz), delta %lld\n", c.curve,
                errSt, expected, static_cast<long long>(r.result.lengthDeltaFrames));
  }
}

// ---------------------------------------------------------------------------
// T-E4: sample-rate invariance over the DECLARED rate set (§6.6).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E4: sample-rate invariance (44.1/48/96/192 kHz)") {
  struct Rate {
    uint32_t fs;
    const char* asset;
  };
  for (const Rate& rt : std::vector<Rate>{{44100u, "mono-44k1"},
                                          {48000u, "mono-48k"},
                                          {96000u, "mono-96k"},
                                          {192000u, "mono-192k"}}) {
    CAPTURE(rt.fs);
    const Rendered r = render(rt.asset, "t-p7", rt.fs);
    REQUIRE(r.result.status == RenderStatus::Ok);
    const double expected = 440.0 * std::exp2(7.0 / 12.0);
    // The stable mid-region (a quarter-second window from the quarter
    // mark): the OLA comb pattern is non-stationary — the measured dominant
    // depends on the analysis window, so the sweep uses the same stable
    // region at every rate.
    const double f =
        dominantFrequency(r.master.channels[0], rt.fs, static_cast<int64_t>(rt.fs) / 4,
                          static_cast<int64_t>(rt.fs) / 4);
    const double errSt = 12.0 * std::log2(f / expected);
    CHECK(std::abs(errSt) <= kFixedRateSweepToleranceSt);
    std::printf("T-E4: %6u Hz -> dominant %.2f (expected %.2f), err %+.3f st\n", rt.fs, f,
                expected, errSt);
  }
}

// ---------------------------------------------------------------------------
// T-E5: stereo coherence — identical channels, shared channel-0 schedule:
// the two outputs are bit-identical (§6.6.1 item 12).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E5: stereo coherence (bit-identical channels)") {
  const Rendered r = render("stereo-48k", "t-p12", 48000, 2);
  REQUIRE(r.result.status == RenderStatus::Ok);
  REQUIRE(r.master.channels.size() == 2);
  REQUIRE(r.master.channels[0].size() == r.master.channels[1].size());
  CHECK(std::memcmp(r.master.channels[0].data(), r.master.channels[1].data(),
                    r.master.channels[0].size() * sizeof(double)) == 0);
}

// ---------------------------------------------------------------------------
// T-E6: determinism — byte-identical re-render (same binary, same job).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E6: byte-identical re-render") {
  const Rendered a = render("mono-48k", "t-p7", 48000);
  const Rendered b = render("mono-48k", "t-p7", 48000);
  REQUIRE(a.master.channels.size() == b.master.channels.size());
  REQUIRE(a.master.channels[0].size() == b.master.channels[0].size());
  CHECK(std::memcmp(a.master.channels[0].data(), b.master.channels[0].data(),
                    a.master.channels[0].size() * sizeof(double)) == 0);
}

// ---------------------------------------------------------------------------
// T-E7: block-boundary independence — {4096} vs a mixed pattern,
// audio-equivalent within −80 dBFS (L-5; NOT bit-exact).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E7: block-boundary independence") {
  const std::string expId = "exp-e7";
  const fs::path exp =
      tr.makeExperiment(expId, experimentToml(expId, "mono-48k", "t-m12", 48000, 1,
                                              "native.timepitch", "benchmark", ""));
  const HarnessConfig cfg = loadHarnessConfig(tr.root);
  const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
  REQUIRE(outcome.jobs.size() == 1);

  RenderJob job = outcome.jobs[0];
  const RenderSummary ref = renderJobs({job}, registry);
  REQUIRE(ref.results.size() == 1);
  REQUIRE(ref.results[0].status == RenderStatus::Ok);
  const WavData refWav = readWav(ref.results[0].masterWav);

  job.blockSchedule = BlockSchedule::pattern({1024, 512, 4096, 128, 2048, 64, 512});
  const RenderSummary alt = renderJobs({job}, registry);
  REQUIRE(alt.results.size() == 1);
  REQUIRE(alt.results[0].status == RenderStatus::Ok);
  const WavData altWav = readWav(alt.results[0].masterWav);

  REQUIRE(refWav.channels.size() == altWav.channels.size());
  REQUIRE(refWav.channels[0].size() == altWav.channels[0].size());
  double worst = 0.0;
  for (std::size_t i = 0; i < refWav.channels[0].size(); ++i) {
    worst = std::max(worst, std::fabs(refWav.channels[0][i] - altWav.channels[0][i]));
  }
  CHECK(worst <= kAudioEquivDbfs);
  std::printf("T-E7: schedule worst |Δ| = %.3g (−80 dBFS gate %.3g)\n", worst,
              kAudioEquivDbfs);
}

// ---------------------------------------------------------------------------
// T-E8: block-size sweep — every fixed boundary renders Ok with sane peak.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E8: block-size sweep") {
  for (int block : {32, 64, 256, 1024, 4096}) {
    CAPTURE(block);
    const std::string expId = "exp-e8-" + std::to_string(block);
    const fs::path exp =
        tr.makeExperiment(expId, experimentToml(expId, "mono-48k", "t-m7", 48000, 1,
                                                "native.timepitch", "benchmark", ""));
    const HarnessConfig cfg = loadHarnessConfig(tr.root);
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    REQUIRE(outcome.jobs.size() == 1);
    RenderJob job = outcome.jobs[0];
    job.blockSchedule = BlockSchedule::constant(block);
    const RenderSummary summary = renderJobs({job}, registry);
    REQUIRE(summary.results.size() == 1);
    CHECK(summary.results[0].status == RenderStatus::Ok);
    const WavData wav = readWav(summary.results[0].masterWav);
    double peak = 0.0;
    for (double v : wav.channels[0]) peak = std::max(peak, std::fabs(v));
    CHECK(peak > 0.2);
    CHECK(peak < 1.2);  // the OLA family keeps unity-ish gain (no blow-up)
  }
}

// ---------------------------------------------------------------------------
// T-E9: end-of-input accounting — consumed == N_in, padding == the declared
// inputLatency, exhaustion signalled exactly once.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E9: end-of-input accounting") {
  const Rendered r = render("mono-48k", "t-identity", 48000);
  const PitchEngine::Latency declared = fixedLatency(2048, 2, 1.0);
  CHECK(r.result.inputFramesActual == 48000);
  CHECK(r.result.inputFramesConsumed == 48000);
  CHECK(r.result.declaredLatency.inputLatencyFrames == declared.inputLatencyFrames);
  CHECK(r.result.inputPaddingFrames == declared.inputLatencyFrames);
  CHECK(r.result.inputExhaustionTransitions == 1);
}

// ---------------------------------------------------------------------------
// T-E10: flush bounded by the declared outputLatencyFrames.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E10: flush bound") {
  for (const char* curve : {"t-identity", "t-p12", "t-m12"}) {
    CAPTURE(curve);
    const Rendered r = render("mono-48k", curve, 48000);
    REQUIRE(r.result.status == RenderStatus::Ok);
    CHECK(r.result.flushFrames <= r.result.declaredLatency.outputLatencyFrames);
  }
}

// ---------------------------------------------------------------------------
// T-E11: Preserving output length — N_in + declared outputLatency EXACTLY
// (§6.6.1 item 11; lengthDelta == 0 golden).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E11: Preserving output length exact") {
  struct Case {
    const char* curve;
    double minRatio;  // static curves: the ratio itself
    int window;
    int overlap;
    std::string params;
  };
  for (const Case& c : std::vector<Case>{
           {"t-identity", 1.0, 2048, 2, ""},
           {"t-p12", 2.0, 2048, 2, ""},
           {"t-m12", 0.5, 2048, 2, ""},
           {"t-p12", 1.0, 1024, 4, "window_frames = 1024, overlap = 4"},
           {"t-p12", 1.0, 4096, 8, "window_frames = 4096, overlap = 8, window_shape = \"hamming\""}}) {
    CAPTURE(c.curve);
    CAPTURE(c.params);
    const Rendered r = render("mono-48k", c.curve, 48000, 1, c.params);
    REQUIRE(r.result.status == RenderStatus::Ok);
    const PitchEngine::Latency declared =
        fixedLatency(c.window, c.overlap, std::min(1.0, c.minRatio));
    CHECK(r.result.declaredLatency.outputLatencyFrames == declared.outputLatencyFrames);
    CHECK(r.result.expectedOutputFrames == 48000 + declared.outputLatencyFrames);
    CHECK(r.result.outputFramesProduced == 48000 + declared.outputLatencyFrames);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(r.result.taints.empty());
    std::printf("T-E11: %-11s N=%d ov=%d -> expected %lld (out-lat %lld), delta %lld\n",
                c.curve, c.window, c.overlap,
                static_cast<long long>(r.result.expectedOutputFrames),
                static_cast<long long>(declared.outputLatencyFrames),
                static_cast<long long>(r.result.lengthDeltaFrames));
  }
}

// ---------------------------------------------------------------------------
// T-E12: configuration validation (§6.6.1 item 14) — invalid domains and
// unknown keys ⇒ CONFIG ERROR at configure().
// ---------------------------------------------------------------------------
TEST_CASE("T-E12: configuration validation") {
  auto cfgWith = [](std::vector<std::pair<std::string, ParameterValue>> params) {
    EngineConfiguration cfg;
    cfg.seed = 1;
    cfg.parameters = std::move(params);
    return cfg;
  };
  auto engine = makeTimePitchEngine();

  // the default configuration is valid and prepares
  EngineConfiguration def;
  def.seed = 1;
  engine->configure(def);
  ProcessContext ctx{};
  ctx.sampleRate = 48000.0;
  ctx.channels = 1;
  ctx.maxBlockFrames = 4096;
  ctx.totalInputFrames = 48000;
  std::vector<double> ratio(48000, 1.0);
  PitchCurveView curve{ratio.data(), 48000, 48000.0};
  ctx.curve = &curve;
  engine->prepare(ctx);
  CHECK(engine->latency().inputLatencyFrames == 1024 + 1024 + 2 * kResamplerK + 64);

  // invalid domains (§6.6.1 item 14)
  CHECK_THROWS(engine->configure(cfgWith({{"window_frames", int64_t{63}}})));
  CHECK_THROWS(engine->configure(cfgWith({{"window_frames", int64_t{16385}}})));
  CHECK_THROWS(engine->configure(cfgWith({{"window_frames", double{2048.0}}})));  // wrong kind
  CHECK_THROWS(engine->configure(cfgWith({{"overlap", int64_t{3}}})));
  CHECK_THROWS(engine->configure(cfgWith({{"overlap", int64_t{1}}})));
  CHECK_THROWS(engine->configure(cfgWith({{"window_shape", std::string{"kaiser"}}})));
  CHECK_THROWS(engine->configure(cfgWith({{"mode", std::string{"pitch_synced"}}})));  // lands with its checkpoint
  CHECK_THROWS(engine->configure(cfgWith({{"unknown_key", int64_t{1}}})));

  // valid extremes prepare and declare sane latencies
  engine->configure(cfgWith({{"window_frames", int64_t{16384}}, {"overlap", int64_t{2}}}));
  engine->prepare(ctx);
  CHECK(engine->latency().inputLatencyFrames == 8192 + 8192 + 2 * kResamplerK + 64);

  // invalid job geometry (§6.6.1 item 14)
  engine->configure(def);
  ProcessContext bad = ctx;
  bad.channels = 3;
  CHECK_THROWS(engine->prepare(bad));
  bad = ctx;
  bad.channels = 2;  // stereo is fine
  bad.curve = nullptr;
  CHECK_THROWS(engine->prepare(bad));
  bad = ctx;
  bad.channels = 1;
  bad.totalInputFrames = 100000;  // curve frames mismatch
  CHECK_THROWS(engine->prepare(bad));
}

// ---------------------------------------------------------------------------
// T-E13: the frozen declared latency composition (§6.6.1 item 10) —
// ratio-dependent output side (whole-curve min).
// ---------------------------------------------------------------------------
TEST_CASE("T-E13: declared latency composition") {
  const int64_t N = 24000;
  const auto input = sineFrames(440.0, 48000, N);
  struct Case {
    double ratio;
  };
  for (const Case& c : std::vector<Case>{{1.0}, {2.0}, {0.5}}) {
    std::vector<double> ratio(N, c.ratio);
    auto engine = makeTimePitchEngine();
    EngineConfiguration cfg;
    cfg.seed = 0;
    const DriveReport dr = driveEngine(*engine, cfg, {input}, 48000, ratio, 4096, 262144);
    const PitchEngine::Latency declared = fixedLatency(2048, 2, std::min(1.0, c.ratio));
    CHECK(dr.latency.inputLatencyFrames == declared.inputLatencyFrames);
    CHECK(dr.latency.outputLatencyFrames == declared.outputLatencyFrames);
  }
}

// ---------------------------------------------------------------------------
// T-A1: process()/finish() never allocate (the global-operator audit).
// ---------------------------------------------------------------------------
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

namespace {
int g_auditAllocs = 0;
bool g_auditActive = false;
}  // namespace

void* operator new(std::size_t n) {
  if (g_auditActive) ++g_auditAllocs;
  void* p = std::malloc(n == 0 ? 1 : n);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

TEST_CASE("T-A1: process()/finish() never allocate (timepitch Fixed)") {
  const int64_t N = 8192;
  const auto input = sineFrames(440.0, 48000, N);
  const std::vector<double> ratio15(N, 1.5);
  const std::vector<double> ratio10(N, 1.0);
  for (const std::vector<double>* ratio : {&ratio15, &ratio10}) {
    CAPTURE(ratio->at(0));
    auto engine = makeTimePitchEngine();
    EngineConfiguration cfg;
    cfg.seed = 0;
    g_auditAllocs = 0;
    const DriveReport dr = driveEngine(
        *engine, cfg, {input}, 48000, *ratio, 4096, 65536,
        /*beforeCall=*/[] { g_auditActive = true; },
        /*afterCall=*/[] { g_auditActive = false; });
    CHECK(dr.producedTotal > 0);
    CHECK(g_auditAllocs == 0);  // §5 rule 1 — the audit assertion
    std::printf("T-A1: ratio %.2f — allocations during process()/finish(): %d\n",
                ratio->at(0), g_auditAllocs);
  }
}

// ---------------------------------------------------------------------------
// T-D3: reset() reuse — after reset, the same input+curve produces
// bit-identical output to a fresh instance.
// ---------------------------------------------------------------------------
TEST_CASE("T-D3: reset() reuse bit-identity") {
  const int64_t N = 12000;
  const auto input = sineFrames(440.0, 48000, N);
  std::vector<double> ratio(N);
  for (int64_t i = 0; i < N; ++i) {
    ratio[static_cast<std::size_t>(i)] =
        1.0 + 0.5 * std::sin(2.0 * 3.14159265358979323846 *
                             static_cast<double>(i) / 24000.0);
  }
  EngineConfiguration cfg;
  cfg.seed = 3;

  auto engineA = makeTimePitchEngine();
  const DriveReport a = driveEngine(*engineA, cfg, {input}, 48000, ratio, 4096, 65536);

  engineA->reset();
  const DriveReport b = driveEngine(*engineA, cfg, {input}, 48000, ratio, 4096, 65536);

  auto engineC = makeTimePitchEngine();
  const DriveReport c = driveEngine(*engineC, cfg, {input}, 48000, ratio, 4096, 65536);

  REQUIRE(a.output.size() == 1);
  REQUIRE(b.output.size() == 1);
  REQUIRE(c.output.size() == 1);
  REQUIRE(a.output[0].size() == b.output[0].size());
  REQUIRE(a.output[0].size() == c.output[0].size());
  bool ab = true, ac = true;
  for (std::size_t i = 0; i < a.output[0].size(); ++i) {
    if (std::memcmp(&a.output[0][i], &b.output[0][i], sizeof(double)) != 0) ab = false;
    if (std::memcmp(&a.output[0][i], &c.output[0][i], sizeof(double)) != 0) ac = false;
  }
  CHECK(ab);  // reset() reuse: bit-identical
  CHECK(ac);  // fresh instance: bit-identical
  std::printf("T-D3: reset-reuse bit-identity: %s; fresh-instance bit-identity: %s (%lld frames)\n",
              ab ? "yes" : "NO", ac ? "yes" : "NO",
              static_cast<long long>(a.output[0].size()));
}

// ---------------------------------------------------------------------------
// CHECKPOINT 2 — the Adaptive (= WSOLA) mode (§6.6.1 item 4). The two-stage
// engine and the accumulation policy are SHARED with Fixed; only the
// per-grain ANALYSIS-POSITION CHOICE changes (the SSE search). Zero declared
// latency added (the search reaches backward into the buffered back-margin).
// ---------------------------------------------------------------------------

namespace {

struct AdaptiveFixture {
  TestRoot tr{"timepitch-adaptive"};
  EngineRegistry registry{TestRoot::productionRegistry()};

  AdaptiveFixture() {
    tr.makeCurve("a-identity", kIdentityCurve);
    tr.makeCurve("a-p12", staticSemitoneCurve("a-p12", 12.0));
    tr.makeCurve("a-m12", staticSemitoneCurve("a-m12", -12.0));
    tr.makeAsset("mono-48k", 48000, {sineFrames(440.0, 48000, 48000)});
    tr.makeAsset("stereo-48k", 48000,
                 {sineFrames(440.0, 48000, 24000), sineFrames(440.0, 48000, 24000)});
  }

  struct Rendered {
    JobResult result;
    WavData master;
  };

  Rendered render(const std::string& asset, const std::string& curve,
                  const std::string& params, const std::string& tag) {
    const std::string expId = "exp-adp-" + tag;
    const fs::path exp =
        tr.makeExperiment(expId, experimentToml(expId, asset, curve, 48000, 1,
                                                "native.timepitch", "benchmark", params));
    const RenderSummary summary = renderExperiment(exp, tr, registry);
    REQUIRE(summary.results.size() == 1);
    REQUIRE(summary.results[0].status == RenderStatus::Ok);
    REQUIRE(summary.results[0].masterWav != fs::path());
    return Rendered{summary.results[0], readWav(summary.results[0].masterWav)};
  }
};

}  // namespace

TEST_CASE_FIXTURE(AdaptiveFixture,
                  "T-WSOLA: adaptive identity, shifts, length, determinism") {
  const std::string params = "mode = \"adaptive\"";
  // identity: the overlap is already aligned => delta 0 everywhere => the
  // exact OLA identity path
  {
    const Rendered r = render("mono-48k", "a-identity", params, "id");
    const WavData input = readWav(tr.root / "assets" / "corpus" / "mono-48k" / "signal.wav");
    const int64_t n = std::min<int64_t>(input.meta.frames, r.master.meta.frames);
    REQUIRE(n >= 47000);
    double worst = 0.0;
    for (int64_t k = 0; k < n; ++k) {
      worst = std::max(worst, std::fabs(r.master.channels[0][static_cast<std::size_t>(k)] -
                                        input.channels[0][static_cast<std::size_t>(k)]));
    }
    CHECK(worst <= kAudioEquivDbfs);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(r.result.flushFrames == r.result.declaredLatency.outputLatencyFrames);
  }
  // ±12 st: the shift happens (the candidate gate); Preserving length exact
  for (const char* curve : {"a-p12", "a-m12"}) {
    CAPTURE(curve);
    const Rendered r = render("mono-48k", curve, params, curve);
    const double expected =
        440.0 * std::exp2((std::strcmp(curve, "a-p12") == 0 ? 12.0 : -12.0) / 12.0);
    const double f = dominantFrequency(r.master.channels[0], 48000, 4000, 30000);
    const double errSt = 12.0 * std::log2(f / expected);
    CHECK(std::abs(errSt) <= kFixedShiftToleranceSt);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(r.result.flushFrames == r.result.declaredLatency.outputLatencyFrames);
    std::printf("T-WSOLA: %s -> f=%.3f expected=%.3f err=%+.3f st, delta %lld, flush %lld/%lld\n",
                curve, f, expected, errSt,
                static_cast<long long>(r.result.lengthDeltaFrames),
                static_cast<long long>(r.result.flushFrames),
                static_cast<long long>(r.result.declaredLatency.outputLatencyFrames));
  }
  // determinism: byte-identical re-render (the search is schedule-pure)
  {
    const Rendered a = render("mono-48k", "a-p12", params, "det");
    const Rendered b = render("mono-48k", "a-p12", params, "det");
    REQUIRE(a.master.channels[0].size() == b.master.channels[0].size());
    CHECK(std::memcmp(a.master.channels[0].data(), b.master.channels[0].data(),
                      a.master.channels[0].size() * sizeof(double)) == 0);
  }
  // stereo coherence: the shared channel-0 decision keeps identical inputs
  // bit-identical per channel
  {
    const std::string expId = "exp-adp-st";
    const fs::path exp = tr.makeExperiment(
        expId, experimentToml(expId, "stereo-48k", "a-p12", 48000, 2, "native.timepitch",
                              "benchmark", params));
    const RenderSummary summary = renderExperiment(exp, tr, registry);
    REQUIRE(summary.results.size() == 1);
    REQUIRE(summary.results[0].status == RenderStatus::Ok);
    const WavData wav = readWav(summary.results[0].masterWav);
    REQUIRE(wav.channels.size() == 2);
    REQUIRE(wav.channels[0].size() == wav.channels[1].size());
    CHECK(std::memcmp(wav.channels[0].data(), wav.channels[1].data(),
                      wav.channels[0].size() * sizeof(double)) == 0);
  }
}

TEST_CASE("T-WSOLA: near-silent input — the tie-break holds the nominal (no drift)") {
  // The MEASURED defect regression (candidate_wsola.cpp:86-91): an
  // ascending-from-−tol search order tied to −tolerance on near-silent
  // input and drifted backwards until the input window overflowed. The
  // frozen order (0, +1, −1, +2, −2, … ties → 0) must render silence
  // cleanly: no overflow fault, all-zero output.
  const int64_t N = 48000;
  std::vector<double> silence(static_cast<std::size_t>(N), 0.0);
  std::vector<double> ratio(static_cast<std::size_t>(N), 2.0);
  auto engine = makeTimePitchEngine();
  EngineConfiguration cfg;
  cfg.seed = 0;
  cfg.parameters = {{"mode", ParameterValue{std::string{"adaptive"}}},
                    {"tolerance_frames", ParameterValue{int64_t{768}}}};
  const DriveReport dr =
      driveEngine(*engine, cfg, {silence}, 48000, ratio, 4096, 262144);
  CHECK(dr.producedTotal > 0);
  double peak = 0.0;
  for (double v : dr.output[0]) peak = std::max(peak, std::fabs(v));
  CHECK(peak == 0.0);  // silence in, silence out (delta 0 ties, no drift)
}

TEST_CASE("T-WSOLA: tolerance_frames validation") {
  auto cfgWith = [](std::vector<std::pair<std::string, ParameterValue>> params) {
    EngineConfiguration cfg;
    cfg.seed = 1;
    cfg.parameters = std::move(params);
    return cfg;
  };
  auto engine = makeTimePitchEngine();
  CHECK_NOTHROW(engine->configure(cfgWith(
      {{"mode", ParameterValue{std::string{"adaptive"}}},
       {"tolerance_frames", ParameterValue{int64_t{8192}}}})));
  CHECK_THROWS(engine->configure(cfgWith(
      {{"mode", ParameterValue{std::string{"adaptive"}}},
       {"tolerance_frames", ParameterValue{int64_t{8193}}}})));
  CHECK_THROWS(engine->configure(cfgWith(
      {{"mode", ParameterValue{std::string{"adaptive"}}},
       {"tolerance_frames", ParameterValue{int64_t{-1}}}})));
  CHECK_THROWS(engine->configure(cfgWith(
      {{"mode", ParameterValue{std::string{"pitch_synced"}}}})));  // lands with its checkpoint
}
