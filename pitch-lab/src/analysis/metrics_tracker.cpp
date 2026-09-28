// Pitch Lab — tracker-dependent metrics (implementation specification §10.5
// item 10, frozen cycle 6 BEFORE coding — supersedes the §10.4 item 19 gated
// stubs): pitch-error, pitch-lag, warble-instability, now MEASURED through
// the clean-room C++ pYIN ReferencePitchTracker (OD-12, §10.5). cpu-cost
// stays honestly not-applicable (the renderer-side timing boundary does not
// exist in v0.1 and is NOT faked — unchanged from §10.4 item 19).
//
// Shared frozen machinery (§10.5 item 10):
//   * the tracker runs on output channel 0 (the v0.1 tracker-material is
//     mono — the phase-coherence precedent, documented in notes);
//   * a frame is IN-SPAN when its center maps to real input (Preserving:
//     center < inputFrames; RateFollowing: r(center) <= inputFrames-1);
//   * expected f0 at an in-span center:
//       Preserving:   ratioEff[clamp(floor(center))] * f_in(center/fs)
//       RateFollowing: ratioEff[clamp(floor(r(center)))] * f_in(r(center)/fs)
//     (the §6.1 read-position rule — exact for varispeed by the chain rule;
//     the class-level ideal for the Preserving engines, whose deviation IS
//     the measurement);
//   * assets without an analytic f0 recipe => not-applicable (never a
//     tracked-vs-tracked comparison — the §10.1 "analytic from input" rule).
//
// No global quality score, no ranking, no aggregation: three separate
// measured dimensions, per the cycle hard rules.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "analysis/analysis_context.h"
#include "analysis/analysis_types.h"
#include "analysis/pitch_tracker.h"
#include "analysis/spectral.h"

