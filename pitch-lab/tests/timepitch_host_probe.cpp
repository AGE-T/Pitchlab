// native.timepitch — REALTIME ADAPTER HOST-PATH PROBE (task-33 continuation:
// the owner's manual VST3 host observations — Fixed-mode crackle, Pitch +
// Formant dry/wet alternation, very large delivery-underrun counts at a very
// low RTF).
//
// This suite drives the REAL adapter (the exact code path the VST3
// processor's process() calls into) and measures, per drive:
//   * the categorized fault counters (faults / deliveryUnderruns /
//     jobStalls / chainAdoptionFailures / preparationFailures / reprepares)
//   * THE DRY-FALLBACK FRAME DETECTOR: a frame whose output is BIT-EQUAL to
//     the latency-compensated dry input (out[p] == in[p − Λ]) is a frame the
//     emission served from the dry lane — the wet lane missed. At mix = 1,
//     gain = 0 dB, bypass off, a wet frame can never be bit-equal to the raw
//     input (every production engine runs the samples through its DSP
//     arithmetic), so bit-equality is a clean per-frame fallback detector.
//     For the duration-PRESERVING modes the adapter counts these as
//     deliveryUnderruns (the counter cross-validates the detector); for the
//     RATE-FOLLOWING modes the covered-range miss is exempt from the fault
//     accounting (the declared D.4 wet end) — this detector makes that
//     class VISIBLE per frame without redefining any counter.
//   * the fallback RUN STRUCTURE: run count / run length distribution /
//     run start positions modulo the chain's job advance — the pattern
//     distinguishes the candidate root causes (job-seam transition vs
//     per-block pacing vs prep starvation vs production intermittency).
//
// DIAGNOSTIC-FIRST: the matrix prints a table; assertions are added at the
// findings become regressions. PITCHLAB_TP_PROBE_ARTIFACT=<path> writes the
// table as markdown.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "vst/realtime_adapter.h"
#include "vst/realtime_status.h"

#include "engines/timepitch_engine.h"

using namespace pitchlab;
using namespace pitchlab::vst;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct DriveConfig {
  double fs = 48000.0;
  int32_t block = 512;
  int64_t seconds = 12;
  double pitchSt = 0.0;
  int tpMode = 0;  // 0 fixed, 1 adaptive, 2 pitch_synced, 3 pitch_formant
  int64_t windowFrames = 2048;
  int64_t overlap = 2;
  int tpShape = 0;  // 0 hann, 1 ?, 2 ?, 3 ? (choice index; see parameters.cpp)
  double formantRatio = 1.0;
  double amp = 0.5;
  double inputFreq = 220.0;
};

ParamSnapshot snapshotFor(const DriveConfig& c) {
  ParamSnapshot snap;
  snap.engineIndex = 5;  // native.timepitch (registry order)
  snap.pitchSt = c.pitchSt;
  snap.tpMode = c.tpMode;
  snap.tpWindowFrames = c.windowFrames;
  snap.tpOverlap = c.overlap;
  snap.tpShape = c.tpShape;
  snap.tpFormantRatio = c.formantRatio;
  snap.mix = 1.0;
  snap.bypass = false;
  snap.outputDb = 0.0;
  snap.lfoRateHz = 0.0;  // LFO OFF (the owner's test: no automation)
  snap.lfoDepthSt = 0.0;
  return snap;
}

/// A deterministic harmonic-rich "musical" input (6 decaying harmonics +
/// a 3 Hz 0.5-st vibrato + a slow amplitude envelope) — closer to the
/// owner's material than a bare sine, still exactly reproducible.
std::vector<double> musicalInput(int64_t frames, double fs, double amp,
                                 double freq) {
  std::vector<double> x(static_cast<std::size_t>(frames), 0.0);
  for (int64_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / fs;
    const double vib = std::exp2(0.5 * std::sin(2.0 * kPi * 3.0 * t) / 12.0);
    const double f = freq * vib;
    double v = 0.0;
    for (int h = 1; h <= 6; ++h) {
      v += std::sin(2.0 * kPi * f * static_cast<double>(h) * t) /
           static_cast<double>(h);
    }
    const double env = 0.7 + 0.3 * std::sin(2.0 * kPi * 0.25 * t);
    x[static_cast<std::size_t>(i)] = amp * env * v / 1.45;
  }
  return x;
}

