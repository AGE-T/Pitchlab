// T-E1..T-E13 + T-A1 + T-D3 + failure fixtures — native.pv.classic engine
// contract suite through the REAL harness (implementation specification
// §13.1 matrix, cycle 4 / §17 step 4; frozen behaviour §6.3.1).
//
// CONTRACT DISTINCTION (owner-locked L-5): engine-level block-boundary and
// identity criteria are AUDIO-EQUIVALENCE within −80 dBFS — NOT bit-exact.
// (pv.classic's cross-schedule bit-identity is an observed stronger property,
// reported but never required — §6.3.1 item 13.)
//
// T-E1 note (documented edge behaviour, not a hidden skip): the first output
// frame reads the 0/0 window-product guard (wP(0) = 0 — the Hann zero
// endpoint) and is emitted as exactly 0; the comparison region starts after
// that single frame. The full-range error is ALSO reported.
//
// T-A1 is the critical pocketfft audit: the persistent plan's exec() must
// not allocate in the processing path (the header's convenience functions
// DO and are not used — §6.3.1 item 16).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

#include "engines/pv_classic_engine.h"
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

struct Fixture {
  TestRoot tr{"pv-classic-contract"};
  EngineRegistry registry{TestRoot::productionRegistry()};

  Fixture() {
    tr.makeCurve("t-identity", kIdentityCurve);
    tr.makeCurve("t-r15", kRatio15Curve);
    tr.makeCurve("t-r075", kRatio075Curve);
    tr.makeCurve("t-p1", staticSemitoneCurve("t-p1", 1.0));
    tr.makeCurve("t-p7", staticSemitoneCurve("t-p7", 7.0));
    tr.makeCurve("t-m1", staticSemitoneCurve("t-m1", -1.0));
    tr.makeCurve("t-m7", staticSemitoneCurve("t-m7", -7.0));
    tr.makeCurve("t-extreme-025", staticRatioCurve("t-extreme-025", 0.25));  // == minRatio (IN)
    tr.makeCurve("t-extreme-00625", staticRatioCurve("t-extreme-00625", 0.0625));
    tr.makeCurve("t-extreme-8", staticRatioCurve("t-extreme-8", 8.0));
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
                  const std::string& params = "") {
    const std::string expId = "exp-pvc-" + asset + "-" + curve;
    const fs::path exp = tr.makeExperiment(
        expId, experimentToml(expId, asset, curve, fs, channels, "native.pv.classic", kind,
                              params));
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
// T-E1: ratio = 1.0 identity — the phase propagation is exact at rho = 1 and
// the window-product normalisation reconstructs x; the first frame is the
// documented 0/0-guard edge. Criterion: −80 dBFS over [1, N_in); the
// full-range error also reported.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E1: ratio 1.0 identity within -80 dBFS") {
  const Rendered r = render("mono-48k", "t-identity", 48000);
  const int64_t N = 2048, H = 512;
  REQUIRE(r.result.inputFramesConsumed == 48000);
  REQUIRE(r.result.inputPaddingFrames == N + H);
  REQUIRE(r.result.inputExhaustionTransitions == 1);
  const WavData input = readWav(tr.root / "assets" / "corpus" / "mono-48k" / "signal.wav");
  REQUIRE(r.master.meta.frames == 48000 + N);
  double maxErr = 0.0, maxErrFull = 0.0;
  for (int64_t k = 0; k < 48000; ++k) {
    const double e = std::fabs(r.master.channels[0][static_cast<std::size_t>(k)] -
                               input.channels[0][static_cast<std::size_t>(k)]);
    if (e > maxErrFull) maxErrFull = e;
    if (k >= 1 && e > maxErr) maxErr = e;
  }
  CHECK(maxErr <= kAudioEquivDbfs);
  std::printf("T-E1: identity max |out-in| = %.3e on [1, N) (criterion %.1e); full-range %.3e "
              "(frame 0 = the documented 0/0 window-product guard)\n",
              maxErr, kAudioEquivDbfs, maxErrFull);
  CHECK(r.result.lengthDeltaFrames == 0);
  CHECK(r.result.flushFrames == N);
  CHECK(r.result.declaredLatency.outputLatencyFrames == N);
  CHECK(r.result.outputFramesProduced == 48000 + N);
}

// ---------------------------------------------------------------------------
// T-E2: constant ratios — measured pitch + exact length policy.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E2: constant ratios 1.5 and 0.75") {
  for (auto [curve, ratio] : std::vector<std::pair<std::string, double>>{
           {std::string("t-r15"), 1.5}, {std::string("t-r075"), 0.75}}) {
    CAPTURE(curve);
    const Rendered r = render("mono-48k", curve, 48000);
    CHECK(r.result.lengthDeltaFrames == 0);  // Preserving: EXACT (§4.3.3)
    CHECK(r.result.outputFramesProduced == 48000 + 2048);
    const double f = dominantFrequency(r.master.channels[0], 48000, 4000, 30000);
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
    const Rendered r = render("mono-48k", curve, 48000);
    CHECK(r.result.status == RenderStatus::Ok);
    CHECK(r.result.lengthDeltaFrames == 0);
    const double expected = 440.0 * std::exp2(st / 12.0);
    const double f = dominantFrequency(r.master.channels[0], 48000, 4000, 30000);
    const double cents = centsBetween(f, expected);
    CHECK(std::abs(cents) <= kPitchToleranceCents);
    std::printf("T-E3: %+g st -> dominant %.2f Hz (expected %.2f; %.2f cents)\n", st, f, expected,
                cents);
  }
}

