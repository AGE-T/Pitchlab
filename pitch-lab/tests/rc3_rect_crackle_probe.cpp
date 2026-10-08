// native.timepitch — RC-3 / FIXED-RECT CRACKLE RECHECK PROBE (TASK G).
//
// THE OPEN ITEM (the owner's report, verbatim scope): "Fixed / RECT:
// occasional crackles". The historical record (the Task-34 era worklog):
// the heavy Fixed crackle (the Lambda geometry -> production-lag ->
// underrun class) was root-caused and FIXED (the host-path latency
// correction, the Phase-1 OLA-reference arbitration: production
// BIT-IDENTICAL to the validated reference in 138 configurations incl.
// the rect shape; at pitch 0 EXACT identity, -240 dB, zero clicks). The
// residual class measured after that fix: the FIXED/RECT SWEEP CLICKS
// (82-101 per 14 s sweep in the pre-RC1 host model) correlated with the
// re-centre/rebuild cadence — classified as the declared Task-30 re-centre
// seam character, never re-measured since. RC-1 (the frozen-Lambda
// delivery defect) has since been FIXED (the epoch-versioned exit handoff)
// and RC-2a closed as a metric artifact (CP-5). The question THIS probe
// answers at canonical d8b226a: DOES THE FIXED/RECT OCCASIONAL CRACKLE
// STILL REPRODUCE, AND IN WHICH CLASS?
//
// WHAT THIS PROBE MEASURES (the mandated recheck matrix; the production
// ADAPTER path — the chain/recentre/seam machinery lives there; zero
// production-code changes; diagnostic-only per the task methodology):
//   Fixed (tpMode=0) x shapes {hann (control), rect (the reported shape)}
//   x pitch {0, +7, -7, +12, -12 st} x automation {static, continuous
//   sweep, instant step, repeated step, repeated reset, recentre} x
//   block {64,128,256,512,1024} x fs {44.1, 48, 96 kHz}.
//   The "recentre" schedule is the pure seam-isolation drive: a +/-1.5 st
//   square oscillation around the row's pitch — EVERY toggle crosses the
//   +/-1 st job envelope boundary, so the drive produces repeated envelope
//   exits with MINIMAL content change (the splice mechanics measured
//   without the pitch-jump content difference the big steps carry).
//
// PER-DRIVE METRICS (the task's list): click count + click positions,
// max sample delta, RMS dip at the worst click, seam/chain-adoption
// positions (per-block status polling), Lambda (declared + min/max during
// the drive), dryFallbackFrames (telemetry), deliveryUnderruns, stalls,
// preparationFailures, chainAdoptionFailures, rebuild count (reprepares),
// retiring-chain state (seamRecoveries + exitEvents + exitDropped),
// wet-active span vs input span, warmup (first wet frame vs declared
// Lambda), absolute grain-lattice alignment (pitch-0 static: cross-
// correlation argmax vs the declared Lambda).
//
// THE A-E EVENT CLASSIFICATION (the task's rule): every counted click is
// classified by its position relative to the adoption-event timeline —
// a click within 4096 frames of a chain-adoption position is SEAM-
// CORRELATED (class C: the re-centre splice); a click elsewhere on a
// fault-free drive is UNCORRELATED content discontinuity (class D:
// the OLA synthesis character) unless a fault counter moved (classes
// A/B: delivery/lifecycle). The row carries BOTH the total and the
// seam-correlated count; the fault counters are printed on every row.
//
// ASSERTION POLICY (the rc2b semantic rule: "no fault" is never a
// substitute for "audio correctness"): the ONLY hard assertions are the
// frozen contract properties ALREADY pinned on the canonical tree —
// (1) the static zero-miss class at the canonical operating point
// (48k/512, both shapes, all pitches: the hostval-1 gate, shape-
// independent by architecture — the Fixed Lambda N+2K+256 carries no
// shape term), (2) render determinism for a repeated static rect drive
// (the adapter's bit-determinism guarantee). Everything else — the click
// counts, the seam correlation, the shape comparison — stays a MEASURED
// REPORT for the owner decision. NO production DSP is touched by this
// probe; a finding that PROVES a defect routes to a separate fix
// checkpoint per the one-checkpoint discipline.
//
// ARTIFACTS (written next to the committed task-35 evidence):
//   results/vst3/task35/rc3-rect-recheck/rc3_rect_recheck.md   (the rows
//     + the frozen-gate records + the click-zoom sample evidence)
//   results/vst3/task35/rc3-rect-recheck/*.wav (16-bit owner listening:
//     Fixed rect/hann static {0,+7,+12} / sweep / instant step / the
//     rect recentre row / the worst measured case's click zoom)
//
// SCOPE: the default (CI budget) pass covers 48k/512 x both shapes x the
// six schedules x pitches {0, +7} + the evidence WAVs + the frozen
// assertions (~90 s). PITCHLAB_RC3_FULL=1 runs the FULL mandated matrix
// (420 drives; produced the committed evidence record).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "vst/realtime_adapter.h"
#include "core/pitch_engine.h"
#include "engines/timepitch_engine.h"

using namespace pitchlab;
using namespace pitchlab::vst;

namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr int kShapeHann = 0;
constexpr int kShapeRect = 3;  // the choice-index mapping: 0=hann 1=hamming
                               // 2=bartlett 3=rect (the descriptor table)
const char* shapeName(int s) { return s == kShapeRect ? "rect" : "hann"; }

// --- artifact plumbing -------------------------------------------------------

const std::filesystem::path kArtifactDir =
    std::filesystem::path(PITCHLAB_SOURCE_DIR) / "results" / "vst3" /
    "task35" / "rc3-rect-recheck";

std::string g_mdPath;
bool g_mdReady = false;

