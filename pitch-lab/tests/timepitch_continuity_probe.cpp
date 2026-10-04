// native.timepitch — HOST-CONTINUITY PROBE (task-34 continuation, Phase 1:
// the owner's second-round real-host observations):
//
//   1. Fixed/RECT improved but OCCASIONAL crackles remain audible.
//   2/3. Pitch Synced does not produce a stable pitch shift; the effect is
//        INTERMITTENT (present in some sections, absent in others).
//   5. Pitch + Formant shows essentially the same intermittent behaviour.
//   6. Moving the Pitch parameter sounds like the wet signal is being
//      switched on and off (HIGH-PRIORITY clue).
//
// This suite drives the REAL adapter (the exact VST3 process path) and
// separates the candidate defect classes the task mandates (1A A..G):
//   * DRY-FALLBACK frames (the emission served the latency-compensated dry
//     lane — the wet lane missed INSIDE the covered range; classes D/E):
//     a settled-region frame whose output is BIT-EQUAL to the dry input.
//     At mix = 1, 0 dB, bypass off a wet frame can never be bit-equal to
//     the raw input. The wet-end miss of a rate-following chain is also
//     dry-served — this probe makes that class visible per frame.
//   * WET-SILENCE sections (class A — the DSP produced silent wet where
//     the input was strong: the "effect disappears" experience WITHOUT any
//     delivery fault): 20 ms sashes where the input RMS is high and the
//     output RMS is near zero.
//   * CLICKS (the audible discontinuity): settled-region sample-to-sample
//     deltas beyond the threshold, with their positions for the
//     modulo-advance pattern analysis (class B vs A discrimination).
//   * THE PITCH-SLIDE MODEL (1F): both host behaviours —
//       (a) automation-only (the snapshot stays at the start value; the
//           host streams IParameterChanges only),
//       (b) automation + SNAPSHOT FLOOD (the host also calls
//           setParamNormalized/publishSnapshot continuously — the drag
//           model; the prep-thread's kParamSettleMs debounce never
//           settles during the flood).
//     Measured: reprepares (the rebuild churn), the dry-fallback bursts
//     around rebuilds, the clicks, and the wet continuity per sweep phase.
//
// DIAGNOSTIC-FIRST: the tables print; the static-voiced zero-fallback
// expectation is asserted (it was the hostval-1 result — a regression of
// THAT is a real defect), the rest stays a measured report.

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

// --- input materials (1C) --------------------------------------------------

enum class Material { Sine, Saw, Noise, Mixed };

const char* materialName(Material m) {
  switch (m) {
    case Material::Sine: return "sine";
    case Material::Saw: return "saw";
    case Material::Noise: return "noise";
    case Material::Mixed: return "mixed";
  }
  return "?";
}

// Mixed: saw (voiced) / silence / saw / noise (unvoiced) / saw — the
// voicing boundaries + the transient gap, deterministic.
std::vector<double> makeMixed(int64_t frames, double fs, double amp,
                              double freq) {
  std::vector<double> x(static_cast<std::size_t>(frames), 0.0);
  uint64_t rng = 0x9E3779B97F4A7C15ULL;
  auto next01 = [&rng]() {
    rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<double>((rng >> 11) & ((1ULL << 52) - 1)) /
           static_cast<double>(1ULL << 52);
  };
  const int64_t segA = static_cast<int64_t>(2.0 * fs);  // saw
  const int64_t segB = static_cast<int64_t>(2.6 * fs);  // silence end
  const int64_t segC = static_cast<int64_t>(4.6 * fs);  // saw end
  const int64_t segD = static_cast<int64_t>(6.0 * fs);  // noise end
  for (int64_t i = 0; i < frames; ++i) {
    double v = 0.0;
    const double t = static_cast<double>(i) / fs;
    if (i < segA) {
      v = std::sin(2.0 * kPi * freq * t) + 0.5 * std::sin(2.0 * kPi * 2.0 * freq * t);
    } else if (i < segB) {
      // soft-edge silence (a 5 ms fade each side — no hard cut clicks from
      // the INPUT itself)
      const double fadeIn = std::min(1.0, static_cast<double>(i - segA) / (0.005 * fs));
      const double fadeOut = std::min(1.0, static_cast<double>(segB - i) / (0.005 * fs));
      v = std::sin(2.0 * kPi * freq * t) * 0.25 * std::min(fadeIn, fadeOut);
    } else if (i < segC) {
      v = std::sin(2.0 * kPi * freq * t) + 0.5 * std::sin(2.0 * kPi * 2.0 * freq * t);
    } else if (i < segD) {
      v = 1.2 * (next01() * 2.0 - 1.0);
    } else {
      v = std::sin(2.0 * kPi * freq * t) + 0.5 * std::sin(2.0 * kPi * 2.0 * freq * t);
    }
    x[static_cast<std::size_t>(i)] = amp * v / 1.5;
  }
  return x;
}

