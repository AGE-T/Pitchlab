// Pitch Lab — contract + function tests for the external DSP integrations
// (native.world — WORLD v1.0.1; native.signalsmith — Signalsmith Stretch
// 1.4.0). Driven through the PitchEngine contract exactly like the offline
// renderer: configure -> prepare -> process blocks (+ the declared
// input-latency zero padding) -> finish.
//
// Asserted (behaviour, not telemetry):
//   * output length == N_in, all samples finite, level sane;
//   * the measured F0 of the output matches input F0 * ratio;
//   * determinism: two fresh instances are bit-identical, and reset()
//     reproduces a fresh instance bit-identically;
//   * block/capacity independence: a tiny output capacity (7 frames) and a
//     different input block size produce the same output.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <random>
#include <thread>
#include <string>
#include <vector>

#include "core/engine_registry.h"
#include "core/errors.h"
#include "core/pitch_engine.h"
#include "engines/world_engine.h"

using namespace pitchlab;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<double> harmonicStack(double f0, double fs, std::size_t n, int harmonics) {
  std::vector<double> x(n, 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    double v = 0.0;
    for (int h = 1; h <= harmonics; ++h) {
      v += std::sin(2.0 * kPi * f0 * h * static_cast<double>(i) / fs) / h;
    }
    x[i] = 0.3 * v;
  }
  return x;
}

/// Autocorrelation F0 estimate over x[from, from+len): the first normalised
/// ACF peak within 90% of the global maximum (octave-error guarded).
double estimateF0(const std::vector<double>& x, std::size_t from, std::size_t len, double fs) {
  const int minLag = static_cast<int>(fs / 1000.0);
  const int maxLag = static_cast<int>(fs / 60.0);
  std::vector<double> r(static_cast<std::size_t>(maxLag + 2), 0.0);
  double e0 = 0.0;
  for (std::size_t i = 0; i < len; ++i) e0 += x[from + i] * x[from + i];
  if (e0 <= 0.0) return 0.0;
  double best = 0.0;
  for (int lag = minLag; lag <= maxLag + 1; ++lag) {
    double acc = 0.0;
    for (std::size_t i = 0; i + static_cast<std::size_t>(lag) < len; ++i) {
      acc += x[from + i] * x[from + i + static_cast<std::size_t>(lag)];
    }
    r[static_cast<std::size_t>(lag)] = acc / e0;
    if (lag <= maxLag) best = std::max(best, r[static_cast<std::size_t>(lag)]);
  }
  for (int lag = minLag + 1; lag <= maxLag; ++lag) {
    const double a = r[static_cast<std::size_t>(lag - 1)];
    const double b = r[static_cast<std::size_t>(lag)];
    const double c = r[static_cast<std::size_t>(lag + 1)];
    if (b >= a && b >= c && b >= 0.9 * best) {
      const double denom = a - 2.0 * b + c;
      const double shift = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
      return fs / (static_cast<double>(lag) + shift);
    }
  }
  return 0.0;
}

struct RenderResult {
  std::vector<std::vector<double>> out;
  bool ok = true;
};

/// The offline-renderer protocol (block exchange + zero padding + finish).
// `cv` must outlive the engine's job (the contract: the SAME curve object
// for every process() call, across reset()).
RenderResult render(PitchEngine& eng, const std::vector<std::vector<double>>& in,
                    const PitchCurveView& cv, double fs, int block, int outCap,
                    bool prepare = true) {
  const int ch = static_cast<int>(in.size());
  const FrameCount n = static_cast<FrameCount>(in[0].size());
  ProcessContext ctx;
  ctx.sampleRate = fs;
  ctx.channels = ch;
  ctx.maxBlockFrames = block;
  ctx.totalInputFrames = n;
  ctx.curve = &cv;
  if (prepare) eng.prepare(ctx);
  const FrameCount pad = eng.latency().inputLatencyFrames;
  const FrameCount total = n + pad;
  RenderResult res;
  res.out.assign(static_cast<std::size_t>(ch), {});
  std::vector<std::vector<double>> inBlk(static_cast<std::size_t>(ch), std::vector<double>(block));
  std::vector<std::vector<double>> outBlk(static_cast<std::size_t>(ch),
                                          std::vector<double>(static_cast<std::size_t>(outCap)));
  std::vector<const double*> ip(static_cast<std::size_t>(ch));
  std::vector<double*> op(static_cast<std::size_t>(ch));
  FrameCount pos = 0;
  int stall = 0;
  while (pos < total && stall < 100000) {
    const int take = static_cast<int>(std::min<FrameCount>(block, total - pos));
    for (int c = 0; c < ch; ++c) {
      for (int i = 0; i < take; ++i) {
        const FrameCount f = pos + i;
        inBlk[c][static_cast<std::size_t>(i)] = f < n ? in[c][static_cast<std::size_t>(f)] : 0.0;
      }
      ip[c] = inBlk[c].data();
      op[c] = outBlk[c].data();
    }
    AudioBlockView iv{ip.data(), ch, take};
    AudioBlockOut ov{op.data(), ch, outCap};
    const ProcessReport rep = eng.process(iv, take, ov, outCap, cv, pos);
    for (int c = 0; c < ch; ++c) {
      res.out[c].insert(res.out[c].end(), outBlk[c].begin(),
                        outBlk[c].begin() + rep.outputFramesProduced);
    }
    // re-offer the unconsumed remainder (partial consumption is legal)
    pos += rep.inputFramesConsumed;
    stall = (rep.inputFramesConsumed == 0 && rep.outputFramesProduced == 0) ? stall + 1 : 0;
  }
  // finish: drain in chunks of outCap (a single finish() call is the
  // contract — give it enough capacity for the whole declared flush)
  {
    const int flushCap = static_cast<int>(eng.latency().outputLatencyFrames) + 1;
    std::vector<std::vector<double>> fb(static_cast<std::size_t>(ch),
                                        std::vector<double>(static_cast<std::size_t>(flushCap)));
    for (int c = 0; c < ch; ++c) op[c] = fb[c].data();
    AudioBlockOut ov{op.data(), ch, flushCap};
    const ProcessReport rep = eng.finish(ov, flushCap);
    for (int c = 0; c < ch; ++c) {
      res.out[c].insert(res.out[c].end(), fb[c].begin(), fb[c].begin() + rep.outputFramesProduced);
    }
  }
  res.ok = stall < 100000;
  return res;
}

