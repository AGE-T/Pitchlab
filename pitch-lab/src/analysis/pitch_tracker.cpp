#include "analysis/pitch_tracker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "analysis/spectral.h"

// The vendored pocketfft header (external/pocketfft/, BSD-3-Clause,
// ORIGIN.toml-pinned — build spec §7). POCKETFFT_CACHE_SIZE 0: no hidden
// global plan cache; the analysis path owns its plan objects per
// computation (§10.4 item 1 / §10.5 item 1).
#define POCKETFFT_CACHE_SIZE 0
#include <pocketfft/pocketfft_hdronly.h>

namespace pitchlab::analysis {
namespace {

using pocketfft::detail::pocketfft_r;

// ---------------------------------------------------------------------------
// Regularised incomplete beta function I_x(a, b) — own clean-room code
// (the standard continued-fraction form; deterministic, libm only). Used for
// the Beta(2, 18) threshold-prior CDF (§10.5 item 5).
// ---------------------------------------------------------------------------

[[nodiscard]] double betacf(double a, double b, double x) {
  // Continued fraction for the incomplete beta function (modified Lentz).
  constexpr int kMaxIter = 200;
  constexpr double kEps = 3.0e-14;
  constexpr double kFpMin = 1.0e-300;
  const double qab = a + b;
  const double qap = a + 1.0;
  const double qam = a - 1.0;
  double c = 1.0;
  double d = 1.0 - qab * x / qap;
  if (std::fabs(d) < kFpMin) d = kFpMin;
  d = 1.0 / d;
  double h = d;
  for (int m = 1; m <= kMaxIter; ++m) {
    const int m2 = 2 * m;
    double aa = static_cast<double>(m) * (b - static_cast<double>(m)) * x /
                ((qam + static_cast<double>(m2)) * (a + static_cast<double>(m2)));
    d = 1.0 + aa * d;
    if (std::fabs(d) < kFpMin) d = kFpMin;
    c = 1.0 + aa / c;
    if (std::fabs(c) < kFpMin) c = kFpMin;
    d = 1.0 / d;
    h *= d * c;
    aa = -(a + static_cast<double>(m)) * (qab + static_cast<double>(m)) * x /
         ((a + static_cast<double>(m2)) * (qap + static_cast<double>(m2)));
    d = 1.0 + aa * d;
    if (std::fabs(d) < kFpMin) d = kFpMin;
    c = 1.0 + aa / c;
    if (std::fabs(c) < kFpMin) c = kFpMin;
    d = 1.0 / d;
    const double del = d * c;
    h *= del;
    if (std::fabs(del - 1.0) < kEps) break;
  }
  return h;
}

[[nodiscard]] double regularisedBeta(double a, double b, double x) {
  if (x <= 0.0) return 0.0;
  if (x >= 1.0) return 1.0;
  const double lbeta =
      std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) +
      a * std::log(x) + b * std::log1p(-x);
  const double front = std::exp(lbeta);
  if (x < (a + 1.0) / (a + b + 2.0)) {
    return front * betacf(a, b, x) / a;
  }
  return 1.0 - front * betacf(b, a, 1.0 - x) / b;
}

// ---------------------------------------------------------------------------
// Frozen pYIN constants (§10.5; every value recorded there with provenance)
// ---------------------------------------------------------------------------

constexpr int kThresholds = 100;         // the 100-point threshold grid
constexpr double kBetaA = 2.0;           // Beta(2, 18) prior (librosa public default)
constexpr double kBetaB = 18.0;
constexpr double kBoltzmannKappa = 2.0;  // the Boltzmann period prior, normalised form
constexpr double kNoTroughProb = 0.01;   // the no-trough bonus cap (librosa/paper value)
constexpr double kBinCents = 10.0;       // pitch-bin resolution (librosa resolution=0.1 st)
constexpr int kMaxJumpBins = 40;         // L: 4.0 st/frame max jump (§10.5 item 7)
constexpr double kPitchStick = 0.99;     // pitch self-transition mass
constexpr double kVoiceStay = 0.99;      // voicing stick (switch 0.01)
constexpr double kObsFloor = 1.0e-30;    // the log-domain observation/init floor
constexpr double kLogObsFloor = -69.07755278982137;  // log(1e-30)
constexpr double kNegInf = -1.0e300;

/// The frozen threshold-prior probabilities over the 100-point grid:
/// p_t = CDF(a_t) - CDF(a_{t-1}), a_t = 0.01*t, Beta(2, 18). Deterministic;
/// normalised so the total is exactly 1.0 (the last element absorbs the
/// rounding residue).
[[nodiscard]] std::vector<double> thresholdPrior() {
  std::vector<double> p(static_cast<std::size_t>(kThresholds));
  double prev = 0.0;  // CDF(0) = 0
  double sum = 0.0;
  for (int t = 1; t <= kThresholds; ++t) {
    const double cdf = regularisedBeta(kBetaA, kBetaB, 0.01 * static_cast<double>(t));
    p[static_cast<std::size_t>(t - 1)] = std::max(0.0, cdf - prev);
    sum += p[static_cast<std::size_t>(t - 1)];
    prev = cdf;
  }
  if (sum > 0.0 && sum != 1.0) {
    p[static_cast<std::size_t>(kThresholds - 1)] += 1.0 - sum;
  }
  return p;
}

/// Streaming log-sum-exp (online max rescaling; ascending add order — the
/// deterministic accumulation §10.5 item 1 requires).
class LogSumExp {
 public:
  void add(double t) {
    if (t == kNegInf) return;
    if (first_ || t > max_) {
      if (!first_) {
        acc_ *= std::exp(max_ - t);
      }
      max_ = t;
      acc_ += 1.0;
      first_ = false;
    } else {
      acc_ += std::exp(t - max_);
    }
  }
  [[nodiscard]] double value() const { return first_ ? kNegInf : max_ + std::log(acc_); }

