// T-E1..T-E13 + T-A1 + T-D3 + failure fixtures + seed-sensitivity —
// native.granular engine contract suite through the REAL harness
// (implementation specification §13.1 matrix, cycle 4 / §17 step 4; frozen
// behaviour §6.5.1).
//
// CONTRACT DISTINCTION (owner-locked L-5): engine-level block-boundary and
// identity criteria are AUDIO-EQUIVALENCE within −80 dBFS — NOT bit-exact.
// (granular's cross-schedule bit-identity is an observed stronger property,
// reported but never required — §6.5.1 item 10.)
//
// Matrix implemented here:
//   T-E1  ratio 1.0 identity (the OLA normalisation reconstructs x at the
//          integer read positions — criterion stays −80 dBFS)
//   T-E2  constant ratios 1.5 / 0.75 (pitch + exact Preserving length)
//   T-E3  positive/negative shifts ±1, ±7 st
//   T-E4  sample-rate invariance (44.1/96/192 kHz)
//   T-E5  stereo: SYNCHRONISED grain schedule (incl. jitter ON: identical
//          channels stay bit-identical; per-channel == mono render)
//   T-E6  determinism (byte-identical WAV + manifest re-render)
//   T-E7  block-boundary independence (schedules {4096} vs mixed pattern —
//          audio-equivalent within −80 dBFS)
//   T-E8  block-size sweep {32..4096} + mixed pattern (stability)
//   T-E9  end-of-input accounting (consumed == N_in; padding == G;
//          exhaustion signalled exactly once)
//   T-E10 flush == declared outputLatency == G (equality by construction)
//   T-E11 output-length policy (Preserving: N_in + G EXACTLY across
//          parameter variants: overlap 8, triangular window, jitter on)
//   T-E12 extremes: 0.25x/8x are IN the declared [0.125, 8] ⇒ benchmark
//          RENDERS (finite, no taint); 0.0625x/16x are OUT ⇒ benchmark JOB
//          SKIP ratio-out-of-range; creative ⇒ saturated render
//          (RANGE_SATURATED in the manifest)
//   T-E13 rapid reversal ±12 st/100 ms + short-grain quantisation stress
//   T-A1  allocation audit: process()/finish() never allocate (jitter ON:
//          the grain-indexed RNG draws happen in the processing path)
//   T-D3  reset() reuse bit-identity (the RNG stream re-derived — jitter ON)
//   seed sensitivity: same seed ⇒ identical; different seed ⇒ different
//          output when jitter > 0 (SeededDeterministic is REAL, §19)
//   fixtures: invalid parameters (direct), unknown key (compiler),
//          non-first-class rate (§8.1)

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

#include "engines/granular_engine.h"
#include "test_fixtures.h"

using namespace pitchlab;
using namespace pitchlab::test;
namespace fs = std::filesystem;

namespace {

constexpr double kAudioEquivDbfs = 1.0e-4;  // −80 dBFS full-scale (L-5 provisional)
constexpr double kPitchToleranceCents = 25.0;  // provisional (tolerances.toml)

const char* kIdentityCurve = "id = \"t-identity\"\nkind = \"static\"\nvalue = { ratio = 1.0 }\n";
const char* kRatio15Curve = "id = \"t-r15\"\nkind = \"static\"\nvalue = { ratio = 1.5 }\n";
const char* kRatio075Curve = "id = \"t-r075\"\nkind = \"static\"\nvalue = { ratio = 0.75 }\n";

std::string staticRatioCurve(const std::string& id, double ratio) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "id = \"%s\"\nkind = \"static\"\nvalue = { ratio = %.17g }\n",
                id.c_str(), ratio);
  return buf;
}

std::string staticSemitoneCurve(const std::string& id, double semitones) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "id = \"%s\"\nkind = \"static\"\nvalue = { semitones = %+.17g }\n",
                id.c_str(), semitones);
  return buf;
}

// G at a given rate with the DEFAULT grain_seconds = 0.1 (§6.5.1 item 1).
int64_t defaultG(uint32_t fs) { return static_cast<int64_t>(std::llround(0.1 * fs)); }

struct Fixture {
  TestRoot tr{"granular-contract"};
  EngineRegistry registry{TestRoot::productionRegistry()};

  Fixture() {
    tr.makeCurve("t-identity", kIdentityCurve);
    tr.makeCurve("t-r15", kRatio15Curve);
    tr.makeCurve("t-r075", kRatio075Curve);
    tr.makeCurve("t-p1", staticSemitoneCurve("t-p1", 1.0));
    tr.makeCurve("t-p7", staticSemitoneCurve("t-p7", 7.0));
    tr.makeCurve("t-m1", staticSemitoneCurve("t-m1", -1.0));
    tr.makeCurve("t-m7", staticSemitoneCurve("t-m7", -7.0));
    // 0.25x / 8x are INSIDE the granular declared range (renders); 0.0625x /
    // 16x are OUTSIDE (skip / saturate).
    tr.makeCurve("t-extreme-025", staticRatioCurve("t-extreme-025", 0.25));
    tr.makeCurve("t-extreme-8", staticRatioCurve("t-extreme-8", 8.0));
    tr.makeCurve("t-extreme-00625", staticRatioCurve("t-extreme-00625", 0.0625));
    tr.makeCurve("t-extreme-16", staticRatioCurve("t-extreme-16", 16.0));
    tr.makeCurve("t-reversal",
                 "id = \"t-reversal\"\nkind = \"reversal\"\nfrom = { semitones = +12.0 }\nto = "
                 "{ semitones = -12.0 }\ntime = { ms = 100 }\nhold = { ms = 400 }\n");
    tr.makeAsset("mono-48k", 48000, {sineFrames(440.0, 48000, 48000)});
    tr.makeAsset("mono-44k1", 44100, {sineFrames(440.0, 44100, 44100)});
    tr.makeAsset("mono-96k", 96000, {sineFrames(440.0, 96000, 96000)});
    tr.makeAsset("mono-192k", 192000, {sineFrames(440.0, 192000, 96000)});
    tr.makeAsset("mono32k", 32000, {sineFrames(440.0, 32000, 16000)});
  }

