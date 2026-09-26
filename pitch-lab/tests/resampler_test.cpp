// T-R1 / T-R2 — shared resampler acceptance + behaviour (implementation
// specification §7.7 / §7.2.1; recorded measurements below are EMPIRICAL
// FACTS from the frozen implementation, GCC 14.2 local, 2026-09-26).
//
// §7.7 acceptance status (kernel-level, cutoff c=0.475, worst over 64
// fractional phases — see the OD-18 evidence printed by this test):
//   criterion                      | small    | standard | reference | verdict
//   stopband >= 80/80/90 dB        | -75.9 dB | -78.0 dB | -87.3 dB  | NOT MET (2-4 dB short)
//   transition <= 5% of Nyquist    | 31.6%    | 16.0%    | 11.8%     | NOT MET (2.3-6.3x over)
//   passband ripple <= ±0.1 dB     | ±0.002   | ±0.0012  | ±0.0004   | MET (50x margin)
//   ratio-1 identity -80 dBFS      | -309 dBFS (3.3e-16)            | MET (far beyond)
//   DC/Nyquist no NaN, bounded     | verified below                 | MET
//   composite AA (§7.4 path, r=2)  | -179 dB stopband probe         | MET (far beyond)
//
// The unmet kernel-level criteria are NOT silently weakened: they are
// measured, printed as evidence, and recorded as open decision OD-18 (owner
// ratifies: current constants, larger beta, or larger K). What IS asserted:
//   (a) the §7.7 criteria that genuinely hold (ripple, identity, DC/Nyquist,
//       composite alias suppression);
//   (b) clearly-labelled REGRESSION BANDS pinning the frozen behaviour
//       (headroom >= 4x over the 2026-09-26 measurements — these are NOT
//       §7.7 acceptance values).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/errors.h"
#include "core/resampler.h"
#include "core/rng.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTestCutoff = 0.475;  // antiAliasCutoff(2.0) — representative upward ratio

struct PresetInfo {
  const char* name;
  pitchlab::ResampleQuality quality;
  double targetStopbandDb;
};

const PresetInfo kPresets[] = {
    {"small", pitchlab::ResampleQuality::Small, 80.0},
    {"standard", pitchlab::ResampleQuality::Standard, 80.0},
    {"reference", pitchlab::ResampleQuality::Reference, 90.0},
};

/// DTFT magnitude of a tap set (array-index j <-> tap offset i = j - K).
double dtftMag(const std::vector<double>& taps, double f) {
  double re = 0.0, im = 0.0;
  for (std::size_t j = 0; j < taps.size(); ++j) {
    const double arg = 2.0 * kPi * f * static_cast<double>(j);
    re += taps[j] * std::cos(arg);
    im += taps[j] * std::sin(arg);
  }
  return std::sqrt(re * re + im * im);
}

/// Subfilter taps for fractional phase d: h_d[i] = kernel(d - i), i in [-K,K].
std::vector<double> phaseTaps(double d, double cutoff, pitchlab::ResampleQuality q) {
  const int k = pitchlab::resampleKernelSpec(q).halfWidthTaps;
  std::vector<double> taps(static_cast<std::size_t>(2 * k + 1));
  for (int i = -k; i <= k; ++i) {
    taps[static_cast<std::size_t>(i + k)] = pitchlab::resampleKernelValue(d - static_cast<double>(i), cutoff, q);
  }
  return taps;
}

double toDb(double m) { return 20.0 * std::log10(m); }

/// Full kernel-level measurement for one preset (worst over 64 phases).
struct KernelMeasurement {
  double fA;              // worst-phase first crossing of the -A dB line (cycles/sample)
  double floorDb;         // worst |H| (dB) at f >= fA over all phases
  double rippleLoDb;      // min |H| (dB) over [0, c/2 - T_A]
  double rippleHiDb;      // max |H| (dB) over [0, c/2 - T_A]
};

