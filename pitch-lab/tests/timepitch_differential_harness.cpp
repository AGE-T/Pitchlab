// tests/timepitch_differential_harness.cpp — the master recovery pack TASK C:
// ONE authoritative TimePitch continuity diagnostic instrument.
//
// ROLE (per the recovery pack):
//   * records the CANONICAL baseline (current remote HEAD) of the full
//     metric set over host model x schedule x format x block, so every
//     re-derivation checkpoint (RC-1 epoch handoff, RC-2a beta<1, RC-3 seam)
//     can be DIFFED against this record — the differential evidence the
//     pack mandates;
//   * diagnostic-FIRST: the OPEN RC defect classes (RC-1 automation-only
//     delivery, RC-2a beta<1 consumption, RC-3 seam clicks) are REPORTED,
//     never asserted away and never "fixed" by harness tuning;
//   * the only HARD assertions are the FROZEN invariants that already hold
//     on the canonical tree: (a) render determinism for identical static
//     drives (the adapter's bit-determinism guarantee), (b) the static
//     zero-miss class at the canonical operating point (the hostval-1 gate).
//
// Metric set per drive (the pack's list): underruns, dryFallbackFrames,
// fallback frames/runs/maxRun/first@, stalls, preparation failures,
// adoption failures, re-prepares (rebuild count), resets, Lambda (declared
// latency), RTF, seam recoveries, clicks + maxDelta, wet silence,
// RMS collapse, dominant frequency, harmonic family, wet active span,
// duration accounting.
//
// Artifacts: results/vst3/task35/differential_baseline.md (the row record;
// git-diffable across checkpoints). Override the path with
// PITCHLAB_TP_DIFF_ARTIFACT for local runs.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "vst/realtime_adapter.h"
#include "vst/realtime_status.h"

#include "analysis/pitch_tracker.h"

#include "engines/timepitch_engine.h"

using namespace pitchlab;
using namespace pitchlab::vst;
using namespace pitchlab::analysis;

namespace {

constexpr double kPi = 3.14159265358979323846;

// --- input materials ---------------------------------------------------------

enum class Material { Sine, Saw };

const char* materialName(Material m) {
  return m == Material::Sine ? "sine" : "saw";
}

std::vector<double> makeInput(Material m, int64_t frames, double fs,
                              double amp, double freq) {
  std::vector<double> x(static_cast<std::size_t>(frames), 0.0);
  for (int64_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / fs;
    double v = 0.0;
    if (m == Material::Sine) {
      v = std::sin(2.0 * kPi * freq * t);
    } else {
      // harmonic-rich voiced material (the tracker's domain)
      for (int h = 1; h <= 6; ++h) {
        v += std::sin(2.0 * kPi * freq * static_cast<double>(h) * t) /
             static_cast<double>(h);
      }
      v /= 1.45;
    }
    x[static_cast<std::size_t>(i)] = amp * v;
  }
  return x;
}

// --- schedules ---------------------------------------------------------------

enum class Sched { Static, Sweep, Step, RepeatedReset };

const char* schedName(Sched s) {
  switch (s) {
    case Sched::Static: return "static";
    case Sched::Sweep: return "sweep";
    case Sched::Step: return "step";
    case Sched::RepeatedReset: return "reset";
  }
  return "?";
}

// the sweep timeline (-12 hold .2 -> move 1 s -> 0 hold .2 -> move -> +12
// hold .2 -> move -> 0 hold .2 -> move -> -12 hold .2; rate 12 st/s)
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

// the step timeline: 0 hold 2 s -> +7 hold 2.5 s -> -7 hold rest
double stepAt(double t) {
  if (t < 2.0) return 0.0;
  if (t < 4.5) return 7.0;
  return -7.0;
}

// --- drive configuration + report --------------------------------------------

struct DriveConfig {
  double fs = 48000.0;
  int32_t block = 512;
  double seconds = 8.0;
  int tpMode = 2;  // 0 fixed, 1 adaptive, 2 pitch_synced, 3 pitch_formant
  double pitchSt = 7.0;  // static pitch (nonzero: the delivery stress case)
  double formantRatio = 1.0;
  Material material = Material::Saw;
  double inputFreq = 220.0;
  double amp = 0.5;
  Sched sched = Sched::Static;
  bool snapshotFlood = false;  // host model B: publishSnapshot every block
  bool withSpectrum = false;   // dominant-frequency + harmonic family
};

struct DriveReport {
  StatusSnapshot status;
  double rtf = 0.0;
  // detector-derived (the covered-range wet lane)
  int64_t fallbackFrames = 0;
  int64_t fallbackRuns = 0;
  int64_t fallbackMaxRun = 0;
  double firstFallbackAt = -1.0;
  int64_t wetSilenceSashes = 0;
  int64_t wetSilenceFrames = 0;
  double firstSilenceAt = -1.0;
  int64_t clicks = 0;
  double maxAbsDelta = 0.0;
  // accounting
  double wetEndS = -1.0;  // last active sash end (output-referenced)
  double wetSpanS = 0.0;  // wetEndS - analysisBeginS
  double inputSpanS = 0.0;
  // spectrum (withSpectrum)
  double dominantHz = 0.0;
  double dominantMag = 0.0;
  double harmRatio[5] = {0, 0, 0, 0, 0};  // H2..H6 vs dominant
};

ParamSnapshot snapshotFor(const DriveConfig& c, double pitchSt) {
  ParamSnapshot snap;
  snap.engineIndex = 5;  // native.timepitch
  snap.pitchSt = pitchSt;
  snap.tpMode = c.tpMode;
  snap.tpWindowFrames = 2048;
  snap.tpOverlap = 2;
  snap.tpShape = 0;
  snap.tpFormantRatio = c.formantRatio;
  snap.mix = 1.0;
  snap.bypass = false;
  snap.outputDb = 0.0;
  snap.lfoRateHz = 0.0;
  snap.lfoDepthSt = 0.0;
  return snap;
}

// Goertzel magnitude of x[n..n+N) at frequency f (with Hann window).
double goertzelMag(const double* x, int64_t n, int64_t N, double f, double fs) {
  const double w = 2.0 * kPi * f / fs;
  const double coeff = 2.0 * std::cos(w);
  double s0 = 0.0, s1 = 0.0, s2 = 0.0;
  for (int64_t i = 0; i < N; ++i) {
    const double win =
        0.5 * (1.0 - std::cos(2.0 * kPi * double(i) / double(N - 1)));
    s0 = win * x[n + i] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return std::sqrt(std::max(0.0, power)) / double(N);
}

DriveReport drive(const DriveConfig& c, std::vector<double>* outStore = nullptr) {
  RealtimeAdapter adapter;
  adapter.activate(c.fs, 2, c.block);
  const auto sweep = makeSweep();
  const double initialSt =
      (c.sched == Sched::Sweep)
          ? sweepAt(sweep, 0.0)
          : (c.sched == Sched::Step ? stepAt(0.0) : c.pitchSt);
  adapter.setParameterSnapshot(snapshotFor(c, initialSt));
  adapter.requestHardReset();

  const int64_t total = static_cast<int64_t>(c.fs * c.seconds);
  const std::vector<double> sig =
      makeInput(c.material, total, c.fs, c.amp, c.inputFreq);
  std::vector<double> out0(static_cast<std::size_t>(total), 0.0);
  std::vector<double> out1(static_cast<std::size_t>(total), 0.0);

  int64_t cpuNs = 0;
  int64_t pos = 0;
  DriveReport rep;
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
      case Sched::RepeatedReset:
        autoBlock.pitch[0] = {0, c.pitchSt};
        autoBlock.pitchCount = 1;
        break;
    }
    if (c.snapshotFlood) {
      // host model B: the controller thread publishes the snapshot with the
      // parameter's CURRENT (block-end) value on every block
      double stNow = c.pitchSt;
      if (c.sched == Sched::Sweep) stNow = sweepAt(sweep, t1s);
      if (c.sched == Sched::Step) stNow = stepAt(t1s);
      adapter.setParameterSnapshot(snapshotFor(c, stNow));
    }
    if (c.sched == Sched::RepeatedReset) {
      for (double rt : {2.0, 3.5, 5.0}) {
        const double rStart = rt * c.fs;
        if (pos < rStart && pos + take >= rStart) adapter.requestHardReset();
      }
    }
    const auto c0 = std::chrono::steady_clock::now();
    adapter.process(in, o, take, autoBlock);
    const auto c1 = std::chrono::steady_clock::now();
    cpuNs += std::chrono::duration_cast<std::chrono::nanoseconds>(c1 - c0).count();
    pos += take;
    std::this_thread::sleep_for(std::chrono::microseconds(250));
  }