// ---------------------------------------------------------------------------
// T-E4: sample-rate invariance (44.1 / 96 / 192 kHz).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E4: sample-rate invariance 44.1/96/192 kHz") {
  const std::vector<std::pair<std::string, uint32_t>> rates = {
      {"mono-44k1", 44100u}, {"mono-96k", 96000u}, {"mono-192k", 192000u}};
  for (const auto& [asset, fsHz] : rates) {
    CAPTURE(asset);
    const Rendered r = render(asset, "t-r15", fsHz);
    CHECK(r.result.status == RenderStatus::Ok);
    CHECK(r.result.lengthDeltaFrames == 0);
    const WavData in = readWav(tr.root / "assets" / "corpus" / asset / "signal.wav");
    const int64_t Nf = in.meta.frames;
    const int64_t skip = Nf / 8;
    const int64_t count = Nf / 2;
    REQUIRE(count > 8000);
    const double f = dominantFrequency(r.master.channels[0], fsHz, skip, count);
    const double cents = centsBetween(f, 440.0 * 1.5);
    CHECK(std::abs(cents) <= kPitchToleranceCents);
    std::printf("T-E4: %u Hz -> dominant %.2f Hz (expected 660.00; %.2f cents)\n", fsHz, f, cents);
  }
}

// ---------------------------------------------------------------------------
// T-E5: stereo — per-channel INDEPENDENT phase state: identical input
// channels evolve identically (bit-identical outputs); each channel equals
// the independent mono render (§6.3.1 item 12).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E5: stereo (independent per-channel phase state)") {
  const int64_t N = 24000;
  const auto left = sineFrames(440.0, 48000, N);
  const auto right = sineFrames(660.0, 48000, N);
  tr.makeAsset("stereo-48k", 48000, {left, right});
  tr.makeAsset("monoL-48k", 48000, {left});
  tr.makeAsset("monoR-48k", 48000, {right});

  const Rendered stereo = render("stereo-48k", "t-r15", 48000, 2);
  REQUIRE(stereo.master.meta.channels == 2);
  CHECK(stereo.result.status == RenderStatus::Ok);

  tr.makeAsset("stereoCorr-48k", 48000, {left, left});
  const Rendered corr = render("stereoCorr-48k", "t-identity", 48000, 2);
  REQUIRE(corr.master.meta.channels == 2);
  bool identical = corr.master.meta.frames > 0;
  for (int64_t k = 0; k < corr.master.meta.frames && identical; ++k) {
    const double a = corr.master.channels[0][static_cast<std::size_t>(k)];
    const double b = corr.master.channels[1][static_cast<std::size_t>(k)];
    if (std::memcmp(&a, &b, sizeof(double)) != 0) identical = false;
  }
  CHECK(identical);  // identical inputs + identical initial state => coherent

  const Rendered monoL = render("monoL-48k", "t-r15", 48000, 1);
  const Rendered monoR = render("monoR-48k", "t-r15", 48000, 1);
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
  std::printf("T-E5: L == mono(L): %s; R == mono(R): %s; correlated pair identical: %s\n",
              matchL ? "yes" : "NO", matchR ? "yes" : "NO", identical ? "yes" : "NO");
}

