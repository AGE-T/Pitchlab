// T-E1..T-E13 + T-A1 + T-D3 + failure fixtures — native.vardelay engine
// contract suite through the REAL harness (implementation specification
// §13.1 matrix, cycle 4 / §17 step 4; frozen behaviour §6.2.1).
//
// CONTRACT DISTINCTION (owner-locked L-5): engine-level block-boundary and
// identity criteria are AUDIO-EQUIVALENCE within −80 dBFS — NOT bit-exact.
// (vardelay's cross-schedule bit-identity is an observed stronger property,
// reported but never required — §6.2.1 item 10.)
//
// Matrix implemented here:
//   T-E1  ratio 1.0 identity (§6.2.1 item 2: v(0)=0 ⇒ y=x bit-exactly at
//          integer positions; criterion stays −80 dBFS)
//   T-E2  constant ratios 1.5 / 0.75 (pitch + exact Preserving length)
//   T-E3  positive/negative shifts ±1, ±7 st
//   T-E4  sample-rate invariance (44.1/96/192 kHz)
//   T-E5  stereo coherence (bit-identical channels for identical input;
//          per-channel == independent mono render)
//   T-E6  determinism (byte-identical WAV + manifest re-render)
//   T-E7  block-boundary independence (schedules {4096} vs mixed pattern —
//          audio-equivalent within −80 dBFS)
//   T-E8  block-size sweep {32..4096} + mixed pattern (stability)
//   T-E9  end-of-input accounting (consumed == N_in; padding == E+K;
//          exhaustion signalled exactly once)
//   T-E10 flush == declared outputLatency == W (equality by construction)
//   T-E11 output-length policy (Preserving: N_in + W EXACTLY)
//   T-E12 extremes 0.25x/8x: benchmark ⇒ JOB SKIP ratio-out-of-range;
//          creative ⇒ saturated render (RANGE_SATURATED in the manifest)
//   T-E13 rapid reversal ±12 st/100 ms (wrap machinery exercised; finite)
//   T-A1  allocation audit: process()/finish() never allocate
//   T-D3  reset() reuse bit-identity
//   fixtures: invalid parameters (direct), unknown key (compiler),
//          E < 1 (prepare), unsupported sample rate (compiler skip)
//
// The over-flush / stall / non-finite fixtures are engine-independent
// renderer guards (already covered by the varispeed suite's test-only
// dummies); this suite asserts vardelay's own flush equality instead.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

#include "engines/vardelay_engine.h"
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

// E and K at a given rate with the DEFAULT parameters (excursion 0.5 s,
// crossfade 2048, small-sinc K = 8) — §6.2.1 items 1/6.
int64_t defaultE(uint32_t fs) { return static_cast<int64_t>(std::llround(0.5 * fs)); }
constexpr int64_t kK = 8;

struct Fixture {
  TestRoot tr{"vardelay-contract"};
  EngineRegistry registry{TestRoot::productionRegistry()};

