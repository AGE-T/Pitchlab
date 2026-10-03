// Pitch Lab VST3 product layer — T-S*: the TASK-31 retiring-chain seam
// coverage suite.
//
// THE DEFECT THIS SUITE PINS (root-caused by measurement, Task 31): a
// same-engine chain replacement whose latency GROWS (native.granular grain
// 0.1 s -> 0.5 s: at 48 kHz the splice geometry's ENVELOPE-SCOPED latency
// (the Task-33 continuation Phase 5: the worst read-rate = the chain's own
// envMax; the whole-mode pitch-worst term is gone) is Λ 4896 -> 24096 —
// ΔΛ = 19200 = the grain growth exactly (the envelope term cancels)
// position BACKWARD by ΔΛ = 19200 frames at adoption — the frozen Λ_eff
// re-coverage policy (spec §4.1 item 5). The retiring chain's jobs covering
// the re-read region are dead and RECYCLED (the prep thread re-stamps a
// dead job's slot for job k+kJobSlots, wiping its lane), so readWet failed
// on the index check and the emission fell back to DRY for the whole
// uncovered span — the measured 13558/14406-frame underrun bursts (region
// exactly [t_a − Λnew, s_oldest_alive + seamX)).
//
// THE FIX (adapter-level, the smallest justified change): per-chain RETAINED
// WET HISTORY (Chain::history) — at each job death the produced wet is
// copied (raw memcpy + the readWet-identical seam blend) before recycling
// can wipe the lane; the retiring emission read falls back to it (counted in
// seamRecoveries, never silent, NOT a fault). This implements the retention
// spec §4.1 item 5 already declares: "the job lanes ... retain (and are
// sized for) the parameter-range worst-case history so a mid-stream Λ_eff
// growth never outruns the retained wet/dry".
//
// THE SUITE'S HEADLINE ASSERTIONS:
//   * the extreme jump runs with ZERO faults/underruns (the burst REMOVED,
//     not hidden — the seamRecoveries counter proves the re-coverage is
//     SERVED from the retained history);
//   * the re-coverage window replays BIT-IDENTICAL the previously-emitted
//     wet (out[p] == out[p − ΔΛ] for the full window: the retained content
//     is exactly what readWet returned when the lanes were live);
//   * the audio is continuous across the seam (RMS/peak windows: no gap,
//     no dry burst) — the transition is an intentional, documented
//     configuration repeat (the Λ_eff policy's inherent cost), NOT a fault;
//   * reverse-direction, block-schedule, sample-rate, pacing and LFO
//     matrices hold the same invariant;
//   * the Task-30 clamp/re-prepare policy is untouched (0 clamps, the
//     legitimate 2 re-prepares at a genuine configuration change).
//
// Matrix evidence (markdown artifact) via PITCHLAB_SEAM_ARTIFACT=<path>.

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

using namespace pitchlab::vst;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<double> makeSine(int64_t frames, double fs, double freq, double amp = 0.5) {
  std::vector<double> out(static_cast<std::size_t>(frames));
  for (int64_t i = 0; i < frames; ++i) {
    out[static_cast<std::size_t>(i)] =
        amp * std::sin(2.0 * kPi * freq * static_cast<double>(i) / fs);
  }
  return out;
}

struct SeamDriveResult {
  std::vector<double> out[2];
  StatusSnapshot status;
  int64_t tAdopt = -1;      // the input position of the first block past adoption
  int64_t latOld = 0;       // the pre-switch reported latency
  int64_t latNew = 0;       // the post-adoption reported latency
  int64_t dLambda = 0;      // latNew - latOld (the re-coverage span)
  int64_t replayMatch = 0;  // frames of the re-coverage window identical to the
                            // previously-emitted region
  int64_t replayTotal = 0;  // the re-coverage window size (min(dLambda, usable))
  double rmsPre = 0.0, rmsReplay = 0.0, rmsPost = 0.0;
  double peakPre = 0.0, peakReplay = 0.0, peakPost = 0.0;
};

