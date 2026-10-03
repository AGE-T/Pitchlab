#include "analysis/pitch_tracker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "analysis/pitch_observation_internal.h"
#include "analysis/spectral.h"

namespace pitchlab::analysis {
namespace {

// ---------------------------------------------------------------------------
// The observation model (stages 1/1b/1c), the pYIN constants, the threshold
// prior and the log-domain helpers live in the SHARED internal TU
// (analysis/pitch_observation_internal.h) — §6.6.1 item 8: ONE
// implementation of the observation math, reused verbatim by the batch
// tracker (here, the metrics + parity reference) and the streaming
// fixed-lag tracker (pitch_tracker_streaming.cpp, the realtime path). The
// forward-backward posterior below is deliberately NOT migrated into the
// realtime path (the owner decision recorded at §6.6.1 item 8).
// ---------------------------------------------------------------------------

using detail::FrameObservation;
using detail::kBinCents;
using detail::kLogObsFloor;
using detail::kMaxJumpBins;
using detail::kNegInf;
using detail::kObsFloor;
using detail::kPitchStick;
using detail::kVoiceStay;
using detail::LogSumExp;
using detail::ObservationScratch;
using detail::frameObservation;
using detail::safeLog;
using detail::thresholdPrior;

}  // namespace

PitchTrack trackPitch(const std::vector<double>& x, double fs, double fmin, double fmax) {
  PitchTrack track;
  track.fminHz = fmin;
  track.fmaxHz = fmax;
  if (!(fs > 0.0) || !(fmin > 0.0) || !(fmax > fmin) || fmax > fs / 2.0) {
    return track;  // degenerate parameters: empty track (honest, never throws)
  }
  // The per-rate analysis window (§10.5 item 2 — the shared §10.4 item 6a rule).
  const int64_t n = analysisStftFrames(fs);
  track.windowFrames = n;
  track.hop = n / 4;
  const int64_t total = static_cast<int64_t>(x.size());
  if (total < n || n < 4) {
    track.droppedTailFrames = total;
    return track;  // shorter than one window: zero frames (§10.5 item 2)
  }

  // Lag range (§10.5 item 3), clipped to the two-period window bound.
  int64_t tauMin = static_cast<int64_t>(std::ceil(fs / fmax));
  int64_t tauMax = static_cast<int64_t>(std::floor(fs / fmin));
  if (tauMax > n / 2) tauMax = n / 2;
  if (tauMin < 1) tauMin = 1;
  if (tauMax - tauMin < 2) {
    track.droppedTailFrames = total;
    return track;  // band degenerate at this rate/window (documented honesty)
  }

  // Pitch bins (§10.5 item 7): 10-cent bins over [fmin, fmax].
  const int64_t K =
      static_cast<int64_t>(std::floor(1200.0 * std::log2(fmax / fmin) / kBinCents)) + 1;
  const auto binOf = [fmin](double fHz) -> int64_t {
    const double cents = 1200.0 * std::log2(fHz / fmin);
    int64_t b = static_cast<int64_t>(std::floor(cents / kBinCents));
    if (b < 0) b = 0;
    return b;
  };
  const auto binCentreHz = [fmin](int64_t b) -> double {
    return fmin * std::exp2((static_cast<double>(b) * kBinCents + 5.0) / 1200.0);
  };

  // Frames (§10.5 item 2): starts ascending, exists iff start + N <= total.
  std::vector<int64_t> starts;
  for (int64_t s = 0; s + n <= total; s += track.hop) {
    starts.push_back(s);
  }
  const int64_t F = static_cast<int64_t>(starts.size());
  track.frameCount = F;
  track.droppedTailFrames = total - (F > 0 ? (starts.back() + n) : 0);

  const std::vector<double> prior = thresholdPrior();
  const std::size_t S = static_cast<std::size_t>(2 * K);  // states s = 2b+v

  // The batch tracker's own observation scratch (§6.6.1 item 8: the shared
  // TU reused verbatim — one scratch per trackPitch call, identical
  // arithmetic to the pre-extraction form; the plans are per-call here,
  // the OFFLINE discipline — the realtime path creates them in prepare()).
  ObservationScratch obsScratch;
  obsScratch.ensureGeometry(n, tauMax);
  FrameObservation obs;

  // Observations -> per-frame, per-bin winner (largest mass; ties resolve to
  // the SMALLER tau because candidates are visited in ascending tau order and
  // the update uses strict >) + the refined frequency + the unvoiced mass.
  std::vector<std::vector<double>> obsVoiced(static_cast<std::size_t>(F),
                                             std::vector<double>(static_cast<std::size_t>(K), 0.0));
  std::vector<std::vector<double>> obsRefined(static_cast<std::size_t>(F),
                                              std::vector<double>(static_cast<std::size_t>(K), 0.0));
  std::vector<double> unvoicedMass(static_cast<std::size_t>(F), 1.0);
  for (int64_t f = 0; f < F; ++f) {
    const double* frame = x.data() + starts[static_cast<std::size_t>(f)];
    frameObservation(frame, n, fs, tauMin, tauMax, prior, obsScratch, obs);
    unvoicedMass[static_cast<std::size_t>(f)] = obs.unvoicedMass;
    for (const FrameObservation::Candidate& cand : obs.candidates) {
      int64_t b = binOf(cand.fObs);
      if (b > K - 1) b = K - 1;
      if (cand.mass > obsVoiced[static_cast<std::size_t>(f)][static_cast<std::size_t>(b)]) {
        obsVoiced[static_cast<std::size_t>(f)][static_cast<std::size_t>(b)] = cand.mass;
        obsRefined[static_cast<std::size_t>(f)][static_cast<std::size_t>(b)] = cand.fObs;
      }
    }
  }

  // Observation log-probs over states: voiced = the bin's mass (floored);
  // unvoiced = U/K (the unvoiced states do not observe pitch, §10.5 item 7).
  std::vector<std::vector<double>> obsLog(static_cast<std::size_t>(F),
                                          std::vector<double>(S, kLogObsFloor));
  for (int64_t f = 0; f < F; ++f) {
    const double uOverK = unvoicedMass[static_cast<std::size_t>(f)] / static_cast<double>(K);
    for (int64_t b = 0; b < K; ++b) {
      obsLog[static_cast<std::size_t>(f)][static_cast<std::size_t>(2 * b)] = safeLog(uOverK);
      obsLog[static_cast<std::size_t>(f)][static_cast<std::size_t>(2 * b + 1)] =
          safeLog(obsVoiced[static_cast<std::size_t>(f)][static_cast<std::size_t>(b)]);
    }
  }

  // Transition log-probs (sparse band, §10.5 item 7, the formula EXACT):
  // P_pitch(b->b') = 0.99[b==b'] + 0.01*Tri(b'-b)/sumTri, Tri(d) = max(0, L-|d|),
  // sumTri = L^2 (closed form: L + 2*sum_{d=1..L-1}(L-d) = L*L). The self
  // term INCLUDES 0.01*Tri(0)/sumTri = 0.01/L (the frozen formula, verbatim).
  // Voicing: 0.99 stay / 0.01 switch. Precomputed log table (|d| = 0..L).
  const double sumTri =
      static_cast<double>(kMaxJumpBins) * static_cast<double>(kMaxJumpBins);
  const double logTriSelf =
      std::log(kPitchStick + 0.01 * static_cast<double>(kMaxJumpBins) / sumTri);
  std::vector<double> logMove(static_cast<std::size_t>(kMaxJumpBins) + 1);
  for (int d = 0; d <= kMaxJumpBins; ++d) {
    logMove[static_cast<std::size_t>(d)] =
        std::log(0.01 * static_cast<double>(kMaxJumpBins - d) / sumTri);
  }
  const double logVoiceStay = std::log(kVoiceStay);
  const double logVoiceSwitch = std::log(1.0 - kVoiceStay);
  const auto logPitch = [&](int64_t db) -> double {
    const int64_t a = db < 0 ? -db : db;
    return a == 0 ? logTriSelf : logMove[static_cast<std::size_t>(a)];
  };
  const double logInitU = -std::log(static_cast<double>(K));  // uniform unvoiced init

  // --- Viterbi (log domain, §10.5 item 8) ---
  std::vector<std::vector<double>> delta(static_cast<std::size_t>(F),
                                         std::vector<double>(S, kLogObsFloor));
  std::vector<std::vector<int32_t>> psi(static_cast<std::size_t>(F),
                                        std::vector<int32_t>(S, 0));
  for (int64_t b = 0; b < K; ++b) {
    delta[0][static_cast<std::size_t>(2 * b)] = logInitU + obsLog[0][static_cast<std::size_t>(2 * b)];
    delta[0][static_cast<std::size_t>(2 * b + 1)] =
        kLogObsFloor + obsLog[0][static_cast<std::size_t>(2 * b + 1)];  // init: unvoiced only
  }
  for (int64_t f = 1; f < F; ++f) {
    const std::vector<double>& prev = delta[static_cast<std::size_t>(f - 1)];
    std::vector<double>& cur = delta[static_cast<std::size_t>(f)];
    std::vector<int32_t>& back = psi[static_cast<std::size_t>(f)];
    for (int64_t b2 = 0; b2 < K; ++b2) {
      const int64_t lo = std::max<int64_t>(0, b2 - kMaxJumpBins);
      const int64_t hi = std::min<int64_t>(K - 1, b2 + kMaxJumpBins);
      for (int v2 = 0; v2 <= 1; ++v2) {
        const std::size_t s2 = static_cast<std::size_t>(2 * b2 + v2);
        double best = kNegInf;
        int32_t bestS = 0;
        for (int v1 = 0; v1 <= 1; ++v1) {
          const double logVoice = (v1 == v2) ? logVoiceStay : logVoiceSwitch;
          for (int64_t b1 = lo; b1 <= hi; ++b1) {
            const double lp = logPitch(b2 - b1);
            const double cand =
                prev[static_cast<std::size_t>(2 * b1 + v1)] + logVoice + lp;
            if (cand > best) {  // strict >: ties keep the first scanned (v1=0, b1 ascending)
              best = cand;
              bestS = static_cast<int32_t>(2 * b1 + v1);
            }
          }
        }
        cur[s2] = best + obsLog[static_cast<std::size_t>(f)][s2];
        back[s2] = bestS;
      }
    }
  }
  std::vector<int32_t> path(static_cast<std::size_t>(F), 0);
  {
    std::size_t bestS = 0;
    double best = kNegInf;
    const std::vector<double>& last = delta[static_cast<std::size_t>(F - 1)];
    for (std::size_t s = 0; s < S; ++s) {
      if (last[s] > best) {
        best = last[s];
        bestS = s;
      }
    }
    path[static_cast<std::size_t>(F - 1)] = static_cast<int32_t>(bestS);
    for (int64_t f = F - 2; f >= 0; --f) {
      path[static_cast<std::size_t>(f)] = psi[static_cast<std::size_t>(f + 1)]
          [static_cast<std::size_t>(path[static_cast<std::size_t>(f + 1)])];
    }
  }

  // --- Forward-backward posteriors (§10.5 item 8) ---
  std::vector<std::vector<double>> alpha(static_cast<std::size_t>(F),
                                         std::vector<double>(S, kLogObsFloor));
  for (int64_t b = 0; b < K; ++b) {
    alpha[0][static_cast<std::size_t>(2 * b)] =
        logInitU + obsLog[0][static_cast<std::size_t>(2 * b)];
    alpha[0][static_cast<std::size_t>(2 * b + 1)] =
        kLogObsFloor + obsLog[0][static_cast<std::size_t>(2 * b + 1)];  // init: unvoiced only
  }
  for (int64_t f = 1; f < F; ++f) {
    const std::vector<double>& prev = alpha[static_cast<std::size_t>(f - 1)];
    std::vector<double>& cur = alpha[static_cast<std::size_t>(f)];
    // For each TARGET state: log-sum-exp over the band predecessors
    // (source-major or target-major is equivalent; target-major keeps the
    // streaming accumulator per state — O(K*L*4) per frame).
    for (int64_t b2 = 0; b2 < K; ++b2) {
      const int64_t lo = std::max<int64_t>(0, b2 - kMaxJumpBins);
      const int64_t hi = std::min<int64_t>(K - 1, b2 + kMaxJumpBins);
      for (int v2 = 0; v2 <= 1; ++v2) {
        const std::size_t s2 = static_cast<std::size_t>(2 * b2 + v2);
        LogSumExp lse;
        for (int v1 = 0; v1 <= 1; ++v1) {
          const double lv = (v1 == v2) ? logVoiceStay : logVoiceSwitch;
          for (int64_t b1 = lo; b1 <= hi; ++b1) {
            lse.add(prev[static_cast<std::size_t>(2 * b1 + v1)] + lv + logPitch(b2 - b1));
          }
        }
        cur[s2] = lse.value() + obsLog[static_cast<std::size_t>(f)][s2];
      }
    }
  }
  std::vector<std::vector<double>> beta(static_cast<std::size_t>(F),
                                        std::vector<double>(S, 0.0));
  for (int64_t f = F - 2; f >= 0; --f) {
    const std::vector<double>& nextBeta = beta[static_cast<std::size_t>(f + 1)];
    const std::vector<double>& nextObs = obsLog[static_cast<std::size_t>(f + 1)];
    std::vector<double>& cur = beta[static_cast<std::size_t>(f)];
    for (int64_t b1 = 0; b1 < K; ++b1) {
      const int64_t lo = std::max<int64_t>(0, b1 - kMaxJumpBins);
      const int64_t hi = std::min<int64_t>(K - 1, b1 + kMaxJumpBins);
      for (int v1 = 0; v1 <= 1; ++v1) {
        const std::size_t s1 = static_cast<std::size_t>(2 * b1 + v1);
        LogSumExp lse;
        for (int v2 = 0; v2 <= 1; ++v2) {
          const double lv = (v1 == v2) ? logVoiceStay : logVoiceSwitch;
          for (int64_t b2 = lo; b2 <= hi; ++b2) {
            const std::size_t s2 = static_cast<std::size_t>(2 * b2 + v2);
            lse.add(lv + logPitch(b2 - b1) + nextObs[s2] + nextBeta[s2]);
          }
        }
        cur[s1] = lse.value();
      }
    }
  }
  // Per-frame log evidence logZ(f) = logsumexp_s (alpha[f][s] + beta[f][s])
  // (computed from the arrays — no homogeneity assumption).
  std::vector<double> logZ(static_cast<std::size_t>(F), kNegInf);
  for (int64_t f = 0; f < F; ++f) {
    LogSumExp lse;
    for (std::size_t s = 0; s < S; ++s) {
      lse.add(alpha[static_cast<std::size_t>(f)][s] + beta[static_cast<std::size_t>(f)][s]);
    }
    logZ[static_cast<std::size_t>(f)] = lse.value();
  }

  // Assemble the output track (§10.5 item 8): the Viterbi path's voicing +
  // the sub-bin refined frequency (the frame's candidate at the decoded bin,
  // nearest within +-1 bin; else the bin centre) + the FB voicing posterior.
  // TIMESTAMP (the evidence-center convention, corrected at implementation
  // time 2026-09-28): the YIN difference function's evidence at lag tau
  // spans the pairs [start, start + W + tau) — its least-biased position tag
  // is start + (W + tau_hat)/2 with the ESTIMATED period tau_hat = fs/f0,
  // NOT the window centre start + N/2 (tagging the estimate at the window
  // centre adds a systematic (N - W - tau)/2-sample bias — measured
  // ~0.83 hops at 440 Hz/48 kHz — to every lag comparison; the delayed-curve
  // diagnostic pinned it). Unvoiced frames keep the conventional window
  // centre (no evidence exists).
  const int64_t evidenceW = n - tauMax;  // W (constant summation length)
  track.frames.resize(static_cast<std::size_t>(F));
  for (int64_t f = 0; f < F; ++f) {
    PitchTrackFrame& out = track.frames[static_cast<std::size_t>(f)];
    out.start = starts[static_cast<std::size_t>(f)];
    out.center = out.start + n / 2;  // unvoiced default (conventional)
    const int64_t b = static_cast<int64_t>(path[static_cast<std::size_t>(f)]) / 2;
    const int v = static_cast<int>(path[static_cast<std::size_t>(f)]) % 2;
    out.voiced = (v == 1);
    if (out.voiced) {
      // Exact-bin candidate wins outright; otherwise the largest-mass
      // candidate among the +-1 neighbour bins (strict >, ascending bins).
      bool found = false;
      double bestMass = 0.0;
      double bestF = 0.0;
      for (int64_t db = -1; db <= 1; ++db) {
        const int64_t bb = b + db;
        if (bb < 0 || bb >= K) continue;
        const double mass = obsVoiced[static_cast<std::size_t>(f)][static_cast<std::size_t>(bb)];
        if (mass <= 0.0) continue;
        if (db == 0) {
          bestMass = mass;
          bestF = obsRefined[static_cast<std::size_t>(f)][static_cast<std::size_t>(bb)];
          found = true;
          break;
        }
        if (mass > bestMass) {
          bestMass = mass;
          bestF = obsRefined[static_cast<std::size_t>(f)][static_cast<std::size_t>(bb)];
          found = true;
        }
      }
      out.f0Hz = found ? bestF : binCentreHz(b);
      if (out.f0Hz > 0.0) {
        const double tauHat = fs / out.f0Hz;
        out.center =
            out.start + static_cast<int64_t>(std::llround((static_cast<double>(evidenceW) + tauHat) / 2.0));
      }
    } else {
      out.f0Hz = 0.0;
    }
    // Voicing posterior: P(voiced at f | all obs) = sum_b exp(alpha+beta-logZ).
    {
      LogSumExp lseV;
      for (int64_t bb = 0; bb < K; ++bb) {
        lseV.add(alpha[static_cast<std::size_t>(f)][static_cast<std::size_t>(2 * bb + 1)] +
                 beta[static_cast<std::size_t>(f)][static_cast<std::size_t>(2 * bb + 1)]);
      }
      const double p = std::exp(lseV.value() - logZ[static_cast<std::size_t>(f)]);
      out.voicedProbability = p >= 0.0 ? (p <= 1.0 ? p : 1.0) : 0.0;
    }
  }
  return track;
}

}  // namespace pitchlab::analysis
