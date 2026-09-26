#include "core/wav_io.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "core/errors.h"

namespace pitchlab {
namespace {

// ---------------------------------------------------------------------------
// Little-endian byte helpers (WAV is LE; no struct-packing games, no host
// endianness assumptions — every multi-byte value is serialised explicitly).
// ---------------------------------------------------------------------------

[[nodiscard]] uint16_t readU16le(const unsigned char* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

[[nodiscard]] uint32_t readU32le(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

[[nodiscard]] uint64_t readU64le(const unsigned char* p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; --i) {
    v = (v << 8) | static_cast<uint64_t>(p[i]);
  }
  return v;
}

void appendU16le(std::vector<unsigned char>& out, uint16_t v) {
  out.push_back(static_cast<unsigned char>(v & 0xFF));
  out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}

void appendU32le(std::vector<unsigned char>& out, uint32_t v) {
  out.push_back(static_cast<unsigned char>(v & 0xFF));
  out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
  out.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
  out.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
}

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT: {00000003-0000-0010-8000-00AA00389B71}
// (byte sequence as stored in a WAVEFORMATEXTENSIBLE header, first 3 fields LE)
constexpr std::array<unsigned char, 16> kIeeeFloatGuid = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

constexpr uint16_t kFormatIeeeFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;
constexpr uint16_t kFormatPcm = 1;

constexpr uint64_t kRiffSizeCap = 0xFFFFFFFFULL;  // 32-bit RIFF size field

[[nodiscard]] ConfigError bad(const std::filesystem::path& file, const std::string& field,
                              const std::string& reason) {
  return ConfigError(file.string(), field, reason);
}

}  // namespace

uint32_t defaultChannelMask(ChannelCount channelCount) {
  static constexpr std::array<uint32_t, 9> kMasks = {
      0x0,    // unused index 0
      0x4,    // 1: FC
      0x3,    // 2: FL|FR
      0x7,    // 3: FL|FR|FC
      0x33,   // 4: FL|FR|BL|BR
      0x37,   // 5: FL|FR|FC|BL|BR
      0x3F,   // 6: 5.1 FL|FR|FC|LFE|BL|BR
      0x13F,  // 7: 6.1 + BC
      0x63F,  // 8: 7.1 + SL|SR
  };
  if (channelCount < 1 || channelCount > 8) {
    throw ConfigError("", "channelCount",
                      "unsupported channel count " + std::to_string(channelCount) +
                          " (v0.1 supports 1..8; >2-channel depth is OD-11)");
  }
  return kMasks[static_cast<std::size_t>(channelCount)];
}

uint64_t computeWavFileSize(FrameCount frameCount, ChannelCount channelCount,
                            WavSampleFormat format) {
  if (frameCount < 0 || channelCount < 1) {
    throw ConfigError("", "frameCount/channelCount",
                      "invalid frame count " + std::to_string(frameCount) + " or channel count " +
                          std::to_string(channelCount));
  }
  const uint64_t bytesPerSample = (format == WavSampleFormat::Float64) ? 8 : 4;
  const uint64_t dataSize =
      static_cast<uint64_t>(frameCount) * static_cast<uint64_t>(channelCount) * bytesPerSample;
  const uint64_t fmtSize = (channelCount > 2) ? 40 : 16;
  const uint64_t total = 12 + (8 + fmtSize) + (8 + 4) + (8 + dataSize);
  if (total - 8 > kRiffSizeCap) {
    throw WavSizeLimitError(
        "WAV output would exceed the RIFF 32-bit size cap (4 GiB): total " +
        std::to_string(total) + " bytes (frameCount=" + std::to_string(frameCount) +
        ", channels=" + std::to_string(channelCount) + "). W64 support is open decision OD-14.");
  }
  return total;
}

WavData readWav(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw bad(path, "", "cannot open file for reading");
  }
  in.seekg(0, std::ios::end);
  const std::streamoff fileSize = in.tellg();
  in.seekg(0, std::ios::beg);
  if (fileSize < 12) {
    throw bad(path, "riff", "file too small to be a RIFF/WAVE file");
  }