  Fixture() {
    tr.makeCurve("t-identity", kIdentityCurve);
    tr.makeCurve("t-r15", kRatio15Curve);
    tr.makeCurve("t-r075", kRatio075Curve);
    tr.makeCurve("t-p1", staticSemitoneCurve("t-p1", 1.0));
    tr.makeCurve("t-p7", staticSemitoneCurve("t-p7", 7.0));
    tr.makeCurve("t-m1", staticSemitoneCurve("t-m1", -1.0));
    tr.makeCurve("t-m7", staticSemitoneCurve("t-m7", -7.0));
    tr.makeCurve("t-extreme-025", staticRatioCurve("t-extreme-025", 0.25));
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

  /// Render one (asset, curve) job with the engine defaults; returns the
  /// result + the master data.
  struct Rendered {
    JobResult result;
    WavData master;
  };

  Rendered render(const std::string& asset, const std::string& curve, uint32_t fs,
                  int channels = 1, const std::string& kind = "benchmark",
                  const std::string& params = "") {
    const std::string expId = "exp-vd-" + asset + "-" + curve;
    const std::string p = params.empty() ? "excursion_seconds = 0.5" : params;
    const fs::path exp = tr.makeExperiment(
        expId, experimentToml(expId, asset, curve, fs, channels, "native.vardelay", kind, p));
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
// T-E1: ratio = 1.0 identity — v(0) = 0 and D' = 0 keep the delay at 0, so
// y(t) = x(t) at INTEGER positions (the sinc kernel is exact there, §7.2.1).
// The CONTRACT criterion stays −80 dBFS (L-5); the observed error is
// reported (bit-exact expected).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E1: ratio 1.0 identity within -80 dBFS") {
  const Rendered r = render("mono-48k", "t-identity", 48000);
  const int64_t E = defaultE(48000);
  REQUIRE(r.result.status == RenderStatus::Ok);
  REQUIRE(r.result.inputFramesConsumed == 48000);        // exactly the real input
  REQUIRE(r.result.inputPaddingFrames == E + kK);       // declared inputLatency (E + K)
  REQUIRE(r.result.inputExhaustionTransitions == 1);    // T-E9 slice as well
  const WavData input = readWav(tr.root / "assets" / "corpus" / "mono-48k" / "signal.wav");
  const int64_t n = std::min<int64_t>(input.meta.frames, r.master.meta.frames);
  REQUIRE(n == 48000);
  double maxErr = 0.0;
  for (int64_t k = 0; k < n; ++k) {
    const double e = std::fabs(r.master.channels[0][static_cast<std::size_t>(k)] -
                               input.channels[0][static_cast<std::size_t>(k)]);
    if (e > maxErr) maxErr = e;
  }
  CHECK(maxErr <= kAudioEquivDbfs);
  std::printf("T-E1: identity max |out-in| = %.3e (criterion %.1e; -80 dBFS)\n", maxErr,
              kAudioEquivDbfs);
  // Tail: the W flush frames read the padding zeros — up to the kernel's
  // integer-offset residues (~1e-16 — the §7.7.1/T-R2a measured identity
  // behaviour: libm sin(π·i) is not exactly zero, so side taps at the real
  // input's last frames leave ~1e-19). Criterion: −80 dBFS.
  double tailMax = 0.0;
  for (int64_t k = n; k < r.master.meta.frames; ++k) {
    tailMax = std::max(tailMax, std::fabs(r.master.channels[0][static_cast<std::size_t>(k)]));
  }
  CHECK(tailMax <= kAudioEquivDbfs);
  std::printf("T-E1: tail max |out| = %.3e (criterion %.1e)\n", tailMax, kAudioEquivDbfs);
  // Length: Preserving exact — N_in + W, delta == 0, flush == W.
  CHECK(r.result.lengthDeltaFrames == 0);
  CHECK(r.result.flushFrames == 2048);
  CHECK(r.result.declaredLatency.outputLatencyFrames == 2048);
  CHECK(r.result.outputFramesProduced == 48000 + 2048);
}

// ---------------------------------------------------------------------------
// T-E2: constant ratios — measured pitch + exact length policy. The first
// wrap lands at frame ~1 (v falls immediately); the read position then
// advances at the ratio through the excursion, wrapping at |1−r|/E cadence.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E2: constant ratios 1.5 and 0.75") {
  for (auto [curve, ratio] : std::vector<std::pair<std::string, double>>{
           {"t-r15", 1.5}, {"t-r075", 0.75}}) {
    const Rendered r = render("mono-48k", curve, 48000);
    CAPTURE(curve);
    CHECK(r.result.lengthDeltaFrames == 0);  // Preserving: EXACT (§4.3.3)
    CHECK(r.result.outputFramesProduced == 48000 + 2048);
    // Measured pitch: dominant frequency within 25 provisional cents. The
    // signal enters at t ≈ E/ratio (the post-wrap delay); measure over the
    // late window [N/2, N/2 + 20000).
    const double f = dominantFrequency(r.master.channels[0], 48000, 24000, 20000);
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
    const double f = dominantFrequency(r.master.channels[0], 48000, 24000, 20000);
    const double cents = centsBetween(f, expected);
    CHECK(std::abs(cents) <= kPitchToleranceCents);
    std::printf("T-E3: %+g st -> dominant %.2f Hz (expected %.2f; %.2f cents)\n", st, f, expected,
                cents);
  }
}

// ---------------------------------------------------------------------------
// T-E4: sample-rate invariance (44.1 / 96 / 192 kHz pass equivalently).
// The signal enters at t ≈ E/ratio = fs/3 for r = 1.5; measure over the
// late window [2N/3, N).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E4: sample-rate invariance 44.1/96/192 kHz") {
  const std::vector<std::pair<std::string, uint32_t>> rates = {
      {"mono-44k1", 44100u}, {"mono-96k", 96000u}, {"mono-192k", 192000u}};
  for (const auto& [asset, fsHz] : rates) {
    CAPTURE(asset);
    const Rendered r = render(asset, "t-r15", fsHz);
    CHECK(r.result.status == RenderStatus::Ok);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(r.result.inputPaddingFrames == defaultE(fsHz) + kK);
    const WavData in = readWav(tr.root / "assets" / "corpus" / asset / "signal.wav");
    const int64_t N = in.meta.frames;
    const int64_t skip = (2 * N) / 3;
    const int64_t n = N - skip - 1000;
    REQUIRE(n > 8000);
    const double f = dominantFrequency(r.master.channels[0], fsHz, skip, n);
    const double cents = centsBetween(f, 440.0 * 1.5);
    CHECK(std::abs(cents) <= kPitchToleranceCents);
    std::printf("T-E4: %u Hz -> dominant %.2f Hz (expected 660.00; %.2f cents)\n", fsHz, f, cents);
  }
}