namespace pitchlab::analysis {
namespace {

/// The frozen lag-search half-width for pitch-lag (§10.5 item 10):
/// +-16 frames (~171 ms at 48 kHz — covers a full default 0.1 s grain
/// plus margin).
constexpr int64_t kMaxLagFrames = 16;

/// The method-metadata block shared by the three tracker metrics (§10.5
/// item 10, method metadata).
void trackerMethodMetadata(MetricResult* r) {
  (*r).method["tracker"] = json::Value("clean-room C++ pYIN (OD-12, §10.5)");
  (*r).method["bandHz"] = json::Value("[50, 1000]");
  (*r).method["window"] = json::Value("nextPow2(ceil(fs*2048/48000)) (the §10.4 item 6a per-rate rule)");
  (*r).method["hop"] = json::Value("window/4 (frames start at 0, centers timestamped)");
  (*r).method["thresholdPrior"] = json::Value("100-point grid 0.01..1.0, Beta(2, 18)");
  (*r).method["boltzmannKappa"] = json::Value(2.0);
  (*r).method["noTroughProb"] = json::Value(0.01);
  (*r).method["hmmBins"] = json::Value("10-cent bins over the band, 2K voiced/unvoiced states");
  (*r).method["hmmMaxJumpBins"] = json::Value(static_cast<int64_t>(40));
  (*r).method["hmmStick"] = json::Value(0.99);
  (*r).method["hmmVoiceSwitch"] = json::Value(0.01);
}

/// The shared per-frame comparison record: for each tracker frame, the
/// in-span flag, the expected f0 (analytic model), and the measured f0 when
/// voiced. Frames are compared ONLY when in-span AND voiced.
struct FrameCompare {
  int64_t center = 0;
  bool inSpan = false;
  bool voiced = false;
  double fExpected = 0.0;
  double fMeasured = 0.0;
};

/// Build the per-frame comparison records (the §10.5 item 10 shared
/// machinery). Requires the output signal loaded and the recipe declared;
/// the caller has already checked applicability.
[[nodiscard]] std::vector<FrameCompare> buildFrameComparison(const AnalysisContext& ctx,
                                                            const PitchTrack& track) {
  std::vector<FrameCompare> frames(static_cast<std::size_t>(track.frameCount));
  const double fs = static_cast<double>(ctx.sampleRate);
  const int64_t nIn = ctx.inputFrames;
  const std::vector<double>& ratio = ctx.effectiveRatio;
  const double totalSec = nIn > 0 ? static_cast<double>(nIn) / fs : 0.0;
  for (int64_t f = 0; f < track.frameCount; ++f) {
    FrameCompare& fc = frames[static_cast<std::size_t>(f)];
    const PitchTrackFrame& tf = track.frames[static_cast<std::size_t>(f)];
    fc.center = tf.center;
    fc.voiced = tf.voiced;
    fc.fMeasured = tf.f0Hz;
    if (ctx.isRateFollowing()) {
      // r(center) from the emission map (integer output position).
      const double rPos =
          (ctx.emissionMap != nullptr && fc.center < ctx.emissionMap->size())
          ? ctx.emissionMap->at(fc.center)
          : static_cast<double>(nIn);  // beyond the map: out of span
      fc.inSpan = rPos <= static_cast<double>(nIn - 1);
      if (fc.inSpan) {
        int64_t idx = static_cast<int64_t>(std::floor(rPos));
        if (idx < 0) idx = 0;
        if (idx > nIn - 1) idx = nIn - 1;
        const double ratioAt = ratio.empty() ? 1.0 : ratio[static_cast<std::size_t>(idx)];
        fc.fExpected = ratioAt * ctx.asset.f0.f0At(rPos / fs, totalSec);
      }
    } else {
      fc.inSpan = fc.center < nIn;
      if (fc.inSpan) {
        int64_t idx = static_cast<int64_t>(std::floor(static_cast<double>(fc.center)));
        if (idx < 0) idx = 0;
        if (idx > nIn - 1) idx = nIn - 1;
        const double ratioAt = ratio.empty() ? 1.0 : ratio[static_cast<std::size_t>(idx)];
        fc.fExpected = ratioAt * ctx.asset.f0.f0At(static_cast<double>(fc.center) / fs, totalSec);
      }
    }
  }
  return frames;
}

/// Median of |e| over the compared deviations (even count => mean of the
/// two middle values — the spectral-error convention).
[[nodiscard]] double medianAbsolute(const std::vector<double>& absE) {
  std::vector<double> v = absE;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  if (n == 0) return 0.0;
  if (n % 2 == 1) return v[n / 2];
  return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

/// 95th percentile of |e|, nearest-rank (sorted ascending, 1-based index
/// ceil(0.95*n)).
[[nodiscard]] double p95Absolute(const std::vector<double>& absE) {
  std::vector<double> v = absE;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  if (n == 0) return 0.0;
  std::size_t idx = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n)));
  if (idx < 1) idx = 1;
  if (idx > n) idx = n;
  return v[idx - 1];
}

/// Pearson correlation of two equal-length series (ascending accumulation).
[[nodiscard]] bool pearson(const std::vector<double>& x, const std::vector<double>& y,
                          double* rOut) {
  const std::size_t n = x.size();
  if (n < 2) return false;
  double sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    sx += x[i];
    sy += y[i];
    sxx += x[i] * x[i];
    syy += y[i] * y[i];
    sxy += x[i] * y[i];
  }
  const double nD = static_cast<double>(n);
  const double cov = sxy - sx * sy / nD;
  const double vx = sxx - sx * sx / nD;
  const double vy = syy - sy * sy / nD;
  if (vx <= 0.0 || vy <= 0.0) return false;  // constant series: undefined
  *rOut = cov / std::sqrt(vx * vy);
  return true;
}