  auto readExact = [&](unsigned char* dst, std::streamsize n) {
    in.read(reinterpret_cast<char*>(dst), n);
    if (in.gcount() != n) {
      throw bad(path, "riff", "file truncated during chunk read");
    }
  };

  unsigned char hdr[12];
  readExact(hdr, 12);
  if (std::memcmp(hdr, "RIFF", 4) != 0) {
    throw bad(path, "riff", "not a RIFF file (missing 'RIFF' signature)");
  }
  const uint32_t riffSize = readU32le(hdr + 4);
  if (std::memcmp(hdr + 8, "WAVE", 4) != 0) {
    throw bad(path, "riff", "not a WAVE file (missing 'WAVE' form type)");
  }
  // FROZEN v0.1 strict rule (documented in wav_io.h): the declared RIFF size
  // must equal fileSize-8 exactly. Our writer guarantees this; the synthetic
  // corpus is authored through it. Relaxation for foreign files is deferred
  // with the real-world corpus (OD-10).
  const auto declaredSize = static_cast<std::streamoff>(riffSize);
  if (declaredSize != fileSize - 8) {
    throw bad(path, "riff.size",
              "inconsistent RIFF size: header declares " + std::to_string(riffSize) +
                  " bytes of payload but file has " + std::to_string(fileSize - 8));
  }

  bool haveFmt = false;
  bool haveData = false;
  uint16_t fmtTag = 0;
  uint16_t channelsU16 = 0;
  uint32_t sampleRate = 0;
  uint32_t byteRate = 0;
  uint16_t blockAlign = 0;
  uint16_t bits = 0;
  bool extensible = false;
  uint32_t channelMask = 0;

  std::streamoff dataOffset = -1;
  uint32_t dataSize = 0;