// ---------------------------------------------------------------------------
// T-E5: stereo — identical channels stay bit-identical; each channel's
// output equals the independent mono render (§6.2.1 item 8: engine-level
// branch/fade state, shared read positions).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E5: stereo coherence") {
  const int64_t N = 24000;
  const auto left = sineFrames(440.0, 48000, N);
  const auto right = sineFrames(660.0, 48000, N);
  tr.makeAsset("stereo-48k", 48000, {left, right});
  tr.makeAsset("monoL-48k", 48000, {left});
  tr.makeAsset("monoR-48k", 48000, {right});

  const Rendered stereo = render("stereo-48k", "t-r15", 48000, 2);
  REQUIRE(stereo.master.meta.channels == 2);
  CHECK(stereo.result.status == RenderStatus::Ok);

  // Coherent correlated pair: identical input channels -> identical output.
  tr.makeAsset("stereoCorr-48k", 48000, {left, left});
  const Rendered corr = render("stereoCorr-48k", "t-identity", 48000, 2);
  REQUIRE(corr.master.meta.channels == 2);
  bool identical = corr.master.meta.frames > 0;
  for (int64_t k = 0; k < corr.master.meta.frames && identical; ++k) {
    const double a = corr.master.channels[0][static_cast<std::size_t>(k)];
    const double b = corr.master.channels[1][static_cast<std::size_t>(k)];
    if (std::memcmp(&a, &b, sizeof(double)) != 0) identical = false;
  }
  CHECK(identical);  // vardelay declares coherent stereo (§6.2/§14)

  // Independent channels: each channel's output is bit-identical to the mono
  // render of the same input.
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
  std::printf("T-E5: stereo L == mono(L) bit-identical: %s; R == mono(R): %s; correlated pair "
              "bit-identical: %s\n",
              matchL ? "yes" : "NO", matchR ? "yes" : "NO", identical ? "yes" : "NO");
}

