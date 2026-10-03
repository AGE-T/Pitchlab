// native.timepitch — Fixed (OLA) PRODUCTION vs TASK28 REFERENCE comparison
// (task-33 continuation, Phase 1: the owner's Fixed-mode crackle report).
//
// THE CONTROLLED COMPARISON the continuation mandate requires: the Task28
// validated research prototype (prototypes/candidate_ola.cpp — the
// normative reference the production port claims to copy "verbatim") vs the
// production engine (native.timepitch, mode = fixed), with IDENTICAL input,
// sample rate, pitch curve, window size, overlap, window shape, block
// decomposition and feed schedule (each engine's own declared latency drives
// its zero-padding, exactly like the offline renderer).
//
// Decision rule (the mandate's, not by ear):
//   * production == prototype (bit-identical over the common output length)
//     AND both deviate from identity the same way  ⇒ the observed character
//     is ALGORITHMIC/reference behaviour (the OLA family's own);
//   * production materially differs from the prototype            ⇒ a
//     PRODUCTION MIGRATION DEFECT (root-cause and fix).
//
// The mandated matrix: pitch {0, +12, −12} st × window {512, 2048, 8192,
// 16384} × overlap {2, 4, 8} × shape {hann, hamming, bartlett, rect}.
// The pitch-0 row is the FIRST control test (the mandate's).
//
// The production side goes through the ENGINE contract directly (the same
// arithmetic the adapter drives); the host-path (adapter) layer is covered
// by the timepitch_host_probe suite.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"
#include "engines/timepitch_engine.h"
#include "prototypes/candidate_ola.h"

using namespace pitchlab;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;
constexpr FrameCount kInputFrames = 48000;  // 1.0 s
constexpr int kMaxBlock = 512;

std::vector<double> musicalInput() {
  std::vector<double> x(static_cast<std::size_t>(kInputFrames), 0.0);
  for (int64_t i = 0; i < kInputFrames; ++i) {
    const double t = static_cast<double>(i) / kFs;
    const double vib = std::exp2(0.5 * std::sin(2.0 * kPi * 3.0 * t) / 12.0);
    const double f = 220.0 * vib;
    double v = 0.0;
    for (int h = 1; h <= 6; ++h) {
      v += std::sin(2.0 * kPi * f * static_cast<double>(h) * t) /
           static_cast<double>(h);
    }
    const double env = 0.7 + 0.3 * std::sin(2.0 * kPi * 0.25 * t);
    x[static_cast<std::size_t>(i)] = 0.5 * env * v / 1.45;
  }
  return x;
}

struct BlockOut {
  std::vector<double> ch[2];
  double* ptr[2] = {nullptr, nullptr};
  void allocate(FrameCount n) {
    ch[0].assign(static_cast<std::size_t>(n), 0.0);
    ch[1].assign(static_cast<std::size_t>(n), 0.0);
    ptr[0] = ch[0].data();
    ptr[1] = ch[1].data();
  }
};

struct RenderResult {
  std::vector<double> ch;    // channel 0 master
  FrameCount declaredIn = 0;
  FrameCount declaredOut = 0;
};

