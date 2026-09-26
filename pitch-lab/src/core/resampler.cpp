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
  const int k = spec.halfWidthTaps;
  const double invI0Beta = 1.0 / besselI0(spec.beta);

  const long long n0 = std::llround(position);
  double acc = 0.0;
  for (int i = -k; i <= k; ++i) {  // ascending tap order (§7.2.1 item 4)
    const long long n = n0 + i;
    if (n < 0 || n >= frameCount) {
      continue;  // zero-padding edge semantics (§7.6 / §7.2.1 item 5)
    }
    const double u = position - static_cast<double>(n);
    const double tap = kernelValueWithInv(u, cutoff, k, spec.beta, invI0Beta);
    if (tap != 0.0) {
      acc += x[n] * tap;
    }
  }
  return acc;
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
