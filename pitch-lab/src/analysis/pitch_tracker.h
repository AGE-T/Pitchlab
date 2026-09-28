#pragma once

// Pitch Lab — the ReferencePitchTracker (OD-12, resolved route B: the
// clean-room C++ pYIN; implementation specification §10.5, frozen cycle 6
// BEFORE coding).
//
// CLEAN-ROOM BOUNDARY (§10.5): implemented from the Mauch & Dixon 2014
// ICASSP paper's published algorithm description + the librosa PUBLIC API
// DOCUMENTATION parameter values. No external pYIN source code is imported,
// read-for-derivation, or linked (no c4dm/pyin, LibPyin, Essentia, librosa
// source, libpyin). Where the paper punts or the public docs are ambiguous,
// §10.5 records our own frozen [DESIGN CHOICE] resolutions.
//
// Algorithm (pYIN, Mauch & Dixon 2014):
//   Stage 1  YIN difference function + cumulative mean normalised difference
//            (de Cheveigne & Kawahara 2002 math), fixed summation length.
//   Stage 1b probabilistic threshold prior (100-point grid, Beta(2,18)) over
//            the FIRST local minimum of the CMNDF below each threshold; the
//            Boltzmann period prior (kappa = 2, normalised form) weights
//            smaller periods; the no-trough mass (mass-conserving, 0.01 cap)
//            goes to unvoiced.
//   Stage 1c parabolic interpolation of the RAW difference function at each
//            candidate trough (sub-bin period refinement, YIN step 5).
//   Stage 2  HMM: 10-cent pitch bins over [fmin, fmax] x {voiced, unvoiced}
//            (the paper's 2M states); sticky pitch (0.99) + triangular
//            transition band (L = 40 bins); voicing switch 0.99/0.01.
//   Stage 3  log-domain Viterbi (the decoded track) + forward-backward
//            posteriors (the per-frame voiced probability).
//
// DETERMINISM (§10.5 item 1): no RNG (the threshold prior is a fixed
// discretised distribution, integrated analytically — never sampled),
// double throughout, -ffp-contract=off, ascending accumulation orders,
// pocketfft for the FFT stages (per-computation plans, POCKETFFT_CACHE_SIZE
// 0). Same binary + same input => bit-identical track.

#include <cstdint>
#include <vector>

namespace pitchlab::analysis {

/// One tracker frame's result. `center` is the timestamp the estimate is
/// tagged at (§10.5 item 2: the frame center — the least-biased position tag
/// for a whole-window period estimate). Unvoiced frames carry f0Hz = 0
/// (not meaningful; the voiced flag carries the semantics — missing data is
/// never a silent 0 in the metrics layer, which reads f0 only where voiced).
struct PitchTrackFrame {
  int64_t start = 0;              // frame start sample (in the input signal)
  int64_t center = 0;             // start + window/2 (the estimate timestamp)
  bool voiced = false;            // the Viterbi path's voicing at this frame
  double f0Hz = 0.0;              // valid when voiced (sub-bin refined)
  double voicedProbability = 0.0; // forward-backward posterior P(voiced|all obs)
};

/// The full track over one channel.
struct PitchTrack {
  int64_t windowFrames = 0;    // the per-rate analysis window N
  int64_t hop = 0;             // N/4
  int64_t frameCount = 0;      // frames actually computed
  int64_t droppedTailFrames = 0;  // samples beyond the last full frame
  double fminHz = 0.0;         // the search band actually used
  double fmaxHz = 0.0;
  std::vector<PitchTrackFrame> frames;
};

/// The frozen production tracking band (§10.5 item 3: identical to the
/// §10.4 item 17 f0-search band — one tracking band for the analysis layer).
inline constexpr double kTrackerFminHz = 50.0;
inline constexpr double kTrackerFmaxHz = 1000.0;

/// Track one channel's fundamental frequency with the clean-room pYIN
/// (§10.5). `fs` > 0; `fmin`/`fmax` in (0, fs/2] with fmin < fmax. Signals
/// shorter than one analysis window produce an empty track (frameCount = 0).
/// Never throws on finite input (short/empty => zero frames).
[[nodiscard]] PitchTrack trackPitch(const std::vector<double>& x, double fs,
                                   double fmin, double fmax);

}  // namespace pitchlab::analysis