void initArtifact() {
  if (g_mdReady) return;
  std::filesystem::create_directories(kArtifactDir);
  g_mdPath = (kArtifactDir / "rc3_rect_recheck.md").string();
  g_mdReady = true;
}

void artifactLine(const std::string& s) {
  if (!g_mdReady) initArtifact();
  std::ofstream f(g_mdPath, std::ios::app);
  f << s << "\n";
}

void artifactHeader(const std::string& s) {
  artifactLine("");
  artifactLine("## " + s);
}

// --- input material ----------------------------------------------------------

std::vector<double> makeSaw(int64_t frames, double fs, double amp,
                            double freq) {
  std::vector<double> x(static_cast<std::size_t>(frames), 0.0);
  for (int64_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / fs;
    double v = 0.0;
    for (int h = 1; h <= 6; ++h) {
      v += std::sin(2.0 * kPi * freq * static_cast<double>(h) * t) /
           static_cast<double>(h);
    }
    x[static_cast<std::size_t>(i)] = amp * v / 1.45;
  }
  return x;
}

// --- schedules ---------------------------------------------------------------

enum class Sched { Static, Sweep, Step, RStep, Reset, Recentre };

const char* schedName(Sched s) {
  switch (s) {
    case Sched::Static: return "static";
    case Sched::Sweep: return "sweep";
    case Sched::Step: return "step";
    case Sched::RStep: return "rstep";
    case Sched::Reset: return "reset";
    case Sched::Recentre: return "recentre";
  }
  return "?";
}

// the canonical sweep timeline (-12 hold .2 -> move 1 s -> 0 hold .2 ->
// +12 hold .2 -> move back -> ...; rate 12 st/s) — the harness schedule
struct SweepPoint { double t0, t1, st0, st1; };
std::vector<SweepPoint> makeSweep() {
  std::vector<SweepPoint> s;
  double t = 0.0;
  const double hold = 0.2, mv = 1.0;
  s.push_back({t, t += hold, -12, -12});
  s.push_back({t, t += mv, -12, 0});
  s.push_back({t, t += hold, 0, 0});
  s.push_back({t, t += mv, 0, 12});
  s.push_back({t, t += hold, 12, 12});
  s.push_back({t, t += mv, 12, 0});
  s.push_back({t, t += hold, 0, 0});
  s.push_back({t, t += mv, 0, -12});
  s.push_back({t, t += hold, -12, -12});
  return s;
}
double sweepAt(const std::vector<SweepPoint>& s, double t) {
  for (const auto& p : s) {
    if (t >= p.t0 && t <= p.t1) {
      const double span = p.t1 - p.t0;
      const double u = span > 0 ? (t - p.t0) / span : 0.0;
      return p.st0 + (p.st1 - p.st0) * u;
    }
  }
  return s.empty() ? 0.0 : s.back().st1;
}

// instant step: 0 hold 2 s -> +7 hold 2.5 s -> -7 hold rest
double stepAt(double t) {
  if (t < 2.0) return 0.0;
  if (t < 4.5) return 7.0;
  return -7.0;
}

// repeated step: +7/-7 alternating every 0.5 s (16 steps over 8 s)
double rstepAt(double t) {
  const long k = static_cast<long>(t / 0.5);
  return (k % 2) == 0 ? 7.0 : -7.0;
}

// recentre: +/-1.5 st square around the row's pitch — every toggle crosses
// the +/-1 st envelope boundary -> a re-centre on (nearly) every toggle,
// with the MINIMAL content change the class allows
double recentreAt(double centre, double t) {
  const long k = static_cast<long>(t / 0.5);
  return (k % 2) == 0 ? centre : centre + 1.5;
}

// --- drive configuration + report --------------------------------------------

struct DriveConfig {
  double fs = 48000.0;
  int32_t block = 512;
  double seconds = 6.0;
  double pitchSt = 0.0;
  int shapeIdx = kShapeRect;
  double inputFreq = 220.0;
  double amp = 0.5;
  Sched sched = Sched::Static;
};

struct ClickRec {
  int64_t pos = 0;         // output frame
  double delta = 0.0;      // |y[p] - y[p-1]|
  double rmsBefore = 0.0;  // 256 frames before
  double rmsAfter = 0.0;   // 256 frames from the click
  bool nearAdoption = false;
  int64_t lastAdoption = -1;  // nearest preceding adoption position
};

struct DriveReport {
  // final telemetry (the adapter's own counters)
  uint64_t underruns = 0;
  uint64_t dryFallbackFrames = 0;
  uint64_t dryHistoryMisses = 0;
  uint64_t stalls = 0;
  uint64_t preparationFailures = 0;
  uint64_t chainAdoptionFailures = 0;
  uint64_t reprepares = 0;
  uint64_t chainsAdopted = 0;
  uint64_t exitEvents = 0;
  uint64_t exitDropped = 0;
  uint64_t seamRecoveries = 0;
  uint64_t resets = 0;
  uint64_t faults = 0;
  int64_t latencyFrames = 0;
  int64_t lambdaMin = 0;
  int64_t lambdaMax = 0;
  double envelopeMin = 1.0;
  double envelopeMax = 1.0;
  double rtf = 0.0;
  // detector-derived
  int64_t clicks = 0;
  int64_t clicksNearAdoption = 0;  // class-C correlation
  double maxAbsDelta = 0.0;
  double worstDipDb = 0.0;
  int64_t worstDipPos = -1;
  std::vector<ClickRec> clickList;  // up to 64
  // warmup + alignment
  double firstWetS = -1.0;
  double warmupMs = -1.0;  // firstWet - declaredLambda
  double alignOffset = 0.0;  // pitch-0 static: argmax - declaredLambda
  double wetSpanS = 0.0;
  double inputSpanS = 0.0;
  // event timelines (the raw evidence)
  std::vector<int64_t> adoptionPositions;
};