KernelMeasurement measureKernel(const PresetInfo& preset, double cutoff, int phaseCount) {
  const int FPTS = 2048;
  const double aLin = std::pow(10.0, -preset.targetStopbandDb / 20.0);
  KernelMeasurement m{0.0, -999.0, +999.0, -999.0};
  // pass 1: worst-phase first crossing of -A
  for (int ph = 0; ph < phaseCount; ++ph) {
    const auto taps = phaseTaps(static_cast<double>(ph) / phaseCount, cutoff, preset.quality);
    for (int fi = 0; fi < FPTS; ++fi) {
      const double f = 0.5 * fi / FPTS;
      if (dtftMag(taps, f) <= aLin) {
        m.fA = std::max(m.fA, f);
        break;
      }
    }
  }
  const double T = m.fA - cutoff / 2.0;
  // pass 2: stopband floor + passband ripple (over the T_A-based region)
  for (int ph = 0; ph < phaseCount; ++ph) {
    const auto taps = phaseTaps(static_cast<double>(ph) / phaseCount, cutoff, preset.quality);
    for (int fi = 0; fi < FPTS; ++fi) {
      const double f = 0.5 * fi / FPTS;
      const double mag = dtftMag(taps, f);
      if (f >= m.fA) {
        m.floorDb = std::max(m.floorDb, toDb(mag));
      }
      if (f <= cutoff / 2.0 - T) {
        m.rippleLoDb = std::min(m.rippleLoDb, toDb(mag));
        m.rippleHiDb = std::max(m.rippleHiDb, toDb(mag));
      }
    }
  }
  return m;
}

double maxAbsError(const std::vector<double>& a, const std::vector<double>& b, std::size_t skip) {
  REQUIRE(a.size() == b.size());
  double e = 0.0;
  for (std::size_t i = skip; i < a.size() - skip; ++i) {
    e = std::max(e, std::fabs(a[i] - b[i]));
  }
  return e;
}