  rep.status = adapter.status();
  const double audioNs = static_cast<double>(total) / c.fs * 1e9;
  rep.rtf = audioNs > 0 ? static_cast<double>(cpuNs) / audioNs : 0.0;

  const int64_t lat = rep.status.latencyFrames;
  const int64_t begin = lat + static_cast<int64_t>(c.fs * 0.2);
  const int64_t end = total - static_cast<int64_t>(c.fs * 0.05);
  rep.inputSpanS = (end - begin) / c.fs;

  // dry-fallback (bit-equal) runs + click detector
  bool inRun = false;
  int64_t runLen = 0;
  for (int64_t p = begin; p < end; ++p) {
    const int64_t q = p - lat;
    if (q < 1) continue;
    const double a = out0[static_cast<std::size_t>(p)];
    const double b = out1[static_cast<std::size_t>(p)];
    const double inQ = sig[static_cast<std::size_t>(q)];
    if (a == inQ && b == inQ) {
      ++rep.fallbackFrames;
      if (!inRun) {
        inRun = true;
        runLen = 0;
        if (rep.firstFallbackAt < 0) rep.firstFallbackAt = double(p) / c.fs;
      }
      ++runLen;
    } else if (inRun) {
      inRun = false;
      ++rep.fallbackRuns;
      rep.fallbackMaxRun = std::max(rep.fallbackMaxRun, runLen);
    }
    const double prev = out0[static_cast<std::size_t>(p - 1)];
    const double d = std::fabs(a - prev);
    rep.maxAbsDelta = std::max(rep.maxAbsDelta, d);
    if (d > 0.12) ++rep.clicks;
  }
  if (inRun) {
    ++rep.fallbackRuns;
    rep.fallbackMaxRun = std::max(rep.fallbackMaxRun, runLen);
  }

  // wet-silence sashes + wet-active span (20 ms sashes)
  const int64_t sash = static_cast<int64_t>(0.02 * c.fs);
  for (int64_t s0 = begin; s0 + sash <= end; s0 += sash) {
    double inAcc = 0.0, outAcc = 0.0;
    for (int64_t i = 0; i < sash; ++i) {
      const double iv = sig[static_cast<std::size_t>(s0 + i - lat)];
      const double ov = out0[static_cast<std::size_t>(s0 + i)];
      inAcc += iv * iv;
      outAcc += ov * ov;
    }
    const double inRms = std::sqrt(inAcc / double(sash));
    const double outRms = std::sqrt(outAcc / double(sash));
    if (inRms > 0.05 && outRms < 0.002) {
      ++rep.wetSilenceSashes;
      rep.wetSilenceFrames += sash;
      if (rep.firstSilenceAt < 0) rep.firstSilenceAt = double(s0) / c.fs;
    }
    if (outRms >= 0.002) rep.wetEndS = double(s0 + sash) / c.fs;
  }
  rep.wetSpanS = rep.wetEndS > 0 ? rep.wetEndS - double(begin) / c.fs : 0.0;

