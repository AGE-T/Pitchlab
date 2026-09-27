#pragma once

// Pitch Lab — shared analysis primitives (implementation specification
// §10.4 item 6, frozen cycle 5 BEFORE coding). Pure functions; the analysis
// layer may allocate freely (NOT an engine process() path — T-A1 untouched).
//
// Contents (all frozen in §10.4 item 6):
//   * analysisStftFrames(fs)         — the per-rate STFT length (2048/4096/
//                                      8192 at the six first-class rates)
//   * StftFrame                       — one frame's power spectrum
//   * computeStft(channel, fs)        — frames + power spectra
//   * projectAt(x, start, window, fs, f) — exact-frequency projection
//   * hilbertEnvelope(x)              — analytic-signal magnitude
//   * detectOnsets(channels, fs)      — the frozen energy-based onset
//                                      detector (θ = 0.25·max, 50 ms
//                                      refractory, maximal runs)
//   * EmissionMap                     — §6.1.1 item 2 recurrence replay +
//                                      the frozen warp/inverse helpers

#include <cstdint>
#include <optional>
#include <vector>

#include "core/types.h"

namespace pitchlab::analysis {

/// §10.4 item 6a: Nw = nextPow2(ceil(fs·2048/48000)) — exactly 2048/4096/
/// 8192 at 44.1-48/88.2-96/176.4-192 kHz. fs must be positive.
[[nodiscard]] int64_t analysisStftFrames(double fs);

/// §10.4 item 6a: periodic Hann window of length n.
[[nodiscard]] std::vector<double> periodicHann(int64_t n);

struct StftFrame {
  int64_t start = 0;               // frame start position in the signal
  std::vector<double> power;       // |X_k|^2, k = 0..Nw/2 (power spectrum)
  std::vector<double> re;          // complex spectrum (real), k = 0..Nw/2
  std::vector<double> im;          // complex spectrum (imag), k = 0..Nw/2
};

struct StftResult {
  int64_t windowFrames = 0;
  int64_t hop = 0;
  int64_t frameCount = 0;          // frames actually computed
  int64_t droppedTailFrames = 0;   // partial frames dropped (documented)
  std::vector<StftFrame> frames;
};

/// §10.4 item 6a: STFT of one channel (window/hop/frames frozen).
[[nodiscard]] StftResult computeStft(const std::vector<double>& x, double fs);

/// §10.4 item 6b: exact-frequency projection X(n, f) = Σ w[i]·x[start+i]·
/// e^{-j2πf·i/fs}; returns (re, im). start+n must be within x.
struct Projection {
  double re = 0.0;
  double im = 0.0;
};
[[nodiscard]] Projection projectAt(const std::vector<double>& x, int64_t start,
                                   const std::vector<double>& window, double fs, double f);

/// §10.4 item 6c: Hilbert envelope (analytic-signal magnitude), full length.
[[nodiscard]] std::vector<double> hilbertEnvelope(const std::vector<double>& x);

/// §10.4 item 6d: the frozen onset detector over multi-channel planar data.
struct OnsetDetection {
  std::vector<int64_t> onsets;   // ascending onset frames (maximal runs)
  double threshold = 0.0;        // the realised θ (reported in method)
  int64_t refractoryFrames = 0;
};
[[nodiscard]] OnsetDetection detectOnsets(const std::vector<std::vector<double>>& channels,
                                          double fs);

/// §10.4 item 5/6e: the §6.1.1 item 2 read-position recurrence replayed from
/// an effective curve (analysis-grade reconstruction; same formula, ascending
/// double accumulation). Only meaningful for RateFollowing engines.
class EmissionMap {
 public:
  /// Replay r over m = 0..frameCount (inclusive end value), r(0) = 0,
  /// r(m+1) = r(m) + ratio[clamp(floor(r(m)), 0, N_in-1)].
  EmissionMap(const std::vector<double>& ratio, int64_t inputFrames, int64_t frameCount);

  [[nodiscard]] bool empty() const { return r_.empty(); }
  [[nodiscard]] int64_t size() const { return static_cast<int64_t>(r_.size()); }
  /// r(m); m must be in [0, size()).
  [[nodiscard]] double at(int64_t m) const { return r_[static_cast<std::size_t>(m)]; }

  /// §10.4 item 6e: first m with r(m) >= p (input onset → output position).
  /// Returns size() when p is beyond the whole map.
  [[nodiscard]] int64_t outputPositionOfInput(int64_t p) const;

  /// §10.4 item 6e: warp a rate-follower output onto the input grid —
  /// warped[p] = out[m]·(1-u) + out[m+1]·u for r(m) <= p < r(m+1).
  /// Output length = inputFrames (the input-timeline grid).
  [[nodiscard]] std::vector<double> warpToInputTimeline(const std::vector<double>& out,
                                                        int64_t inputFrames) const;

  /// Number of output frames m whose read position r(m) <= lastRealInput.
  [[nodiscard]] int64_t framesCoveringInput(int64_t lastRealInput) const;

 private:
  std::vector<double> r_;
};

}  // namespace pitchlab::analysis