  struct Rendered {
    JobResult result;
    WavData master;
  };

  Rendered render(const std::string& asset, const std::string& curve, uint32_t fs,
                  int channels = 1, const std::string& kind = "benchmark",
                  const std::string& params = "grain_seconds = 0.1") {
    const std::string expId = "exp-gr-" + asset + "-" + curve;
    const fs::path exp = tr.makeExperiment(
        expId, experimentToml(expId, asset, curve, fs, channels, "native.granular", kind, params));
    const RenderSummary summary = renderExperiment(exp, tr, registry);
    REQUIRE(summary.results.size() == 1);
    REQUIRE(summary.results[0].status == RenderStatus::Ok);
    REQUIRE(summary.results[0].masterWav != fs::path());
    return Rendered{summary.results[0], readWav(summary.results[0].masterWav)};
  }
};

std::string readText(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

// ---------------------------------------------------------------------------
// T-E1: ratio = 1.0 identity — the read grid r_k = k·Hg with ratio 1 gives
// every active grain the SAME integer read position n; the local-sum OLA
// then reconstructs x[n] (to the §7.7.1 kernel residues). The CONTRACT
// criterion stays −80 dBFS; the observed error is reported.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E1: ratio 1.0 identity within -80 dBFS") {
  const Rendered r = render("mono-48k", "t-identity", 48000);
  const int64_t G = defaultG(48000);
  REQUIRE(r.result.inputFramesConsumed == 48000);        // exactly the real input
  REQUIRE(r.result.inputPaddingFrames == G);            // declared inputLatency == G
  REQUIRE(r.result.inputExhaustionTransitions == 1);
  const WavData input = readWav(tr.root / "assets" / "corpus" / "mono-48k" / "signal.wav");
  const int64_t n = 48000;  // the real-output region
  REQUIRE(r.master.meta.frames == 48000 + G);
  double maxErr = 0.0;
  for (int64_t k = 0; k < n; ++k) {
    const double e = std::fabs(r.master.channels[0][static_cast<std::size_t>(k)] -
                               input.channels[0][static_cast<std::size_t>(k)]);
    if (e > maxErr) maxErr = e;
  }
  CHECK(maxErr <= kAudioEquivDbfs);
  std::printf("T-E1: identity max |out-in| = %.3e (criterion %.1e; -80 dBFS)\n", maxErr,
              kAudioEquivDbfs);
  // Tail: the last grains read the padding zeros — the OLA of zeros is zero.
  double tailMax = 0.0;
  for (int64_t k = n; k < r.master.meta.frames; ++k) {
    tailMax = std::max(tailMax, std::fabs(r.master.channels[0][static_cast<std::size_t>(k)]));
  }
  CHECK(tailMax <= kAudioEquivDbfs);
  std::printf("T-E1: tail max |out| = %.3e\n", tailMax);
  // Length: Preserving exact — N_in + G, delta == 0, flush == G.
  CHECK(r.result.lengthDeltaFrames == 0);
  CHECK(r.result.flushFrames == G);
  CHECK(r.result.declaredLatency.outputLatencyFrames == G);
  CHECK(r.result.outputFramesProduced == 48000 + G);
}

// ---------------------------------------------------------------------------
// T-E2: constant ratios — measured pitch + exact length policy. At 1.5× the
// reads consume the input by output frame N_in/1.5 (the rest reads zeros —
// Preserving holds the timeline, not the content); measure over the early
// window. At 0.75× the content spans the whole output.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E2: constant ratios 1.5 and 0.75") {
  for (auto [curve, ratio, skip] : std::vector<std::tuple<std::string, double, int64_t>>{
           {std::string("t-r15"), 1.5, 10000}, {std::string("t-r075"), 0.75, 24000}}) {
    CAPTURE(curve);
    const Rendered r = render("mono-48k", curve, 48000);
    CHECK(r.result.lengthDeltaFrames == 0);  // Preserving: EXACT (§4.3.3)
    CHECK(r.result.outputFramesProduced == 48000 + defaultG(48000));
    const int64_t count = (ratio > 1.0) ? 20000 : 20000;
    const double f = dominantFrequency(r.master.channels[0], 48000, skip, count);
    const double cents = centsBetween(f, 440.0 * ratio);
    CHECK(std::abs(cents) <= kPitchToleranceCents);
    std::printf("T-E2: ratio %.2f -> dominant %.2f Hz (expected %.2f; %.2f cents)\n", ratio, f,
                440.0 * ratio, cents);
  }
}