/// The not-applicable result for assets without an analytic f0 recipe.
[[nodiscard]] MetricResult recipeGated(const char* metricId, const char* unit,
                                     const std::string& assetId) {
  MetricResult r;
  r.metricId = metricId;
  r.unit = unit;
  r.alignment["axis"] = json::Value("input-timeline");
  r.alignment["method"] = json::Value("emission-map (rate-followers) / identity (preserving)");
  trackerMethodMetadata(&r);
  r.notes.push_back("asset '" + assetId + "' declares no analytic f0 recipe — the "
                    "expected-f0 model is undefined for this material class (§10.5 "
                    "item 10; no substitute expected-f0 estimator exists)");
  r.status = Status::NotApplicable;
  return r;
}

}  // namespace

MetricResult computePitchError(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "pitch-error";
  r.unit = "cents";
  r.alignment["axis"] = json::Value("input-timeline");
  r.alignment["method"] = json::Value(
      "emission-map (rate-followers) / identity (preserving), frame centers");
  trackerMethodMetadata(&r);
  r.tolerance = ToleranceInfo::provisional(
      ctx.tolerances.pitchErrorMedianCents, "pitch_error_median_cents");
  r.notes.push_back("tolerance is REPORTED CONTEXT (the owner's provisional budget), "
                    "never a gate — the metric measures (OD-9 discipline)");

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  if (!ctx.asset.hasF0Recipe) {
    return recipeGated("pitch-error", "cents", ctx.assetId);
  }

  const PitchTrack track =
      trackPitch(ctx.output.wav.channels[0], static_cast<double>(ctx.sampleRate),
                 kTrackerFminHz, kTrackerFmaxHz);
  const std::vector<FrameCompare> frames = buildFrameComparison(ctx, track);
  const int64_t total = track.frameCount;

  r.values["trackerFrameCount"] = json::Value(total);
  r.values["trackerWindowFrames"] = json::Value(track.windowFrames);
  r.values["trackerHop"] = json::Value(track.hop);
  r.values["droppedTailFrames"] = json::Value(track.droppedTailFrames);
  r.hasFrameCount = true;
  r.frameCount = total;
  r.notes.push_back("tracker on output channel 0 (the v0.1 tracker-material is mono — "
                    "the phase-coherence precedent)");

  if (total <= 0) {
    r.status = Status::InsufficientData;
    r.values["medianCents"] = json::Value(nullptr);
    r.values["p95Cents"] = json::Value(nullptr);
    r.values["excludedVoicedFraction"] = json::Value(1.0);
    r.values["comparedFrameCount"] = json::Value(static_cast<int64_t>(0));
    r.values["voicedFrameCount"] = json::Value(static_cast<int64_t>(0));
    r.notes.push_back("signal shorter than one tracker window: zero frames");
    return r;
  }

  int64_t voicedCount = 0;
  int64_t compared = 0;
  int64_t outOfBandExpected = 0;
  std::vector<double> absE;
  std::vector<double> fMeasured;
  for (const FrameCompare& fc : frames) {
    if (fc.voiced) ++voicedCount;
    if (!(fc.voiced && fc.inSpan)) continue;
    if (!(fc.fMeasured > 0.0) || !(fc.fExpected > 0.0)) continue;
    ++compared;
    if (fc.fExpected < kTrackerFminHz || fc.fExpected > kTrackerFmaxHz) {
      ++outOfBandExpected;
    }
    absE.push_back(std::fabs(1200.0 * std::log2(fc.fMeasured / fc.fExpected)));
    fMeasured.push_back(fc.fMeasured);
  }
  r.values["voicedFrameCount"] = json::Value(voicedCount);
  r.values["comparedFrameCount"] = json::Value(compared);
  r.values["excludedVoicedFraction"] =
      json::Value(static_cast<double>(total - compared) / static_cast<double>(total));
  if (outOfBandExpected > 0) {
    r.notes.push_back("expected f0 outside the tracker band [50, 1000] Hz for " +
                      std::to_string(outOfBandExpected) +
                      " compared frame(s): the tracker locks onto in-band "
                      "subharmonics there (§10.5 item 3) — the deviation measures "
                      "the tracker range limit, an honest result");
  }

  if (compared <= 0) {
    r.status = Status::InsufficientData;
    r.values["medianCents"] = json::Value(nullptr);
    r.values["p95Cents"] = json::Value(nullptr);
    r.values["f0MedianMeasuredHz"] = json::Value(nullptr);
    r.notes.push_back("no comparable frames: the tracker found none in-span and voiced "
                      "(all excluded — unvoiced, out-of-span or above-band)");
    return r;
  }
  r.values["medianCents"] = json::Value(medianAbsolute(absE));
  r.values["p95Cents"] = json::Value(p95Absolute(absE));
  {
    std::vector<double> logs;
    logs.reserve(fMeasured.size());
    for (double f : fMeasured) logs.push_back(std::log2(f));
    std::sort(logs.begin(), logs.end());
    const std::size_t n = logs.size();
    const double m = (n % 2 == 1)
        ? logs[n / 2]
        : 0.5 * (logs[n / 2 - 1] + logs[n / 2]);
    r.values["f0MedianMeasuredHz"] = json::Value(std::exp2(m));
  }
  r.status = Status::Ok;
  return r;
}