std::vector<double> makeInput(Material m, int64_t frames, double fs,
                              double amp, double freq) {
  uint64_t rng = 0x9E3779B97F4A7C15ULL;
  auto next01 = [&rng]() {
    rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<double>((rng >> 11) & ((1ULL << 52) - 1)) /
           static_cast<double>(1ULL << 52);
  };
  std::vector<double> x(static_cast<std::size_t>(frames), 0.0);
  for (int64_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / fs;
    double v = 0.0;
    switch (m) {
      case Material::Sine:
        v = std::sin(2.0 * kPi * freq * t);
        break;
      case Material::Saw:
        // band-limited-ish harmonic stack (6 harmonics — the tracker's
        // voiced material)
        for (int h = 1; h <= 6; ++h) {
          v += std::sin(2.0 * kPi * freq * static_cast<double>(h) * t) /
               static_cast<double>(h);
        }
        v /= 1.45;
        break;
      case Material::Noise:
        v = 1.2 * (next01() * 2.0 - 1.0);
        break;
      case Material::Mixed:
        return makeMixed(frames, fs, amp, freq);  // never reached (handled below)
    }
    x[static_cast<std::size_t>(i)] = amp * v;
  }
  if (m == Material::Mixed) return makeMixed(frames, fs, amp, freq);
  return x;
}

// --- the pitch-slide schedule (1C sweep: -12 -> 0 -> +12 -> 0 -> -12) ------

struct SweepSeg {
  double t0 = 0.0;  // seconds
  double t1 = 0.0;
  double st0 = 0.0;
  double st1 = 0.0;
};

// rateSt = semitones per second of the continuous move (the slider speed)
std::vector<SweepSeg> sweepSchedule(double holdS, double rateSt) {
  // hold(-12) -> move to 0 -> hold(0) -> move to +12 -> hold(+12) -> move to
  // 0 -> hold(0) -> move to -12 -> hold(-12)
  const double mv = 12.0 / rateSt;
  std::vector<SweepSeg> s;
  double t = 0.0;
  s.push_back({t, t += holdS, -12.0, -12.0});
  s.push_back({t, t += mv, -12.0, 0.0});
  s.push_back({t, t += holdS, 0.0, 0.0});
  s.push_back({t, t += mv, 0.0, 12.0});
  s.push_back({t, t += holdS, 12.0, 12.0});
  s.push_back({t, t += mv, 12.0, 0.0});
  s.push_back({t, t += holdS, 0.0, 0.0});
  s.push_back({t, t += mv, 0.0, -12.0});
  s.push_back({t, t += holdS, -12.0, -12.0});
  return s;
}

double sweepAt(const std::vector<SweepSeg>& s, double tS) {
  for (const auto& seg : s) {
    if (tS >= seg.t0 && tS <= seg.t1) {
      const double span = seg.t1 - seg.t0;
      const double u = span > 0 ? (tS - seg.t0) / span : 0.0;
      return seg.st0 + (seg.st1 - seg.st0) * u;
    }
  }
  return s.empty() ? 0.0 : s.back().st1;
}

// --- drive configuration ----------------------------------------------------

struct DriveConfig {
  double fs = 48000.0;
  int32_t block = 512;
  int64_t seconds = 12;
  int tpMode = 0;  // 0 fixed, 1 adaptive, 2 pitch_synced, 3 pitch_formant
  double pitchSt = 0.0;
  int64_t windowFrames = 2048;
  int64_t overlap = 2;
  int tpShape = 0;  // 0 hann, 1 hamming, 2 bartlett, 3 rect
  double formantRatio = 1.0;
  double amp = 0.5;
  double inputFreq = 220.0;
  Material material = Material::Saw;
  // the sweep (empty = static)
  std::vector<SweepSeg> sweep;
  bool snapshotFlood = false;  // 1F(b): publishSnapshot every block
};

ParamSnapshot snapshotFor(const DriveConfig& c, double pitchSt) {
  ParamSnapshot snap;
  snap.engineIndex = 5;  // native.timepitch
  snap.pitchSt = pitchSt;
  snap.tpMode = c.tpMode;
  snap.tpWindowFrames = c.windowFrames;
  snap.tpOverlap = c.overlap;
  snap.tpShape = c.tpShape;
  snap.tpFormantRatio = c.formantRatio;
  snap.mix = 1.0;
  snap.bypass = false;
  snap.outputDb = 0.0;
  snap.lfoRateHz = 0.0;
  snap.lfoDepthSt = 0.0;
  return snap;
}