// ---------------------------------------------------------------------------
// T-E3: positive/negative shifts ±1, ±7 st.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E3: shifts +/-1, +/-7 st") {
  const std::vector<std::pair<std::string, double>> shifts = {
      {"t-p1", 1.0}, {"t-p7", 7.0}, {"t-m1", -1.0}, {"t-m7", -7.0}};
  for (const auto& [curve, st] : shifts) {
    CAPTURE(curve);
    const double ratio = std::exp2(st / 12.0);
    const int64_t skip = (ratio > 1.0) ? 8000 : 24000;
    const Rendered r = render("mono-48k", curve, 48000);
    CHECK(r.result.status == RenderStatus::Ok);
    CHECK(r.result.lengthDeltaFrames == 0);
    const double expected = 440.0 * ratio;
    const double f = dominantFrequency(r.master.channels[0], 48000, skip, 16000);
    const double cents = centsBetween(f, expected);
    CHECK(std::abs(cents) <= kPitchToleranceCents);
    std::printf("T-E3: %+g st -> dominant %.2f Hz (expected %.2f; %.2f cents)\n", st, f, expected,
                cents);
  }
}

// ---------------------------------------------------------------------------
// T-E4: sample-rate invariance (44.1 / 96 / 192 kHz pass equivalently).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E4: sample-rate invariance 44.1/96/192 kHz") {
  const std::vector<std::pair<std::string, uint32_t>> rates = {
      {"mono-44k1", 44100u}, {"mono-96k", 96000u}, {"mono-192k", 192000u}};
  for (const auto& [asset, fsHz] : rates) {
    CAPTURE(asset);
    const Rendered r = render(asset, "t-r15", fsHz);
    CHECK(r.result.status == RenderStatus::Ok);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(r.result.inputPaddingFrames == defaultG(fsHz));
    const WavData in = readWav(tr.root / "assets" / "corpus" / asset / "signal.wav");
    const int64_t N = in.meta.frames;
    // The 1.5x read consumes the input by N/1.5; measure over [N/6, N/2).
    const int64_t skip = N / 6;
    const int64_t count = N / 3;
    REQUIRE(count > 8000);
    const double f = dominantFrequency(r.master.channels[0], fsHz, skip, count);
    const double cents = centsBetween(f, 440.0 * 1.5);
    CHECK(std::abs(cents) <= kPitchToleranceCents);
    std::printf("T-E4: %u Hz -> dominant %.2f Hz (expected 660.00; %.2f cents)\n", fsHz, f, cents);
  }
}

// ---------------------------------------------------------------------------
// T-E5: stereo — the SYNCHRONISED grain schedule: identical channels stay
// bit-identical (including with jitter ON — the dither sequence is shared);
// each channel's output equals the independent mono render.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E5: stereo coherence (incl. jitter ON)") {
  const int64_t N = 24000;
  const auto left = sineFrames(440.0, 48000, N);
  const auto right = sineFrames(660.0, 48000, N);
  tr.makeAsset("stereo-48k", 48000, {left, right});
  tr.makeAsset("monoL-48k", 48000, {left});
  tr.makeAsset("monoR-48k", 48000, {right});

  for (const std::string& params :
       {std::string("grain_seconds = 0.05"), std::string("grain_seconds = 0.05, jitter_frames = 96")}) {
    CAPTURE(params);
    const Rendered stereo = render("stereo-48k", "t-r15", 48000, 2, "benchmark", params);
    REQUIRE(stereo.master.meta.channels == 2);
    CHECK(stereo.result.status == RenderStatus::Ok);

    // Coherent correlated pair: identical input channels -> identical output.
    tr.makeAsset("stereoCorr-48k", 48000, {left, left});
    const Rendered corr = render("stereoCorr-48k", "t-identity", 48000, 2, "benchmark", params);
    REQUIRE(corr.master.meta.channels == 2);
    bool identical = corr.master.meta.frames > 0;
    for (int64_t k = 0; k < corr.master.meta.frames && identical; ++k) {
      const double a = corr.master.channels[0][static_cast<std::size_t>(k)];
      const double b = corr.master.channels[1][static_cast<std::size_t>(k)];
      if (std::memcmp(&a, &b, sizeof(double)) != 0) identical = false;
    }
    CHECK(identical);  // the synchronised schedule (§6.5/§6.5.1 item 8)

    // Independent channels: each output == the mono render of that channel.
    const Rendered monoL = render("monoL-48k", "t-r15", 48000, 1, "benchmark", params);
    const Rendered monoR = render("monoR-48k", "t-r15", 48000, 1, "benchmark", params);
    CHECK(stereo.master.meta.frames == monoL.master.meta.frames);
    bool matchL = true, matchR = true;
    for (int64_t k = 0; k < monoL.master.meta.frames; ++k) {
      if (std::memcmp(&stereo.master.channels[0][static_cast<std::size_t>(k)],
                      &monoL.master.channels[0][static_cast<std::size_t>(k)],
                      sizeof(double)) != 0) {
        matchL = false;
      }
      if (std::memcmp(&stereo.master.channels[1][static_cast<std::size_t>(k)],
                      &monoR.master.channels[0][static_cast<std::size_t>(k)],
                      sizeof(double)) != 0) {
        matchR = false;
      }
    }
    CHECK(matchL);
    CHECK(matchR);
    std::printf("T-E5 [%s]: L == mono(L): %s; R == mono(R): %s; correlated pair identical: %s\n",
                params.c_str(), matchL ? "yes" : "NO", matchR ? "yes" : "NO",
                identical ? "yes" : "NO");
  }
}

