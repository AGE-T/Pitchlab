// Pitch Lab VST3 product layer — T-RT* (Task 32): the REALTIME-CAPABILITY
// STATUS suite.
//
// Part 1 — the CLASSIFIER as pure fixtures (deterministic, no audio):
//   * the four levels (OK / LIMITED / NOT SUSTAINABLE / UNKNOWN)
//   * the three bases (MEASURED / BENCHMARK / NONE) and their precedence
//     (a measurement ALWAYS supersedes the benchmark prior — the honest
//     no-blanket-rule case: granular @ 96 kHz + a measured RTF of 0.5
//     classifies OK)
//   * the thresholds (0.75 / 1.0, inclusive bounds documented) and the
//     measurement-window validity rule (>= 8192 frames AND >= 2 blocks)
//   * the fault escalation (one level, never two; the causal flag)
//   * the benchmark prior's exact scope (native.granular @ >= 88.2 kHz,
//     LIMITED — never an engine-wide offline verdict)
//
// Part 2 — the LABEL TEXTS (the strings the editor renders; the audio
// thread never formats — the formatters are the single string site):
//   * the status line for every level/basis combination
//   * the NOT-SUSTAINABLE warning detail carries the required semantics:
//     the CURRENT configuration exceeds the MEASURED realtime capacity —
//     use the offline render for this setting
//
// Part 3 — the INTEGRATION drives (the real adapter + the real engines):
//   * the measurement accumulates (engineCpuNanos > 0, rtFrames == driven
//     frames) and a healthy 48 kHz configuration classifies OK/MEASURED
//   * the window FOLLOWS the active chain: an engine switch resets the
//     accumulators at adoption (the classification describes the CURRENT
//     configuration, never a blend of retired chains)
//   * the granular 96 kHz -12 st configuration (the task-30 evidence
//     family) classifies NOT SUSTAINABLE through the REAL measurement —
//     the product warning the user sees
//   * the LFO rate is NOT a lifecycle input: toggling the rate OFF/ON
//     mid-stream never rebuilds the chain (no churn — Task 32 Part A)

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "vst/parameters.h"
#include "vst/realtime_adapter.h"
#include "vst/realtime_status.h"

using namespace pitchlab::vst;

namespace {

constexpr double kFs = 48000.0;

std::vector<double> makeSine(int64_t frames, double freq, double amp = 0.5) {
  std::vector<double> out(static_cast<std::size_t>(frames));
  for (int64_t i = 0; i < frames; ++i) {
    out[static_cast<std::size_t>(i)] =
        amp * std::sin(2.0 * 3.14159265358979323846 * freq * static_cast<double>(i) / kFs);
  }
  return out;
}

/// A StatusSnapshot fixture with a MEASURED window: `frames` of input at
/// `fs`, the engine having consumed `cpuNs` of steady-clock time.
StatusSnapshot measuredSnapshot(double fs, int64_t frames, int64_t cpuNs,
                                int engineIndex = 1, int maxBlock = 1024) {
  StatusSnapshot st;
  st.chainReady = true;
  st.sampleRate = fs;
  st.maxBlock = maxBlock;
  st.engineIndex = engineIndex;
  st.rtFrames = frames;
  st.engineCpuNanos = static_cast<uint64_t>(cpuNs);
  return st;
}

/// The RTF the fixture encodes (the classifier must reproduce it exactly).
double rtfOf(int64_t frames, int64_t cpuNs, double fs) {
  return (static_cast<double>(cpuNs) / 1e9) / (static_cast<double>(frames) / fs);
}

/// Drive the adapter at an arbitrary rate (the diagnostics suite's paced
/// pattern: 250 us between blocks — the preparation thread needs realistic
/// host cadence) and return the final status.
StatusSnapshot pacedDrive(const ParamSnapshot& snap, double fs, int32_t block,
                          double seconds, double inputFreq = 220.0) {
  RealtimeAdapter adapter;
  adapter.activate(fs, 2, block);
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();
  const int64_t total = static_cast<int64_t>(fs * seconds);
  const auto sig = makeSine(total, inputFreq);
  std::vector<double> out[2] = {std::vector<double>(static_cast<std::size_t>(total), 0.0),
                                std::vector<double>(static_cast<std::size_t>(total), 0.0)};
  int64_t pos = 0;
  while (pos < total) {
    const int32_t take = static_cast<int32_t>(std::min<int64_t>(block, total - pos));
    const double* in[2] = {sig.data() + pos, sig.data() + pos};
    double* o[2] = {out[0].data() + pos, out[1].data() + pos};
    BlockAutomation none;
    adapter.process(in, o, take, none);
    pos += take;
    std::this_thread::sleep_for(std::chrono::microseconds(250));
  }
  const StatusSnapshot st = adapter.status();
  adapter.deactivate();
  return st;
}

}  // namespace