  std::streamoff pos = 12;
  while (pos < fileSize) {
    in.seekg(pos, std::ios::beg);
    unsigned char chunkHdr[8];
    if (fileSize - pos < 8) {
      throw bad(path, "chunks", "truncated chunk header at end of file");
    }
    readExact(chunkHdr, 8);
    const uint32_t chunkSize = readU32le(chunkHdr + 4);
    const std::streamoff payloadStart = pos + 8;
    if (static_cast<std::streamoff>(chunkSize) > fileSize - payloadStart) {
      throw bad(path, "data",
                "chunk '" + std::string(reinterpret_cast<char*>(chunkHdr), 4) +
                    "' declares " + std::to_string(chunkSize) +
                    " bytes but only " + std::to_string(fileSize - payloadStart) +
                    " remain (truncated file)");
    }

    if (std::memcmp(chunkHdr, "fmt ", 4) == 0) {
      if (haveFmt) {
        throw bad(path, "fmt", "duplicate fmt chunk");
      }
      haveFmt = true;
      unsigned char fmt[40] = {};
      if (chunkSize != 16 && chunkSize != 18 && chunkSize != 40) {
        throw bad(path, "fmt.size",
                  "unexpected fmt chunk size " + std::to_string(chunkSize) +
                      " (expected 16, 18 or 40)");
      }
      in.seekg(payloadStart, std::ios::beg);
      readExact(fmt, static_cast<std::streamsize>(chunkSize));

      fmtTag = readU16le(fmt);
      channelsU16 = readU16le(fmt + 2);
      sampleRate = readU32le(fmt + 4);
      byteRate = readU32le(fmt + 8);
      blockAlign = readU16le(fmt + 12);
      bits = readU16le(fmt + 14);

      if (fmtTag == kFormatPcm) {
        throw bad(path, "fmt.tag",
                  "integer PCM not accepted — author assets as IEEE float");
      }
      if (fmtTag == kFormatExtensible) {
        if (chunkSize != 40) {
          throw bad(path, "fmt.tag",
                    "WAVE_FORMAT_EXTENSIBLE requires a 40-byte fmt chunk (got " +
                        std::to_string(chunkSize) + ")");
        }
        const uint16_t cbSize = readU16le(fmt + 16);
        const uint16_t validBits = readU16le(fmt + 18);
        channelMask = readU32le(fmt + 20);
        if (cbSize != 22) {
          throw bad(path, "fmt.cbSize",
                    "WAVE_FORMAT_EXTENSIBLE cbSize must be 22 (got " + std::to_string(cbSize) + ")");
        }
        if (validBits != 0 && validBits != bits) {
          throw bad(path, "fmt.validBits",
                    "wValidBitsPerSample must be 0 or equal the container size (got " +
                        std::to_string(validBits) + " for " + std::to_string(bits) + "-bit)");
        }
        if (std::memcmp(fmt + 24, kIeeeFloatGuid.data(), 16) != 0) {
          throw bad(path, "fmt.subFormat",
                    "WAVE_FORMAT_EXTENSIBLE subformat is not KSDATAFORMAT_SUBTYPE_IEEE_FLOAT "
                    "(only IEEE float is accepted; integer PCM is rejected — author assets as "
                    "IEEE float)");
        }
        extensible = true;
      } else if (fmtTag != kFormatIeeeFloat) {
        throw bad(path, "fmt.tag",
                  "unsupported WAVE format tag " + std::to_string(fmtTag) +
                      " (only IEEE float 3 and extensible 0xFFFE are accepted)");
      }

      if (channelsU16 < 1) {
        throw bad(path, "fmt.channels", "channel count must be >= 1");
      }
      if (sampleRate < 1) {
        throw bad(path, "fmt.sampleRate", "sample rate must be >= 1 Hz");
      }
      if (bits != 32 && bits != 64) {
        throw bad(path, "fmt.bits",
                  "unsupported sample container " + std::to_string(bits) +
                      " bits (IEEE float 32 or 64 only)");
      }
      const uint16_t frameSize = static_cast<uint16_t>(channelsU16 * (bits / 8));
      if (blockAlign != frameSize) {
        throw bad(path, "fmt.blockAlign",
                  "inconsistent blockAlign: " + std::to_string(blockAlign) +
                      " (expected channels*bytesPerSample = " + std::to_string(frameSize) + ")");
      }
      const uint64_t expectedByteRate =
          static_cast<uint64_t>(sampleRate) * static_cast<uint64_t>(frameSize);
      if (byteRate != expectedByteRate) {
        throw bad(path, "fmt.byteRate",
                  "inconsistent byteRate: " + std::to_string(byteRate) + " (expected " +
                      std::to_string(expectedByteRate) + ")");
      }
    } else if (std::memcmp(chunkHdr, "data", 4) == 0) {
      if (haveData) {
        throw bad(path, "data", "duplicate data chunk");
      }
      haveData = true;
      dataOffset = payloadStart;
      dataSize = chunkSize;
    }
    // All other chunks (incl. 'fact', unknown/junk) are skipped by size
    // (spec §9 metadata policy: the reader ignores unknown chunks; 'fact'
    // content is not authoritative — the data chunk is).

    // RIFF chunks are word-aligned: odd sizes carry one pad byte not counted
    // in chunkSize.
    const std::streamoff advance = 8 + static_cast<std::streamoff>(chunkSize) +
                                   (static_cast<std::streamoff>(chunkSize) & 1);
    pos += advance;
  }

  if (!haveFmt) {
    throw bad(path, "fmt", "missing fmt chunk");
  }
  if (!haveData) {
    throw bad(path, "data", "missing data chunk");
  }