ParamSnapshot snapshotFor(const DriveConfig& c, double pitchSt) {
  ParamSnapshot snap;
  snap.engineIndex = 5;  // native.timepitch
  snap.pitchSt = pitchSt;
  snap.tpMode = 0;  // Fixed (OLA)
  snap.tpWindowFrames = 2048;
  snap.tpOverlap = 2;
  snap.tpShape = c.shapeIdx;
  snap.tpFormantRatio = 1.0;
  snap.mix = 1.0;
  snap.bypass = false;
  snap.outputDb = 0.0;
  snap.lfoRateHz = 0.0;
  snap.lfoDepthSt = 0.0;
  return snap;
}

double rmsOf(const std::vector<double>& y, int64_t from, int64_t to) {
  if (from < 0) from = 0;
  if (to > (int64_t)y.size()) to = (int64_t)y.size();
  if (to <= from) return 0.0;
  double acc = 0.0;
  for (int64_t i = from; i < to; ++i) acc += y[static_cast<std::size_t>(i)] *
                                            y[static_cast<std::size_t>(i)];
  return std::sqrt(acc / double(to - from));
}

DriveReport drive(const DriveConfig& c, std::vector<double>* outStore = nullptr) {
  RealtimeAdapter adapter;
  adapter.activate(c.fs, 2, c.block);
  const auto sweep = makeSweep();
  double initialSt = c.pitchSt;
  if (c.sched == Sched::Sweep) initialSt = sweepAt(sweep, 0.0);
  if (c.sched == Sched::Step) initialSt = stepAt(0.0);
  if (c.sched == Sched::RStep) initialSt = rstepAt(0.0);
  adapter.setParameterSnapshot(snapshotFor(c, initialSt));
  adapter.requestHardReset();

  const int64_t total = static_cast<int64_t>(c.fs * c.seconds);
  const std::vector<double> sig = makeSaw(total, c.fs, c.amp, c.inputFreq);
  std::vector<double> out0(static_cast<std::size_t>(total), 0.0);
  std::vector<double> out1(static_cast<std::size_t>(total), 0.0);

  int64_t cpuNs = 0;
  int64_t pos = 0;
  DriveReport rep;
  int64_t lastRprep = -1, lastAdopt = -1, lastExit = -1;
  while (pos < total) {
    const int32_t take =
        static_cast<int32_t>(std::min<int64_t>(c.block, total - pos));
    const double* in[2] = {sig.data() + pos, sig.data() + pos};
    double* o[2] = {out0.data() + pos, out1.data() + pos};
    BlockAutomation autoBlock;
    const double t0s = static_cast<double>(pos) / c.fs;
    const double t1s = static_cast<double>(pos + take) / c.fs;
    switch (c.sched) {
      case Sched::Static:
        break;  // no per-block automation; the snapshot carries the pitch
      case Sched::Sweep:
        autoBlock.pitch[0] = {0, sweepAt(sweep, t0s)};
        autoBlock.pitch[1] = {take - 1, sweepAt(sweep, t1s)};
        autoBlock.pitchCount = 2;
        break;
      case Sched::Step:
        autoBlock.pitch[0] = {0, stepAt(t0s)};
        autoBlock.pitch[1] = {take - 1, stepAt(t1s)};
        autoBlock.pitchCount = 2;
        break;
      case Sched::RStep:
        autoBlock.pitch[0] = {0, rstepAt(t0s)};
        autoBlock.pitch[1] = {take - 1, rstepAt(t1s)};
        autoBlock.pitchCount = 2;
        break;
      case Sched::Recentre:
        autoBlock.pitch[0] = {0, recentreAt(c.pitchSt, t0s)};
        autoBlock.pitch[1] = {take - 1, recentreAt(c.pitchSt, t1s)};
        autoBlock.pitchCount = 2;
        break;
      case Sched::Reset:
        autoBlock.pitch[0] = {0, c.pitchSt};
        autoBlock.pitchCount = 1;
        break;
    }
    if (c.sched == Sched::Reset) {
      for (double rt : {2.0, 3.5, 5.0}) {
        const double rStart = rt * c.fs;
        if (pos < rStart && pos + take >= rStart) adapter.requestHardReset();
      }
    }
    const auto c0 = std::chrono::steady_clock::now();
    adapter.process(in, o, take, autoBlock);
    const auto c1 = std::chrono::steady_clock::now();
    cpuNs += std::chrono::duration_cast<std::chrono::nanoseconds>(c1 - c0)
                 .count();
    pos += take;
    // per-block telemetry poll: the event timeline (adoption / rebuild /
    // exit positions on the output frame axis)
    {
      const StatusSnapshot st = adapter.status();
      if (rep.lambdaMin == 0 || st.latencyFrames < rep.lambdaMin) {
        rep.lambdaMin = st.latencyFrames;
      }
      if (st.latencyFrames > rep.lambdaMax) rep.lambdaMax = st.latencyFrames;
      if (lastRprep >= 0 &&
          st.reprepares > static_cast<uint64_t>(lastRprep)) {
        rep.adoptionPositions.push_back(pos);  // a re-centre build landed
      }
      lastRprep = static_cast<int64_t>(st.reprepares);
      if (lastAdopt >= 0 && st.chainsAdopted > static_cast<uint64_t>(lastAdopt)) {
        rep.adoptionPositions.push_back(pos);
      }
      lastAdopt = static_cast<int64_t>(st.chainsAdopted);
      if (lastExit >= 0 && st.exitEvents > static_cast<uint64_t>(lastExit)) {
        rep.exitEvents += st.exitEvents - static_cast<uint64_t>(lastExit);
      }
      lastExit = static_cast<int64_t>(st.exitEvents);
      rep.exitDropped = st.exitDropped;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(250));
  }

  {
    const StatusSnapshot st = adapter.status();
    rep.underruns = st.deliveryUnderruns;
    rep.dryFallbackFrames = st.dryFallbackFrames;
    rep.dryHistoryMisses = st.dryHistoryMisses;
    rep.stalls = st.jobStalls;
    rep.preparationFailures = st.preparationFailures;
    rep.chainAdoptionFailures = st.chainAdoptionFailures;
    rep.reprepares = st.reprepares;
    rep.chainsAdopted = st.chainsAdopted;
    rep.seamRecoveries = st.seamRecoveries;
    rep.resets = st.resets;
    rep.faults = st.faults;
    rep.latencyFrames = st.latencyFrames;
    rep.envelopeMin = st.envelopeMin;
    rep.envelopeMax = st.envelopeMax;
  }
  const double audioNs = static_cast<double>(total) / c.fs * 1e9;
  rep.rtf = audioNs > 0 ? static_cast<double>(cpuNs) / audioNs : 0.0;
  std::sort(rep.adoptionPositions.begin(), rep.adoptionPositions.end());
  rep.adoptionPositions.erase(
      std::unique(rep.adoptionPositions.begin(), rep.adoptionPositions.end()),
      rep.adoptionPositions.end());

  const int64_t lat = rep.latencyFrames;
  const int64_t begin = lat + static_cast<int64_t>(c.fs * 0.2);
  const int64_t end = total - static_cast<int64_t>(c.fs * 0.05);
  rep.inputSpanS = (end - begin) / c.fs;

  // click detection (the canonical harness threshold 0.12) + positions
  // + the local RMS dip + the adoption correlation
  for (int64_t p = std::max<int64_t>(begin, 1); p < end; ++p) {
    const double a = out0[static_cast<std::size_t>(p)];
    const double prev = out0[static_cast<std::size_t>(p - 1)];
    const double d = std::fabs(a - prev);
    rep.maxAbsDelta = std::max(rep.maxAbsDelta, d);
    if (d > 0.12) {
      ++rep.clicks;
      ClickRec cr;
      cr.pos = p;
      cr.delta = d;
      cr.rmsBefore = rmsOf(out0, p - 256, p);
      cr.rmsAfter = rmsOf(out0, p, p + 256);
      // the nearest preceding adoption position
      auto it = std::upper_bound(rep.adoptionPositions.begin(),
                                 rep.adoptionPositions.end(), p);
      if (it != rep.adoptionPositions.begin()) {
        cr.lastAdoption = *(it - 1);
        cr.nearAdoption = (p - cr.lastAdoption) <= 4096;
      }
      if (cr.nearAdoption) ++rep.clicksNearAdoption;
      const double dipDb = 20.0 * std::log10(
          std::max(cr.rmsAfter, 1e-9) / std::max(cr.rmsBefore, 1e-9));
      if (rep.worstDipPos < 0 || dipDb < rep.worstDipDb) {
        rep.worstDipDb = dipDb;
        rep.worstDipPos = p;
      }
      if (rep.clickList.size() < 64) rep.clickList.push_back(cr);
    }
  }

  // wet-active span + warmup (first output frame above 1e-3 absolute)
  for (int64_t p = 0; p < total; ++p) {
    if (std::fabs(out0[static_cast<std::size_t>(p)]) > 1e-3) {
      rep.firstWetS = static_cast<double>(p) / c.fs;
      break;
    }
  }
  if (rep.firstWetS >= 0.0) {
    rep.warmupMs = (rep.firstWetS -
                    static_cast<double>(lat) / c.fs) * 1000.0;
  }
  const int64_t sash = static_cast<int64_t>(0.02 * c.fs);
  for (int64_t s0 = begin; s0 + sash <= end; s0 += sash) {
    if (rmsOf(out0, s0, s0 + sash) >= 0.002) {
      rep.wetSpanS = static_cast<double>(s0 + sash) / c.fs -
                     static_cast<double>(begin) / c.fs;
    }
  }

  // absolute grain-lattice alignment (pitch-0 static only): cross-correlate
  // the wet against the input at candidate delays around the declared
  // Lambda; argmax - Lambda = the alignment offset (0 = lattice aligned)
  if (c.sched == Sched::Static && c.pitchSt == 0.0) {
    const int64_t N = 16384;
    const int64_t n0 = begin + 4096;
    if (n0 + N < end) {
      double best = -1e30;
      int64_t bestLag = 0;
      for (int64_t lag = lat - 4096; lag <= lat + 4096; ++lag) {
        if (lag < 0 || n0 + N + (lag - lat) >= total) continue;
        double acc = 0.0;
        for (int64_t i = 0; i < N; i += 4) {  // subsampled (4x) — the peak
          // is a broad identity-correlation peak; 4x keeps the resolution
          // at 4 frames, ample against the +/-4096 search span
          const int64_t q = n0 + i - lag;
          if (q < 0) continue;
          acc += out0[static_cast<std::size_t>(n0 + i)] *
                 sig[static_cast<std::size_t>(q)];
        }
        if (acc > best) {
          best = acc;
          bestLag = lag;
        }
      }
      rep.alignOffset = static_cast<double>(bestLag - lat);
    }
  }

  if (outStore) *outStore = out0;
  adapter.deactivate();
  return rep;
}