 private:
  bool first_ = true;
  double max_ = kNegInf;
  double acc_ = 0.0;
};

// ---------------------------------------------------------------------------
// Per-frame observation model (Stages 1, 1b, 1c)
// ---------------------------------------------------------------------------

struct FrameObservation {
  struct Candidate {
    int64_t tau = 0;
    double fObs = 0.0;   // the parabolic-refined candidate frequency fs/(tau+delta)
    double mass = 0.0;   // the final observation probability P(tau)
  };
  std::vector<Candidate> candidates;  // ascending tau
  double unvoicedMass = 1.0;
};

/// The YIN difference function d(tau) for tau = 0..tauMax with the CONSTANT
/// summation length W = N - tauMax (§10.5 item 4), via the identity
/// d(tau) = E0 + E_tau - 2 r_W(tau): the energy terms from ascending prefix
/// sums of x^2; r_W(tau) = sum_{i<W} x[i] x[i+tau] as the FFT
/// cross-correlation of x[0..W) with x[0..N) (both zero-padded to 2N — two
/// r2c transforms, halfcomplex elementwise conj(A)*B, one r2c backward
/// transform; deterministic for a given size). The sum-of-squares is
/// non-negative mathematically; the FFT rounding residue is clamped at 0.
[[nodiscard]] std::vector<double> differenceFunction(const double* x, int64_t n,
                                                    int64_t tauMax) {
  const int64_t W = n - tauMax;  // constant summation length
  std::vector<double> d(static_cast<std::size_t>(tauMax + 1), 0.0);
  if (W <= 0) {
    for (int64_t tau = 1; tau <= tauMax; ++tau) d[static_cast<std::size_t>(tau)] = 1.0;
    return d;  // degenerate guard (unreachable: tauMax <= n/2 < n)
  }
  std::vector<double> s2(static_cast<std::size_t>(n + 1), 0.0);
  for (int64_t i = 0; i < n; ++i) {
    s2[static_cast<std::size_t>(i + 1)] =
        s2[static_cast<std::size_t>(i)] + x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)];
  }
  // Cross-correlation via FFT: P = 2N (>= N + W - 1 required for the linear
  // correlation; 2N >= N + N - tauMax - 1 holds since tauMax >= 1).
  const std::size_t P = static_cast<std::size_t>(2 * n);
  const std::size_t half = P / 2;
  std::vector<double> fa(P, 0.0), fb(P, 0.0);
  for (int64_t i = 0; i < W; ++i) fa[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(i)];
  for (int64_t i = 0; i < n; ++i) fb[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(i)];
  pocketfft_r<double> planA(P);
  pocketfft_r<double> planB(P);
  planA.exec(fa.data(), 1.0, true);
  planB.exec(fb.data(), 1.0, true);
  // conj(A)*B in the halfcomplex layout (the r2c spectrum of the REAL
  // cross-correlation), then one backward r2c transform with 1/P scaling.
  std::vector<double> prod(P, 0.0);
  {
    // k = 0 (both real).
    prod[0] = fa[0] * fb[0];
    for (std::size_t k = 1; k < half; ++k) {
      const double ar = fa[2 * k - 1], ai = fa[2 * k];
      const double br = fb[2 * k - 1], bi = fb[2 * k];
      prod[2 * k - 1] = ar * br + ai * bi;
      prod[2 * k] = ar * bi - ai * br;
    }
    // k = half (Nyquist bin; both real).
    prod[P - 1] = fa[P - 1] * fb[P - 1];
  }
  pocketfft_r<double> planBack(P);
  planBack.exec(prod.data(), 1.0 / static_cast<double>(P), false);
  const double e0 = s2[static_cast<std::size_t>(W)];
  for (int64_t tau = 0; tau <= tauMax; ++tau) {
    const double eTau =
        s2[static_cast<std::size_t>(tau + W)] - s2[static_cast<std::size_t>(tau)];
    double v = e0 + eTau - 2.0 * prod[static_cast<std::size_t>(tau)];
    if (v < 0.0) v = 0.0;
    d[static_cast<std::size_t>(tau)] = v;
  }
  return d;
}