std::unique_ptr<PitchEngine> make(const char* id, EngineConfiguration cfg) {
  static EngineRegistry reg = [] {
    EngineRegistry r;
    registerProductionEngines(r);
    r.seal();
    return r;
  }();
  const EngineDescriptor* d = reg.findById(id);
  REQUIRE(d != nullptr);
  auto e = d->factory();
  e->configure(cfg);
  return e;
}

double rms(const std::vector<double>& x, std::size_t from, std::size_t len) {
  double s = 0.0;
  for (std::size_t i = 0; i < len; ++i) s += x[from + i] * x[from + i];
  return std::sqrt(s / static_cast<double>(len));
}

void checkPitchShift(const char* id, EngineConfiguration cfg, double ratio, int channels) {
  const double fs = 48000.0;
  const std::size_t n = 48000;  // 1 s
  const double f0 = 220.0;
  std::vector<std::vector<double>> in(static_cast<std::size_t>(channels),
                                      harmonicStack(f0, fs, n, 6));
  std::vector<double> curve(n, ratio);
  PitchCurveView cv;
  cv.ratio = curve.data();
  cv.frames = static_cast<FrameCount>(n);
  cv.sampleRate = fs;
  auto e1 = make(id, cfg);
  const RenderResult r1 = render(*e1, in, cv, fs, 512, 4096);
  REQUIRE(r1.ok);
  for (int c = 0; c < channels; ++c) {
    REQUIRE(r1.out[c].size() == n);
    for (double v : r1.out[c]) REQUIRE(std::isfinite(v));
  }
  const std::size_t from = n / 2;
  const std::size_t len = 8192;
  const double inRms = rms(in[0], from, len);
  const double outRms = rms(r1.out[0], from, len);
  CAPTURE(id);
  CAPTURE(ratio);
  CAPTURE(outRms / inRms);
  CHECK(outRms > 0.25 * inRms);
  CHECK(outRms < 4.0 * inRms);
  const double got = estimateF0(r1.out[0], from, len, fs);
  CAPTURE(got);
  CHECK(std::fabs(got - f0 * ratio) / (f0 * ratio) < 0.02);

  // determinism (fresh instance) + reset reproduction
  auto e2 = make(id, cfg);
  const RenderResult r2 = render(*e2, in, cv, fs, 512, 4096);
  CHECK(r2.out == r1.out);
  e2->reset();
  const RenderResult r3 = render(*e2, in, cv, fs, 512, 4096, /*prepare=*/false);
  CHECK(r3.out == r1.out);
  // block/capacity independence: tiny output capacity + another block size
  auto e4 = make(id, cfg);
  const RenderResult r4 = render(*e4, in, cv, fs, 333, 7);
  CHECK(r4.out == r1.out);
}

}  // namespace

TEST_CASE("native.signalsmith: +7 st / -5 st, mono + stereo, deterministic, block-independent") {
  EngineConfiguration cfg;
  cfg.seed = 12345;
  checkPitchShift("native.signalsmith", cfg, std::exp2(7.0 / 12.0), 1);
  checkPitchShift("native.signalsmith", cfg, std::exp2(-5.0 / 12.0), 2);
  EngineConfiguration cheaper;
  cheaper.seed = 1;
  cheaper.parameters = {{"formant_ratio", ParameterValue{1.0}},
                        {"preserve_formants", ParameterValue{false}},
                        {"preset", ParameterValue{std::string("cheaper")}}};
  checkPitchShift("native.signalsmith", cheaper, std::exp2(4.0 / 12.0), 1);
}

TEST_CASE("native.world: +5 st / -7 st (DIO), +3 st (Harvest), deterministic, block-independent") {
  EngineConfiguration cfg;
  cfg.seed = 7;
  checkPitchShift("native.world", cfg, std::exp2(5.0 / 12.0), 1);
  checkPitchShift("native.world", cfg, std::exp2(-7.0 / 12.0), 2);
  EngineConfiguration harvest;
  harvest.seed = 7;
  harvest.parameters = {{"f0_method", ParameterValue{std::string("harvest")}}};
  checkPitchShift("native.world", harvest, std::exp2(3.0 / 12.0), 1);
}

TEST_CASE("external engines: configuration validation rejects out-of-domain values") {
  {
    EngineConfiguration bad;
    bad.parameters = {{"preset", ParameterValue{std::string("fast")}}};
    CHECK_THROWS(make("native.signalsmith", bad));
  }
  {
    EngineConfiguration bad;
    bad.parameters = {{"formant_ratio", ParameterValue{8.0}}};
    CHECK_THROWS(make("native.signalsmith", bad));
  }
  {
    EngineConfiguration bad;
    bad.parameters = {{"f0_floor_hz", ParameterValue{300.0}}};
    CHECK_THROWS(make("native.world", bad));
  }
  {
    EngineConfiguration bad;
    bad.parameters = {{"f0_method", ParameterValue{std::string("yin")}}};
    CHECK_THROWS(make("native.world", bad));
  }
}