// ---------------------------------------------------------------------------
// T-E6: determinism — same (input, config, schedule) twice ⇒ byte-identical
// WAV + manifest (fresh root; path-normalised manifest comparison).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E6: byte-identical repeated rendering") {
  const fs::path exp = tr.makeExperiment(
      "exp-vd-det",
      experimentToml("exp-vd-det", "mono-48k", "t-r15", 48000, 1, "native.vardelay",
                     "benchmark", "excursion_seconds = 0.5"));
  const RenderSummary a = renderExperiment(exp, tr, registry);
  REQUIRE(a.results.size() == 1);
  REQUIRE(a.results[0].status == RenderStatus::Ok);
  const std::string wavHash1 = a.results[0].masterSha256;
  const std::string manifest1 = readText(a.results[0].manifestPath);

  {
    TestRoot tr2{"vardelay-contract-det2"};
    tr2.makeCurve("t-r15", kRatio15Curve);
    tr2.makeAsset("mono-48k", 48000, {sineFrames(440.0, 48000, 48000)});
    const fs::path exp2 = tr2.makeExperiment(
        "exp-vd-det",
        experimentToml("exp-vd-det", "mono-48k", "t-r15", 48000, 1, "native.vardelay",
                       "benchmark", "excursion_seconds = 0.5"));
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
// AUDIO-EQUIVALENT within −80 dBFS (L-5). Bit-exactness is NOT required.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E7: block-boundary independence (-80 dBFS)") {
  const int64_t N = 24000;
  tr.makeAsset("mono24k-48k", 48000, {sineFrames(440.0, 48000, N)});

  auto renderWithSchedule = [&](BlockSchedule sched) {
    const fs::path exp = tr.makeExperiment(
        "exp-vd-e7", experimentToml("exp-vd-e7", "mono24k-48k", "t-r15", 48000, 1,
                                    "native.vardelay", "benchmark", "excursion_seconds = 0.5"));
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
// T-E8: block-size variation — schedule sweep renders valid, stable output
// (stability, NOT equality across sizes).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E8: block-size sweep 32..4096") {
  const int64_t N = 24000;
  tr.makeAsset("mono8-48k", 48000, {sineFrames(440.0, 48000, N)});
  const fs::path exp = tr.makeExperiment(
      "exp-vd-e8", experimentToml("exp-vd-e8", "mono8-48k", "t-r15", 48000, 1, "native.vardelay",
                                  "benchmark", "excursion_seconds = 0.5"));
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
    CHECK(r.outputFramesProduced == N + 2048);
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
// T-E9: end-of-input — engine consumes exactly the real input + the E+K
// padding; exhaustion signalled exactly once.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E9: end-of-input accounting") {
  const Rendered r = render("mono-48k", "t-r15", 48000);
  CHECK(r.result.inputFramesActual == 48000);
  CHECK(r.result.inputFramesConsumed == 48000);        // REAL input only (§4.3.6)
  CHECK(r.result.inputPaddingFrames == defaultE(48000) + kK);  // E + K
  CHECK(r.result.inputExhaustionTransitions == 1);     // exactly once (§4.2.1 item 7)
  CHECK(r.result.declaredLatency.inputLatencyFrames == defaultE(48000) + kK);
  // Identity: same padding (E does not depend on the curve).
  const Rendered id = render("mono-48k", "t-identity", 48000);
  CHECK(id.result.inputPaddingFrames == defaultE(48000) + kK);
}

// ---------------------------------------------------------------------------
// T-E10: flush — EQUAL to the declared outputLatencyFrames (W) by
// construction (§6.2.1 item 7: the tail [N_in, N_in+W) is emitted by
// finish()); the renderer's ≤-bound check passes with equality. The
// over-flush FAILURE fixture is the engine-independent test-only dummy in
// the varispeed suite; not duplicated here.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E10: flush == declared outputLatency == W") {
  for (auto [curve, W] : std::vector<std::pair<std::string, int64_t>>{
           {"t-identity", 2048}, {"t-r15", 2048}}) {
    CAPTURE(curve);
    const Rendered r = render("mono-48k", curve, 48000);
    CHECK(r.result.flushFrames == r.result.declaredLatency.outputLatencyFrames);
    CHECK(r.result.flushFrames == W);
  }
  // Short crossfade parameter: W = 64.
  const Rendered r64 = render("mono-48k", "t-r15", 48000, 1, "benchmark",
                              "excursion_seconds = 0.5, crossfade_frames = 64");
  CHECK(r64.result.flushFrames == 64);
  CHECK(r64.result.declaredLatency.outputLatencyFrames == 64);
  CHECK(r64.result.lengthDeltaFrames == 0);
}

