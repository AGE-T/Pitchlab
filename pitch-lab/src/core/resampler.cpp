#include "core/resampler.h"

#include <cmath>
#include <cstring>

#include "core/errors.h"

namespace pitchlab {
namespace {

constexpr double kPi = 3.14159265358979323846;  // pi to double precision

/// Modified Bessel function of the first kind, order 0 (own power series,
/// §7.2.1 item 3): I0(x) = Σ_k ((x²/4)^k)/(k!)²  — fixed ascending-term
/// summation order; early exit when a term falls below 1e-17·|sum|; at most
/// 48 iterations. Deterministic for identical x (same-binary bit-exact).
[[nodiscard]] double besselI0(double x) {
  const double t = x * x * 0.25;
  double term = 1.0;
  double sum = 1.0;
  for (int k = 1; k <= 48; ++k) {
    term *= t / (static_cast<double>(k) * static_cast<double>(k));
    sum += term;
    if (term < 1e-17 * sum) {
      break;
    }
  }
  return sum;
}

[[nodiscard]] ResampleKernelSpec validatedSpec(ResampleQuality quality) {
  const ResampleKernelSpec spec = resampleKernelSpec(quality);
  if (spec.halfWidthTaps < 1 || !(spec.beta > 0.0) || !std::isfinite(spec.beta)) {
    throw ConfigError("", "quality", "internal error: invalid preset (K/beta)");
  }
  return spec;
}

void validateCutoff(double cutoff) {
  if (!std::isfinite(cutoff) || cutoff <= 0.0 || cutoff > 1.0) {
    throw ConfigError("", "cutoff",
                      "cutoff must be in (0, 1] (Nyquist units); got " +
                          std::to_string(cutoff));
  }
}

/// Kernel value with a precomputed 1/I0(beta) (bit-identical to
/// resampleKernelValue — I0(beta) is the same deterministic function of the
/// same input whether computed once per call or once per tap).
[[nodiscard]] double kernelValueWithInv(double u, double cutoff, int kTaps, double beta,
                                        double invI0Beta) {
  const double au = std::fabs(u);
  if (au >= static_cast<double>(kTaps)) {
    return 0.0;  // window support (§7.2.1 item 3)
  }
  double s;
  if (u == 0.0) {
    s = cutoff;  // sinc_lp(c, 0) = c
  } else {
    const double a = kPi * cutoff * u;
    s = cutoff * std::sin(a) / a;
  }
  const double r = u / static_cast<double>(kTaps);
  const double w = besselI0(beta * std::sqrt(1.0 - r * r)) * invI0Beta;
  return s * w;
}

// ---------------------------------------------------------------------------
// 2026-11 PERFORMANCE: the tap loop no longer evaluates sin() + the Bessel
// series per TAP (measured: 18.7 % of realtime per mono 96 kHz channel at
// the Reference preset — the varispeed/granular CPU wall). The kernel is
//   k(u) = sin(pi*c*u)/(pi*u) * I0(beta*sqrt(1-(u/K)^2)) / I0(beta)
// and the two factors are produced cheaply:
//   * the Kaiser factor as g(t) = I0(beta*sqrt(t))/I0(beta), t = 1-(u/K)^2:
//     an ENTIRE function of t (no sqrt singularity at the support edge),
//     tabulated once at load time per beta (8192 cells, value + exact
//     derivative) and read by cubic Hermite interpolation — relative error
//     ~1e-13 (far below the 64-bit summation noise of the 49-tap sum);
//   * the sinc numerator by an angle-rotation recurrence across the taps
//     (4 trig calls per output sample instead of one per tap).
// resampleKernelValue() / designAntiAliasFir() keep the exact series
// (the reference definition); only the interpolation hot paths use this.
// ---------------------------------------------------------------------------

struct KaiserTable {
  static constexpr int kCells = 8192;
  double beta = 0.0;
  double g[kCells + 1] = {};
  double dg[kCells + 1] = {};  // derivative scaled by the cell width (Hermite form)