// ---------------------------------------------------------------------------
// Part 1 — the classifier (pure fixtures)
// ---------------------------------------------------------------------------

TEST_CASE("T-RT1: no active chain classifies UNKNOWN (never a guess)") {
  StatusSnapshot st;  // defaults: chainReady = false
  const RtClassification c = classifyRealtimeStatus(st);
  CHECK(c.level == RtLevel::Unknown);
  CHECK(c.basis == RtBasis::Unset);
  // a ready chain with no sample rate is equally unclassifiable
  StatusSnapshot st2 = measuredSnapshot(0.0, 1'000'000, 500'000'000);
  st2.chainReady = true;
  st2.sampleRate = 0.0;
  const RtClassification c2 = classifyRealtimeStatus(st2);
  CHECK(c2.level == RtLevel::Unknown);
  CHECK(c2.basis == RtBasis::Unset);
}

TEST_CASE("T-RT1: an insufficient measurement window is UNKNOWN for unbenchmarked engines") {
  // vardelay @ 48 kHz with 8191 frames: below the window floor -> no
  // measured basis, no benchmark prior -> the honest UNKNOWN
  StatusSnapshot st = measuredSnapshot(kFs, kRtfMinWindowFrames - 1, 10'000'000);
  const RtClassification c = classifyRealtimeStatus(st);
  CHECK(c.level == RtLevel::Unknown);
  CHECK(c.basis == RtBasis::Unset);
  // the window is additionally floored at TWO process blocks: 8192 frames
  // with maxBlock 8192 requires 16384
  StatusSnapshot st2 = measuredSnapshot(kFs, 8192, 10'000'000, 1, 8192);
  const RtClassification c2 = classifyRealtimeStatus(st2);
  CHECK(c2.level == RtLevel::Unknown);
  CHECK(c2.basis == RtBasis::Unset);
}

