// T-E1..T-E11 + T-A1 + T-D3 — native.varispeed engine contract suite through
// the REAL harness (implementation specification §13.1 matrix, cycle 3).
//
// CONTRACT DISTINCTION (owner-locked L-5): engine-level block-boundary and
// identity criteria are AUDIO-EQUIVALENCE within −80 dBFS — NOT bit-exact.
// (The resampler's T-R2g primitive-level bit-identity does NOT propagate to
// the engine contract; varispeed's cross-schedule bit-identity is an
// observed stronger property, reported but never required.)
//
// Matrix implemented here:
//   T-E1  ratio 1.0 identity (audio-equivalence within −80 dBFS)
//   T-E2  constant ratios 1.5 / 0.75 (pitch + length policy)
//   T-E3  positive/negative shifts ±1, ±7 st
//   T-E4  sample-rate invariance (44.1/96/192 kHz)
//   T-E5  stereo coherence (bit-identical channels for identical input;
//          per-channel == independent mono render)
//   T-E6  determinism (byte-identical WAV + manifest re-render)
//   T-E7  block-boundary independence (schedules {4096} vs {1024, ...} —
//          audio-equivalent within −80 dBFS)
//   T-E8  block-size sweep {32..4096} + mixed pattern (stability)
//   T-E9  end-of-input accounting (consumed == N_in, padding, exhaustion
//          signalled exactly once)
//   T-E10 flush bound + over-flush failure fixture (test-only dummy engine)
//   T-E11 output-length policy (golden exact deltas; Preserving dummy)
//   T-A1  allocation audit: process()/finish() never allocate
//   T-D3  reset() reuse bit-identity

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "engines/varispeed_engine.h"
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
  TestRoot tr{"varispeed-contract"};
  EngineRegistry registry{TestRoot::productionRegistry()};

  Fixture() {
    tr.makeCurve("t-identity", kIdentityCurve);
    tr.makeCurve("t-r15", kRatio15Curve);
    tr.makeCurve("t-r075", kRatio075Curve);
    tr.makeCurve("t-p1", staticSemitoneCurve("t-p1", 1.0));
    tr.makeCurve("t-p7", staticSemitoneCurve("t-p7", 7.0));
    tr.makeCurve("t-m1", staticSemitoneCurve("t-m1", -1.0));
    tr.makeCurve("t-m7", staticSemitoneCurve("t-m7", -7.0));
    tr.makeCurve("t-r2", staticRatioCurve("t-r2", 2.0));
    tr.makeCurve("t-r025", staticRatioCurve("t-r025", 0.25));
    tr.makeCurve("t-r8", staticRatioCurve("t-r8", 8.0));
    tr.makeAsset("mono-48k", 48000, {sineFrames(440.0, 48000, 48000)});
    tr.makeAsset("mono-44k1", 44100, {sineFrames(440.0, 44100, 44100)});
    tr.makeAsset("mono-96k", 96000, {sineFrames(440.0, 96000, 96000)});
    tr.makeAsset("mono-192k", 192000, {sineFrames(440.0, 192000, 96000)});
  }

  /// Render one (asset, curve) job; returns the result + the master data.
  struct Rendered {
    JobResult result;
    WavData master;
  };

  Rendered render(const std::string& asset, const std::string& curve, uint32_t fs, int channels = 1,
                  const std::string& kind = "benchmark") {
    const std::string expId = "exp-" + asset + "-" + curve;
    const fs::path exp =
        tr.makeExperiment(expId, experimentToml(expId, asset, curve, fs, channels,
                                                "native.varispeed", kind));
    const RenderSummary summary = renderExperiment(exp, tr, registry);
    REQUIRE(summary.results.size() == 1);
    REQUIRE(summary.results[0].status == RenderStatus::Ok);
    REQUIRE(summary.results[0].masterWav != fs::path());
    return Rendered{summary.results[0], readWav(summary.results[0].masterWav)};
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// T-E1: ratio = 1.0 identity — output == bypass within −80 dBFS after
// declared-latency alignment (L-5 provisional criterion; NOT bit-exact).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E1: ratio 1.0 identity within -80 dBFS") {
  const Rendered r = render("mono-48k", "t-identity", 48000);
  REQUIRE(r.result.status == RenderStatus::Ok);
  REQUIRE(r.result.inputFramesConsumed == 48000);          // exactly the real input
  REQUIRE(r.result.inputPaddingFrames == 24);             // declared inputLatency (K)
  REQUIRE(r.result.inputExhaustionTransitions == 1);      // T-E9 slice as well
  // Alignment: identity maps output frame k to input frame k.
  const auto& in = r.master;  // read-back of the SAME sine (bit-exact round trip)
  // Compare against the analytically re-read input asset:
  const WavData input = readWav(tr.root / "assets" / "corpus" / "mono-48k" / "signal.wav");
  const int64_t n = std::min<int64_t>(input.meta.frames, r.master.meta.frames);
  REQUIRE(n >= 47000);
  double maxErr = 0.0;
  for (int64_t k = 0; k < n; ++k) {
    const double e = std::fabs(in.channels[0][static_cast<std::size_t>(k)] -
                               input.channels[0][static_cast<std::size_t>(k)]);
    if (e > maxErr) maxErr = e;
  }
  CHECK(maxErr <= kAudioEquivDbfs);
  // Reported observed error (information preservation; the contract is the
  // −80 dBFS bound above, NOT bit-exactness).
  std::printf("T-E1: identity max |out-in| = %.3e (criterion %.1e; -80 dBFS)\n", maxErr,
              kAudioEquivDbfs);
  // Length: identity delta == 0 exactly (constant-ratio arithmetic:
  // flush = ceil((K-0.5)/1) == ceil(K/1) == outputLatency).
  CHECK(r.result.lengthDeltaFrames == 0);
  CHECK(r.result.flushFrames == 24);
  CHECK(r.result.declaredLatency.outputLatencyFrames == 24);
}