// ---------------------------------------------------------------------------
// T-E6: determinism — byte-identical WAV + manifest re-render.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E6: byte-identical repeated rendering") {
  const fs::path exp = tr.makeExperiment(
      "exp-pvc-det", experimentToml("exp-pvc-det", "mono-48k", "t-r15", 48000, 1,
                                    "native.pv.classic", "benchmark", ""));
  const RenderSummary a = renderExperiment(exp, tr, registry);
  REQUIRE(a.results.size() == 1);
  REQUIRE(a.results[0].status == RenderStatus::Ok);
  const std::string wavHash1 = a.results[0].masterSha256;
  const std::string manifest1 = readText(a.results[0].manifestPath);

  {
    TestRoot tr2{"pv-classic-contract-det2"};
    tr2.makeCurve("t-r15", kRatio15Curve);
    tr2.makeAsset("mono-48k", 48000, {sineFrames(440.0, 48000, 48000)});
    const fs::path exp2 = tr2.makeExperiment(
        "exp-pvc-det", experimentToml("exp-pvc-det", "mono-48k", "t-r15", 48000, 1,
                                      "native.pv.classic", "benchmark", ""));
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
// T-E7: block-boundary independence — schedules {4096} vs a mixed pattern:
// AUDIO-EQUIVALENT within −80 dBFS (L-5).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E7: block-boundary independence (-80 dBFS)") {
  const int64_t N = 24000;
  tr.makeAsset("mono24k-48k", 48000, {sineFrames(440.0, 48000, N)});

  auto renderWithSchedule = [&](BlockSchedule sched) {
    const fs::path exp = tr.makeExperiment(
        "exp-pvc-e7", experimentToml("exp-pvc-e7", "mono24k-48k", "t-r15", 48000, 1,
                                     "native.pv.classic", "benchmark", ""));
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
      "exp-pvc-e8", experimentToml("exp-pvc-e8", "mono8-48k", "t-r15", 48000, 1,
                                   "native.pv.classic", "benchmark", ""));
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
    CHECK(r.lengthDeltaFrames == 0);
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
// T-E9: end-of-input — consumed == N_in; padding == N + H; exhaustion once.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E9: end-of-input accounting") {
  const Rendered r = render("mono-48k", "t-r15", 48000);
  CHECK(r.result.inputFramesActual == 48000);
  CHECK(r.result.inputFramesConsumed == 48000);
  CHECK(r.result.inputPaddingFrames == 2048 + 512);
  CHECK(r.result.inputExhaustionTransitions == 1);
  CHECK(r.result.declaredLatency.inputLatencyFrames == 2048 + 512);
  const Rendered id = render("mono-48k", "t-identity", 48000);
  CHECK(id.result.inputPaddingFrames == 2048 + 512);
}

// ---------------------------------------------------------------------------
// T-E10: flush == declared outputLatency == N.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E10: flush == declared outputLatency == N") {
  const Rendered r = render("mono-48k", "t-r15", 48000);
  CHECK(r.result.flushFrames == r.result.declaredLatency.outputLatencyFrames);
  CHECK(r.result.flushFrames == 2048);
  CHECK(r.result.lengthDeltaFrames == 0);
}

// ---------------------------------------------------------------------------
// T-E11: output-length policy — Preserving: N_in + N EXACTLY (golden),
// across parameter variants.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E11: output length == N_in + N exactly") {
  struct Case {
    std::string curve;
    std::string params;
    int64_t N;
  };
  for (const Case& c : std::vector<Case>{
           {"t-identity", "", 2048},
           {"t-r15", "", 2048},
           {"t-r075", "fft_size = 1024, hop = 256", 1024},
           {"t-r15", "fft_size = 1024, hop = 256", 1024},
           {"t-r15", "fft_size = 4096, hop = 1024", 4096}}) {
    CAPTURE(c.curve);
    CAPTURE(c.params);
    const Rendered r = render("mono-48k", c.curve, 48000, 1, "benchmark", c.params);
    CHECK(r.result.expectedOutputFrames == 48000 + c.N);
    CHECK(r.result.outputFramesProduced == 48000 + c.N);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(!r.result.lengthPolicyTaint);
    CHECK(r.result.taints.empty());
  }
}