std::vector<double> makeNoise(std::size_t n, uint64_t seed) {
  auto rng = pitchlab::makeConsumerStream(seed, "res-test");
  std::vector<double> v(n);
  for (auto& x : v) x = rng.nextDouble01() * 2.0 - 1.0;
  return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// §7.3 preset table (frozen by §7.2.1 item 7)
// ---------------------------------------------------------------------------

TEST_CASE("T-R0a: preset table has the frozen §7.3 values") {
  const auto s = pitchlab::resampleKernelSpec(pitchlab::ResampleQuality::Small);
  CHECK(s.halfWidthTaps == 8);
  CHECK(s.beta == doctest::Approx(7.9));
  const auto st = pitchlab::resampleKernelSpec(pitchlab::ResampleQuality::Standard);
  CHECK(st.halfWidthTaps == 16);
  CHECK(st.beta == doctest::Approx(7.9));
  const auto r = pitchlab::resampleKernelSpec(pitchlab::ResampleQuality::Reference);
  CHECK(r.halfWidthTaps == 24);
  CHECK(r.beta == doctest::Approx(8.8));
}

TEST_CASE("T-R0b: cutoff policy c(r) = (r>1) ? 0.95/r : 1 (§7.2.1 item 6)") {
  CHECK(pitchlab::antiAliasCutoff(0.5) == 1.0);   // downward: full band
  CHECK(pitchlab::antiAliasCutoff(1.0) == 1.0);   // identity: full band (identity criterion)
  CHECK(pitchlab::antiAliasCutoff(2.0) == doctest::Approx(0.475));
  CHECK(pitchlab::antiAliasCutoff(4.0) == doctest::Approx(0.2375));
  CHECK_THROWS_AS((void)pitchlab::antiAliasCutoff(0.0), pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::antiAliasCutoff(-1.0), pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::antiAliasCutoff(std::nan("")), pitchlab::ConfigError);
}

TEST_CASE("T-R0c: kernel exact properties (h(0)=c; even; zero beyond support)") {
  const double c = 0.8;
  CHECK(pitchlab::resampleKernelValue(0.0, c, pitchlab::ResampleQuality::Reference) == c);
  for (const double u : {0.3, 1.7, 5.9, -2.3}) {
    const double a = pitchlab::resampleKernelValue(u, c, pitchlab::ResampleQuality::Standard);
    const double b = pitchlab::resampleKernelValue(-u, c, pitchlab::ResampleQuality::Standard);
    CHECK(std::fabs(a - b) <= 1e-12 * std::fabs(a) + 1e-15);  // even kernel
  }
  CHECK(pitchlab::resampleKernelValue(16.0, c, pitchlab::ResampleQuality::Standard) == 0.0);
  CHECK(pitchlab::resampleKernelValue(16.5, c, pitchlab::ResampleQuality::Standard) == 0.0);
  CHECK(pitchlab::resampleKernelValue(-17.0, c, pitchlab::ResampleQuality::Standard) == 0.0);
  CHECK_THROWS_AS((void)pitchlab::resampleKernelValue(0.0, 1.5, pitchlab::ResampleQuality::Small),
                  pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::resampleKernelValue(0.0, 0.0, pitchlab::ResampleQuality::Small),
                  pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::resampleKernelValue(std::nan(""), 0.5, pitchlab::ResampleQuality::Small),
                  pitchlab::ConfigError);
}

// ---------------------------------------------------------------------------
// T-R1 — §7.7 kernel acceptance criteria (measured evidence + honest verdicts)
// ---------------------------------------------------------------------------

TEST_CASE("T-R1: §7.7 kernel stopband/transition/ripple (evidence + OD-18)") {
  for (const auto& preset : kPresets) {
    const auto m = measureKernel(preset, kTestCutoff, 64);
    const double T = m.fA - kTestCutoff / 2.0;

    // ---- EVIDENCE (printed; lands in the CTest JUnit output) — OD-18 ----
    std::printf("[T-R1][OD-18 evidence] preset=%-9s cutoff=%.3f: worst-phase -%.0fdB point f=%.5f "
                "cyc; transition T=%.5f cyc = %.1f%% of Nyquist (criterion: 5%%); kernel floor "
                "beyond f_A = %.1f dB (criterion: -%.0f dB); passband ripple over [0, c/2-T] = "
                "[%.4f, %.4f] dB (criterion: +-0.1 dB)\n",
                preset.name, kTestCutoff, preset.targetStopbandDb, m.fA, T, 200.0 * T, m.floorDb,
                preset.targetStopbandDb, m.rippleLoDb, m.rippleHiDb);

    // ---- §7.7 criteria that HOLD (asserted at the spec values) ----
    // Passband ripple <= +-0.1 dB below cutoff - T_A (measured: 0.002 max).
    CHECK(m.rippleLoDb > -0.1);
    CHECK(m.rippleHiDb < 0.1);
    // The -A point must exist inside Nyquist (the kernel does band-limit).
    CHECK(m.fA > kTestCutoff / 2.0);
    CHECK(m.fA < 0.5);

    // ---- REGRESSION BANDS (NOT §7.7 acceptance values; pin the frozen
    // behaviour; >= 4x/5.9 dB headroom over the 2026-09-26 measurements) ----
    CHECK(m.floorDb < -70.0);   // measured: -75.9 / -78.0 / -87.3
    CHECK(T < 0.40);            // measured: 0.316 / 0.160 / 0.118 (cycles = fraction of 2*Ny)
  }
}

TEST_CASE("T-R1b: white-noise power transfer matches the kernel DTFT (LTI sanity)") {
  // §7.7 names white-noise injection as a measurement method. For an LTI
  // kernel the DTFT of the phase subfilters IS the truth; this test confirms
  // the LTI claim empirically: filtering white noise at a FIXED fractional
  // read offset reproduces |H_d(f)|^2 as the output/input band-power ratio
  // (within the chi^2 estimation noise of a fixed seeded stream).
  const double c = 0.7;
  const double d = 0.25;
  const auto q = pitchlab::ResampleQuality::Reference;
  const std::size_t N = 16384;
  const auto x = makeNoise(N, 31);

  // y[m] = interpolateAt(x, m + d): constant fractional offset (phase d).
  std::vector<double> y(N, 0.0);
  for (std::size_t m = 0; m + 24 < N; ++m) {
    y[m] = pitchlab::interpolateAt(x.data(), static_cast<pitchlab::FrameCount>(N),
                                   static_cast<double>(m) + d, c, q);
  }
  // Band powers via Hann-windowed single-bin sums over a mid region (the
  // window suppresses the rectangular-window leakage floor so the stopband
  // measurement can see the kernel attenuation itself).
  auto bandPower = [&](const std::vector<double>& s, double f0, double f1) {
    double acc = 0.0;
    const std::size_t lo = 2000, hi = s.size() - 1000;
    const double L = static_cast<double>(hi - lo);
    for (double f = f0; f <= f1; f += 0.01) {
      double re = 0.0, im = 0.0, wsum = 0.0;
      for (std::size_t n = lo; n < hi; ++n) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(n - lo) / L));
        re += w * s[n] * std::cos(2.0 * kPi * f * static_cast<double>(n));
        im += w * s[n] * std::sin(2.0 * kPi * f * static_cast<double>(n));
        wsum += w;
      }
      acc += (re * re + im * im) / (wsum * wsum) * L;
    }
    return acc;
  };
  const double yPass = bandPower(y, 0.05, 0.20);
  const double yStop = bandPower(y, 0.43, 0.48);  // safely beyond the -A point for c=0.7
  const double xPass = bandPower(x, 0.05, 0.20);
  // Passband: y power ~ x power (|H| ~ 1, ripple small). Stopband: y power at
  // least 60 dB below x power in the same band.
  CHECK(yPass / xPass > 0.5);
  CHECK(yStop / xPass < 1e-6);  // >= 60 dB attenuation (measured far below)
}

// ---------------------------------------------------------------------------
// T-R2 — §7.7 identity, DC/Nyquist, and the mandated behaviour tests
// ---------------------------------------------------------------------------