TEST_CASE("external engines: adapter-like drive (4096 blocks, huge capacity, varying ratio, reset reuse)") {
  const double fs = 48000.0;
  const std::size_t n = static_cast<std::size_t>(fs * 3.0);
  std::vector<std::vector<double>> in(2, harmonicStack(180.0, fs, n, 5));
  std::vector<double> curve(n);
  for (std::size_t i = 0; i < n; ++i) {
    curve[i] = std::exp2((4.0 * std::sin(2.0 * kPi * 0.7 * static_cast<double>(i) / fs)) / 12.0);
  }
  PitchCurveView cv;
  cv.ratio = curve.data();
  cv.frames = static_cast<FrameCount>(n);
  cv.sampleRate = fs;
  for (const char* id : {"native.world", "native.signalsmith"}) {
    CAPTURE(id);
    EngineConfiguration cfg;
    cfg.seed = 3;
    auto e = make(id, cfg);
    const RenderResult a = render(*e, in, cv, fs, 4096, 200000);
    REQUIRE(a.out[0].size() == n);
    e->reset();
    const RenderResult b = render(*e, in, cv, fs, 4096, 200000, /*prepare=*/false);
    CHECK(b.out == a.out);
  }
}

TEST_CASE("external engines: identical stereo channels stay (near-)identical") {
  const double fs = 48000.0;
  const std::size_t n = 48000;
  std::vector<std::vector<double>> in(2, harmonicStack(220.0, fs, n, 6));
  std::vector<double> curve(n, std::exp2(5.0 / 12.0));
  PitchCurveView cv;
  cv.ratio = curve.data();
  cv.frames = static_cast<FrameCount>(n);
  cv.sampleRate = fs;
  for (const char* id : {"native.world", "native.signalsmith"}) {
    CAPTURE(id);
    EngineConfiguration cfg;
    cfg.seed = 9;
    auto e = make(id, cfg);
    const RenderResult r = render(*e, in, cv, fs, 512, 4096);
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < n; ++i) maxDiff = std::max(maxDiff, std::fabs(r.out[0][i] - r.out[1][i]));
    MESSAGE(id << " max |L-R| = " << maxDiff);
    // WORLD analyses channels independently: exact. Signalsmith links the
    // channels' phase handling: measured 1.6e-4 (-76 dB) — benign.
    if (std::string(id) == "native.world") {
      CHECK(maxDiff == 0.0);
    } else {
      CHECK(maxDiff < 1e-3);
    }
  }
}

TEST_CASE("pure sine +12 st: level behaviour of envelope-preserving vs plain transposition") {
  const double fs = 48000.0;
  const std::size_t n = 96000;
  std::vector<std::vector<double>> in(1, std::vector<double>(n));
  for (std::size_t i = 0; i < n; ++i) in[0][i] = 0.5 * std::sin(2.0 * kPi * 220.0 * i / fs);
  std::vector<double> curve(n, 2.0);
  PitchCurveView cv{curve.data(), static_cast<FrameCount>(n), fs};
  for (bool preserve : {true, false}) {
    for (const char* id : {"native.world", "native.signalsmith"}) {
      EngineConfiguration cfg;
      cfg.seed = 1;
      if (std::string(id) == "native.signalsmith") cfg.parameters = {{"preserve_formants", ParameterValue{preserve}}};
      else if (!preserve) continue;
      auto e = make(id, cfg);
      const RenderResult r = render(*e, in, cv, fs, 512, 4096);
      const double ratio = rms(r.out[0], n / 2, 16384) / rms(in[0], n / 2, 16384);
      MESSAGE(id << " preserve=" << preserve << " rms ratio=" << ratio);
      if (std::string(id) == "native.world") {
        // source-filter behaviour, documented (vst_audio_path_test
        // worldEnvelopeLimit): the carrier lands in the envelope trough
        CHECK(ratio < 0.2);
        CHECK(std::fabs(estimateF0(r.out[0], n / 2, 8192, fs) - 440.0) < 9.0);
      } else if (!preserve) {
        CHECK(ratio > 0.8);  // plain transposition keeps the level
      }
    }
  }
}

namespace {
/// Sine frequency from interpolated rising zero crossings over [from, to):
/// sub-millicent resolution on a steady sine (used for the upstream
/// signalsmith-stretch #31/#32 pitch-accuracy regressions).
double zeroCrossFreq(const std::vector<double>& x, std::size_t from, std::size_t to, double fs) {
  double first = -1.0, last = -1.0;
  int count = 0;
  for (std::size_t i = from + 1; i < to; ++i) {
    if (x[i - 1] < 0.0 && x[i] >= 0.0) {
      const double t = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
      if (first < 0.0) first = t;
      last = t;
      ++count;
    }
  }
  return count > 1 ? fs * static_cast<double>(count - 1) / (last - first) : 0.0;
}
}  // namespace

TEST_CASE("native.signalsmith: pitch accuracy in cents (upstream issues #31/#32 regression)") {
  const double fs = 48000.0;
  const std::size_t n = static_cast<std::size_t>(fs * 4.0);
  struct Row { double f0; double cents; };
  const Row rows[] = {{220.0, 100.0}, {220.0, -100.0}, {220.0, 25.0}, {220.0, -25.0},
                      {55.0, -100.0}, {440.0, 300.0}, {220.0, 1200.0}, {220.0, -1200.0},
                      {220.0, -2400.0}};
  double worst = 0.0;
  for (const Row& r : rows) {
    std::vector<std::vector<double>> in(1, std::vector<double>(n));
    for (std::size_t i = 0; i < n; ++i) in[0][i] = 0.5 * std::sin(2.0 * kPi * r.f0 * i / fs);
    const double ratio = std::exp2(r.cents / 1200.0);
    std::vector<double> curve(n, ratio);
    PitchCurveView cv{curve.data(), static_cast<FrameCount>(n), fs};
    EngineConfiguration cfg;
    cfg.seed = 1;
    auto e = make("native.signalsmith", cfg);
    const RenderResult out = render(*e, in, cv, fs, 1024, 4096);
    const double got = zeroCrossFreq(out.out[0], n / 4, n - n / 8, fs);
    const double errC = 1200.0 * std::log2(got / (r.f0 * ratio));
    MESSAGE("f0=" << r.f0 << " shift=" << r.cents << "c -> " << got << " Hz, error " << errC << " c");
    worst = std::max(worst, std::fabs(errC));
  }
  MESSAGE("worst |error| = " << worst << " c");
  CHECK(worst < 1.0);  // upstream HEAD: up to +42 c (55 Hz, -100 c); patched: ~0
}

