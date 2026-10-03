#pragma once

// Pitch Lab — the SHARED pYIN observation model (internal header).
//
// §6.6.1 item 8 (task 33): stages 1/1b/1c (differenceFunction / cmndf /
// frameObservation / parabolicShift) are the ONE observation-math
// implementation, reused VERBATIM by the batch tracker (pitch_tracker.cpp —
// the metrics + parity reference, behaviour untouched) and the streaming
// fixed-lag tracker (pitch_tracker_streaming.cpp — the realtime path).
// This header is INTERNAL to the analysis library: no engine, harness or
// product code includes it directly.
//
// The math is verbatim from pitch_tracker.cpp as frozen at §10.5 (the
// clean-room pYIN; the provenance notes live there and are not repeated).
// The ONLY change is the SCRATCH PARAMETERISATION: every per-frame scratch
// vector and the FFT plans travel in ObservationScratch so the realtime
// path can allocate them ONCE in prepare() (the §6.6.1 item 16 rule: all
// plans created in prepare(); zero allocation in process()/finish()). The
// batch tracker constructs one Scratch per trackPitch call — identical
// arithmetic, identical results.
//
// DETERMINISM (§10.5 item 1, unchanged): no RNG, double throughout,
// -ffp-contract=off, ascending accumulation orders, pocketfft with
// POCKETFFT_CACHE_SIZE 0.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// The vendored pocketfft header (external/pocketfft/, BSD-3-Clause,
// ORIGIN.toml-pinned — build spec §7). POCKETFFT_CACHE_SIZE 0: no hidden
// global plan cache (§10.4 item 1 / §10.5 item 1).
#define POCKETFFT_CACHE_SIZE 0
#include <pocketfft/pocketfft_hdronly.h>