// ---------------------------------------------------------------------------
// T-E6: determinism — same (input, config, SEED, schedule) twice ⇒
// byte-identical WAV + manifest (fresh root; path-normalised comparison).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E6: byte-identical repeated rendering (jitter ON)") {
  const std::string params = "grain_seconds = 0.05, jitter_frames = 64";
  const fs::path exp = tr.makeExperiment(
      "exp-gr-det", experimentToml("exp-gr-det", "mono-48k", "t-r15", 48000, 1, "native.granular",
                                   "benchmark", params));
  const RenderSummary a = renderExperiment(exp, tr, registry);
  REQUIRE(a.results.size() == 1);
  REQUIRE(a.results[0].status == RenderStatus::Ok);
  const std::string wavHash1 = a.results[0].masterSha256;
  const std::string manifest1 = readText(a.results[0].manifestPath);

  {
    TestRoot tr2{"granular-contract-det2"};
    tr2.makeCurve("t-r15", kRatio15Curve);
    tr2.makeAsset("mono-48k", 48000, {sineFrames(440.0, 48000, 48000)});
    const fs::path exp2 = tr2.makeExperiment(
        "exp-gr-det", experimentToml("exp-gr-det", "mono-48k", "t-r15", 48000, 1, "native.granular",
                                     "benchmark", params));
    const RenderSummary b = renderExperiment(exp2, tr2, registry);
    REQUIRE(b.results.size() == 1);
    REQUIRE(b.results[0].status == RenderStatus::Ok);
    CHECK(b.results[0].masterSha256 == wavHash1);
    std::string m1 = manifest1, m2 = readText(b.results[0].manifestPath);
    const std::string needle1 = tr.root.generic_string();
    const std::string needle2 = tr2.root.generic_string();
    std::string::size_type p;
    while ((p = m1.find(needle1)) != std::string::npos) m1.replace(p, needle1.size(), "<ROOT>");
    while ((p = m2.find(needle2)) != std::string::npos) m2.replace(p, needle2.size(), "<ROOT>");
    CHECK(m1 == m2);
    std::printf("T-E6: WAV sha256 identical across roots; manifest identical after path "
                "normalisation\n");
  }
}

// ---------------------------------------------------------------------------
// Seed sensitivity (§19 granular): same seed ⇒ identical; DIFFERENT seed ⇒
// DIFFERENT output when jitter > 0 (SeededDeterministic is real). With
// jitter == 0 the seed is unused ⇒ same output (recorded property).
// ---------------------------------------------------------------------------
TEST_CASE("seed sensitivity: same/different seed with jitter on/off") {
  const int64_t N = 12000;
  const auto input = sineFrames(440.0, 48000, N);
  const std::vector<double> ratio15(N, 1.5);
  const auto makeCfg = [](uint64_t seed, int64_t jitter) {
    EngineConfiguration cfg;
    cfg.seed = seed;
    cfg.parameters.emplace_back("grain_seconds", 0.02);
    cfg.parameters.emplace_back("jitter_frames", jitter);
    return cfg;
  };

  {  // jitter ON: different seeds -> different outputs; same seed -> identical.
    auto e1 = makeGranularEngine();
    const DriveReport a = driveEngine(*e1, makeCfg(7, 128), {input}, 48000, ratio15, 4096, 65536);
    auto e2 = makeGranularEngine();
    const DriveReport b = driveEngine(*e2, makeCfg(7, 128), {input}, 48000, ratio15, 4096, 65536);
    auto e3 = makeGranularEngine();
    const DriveReport c = driveEngine(*e3, makeCfg(8, 128), {input}, 48000, ratio15, 4096, 65536);
    REQUIRE(a.output.size() == 1);
    REQUIRE(b.output.size() == 1);
    REQUIRE(c.output.size() == 1);
    CHECK(a.producedTotal == b.producedTotal);
    bool sameSeed = a.producedTotal == b.producedTotal;
    for (int64_t k = 0; k < a.producedTotal && sameSeed; ++k) {
      if (std::memcmp(&a.output[0][static_cast<std::size_t>(k)],
                      &b.output[0][static_cast<std::size_t>(k)], sizeof(double)) != 0) {
        sameSeed = false;
      }
    }
    CHECK(sameSeed);
    bool diffSeed = false;
    for (int64_t k = 0; k < c.producedTotal; ++k) {
      if (std::memcmp(&a.output[0][static_cast<std::size_t>(k)],
                      &c.output[0][static_cast<std::size_t>(k)], sizeof(double)) != 0) {
        diffSeed = true;
        break;
      }
    }
    CHECK(diffSeed);
    std::printf("seed sensitivity (jitter 128): same seed identical: %s; different seed differs: %s\n",
                sameSeed ? "yes" : "NO", diffSeed ? "yes" : "NO");
  }
  {  // jitter OFF: the seed is unused -> identical outputs.
    auto e1 = makeGranularEngine();
    const DriveReport a = driveEngine(*e1, makeCfg(7, 0), {input}, 48000, ratio15, 4096, 65536);
    auto e2 = makeGranularEngine();
    const DriveReport b = driveEngine(*e2, makeCfg(99, 0), {input}, 48000, ratio15, 4096, 65536);
    REQUIRE(a.output.size() == 1);
    REQUIRE(b.output.size() == 1);
    bool identical = a.producedTotal == b.producedTotal;
    for (int64_t k = 0; k < a.producedTotal && identical; ++k) {
      if (std::memcmp(&a.output[0][static_cast<std::size_t>(k)],
                      &b.output[0][static_cast<std::size_t>(k)], sizeof(double)) != 0) {
        identical = false;
      }
    }
    CHECK(identical);
    std::printf("seed sensitivity (jitter 0): outputs identical across seeds (seed unused): %s\n",
                identical ? "yes" : "NO");
  }
}

