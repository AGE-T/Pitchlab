#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <cmath>
#include <cstdio>
#include <thread>
#include <chrono>
#include <vector>
#include "vst/realtime_adapter.h"
using namespace pitchlab::vst;
static constexpr double kPi = 3.14159265358979323846;
static StatusSnapshot driveAt(double fs, int32_t block, const ParamSnapshot& snap0, int64_t seconds, double grain) {
  ParamSnapshot snap = snap0; snap.grGrainSec = grain;
  RealtimeAdapter adapter;
  adapter.activate(fs, 2, block);
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();
  const int64_t total = static_cast<int64_t>(fs * seconds);
  std::vector<double> sig(static_cast<std::size_t>(total));
  for (int64_t i = 0; i < total; ++i)
    sig[static_cast<std::size_t>(i)] = 0.5 * std::sin(2.0 * kPi * 220.0 * i / fs);
  std::vector<double> o0(static_cast<std::size_t>(total), 0.0), o1(static_cast<std::size_t>(total), 0.0);
  int64_t cpuNs = 0, pos = 0;
  while (pos < total) {
    const int32_t take = static_cast<int32_t>(std::min<int64_t>(block, total - pos));
    const double* in[2] = {sig.data() + pos, sig.data() + pos};
    double* o[2] = {o0.data() + pos, o1.data() + pos};
    BlockAutomation none;
    const auto t0 = std::chrono::steady_clock::now();
    adapter.process(in, o, take, none);
    const auto t1 = std::chrono::steady_clock::now();
    cpuNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    pos += take;
    std::this_thread::sleep_for(std::chrono::microseconds(250));
  }
  auto st = adapter.status();
  const double audioNs = static_cast<double>(total) / fs * 1e9;
  st.reprepares = static_cast<uint64_t>(audioNs > 0 ? static_cast<double>(cpuNs) / audioNs * 1000 : 0);  // RTF*1000 smuggled
  adapter.deactivate();
  return st;
}
TEST_CASE("rtf survey") {
  for (auto cfg : std::vector<std::tuple<int, double, double, int>>{
           {4, -12.0, 0.1, 96000}, {4, 12.0, 0.1, 96000}, {4, -12.0, 0.5, 96000},
           {4, 12.0, 0.5, 96000}, {4, -12.0, 0.5, 192000}, {0, 12.0, 0.1, 96000},
           {0, -12.0, 0.1, 192000}, {0, 48.0, 0.1, 48000}}) {
    ParamSnapshot s;
    s.engineIndex = std::get<0>(cfg);
    s.pitchSt = std::get<1>(cfg);
    const auto st = driveAt(96000.0 * 0 + std::get<3>(cfg), 128, s, 3, std::get<2>(cfg));
    std::printf("eng=%d pitch=%+.0f grain=%.1f fs=%d rtf=%.3f faults=%llu\n",
                std::get<0>(cfg), std::get<1>(cfg), std::get<2>(cfg), std::get<3>(cfg),
                st.reprepares / 1000.0, (unsigned long long)st.faults);
  }
}
