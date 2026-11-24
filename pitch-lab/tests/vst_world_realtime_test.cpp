// Pitch Lab VST3 product layer — native.world through the REAL realtime
// adapter path (RealtimeAdapter::process, realtime mode: never wait).
//
// Asserted:
//   * realtime-paced drive (wall-clock pacing, 512-frame blocks) runs WORLD
//     (DIO + Harvest, stereo) without dry fallback / faults;
//   * an overloaded pool (a 300 ms chunk delay) never blocks the audio
//     callback: the adapter's process() stays fast, the missing wet frames
//     are COUNTED (starved calls + underruns), nothing throws or hangs, and
//     the wet path recovers once the pool catches up;
//   * a worker failure surfaces as a counted engine exception + a chain
//     rebuild (never a process abort) and the audio keeps flowing;
//   * three concurrent "plug-in instances" stay within the shared pool bound.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>
#include <vector>

#include "engines/world_engine.h"
#include "vst/realtime_adapter.h"

using namespace pitchlab::vst;

namespace {

/// Wall-clock CAPACITY gates (realtime factor, zero fallback at real-time
/// pacing) are machine-dependent: sanitizer builds (2-10x slower) set
/// PITCHLAB_SANITIZER=1 and skip them — the functional and never-wait
/// assertions still run there.
inline bool capacityGates() { return std::getenv("PITCHLAB_SANITIZER") == nullptr; }

struct WorldHooksOff {  // a REQUIRE abort must not leak a hook into later cases
  ~WorldHooksOff() {
    pitchlab::testing::setWorldFaultInjectionChunk(-1);
    pitchlab::testing::setWorldChunkDelayMs(0);
  }
};

constexpr double kFs = 48000.0;
constexpr int kWorldIndex = 6;  // registry position of native.world

std::vector<double> voiceLike(int64_t n, double f0) {
  std::vector<double> x(static_cast<std::size_t>(n));
  for (int64_t i = 0; i < n; ++i) {
    double v = 0.0;
    for (int h = 1; h <= 8; ++h) {
      v += std::sin(2.0 * 3.14159265358979323846 * f0 * h * static_cast<double>(i) / kFs) / h;
    }
    x[static_cast<std::size_t>(i)] = 0.25 * v;
  }
  return x;
}

struct Drive {
  RealtimeAdapter adapter;
  double worstCallMs = 0.0;

  void start(int f0Method, double pitchSt) {
    adapter.setOfflineRendering(false);  // REALTIME mode (the product default)
    adapter.activate(kFs, 2, 512);
    ParamSnapshot s;
    s.engineIndex = kWorldIndex;
    s.pitchSt = pitchSt;
    s.woF0Method = f0Method;
    adapter.setParameterSnapshot(s);
    adapter.requestHardReset();
  }

  /// `paced`: wall-clock pacing at the block period (a real audio device).
  void run(const std::vector<double>& sig, bool paced) {
    std::vector<double> l(512), r(512);
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t pos = 0; pos + 512 <= sig.size(); pos += 512) {
      if (paced) {
        const auto due = t0 + std::chrono::microseconds(
                                  static_cast<int64_t>(static_cast<double>(pos) / kFs * 1e6));
        std::this_thread::sleep_until(due);
      }
      const double* in[2] = {sig.data() + pos, sig.data() + pos};
      double* out[2] = {l.data(), r.data()};
      BlockAutomation none;
      const auto c0 = std::chrono::steady_clock::now();
      adapter.process(in, out, 512, none);
      worstCallMs = std::max(
          worstCallMs,
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count());
      for (double v : l) REQUIRE(std::isfinite(v));
    }
  }
};

}  // namespace

TEST_CASE("native.world realtime-paced drive: DIO and Harvest, stereo, no fallback") {
  initEngineRegistryOnce();
  REQUIRE(std::string(engineIdForIndex(kWorldIndex)) == "native.world");
  const auto sig = voiceLike(static_cast<int64_t>(kFs * 3.0), 160.0);
  for (int method : {0, 1}) {
    CAPTURE(method);
    Drive d;
    d.start(method, -7.0);
    d.run(sig, /*paced=*/true);
    const StatusSnapshot st = d.adapter.status();
    MESSAGE((method == 0 ? "DIO" : "Harvest")
            << ": starved=" << st.engineStarvedCalls << " underruns=" << st.deliveryUnderruns
            << " dryFb=" << st.dryFallbackFrames << " worst call " << d.worstCallMs << " ms");
    if (capacityGates()) {
      CHECK(st.faults == 0);
      CHECK(st.dryFallbackFrames == 0);
    }
    CHECK(st.chainReady);
    d.adapter.deactivate();
  }
}