TEST_CASE("T-R2a: ratio-1 identity (§7.7: output == input within -80 dBFS)") {
  for (const auto& preset : kPresets) {
    const int N = 5000;
    const auto x = makeNoise(N, 100 + static_cast<int>(preset.quality));
    std::vector<double> y(N);
    double pos = 0.0;
    pitchlab::resampleBlock(x.data(), N, pos, nullptr, 1.0, y.data(), N, 1.0, preset.quality);
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) maxErr = std::max(maxErr, std::fabs(y[i] - x[i]));
    // Measured 2026-09-26: 3.3e-16 (small: 2.2e-16) = -309 dBFS.
    std::printf("[T-R2a] preset=%-9s identity max|y-x| = %.3g (-80 dBFS criterion: %s)\n",
                preset.name, maxErr, (maxErr < 1e-4 ? "MET" : "NOT MET"));
    CHECK(maxErr < 1e-4);   // the §7.7 criterion (-80 dBFS on unit-peak signals)
    CHECK(maxErr < 1e-12);  // regression band: 3000x headroom over measured 3.3e-16
    // Position advanced by exactly N sequential additions of 1.0.
    CHECK(pos == static_cast<double>(N));
  }
}

TEST_CASE("T-R2b: impulse input at ratio 1 is (numerically) the delta kernel") {
  std::vector<double> x(100, 0.0);
  x[50] = 1.0;
  const double got = pitchlab::interpolateAt(x.data(), 100, 50.0, 1.0,
                                             pitchlab::ResampleQuality::Reference);
  CHECK(got == doctest::Approx(1.0).epsilon(1e-9));
  // Off-impulse integer positions: kernel ~ 0 (sinc zeros, numerically ~1e-16).
  const double gotOff = pitchlab::interpolateAt(x.data(), 100, 51.0, 1.0,
                                                pitchlab::ResampleQuality::Reference);
  CHECK(std::fabs(gotOff) < 1e-15);
}

TEST_CASE("T-R2c: DC and Nyquist edge cases (§7.7: no NaN/Inf; bounded error)") {
  const int N = 8000;
  // DC: constant input, downward ratio (no AA needed, full-band kernel).
  {
    std::vector<double> x(N, 1.0), y(N / 2);
    double pos = 0.0;
    pitchlab::resampleBlock(x.data(), N, pos, nullptr, 0.5, y.data(), N / 2, 1.0,
                            pitchlab::ResampleQuality::Reference);
    // Steady-state DC accuracy over the interior (edge frames carry the
    // documented zero-padding transient of §7.6 — see below).
    double lo = 1e9, hi = -1e9;
    for (int m = 100; m < N / 2 - 100; ++m) {
      lo = std::min(lo, y[m]);
      hi = std::max(hi, y[m]);
    }
    // Measured 2026-09-26: mean error 7.96e-6 (-102 dB); interior ripple
    // [0.99999, 1.0]. Assert <= -80 dB steady-state accuracy.
    double sum = 0.0;
    for (double v : y) sum += v;
    const double mean = sum / (N / 2);
    CHECK(mean > 1.0 - 1e-4);
    CHECK(mean < 1.0 + 1e-4);
    CHECK(lo > 0.99);
    CHECK(hi < 1.01);
    // §7.6 edge semantics: the leading ~2K output frames read zero padding
    // (bounded transient — measured peak 1.135 at r=0.5, K=24); no NaN/Inf,
    // amplitude bounded.
    for (double v : y) {
      CHECK(std::isfinite(v));
      CHECK(std::fabs(v) < 2.0);
    }
  }
  // Nyquist: alternating +/-1 (the most adversarial in-band content for an
  // even symmetric kernel): finite, bounded, no NaN.
  {
    std::vector<double> x(N), y(N / 2);
    for (int i = 0; i < N; ++i) x[i] = (i % 2 == 0) ? 1.0 : -1.0;
    double pos = 0.0;
    pitchlab::resampleBlock(x.data(), N, pos, nullptr, 0.5, y.data(), N / 2, 1.0,
                            pitchlab::ResampleQuality::Reference);
    for (double v : y) {
      CHECK(std::isfinite(v));
      CHECK(std::fabs(v) < 1.0 + 1e-9);
    }
  }
}