// ---------------------------------------------------------------------------
// T-E11: output-length policy — Preserving: N_in + W EXACTLY (golden).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E11: output length == N_in + W exactly") {
  struct Case {
    std::string curve;
    std::string params;
    int64_t W;
  };
  for (const Case& c : std::vector<Case>{
           {"t-identity", "excursion_seconds = 0.5", 2048},
           {"t-r15", "excursion_seconds = 0.5", 2048},
           {"t-r075", "excursion_seconds = 0.25", 2048},
           {"t-r15", "excursion_seconds = 0.05, crossfade_frames = 512", 512},
           {"t-r15", "excursion_seconds = 0.5, crossfade_frames = 0", 0}}) {
    CAPTURE(c.curve);
    CAPTURE(c.params);
    const Rendered r = render("mono-48k", c.curve, 48000, 1, "benchmark", c.params);
    CHECK(r.result.expectedOutputFrames == 48000 + c.W);
    CHECK(r.result.outputFramesProduced == 48000 + c.W);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(!r.result.lengthPolicyTaint);
    CHECK(r.result.taints.empty());
  }
}

// ---------------------------------------------------------------------------
// T-E12: extreme curve movement — 0.25x/8x are OUTSIDE the declared [0.5, 2]:
// benchmark ⇒ JOB SKIP ratio-out-of-range (never rendered, never saturated);
// creative ⇒ saturation into the declared range, rendered, RANGE_SATURATED
// recorded in the manifest (§4.4.6).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E12: extremes — benchmark skip / creative saturation") {
  const HarnessConfig cfg = loadHarnessConfig(tr.root);

  // Benchmark: both extremes skip.
  for (const char* curveC : {"t-extreme-025", "t-extreme-8"}) {
    const std::string curve(curveC);
    CAPTURE(curve);
    const fs::path exp = tr.makeExperiment(
        "exp-vd-e12-" + curve,
        experimentToml("exp-vd-e12", "mono-48k", curve, 48000, 1, "native.vardelay", "benchmark",
                       "excursion_seconds = 0.5"));
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    REQUIRE(outcome.jobs.empty());
    REQUIRE(outcome.skips.size() == 1);
    CHECK(outcome.skips[0].reason == "ratio-out-of-range");
    CHECK(outcome.skips[0].engineId == "native.vardelay");
    CHECK(outcome.skips[0].curveId == curve);
  }

  // Creative: saturated render — the engine sees the effective curve only
  // (clamped into [0.5, 2]); finite output; manifest records RANGE_SATURATED.
  for (const char* curveC : {"t-extreme-025", "t-extreme-8"}) {
    const std::string curve(curveC);
    CAPTURE(curve);
    const fs::path exp = tr.makeExperiment(
        "exp-vd-e12c-" + curve,
        experimentToml("exp-vd-e12c", "mono-48k", curve, 48000, 1, "native.vardelay", "creative",
                       "excursion_seconds = 0.5"));
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    REQUIRE(outcome.jobs.size() == 1);
    CHECK(outcome.jobs[0].saturated);
    CHECK(outcome.jobs[0].requestedSignal != outcome.jobs[0].effectiveSignal);
    // Effective curve inside the declared range.
    double effMin = 1e9, effMax = -1e9;
    for (double v : outcome.jobs[0].effectiveSignal->ratios) {
      effMin = std::min(effMin, v);
      effMax = std::max(effMax, v);
    }
    CHECK(effMin >= 0.5);
    CHECK(effMax <= 2.0);
    const RenderSummary summary = renderJobs(outcome.jobs, registry);
    REQUIRE(summary.results.size() == 1);
    const JobResult& r = summary.results[0];
    CHECK(r.status == RenderStatus::Ok);
    CHECK(r.lengthDeltaFrames == 0);
    const std::string manifest = readText(r.manifestPath);
    CHECK(manifest.find("RANGE_SATURATED") != std::string::npos);
    std::printf("T-E12: %s creative saturated -> effective [%.3f, %.3f], %lld frames, ok\n",
                curve.c_str(), effMin, effMax, static_cast<long long>(r.outputFramesProduced));
  }
}