// ---------------------------------------------------------------------------
// T-E2: constant ratios — measured pitch + length policy.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E2: constant ratios 1.5 and 0.75") {
  for (auto [curve, ratio] : std::vector<std::pair<std::string, double>>{
           {"t-r15", 1.5}, {"t-r075", 0.75}}) {
    const Rendered r = render("mono-48k", curve, 48000);
    CAPTURE(curve);
    // Length policy: within the provisional ±1 block (OD-6; T-LEN-CAL
    // calibrates). Constant ratios in these cases hit delta == 0 exactly.
    CHECK(std::abs(r.result.lengthDeltaFrames) <= 4096);
    CHECK(r.result.lengthDeltaFrames == 0);
    // Measured pitch: dominant frequency within 25 provisional cents.
    const double f = dominantFrequency(r.master.channels[0], 48000, 10000, 24000);
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
    CHECK(std::abs(r.result.lengthDeltaFrames) <= 4096);
    const double expected = 440.0 * std::exp2(st / 12.0);
    const double f = dominantFrequency(r.master.channels[0], 48000, 10000, 24000);
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
    CHECK(std::abs(r.result.lengthDeltaFrames) <= 4096);
    const int64_t skip = static_cast<int64_t>(fsHz) / 5;
    const int64_t n = std::min<int64_t>(static_cast<int64_t>(fsHz) / 2,
                                        r.master.meta.frames - skip);
    const double f = dominantFrequency(r.master.channels[0], fsHz, skip, n);
    const double cents = centsBetween(f, 440.0 * 1.5);
    CHECK(std::abs(cents) <= kPitchToleranceCents);
    std::printf("T-E4: %u Hz -> dominant %.2f Hz (%.2f cents)\n", fsHz, f, cents);
  }
}