TEST_CASE("T-R2d: analytic sine at constant ratios (analytically controlled signal)") {
  // y[m] should equal sin(2*pi*f*p_m) with p_m = m*r (sequential accumulation
  // — the test reproduces the same accumulation order). f = 0.05 cycles, well
  // inside the passband for every cutoff below.
  // Measured 2026-09-26 (max abs err, edge frames skipped):
  //   r=0.5:  small 1.35e-3 | standard 9.5e-4 | reference 5.9e-4
  //   r=1.5:  small 4.4e-5  | standard 2.6e-5  | reference 5.1e-6
  //   r=2.0:  small 1.65e-4 | standard 5.1e-5  | reference 7.2e-6
  // Regression bands (NOT spec criteria; ~4x headroom over measurements):
  const double band[3] = {5.0e-3, 4.0e-3, 2.0e-3};  // small, standard, reference
  const int N = 20000;
  std::vector<double> x(N);
  for (int i = 0; i < N; ++i) x[i] = std::sin(2.0 * kPi * 0.05 * i);
  for (const double r : {0.5, 1.5, 2.0}) {
    const int M = static_cast<int>(N / r);
    const double c = pitchlab::antiAliasCutoff(r);
    for (int pi = 0; pi < 3; ++pi) {
      const auto& preset = kPresets[pi];
      std::vector<double> y(M);
      double pos = 0.0;
      pitchlab::resampleBlock(x.data(), N, pos, nullptr, r, y.data(), M, c, preset.quality);
      // expected with the SAME sequential accumulation
      std::vector<double> expect(M);
      double p = 0.0;
      for (int m = 0; m < M; ++m) {
        expect[m] = std::sin(2.0 * kPi * 0.05 * p);
        p += r;
      }
      // Skip edge frames (zero-padding transients at both ends: up to
      // ~K*(1 + 1/r) output frames are affected by the input boundaries).
      const int k = pitchlab::resampleKernelSpec(preset.quality).halfWidthTaps;
      const auto skip = static_cast<std::size_t>(4 * k + 16);
      REQUIRE(y.size() > 2 * skip);
      const double err = maxAbsError(y, expect, skip);
      std::printf("[T-R2d] r=%.1f preset=%-9s cutoff=%.3f max|y-analytic| = %.3g (band %.1g)\n",
                  r, preset.name, c, err, band[pi]);
      CHECK(err < band[pi]);
    }
  }
}

TEST_CASE("T-R2e: composite anti-alias path (§7.4 prefilter + ratio-2 read)") {
  // Input: three unit tones at 0.04 (in-band), 0.30 (kernel/prefilter
  // stopband edge region), 0.45 (deep stopband). Read at r=2 with the §7.4
  // policy (prefilter at c=0.475, kernel c=0.475, reference preset).
  // Expected: in-band tone passes at unity; stopband content is suppressed
  // by prefilter x kernel (double band-limiting).
  const int N = 65536;
  std::vector<double> x(N);
  for (int i = 0; i < N; ++i) {
    x[i] = 0.5 * std::sin(2.0 * kPi * 0.04 * i) + 0.5 * std::sin(2.0 * kPi * 0.30 * i) +
           0.5 * std::sin(2.0 * kPi * 0.45 * i);
  }
  const auto fir = pitchlab::designAntiAliasFir(kTestCutoff, pitchlab::ResampleQuality::Reference);
  REQUIRE(fir.size() == 49);
  std::vector<double> xf(N);
  pitchlab::applyFirZeroPhase(x.data(), N, fir.data(), 24, xf.data());
  const int M = N / 2;
  std::vector<double> y(M);
  double pos = 0.0;
  pitchlab::resampleBlock(xf.data(), N, pos, nullptr, 2.0, y.data(), M, kTestCutoff,
                          pitchlab::ResampleQuality::Reference);

  // Single-bin Hann-windowed DFT tone amplitudes (avoid FFT dependency —
  // pocketfft is planned for the PV engine cycle, not this one).
  auto toneAmp = [&](const std::vector<double>& s, double f) {
    double re = 0.0, im = 0.0, wsum = 0.0;
    const std::size_t skip = 400;
    for (std::size_t n = skip; n < s.size(); ++n) {
      const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(n - skip) /
                                             static_cast<double>(s.size() - skip)));
      re += w * s[n] * std::cos(2.0 * kPi * f * static_cast<double>(n));
      im += w * s[n] * std::sin(2.0 * kPi * f * static_cast<double>(n));
      wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
  };
  const double inBand = toneAmp(y, 0.08);    // 0.04 -> 0.08 (legit)
  const double folded = toneAmp(y, 0.10);    // 0.45 -> 0.90 -> folds to 0.10
  const double foldedT = toneAmp(y, 0.40);   // 0.30 -> 0.60 -> folds to 0.40
  std::printf("[T-R2e] in-band amp=%.6f (target 0.5); folded stopband tone=%.3g; folded "
              "edge tone=%.3g (measured 2026-09-26: 5.5e-10 / 4.0e-10)\n",
              inBand, folded, foldedT);
  CHECK(inBand == doctest::Approx(0.5).epsilon(0.01));
  // Measured: ~5e-10 (-179 dB). Assert >= 100 dB suppression (79 dB headroom).
  CHECK(folded < 0.5 * 1e-5);
  CHECK(foldedT < 0.5 * 1e-5);
}