MetricResult computePitchLag(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "pitch-lag";
  r.unit = "ms";
  r.alignment["axis"] = json::Value("input-timeline");
  r.alignment["method"] = json::Value(
      "emission-map (rate-followers) / identity (preserving), frame centers");
  trackerMethodMetadata(&r);
  r.method["lagSearchFrames"] = json::Value(kMaxLagFrames);
  r.method["lagScanOrder"] = json::Value("0, +1, -1, ..., +-L (first strict max wins)");
  r.method["sign"] = json::Value("positive lagMs = the measured LAGS the expected");
  r.tolerance = ToleranceInfo::none();
  r.notes.push_back("the §10.1 sheet carries a provisional ±1 ms intent (arch §H.1); "
                    "no tolerances.toml key exists and none is invented (OD-9) — "
                    "pure measurement, correlationAtLag carries discriminability");
  r.notes.push_back("on slowly-monotone curves the correlation surface is flat (a line "
                    "correlates with its shifted self) — the metric is designed for "
                    "ramps/reversals (direction changes)");

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  if (!ctx.asset.hasF0Recipe) {
    return recipeGated("pitch-lag", "ms", ctx.assetId);
  }
  if (ctx.curveStatic) {
    r.status = Status::NotApplicable;
    r.notes.push_back("flat curve: no modulation to lag (§10.1 — the metric is defined "
                      "under ramps/reversals)");
    return r;
  }

  const PitchTrack track =
      trackPitch(ctx.output.wav.channels[0], static_cast<double>(ctx.sampleRate),
                 kTrackerFminHz, kTrackerFmaxHz);
  const std::vector<FrameCompare> frames = buildFrameComparison(ctx, track);
  const int64_t total = track.frameCount;
  r.values["trackerFrameCount"] = json::Value(total);
  r.values["trackerHop"] = json::Value(track.hop);
  r.hasFrameCount = true;
  r.frameCount = total;

  if (total <= 0) {
    r.status = Status::InsufficientData;
    r.values["lagMs"] = json::Value(nullptr);
    r.values["correlationAtLag"] = json::Value(nullptr);
    r.values["voicedFrameCount"] = json::Value(static_cast<int64_t>(0));
    r.notes.push_back("signal shorter than one tracker window: zero frames");
    return r;
  }

  // The usable set: in-span AND voiced frames. Series in cents relative to
  // the geometric mean g of the expected over the usable set (Pearson is
  // affine-invariant — the reference is cosmetic; frozen anyway, §10.5).
  std::vector<int64_t> usable;
  std::vector<double> cExp(static_cast<std::size_t>(total), 0.0);
  std::vector<double> cMeas(static_cast<std::size_t>(total), 0.0);
  double logSumExp = 0.0;
  for (int64_t f = 0; f < total; ++f) {
    const FrameCompare& fc = frames[static_cast<std::size_t>(f)];
    if (!(fc.voiced && fc.inSpan && fc.fMeasured > 0.0 && fc.fExpected > 0.0)) continue;
    usable.push_back(f);
    logSumExp += std::log(fc.fExpected);
  }
  r.values["voicedFrameCount"] = json::Value(static_cast<int64_t>(usable.size()));
  if (usable.size() < 3) {
    r.status = Status::InsufficientData;
    r.values["lagMs"] = json::Value(nullptr);
    r.values["correlationAtLag"] = json::Value(nullptr);
    r.notes.push_back("fewer than 3 usable (in-span, voiced) frames: the correlation "
                      "is undefined");
    return r;
  }
  const double g = std::exp(logSumExp / static_cast<double>(usable.size()));
  for (int64_t f : usable) {
    cExp[static_cast<std::size_t>(f)] =
        1200.0 * std::log2(frames[static_cast<std::size_t>(f)].fExpected / g);
    cMeas[static_cast<std::size_t>(f)] =
        1200.0 * std::log2(frames[static_cast<std::size_t>(f)].fMeasured / g);
  }

  // Lag search: scan order 0, +1, -1, +2, -2, ..., +-L; strict > replacement
  // (the smallest |lag| wins ties). Pairs (n, n-k) require BOTH frames usable:
  // the measured at n correlates against the expected from k frames EARLIER
  // — positive k = the measured LAGS the expected (the corrected pairing,
  // §10.5 item 10: a delayed engine measured(n) = expected(n-k0) maximises
  // the correlation exactly at k = k0).
  std::vector<bool> isUsable(static_cast<std::size_t>(total), false);
  for (int64_t f : usable) isUsable[static_cast<std::size_t>(f)] = true;
  double bestR = 0.0;
  int64_t bestLag = 0;
  bool haveBest = false;
  int64_t pairsAtBest = 0;
  for (int64_t step = 0; step <= kMaxLagFrames; ++step) {
    const int nLags = (step == 0) ? 1 : 2;
    for (int li = 0; li < nLags; ++li) {
      const int64_t k = (li == 0) ? step : -step;
      std::vector<double> xs;
      std::vector<double> ys;
      for (int64_t n = 0; n < total; ++n) {
        if (!isUsable[static_cast<std::size_t>(n)]) continue;
        const int64_t m = n - k;
        if (m < 0 || m >= total || !isUsable[static_cast<std::size_t>(m)]) continue;
        xs.push_back(cMeas[static_cast<std::size_t>(n)]);
        ys.push_back(cExp[static_cast<std::size_t>(m)]);
      }
      double rr = 0.0;
      if (xs.size() < 3 || !pearson(xs, ys, &rr)) continue;
      if (!haveBest || rr > bestR) {  // strict >: first (smallest |lag|) wins
        bestR = rr;
        bestLag = k;
        haveBest = true;
        pairsAtBest = static_cast<int64_t>(xs.size());
      }
    }
  }
  if (!haveBest) {
    r.status = Status::InsufficientData;
    r.values["lagMs"] = json::Value(nullptr);
    r.values["correlationAtLag"] = json::Value(nullptr);
    r.notes.push_back("no lag produced >= 3 pairs with a defined correlation");
    return r;
  }
  const double fs = static_cast<double>(ctx.sampleRate);
  const double hopSec = static_cast<double>(track.hop) / fs;
  r.values["lagMs"] = json::Value(static_cast<double>(bestLag) * hopSec * 1000.0);
  r.values["lagFrames"] = json::Value(bestLag);
  r.values["correlationAtLag"] = json::Value(bestR);
  r.values["pairsAtLag"] = json::Value(pairsAtBest);
  r.status = Status::Ok;
  return r;
}