/// Drive a mid-stream engine-configuration change through the REAL adapter
/// and measure the seam. `mutate` is applied to the snapshot at `switchSec`
/// and re-published (the genuine chain-signature change path).
/// `paced`: 250 µs host-cadence inter-block sleep (the realistic host — the
/// diagnostics-suite pattern); false = the back-to-back stress mode.
SeamDriveResult driveSeam(double fs, const std::vector<int32_t>& blockSchedule,
                          ParamSnapshot snap, void (*mutate)(ParamSnapshot&),
                          double switchSec, double tailSec, bool paced) {
  RealtimeAdapter adapter;
  int32_t maxBlock = 0;
  for (int32_t b : blockSchedule) maxBlock = std::max(maxBlock, b);
  adapter.activate(fs, 2, maxBlock);
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();

  const int64_t total = static_cast<int64_t>(fs * (switchSec + tailSec));
  const int64_t switchAt = static_cast<int64_t>(fs * switchSec);
  const std::vector<double> sig = makeSine(total, fs, 220.0);
  SeamDriveResult res;
  res.out[0].assign(static_cast<std::size_t>(total), 0.0);
  res.out[1].assign(static_cast<std::size_t>(total), 0.0);

  int64_t pos = 0;
  std::size_t blockIdx = 0;
  bool switched = false;
  while (pos < total) {
    if (!switched && pos >= switchAt) {
      switched = true;
      res.latOld = adapter.status().latencyFrames;
      mutate(snap);
      adapter.setParameterSnapshot(snap);
    }
    const StatusSnapshot before = adapter.status();
    const int32_t n = blockSchedule[blockIdx % blockSchedule.size()];
    const int32_t take = static_cast<int32_t>(std::min<int64_t>(n, total - pos));
    const double* in[2] = {sig.data() + pos, sig.data() + pos};
    double* o[2] = {res.out[0].data() + pos, res.out[1].data() + pos};
    BlockAutomation none;
    adapter.process(in, o, take, none);
    if (res.tAdopt < 0 && switched && before.chainReady &&
        adapter.status().latencyFrames > before.latencyFrames) {
      res.tAdopt = pos;  // the first block past the adoption
    }
    pos += take;
    ++blockIdx;
    if (paced) std::this_thread::sleep_for(std::chrono::microseconds(250));
  }
  res.status = adapter.status();
  res.latNew = res.status.latencyFrames;
  adapter.deactivate();

  // ---- the seam metrics (only meaningful for a latency GROWTH) -----------
  res.dLambda = res.latNew - res.latOld;
  if (res.tAdopt >= 0 && res.dLambda > 0) {
    const int64_t win = 4096;
    const int64_t replayFrom = res.tAdopt;
    const int64_t replayTo = std::min(res.tAdopt + res.dLambda, total);
    res.replayTotal = replayTo - replayFrom;
    for (int64_t p = replayFrom; p < replayTo; ++p) {
      if (p - res.dLambda >= 0 &&
          res.out[0][static_cast<std::size_t>(p)] ==
              res.out[0][static_cast<std::size_t>(p - res.dLambda)]) {
        ++res.replayMatch;
      }
    }
    const auto rmsOf = [&](int64_t a, int64_t b) {
      if (b <= a) return 0.0;
      double acc = 0.0;
      for (int64_t i = a; i < b; ++i) acc += res.out[0][static_cast<std::size_t>(i)] *
                                             res.out[0][static_cast<std::size_t>(i)];
      return std::sqrt(acc / static_cast<double>(b - a));
    };
    const auto peakOf = [&](int64_t a, int64_t b) {
      double pk = 0.0;
      for (int64_t i = a; i < b; ++i)
        pk = std::max(pk, std::fabs(res.out[0][static_cast<std::size_t>(i)]));
      return pk;
    };
    res.rmsPre = rmsOf(std::max<int64_t>(0, replayFrom - 3 * win), replayFrom - win);
    res.rmsReplay = rmsOf(replayFrom, std::min(replayFrom + win, total));
    res.rmsPost = rmsOf(std::min(replayTo, total - win), std::min(replayTo + win, total));
    res.peakPre = peakOf(std::max<int64_t>(0, replayFrom - 3 * win), replayFrom - win);
    res.peakReplay = peakOf(replayFrom, std::min(replayFrom + win, total));
    res.peakPost = peakOf(std::min(replayTo, total - win), std::min(replayTo + win, total));
  }
  return res;
}