struct Click {
  int64_t pos = 0;
  double delta = 0.0;
};

struct SilentSash {
  int64_t start = 0;
  int64_t len = 0;
};

struct DriveReport {
  StatusSnapshot status;
  double rtf = 0.0;
  int64_t fallbackFrames = 0;
  int64_t fallbackRuns = 0;
  int64_t fallbackMaxRun = 0;
  double firstFallbackAt = -1.0;
  int64_t wetSilenceFrames = 0;  // in sash-sampled frames (20 ms sashes)
  int64_t wetSilenceSashes = 0;
  double firstSilenceAt = -1.0;
  int64_t clicks = 0;
  double maxAbsDelta = 0.0;
  std::vector<Click> clickList;    // up to 32 (the pattern analysis)
  std::vector<SilentSash> sashes;  // up to 32
  // Task 34: the per-block telemetry trace (the adapter's own counters —
  // immune to the dynamic-latency bit-equal false positives): the miss
  // bursts' TIME localization vs the rebuild/adoption events.
  struct BlockSample {
    double tS = 0.0;                 // stream time (s)
    uint64_t dryFallback = 0;        // cumulative (adapter telemetry)
    uint64_t underruns = 0;          // cumulative
    uint64_t reprepares = 0;         // cumulative
    uint64_t adopted = 0;            // cumulative
    uint64_t seamRecov = 0;          // cumulative
    int64_t latency = 0;             // Λ_eff snapshot
  };
  std::vector<BlockSample> trace;    // filled when collectTrace
};

struct FallbackRun {
  int64_t start = 0;
  int64_t len = 0;
};

// (helper for the flood snapshot's block-end timestamp)
double t1sOf(int64_t pos, int32_t take, const DriveConfig& c) {
  return static_cast<double>(pos + take) / c.fs;
}

