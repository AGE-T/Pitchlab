#pragma once

// Pitch Lab — shared band-limited resampling primitive (implementation
// specification §7; frozen implementation behaviour §7.2.1, recorded
// 2026-09-26 BEFORE this code was written).
//
// The primitive (§7.2 model, §7.2.1 exact reading):
//   * One windowed-sinc interpolation kernel  h(u) = sinc_lp(c, u) · w(u)
//     with  sinc_lp(c, u) = c·sin(π·c·u)/(π·c·u)  (cutoff c in NYQUIST UNITS:
//     c = 1 <=> passband to Nyquist; c = 0.95/r for upward ratio r per §7.4),
//     and  w(u) = I0(β·sqrt(1−(u/K)²))/I0(β)  for |u| < K, else 0
//     (Kaiser window, own I0 series; ascending-term summation; deterministic).
//   * Taps n = llround(p)+i, i ∈ {−K..K}; summation in ascending i order,
//     double accumulator; direct sin() evaluation — NO tables (§7.2);
//     -ffp-contract=off on the core library (§7.5).
//   * Taps outside [0, frameCount) read ZERO (§7.6 zero-padding semantics,
//     both sides). The position itself is never clamped (caller owns it).
//   * Per-sample position advance (§7.5): read at p, then p += ratio_m.
//   * NO per-phase normalisation (ripple is measured, not forced — §7.2.1
//     item 8; a per-phase normalisation would break the LTI kernel).
//
// Determinism (§7.5): same-binary bit-exact for identical inputs; the
// reproducibility guarantee is same-binary, not cross-platform (libm sin
// accuracy varies) — cross-platform tests use tolerance bands.
//
// Invalid configuration => pitchlab::ConfigError (§4.8 vocabulary, §7.2.1
// item 10): cutoff outside (0, 1], non-finite cutoff/position/ratio,
// negative counts, null buffers with non-zero counts, in-place calls.

#include <vector>

#include "core/types.h"

namespace pitchlab {

enum class ResampleQuality {
  Small,      // K=8,  β=7.9  (~80 dB stopband) — vardelay/granular reads (§7.3)
  Standard,   // K=16, β=7.9              — pv.classic post-resample (§7.3)
  Reference,  // K=24, β=8.8 (~90 dB stopband) — varispeed reference engine (§7.3)
};

struct ResampleKernelSpec {
  int halfWidthTaps = 0;  // K (§7.2)
  double beta = 0.0;      // Kaiser β (§7.2)
};

/// The frozen preset table (§7.3 values, frozen by §7.2.1 item 7).
[[nodiscard]] ResampleKernelSpec resampleKernelSpec(ResampleQuality quality);

/// §7.4 cutoff policy as frozen by §7.2.1 item 6:
///   c(r) = (r > 1) ? 0.95 / r : 1.0
/// (full-band kernel for ratio <= 1 — required by the §7.7 identity
/// criterion; 5% margin below the fold point for ratio > 1).
/// NOTE: the refinement of the [RECOMMENDED] §7.4 formula at r <= 1 is
/// queued for owner ratification (OD-18) together with the transition-width
/// evidence (§7.7 measurements, tests/resampler_test.cpp).
[[nodiscard]] double antiAliasCutoff(double ratio);

/// The frozen kernel value h(u) = sinc_lp(c, u)·w(u) (§7.2.1 items 1-3) at
/// ANY real u. Public because it IS the primitive: unit tests verify the
/// acceptance criteria against it, the AA FIR is designed from it (§7.2.1
/// item 8: same machinery by construction), and future metrics verify kernel
/// behaviour through it. Cutoff is validated here (ConfigError).
[[nodiscard]] double resampleKernelValue(double u, double cutoff, ResampleQuality quality);

/// §7.2 fractional read: one interpolated sample of x at fractional input
/// position p (caller's accumulator, §7.2.1 item 9). Taps
/// n = llround(p) + i, i ∈ {−K..K}; x[n] outside [0, frameCount) reads zero
/// (§7.6). Deterministic; no allocation.
[[nodiscard]] double interpolateAt(const double* x, FrameCount frameCount, double position,
                                   double cutoff, ResampleQuality quality);

/// §7.5 per-sample-advance block resampling. Produces `outputFrames` output
/// samples: y[m] = interpolateAt(x, p_m); then p advances by the
/// instantaneous ratio (ratioPerOutputFrame[m] when non-null, else
/// constantRatio). `positionInOut` is the caller's double accumulator: it is
/// read as the position of y[0] and updated to the position of
/// y[outputFrames] (p after outputFrames ratio additions).
///
/// A single call and any sequence of block splits producing the identical
/// per-frame ratio sequence are bit-identical (§7.2.1 item 9; tested).
/// Ratios must be finite and strictly positive (curve contract §4.4.1).
/// outputFrames == 0 is a no-op (position unchanged). y must not alias x.
void resampleBlock(const double* x, FrameCount frameCount, double& positionInOut,
                   const double* ratioPerOutputFrame, double constantRatio, double* y,
                   FrameCount outputFrames, double cutoff, ResampleQuality quality);

/// §7.4 anti-alias pre-filter design: symmetric windowed-sinc FIR,
/// taps h[n] = kernelValue(n, cutoff) for n ∈ {−K..K} with the preset's
/// (K, β) — the SAME Kaiser machinery as the read kernel (§7.2.1 item 8).
/// Returned coefficients are ordered [n = −K .. +K] (length 2K+1).
/// Apply only when the effective ratio > 1 (§7.4); unnormalised (measured
/// ripple, §7.2.1 item 8).
[[nodiscard]] std::vector<double> designAntiAliasFir(double cutoff, ResampleQuality quality);

/// §7.4 offline zero-phase FIR application (forward convolution,
/// delay-compensated): y[k] = Σ_{j=−K..K} h[j]·x[k+j], x zero-padded outside
/// [0, frameCount). `firCoeffs` must contain 2·halfTaps+1 values ordered
/// [−K .. +K] (the designAntiAliasFir layout). y must not alias x.
void applyFirZeroPhase(const double* x, FrameCount frameCount, const double* firCoeffs,
                       int halfTaps, double* y);

}  // namespace pitchlab
