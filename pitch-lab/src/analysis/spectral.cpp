#include "analysis/spectral.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// The vendored pocketfft header (external/pocketfft/, BSD-3-Clause,
// ORIGIN.toml-pinned — build spec §7). POCKETFFT_CACHE_SIZE 0: no hidden
// global plan cache; the analysis path owns its plan objects per
// computation (deterministic for a given size; not an engine processing
// path, so the persistent-plan rule of §6.3.1 item 16 does not apply —
// §10.4 item 1).
#define POCKETFFT_CACHE_SIZE 0
#include <pocketfft/pocketfft_hdronly.h>

namespace pitchlab::analysis {
namespace {

using pocketfft::detail::cmplx;
using pocketfft::detail::pocketfft_c;
using pocketfft::detail::pocketfft_r;

[[nodiscard]] int64_t nextPow2(int64_t v) {
  int64_t p = 1;
  while (p < v) p <<= 1;
  return p;
}

}  // namespace

int64_t analysisStftFrames(double fs) {
  const double target = std::ceil(fs * 2048.0 / 48000.0);
  return nextPow2(static_cast<int64_t>(target));
}

std::vector<double> periodicHann(int64_t n) {
  std::vector<double> w(static_cast<std::size_t>(n));
  const double twoPi = 6.283185307179586476925286766559;
  for (int64_t k = 0; k < n; ++k) {
    w[static_cast<std::size_t>(k)] =
        0.5 * (1.0 - std::cos(twoPi * static_cast<double>(k) / static_cast<double>(n)));
  }
  return w;
}

StftResult computeStft(const std::vector<double>& x, double fs) {
  StftResult res;
  res.windowFrames = analysisStftFrames(fs);
  res.hop = res.windowFrames / 4;
  const int64_t N = res.windowFrames;
  const int64_t bins = N / 2 + 1;
  const std::vector<double> w = periodicHann(N);

  const int64_t total = static_cast<int64_t>(x.size());
  if (total < N) {
    res.frameCount = 0;
    res.droppedTailFrames = total;
    return res;
  }
  int64_t start = 0;
  while (start + N <= total) {
    StftFrame f;
    f.start = start;
    f.power.assign(static_cast<std::size_t>(bins), 0.0);
    f.re.assign(static_cast<std::size_t>(bins), 0.0);
    f.im.assign(static_cast<std::size_t>(bins), 0.0);

    std::vector<double> buf(static_cast<std::size_t>(N));
    for (int64_t j = 0; j < N; ++j) {
      buf[static_cast<std::size_t>(j)] =
          w[static_cast<std::size_t>(j)] * x[static_cast<std::size_t>(start + j)];
    }
    pocketfft_r<double> plan(static_cast<std::size_t>(N));
    plan.exec(buf.data(), 1.0, true);
    // Unpack the halfcomplex layout (the vendoring convention; k = 0..N/2).
    for (int64_t k = 0; k < bins; ++k) {
      double re, im;
      if (k == 0) {
        re = buf[0];
        im = 0.0;
      } else if (k == N / 2) {
        re = buf[static_cast<std::size_t>(N - 1)];
        im = 0.0;
      } else {
        re = buf[static_cast<std::size_t>(2 * k - 1)];
        im = buf[static_cast<std::size_t>(2 * k)];
      }
      f.re[static_cast<std::size_t>(k)] = re;
      f.im[static_cast<std::size_t>(k)] = im;
      f.power[static_cast<std::size_t>(k)] = re * re + im * im;
    }
    res.frames.push_back(std::move(f));
    start += res.hop;
  }
  res.frameCount = static_cast<int64_t>(res.frames.size());
  res.droppedTailFrames = total - (start - res.hop + N);
  return res;
}

Projection projectAt(const std::vector<double>& x, int64_t start,
                     const std::vector<double>& window, double fs, double f) {
  Projection p;
  const int64_t N = static_cast<int64_t>(window.size());
  const double twoPi = 6.283185307179586476925286766559;
  const double w0 = twoPi * f / fs;
  // Two real sums in ascending i order (§10.4 item 6b — frozen op order).
  double sumRe = 0.0;
  double sumIm = 0.0;
  for (int64_t i = 0; i < N; ++i) {
    const double arg = w0 * static_cast<double>(i);
    const double v = window[static_cast<std::size_t>(i)] *
                     x[static_cast<std::size_t>(start + i)];
    sumRe += v * std::cos(arg);
    sumIm -= v * std::sin(arg);
  }
  p.re = sumRe;
  p.im = sumIm;
  return p;
}

std::vector<double> hilbertEnvelope(const std::vector<double>& x) {
  const int64_t n = static_cast<int64_t>(x.size());
  std::vector<double> env(static_cast<std::size_t>(n), 0.0);
  if (n == 0) return env;

  std::vector<double> buf(x);
  pocketfft_r<double> rplan(static_cast<std::size_t>(n));
  rplan.exec(buf.data(), 1.0, true);

  // Unpack the halfcomplex spectrum into the analytic-spectrum form
  // (§10.4 item 6c): Y[0] = X[0]; Y[k] = 2X[k] for 1 <= k < N/2; Y[N/2] =
  // X[N/2] (even N); zeros above. |IFFT| = envelope.
  const int64_t half = n / 2;  // floor; for odd n there is no Nyquist bin
  std::vector<cmplx<double>> spec(static_cast<std::size_t>(n));
  {
    // k = 0
    spec[0].r = buf[0];
    spec[0].i = 0.0;
    for (int64_t k = 1; k <= half; ++k) {
      double re, im;
      if (k == half && (n % 2 == 0)) {
        re = buf[static_cast<std::size_t>(n - 1)];
        im = 0.0;
      } else {
        re = buf[static_cast<std::size_t>(2 * k - 1)];
        im = buf[static_cast<std::size_t>(2 * k)];
      }
      const double scale = (k == half && (n % 2 == 0)) ? 1.0 : 2.0;
      spec[static_cast<std::size_t>(k)].r = re * scale;
      spec[static_cast<std::size_t>(k)].i = im * scale;
    }
    for (int64_t k = half + 1; k < n; ++k) {
      spec[static_cast<std::size_t>(k)].r = 0.0;
      spec[static_cast<std::size_t>(k)].i = 0.0;
    }
  }
  pocketfft_c<double> cplan(static_cast<std::size_t>(n));
  cplan.exec(spec.data(), 1.0 / static_cast<double>(n), false);
  for (int64_t i = 0; i < n; ++i) {
    env[static_cast<std::size_t>(i)] = std::hypot(spec[static_cast<std::size_t>(i)].r,
                                                  spec[static_cast<std::size_t>(i)].i);
  }
  return env;
}

OnsetDetection detectOnsets(const std::vector<std::vector<double>>& channels, double fs) {
  OnsetDetection det;
  det.refractoryFrames =
      static_cast<int64_t>(std::llround(0.05 * fs));  // 50 ms, §10.4 item 6d
  if (channels.empty()) {
    det.threshold = 0.0;
    return det;
  }
  const int64_t n = static_cast<int64_t>(channels[0].size());
  for (const auto& c : channels) {
    if (static_cast<int64_t>(c.size()) != n) return det;  // caller contract
  }
  // Channel-max envelope, ascending.
  std::vector<double> e(static_cast<std::size_t>(n), 0.0);
  double maxE = 0.0;
  for (int64_t i = 0; i < n; ++i) {
    double m = 0.0;
    for (const auto& c : channels) {
      const double a = std::fabs(c[static_cast<std::size_t>(i)]);
      if (a > m) m = a;
    }
    e[static_cast<std::size_t>(i)] = m;
    if (m > maxE) maxE = m;
  }
  if (maxE == 0.0) {
    det.threshold = 0.0;
    return det;  // silent: no onsets (documented; callers gate silence first)
  }
  det.threshold = 0.25 * maxE;
  // Maximal-run scan: a run = consecutive frames with e >= threshold; runs
  // separated by fewer than refractoryFrames below-threshold frames merge.
  constexpr int64_t kUnset = -1;
  int64_t lastRunEnd = kUnset;  // inclusive last frame >= threshold
  bool inRun = false;
  int64_t runStart = 0;
  for (int64_t i = 0; i < n; ++i) {
    if (e[static_cast<std::size_t>(i)] >= det.threshold) {
      if (!inRun) {
        inRun = true;
        runStart = i;
        if (lastRunEnd == kUnset || (i - lastRunEnd - 1) >= det.refractoryFrames) {
          det.onsets.push_back(i);  // first frame of a (merged) maximal run
        }
      }
      lastRunEnd = i;
    } else {
      inRun = false;
    }
    (void)runStart;
  }
  return det;
}

EmissionMap::EmissionMap(const std::vector<double>& ratio, int64_t inputFrames,
                         int64_t frameCount) {
  if (frameCount < 0 || inputFrames <= 0 || ratio.empty()) {
    r_.assign(1, 0.0);  // degenerate map: r(0) = 0 only
    return;
  }
  r_.resize(static_cast<std::size_t>(frameCount + 1));
  r_[0] = 0.0;
  double r = 0.0;
  for (int64_t m = 0; m < frameCount; ++m) {
    // clamp(floor(r), 0, N_in - 1) — the §6.1.1 item 2 index rule.
    int64_t idx = static_cast<int64_t>(std::floor(r));
    if (idx < 0) idx = 0;
    if (idx > inputFrames - 1) idx = inputFrames - 1;
    r += ratio[static_cast<std::size_t>(idx)];
    r_[static_cast<std::size_t>(m + 1)] = r;
  }
}

int64_t EmissionMap::outputPositionOfInput(int64_t p) const {
  // First m with r(m) >= p (r is strictly increasing: ratio > 0).
  int64_t lo = 0;
  int64_t hi = static_cast<int64_t>(r_.size());  // exclusive
  while (lo < hi) {
    const int64_t mid = lo + (hi - lo) / 2;
    if (r_[static_cast<std::size_t>(mid)] < static_cast<double>(p)) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

std::vector<double> EmissionMap::warpToInputTimeline(const std::vector<double>& out,
                                                     int64_t inputFrames) const {
  std::vector<double> warped(static_cast<std::size_t>(inputFrames), 0.0);
  if (r_.empty() || out.empty()) return warped;
  const int64_t M = static_cast<int64_t>(r_.size()) - 1;  // r has M+1 entries
  int64_t m = 0;
  for (int64_t p = 0; p < inputFrames; ++p) {
    while (m + 1 <= M && r_[static_cast<std::size_t>(m + 1)] <= static_cast<double>(p)) {
      ++m;
    }
    // r(m) <= p < r(m+1) (or m == M: past the end — hold the last frame).
    if (m + 1 <= M && m + 1 < static_cast<int64_t>(out.size())) {
      const double span = r_[static_cast<std::size_t>(m + 1)] - r_[static_cast<std::size_t>(m)];
      const double u = span > 0.0
          ? (static_cast<double>(p) - r_[static_cast<std::size_t>(m)]) / span
          : 0.0;
      warped[static_cast<std::size_t>(p)] =
          out[static_cast<std::size_t>(m)] * (1.0 - u) +
          out[static_cast<std::size_t>(m + 1)] * u;
    } else if (m < static_cast<int64_t>(out.size())) {
      warped[static_cast<std::size_t>(p)] = out[static_cast<std::size_t>(m)];
    }
  }
  return warped;
}

int64_t EmissionMap::framesCoveringInput(int64_t lastRealInput) const {
  // Count of m with r(m) <= lastRealInput (binary search on the monotone r).
  int64_t lo = 0;
  int64_t hi = static_cast<int64_t>(r_.size());
  while (lo < hi) {
    const int64_t mid = lo + (hi - lo) / 2;
    if (r_[static_cast<std::size_t>(mid)] <= static_cast<double>(lastRealInput)) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

}  // namespace pitchlab::analysis