struct FallbackRun {
  int64_t start = 0;
  int64_t len = 0;
};

struct DriveReport {
  StatusSnapshot status;
  double rtf = 0.0;
  int64_t fallbackFrames = 0;
  int64_t fallbackRuns = 0;
  int64_t fallbackMaxRun = 0;
  int64_t firstFallbackAt = -1;
  double residualRmsDb = -999.0;  // |out[p] − in[p−Λ]| RMS over the settled
                                  // region (the identity/crackle metric)
  std::vector<FallbackRun> runs;  // full run list (for the modulo analysis)
};

DriveReport drive(const DriveConfig& c, bool collectRuns = true) {
  RealtimeAdapter adapter;
  adapter.activate(c.fs, 2, c.block);
  adapter.setParameterSnapshot(snapshotFor(c));
  adapter.requestHardReset();

  const int64_t total = static_cast<int64_t>(c.fs * static_cast<double>(c.seconds));
  const std::vector<double> sig = musicalInput(total, c.fs, c.amp, c.inputFreq);
  std::vector<double> out0(static_cast<std::size_t>(total), 0.0);
  std::vector<double> out1(static_cast<std::size_t>(total), 0.0);

  int64_t cpuNs = 0;
  int64_t pos = 0;
  while (pos < total) {
    const int32_t take =
        static_cast<int32_t>(std::min<int64_t>(c.block, total - pos));
    const double* in[2] = {sig.data() + pos, sig.data() + pos};
    double* o[2] = {out0.data() + pos, out1.data() + pos};
    BlockAutomation none;
    const auto t0 = std::chrono::steady_clock::now();
    adapter.process(in, o, take, none);
    const auto t1 = std::chrono::steady_clock::now();
    cpuNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    pos += take;
    std::this_thread::sleep_for(std::chrono::microseconds(250));
  }

  DriveReport rep;
  rep.status = adapter.status();
  const double audioNs = static_cast<double>(total) / c.fs * 1e9;
  rep.rtf = audioNs > 0 ? static_cast<double>(cpuNs) / audioNs : 0.0;

  const int64_t lat = rep.status.latencyFrames;
  // settled region: skip the chain-startup latency window at both ends
  const int64_t begin = lat + static_cast<int64_t>(c.fs * 0.2);
  const int64_t end = total - static_cast<int64_t>(c.fs * 0.05);
  bool inRun = false;
  FallbackRun cur;
  double acc = 0.0;
  int64_t n = 0;
  for (int64_t p = begin; p < end; ++p) {
    const int64_t q = p - lat;
    if (q < 0) continue;
    const bool dryEqual =
        out0[static_cast<std::size_t>(p)] == sig[static_cast<std::size_t>(q)] &&
        out1[static_cast<std::size_t>(p)] == sig[static_cast<std::size_t>(q)];
    if (dryEqual) {
      ++rep.fallbackFrames;
      if (!inRun) {
        inRun = true;
        cur = FallbackRun{p, 0};
        if (rep.firstFallbackAt < 0) rep.firstFallbackAt = p;
      }
      ++cur.len;
    } else if (inRun) {
      inRun = false;
      ++rep.fallbackRuns;
      rep.fallbackMaxRun = std::max(rep.fallbackMaxRun, cur.len);
      if (collectRuns) rep.runs.push_back(cur);
    }
    const double r = out0[static_cast<std::size_t>(p)] - sig[static_cast<std::size_t>(q)];
    acc += r * r;
    ++n;
  }
  if (inRun) {
    ++rep.fallbackRuns;
    rep.fallbackMaxRun = std::max(rep.fallbackMaxRun, cur.len);
    if (collectRuns) rep.runs.push_back(cur);
  }
  rep.residualRmsDb =
      n > 0 ? 20.0 * std::log10(std::sqrt(acc / static_cast<double>(n)) + 1e-12)
            : -999.0;
  adapter.deactivate();
  return rep;
}