// ---------------------------------------------------------------------------
// T-E7: block-boundary independence — schedules {4096} vs a mixed pattern:
// AUDIO-EQUIVALENT within −80 dBFS (L-5). Bit-exactness is NOT required.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E7: block-boundary independence (-80 dBFS)") {
  const int64_t N = 24000;
  tr.makeAsset("mono24k-48k", 48000, {sineFrames(440.0, 48000, N)});

  auto renderWithSchedule = [&](BlockSchedule sched) {
    const fs::path exp = tr.makeExperiment(
        "exp-gr-e7", experimentToml("exp-gr-e7", "mono24k-48k", "t-r15", 48000, 1,
                                    "native.granular", "benchmark", "grain_seconds = 0.05"));
    const HarnessConfig cfg = loadHarnessConfig(tr.root);
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    REQUIRE(outcome.jobs.size() == 1);
    RenderJob job = outcome.jobs[0];
    job.blockSchedule = sched;
    const RenderSummary summary = renderJobs({job}, registry);
    REQUIRE(summary.results.size() == 1);
    REQUIRE(summary.results[0].status == RenderStatus::Ok);
    return readWav(summary.results[0].masterWav);
  };

  const WavData a = renderWithSchedule(BlockSchedule::constant(4096));
  const WavData b = renderWithSchedule(
      BlockSchedule::pattern({1024, 4096, 512, 32, 2048, 64, 128, 256}));
  REQUIRE(a.meta.frames == b.meta.frames);
  double maxDiff = 0.0;
  for (int64_t k = 0; k < a.meta.frames; ++k) {
    const double d = std::fabs(a.channels[0][static_cast<std::size_t>(k)] -
                               b.channels[0][static_cast<std::size_t>(k)]);
    if (d > maxDiff) maxDiff = d;
  }
  CHECK(maxDiff <= kAudioEquivDbfs);
  std::printf("T-E7: schedules {4096} vs {1024,4096,512,32,2048,64,128,256}: max diff %.3e "
              "(criterion %.1e)\n",
              maxDiff, kAudioEquivDbfs);
}

// ---------------------------------------------------------------------------
// T-E8: block-size variation — schedule sweep renders valid, stable output.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E8: block-size sweep 32..4096") {
  const int64_t N = 24000;
  tr.makeAsset("mono8-48k", 48000, {sineFrames(440.0, 48000, N)});
  const fs::path exp = tr.makeExperiment(
      "exp-gr-e8", experimentToml("exp-gr-e8", "mono8-48k", "t-r15", 48000, 1, "native.granular",
                                  "benchmark", "grain_seconds = 0.05"));
  const HarnessConfig cfg = loadHarnessConfig(tr.root);

  for (int64_t bs : {32, 64, 128, 256, 512, 1024, 2048, 4096}) {
    CAPTURE(bs);
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    REQUIRE(outcome.jobs.size() == 1);
    RenderJob job = outcome.jobs[0];
    job.blockSchedule = BlockSchedule::constant(bs);
    const RenderSummary summary = renderJobs({job}, registry);
    REQUIRE(summary.results.size() == 1);
    const JobResult& r = summary.results[0];
    CHECK(r.status == RenderStatus::Ok);
    CHECK(r.failureReason.empty());
    CHECK(r.lengthDeltaFrames == 0);  // Preserving exact at every block size
    CHECK(r.inputFramesConsumed == N);
    CHECK(r.inputExhaustionTransitions == 1);
    const WavData master = readWav(r.masterWav);
    double peak = 0.0;
    for (double v : master.channels[0]) peak = std::max(peak, std::fabs(v));
    CHECK(peak < 1.0);
  }
  {
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    RenderJob job = outcome.jobs[0];
    job.blockSchedule = BlockSchedule::pattern({32, 4096, 512, 128, 2048});
    const RenderSummary summary = renderJobs({job}, registry);
    REQUIRE(summary.results.size() == 1);
    CHECK(summary.results[0].status == RenderStatus::Ok);
    CHECK(summary.results[0].lengthDeltaFrames == 0);
  }
}