/// The cumulative mean normalised difference (§10.5 item 4): d'(0) = 1;
/// d'(tau) = d(tau)*tau / sum_{j=1..tau} d(j) (running sum ascending;
/// denominator 0 => d' = 1, the guard).
[[nodiscard]] std::vector<double> cmndf(const std::vector<double>& d) {
  const int64_t tauMax = static_cast<int64_t>(d.size()) - 1;
  std::vector<double> dp(d.size(), 1.0);
  double run = 0.0;
  for (int64_t tau = 1; tau <= tauMax; ++tau) {
    run += d[static_cast<std::size_t>(tau)];
    if (run > 0.0) {
      dp[static_cast<std::size_t>(tau)] =
          d[static_cast<std::size_t>(tau)] * static_cast<double>(tau) / run;
    } else {
      dp[static_cast<std::size_t>(tau)] = 1.0;
    }
  }
  return dp;
}

/// Parabolic refinement of the RAW difference function at tau (§10.5 item 6,
/// YIN step 5): delta = 0.5 (d[tau-1] - d[tau+1]) / (d[tau-1] - 2 d[tau] +
/// d[tau+1]); denominator 0 => 0; clamped to +-0.5; edges (no neighbour) => 0.
[[nodiscard]] double parabolicShift(const std::vector<double>& d, int64_t tau) {
  if (tau < 1 || tau + 1 >= static_cast<int64_t>(d.size())) return 0.0;
  const double a = d[static_cast<std::size_t>(tau - 1)];
  const double b = d[static_cast<std::size_t>(tau)];
  const double c = d[static_cast<std::size_t>(tau + 1)];
  const double denom = a - 2.0 * b + c;
  if (denom == 0.0) return 0.0;
  double delta = 0.5 * (a - c) / denom;
  if (delta > 0.5) delta = 0.5;
  if (delta < -0.5) delta = -0.5;
  return delta;
}