// ---------------------------------------------------------------------------
// T-E5: stereo — identical channels stay bit-identical; per-channel output
// equals the independent mono render of that channel (coherence, §6.1).
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
  CHECK(identical);  // varispeed declares coherent stereo (§6.1/§14)

  // Independent channels: each channel's output is bit-identical to the mono
  // render of the same input (the read position is shared, per-channel code
  // paths identical — no per-channel phase/state divergence).
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
// WAV + manifest (§4.8.1 item 11).
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E6: byte-identical repeated rendering") {
  const fs::path exp =
      tr.makeExperiment("exp-det", experimentToml("exp-det", "mono-48k", "t-r15", 48000, 1));
  const RenderSummary a = renderExperiment(exp, tr, registry);
  REQUIRE(a.results.size() == 1);
  REQUIRE(a.results[0].status == RenderStatus::Ok);
  const std::string wavHash1 = a.results[0].masterSha256;

  std::string manifest1;
  {
    std::ifstream in(a.results[0].manifestPath, std::ios::binary);
    manifest1 = std::string((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  }
  // Re-render into a FRESH root (byte-identity across runs, not cache hits).
  {
    TestRoot tr2{"varispeed-contract-det2"};
    tr2.makeCurve("t-r15", kRatio15Curve);
    tr2.makeAsset("mono-48k", 48000, {sineFrames(440.0, 48000, 48000)});
    const fs::path exp2 =
        tr2.makeExperiment("exp-det", experimentToml("exp-det", "mono-48k", "t-r15", 48000, 1));
    const RenderSummary b = renderExperiment(exp2, tr2, registry);
    REQUIRE(b.results.size() == 1);
    REQUIRE(b.results[0].status == RenderStatus::Ok);
    CHECK(b.results[0].masterSha256 == wavHash1);
    std::string manifest2;
    {
      std::ifstream in(b.results[0].manifestPath, std::ios::binary);
      manifest2 = std::string((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    }
    // Byte-identical manifests (no timestamps, no run-unique data). NOTE: the
    // experiment FILE field records the absolute path, which differs between
    // roots by design of this double-root check — compare everything else by
    // normalising that one field.
    std::string m1 = manifest1, m2 = manifest2;
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
        "exp-e7", experimentToml("exp-e7", "mono24k-48k", "t-r15", 48000, 1));
    const HarnessConfig cfg = loadHarnessConfig(tr.root);
    const CompileOutcome outcome =
        compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    REQUIRE(outcome.jobs.size() == 1);
    RenderJob job = outcome.jobs[0];
    job.blockSchedule = sched;
    const std::vector<RenderJob> jobs{job};
    const RenderSummary summary = renderJobs(jobs, registry);
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
  // ENGINE-LEVEL CONTRACT: audio-equivalent within −80 dBFS (NOT bit-exact).
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
  const fs::path exp =
      tr.makeExperiment("exp-e8", experimentToml("exp-e8", "mono8-48k", "t-r15", 48000, 1));
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
    CHECK(std::abs(r.lengthDeltaFrames) <= 4096);
    CHECK(r.outputFramesProduced > 0);
    CHECK(r.inputFramesConsumed == N);
    CHECK(r.inputExhaustionTransitions == 1);
    // Valid audio: finite (renderer NaN gate already enforces; double-check
    // the master reads back and stays bounded).
    const WavData master = readWav(r.masterWav);
    double peak = 0.0;
    for (double v : master.channels[0]) peak = std::max(peak, std::fabs(v));
    CHECK(peak < 1.0);
  }
  // Mixed pattern as well.
  {
    const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
    RenderJob job = outcome.jobs[0];
    job.blockSchedule = BlockSchedule::pattern({32, 4096, 512, 128, 2048});
    const RenderSummary summary = renderJobs({job}, registry);
    REQUIRE(summary.results.size() == 1);
    CHECK(summary.results[0].status == RenderStatus::Ok);
  }
}

// ---------------------------------------------------------------------------
// T-E9: end-of-input — engine consumes exactly the real input + padding;
// exhaustion signalled exactly once.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E9: end-of-input accounting") {
  const Rendered r = render("mono-48k", "t-r15", 48000);
  CHECK(r.result.inputFramesActual == 48000);
  CHECK(r.result.inputFramesConsumed == 48000);      // REAL input only (§4.3.6)
  CHECK(r.result.inputPaddingFrames == 48);          // K + FIR K (r_max = 1.5 > 1)
  CHECK(r.result.inputExhaustionTransitions == 1);   // exactly once (§4.2.1 item 7)
  CHECK(r.result.declaredLatency.inputLatencyFrames == 48);
  // Bypass case: r_max <= 1 -> padding == K.
  const Rendered id = render("mono-48k", "t-identity", 48000);
  CHECK(id.result.inputPaddingFrames == 24);
  CHECK(id.result.declaredLatency.inputLatencyFrames == 24);
}

// ---------------------------------------------------------------------------
// T-E10: flush — bounded by declared outputLatency; over-flush ⇒ JOB FAILURE
// end-of-render-violation (failure fixture via a test-only dummy engine).
// ---------------------------------------------------------------------------
namespace {

// Test-only dummy: Preserving passthrough that OVER-FLUSHES at finish()
// (declared outputLatency 2, emits 3). Registered ONLY in test binaries
// (architecture §L; origin "test-only").
class OverFlushDummy : public PitchEngine {
 public:
  const char* engineId() const override { return "test.overflush"; }
  void configure(const EngineConfiguration&) override {}
  void prepare(const ProcessContext& ctx) override { ctx_ = ctx; }
  Latency latency() const override { return Latency{0, 2}; }
  ProcessReport process(const AudioBlockView& in, int inFrames, AudioBlockOut& out,
                        int outCapacity, const PitchCurveView&, FrameCount) override {
    (void)in;
    (void)out;
    (void)outCapacity;
    // Sticky exhaustion from the first call (fixture simplicity; §4.2.1 item 7).
    return ProcessReport{inFrames, 0, true};
  }
  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    for (int i = 0; i < 3 && i < outCapacity; ++i) {
      out.channels[0][i] = 0.0;
    }
    return ProcessReport{0, std::min(3, outCapacity), true};
  }
  void reset() override {}

 private:
  ProcessContext ctx_{};
};

std::unique_ptr<PitchEngine> makeOverFlushDummy() { return std::make_unique<OverFlushDummy>(); }

EngineDescriptor overFlushDescriptor() {
  EngineDescriptor d;
  d.info.id = "test.overflush";
  d.info.displayName = "over-flush dummy";
  d.info.version = "0.0-test";
  d.info.usageClass = UsageClass::Prototype;
  d.info.origin = "test-only";
  d.info.license = "n/a";
  d.capabilities.minRatio = 0.5;
  d.capabilities.maxRatio = 2.0;
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.supportedSampleRates = {48000u};
  d.parameterKeys = {};
  d.factory = &makeOverFlushDummy;
  return d;
}

}  // namespace