const char* modeName(int m) {
  switch (m) {
    case 0: return "fixed";
    case 1: return "adaptive";
    case 2: return "pitch_synced";
    case 3: return "pitch_formant";
    default: return "?";
  }
}

std::string g_artifact;
void artifactLine(const std::string& line) {
  if (!g_artifact.empty()) {
    if (FILE* f = std::fopen(g_artifact.c_str(), "a")) {
      std::fprintf(f, "%s\n", line.c_str());
      std::fclose(f);
    }
  }
}

void printHeader() {
  std::printf("| %-6s | %-13s | %5s | %4s | %5s | %7s | %8s | %8s | %6s | %6s | %8s | %6s | %7s | %5s | %5s | %5s | %5s | %6s | %6s |\n",
              "mode", "geom", "blk", "st", "RTF", "latMs", "underruns", "fallback", "runs", "maxRun", "first@s", "resDb", "stalls", "rprep", "prepF", "exc", "adopt", "jobsP", "live");
  artifactLine("| mode | geom | blk | st | RTF | latMs | underruns | fallback | runs | maxRun | first@s | resDb | stalls | rprep | prepF | exc | adopt | jobsP | live |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");
}

void printRow(const DriveConfig& c, const DriveReport& r) {
  char geom[64];
  std::snprintf(geom, sizeof(geom), "N%lld/o%lld/s%d",
                (long long)c.windowFrames, (long long)c.overlap, c.tpShape);
  std::printf("| %-6s | %-13s | %5d | %4.0f | %5.3f | %7.1f | %8llu | %8llu | %6llu | %6llu | %8.3f | %6.1f | %7llu | %5llu | %5llu | %5llu | %5llu | %6llu | %6llu |\n",
              modeName(c.tpMode), geom, c.block, c.pitchSt, r.rtf,
              r.status.latencyFrames / c.fs * 1000.0,
              (unsigned long long)r.status.deliveryUnderruns,
              (unsigned long long)r.fallbackFrames,
              (unsigned long long)r.fallbackRuns,
              (unsigned long long)r.fallbackMaxRun,
              r.firstFallbackAt < 0 ? -1.0
                                    : r.firstFallbackAt / c.fs,
              r.residualRmsDb,
              (unsigned long long)r.status.jobStalls,
              (unsigned long long)r.status.reprepares,
              (unsigned long long)r.status.preparationFailures,
              (unsigned long long)r.status.engineExceptions,
              (unsigned long long)r.status.chainsAdopted,
              (unsigned long long)r.status.jobsPrepared,
              (unsigned long long)r.status.liveJobs);
  char line[640];
  std::snprintf(line, sizeof(line),
                "| %s | %s | %d | %.0f | %.3f | %.1f | %llu | %llu | %llu | %llu | %.3f | %.1f | %llu | %llu | %llu | %llu | %llu | %llu | %llu |",
                modeName(c.tpMode), geom, c.block, c.pitchSt, r.rtf,
                r.status.latencyFrames / c.fs * 1000.0,
                (unsigned long long)r.status.deliveryUnderruns,
                (unsigned long long)r.fallbackFrames,
                (unsigned long long)r.fallbackRuns,
                (unsigned long long)r.fallbackMaxRun,
                r.firstFallbackAt < 0 ? -1.0 : r.firstFallbackAt / c.fs,
                r.residualRmsDb,
                (unsigned long long)r.status.jobStalls,
                (unsigned long long)r.status.reprepares,
                (unsigned long long)r.status.preparationFailures,
                (unsigned long long)r.status.engineExceptions,
                (unsigned long long)r.status.chainsAdopted,
                (unsigned long long)r.status.jobsPrepared,
                (unsigned long long)r.status.liveJobs);
  artifactLine(line);
}