  const uint16_t frameSize = blockAlign;
  if (dataSize % frameSize != 0) {
    throw bad(path, "data.size",
              "data chunk size " + std::to_string(dataSize) +
                  " is not a multiple of the frame size " + std::to_string(frameSize));
  }
  const FrameCount frames = static_cast<FrameCount>(dataSize / frameSize);

  // ---- payload: de-interleave, convert, finiteness scan ----
  WavData out;
  out.meta.sampleRate = sampleRate;
  out.meta.channels = static_cast<ChannelCount>(channelsU16);
  out.meta.frames = frames;
  out.meta.extensible = extensible;
  out.meta.channelMask = channelMask;
  out.meta.bitsPerSample = bits;

  const std::size_t ch = channelsU16;
  out.channels.assign(ch, std::vector<double>(static_cast<std::size_t>(frames), 0.0));

  const std::size_t bytesPerSample = bits / 8;
  std::vector<unsigned char> raw(static_cast<std::size_t>(bytesPerSample));
  in.seekg(dataOffset, std::ios::beg);
  for (FrameCount f = 0; f < frames; ++f) {
    for (std::size_t c = 0; c < ch; ++c) {
      readExact(raw.data(), static_cast<std::streamsize>(bytesPerSample));
      double v = 0.0;
      if (bits == 32) {
        const uint32_t u = readU32le(raw.data());
        v = static_cast<double>(std::bit_cast<float>(u));  // exact f32 -> f64 widening
      } else {
        const uint64_t u = readU64le(raw.data());
        v = std::bit_cast<double>(u);
      }
      if (!std::isfinite(v)) {
        throw bad(path, "data",
                  "non-finite sample value at frame " + std::to_string(f) + ", channel " +
                      std::to_string(c) + " (input integrity gate, spec §9)");
      }
      out.channels[c][static_cast<std::size_t>(f)] = v;
    }
  }