TEST_CASE("T-R2f: deterministic repeatability (same-binary bit-exactness)") {
  const int N = 12000;
  const auto x = makeNoise(N, 77);
  std::vector<double> y1(N / 2), y2(N / 2);
  double p1 = 0.0, p2 = 0.0;
  const double c = pitchlab::antiAliasCutoff(2.0);
  pitchlab::resampleBlock(x.data(), N, p1, nullptr, 2.0, y1.data(), N / 2, c,
                          pitchlab::ResampleQuality::Standard);
  pitchlab::resampleBlock(x.data(), N, p2, nullptr, 2.0, y2.data(), N / 2, c,
                          pitchlab::ResampleQuality::Standard);
  CHECK(std::memcmp(y1.data(), y2.data(), (N / 2) * sizeof(double)) == 0);
  CHECK(p1 == p2);
}

TEST_CASE("T-R2g: block-boundary independence (any split is bit-identical)") {
  // §7.2.1 item 9: one call == any sequence of block splits with the same
  // per-frame ratio sequence. Harness block size 4096 (owner L-3) plus
  // adversarial splits.
  const int N = 30000, M = 17647;
  const auto x = makeNoise(N, 78);
  std::vector<double> ratio(M);
  for (int m = 0; m < M; ++m) {
    ratio[m] = 1.7 - 0.4 * std::sin(2.0 * kPi * static_cast<double>(m) / M);  // changing ratios
  }
  std::vector<double> y1(M), y2(M);
  double p1 = 0.123, p2 = 0.123;
  const double c = pitchlab::antiAliasCutoff(1.7);
  pitchlab::resampleBlock(x.data(), N, p1, ratio.data(), 0.0, y1.data(), M, c,
                          pitchlab::ResampleQuality::Standard);
  const int splits[] = {1, 4095, 4096, 100, 5000, 0};
  std::size_t off = 0;
  for (int s : splits) {
    if (s == 0 || off >= static_cast<std::size_t>(M)) continue;
    const std::size_t take = std::min(static_cast<std::size_t>(s),
                                      static_cast<std::size_t>(M) - off);
    pitchlab::resampleBlock(x.data(), N, p2, ratio.data() + off, 0.0, y2.data() + off,
                            static_cast<pitchlab::FrameCount>(take), c,
                            pitchlab::ResampleQuality::Standard);
    off += take;
  }
  if (off < static_cast<std::size_t>(M)) {
    pitchlab::resampleBlock(x.data(), N, p2, ratio.data() + off, 0.0, y2.data() + off,
                            static_cast<pitchlab::FrameCount>(static_cast<std::size_t>(M) - off),
                            c, pitchlab::ResampleQuality::Standard);
  }
  CHECK(std::memcmp(y1.data(), y2.data(), M * sizeof(double)) == 0);
  CHECK(p1 == p2);
  // And the accumulator is sequential-addition exact: p1 == 0.123 + sum(ratio)
  // computed in the same order.
  double expectPos = 0.123;
  for (int m = 0; m < M; ++m) expectPos += ratio[m];
  CHECK(p1 == expectPos);
}

TEST_CASE("T-R2h: sample-rate invariance (§8 set; kernel is normalized-frequency)") {
  // The primitive is rate-agnostic (positions in frames, cutoff in Nyquist
  // units); the rates exercise the pipeline end-to-end per §8 supported set.
  // The rate value itself is not an input to the primitive — hence unused.
  for (const uint32_t rate : {44100u, 48000u, 88200u, 96000u, 176400u, 192000u}) {
    (void)rate;
    const int N = 8192;
    std::vector<double> x(N), y(N / 2 + 1);
    for (int i = 0; i < N; ++i) {
      x[i] = std::sin(2.0 * kPi * 0.05 * i);  // 0.05 cycles = 0.1 * Ny at ANY rate
    }
    double pos = 0.0;
    pitchlab::resampleBlock(x.data(), N, pos, nullptr, 0.75, y.data(), N / 2 + 1, 1.0,
                            pitchlab::ResampleQuality::Reference);
    std::vector<double> expect(N / 2 + 1);
    double p = 0.0;
    for (int m = 0; m < N / 2 + 1; ++m) {
      expect[m] = std::sin(2.0 * kPi * 0.05 * p);
      p += 0.75;
    }
    const double err = maxAbsError(y, expect, 24);
    CHECK(err < 2.0e-3);  // reference regression band (measured class: ~6e-4 at r<=1)
  }
}

