// Pitch Lab VST3 product layer — T-RD* (Task 29): realtime fault
// DIAGNOSTICS + the 96 kHz investigations.
//
// Part 1 — the categorized counters' invariants:
//   * the aggregate `faults` == engineProcessFaults + engineExceptions +
//     deliveryUnderruns + dryHistoryMisses (its historical composition)
//   * clean drives (every engine) leave every category at zero
//
// Part 2 — the GRANULAR 96 kHz investigation (the screenshot finding:
// ~664 ms latency, very high re-prepares, very high clamps, 1021 faults):
// drives granular at 96 kHz across block sizes, pitch, grain length and
// LFO, and MEASURES where the faults land (underrun vs dry-miss vs stall
// vs clamp/re-prepare churn) plus the host-side CPU cost per process call.
//
// Part 3 — the PV-PHASELOCKED investigation: 48 vs 96 kHz x blocks
// {128, 256, 512, 1024}: CPU budget (RTF), job/adoption cadence, faults,
// delivery underruns, latency, output continuity — establishing whether a
// stutter is CPU-budget related or adapter-lifecycle related.
//
// The measurement table prints to stdout; set PITCHLAB_DIAG_ARTIFACT=<path>
// to additionally write it as a markdown artifact (the local evidence run;
// CI runs the invariants only).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "vst/realtime_adapter.h"

using namespace pitchlab::vst;