// ---------------------------------------------------------------------------
// T-E13: rapid reversal ±12 st / 100 ms — the curve stays inside [0.5, 2]
// (±12 st = exactly the bounds); the wrap/crossfade machinery is exercised
// hard. Finite output, exact length, no failure. Includes a wrap-stress
// variant with a SHORT excursion (E = 480 frames ⇒ wraps every ~960 frames,
// ~50 wraps; and the boundary-pinned degenerate path of §6.2.1 item 4c).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E13: rapid reversal + wrap stress") {
  {
    const Rendered r = render("mono-48k", "t-reversal", 48000);
    CHECK(r.result.status == RenderStatus::Ok);
    CHECK(r.result.lengthDeltaFrames == 0);
    CHECK(r.result.outputFramesProduced == 48000 + 2048);
    // Finiteness (the renderer NaN gate enforces; double-check the master).
    double peak = 0.0;
    for (double v : r.master.channels[0]) peak = std::max(peak, std::fabs(v));
    CHECK(std::isfinite(peak));
    CHECK(peak < 1.0);
  }
  {
    // Wrap stress: short excursion + constant 1.5 (wrap every ~960 frames)
    // and short excursion + reversal (the boundary-saturation degenerate).
    for (const char* curveC : {"t-r15", "t-reversal"}) {
      const std::string curve(curveC);
      CAPTURE(curve);
      const Rendered r = render("mono-48k", curve, 48000, 1, "benchmark",
                                "excursion_seconds = 0.01, crossfade_frames = 256");
      CHECK(r.result.status == RenderStatus::Ok);
      CHECK(r.result.lengthDeltaFrames == 0);
      CHECK(r.result.outputFramesProduced == 48000 + 256);
      CHECK(r.result.inputExhaustionTransitions == 1);
      double peak = 0.0;
      for (double v : r.master.channels[0]) peak = std::max(peak, std::fabs(v));
      CHECK(std::isfinite(peak));
      CHECK(peak < 1.0);
      std::printf("T-E13: %s with E=480/W=256: ok, peak %.3f (wrap machinery saturated)\n",
                  curve.c_str(), peak);
    }
  }
}

// ---------------------------------------------------------------------------
// Failure fixtures: invalid parameter values (direct engine drives).
// §6.2.1 item 11 — ConfigError from configure()/prepare().
// ---------------------------------------------------------------------------
TEST_CASE("fixtures: invalid vardelay parameters (CONFIG ERROR)") {
  auto engine = makeVardelayEngine();
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

  checkThrows("excursion_seconds", [](EngineConfiguration& c) {
    c.parameters.emplace_back("excursion_seconds", -0.5);
  });
  checkThrows("excursion_seconds", [](EngineConfiguration& c) {
    c.parameters.emplace_back("excursion_seconds", 0.0);
  });
  checkThrows("excursion_seconds", [](EngineConfiguration& c) {
    c.parameters.emplace_back("excursion_seconds", std::nan(""));
  });
  checkThrows("excursion_seconds", [](EngineConfiguration& c) {
    c.parameters.emplace_back("excursion_seconds", std::numeric_limits<double>::infinity());
  });
  checkThrows("crossfade_frames", [](EngineConfiguration& c) {
    c.parameters.emplace_back("crossfade_frames", static_cast<int64_t>(-1));
  });
  checkThrows("crossfade_frames", [](EngineConfiguration& c) {
    c.parameters.emplace_back("crossfade_frames", 2048.5);  // double, not integer
  });
  checkThrows("read_kernel", [](EngineConfiguration& c) {
    c.parameters.emplace_back("read_kernel", std::string("large-sinc"));
  });
  checkThrows("read_kernel", [](EngineConfiguration& c) {
    c.parameters.emplace_back("read_kernel", static_cast<int64_t>(8));
  });

  // E < 1 after conversion (prepare-time): 1e-9 s at 48000 Hz.
  {
    EngineConfiguration c = cfg;
    c.parameters.emplace_back("excursion_seconds", 1e-9);
    engine->configure(c);
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
      engine->prepare(ctx);
    } catch (const ConfigError& e) {
      threw = true;
      CHECK(std::string(e.what()).find("excursion_seconds") != std::string::npos);
    }
    CHECK(threw);
  }

  // W == 0 is VALID (degenerate instant wraps) — configure must accept it.
  {
    EngineConfiguration c = cfg;
    c.parameters.emplace_back("crossfade_frames", static_cast<int64_t>(0));
    engine->configure(c);
  }
}