TEST_CASE("T-R2i: stereo coherence — identical channels give bit-identical outputs") {
  const int N = 6000;
  const auto mono = makeNoise(N, 79);
  std::vector<double> yL(N / 2), yR(N / 2);
  double pL = 0.0, pR = 0.0;
  const double c = pitchlab::antiAliasCutoff(2.0);
  pitchlab::resampleBlock(mono.data(), N, pL, nullptr, 2.0, yL.data(), N / 2, c,
                          pitchlab::ResampleQuality::Reference);
  pitchlab::resampleBlock(mono.data(), N, pR, nullptr, 2.0, yR.data(), N / 2, c,
                          pitchlab::ResampleQuality::Reference);
  CHECK(std::memcmp(yL.data(), yR.data(), (N / 2) * sizeof(double)) == 0);  // per-channel identical processing
  CHECK(pL == pR);
}

TEST_CASE("T-R2j: boundary handling (zero-padding semantics, §7.6)") {
  // Delta at index 0 read at fractional position 0.25: only tap n=0 is
  // in-range, so the result equals kernelValue(0.25) EXACTLY (same binary).
  {
    std::vector<double> x(64, 0.0);
    x[0] = 1.0;
    const double got = pitchlab::interpolateAt(x.data(), 64, 0.25, 1.0,
                                               pitchlab::ResampleQuality::Small);
    const double expect = pitchlab::resampleKernelValue(0.25, 1.0,
                                                        pitchlab::ResampleQuality::Small);
    CHECK(got == expect);
  }
  // Delta at the last index read past the end (position N-1+0.3): only the
  // n = N-1 tap contributes. NOTE: the kernel argument is u = position - n,
  // computed exactly as interpolateAt computes it (the literal 63.3 is not
  // exactly 63 + 0.3 in binary — use the identical subtraction).
  {
    std::vector<double> x(64, 0.0);
    x[63] = 1.0;
    const double p = 63.3;
    const double got = pitchlab::interpolateAt(x.data(), 64, p, 1.0,
                                               pitchlab::ResampleQuality::Small);
    const double u = p - 63.0;
    const double expect = pitchlab::resampleKernelValue(u, 1.0, pitchlab::ResampleQuality::Small);
    CHECK(got == doctest::Approx(expect).epsilon(1e-12));
  }
  // Negative position: zero-padded extrapolation equals the manual kernel sum
  // over in-range taps only (validates the tap window + bounds logic).
  {
    std::vector<double> x(40);
    for (int i = 0; i < 40; ++i) x[i] = 0.1 * i - 2.0;
    const double p = -3.7;
    const auto q = pitchlab::ResampleQuality::Small;
    const int K = 8;
    const long long n0 = std::llround(p);  // -4
    double expect = 0.0;
    for (int i = -K; i <= K; ++i) {
      const long long n = n0 + i;
      if (n < 0 || n >= 40) continue;
      expect += x[n] * pitchlab::resampleKernelValue(p - static_cast<double>(n), 1.0, q);
    }
    const double got = pitchlab::interpolateAt(x.data(), 40, p, 1.0, q);
    CHECK(got == doctest::Approx(expect).epsilon(1e-12));
  }
  // Positions far outside read exactly zero.
  {
    std::vector<double> x(10, 1.0);
    CHECK(pitchlab::interpolateAt(x.data(), 10, 100.0, 1.0,
                                  pitchlab::ResampleQuality::Small) == 0.0);
    CHECK(pitchlab::interpolateAt(x.data(), 10, -50.0, 1.0,
                                  pitchlab::ResampleQuality::Small) == 0.0);
  }
  // Empty input reads zero.
  CHECK(pitchlab::interpolateAt(nullptr, 0, 5.5, 1.0, pitchlab::ResampleQuality::Small) == 0.0);
}

