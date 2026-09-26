#include "core/rng.h"

namespace {

// PCG 128-bit LCG multiplier: 0x2360ED051FC65DA44385B68819C503F5
constexpr pitchlab::detail::u128 kPcgMultiplier{0x4385B68819C503F5ULL, 0x2360ED051FC65DA4ULL};

}  // namespace

namespace pitchlab {

uint64_t splitMix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

uint64_t fnv1a64(const char* tag) {
  uint64_t h = 0xCBF29CE484222325ULL;
  for (const char* p = tag; *p != '\0'; ++p) {
    h ^= static_cast<uint64_t>(static_cast<unsigned char>(*p));
    h *= 0x100000001B3ULL;
  }
  return h;
}

Pcg64::Pcg64(uint64_t seedLo, uint64_t seedHi, uint64_t streamLo, uint64_t streamHi) {
  // setseq: increment = (stream << 1) | 1 (128-bit shift with limb carry).
  inc_ = detail::u128{(streamLo << 1) | 1ULL, (streamHi << 1) | (streamLo >> 63)};
  state_ = detail::u128{0, 0};
  step();
  state_ = detail::u128Add(state_, detail::u128{seedLo, seedHi});
  step();
}

void Pcg64::step() { state_ = detail::u128Add(detail::u128Mul(state_, kPcgMultiplier), inc_); }

uint64_t Pcg64::next() {
  step();
  // XSL-RR 128->64: value = hi XOR lo; rotation count = top 6 state bits.
  const unsigned rot = static_cast<unsigned>(state_.hi >> 58);
  const uint64_t value = state_.hi ^ state_.lo;
  return (value >> rot) | (value << ((64u - rot) & 63u));
}

double Pcg64::nextDouble01() {
  return static_cast<double>(next() >> 11) * 0x1.0p-53;
}

Pcg64 makeConsumerStream(uint64_t jobSeed, const char* consumerTag) {
  if (consumerTag == nullptr) {
    throw ConfigError("", "consumerTag", "consumer tag string must not be null");
  }
  const uint64_t tag = fnv1a64(consumerTag);
  const uint64_t seedLo = splitMix64(jobSeed ^ tag);
  const uint64_t seedHi = splitMix64(seedLo);
  const uint64_t streamLo = splitMix64(jobSeed + tag);
  const uint64_t streamHi = splitMix64(streamLo);
  return Pcg64(seedLo, seedHi, streamLo, streamHi);
}

}  // namespace pitchlab
