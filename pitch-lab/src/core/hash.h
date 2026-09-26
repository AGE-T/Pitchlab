#pragma once

// Pitch Lab — own SHA-256 (FIPS 180-4, clean-room implementation).
//
// ROLE (implementation specification §4.8.1 item 8, frozen cycle 3): content
// hashes for the render manifests — input asset files (raw file bytes),
// compiled curve signals (little-endian IEEE-754 double array bytes) and
// rendered output WAVs (raw file bytes). Own implementation per the
// v0.1 dependency rule (set stays {doctest}); deterministic by construction
// (pure integer arithmetic, no environment dependence).
//
// The implementation follows the published FIPS 180-4 algorithm: message
// padding (0x80 + zeros + 64-bit big-endian bit length), 512-bit blocks,
// 64-round compression with the standard K constants (fractional parts of
// cube roots/square roots of the first primes) and initial state (fractional
// parts of square roots of the first 8 primes). The unit test pins this
// exact implementation with the standard NIST/SHA-256 test vectors
// (empty string, "abc", "abcdbcde...nopq" — the same self-captured-regression
// stance as the RNG goldens: they document THIS implementation's frozen
// output, they are not compatibility claims).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/types.h"

namespace pitchlab {

/// SHA-256 of a byte range. Returns 32 bytes (binary digest).
[[nodiscard]] std::array<uint8_t, 32> sha256(const uint8_t* data, std::size_t byteCount);

/// SHA-256 of a double array, serialised as little-endian IEEE-754 8-byte
/// values (the curve-signal hashing convention, §4.8.1 item 8).
[[nodiscard]] std::array<uint8_t, 32> sha256Doubles(const double* values, std::size_t count);

/// Lowercase hex encoding (64 characters).
[[nodiscard]] std::string toHexLower(const std::array<uint8_t, 32>& digest);

/// Convenience: hex SHA-256 of a raw byte buffer.
[[nodiscard]] std::string sha256Hex(const uint8_t* data, std::size_t byteCount);

/// Convenience: hex SHA-256 of a string's bytes.
[[nodiscard]] std::string sha256HexString(const std::string& text);

}  // namespace pitchlab