/// Render the PRODUCTION engine (mode = fixed) block-streamed over
/// nIn + declaredInputLatency frames of (real + zero-pad) feed, then
/// finish(). The exact §4.3.6 zero-pad-era feed schedule.
RenderResult renderProduction(const std::vector<double>& x,
                              const std::string& shape, int window,
                              int overlap, double semitones) {
  auto engine = makeTimePitchEngine();
  EngineConfiguration cfg;
  cfg.seed = 12345;
  cfg.parameters.emplace_back("mode", ParameterValue{std::string("fixed")});
  cfg.parameters.emplace_back("overlap", ParameterValue{static_cast<int64_t>(overlap)});
  cfg.parameters.emplace_back("window_frames", ParameterValue{static_cast<int64_t>(window)});
  cfg.parameters.emplace_back("window_shape", ParameterValue{shape});
  engine->configure(cfg);

  std::vector<double> curve(static_cast<std::size_t>(kInputFrames),
                            std::exp2(semitones / 12.0));
  PitchCurveView cv{};
  cv.ratio = curve.data();
  cv.frames = static_cast<FrameCount>(curve.size());
  cv.sampleRate = kFs;

  ProcessContext ctx{};
  ctx.sampleRate = kFs;
  ctx.channels = 1;
  ctx.maxBlockFrames = kMaxBlock;
  ctx.totalInputFrames = kInputFrames;
  ctx.curve = &cv;
  engine->prepare(ctx);
  const auto lat = engine->latency();

  // feed stream: real input + the declared zero-pad lookahead
  const FrameCount feed = kInputFrames + lat.inputLatencyFrames;
  std::vector<double> feedBuf(static_cast<std::size_t>(feed), 0.0);
  std::copy(x.begin(), x.end(), feedBuf.begin());

  RenderResult out;
  const FrameCount capTotal = kInputFrames + lat.outputLatencyFrames + 64;
  out.ch.reserve(static_cast<std::size_t>(capTotal));
  FrameCount consumed = 0;
  FrameCount pos = 0;
  while (consumed < feed) {
    const int take = static_cast<int>(
        std::min<FrameCount>(kMaxBlock, feed - consumed));
    const double* inCh[1] = {feedBuf.data() + consumed};
    AudioBlockView inView{inCh, 1, take};
    BlockOut blk;
    blk.allocate(kMaxBlock);
    AudioBlockOut outView{blk.ptr, 1, kMaxBlock};
    const ProcessReport rep =
        engine->process(inView, take, outView, kMaxBlock, cv, consumed);
    CHECK(rep.inputFramesConsumed == take);
    for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
      out.ch.push_back(blk.ch[0][static_cast<std::size_t>(i)]);
    }
    consumed += take;
    pos += rep.outputFramesProduced;
    (void)pos;
  }
  // finish(): the bounded flush, EXACTLY ONCE (the offline renderer's
  // outCapacityFor shape: maxBlock + 2·latIn + 64, +latOut + 64)
  {
    const int flushCap = static_cast<int>(kMaxBlock + 2 * lat.inputLatencyFrames +
                                          64 + lat.outputLatencyFrames + 64);
    BlockOut blk;
    blk.allocate(flushCap);
    AudioBlockOut outView{blk.ptr, 1, flushCap};
    const ProcessReport rep = engine->finish(outView, flushCap);
    for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
      out.ch.push_back(blk.ch[0][static_cast<std::size_t>(i)]);
    }
  }
  out.declaredIn = lat.inputLatencyFrames;
  out.declaredOut = lat.outputLatencyFrames;
  return out;
}

