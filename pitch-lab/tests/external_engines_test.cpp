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
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"

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