// ---------------------------------------------------------------------------
// T-E9: end-of-input — engine consumes exactly the real input + the G
// padding; exhaustion signalled exactly once.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E9: end-of-input accounting") {
  const Rendered r = render("mono-48k", "t-r15", 48000);
  CHECK(r.result.inputFramesActual == 48000);
  CHECK(r.result.inputFramesConsumed == 48000);        // REAL input only (§4.3.6)
  CHECK(r.result.inputPaddingFrames == defaultG(48000));  // == G
  CHECK(r.result.inputExhaustionTransitions == 1);     // exactly once (§4.2.1 item 7)
  CHECK(r.result.declaredLatency.inputLatencyFrames == defaultG(48000));
  const Rendered id = render("mono-48k", "t-identity", 48000);
  CHECK(id.result.inputPaddingFrames == defaultG(48000));
}

// ---------------------------------------------------------------------------
// T-E10: flush — EQUAL to the declared outputLatencyFrames (G) by
// construction (§6.5.1 item 7); the renderer's ≤-bound check passes with
// equality. The over-flush FAILURE fixture is the engine-independent dummy
// in the varispeed suite; not duplicated here.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E10: flush == declared outputLatency == G") {
  const Rendered r = render("mono-48k", "t-r15", 48000, 1, "benchmark", "grain_seconds = 0.05");
  CHECK(r.result.flushFrames == r.result.declaredLatency.outputLatencyFrames);
  CHECK(r.result.flushFrames == 2400);  // 0.05 s at 48 kHz
  CHECK(r.result.lengthDeltaFrames == 0);
}

// ---------------------------------------------------------------------------
// T-E11: output-length policy — Preserving: N_in + G EXACTLY (golden),
// across parameter variants (overlap 8, triangular window, jitter on).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E11: output length == N_in + G exactly") {
  struct Case {
    std::string curve;
    std::string params;
    int64_t G;
  };
  for (const Case& c : std::vector<Case>{
           {"t-identity", "grain_seconds = 0.1", 4800},
           {"t-r15", "grain_seconds = 0.1", 4800},
           {"t-r075", "grain_seconds = 0.02", 960},
           {"t-r15", "grain_seconds = 0.02, overlap = 8", 960},
           {"t-r15", "grain_seconds = 0.02, window = \"triangular\"", 960},
           {"t-r15", "grain_seconds = 0.02, jitter_frames = 64", 960}}) {
    CAPTURE(c.curve);
    CAPTURE(c.params);
    const Rendered r = render("mono-48k", c.curve, 48000, 1, "benchmark", c.params);
    CHECK(r.result.expectedOutputFrames == 48000 + c.G);
    CHECK(r.result.outputFramesProduced == 48000 + c.G);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(!r.result.lengthPolicyTaint);
    CHECK(r.result.taints.empty());
  }
}

// ---------------------------------------------------------------------------
// T-E12: extremes — 0.25x/8x are INSIDE the declared [0.125, 8] ⇒ benchmark
// RENDERS (finite, no taints); 0.0625x/16x are OUTSIDE ⇒ benchmark JOB SKIP
// ratio-out-of-range; creative ⇒ saturated render with RANGE_SATURATED in
// the manifest (§4.4.6).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E12: extremes — in-range render / out-of-range skip+saturation") {
  const HarnessConfig cfg = loadHarnessConfig(tr.root);

  // In-range extremes: benchmark renders (finite, exact length, no taints).
  for (const char* curveC : {"t-extreme-025", "t-extreme-8"}) {
    const std::string curve(curveC);
    CAPTURE(curve);
    const Rendered r = render("mono-48k", curve, 48000);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(r.result.taints.empty());
    double peak = 0.0;
    for (double v : r.master.channels[0]) peak = std::max(peak, std::fabs(v));
    CHECK(std::isfinite(peak));
    std::printf("T-E12: %s (IN range) benchmark rendered, peak %.3f\n", curve.c_str(), peak);
  }

  // Out-of-range: benchmark skips.
  for (const char* curveC : {"t-extreme-00625", "t-extreme-16"}) {
    const std::string curve(curveC);
    CAPTURE(curve);
    const fs::path exp = tr.makeExperiment(
        "exp-gr-e12-" + curve,
        experimentToml("exp-gr-e12", "mono-48k", curve, 48000, 1, "native.granular", "benchmark",
                       "grain_seconds = 0.1"));
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    REQUIRE(outcome.jobs.empty());
    REQUIRE(outcome.skips.size() == 1);
    CHECK(outcome.skips[0].reason == "ratio-out-of-range");
    CHECK(outcome.skips[0].engineId == "native.granular");
  }

  // Out-of-range creative: saturated render (effective curve inside [0.125, 8]).
  for (const char* curveC : {"t-extreme-00625", "t-extreme-16"}) {
    const std::string curve(curveC);
    CAPTURE(curve);
    const fs::path exp = tr.makeExperiment(
        "exp-gr-e12c-" + curve,
        experimentToml("exp-gr-e12c", "mono-48k", curve, 48000, 1, "native.granular", "creative",
                       "grain_seconds = 0.1"));
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    REQUIRE(outcome.jobs.size() == 1);
    CHECK(outcome.jobs[0].saturated);
    double effMin = 1e9, effMax = -1e9;
    for (double v : outcome.jobs[0].effectiveSignal->ratios) {
      effMin = std::min(effMin, v);
      effMax = std::max(effMax, v);
    }
    CHECK(effMin >= 0.125);
    CHECK(effMax <= 8.0);
    const RenderSummary summary = renderJobs(outcome.jobs, registry);
    REQUIRE(summary.results.size() == 1);
    const JobResult& r = summary.results[0];
    CHECK(r.status == RenderStatus::Ok);
    CHECK(r.lengthDeltaFrames == 0);
    const std::string manifest = readText(r.manifestPath);
    CHECK(manifest.find("RANGE_SATURATED") != std::string::npos);
    std::printf("T-E12: %s creative saturated -> effective [%.4f, %.4f], %lld frames, ok\n",
                curve.c_str(), effMin, effMax, static_cast<long long>(r.outputFramesProduced));
  }
}