TEST_CASE("T-R2k: FIR pre-filter properties (§7.4 / §7.2.1 item 8)") {
  const auto fir = pitchlab::designAntiAliasFir(0.475, pitchlab::ResampleQuality::Reference);
  REQUIRE(fir.size() == 49);
  // Symmetric (linear phase by construction).
  for (int j = 0; j < 24; ++j) {
    CHECK(fir[j] == doctest::Approx(fir[48 - j]).epsilon(1e-12));
  }
  // Taps are the kernel at integer arguments (same machinery).
  for (int n = -24; n <= 24; ++n) {
    CHECK(fir[n + 24] ==
          doctest::Approx(pitchlab::resampleKernelValue(n, 0.475,
                                                        pitchlab::ResampleQuality::Reference))
              .epsilon(1e-12));
  }
  // DC gain ~ 1 (unnormalised; ripple, not forced).
  double dc = 0.0;
  for (double t : fir) dc += t;
  CHECK(dc == doctest::Approx(1.0).epsilon(1e-3));
  // applyFirZeroPhase: constant input -> constant output (DC gain), edges zero-padded.
  const int N = 512;
  std::vector<double> x(N, 1.0), y(N);
  pitchlab::applyFirZeroPhase(x.data(), N, fir.data(), 24, y.data());
  double lo = 1e9, hi = -1e9;
  for (std::size_t i = 48; i < N - 48; ++i) {
    lo = std::min(lo, y[i]);
    hi = std::max(hi, y[i]);
  }
  CHECK(lo == doctest::Approx(dc).epsilon(1e-9));
  CHECK(hi == doctest::Approx(dc).epsilon(1e-9));
}

TEST_CASE("T-R2l: invalid inputs/configuration (§7.2.1 item 10)") {
  std::vector<double> x(100, 0.5), y(50);
  double pos = 0.0;
  using P = pitchlab::ResampleQuality;
  // Invalid cutoffs.
  for (const double c : {0.0, -0.1, 1.5, std::nan(""), HUGE_VAL}) {
    CHECK_THROWS_AS((void)pitchlab::interpolateAt(x.data(), 100, 0.0, c, P::Small),
                    pitchlab::ConfigError);
  }
  // Invalid positions / ratios.
  CHECK_THROWS_AS((void)pitchlab::interpolateAt(x.data(), 100, std::nan(""), 1.0, P::Small),
                  pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::resampleBlock(x.data(), 100, pos, nullptr, 0.0, y.data(), 50,
                                                1.0, P::Small),
                  pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::resampleBlock(x.data(), 100, pos, nullptr, -1.0, y.data(), 50,
                                                1.0, P::Small),
                  pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::resampleBlock(x.data(), 100, pos, nullptr, std::nan(""), y.data(),
                                                50, 1.0, P::Small),
                  pitchlab::ConfigError);
  double badPos = std::nan("");
  CHECK_THROWS_AS((void)pitchlab::resampleBlock(x.data(), 100, badPos, nullptr, 1.0, y.data(), 50,
                                                1.0, P::Small),
                  pitchlab::ConfigError);
  // Per-frame ratio array with an invalid entry.
  std::vector<double> ratios(50, 1.0);
  ratios[7] = -0.5;
  CHECK_THROWS_AS((void)pitchlab::resampleBlock(x.data(), 100, pos, ratios.data(), 1.0, y.data(), 50,
                                                1.0, P::Small),
                  pitchlab::ConfigError);
  ratios[7] = 0.0;
  CHECK_THROWS_AS((void)pitchlab::resampleBlock(x.data(), 100, pos, ratios.data(), 1.0, y.data(), 50,
                                                1.0, P::Small),
                  pitchlab::ConfigError);
  // Negative counts, null buffers, in-place.
  CHECK_THROWS_AS((void)pitchlab::resampleBlock(x.data(), -1, pos, nullptr, 1.0, y.data(), 0, 1.0,
                                                P::Small),
                  pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::resampleBlock(nullptr, 100, pos, nullptr, 1.0, y.data(), 50, 1.0,
                                                P::Small),
                  pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::resampleBlock(x.data(), 100, pos, nullptr, 1.0, nullptr, 50, 1.0,
                                                P::Small),
                  pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::resampleBlock(x.data(), 100, pos, nullptr, 1.0, x.data(), 50, 1.0,
                                                P::Small),
                  pitchlab::ConfigError);  // in-place prohibited
  // Zero output frames: no-op, position unchanged.
  double posBefore = 5.5;
  double posAfter = 5.5;
  pitchlab::resampleBlock(x.data(), 100, posAfter, nullptr, 1.0, y.data(), 0, 1.0, P::Small);
  CHECK(posAfter == posBefore);
  // FIR invalid inputs.
  CHECK_THROWS_AS((void)pitchlab::designAntiAliasFir(1.5, P::Small), pitchlab::ConfigError);
  CHECK_THROWS_AS((void)pitchlab::applyFirZeroPhase(x.data(), 100, nullptr, 8, y.data()),
                  pitchlab::ConfigError);  // null coefficients with halfTaps > 0
  CHECK_THROWS_AS((void)pitchlab::applyFirZeroPhase(x.data(), 100, x.data(), 8, x.data()),
                  pitchlab::ConfigError);  // in-place prohibited
}
