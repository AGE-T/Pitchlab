// T-D1 / T-D2 — deterministic RNG facility (implementation specification §4.7).
//
// T-D1: golden streams. The golden values below are SELF-CAPTURED from the
// pinned implementation on 2026-09-26 (GCC 14.2, -ffp-contract=off) — they are
// a regression baseline documenting the FROZEN stream behaviour, not a claim
// of compatibility with any other PCG64 implementation. The derivation is
// integer-exact (no floating point), so the values are cross-compiler stable
// by construction. Exception: fnv1a64("") == the published FNV-1a offset
// basis, which is an analytic cross-check of the FNV part.
//
// T-D2: determinism (same seed => bit-identical streams across instances) and
// consumer separation (different consumer tags / seeds => different streams).
//
// Statistical checks are sanity checks on a FIXED deterministic stream
// (bounds are generous; the stream itself is the regression baseline).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/errors.h"
#include "core/rng.h"

namespace {

uint64_t hexToU64(const char* s) {
  uint64_t v = 0;
  for (const char* p = s; *p != '\0'; ++p) {
    v <<= 4;
    char c = *p;
    if (c >= '0' && c <= '9') v |= static_cast<uint64_t>(c - '0');
    else if (c >= 'a' && c <= 'f') v |= static_cast<uint64_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= static_cast<uint64_t>(c - 'A' + 10);
  }
  return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// T-D1 — golden values (frozen behaviour documentation)
// ---------------------------------------------------------------------------

TEST_CASE("T-D1a: SplitMix64 golden values") {
  CHECK(pitchlab::splitMix64(0) == hexToU64("E220A8397B1DCDAF"));
  CHECK(pitchlab::splitMix64(1) == hexToU64("910A2DEC89025CC1"));
  CHECK(pitchlab::splitMix64(0xDEADBEEF12345678ULL) == hexToU64("4DC32B44ABDC6395"));
  CHECK(pitchlab::splitMix64(0x9E3779B97F4A7C15ULL) == hexToU64("6E789E6AA1B965F4"));
}

TEST_CASE("T-D1b: FNV-1a-64 golden values (empty string is the published offset basis)") {
  CHECK(pitchlab::fnv1a64("") == 0xCBF29CE484222325ULL);  // analytic: FNV offset basis
  CHECK(pitchlab::fnv1a64("engine") == hexToU64("3DACD07FC0CAD3DB"));
  CHECK(pitchlab::fnv1a64("curve.random-walk") == hexToU64("5E62B96B2402228D"));
  CHECK(pitchlab::fnv1a64("corpus") == hexToU64("9B7506DB4FE588A1"));
  CHECK(pitchlab::fnv1a64("granular") == hexToU64("687C4563E3FBD6D1"));
}

TEST_CASE("T-D1c: Pcg64 direct-seed golden stream") {
  pitchlab::Pcg64 r(1, 2, 3, 4);
  CHECK(r.next() == hexToU64("BDC9CDF9178CDFD7"));
  CHECK(r.next() == hexToU64("E779A7B1A6BDA813"));
  CHECK(r.next() == hexToU64("448208002E025DCE"));
  CHECK(r.next() == hexToU64("FB032E7E3B7630BB"));

  pitchlab::Pcg64 z(0, 0, 0, 0);  // all-zero initialisation must be a valid stream
  CHECK(z.next() == hexToU64("30817F62DFB893BD"));
  CHECK(z.next() == hexToU64("A097D709D367F1C5"));
  CHECK(z.next() == hexToU64("B6CF7B44BB64B306"));
  CHECK(z.next() == hexToU64("D37AC6F087E258D0"));
}

TEST_CASE("T-D1d: consumer-stream golden values (spec §4.7 mixing rule)") {
  {
    auto r = pitchlab::makeConsumerStream(42, "corpus");
    CHECK(r.next() == hexToU64("033DC2AA1CD562F2"));
    CHECK(r.next() == hexToU64("846364013CEA80E4"));
    CHECK(r.next() == hexToU64("E3DADE8B9FC73478"));
    CHECK(r.next() == hexToU64("F8BDB29FB7B921A7"));
  }
  {
    auto r = pitchlab::makeConsumerStream(42, "engine");
    CHECK(r.next() == hexToU64("F38C01900D9581BA"));
    CHECK(r.next() == hexToU64("97231BD447B3CF6C"));
    CHECK(r.next() == hexToU64("9CFFC22F58A36C3E"));
    CHECK(r.next() == hexToU64("774183D2160165A4"));
  }
  {
    auto r = pitchlab::makeConsumerStream(7, "curve.random-walk");
    CHECK(r.next() == hexToU64("ABA2288A335FC8B5"));
    CHECK(r.next() == hexToU64("7E0402D8A1DFE338"));
    CHECK(r.next() == hexToU64("5D050AF9BE062BA8"));
    CHECK(r.next() == hexToU64("A4648C06D3950B5A"));
  }
  {
    // The spec §11.4 example master seed.
    auto r = pitchlab::makeConsumerStream(0x50E5A1ABULL, "corpus");
    CHECK(r.next() == hexToU64("789535EFCF9B58EB"));
    CHECK(r.next() == hexToU64("BE443E6826F0D2C4"));
  }
}

TEST_CASE("T-D1e: nextDouble01 golden values (fixed mapping, [0,1))") {
  auto r = pitchlab::makeConsumerStream(123, "corpus");
  CHECK(r.nextDouble01() == doctest::Approx(0.89117023579074361).epsilon(1e-15));
  CHECK(r.nextDouble01() == doctest::Approx(0.11070687149901459).epsilon(1e-15));
  CHECK(r.nextDouble01() == doctest::Approx(0.38991808571192998).epsilon(1e-15));
}

// ---------------------------------------------------------------------------
// T-D2 — determinism and separation
// ---------------------------------------------------------------------------

TEST_CASE("T-D2a: same (seed, tag) => bit-identical streams across instances") {
  constexpr int kN = 1000;
  std::vector<uint64_t> a, b;
  a.reserve(kN);
  b.reserve(kN);
  auto ra = pitchlab::makeConsumerStream(987654321ULL, "curve.random-walk");
  auto rb = pitchlab::makeConsumerStream(987654321ULL, "curve.random-walk");
  for (int i = 0; i < kN; ++i) {
    a.push_back(ra.next());
    b.push_back(rb.next());
  }
  CHECK(std::memcmp(a.data(), b.data(), kN * sizeof(uint64_t)) == 0);
}

TEST_CASE("T-D2b: different consumer tags => different streams (no cross-talk)") {
  constexpr int kN = 64;
  const char* tags[] = {"engine", "curve.random-walk", "corpus", "granular", "metrics", ""};
  std::vector<std::vector<uint64_t>> streams;
  for (const char* tag : tags) {
    auto r = pitchlab::makeConsumerStream(0xABCD1234ULL, tag);
    std::vector<uint64_t> s;
    s.reserve(kN);
    for (int i = 0; i < kN; ++i) s.push_back(r.next());
    streams.push_back(std::move(s));
  }
  for (std::size_t i = 0; i < streams.size(); ++i) {
    for (std::size_t j = i + 1; j < streams.size(); ++j) {
      // Entire 64-sample windows must differ (not merely the first word).
      CHECK(std::memcmp(streams[i].data(), streams[j].data(), kN * sizeof(uint64_t)) != 0);
    }
  }
}

TEST_CASE("T-D2c: different job seeds => different streams") {
  auto r1 = pitchlab::makeConsumerStream(1, "corpus");
  auto r2 = pitchlab::makeConsumerStream(2, "corpus");
  bool anyDifferent = false;
  for (int i = 0; i < 100; ++i) {
    if (r1.next() != r2.next()) anyDifferent = true;
  }
  CHECK(anyDifferent);  // trivially true for any sane generator; frozen behaviour
}

TEST_CASE("T-D2d: null consumer tag is a config error") {
  bool threw = false;
  try {
    (void)pitchlab::makeConsumerStream(1, nullptr);
  } catch (const pitchlab::ConfigError& e) {
    threw = true;
    CHECK(e.field() == "consumerTag");
  }
  CHECK(threw);
}

// ---------------------------------------------------------------------------
// Statistical sanity on the fixed stream (sanity checks, not proofs)
// ---------------------------------------------------------------------------

TEST_CASE("T-D2e: uniform doubles in [0,1), plausible moments") {
  auto r = pitchlab::makeConsumerStream(42, "corpus");
  constexpr int kN = 100000;
  double sum = 0.0, sum2 = 0.0;
  double minV = 1.0, maxV = 0.0;
  for (int i = 0; i < kN; ++i) {
    const double v = r.nextDouble01();
    CHECK(v >= 0.0);
    CHECK(v < 1.0);
    sum += v;
    sum2 += v * v;
    if (v < minV) minV = v;
    if (v > maxV) maxV = v;
  }
  const double mean = sum / kN;
  const double var = sum2 / kN - mean * mean;
  // Uniform[0,1): mean 0.5, variance 1/12 = 0.08333..; generous bounds on the
  // FIXED stream (documented 2026-09-26 measurement: mean 0.499838, var 0.083872).
  CHECK(mean > 0.49);
  CHECK(mean < 0.51);
  CHECK(var > 0.080);
  CHECK(var < 0.087);
  CHECK(minV < 0.001);   // dense coverage of the interval on 100k draws
  CHECK(maxV > 0.999);
}

TEST_CASE("T-D2f: bit balance of raw outputs") {
  auto r = pitchlab::makeConsumerStream(42, "engine");
  constexpr int kN = 100000;
  int ones[64] = {0};
  for (int i = 0; i < kN; ++i) {
    const uint64_t v = r.next();
    for (int b = 0; b < 64; ++b) ones[b] += static_cast<int>((v >> b) & 1ULL);
  }
  // Fixed-stream measurement (2026-09-26): all bit-position ones-fractions
  // within [0.4973, 0.5053]; bound at [0.49, 0.51] (±6 sigma of binomial).
  for (int b = 0; b < 64; ++b) {
    const double f = static_cast<double>(ones[b]) / kN;
    CHECK(f > 0.49);
    CHECK(f < 0.51);
  }
}

// ---------------------------------------------------------------------------
// u128 arithmetic (analytic goldens — exact math, not self-captured)
// ---------------------------------------------------------------------------

TEST_CASE("T-D1f: 128-bit arithmetic analytic goldens (detail::u128)") {
  using pitchlab::detail::u128;
  using pitchlab::detail::u128Add;
  using pitchlab::detail::u128Mul;

  // (2^64-1) + 1 = 2^64  -> {lo:0, hi:1}
  const auto s1 = u128Add(u128{0xFFFFFFFFFFFFFFFFULL, 0}, u128{1, 0});
  CHECK(s1.lo == 0);
  CHECK(s1.hi == 1);
  // Carry propagation into the high limb.
  const auto s2 = u128Add(u128{0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
                          u128{2, 0});
  CHECK(s2.lo == 1);
  CHECK(s2.hi == 0);

  const auto r1 = u128Mul(u128{0xFFFFFFFFFFFFFFFFULL, 0}, u128{0xFFFFFFFFFFFFFFFFULL, 0});
  CHECK(r1.lo == 0x0000000000000001ULL);  // (2^64-1)^2 = 2^128 - 2^65 + 1
  CHECK(r1.hi == 0xFFFFFFFFFFFFFFFEULL);

  const auto r2 = u128Mul(u128{0x8000000000000000ULL, 0}, u128{0x8000000000000000ULL, 0});
  CHECK(r2.lo == 0x0000000000000000ULL);  // 2^63 * 2^63 = 2^126
  CHECK(r2.hi == 0x4000000000000000ULL);

  const auto r3 = u128Mul(u128{3, 0}, u128{5, 0});
  CHECK(r3.lo == 15);
  CHECK(r3.hi == 0);

  // (2^64-1) * (2^65-1) = 2^129 - 2^64 - 2^65 + 1  ==  (2^64-3)*2^64 + 1 (mod 2^128)
  const auto r4 = u128Mul(u128{0xFFFFFFFFFFFFFFFFULL, 0}, u128{0xFFFFFFFFFFFFFFFFULL, 1});
  CHECK(r4.lo == 0x0000000000000001ULL);
  CHECK(r4.hi == 0xFFFFFFFFFFFFFFFDULL);
}
