// Pitch Lab VST3 product layer — T-RD* (Task 29 + Task 30): realtime fault
// DIAGNOSTICS + the 96 kHz investigations + the churn-correction evidence.
//
// Part 1 — the categorized counters' invariants:
//   * the aggregate `faults` == engineProcessFaults + engineExceptions +
//     deliveryUnderruns + dryHistoryMisses (its historical composition)
//   * clean drives (every engine) leave every category at zero
//
// Part 2 — the GRANULAR investigation (Task 30 Task G: the FULL matrix —
// 44.1/48/96 kHz x blocks {128,256,512,1024} x pitch {0, +12, −12} plus
// the −12 st + LFO boundary rows and the +12 st + LFO row): measures RTF,
// jobs, prepared jobs, re-prepares (chain builds), adoptions, clamps,
// faults, underruns, stalls, latency — and ASSERTS the Task-30 churn
// policy: the −12 st + LFO drives that previously produced the measured
// futile-churn loop (759 clamps / 329 re-prepares / a seam every ~6 ms)
// now run with ZERO clamps, ONE chain build and a fault-free wet grid
// (the envelope covers the complete legal PITCH+LFO surface at the
// engine's declared range; a clamp is never a lifecycle trigger).
//
// Part 3 — the PV-PHASELOCKED investigation (Task H: RETAINED baseline —
// 48 vs 96 kHz x blocks: fault-free, RTF far inside the budget; the engine
// is NOT a current runtime failure and must remain untouched).
//
// Part 4 — the repeated-LFO-cycle stability (Task 30 Task J item 16): a
// 10 s drive at −12 st + LFO depth 2 (the widest legal excursion, ~50 LFO
// cycles at 5 Hz) must NOT accumulate re-prepares with the cycle count.
//
// The measurement table prints to stdout; set PITCHLAB_DIAG_ARTIFACT=<path>
// to additionally write it as a markdown artifact (the local evidence run;
// CI runs the assertions only).

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
  // Task 30 — the HOST-CADENCE PACING (measured, then corrected): a real
  // VST3 host delivers blocks at the block-period cadence with idle time
  // between process() calls; the previous back-to-back drive loop starved
  // the PREPARATION thread (the audio thread monopolises the core; the
  // prep thread's 1-ms-poll wakeups were delayed by hundreds of ms) —
  // measured as run-dependent one-window dry fallbacks at the extreme
  // grain length (grain 0.5 at 44.1/48 kHz: 377..9600 faults, varying per
  // run; GONE with pacing — the mechanism verified by the A/B experiment).
  // The 250 µs inter-block sleep is a fraction of the real block period
  // (2.67..21.7 ms) — the minimum realistic scheduling gap. The CPU/RTF
  // measurements are UNAFFECTED (timed around process() only).
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
    std::this_thread::sleep_for(std::chrono::microseconds(250));
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
  std::printf("| %-16s | %-26s | %5d | %6llu | %10.1f | %7.3f | %6.3f | %6llu | %6llu | %6llu | "
              "%6llu | %6llu | %6llu | %6llu | %6llu | %6llu | %8.3f |\n",
              engine, drive, st.status.liveJobs,
              (unsigned long long)st.status.jobsPrepared,
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

TEST_CASE("granular matrix: 44.1/48/96 kHz x blocks x pitch + the LFO boundary rows") {
  initEngineRegistryOnce();
  if (const char* path = std::getenv("PITCHLAB_DIAG_ARTIFACT")) g_artifact = path;
  std::printf("\nGRANULAR (2 s drives, 220 Hz sine) — the Task-30 measured matrix\n");
  std::printf("| %-16s | %-26s | %5s | %6s | %10s | %7s | %6s | %6s | %6s | %6s | "
              "%6s | %6s | %6s | %6s | %6s | %6s | %8s |\n",
              "engine", "drive", "jobs", "prep'd", "lat ms", "RTF", "cpu/c", "reprep", "clamp",
              "fault", "underr", "dryms", "stall", "adopt", "prepf", "chadpt", "out dB");
  artifactLine("# Task 30 — Granular measured matrix (vst_realtime_diagnostics_test)");
  artifactLine("");
  artifactLine("| drive | block | jobs | prepared | lat ms | RTF | cpu ms/call | re-prepares | clamps | "
               "faults | underruns | dry-miss | stalls | adopt-fail | prep-fail | out dB |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");

  struct Drive {
    const char* name;
    double pitchSt;
    double grainSec;
    double lfoDepth;
  };
  // Task G (measured, not guessed): every drive at every rate x block.
  const Drive drives[] = {
      {"pitch 0, grain 0.10", 0.0, 0.10, 0.0},
      {"pitch +12, grain 0.10", 12.0, 0.10, 0.0},
      {"pitch -12, grain 0.10", -12.0, 0.10, 0.0},
      {"pitch -12, grain 0.50", -12.0, 0.50, 0.0},
      {"pitch -12 + lfo 2.0", -12.0, 0.10, 2.0},   // the Task-30 headline case
      {"pitch +12 + lfo 2.0", 12.0, 0.10, 2.0},    // the upper boundary (J item 4)
  };
  const double rates[] = {44100.0, 48000.0, 96000.0};
  const int32_t blocks[] = {128, 256, 512, 1024};
  for (double fs : rates) {
    for (const Drive& d : drives) {
      for (int32_t block : blocks) {
        ParamSnapshot snap;
        snap.engineIndex = 4;  // native.granular (registry order)
        snap.pitchSt = d.pitchSt;
        snap.grGrainSec = d.grainSec;
        snap.lfoDepthSt = d.lfoDepth;
        const DriveStats st = driveAt(fs, block, snap, 2);
        char name[64];
        std::snprintf(name, sizeof(name), "%.1f kHz %s", fs / 1000.0, d.name);
        reportRow("native.granular", name, st);
        char line[512];
        std::snprintf(line, sizeof(line), "| %s | %d | %d | %llu | %.1f | %.3f | %.3f | %llu | %llu | "
                      "%llu | %llu | %llu | %llu | %llu | %llu | %.1f |",
                      d.name, block, st.status.liveJobs,
                      (unsigned long long)st.status.jobsPrepared,
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
        // The INVARIANT holds everywhere (the decomposition is complete):
        CHECK(st.status.faults == st.status.engineProcessFaults +
                                      st.status.engineExceptions +
                                      st.status.deliveryUnderruns +
                                      st.status.dryHistoryMisses);
        // engine defects are NOT the cause (no process faults / exceptions):
        CHECK(st.status.engineProcessFaults == 0);
        CHECK(st.status.engineExceptions == 0);
        // the drive produces audio (never a silent failure) and the chain
        // is live; the CPU cost is the MEASURED EVIDENCE (the windowed-
        // splice cost at 96 kHz exceeds realtime on the reference machine —
        // RTF ~1.1-1.8 at grain 0.1, ~1.8-3.9 at grain 0.5; the bound below
        // is a RUNAWAY sanity bound, not a realtime claim — 6.0 leaves
        // headroom for shared-runner contention (a parallel local run
        // measured 4.3 under 4-way load; the canonical lane is serial).
        CHECK(st.outRmsDb > -60.0);
        CHECK(st.rtf > 0.0);
        CHECK(st.rtf < 6.0);
        CHECK(st.status.chainReady);
        // ---- the TASK-30 CHURN POLICY (asserted at EVERY rate/block) ----
        // The LFO boundary rows are the pre-fix futile-churn scenario
        // (measured: 759 clamps / 329 re-prepares / 328 adoptions per 2 s
        // at -12 st). The corrected envelope covers the complete legal
        // PITCH+LFO surface at the engine's declared range, and a clamp is
        // never a lifecycle trigger: ZERO clamps, ONE chain build, ONE
        // adoption, no faults, no underruns, no stalls.
        if (d.lfoDepth > 0.0) {
          CHECK(st.status.clampEvents == 0);
          CHECK(st.status.reprepares <= 2);
          CHECK(st.status.chainsAdopted <= 2);
          CHECK(st.status.faults == 0);
          CHECK(st.status.deliveryUnderruns == 0);
          CHECK(st.status.dryHistoryMisses == 0);
          CHECK(st.status.jobStalls == 0);
          CHECK(st.status.chainAdoptionFailures == 0);
          CHECK(st.status.preparationFailures == 0);
        } else {
          // non-LFO rows: the static behaviour (one chain, no churn) —
          // byte-identical envelope policy at depth 0
          CHECK(st.status.clampEvents == 0);
          CHECK(st.status.reprepares <= 2);
        }
      }
    }
  }

  // ---- the TASK-29 measured boundary case, re-measured under the fix ----
  // (-12 st + LFO 0.5 st at 96 kHz/block 128 was the exact churn scenario:
  // 759 clamps / 329 re-prepares / 328 adoptions per 2 s). Under the Task-30
  // policy the envelope covers the excursion: zero clamps, one chain, the
  // wet grid fault-free — the churn is GONE, not hidden (the counters prove
  // the absence, and the drive stays audible).
  {
    ParamSnapshot snap;
    snap.engineIndex = 4;
    snap.pitchSt = -12.0;
    snap.grGrainSec = 0.10;
    snap.lfoDepthSt = 0.5;
    const DriveStats st = driveAt(96000.0, 128, snap, 2);
    reportRow("native.granular", "BOUNDARY: -12 + lfo 0.5", st);
    CHECK(st.status.clampEvents == 0);   // the churn is REMOVED (was 759)
    CHECK(st.status.reprepares <= 2);    // one initial chain (was 329)
    CHECK(st.status.chainsAdopted <= 2); // one adoption (was 328)
    CHECK(st.status.faults == 0);        // the wet grid stays healthy
    CHECK(st.status.deliveryUnderruns == 0);
    CHECK(st.status.dryHistoryMisses == 0);
    CHECK(st.status.jobStalls == 0);
    CHECK(st.status.chainAdoptionFailures == 0);
    CHECK(st.status.preparationFailures == 0);
    CHECK(st.outRmsDb > -60.0);          // and the drive is audible
    artifactLine("");
    artifactLine("BOUNDARY (block 128, -12 st + LFO 0.5 st): clamps " +
                 std::to_string(st.status.clampEvents) + ", re-prepares " +
                 std::to_string(st.status.reprepares) + ", adoptions " +
                 std::to_string(st.status.chainsAdopted) +
                 ", faults " + std::to_string(st.status.faults) +
                 " — the Task-29 futile-churn loop (759/329/328 per 2 s) is "
                 "REMOVED by the depth-aware envelope + the clamp/re-prepare "
                 "decoupling; the legal PITCH+LFO surface is covered at the "
                 "engine's declared range and no clamp requests a rebuild.");
  }
  std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Part 4 — the repeated-LFO-cycle stability (Task J item 16)
// ---------------------------------------------------------------------------

TEST_CASE("repeated LFO cycles do not accumulate re-prepares (10 s, -12 st, depth 2)") {
  initEngineRegistryOnce();
  // ~50 LFO cycles at the default 5 Hz over 10 s, at the WIDEST legal
  // excursion (-12 st + 2 st depth = the -14 st surface edge, where the
  // pre-Task-30 design rebuilt on every dip). The re-prepare count must be
  // BOUNDED (the initial chain only): no growth with the cycle count — the
  // structural-invariance proof that the churn cannot return over time.
  ParamSnapshot snap;
  snap.engineIndex = 4;  // native.granular
  snap.pitchSt = -12.0;
  snap.grGrainSec = 0.10;
  snap.lfoDepthSt = 2.0;
  const DriveStats st = driveAt(96000.0, 128, snap, 10);
  std::printf("\nREPEATED-CYCLE STABILITY (10 s, -12 st + LFO 2 st, 96 kHz, block 128): "
              "re-prepares %llu, clamps %llu, faults %llu\n",
              (unsigned long long)st.status.reprepares,
              (unsigned long long)st.status.clampEvents,
              (unsigned long long)st.status.faults);
  artifactLine("");
  artifactLine("REPEATED-CYCLE STABILITY (10 s, -12 st + LFO 2 st, 96 kHz, block 128): "
               "re-prepares " + std::to_string(st.status.reprepares) +
               ", clamps " + std::to_string(st.status.clampEvents) +
               ", faults " + std::to_string(st.status.faults) +
               " — bounded (one chain for the whole drive; ~50 LFO cycles).");
  CHECK(st.status.reprepares <= 2);
  CHECK(st.status.clampEvents == 0);
  CHECK(st.status.faults == 0);
  CHECK(st.status.chainReady);
  CHECK(st.outRmsDb > -60.0);
  std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Part 3 — the PV-PHASELOCKED investigation (48 vs 96 kHz x block matrix)
// ---------------------------------------------------------------------------

TEST_CASE("pv-phaselocked: 48 vs 96 kHz x block matrix (Task H — the RETAINED baseline)") {
  initEngineRegistryOnce();
  if (g_artifact.empty()) {
    if (const char* path = std::getenv("PITCHLAB_DIAG_ARTIFACT")) g_artifact = path;
  }
  std::printf("\nPV-PHASELOCKED (2 s drives, 220 Hz sine, +12 st — Task H: retained "
              "baseline, engine untouched)\n");
  std::printf("| %-16s | %-26s | %5s | %6s | %10s | %7s | %6s | %6s | %6s | %6s | %6s | "
              "%6s | %6s | %6s | %6s | %6s | %8s |\n",
              "engine", "drive", "jobs", "prep'd", "lat ms", "RTF", "cpu/c", "reprep", "clamp",
              "fault", "underr", "dryms", "stall", "adopt", "prepf", "chadpt", "out dB");
  artifactLine("");
  artifactLine("# Task 29 — PV-phaselocked investigation (48 vs 96 kHz x blocks)");
  artifactLine("");
  artifactLine("| drive | block | jobs | prepared | lat ms | RTF | cpu ms/call | re-prepares | clamps | "
               "faults | underruns | dry-miss | stalls | adopt-fail | prep-fail | out dB |");
  artifactLine("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");

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
      std::snprintf(line, sizeof(line), "| %s | %d | %d | %llu | %.1f | %.3f | %.3f | %llu | %llu | "
                    "%llu | %llu | %llu | %llu | %llu | %llu | %.1f |",
                    name, block, st.status.liveJobs,
                    (unsigned long long)st.status.jobsPrepared,
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


// ---------------------------------------------------------------------------
// Task 32 — the realtime-capability MEASUREMENT fields on the real drives
// ---------------------------------------------------------------------------

TEST_CASE("Task 32: the measurement fields accumulate and classify (pv-phaselocked, both rates)") {
  initEngineRegistryOnce();
  // The adapter's own measurement (engineCpuNanos / rtFrames since the
  // current chain's adoption) must accumulate over a clean single-chain
  // drive and classify through the pure classifier — the same figures the
  // editor's status line renders. The LEVEL is machine-dependent (timing);
  // the BASIS and the window mechanics are not.
  for (double fs : {48000.0, 96000.0}) {
    for (int32_t block : {128, 1024}) {
      ParamSnapshot snap;
      snap.engineIndex = 3;  // native.pv.phaselocked
      snap.pitchSt = 12.0;
      CAPTURE(fs);
      CAPTURE(block);
      const DriveStats st = driveAt(fs, block, snap, 1);
      REQUIRE(st.status.faults == 0);
      REQUIRE(st.status.chainReady);
      // the window covers the whole single-chain drive
      CHECK(st.status.rtFrames == st.frames);
      CHECK(st.status.engineCpuNanos > 0);
      const RtClassification rt = classifyRealtimeStatus(st.status);
      CHECK(rt.basis == RtBasis::Measured);
      CHECK(rt.windowFrames == st.status.rtFrames);
      CHECK(rt.measuredRtf > 0.0);
      // pv-phaselocked measures far inside capacity at both rates (the
      // task-29/30 evidence: RTF 0.012-0.063) — a healthy classification
      CHECK(rt.level == RtLevel::Ok);
      // the adapter-measured RTF is the same quantity the harness measured
      // host-side (steady-clock engine cost vs timeline): same order
      const double harnessRtf = st.rtf;
      CHECK(rt.measuredRtf < 10.0 * harnessRtf + 0.01);
      CHECK(harnessRtf < 10.0 * rt.measuredRtf + 0.01);
    }
  }
}