  return out;
}

void writeWav(const std::filesystem::path& path, const double* const* channels,
              ChannelCount channelCount, FrameCount frameCount, uint32_t sampleRate,
              WavSampleFormat format) {
  // ---- argument validation (before the size cap check: cheap config errors
  // first; the cap check itself is the pure computation) ----
  if (channelCount < 1) {
    throw ConfigError(path.string(), "channelCount", "channel count must be >= 1");
  }
  if (frameCount < 0) {
    throw ConfigError(path.string(), "frameCount", "frame count must be >= 0");
  }
  if (sampleRate < 1) {
    throw ConfigError(path.string(), "sampleRate", "sample rate must be >= 1 Hz");
  }
  const uint16_t bitsCheck = (format == WavSampleFormat::Float64) ? 16 : 32;
  const uint16_t frameSizeCheck = static_cast<uint16_t>(channelCount * (bitsCheck / 8));
  if (sampleRate > 0xFFFFFFFFULL / frameSizeCheck) {
    throw ConfigError(path.string(), "sampleRate",
                      "sample rate too large for a consistent 32-bit byteRate field");
  }
  if (channels == nullptr && (channelCount > 0 && frameCount > 0)) {
    throw ConfigError(path.string(), "channels", "channel buffer array is null");
  }
  for (ChannelCount c = 0; c < channelCount; ++c) {
    if (channels[c] == nullptr && frameCount > 0) {
      throw ConfigError(path.string(), "channels",
                        "channel " + std::to_string(c) + " buffer is null");
    }
  }
  // Enforce the v0.1 channel-count bound (1..8; mask table governs >2).
  (void)defaultChannelMask(channelCount);

  const uint64_t totalSize = computeWavFileSize(frameCount, channelCount, format);
  const uint16_t bits = (format == WavSampleFormat::Float64) ? 64 : 32;
  const uint16_t bytesPerSample = bits / 8;
  const uint16_t frameSize = static_cast<uint16_t>(channelCount * bytesPerSample);
  const uint32_t dataSize =
      static_cast<uint32_t>(static_cast<uint64_t>(frameCount) * frameSize);
  const bool useExtensible = channelCount > 2;

  // ---- master-integrity pre-scan: ALL validation happens before the file is
  // created (a rejected write leaves no partial file on disk) ----
  for (FrameCount f = 0; f < frameCount; ++f) {
    for (ChannelCount c = 0; c < channelCount; ++c) {
      if (!std::isfinite(channels[c][f])) {
        throw ConfigError(path.string(), "data",
                          "non-finite sample at frame " + std::to_string(f) + ", channel " +
                              std::to_string(c) + " (master integrity gate, spec §9)");
      }
    }
  }

  // ---- deterministic header: RIFF / fmt / fact / data (in this order) ----
  std::vector<unsigned char> header;
  header.reserve(64);
  header.insert(header.end(), {'R', 'I', 'F', 'F'});
  appendU32le(header, static_cast<uint32_t>(totalSize - 8));
  header.insert(header.end(), {'W', 'A', 'V', 'E'});

  header.insert(header.end(), {'f', 'm', 't', ' '});
  if (useExtensible) {
    appendU32le(header, 40);
    appendU16le(header, kFormatExtensible);
    appendU16le(header, static_cast<uint16_t>(channelCount));
    appendU32le(header, sampleRate);
    appendU32le(header, sampleRate * frameSize);  // byteRate (fits: total < 4 GiB)
    appendU16le(header, frameSize);
    appendU16le(header, bits);
    appendU16le(header, 22);                       // cbSize
    appendU16le(header, bits);                     // wValidBitsPerSample == container
    appendU32le(header, defaultChannelMask(channelCount));
    header.insert(header.end(), kIeeeFloatGuid.begin(), kIeeeFloatGuid.end());
  } else {
    appendU32le(header, 16);
    appendU16le(header, kFormatIeeeFloat);
    appendU16le(header, static_cast<uint16_t>(channelCount));
    appendU32le(header, sampleRate);
    appendU32le(header, sampleRate * frameSize);
    appendU16le(header, frameSize);
    appendU16le(header, bits);
  }

  header.insert(header.end(), {'f', 'a', 'c', 't'});
  appendU32le(header, 4);
  appendU32le(header, static_cast<uint32_t>(frameCount));  // dwSampleLength

  header.insert(header.end(), {'d', 'a', 't', 'a'});
  appendU32le(header, dataSize);

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw ConfigError(path.string(), "", "cannot open file for writing");
  }
  file.write(reinterpret_cast<const char*>(header.data()),
             static_cast<std::streamsize>(header.size()));

  // ---- interleaved payload; f64->f32 via static_cast<float> (IEEE
  // round-to-nearest-even in the default FP environment — frozen, tested) ----
  unsigned char sample[8];
  for (FrameCount f = 0; f < frameCount; ++f) {
    for (ChannelCount c = 0; c < channelCount; ++c) {
      const double v = channels[c][f];  // finiteness already guaranteed by the pre-scan
      if (format == WavSampleFormat::Float64) {
        const uint64_t u = std::bit_cast<uint64_t>(v);
        for (int i = 0; i < 8; ++i) {
          sample[i] = static_cast<unsigned char>((u >> (8 * i)) & 0xFF);
        }
        file.write(reinterpret_cast<const char*>(sample), 8);
      } else {
        const uint32_t u = std::bit_cast<uint32_t>(static_cast<float>(v));
        sample[0] = static_cast<unsigned char>(u & 0xFF);
        sample[1] = static_cast<unsigned char>((u >> 8) & 0xFF);
        sample[2] = static_cast<unsigned char>((u >> 16) & 0xFF);
        sample[3] = static_cast<unsigned char>((u >> 24) & 0xFF);
        file.write(reinterpret_cast<const char*>(sample), 4);
      }
    }
  }
  file.flush();
  if (!file) {
    throw ConfigError(path.string(), "", "file write failed (I/O error)");
  }
}

}  // namespace pitchlab