namespace pitchlab::analysis {
namespace detail {

using pocketfft::detail::pocketfft_r;

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

// ---------------------------------------------------------------------------
// Regularised incomplete beta function I_x(a, b) — own clean-room code
// (the standard continued-fraction form; deterministic, libm only). Used for
// the Beta(2, 18) threshold-prior CDF (§10.5 item 5).
// ---------------------------------------------------------------------------

[[nodiscard]] inline double betacf(double a, double b, double x) {
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

[[nodiscard]] inline double regularisedBeta(double a, double b, double x) {
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

/// The frozen threshold-prior probabilities over the 100-point grid:
/// p_t = CDF(a_t) - CDF(a_{t-1}), a_t = 0.01*t, Beta(2, 18). Deterministic;
/// normalised so the total is exactly 1.0 (the last element absorbs the
/// rounding residue).
[[nodiscard]] inline std::vector<double> thresholdPrior() {
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
// Per-frame observation model (Stages 1, 1b, 1c) — scratch-parameterised
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

/// The per-frame scratch (vectors + the FFT plans). Owned by the caller:
/// the batch tracker constructs one per trackPitch call (identical
/// behaviour); the streaming tracker owns ONE, allocated in prepare()
/// (the §6.6.1 item 16 rule — zero allocation in the realtime path).
struct ObservationScratch {
  // differenceFunction
  std::vector<double> s2;
  std::vector<double> fa, fb, prod;
  std::vector<double> d;
  // cmndf
  std::vector<double> dp;
  // frameObservation
  std::vector<int64_t> troughs;
  std::vector<double> p0, w;
  // the FFT plans (created once per size in prepare — §6.6.1 item 16)
  std::unique_ptr<pocketfft_r<double>> planA, planB, planBack;
  std::size_t planSize = 0;

  /// Idempotent: sizes the vectors for one (n, tauMax) geometry and creates
  /// the 2n-point plan trio ONCE. Call from prepare(); the realtime path
  /// never resizes.
  void ensureGeometry(int64_t n, int64_t tauMax) {
    const std::size_t P = static_cast<std::size_t>(2 * n);
    s2.assign(static_cast<std::size_t>(n + 1), 0.0);
    fa.assign(P, 0.0);
    fb.assign(P, 0.0);
    prod.assign(P, 0.0);
    d.assign(static_cast<std::size_t>(tauMax + 1), 0.0);
    dp.assign(static_cast<std::size_t>(tauMax + 1), 1.0);
    troughs.reserve(static_cast<std::size_t>(tauMax + 2));
    p0.assign(static_cast<std::size_t>(kMaxJumpBins) * 8 + 64, 0.0);
    w.assign(static_cast<std::size_t>(kMaxJumpBins) * 8 + 64, 0.0);
    if (planSize != P) {
      planA = std::make_unique<pocketfft_r<double>>(P);
      planB = std::make_unique<pocketfft_r<double>>(P);
      planBack = std::make_unique<pocketfft_r<double>>(P);
      planSize = P;
    }
  }
};

/// The YIN difference function d(tau) for tau = 0..tauMax with the CONSTANT
/// summation length W = N - tauMax (§10.5 item 4), via the identity
/// d(tau) = E0 + E_tau - 2 r_W(tau): the energy terms from ascending prefix
/// sums of x^2; r_W(tau) = sum_{i<W} x[i] x[i+tau] as the FFT
/// cross-correlation of x[0..W) with x[0..N) (both zero-padded to 2N — two
/// r2c transforms, halfcomplex elementwise conj(A)*B, one r2c backward
/// transform; deterministic for a given size). The sum-of-squares is
/// non-negative mathematically; the FFT rounding residue is clamped at 0.
inline void differenceFunction(const double* x, int64_t n, int64_t tauMax,
                               ObservationScratch& sc) {
  const int64_t W = n - tauMax;  // constant summation length
  if (W <= 0) {
    for (int64_t tau = 1; tau <= tauMax; ++tau) sc.d[static_cast<std::size_t>(tau)] = 1.0;
    return;  // degenerate guard (unreachable: tauMax <= n/2 < n)
  }
  for (int64_t i = 0; i < n; ++i) {
    sc.s2[static_cast<std::size_t>(i + 1)] =
        sc.s2[static_cast<std::size_t>(i)] + x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)];
  }
  // Cross-correlation via FFT: P = 2N (>= N + W - 1 required for the linear
  // correlation; 2N >= N + N - tauMax - 1 holds since tauMax >= 1).
  const std::size_t P = static_cast<std::size_t>(2 * n);
  const std::size_t half = P / 2;
  for (std::size_t i = 0; i < P; ++i) {
    sc.fa[i] = 0.0;
    sc.fb[i] = 0.0;
  }
  for (int64_t i = 0; i < W; ++i) sc.fa[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(i)];
  for (int64_t i = 0; i < n; ++i) sc.fb[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(i)];
  sc.planA->exec(sc.fa.data(), 1.0, true);
  sc.planB->exec(sc.fb.data(), 1.0, true);
  // conj(A)*B in the halfcomplex layout (the r2c spectrum of the REAL
  // cross-correlation), then one backward r2c transform with 1/P scaling.
  {
    // k = 0 (both real).
    sc.prod[0] = sc.fa[0] * sc.fb[0];
    for (std::size_t k = 1; k < half; ++k) {
      const double ar = sc.fa[2 * k - 1], ai = sc.fa[2 * k];
      const double br = sc.fb[2 * k - 1], bi = sc.fb[2 * k];
      sc.prod[2 * k - 1] = ar * br + ai * bi;
      sc.prod[2 * k] = ar * bi - ai * br;
    }
    // k = half (Nyquist bin; both real).
    sc.prod[P - 1] = sc.fa[P - 1] * sc.fb[P - 1];
  }
  sc.planBack->exec(sc.prod.data(), 1.0 / static_cast<double>(P), false);
  const double e0 = sc.s2[static_cast<std::size_t>(W)];
  for (int64_t tau = 0; tau <= tauMax; ++tau) {
    const double eTau =
        sc.s2[static_cast<std::size_t>(tau + W)] - sc.s2[static_cast<std::size_t>(tau)];
    double v = e0 + eTau - 2.0 * sc.prod[static_cast<std::size_t>(tau)];
    if (v < 0.0) v = 0.0;
    sc.d[static_cast<std::size_t>(tau)] = v;
  }
}

/// The cumulative mean normalised difference (§10.5 item 4): d'(0) = 1;
/// d'(tau) = d(tau)*tau / sum_{j=1..tau} d(j) (running sum ascending;
/// denominator 0 => d' = 1, the guard). Writes sc.dp; sc.dp[0..tauMax].
inline void cmndf(ObservationScratch& sc) {
  const int64_t tauMax = static_cast<int64_t>(sc.d.size()) - 1;
  sc.dp[0] = 1.0;
  double run = 0.0;
  for (int64_t tau = 1; tau <= tauMax; ++tau) {
    run += sc.d[static_cast<std::size_t>(tau)];
    if (run > 0.0) {
      sc.dp[static_cast<std::size_t>(tau)] =
          sc.d[static_cast<std::size_t>(tau)] * static_cast<double>(tau) / run;
    } else {
      sc.dp[static_cast<std::size_t>(tau)] = 1.0;
    }
  }
}

/// Parabolic refinement of the RAW difference function at tau (§10.5 item 6,
/// YIN step 5): delta = 0.5 (d[tau-1] - d[tau+1]) / (d[tau-1] - 2 d[tau] +
/// d[tau+1]); denominator 0 => 0; clamped to +-0.5; edges (no neighbour) => 0.
[[nodiscard]] inline double parabolicShift(const std::vector<double>& d, int64_t tau) {
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
/// Writes into `obs` (its candidates vector is cleared; the capacity
/// persists across frames — no allocation after the warm-up).
inline void frameObservation(const double* x, int64_t n, double fs,
                             int64_t tauMin, int64_t tauMax,
                             const std::vector<double>& prior,
                             ObservationScratch& sc, FrameObservation& obs) {
  obs.candidates.clear();
  obs.unvoicedMass = 1.0;
  differenceFunction(x, n, tauMax, sc);
  cmndf(sc);
  const std::vector<double>& dp = sc.dp;

  // Local minima of d' over [tauMin, tauMax] (§10.5 item 5 boundary rules:
  // tau = tauMax compares the LEFT neighbour only — the right neighbour is
  // outside the defined range; tau = tauMin has both).
  sc.troughs.clear();
  for (int64_t tau = tauMin; tau <= tauMax; ++tau) {
    const bool left =
        tau == 0 || dp[static_cast<std::size_t>(tau)] < dp[static_cast<std::size_t>(tau - 1)];
    const bool right =
        tau == tauMax || dp[static_cast<std::size_t>(tau)] <= dp[static_cast<std::size_t>(tau + 1)];
    if (left && right) sc.troughs.push_back(tau);
  }
  if (sc.troughs.empty()) {
    obs.unvoicedMass = 1.0;
    return;
  }

  // Raw threshold masses: for each threshold, the FIRST trough below it
  // (ascending tau; a single ascending pointer suffices — the first trough
  // below a_t is the smallest tau with dp < a_t, and dp[troughs] is not
  // necessarily monotone, so the pointer must reset per threshold... no:
  // for threshold a_t, scan from the FIRST trough; the first below a_t is
  // found by ascending scan. Doing this per threshold is O(T * troughs);
  // with T = 100 and troughs typically < 20, that is trivial and CLEARLY
  // correct (no monotonicity assumption on dp).
  const std::size_t nt = sc.troughs.size();
  sc.p0.assign(nt, 0.0);
  double noTroughMass = 0.0;
  for (int t = 0; t < kThresholds; ++t) {
    const double a = 0.01 * static_cast<double>(t + 1);
    std::size_t chosen = nt;
    for (std::size_t i = 0; i < nt; ++i) {
      if (dp[static_cast<std::size_t>(sc.troughs[i])] < a) {
        chosen = i;
        break;
      }
    }
    if (chosen < nt) {
      sc.p0[chosen] += prior[static_cast<std::size_t>(t)];
    } else {
      noTroughMass += prior[static_cast<std::size_t>(t)];
    }
  }

  // No-trough handling (§10.5 item 5, mass-conserving): the global-minimum
  // trough (ties: smallest tau — the ascending scan with strict < keeps the
  // first) gains min(kNoTroughProb, M); U = M - bonus.
  std::size_t gIdx = 0;
  for (std::size_t i = 1; i < nt; ++i) {
    if (dp[static_cast<std::size_t>(sc.troughs[i])] <
        dp[static_cast<std::size_t>(sc.troughs[gIdx])]) {
      gIdx = i;
    }
  }
  double unvoiced = noTroughMass;
  if (noTroughMass > 0.0) {
    const double bonus = std::min(kNoTroughProb, noTroughMass);
    sc.p0[gIdx] += bonus;
    unvoiced = noTroughMass - bonus;
  }
  const double voicedMass = 1.0 - unvoiced;

  // Boltzmann period prior (the normalised form, §10.5 item 5) + final masses.
  const double span = static_cast<double>(tauMax - tauMin);
  sc.w.assign(nt, 1.0);
  double wsum = 0.0;
  for (std::size_t i = 0; i < nt; ++i) {
    const double u =
        span > 0.0 ? static_cast<double>(sc.troughs[i] - tauMin) / span : 0.0;
    sc.w[i] = std::exp(-kBoltzmannKappa * u);
    wsum += sc.p0[i] * sc.w[i];
  }
  obs.candidates.resize(nt);
  for (std::size_t i = 0; i < nt; ++i) {
    FrameObservation::Candidate& cand = obs.candidates[i];
    cand.tau = sc.troughs[i];
    const double delta = parabolicShift(sc.d, sc.troughs[i]);
    const double tauStar = static_cast<double>(sc.troughs[i]) + delta;
    cand.fObs = tauStar > 0.0 ? fs / tauStar : 0.0;
    cand.mass = wsum > 0.0 ? voicedMass * sc.p0[i] * sc.w[i] / wsum : 0.0;
  }
  obs.unvoicedMass = unvoiced;
}

[[nodiscard]] inline double safeLog(double p) {
  return p > kObsFloor ? std::log(p) : kLogObsFloor;
}

}  // namespace detail
}  // namespace pitchlab::analysis