TEST_CASE("native.world: offline throughput (realtime factor, stereo 48 kHz)") {
  const double fs = 48000.0;
  const std::size_t n = static_cast<std::size_t>(fs * 6.0);
  std::vector<std::vector<double>> in(2, harmonicStack(150.0, fs, n, 8));
  for (std::size_t i = 0; i < n; ++i) in[1][i] *= 0.9;
  std::vector<double> curve(n, std::exp2(-10.0 / 12.0));
  PitchCurveView cv{curve.data(), static_cast<FrameCount>(n), fs};
  for (const char* method : {"dio", "harvest"}) {
    EngineConfiguration cfg;
    cfg.parameters = {{"f0_method", ParameterValue{std::string(method)}}};
    auto e = make("native.world", cfg);
    const auto t0 = std::chrono::steady_clock::now();
    const RenderResult r = render(*e, in, cv, fs, 256, 256);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double rtf = sec / (static_cast<double>(n) / fs);
    MESSAGE("native.world " << method << " stereo: realtime factor " << rtf);
    CHECK(r.out[0].size() == n);
    CHECK(rtf < 1.0);  // must run faster than realtime on the build machine
  }
}

#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/dio.h"
#include "world/harvest.h"
#include "world/stonemask.h"
#include "world/synthesis.h"

