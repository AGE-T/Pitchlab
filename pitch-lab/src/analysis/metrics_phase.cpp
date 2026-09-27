// Pitch Lab — phase/stereo coherence metrics (implementation specification
// §10.4 items 17-18, frozen cycle 5 BEFORE coding):
//   * phase-coherence — vertical phase coherence R_h of partials h = 2..6
//     (circular concentration of increment coherence c_h = princarg(dPhi_h
//     - h*dPhi_1)); material-applicability from the asset category.
//   * stereo-coherence — zero-lag inter-channel correlation, input vs
//     output, delta reported (never an assertion that engines must
//     preserve coherence; PV decorrelation is a result to report).

#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

#include "analysis/analysis_context.h"
#include "analysis/analysis_types.h"
#include "analysis/spectral.h"

namespace pitchlab::analysis {
namespace {

/// Quadratic (parabolic) sub-bin peak interpolation on the power spectrum:
/// returns the interpolated frequency of the strongest peak in [fLo, fHi].
[[nodiscard]] bool parabolicPeakHz(const StftFrame& f, double binHz, double fLo, double fHi,
                                   int64_t binCount, double* outHz) {
  int64_t bestK = -1;
  double bestP = -1.0;
  for (int64_t k = 0; k < binCount; ++k) {
    const double p = f.power[static_cast<std::size_t>(k)];
    if (p > bestP) {
      const double freq = static_cast<double>(k) * binHz;
      if (freq >= fLo && freq <= fHi) {
        bestP = p;
        bestK = k;
      }
    }
  }
  if (bestK < 0) return false;
  double delta = 0.0;
  if (bestK >= 1 && bestK + 1 < binCount) {
    const double a = f.power[static_cast<std::size_t>(bestK - 1)];
    const double b = f.power[static_cast<std::size_t>(bestK)];
    const double cc = f.power[static_cast<std::size_t>(bestK + 1)];
    const double denom = a - 2.0 * b + cc;
    if (denom != 0.0) {
      delta = 0.5 * (a - cc) / denom;
      if (delta > 0.5) delta = 0.5;
      if (delta < -0.5) delta = -0.5;
    }
  }
  *outHz = (static_cast<double>(bestK) + delta) * binHz;
  return true;
}

/// Zero-lag normalised inter-channel correlation (full signals, ascending).
[[nodiscard]] bool interChannelRho(const std::vector<double>& L, const std::vector<double>& R,
                                   double* rho) {
  const std::size_t n = std::min(L.size(), R.size());
  if (n == 0) return false;
  double sLR = 0.0, sLL = 0.0, sRR = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    sLR += L[i] * R[i];
    sLL += L[i] * L[i];
    sRR += R[i] * R[i];
  }
  if (sLL == 0.0 || sRR == 0.0) return false;  // silent channel: undefined
  *rho = sLR / std::sqrt(sLL * sRR);
  return true;
}

}  // namespace