// ---------------------------------------------------------------------------
// T-E12: extremes — 0.25x == minRatio (IN range ⇒ renders); 0.0625x/8x are
// OUT ⇒ benchmark JOB SKIP ratio-out-of-range; creative ⇒ saturated render
// with RANGE_SATURATED in the manifest.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E12: extremes — in-range render / out-of-range skip+saturation") {
  const HarnessConfig cfg = loadHarnessConfig(tr.root);

  {  // 0.25 == minRatio: benchmark renders (finite, exact length).
    const Rendered r = render("mono-48k", "t-extreme-025", 48000);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(r.result.taints.empty());
    double peak = 0.0;
    for (double v : r.master.channels[0]) peak = std::max(peak, std::fabs(v));
    CHECK(std::isfinite(peak));
    std::printf("T-E12: 0.25x (== minRatio, IN range) rendered, peak %.3f\n", peak);
  }

  for (const char* curveC : {"t-extreme-00625", "t-extreme-8"}) {
    const std::string curve(curveC);
    CAPTURE(curve);
    const fs::path exp = tr.makeExperiment(
        "exp-pvc-e12-" + curve,
        experimentToml("exp-pvc-e12", "mono-48k", curve, 48000, 1, "native.pv.classic",
                       "benchmark", ""));
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    REQUIRE(outcome.jobs.empty());
    REQUIRE(outcome.skips.size() == 1);
    CHECK(outcome.skips[0].reason == "ratio-out-of-range");
    CHECK(outcome.skips[0].engineId == "native.pv.classic");
  }
  for (const char* curveC : {"t-extreme-00625", "t-extreme-8"}) {
    const std::string curve(curveC);
    CAPTURE(curve);
    const fs::path exp = tr.makeExperiment(
        "exp-pvc-e12c-" + curve,
        experimentToml("exp-pvc-e12c", "mono-48k", curve, 48000, 1, "native.pv.classic",
                       "creative", ""));
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    REQUIRE(outcome.jobs.size() == 1);
    CHECK(outcome.jobs[0].saturated);
    double effMin = 1e9, effMax = -1e9;
    for (double v : outcome.jobs[0].effectiveSignal->ratios) {
      effMin = std::min(effMin, v);
      effMax = std::max(effMax, v);
    }
    CHECK(effMin >= 0.25);
    CHECK(effMax <= 4.0);
    const RenderSummary summary = renderJobs(outcome.jobs, registry);
    REQUIRE(summary.results.size() == 1);
    const JobResult& r = summary.results[0];
    CHECK(r.status == RenderStatus::Ok);
    CHECK(r.lengthDeltaFrames == 0);
    const std::string manifest = readText(r.manifestPath);
    CHECK(manifest.find("RANGE_SATURATED") != std::string::npos);
    std::printf("T-E12: %s creative saturated -> effective [%.4f, %.4f], ok\n", curve.c_str(),
                effMin, effMax);
  }
}