/// The extreme-jump mutation: native.granular grain 0.1 s -> 0.5 s
/// (Λ 4896 -> 24096 at 48 kHz; ΔΛ = 19200 = the grain growth exactly).
void mutateGrainExtreme(ParamSnapshot& s) { s.grGrainSec = 0.50; }
void mutateGrainModerate(ParamSnapshot& s) { s.grGrainSec = 0.20; }
void mutateGrainReverse(ParamSnapshot& s) { s.grGrainSec = 0.10; }
/// pv.classic fft 2048 -> 4096 + hop 512 -> 1024 (Λ 2656 -> 5216 at 48 kHz)
/// — a LONG-WINDOW engine's latency growth (the control case: the alive
/// job's own wet span covers the re-coverage without the history).
void mutatePvFft(ParamSnapshot& s) {
  s.pvcFftSize = 4096;
  s.pvcHop = 1024;
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

ParamSnapshot granularBase() {
  ParamSnapshot snap;
  snap.engineIndex = 4;  // native.granular (registry order)
  snap.pitchSt = -4.0;
  snap.grGrainSec = 0.10;
  return snap;
}

}  // namespace

// ---------------------------------------------------------------------------
// T-S1 — THE HEADLINE: the extreme same-engine latency jump is fault-free
// ---------------------------------------------------------------------------

TEST_CASE("T-S1: granular grain 0.1 -> 0.5 (ΔΛ 19200): no underruns, the re-coverage served from the retained history") {
  initEngineRegistryOnce();
  if (const char* path = std::getenv("PITCHLAB_SEAM_ARTIFACT")) g_artifact = path;
  if (!g_artifact.empty()) {
    if (FILE* f = std::fopen(g_artifact.c_str(), "w")) {  // truncate: one suite run
      std::fprintf(f, "# Task 31 — the retiring-chain seam coverage matrix (vst_seam_test)\n\n");
      std::fclose(f);
    }
  }
  const SeamDriveResult r = driveSeam(48000.0, {512}, granularBase(), mutateGrainExtreme, 1.0, 1.6, true);
  CAPTURE(r.tAdopt);
  CAPTURE(r.dLambda);
  // THE 13558/14406-FRAME BURST IS GONE: zero faults of every class
  CHECK(r.status.faults == 0);
  CHECK(r.status.deliveryUnderruns == 0);
  CHECK(r.status.dryHistoryMisses == 0);
  CHECK(r.status.jobStalls == 0);
  CHECK(r.status.chainAdoptionFailures == 0);
  // the re-coverage was SERVED (not hidden): the retained history carried
  // the recycled-lane region — the count is the exact previously-faulting
  // span class (O(ΔΛ − wetLen + grid phase), always > 0 here)
  CHECK(r.status.seamRecoveries > 0);
  CHECK(r.status.seamRecoveries <= r.dLambda);
  // the lifecycle: exactly the legitimate rebuilds (initial + the genuine
  // configuration change), no churn, the Task-30 clamp policy intact
  CHECK(r.status.reprepares == 2);
  CHECK(r.status.chainsAdopted == 2);
  CHECK(r.status.clampEvents == 0);
  // the latency reported is the new chain's own (the Λ_eff growth)
  // the envelope-scoped splice latency (the Phase-5 correction): the
  // pitch term scales with the chain's own envMax (0.841 at -4 st), the
  // grain term is the growth
  CHECK(r.latOld == 4800 + 96);
  CHECK(r.latNew == 24000 + 96);
  // THE REPLAY: the re-coverage window is a BIT-IDENTICAL repeat of the
  // previously-emitted wet (the retained content == what readWet returned
  // while the lanes were live; the repeat itself is the frozen Λ_eff
  // re-coverage policy's declared cost — an intentional configuration
  // transition, now rendered correctly instead of as a dry burst)
  REQUIRE(r.replayTotal > 0);
  CHECK(r.replayMatch == r.replayTotal);
  // AUDIO CONTINUITY: no gap, no silence, no dry burst across the seam
  // (the replayed region is the same signal content; the granular
  // windowed-splice RMS varies with the window overlap — generous bounds,
  // the bit-identity above is the exact check)
  CHECK(r.rmsReplay > 0.3 * r.rmsPre);
  CHECK(r.rmsReplay < 3.0 * r.rmsPre);
  CHECK(r.rmsPost > 0.3 * r.rmsPre);
  CHECK(r.peakReplay < 3.0 * r.peakPre + 1e-9);
  artifactLine("| T-S1 | grain 0.1->0.5 @48k/512/paced | faults 0 | underruns 0 | seamRecoveries " +
               std::to_string(r.status.seamRecoveries) + " / ΔΛ " + std::to_string(r.dLambda) +
               " | replay " + std::to_string(r.replayMatch) + "/" + std::to_string(r.replayTotal) +
               " | RMS " + std::to_string(r.rmsPre) + "/" + std::to_string(r.rmsReplay) + "/" +
               std::to_string(r.rmsPost) + " |");
}