DriveReport drive(const DriveConfig& c, bool collectTrace = false) {
  RealtimeAdapter adapter;
  adapter.activate(c.fs, 2, c.block);
  adapter.setParameterSnapshot(snapshotFor(c, c.sweep.empty() ? c.pitchSt
                                                              : sweepAt(c.sweep, 0.0)));
  adapter.requestHardReset();

  const int64_t total = static_cast<int64_t>(c.fs * static_cast<double>(c.seconds));
  std::vector<double> sig;
  if (c.material == Material::Mixed) {
    sig = makeMixed(total, c.fs, c.amp, c.inputFreq);
  } else {
    sig = makeInput(c.material, total, c.fs, c.amp, c.inputFreq);
  }
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
    BlockAutomation auto_block;
    if (!c.sweep.empty()) {
      // the block's pitch timeline: the segment value at the block start
      // and end (frame-correct linear resolution inside)
      const double t0s = static_cast<double>(pos) / c.fs;
      const double t1s = static_cast<double>(pos + take) / c.fs;
      auto_block.pitch[0] = {0, sweepAt(c.sweep, t0s)};
      auto_block.pitch[1] = {take - 1, sweepAt(c.sweep, t1s)};
      auto_block.pitchCount = 2;
    }
    if (c.snapshotFlood && !c.sweep.empty()) {
      // 1F(b): the host drag model — the controller thread publishes the
      // snapshot with the parameter's CURRENT (block-end) value on every
      // block (the host-side setParamNormalized cadence)
      adapter.setParameterSnapshot(
          snapshotFor(c, sweepAt(c.sweep, t1sOf(pos, take, c))));
    }
    const auto t0 = std::chrono::steady_clock::now();
    adapter.process(in, o, take, auto_block);
    const auto t1 = std::chrono::steady_clock::now();
    cpuNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    pos += take;
    if (collectTrace) {
      const StatusSnapshot s = adapter.status();
      DriveReport::BlockSample bs;
      bs.tS = static_cast<double>(pos) / c.fs;
      bs.dryFallback = s.dryFallbackFrames;
      bs.underruns = s.deliveryUnderruns;
      bs.reprepares = s.reprepares;
      bs.adopted = s.chainsAdopted;
      bs.seamRecov = s.seamRecoveries;
      bs.latency = s.latencyFrames;
      rep.trace.push_back(bs);
    }
    std::this_thread::sleep_for(std::chrono::microseconds(250));
  }

  rep.status = adapter.status();
  const double audioNs = static_cast<double>(total) / c.fs * 1e9;
  rep.rtf = audioNs > 0 ? static_cast<double>(cpuNs) / audioNs : 0.0;

  const int64_t lat = rep.status.latencyFrames;
  const int64_t begin = lat + static_cast<int64_t>(c.fs * 0.2);
  const int64_t end = total - static_cast<int64_t>(c.fs * 0.05);

  // dry-fallback (bit-equal) detector + click detector
  bool inRun = false;
  FallbackRun cur;
  int64_t prevClickPos = -1;
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
        cur = FallbackRun{p, 0};
        if (rep.firstFallbackAt < 0) rep.firstFallbackAt = static_cast<double>(p) / c.fs;
      }
      ++cur.len;
    } else if (inRun) {
      inRun = false;
      ++rep.fallbackRuns;
      rep.fallbackMaxRun = std::max(rep.fallbackMaxRun, cur.len);
    }
    // click: settled-region sample-to-sample delta
    const double prev = out0[static_cast<std::size_t>(p - 1)];
    const double d = std::fabs(a - prev);
    rep.maxAbsDelta = std::max(rep.maxAbsDelta, d);
    if (d > 0.12) {
      ++rep.clicks;
      if (rep.clickList.size() < 32) {
        rep.clickList.push_back(Click{p, d});
      }
      (void)prevClickPos;
    }
  }
  if (inRun) {
    ++rep.fallbackRuns;
    rep.fallbackMaxRun = std::max(rep.fallbackMaxRun, cur.len);
  }

  // wet-silence detector: 20 ms sashes — input RMS high, output RMS ~0
  const int64_t sash = static_cast<int64_t>(0.02 * c.fs);
  for (int64_t s0 = begin; s0 + sash <= end; s0 += sash) {
    double inAcc = 0.0, outAcc = 0.0;
    for (int64_t i = 0; i < sash; ++i) {
      const double iv = sig[static_cast<std::size_t>(s0 + i - lat)];
      const double ov = out0[static_cast<std::size_t>(s0 + i)];
      inAcc += iv * iv;
      outAcc += ov * ov;
    }
    const double inRms = std::sqrt(inAcc / static_cast<double>(sash));
    const double outRms = std::sqrt(outAcc / static_cast<double>(sash));
    if (inRms > 0.05 && outRms < 0.002) {
      rep.wetSilenceFrames += sash;
      ++rep.wetSilenceSashes;
      if (rep.firstSilenceAt < 0) rep.firstSilenceAt = static_cast<double>(s0) / c.fs;
      if (rep.sashes.size() < 32) rep.sashes.push_back(SilentSash{s0, sash});
    }
  }

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

void printStaticRow(const DriveConfig& c, const DriveReport& r) {
  char geom[64];
  std::snprintf(geom, sizeof(geom), "N%lld/o%lld/s%d",
                (long long)c.windowFrames, (long long)c.overlap, c.tpShape);
  std::printf("| %-6s | %-13s | %-6s | %4.0f | %5.3f | %7.1f | %8llu | %8llu | %6llu | %6llu | %8.3f | %8llu | %7llu | %6llu | %5llu | %5llu | %5llu |\n",
              modeName(c.tpMode), geom, materialName(c.material), c.pitchSt, r.rtf,
              r.status.latencyFrames / c.fs * 1000.0,
              (unsigned long long)r.status.deliveryUnderruns,
              (unsigned long long)r.fallbackFrames,
              (unsigned long long)r.fallbackRuns,
              (unsigned long long)r.fallbackMaxRun,
              r.firstFallbackAt < 0 ? -1.0 : r.firstFallbackAt,
              (unsigned long long)r.wetSilenceFrames,
              (unsigned long long)r.clicks,
              (unsigned long long)r.status.jobStalls,
              (unsigned long long)r.status.reprepares,
              (unsigned long long)r.status.preparationFailures,
              (unsigned long long)r.status.chainsAdopted);
  char line[640];
  std::snprintf(line, sizeof(line),
                "| %s | %s | %s | %.0f | %.3f | %.1f | %llu | %llu | %llu | %llu | %.3f | %llu | %llu | %llu | %llu | %llu | %llu |",
                modeName(c.tpMode), geom, materialName(c.material), c.pitchSt, r.rtf,
                r.status.latencyFrames / c.fs * 1000.0,
                (unsigned long long)r.status.deliveryUnderruns,
                (unsigned long long)r.fallbackFrames,
                (unsigned long long)r.fallbackRuns,
                (unsigned long long)r.fallbackMaxRun,
                r.firstFallbackAt < 0 ? -1.0 : r.firstFallbackAt,
                (unsigned long long)r.wetSilenceFrames,
                (unsigned long long)r.clicks,
                (unsigned long long)r.status.jobStalls,
                (unsigned long long)r.status.reprepares,
                (unsigned long long)r.status.preparationFailures,
                (unsigned long long)r.status.chainsAdopted);
  artifactLine(line);
}