TEST_CASE("T-RT1: the measured thresholds classify OK / LIMITED / NOT SUSTAINABLE") {
  // 1 second of timeline at 48 kHz
  const int64_t frames = 48'000;
  // rtf 0.30 -> OK
  {
    StatusSnapshot st = measuredSnapshot(kFs, frames, static_cast<int64_t>(0.30e9));
    CHECK(rtfOf(frames, static_cast<int64_t>(0.30e9), kFs) == doctest::Approx(0.30));
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::Ok);
    CHECK(c.basis == RtBasis::Measured);
    CHECK(c.measuredRtf == doctest::Approx(0.30).epsilon(1e-9));
    CHECK(c.faultEscalated == false);
  }
  // rtf 0.75 EXACTLY (the documented inclusive bound) -> OK
  {
    const int64_t cpu = static_cast<int64_t>(0.75e9);
    StatusSnapshot st = measuredSnapshot(kFs, frames, cpu);
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::Ok);
  }
  // rtf 0.76 -> LIMITED
  {
    StatusSnapshot st = measuredSnapshot(kFs, frames, static_cast<int64_t>(0.76e9));
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::Limited);
    CHECK(c.basis == RtBasis::Measured);
    CHECK(c.faultEscalated == false);
  }
  // rtf 1.0 EXACTLY (work fills the timeline: no headroom) -> LIMITED
  {
    StatusSnapshot st = measuredSnapshot(kFs, frames, static_cast<int64_t>(1.0e9));
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::Limited);
  }
  // rtf 1.01 -> NOT SUSTAINABLE
  {
    StatusSnapshot st = measuredSnapshot(kFs, frames, static_cast<int64_t>(1.01e9));
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::NotSustainable);
    CHECK(c.basis == RtBasis::Measured);
    CHECK(c.faultEscalated == false);
  }
  // the granular-class evidence figure (task-30 matrix: -12 st @ 96 kHz
  // measures 1.35-1.89) -> NOT SUSTAINABLE. 96'000 frames at 96 kHz = 1 s
  // of timeline; 1.42 s of engine CPU -> rtf 1.42.
  {
    StatusSnapshot st = measuredSnapshot(96000.0, 96'000, static_cast<int64_t>(1.42e9), 4);
    CHECK(rtfOf(96'000, static_cast<int64_t>(1.42e9), 96000.0) == doctest::Approx(1.42));
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::NotSustainable);
    CHECK(c.basis == RtBasis::Measured);
    CHECK(c.measuredRtf == doctest::Approx(1.42).epsilon(1e-9));
  }
  // a zero-cost measurement (a path that measured no engine work in a
  // sufficient window) is a legitimate rtf 0 -> OK
  {
    StatusSnapshot st = measuredSnapshot(kFs, frames, 0);
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::Ok);
    CHECK(c.basis == RtBasis::Measured);
    CHECK(c.measuredRtf == 0.0);
  }
}

TEST_CASE("T-RT1: fault evidence escalates exactly one level (never two)") {
  const int64_t frames = 48'000;
  // rtf 0.30 (Ok) + faults -> LIMITED, flagged as fault-escalated
  {
    StatusSnapshot st = measuredSnapshot(kFs, frames, static_cast<int64_t>(0.30e9));
    st.faults = 2;
    st.deliveryUnderruns = 2;  // the aggregate's composition
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::Limited);
    CHECK(c.basis == RtBasis::Measured);
    CHECK(c.faultEscalated == true);
  }
  // rtf 0.90 (Limited) + faults -> NOT SUSTAINABLE (near the boundary AND
  // demonstrably failing to deliver)
  {
    StatusSnapshot st = measuredSnapshot(kFs, frames, static_cast<int64_t>(0.90e9));
    st.faults = 1;
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::NotSustainable);
    CHECK(c.faultEscalated == true);
  }
  // rtf 1.4 (NotSustainable) + faults -> stays NotSustainable (no double
  // jump — there is nothing beyond it), still flagged for the causal text
  {
    StatusSnapshot st = measuredSnapshot(kFs, frames, static_cast<int64_t>(1.4e9));
    st.faults = 7;
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::NotSustainable);
    CHECK(c.faultEscalated == true);
  }
}

TEST_CASE("T-RT1: the benchmark prior is scoped to its measured evidence") {
  initEngineRegistryOnce();
  // native.granular (registry index 4) at 96 kHz, BEFORE measurement ->
  // LIMITED on the benchmark basis (the task-30 matrix family: RTF
  // 0.58-1.89 BY CONFIGURATION — deliberately NOT "not sustainable" and
  // NEVER an engine-wide offline verdict)
  {
    StatusSnapshot st;  // no window at all (fresh adoption)
    st.chainReady = true;
    st.sampleRate = 96000.0;
    st.engineIndex = 4;
    st.maxBlock = 1024;
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::Limited);
    CHECK(c.basis == RtBasis::Benchmark);
    CHECK(c.measuredRtf == 0.0);  // no measurement yet — the prior claims no figure
  }
  // the double-rate CLASS (88.2 kHz) carries the same prior; 48 kHz does NOT
  {
    StatusSnapshot st;
    st.chainReady = true;
    st.sampleRate = 88200.0;
    st.engineIndex = 4;
    st.maxBlock = 1024;
    CHECK(classifyRealtimeStatus(st).level == RtLevel::Limited);
    CHECK(classifyRealtimeStatus(st).basis == RtBasis::Benchmark);
    st.sampleRate = 48000.0;
    const RtClassification c48 = classifyRealtimeStatus(st);
    CHECK(c48.level == RtLevel::Unknown);  // no prior at 48 kHz: honest UNKNOWN
    CHECK(c48.basis == RtBasis::Unset);
  }
  // every OTHER engine at 96 kHz has NO prior (no blanket rate rule):
  // varispeed(0), vardelay(1), pv.classic(2), pv.phaselocked(3)
  for (int e : {0, 1, 2, 3}) {
    StatusSnapshot st;
    st.chainReady = true;
    st.sampleRate = 96000.0;
    st.engineIndex = e;
    st.maxBlock = 1024;
    CAPTURE(e);
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::Unknown);
    CHECK(c.basis == RtBasis::Unset);
  }
}