// ---------------------------------------------------------------------------
// T-S2/T-S3 — the moderate jump and the reverse direction
// ---------------------------------------------------------------------------

TEST_CASE("T-S2: granular grain 0.1 -> 0.2 (ΔΛ 4800): the bounded transient is covered") {
  initEngineRegistryOnce();
  const SeamDriveResult r = driveSeam(48000.0, {512}, granularBase(), mutateGrainModerate, 1.0, 1.1, true);
  // the previously-documented ~6-frame bounded transient: now served from
  // the retained history (the moderate jump's re-coverage is fully inside
  // the retention) — zero faults at every grid phase
  CHECK(r.status.faults == 0);
  CHECK(r.status.deliveryUnderruns == 0);
  CHECK(r.status.reprepares == 2);
  CHECK(r.status.clampEvents == 0);
  // NOTE: seamRecoveries is GRID-PHASE-DEPENDENT here (ΔΛ 4800 < wetLen
  // 9600: with a favourable job-grid phase the whole re-coverage fits the
  // alive lanes and the history serves nothing — measured 0..6 frames);
  // the invariant is zero faults + the exact replay, not the serve count
  CHECK(r.status.seamRecoveries <= r.dLambda);
  REQUIRE(r.replayTotal > 0);
  CHECK(r.replayMatch == r.replayTotal);
}

TEST_CASE("T-S3: granular grain 0.5 -> 0.1 (reverse: latency DECREASE): no re-coverage, no faults") {
  initEngineRegistryOnce();
  ParamSnapshot snap = granularBase();
  snap.grGrainSec = 0.50;
  const SeamDriveResult r = driveSeam(48000.0, {512}, snap, mutateGrainReverse, 1.0, 1.6, true);
  // Λ_eff never DECREASES within an activation (the frozen policy): the
  // emission timeline is unchanged, the new (cheaper) chain adopts under
  // the old latency — the retiring wet keeps covering q < act->base; there
  // is NO re-coverage and the history must not be consulted
  CHECK(r.latNew == 24096);  // the activation's max (the old chain's Λ)
  CHECK(r.status.faults == 0);
  CHECK(r.status.deliveryUnderruns == 0);
  CHECK(r.status.seamRecoveries == 0);
  CHECK(r.status.reprepares == 2);
  CHECK(r.status.clampEvents == 0);
}

