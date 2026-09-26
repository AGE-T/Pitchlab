#pragma once

// Pitch Lab — deterministic RNG facility (implementation specification §4.7).
//
// CONTRACT (v0.1, frozen 2026-09-25; implemented 2026-09-26, cycle 1):
//   * Own PCG64 implementation — NO <random>, no device entropy, no wall
//     clock (architecture §O.12; spec §4.7). Bit-exact reproducibility:
//     same seed => identical streams.
//   * One stream per (job, consumer): the job seed is mixed with a consumer
//     tag string ("engine", "curve.random-walk", "corpus", ...) via a fixed
//     64-bit mixing chain (SplitMix64 + FNV-1a-64), specified EXACTLY here
//     and pinned by golden values in tests/rng_test.cpp (T-D1) so all
//     consumers derive identically.
//
// Variant implemented (clean-room from the published PCG scheme, O'Neill):
//   PCG "setseq 128/64 XSL-RR": 128-bit LCG state, 64-bit output
//   (hi XOR lo, rotated by the top 6 state bits).
//   Constants (published PCG values):
//     multiplier = 0x2360ED051FC65DA44385B68819C503F5
//     increment  = (stream << 1) | 1   (setseq: stream selector)
//     seeding    : state=0; step; state += seed; step
//   Golden values in the unit test document THIS implementation's frozen
//   output stream (self-captured regression baseline recording the frozen
//   behaviour; they are not claims about compatibility with any other PCG
//   implementation).

#include <cstdint>

#include "core/errors.h"

namespace pitchlab {

namespace detail {

// Minimal unsigned 128-bit arithmetic (two 64-bit limbs, little-endian:
// .lo = bits 0..63, .hi = bits 64..127). Used only by the RNG.
struct u128 {
  uint64_t lo = 0;
  uint64_t hi = 0;
};

[[nodiscard]] constexpr u128 u128Add(u128 a, u128 b) {
  u128 r{};
  r.lo = a.lo + b.lo;
  r.hi = a.hi + b.hi + (r.lo < a.lo ? 1u : 0u);
  return r;
}

// 64x64 -> 128 unsigned multiply (portable, limb-based; no __int128, no
// compiler extensions — full portability under -Wpedantic).
[[nodiscard]] constexpr u128 mul64To128(uint64_t a, uint64_t b) {
  const uint64_t aL = a & 0xFFFFFFFFULL;
  const uint64_t aH = a >> 32;
  const uint64_t bL = b & 0xFFFFFFFFULL;
  const uint64_t bH = b >> 32;

  const uint64_t ll = aL * bL;  // fits 64 bits (32x32)
  const uint64_t lh = aL * bH;  // fits 64 bits (32x32)
  const uint64_t hl = aH * bL;  // fits 64 bits (32x32)
  const uint64_t hh = aH * bH;  // fits 64 bits (32x32)

  const uint64_t carryMid = (ll >> 32) + (lh & 0xFFFFFFFFULL) + (hl & 0xFFFFFFFFULL);
  return u128{(ll & 0xFFFFFFFFULL) | (carryMid << 32),
              hh + (lh >> 32) + (hl >> 32) + (carryMid >> 32)};
}

[[nodiscard]] constexpr u128 u128Mul(u128 a, u128 b) {
  // (aHi*2^64 + aLo)(bHi*2^64 + bLo) mod 2^128
  //   = aLo*bLo + (aLo*bHi + aHi*bLo) << 64   (higher terms drop mod 2^128)
  const u128 p00 = mul64To128(a.lo, b.lo);
  const u128 p01 = mul64To128(a.lo, b.hi);
  const u128 p10 = mul64To128(a.hi, b.lo);
  // Adding {lo:0, hi:pXX.lo} to p00: low word untouched; high-word wrapping
  // addition is exactly mod-2^128 semantics.
  return u128{p00.lo, p00.hi + p01.lo + p10.lo};
}

}  // namespace detail

/// SplitMix64 one-shot mixer, fixed published constants (spec §4.7).
/// Stateless mixing function; also used for corpus per-item seed derivation
/// (spec §11.4) and consumer-stream derivation below.
[[nodiscard]] uint64_t splitMix64(uint64_t x);

/// FNV-1a 64-bit hash of a NUL-terminated consumer-tag string.
/// (Chosen and frozen here as the string->64-bit tag mapping of the §4.7
/// consumer mixing; pinned by golden values in tests/rng_test.cpp.)
[[nodiscard]] uint64_t fnv1a64(const char* tag);

/// PCG64 generator (setseq 128/64 XSL-RR) — see file header.
class Pcg64 {
 public:
  /// seed128 and stream128 are the two independent 128-bit initialisation
  /// values (the stream selector becomes the LCG increment).
  Pcg64(uint64_t seedLo, uint64_t seedHi, uint64_t streamLo, uint64_t streamHi);

  /// Next raw 64-bit output (advances the state).
  [[nodiscard]] uint64_t next();

  /// Next uniform double in [0, 1): (next() >> 11) * 2^-53
  /// (53-bit mantissa, canonical bounded mapping — every representable
  /// value in [0,1) with equal probability, no rounding bias).
  [[nodiscard]] double nextDouble01();

 private:
  void step();  // state = state * M + inc

  detail::u128 state_;
  detail::u128 inc_;
};

/// Consumer-stream derivation (spec §4.7) — EXACT frozen rule:
///
///   tag     = fnv1a64(consumerTag)
///   seedLo  = splitMix64(jobSeed XOR tag)
///   seedHi  = splitMix64(seedLo)
///   streamLo= splitMix64(jobSeed + tag)     (64-bit wrapping '+')
///   streamHi= splitMix64(streamLo)
///   Pcg64(seedLo, seedHi, streamLo, streamHi)
///
/// Golden values in tests/rng_test.cpp (T-D1). Throws ConfigError on a null
/// consumerTag (field "consumerTag").
[[nodiscard]] Pcg64 makeConsumerStream(uint64_t jobSeed, const char* consumerTag);

}  // namespace pitchlab