// ---------------------------------------------------------------------------
// T-E13: rapid reversal ±12 st/100 ms — the per-grain quantisation is the
// declared characteristic; finite output, exact length, no failure.
// Includes a short-grain variant (G = 240, hop 60 ⇒ 800 grains) and the
// jittered variant.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E13: rapid reversal + short-grain stress") {
  for (const std::string& params :
       {std::string("grain_seconds = 0.05"),
        std::string("grain_seconds = 0.005"),
        std::string("grain_seconds = 0.005, jitter_frames = 32")}) {
    CAPTURE(params);
    const Rendered r = render("mono-48k", "t-reversal", 48000, 1, "benchmark", params);
    CHECK(r.result.status == RenderStatus::Ok);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(r.result.inputExhaustionTransitions == 1);
    double peak = 0.0;
    for (double v : r.master.channels[0]) peak = std::max(peak, std::fabs(v));
    CHECK(std::isfinite(peak));
    CHECK(peak < 1.0);
    std::printf("T-E13: reversal with [%s]: ok, peak %.3f\n", params.c_str(), peak);
  }
}

// ---------------------------------------------------------------------------
// Failure fixtures: invalid parameter values (direct engine drives).
// §6.5.1 item 11 — ConfigError from configure()/prepare().
// ---------------------------------------------------------------------------
TEST_CASE("fixtures: invalid granular parameters (CONFIG ERROR)") {
  auto engine = makeGranularEngine();
  EngineConfiguration cfg;
  cfg.seed = 7;

  auto checkThrows = [&](const char* field, auto addParam) {
    EngineConfiguration c = cfg;
    addParam(c);
    bool threw = false;
    try {
      engine->configure(c);
    } catch (const ConfigError& e) {
      threw = true;
      CHECK(std::string(e.what()).find(field) != std::string::npos);
    }
    CHECK(threw);
  };

  checkThrows("grain_seconds", [](EngineConfiguration& c) {
    c.parameters.emplace_back("grain_seconds", -0.1);
  });
  checkThrows("grain_seconds", [](EngineConfiguration& c) {
    c.parameters.emplace_back("grain_seconds", 0.0);
  });
  checkThrows("grain_seconds", [](EngineConfiguration& c) {
    c.parameters.emplace_back("grain_seconds", std::nan(""));
  });
  checkThrows("overlap", [](EngineConfiguration& c) {
    c.parameters.emplace_back("overlap", static_cast<int64_t>(2));  // G < 4·Hg (§6.5 sheet)
  });
  checkThrows("overlap", [](EngineConfiguration& c) {
    c.parameters.emplace_back("overlap", 4.0);  // real, not integer
  });
  checkThrows("window", [](EngineConfiguration& c) {
    c.parameters.emplace_back("window", std::string("hamming"));
  });
  checkThrows("jitter_frames", [](EngineConfiguration& c) {
    c.parameters.emplace_back("jitter_frames", static_cast<int64_t>(-1));
  });
  checkThrows("jitter_frames", [](EngineConfiguration& c) {
    c.parameters.emplace_back("jitter_frames", 32.5);
  });

  // prepare-time: G < 4 (1e-9 s at 48000) and Hg < 1 (overlap 10000).
  auto checkPrepare = [&](auto addParam, const char* field) {
    auto e = makeGranularEngine();
    EngineConfiguration c = cfg;
    addParam(c);
    e->configure(c);
    ProcessContext ctx;
    ctx.sampleRate = 48000.0;
    ctx.channels = 1;
    ctx.maxBlockFrames = 4096;
    ctx.totalInputFrames = 48000;
    PitchCurveView view;
    std::vector<double> ratio(48000, 1.0);
    view.ratio = ratio.data();
    view.frames = 48000;
    view.sampleRate = 48000.0;
    ctx.curve = &view;
    bool threw = false;
    try {
      e->prepare(ctx);
    } catch (const ConfigError& e2) {
      threw = true;
      CHECK(std::string(e2.what()).find(field) != std::string::npos);
    }
    CHECK(threw);
  };
  checkPrepare([](EngineConfiguration& c) { c.parameters.emplace_back("grain_seconds", 1e-9); },
               "grain_seconds");
  checkPrepare([](EngineConfiguration& c) {
                 c.parameters.emplace_back("grain_seconds", 0.01);
                 c.parameters.emplace_back("overlap", static_cast<int64_t>(10000));
               },
               "overlap");
}