void printSweepRow(const DriveConfig& c, const DriveReport& r, const char* tag) {
  std::printf("| %-6s | %-10s | %5.3f | %7.1f | %8llu | %8llu(fbtel %8llu) | %6llu | %6llu | %8llu | %6llu | %5llu | %5llu | %5llu | %6llu |\n",
              modeName(c.tpMode), tag, r.rtf,
              r.status.latencyFrames / c.fs * 1000.0,
              (unsigned long long)r.status.deliveryUnderruns,
              (unsigned long long)r.fallbackFrames,
              (unsigned long long)r.status.dryFallbackFrames,
              (unsigned long long)r.fallbackRuns,
              (unsigned long long)r.fallbackMaxRun,
              (unsigned long long)r.wetSilenceFrames,
              (unsigned long long)r.clicks,
              (unsigned long long)r.status.reprepares,
              (unsigned long long)r.status.preparationFailures,
              (unsigned long long)r.status.chainsAdopted,
              (unsigned long long)r.status.seamRecoveries);
  char line[512];
  std::snprintf(line, sizeof(line),
                "| %s | %s | %.3f | %.1f | %llu | %llu | %llu | %llu | %llu | %llu | %llu | %llu | %llu | %llu | %llu |",
                modeName(c.tpMode), tag, r.rtf,
                r.status.latencyFrames / c.fs * 1000.0,
                (unsigned long long)r.status.deliveryUnderruns,
                (unsigned long long)r.fallbackFrames,
                (unsigned long long)r.status.dryFallbackFrames,
                (unsigned long long)r.fallbackRuns,
                (unsigned long long)r.fallbackMaxRun,
                (unsigned long long)r.wetSilenceFrames,
                (unsigned long long)r.clicks,
                (unsigned long long)r.status.reprepares,
                (unsigned long long)r.status.preparationFailures,
                (unsigned long long)r.status.chainsAdopted,
                (unsigned long long)r.status.seamRecoveries);
  artifactLine(line);
}