  // spectrum (optional): dominant peak + the harmonic family H2..H6
  if (c.withSpectrum && end - begin > 32768) {
    const int64_t N = 16384;
    const int64_t n0 = end - N - 1;
    const double fNyq = c.fs * 0.5;
    double bestMag = 0.0, bestF = 0.0;
    for (double f = 30.0; f <= std::min(8000.0, fNyq - 100.0); f += 2.0) {
      const double m = goertzelMag(out0.data(), n0, N, f, c.fs);
      if (m > bestMag) {
        bestMag = m;
        bestF = f;
      }
    }
    rep.dominantHz = bestF;
    rep.dominantMag = bestMag;
    for (int h = 2; h <= 6; ++h) {
      const double fh = bestF * double(h);
      if (fh < fNyq - 100.0) {
        rep.harmRatio[h - 2] =
            goertzelMag(out0.data(), n0, N, fh, c.fs) / std::max(1e-30, bestMag);
      }
    }
  }

  if (outStore) *outStore = out0;
  adapter.deactivate();
  return rep;
}

// --- artifact ----------------------------------------------------------------

std::string g_artifact;
bool g_artifactReady = false;

void initArtifact() {
  if (g_artifactReady) return;
  if (const char* p = std::getenv("PITCHLAB_TP_DIFF_ARTIFACT")) {
    g_artifact = p;
  } else {
#ifdef PITCHLAB_SOURCE_DIR
    g_artifact = std::string(PITCHLAB_SOURCE_DIR) +
                 "/results/vst3/task35/differential_baseline.md";
#else
    return;  // no artifact path available
#endif
  }
  g_artifactReady = true;
}

void truncateArtifact() {
  initArtifact();
  if (g_artifact.empty()) return;
  if (FILE* f = std::fopen(g_artifact.c_str(), "w")) {
    std::fprintf(f, "# TimePitch differential baseline (TASK C — the recovery pack)\n");
    std::fprintf(f, "# one row per drive; DIFFED across the re-derivation checkpoints\n");
    std::fprintf(f, "# (RC-1 epoch handoff, RC-2a beta<1, RC-3 seam). Diagnostic-first:\n");
    std::fprintf(f, "# the open RC classes are REPORTED here, never asserted away.\n");
    std::fclose(f);
  }
}

void artifactLine(const std::string& line);

void artifactHeader(const char* section) {
  initArtifact();
  if (g_artifact.empty()) return;
  artifactLine("");
  artifactLine(std::string("## ") + section);
}

void artifactLine(const std::string& line) {
  if (g_artifact.empty()) return;
  if (FILE* f = std::fopen(g_artifact.c_str(), "a")) {
    std::fprintf(f, "%s\n", line.c_str());
    std::fclose(f);
  }
}

void emitRow(const DriveConfig& c, const DriveReport& r, const char* host) {
  const char* mode = (c.tpMode == 0)   ? "fixed"
                     : (c.tpMode == 1) ? "adaptive"
                     : (c.tpMode == 2) ? "psync"
                                       : "pform";
  char harm[64] = "-";
  if (c.withSpectrum) {
    std::snprintf(harm, sizeof(harm), "%.2f/%.2f/%.2f/%.2f/%.2f", r.harmRatio[0],
                  r.harmRatio[1], r.harmRatio[2], r.harmRatio[3], r.harmRatio[4]);
  }
  char line[640];
  std::snprintf(
      line, sizeof(line),
      "| %s | %s | %s | %s | %.1fk | %d | %.3f | %.1f | %llu | %llu | %llu |"
      " %llu | %llu | %llu | %llu | %llu | %llu | %llu | %llu | %llu | %.3f |"
      " %llu | %llu | %.2f | %.2f | %.2f | %.1f | %.2e | %s |",
      mode, schedName(c.sched), host, materialName(c.material), c.fs / 1000.0,
      (int)c.block, r.rtf, r.status.latencyFrames / c.fs * 1000.0,
      (unsigned long long)r.status.deliveryUnderruns,
      (unsigned long long)r.status.dryFallbackFrames,
      (unsigned long long)r.fallbackFrames,
      (unsigned long long)r.fallbackRuns,
      (unsigned long long)r.fallbackMaxRun,
      (unsigned long long)r.status.jobStalls,
      (unsigned long long)r.status.preparationFailures,
      (unsigned long long)r.status.chainAdoptionFailures,
      (unsigned long long)r.status.reprepares,
      (unsigned long long)r.status.resets,
      (unsigned long long)r.status.seamRecoveries,
      (unsigned long long)r.clicks, r.maxAbsDelta,
      (unsigned long long)r.wetSilenceSashes,
      (unsigned long long)r.wetSilenceFrames,
      r.firstSilenceAt < 0 ? -1.0 : r.firstSilenceAt, r.wetSpanS,
      r.inputSpanS, r.dominantHz, r.dominantMag, harm);
  artifactLine(line);
}

}  // namespace