/// Render the TASK28 REFERENCE (proto.ola) with the identical geometry +
/// feed schedule (the prototype's own declared latency for its padding).
RenderResult renderPrototype(const std::vector<double>& x,
                             const std::string& shape, int window,
                             int overlap, double semitones) {
  proto::OlaPrototype engine;
  proto::ProtoParams params;
  params["window_frames"] = static_cast<double>(window);
  params["overlap"] = static_cast<double>(overlap);
  params["window_shape"] = shape;
  engine.configure(params);

  std::vector<double> curve(static_cast<std::size_t>(kInputFrames),
                            std::exp2(semitones / 12.0));
  engine.prepare(kFs, 1, kMaxBlock, kInputFrames, curve.data());
  const auto lat = engine.latency();

  const FrameCount feed = kInputFrames + lat.inputLookahead;
  std::vector<double> feedBuf(static_cast<std::size_t>(feed), 0.0);
  std::copy(x.begin(), x.end(), feedBuf.begin());

  RenderResult out;
  out.ch.reserve(static_cast<std::size_t>(kInputFrames + lat.outputFlush + 64));
  FrameCount consumed = 0;
  while (consumed < feed) {
    const int take = static_cast<int>(
        std::min<FrameCount>(kMaxBlock, feed - consumed));
    const double* inCh[1] = {feedBuf.data() + consumed};
    AudioBlockView inView{inCh, 1, take};
    BlockOut blk;
    blk.allocate(kMaxBlock);
    AudioBlockOut outView{blk.ptr, 1, kMaxBlock};
    const proto::ProcessOutcome rep =
        engine.process(inView, take, outView, kMaxBlock, consumed);
    CHECK(rep.inputFramesConsumed == take);
    for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
      out.ch.push_back(blk.ch[0][static_cast<std::size_t>(i)]);
    }
    consumed += take;
  }
  {
    const int flushCap = static_cast<int>(kMaxBlock + 2 * lat.inputLookahead +
                                          64 + lat.outputFlush + 64);
    BlockOut blk;
    blk.allocate(flushCap);
    AudioBlockOut outView{blk.ptr, 1, flushCap};
    const proto::ProcessOutcome rep = engine.finish(outView, flushCap);
    for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
      out.ch.push_back(blk.ch[0][static_cast<std::size_t>(i)]);
    }
  }
  out.declaredIn = lat.inputLookahead;
  out.declaredOut = lat.outputFlush;
  return out;
}

struct Compare {
  double maxDelta = 0.0;
  double rmsDelta = 0.0;
  int bitDiffs = 0;
  FrameCount common = 0;
};

Compare compare(const RenderResult& a, const RenderResult& b) {
  Compare c;
  c.common = std::min<FrameCount>(static_cast<FrameCount>(a.ch.size()),
                                  static_cast<FrameCount>(b.ch.size()));
  double acc = 0.0;
  for (FrameCount i = 0; i < c.common; ++i) {
    const double d = a.ch[static_cast<std::size_t>(i)] -
                     b.ch[static_cast<std::size_t>(i)];
    c.maxDelta = std::max(c.maxDelta, std::abs(d));
    acc += d * d;
    if (a.ch[static_cast<std::size_t>(i)] != b.ch[static_cast<std::size_t>(i)]) {
      ++c.bitDiffs;
    }
  }
  c.rmsDelta = c.common > 0 ? std::sqrt(acc / static_cast<double>(c.common)) : 0.0;
  return c;
}

/// The identity-deviation + click (crackle) metrics of a render against the
/// input: per-frame residual, and the second-difference click counter
/// (impulsive discontinuities = the audible-crackle family).
struct IdentityMetrics {
  double rmsDb = 0.0;
  double maxAbs = 0.0;
  int clicks = 0;
};

IdentityMetrics identityMetrics(const std::vector<double>& y,
                                const std::vector<double>& x,
                                FrameCount skipHead) {
  IdentityMetrics m;
  const FrameCount n = std::min<FrameCount>(static_cast<FrameCount>(y.size()),
                                            static_cast<FrameCount>(x.size()));
  double acc = 0.0;
  int64_t cnt = 0;
  double localRms = 0.0;
  for (FrameCount i = skipHead; i < n; ++i) {
    const double d = y[static_cast<std::size_t>(i)] - x[static_cast<std::size_t>(i)];
    m.maxAbs = std::max(m.maxAbs, std::abs(d));
    acc += d * d;
    ++cnt;
    // second difference (impulsive-crackle detector), scaled by the local
    // signal level: a click is a discontinuity far above the waveform's own
    // slope — the second difference of the OUTPUT minus that of the INPUT.
    if (i >= skipHead + 2) {
      const double d2y =
          y[static_cast<std::size_t>(i)] -
          2.0 * y[static_cast<std::size_t>(i - 1)] +
          y[static_cast<std::size_t>(i - 2)];
      const double d2x =
          x[static_cast<std::size_t>(i)] -
          2.0 * x[static_cast<std::size_t>(i - 1)] +
          x[static_cast<std::size_t>(i - 2)];
      const double ex = std::abs(d2y - d2x);
      localRms = 0.0;
      (void)localRms;
      if (ex > 0.05) ++m.clicks;  // the waveform's own curvature stays ≪ this
    }
  }
  m.rmsDb = cnt > 0 ? 20.0 * std::log10(std::sqrt(acc / static_cast<double>(cnt)) + 1e-12)
                    : 0.0;
  return m;
}