TEST_CASE_FIXTURE(Fixture, "T-E10: flush bound + over-flush failure fixture") {
  // Real engine: flush <= declared (bound holds by construction).
  const Rendered r = render("mono-48k", "t-r15", 48000);
  CHECK(r.result.flushFrames <= r.result.declaredLatency.outputLatencyFrames);
  std::printf("T-E10: varispeed flush %lld <= declared %lld\n",
              static_cast<long long>(r.result.flushFrames),
              static_cast<long long>(r.result.declaredLatency.outputLatencyFrames));

  // Failure fixture: the over-flushing dummy must FAIL the job with the
  // end-of-render-violation reason while the run continues (§K).
  {
    EngineRegistry testRegistry;
    testRegistry.registerEngine(overFlushDescriptor());
    testRegistry.seal();
    const fs::path exp = tr.makeExperiment("exp-e10b", [] {
      return std::string("id = \"exp-e10b\"\nkind = \"benchmark\"\n[suite]\ninputs  = "
                         "[\"mono-48k\"]\ncurves  = [\"t-identity\"]\nengines = "
                         "[\"test.overflush\"]\nsampleRates = [48000]\nchannels = [1]\n[["
                         "engine_config]]\nengine = \"test.overflush\"\nparams = { }\nseed = "
                         "1\n[output]\nmaster = true\n");
    }());
    const RenderSummary summary = renderExperiment(exp, tr, testRegistry);
    REQUIRE(summary.results.size() == 1);
    CHECK(summary.results[0].status == RenderStatus::Failed);
    CHECK(summary.results[0].failureReason.find("end-of-render-violation") != std::string::npos);
    CHECK(summary.results[0].masterWav == fs::path());  // no WAV on failure
    CHECK(summary.results[0].manifestPath != fs::path());  // failure manifest exists
    std::printf("T-E10 fixture: over-flush dummy failed with '%s'\n",
                summary.results[0].failureReason.c_str());
  }
}