// ---------------------------------------------------------------------------
// T-S4 — another engine's latency growth (the long-window control case)
// ---------------------------------------------------------------------------

TEST_CASE("T-S4: pv.classic fft/hop growth (ΔΛ 2560): long-window re-coverage fault-free") {
  initEngineRegistryOnce();
  ParamSnapshot snap;
  snap.engineIndex = 2;  // native.pv.classic
  snap.pitchSt = -4.0;
  const SeamDriveResult r = driveSeam(48000.0, {512}, snap, mutatePvFft, 1.0, 1.1, true);
  CHECK(r.latOld == 2048 + 512 + 32 + 64);
  CHECK(r.latNew == 4096 + 1024 + 32 + 64);
  CHECK(r.status.faults == 0);
  CHECK(r.status.deliveryUnderruns == 0);
  CHECK(r.status.reprepares == 2);
  CHECK(r.status.clampEvents == 0);
  // the long-window engine's re-coverage is carried by its OWN alive job
  // (wetLen 10 s >> ΔΛ): the history serves at most the recycled seam —
  // either way, fault-free
  CHECK(r.status.seamRecoveries <= r.dLambda);
  REQUIRE(r.replayTotal > 0);
  CHECK(r.replayMatch == r.replayTotal);
}

// ---------------------------------------------------------------------------
// T-S5 — the block-schedule matrix (the seam must not depend on a lucky
// host block size): fixed blocks + split/irregular sequences
// ---------------------------------------------------------------------------

TEST_CASE("T-S5: the extreme jump across block schedules 128..2048 + irregular") {
  initEngineRegistryOnce();
  const std::vector<std::vector<int32_t>> schedules = {
      {128}, {256}, {512}, {1024}, {2048}, {97, 512, 397, 1024, 311, 2048, 128}};
  for (const auto& sched : schedules) {
    const SeamDriveResult r = driveSeam(48000.0, sched, granularBase(), mutateGrainExtreme, 0.8, 1.6, true);
    CAPTURE(sched.front());
    CAPTURE(r.tAdopt);
    CAPTURE(r.status.seamRecoveries);
    CHECK(r.status.faults == 0);
    CHECK(r.status.deliveryUnderruns == 0);
    CHECK(r.status.jobStalls == 0);
    CHECK(r.status.reprepares == 2);
    CHECK(r.status.clampEvents == 0);
    CHECK(r.status.seamRecoveries > 0);
    REQUIRE(r.replayTotal > 0);
    CHECK(r.replayMatch == r.replayTotal);
    artifactLine("| T-S5 | block sched " + std::to_string(sched.size()) + " entries (" +
                 std::to_string(sched.front()) + ".." + std::to_string(sched.back()) +
                 ") | faults 0 | seamRecoveries " + std::to_string(r.status.seamRecoveries) +
                 " | replay " + std::to_string(r.replayMatch) + "/" + std::to_string(r.replayTotal) +
                 " |");
  }
}

// ---------------------------------------------------------------------------
// T-S6 — the sample-rate matrix (44.1 / 48 / 96 kHz)
// ---------------------------------------------------------------------------

TEST_CASE("T-S6: the extreme jump at 44.1 / 48 / 96 kHz") {
  initEngineRegistryOnce();
  for (double fs : {44100.0, 48000.0, 96000.0}) {
    const SeamDriveResult r = driveSeam(fs, {512}, granularBase(), mutateGrainExtreme, 0.8, 1.8, true);
    CAPTURE(fs);
    CAPTURE(r.latOld);
    CAPTURE(r.latNew);
    CAPTURE(r.status.seamRecoveries);
    CHECK(r.status.faults == 0);
    CHECK(r.status.deliveryUnderruns == 0);
    CHECK(r.status.reprepares == 2);
    CHECK(r.status.clampEvents == 0);
    CHECK(r.status.seamRecoveries > 0);
    REQUIRE(r.replayTotal > 0);
    CHECK(r.replayMatch == r.replayTotal);
    artifactLine("| T-S6 | fs " + std::to_string(static_cast<int>(fs)) + " | Λ " +
                 std::to_string(r.latOld) + "->" + std::to_string(r.latNew) + " | faults 0 | seamRecoveries " +
                 std::to_string(r.status.seamRecoveries) + " | replay " +
                 std::to_string(r.replayMatch) + "/" + std::to_string(r.replayTotal) + " |");
  }
}