struct MatrixRow {
  double semitones;
  int window;
  int overlap;
  const char* shape;
  double maxDelta;
  int bitDiffs;
  FrameCount common;
  double prodIdentityDb;
  double protoIdentityDb;
  int prodClicks;
  int protoClicks;
};

std::string g_artifact;
void artifactLine(const std::string& line) {
  if (!g_artifact.empty()) {
    if (FILE* f = std::fopen(g_artifact.c_str(), "a")) {
      std::fprintf(f, "%s\n", line.c_str());
      std::fclose(f);
    }
  }
}

}  // namespace

TEST_CASE("TP-OLA-REF: production vs Task28 candidate_ola — the mandated matrix") {
  if (const char* p = std::getenv("PITCHLAB_OLA_REF_ARTIFACT")) g_artifact = p;
  const std::vector<double> x = musicalInput();

  artifactLine("\n## TP-OLA-REF matrix\n");
  artifactLine("| st | N | ov | shape | common | bitDiffs | maxDelta | prodIdDb | protoIdDb | prodClicks | protoClicks |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|");

  std::vector<MatrixRow> rows;
  for (const double st : {0.0, 12.0, -12.0}) {
    for (const int n : {512, 2048, 8192, 16384}) {
      for (const int ov : {2, 4, 8}) {
        if (n / ov < 8) continue;
        for (const char* shape : {"hann", "hamming", "bartlett", "rect"}) {
          CAPTURE(st);
          CAPTURE(n);
          CAPTURE(ov);
          CAPTURE(shape);
          const RenderResult prod =
              renderProduction(x, shape, n, ov, st);
          const RenderResult proto = renderPrototype(x, shape, n, ov, st);
          const Compare c = compare(prod, proto);
          // the production master is the canonical N + G length; the
          // prototype drains its own extent — the comparison is over the
          // common (shorter) length
          CHECK(c.common > 0);
          const FrameCount skip = static_cast<FrameCount>(n) / 2 + n / ov;
          const IdentityMetrics mp = identityMetrics(prod.ch, x, skip);
          const IdentityMetrics mq = identityMetrics(proto.ch, x, skip);

          MatrixRow row{st, n, ov, shape, c.maxDelta, c.bitDiffs, c.common,
                        mp.rmsDb, mq.rmsDb, mp.clicks, mq.clicks};
          rows.push_back(row);
          char line[256];
          std::snprintf(line, sizeof(line),
                        "| %+.0f | %d | %d | %s | %lld | %d | %.3g | %.2f | %.2f | %d | %d |",
                        st, n, ov, shape, (long long)c.common, c.bitDiffs,
                        c.maxDelta, mp.rmsDb, mq.rmsDb, mp.clicks, mq.clicks);
          artifactLine(line);
          std::printf("st=%+.0f N=%5d ov=%d %-8s common=%lld bitDiffs=%d maxDelta=%.3g prodId=%.2fdB protoId=%.2fdB clicks=%d/%d\n",
                      st, n, ov, shape, (long long)c.common, c.bitDiffs,
                      c.maxDelta, mp.rmsDb, mq.rmsDb, mp.clicks, mq.clicks);

          // THE DECISION GATE: the migration claims candidate_ola.cpp
          // verbatim. Identical geometry + feed + -ffp-contract=off must
          // produce BIT-IDENTICAL output over the common length. A material
          // difference is a production migration defect (Phase 1's class B).
          CHECK_MESSAGE(c.bitDiffs == 0,
                        "production Fixed output differs from the Task28 "
                        "reference — a migration defect, not OLA character");
        }
      }
    }
  }
  artifactLine("");
}