TEST_CASE("T-RT1: a measurement ALWAYS supersedes the prior (no blanket rule)") {
  // THE central honesty case: native.granular @ 96 kHz with a sufficient
  // window measuring rtf 0.5 (the task-30 +12 st rows measure 0.58-0.69)
  // classifies OK/MEASURED — the benchmark prior does NOT override the
  // current configuration's own measurement
  {
    StatusSnapshot st = measuredSnapshot(96000.0, 96'000, static_cast<int64_t>(0.5e9), 4);
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::Ok);
    CHECK(c.basis == RtBasis::Measured);
    CHECK(c.measuredRtf == doctest::Approx(0.5).epsilon(1e-9));
  }
  // and the same engine/rate measuring 1.5 (the -12 rows) is NOT
  // SUSTAINABLE — one engine, opposite verdicts BY CONFIGURATION
  {
    StatusSnapshot st = measuredSnapshot(96000.0, 96'000, static_cast<int64_t>(1.5e9), 4);
    const RtClassification c = classifyRealtimeStatus(st);
    CHECK(c.level == RtLevel::NotSustainable);
    CHECK(c.basis == RtBasis::Measured);
  }
}

// ---------------------------------------------------------------------------
// Part 2 — the label texts (the single string site)
// ---------------------------------------------------------------------------

TEST_CASE("T-RT2: the status line renders every level/basis combination") {
  char buf[160];
  // OK / measured
  {
    const RtClassification c{RtLevel::Ok, RtBasis::Measured, 0.34, 48000, 0, false};
    formatRealtimeStatusLine(c, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "REALTIME OK · MEASURED RTF 0.34") == 0);
  }
  // LIMITED / measured
  {
    const RtClassification c{RtLevel::Limited, RtBasis::Measured, 0.86, 48000, 0, false};
    formatRealtimeStatusLine(c, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "REALTIME LIMITED · MEASURED RTF 0.86") == 0);
  }
  // NOT SUSTAINABLE / measured — the loud, prefixed form
  {
    const RtClassification c{RtLevel::NotSustainable, RtBasis::Measured, 1.42, 48000, 0, false};
    formatRealtimeStatusLine(c, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "!! REALTIME NOT SUSTAINABLE · MEASURED RTF 1.42") == 0);
    CHECK(std::strstr(buf, "REALTIME NOT SUSTAINABLE") != nullptr);
    CHECK(std::strstr(buf, "1.42") != nullptr);
  }
  // LIMITED / benchmark (pre-measurement)
  {
    const RtClassification c{RtLevel::Limited, RtBasis::Benchmark, 0.0, 0, 0, false};
    formatRealtimeStatusLine(c, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "REALTIME LIMITED · BENCHMARK PRIOR · MEASURING") == 0);
  }
  // UNKNOWN — the honest no-claim state
  {
    const RtClassification c{RtLevel::Unknown, RtBasis::Unset, 0.0, 100, 0, false};
    formatRealtimeStatusLine(c, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "REALTIME STATUS UNKNOWN · MEASURING") == 0);
  }
}