// --- WAV ---------------------------------------------------------------------

void writeWav16(const std::filesystem::path& p, const std::vector<double>& y,
                double fs) {
  std::ofstream f(p, std::ios::binary);
  const int64_t n = static_cast<int64_t>(y.size());
  const int64_t dataBytes = n * 2;
  auto wr4 = [&](int64_t v) {
    for (int i = 0; i < 4; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xFF));
  };
  auto wr2 = [&](int64_t v) {
    for (int i = 0; i < 2; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xFF));
  };
  f.write("RIFF", 4);
  wr4(36 + dataBytes);
  f.write("WAVEfmt ", 8);
  wr4(16);
  wr2(1);
  wr2(1);
  wr4(static_cast<int64_t>(fs));
  wr4(static_cast<int64_t>(fs) * 2);
  wr2(2);
  wr2(16);
  f.write("data", 4);
  wr4(dataBytes);
  for (int64_t i = 0; i < n; ++i) {
    double v = std::clamp(y[static_cast<std::size_t>(i)], -1.0, 1.0);
    wr2(static_cast<int64_t>(std::lround(v * 32767.0)));
  }
}

// --- row emission ------------------------------------------------------------

void emitRow(const DriveConfig& c, const DriveReport& r) {
  char row[512];
  std::snprintf(row, sizeof(row),
                "| %s | %s | %.1f | %.1fk | %d | %.1f | %.1f | %.1f | %lld | "
                "%lld | %.4f | %.1f | %llu | %llu | %llu | %llu | %llu | "
                "%llu | %llu | %llu | %llu | %llu | %llu | %lld | %llu | "
                "%.1f | %llu |",
                shapeName(c.shapeIdx), schedName(c.sched), c.pitchSt,
                c.fs / 1000.0, (int)c.block,
                static_cast<double>(r.latencyFrames) / c.fs * 1000.0,
                r.warmupMs, r.alignOffset, (long long)r.clicks,
                (long long)r.clicksNearAdoption, r.maxAbsDelta,
                r.worstDipDb, (unsigned long long)r.underruns,
                (unsigned long long)r.dryFallbackFrames,
                (unsigned long long)r.dryHistoryMisses,
                (unsigned long long)r.stalls,
                (unsigned long long)r.preparationFailures,
                (unsigned long long)r.chainAdoptionFailures,
                (unsigned long long)r.reprepares,
                (unsigned long long)r.chainsAdopted,
                (unsigned long long)r.exitEvents,
                (unsigned long long)r.exitDropped,
                (unsigned long long)r.seamRecoveries,
                (long long)r.adoptionPositions.size(),
                (unsigned long long)r.faults, r.rtf,
                (unsigned long long)r.resets);
  artifactLine(row);
  std::printf("  RC3 %s %s %.0fst %.0fk b%d: clicks=%lld(near=%lld) maxD=%.4f dip=%.1fdB undr=%llu fbTel=%llu stall=%llu prepF=%llu adoptF=%llu rprep=%llu adopt=%llu seamRec=%llu exits=%llu warmup=%.1fms align=%.0f\n",
              shapeName(c.shapeIdx), schedName(c.sched), c.pitchSt,
              c.fs / 1000.0, (int)c.block, (long long)r.clicks,
              (long long)r.clicksNearAdoption, r.maxAbsDelta, r.worstDipDb,
              (unsigned long long)r.underruns,
              (unsigned long long)r.dryFallbackFrames,
              (unsigned long long)r.stalls,
              (unsigned long long)r.preparationFailures,
              (unsigned long long)r.chainAdoptionFailures,
              (unsigned long long)r.reprepares,
              (unsigned long long)r.chainsAdopted,
              (unsigned long long)r.seamRecoveries,
              (unsigned long long)r.exitEvents, r.warmupMs, r.alignOffset);
  if (r.underruns != 0 || r.dryFallbackFrames != 0 || r.stalls != 0 ||
      r.preparationFailures != 0 || r.chainAdoptionFailures != 0) {
    char w[256];
    std::snprintf(w, sizeof(w),
                  "WARNING faults moved: %s %s %.0fst %.0fk b%d undr=%llu fbTel=%llu stall=%llu prepF=%llu adoptF=%llu",
                  shapeName(c.shapeIdx), schedName(c.sched), c.pitchSt,
                  c.fs / 1000.0, (int)c.block,
                  (unsigned long long)r.underruns,
                  (unsigned long long)r.dryFallbackFrames,
                  (unsigned long long)r.stalls,
                  (unsigned long long)r.preparationFailures,
                  (unsigned long long)r.chainAdoptionFailures);
    artifactLine(w);
    std::printf("  %s\n", w);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// RC3-RECT-MATRIX: the mandated recheck matrix (reported, diagnostic-first)
// ---------------------------------------------------------------------------

TEST_CASE("RC3-RECT-MATRIX: fixed x shape x pitch x automation x block x fs") {
  initArtifact();
  artifactLine("# RC-3 / FIXED-RECT CRACKLE RECHECK (TASK G) — canonical d8b226a");
  artifactLine("# Fixed (tpMode=0, window 2048, overlap 2) through the PRODUCTION adapter;");
  artifactLine("# shapes hann (control) / rect (the reported shape); saw 220 Hz, amp 0.5;");
  artifactLine("# click threshold 0.12 (the canonical harness value); near-adoption window 4096 frames;");
  artifactLine("# recentre = +/-1.5 st square (every toggle crosses the +/-1 st envelope).");
  artifactHeader("MATRIX — the full metric set per drive");
  artifactLine("| shape | sched | st | fs | blk | latMs | warmupMs | alignOff | clicks | clicksNearAd | maxD | worstDipDb | undr | fbTel | dryHist | stall | prepF | adoptF | rprep | adopted | exits | exitDrop | seamRec | adoptions | faults | rtf | resets |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");

  const bool full = std::getenv("PITCHLAB_RC3_FULL") != nullptr;
  // the sandbox-slice knobs: the FULL matrix is wall-time heavy, so the
  // committed evidence record is produced in FOREGROUND slices keyed by
  // (shape, fs, schedule group) — the artifact accumulates across runs.
  const char* sliceShape = std::getenv("PITCHLAB_RC3_SHAPE");    // rect|hann
  const char* sliceFs = std::getenv("PITCHLAB_RC3_FSONLY");      // e.g. 96000
  const char* sliceGroup = std::getenv("PITCHLAB_RC3_GROUP");    // static|auto
  const char* sliceBlk = std::getenv("PITCHLAB_RC3_BLKONLY");    // e.g. 64
  const double fsSet[] = {44100.0, 48000.0, 96000.0};
  const int blkSet[] = {64, 128, 256, 512, 1024};

  auto sliceMatches = [&](int shape, double fs, Sched sched, int blk) {
    if (sliceShape != nullptr &&
        !(std::strcmp(sliceShape, "rect") == 0 ? shape == kShapeRect
                                               : shape == kShapeHann)) {
      return false;
    }
    if (sliceFs != nullptr &&
        std::llround(fs) != std::atoll(sliceFs)) {
      return false;
    }
    if (sliceBlk != nullptr && blk != std::atoi(sliceBlk)) {
      return false;
    }
    if (sliceGroup != nullptr) {
      const bool isStatic = (sched == Sched::Static || sched == Sched::Recentre);
      if (std::strcmp(sliceGroup, "static") == 0 ? !isStatic : isStatic) {
        return false;
      }
    }
    return true;
  };

  int rows = 0;
  for (int shape : {kShapeHann, kShapeRect}) {
    // STATIC + RECENTRE carry the pitch dimension ...
    for (Sched sched : {Sched::Static, Sched::Recentre}) {
      for (double st : {0.0, 7.0, -7.0, 12.0, -12.0}) {
        for (double fs : fsSet) {
          for (int blk : blkSet) {
            if (!full && !(fs == 48000.0 && blk == 512)) continue;
            if (!full && sched == Sched::Recentre && st != 0.0 &&
                st != 7.0) continue;
            if (!sliceMatches(shape, fs, sched, blk)) continue;
            DriveConfig c;
            c.fs = fs;
            c.block = static_cast<int32_t>(blk);
            c.pitchSt = st;
            c.shapeIdx = shape;
            c.sched = sched;
            c.seconds = full ? 6.0 : 4.0;
            const DriveReport r = drive(c);
            emitRow(c, r);
            ++rows;
            std::fflush(stdout);
          }
        }
      }
    }
    // ... SWEEP / STEP / RSTEP / RESET carry their own trajectory
    for (Sched sched : {Sched::Sweep, Sched::Step, Sched::RStep, Sched::Reset}) {
      for (double fs : fsSet) {
        for (int blk : blkSet) {
          if (!full && !(fs == 48000.0 && blk == 512)) continue;
          if (!sliceMatches(shape, fs, sched, blk)) continue;
          DriveConfig c;
          c.fs = fs;
          c.block = static_cast<int32_t>(blk);
          c.shapeIdx = shape;
          c.sched = sched;
          c.pitchSt = sched == Sched::Reset ? 7.0 : 0.0;
          c.seconds = 8.0;
          const DriveReport r = drive(c);
          emitRow(c, r);
          ++rows;
          std::fflush(stdout);
        }
      }
    }
  }

  // --- the frozen-invariant assertions (the ONLY hard gates) ---------------
  // (run in the DEFAULT quick pass; skipped on slice runs — the gates are
  // env-sliced only for wall-time budgeting, the verdicts live in the
  // default record and in every CI run)
  // (1) the static zero-miss class at the canonical operating point —
  //     48k/512, BOTH shapes, all pitches (shape-independent by
  //     architecture: the Fixed chain Lambda N+2K+256 carries no shape
  //     term). A violation here IS a finding (a real delivery defect).
  if (sliceShape == nullptr && sliceFs == nullptr && sliceGroup == nullptr) {
  artifactHeader("FROZEN GATE — static zero-miss at the canonical point (both shapes)");
  for (int shape : {kShapeHann, kShapeRect}) {
    for (double st : {0.0, 7.0, -7.0, 12.0, -12.0}) {
      DriveConfig c;
      c.fs = 48000.0;
      c.block = 512;
      c.pitchSt = st;
      c.shapeIdx = shape;
      c.sched = Sched::Static;
      c.seconds = 4.0;
      const DriveReport r = drive(c);
      const bool zeroMiss = r.underruns == 0u && r.dryFallbackFrames == 0u &&
                            r.stalls == 0u && r.preparationFailures == 0u &&
                            r.chainAdoptionFailures == 0u;
      CHECK_MESSAGE(zeroMiss,
                    "the static drive must stay zero-miss (the hostval-1 "
                    "class; shape-independent by architecture)");
      char row[256];
      std::snprintf(row, sizeof(row),
                    "| %s | %.0f st | undr=%llu fbTel=%llu stall=%llu prepF=%llu adoptF=%llu | %s |",
                    shapeName(shape), st,
                    (unsigned long long)r.underruns,
                    (unsigned long long)r.dryFallbackFrames,
                    (unsigned long long)r.stalls,
                    (unsigned long long)r.preparationFailures,
                    (unsigned long long)r.chainAdoptionFailures,
                    zeroMiss ? "OK" : "VIOLATION");
      artifactLine(row);
    }
  }

  // (2) render determinism for a repeated static rect drive (the adapter's
  //     bit-determinism guarantee — the T-E6/T-E7 class)
  {
    DriveConfig c;
    c.fs = 48000.0;
    c.block = 512;
    c.pitchSt = 7.0;
    c.shapeIdx = kShapeRect;
    c.sched = Sched::Static;
    c.seconds = 3.0;
    std::vector<double> a, b;
    drive(c, &a);
    drive(c, &b);
    int64_t mismatches = 0;
    for (std::size_t p = 0; p < a.size(); ++p) {
      if (a[p] != b[p]) ++mismatches;
    }
    CHECK_EQ(mismatches, 0);
    artifactHeader("FROZEN GATE — render determinism (repeated static rect drive)");
    artifactLine(mismatches == 0
                     ? "| determinism | PASS (bit-identical) |"
                     : "| determinism | VIOLATION |");
  }
  }  // end of the default-pass gate section (skipped on slice runs)

  std::printf("  RC3-RECT-MATRIX: %d rows (full=%d)\n", rows, full ? 1 : 0);
}

// ---------------------------------------------------------------------------
// RC3-RECT-EVIDENCE: the owner listening artifacts + the sample-level seam
// evidence (the task's rule: at least one reproducible waveform/sample-level
// evidence for the relevant seam cases)
// ---------------------------------------------------------------------------

TEST_CASE("RC3-RECT-EVIDENCE: listening wavs + the click zoom evidence") {
  initArtifact();
  artifactHeader("EVIDENCE — the listening WAVs + the click-zoom sample evidence");

  // the flagship rows (48k/512, both shapes): static / sweep / instant step
  // + the rect CRACKLE rows (static at the shifted pitches — the reproduced
  // owner report) + the hann controls of the same rows
  struct Flag { const char* name; DriveConfig c; };
  std::vector<Flag> flags;
  for (int shape : {kShapeRect, kShapeHann}) {
    DriveConfig cs;
    cs.shapeIdx = shape;
    cs.sched = Sched::Static;
    cs.pitchSt = 0.0;
    cs.seconds = 6.0;
    flags.push_back({"static0", cs});
    DriveConfig cs7;
    cs7.shapeIdx = shape;
    cs7.sched = Sched::Static;
    cs7.pitchSt = 7.0;
    cs7.seconds = 6.0;
    flags.push_back({"static7", cs7});
    DriveConfig cs12;
    cs12.shapeIdx = shape;
    cs12.sched = Sched::Static;
    cs12.pitchSt = 12.0;
    cs12.seconds = 6.0;
    flags.push_back({"static12", cs12});
    DriveConfig cw;
    cw.shapeIdx = shape;
    cw.sched = Sched::Sweep;
    cw.seconds = 8.0;
    flags.push_back({"sweep", cw});
    DriveConfig ct;
    ct.shapeIdx = shape;
    ct.sched = Sched::Step;
    ct.seconds = 8.0;
    flags.push_back({"step", ct});
  }
  {
    DriveConfig cr;
    cr.shapeIdx = kShapeRect;
    cr.sched = Sched::Recentre;
    cr.pitchSt = 7.0;
    cr.seconds = 6.0;
    flags.push_back({"recentre7", cr});
  }

  std::vector<double> worstOut;
  DriveConfig worstCfg;
  int64_t worstClicks = -1;

  for (const Flag& f : flags) {
    std::vector<double> out;
    const DriveReport r = drive(f.c, &out);
    emitRow(f.c, r);
    char path[256];
    std::snprintf(path, sizeof(path), "fixed_%s_%s.wav",
                  shapeName(f.c.shapeIdx), f.name);
    writeWav16(kArtifactDir / path, out, f.c.fs);
    if (f.c.shapeIdx == kShapeRect && r.clicks > worstClicks) {
      worstClicks = r.clicks;
      worstOut = out;
      worstCfg = f.c;
    }
  }

  // the click zoom: +/-4096 frames around the worst rect click (the
  // sample-level seam evidence) + the immediate context
  if (worstClicks > 0 && !worstOut.empty()) {
    // re-derive the click list for the zoom (the drive stored only the
    // worst dip position through the report; re-drive is deterministic)
    const DriveReport rw = drive(worstCfg, &worstOut);
    if (!rw.clickList.empty()) {
      // the strongest delta click
      const ClickRec* best = &rw.clickList[0];
      for (const auto& cl : rw.clickList) {
        if (cl.delta > best->delta) best = &cl;
      }
      const int64_t z0 = std::max<int64_t>(0, best->pos - 4096);
      const int64_t z1 = std::min<int64_t>((int64_t)worstOut.size(),
                                           best->pos + 4096);
      std::vector<double> zoom(worstOut.begin() + static_cast<long>(z0),
                               worstOut.begin() + static_cast<long>(z1));
      writeWav16(kArtifactDir / "fixed_rect_click_zoom.wav", zoom,
                 worstCfg.fs);
      artifactHeader("CLICK ZOOM (the strongest rect click)");
      char buf[384];
      std::snprintf(buf, sizeof(buf),
                    "| pos=%.6f s | delta=%.4f | rmsBefore=%.4f rmsAfter=%.4f dip=%.1f dB | lastAdoption=%.6f s (near=%d) | zoom=+/-4096 frames -> fixed_rect_click_zoom.wav |",
                    static_cast<double>(best->pos) / worstCfg.fs, best->delta,
                    best->rmsBefore, best->rmsAfter,
                    20.0 * std::log10(std::max(best->rmsAfter, 1e-9) /
                                      std::max(best->rmsBefore, 1e-9)),
                    best->lastAdoption < 0
                        ? -1.0
                        : static_cast<double>(best->lastAdoption) /
                              worstCfg.fs,
                    best->nearAdoption ? 1 : 0);
      artifactLine(buf);
      std::printf("  CLICK ZOOM: pos=%.6fs delta=%.4f nearAdoption=%d lastAdoption=%.6fs\n",
                  static_cast<double>(best->pos) / worstCfg.fs, best->delta,
                  best->nearAdoption ? 1 : 0,
                  best->lastAdoption < 0
                      ? -1.0
                      : static_cast<double>(best->lastAdoption) / worstCfg.fs);
    }
  }
  std::printf("  RC3-RECT-EVIDENCE: WAVs written to %s\n", kArtifactDir.c_str());
}

// ---------------------------------------------------------------------------
// RC3-RECT-DIRECT: the class-D arbitration — the DIRECT engine (offline
// contract, NO adapter) at the static pitches. The OLA reference test
// proved production == validated prototype bit-identical (its clicks
// column counts BOTH sides equally, e.g. 717/717 at rect +12); this case
// completes the arbitration at the FULL mandated pitch set {0,+7,-7,+12,
// -12} with the harness click threshold, separating:
//   direct clicky == adapter clicky  => class D (the frozen OLA+shape
//                                       synthesis character),
//   direct clean  != adapter clicky  => an ADAPTER-PATH defect (classes
//                                       A/B — but the adapter telemetry
//                                       is zero-fault on those rows, so
//                                       this branch would demand a new
//                                       root-cause checkpoint).
// REPORTED rows (the shape comparison hann vs rect is the finding's core
// evidence); the pitch-0 identity row is the frozen sanity anchor.
// ---------------------------------------------------------------------------

TEST_CASE("RC3-RECT-DIRECT: the direct-engine arbitration at all pitches") {
  initArtifact();
  artifactHeader("DIRECT — the offline engine render (NO adapter), static, saw 220, 48k/512");
  artifactLine("| shape | st | clicks | maxD | identityResidDb |");

  auto renderFixed = [&](int shapeIdx, double betaSt, std::vector<double>* out)
      -> std::string {
    using pitchlab::AudioBlockOut;
    using pitchlab::AudioBlockView;
    using pitchlab::EngineConfiguration;
    using pitchlab::FrameCount;
    using pitchlab::ParameterValue;
    using pitchlab::PitchCurveView;
    using pitchlab::ProcessContext;
    using pitchlab::ProcessReport;
    auto engine = pitchlab::makeTimePitchEngine();
    EngineConfiguration cfg;
    cfg.seed = 12345;
    cfg.parameters.emplace_back("mode", ParameterValue{std::string("fixed")});
    cfg.parameters.emplace_back("window_shape",
                                ParameterValue{std::string(
                                    shapeIdx == kShapeRect ? "rect" : "hann")});
    engine->configure(cfg);
    const double fs = 48000.0;
    const int64_t total = static_cast<int64_t>(fs * 6.0);
    std::vector<double> x = makeSaw(total, fs, 0.5, 220.0);
    const double beta = std::exp2(betaSt / 12.0);
    std::vector<double> curve(static_cast<std::size_t>(total), beta);
    PitchCurveView cv{};
    cv.ratio = curve.data();
    cv.frames = static_cast<FrameCount>(curve.size());
    cv.sampleRate = fs;
    ProcessContext ctx{};
    ctx.sampleRate = fs;
    ctx.channels = 1;
    ctx.maxBlockFrames = 512;
    ctx.totalInputFrames = static_cast<FrameCount>(total);
    ctx.curve = &cv;
    engine->prepare(ctx);
    const auto lat = engine->latency();
    const FrameCount feed =
        static_cast<FrameCount>(total) + lat.inputLatencyFrames;
    std::vector<double> feedBuf(static_cast<std::size_t>(feed), 0.0);
    std::copy(x.begin(), x.end(), feedBuf.begin());
    std::vector<double> rendered;
    const int outCap = 4 * 2048;
    try {
      FrameCount consumed = 0;
      while (consumed < feed) {
        const int take = static_cast<int>(
            std::min<FrameCount>(static_cast<FrameCount>(512), feed - consumed));
        const double* inCh[1] = {feedBuf.data() + consumed};
        AudioBlockView inView{inCh, 1, take};
        std::vector<double> blkCh(static_cast<std::size_t>(outCap), 0.0);
        double* blkPtr[1] = {blkCh.data()};
        AudioBlockOut outView{blkPtr, 1, outCap};
        const ProcessReport rep =
            engine->process(inView, take, outView, outCap, cv, consumed);
        for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
          rendered.push_back(blkCh[static_cast<std::size_t>(i)]);
        }
        consumed += take;
      }
      const int flushCap = static_cast<int>(512 + 2 * lat.inputLatencyFrames +
                                            64 + lat.outputLatencyFrames + 64);
      std::vector<double> blkCh(static_cast<std::size_t>(flushCap), 0.0);
      double* blkPtr[1] = {blkCh.data()};
      AudioBlockOut outView{blkPtr, 1, flushCap};
      const ProcessReport rep = engine->finish(outView, flushCap);
      for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
        rendered.push_back(blkCh[static_cast<std::size_t>(i)]);
      }
    } catch (const std::exception& e) {
      return e.what();
    }
    *out = std::move(rendered);
    return {};
  };

  for (int shape : {kShapeRect, kShapeHann}) {
    for (double st : {0.0, 7.0, -7.0, 12.0, -12.0}) {
      std::vector<double> out;
      const std::string err = renderFixed(shape, st, &out);
      REQUIRE(err.empty());
      REQUIRE(out.size() > 8192);
      // the settled region: after the declared input latency, before the tail
      const int64_t begin = 4096;
      const int64_t end = static_cast<int64_t>(out.size()) -
                          static_cast<int64_t>(8192);
      int64_t clicks = 0;
      double maxD = 0.0;
      for (int64_t p = std::max<int64_t>(begin, 1); p < end; ++p) {
        const double d = std::fabs(out[static_cast<std::size_t>(p)] -
                                   out[static_cast<std::size_t>(p - 1)]);
        maxD = std::max(maxD, d);
        if (d > 0.12) ++clicks;
      }
      char row[192];
      std::snprintf(row, sizeof(row), "| %s | %.0f | %lld | %.4f | - |",
                    shapeName(shape), st, (long long)clicks, maxD);
      artifactLine(row);
      std::printf("  RC3-DIRECT %s %.0fst: clicks=%lld maxD=%.4f\n",
                  shapeName(shape), st, (long long)clicks, maxD);
      if (st == 0.0 && shape == kShapeRect) {
        // the frozen sanity anchor: rect identity is EXACT (the reference
        // test's -240 dB; the clicks must be ZERO here)
        CHECK_EQ(clicks, 0);
      }
    }
  }
}
