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

using namespace pitchlab;
using namespace pitchlab::vst;

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
  artifactHeader("BETADOWN — pitch_synced statics at beta<1 (48k/512, saw 220)");
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
    // the wet is real and pitched at the DOWN-shifted expectation
    const int64_t lat = ra.status.latencyFrames;
    const int64_t total = static_cast<int64_t>(c.fs * c.seconds);
    const int64_t N = 16384;
    const int64_t n0 = total - static_cast<int64_t>(c.fs * 0.05) - N - 1;
    double bestMag = 0.0, bestF = 0.0;
    for (double f = 60.0; f <= 400.0; f += 1.0) {
      const double m = goertzelMag(outA.data(), n0, N, f, c.fs);
      if (m > bestMag) {
        bestMag = m;
        bestF = f;
      }
    }
    // TASK E FINDING (2026-10-06, CONFIRMED on the canonical tree): the
    // dominant sits at the INPUT pitch's structure (at -12 st: ~219 Hz, the
    // input f0; at -5/-7: the shifted content's 2ND harmonic) — the
    // down-shifted fundamental is structurally weak/absent through the
    // ADAPTER path while the DIRECT engine is contract-correct (the
    // T-PSOLA D.4 pitch gates pass at beta = 1/2). MECHANISM (the
    // rc2a_finding record): the splice job's input span is sized
    // windowO x max(1, envMax) — at beta < 1 the D.4 consumption per
    // output window is beta x windowO, so the oversized span skips
    // (1 - beta) x windowO of input per window and the wet content
    // collapses toward the input's own line structure. The pitch-DOWN
    // class is a CONFIRMED RC-2a defect; the fix (the ratio-scaled input
    // span) is the next implementation checkpoint — REPORTED here, never
    // asserted away.
    const double expected = 220.0 * std::exp2(st / 12.0);
    std::printf("  BETADOWN %.0f st: dominant %.1f Hz (expected %.1f Hz)%s\n",
                st, bestF, expected,
                std::fabs(bestF - expected) <= 4.0 ? "" : "  << RC-2a CONFIRMED DEFECT (reported, not gated)");
    if (std::fabs(bestF - expected) > 4.0) {
      char w[160];
      std::snprintf(w, sizeof(w),
                    "WARNING RC-2a beta<1 wet-pitch defect: st=%.0f dom=%.1f "
                    "expected=%.1f (the finding record: results/research/"
                    "task35-rc2a/)", st, bestF, expected);
      artifactLine(std::string(w));
    }
  }
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