TEST_CASE("T-RT2: the NOT-SUSTAINABLE warning detail carries the required semantics") {
  char buf[160];
  // the measured-CPU case: the CURRENT configuration exceeds the MEASURED
  // realtime capacity; the offline render is the tool for this setting
  {
    const RtClassification c{RtLevel::NotSustainable, RtBasis::Measured, 1.42, 48000, 0, false};
    formatRealtimeDetailLine(c, buf, sizeof(buf));
    CHECK(std::strstr(buf, "EXCEEDS MEASURED REALTIME CAPACITY") != nullptr);
    CHECK(std::strstr(buf, "USE OFFLINE RENDER") != nullptr);
    CHECK(std::strstr(buf, "CURRENT CONFIGURATION") != nullptr);
  }
  // the fault-evidenced case names BOTH causes
  {
    const RtClassification c{RtLevel::NotSustainable, RtBasis::Measured, 0.9, 48000, 3, true};
    formatRealtimeDetailLine(c, buf, sizeof(buf));
    CHECK(std::strstr(buf, "DELIVERY FAULTS") != nullptr);
    CHECK(std::strstr(buf, "OFFLINE RENDER") != nullptr);
  }
  // the benchmark case claims only what the evidence says
  {
    const RtClassification c{RtLevel::NotSustainable, RtBasis::Benchmark, 0.0, 0, 0, false};
    formatRealtimeDetailLine(c, buf, sizeof(buf));
    CHECK(std::strstr(buf, "BENCHMARK EVIDENCE") != nullptr);
  }
}

TEST_CASE("T-RT2: the LIMITED detail explains its cause; OK/UNKNOWN stay quiet") {
  char buf[160];
  {
    const RtClassification c{RtLevel::Limited, RtBasis::Measured, 0.86, 48000, 0, false};
    formatRealtimeDetailLine(c, buf, sizeof(buf));
    CHECK(std::strstr(buf, "NEAR MEASURED REALTIME CAPACITY") != nullptr);
    CHECK(std::strstr(buf, "HEADROOM") != nullptr);
  }
  {
    const RtClassification c{RtLevel::Limited, RtBasis::Benchmark, 0.0, 0, 0, false};
    formatRealtimeDetailLine(c, buf, sizeof(buf));
    CHECK(std::strstr(buf, "96 KHZ BENCHMARK") != nullptr);
    CHECK(std::strstr(buf, "BY CONFIG") != nullptr);  // configuration-dependent, never blanket
  }
  {
    const RtClassification c{RtLevel::Limited, RtBasis::Measured, 0.3, 48000, 2, true};
    formatRealtimeDetailLine(c, buf, sizeof(buf));
    CHECK(std::strstr(buf, "DELIVERY FAULTS RECORDED") != nullptr);
  }
  // OK and UNKNOWN: no detail line (the envelope line owns the slot)
  {
    const RtClassification c{RtLevel::Ok, RtBasis::Measured, 0.3, 48000, 0, false};
    formatRealtimeDetailLine(c, buf, sizeof(buf));
    CHECK(buf[0] == '\0');
  }
  {
    const RtClassification c{RtLevel::Unknown, RtBasis::Unset, 0.0, 0, 0, false};
    formatRealtimeDetailLine(c, buf, sizeof(buf));
    CHECK(buf[0] == '\0');
  }
}

TEST_CASE("T-RT2: the stable names for tools/tests") {
  CHECK(std::strcmp(rtLevelName(RtLevel::Ok), "ok") == 0);
  CHECK(std::strcmp(rtLevelName(RtLevel::Limited), "limited") == 0);
  CHECK(std::strcmp(rtLevelName(RtLevel::NotSustainable), "not-sustainable") == 0);
  CHECK(std::strcmp(rtLevelName(RtLevel::Unknown), "unknown") == 0);
  CHECK(std::strcmp(rtBasisName(RtBasis::Unset), "none") == 0);
  CHECK(std::strcmp(rtBasisName(RtBasis::Measured), "measured") == 0);
  CHECK(std::strcmp(rtBasisName(RtBasis::Benchmark), "benchmark") == 0);
}