// ---------------------------------------------------------------------------
// T-A1: allocation audit — process()/finish() NEVER allocate (§5 rule 1;
// global new/delete interposition active only around the calls).
// ---------------------------------------------------------------------------
namespace {
std::size_t g_auditAllocs = 0;
bool g_auditActive = false;

struct AuditGuard {
  explicit AuditGuard(bool on) { g_auditActive = on; }
  ~AuditGuard() { g_auditActive = false; }
};

// Passthrough dummy factory (T-E11 Preserving case) at namespace scope so
// the descriptor factory binding can take its address.
class PassthroughDummy : public PitchEngine {
 public:
  const char* engineId() const override { return "test.passthrough"; }
  void configure(const EngineConfiguration&) override {}
  void prepare(const ProcessContext& ctx) override { ctx_ = ctx; }
  Latency latency() const override { return Latency{0, 5}; }
  ProcessReport process(const AudioBlockView& in, int inFrames, AudioBlockOut& out,
                        int outCapacity, const PitchCurveView&, FrameCount) override {
    const int n = std::min(inFrames, outCapacity);
    for (int i = 0; i < n; ++i) {
      out.channels[0][i] = in.channels[0][i];
    }
    // Sticky exhaustion from the first call (fixture simplicity; §4.2.1 item 7).
    return ProcessReport{inFrames, n, true};
  }
  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    for (int i = 0; i < 5 && i < outCapacity; ++i) {
      out.channels[0][i] = 0.0;
    }
    return ProcessReport{0, std::min(5, outCapacity), true};
  }
  void reset() override {}

 private:
  ProcessContext ctx_{};
};

std::unique_ptr<PitchEngine> makePassthroughDummy() {
  return std::make_unique<PassthroughDummy>();
}

}  // namespace


// ---------------------------------------------------------------------------
// T-E11: output-length policy — golden exact deltas for constant ratios
// (designed arithmetic: flush = ceil((K-0.5)/r) vs expectation ceil(K/r));
// Preserving dummy: canonical length EXACTLY.
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(Fixture, "T-E11: output-length policy golden deltas") {
  struct Case {
    const char* curve;
    double ratio;
    int64_t expectedDelta;
  };
  const int64_t N = 24000;
  tr.makeAsset("mono24-48k", 48000, {sineFrames(440.0, 48000, N)});
  // N divisible by every ratio below => M0 = N/r exactly, delta = 0 unless
  // ceil((K-0.5)/r) < ceil(K/r) (e.g. r = 0.25: 94 < 96).
  const std::vector<Case> cases = {
      {"t-identity", 1.0, 0}, {"t-r15", 1.5, 0},  {"t-r075", 0.75, 0},
      {"t-r2", 2.0, 0},       {"t-r8", 8.0, 0},   {"t-r025", 0.25, -2},
  };
  for (const Case& c : cases) {
    CAPTURE(c.curve);
    const Rendered r = render("mono24-48k", c.curve, 48000);
    CHECK(r.result.status == RenderStatus::Ok);
    CHECK(r.result.lengthDeltaFrames == c.expectedDelta);  // golden exact pin
    CHECK(std::abs(r.result.lengthDeltaFrames) <= 4096);   // provisional tolerance
    CHECK(r.result.taints.empty());  // |delta| <= tolerance => no length-policy taint
    std::printf("T-E11: ratio %.2f -> delta %lld (expected %lld), flush %lld / declared %lld\n",
                c.ratio, static_cast<long long>(r.result.lengthDeltaFrames),
                static_cast<long long>(c.expectedDelta),
                static_cast<long long>(r.result.flushFrames),
                static_cast<long long>(r.result.declaredLatency.outputLatencyFrames));
  }

  // Preserving dummy: canonical length N_in + outputLatency EXACTLY (§4.3.3).
  {
    EngineDescriptor d;
    d.info.id = "test.passthrough";
    d.info.displayName = "passthrough dummy";
    d.info.version = "0.0-test";
    d.info.usageClass = UsageClass::Prototype;
    d.info.origin = "test-only";
    d.info.license = "n/a";
    d.capabilities.minRatio = 0.5;
    d.capabilities.maxRatio = 2.0;
    d.capabilities.channelMode = ChannelMode::MonoAndStereo;
    d.capabilities.maxChannels = 2;
    d.capabilities.duration = DurationBehaviour::Preserving;
    d.capabilities.supportedSampleRates = {48000u};
    d.factory = &makePassthroughDummy;
    EngineRegistry testRegistry;
    testRegistry.registerEngine(d);
    testRegistry.seal();

    const fs::path exp = tr.makeExperiment("exp-e11p", [] {
      return std::string("id = \"exp-e11p\"\nkind = \"benchmark\"\n[suite]\ninputs  = "
                         "[\"mono24-48k\"]\ncurves  = [\"t-identity\"]\nengines = "
                         "[\"test.passthrough\"]\nsampleRates = [48000]\nchannels = [1]\n[["
                         "engine_config]]\nengine = \"test.passthrough\"\nparams = { }\nseed = "
                         "1\n[output]\nmaster = true\n");
    }());
    const RenderSummary summary = renderExperiment(exp, tr, testRegistry);
    REQUIRE(summary.results.size() == 1);
    const JobResult& r = summary.results[0];
    CHECK(r.status == RenderStatus::Ok);
    CHECK(r.expectedOutputFrames == N + 5);
    CHECK(r.outputFramesProduced == N + 5);  // EXACT (Preserving policy)
    CHECK(r.lengthDeltaFrames == 0);
    CHECK(r.taints.empty());
  }
}