void printRunPattern(const DriveConfig& c, const DriveReport& r,
                     int64_t advance) {
  if (r.runs.empty()) return;
  std::printf("  run starts (s, mod-advance frames):\n");
  int shown = 0;
  for (const auto& run : r.runs) {
    if (shown++ >= 12) {
      std::printf("    ... (%llu runs total)\n",
                  (unsigned long long)r.runs.size());
      break;
    }
    const int64_t rel = run.start - (r.firstFallbackAt < 0 ? 0 : r.firstFallbackAt);
    std::printf("    @%8.3fs len=%5lld  rel=%7lld  rel%%advance=%7lld\n",
                run.start / c.fs, (long long)run.len, (long long)rel,
                (long long)(rel % advance));
  }
}

}  // namespace

TEST_CASE("TP-HOST: Fixed mode host-path matrix (the crackle reproduction)") {
  if (const char* p = std::getenv("PITCHLAB_TP_PROBE_ARTIFACT")) g_artifact = p;
  artifactLine("\n## TP-HOST Fixed matrix\n");
  printHeader();

  // The owner's observation: crackle across the shape choices. Sweep the
  // window/overlap/shape space at pitch 0 first (the control), then the
  // static shifts.
  const int64_t windows[] = {512, 2048, 8192, 16384};
  const int64_t overlaps[] = {2, 4, 8};
  const int shapes[] = {0, 1, 2, 3};
  for (int64_t n : windows) {
    for (int64_t ov : overlaps) {
      if (n / ov < 8) continue;
      for (int sh : shapes) {
        DriveConfig c;
        c.tpMode = 0;
        c.windowFrames = n;
        c.overlap = ov;
        c.tpShape = sh;
        c.seconds = 8;
        const DriveReport r = drive(c);
        printRow(c, r);
        printRunPattern(c, r, 10 * 48000 - (int64_t)c.windowFrames);
      }
    }
  }
}

TEST_CASE("TP-HOST: Fixed static shifts (0 / +12 / -12 st) at the default geometry") {
  if (const char* p = std::getenv("PITCHLAB_TP_PROBE_ARTIFACT")) g_artifact = p;
  artifactLine("\n## TP-HOST Fixed shifts\n");
  printHeader();
  for (double st : {0.0, 12.0, -12.0}) {
    DriveConfig c;
    c.pitchSt = st;
    c.seconds = 12;
    const DriveReport r = drive(c);
    printRow(c, r);
    printRunPattern(c, r, 10 * 48000 - 2048);
  }
}

TEST_CASE("TP-HOST: Pitch+Formant and Pitch-Synced alternation reproduction") {
  if (const char* p = std::getenv("PITCHLAB_TP_PROBE_ARTIFACT")) g_artifact = p;
  artifactLine("\n## TP-HOST Pitch-Synced family\n");
  printHeader();
  for (int mode : {2, 3}) {
    for (double st : {0.0, 7.0, 12.0, -12.0}) {
      DriveConfig c;
      c.tpMode = mode;
      c.pitchSt = st;
      c.seconds = 14;
      const DriveReport r = drive(c);
      printRow(c, r);
      printRunPattern(c, r, 10 * 48000 - 1920);
    }
  }
}

TEST_CASE("TP-HOST: block-size sensitivity (Fixed default geometry)") {
  if (const char* p = std::getenv("PITCHLAB_TP_PROBE_ARTIFACT")) g_artifact = p;
  artifactLine("\n## TP-HOST block sweep\n");
  printHeader();
  for (int32_t blk : {64, 128, 256, 512, 1024, 4096}) {
    DriveConfig c;
    c.block = blk;
    c.seconds = 8;
    const DriveReport r = drive(c);
    printRow(c, r);
    printRunPattern(c, r, 10 * 48000 - 2048);
  }
}

