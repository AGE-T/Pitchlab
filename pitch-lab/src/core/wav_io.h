#pragma once

// Pitch Lab — own WAV I/O (implementation specification §9 / §4.9; architecture
// §J: libsndfile explicitly rejected — replaced by this module).
//
// CONTRACT (v0.1, frozen 2026-09-25; implemented 2026-09-26, cycle 1):
//
// Reader:
//   * RIFF/WAVE, WAVE_FORMAT_IEEE_FLOAT (fmt tag 3) with 32- or 64-bit
//     sample containers; WAVE_FORMAT_EXTENSIBLE (0xFFFE) with the IEEE-float
//     subformat GUID (accepted for ANY channel count — lenient read; the
//     strict "extensible when channels > 2" rule governs OUR writer).
//   * Returns planar `double` buffers (de-interleaved) + metadata
//     {sampleRate, channels, frames, extensible flag, channel mask (verbatim
//     when present, else 0), bitsPerSample}.
//   * Unknown chunks are skipped by size (metadata policy: reader ignores
//     unknown chunks). `fact` chunk content is ignored (the data chunk is
//     authoritative for length).
//   * REJECTED with ConfigError (§4.9 "CONFIG ERROR mapping"): integer PCM
//     (explicit message: "integer PCM not accepted — author assets as IEEE
//     float"), compressed/unknown fmt tags, truncated files, inconsistent
//     header sizes (incl. the frozen v0.1 strict rule: RIFF declared size
//     must equal fileSize-8 EXACTLY — our own writer guarantees this; the
//     corpus is authored by it; relaxing for foreign files is deferred with
//     the real-world corpus, OD-10), data size not frame-aligned, channels
//     == 0, sample rate == 0, bits not in {32, 64}, blockAlign/byteRate
//     mismatches, extensible cbSize/validBits/subformat violations.
//   * Finiteness gate (§9 "invalid input behaviour: CONFIG ERROR at corpus
//     validation — the finiteness scan is implemented HERE at read time):
//     any NaN/Inf sample value in the data chunk => ConfigError naming the
//     first offending frame and channel.
//   * Sample rate: read from fmt, carried per job, NEVER implicitly
//     resampled. The reader accepts any positive uint32 rate (the harness
//     supported-set check {44100..192000} is a harness/job-level policy,
//     spec §8, not a file-format rule).
//
// Writer:
//   * Deterministic bytes: same render => byte-identical WAV. Emits ONLY
//     `fmt` + `fact` + `data` chunks in that order — no timestamps, no
//     software tags, no LIST/INFO (provenance lives in the sidecar manifest).
//   * Float64 masters (L-4) / float32 listening copies; the f64->f32
//     conversion is IEEE round-to-nearest-even (plain static_cast<float> in
//     the default FP environment — frozen and unit-tested, incl. a
//     round-half-to-even tie case).
//   * Plain fmt tag 3 (16-byte fmt) for channels <= 2; WAVE_FORMAT_EXTENSIBLE
//     (40-byte fmt) for channels > 2 with the deterministic default channel
//     mask table below. The "channel-mask required" case (§4.9) cannot arise
//     from v0.1 corpus content; if it ever does, that is a spec extension.
//   * Channel-mask table (mmreg standard layout masks, frozen):
//       1: 0x4 (FC), 2: 0x3 (FL|FR), 3: 0x7, 4: 0x33, 5: 0x37, 6: 0x3F (5.1),
//       7: 0x13F (6.1), 8: 0x63F (7.1). channelCount > 8 => ConfigError
//       (v0.1 honest bound; >2-channel depth is OD-11).
//   * All sample values must be finite => else ConfigError (master
//     integrity; same gate as the reader).
//   * RIFF 32-bit size cap: total file size > 4 GiB => WavSizeLimitError
//     (spec §9: JOB FAILURE with explicit message; W64 = open decision
//     OD-14). The size arithmetic is checked BEFORE any bytes are written.
//     Note: v0.1 test coverage of the cap exercises the pure size
//     computation (a real >4 GiB buffer is not allocated in CI); this is
//     documented honestly in tests/wav_io_test.cpp.
//   * Interleaves planar input to file order; channel order = file order, no
//     mask reordering (the mask is recorded verbatim when read).

#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/types.h"

namespace pitchlab {

enum class WavSampleFormat { Float32, Float64 };

struct WavMetadata {
  uint32_t sampleRate = 0;    // Hz, verbatim from the fmt chunk
  ChannelCount channels = 0;
  FrameCount frames = 0;      // data-chunk frames (authoritative)
  bool extensible = false;    // file used WAVE_FORMAT_EXTENSIBLE
  uint32_t channelMask = 0;   // verbatim when present, else 0
  int bitsPerSample = 0;      // 32 | 64
};

struct WavData {
  WavMetadata meta;
  std::vector<std::vector<double>> channels;  // planar, [c][f], meta.frames each
};

/// Read and fully validate a WAV file (see contract above).
/// Throws pitchlab::ConfigError on any rejection (file/field/reason filled).
[[nodiscard]] WavData readWav(const std::filesystem::path& path);

/// Write a deterministic WAV file from planar double buffers.
/// `channels[c]` must have exactly `frameCount` samples each.
/// Throws pitchlab::ConfigError on invalid arguments/non-finite samples;
/// pitchlab::WavSizeLimitError when the RIFF 32-bit size cap would be exceeded.
void writeWav(const std::filesystem::path& path, const double* const* channels,
              ChannelCount channelCount, FrameCount frameCount, uint32_t sampleRate,
              WavSampleFormat format);

/// Deterministic default channel mask for a channel count (see table above).
/// Returns 0 for mono/stereo (no mask needed/emitted by the writer).
/// Throws ConfigError for channelCount outside [1, 8].
[[nodiscard]] uint32_t defaultChannelMask(ChannelCount channelCount);

/// Pure RIFF size computation used by the writer's cap check (frames x
/// channels x bytesPerSample + header). Returns the total file size in
/// bytes. Throws WavSizeLimitError if the size exceeds the 32-bit RIFF cap.
[[nodiscard]] uint64_t computeWavFileSize(FrameCount frameCount, ChannelCount channelCount,
                                          WavSampleFormat format);

}  // namespace pitchlab