namespace {

struct DriveStats {
  StatusSnapshot status;
  double cpuMsPerCall = 0.0;   // mean host-side CPU per process() call
  double rtf = 0.0;            // total CPU time / audio time
  double outRmsDb = -999.0;    // settled output level (continuity)
  int64_t frames = 0;
};

/// Drive the adapter directly (the real adapter + the real engines) at an
/// arbitrary sample rate / block size; measures per-call CPU.
DriveStats driveAt(double fs, int32_t block, const ParamSnapshot& snap,
                  int64_t seconds, double inputFreq = 220.0, double amp = 0.5) {
  RealtimeAdapter adapter;
  adapter.activate(fs, 2, block);
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();

  const int64_t total = static_cast<int64_t>(fs * static_cast<double>(seconds));
  std::vector<double> sig(static_cast<std::size_t>(total));
  for (int64_t i = 0; i < total; ++i) {
    sig[static_cast<std::size_t>(i)] =
        amp * std::sin(2.0 * 3.14159265358979323846 * inputFreq *
                       static_cast<double>(i) / fs);
  }
  std::vector<double> out[2] = {std::vector<double>(static_cast<std::size_t>(total), 0.0),
                                std::vector<double>(static_cast<std::size_t>(total), 0.0)};
  int64_t cpuNs = 0;
  int64_t calls = 0;
  int64_t pos = 0;
  while (pos < total) {
    const int32_t take = static_cast<int32_t>(std::min<int64_t>(block, total - pos));
    const double* in[2] = {sig.data() + pos, sig.data() + pos};
    double* o[2] = {out[0].data() + pos, out[1].data() + pos};
    BlockAutomation none;
    const auto t0 = std::chrono::steady_clock::now();
    adapter.process(in, o, take, none);
    const auto t1 = std::chrono::steady_clock::now();
    cpuNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    ++calls;
    pos += take;
  }
  DriveStats st;
  st.status = adapter.status();
  st.frames = total;
  st.cpuMsPerCall = calls > 0 ? static_cast<double>(cpuNs) / 1e6 / static_cast<double>(calls) : 0.0;
  const double audioNs = static_cast<double>(total) / fs * 1e9;
  st.rtf = audioNs > 0.0 ? static_cast<double>(cpuNs) / audioNs : 0.0;
  // settled output RMS (skip the latency + seam region)
  const int64_t settle = static_cast<int64_t>(fs * 0.6);
  double acc = 0.0;
  int64_t n = 0;
  for (int64_t i = settle; i < total - static_cast<int64_t>(fs * 0.05); ++i) {
    acc += out[0][static_cast<std::size_t>(i)] * out[0][static_cast<std::size_t>(i)];
    ++n;
  }
  st.outRmsDb = n > 0 ? 20.0 * std::log10(std::sqrt(acc / static_cast<double>(n)) + 1e-12) : -999.0;
  adapter.deactivate();
  return st;
}

void reportRow(const char* engine, const char* drive, const DriveStats& st) {
  std::printf("| %-16s | %-26s | %5d | %10.1f | %7.3f | %6.3f | %6llu | %6llu | %6llu | "
              "%6llu | %6llu | %6llu | %6llu | %6llu | %6llu | %8.3f |\n",
              engine, drive, st.status.liveJobs,
              st.status.latencyFrames / st.status.sampleRate * 1000.0, st.rtf,
              st.cpuMsPerCall, (unsigned long long)st.status.reprepares,
              (unsigned long long)st.status.clampEvents,
              (unsigned long long)st.status.faults,
              (unsigned long long)st.status.deliveryUnderruns,
              (unsigned long long)st.status.dryHistoryMisses,
              (unsigned long long)st.status.jobStalls,
              (unsigned long long)st.status.chainAdoptionFailures,
              (unsigned long long)st.status.preparationFailures,
              (unsigned long long)st.status.chainsAdopted, st.outRmsDb);
}

std::string g_artifact;  // optional markdown artifact (PITCHLAB_DIAG_ARTIFACT)

void artifactLine(const std::string& line) {
  if (!g_artifact.empty()) {
    if (FILE* f = std::fopen(g_artifact.c_str(), "a")) {
      std::fprintf(f, "%s\n", line.c_str());
      std::fclose(f);
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Part 1 — the invariants (asserted; run everywhere including CI)
// ---------------------------------------------------------------------------

TEST_CASE("counter invariant: faults == the historical category sum (every engine)") {
  initEngineRegistryOnce();
  for (int engine = 0; engine < engineCount(); ++engine) {
    ParamSnapshot snap;
    snap.engineIndex = engine;
    snap.pitchSt = 5.0;
    CAPTURE(engineIdForIndex(engine));
    const DriveStats st = driveAt(48000.0, 256, snap, 1);
    CHECK(st.status.faults == st.status.engineProcessFaults +
                                  st.status.engineExceptions +
                                  st.status.deliveryUnderruns +
                                  st.status.dryHistoryMisses);
  }
}

TEST_CASE("clean drives: every engine, every category zero (48 kHz)") {
  initEngineRegistryOnce();
  const int32_t blocks[] = {128, 256, 512, 1024};
  for (int engine = 0; engine < engineCount(); ++engine) {
    for (int32_t block : blocks) {
      ParamSnapshot snap;
      snap.engineIndex = engine;
      snap.pitchSt = 5.0;
      CAPTURE(engineIdForIndex(engine));
      CAPTURE(block);
      const DriveStats st = driveAt(48000.0, block, snap, 1);
      CHECK(st.status.faults == 0);
      CHECK(st.status.engineProcessFaults == 0);
      CHECK(st.status.engineExceptions == 0);
      CHECK(st.status.deliveryUnderruns == 0);
      CHECK(st.status.dryHistoryMisses == 0);
      CHECK(st.status.jobStalls == 0);
      CHECK(st.status.chainAdoptionFailures == 0);
      CHECK(st.status.preparationFailures == 0);
      CHECK(st.status.chainReady);
      CHECK(st.outRmsDb > -50.0);  // audible wet (continuity)
      CHECK(st.rtf < 1.0);         // comfortably inside the realtime budget
    }
  }
}

// ---------------------------------------------------------------------------
// Part 2 — the GRANULAR 96 kHz investigation (MEASURED, never assumed)
// ---------------------------------------------------------------------------

TEST_CASE("granular 96 kHz: the stutter is decomposed into measured categories") {
  initEngineRegistryOnce();
  if (const char* path = std::getenv("PITCHLAB_DIAG_ARTIFACT")) g_artifact = path;
  std::printf("\nGRANULAR @ 96 kHz (2 s drives, 220 Hz sine) — measured decomposition\n");
  std::printf("| %-16s | %-26s | %5s | %10s | %7s | %6s | %6s | %6s | %6s | %6s | %6s | "
              "%6s | %6s | %6s | %6s | %8s |\n",
              "engine", "drive", "jobs", "lat ms", "RTF", "cpu/c", "reprep", "clamp",
              "fault", "underr", "dryms", "stall", "adopt", "prepf", "chadpt", "out dB");
  artifactLine("# Task 29 — Granular 96 kHz investigation (vst_realtime_diagnostics_test)");
  artifactLine("");
  artifactLine("| drive | block | jobs | lat ms | RTF | cpu ms/call | re-prepares | clamps | "
               "faults | underruns | dry-miss | stalls | adopt-fail | prep-fail | out dB |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");

  struct Drive {
    const char* name;
    double pitchSt;
    double grainSec;
    double lfoDepth;
  };
  const Drive drives[] = {
      {"pitch 0, grain 0.10", 0.0, 0.10, 0.0},
      {"pitch +12, grain 0.10", 12.0, 0.10, 0.0},
      {"pitch -12, grain 0.10", -12.0, 0.10, 0.0},
      {"pitch -12, grain 0.50", -12.0, 0.50, 0.0},
      {"pitch -12, grain 0.10, lfo", -12.0, 0.10, 0.5},
  };
  const int32_t blocks[] = {128, 256, 512, 1024};
  for (const Drive& d : drives) {
    for (int32_t block : blocks) {
      ParamSnapshot snap;
      snap.engineIndex = 4;  // native.granular (registry order)
      snap.pitchSt = d.pitchSt;
      snap.grGrainSec = d.grainSec;
      snap.lfoDepthSt = d.lfoDepth;
      const DriveStats st = driveAt(96000.0, block, snap, 2);
      reportRow("native.granular", d.name, st);
      char line[512];
      std::snprintf(line, sizeof(line), "| %s | %d | %d | %.1f | %.3f | %.3f | %llu | %llu | "
                    "%llu | %llu | %llu | %llu | %llu | %llu | %.1f |",
                    d.name, block, st.status.liveJobs,
                    st.status.latencyFrames / st.status.sampleRate * 1000.0, st.rtf,
                    st.cpuMsPerCall, (unsigned long long)st.status.reprepares,
                    (unsigned long long)st.status.clampEvents,
                    (unsigned long long)st.status.faults,
                    (unsigned long long)st.status.deliveryUnderruns,
                    (unsigned long long)st.status.dryHistoryMisses,
                    (unsigned long long)st.status.jobStalls,
                    (unsigned long long)st.status.chainAdoptionFailures,
                    (unsigned long long)st.status.preparationFailures, st.outRmsDb);
      artifactLine(line);
      // The INVARIANT holds under the stutter too (the decomposition is
      // complete — the counters explain the aggregate exactly):
      CHECK(st.status.faults == st.status.engineProcessFaults +
                                    st.status.engineExceptions +
                                    st.status.deliveryUnderruns +
                                    st.status.dryHistoryMisses);
      // engine defects are NOT the cause (no process faults / exceptions):
      CHECK(st.status.engineProcessFaults == 0);
      CHECK(st.status.engineExceptions == 0);
      // the drive produces audio (never a silent failure), the latency is
      // the declared worst-case splice geometry, and the CPU cost is the
      // MEASURED EVIDENCE: the windowed-splice cost at 96 kHz EXCEEDS the
      // realtime budget on the reference machine (RTF ~1.1-1.7 measured —
      // the stutter's primary cause; the bound below is a sanity bound, not
      // a realtime claim).
      CHECK(st.outRmsDb > -60.0);
      CHECK(st.rtf > 0.0);
      CHECK(st.rtf < 4.0);
      CHECK(st.status.chainReady);
    }
  }

  // ---- the LFO-at-boundary CHURN measurement (the screenshot's "very high
  // re-prepares / very high clamps"): at -12 st the ±1 st envelope is
  // TRUNCATED by the parameter range (envMin floors at ratio 0.5), so every
  // LFO dip below -12 st clamps and requests a rebuild whose envelope STILL
  // cannot cover the excursion — a measured futile-churn loop (hundreds of
  // clamps + rebuilds + mid-stream adoptions per 2 s; audible as seam
  // stutter) while the wet grid itself stays fault-free.
  {
    ParamSnapshot snap;
    snap.engineIndex = 4;
    snap.pitchSt = -12.0;
    snap.grGrainSec = 0.10;
    snap.lfoDepthSt = 0.5;
    const DriveStats st = driveAt(96000.0, 128, snap, 2);
    reportRow("native.granular", "BOUNDARY: -12 + lfo 0.5", st);
    CHECK(st.status.clampEvents > 100);  // the churn is real and measured
    CHECK(st.status.faults == 0);        // ...but the wet grid stays healthy
    CHECK(st.status.deliveryUnderruns == 0);
    CHECK(st.status.jobStalls == 0);
    artifactLine("");
    artifactLine("BOUNDARY CHURN (block 128, -12 st + LFO 0.5 st): clamps " +
                 std::to_string(st.status.clampEvents) + ", re-prepares " +
                 std::to_string(st.status.reprepares) + ", adoptions " +
                 std::to_string(st.status.chainsAdopted) +
                 ", faults " + std::to_string(st.status.faults) +
                 " — the envelope is truncated by the parameter range at the "
                 "±12 st boundary; every LFO dip clamps and requests a rebuild "
                 "that still cannot cover the excursion (futile churn; seam "
                 "every ~6 ms = the audible stutter).");
  }
  std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Part 3 — the PV-PHASELOCKED investigation (48 vs 96 kHz x block matrix)
// ---------------------------------------------------------------------------

TEST_CASE("pv-phaselocked: 48 vs 96 kHz x block matrix (CPU vs lifecycle)") {
  initEngineRegistryOnce();
  if (g_artifact.empty()) {
    if (const char* path = std::getenv("PITCHLAB_DIAG_ARTIFACT")) g_artifact = path;
  }
  std::printf("\nPV-PHASELOCKED (2 s drives, 220 Hz sine, +12 st)\n");
  std::printf("| %-16s | %-26s | %5s | %10s | %7s | %6s | %6s | %6s | %6s | %6s | %6s | "
              "%6s | %6s | %6s | %6s | %8s |\n",
              "engine", "drive", "jobs", "lat ms", "RTF", "cpu/c", "reprep", "clamp",
              "fault", "underr", "dryms", "stall", "adopt", "prepf", "chadpt", "out dB");
  artifactLine("");
  artifactLine("# Task 29 — PV-phaselocked investigation (48 vs 96 kHz x blocks)");
  artifactLine("");
  artifactLine("| drive | block | jobs | lat ms | RTF | cpu ms/call | re-prepares | clamps | "
               "faults | underruns | dry-miss | stalls | adopt-fail | prep-fail | out dB |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");

  const double rates[] = {48000.0, 96000.0};
  const int32_t blocks[] = {128, 256, 512, 1024};
  for (double fs : rates) {
    for (int32_t block : blocks) {
      ParamSnapshot snap;
      snap.engineIndex = 3;  // native.pv.phaselocked (registry order)
      snap.pitchSt = 12.0;
      const DriveStats st = driveAt(fs, block, snap, 2);
      char name[64];
      std::snprintf(name, sizeof(name), "%.0f kHz, pitch +12", fs / 1000.0);
      reportRow("native.pv.phaselocked", name, st);
      char line[512];
      std::snprintf(line, sizeof(line), "| %s | %d | %d | %.1f | %.3f | %.3f | %llu | %llu | "
                    "%llu | %llu | %llu | %llu | %llu | %llu | %.1f |",
                    name, block, st.status.liveJobs,
                    st.status.latencyFrames / st.status.sampleRate * 1000.0, st.rtf,
                    st.cpuMsPerCall, (unsigned long long)st.status.reprepares,
                    (unsigned long long)st.status.clampEvents,
                    (unsigned long long)st.status.faults,
                    (unsigned long long)st.status.deliveryUnderruns,
                    (unsigned long long)st.status.dryHistoryMisses,
                    (unsigned long long)st.status.jobStalls,
                    (unsigned long long)st.status.chainAdoptionFailures,
                    (unsigned long long)st.status.preparationFailures, st.outRmsDb);
      artifactLine(line);
      // fault-freedom at BOTH rates and every block size: the engine and
      // the adapter lifecycle are healthy — a measured negative result for
      // the "adapter lifecycle defect" hypothesis
      CHECK(st.status.faults == 0);
      CHECK(st.status.deliveryUnderruns == 0);
      CHECK(st.status.jobStalls == 0);
      CHECK(st.status.chainAdoptionFailures == 0);
      CHECK(st.status.preparationFailures == 0);
      CHECK(st.status.chainReady);
      CHECK(st.outRmsDb > -50.0);   // audible wet (output continuity)
      CHECK(st.rtf < 1.0);          // the CPU budget holds at 96 kHz
    }
  }
  std::fflush(stdout);
}
