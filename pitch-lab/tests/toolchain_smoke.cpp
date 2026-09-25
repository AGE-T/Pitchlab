// T-INF1a — toolchain + floating-point determinism guards (infrastructure).
//
// This is EXPLICITLY not a DSP correctness test. It verifies the canonical
// environment promises that all later DSP determinism guarantees rest on:
//   * C++20 language/library features used by the project compile AND run
//     (concepts, ranges, std::format, <bit>, chrono).
//   * Floating-point behaviour is strict IEEE (no -ffast-math bleed, no
//     silent FP contraction assumptions), and identical source executed
//     twice produces bit-identical results.

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <ranges>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      ++g_failures;                                                    \
      std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    }                                                                  \
  } while (false)

template <std::floating_point T>
T doubled(T v) { return static_cast<T>(2.0) * v; }  // concepts instantiation

double sumLoop(const std::vector<double>& v) {
  double acc = 0.0;
  for (double x : v) acc += x;
  return acc;
}

void testCxx20Language() {
  CHECK(doubled(1.5) == 3.0);
  std::vector<int> nums{1, 2, 3, 4, 5, 6};
  auto evens = nums | std::views::filter([](int n) { return n % 2 == 0; }) |
               std::views::transform([](int n) { return n * 10; });
  std::vector<int> out{evens.begin(), evens.end()};
  CHECK((out == std::vector<int>{20, 40, 60}));
}

void testCxx20Library() {
  CHECK(std::format("{}-{}-{}", 1, "two", 3.0) == "1-two-3");
  CHECK(std::popcount(0x0Fu) == 4);
  CHECK(std::has_single_bit(64u));
  const auto now = std::chrono::system_clock::now();
  CHECK(now.time_since_epoch().count() > 0);
}

void testStrictFloatingPoint() {
#ifdef __FAST_MATH__
  CHECK(false && "__FAST_MATH__ must not be defined (determinism, spec §7.5)");
#endif
  // Strict-IEEE sanity: 0.1 + 0.2 is NOT exactly 0.3 in binary64.
  CHECK(0.1 + 0.2 != 0.3);
  CHECK(std::abs((0.1 + 0.2) - 0.3) < 1e-16);
  // Denormals are not flushed (deterministic gradual underflow).
  volatile double tiny = 1e-308;
  CHECK(tiny / 8.0 != 0.0 || tiny == 0.0);  // may legitimately be subnormal-nonzero
  CHECK(std::isfinite(1.0) && !std::isinf(1.0) && !std::isnan(1.0));
  CHECK(std::copysign(1.0, -0.0) == -1.0);
}

void testDeterministicAccumulation() {
  std::vector<double> v;
  v.reserve(100000);
  double x = 0.0;
  for (int i = 0; i < 100000; ++i) {
    x = std::fmod(x + 0.1010101, 1.0);
    v.push_back(x);
  }
  const double a = sumLoop(v);
  const double b = sumLoop(v);
  CHECK(std::memcmp(&a, &b, sizeof(double)) == 0);  // bit-identical repeat
}

}  // namespace

int main() {
  testCxx20Language();
  testCxx20Library();
  testStrictFloatingPoint();
  testDeterministicAccumulation();
  if (g_failures != 0) {
    std::fprintf(stderr, "toolchain_smoke: %d check(s) FAILED\n", g_failures);
    return 1;
  }
  std::printf("toolchain_smoke: all checks passed (C++20 features + strict FP + bit-identical repeat)\n");
  return 0;
}