// GCC's -Wmismatched-new-delete cannot see the whole-binary interposition
// (every allocation funnels through THESE operators at link time), so its
// analysis produces false positives here. Empirical evidence: it fires on
// BOTH compilers we build with — local GCC 14.2 AND the canonical CI
// GCC 13.3.0 (run 36269701944: 18 analysis paths, all at the sized
// operator delete, line 637:64 — the earlier "GCC 13 does not have the
// warning" assumption was wrong and cost that run). Suppressed for ALL
// GCC versions (not Clang: not observed there, and this audit is
// GCC-centric anyway).
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

TEST_CASE("T-A1: process()/finish() never allocate (varispeed, FIR + bypass)") {
  const int64_t N = 8192;
  const auto input = sineFrames(440.0, 48000, N);
  const std::vector<double> ratio15(N, 1.5);  // FIR active (r_max > 1)
  const std::vector<double> ratio10(N, 1.0);  // bypass

  for (const std::vector<double>* ratio : {&ratio15, &ratio10}) {
    CAPTURE(ratio->at(0));
    auto engine = makeVarispeedEngine();
    EngineConfiguration cfg;
    cfg.seed = 0;
    g_auditAllocs = 0;
    const DriveReport dr = driveEngine(
        *engine, cfg, {input}, 48000, *ratio, 4096, 65536,
        /*beforeCall=*/[] { g_auditActive = true; },
        /*afterCall=*/[] { g_auditActive = false; });
    // prepare() MAY allocate; the audit covers only process()/finish().
    CHECK(dr.producedTotal > 0);
    CHECK(g_auditAllocs == 0);  // §5 rule 1 — the audit assertion
    std::printf("T-A1: ratio %.2f — allocations during process()/finish(): %zu\n",
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
    ratio[static_cast<std::size_t>(i)] = 1.0 + 0.5 * std::sin(2.0 * 3.14159265358979323846 *
                                                             24000.0 * static_cast<double>(i) /
                                                             48000.0 / 24000.0);
  }
  EngineConfiguration cfg;
  cfg.seed = 3;

  auto engineA = makeVarispeedEngine();
  const DriveReport a = driveEngine(*engineA, cfg, {input}, 48000, ratio, 4096, 65536);

  engineA->reset();
  const DriveReport b = driveEngine(*engineA, cfg, {input}, 48000, ratio, 4096, 65536);

  auto engineC = makeVarispeedEngine();
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
              ab ? "yes" : "NO", ac ? "yes" : "NO", static_cast<long long>(a.output[0].size()));
}