// ---------------------------------------------------------------------------
// T-S7 — host-like timing: paced (realistic cadence) vs back-to-back (the
// starvation stress). Pacing distinguishes a TRUE seam failure from a
// preparation-starvation artefact (Task 30's measured harness lesson):
// the seam invariant must hold in BOTH modes — if the back-to-back mode
// ever showed faults, the fault CLASS (delivery underruns at the
// production frontier vs the re-coverage region) identifies the mechanism.
// ---------------------------------------------------------------------------

TEST_CASE("T-S7: paced vs back-to-back — the seam invariant in both cadence modes") {
  initEngineRegistryOnce();
  const SeamDriveResult paced = driveSeam(48000.0, {512}, granularBase(), mutateGrainExtreme, 1.0, 1.6, true);
  CHECK(paced.status.faults == 0);
  CHECK(paced.status.seamRecoveries > 0);
  REQUIRE(paced.replayTotal > 0);
  CHECK(paced.replayMatch == paced.replayTotal);

  const SeamDriveResult tight = driveSeam(48000.0, {512}, granularBase(), mutateGrainExtreme, 1.0, 1.6, false);
  CAPTURE(tight.tAdopt);
  CAPTURE(tight.status.seamRecoveries);
  CAPTURE(tight.status.faults);
  // the seam coverage invariant holds regardless of the prep thread's
  // scheduling (the adoption may land LATER under starvation — the
  // re-coverage region grows with the grid phase — but every re-covered
  // frame is served: faults stay ZERO and the replay stays bit-identical)
  CHECK(tight.status.faults == 0);
  CHECK(tight.status.deliveryUnderruns == 0);
  REQUIRE(tight.replayTotal > 0);
  CHECK(tight.replayMatch == tight.replayTotal);
  artifactLine("| T-S7 | paced faults " + std::to_string(paced.status.faults) +
               ", seamRecoveries " + std::to_string(paced.status.seamRecoveries) +
               " | back-to-back faults " + std::to_string(tight.status.faults) +
               ", seamRecoveries " + std::to_string(tight.status.seamRecoveries) + " |");
}

// ---------------------------------------------------------------------------
// T-S8 — determinism: the same drive twice. The per-run invariant set is
// the architectural guarantee (mid-stream adoption positions are async by
// design — the audio-path artifact's phase-G records carry only
// adoption-robust fields for exactly this reason); the REPLAY bit-identity
// within each run is the deterministic seam property.
// ---------------------------------------------------------------------------

TEST_CASE("T-S8: the extreme jump twice — the invariant set deterministic, the seam replay exact in both") {
  initEngineRegistryOnce();
  for (int run = 0; run < 2; ++run) {
    const SeamDriveResult r = driveSeam(48000.0, {512}, granularBase(), mutateGrainExtreme, 1.0, 1.6, true);
    CAPTURE(run);
    CAPTURE(r.tAdopt);
    CHECK(r.status.faults == 0);
    CHECK(r.status.reprepares == 2);
    CHECK(r.status.chainsAdopted == 2);
    CHECK(r.status.clampEvents == 0);
    CHECK(r.status.seamRecoveries > 0);
    REQUIRE(r.replayTotal > 0);
    CHECK(r.replayMatch == r.replayTotal);
    // the counters are integers — the deterministic lifecycle figures
    // (the envelope-scoped splice latencies, the Phase-5 correction)
    CHECK(r.latOld == 4896);
    CHECK(r.latNew == 24096);
  }
}