MetricResult computePhaseCoherence(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "phase-coherence";
  r.unit = "ratio";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("frame-aligned (STFT frames)");
  r.method["applicability"] =
      json::Value("asset category in {harmonic, voice-like} (declared material class)");
  r.method["f0SearchHz"] = json::Value("50 .. 1000 (parabolic peak per frame)");
  r.method["partials"] = json::Value("h = 2..6, exact-frequency projection at h*f0");
  r.method["statistic"] =
      json::Value("R_h = |mean exp(j*princarg(dPhi_h - h*dPhi_1))| (circular "
                  "concentration of increment coherence)");
  r.method["participation"] =
      json::Value("partial magnitude >= 1e-3 * fundamental magnitude (both frames)");
  r.tolerance = ToleranceInfo::none();

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  if (ctx.asset.category != "harmonic" && ctx.asset.category != "voice-like") {
    r.status = Status::NotApplicable;
    r.notes.push_back("material category '" + ctx.asset.category +
                      "' has no declared harmonic structure: phase coherence "
                      "not applicable (a score is never manufactured)");
    return r;
  }
  const double fs = static_cast<double>(ctx.sampleRate);
  const int64_t nw = analysisStftFrames(fs);
  const std::vector<double> w = periodicHann(nw);
  const int64_t hop = nw / 4;

  // Analysis on channel 0 (the metric's declared material is mono-harmonic;
  // per-channel application is a future extension — noted).
  const std::vector<double>& x = ctx.output.wav.channels[0];
  const StftResult stft = computeStft(x, fs);
  const int64_t bins = nw / 2 + 1;
  const double binHz = fs / static_cast<double>(nw);
  if (stft.frameCount < 3) {
    r.status = Status::InsufficientData;
    r.notes.push_back("fewer than 3 analysis frames: increment coherence undefined");
    return r;
  }

  // Per frame: tracked f0 + projected phases/magnitudes at h*f0 (h = 1..6).
  struct FrameInfo {
    double f0 = 0.0;
    Projection proj[7];  // index h = 1..6 (0 unused)
    bool valid = false;
  };
  std::vector<FrameInfo> info(static_cast<std::size_t>(stft.frameCount));
  std::vector<double> f0s;
  for (int64_t nF = 0; nF < stft.frameCount; ++nF) {
    FrameInfo& fi = info[static_cast<std::size_t>(nF)];
    double f0 = 0.0;
    if (!parabolicPeakHz(stft.frames[static_cast<std::size_t>(nF)], binHz, 50.0, 1000.0,
                         bins, &f0)) {
      fi.valid = false;
      continue;
    }
    fi.f0 = f0;
    fi.valid = true;
    f0s.push_back(f0);
    for (int h = 1; h <= 6; ++h) {
      const double fh = static_cast<double>(h) * f0;
      if (fh >= 0.45 * fs) {  // beyond the honest band: no participation
        fi.proj[h].re = 0.0;
        fi.proj[h].im = 0.0;
        continue;
      }
      fi.proj[h] = projectAt(x, stft.frames[static_cast<std::size_t>(nF)].start, w, fs, fh);
    }
  }
  std::sort(f0s.begin(), f0s.end());
  r.values["f0MedianHz"] = json::Value(f0s.empty()
                                           ? 0.0
                                           : f0s[f0s.size() / 2]);

  // Increment coherence per harmonic: c_h(n) = princarg(dPhi_h - h*dPhi_1)
  // between consecutive VALID frames; participation requires the partial's
  // magnitude >= 1e-3 * fundamental magnitude in BOTH frames.
  for (int h = 2; h <= 6; ++h) {
    const std::string key = "R" + std::to_string(h);
    const std::string cnt = "participatingFrames" + std::to_string(h);
    double sumRe = 0.0, sumIm = 0.0;
    int64_t count = 0;
    for (int64_t nF = 1; nF < stft.frameCount; ++nF) {
      const FrameInfo& a = info[static_cast<std::size_t>(nF - 1)];
      const FrameInfo& b = info[static_cast<std::size_t>(nF)];
      if (!a.valid || !b.valid) continue;
      const double magA1 = std::hypot(a.proj[1].re, a.proj[1].im);
      const double magB1 = std::hypot(b.proj[1].re, b.proj[1].im);
      if (magA1 <= 0.0 || magB1 <= 0.0) continue;
      const double magAh = std::hypot(a.proj[h].re, a.proj[h].im);
      const double magBh = std::hypot(b.proj[h].re, b.proj[h].im);
      if (magAh < 1e-3 * magA1 || magBh < 1e-3 * magB1) continue;
      const double phiAh = std::atan2(a.proj[h].im, a.proj[h].re);
      const double phiBh = std::atan2(b.proj[h].im, b.proj[h].re);
      const double phiA1 = std::atan2(a.proj[1].im, a.proj[1].re);
      const double phiB1 = std::atan2(b.proj[1].im, b.proj[1].re);
      const double c = princarg((phiBh - phiAh) - static_cast<double>(h) * (phiB1 - phiA1));
      sumRe += std::cos(c);
      sumIm += std::sin(c);
      ++count;
    }
    if (count == 0) {
      r.values[key] = json::Value(nullptr);
      r.values[cnt] = json::Value(0);
      r.notes.push_back("harmonic " + std::to_string(h) + " never participates (absent "
                        "or below the 1e-3 fundamental-magnitude floor)");
      continue;
    }
    r.values[key] = json::Value(std::hypot(sumRe, sumIm) / static_cast<double>(count));
    r.values[cnt] = json::Value(count);
  }
  r.values["stftWindowFrames"] = json::Value(nw);
  r.values["stftHop"] = json::Value(hop);
  r.values["frameCount"] = json::Value(stft.frameCount);
  r.hasFrameCount = true;
  r.frameCount = stft.frameCount;
  r.notes.push_back("R_h ~ 1: partials frequency/phase-locked across frames; R_h -> 0: "
                    "incoherent. The statistic is sensitive to sub-sample delay "
                    "modulation (fractional-read periodicity) — a real, measurable "
                    "behaviour, not a defect");
  r.notes.push_back("channel 0 (declared harmonic material is mono in the v0.1 corpus)");
  r.status = Status::Ok;
  return r;
}

