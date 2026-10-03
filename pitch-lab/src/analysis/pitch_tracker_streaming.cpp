// Pitch Lab — the STREAMING fixed-lag pYIN tracker (task 33, §6.6.1 item 8;
// see the header for the frozen design). The observation math comes from the
// SHARED internal TU (ONE implementation — the batch tracker uses the same
// functions); the Viterbi step and the frame assembly mirror the batch
// decoder's formulae verbatim; the forward-backward posterior is deliberately
// absent (the recorded owner decision).

#include "analysis/pitch_tracker_streaming.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "analysis/spectral.h"

namespace pitchlab::analysis {

bool StreamingPitchTracker::configure(double fs, double fmin, double fmax) {
  configured_ = false;
  if (!(fs > 0.0) || !(fmin > 0.0) || !(fmax > fmin) || fmax > fs / 2.0) {
    return false;  // degenerate parameters (the honest all-unvoiced fallback)
  }
  // The per-rate analysis window (§10.5 item 2 — the shared rule).
  n_ = analysisStftFrames(fs);
  hop_ = n_ / 4;
  if (n_ < 4) return false;
  // Lag range (§10.5 item 3), clipped to the two-period window bound.
  tauMin_ = static_cast<int64_t>(std::ceil(fs / fmax));
  tauMax_ = static_cast<int64_t>(std::floor(fs / fmin));
  if (tauMax_ > n_ / 2) tauMax_ = n_ / 2;
  if (tauMin_ < 1) tauMin_ = 1;
  if (tauMax_ - tauMin_ < 2) return false;  // band degenerate at this rate
  // Pitch bins (§10.5 item 7): 10-cent bins over [fmin, fmax] (fs-independent).
  k_ = static_cast<int64_t>(std::floor(1200.0 * std::log2(fmax / fmin) /
                                       detail::kBinCents)) + 1;
  fmin_ = fmin;
  fs_ = fs;
  configured_ = true;
  return true;
}

void StreamingPitchTracker::prepare() {
  // The transition log-probs (the batch formula EXACT — §10.5 item 7):
  // P_pitch(b->b') = 0.99[b==b'] + 0.01*Tri(b'-b)/sumTri, sumTri = L^2; the
  // self term INCLUDES 0.01*Tri(0)/sumTri = 0.01/L. Voicing: 0.99 / 0.01.
  const double sumTri =
      static_cast<double>(detail::kMaxJumpBins) * static_cast<double>(detail::kMaxJumpBins);
  logTriSelf_ = std::log(detail::kPitchStick +
                         0.01 * static_cast<double>(detail::kMaxJumpBins) / sumTri);
  logMove_.assign(static_cast<std::size_t>(detail::kMaxJumpBins) + 1, 0.0);
  for (int d = 0; d <= detail::kMaxJumpBins; ++d) {
    logMove_[static_cast<std::size_t>(d)] =
        std::log(0.01 * static_cast<double>(detail::kMaxJumpBins - d) / sumTri);
  }
  logVoiceStay_ = std::log(detail::kVoiceStay);
  logVoiceSwitch_ = std::log(1.0 - detail::kVoiceStay);
  logInitU_ = -std::log(static_cast<double>(k_));  // uniform unvoiced init

  prior_ = detail::thresholdPrior();

  // The observation scratch + the plan trio (created ONCE — item 16).
  scratch_.ensureGeometry(n_, tauMax_);

  const std::size_t S = static_cast<std::size_t>(2 * k_);
  obsVoiced_.assign(static_cast<std::size_t>(k_), 0.0);
  obsRefined_.assign(static_cast<std::size_t>(k_), 0.0);
  obsLogRow_.assign(S, detail::kLogObsFloor);
  laggedVoiced_.assign(static_cast<std::size_t>(dLag_ + 1) * static_cast<std::size_t>(k_), 0.0);
  laggedRefined_.assign(static_cast<std::size_t>(dLag_ + 1) * static_cast<std::size_t>(k_), 0.0);

  // The fixed-lag rings: (D+1) delta rows + (D+1) psi rows — 62 280 B for
  // the frozen D = 4 / S = 1038 (the item-8 accounting).
  const int rows = dLag_ + 1;
  delta_.assign(static_cast<std::size_t>(rows),
                std::vector<double>(S, detail::kLogObsFloor));
  psi_.assign(static_cast<std::size_t>(rows),
              std::vector<int32_t>(S, 0));

  // The decoded-frame output ring: the engine consumes in order; the size
  // covers the decode lag plus a generous scheduling margin (the engine's
  // mark generator consumes behind the decode frontier — a few hops).
  decodedRing_.assign(static_cast<std::size_t>(dLag_ + 32), DecodedFrame{});
  decodedHead_ = 0;
  decodedCount_ = 0;

  framesObserved_ = 0;
  nextStart_ = 0;
}

void StreamingPitchTracker::reset() {
  // Clear the decode state in place (T-D3): rings + cursors; the geometry,
  // the plans, the transition tables and the scratch survive (config- or
  // prepare-derived).
  for (auto& row : delta_) {
    std::fill(row.begin(), row.end(), detail::kLogObsFloor);
  }
  for (auto& row : psi_) {
    std::fill(row.begin(), row.end(), 0);
  }
  framesObserved_ = 0;
  nextStart_ = 0;
  decodedHead_ = 0;
  decodedCount_ = 0;
}

void StreamingPitchTracker::observeFrame(const double* windowData, int64_t frameStart) {
  const int64_t f = framesObserved_;
  const std::size_t row = static_cast<std::size_t>(f % (dLag_ + 1));

  // ---- the observation (the shared TU — the batch math verbatim) ---------
  frameObservation(windowData, n_, fs_, tauMin_, tauMax_, prior_, scratch_, obs_);
  std::fill(obsVoiced_.begin(), obsVoiced_.end(), 0.0);
  std::fill(obsRefined_.begin(), obsRefined_.end(), 0.0);
  for (const detail::FrameObservation::Candidate& cand : obs_.candidates) {
    const double cents = 1200.0 * std::log2(cand.fObs / fmin_);
    int64_t b = static_cast<int64_t>(std::floor(cents / detail::kBinCents));
    if (b < 0) b = 0;
    if (b > k_ - 1) b = k_ - 1;
    if (cand.mass > obsVoiced_[static_cast<std::size_t>(b)]) {
      obsVoiced_[static_cast<std::size_t>(b)] = cand.mass;
      obsRefined_[static_cast<std::size_t>(b)] = cand.fObs;
    }
  }
  // Observation log-probs over states: voiced = the bin's mass (floored);
  // unvoiced = U/K (the unvoiced states do not observe pitch, §10.5 item 7).
  const double uOverK = obs_.unvoicedMass / static_cast<double>(k_);
  for (int64_t b = 0; b < k_; ++b) {
    obsLogRow_[static_cast<std::size_t>(2 * b)] = detail::safeLog(uOverK);
    obsLogRow_[static_cast<std::size_t>(2 * b + 1)] =
        detail::safeLog(obsVoiced_[static_cast<std::size_t>(b)]);
  }

  // Cache this frame's per-bin winners on its ring row (the decode reads
  // the decoded frame's refined frequency from here).
  {
    const std::size_t base = row * static_cast<std::size_t>(k_);
    for (int64_t b = 0; b < k_; ++b) {
      laggedVoiced_[base + static_cast<std::size_t>(b)] = obsVoiced_[static_cast<std::size_t>(b)];
      laggedRefined_[base + static_cast<std::size_t>(b)] = obsRefined_[static_cast<std::size_t>(b)];
    }
  }

  // ---- one Viterbi step (the batch formula verbatim, log domain) ---------
  std::vector<double>& cur = delta_[row];
  if (f == 0) {
    for (int64_t b = 0; b < k_; ++b) {
      cur[static_cast<std::size_t>(2 * b)] = logInitU_ + obsLogRow_[static_cast<std::size_t>(2 * b)];
      cur[static_cast<std::size_t>(2 * b + 1)] =
          detail::kLogObsFloor + obsLogRow_[static_cast<std::size_t>(2 * b + 1)];  // init: unvoiced only
    }
  } else {
    const std::size_t prevRow =
        static_cast<std::size_t>((f - 1) % (dLag_ + 1));
    const std::vector<double>& prev = delta_[prevRow];
    std::vector<int32_t>& back = psi_[row];
    for (int64_t b2 = 0; b2 < k_; ++b2) {
      const int64_t lo = std::max<int64_t>(0, b2 - detail::kMaxJumpBins);
      const int64_t hi = std::min<int64_t>(k_ - 1, b2 + detail::kMaxJumpBins);
      for (int v2 = 0; v2 <= 1; ++v2) {
        const std::size_t s2 = static_cast<std::size_t>(2 * b2 + v2);
        double best = detail::kNegInf;
        int32_t bestS = 0;
        for (int v1 = 0; v1 <= 1; ++v1) {
          const double logVoice = (v1 == v2) ? logVoiceStay_ : logVoiceSwitch_;
          for (int64_t b1 = lo; b1 <= hi; ++b1) {
            const int64_t db = b2 - b1;
            const int64_t a = db < 0 ? -db : db;
            const double lp = a == 0 ? logTriSelf_ : logMove_[static_cast<std::size_t>(a)];
            const double cand =
                prev[static_cast<std::size_t>(2 * b1 + v1)] + logVoice + lp;
            if (cand > best) {  // strict >: ties keep the first scanned (v1=0, b1 ascending)
              best = cand;
              bestS = static_cast<int32_t>(2 * b1 + v1);
            }
          }
        }
        cur[s2] = best + obsLogRow_[s2];
        back[s2] = bestS;
      }
    }
  }
  ++framesObserved_;

  // ---- the fixed-lag decode: frame (f - D) from the argmax at frame f ----
  if (f >= dLag_) {
    decodeLagged();
  }
  nextStart_ = frameStart + hop_;
}

void StreamingPitchTracker::decodeLagged() {
  const int64_t f = framesObserved_ - 1;  // the just-observed frame index
  const std::size_t curRow = static_cast<std::size_t>(f % (dLag_ + 1));
  const std::vector<double>& cur = delta_[curRow];
  // The best current state (strict >: the first maximum wins — the batch's
  // final-state convention).
  int32_t bestS = 0;
  double best = detail::kNegInf;
  for (std::size_t s = 0; s < cur.size(); ++s) {
    if (cur[s] > best) {
      best = cur[s];
      bestS = static_cast<int32_t>(s);
    }
  }
  // Trace back D hops: psi for the transition INTO row r lives WITH row r.
  int32_t s = bestS;
  for (int64_t g = f; g > f - dLag_; --g) {
    const std::size_t r = static_cast<std::size_t>(g % (dLag_ + 1));
    s = psi_[r][static_cast<std::size_t>(s)];
  }
  // s is the decoded state at frame (f - D).
  const int64_t fd = f - dLag_;
  const int64_t b = s / 2;
  const int v = s % 2;
  const int64_t start = fd * hop_;
  DecodedFrame out;
  out.start = start;
  out.center = start + n_ / 2;  // unvoiced default (the batch convention)
  out.voiced = (v == 1);
  if (out.voiced) {
    // Exact-bin candidate wins outright; otherwise the largest-mass
    // candidate among the +-1 neighbour bins (strict >, ascending bins) —
    // the batch's assembly, evaluated on the CURRENT frame's observation
    // (frame f's — the fixed-lag decode reads the bin choice from the
    // traceback and the refined frequency from the decoded frame's own
    // observation cache... the observation is per-frame transient here, so
    // the refined frequency comes from the CURRENT frame's bins at the
    // decoded bin — the documented streaming reading: the estimate travels
    // with the decoded bin at the LAGGED frame through the same +-1-bin
    // lookup against the frame-(f-D) observation. To keep the observation
    // of frame (f-D) available, the tracker caches obsVoiced/obsRefined
    // per ring row.
    const std::size_t lagRow = static_cast<std::size_t>(fd % (dLag_ + 1));
    bool found = false;
    double bestMass = 0.0;
    double bestF = 0.0;
    for (int64_t db = -1; db <= 1; ++db) {
      const int64_t bb = b + db;
      if (bb < 0 || bb >= k_) continue;
      const double mass = laggedVoiced_[static_cast<std::size_t>(lagRow * k_ + static_cast<std::size_t>(bb))];
      if (mass <= 0.0) continue;
      if (db == 0) {
        bestMass = mass;
        bestF = laggedRefined_[static_cast<std::size_t>(lagRow * k_ + static_cast<std::size_t>(bb))];
        found = true;
        break;
      }
      if (mass > bestMass) {
        bestMass = mass;
        bestF = laggedRefined_[static_cast<std::size_t>(lagRow * k_ + static_cast<std::size_t>(bb))];
        found = true;
      }
    }
    out.f0Hz = found ? bestF : fmin_ * std::exp2((static_cast<double>(b) * detail::kBinCents + 5.0) / 1200.0);
    if (out.f0Hz > 0.0) {
      const double tauHat = fs_ / out.f0Hz;
      out.center =
          start + static_cast<int64_t>(std::llround(
                      (static_cast<double>(evidenceW()) + tauHat) / 2.0));
    }
  } else {
    out.f0Hz = 0.0;
  }
  // Push into the decoded ring (bounded; the engine consumes in order).
  if (decodedCount_ < static_cast<int>(decodedRing_.size())) {
    const std::size_t slot =
        static_cast<std::size_t>((decodedHead_ + decodedCount_) % decodedRing_.size());
    decodedRing_[slot] = out;
    ++decodedCount_;
  }
}

}  // namespace pitchlab::analysis
