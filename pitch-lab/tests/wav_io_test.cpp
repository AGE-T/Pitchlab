// T-W1 / T-W2 / T-W3 — WAV I/O (implementation specification §9).
//
// T-W1: round-trips — float64 bit-exact (mono/stereo/multichannel-extensible),
//       float32 exact through the f64->f32 conversion, 192 kHz + multichannel
//       extensible round-trip (architecture §L unit-test mandate), supported
//       sample-rate set, odd frame counts, metadata fidelity.
// T-W2: deterministic writer bytes — same content => byte-identical files;
//       chunk layout is exactly fmt/fact/data (frozen deterministic layout).
// T-W3: invalid-input rejection — every §9 rejection class with the mandated
//       integer-PCM message, plus the 4 GiB RIFF cap (pure arithmetic path —
//       a real >4 GiB buffer is NOT allocated in CI; documented honestly).
//
// Empirical note: all bounds here are exact-value or structural assertions
// (no tolerances needed — the module is exact-by-construction).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "core/errors.h"
#include "core/rng.h"
#include "core/wav_io.h"

namespace {

const std::filesystem::path kTestDir =
    std::filesystem::temp_directory_path() / "pitchlab-wav-test";

std::filesystem::path testFile(const char* name) {
  std::filesystem::create_directories(kTestDir);
  return kTestDir / name;
}

std::vector<std::vector<double>> makePlanar(int channels, int frames, uint64_t seed) {
  auto rng = pitchlab::makeConsumerStream(seed, "wav-test");
  std::vector<std::vector<double>> chs(static_cast<std::size_t>(channels));
  for (auto& c : chs) {
    c.reserve(static_cast<std::size_t>(frames));
    for (int f = 0; f < frames; ++f) {
      c.push_back(rng.nextDouble01() * 2.0 - 1.0);
    }
  }
  return chs;
}

const double* const* asViews(const std::vector<std::vector<double>>& chs) {
  static thread_local std::vector<const double*> views;
  views.clear();
  for (const auto& c : chs) views.push_back(c.data());
  return views.data();
}

std::vector<unsigned char> readFileBytes(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  REQUIRE(in);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

// --- byte-crafting helpers for invalid-file fixtures (T-W3) ---

void u16(std::vector<unsigned char>& b, uint16_t v) {
  b.push_back(static_cast<unsigned char>(v & 0xFF));
  b.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}
void u32(std::vector<unsigned char>& b, uint32_t v) {
  b.push_back(static_cast<unsigned char>(v & 0xFF));
  b.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
  b.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
  b.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
}
void tag(std::vector<unsigned char>& b, const char* t) {
  b.insert(b.end(), t, t + 4);
}

// Minimal valid float-WAV body builder (fmt tag 3, 16-byte fmt).
struct FmtSpec {
  uint16_t tag = 3;
  uint16_t channels = 1;
  uint32_t rate = 48000;
  uint16_t bits = 64;
  uint16_t blockAlign = 0;  // 0 => channels * bits/8
  uint32_t byteRate = 0;    // 0 => rate * blockAlign
};

std::vector<unsigned char> buildWav(const FmtSpec& fmt, uint32_t dataBytes,
                                    const unsigned char* data = nullptr) {
  const uint16_t blockAlign = fmt.blockAlign != 0 ? fmt.blockAlign
                                                  : static_cast<uint16_t>(fmt.channels * fmt.bits / 8);
  const uint32_t byteRate =
      fmt.byteRate != 0 ? fmt.byteRate
                        : static_cast<uint32_t>(fmt.rate) * blockAlign;
  std::vector<unsigned char> b;
  tag(b, "RIFF");
  u32(b, 4 + 8 + 16 + 8 + dataBytes);  // WAVE + fmt + data
  tag(b, "WAVE");
  tag(b, "fmt ");
  u32(b, 16);
  u16(b, fmt.tag);
  u16(b, fmt.channels);
  u32(b, fmt.rate);
  u32(b, byteRate);
  u16(b, blockAlign);
  u16(b, fmt.bits);
  tag(b, "data");
  u32(b, dataBytes);
  // Payload: caller-provided bytes, or dataBytes zero bytes (a physically
  // complete chunk) when data == nullptr.
  b.insert(b.end(), static_cast<std::size_t>(dataBytes), 0);
  if (data != nullptr) {
    std::memcpy(b.data() + b.size() - dataBytes, data, dataBytes);
  }
  return b;
}

void writeFileBytes(const std::filesystem::path& p, const std::vector<unsigned char>& bytes) {
  std::filesystem::create_directories(kTestDir);
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  REQUIRE(out);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

bool throwsConfigErrorWith(const std::filesystem::path& p, const char* needle) {
  try {
    (void)pitchlab::readWav(p);
  } catch (const pitchlab::ConfigError& e) {
    return std::string(e.what()).find(needle) != std::string::npos;
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// T-W1 — round-trip correctness
// ---------------------------------------------------------------------------

TEST_CASE("T-W1a: float64 mono round-trip is bit-exact (incl. odd frame count)") {
  for (const int frames : {1, 2, 101, 4096}) {
    auto data = makePlanar(1, frames, 1000 + frames);
    const auto path = testFile("mono64.wav");
    pitchlab::writeWav(path, asViews(data), 1, frames, 48000,
                       pitchlab::WavSampleFormat::Float64);
    auto back = pitchlab::readWav(path);
    CHECK(back.meta.channels == 1);
    CHECK(back.meta.frames == frames);
    CHECK(back.meta.sampleRate == 48000);
    CHECK(back.meta.bitsPerSample == 64);
    CHECK(back.meta.extensible == false);
    CHECK(back.meta.channelMask == 0);
    REQUIRE(back.channels.size() == 1);
    REQUIRE(back.channels[0].size() == static_cast<std::size_t>(frames));
    CHECK(std::memcmp(back.channels[0].data(), data[0].data(),
                      static_cast<std::size_t>(frames) * sizeof(double)) == 0);
  }
}

TEST_CASE("T-W1b: float64 stereo round-trip is bit-exact (channel order preserved)") {
  const int frames = 3000;
  auto data = makePlanar(2, frames, 42);
  const auto path = testFile("stereo64.wav");
  pitchlab::writeWav(path, asViews(data), 2, frames, 44100,
                     pitchlab::WavSampleFormat::Float64);
  auto back = pitchlab::readWav(path);
  CHECK(back.meta.channels == 2);
  CHECK(back.meta.extensible == false);
  REQUIRE(back.channels.size() == 2);
  CHECK(std::memcmp(back.channels[0].data(), data[0].data(), frames * sizeof(double)) == 0);
  CHECK(std::memcmp(back.channels[1].data(), data[1].data(), frames * sizeof(double)) == 0);
}

TEST_CASE("T-W1c: 192 kHz + 6-channel extensible round-trip is bit-exact (§L mandate)") {
  const int frames = 777;
  auto data = makePlanar(6, frames, 7);
  const auto path = testFile("mc6-192k.wav");
  pitchlab::writeWav(path, asViews(data), 6, frames, 192000,
                     pitchlab::WavSampleFormat::Float64);
  auto back = pitchlab::readWav(path);
  CHECK(back.meta.sampleRate == 192000);
  CHECK(back.meta.channels == 6);
  CHECK(back.meta.bitsPerSample == 64);
  CHECK(back.meta.extensible == true);
  CHECK(back.meta.channelMask == 0x3F);  // 5.1 default mask, recorded verbatim
  REQUIRE(back.channels.size() == 6);
  for (int c = 0; c < 6; ++c) {
    CHECK(std::memcmp(back.channels[c].data(), data[c].data(), frames * sizeof(double)) == 0);
  }
}

TEST_CASE("T-W1d: float32 3-channel extensible round-trip equals exact f64->f32 conversion") {
  const int frames = 512;
  auto data = makePlanar(3, frames, 99);
  const auto path = testFile("mc3-32.wav");
  pitchlab::writeWav(path, asViews(data), 3, frames, 96000,
                     pitchlab::WavSampleFormat::Float32);
  auto back = pitchlab::readWav(path);
  CHECK(back.meta.bitsPerSample == 32);
  CHECK(back.meta.extensible == true);
  CHECK(back.meta.channelMask == 0x7);
  REQUIRE(back.channels.size() == 3);
  for (int c = 0; c < 3; ++c) {
    for (int f = 0; f < frames; ++f) {
      const double expected = static_cast<double>(static_cast<float>(data[c][f]));
      CHECK(back.channels[c][f] == expected);
    }
  }
}

TEST_CASE("T-W1e: f64->f32 export conversion is IEEE round-to-nearest-even (tie cases)") {
  // Around 1.0 the float grid spacing is 2^-23 (= 1/8388608). Cases:
  //   1 + 2^-24   = 1 + 1/2^24: midpoint of 1.0f (mantissa 0, EVEN) and
  //                 1.0f + 2^-23 (mantissa 1, odd) -> ties-to-even rounds
  //                 DOWN to 1.0.
  //   1 + 3*2^-24 = 1 + 3/2^24: midpoint of 1.0f + 2^-23 (mantissa 1, odd)
  //                 and 1.0f + 2^-22 (mantissa 2, EVEN) -> ties-to-even
  //                 rounds UP to 1.0f + 2^-22 = 1 + 4/2^24.
  //   1 + 2^-23   = 1 + 2/2^24: exactly a float -> unchanged.
  const double tieDown = 1.0 + 1.0 / 16777216.0;             // 1 + 2^-24
  const double tieUp = 1.0 + 3.0 / 16777216.0;               // 1 + 3*2^-24
  const double gridAbove = 1.0 + 4.0 / 16777216.0;           // 1 + 2^-22 (even mantissa)
  const double gridExact = 1.0 + 2.0 / 16777216.0;           // 1 + 2^-23 (a float)
  std::vector<double> ch1 = {tieDown, tieUp, gridExact};
  const double* views[1] = {ch1.data()};
  const auto path = testFile("ties.wav");
  pitchlab::writeWav(path, views, 1, 3, 48000, pitchlab::WavSampleFormat::Float32);
  auto back = pitchlab::readWav(path);
  REQUIRE(back.channels[0].size() == 3);
  CHECK(back.channels[0][0] == 1.0);         // tie -> even neighbour below
  CHECK(back.channels[0][1] == gridAbove);   // tie -> even neighbour above (1+2^-22)
  CHECK(back.channels[0][2] == gridExact);   // exactly representable -> unchanged
}

TEST_CASE("T-W1f: all six supported sample rates round-trip verbatim (§8 set)") {
  for (const uint32_t rate : {44100u, 48000u, 88200u, 96000u, 176400u, 192000u}) {
    auto data = makePlanar(2, 64, rate);
    const auto path = testFile("rates.wav");
    pitchlab::writeWav(path, asViews(data), 2, 64, rate,
                       pitchlab::WavSampleFormat::Float64);
    auto back = pitchlab::readWav(path);
    CHECK(back.meta.sampleRate == rate);
  }
}

TEST_CASE("T-W1g: zero-frame file is representable and round-trips") {
  std::vector<double> empty;
  const double* views[1] = {empty.data()};
  const auto path = testFile("empty.wav");
  pitchlab::writeWav(path, views, 1, 0, 48000, pitchlab::WavSampleFormat::Float64);
  auto back = pitchlab::readWav(path);
  CHECK(back.meta.frames == 0);
  CHECK(back.channels.size() == 1);
  CHECK(back.channels[0].empty());
}

// ---------------------------------------------------------------------------
// T-W2 — deterministic writer bytes
// ---------------------------------------------------------------------------

TEST_CASE("T-W2a: same content => byte-identical files (no timestamps, no tags)") {
  const int frames = 4096;
  auto data = makePlanar(2, frames, 2024);
  const auto p1 = testFile("det1.wav");
  const auto p2 = testFile("det2.wav");
  pitchlab::writeWav(p1, asViews(data), 2, frames, 48000, pitchlab::WavSampleFormat::Float64);
  pitchlab::writeWav(p2, asViews(data), 2, frames, 48000, pitchlab::WavSampleFormat::Float64);
  CHECK(readFileBytes(p1) == readFileBytes(p2));
}

TEST_CASE("T-W2b: frozen chunk layout is exactly fmt, fact, data") {
  auto data = makePlanar(1, 5, 3);
  const auto path = testFile("layout.wav");
  pitchlab::writeWav(path, asViews(data), 1, 5, 48000, pitchlab::WavSampleFormat::Float64);
  const auto bytes = readFileBytes(path);
  REQUIRE(bytes.size() > 44);
  CHECK(std::memcmp(bytes.data(), "RIFF", 4) == 0);
  CHECK(std::memcmp(bytes.data() + 8, "WAVE", 4) == 0);
  CHECK(std::memcmp(bytes.data() + 12, "fmt ", 4) == 0);   // chunk 1: fmt (16 bytes)
  CHECK(std::memcmp(bytes.data() + 36, "fact", 4) == 0);   // chunk 2: fact (4 bytes)
  CHECK(std::memcmp(bytes.data() + 48, "data", 4) == 0);   // chunk 3: data
  // RIFF size == fileSize - 8 (strict rule our own writer satisfies).
  const uint32_t riff =
      bytes[4] | (bytes[5] << 8) | (bytes[6] << 16) | (static_cast<uint32_t>(bytes[7]) << 24);
  CHECK(riff == bytes.size() - 8);
  // data size == frames * frameSize.
  const uint32_t dsz =
      bytes[52] | (bytes[53] << 8) | (bytes[54] << 16) | (static_cast<uint32_t>(bytes[55]) << 24);
  CHECK(dsz == 5 * 8);
  // total size matches the pure computation.
  CHECK(bytes.size() == pitchlab::computeWavFileSize(5, 1, pitchlab::WavSampleFormat::Float64));
}

// ---------------------------------------------------------------------------
// T-W3 — invalid input rejection (reader) + writer guards + size cap
// ---------------------------------------------------------------------------

TEST_CASE("T-W3a: integer PCM rejected with the mandated message") {
  FmtSpec pcm;
  pcm.tag = 1;
  pcm.bits = 16;
  pcm.channels = 1;
  std::vector<unsigned char> body(4, 0);
  const auto bytes = buildWav(pcm, 4, body.data());
  const auto path = testFile("pcm16.wav");
  writeFileBytes(path, bytes);
  CHECK(throwsConfigErrorWith(path, "integer PCM not accepted"));
  // Also the plain message form from the spec text:
  try {
    (void)pitchlab::readWav(path);
    CHECK(false);
  } catch (const pitchlab::ConfigError& e) {
    CHECK(std::string(e.what()).find("author assets as IEEE float") != std::string::npos);
  }
}

TEST_CASE("T-W3b: unknown/compressed format tags rejected") {
  for (const uint16_t badTag : {uint16_t(2), uint16_t(6), uint16_t(7), uint16_t(0x55)}) {
    FmtSpec f;
    f.tag = badTag;
    const auto bytes = buildWav(f, 8);
    const auto path = testFile("badtag.wav");
    writeFileBytes(path, bytes);
    CHECK(throwsConfigErrorWith(path, "unsupported WAVE format tag"));
  }
}

TEST_CASE("T-W3c: extensible with non-float subformat rejected") {
  // Extensible fmt with the PCM subformat GUID.
  const std::vector<unsigned char> pcmGuid = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
                                              0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38,
                                              0x9B, 0x71};
  std::vector<unsigned char> b;
  tag(b, "RIFF");
  u32(b, 4 + 8 + 40 + 8 + 8);
  tag(b, "WAVE");
  tag(b, "fmt ");
  u32(b, 40);
  u16(b, 0xFFFE);
  u16(b, 2);
  u32(b, 48000);
  u32(b, 48000 * 16);
  u16(b, 16);
  u16(b, 64);
  u16(b, 22);
  u16(b, 64);
  u32(b, 0x3);
  b.insert(b.end(), pcmGuid.begin(), pcmGuid.end());
  tag(b, "data");
  u32(b, 8);
  b.insert(b.end(), 8, 0);
  const auto path = testFile("ext-pcm.wav");
  writeFileBytes(path, b);
  CHECK(throwsConfigErrorWith(path, "KSDATAFORMAT_SUBTYPE_IEEE_FLOAT"));
}

TEST_CASE("T-W3d: truncated data chunk rejected") {
  FmtSpec f;
  f.channels = 1;
  f.bits = 64;
  // Header declares 100 data bytes; file physically contains 50. RIFF size
  // accounts only the physical bytes so the chunk-bound check fires.
  auto bytes = buildWav(f, 100);
  bytes.resize(bytes.size() - 50);
  // Fix riff size to the actual content (12 + 24 + 8 + 50 - 8).
  const uint32_t riff = static_cast<uint32_t>(bytes.size() - 8);
  bytes[4] = static_cast<unsigned char>(riff & 0xFF);
  bytes[5] = static_cast<unsigned char>((riff >> 8) & 0xFF);
  bytes[6] = static_cast<unsigned char>((riff >> 16) & 0xFF);
  bytes[7] = static_cast<unsigned char>((riff >> 24) & 0xFF);
  const auto path = testFile("trunc.wav");
  writeFileBytes(path, bytes);
  CHECK(throwsConfigErrorWith(path, "truncated"));
}

TEST_CASE("T-W3e: inconsistent RIFF size rejected (frozen strict rule)") {
  FmtSpec f;
  const auto bytes = buildWav(f, 8);
  auto patched = bytes;
  patched[4] += 1;  // riffSize off by one
  const auto path = testFile("riffmismatch.wav");
  writeFileBytes(path, patched);
  CHECK(throwsConfigErrorWith(path, "inconsistent RIFF size"));
}

TEST_CASE("T-W3f: data size not frame-aligned rejected") {
  FmtSpec f;
  f.channels = 2;
  f.bits = 32;
  std::vector<unsigned char> data(9, 0);  // 9 bytes: not a multiple of 8
  const auto bytes = buildWav(f, 9, data.data());
  const auto path = testFile("unaligned.wav");
  writeFileBytes(path, bytes);
  CHECK(throwsConfigErrorWith(path, "not a multiple of the frame size"));
}

TEST_CASE("T-W3g: zero channels / zero rate / bad bits / bad blockAlign / bad byteRate") {
  {
    FmtSpec f;
    f.channels = 0;
    const auto path = testFile("ch0.wav");
    writeFileBytes(path, buildWav(f, 8));
    CHECK(throwsConfigErrorWith(path, "channel count must be >= 1"));
  }
  {
    FmtSpec f;
    f.rate = 0;
    const auto path = testFile("rate0.wav");
    writeFileBytes(path, buildWav(f, 8));
    CHECK(throwsConfigErrorWith(path, "sample rate must be >= 1"));
  }
  {
    FmtSpec f;
    f.bits = 16;  // integer bits under a float tag
    const auto path = testFile("bits16.wav");
    writeFileBytes(path, buildWav(f, 8));
    CHECK(throwsConfigErrorWith(path, "unsupported sample container"));
  }
  {
    FmtSpec f;
    f.channels = 2;
    f.bits = 64;
    f.blockAlign = 12;  // must be 16
    const auto path = testFile("align.wav");
    writeFileBytes(path, buildWav(f, 16));
    CHECK(throwsConfigErrorWith(path, "inconsistent blockAlign"));
  }
  {
    FmtSpec f;
    f.channels = 2;
    f.bits = 64;
    f.byteRate = 12345;  // must be 48000*16
    const auto path = testFile("byterate.wav");
    writeFileBytes(path, buildWav(f, 16));
    CHECK(throwsConfigErrorWith(path, "inconsistent byteRate"));
  }
}

TEST_CASE("T-W3h: duplicate fmt / missing data / missing fmt / bad fmt size rejected") {
  {
    // duplicate fmt chunk
    std::vector<unsigned char> b;
    tag(b, "RIFF");
    u32(b, 4 + (8 + 16) * 2 + (8 + 8));
    tag(b, "WAVE");
    for (int i = 0; i < 2; ++i) {
      tag(b, "fmt ");
      u32(b, 16);
      u16(b, 3); u16(b, 1); u32(b, 48000); u32(b, 48000 * 8); u16(b, 8); u16(b, 64);
    }
    tag(b, "data");
    u32(b, 8);
    b.insert(b.end(), 8, 0);
    const auto path = testFile("dupfmt.wav");
    writeFileBytes(path, b);
    CHECK(throwsConfigErrorWith(path, "duplicate fmt chunk"));
  }
  {
    FmtSpec f;
    auto bytes = buildWav(f, 0);
    // replace 'data' chunk with 'datx' => missing data
    std::memcpy(bytes.data() + 36, "datx", 4);
    const auto path = testFile("nodata.wav");
    writeFileBytes(path, bytes);
    CHECK(throwsConfigErrorWith(path, "missing data chunk"));
  }
  {
    FmtSpec f;
    auto bytes = buildWav(f, 8);
    std::memcpy(bytes.data() + 12, "fxmt", 4);  // missing fmt
    const auto path = testFile("nofmt.wav");
    writeFileBytes(path, bytes);
    CHECK(throwsConfigErrorWith(path, "missing fmt chunk"));
  }
  {
    // fmt chunk size 17 (not 16/18/40)
    std::vector<unsigned char> b;
    tag(b, "RIFF");
    u32(b, 4 + 8 + 17 + 8 + 8);
    tag(b, "WAVE");
    tag(b, "fmt ");
    u32(b, 17);
    u16(b, 3); u16(b, 1); u32(b, 48000); u32(b, 48000 * 8); u16(b, 8); u16(b, 64);
    b.push_back(0);  // 17th byte
    tag(b, "data");
    u32(b, 8);
    b.insert(b.end(), 8, 0);
    const auto path = testFile("fmt17.wav");
    writeFileBytes(path, b);
    CHECK(throwsConfigErrorWith(path, "unexpected fmt chunk size"));
  }
}

TEST_CASE("T-W3i: extensible validBits/cbSize violations rejected") {
  {
    std::vector<unsigned char> b;
    tag(b, "RIFF");
    u32(b, 4 + 8 + 40 + 8 + 8);
    tag(b, "WAVE");
    tag(b, "fmt ");
    u32(b, 40);
    u16(b, 0xFFFE); u16(b, 2); u32(b, 48000); u32(b, 48000 * 16); u16(b, 16); u16(b, 64);
    u16(b, 22);
    u16(b, 32);  // validBits != bits
    u32(b, 0x3);
    const std::vector<unsigned char> floatGuid = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
                                                  0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38,
                                                  0x9B, 0x71};
    b.insert(b.end(), floatGuid.begin(), floatGuid.end());
    tag(b, "data");
    u32(b, 8);
    b.insert(b.end(), 8, 0);
    const auto path = testFile("vb.wav");
    writeFileBytes(path, b);
    CHECK(throwsConfigErrorWith(path, "wValidBitsPerSample"));
  }
  {
    // cbSize = 21 (must be 22)
    std::vector<unsigned char> b;
    tag(b, "RIFF");
    u32(b, 4 + 8 + 40 + 8 + 8);
    tag(b, "WAVE");
    tag(b, "fmt ");
    u32(b, 40);
    u16(b, 0xFFFE); u16(b, 2); u32(b, 48000); u32(b, 48000 * 16); u16(b, 16); u16(b, 64);
    u16(b, 21);
    u16(b, 64);
    u32(b, 0x3);
    const std::vector<unsigned char> floatGuid = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
                                                  0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38,
                                                  0x9B, 0x71};
    b.insert(b.end(), floatGuid.begin(), floatGuid.end());
    tag(b, "data");
    u32(b, 8);
    b.insert(b.end(), 8, 0);
    const auto path = testFile("cb21.wav");
    writeFileBytes(path, b);
    CHECK(throwsConfigErrorWith(path, "cbSize"));
  }
}

TEST_CASE("T-W3j: NaN/Inf in file data rejected by the reader finiteness gate") {
  // f32 NaN payload (0x7FC00000) as the single sample of a 1-channel float32 file.
  std::vector<unsigned char> nanBytes = {0x00, 0x00, (unsigned char)0xC0, 0x7F};
  FmtSpec f;
  f.bits = 32;
  const auto bytes = buildWav(f, 4, nanBytes.data());
  const auto path = testFile("nan.wav");
  writeFileBytes(path, bytes);
  CHECK(throwsConfigErrorWith(path, "non-finite sample value at frame 0"));
}

TEST_CASE("T-W3k: unknown chunks are skipped (reader is chunk-order tolerant)") {
  FmtSpec f;
  const auto good = buildWav(f, 8);
  // Insert a 'JUNK' chunk (size 5, odd => exercises the pad-byte path)
  // between fmt and data. RIFF word alignment: the odd-size JUNK chunk is
  // followed by one pad byte not counted in its size.
  std::vector<unsigned char> b(good.begin(), good.begin() + 36);  // RIFF..fmt end
  tag(b, "JUNK");
  u32(b, 5);
  b.insert(b.end(), 5, 0);
  b.push_back(0);  // RIFF pad byte for the odd chunk size
  b.insert(b.end(), good.begin() + 36, good.end());  // data chunk
  // Fix riff size.
  const uint32_t riff = static_cast<uint32_t>(b.size() - 8);
  b[4] = static_cast<unsigned char>(riff & 0xFF);
  b[5] = static_cast<unsigned char>((riff >> 8) & 0xFF);
  b[6] = static_cast<unsigned char>((riff >> 16) & 0xFF);
  b[7] = static_cast<unsigned char>((riff >> 24) & 0xFF);
  const auto path = testFile("junk.wav");
  writeFileBytes(path, b);
  auto back = pitchlab::readWav(path);  // must NOT throw
  CHECK(back.meta.frames == 1);
}

TEST_CASE("T-W3l: writer rejects non-finite samples and invalid arguments") {
  const auto path = testFile("guard.wav");
  {
    std::vector<double> ch1 = {0.5, std::numeric_limits<double>::quiet_NaN()};
    const double* views[1] = {ch1.data()};
    bool threw = false;
    try {
      pitchlab::writeWav(path, views, 1, 2, 48000, pitchlab::WavSampleFormat::Float64);
    } catch (const pitchlab::ConfigError& e) {
      threw = true;
      CHECK(std::string(e.what()).find("non-finite") != std::string::npos);
    }
    CHECK(threw);
  }
  {
    auto data = makePlanar(1, 4, 5);
    bool threw = false;
    try {
      pitchlab::writeWav(path, asViews(data), 0, 4, 48000, pitchlab::WavSampleFormat::Float64);
    } catch (const pitchlab::ConfigError&) {
      threw = true;
    }
    CHECK(threw);  // channelCount 0
  }
  {
    auto data = makePlanar(9, 2, 5);
    bool threw = false;
    try {
      pitchlab::writeWav(path, asViews(data), 9, 2, 48000, pitchlab::WavSampleFormat::Float64);
    } catch (const pitchlab::ConfigError&) {
      threw = true;
    }
    CHECK(threw);  // 9 channels: outside the v0.1 mask table (1..8)
  }
  {
    auto data = makePlanar(1, 4, 5);
    bool threw = false;
    try {
      pitchlab::writeWav(path, asViews(data), 1, -1, 48000,
                         pitchlab::WavSampleFormat::Float64);
    } catch (const pitchlab::ConfigError&) {
      threw = true;
    }
    CHECK(threw);  // negative frame count
  }
}

TEST_CASE("T-W3m: RIFF 4 GiB cap — pure arithmetic path at the exact boundary") {
  // HONEST SCOPE NOTE: a real >4 GiB write is not allocated in CI; the cap
  // is a pure size computation performed BEFORE any byte is written
  // (wav_io.cpp: computeWavFileSize is called first in writeWav). The exact
  // boundary is tested analytically here. End-to-end coverage of the actual
  // multi-GiB write path is deliberately skipped (CI memory).
  using pitchlab::WavSampleFormat;
  // 2ch f64: header = 56 bytes total (12 RIFF + 8+16 fmt + 8+4 fact + 8 data),
  // so total - 8 = 48 + frames*16 <= 0xFFFFFFFF.
  const int64_t maxFrames = static_cast<int64_t>((0xFFFFFFFFULL - 48ULL) / 16ULL);  // 268435452
  CHECK_NOTHROW((void)pitchlab::computeWavFileSize(maxFrames, 2, WavSampleFormat::Float64));
  REQUIRE_THROWS_AS((void)pitchlab::computeWavFileSize(maxFrames + 1, 2, WavSampleFormat::Float64),
                    pitchlab::WavSizeLimitError);
  // The error message cites the cap and OD-14.
  try {
    (void)pitchlab::computeWavFileSize(maxFrames + 1, 2, WavSampleFormat::Float64);
    CHECK(false);
  } catch (const pitchlab::WavSizeLimitError& e) {
    CHECK(std::string(e.what()).find("4 GiB") != std::string::npos);
    CHECK(std::string(e.what()).find("OD-14") != std::string::npos);
  }
}

TEST_CASE("T-W3n: default channel mask table (mmreg standard, frozen)") {
  using pitchlab::defaultChannelMask;
  CHECK(defaultChannelMask(1) == 0x4);
  CHECK(defaultChannelMask(2) == 0x3);
  CHECK(defaultChannelMask(3) == 0x7);
  CHECK(defaultChannelMask(4) == 0x33);
  CHECK(defaultChannelMask(5) == 0x37);
  CHECK(defaultChannelMask(6) == 0x3F);
  CHECK(defaultChannelMask(7) == 0x13F);
  CHECK(defaultChannelMask(8) == 0x63F);
  CHECK_THROWS_AS((void)defaultChannelMask(0), pitchlab::ConfigError);
  CHECK_THROWS_AS((void)defaultChannelMask(9), pitchlab::ConfigError);
}

TEST_CASE("T-W3o: non-RIFF and non-WAVE magics rejected") {
  {
    std::vector<unsigned char> b;
    tag(b, "RIFX");
    u32(b, 100);
    tag(b, "WAVE");
    b.insert(b.end(), 88, 0);
    const auto path = testFile("rifx.wav");
    writeFileBytes(path, b);
    CHECK(throwsConfigErrorWith(path, "not a RIFF file"));
  }
  {
    std::vector<unsigned char> b;
    tag(b, "RIFF");
    u32(b, 100);
    tag(b, "AVI ");
    b.insert(b.end(), 88, 0);
    const auto path = testFile("notwave.wav");
    writeFileBytes(path, b);
    CHECK(throwsConfigErrorWith(path, "not a WAVE file"));
  }
}