// ---------------------------------------------------------------------------
// Part 3 — integration (the real adapter + the real engines)
// ---------------------------------------------------------------------------

TEST_CASE("T-RT3: a healthy 48 kHz drive measures and classifies OK/MEASURED") {
  initEngineRegistryOnce();
  ParamSnapshot snap;  // vardelay, pitch 0 — the default engine
  snap.pitchSt = -5.0;
  const StatusSnapshot st = pacedDrive(snap, kFs, 1024, 1.0);
  REQUIRE(st.chainReady);
  REQUIRE(st.faults == 0);
  // the measurement accumulated over the WHOLE drive (one chain, no switch)
  CHECK(st.rtFrames == static_cast<int64_t>(kFs * 1.0));
  CHECK(st.engineCpuNanos > 0);
  const RtClassification c = classifyRealtimeStatus(st);
  CHECK(c.basis == RtBasis::Measured);
  CHECK(c.level == RtLevel::Ok);
  CHECK(c.measuredRtf > 0.0);
  CHECK(c.measuredRtf < kRtfOkMax);  // vardelay @ 48 kHz is far inside capacity
  MESSAGE("vardelay@48k measured rtf " << c.measuredRtf);
}

TEST_CASE("T-RT3: the measurement window FOLLOWS the active chain (engine switch resets it)") {
  initEngineRegistryOnce();
  // drive vardelay for 1.2 s, then switch to pv.classic and drive 0.4 s
  // more: the accumulators reset at the ADOPTION, so the final window
  // covers only the current (pv.classic) configuration
  RealtimeAdapter adapter;
  adapter.activate(kFs, 2, 1024);
  ParamSnapshot snap;  // vardelay
  snap.pitchSt = -5.0;
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();
  const int64_t total1 = static_cast<int64_t>(kFs * 1.2);
  const int64_t total2 = static_cast<int64_t>(kFs * 0.4);
  const auto sig = makeSine(total1 + total2, 220.0);
  std::vector<double> out(static_cast<std::size_t>(total1 + total2), 0.0);
  double* o[2] = {out.data(), out.data()};
  int64_t pos = 0;
  auto runFor = [&](int64_t frames) {
    int64_t end = pos + frames;
    while (pos < end) {
      const int32_t take = static_cast<int32_t>(std::min<int64_t>(1024, end - pos));
      const double* in[2] = {sig.data() + pos, sig.data() + pos};
      BlockAutomation none;
      adapter.process(in, o, take, none);
      pos += take;
      std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
  };
  runFor(total1);
  const uint64_t adoptedBefore = adapter.status().chainsAdopted;
  REQUIRE(adoptedBefore >= 1);
  // the switch (a chain-signature change): pv.classic
  ParamSnapshot snap2 = snap;
  snap2.engineIndex = 2;
  adapter.setParameterSnapshot(snap2);
  runFor(total2);
  const StatusSnapshot st = adapter.status();
  REQUIRE(st.chainsAdopted == adoptedBefore + 1);
  REQUIRE(st.engineIndex == 2);
  // the window: frames SINCE the adoption — bounded by the post-switch
  // audio (the adoption lands within the first blocks after the switch)
  CHECK(st.rtFrames > 0);
  CHECK(st.rtFrames < total1);  // strictly smaller than the whole drive
  CHECK(st.rtFrames <= total2 + 4 * 1024);  // + the pre-adoption blocks at most
  CHECK(st.engineCpuNanos > 0);
  // and the classification is MEASURED for the current configuration
  const RtClassification c = classifyRealtimeStatus(st);
  CHECK(c.basis == RtBasis::Measured);
  CHECK(c.windowFrames == st.rtFrames);
  adapter.deactivate();
}

TEST_CASE("T-RT3: granular @ 96 kHz / -12 st classifies NOT SUSTAINABLE (the real warning)") {
  initEngineRegistryOnce();
  // The task-30 evidence family: the windowed-splice granular cost at the
  // double rate with a downshift measured RTF 1.35-1.89 (reference machine
  // AND the canonical CI runner, results/vst3/task30). The product status
  // the user sees for this configuration is the measured NOT SUSTAINABLE
  // warning — the honest per-configuration verdict (the +12 st rows
  // measure ~0.58 and classify OK: no blanket rule is encoded).
  ParamSnapshot snap;
  snap.engineIndex = 4;  // native.granular
  snap.pitchSt = -12.0;
  snap.grGrainSec = 0.1;
  const StatusSnapshot st = pacedDrive(snap, 96000.0, 128, 1.5);
  REQUIRE(st.chainReady);
  REQUIRE(st.faults == 0);  // the task-29/30 finding: fault-free but CPU-bound
  const RtClassification c = classifyRealtimeStatus(st);
  CHECK(c.basis == RtBasis::Measured);
  CHECK(c.level == RtLevel::NotSustainable);
  CHECK(c.measuredRtf > kRtfSustainableMax);
  MESSAGE("granular@96k/-12st measured rtf " << c.measuredRtf);
  // the warning the editor renders for this state (the exact texts)
  char line[160];
  formatRealtimeStatusLine(c, line, sizeof(line));
  CHECK(std::strstr(line, "REALTIME NOT SUSTAINABLE") != nullptr);
  char detail[160];
  formatRealtimeDetailLine(c, detail, sizeof(detail));
  CHECK(std::strstr(detail, "USE OFFLINE RENDER") != nullptr);
}

TEST_CASE("T-RT3: the LFO rate never touches the chain lifecycle (OFF/ON mid-stream)") {
  initEngineRegistryOnce();
  // Task 32 Part A's lifecycle guarantee: the rate is not in the chain
  // signature, not in the envelope geometry, not in any capability exit —
  // toggling the rate OFF and back ON mid-stream must NOT rebuild the
  // chain (no churn) and stays fault-free.
  RealtimeAdapter adapter;
  adapter.activate(kFs, 2, 1024);
  ParamSnapshot snap;  // vardelay
  snap.pitchSt = -5.0;
  snap.lfoRateHz = 5.0;
  snap.lfoDepthSt = 1.0;  // a live LFO: turning the rate OFF really changes the curve
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();
  const int64_t total = static_cast<int64_t>(kFs * 0.9);
  const auto sig = makeSine(total, 220.0);
  std::vector<double> out(static_cast<std::size_t>(total), 0.0);
  double* o[2] = {out.data(), out.data()};
  int64_t pos = 0;
  uint64_t epochBumps = 0;
  auto drive = [&](int64_t frames) {
    int64_t end = pos + frames;
    while (pos < end) {
      const int32_t take = static_cast<int32_t>(std::min<int64_t>(1024, end - pos));
      const double* in[2] = {sig.data() + pos, sig.data() + pos};
      BlockAutomation none;
      adapter.process(in, o, take, none);
      pos += take;
    }
  };
  drive(static_cast<int64_t>(kFs * 0.3));
  const uint64_t repreparesBefore = adapter.status().reprepares;
  const uint64_t adoptedBefore = adapter.status().chainsAdopted;
  // OFF: rate 0 with a NONZERO depth (the required case matrix)
  ParamSnapshot off = snap;
  off.lfoRateHz = 0.0;
  adapter.setParameterSnapshot(off);
  drive(static_cast<int64_t>(kFs * 0.3));
  // back ON: the modulation resumes from the parked zero-crossing phase
  adapter.setParameterSnapshot(snap);
  drive(static_cast<int64_t>(kFs * 0.3));
  const StatusSnapshot st = adapter.status();
  CHECK(st.faults == 0);
  CHECK(st.reprepares == repreparesBefore);  // ONE chain for the whole drive
  CHECK(st.chainsAdopted == adoptedBefore);
  CHECK(st.rtFrames == total);
  (void)epochBumps;
  adapter.deactivate();
}