MetricResult computeWarbleInstability(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "warble-instability";
  r.unit = "cents";
  r.alignment["axis"] = json::Value("input-timeline");
  r.alignment["method"] = json::Value(
      "emission-map (rate-followers) / identity (preserving), frame centers");
  trackerMethodMetadata(&r);
  r.tolerance = ToleranceInfo::none();

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  if (!ctx.asset.hasF0Recipe) {
    return recipeGated("warble-instability", "cents", ctx.assetId);
  }
  if (!ctx.curveStatic) {
    r.status = Status::NotApplicable;
    r.notes.push_back("dynamic curve: the curve's own modulation dominates — the "
                      "metric is defined under static ratios (§10.1)");
    return r;
  }

  const PitchTrack track =
      trackPitch(ctx.output.wav.channels[0], static_cast<double>(ctx.sampleRate),
                 kTrackerFminHz, kTrackerFmaxHz);
  const std::vector<FrameCompare> frames = buildFrameComparison(ctx, track);
  const int64_t total = track.frameCount;
  r.values["trackerFrameCount"] = json::Value(total);
  r.hasFrameCount = true;
  r.frameCount = total;

  // The deviation series d(n) = 1200*log2(f_meas/f_exp) over in-span voiced
  // frames: the expected model removes the input's own modulation — vibrato
  // inputs measure the ENGINE's flutter (the design intent, §10.5).
  std::vector<double> dev;
  int64_t voicedCount = 0;
  for (const FrameCompare& fc : frames) {
    if (fc.voiced) ++voicedCount;
    if (!(fc.voiced && fc.inSpan && fc.fMeasured > 0.0 && fc.fExpected > 0.0)) continue;
    dev.push_back(1200.0 * std::log2(fc.fMeasured / fc.fExpected));
  }
  r.values["voicedFrameCount"] = json::Value(voicedCount);
  r.values["comparedFrameCount"] = json::Value(static_cast<int64_t>(dev.size()));

  if (dev.empty()) {
    r.status = Status::InsufficientData;
    r.values["f0FlutterStdCents"] = json::Value(nullptr);
    r.values["f0MeanHz"] = json::Value(nullptr);
    r.values["deviationMeanCents"] = json::Value(nullptr);
    r.notes.push_back("no comparable frames: the tracker found none in-span and voiced");
    return r;
  }
  double mean = 0.0;
  for (double v : dev) mean += v;
  mean /= static_cast<double>(dev.size());
  double var = 0.0;
  for (double v : dev) var += (v - mean) * (v - mean);
  var /= static_cast<double>(dev.size());
  // f0MeanHz: the geometric mean of the measured f0 over compared frames.
  double logSum = 0.0;
  for (const FrameCompare& fc : frames) {
    if (!(fc.voiced && fc.inSpan && fc.fMeasured > 0.0 && fc.fExpected > 0.0)) continue;
    logSum += std::log2(fc.fMeasured);
  }
  r.values["f0FlutterStdCents"] = json::Value(std::sqrt(var));
  r.values["deviationMeanCents"] = json::Value(mean);
  r.values["f0MeanHz"] =
      json::Value(std::exp2(logSum / static_cast<double>(dev.size())));
  r.notes.push_back("f0FlutterStdCents is the AC fluctuation of the deviation series "
                    "(bias removed — the bias is reported separately as "
                    "deviationMeanCents, never combined)");
  r.status = Status::Ok;
  return r;
}

MetricResult computeCpuCost(const AnalysisContext& ctx) {
  (void)ctx;
  MetricResult r;
  r.metricId = "cpu-cost";
  r.unit = "none";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("renderer-measured (measurement-only)");
  r.method["declaredInputs"] =
      json::Value("per block size {32..4096}: real-time factor, single thread");
  r.values["realTimeFactor"] = json::Value(nullptr);
  r.values["blockFrames"] = json::Value(nullptr);
  r.values["processingTimeSec"] = json::Value(nullptr);
  r.notes.push_back("not-applicable: renderer-side timing instrumentation is not "
                    "implemented in v0.1; cpu-cost is a measurement-only metric "
                    "(machine-dependent, never a CI gate) — the timing boundary is "
                    "not faked (§10.4 item 19, unchanged by OD-12)");
  r.status = Status::NotApplicable;
  return r;
}

}  // namespace pitchlab::analysis
