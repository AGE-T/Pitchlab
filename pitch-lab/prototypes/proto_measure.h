#pragma once

// Pitch Lab — Task 28 candidate re-evaluation: the shared measurement
// layer (artifact characterisation, Phase G; differentiation, Phase H).
//
// MEASUREMENT CONTRACT (frozen here before coding; all double, all
// deterministic, no allocation constraints — this is analysis-side code):
//   * integrity: frame count, finite, non-silent, peak/RMS dBFS
//   * pitch response: dominant frequency via exact-frequency Hann
//     projection (coarse grid + zero-crossing-seeded main-lobe
//     refinement — the task-27-validated dual estimator) + a zero-
//     crossing median cross-check; F0 via the clean-room pYIN tracker
//   * level: RMS ratio vs the reference input
//   * artifact signature:
//       - amDepth / amRateHz  (Hilbert-envelope AM statistics: the
//         grain-rate amplitude modulation family)
//       - combRippleDb        (mean |log(P / movavg(P))| over the averaged
//         power spectrum: the comb/notching family)
//       - onsetCount, onsetSharpDb, onsetTailDb (transient behaviour:
//         sharpness = energy in +-30 frames / energy in +-300 frames;
//         tail = decay after the onset core)
//   * spectral character: centroid Hz (the formant-shift indicator)
//   * waveform similarity: Pearson correlation vs the reference input
//     over the steady region with best-lag alignment
//   * stereo: Pearson L/R correlation (inter-channel coherence)
//   * determinism: SHA-256 of the planar double bytes (core/hash.h)
//
// The steady measurement region is [0.15 N, 0.90 N) of the signal (edges
// and algorithm latency transients excluded).

#include <string>
#include <vector>

#include "core/types.h"

namespace pitchlab::proto {

struct WaveMetrics {
  // Integrity.
  FrameCount frames = 0;
  bool finite = true;
  bool silent = true;          // true when peak < 1e-9
  double peakDbfs = -999.0;
  double rmsDbfs = -999.0;
  double rmsRatio = 0.0;       // out RMS / reference RMS (0 when no ref)

  // Pitch response (0 / -1 sentinels when not measurable).
  double dominantHz = 0.0;     // projection-peak estimate
  double dominantZcHz = 0.0;   // zero-crossing median cross-check
  double f0TrackerHz = 0.0;    // pYIN median voiced F0 (0 when unvoiced)
  double trackerVoicedRatio = 0.0;

  // Artifact signature.
  double amDepth = 0.0;        // std(env)/mean(env) over the steady region
  double amRateHz = 0.0;       // dominant envelope modulation rate (0 = none)
  double combRippleDb = 0.0;   // spectral ripple vs +-6-bin moving average
  FrameCount onsetCount = 0;
  double onsetSharpDb = -999.0;  // mean E(+-30f)/E(+-300f) at onsets (dB)
  double onsetTailDb = -999.0;   // mean E(+30..+90)/E(+-30) at onsets (dB)

  // Spectral character.
  double centroidHz = 0.0;

  // Similarity / coherence.
  double corrCoef = 0.0;       // vs reference input, best lag in +-8192
  double stereoCorr = 1.0;     // L vs R (>= 2 channels only)
};

/// Measure one output. `reference` (optional, same channel count) enables
/// rmsRatio and corrCoef. Channels are measured on channel 0 except
/// stereoCorr (L vs R).
[[nodiscard]] WaveMetrics measureWave(
    const std::vector<std::vector<double>>& ch, double fs,
    const std::vector<std::vector<double>>& reference = {});

/// SHA-256 hex of the planar double bytes (determinism record).
[[nodiscard]] std::string waveHashHex(const std::vector<std::vector<double>>& ch);

/// Median voiced F0 + voiced ratio from the clean-room pYIN tracker
/// (exposed separately: the trackers are also used for the ramp
/// start/end endpoints).
struct F0Summary {
  double medianHz = 0.0;
  double voicedRatio = 0.0;
  double startHz = 0.0;  // median over the first 25% voiced frames
  double endHz = 0.0;    // median over the last 25% voiced frames
};
[[nodiscard]] F0Summary f0Summary(const std::vector<double>& x, double fs);

}  // namespace pitchlab::proto
