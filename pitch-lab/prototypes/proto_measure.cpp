// Pitch Lab — Task 28 measurement layer implementation. See
// proto_measure.h for the frozen measurement contract.

#include "prototypes/proto_measure.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

#include "analysis/pitch_tracker.h"
#include "analysis/spectral.h"
#include "core/hash.h"

namespace pitchlab::proto {
namespace {

constexpr double kPi = 3.14159265358979323846;

double dbfs(double lin) {
  if (!(lin > 0.0) || !std::isfinite(lin)) return -999.0;
  return 20.0 * std::log10(lin);
}

double meanOf(const std::vector<double>& v, int64_t from, int64_t to) {
  if (to <= from) return 0.0;
  double s = 0.0;
  for (int64_t i = from; i < to; ++i) s += v[static_cast<std::size_t>(i)];
  return s / static_cast<double>(to - from);
}

double stdOf(const std::vector<double>& v, int64_t from, int64_t to,
             double mean) {
  if (to <= from) return 0.0;
  double s = 0.0;
  for (int64_t i = from; i < to; ++i) {
    const double d = v[static_cast<std::size_t>(i)] - mean;
    s += d * d;
  }
  return std::sqrt(s / static_cast<double>(to - from));
}

double pearson(const double* a, const double* b, int64_t n) {
  if (n <= 1) return 0.0;
  double ma = 0.0, mb = 0.0;
  for (int64_t i = 0; i < n; ++i) {
    ma += a[i];
    mb += b[i];
  }
  ma /= static_cast<double>(n);
  mb /= static_cast<double>(n);
  double sab = 0.0, sa = 0.0, sb = 0.0;
  for (int64_t i = 0; i < n; ++i) {
    const double da = a[i] - ma;
    const double db = b[i] - mb;
    sab += da * db;
    sa += da * da;
    sb += db * db;
  }
  const double den = std::sqrt(sa) * std::sqrt(sb);
  if (den <= 0.0) return 0.0;
  return sab / den;
}

// Dominant-frequency estimation: exact-frequency Hann projection over the
// steady region, coarse grid then zero-crossing-seeded main-lobe
// refinement (the task-27-validated estimator).
double projectionPeakHz(const std::vector<double>& x, double fs,
                        int64_t start, int64_t len, double* zcOut) {
  const std::vector<double> w = analysis::periodicHann(len);
  const double* xa = x.data();

  // Zero-crossing median (cross-check + refinement seed).
  std::vector<double> intervals;
  int64_t lastCross = -1;
  for (int64_t i = start + 1; i < start + len; ++i) {
    const double a = xa[i - 1];
    const double b = xa[i];
    if ((a <= 0.0 && b > 0.0) || (a >= 0.0 && b < 0.0)) {
      if (lastCross >= 0) {
        intervals.push_back(static_cast<double>(i - lastCross));
      }
      lastCross = i;
    }
  }
  double zcHz = 0.0;
  if (!intervals.empty() && intervals.size() >= 4) {
    std::vector<double> s = intervals;
    std::sort(s.begin(), s.end());
    const double med = s[s.size() / 2];
    if (med > 0.5) zcHz = fs / (2.0 * med);
  }
  if (zcOut != nullptr) *zcOut = zcHz;

  if (zcHz <= 0.0 || zcHz > fs * 0.45) return 0.0;

  auto proj = [&](double f) {
    const analysis::Projection p = analysis::projectAt(
        x, start, w, fs, f);
    return p.re * p.re + p.im * p.im;
  };

  // Coarse grid: +-40% around the ZC estimate, step = 1/(4*len) normalised
  // (four grid points per main lobe width; the refinement finishes the job).
  const double lo = std::max(20.0, zcHz * 0.6);
  const double hi = std::min(fs * 0.45, zcHz * 1.4);
  const double step = fs / (4.0 * static_cast<double>(len));
  double bestF = 0.0, bestP = -1.0;
  for (double f = lo; f <= hi; f += step) {
    const double p = proj(f);
    if (p > bestP) {
      bestP = p;
      bestF = f;
    }
  }
  // Main-lobe refinement: shrink step x10 twice around the best.
  double center = bestF;
  double span = step * 4.0;
  for (int round = 0; round < 2; ++round) {
    const double s2 = span / 10.0;
    double bf = center, bp = proj(center);
    for (double f = center - span; f <= center + span; f += s2) {
      if (f <= 0.0) continue;
      const double p = proj(f);
      if (p > bp) {
        bp = p;
        bf = f;
      }
    }
    center = bf;
    span = s2 * 2.0;
  }
  return center;
}

// Averaged power spectrum over the steady region (the frozen analyzer
// STFT: 2048 @48k window, hop 1024 per computeStft) + centroid + ripple.
struct SpecStats {
  double centroidHz = 0.0;
  double rippleDb = 0.0;
};

SpecStats spectrumStats(const std::vector<double>& x, double fs) {
  SpecStats st;
  const auto stft = analysis::computeStft(x, fs);
  if (stft.frames.empty()) return st;
  const std::size_t bins = stft.frames[0].power.size();
  std::vector<double> avg(bins, 0.0);
  for (const auto& fr : stft.frames) {
    for (std::size_t k = 0; k < bins; ++k) avg[k] += fr.power[k];
  }
  const double nInv = 1.0 / static_cast<double>(stft.frames.size());
  for (auto& v : avg) v *= nInv;

  // Centroid over [20 Hz, 16 kHz] (48k assumed -> guard by fs/2*0.9).
  const double binHz = fs / static_cast<double>(stft.windowFrames);
  const double fLo = 20.0;
  const double fHi = std::min(16000.0, fs * 0.45);
  double num = 0.0, den = 0.0;
  for (std::size_t k = 0; k < bins; ++k) {
    const double f = static_cast<double>(k) * binHz;
    if (f < fLo || f > fHi) continue;
    num += f * avg[k];
    den += avg[k];
  }
  if (den > 0.0) st.centroidHz = num / den;

  // Comb ripple: mean |20log10(P / movavg(P))|, movavg over +-6 bins,
  // over the same band, skipping the first/last 6 bins.
  const std::size_t half = 6;
  if (bins > 2 * half + 2) {
    double acc = 0.0;
    std::size_t cnt = 0;
    for (std::size_t k = half; k + half < bins; ++k) {
      const double f = static_cast<double>(k) * binHz;
      if (f < fLo || f > fHi) continue;
      double s = 0.0;
      for (std::size_t j = k - half; j <= k + half; ++j) s += avg[j];
      const double m = s / static_cast<double>(2 * half + 1);
      if (m > 1.0e-300 && avg[k] > 1.0e-300) {
        acc += std::abs(20.0 * std::log10(avg[k] / m));
        ++cnt;
      }
    }
    if (cnt > 0) st.rippleDb = acc / static_cast<double>(cnt);
  }
  return st;
}

// Envelope AM statistics: Hilbert envelope of the steady region.
struct AmStats {
  double depth = 0.0;
  double rateHz = 0.0;
};

AmStats envelopeAmStats(const std::vector<double>& x, double fs, int64_t start,
                        int64_t len) {
  AmStats am;
  std::vector<double> region(x.begin() + static_cast<std::ptrdiff_t>(start),
                             x.begin() + static_cast<std::ptrdiff_t>(start + len));
  const std::vector<double> env = analysis::hilbertEnvelope(region);
  const double m = meanOf(env, 0, len);
  if (m <= 0.0) return am;
  am.depth = stdOf(env, 0, len, m) / m;

  // Dominant modulation rate: autocorrelation of (env - mean) over lags
  // [fs/200 .. fs/8] (4 Hz .. 6 kHz); peak normalised by lag-0 energy.
  const double e0 = [len, &env]() {
    double s = 0.0;
    for (int64_t i = 0; i < len; ++i) {
      const double d = env[static_cast<std::size_t>(i)];
      s += d * d;
    }
    return s;
  }();
  if (e0 <= 0.0) return am;
  const int64_t lagMin = std::max<int64_t>(2, static_cast<int64_t>(fs / 200.0));
  const int64_t lagMax = std::min<int64_t>(len / 4, static_cast<int64_t>(fs / 8.0));
  if (lagMax <= lagMin) return am;
  double bestLag = 0.0, bestVal = 0.0;
  for (int64_t lag = lagMin; lag <= lagMax; ++lag) {
    double s = 0.0;
    for (int64_t i = 0; i + lag < len; ++i) {
      s += (env[static_cast<std::size_t>(i)] - m) *
           (env[static_cast<std::size_t>(i + lag)] - m);
    }
    s /= e0;
    if (s > bestVal) {
      bestVal = s;
      bestLag = static_cast<double>(lag);
    }
  }
  if (bestLag > 0.0 && bestVal > 0.05) am.rateHz = fs / bestLag;
  return am;
}

}  // namespace

WaveMetrics measureWave(const std::vector<std::vector<double>>& ch, double fs,
                        const std::vector<std::vector<double>>& reference) {
  WaveMetrics m;
  if (ch.empty() || ch[0].empty()) {
    m.frames = 0;
    m.finite = false;
    m.silent = true;
    return m;
  }
  const int64_t n = static_cast<int64_t>(ch[0].size());
  m.frames = n;
  double peak = 0.0;
  double sumSq = 0.0;
  for (const auto& c : ch) {
    const int64_t cn = static_cast<int64_t>(c.size());
    for (int64_t i = 0; i < std::min(n, cn); ++i) {
      const double v = c[static_cast<std::size_t>(i)];
      if (!std::isfinite(v)) {
        m.finite = false;
        return m;
      }
      peak = std::max(peak, std::abs(v));
      sumSq += v * v;
    }
  }
  m.silent = peak < 1.0e-9;
  m.peakDbfs = dbfs(peak);
  m.rmsDbfs = dbfs(std::sqrt(sumSq / (static_cast<double>(n) * ch.size())));

  if (!m.silent) {
    // Steady region.
    const int64_t s0 = static_cast<int64_t>(0.15 * static_cast<double>(n));
    const int64_t s1 = static_cast<int64_t>(0.90 * static_cast<double>(n));
    if (s1 - s0 >= 4096) {
      const int64_t len = 4096;
      double zc = 0.0;
      m.dominantHz = projectionPeakHz(ch[0], fs, s0, len, &zc);
      m.dominantZcHz = zc;
      const F0Summary f0 = f0Summary(
          std::vector<double>(ch[0].begin() + static_cast<std::ptrdiff_t>(s0),
                              ch[0].begin() + static_cast<std::ptrdiff_t>(s1)),
          fs);
      m.f0TrackerHz = f0.medianHz;
      m.trackerVoicedRatio = f0.voicedRatio;
      const AmStats am = envelopeAmStats(ch[0], fs, s0, s1 - s0);
      m.amDepth = am.depth;
      m.amRateHz = am.rateHz;
      const SpecStats sp = spectrumStats(
          std::vector<double>(ch[0].begin() + static_cast<std::ptrdiff_t>(s0),
                              ch[0].begin() + static_cast<std::ptrdiff_t>(s1)),
          fs);
      m.centroidHz = sp.centroidHz;
      m.combRippleDb = sp.rippleDb;
    }

    // Onsets + transient statistics (full signal, the frozen detector).
    const auto od = analysis::detectOnsets(ch, fs);
    m.onsetCount = static_cast<FrameCount>(od.onsets.size());
    if (!od.onsets.empty()) {
      double sharpAcc = 0.0, tailAcc = 0.0;
      int cnt = 0;
      for (int64_t o : od.onsets) {
        if (o < 300 || o + 300 >= n) continue;
        double core = 0.0, wide = 0.0, tail = 0.0;
        for (int64_t i = o - 300; i < o + 300; ++i) {
          const double v = ch[0][static_cast<std::size_t>(i)];
          wide += v * v;
        }
        for (int64_t i = o - 30; i < o + 30; ++i) {
          const double v = ch[0][static_cast<std::size_t>(i)];
          core += v * v;
        }
        for (int64_t i = o + 30; i < o + 90; ++i) {
          const double v = ch[0][static_cast<std::size_t>(i)];
          tail += v * v;
        }
        if (wide > 1.0e-300 && core > 1.0e-300) {
          sharpAcc += 10.0 * std::log10(core / wide);
          tailAcc += 10.0 * std::log10((tail + 1.0e-300) / core);
          ++cnt;
        }
      }
      if (cnt > 0) {
        m.onsetSharpDb = sharpAcc / cnt;
        m.onsetTailDb = tailAcc / cnt;
      }
    }

    // Stereo coherence (channel 0 vs 1).
    if (ch.size() >= 2 && static_cast<int64_t>(ch[1].size()) == n) {
      m.stereoCorr =
          pearson(ch[0].data() + s0, ch[1].data() + s0, s1 - s0);
    }

    // Reference-based metrics (compared over the COMMON steady region:
    // output length legitimately differs from input length by the flush
    // tail, so a full-length equality check would silently skip these).
    if (!reference.empty() && !reference[0].empty()) {
      const int64_t nRef = static_cast<int64_t>(reference[0].size());
      const int64_t common = std::min(n, nRef);
      const int64_t rs0 = static_cast<int64_t>(0.15 * static_cast<double>(common));
      const int64_t rs1 = static_cast<int64_t>(0.90 * static_cast<double>(common));
      double inSumSq = 0.0;
      for (const auto& c : reference) {
        for (int64_t i = rs0; i < rs1; ++i) {
          const double v = c[static_cast<std::size_t>(i)];
          inSumSq += v * v;
        }
      }
      double outSumSq = 0.0;
      for (const auto& c : ch) {
        for (int64_t i = rs0; i < rs1; ++i) {
          const double v = c[static_cast<std::size_t>(i)];
          outSumSq += v * v;
        }
      }
      const double framesF = static_cast<double>(rs1 - rs0);
      const double inRms =
          std::sqrt(inSumSq / (framesF * reference.size()));
      const double outRms =
          std::sqrt(outSumSq / (framesF * ch.size()));
      if (inRms > 0.0) m.rmsRatio = outRms / inRms;
      // Best-lag Pearson over the steady region. Lag window +-maxLag with
      // the comparison window sized region - 2*maxLag - 8 so BOTH arrays
      // stay strictly inside [rs0, rs1) for every lag (the earlier
      // region - maxLag - 8 window read past the end on positive lags —
      // caught by ASAN).
      if (rs1 - rs0 > 8192 * 2 + 64) {
        const int64_t region = rs1 - rs0;
        const int64_t maxLag = std::min<int64_t>(8192, region / 4);
        const int64_t len = region - 2 * maxLag - 8;
        double bestC = -2.0;
        for (int64_t lag = -maxLag; lag <= maxLag; lag += 4) {
          const double cc = pearson(ch[0].data() + rs0 + maxLag + lag,
                                    reference[0].data() + rs0 + maxLag, len);
          if (cc > bestC) bestC = cc;
        }
        m.corrCoef = (bestC > -2.0) ? bestC : 0.0;
      }
    }
  }
  return m;
}

std::string waveHashHex(const std::vector<std::vector<double>>& ch) {
  if (ch.empty() || ch[0].empty()) return "empty";
  std::string acc = sha256HexString(
      std::string(reinterpret_cast<const char*>(ch[0].data()),
                  ch[0].size() * sizeof(double)));
  for (std::size_t c = 1; c < ch.size(); ++c) {
    acc = sha256HexString(acc + std::string(reinterpret_cast<const char*>(ch[c].data()),
                                            ch[c].size() * sizeof(double)));
  }
  return acc;
}

F0Summary f0Summary(const std::vector<double>& x, double fs) {
  F0Summary s;
  const auto track = analysis::trackPitch(x, fs, analysis::kTrackerFminHz,
                                          analysis::kTrackerFmaxHz);
  if (track.frames.empty()) return s;
  std::vector<double> voiced;
  voiced.reserve(track.frames.size());
  for (const auto& f : track.frames) {
    if (f.voiced && f.f0Hz > 0.0) voiced.push_back(f.f0Hz);
  }
  s.voicedRatio =
      static_cast<double>(voiced.size()) / static_cast<double>(track.frames.size());
  if (voiced.empty()) return s;
  std::vector<double> sorted = voiced;
  std::sort(sorted.begin(), sorted.end());
  s.medianHz = sorted[sorted.size() / 2];
  const std::size_t quarter = std::max<std::size_t>(1, voiced.size() / 4);
  std::vector<double> firstV(voiced.begin(), voiced.begin() + quarter);
  std::vector<double> lastV(voiced.end() - quarter, voiced.end());
  std::sort(firstV.begin(), firstV.end());
  std::sort(lastV.begin(), lastV.end());
  s.startHz = firstV[firstV.size() / 2];
  s.endHz = lastV[lastV.size() / 2];
  return s;
}

}  // namespace pitchlab::proto