TEST_CASE("native.world realtime: an overloaded pool never blocks the callback; misses are counted") {
  initEngineRegistryOnce();
  const auto sig = voiceLike(static_cast<int64_t>(kFs * 2.0), 160.0);
  WorldHooksOff guard;
  Drive d;
  d.start(0, 5.0);
  pitchlab::testing::setWorldChunkDelayMs(300);
  d.run(sig, /*paced=*/false);  // as fast as possible: the pool cannot keep up
  const StatusSnapshot overloaded = d.adapter.status();
  MESSAGE("overload: worst callback " << d.worstCallMs << " ms, starved="
                                       << overloaded.engineStarvedCalls
                                       << " underruns=" << overloaded.deliveryUnderruns);
  CHECK(d.worstCallMs < 50.0);  // the pre-fix engine waited >= 300 ms per chunk
  CHECK(overloaded.engineStarvedCalls > 0);
  CHECK(overloaded.deliveryUnderruns > 0);  // honest accounting
  CHECK(overloaded.engineExceptions == 0);
  // F2: no chain geometry sets rateFollowing today, so the rate-following
  // fault exemption cannot apply — every dry-served covered frame is also a
  // counted delivery underrun (one-to-one)
  CHECK(overloaded.dryFallbackFrames == overloaded.deliveryUnderruns);
  // recovery: the delay removed, a paced continuation stops falling back
  pitchlab::testing::setWorldChunkDelayMs(0);
  d.adapter.requestProcessingReset();
  d.run(sig, /*paced=*/true);
  const uint64_t fbBefore = d.adapter.status().dryFallbackFrames;
  d.run(sig, /*paced=*/true);
  const uint64_t fbAfter = d.adapter.status().dryFallbackFrames;
  MESSAGE("recovery: dry fallback frames in the last paced 2 s: " << (fbAfter - fbBefore));
  if (capacityGates()) CHECK(fbAfter == fbBefore);
  d.adapter.deactivate();
}

TEST_CASE("native.world realtime: a worker failure is a counted engine exception + rebuild") {
  initEngineRegistryOnce();
  const auto sig = voiceLike(static_cast<int64_t>(kFs * 1.5), 160.0);
  WorldHooksOff guard;
  Drive d;
  d.start(0, 3.0);
  pitchlab::testing::setWorldFaultInjectionChunk(4);  // global: every job's chunk 4 fails
  d.run(sig, /*paced=*/true);
  pitchlab::testing::setWorldFaultInjectionChunk(-1);
  d.run(sig, /*paced=*/true);
  const StatusSnapshot st = d.adapter.status();
  MESSAGE("engineExceptions=" << st.engineExceptions << " reprepares=" << st.reprepares);
  CHECK(st.engineExceptions >= 1);
  CHECK(st.reprepares >= 2);  // the failed chain was rebuilt
  CHECK(st.chainReady);
  d.adapter.deactivate();
}

TEST_CASE("native.world realtime: capacity probe — concurrent stereo instances (measured, not gated)" * doctest::skip()) {
  // run with --no-skip: reports dry-fallback frames for 1..4 concurrent
  // realtime-paced stereo instances per F0 method (the machine's capacity)
  initEngineRegistryOnce();
  const auto sig = voiceLike(static_cast<int64_t>(kFs * 2.0), 150.0);
  for (int method : {0, 1}) {
    for (int count = 1; count <= 4; ++count) {
      std::vector<std::thread> instances;
      std::vector<uint64_t> fb(static_cast<std::size_t>(count), 0);
      for (int i = 0; i < count; ++i) {
        instances.emplace_back([&, i] {
          Drive d;
          d.start(method, -5.0);
          d.run(sig, true);
          fb[static_cast<std::size_t>(i)] = d.adapter.status().dryFallbackFrames;
          d.adapter.deactivate();
        });
      }
      for (auto& t : instances) t.join();
      uint64_t total = 0;
      for (uint64_t v : fb) total += v;
      MESSAGE((method == 0 ? "DIO" : "Harvest") << " x" << count << " stereo: dry-fallback frames " << total);
    }
  }
}

TEST_CASE("native.world realtime: three concurrent plug-in instances stay within the pool bound") {
  initEngineRegistryOnce();
  const auto sig = voiceLike(static_cast<int64_t>(kFs * 2.0), 150.0);
  std::atomic<int> maxLive{0};
  std::atomic<bool> running{true};
  std::thread probe([&] {
    while (running.load()) {
      maxLive.store(std::max(maxLive.load(), pitchlab::testing::worldPoolLiveThreads()));
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });
  std::vector<std::thread> instances;
  std::vector<uint64_t> faults(3, 0);
  for (int i = 0; i < 3; ++i) {
    instances.emplace_back([&, i] {
      Drive d;
      d.start(/*DIO*/ 0, -5.0 + 2.0 * i);
      d.run(sig, /*paced=*/true);
      faults[static_cast<std::size_t>(i)] = d.adapter.status().faults;
      d.adapter.deactivate();
    });
  }
  for (auto& t : instances) t.join();
  running.store(false);
  probe.join();
  MESSAGE("3 instances: max live WORLD pool threads " << maxLive.load() << " (bound "
                                                      << pitchlab::testing::worldPoolSize() << ")");
  CHECK(maxLive.load() <= pitchlab::testing::worldPoolSize());
  if (capacityGates()) {
    for (uint64_t f : faults) CHECK(f == 0);
  }
  CHECK(pitchlab::testing::worldPoolLiveThreads() == 0);
}

TEST_CASE("native.world at an UNDECLARED sample rate: no chain, counted failure, finite dry output") {
  initEngineRegistryOnce();
  RealtimeAdapter adapter;
  adapter.activate(192000.0, 2, 512);
  ParamSnapshot s;
  s.engineIndex = kWorldIndex;
  s.pitchSt = 4.0;
  adapter.setParameterSnapshot(s);
  std::vector<double> sig(512), l(512), r(512);
  for (int i = 0; i < 512; ++i) sig[static_cast<std::size_t>(i)] = 0.3 * std::sin(0.05 * i);
  for (int b = 0; b < 200; ++b) {
    const double* in[2] = {sig.data(), sig.data()};
    double* out[2] = {l.data(), r.data()};
    BlockAutomation none;
    adapter.process(in, out, 512, none);
    for (double v : l) REQUIRE(std::isfinite(v));
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const StatusSnapshot st = adapter.status();
  CHECK_FALSE(st.chainReady);
  CHECK(st.preparationFailures > 0);
  adapter.deactivate();
}
