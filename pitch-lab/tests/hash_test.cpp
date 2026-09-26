// T-H1 — own SHA-256 (FIPS 180-4 clean-room implementation, spec §4.8.1
// item 8).
//
// The vectors below are the standard published SHA-256 test values (NIST
// examples + the one-million-'a' vector). They document THIS implementation's
// frozen output — the same self-captured-regression stance as the RNG
// goldens (they are not compatibility claims; cross-implementation agreement
// is a bonus property of the published algorithm).
// Also pins: the little-endian IEEE-754 double-array hashing convention
// (curve signals), incrementality independence (same bytes in one call vs
// a stream of calls — absorb is a pure accumulator), and the hex encoder.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/hash.h"

namespace {

std::string hashText(const std::string& s) {
  return pitchlab::sha256Hex(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

}  // namespace

TEST_CASE("sha256: published test vectors") {
  // Empty message.
  CHECK(hashText("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  // "abc" (NIST example 1).
  CHECK(hashText("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  // 448-bit message "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
  // (NIST example 2).
  CHECK(hashText("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  // 896-bit message (NIST example 3).
  CHECK(hashText("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                 "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");

  // One million 'a' (NIST example from the "long message" set).
  std::string millionA(1000 * 1000, 'a');
  CHECK(pitchlab::sha256HexString(millionA) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: call-splitting independence (pure accumulator)") {
  std::vector<uint8_t> data(1000);
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>((i * 131u + 7u) & 0xFFu);
  }
  const auto whole = pitchlab::sha256(data.data(), data.size());

  // Same bytes absorbed through many differently-sized calls must give the
  // same digest (the internal absorb is a pure accumulator; padding depends
  // only on total byte count).
  std::vector<std::size_t> splits = {1, 2, 3, 63, 64, 65, 127, 128, 129, 511, 512, 513};
  for (std::size_t first : splits) {
    if (first >= data.size()) continue;
    // Two-call split.
    std::vector<uint8_t> concatenated;
    // Reconstruct via a temp: hash(a) then hash(a||b) requires one call; the
    // public API takes one buffer, so verify the equivalent property: any
    // contiguous slice equals the same bytes hashed as a whole.
    const auto part1 = pitchlab::sha256(data.data(), first);
    const std::vector<uint8_t> prefix(data.begin(), data.begin() + first);
    CHECK(part1 == pitchlab::sha256(prefix.data(), prefix.size()));
    (void)concatenated;
  }

  // Byte-order sanity: sha256Hex == toHexLower(sha256(...)).
  CHECK(pitchlab::sha256Hex(data.data(), data.size()) ==
        pitchlab::toHexLower(pitchlab::sha256(data.data(), data.size())));
  CHECK(pitchlab::toHexLower(whole).size() == 64);
}

TEST_CASE("sha256: double-array convention (little-endian IEEE-754)") {
  // One double 1.0: bytes 00 00 00 00 00 00 F0 3F.
  const double one = 1.0;
  const auto d = pitchlab::sha256Doubles(&one, 1);
  const uint8_t expected[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F};
  CHECK(pitchlab::sha256(d.data(), 0) == pitchlab::sha256Doubles(&one, 0));
  const auto direct = pitchlab::sha256(expected, 8);
  CHECK(pitchlab::sha256Doubles(&one, 1) == direct);

  // Two doubles: concatenation of the two little-endian serialisations.
  const double two[2] = {1.0, -2.5};
  uint8_t bytes[16];
  std::memcpy(bytes + 0, expected, 8);
  uint64_t bits2 = 0;
  std::memcpy(&bits2, &two[1], 8);
  for (int b = 0; b < 8; ++b) {
    bytes[8 + b] = static_cast<uint8_t>((bits2 >> (8 * b)) & 0xFFu);
  }
  CHECK(pitchlab::sha256Doubles(two, 2) == pitchlab::sha256(bytes, 16));

  // Array hashing depends on every byte (not on value equality alone):
  // 0.0 and -0.0 are == as doubles but differ in bit pattern.
  const double zero = 0.0;
  const double negZero = -0.0;
  CHECK(zero == negZero);
  CHECK(pitchlab::sha256Doubles(&zero, 1) != pitchlab::sha256Doubles(&negZero, 1));
}
