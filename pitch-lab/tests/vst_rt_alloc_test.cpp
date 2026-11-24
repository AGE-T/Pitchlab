// Pitch Lab VST3 product layer — F1: NO heap allocation on the audio
// callback, measured on the REAL realtime adapter path for every engine and
// mode, through the lifecycle events that historically allocated (first
// frame, hard/processing resets, envelope exits -> chain rebuild + adoption,
// repeated exits). Plus F7: the per-callback latency distribution is
// populated and consistent (worst >= P99.9 >= P99).
//
// Method: a global operator new/delete pair counts allocations ONLY while
// the calling thread is inside RealtimeAdapter::process() (thread_local
// flag) — the preparation thread and the WORLD pool allocate by design and
// are not counted.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "vst/realtime_adapter.h"

namespace {
thread_local bool tInCallback = false;
std::atomic<uint64_t> gCallbackAllocs{0};
}  // namespace

void* operator new(std::size_t n) {
  if (tInCallback) gCallbackAllocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
  if (tInCallback) gCallbackAllocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  if (tInCallback) gCallbackAllocs.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(n == 0 ? 1 : n);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  if (tInCallback) gCallbackAllocs.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(n == 0 ? 1 : n);
}
#if defined(_MSC_VER)
#include <malloc.h>
inline void* plAlignedAlloc(std::size_t n, std::size_t a) { return _aligned_malloc(n == 0 ? 1 : n, a); }
inline void plAlignedFree(void* p) { _aligned_free(p); }
#else
inline void* plAlignedAlloc(std::size_t n, std::size_t a) {
  return std::aligned_alloc(a, ((n == 0 ? 1 : n) + a - 1) / a * a);
}
inline void plAlignedFree(void* p) { std::free(p); }
#endif
void* operator new(std::size_t n, std::align_val_t al) {
  if (tInCallback) gCallbackAllocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = plAlignedAlloc(n, static_cast<std::size_t>(al))) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t al) {
  if (tInCallback) gCallbackAllocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = plAlignedAlloc(n, static_cast<std::size_t>(al))) return p;
  throw std::bad_alloc();
}
void operator delete(void* p, std::align_val_t) noexcept { plAlignedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { plAlignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { plAlignedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { plAlignedFree(p); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace pitchlab::vst;

namespace {

constexpr double kFs = 48000.0;

struct Drive {
  RealtimeAdapter adapter;
  std::vector<double> sig, l, r;
  uint64_t allocs = 0;

  explicit Drive(const ParamSnapshot& s) {
    sig.resize(512);
    l.resize(512);
    r.resize(512);
    adapter.setOfflineRendering(false);  // REALTIME mode
    adapter.activate(kFs, 2, 512);
    adapter.setParameterSnapshot(s);
    adapter.requestHardReset();
  }
  ~Drive() { adapter.deactivate(); }

  void blocks(int count, double f0, BlockAutomation* automation = nullptr) {
    for (int b = 0; b < count; ++b) {
      for (int i = 0; i < 512; ++i) {
        sig[static_cast<std::size_t>(i)] =
            0.3 * std::sin(2.0 * 3.14159265358979323846 * f0 * (pos + i) / kFs);
      }
      pos += 512;
      const double* in[2] = {sig.data(), sig.data()};
      double* out[2] = {l.data(), r.data()};
      BlockAutomation none;
      BlockAutomation& a = automation != nullptr && b == 0 ? *automation : none;
      const uint64_t before = gCallbackAllocs.load();
      tInCallback = true;
      adapter.process(in, out, 512, a);
      tInCallback = false;
      allocs += gCallbackAllocs.load() - before;
      // REALTIME pacing (a real device: one 512-frame block per 10.67 ms);
      // faster pacing makes late-prepared jobs catch up inside one callback
      // and measures the harness, not the product
      next += std::chrono::microseconds(static_cast<int64_t>(512.0 / kFs * 1e6));
      std::this_thread::sleep_until(next);
    }
  }
  int64_t pos = 0;
  std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();
};

BlockAutomation pitchStep(double st) {
  BlockAutomation a;
  a.pitch[0] = AutomationPoint{0, st};
  a.pitchCount = 1;
  return a;
}

}  // namespace

TEST_CASE("F1: zero callback allocations — every engine, mode and lifecycle event") {
  initEngineRegistryOnce();
  struct Cfg {
    std::string name;
    ParamSnapshot snap;
  };
  std::vector<Cfg> cfgs;
  for (int e = 0; e < engineCount(); ++e) {
    ParamSnapshot s;
    s.engineIndex = e;
    s.pitchSt = 3.0;
    if (std::string(engineIdForIndex(e)) == "native.timepitch") {
      for (int mode = 0; mode < 4; ++mode) {
        ParamSnapshot m = s;
        m.tpMode = mode;
        cfgs.push_back({std::string(engineIdForIndex(e)) + " mode " + std::to_string(mode), m});
      }
    } else if (std::string(engineIdForIndex(e)) == "native.signalsmith") {
      cfgs.push_back({"native.signalsmith", s});
      ParamSnapshot k = s;
      k.ssPreserveFormants = true;  // the formant-base tracker path
      cfgs.push_back({"native.signalsmith keep-formants", k});
    } else {
      cfgs.push_back({engineIdForIndex(e), s});
    }
  }
  for (const Cfg& c : cfgs) {
    CAPTURE(c.name);
    Drive d(c.snap);
    d.blocks(60, 180.0);                   // first frames, adoption
    BlockAutomation up = pitchStep(10.0);  // envelope exit -> rebuild + adoption
    d.blocks(60, 180.0, &up);
    for (int k = 0; k < 4; ++k) {          // repeated exits
      BlockAutomation step = pitchStep(k % 2 == 0 ? -8.0 : 6.0);
      d.blocks(30, 200.0, &step);
    }
    d.adapter.requestProcessingReset();    // resume reset (tracker reset path)
    d.blocks(40, 160.0);
    d.adapter.requestHardReset();          // host flush
    d.blocks(40, 160.0);
    const StatusSnapshot st = d.adapter.status();
    MESSAGE(c.name << ": callback allocations " << d.allocs << ", adoptions " << st.chainsAdopted
                   << ", worst callback " << st.callbackWorstNanos / 1000.0 << " us, P99 "
                   << callbackPercentileNanos(st, 0.99) / 1000.0 << " us, P99.9 "
                   << callbackPercentileNanos(st, 0.999) / 1000.0 << " us");
    CHECK(d.allocs == 0);
    CHECK(st.chainsAdopted >= 2);  // the exits really rebuilt
    CHECK(st.chainReady);
    // the drive really ran WET (a fully starved/bypassed engine would pass
    // an allocation audit trivially): at most 10% of the frames dry-served
    CHECK(st.dryFallbackFrames < static_cast<uint64_t>(0.1 * static_cast<double>(d.pos)));
    // F7: the distribution is populated and ordered
    CHECK(st.callbackCount > 0);
    uint64_t histTotal = 0;
    for (uint64_t h : st.callbackHist) histTotal += h;
    CHECK(histTotal == st.callbackCount);
    CHECK(callbackPercentileNanos(st, 0.99) <= callbackPercentileNanos(st, 0.999));
    CHECK(callbackPercentileNanos(st, 0.999) <= st.callbackWorstNanos);
  }
}

TEST_CASE("F7: callback bucket arithmetic") {
  CHECK(callbackBucket(500) == 0);
  CHECK(callbackBucketUpperNanos(0) == 1024);
  for (uint64_t ns : {1024ull, 1500ull, 1536ull, 2047ull, 2048ull, 999999ull, 123456789ull}) {
    const int b = callbackBucket(ns);
    CHECK(ns < callbackBucketUpperNanos(b));
    if (b > 0) CHECK(ns >= callbackBucketUpperNanos(b - 1));
  }
}