/// One frame's full observation (§10.5 items 4-6): troughs (ascending tau)
/// with final masses; the unvoiced mass. Bin mapping happens at the HMM
/// assembly (needs fmin); the refined f_obs travels with the candidate.
[[nodiscard]] FrameObservation frameObservation(const double* x, int64_t n, double fs,
                                               int64_t tauMin, int64_t tauMax,
                                               const std::vector<double>& prior) {
  FrameObservation obs;
  const std::vector<double> d = differenceFunction(x, n, tauMax);
  const std::vector<double> dp = cmndf(d);

  // Local minima of d' over [tauMin, tauMax] (§10.5 item 5 boundary rules:
  // tau = tauMax compares the LEFT neighbour only — the right neighbour is
  // outside the defined range; tau = tauMin has both).
  std::vector<int64_t> troughs;
  for (int64_t tau = tauMin; tau <= tauMax; ++tau) {
    const bool left =
        tau == 0 || dp[static_cast<std::size_t>(tau)] < dp[static_cast<std::size_t>(tau - 1)];
    const bool right =
        tau == tauMax || dp[static_cast<std::size_t>(tau)] <= dp[static_cast<std::size_t>(tau + 1)];
    if (left && right) troughs.push_back(tau);
  }
  if (troughs.empty()) {
    obs.unvoicedMass = 1.0;
    return obs;
  }

  // Raw threshold masses: for each threshold, the FIRST trough below it
  // (ascending tau; a single ascending pointer suffices — the first trough
  // below a_t is the smallest tau with dp < a_t, and dp[troughs] is not
  // necessarily monotone, so the pointer must reset per threshold... no:
  // for threshold a_t, scan from the FIRST trough; the first below a_t is
  // found by ascending scan. Doing this per threshold is O(T * troughs);
  // with T = 100 and troughs typically < 20, that is trivial and CLEARLY
  // correct (no monotonicity assumption on dp).
  const std::size_t nt = troughs.size();
  std::vector<double> p0(nt, 0.0);
  double noTroughMass = 0.0;
  for (int t = 0; t < kThresholds; ++t) {
    const double a = 0.01 * static_cast<double>(t + 1);
    std::size_t chosen = nt;
    for (std::size_t i = 0; i < nt; ++i) {
      if (dp[static_cast<std::size_t>(troughs[i])] < a) {
        chosen = i;
        break;
      }
    }
    if (chosen < nt) {
      p0[chosen] += prior[static_cast<std::size_t>(t)];
    } else {
      noTroughMass += prior[static_cast<std::size_t>(t)];
    }
  }

  // No-trough handling (§10.5 item 5, mass-conserving): the global-minimum
  // trough (ties: smallest tau — the ascending scan with strict < keeps the
  // first) gains min(kNoTroughProb, M); U = M - bonus.
  std::size_t gIdx = 0;
  for (std::size_t i = 1; i < nt; ++i) {
    if (dp[static_cast<std::size_t>(troughs[i])] <
        dp[static_cast<std::size_t>(troughs[gIdx])]) {
      gIdx = i;
    }
  }
  double unvoiced = noTroughMass;
  if (noTroughMass > 0.0) {
    const double bonus = std::min(kNoTroughProb, noTroughMass);
    p0[gIdx] += bonus;
    unvoiced = noTroughMass - bonus;
  }
  const double voicedMass = 1.0 - unvoiced;

  // Boltzmann period prior (the normalised form, §10.5 item 5) + final masses.
  const double span = static_cast<double>(tauMax - tauMin);
  std::vector<double> w(nt, 1.0);
  double wsum = 0.0;
  for (std::size_t i = 0; i < nt; ++i) {
    const double u =
        span > 0.0 ? static_cast<double>(troughs[i] - tauMin) / span : 0.0;
    w[i] = std::exp(-kBoltzmannKappa * u);
    wsum += p0[i] * w[i];
  }
  obs.candidates.resize(nt);
  for (std::size_t i = 0; i < nt; ++i) {
    FrameObservation::Candidate& cand = obs.candidates[i];
    cand.tau = troughs[i];
    const double delta = parabolicShift(d, troughs[i]);
    const double tauStar = static_cast<double>(troughs[i]) + delta;
    cand.fObs = tauStar > 0.0 ? fs / tauStar : 0.0;
    cand.mass = wsum > 0.0 ? voicedMass * p0[i] * w[i] / wsum : 0.0;
  }
  obs.unvoicedMass = unvoiced;
  return obs;
}

[[nodiscard]] inline double safeLog(double p) {
  return p > kObsFloor ? std::log(p) : kLogObsFloor;
}

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
    FrameObservation obs = frameObservation(frame, n, fs, tauMin, tauMax, prior);
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