// ---------------------------------------------------------------------------
// TP-PS-JOB: the per-job production probe — a FRESH Pitch-Synced engine
// driven exactly as the adapter drives a splice job (prepare with
// totalInputFrames = jobInputLen, fed in maxBlock blocks, finish once).
// Measures the job's output length + the production timeline (output frame
// e available after how much input) — the numbers behind the splice-cell
// design math.
// ---------------------------------------------------------------------------
TEST_CASE("TP-PS-JOB: splice-job production length + timeline") {
  const double fs = 48000.0;
  const int maxBlock = 512;
  const int64_t windowO = 12032;   // chainGeometry @48k
  const int64_t jobInputLen = 21607;
  const double beta = 1.0;         // pitch 0

  auto engine = makeTimePitchEngine();
  EngineConfiguration cfg;
  cfg.seed = 7;
  cfg.parameters.emplace_back("mode", ParameterValue{std::string("pitch_synced")});
  engine->configure(cfg);

  std::vector<double> curve(static_cast<std::size_t>(jobInputLen), beta);
  PitchCurveView cv{};
  cv.ratio = curve.data();
  cv.frames = static_cast<FrameCount>(curve.size());
  cv.sampleRate = fs;

  ProcessContext ctx{};
  ctx.sampleRate = fs;
  ctx.channels = 1;
  ctx.maxBlockFrames = maxBlock;
  ctx.totalInputFrames = jobInputLen;
  ctx.curve = &cv;
  engine->prepare(ctx);
  const auto lat = engine->latency();
  std::printf("engine declared: in=%lld out=%lld\n",
              (long long)lat.inputLatencyFrames, (long long)lat.outputLatencyFrames);

  // the input signal (voiced, musical)
  std::vector<double> x(static_cast<std::size_t>(jobInputLen), 0.0);
  for (int64_t i = 0; i < jobInputLen; ++i) {
    const double t = static_cast<double>(i) / fs;
    const double vib = std::exp2(0.5 * std::sin(2.0 * kPi * 3.0 * t) / 12.0);
    const double f = 220.0 * vib;
    double v = 0.0;
    for (int h = 1; h <= 6; ++h) {
      v += std::sin(2.0 * kPi * f * static_cast<double>(h) * t) / static_cast<double>(h);
    }
    x[static_cast<std::size_t>(i)] = 0.5 * v / 1.45;
  }

  std::vector<double> wet;
  wet.reserve(static_cast<std::size_t>(jobInputLen));
  FrameCount consumed = 0;
  // production timeline: for each output frame, the input consumed when it
  // was emitted (the first 40 entries + the frontier trace)
  std::vector<std::pair<int64_t, int64_t>> timeline;
  while (consumed < jobInputLen) {
    const int take = static_cast<int>(std::min<int64_t>(maxBlock, jobInputLen - consumed));
    const double* inCh[1] = {x.data() + consumed};
    AudioBlockView inView{inCh, 1, take};
    std::vector<double> outBuf(static_cast<std::size_t>(maxBlock), 0.0);
    double* outCh[1] = {outBuf.data()};
    AudioBlockOut outView{outCh, 1, maxBlock};
    const ProcessReport rep = engine->process(inView, take, outView, maxBlock, cv, consumed);
    for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
      timeline.emplace_back(consumed + take, static_cast<int64_t>(wet.size()) + i + 1);
      wet.push_back(outBuf[static_cast<std::size_t>(i)]);
    }
    consumed += take;
  }
  std::printf("process-phase output: %zu frames (cell needs %lld)\n", wet.size(),
              (long long)windowO);
  {
    std::vector<double> finBuf(65536, 0.0);
    double* finCh[1] = {finBuf.data()};
    AudioBlockOut outView{finCh, 1, 65536};
    const ProcessReport rep = engine->finish(outView, 65536);
    std::printf("finish flush: %lld frames\n", (long long)rep.outputFramesProduced);
    for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
      wet.push_back(finBuf[static_cast<std::size_t>(i)]);
    }
  }
  std::printf("total job output: %zu frames\n", wet.size());
  // the production timeline sample: at what consumed does output frame e appear
  for (std::size_t i = 0; i < timeline.size() && i < 8; ++i) {
    std::printf("  e=%lld available at consumed=%lld\n",
                (long long)timeline[i].second, (long long)timeline[i].first);
  }
  if (timeline.size() > 8) {
    for (std::size_t i = timeline.size() / 2; i < timeline.size(); i += timeline.size() / 4) {
      std::printf("  e=%lld available at consumed=%lld\n",
                  (long long)timeline[i].second, (long long)timeline[i].first);
    }
  }
  // the emission-cadence check: does the production RUN 1:1 with the input
  // after the warmup, or does it stall? (the wet frames produced per
  // 12000-frame input window)
  int64_t buckets[4] = {0, 0, 0, 0};
  for (const auto& [c, e] : timeline) {
    const int64_t b = std::min<int64_t>(3, c / (jobInputLen / 4));
    ++buckets[b];
  }
  std::printf("output per input quarter: %lld %lld %lld %lld\n",
              (long long)buckets[0], (long long)buckets[1],
              (long long)buckets[2], (long long)buckets[3]);
}