TEST_CASE("PROFILE world stages on one chunk region" * doctest::skip()) {  // run with -nr / -ns
  const int fs = 48000;
  const int len = static_cast<int>(0.18 * fs);
  std::vector<double> x = harmonicStack(150.0, fs, static_cast<std::size_t>(len), 8);
  const int f0Len = GetSamplesForDIO(fs, len, 5.0);
  std::vector<double> tpos(f0Len + 2), f0(f0Len + 2), f0r(f0Len + 2);
  CheapTrickOption ct;
  InitializeCheapTrickOption(fs, &ct);
  ct.f0_floor = 71.0;
  ct.fft_size = GetFFTSizeForCheapTrick(fs, &ct);
  const int bins = ct.fft_size / 2 + 1;
  std::vector<double> spd(static_cast<std::size_t>(f0Len) * bins), apd(spd.size());
  std::vector<double*> sp(f0Len), ap(f0Len);
  for (int i = 0; i < f0Len; ++i) { sp[i] = spd.data() + i * bins; ap[i] = apd.data() + i * bins; }
  std::vector<double> y(len);
  auto ms = [](auto t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
  auto t = std::chrono::steady_clock::now();
  HarvestOption ho; InitializeHarvestOption(&ho); ho.frame_period = 5.0; ho.f0_floor = 71; ho.f0_ceil = 800;
  Harvest(x.data(), len, fs, &ho, tpos.data(), f0.data());
  const double tH = ms(t); t = std::chrono::steady_clock::now();
  DioOption dop; InitializeDioOption(&dop); dop.frame_period = 5.0; dop.f0_floor = 71; dop.f0_ceil = 800; dop.speed = 1;
  Dio(x.data(), len, fs, &dop, tpos.data(), f0r.data());
  StoneMask(x.data(), len, fs, tpos.data(), f0r.data(), f0Len, f0.data());
  const double tD = ms(t); t = std::chrono::steady_clock::now();
  CheapTrick(x.data(), len, fs, tpos.data(), f0.data(), f0Len, &ct, sp.data());
  const double tC = ms(t); t = std::chrono::steady_clock::now();
  D4COption d4; InitializeD4COption(&d4);
  D4C(x.data(), len, fs, tpos.data(), f0.data(), f0Len, ct.fft_size, &d4, ap.data());
  const double t4 = ms(t); t = std::chrono::steady_clock::now();
  Synthesis(f0.data(), f0Len, sp.data(), ap.data(), ct.fft_size, 5.0, fs, len, y.data());
  const double tS = ms(t);
  MESSAGE("fft=" << ct.fft_size << " frames=" << f0Len << " | harvest " << tH << " ms, dio+stonemask " << tD
          << " ms, cheaptrick " << tC << " ms, d4c " << t4 << " ms, synthesis " << tS << " ms");
}

// ===========================================================================
// native.world — realtime safety, failure handling, pool bound, stress,
// the stereo F0 model and the sample-rate declaration (2026-11 P0 work)
// ===========================================================================

namespace {

std::vector<double> sineStack(double f0, double fs, std::size_t n, int harmonics, double sign = 1.0) {
  std::vector<double> x = harmonicStack(f0, fs, n, harmonics);
  if (sign < 0.0) {
    for (double& v : x) v = -v;
  }
  return x;
}

struct WorldJob {
  std::vector<double> curve;
  PitchCurveView cv{};
  ProcessContext ctx{};
  void init(std::size_t n, double fs, int ch, int block, double ratio, bool realtime) {
    curve.assign(n, ratio);
    cv = PitchCurveView{curve.data(), static_cast<FrameCount>(n), fs};
    ctx.sampleRate = fs;
    ctx.channels = ch;
    ctx.maxBlockFrames = block;
    ctx.totalInputFrames = static_cast<FrameCount>(n);
    ctx.curve = &cv;
    ctx.realtime = realtime;
  }
};

struct WorldHooksOff {
  ~WorldHooksOff() {
    pitchlab::testing::setWorldFaultInjectionChunk(-1);
    pitchlab::testing::setWorldChunkDelayMs(0);
  }
};

}  // namespace

TEST_CASE("native.world realtime: process() never waits for a slow pool (starvation is reported)") {
  WorldHooksOff guard;
  const double fs = 48000.0;
  const std::size_t n = static_cast<std::size_t>(fs * 1.0);
  std::vector<double> x = harmonicStack(150.0, fs, n, 6);
  WorldJob job;
  job.init(n, fs, 1, 256, 1.2, /*realtime=*/true);
  auto e = make("native.world", EngineConfiguration{});
  e->prepare(job.ctx);
  pitchlab::testing::setWorldChunkDelayMs(250);  // every chunk takes >= 250 ms
  std::vector<double> outBuf(4096);
  double* op[1] = {outBuf.data()};
  const double* ip[1] = {nullptr};
  double worstMs = 0.0;
  bool sawStarved = false;
  FrameCount pos = 0;
  const FrameCount total = static_cast<FrameCount>(n) + e->latency().inputLatencyFrames;
  std::vector<double> blockIn(256, 0.0);
  while (pos < total) {
    const int take = static_cast<int>(std::min<FrameCount>(256, total - pos));
    for (int i = 0; i < take; ++i) {
      blockIn[static_cast<std::size_t>(i)] =
          pos + i < static_cast<FrameCount>(n) ? x[static_cast<std::size_t>(pos + i)] : 0.0;
    }
    ip[0] = blockIn.data();
    AudioBlockView iv{ip, 1, take};
    AudioBlockOut ov{op, 1, 4096};
    const auto t0 = std::chrono::steady_clock::now();
    const ProcessReport rep = e->process(iv, take, ov, 4096, job.cv, pos);
    worstMs = std::max(worstMs, std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count());
    sawStarved = sawStarved || rep.starved;
    if (rep.inputFramesConsumed == 0) break;  // ring full: a host would re-offer later
    pos += rep.inputFramesConsumed;
  }
  MESSAGE("worst process() call: " << worstMs << " ms (chunk delay 250 ms)");
  CHECK(sawStarved);
  CHECK(worstMs < 50.0);  // the pre-fix emit() waited the full chunk time here
  pitchlab::testing::setWorldChunkDelayMs(0);
  e.reset();  // destruction while chunks are in flight must not hang or crash
}

TEST_CASE("native.world: a worker failure becomes EngineException (never an abort); reset recovers") {
  WorldHooksOff guard;
  const double fs = 48000.0;
  const std::size_t n = static_cast<std::size_t>(fs * 1.0);
  std::vector<std::vector<double>> in(1, harmonicStack(150.0, fs, n, 6));
  std::vector<double> curve(n, 1.25);
  PitchCurveView cv{curve.data(), static_cast<FrameCount>(n), fs};
  auto ref = make("native.world", EngineConfiguration{});
  const RenderResult good = render(*ref, in, cv, fs, 512, 4096);
  pitchlab::testing::setWorldFaultInjectionChunk(3);  // chunk 3 throws on the pool thread
  auto e = make("native.world", EngineConfiguration{});
  CHECK_THROWS_AS(render(*e, in, cv, fs, 512, 4096), EngineException);
  pitchlab::testing::setWorldFaultInjectionChunk(-1);
  e->reset();  // reset clears the failure: bit-identical to a fresh engine
  const RenderResult again = render(*e, in, cv, fs, 512, 4096, /*prepare=*/false);
  CHECK(again.out == good.out);
  pitchlab::testing::setWorldFaultInjectionChunk(5);
  auto f = make("native.world", EngineConfiguration{});
  CHECK_THROWS_AS(render(*f, in, cv, fs, 512, 4096), EngineException);
  pitchlab::testing::setWorldFaultInjectionChunk(-1);
  const RenderResult re = render(*f, in, cv, fs, 512, 4096, /*prepare=*/true);  // re-prepare
  CHECK(re.out == good.out);
}

TEST_CASE("native.world: the shared pool is bounded across many engines and threads") {
  const double fs = 48000.0;
  const std::size_t n = static_cast<std::size_t>(fs * 0.5);
  CHECK(pitchlab::testing::worldPoolLiveThreads() == 0);  // nothing attached
  const int bound = pitchlab::testing::worldPoolSize();
  CHECK(bound >= 1);
  CHECK(bound <= 8);
  {
    std::vector<WorldJob> jobs(10);
    std::vector<std::unique_ptr<PitchEngine>> engines;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
      jobs[i].init(n, fs, 2, 512, 1.1, false);
      engines.push_back(make("native.world", EngineConfiguration{}));
      engines.back()->prepare(jobs[i].ctx);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    MESSAGE("10 prepared engines -> live pool threads " << pitchlab::testing::worldPoolLiveThreads()
                                                        << " (bound " << bound << ")");
    CHECK(pitchlab::testing::worldPoolLiveThreads() <= bound);
    // three concurrent renders ("plug-in instances"): results stay deterministic
    std::vector<std::vector<double>> in(2, harmonicStack(180.0, fs, n, 6));
    std::vector<double> curve(n, 0.9);
    PitchCurveView cv{curve.data(), static_cast<FrameCount>(n), fs};
    auto refE = make("native.world", EngineConfiguration{});
    const RenderResult reference = render(*refE, in, cv, fs, 512, 4096);
    std::vector<RenderResult> results(3);
    std::vector<std::thread> threads;
    for (int t = 0; t < 3; ++t) {
      threads.emplace_back([&, t] {
        auto eng = make("native.world", EngineConfiguration{});
        results[static_cast<std::size_t>(t)] = render(*eng, in, cv, fs, 256 + 128 * t, 4096);
      });
    }
    for (auto& th : threads) th.join();
    for (const auto& r : results) CHECK(r.out == reference.out);
    CHECK(pitchlab::testing::worldPoolLiveThreads() <= bound);
  }
  CHECK(pitchlab::testing::worldPoolLiveThreads() == 0);  // the last detach joined the pool
}

TEST_CASE("native.world: reset / re-prepare / destruction stress (in-flight chunks)") {
  WorldHooksOff guard;
  const double fs = 48000.0;
  const std::size_t n = static_cast<std::size_t>(fs * 0.6);
  std::vector<double> x = harmonicStack(200.0, fs, n, 5);
  for (int iter = 0; iter < 30; ++iter) {
    pitchlab::testing::setWorldChunkDelayMs(iter % 3 == 0 ? 15 : 0);
    WorldJob job;
    job.init(n, fs, 1 + (iter & 1), 512, 1.0 + 0.01 * iter, iter % 2 == 0);
    auto e = make("native.world", EngineConfiguration{});
    e->prepare(job.ctx);
    std::vector<double> blk(512), o0(4096), o1(4096);
    const double* ip[2] = {blk.data(), blk.data()};
    double* op[2] = {o0.data(), o1.data()};
    FrameCount pos = 0;
    for (int b = 0; b < 12; ++b) {
      for (int i = 0; i < 512; ++i) blk[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>((pos + i) % n)];
      AudioBlockView iv{ip, job.ctx.channels, 512};
      AudioBlockOut ov{op, job.ctx.channels, 4096};
      pos += e->process(iv, 512, ov, 4096, job.cv, pos).inputFramesConsumed;
    }
    if (iter % 3 == 1) e->reset();
    if (iter % 3 == 2) e->prepare(job.ctx);
    // destruction here (possibly with chunks in flight)
  }
  pitchlab::testing::setWorldChunkDelayMs(0);
  CHECK(pitchlab::testing::worldPoolLiveThreads() == 0);
}

TEST_CASE("native.world stereo F0 model (dual mono): in-phase, anti-phase, different L/R, silence, noise") {
  const double fs = 48000.0;
  const std::size_t n = static_cast<std::size_t>(fs * 1.5);
  const double ratio = std::exp2(4.0 / 12.0);
  std::vector<double> curve(n, ratio);
  PitchCurveView cv{curve.data(), static_cast<FrameCount>(n), fs};
  const std::size_t from = n / 2, len = 8192;
  for (const char* method : {"dio", "harvest"}) {
    CAPTURE(std::string(method));
    EngineConfiguration cfg;
    cfg.parameters = {{"f0_method", ParameterValue{std::string(method)}}};
    {  // same F0, in phase -> identical channels
      std::vector<std::vector<double>> in{sineStack(200.0, fs, n, 6), sineStack(200.0, fs, n, 6)};
      auto e = make("native.world", cfg);
      const RenderResult r = render(*e, in, cv, fs, 512, 4096);
      CHECK(r.out[0] == r.out[1]);
      CHECK(std::fabs(estimateF0(r.out[0], from, len, fs) - 200.0 * ratio) / (200.0 * ratio) < 0.02);
    }
    {  // same F0, anti-phase -> both channels keep their pitch and level
      std::vector<std::vector<double>> in{sineStack(200.0, fs, n, 6), sineStack(200.0, fs, n, 6, -1.0)};
      auto e = make("native.world", cfg);
      const RenderResult r = render(*e, in, cv, fs, 512, 4096);
      for (int c = 0; c < 2; ++c) {
        CHECK(std::fabs(estimateF0(r.out[static_cast<std::size_t>(c)], from, len, fs) - 200.0 * ratio) /
                  (200.0 * ratio) <
              0.02);
        CHECK(rms(r.out[static_cast<std::size_t>(c)], from, len) >
              0.25 * rms(in[static_cast<std::size_t>(c)], from, len));
      }
    }
    {  // different F0 per channel (L 220, R 311) -> each channel follows its own
      std::vector<std::vector<double>> in{sineStack(220.0, fs, n, 6), sineStack(311.0, fs, n, 6)};
      auto e = make("native.world", cfg);
      const RenderResult r = render(*e, in, cv, fs, 512, 4096);
      const double gl = estimateF0(r.out[0], from, len, fs);
      const double gr = estimateF0(r.out[1], from, len, fs);
      MESSAGE(method << " L/R " << gl << " / " << gr << " Hz (expected " << 220.0 * ratio << " / "
                     << 311.0 * ratio << ")");
      CHECK(std::fabs(gl - 220.0 * ratio) / (220.0 * ratio) < 0.02);
      CHECK(std::fabs(gr - 311.0 * ratio) / (311.0 * ratio) < 0.02);
    }
    {  // silence -> finite, silent
      std::vector<std::vector<double>> in(2, std::vector<double>(n, 0.0));
      auto e = make("native.world", cfg);
      const RenderResult r = render(*e, in, cv, fs, 512, 4096);
      for (double v : r.out[0]) REQUIRE(std::isfinite(v));
      CHECK(rms(r.out[0], 0, n) < 1e-3);
    }
    {  // white noise -> finite, not silent, bounded
      std::mt19937 rng(7);
      std::normal_distribution<double> nd(0.0, 0.1);
      std::vector<double> noise(n);
      for (double& v : noise) v = nd(rng);
      std::vector<std::vector<double>> in{noise};
      auto e = make("native.world", cfg);
      const RenderResult r = render(*e, in, cv, fs, 512, 4096);
      for (double v : r.out[0]) REQUIRE(std::isfinite(v));
      const double ro = rms(r.out[0], from, len), ri = rms(noise, from, len);
      CHECK(ro > 0.05 * ri);
      CHECK(ro < 5.0 * ri);
    }
  }
}

TEST_CASE("native.world sample rates: the declared set works, everything else is rejected") {
  for (double fs : {44100.0, 48000.0, 88200.0, 96000.0}) {
    CAPTURE(fs);
    const std::size_t n = static_cast<std::size_t>(fs * 1.0);
    std::vector<std::vector<double>> in(1, harmonicStack(180.0, fs, n, 6));
    std::vector<double> curve(n, std::exp2(5.0 / 12.0));
    PitchCurveView cv{curve.data(), static_cast<FrameCount>(n), fs};
    auto e = make("native.world", EngineConfiguration{});
    const RenderResult r = render(*e, in, cv, fs, 512, 4096);
    REQUIRE(r.out[0].size() == n);
    const double want = 180.0 * std::exp2(5.0 / 12.0);
    CHECK(std::fabs(estimateF0(r.out[0], n / 2, 8192, fs) - want) / want < 0.02);
  }
  for (double fs : {22050.0, 32000.0, 176400.0, 192000.0}) {
    CAPTURE(fs);
    WorldJob job;
    job.init(4096, fs, 1, 512, 1.0, false);
    auto e = make("native.world", EngineConfiguration{});
    CHECK_THROWS_AS(e->prepare(job.ctx), ConfigError);
  }
}

// ===========================================================================
// native.signalsmith — automation TIMING on the aligned output timeline
// (the library centres pitch changes at its PROCESSING time, inputLatency()
// behind the newest input — README "Automation"). A ratio change commanded
// for input frame T must appear at aligned output frame T.
// ===========================================================================

namespace {

/// Instantaneous frequency of a (shifted) sine from rising zero crossings in
/// [c - w/2, c + w/2).
double localFreq(const std::vector<double>& x, std::size_t c, std::size_t w, double fs) {
  const std::size_t a = c > w / 2 ? c - w / 2 : 0;
  const std::size_t b = std::min(x.size(), a + w);
  return zeroCrossFreq(x, a, b, fs);
}

/// First frame (scanning in `hop` steps from `from`) where the local frequency
/// crosses `threshold` in the direction from f0 to f1.
int64_t crossingFrame(const std::vector<double>& x, std::size_t from, std::size_t to, double fs,
                      double f0, double f1) {
  const double thr = 0.5 * (f0 + f1);
  for (std::size_t c = from; c < to; c += 32) {
    const double f = localFreq(x, c, 1024, fs);
    if (f <= 0.0) continue;
    if ((f1 > f0 && f > thr) || (f1 < f0 && f < thr)) return static_cast<int64_t>(c);
  }
  return -1;
}

RenderResult renderSs(const std::vector<double>& in, const std::vector<double>& curve, double fs,
                      int block, EngineConfiguration cfg = {}) {
  PitchCurveView cv{curve.data(), static_cast<FrameCount>(curve.size()), fs};
  cfg.seed = 1;
  auto e = make("native.signalsmith", cfg);
  std::vector<std::vector<double>> ins{in};
  return render(*e, ins, cv, fs, block, 4096);
}

}  // namespace

TEST_CASE("native.signalsmith automation timing: steps land at their input frame (all rates, blocks)") {
  for (double fs : {44100.0, 48000.0, 96000.0}) {
    for (int block : {64, 512, 3000}) {
      CAPTURE(fs);
      CAPTURE(block);
      const std::size_t n = static_cast<std::size_t>(fs * 3.0);
      const double f = 330.0;
      std::vector<double> x(n);
      for (std::size_t i = 0; i < n; ++i) x[i] = 0.5 * std::sin(2.0 * kPi * f * static_cast<double>(i) / fs);
      // up step at 1.0 s, down step back at 2.0 s (repeated stepping)
      const std::size_t t1 = static_cast<std::size_t>(fs * 1.0), t2 = static_cast<std::size_t>(fs * 2.0);
      std::vector<double> curve(n, 1.0);
      for (std::size_t i = t1; i < t2; ++i) curve[i] = 1.5;
      const RenderResult r = renderSs(x, curve, fs, block);
      REQUIRE(r.out[0].size() == n);
      const int64_t up = crossingFrame(r.out[0], t1 - static_cast<std::size_t>(0.3 * fs),
                                       t1 + static_cast<std::size_t>(0.3 * fs), fs, f, 1.5 * f);
      const int64_t down = crossingFrame(r.out[0], t2 - static_cast<std::size_t>(0.3 * fs),
                                         t2 + static_cast<std::size_t>(0.3 * fs), fs, 1.5 * f, f);
      const double upMs = 1000.0 * static_cast<double>(up - static_cast<int64_t>(t1)) / fs;
      const double downMs = 1000.0 * static_cast<double>(down - static_cast<int64_t>(t2)) / fs;
      MESSAGE("fs " << fs << " block " << block << ": up-step lands " << upMs << " ms, down-step "
                    << downMs << " ms (0 = on time)");
      // measured after the processing-time fix: within +-5.5 ms at every rate
      // and block size (before: 55-67 ms EARLY). Gate: 15 ms (half the
      // 30 ms spectral interval) — a regression to the input-position index
      // fails by ~4x.
      CHECK(std::fabs(upMs) < 15.0);
      CHECK(std::fabs(downMs) < 15.0);
    }
  }
}

TEST_CASE("native.signalsmith automation timing: slow/fast sweeps track the commanded ratio") {
  const double fs = 48000.0;
  const std::size_t n = static_cast<std::size_t>(fs * 4.0);
  const double f = 300.0;
  std::vector<double> x(n);
  for (std::size_t i = 0; i < n; ++i) x[i] = 0.5 * std::sin(2.0 * kPi * f * static_cast<double>(i) / fs);
  struct Sweep {
    const char* name;
    double seconds;
    double fromSt;
    double toSt;
  };
  for (const Sweep sw : {Sweep{"slow up", 3.0, -6.0, 6.0}, Sweep{"slow down", 3.0, 6.0, -6.0},
                         Sweep{"fast up", 0.5, -6.0, 6.0}, Sweep{"fast down", 0.5, 6.0, -6.0}}) {
    CAPTURE(sw.name);
    std::vector<double> curve(n);
    const std::size_t s0 = static_cast<std::size_t>(fs * 0.5);
    const std::size_t s1 = s0 + static_cast<std::size_t>(fs * sw.seconds);
    for (std::size_t i = 0; i < n; ++i) {
      const double u = i < s0 ? 0.0 : (i >= s1 ? 1.0 : static_cast<double>(i - s0) / static_cast<double>(s1 - s0));
      curve[i] = std::exp2((sw.fromSt + (sw.toSt - sw.fromSt) * u) / 12.0);
    }
    const RenderResult r = renderSs(x, curve, fs, 512);
    // mean absolute tracking error (cents) over the sweep, sampled every 10 ms
    double sum = 0.0;
    int cnt = 0;
    for (std::size_t c = s0 + 2048; c + 2048 < s1; c += static_cast<std::size_t>(fs * 0.01)) {
      const double got = localFreq(r.out[0], c, 1024, fs);
      if (got <= 0.0) continue;
      sum += std::fabs(1200.0 * std::log2(got / (f * curve[c])));
      ++cnt;
    }
    const double meanC = cnt > 0 ? sum / cnt : 1e9;
    MESSAGE(sw.name << ": mean tracking error " << meanC << " c over " << cnt << " points");
    // measured: slow 5.6 c / fast 33-34 c after the fix (before: 29.5 c /
    // 136-175 c — the 60 ms early application)
    CHECK(meanC < (sw.seconds < 1.0 ? 60.0 : 10.0));
  }
}

// ===========================================================================
// native.signalsmith formant preservation: automatic base-frequency
// detection (what the engine uses — it cannot know the input's f0) versus
// the library given the true f0 through setFormantBase(). A synthetic vowel
// with a KNOWN smooth envelope is shifted with formants kept; each output
// harmonic's level is compared with the envelope at its new frequency.
// ===========================================================================

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4305)
#endif
#include <signalsmith-stretch/signalsmith-stretch.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace {

/// Four two-pole resonances (700/1220/2600/3400 Hz) — an /a/-like envelope (dB).
double vowelEnvDb(double f) {
  const double formants[4] = {700.0, 1220.0, 2600.0, 3400.0};
  const double bw[4] = {130.0, 70.0, 160.0, 250.0};
  double p = 0.0;
  for (int i = 0; i < 4; ++i) {
    const double d = (f - formants[i]) / (0.5 * bw[i]);
    p += 1.0 / (1.0 + d * d) * (i == 0 ? 1.0 : 0.5);
  }
  return 10.0 * std::log10(p + 1e-6);
}

double goertzelDb(const std::vector<double>& x, std::size_t a, std::size_t n, double f, double fs) {
  double re = 0.0, im = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n - 1));
    const double ph = 2.0 * kPi * f * static_cast<double>(a + i) / fs;
    re += x[a + i] * w * std::cos(ph);
    im -= x[a + i] * w * std::sin(ph);
  }
  return 20.0 * std::log10(std::sqrt(re * re + im * im) + 1e-12);
}

/// RMS dB error of the output harmonics (250..2500 Hz) against the envelope,
/// after matching the total level once.
// mode 0: the raw library, automatic base; 1: the raw library given the
// true f0; 2: the native.signalsmith ENGINE with preserve_formants (its
// tracker-fed base)
double formantErrorDb(int mode, double f0, double st) {
  const double fs = 48000.0;
  const std::size_t n = static_cast<std::size_t>(fs * 3.0);
  std::vector<float> x(n), y(n);
  for (std::size_t i = 0; i < n; ++i) {
    double v = 0.0;
    for (int h = 1; h * f0 < 5000.0; ++h) {
      v += std::pow(10.0, vowelEnvDb(h * f0) / 20.0) * std::sin(2.0 * kPi * h * f0 * static_cast<double>(i) / fs);
    }
    x[i] = static_cast<float>(0.05 * v);
  }
  std::vector<double> yd;
  if (mode == 2) {
    std::vector<std::vector<double>> ins{std::vector<double>(x.begin(), x.end())};
    std::vector<double> curve(n, std::exp2(st / 12.0));
    PitchCurveView cv{curve.data(), static_cast<FrameCount>(n), fs};
    EngineConfiguration cfg;
    cfg.seed = 1;
    cfg.parameters = {{"preserve_formants", ParameterValue{true}}};
    auto e = make("native.signalsmith", cfg);
    yd = render(*e, ins, cv, fs, 256, 4096).out[0];
  } else {
    signalsmith::stretch::SignalsmithStretch<float> s(1);
    s.presetDefault(1, static_cast<float>(fs));
    s.setTransposeSemitones(static_cast<float>(st));
    s.setFormantFactor(1.0f, true);
    if (mode == 1) s.setFormantBase(static_cast<float>(f0 / fs));
    for (std::size_t p = 0; p + 256 <= n; p += 256) {
      float* in[1] = {x.data() + p};
      float* out[1] = {y.data() + p};
      s.process(in, 256, out, 256);
    }
    yd.assign(y.begin(), y.end());
  }
  const double f1 = f0 * std::exp2(st / 12.0);
  std::vector<double> errs;
  for (int h = 1; h * f1 < 2500.0; ++h) {
    const double f = h * f1;
    if (f < 250.0) continue;
    errs.push_back(goertzelDb(yd, n / 2, 32768, f, fs) - vowelEnvDb(f));
  }
  double mean = 0.0;
  for (double e : errs) mean += e;
  mean /= static_cast<double>(errs.size());
  double sq = 0.0;
  for (double e : errs) sq += (e - mean) * (e - mean);
  return std::sqrt(sq / static_cast<double>(errs.size()));
}

}  // namespace