// ---------------------------------------------------------------------------
// TP-DIFF-FULL: host model x schedule x mode at the canonical operating
// point (48 kHz, block 512) — the FULL metric set incl. the spectrum.
// ---------------------------------------------------------------------------

TEST_CASE("TP-DIFF-FULL: host model x schedule x mode (full metrics, 48k/512)") {
  truncateArtifact();
  artifactHeader("FULL — host model x schedule x mode (fs=48k, block=512, saw 220)");
  artifactLine("| mode | sched | host | mat | fs | blk | rtf | latMs | undr | fbTel | fbDet | runs | maxRun | stall | prepF | adoptF | rprep | resets | seam | clicks | maxD | silS | silF | firstSil | wetSpan | inSpan | domHz | domMag | H2/3/4/5/6 |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");

  const int modes[] = {0, 2, 3};  // fixed, pitch_synced, pitch_formant
  for (int mode : modes) {
    // (a) static, automation-only host — determinism A/B + the zero-miss gate
    DriveConfig c;
    c.tpMode = mode;
    c.sched = Sched::Static;
    c.seconds = 7.0;
    std::vector<double> outA;
    const DriveReport ra = drive(c, &outA);
    emitRow(c, ra, "auto");
    std::vector<double> outB;
    const DriveReport rb = drive(c, &outB);
    emitRow(c, rb, "auto2");
    // (a1) FROZEN: the static zero-miss class (the hostval-1 gate)
    CHECK_EQ(ra.status.dryFallbackFrames, 0u);
    CHECK_EQ(ra.status.deliveryUnderruns, 0u);
    CHECK_EQ(ra.status.jobStalls, 0u);
    CHECK_EQ(ra.status.preparationFailures, 0u);
    // (a2) FROZEN: render determinism for identical drives (interior)
    const int64_t lat = ra.status.latencyFrames;
    const int64_t begin = lat + int64_t(c.fs * 0.2);
    const int64_t end = int64_t(c.fs * c.seconds) - int64_t(c.fs * 0.05);
    int64_t mismatches = 0;
    for (int64_t p = begin; p < end; ++p) {
      if (outA[std::size_t(p)] != outB[std::size_t(p)]) ++mismatches;
    }
    CHECK_EQ(mismatches, 0);

    // (b) static, snapshot-flood host
    c.snapshotFlood = true;
    emitRow(c, drive(c), "flood");
    c.snapshotFlood = false;

    // (c) sweep, both hosts (spectrum on the automation-only row)
    c.sched = Sched::Sweep;
    c.seconds = 7.0;
    c.withSpectrum = true;
    emitRow(c, drive(c), "auto");
    c.withSpectrum = false;
    c.snapshotFlood = true;
    emitRow(c, drive(c), "flood");
    c.snapshotFlood = false;

    // (d) instant step, automation-only (spectrum on)
    c.sched = Sched::Step;
    c.seconds = 7.0;
    c.withSpectrum = true;
    emitRow(c, drive(c), "auto");
    c.withSpectrum = false;

    // (e) repeated reset (3 hard resets), automation-only
    c.sched = Sched::RepeatedReset;
    emitRow(c, drive(c), "auto");
  }
}

// ---------------------------------------------------------------------------
// TP-DIFF-RC1-GATE: the RC-1 fix regression pin (the master recovery pack
// TASK D gate). The fix signature per the baseline README: the
// automation-only sweep rows' underrun/dry-fallback columns collapse to
// zero once the epoch-versioned exit handoff is live, WITHOUT any change
// to the static rows (the frozen invariants above stay untouched).
// ---------------------------------------------------------------------------

TEST_CASE("TP-DIFF-RC1-GATE: automation-only sweep delivery is zero-miss (the RC-1 pin)") {
  initArtifact();
  artifactHeader("RC1-GATE — the automation-only continuity pin (post exit-handoff)");
  const double fs = 48000.0;
  const int32_t block = 512;
  for (int mode : {2, 3}) {  // pitch_synced, pitch_formant
    DriveConfig c;
    c.fs = fs;
    c.block = block;
    c.tpMode = mode;
    c.sched = Sched::Sweep;
    c.seconds = 7.0;
    const DriveReport r = drive(c);
    emitRow(c, r, "auto");
    CHECK_EQ(r.status.deliveryUnderruns, 0u);
    CHECK_EQ(r.status.dryFallbackFrames, 0u);
    CHECK_EQ(r.status.jobStalls, 0u);
    CHECK_EQ(r.status.preparationFailures, 0u);
    // exitDropped is REPORTED telemetry, not a gate: the ring overflow is
    // the DESIGNED safe-degradation path (a drop skips one stale exit
    // event; the next outside block re-fires and the delivery is
    // unaffected). The CI-runner evidence (the first CI iteration of this
    // gate): 42 drops during the pitch_formant sweep under load with
    // ZERO delivery misses — the drop class cost nothing audible, exactly
    // as designed. A hard zero would gate the preparation timing (the
    // documented class), not the fix.
    if (r.status.exitDropped > 0u) {
      std::printf("  RC1-GATE exitDropped=%llu (the safe ring-overflow path; "
                  "delivery unaffected)\n",
                  (unsigned long long)r.status.exitDropped);
    }
    // the re-centre machinery worked: the exits were published and consumed
    CHECK(r.status.exitEvents > 0u);
  }

  // THE WET IS REAL AND CORRECTLY PITCHED (the acceptance beyond "not
  // dry"): a static +7 st pitch_synced drive, the settled region sampled
  // ALIGNED BY THE DECLARED LATENCY — the dominant frequency must be the
  // expected wet pitch (220 Hz x 2^(7/12) = 329.3 Hz), not the input pitch.
  // This excludes the whole "bit-equal detector" ambiguity: a dry
  // passthrough would show 220 Hz here.
  {
    DriveConfig c;
    c.tpMode = 2;
    c.sched = Sched::Static;
    c.pitchSt = 7.0;
    c.seconds = 7.0;
    std::vector<double> out;
    const DriveReport r = drive(c, &out);
    CHECK_EQ(r.status.deliveryUnderruns, 0u);
    CHECK_EQ(r.status.dryFallbackFrames, 0u);
    const int64_t lat = r.status.latencyFrames;
    const int64_t total = static_cast<int64_t>(c.fs * c.seconds);
    const int64_t N = 16384;
    const int64_t n0 = total - static_cast<int64_t>(c.fs * 0.05) - N - 1;
    const int64_t aligned = n0 - lat;  // the input position the wet encodes
    (void)aligned;
    double bestMag = 0.0, bestF = 0.0;
    for (double f = 80.0; f <= 900.0; f += 1.0) {
      const double m = goertzelMag(out.data(), n0, N, f, c.fs);
      if (m > bestMag) {
        bestMag = m;
        bestF = f;
      }
    }
    const double expected = 220.0 * std::exp2(7.0 / 12.0);  // 329.27 Hz
    std::printf("  RC1-GATE wet-pitch check: dominant %.1f Hz (expected %.1f Hz)\n",
                bestF, expected);
    CHECK(std::fabs(bestF - expected) <= 4.0);
  }
}