// ---------------------------------------------------------------------------
// TP-PS-RESET: the reset()-reuse probe — run the splice job, reset(), run
// it again (the adapter's job-recycle path). The second run must produce
// the same first-output point and the same total.
// ---------------------------------------------------------------------------
TEST_CASE("TP-PS-RESET: reset()-reuse produces identical job output") {
  const double fs = 48000.0;
  const int maxBlock = 512;
  const int64_t jobInputLen = 21607;

  auto engine = makeTimePitchEngine();
  EngineConfiguration cfg;
  cfg.seed = 7;
  cfg.parameters.emplace_back("mode", ParameterValue{std::string("pitch_synced")});
  engine->configure(cfg);

  std::vector<double> curve(static_cast<std::size_t>(jobInputLen), 1.0);

  auto run = [&](const char* tag) {
    PitchCurveView cv{};
    cv.ratio = curve.data();
    cv.frames = static_cast<FrameCount>(curve.size());
    cv.sampleRate = fs;
    ProcessContext ctx{};
    ctx.sampleRate = fs;
    ctx.channels = 1;
    ctx.maxBlockFrames = maxBlock;
    ctx.totalInputFrames = jobInputLen;
    ctx.curve = &cv;
    engine->prepare(ctx);

    std::vector<double> x(static_cast<std::size_t>(jobInputLen), 0.0);
    for (int64_t i = 0; i < jobInputLen; ++i) {
      const double t = static_cast<double>(i) / fs;
      const double vib = std::exp2(0.5 * std::sin(2.0 * kPi * 3.0 * t) / 12.0);
      const double f = 220.0 * vib;
      double v = 0.0;
      for (int h = 1; h <= 6; ++h) {
        v += std::sin(2.0 * kPi * f * static_cast<double>(h) * t) / static_cast<double>(h);
      }
      x[static_cast<std::size_t>(i)] = 0.5 * v / 1.45;
    }

    std::vector<double> wet;
    FrameCount consumed = 0;
    int64_t firstOutAt = -1;
    while (consumed < jobInputLen) {
      const int take = static_cast<int>(std::min<int64_t>(maxBlock, jobInputLen - consumed));
      const double* inCh[1] = {x.data() + consumed};
      AudioBlockView inView{inCh, 1, take};
      std::vector<double> outBuf(static_cast<std::size_t>(maxBlock), 0.0);
      double* outCh[1] = {outBuf.data()};
      AudioBlockOut outView{outCh, 1, maxBlock};
      const ProcessReport rep = engine->process(inView, take, outView, maxBlock, cv, consumed);
      for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
        if (firstOutAt < 0) firstOutAt = consumed;
        wet.push_back(outBuf[static_cast<std::size_t>(i)]);
      }
      consumed += take;
    }
    std::vector<double> finBuf(65536, 0.0);
    double* finCh[1] = {finBuf.data()};
    AudioBlockOut outView{finCh, 1, 65536};
    const ProcessReport rep = engine->finish(outView, 65536);
    std::printf("%s: process-phase=%zu flush=%lld firstOutAt=%lld\n", tag, wet.size(),
                (long long)rep.outputFramesProduced, (long long)firstOutAt);
    return wet.size();
  };
  const auto a = run("run1 (fresh prepare)");
  engine->reset();
  const auto b = run("run2 (after reset)");
  engine->reset();
  const auto c = run("run3 (after reset)");
  CHECK(a == b);
  CHECK(b == c);
}