TEST_CASE("native.signalsmith formants: tracked base vs automatic detection vs a given f0") {
  double sumAuto = 0.0, sumBase = 0.0, sumEngine = 0.0, worstEngine = 0.0;
  int n = 0;
  for (double f0 : {147.0, 220.0, 294.0}) {
    for (double st : {-5.0, 3.0}) {
      const double eAuto = formantErrorDb(0, f0, st);
      const double eBase = formantErrorDb(1, f0, st);
      const double eEng = formantErrorDb(2, f0, st);
      MESSAGE("f0 " << f0 << " Hz, " << st << " st: formant RMS error  library-auto " << eAuto
                    << " dB | library-given-base " << eBase << " dB | ENGINE (tracked base) " << eEng
                    << " dB");
      sumAuto += eAuto;
      sumBase += eBase;
      sumEngine += eEng;
      worstEngine = std::max(worstEngine, eEng);
      ++n;
    }
  }
  MESSAGE("mean: library-auto " << sumAuto / n << " dB, given base " << sumBase / n
                                << " dB, ENGINE " << sumEngine / n << " dB (worst " << worstEngine
                                << ")");
  // the engine's tracked base must stay close to the given-base reference
  // (the library's own auto detection measured 9.4 dB mean / 22 dB worst)
  CHECK(sumEngine / n < 3.5);
  CHECK(worstEngine < 6.0);
}
