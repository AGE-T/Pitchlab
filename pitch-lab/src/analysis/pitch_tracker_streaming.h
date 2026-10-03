#pragma once

// Pitch Lab — the STREAMING fixed-lag pYIN tracker (task 33, §6.6.1 item 8).
//
// THE FROZEN DESIGN (recorded BEFORE coding at the SoT, 2026-10-01):
//   * Decode: fixed-lag Viterbi, D = 4 hops [FROZEN — bounded-divergence
//     design; the ring and the parity gate below are sized by it]. Same
//     observation model (the SHARED internal TU — pitch_observation_internal.h,
//     ONE implementation of the observation math), same transition matrix
//     (2K states, banded L = 40, sticky 0.99, voicing 0.99/0.01), same
//     determinism discipline as the batch decoder.
//   * HMM size: K = 519 bins (fs-INDEPENDENT), S = 2K = 1038 states.
//   * Fixed-lag ring: deltas (D+1)·S·8 B + backpointers (D+1)·S·4 B =
//     62 280 B ≈ 61 kB, plus the observation scratch — all prepare()-allocated.
//   * Λ_tr (the first-class tracker latency term):
//         Λ_tr = [n − minTag] + D·hop + ⌈pMax/4⌉
//     with minTag = llround((W + tauMin − 0.5)/2), W = n − tauMax — the
//     frozen per-rate table 3712/3768/7423/7536/15072 @44.1/48/88.2/96/192
//     kHz (the totals incl. the synthesis part: 5636/5848/11111/11536/22912;
//     5848 ≈ 122 ms @48k — FIRST-CLASS, never hidden).
//   * The forward-backward posterior is NOT computed here (the owner
//     decision recorded at item 8: not required by synthesis;
//     voicedProbability is consumed by the offline metrics layer only).
//   * PARITY GATES (frozen test gates vs the batch track — the OQ-2 gate):
//     voiced/unvoiced frame agreement ≥ 98 %; |Δf0| ≤ 10 cents on
//     jointly-voiced frames; voicing-transition alignment ≤ 2 hops. A breach
//     is the concrete implementation STOP + report trigger.
//
// REALTIME DISCIPLINE: every buffer and the FFT plans are allocated in
// prepare() (§6.6.1 item 16 — one 2n-point plan trio); feed()/decode touch
// no heap. reset() clears the rings + cursors in place, keeps the geometry
// and the plans (config-derived; T-D3).
//
// This header is INTERNAL to the analysis library (the engine drives it
// through the synthesis TU; the parity test includes it directly).

#include <cmath>
#include <cstdint>
#include <vector>

#include "analysis/pitch_observation_internal.h"
#include "analysis/spectral.h"

namespace pitchlab::analysis {

class StreamingPitchTracker {
 public:
  /// The frozen geometry for one (fs, band) — mirrors the batch preamble
  /// (§10.5 items 2-3, 7). Returns false on a degenerate band (the caller
  /// renders the all-unvoiced fallback path — the engine never fails).
  bool configure(double fs, double fmin, double fmax);

  [[nodiscard]] bool configured() const { return configured_; }
  [[nodiscard]] int64_t windowFrames() const { return n_; }
  [[nodiscard]] int64_t hop() const { return hop_; }
  [[nodiscard]] int64_t bins() const { return k_; }
  [[nodiscard]] int64_t tauMin() const { return tauMin_; }
  [[nodiscard]] int64_t tauMax() const { return tauMax_; }
  [[nodiscard]] int64_t evidenceW() const { return n_ - tauMax_; }
  [[nodiscard]] int lagFrames() const { return dLag_; }  // D (hops) — frozen 4

  /// prepare-time allocation (§6.6.1 item 16): the observation scratch, the
  /// FFT plan trio (2n points), the delta/backpointer rings. Idempotent.
  void prepare();

  /// Clears all decode state in place (rings, cursors, the decoded ring) —
  /// NO allocation; the geometry and the plans survive (T-D3).
  void reset();

  /// Processes every frame whose window [s, s + n) is complete at
  /// `availableEnd` (absolute input positions), starting at the internal
  /// cursor. `windowData` points at the frame's first sample in the
  /// CALLER'S sliding window; `frameStart` is its absolute position.
  /// (One call per frame — the engine drives the while-loop so it controls
  /// the sliding-window lifetime.) Decodes frame (index − D) after
  /// observing frame `index`.
  void observeFrame(const double* windowData, int64_t frameStart);

  struct DecodedFrame {
    int64_t start = 0;    // the frame's window start (input timeline)
    int64_t center = 0;   // the evidence-center tag (the batch convention)
    bool voiced = false;
    double f0Hz = 0.0;    // valid when voiced (the sub-bin refined estimate)
  };

  /// The number of decoded frames not yet consumed by the caller.
  [[nodiscard]] int decodedCount() const { return decodedCount_; }
  /// The i-th oldest undecoded-consumed frame (0 = the oldest).
  [[nodiscard]] const DecodedFrame& decoded(int i) const {
    return decodedRing_[static_cast<std::size_t>((decodedHead_ + i) %
                                                 decodedRing_.size())];
  }
  void consumeDecoded(int count) {
    decodedHead_ = (decodedHead_ + count) % decodedRing_.size();
    decodedCount_ -= count;
  }

