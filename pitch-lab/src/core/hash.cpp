#include "core/hash.h"

#include <cstring>

namespace pitchlab {
namespace {

// FIPS 180-4 section 4.2.2: first 32 bits of the fractional parts of the
// cube roots of the first 64 primes.
constexpr uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr uint32_t kRrot(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha256State {
  uint32_t h[8];
  uint64_t totalBits = 0;
  uint8_t buffer[64];
  std::size_t buffered = 0;  // bytes in buffer (0..63)
};

void compressBlock(const uint8_t* block, uint32_t h[8]) {
  uint32_t w[64];
  for (int t = 0; t < 16; ++t) {
    w[t] = (static_cast<uint32_t>(block[4 * t + 0]) << 24) |
           (static_cast<uint32_t>(block[4 * t + 1]) << 16) |
           (static_cast<uint32_t>(block[4 * t + 2]) << 8) |
           (static_cast<uint32_t>(block[4 * t + 3]));
  }
  for (int t = 16; t < 64; ++t) {
    const uint32_t s0 = kRrot(w[t - 15], 7) ^ kRrot(w[t - 15], 18) ^ (w[t - 15] >> 3);
    const uint32_t s1 = kRrot(w[t - 2], 17) ^ kRrot(w[t - 2], 19) ^ (w[t - 2] >> 10);
    w[t] = w[t - 16] + s0 + w[t - 7] + s1;
  }

  uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
  uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int t = 0; t < 64; ++t) {
    const uint32_t S1 = kRrot(e, 6) ^ kRrot(e, 11) ^ kRrot(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t t1 = hh + S1 + ch + kK[t] + w[t];
    const uint32_t S0 = kRrot(a, 2) ^ kRrot(a, 13) ^ kRrot(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = S0 + maj;
    hh = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
  h[5] += f;
  h[6] += g;
  h[7] += hh;
}

void absorb(Sha256State& st, const uint8_t* data, std::size_t n) {
  st.totalBits += static_cast<uint64_t>(n) * 8u;
  while (n > 0) {
    const std::size_t take = (st.buffered + n <= 64) ? n : (64 - st.buffered);
    std::memcpy(st.buffer + st.buffered, data, take);
    st.buffered += take;
    data += take;
    n -= take;
    if (st.buffered == 64) {
      compressBlock(st.buffer, st.h);
      st.buffered = 0;
    }
  }
}

}  // namespace

std::array<uint8_t, 32> sha256(const uint8_t* data, std::size_t byteCount) {
  Sha256State st;
  // FIPS 180-4 section 5.3.1 initial state.
  st.h[0] = 0x6a09e667u;
  st.h[1] = 0xbb67ae85u;
  st.h[2] = 0x3c6ef372u;
  st.h[3] = 0xa54ff53au;
  st.h[4] = 0x510e527fu;
  st.h[5] = 0x9b05688cu;
  st.h[6] = 0x1f83d9abu;
  st.h[7] = 0x5be0cd19u;

  absorb(st, data, byteCount);

  // Padding: 0x80, zeros, 64-bit big-endian bit length (multiple of 8 only —
  // Pitch Lab never hashes sub-byte data).
  const uint64_t bitLen = st.totalBits;
  uint8_t pad[72];
  std::size_t padLen = 0;
  pad[padLen++] = 0x80;
  const std::size_t currentBits = static_cast<std::size_t>((bitLen % 512) / 8);
  std::size_t zeroCount = (currentBits + 1 + 8 <= 64) ? (64 - currentBits - 1 - 8) : (128 - currentBits - 1 - 8);
  for (std::size_t i = 0; i < zeroCount; ++i) {
    pad[padLen++] = 0x00;
  }
  for (int i = 7; i >= 0; --i) {
    pad[padLen++] = static_cast<uint8_t>((bitLen >> (8 * i)) & 0xFFu);
  }
  absorb(st, pad, padLen);

  std::array<uint8_t, 32> digest{};
  for (int i = 0; i < 8; ++i) {
    digest[static_cast<std::size_t>(4 * i + 0)] = static_cast<uint8_t>(st.h[i] >> 24);
    digest[static_cast<std::size_t>(4 * i + 1)] = static_cast<uint8_t>(st.h[i] >> 16);
    digest[static_cast<std::size_t>(4 * i + 2)] = static_cast<uint8_t>(st.h[i] >> 8);
    digest[static_cast<std::size_t>(4 * i + 3)] = static_cast<uint8_t>(st.h[i]);
  }
  return digest;
}

std::array<uint8_t, 32> sha256Doubles(const double* values, std::size_t count) {
  // Little-endian IEEE-754 double serialisation (§4.8.1 item 8 convention).
  // Byte-by-byte construction: no type punning, no endianness assumptions
  // beyond IEEE-754 double representation (re-interpreted as uint64 via
  // memcpy — the host representation, emitted least-significant byte first).
  std::vector<uint8_t> bytes(count * 8);
  for (std::size_t i = 0; i < count; ++i) {
    uint64_t bits = 0;
    std::memcpy(&bits, &values[i], sizeof(bits));
    for (int b = 0; b < 8; ++b) {
      bytes[i * 8 + static_cast<std::size_t>(b)] = static_cast<uint8_t>((bits >> (8 * b)) & 0xFFu);
    }
  }
  return sha256(bytes.data(), bytes.size());
}

std::string toHexLower(const std::array<uint8_t, 32>& digest) {
  static const char kHex[] = "0123456789abcdef";
  std::string out(64, '0');
  for (std::size_t i = 0; i < digest.size(); ++i) {
    out[2 * i + 0] = kHex[digest[i] >> 4];
    out[2 * i + 1] = kHex[digest[i] & 0x0Fu];
  }
  return out;
}

std::string sha256Hex(const uint8_t* data, std::size_t byteCount) {
  return toHexLower(sha256(data, byteCount));
}

std::string sha256HexString(const std::string& text) {
  return sha256Hex(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

}  // namespace pitchlab