// ---------------------------------------------------------------------------
// Failure fixtures through the compiler: unknown parameter key ⇒ CONFIG
// ERROR; non-first-class rate ⇒ CONFIG ERROR (§8.1 gate).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "fixtures: unknown key + non-first-class rate") {
  const HarnessConfig cfg = loadHarnessConfig(tr.root);
  {
    const fs::path exp = tr.makeExperiment(
        "exp-gr-badkey",
        experimentToml("exp-gr-badkey", "mono-48k", "t-identity", 48000, 1, "native.granular",
                       "benchmark", "grain_len = 0.1"));
    bool threw = false;
    try {
      const CompileOutcome outcome =
          compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
      (void)outcome;  // the key-set validation must throw before returning
    } catch (const ConfigError& e) {
      threw = true;
      CHECK(std::string(e.what()).find("grain_len") != std::string::npos);
    }
    CHECK(threw);
  }
  {
    const fs::path exp = tr.makeExperiment(
        "exp-gr-32k",
        experimentToml("exp-gr-32k", "mono32k", "t-identity", 32000, 1, "native.granular",
                       "benchmark", "grain_seconds = 0.1"));
    bool threw = false;
    try {
      const CompileOutcome outcome =
          compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
      (void)outcome;  // the §8.1 gate must throw before returning
    } catch (const ConfigError& e) {
      threw = true;
      CHECK(std::string(e.what()).find("32000") != std::string::npos);
    }
    CHECK(threw);
  }
}

// ---------------------------------------------------------------------------
// T-A1: allocation audit — process()/finish() never allocate (§5 rule 1;
// the grain-indexed RNG draws happen in the processing path — they are
// state mutations, not allocations).
// ---------------------------------------------------------------------------
namespace {
std::size_t g_auditAllocs = 0;
bool g_auditActive = false;
}  // namespace

// GCC's -Wmismatched-new-delete cannot see the whole-binary interposition
// (every allocation funnels through THESE operators at link time), so its
// analysis produces false positives here. Empirical evidence: it fires on
// BOTH compilers we build with — local GCC 14.2 AND the canonical CI
// GCC 13.3.0 (run 36269701944: 18 analysis paths, all at the sized
// operator delete — the earlier "GCC 13 does not have the warning"
// assumption was wrong and cost that run). Suppressed for ALL GCC versions
// (not Clang: not observed there, and this audit is GCC-centric anyway).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

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

TEST_CASE("T-A1: process()/finish() never allocate (granular, jitter ON + identity)") {
  const int64_t N = 16384;
  const auto input = sineFrames(440.0, 48000, N);
  const std::vector<double> ratio15(N, 1.5);
  const std::vector<double> ratio10(N, 1.0);

  for (const std::vector<double>* ratio : {&ratio15, &ratio10}) {
    CAPTURE(ratio->at(0));
    auto engine = makeGranularEngine();
    EngineConfiguration cfg;
    cfg.seed = 42;
    cfg.parameters.emplace_back("grain_seconds", 0.02);
    cfg.parameters.emplace_back("jitter_frames", static_cast<int64_t>(64));
    g_auditAllocs = 0;
    const DriveReport dr = driveEngine(
        *engine, cfg, {input}, 48000, *ratio, 4096, 65536,
        /*beforeCall=*/[] { g_auditActive = true; },
        /*afterCall=*/[] { g_auditActive = false; });
    CHECK(dr.producedTotal > 0);
    CHECK(g_auditAllocs == 0);  // §5 rule 1 — the audit assertion
    std::printf("T-A1: ratio %.2f (jitter 64) — allocations during process()/finish(): %zu\n",
                ratio->at(0), g_auditAllocs);
  }
}

// ---------------------------------------------------------------------------
// T-D3: reset() reuse — after reset, the same input+curve+SEED produces
// bit-identical output to a fresh instance (the RNG stream re-derived).
// ---------------------------------------------------------------------------
TEST_CASE("T-D3: reset() reuse bit-identity (jitter ON)") {
  const int64_t N = 12000;
  const auto input = sineFrames(440.0, 48000, N);
  std::vector<double> ratio(N);
  for (int64_t i = 0; i < N; ++i) {
    ratio[static_cast<std::size_t>(i)] =
        1.0 + 0.5 * std::sin(2.0 * 3.14159265358979323846 * 240.0 * static_cast<double>(i) /
                             48000.0);
  }
  EngineConfiguration cfg;
  cfg.seed = 123;
  cfg.parameters.emplace_back("grain_seconds", 0.02);
  cfg.parameters.emplace_back("jitter_frames", static_cast<int64_t>(64));

  auto engine = makeGranularEngine();
  const DriveReport first = driveEngine(*engine, cfg, {input}, 48000, ratio, 4096, 65536);
  engine->reset();
  const DriveReport second = driveEngine(*engine, cfg, {input}, 48000, ratio, 4096, 65536);
  REQUIRE(first.output.size() == 1);
  REQUIRE(second.output.size() == 1);
  CHECK(first.producedTotal == second.producedTotal);
  CHECK(first.flushFrames == second.flushFrames);
  bool identical = first.producedTotal == second.producedTotal;
  for (int64_t k = 0; k < first.producedTotal && identical; ++k) {
    if (std::memcmp(&first.output[0][static_cast<std::size_t>(k)],
                    &second.output[0][static_cast<std::size_t>(k)],
                    sizeof(double)) != 0) {
      identical = false;
    }
  }
  CHECK(identical);
  std::printf("T-D3: reset reuse bit-identity (jitter ON): %s (%lld frames)\n",
              identical ? "yes" : "NO", static_cast<long long>(first.producedTotal));
}