  [[nodiscard]] int64_t nextFrameStart() const { return nextStart_; }
  [[nodiscard]] int64_t framesObserved() const { return framesObserved_; }

 private:
  void decodeLagged();

  bool configured_ = false;
  double fs_ = 0.0;
  int64_t n_ = 0;
  int64_t hop_ = 0;
  int64_t k_ = 0;        // the pitch-bin count (fs-independent: 519)
  int64_t tauMin_ = 0;
  int64_t tauMax_ = 0;
  int dLag_ = 4;         // the frozen fixed lag D = 4 hops (§6.6.1 item 8)

  // The transition table (log domain) — the batch formula verbatim.
  double logTriSelf_ = 0.0;
  std::vector<double> logMove_;
  double logVoiceStay_ = 0.0;
  double logVoiceSwitch_ = 0.0;
  double logInitU_ = 0.0;

  // The observation machinery (shared TU; prepared once).
  detail::ObservationScratch scratch_;
  detail::FrameObservation obs_;
  std::vector<double> prior_;
  // The per-bin winner state for the current frame (the batch's obsVoiced/
  // obsRefined/unvoicedMass, one frame at a time) + the PER-ROW caches of
  // the same for the (D+1) ring rows: the fixed-lag decode resolves the
  // refined frequency from the DECODED frame's own observation (the batch
  // reads it from the same frame's tables — the ring keeps them alive).
  std::vector<double> obsVoiced_;
  std::vector<double> obsRefined_;
  std::vector<double> obsLogRow_;
  std::vector<double> laggedVoiced_;    // (D+1) * K
  std::vector<double> laggedRefined_;   // (D+1) * K

  // The fixed-lag Viterbi rings: (D+1) delta rows + (D+1) psi rows (the psi
  // row for the transition INTO row r is stored WITH row r; row 0's psi is
  // unused). Indexed modulo (D+1); frame f -> row f % (D+1).
  std::vector<std::vector<double>> delta_;
  std::vector<std::vector<int32_t>> psi_;
  int64_t framesObserved_ = 0;  // the absolute frame index counter
  int64_t nextStart_ = 0;       // the next frame's window start

  // The decoded-frame output ring (the engine consumes in order).
  std::vector<DecodedFrame> decodedRing_;
  int decodedHead_ = 0;
  int decodedCount_ = 0;

  // The bin mapping (the batch's binOf/binCentreHz, fmin-captured).
  double fmin_ = 0.0;
};

/// The frozen tracker latency term (§6.6.1 item 8, the ONE definition —
/// the engine's latency() and the product layer's chainGeometry both read
/// it): Λ_tr = [n − minTag] + D·hop + ⌈pMax/4⌉ with
/// minTag = llround((W + tauMin − 0.5)/2), W = n − tauMax, n = the per-rate
/// analysis window, D = 4 (frozen), hop = n/4, pMax = ⌊fs/50⌋. The frozen
/// per-rate totals: 3712/3768/7423/7536/15072 @44.1/48/88.2/96/192 kHz.
[[nodiscard]] inline int64_t trackerLagFrames(double fs, double pMax, int dLag = 4) {
  const int64_t n = analysisStftFrames(fs);
  const int64_t hop = n / 4;
  int64_t tauMin = static_cast<int64_t>(std::ceil(fs / 1000.0));
  const int64_t tauMax = static_cast<int64_t>(std::floor(fs / 50.0)) > n / 2
                             ? n / 2
                             : static_cast<int64_t>(std::floor(fs / 50.0));
  if (tauMin < 1) tauMin = 1;
  const int64_t w = n - tauMax;
  const int64_t minTag = static_cast<int64_t>(
      std::llround((static_cast<double>(w) + static_cast<double>(tauMin) - 0.5) / 2.0));
  const int64_t pMaxI = static_cast<int64_t>(pMax);
  return (n - minTag) + static_cast<int64_t>(dLag) * hop + (pMaxI + 3) / 4;
}

/// THE MEASURED STREAMING RELEASE MARGIN (task-33 checkpoint 3/4 correction;
/// ratification queued — the recorded-correction class, cp1's 980->1480
/// precedent): the decode-gated production releases output in bursts whose
/// steady-state delay peaks exceed the item-10 composition by the gates'
/// granularity — the decode-release granularity (<= 2 hop across the mark
/// and placement gates) plus the mark-spacing/window reach (<= 2 pMax).
/// Probe-measured peak delay 7876 @48 kHz through the realtime adapter
/// (6866 through the direct harness; maxBlock 4096) vs the composed 5848:
/// the margin + 2·hop + 2·pMax covers it with ~900 frames of headroom. The
/// ONE definition both the engine's latency() and the product layer's
/// chainGeometry read.
[[nodiscard]] inline int64_t trackerReleaseMargin(double fs, double pMax) {
  const int64_t hop = analysisStftFrames(fs) / 4;
  const int64_t pm = static_cast<int64_t>(pMax);
  return 2 * hop + 2 * pm;
}

}  // namespace pitchlab::analysis