// ---------------------------------------------------------------------------
// Failure fixtures through the compiler: unknown parameter key ⇒ CONFIG
// ERROR; unsupported sample rate ⇒ visible JOB SKIP.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "fixtures: unknown key + unsupported rate") {
  const HarnessConfig cfg = loadHarnessConfig(tr.root);
  {
    const fs::path exp = tr.makeExperiment(
        "exp-vd-badkey",
        experimentToml("exp-vd-badkey", "mono-48k", "t-identity", 48000, 1, "native.vardelay",
                       "benchmark", "excursion_sec = 0.5"));
    bool threw = false;
    try {
      const CompileOutcome outcome =
          compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
      (void)outcome;  // the key-set validation must throw before returning
    } catch (const ConfigError& e) {
      threw = true;
      CHECK(std::string(e.what()).find("excursion_sec") != std::string::npos);
    }
    CHECK(threw);
  }
  {
    // A non-first-class rate is rejected by the harness-level validation
    // (spec §8.1) BEFORE any engine-support check — CONFIG ERROR with the
    // rate in the message. (The engine-side sample-rate-unsupported SKIP for
    // first-class rates an engine does not declare is engine-independent
    // compiler machinery, covered by experiment_compiler_test with a
    // test-only descriptor; vardelay declares all six first-class rates.)
    const fs::path exp = tr.makeExperiment(
        "exp-vd-32k",
        experimentToml("exp-vd-32k", "mono32k", "t-identity", 32000, 1, "native.vardelay",
                       "benchmark", "excursion_seconds = 0.5"));
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
// the global new/delete interposition counts allocations while the hooks
// are active). Covers the wrap/fade machinery (short excursion ⇒ constant
// wrapping) and the identity path.
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

TEST_CASE("T-A1: process()/finish() never allocate (vardelay, wrap + identity)") {
  const int64_t N = 16384;
  const auto input = sineFrames(440.0, 48000, N);
  const std::vector<double> ratio15(N, 1.5);  // wraps constantly at E = 480
  const std::vector<double> ratio10(N, 1.0);  // identity path

  for (const std::vector<double>* ratio : {&ratio15, &ratio10}) {
    CAPTURE(ratio->at(0));
    auto engine = makeVardelayEngine();
    EngineConfiguration cfg;
    cfg.seed = 0;
    cfg.parameters.emplace_back("excursion_seconds", 0.01);   // E = 480
    cfg.parameters.emplace_back("crossfade_frames", static_cast<int64_t>(256));
    g_auditAllocs = 0;
    const DriveReport dr = driveEngine(
        *engine, cfg, {input}, 48000, *ratio, 4096, 65536,
        /*beforeCall=*/[] { g_auditActive = true; },
        /*afterCall=*/[] { g_auditActive = false; });
    // prepare() MAY allocate; the audit covers only process()/finish().
    CHECK(dr.producedTotal > 0);
    CHECK(g_auditAllocs == 0);  // §5 rule 1 — the audit assertion
    std::printf("T-A1: ratio %.2f (E=480) — allocations during process()/finish(): %zu\n",
                ratio->at(0), g_auditAllocs);
  }
}

// ---------------------------------------------------------------------------
// T-D3: reset() reuse — after reset, the same input+curve produces
// bit-identical output to a fresh instance (§5 rule 5).
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
  cfg.parameters.emplace_back("excursion_seconds", 0.01);
  cfg.parameters.emplace_back("crossfade_frames", static_cast<int64_t>(256));

  auto engine = makeVardelayEngine();
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
  std::printf("T-D3: reset reuse bit-identity: %s (%lld frames)\n", identical ? "yes" : "NO",
              static_cast<long long>(first.producedTotal));
}