MetricResult computeStereoCoherence(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "stereo-coherence";
  r.unit = "ratio";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("frame-aligned (input vs output pairs)");
  r.method["correlation"] =
      json::Value("zero-lag normalised cross-correlation, full signals");
  r.method["aggregation"] = json::Value("none (per-signal rho; delta reported)");
  r.tolerance = ToleranceInfo::none();

  if (!ctx.output.loaded || ctx.output.wav.channels.empty() || !ctx.input.loaded ||
      ctx.input.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master or input asset not loaded";
    return r;
  }
  if (ctx.channels < 2 || ctx.output.wav.channels.size() < 2 ||
      ctx.input.wav.channels.size() < 2) {
    r.status = Status::NotApplicable;
    r.notes.push_back("mono (or channel count < 2): inter-channel correlation undefined");
    return r;
  }
  double rhoIn = 0.0, rhoOut = 0.0;
  const bool okIn = interChannelRho(ctx.input.wav.channels[0], ctx.input.wav.channels[1], &rhoIn);
  const bool okOut =
      interChannelRho(ctx.output.wav.channels[0], ctx.output.wav.channels[1], &rhoOut);
  if (!okIn || !okOut) {
    r.status = Status::NotApplicable;
    r.notes.push_back("silent channel in the input or output: rho undefined "
                      "(never zero-substituted)");
    return r;
  }
  r.values["rhoInput"] = json::Value(rhoIn);
  r.values["rhoOutput"] = json::Value(rhoOut);
  r.values["delta"] = json::Value(rhoOut - rhoIn);
  {
    double inL = 0.0, inR = 0.0, outL = 0.0, outR = 0.0;
    for (double v : ctx.input.wav.channels[0]) inL += v * v;
    for (double v : ctx.input.wav.channels[1]) inR += v * v;
    for (double v : ctx.output.wav.channels[0]) outL += v * v;
    for (double v : ctx.output.wav.channels[1]) outR += v * v;
    r.values["inputEnergyCh0"] = json::Value(inL);
    r.values["inputEnergyCh1"] = json::Value(inR);
    r.values["outputEnergyCh0"] = json::Value(outL);
    r.values["outputEnergyCh1"] = json::Value(outR);
  }
  r.hasSampleCount = true;
  r.sampleCount = ctx.actualOutputFrames;
  r.notes.push_back("inter-channel decorrelation (e.g. PV phase divergence on "
                    "non-identical stereo) is a measured RESULT, not a bug to hide");
  r.status = Status::Ok;
  return r;
}

}  // namespace pitchlab::analysis