// ---------------------------------------------------------------------------
// T-E13: rapid reversal ±12 st/100 ms (in range) — finite, exact length.
// Includes a hop-quantisation stress variant (fft 1024, hop 128 — the
// control updates every 128 frames).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E13: rapid reversal + hop-quantisation stress") {
  for (const std::string& params :
       {std::string(""), std::string("fft_size = 1024, hop = 128")}) {
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
// ---------------------------------------------------------------------------
TEST_CASE("fixtures: invalid pv.classic parameters (CONFIG ERROR)") {
  auto engine = makePvClassicEngine();
  EngineConfiguration cfg;
  cfg.seed = 7;

  auto checkThrows = [&](auto addParam) {
    EngineConfiguration c = cfg;
    addParam(c);
    bool threw = false;
    try {
      engine->configure(c);
    } catch (const ConfigError&) {
      threw = true;
    }
    CHECK(threw);
  };
  checkThrows([](EngineConfiguration& c) {
    c.parameters.emplace_back("window", std::string("hamming"));
  });
  checkThrows([](EngineConfiguration& c) {
    c.parameters.emplace_back("fft_size", static_cast<int64_t>(1000));  // not a power of two
  });
  checkThrows([](EngineConfiguration& c) {
    c.parameters.emplace_back("fft_size", static_cast<int64_t>(16));  // < 32
  });
  checkThrows([](EngineConfiguration& c) {
    c.parameters.emplace_back("fft_size", 2048.0);  // real, not integer
  });
  checkThrows([](EngineConfiguration& c) {
    c.parameters.emplace_back("hop", static_cast<int64_t>(2048));  // > fft_size/2 at default 2048
  });
  checkThrows([](EngineConfiguration& c) {
    c.parameters.emplace_back("hop", static_cast<int64_t>(0));
  });
  checkThrows([](EngineConfiguration& c) {
    c.parameters.emplace_back("hop", 512.5);  // real, not integer
  });
  // Valid combination: small FFT + matching hop accepted.
  {
    EngineConfiguration c = cfg;
    c.parameters.emplace_back("fft_size", static_cast<int64_t>(1024));
    c.parameters.emplace_back("hop", static_cast<int64_t>(256));
    engine->configure(c);
  }
}

// ---------------------------------------------------------------------------
// Failure fixtures through the compiler: unknown key; non-first-class rate.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "fixtures: unknown key + non-first-class rate") {
  const HarnessConfig cfg = loadHarnessConfig(tr.root);
  {
    const fs::path exp = tr.makeExperiment(
        "exp-pvc-badkey",
        experimentToml("exp-pvc-badkey", "mono-48k", "t-identity", 48000, 1, "native.pv.classic",
                       "benchmark", "fft = 2048"));
    bool threw = false;
    try {
      const CompileOutcome outcome =
          compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
      (void)outcome;
    } catch (const ConfigError& e) {
      threw = true;
      CHECK(std::string(e.what()).find("fft") != std::string::npos);
    }
    CHECK(threw);
  }
  {
    const fs::path exp = tr.makeExperiment(
        "exp-pvc-32k",
        experimentToml("exp-pvc-32k", "mono32k", "t-identity", 32000, 1, "native.pv.classic",
                       "benchmark", ""));
    bool threw = false;
    try {
      const CompileOutcome outcome =
          compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
      (void)outcome;
    } catch (const ConfigError& e) {
      threw = true;
      CHECK(std::string(e.what()).find("32000") != std::string::npos);
    }
    CHECK(threw);
  }
}

// ---------------------------------------------------------------------------
// T-A1: allocation audit — process()/finish() never allocate (§5 rule 1).
// THE pocketfft audit: the persistent plan exec must be allocation-free.
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

TEST_CASE("T-A1: process()/finish() never allocate (pv.classic, FFT path)") {
  const int64_t N = 16384;
  const auto input = sineFrames(440.0, 48000, N);
  const std::vector<double> ratio15(N, 1.5);  // FIR active (rho_max > 1)
  const std::vector<double> ratio075(N, 0.75);  // no FIR
  const std::vector<double> ratio10(N, 1.0);   // identity

  for (const std::vector<double>* ratio : {&ratio15, &ratio075, &ratio10}) {
    CAPTURE(ratio->at(0));
    auto engine = makePvClassicEngine();
    EngineConfiguration cfg;
    cfg.seed = 0;
    g_auditAllocs = 0;
    const DriveReport dr = driveEngine(
        *engine, cfg, {input}, 48000, *ratio, 4096, 131072,
        /*beforeCall=*/[] { g_auditActive = true; },
        /*afterCall=*/[] { g_auditActive = false; });
    CHECK(dr.producedTotal > 0);
    CHECK(g_auditAllocs == 0);  // §5 rule 1 — the audit assertion
    std::printf("T-A1: ratio %.2f — allocations during process()/finish(): %zu\n",
                ratio->at(0), g_auditAllocs);
  }
}

// ---------------------------------------------------------------------------
// T-D3: reset() reuse — bit-identical output after reset (fresh spectral
// state: phases, accumulators, the synthesis grid).
// ---------------------------------------------------------------------------
TEST_CASE("T-D3: reset() reuse bit-identity") {
  const int64_t N = 12000;
  const auto input = sineFrames(440.0, 48000, N);
  std::vector<double> ratio(N);
  for (int64_t i = 0; i < N; ++i) {
    ratio[static_cast<std::size_t>(i)] =
        1.0 + 0.5 * std::sin(2.0 * 3.14159265358979323846 * 240.0 * static_cast<double>(i) /
                             48000.0);
  }
  EngineConfiguration cfg;
  cfg.seed = 0;

  auto engine = makePvClassicEngine();
  const DriveReport first = driveEngine(*engine, cfg, {input}, 48000, ratio, 4096, 131072);
  engine->reset();
  const DriveReport second = driveEngine(*engine, cfg, {input}, 48000, ratio, 4096, 131072);
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
  std::printf("T-D3: reset reuse bit-identity: %s (%lld frames)\n", identical ? "yes" : "NO",
              static_cast<long long>(first.producedTotal));
}