void printClickPattern(const DriveReport& r, int64_t advance, double fs) {
  if (r.clickList.empty()) return;
  std::printf("  first clicks (t[s], delta, mod-advance):\n");
  int shown = 0;
  for (const auto& cl : r.clickList) {
    if (shown++ >= 12) break;
    const int64_t m = advance > 0 ? (cl.pos % advance) : -1;
    std::printf("    %.3f s  d=%.3f  mod=%lld\n", cl.pos / fs, cl.delta,
                (long long)m);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// 1C — STATIC matrix: mode x material x pitch (mix=1, no automation)
// ---------------------------------------------------------------------------

TEST_CASE("TP-CONT-STATIC: mode x material x pitch (mix=1, no automation)") {
  if (const char* p = std::getenv("PITCHLAB_TP_CONT_ARTIFACT")) g_artifact = p;
  artifactLine("");
  artifactLine("## TP-CONT-STATIC");
  artifactLine("| mode | geom | material | st | RTF | latMs | underruns | fallback | runs | maxRun | first@s | wetSilenceF | clicks | stalls | rprep | prepF | adopt |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");

  const Material materials[] = {Material::Sine, Material::Saw, Material::Noise,
                                Material::Mixed};
  const double pitches[] = {0.0, -12.0, 12.0};
  const int modes[] = {0, 1, 2, 3};

  for (int mode : modes) {
    for (double st : pitches) {
      for (Material m : materials) {
        DriveConfig c;
        c.tpMode = mode;
        c.pitchSt = st;
        c.material = m;
        c.seconds = 10;
        const DriveReport r = drive(c);
        printStaticRow(c, r);
        // the STATIC ZERO-MISS expectation (the hostval-1 result): the
        // adapter's own covered-miss telemetry (immune to the pitch-0
        // bit-identity false positive of the out==in detector — the Fixed
        // OLA identity at pitch 0 IS bit-equal to the dry lane) must stay
        // ZERO on every static configuration.
        CHECK_EQ(r.status.dryFallbackFrames, 0u);
        CHECK_EQ(r.status.deliveryUnderruns, 0u);
        CHECK_EQ(r.status.jobStalls, 0u);
        CHECK_EQ(r.status.preparationFailures, 0u);
        // the wet must not SILENTLY disappear on strong input (the "effect
        // disappears" class) — diagnostic print with the sash positions
        if (r.wetSilenceSashes > 0) {
          std::printf("    WET-SILENCE %s st=%.0f: %llu sashes (%llu frames), first at %.2f s:",
                      materialName(m), st, (unsigned long long)r.wetSilenceSashes,
                      (unsigned long long)r.wetSilenceFrames,
                      r.firstSilenceAt < 0 ? -1.0 : r.firstSilenceAt);
          int shown = 0;
          for (const auto& sh : r.sashes) {
            if (shown++ >= 12) break;
            std::printf(" %.2f", sh.start / c.fs);
          }
          std::printf("\n");
          artifactLine("  wet-silence: " + std::string(modeName(mode)) + " " +
                       materialName(m) + " st=" + std::to_string((int)st) +
                       " sashes=" + std::to_string(r.wetSilenceSashes) +
                       " first=" + std::to_string(r.firstSilenceAt));
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 1D — RECT focus: the remaining crackle question on the host path
// ---------------------------------------------------------------------------

TEST_CASE("TP-CONT-RECT: Fixed/rect continuity focus") {
  artifactLine("");
  artifactLine("## TP-CONT-RECT");
  artifactLine("| geom | material | st | RTF | underruns | fallback | runs | maxRun | clicks | maxDelta | stalls | rprep |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|");
  const Material mats[] = {Material::Sine, Material::Saw, Material::Noise,
                           Material::Mixed};
  for (int64_t o : {2, 4, 8}) {
    for (Material m : mats) {
      for (double st : {0.0, 12.0, -12.0}) {
        DriveConfig c;
        c.tpMode = 0;
        c.tpShape = 3;  // rect
        c.overlap = o;
        c.material = m;
        c.pitchSt = st;
        c.seconds = 8;
        const DriveReport r = drive(c);
        char geom[32];
        std::snprintf(geom, sizeof(geom), "rect/o%lld", (long long)o);
        std::printf("| %-9s | %-6s | %4.0f | %5.3f | %8llu | %8llu | %6llu | %6llu | %6llu | %8.3f | %6llu | %5llu |\n",
                    geom, materialName(m), st, r.rtf,
                    (unsigned long long)r.status.deliveryUnderruns,
                    (unsigned long long)r.fallbackFrames,
                    (unsigned long long)r.fallbackRuns,
                    (unsigned long long)r.fallbackMaxRun,
                    (unsigned long long)r.clicks, r.maxAbsDelta,
                    (unsigned long long)r.status.jobStalls,
                    (unsigned long long)r.status.reprepares);
        char line[320];
        std::snprintf(line, sizeof(line),
                      "| %s | %s | %.0f | %.3f | %llu | %llu | %llu | %llu | %llu | %.3f | %llu | %llu |",
                      geom, materialName(m), st, r.rtf,
                      (unsigned long long)r.status.deliveryUnderruns,
                      (unsigned long long)r.fallbackFrames,
                      (unsigned long long)r.fallbackRuns,
                      (unsigned long long)r.fallbackMaxRun,
                      (unsigned long long)r.clicks, r.maxAbsDelta,
                      (unsigned long long)r.status.jobStalls,
                      (unsigned long long)r.status.reprepares);
        artifactLine(line);
        CHECK_EQ(r.status.dryFallbackFrames, 0u);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 1C/1F — SWEEP: -12 -> 0 -> +12 -> 0 -> -12, both host behaviours
// ---------------------------------------------------------------------------

TEST_CASE("TP-CONT-SWEEP: pitch-slide continuity (all modes, both host models)") {
  artifactLine("");
  artifactLine("## TP-CONT-SWEEP");
  artifactLine("| mode | host | RTF | latMs | underruns | fallback(fbtel) | runs | maxRun | wetSilenceF | clicks | rprep | prepF | adopt | seam |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");

  const int modes[] = {0, 1, 2, 3};
  const Material mats[] = {Material::Saw, Material::Mixed};
  for (int mode : modes) {
    for (Material m : mats) {
      for (int host = 0; host < 2; ++host) {
        DriveConfig c;
        c.tpMode = mode;
        c.material = m;
        c.seconds = 14;
        c.sweep = sweepSchedule(2.0, 24.0);  // 2 st / 0.1 s = the drag speed
        c.snapshotFlood = host == 1;
        const DriveReport r = drive(c, /*collectTrace=*/true);
        printSweepRow(c, r, host == 0 ? "auto-only" : "auto+flood");
        // the trace: localize the miss bursts against the rebuild/adoption
        // events (the first 24 counter-increment blocks + every adoption)
        {
          uint64_t prevFb = 0, prevRe = 0, prevAd = 0;
          int shown = 0;
          for (const auto& bs : r.trace) {
            const bool event = bs.reprepares != prevRe || bs.adopted != prevAd;
            if ((bs.dryFallback != prevFb || event) && shown < 48) {
              std::printf("    t=%.2fs fb=%llu (+%llu) und=%llu reprep=%llu adopt=%llu seam=%llu lat=%lld%s\n",
                          bs.tS, (unsigned long long)bs.dryFallback,
                          (unsigned long long)(bs.dryFallback - prevFb),
                          (unsigned long long)bs.underruns,
                          (unsigned long long)bs.reprepares,
                          (unsigned long long)bs.adopted,
                          (unsigned long long)bs.seamRecov,
                          (long long)bs.latency,
                          event ? "  <-- rebuild/adopt" : "");
              ++shown;
            }
            prevFb = bs.dryFallback; prevRe = bs.reprepares; prevAd = bs.adopted;
          }
        }
        // the sweep's diagnostic evidence — the clicks/fallback pattern
        // analysis belongs to the root-cause report
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 1F detail — the slider-step model: the fastest host move (instant step)
// ---------------------------------------------------------------------------

TEST_CASE("TP-CONT-STEP: instant pitch steps (-12 -> 0 -> +12 -> 0 -> -12)") {
  artifactLine("");
  artifactLine("## TP-CONT-STEP");
  artifactLine("| mode | RTF | latMs | underruns | fallback | runs | maxRun | clicks | rprep | adopt |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|");
  for (int mode : {0, 1, 2, 3}) {
    DriveConfig c;
    c.tpMode = mode;
    c.material = Material::Saw;
    c.seconds = 14;
    // instant steps: 2 s holds, zero-length ramps
    std::vector<SweepSeg> s;
    double t = 0.0;
    s.push_back({t, t += 2.0, -12, -12});
    s.push_back({t, t += 2.0, 0, 0});
    s.push_back({t, t += 2.0, 12, 12});
    s.push_back({t, t += 2.0, 0, 0});
    s.push_back({t, t += 2.0, -12, -12});
    c.sweep = s;
    const DriveReport r = drive(c);
    std::printf("| %-6s | %5.3f | %7.1f | %8llu | %8llu | %6llu | %6llu | %6llu | %5llu | %5llu |\n",
                modeName(mode), r.rtf,
                r.status.latencyFrames / c.fs * 1000.0,
                (unsigned long long)r.status.deliveryUnderruns,
                (unsigned long long)r.fallbackFrames,
                (unsigned long long)r.fallbackRuns,
                (unsigned long long)r.fallbackMaxRun,
                (unsigned long long)r.clicks,
                (unsigned long long)r.status.reprepares,
                (unsigned long long)r.status.chainsAdopted);
    char line[320];
    std::snprintf(line, sizeof(line),
                  "| %s | %.3f | %.1f | %llu | %llu | %llu | %llu | %llu | %llu | %llu |",
                  modeName(mode), r.rtf,
                  r.status.latencyFrames / c.fs * 1000.0,
                  (unsigned long long)r.status.deliveryUnderruns,
                  (unsigned long long)r.fallbackFrames,
                  (unsigned long long)r.fallbackRuns,
                  (unsigned long long)r.fallbackMaxRun,
                  (unsigned long long)r.clicks,
                  (unsigned long long)r.status.reprepares,
                  (unsigned long long)r.status.chainsAdopted);
    artifactLine(line);
  }
}

// ---------------------------------------------------------------------------
// TP-CONT-PS-ENGINE: the direct-engine render (NO adapter) — the wet-silence
// class separation: if the OFFLINE engine render of the same material is
// silent too, the defect is DSP-content (the engine); if the offline render
// is continuous and loud, the silence was created by the streaming
// adaptation (the adapter/window interaction).
// ---------------------------------------------------------------------------

TEST_CASE("TP-CONT-PS-ENGINE: direct engine render of the silent-wet material") {
  using pitchlab::EngineConfiguration;
  using pitchlab::ParameterValue;
  using pitchlab::PitchCurveView;
  using pitchlab::ProcessContext;
  using pitchlab::ProcessReport;
  using pitchlab::AudioBlockView;
  using pitchlab::AudioBlockOut;
  using pitchlab::FrameCount;

  struct BlockOut {
    std::vector<double> ch0;
    double* ptr[1] = {nullptr};
    void allocate(int n) {
      ch0.assign(static_cast<std::size_t>(n), 0.0);
      ptr[0] = ch0.data();
    }
  };

  auto render = [&](Material m, double semitones) {
    auto engine = makeTimePitchEngine();
    EngineConfiguration cfg;
    cfg.seed = 12345;
    cfg.parameters.emplace_back("mode", ParameterValue{std::string("pitch_synced")});
    engine->configure(cfg);

    const double fs = 48000.0;
    const int64_t total = static_cast<int64_t>(fs * 6.0);
    std::vector<double> x = m == Material::Mixed
                                ? makeMixed(total, fs, 0.5, 220.0)
                                : makeInput(m, total, fs, 0.5, 220.0);

    std::vector<double> curve(static_cast<std::size_t>(total),
                              std::exp2(semitones / 12.0));
    PitchCurveView cv{};
    cv.ratio = curve.data();
    cv.frames = static_cast<pitchlab::FrameCount>(curve.size());
    cv.sampleRate = fs;

    ProcessContext ctx{};
    ctx.sampleRate = fs;
    ctx.channels = 1;
    ctx.maxBlockFrames = 512;
    ctx.totalInputFrames = static_cast<pitchlab::FrameCount>(total);
    ctx.curve = &cv;
    engine->prepare(ctx);
    const auto lat = engine->latency();

    const FrameCount feed = static_cast<FrameCount>(total) + lat.inputLatencyFrames;
    std::vector<double> feedBuf(static_cast<std::size_t>(feed), 0.0);
    std::copy(x.begin(), x.end(), feedBuf.begin());

    std::vector<double> out;
    FrameCount consumed = 0;
    while (consumed < feed) {
      const int take =
          static_cast<int>(std::min<FrameCount>(512, feed - consumed));
      const double* inCh[1] = {feedBuf.data() + consumed};
      AudioBlockView inView{inCh, 1, take};
      BlockOut blk;
      blk.allocate(512);
      AudioBlockOut outView{blk.ptr, 1, 512};
      const ProcessReport rep = engine->process(inView, take, outView, 512, cv, consumed);
      CHECK(rep.inputFramesConsumed == take);
      for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
        out.push_back(blk.ch0[static_cast<std::size_t>(i)]);
      }
      consumed += take;
    }
    {
      const int flushCap = static_cast<int>(512 + 2 * lat.inputLatencyFrames + 64 +
                                            lat.outputLatencyFrames + 64);
      BlockOut blk;
      blk.allocate(flushCap);
      AudioBlockOut outView{blk.ptr, 1, flushCap};
      const ProcessReport rep = engine->finish(outView, flushCap);
      for (FrameCount i = 0; i < rep.outputFramesProduced; ++i) {
        out.push_back(blk.ch0[static_cast<std::size_t>(i)]);
      }
    }
    return out;
  };

  // the 0.25 s RMS profile of the DIRECT render — the wet-silence question.
  // The render wraps the engine calls: the pitch_synced OFFLINE zero-pad-era
  // feed schedule (the §4.3.6 reference-test shape) can hit the engine's own
  // input-window sizing guard on some materials (measured: the harmonically
  // rich saw at +12 st throws "input window overflow (sizing bug)") — that
  // throw is itself a RECORDED DIAGNOSTIC (the offline feed-schedule vs the
  // pitch-synced window sizing mismatch, the RC-2 iteration's separate
  // item), never a probe crash.
  const double fs = 48000.0;
  for (Material m : {Material::Sine, Material::Saw}) {
    for (double st : {12.0, -12.0}) {
      std::vector<double> y;
      try {
        y = render(m, st);
      } catch (const std::exception& e) {
        std::printf("DIRECT %s st=%+.0f: render threw (recorded diagnostic): %s\n",
                    materialName(m), st, e.what());
        continue;
      }
      const int64_t sash = static_cast<int64_t>(0.25 * fs);
      std::printf("DIRECT %s st=%+.0f: outFrames=%lld, 0.25s RMS profile:\n",
                  materialName(m), st, (long long)y.size());
      std::string prof;
      for (int64_t s0 = 0; s0 + sash <= (int64_t)y.size(); s0 += sash) {
        double acc = 0.0;
        for (int64_t i = 0; i < sash; ++i) {
          acc += y[static_cast<std::size_t>(s0 + i)] * y[static_cast<std::size_t>(s0 + i)];
        }
        const double rms = std::sqrt(acc / static_cast<double>(sash));
        prof += std::to_string(static_cast<int>(std::round(rms * 100.0)));
        prof += " ";
        (void)fs;
      }
      std::printf("  rms(x100): %s\n", prof.c_str());
    }
  }
}