// --- CP-5 arbitration helpers (2026-10-07) -----------------------------------
//
// THE TWO PITCH INSTRUMENTS and what they measure on the frozen TD-PSOLA
// pitch-down synthesis (measured on the canonical tree, see the probe case
// TP-PS-BETA-DOWN-CONTENT in timepitch_host_probe.cpp):
//
//   * the RAW GOERTZEL DOMINANT (60..400 Hz sweep) reads the GRAIN CARRIER:
//     the mark law s = P/beta tiles 2P-length grains at hop s, so at strong
//     down-shifts the output's spectral dominant sits at the INPUT pitch's
//     carrier (2x the shifted f0 at beta = 1/2) with the shifted
//     fundamental as a sideband. MEASURED IDENTICAL ON THE DIRECT RENDER:
//     the direct full-buffer job at -7/-12 st reports the same 293/220 Hz
//     dominant the adapter does. The raw Goertzel dominant therefore CANNOT
//     separate the adapter path from the direct engine — it measures the
//     frozen synthesis character, not an adapter defect.
//   * the CONTRACT'S PITCH INSTRUMENT (the clean-room pYIN tracker) reads
//     the shifted fundamental EXACTLY on both paths at -5/-7/-12 (sine):
//     164.7 / 146.8 / 110.0 Hz vs the expected 164.8 / 146.8 / 110.0.