  explicit KaiserTable(double b) : beta(b) {
    const double q = beta * beta * 0.25;
    const double invI0 = 1.0 / besselI0(beta);
    for (int j = 0; j <= kCells; ++j) {
      const double t = static_cast<double>(j) / kCells;
      // I0(beta*sqrt(t)) = sum_k (q t)^k / (k!)^2 ; d/dt = sum_k k q^k t^(k-1) / (k!)^2
      double term = 1.0;   // (q t)^k / (k!)^2 at k = 0
      double sum = 1.0;
      double dsum = 0.0;
      double dterm = q;    // k q^k t^(k-1) / (k!)^2 at k = 1
      for (int k = 1; k <= 80; ++k) {
        term *= q * t / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (k > 1) dterm *= q * t / (static_cast<double>(k) * static_cast<double>(k - 1));
        dsum += dterm;
        if (term < 1e-19 * sum && dterm < 1e-19 * (dsum + 1e-300)) break;
      }
      g[j] = sum * invI0;
      dg[j] = dsum * invI0 / kCells;
    }
  }

  /// g(t) for t in [0, 1] (cubic Hermite on the cell)
  [[nodiscard]] double at(double t) const {
    double x = t * kCells;
    int j = static_cast<int>(x);
    if (j >= kCells) j = kCells - 1;
    x -= j;
    const double x2 = x * x;
    const double x3 = x2 * x;
    return (2.0 * x3 - 3.0 * x2 + 1.0) * g[j] + (x3 - 2.0 * x2 + x) * dg[j] +
           (-2.0 * x3 + 3.0 * x2) * g[j + 1] + (x3 - x2) * dg[j + 1];
  }
};

// built at load time (static storage, no heap): the audio thread never
// builds a table. Small and Standard share beta = 7.9.
const KaiserTable kKaiser79(7.9);
const KaiserTable kKaiser88(8.8);

[[nodiscard]] const KaiserTable& kaiserFor(double beta) {
  return beta == 8.8 ? kKaiser88 : kKaiser79;
}

/// The fast 2K+1-tap sum: x is indexed by (n - base); taps outside
/// [0, frameCount) read zero. Ascending tap order (§7.2.1 item 4).
[[nodiscard]] double fastTapSum(const double* x, long long frameCount, long long n0,
                                long long base, double f, double cutoff, int k,
                                const KaiserTable& table) {
  const double pic = kPi * cutoff;
  const double invK = 1.0 / static_cast<double>(k);
  // angle of the first tap (i = -k): pi*c*(f + k); each tap subtracts pi*c
  double sa = std::sin(pic * (f + static_cast<double>(k)));
  double ca = std::cos(pic * (f + static_cast<double>(k)));
  const double st = std::sin(pic);
  const double ct = std::cos(pic);
  double acc = 0.0;
  for (int i = -k; i <= k; ++i) {
    const long long idx = n0 + i - base;
    if (idx >= 0 && idx < frameCount) {
      const double u = f - static_cast<double>(i);
      const double r = u * invK;
      const double t = 1.0 - r * r;
      if (t > 0.0) {  // |u| < K: inside the window support (§7.2.1 item 3)
        const double sinc = (u == 0.0) ? cutoff : sa / (kPi * u);
        acc += x[idx] * (sinc * table.at(t));
      }
    }
    const double ns = sa * ct - ca * st;  // sin(a - pi*c)
    const double nc = ca * ct + sa * st;  // cos(a - pi*c)
    sa = ns;
    ca = nc;
  }
  return acc;
}

}  // namespace

ResampleKernelSpec resampleKernelSpec(ResampleQuality quality) {
  // §7.3 preset table (frozen by §7.2.1 item 7).
  switch (quality) {
    case ResampleQuality::Small:
      return ResampleKernelSpec{8, 7.9};
    case ResampleQuality::Standard:
      return ResampleKernelSpec{16, 7.9};
    case ResampleQuality::Reference:
      return ResampleKernelSpec{24, 8.8};
  }
  return ResampleKernelSpec{0, 0.0};  // unreachable (all enum values covered)
}

double antiAliasCutoff(double ratio) {
  if (!std::isfinite(ratio) || ratio <= 0.0) {
    throw ConfigError("", "ratio",
                      "ratio must be finite and strictly positive; got " +
                          std::to_string(ratio));
  }
  // §7.2.1 item 6 (⟡ queued for owner ratification, OD-18).
  return (ratio > 1.0) ? 0.95 / ratio : 1.0;
}

double resampleKernelValue(double u, double cutoff, ResampleQuality quality) {
  validateCutoff(cutoff);
  if (!std::isfinite(u)) {
    throw ConfigError("", "u", "kernel argument must be finite");
  }
  const ResampleKernelSpec spec = validatedSpec(quality);
  return kernelValueWithInv(u, cutoff, spec.halfWidthTaps, spec.beta,
                            1.0 / besselI0(spec.beta));
}

double interpolateAtAbs(const double* x, FrameCount frameCount, int64_t baseIndex,
                       double position, double cutoff, ResampleQuality quality) {
  // §7.2.2: the exact-fraction, placement-invariant variant. Same validation
  // and kernel machinery as interpolateAt; the tap set and the fraction are
  // computed from the ABSOLUTE position only.
  validateCutoff(cutoff);
  if (!std::isfinite(position)) {
    throw ConfigError("", "position", "read position must be finite");
  }
  if (std::fabs(position) >= 9.2e18) {
    throw ConfigError("", "position", "read position magnitude exceeds the int64 frame range");
  }
  if (frameCount < 0) {
    throw ConfigError("", "frameCount", "frame count must be >= 0");
  }
  if (x == nullptr && frameCount > 0) {
    throw ConfigError("", "x", "input buffer is null with non-zero frame count");
  }
  const ResampleKernelSpec spec = validatedSpec(quality);
  const long long n0 = std::llround(position);
  const double u0 = position - static_cast<double>(n0);  // Sterbenz-exact
  return fastTapSum(x, static_cast<long long>(frameCount), n0,
                    static_cast<long long>(baseIndex), u0, cutoff, spec.halfWidthTaps,
                    kaiserFor(spec.beta));
}

double interpolateAt(const double* x, FrameCount frameCount, double position, double cutoff,
                     ResampleQuality quality) {
  validateCutoff(cutoff);
  if (!std::isfinite(position)) {
    throw ConfigError("", "position", "read position must be finite");
  }
  // Guard llround's domain: positions beyond the int64 range have no
  // representable tap centre (frame counts are int64 by contract).
  if (std::fabs(position) >= 9.2e18) {
    throw ConfigError("", "position", "read position magnitude exceeds the int64 frame range");
  }
  if (frameCount < 0) {
    throw ConfigError("", "frameCount", "frame count must be >= 0");
  }
  if (x == nullptr && frameCount > 0) {
    throw ConfigError("", "x", "input buffer is null with non-zero frame count");
  }
  const ResampleKernelSpec spec = validatedSpec(quality);
  const long long n0 = std::llround(position);
  const double f = position - static_cast<double>(n0);
  return fastTapSum(x, static_cast<long long>(frameCount), n0, 0, f, cutoff,
                    spec.halfWidthTaps, kaiserFor(spec.beta));
}

void resampleBlock(const double* x, FrameCount frameCount, double& positionInOut,
                   const double* ratioPerOutputFrame, double constantRatio, double* y,
                   FrameCount outputFrames, double cutoff, ResampleQuality quality) {
  validateCutoff(cutoff);
  if (!std::isfinite(positionInOut)) {
    throw ConfigError("", "position", "read position must be finite");
  }
  if (frameCount < 0 || outputFrames < 0) {
    throw ConfigError("", "frameCount/outputFrames", "frame counts must be >= 0");
  }
  if (x == nullptr && frameCount > 0) {
    throw ConfigError("", "x", "input buffer is null with non-zero frame count");
  }
  if (y == nullptr && outputFrames > 0) {
    throw ConfigError("", "y", "output buffer is null with non-zero output count");
  }
  if (y == x && y != nullptr) {
    throw ConfigError("", "y", "in-place processing is prohibited (spec §5 rule 2)");
  }
  if (ratioPerOutputFrame == nullptr) {
    if (!std::isfinite(constantRatio) || constantRatio <= 0.0) {
      throw ConfigError("", "ratio",
                        "constant ratio must be finite and strictly positive; got " +
                            std::to_string(constantRatio));
    }
  } else {
    for (FrameCount m = 0; m < outputFrames; ++m) {
      if (!std::isfinite(ratioPerOutputFrame[m]) || ratioPerOutputFrame[m] <= 0.0) {
        throw ConfigError("", "ratio",
                          "ratio at output frame " + std::to_string(m) +
                              " must be finite and strictly positive");
      }
    }
  }

  // All validation complete before any output is written (fail-fast; a
  // rejected call leaves the caller's buffers untouched).
  double p = positionInOut;
  for (FrameCount m = 0; m < outputFrames; ++m) {
    const double ratio =
        (ratioPerOutputFrame != nullptr) ? ratioPerOutputFrame[m] : constantRatio;
    y[m] = interpolateAt(x, frameCount, p, cutoff, quality);
    p += ratio;  // per-sample advance (§7.5 / §7.2.1 item 9)
  }
  positionInOut = p;
}

std::vector<double> designAntiAliasFir(double cutoff, ResampleQuality quality) {
  validateCutoff(cutoff);
  const ResampleKernelSpec spec = validatedSpec(quality);
  const int k = spec.halfWidthTaps;
  std::vector<double> h(static_cast<std::size_t>(2 * k + 1));
  const double invI0Beta = 1.0 / besselI0(spec.beta);
  for (int n = -k; n <= k; ++n) {
    h[static_cast<std::size_t>(n + k)] =
        kernelValueWithInv(static_cast<double>(n), cutoff, k, spec.beta, invI0Beta);
  }
  return h;
}

void applyFirZeroPhase(const double* x, FrameCount frameCount, const double* firCoeffs,
                       int halfTaps, double* y) {
  if (halfTaps < 0) {
    throw ConfigError("", "halfTaps", "halfTaps must be >= 0");
  }
  if (frameCount < 0) {
    throw ConfigError("", "frameCount", "frame count must be >= 0");
  }
  if (firCoeffs == nullptr && halfTaps > 0) {
    throw ConfigError("", "firCoeffs", "FIR coefficients are null with non-zero halfTaps");
  }
  if (x == nullptr && frameCount > 0) {
    throw ConfigError("", "x", "input buffer is null with non-zero frame count");
  }
  if (y == nullptr && frameCount > 0) {
    throw ConfigError("", "y", "output buffer is null with non-zero frame count");
  }
  if (y == x && y != nullptr) {
    throw ConfigError("", "y", "in-place processing is prohibited (spec §5 rule 2)");
  }

  const int k = halfTaps;
  for (FrameCount idx = 0; idx < frameCount; ++idx) {
    double acc = 0.0;
    for (int j = -k; j <= k; ++j) {  // ascending tap order (determinism)
      const long long n = static_cast<long long>(idx) + j;
      if (n < 0 || n >= frameCount) {
        continue;  // zero-padded edges (§7.4 / §7.2.1 item 8)
      }
      acc += firCoeffs[static_cast<std::size_t>(j + k)] * x[n];
    }
    y[idx] = acc;
  }
}

}  // namespace pitchlab