// ---------------------------------------------------------------------------
// T-S9 — the Task-30 no-regression: the extreme jump ON TOP of the churn
// scenario (−12 st + LFO depth 2): clamps stay boundary events, the
// re-prepare count stays the legitimate configuration change, the seam
// stays covered.
// ---------------------------------------------------------------------------

TEST_CASE("T-S9: extreme jump at −12 st + LFO depth 2 — the Task-30 clamp policy intact") {
  initEngineRegistryOnce();
  ParamSnapshot snap = granularBase();
  snap.pitchSt = -12.0;
  snap.lfoDepthSt = 2.0;
  snap.lfoRateHz = 5.0;
  const SeamDriveResult r = driveSeam(48000.0, {512}, snap, mutateGrainExtreme, 1.0, 1.6, true);
  CAPTURE(r.status.seamRecoveries);
  // the envelope covers the ±14 st surface: the LFO never clamps (a static
  // PITCH+LFO setting never clamps — the Task-30 rule) — including across
  // the genuine grain-change rebuild
  CHECK(r.status.clampEvents == 0);
  // exactly the legitimate lifecycle: the initial chain + the grain change
  // (the LFO depth/curve movement is NOT a rebuild trigger)
  CHECK(r.status.reprepares == 2);
  CHECK(r.status.chainsAdopted == 2);
  CHECK(r.status.faults == 0);
  CHECK(r.status.deliveryUnderruns == 0);
  CHECK(r.status.seamRecoveries > 0);
  REQUIRE(r.replayTotal > 0);
  CHECK(r.replayMatch == r.replayTotal);
}

// ---------------------------------------------------------------------------
// T-S10 — a cross-engine latency-increasing switch (varispeed -> granular):
// the re-coverage across DIFFERENT engines (the retiring content is the old
// engine's wet — the retained history replays exactly that).
// ---------------------------------------------------------------------------

TEST_CASE("T-S10: varispeed -> granular (cross-engine ΔΛ ≈ 4800): re-coverage fault-free") {
  initEngineRegistryOnce();
  // Task-33 continuation (Phase 5): the envelope-scoped splice latencies —
  // varispeed at -4 st: Λ 96 (the down-read needs no future input); the
  // granular grain-0.1 chain: Λ 4896 (the grain term). ΔΛ = 4800 = the
  // grain growth exactly (the comment's invariant is unchanged).
  ParamSnapshot snap;
  snap.engineIndex = 0;  // native.varispeed
  snap.pitchSt = -4.0;
  const SeamDriveResult r = driveSeam(48000.0, {512}, snap,
                                      [](ParamSnapshot& s) { s.engineIndex = 4; }, 1.0, 1.6, true);
  CAPTURE(r.latOld);
  CAPTURE(r.latNew);
  CAPTURE(r.status.seamRecoveries);
  CHECK(r.latOld == 96);      // varispeed at -4 st, the envelope-scoped lead:
                              // the down-read (0.794x) never outruns realtime,
                              // only the kernel/safety margin remains
  CHECK(r.latNew == 4896);    // granular grain 0.1 (the grain term + margin)
  CHECK(r.status.faults == 0);
  CHECK(r.status.deliveryUnderruns == 0);
  CHECK(r.status.reprepares == 2);
  CHECK(r.status.clampEvents == 0);
  // the retiring varispeed chain is ALSO splice-mode (9600-frame windows):
  // the re-coverage of the recycled-lane region comes from the retained
  // history when the grid phase leaves a gap (ΔΛ 4800 < wetLen — the
  // serve count is phase-dependent); the invariants are zero faults + the
  // exact replay of the OLD engine's wet, then the blend into the new
  CHECK(r.status.seamRecoveries <= r.dLambda);
  REQUIRE(r.replayTotal > 0);
  CHECK(r.replayMatch == r.replayTotal);
}