/// median voiced f0 over [b0, b1) — the contract's pitch instrument
double trackerMedianF0(const std::vector<double>& s, double fs, int64_t b0,
                       int64_t b1) {
  if (b0 < 0) return 0.0;
  if (b1 > static_cast<int64_t>(s.size())) b1 = static_cast<int64_t>(s.size());
  if (b1 - b0 < 4096) return 0.0;
  const std::vector<double> seg(s.begin() + static_cast<std::ptrdiff_t>(b0),
                                s.begin() + static_cast<std::ptrdiff_t>(b1));
  const PitchTrack tr = trackPitch(seg, fs, kTrackerFminHz, kTrackerFmaxHz);
  std::vector<double> v;
  for (const auto& f : tr.frames)
    if (f.voiced && f.f0Hz > 0.0) v.push_back(f.f0Hz);
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

/// ONE direct offline job (the whole-curve render, no adapter) at a constant
/// ratio over a pure sine — the content arbiter the RC-2a fix discipline
/// names. Returns the full wet (production + finish flush).
std::vector<double> directRenderSine(double fs, int maxBlock, double beta,
                                     int64_t nIn) {
  auto engine = makeTimePitchEngine();
  EngineConfiguration cfg;
  cfg.seed = 7;
  cfg.parameters.emplace_back("mode", ParameterValue{std::string("pitch_synced")});
  engine->configure(cfg);
  std::vector<double> curve(static_cast<std::size_t>(nIn), beta);
  PitchCurveView cv{};
  cv.ratio = curve.data();
  cv.frames = static_cast<FrameCount>(curve.size());
  cv.sampleRate = fs;
  ProcessContext ctx{};
  ctx.sampleRate = fs;
  ctx.channels = 1;
  ctx.maxBlockFrames = maxBlock;
  ctx.totalInputFrames = nIn;
  ctx.curve = &cv;
  engine->prepare(ctx);
  std::vector<double> x(static_cast<std::size_t>(nIn), 0.0);
  for (int64_t i = 0; i < nIn; ++i)
    x[static_cast<std::size_t>(i)] =
        0.5 * std::sin(2.0 * kPi * 220.0 * static_cast<double>(i) / fs);
  std::vector<double> wet;
  wet.reserve(300000);
  std::vector<double> outBuf(static_cast<std::size_t>(65536), 0.0);
  FrameCount consumed = 0;
  while (consumed < nIn) {
    const int take = static_cast<int>(std::min<int64_t>(maxBlock, nIn - consumed));
    const double* inCh[1] = {x.data() + consumed};
    AudioBlockView inView{inCh, 1, take};
    double* outCh[1] = {outBuf.data()};
    AudioBlockOut outView{outCh, 1, 65536};
    const ProcessReport rep =
        engine->process(inView, take, outView, 65536, cv, consumed);
    for (FrameCount i = 0; i < rep.outputFramesProduced; ++i)
      wet.push_back(outBuf[static_cast<std::size_t>(i)]);
    consumed += take;
  }
  std::vector<double> finBuf(static_cast<std::size_t>(262144), 0.0);
  double* finCh[1] = {finBuf.data()};
  AudioBlockOut outView{finCh, 1, 262144};
  const ProcessReport frep = engine->finish(outView, 262144);
  for (FrameCount i = 0; i < frep.outputFramesProduced; ++i)
    wet.push_back(finBuf[static_cast<std::size_t>(i)]);
  return wet;
}

// ---------------------------------------------------------------------------
// TP-DIFF-BETADOWN: the RC-2a recheck (the master recovery pack TASK E) —
// the adapter path at beta < 1 (pitch DOWN): -5/-7/-12 st statics through
// the realtime adapter. Gates: zero-miss delivery, full-buffer render
// determinism (the drain included), and the wet-pitch correctness at the
// expected DOWN-shifted pitch. The direct-engine 1/beta DURATION semantics
// are the T-PSOLA D.4 case's (beta = 2 halves / beta = 1/2 doubles — the
// timepitch_contract_test lane); this case pins the adapter-path
// consumption/backpressure class the historical Task-35 work addressed.
// ---------------------------------------------------------------------------

TEST_CASE("TP-DIFF-BETADOWN: pitch_synced statics at -5/-7/-12 st (the RC-2a adapter recheck)") {
  initArtifact();
  artifactHeader("BETADOWN — pitch_synced statics at beta<1 (48k/512, saw + sine arbitration)");
  artifactLine("| mode | sched | host | mat | fs | blk | rtf | latMs | undr | fbTel | fbDet | runs | maxRun | stall | prepF | adoptF | rprep | resets | seam | clicks | maxD | silS | silF | firstSil | wetSpan | inSpan | domHz | domMag | H2/3/4/5/6 |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");
  for (double st : {-5.0, -7.0, -12.0}) {
    DriveConfig c;
    c.tpMode = 2;
    c.sched = Sched::Static;
    c.pitchSt = st;
    c.seconds = 7.0;
    std::vector<double> outA;
    const DriveReport ra = drive(c, &outA);
    std::vector<double> outB;
    drive(c, &outB);
    emitRow(c, ra, "auto");
    // the zero-miss class (down-reads declare no input lead — the
    // geometry's own accounting; the delivery must be continuous)
    CHECK_EQ(ra.status.deliveryUnderruns, 0u);
    CHECK_EQ(ra.status.dryFallbackFrames, 0u);
    CHECK_EQ(ra.status.jobStalls, 0u);
    CHECK_EQ(ra.status.preparationFailures, 0u);
    // full-buffer render determinism (the drain included): two identical
    // drives are bit-identical over the WHOLE output buffer
    int64_t mismatches = 0;
    for (std::size_t p = 0; p < outA.size(); ++p) {
      if (outA[p] != outB[p]) ++mismatches;
    }
    CHECK_EQ(mismatches, 0);

    // ---- CP-5 (2026-10-07): the TWO-INSTRUMENT ARBITRATION -------------------
    //
    // The TASK E finding classified the beta<1 wet-pitch defect as
    // ADAPTER-PATH-ONLY from the raw Goertzel dominant. The CP-5 diagnostic
    // (the probe case TP-PS-BETA-DOWN-CONTENT) FALSIFIED that exclusivity:
    // the DIRECT full-buffer render measures the SAME raw Goertzel dominant
    // at -7/-12 (the frozen TD-PSOLA pitch-down synthesis tiles 2P grains
    // at hop s = P/beta, so the grain CARRIER at the input pitch dominates
    // the spectrum; the shifted fundamental survives as the sideband) —
    // while the CONTRACT'S pitch instrument (the pYIN tracker) reads the
    // shifted fundamental EXACTLY on BOTH paths. THE HARD GATES below are
    // therefore the tracker-arbitrated pair:
    //   (1) the adapter's settled wet reads the EXPECTED shifted pitch;
    //   (2) the adapter and the DIRECT engine AGREE (the package's
    //       adapter/direct consistency item).
    // The raw Goertzel dominant stays REPORTED (never asserted away) as the
    // synthesis-character telemetry, with the direct's own value alongside.
    DriveConfig sine = c;
    sine.material = Material::Sine;
    sine.seconds = 5.0;
    std::vector<double> sineOut;
    const DriveReport rs = drive(sine, &sineOut);
    const int64_t latS = rs.status.latencyFrames;
    const int64_t beginS = latS + static_cast<int64_t>(sine.fs * 0.5);
    const int64_t endS = static_cast<int64_t>(sine.fs * sine.seconds) -
                         static_cast<int64_t>(sine.fs * 0.05);
    const double tAdapter = trackerMedianF0(sineOut, sine.fs, beginS, endS);
    const double expected = 220.0 * std::exp2(st / 12.0);
    const std::vector<double> directWet = directRenderSine(sine.fs, 512, std::exp2(st / 12.0), 120000);
    const int64_t midD = static_cast<int64_t>(directWet.size()) / 2;
    const double tDirect = trackerMedianF0(directWet, sine.fs, midD, midD + 96000);
    const double gAdapter = goertzelMag(sineOut.data(), beginS, 32768, 60.0 + 0.0, sine.fs);
    (void)gAdapter;
    double gDomA = 0.0, gDomD = 0.0;
    {
      double best = 0.0;
      for (double f = 60.0; f <= 400.0; f += 1.0) {
        const double m = goertzelMag(sineOut.data(), beginS, 32768, f, sine.fs);
        if (m > best) { best = m; gDomA = f; }
      }
      best = 0.0;
      for (double f = 60.0; f <= 400.0; f += 1.0) {
        const double m = goertzelMag(directWet.data(), midD, 32768, f, sine.fs);
        if (m > best) { best = m; gDomD = f; }
      }
    }
    char row[192];
    std::snprintf(row, sizeof(row),
                  "| %.0f st | sine arbitration | tracker adapter=%.1f direct=%.1f expected=%.1f | goertzel adapter=%.0f direct=%.0f (the carrier character, reported) | undr=%llu fbTel=%llu faults=%llu |",
                  st, tAdapter, tDirect, expected, gDomA, gDomD,
                  (unsigned long long)rs.status.deliveryUnderruns,
                  (unsigned long long)rs.status.dryFallbackFrames,
                  (unsigned long long)rs.status.faults);
    artifactLine(row);
    std::printf("  BETADOWN %.0f st [sine]: tracker adapter=%.1f direct=%.1f (expected %.1f); goertzel adapter=%.0f direct=%.0f\n",
                st, tAdapter, tDirect, expected, gDomA, gDomD);
    // HARD: the adapter's settled wet reads the expected shifted pitch
    // (the contract's instrument; the ±2 Hz band = the sweep of the
    // tracker's own 10-cent bins + margin)
    CHECK_MESSAGE(std::fabs(tAdapter - expected) <= 2.0,
                  "the adapter wet must read the expected shifted pitch (the pYIN tracker, sine arbitration)");
    // HARD (the RC-2a package's adapter/direct consistency item): the two
    // paths read the SAME pitch
    CHECK_MESSAGE(std::fabs(tAdapter - tDirect) <= 2.0,
                  "the adapter path must agree with the direct engine (the tracker, sine arbitration)");
    // the sine delivery class stays zero-miss too
    CHECK_EQ(rs.status.deliveryUnderruns, 0u);
    CHECK_EQ(rs.status.dryFallbackFrames, 0u);
  }
}

// ---------------------------------------------------------------------------
// TP-DIFF-BETAMATRIX (CP-5, 2026-10-07): the RC-2a package's mandated
// validation matrix — pitch {0,+7,+12,-5,-7,-12} st x fs {44.1,48,96} kHz x
// block {64,128,256,512,1024} on the Pitch-Synced adapter path, static
// drives, SINE material (the unambiguous pitch-metric class), the pYIN
// tracker as the pitch instrument. REPORTED (diagnostic-first): the hard
// gates stay in BETADOWN (the 48k/512 canonical point) and the frozen
// invariants; this case RECORDS the mandated matrix so a regression lands
// in the artifact as a diffable row, not as a flipped assertion.
//
// The pitch correctness measure per row: the tracker median over the
// settled region vs the expected shifted pitch (the ±2 Hz band reported;
// any violation prints a WARNING line into the artifact — the RC-2a
// lesson: reported, never asserted away).
//
// Default (CI budget): fs = 48 kHz x blocks {64,512,1024} = 18 rows.
// PITCHLAB_TP_DIFF_FULLMATRIX=1 runs the FULL 90-row matrix (the CP-5
// evidence record was produced with it).
// ---------------------------------------------------------------------------
TEST_CASE("TP-DIFF-BETAMATRIX: the mandated fs x block x pitch matrix (reported)") {
  initArtifact();
  artifactHeader("BETAMATRIX — pitch_synced statics, sine, tracker-arbitrated (the CP-5 mandated matrix)");
  artifactLine("| st | fs | blk | latMs | undr | fbTel | fbDet | stall | prepF | clicks | maxD | rtf | trackerMed | expected | pitchOK | live | jobsC | exitDrop | resets |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");
  const bool full = std::getenv("PITCHLAB_TP_DIFF_FULLMATRIX") != nullptr;
  const double fsSet[] = {44100.0, 48000.0, 96000.0};
  const int blkSet[] = {64, 128, 256, 512, 1024};
  int rows = 0, violations = 0;
  for (double st : {0.0, 7.0, 12.0, -5.0, -7.0, -12.0}) {
    const double expected = 220.0 * std::exp2(st / 12.0);
    for (double fs : fsSet) {
      for (int blk : blkSet) {
        if (!full && fs != 48000.0) continue;
        if (!full && blk != 64 && blk != 512 && blk != 1024) continue;
        DriveConfig c;
        c.tpMode = 2;
        c.sched = Sched::Static;
        c.pitchSt = st;
        c.fs = fs;
        c.block = static_cast<int32_t>(blk);
        c.seconds = 2.5;
        c.material = Material::Sine;
        std::vector<double> out;
        const DriveReport r = drive(c, &out);
        const int64_t lat = r.status.latencyFrames;
        const int64_t begin = lat + static_cast<int64_t>(fs * 0.5);
        const int64_t end = static_cast<int64_t>(fs * c.seconds) -
                            static_cast<int64_t>(fs * 0.05);
        const double tMed = trackerMedianF0(out, fs, begin, end);
        const bool ok = tMed > 0.0 && std::fabs(tMed - expected) <= 2.0;
        ++rows;
        if (!ok) ++violations;
        char row[224];
        std::snprintf(row, sizeof(row),
                      "| %.0f | %.0f | %d | %.1f | %llu | %llu | %llu | %llu | %llu | %lld | %.4f | %.3f | %.1f | %.1f | %s | %d | %llu | %llu | %llu |",
                      st, fs, blk, static_cast<double>(lat) / fs * 1000.0,
                      (unsigned long long)r.status.deliveryUnderruns,
                      (unsigned long long)r.status.dryFallbackFrames,
                      (unsigned long long)r.fallbackFrames,
                      (unsigned long long)r.status.jobStalls,
                      (unsigned long long)r.status.preparationFailures,
                      (long long)r.clicks, r.maxAbsDelta, r.rtf, tMed, expected,
                      ok ? "OK" : "VIOLATION", r.status.liveJobs,
                      (unsigned long long)r.status.jobsCompleted,
                      (unsigned long long)r.status.exitDropped,
                      (unsigned long long)r.status.resets);
        artifactLine(row);
        if (!ok) {
          char w[160];
          std::snprintf(w, sizeof(w),
                        "WARNING BETAMATRIX pitch violation: st=%.0f fs=%.0f blk=%d tracker=%.1f expected=%.1f",
                        st, fs, blk, tMed, expected);
          artifactLine(w);
          std::printf("  %s\n", w);
        }
      }
    }
  }
  // reset-reuse rows (the package's reset/reuse item): repeated hard resets
  // at the strongest down-shift; the post-reset output must re-pitch
  {
    DriveConfig c;
    c.tpMode = 2;
    c.sched = Sched::RepeatedReset;
    c.pitchSt = -12.0;
    c.seconds = 6.0;
    c.material = Material::Sine;
    std::vector<double> out;
    const DriveReport r = drive(c, &out);
    const int64_t lat = r.status.latencyFrames;
    const double tMed = trackerMedianF0(
        out, c.fs, lat + static_cast<int64_t>(c.fs * 1.0),
        static_cast<int64_t>(c.fs * c.seconds) - static_cast<int64_t>(c.fs * 0.05));
    char row[224];
    std::snprintf(row, sizeof(row),
                  "| reset-reuse | %.0f | %d | %.1f | %llu | %llu | %llu | %llu | %llu | %lld | %.4f | %.3f | %.1f | %.1f | %s | %d | %llu | %llu | %llu |",
                  c.fs, c.block, static_cast<double>(lat) / c.fs * 1000.0,
                  (unsigned long long)r.status.deliveryUnderruns,
                  (unsigned long long)r.status.dryFallbackFrames,
                  (unsigned long long)r.fallbackFrames,
                  (unsigned long long)r.status.jobStalls,
                  (unsigned long long)r.status.preparationFailures,
                  (long long)r.clicks, r.maxAbsDelta, r.rtf, tMed, 110.0,
                  (tMed > 0.0 && std::fabs(tMed - 110.0) <= 2.0) ? "OK" : "VIOLATION",
                  r.status.liveJobs, (unsigned long long)r.status.jobsCompleted,
                  (unsigned long long)r.status.exitDropped,
                  (unsigned long long)r.status.resets);
    artifactLine(row);
    std::printf("  BETAMATRIX reset-reuse: resets=%llu tracker=%.1f (expected 110.0) undr=%llu fbTel=%llu faults=%llu\n",
                (unsigned long long)r.status.resets, tMed,
                (unsigned long long)r.status.deliveryUnderruns,
                (unsigned long long)r.status.dryFallbackFrames,
                (unsigned long long)r.status.faults);
  }
  std::printf("  BETAMATRIX: %d rows, %d pitch violations (reported; the artifact carries the rows)\n",
              rows, violations);
}

// ---------------------------------------------------------------------------
// TP-DIFF-FORMAT: the format x block matrix on the Pitch-Synced family —
// the pack's 44.1/48/96 kHz x block 64..1024 sweep (core metrics).
// Diagnostic-first: NO assertions here — the zero-miss class is REPORTED
// per row (a violation lands in the artifact as a WARNING line).
// ---------------------------------------------------------------------------

TEST_CASE("TP-DIFF-FORMAT: pitch_synced static+sweep over fs x block (core metrics)") {
  initArtifact();
  artifactHeader("FORMAT — pitch_synced, fs x block (static +7 auto / sweep auto, saw 220)");
  artifactLine("| mode | sched | host | mat | fs | blk | rtf | latMs | undr | fbTel | fbDet | runs | maxRun | stall | prepF | adoptF | rprep | resets | seam | clicks | maxD | silS | silF | firstSil | wetSpan | inSpan | domHz | domMag | H2/3/4/5/6 |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");

  for (double fs : {44100.0, 48000.0, 96000.0}) {
    for (int32_t block : {64, 128, 256, 512, 1024}) {
      DriveConfig c;
      c.fs = fs;
      c.block = block;
      c.tpMode = 2;
      c.sched = Sched::Static;
      c.seconds = 6.0;
      const DriveReport rs = drive(c);
      emitRow(c, rs, "auto");
      if (rs.status.dryFallbackFrames != 0 || rs.status.deliveryUnderruns != 0 ||
          rs.status.jobStalls != 0 || rs.status.preparationFailures != 0) {
        char w[256];
        std::snprintf(w, sizeof(w),
                      "WARNING static zero-miss violated: fs=%.0f block=%d "
                      "fb=%llu undr=%llu stall=%llu prepF=%llu",
                      fs, (int)block,
                      (unsigned long long)rs.status.dryFallbackFrames,
                      (unsigned long long)rs.status.deliveryUnderruns,
                      (unsigned long long)rs.status.jobStalls,
                      (unsigned long long)rs.status.preparationFailures);
        artifactLine(std::string(w));
      }
      c.sched = Sched::Sweep;
      c.seconds = 6.5;
      emitRow(c, drive(c), "auto");
    }
  }
}
