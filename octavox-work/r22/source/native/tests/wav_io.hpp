// OCTAVOX round 12 -- WAV I/O for the native test harness.
// Mirrors poc/octavox_offline.py read_wav/write_wav EXACTLY (same scaling,
// same clip-guard, same rounding) so WAV-level comparisons with the Python
// reference are meaningful. Harness-only code (not part of the library).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace octavox {
namespace wav {

/// Read a PCM WAV (16/24/32-bit). Returns interleaved samples [n * ch] in
/// [-1, 1) and the sample rate. Throws std::runtime_error on bad input.
std::vector<float> readWav(const std::string& path, int& sr, int& channels);

/// Write a 16-bit PCM WAV. Deterministic, no dither, clip-guarded, exactly
/// like the Python write_wav (round(y*32767) after clipping to [-1,1]).
void writeWav(const std::string& path, const float* x, int n, int channels,
              int sr);

}  // namespace wav
}  // namespace octavox

/// R19: write a 32-bit IEEE-float WAV (format tag 3). NO quantisation at
/// all -- the file carries the exact float bits (the deterministic A/B
/// null-test output format). Deterministic, endian-explicit.
void writeWavF32(const std::string& path, const float* x, int n, int channels,
                 int sr);
