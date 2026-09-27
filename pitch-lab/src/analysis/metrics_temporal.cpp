// Pitch Lab — temporal metrics (implementation specification §10.4 items
// 11-12, frozen cycle 5 BEFORE coding): onset-timing (detector + expected
// positions), transient-preservation (onset-time error + attack correlation
// vs the varispeed reference — two SEPARATE components, never combined).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "analysis/analysis_context.h"
#include "analysis/analysis_types.h"
#include "analysis/spectral.h"

namespace pitchlab::analysis {
namespace {

constexpr int64_t kMaxHarmonics = 6;  // unused here; kept for shared helpers

/// Greedy-nearest onset matching (§10.4 item 11): expected-ascending; each
/// expected onset pairs with the unused measured onset minimising |q - e|
/// within the match window; leftovers are extra/missed.
struct OnsetMatch {
  std::vector<int64_t> matchedExpected;
  std::vector<int64_t> matchedMeasured;
  int64_t missed = 0;
  int64_t extra = 0;
};

[[nodiscard]] OnsetMatch matchOnsets(const std::vector<int64_t>& expected,
                                     const std::vector<int64_t>& measured, int64_t window) {
  OnsetMatch m;
  std::vector<bool> used(measured.size(), false);
  for (int64_t e : expected) {
    int64_t bestIdx = -1;
    int64_t bestDist = window + 1;
    for (std::size_t j = 0; j < measured.size(); ++j) {
      if (used[j]) continue;
      const int64_t d = std::llabs(measured[j] - e);
      if (d <= window && d < bestDist) {
        bestDist = d;
        bestIdx = static_cast<int64_t>(j);
      }
    }
    if (bestIdx >= 0) {
      used[static_cast<std::size_t>(bestIdx)] = true;
      m.matchedExpected.push_back(e);
      m.matchedMeasured.push_back(measured[static_cast<std::size_t>(bestIdx)]);
    } else {
      ++m.missed;
    }
  }
  for (std::size_t j = 0; j < used.size(); ++j) {
    if (!used[j]) ++m.extra;
  }
  return m;
}

/// Map input onsets to expected output positions (§10.4 item 11).
[[nodiscard]] std::vector<int64_t> expectedOnsetPositions(const AnalysisContext& ctx,
                                                          const std::vector<int64_t>& inOnsets) {
  std::vector<int64_t> out;
  out.reserve(inOnsets.size());
  for (int64_t p : inOnsets) {
    if (ctx.isRateFollowing()) {
      out.push_back(ctx.emissionMap->outputPositionOfInput(p));
    } else {
      out.push_back(p);
    }
  }
  return out;
}

/// Normalised zero-lag cross-correlation of two equal-length windows.
[[nodiscard]] double normalisedCorrelation(const std::vector<double>& x,
                                           const std::vector<double>& y) {
  double sxy = 0.0, sxx = 0.0, syy = 0.0;
  const std::size_t n = std::min(x.size(), y.size());
  for (std::size_t i = 0; i < n; ++i) {
    sxy += x[i] * y[i];
    sxx += x[i] * x[i];
    syy += y[i] * y[i];
  }
  if (sxx == 0.0 || syy == 0.0) return 2.0;  // sentinel: undefined (caller notes)
  return sxy / std::sqrt(sxx * syy);
}

}  // namespace

MetricResult computeOnsetTiming(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "onset-timing";
  r.unit = "frames";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("expected positions from input onsets "
                                      "(identity / emission-map)");
  r.method["detector"] = json::Value("energy threshold 0.25*max(channel-max envelope)");
  r.method["refractoryMs"] = json::Value(50.0);
  r.method["matchWindowMs"] = json::Value(50.0);
  r.tolerance = ToleranceInfo::none();

  if (!ctx.output.loaded || ctx.output.wav.channels.empty() || !ctx.input.loaded ||
      ctx.input.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master or input asset not loaded";
    return r;
  }
  const double fs = static_cast<double>(ctx.sampleRate);
  const OnsetDetection inDet = detectOnsets(ctx.input.wav.channels, fs);
  if (inDet.onsets.empty()) {
    r.status = Status::NotApplicable;
    r.notes.push_back("material has no detectable onsets (designed for the transient "
                      "corpus; the detector reports what it finds)");
    return r;
  }
  const OnsetDetection outDet = detectOnsets(ctx.output.wav.channels, fs);
  const std::vector<int64_t> expected = expectedOnsetPositions(ctx, inDet.onsets);
  const int64_t window = static_cast<int64_t>(std::llround(0.05 * fs));
  const OnsetMatch match = matchOnsets(expected, outDet.onsets, window);

  json::Array errFrames;
  json::Array errMs;
  double sumAbs = 0.0;
  int64_t maxAbs = 0;
  for (std::size_t i = 0; i < match.matchedExpected.size(); ++i) {
    const int64_t e = match.matchedMeasured[i] - match.matchedExpected[i];
    errFrames.push_back(json::Value(e));
    errMs.push_back(json::Value(static_cast<double>(e) * 1000.0 / fs));
    sumAbs += static_cast<double>(std::llabs(e));
    if (std::llabs(e) > maxAbs) maxAbs = std::llabs(e);
  }
  r.values["expectedOnsetCount"] = json::Value(static_cast<int64_t>(expected.size()));
  r.values["measuredOnsetCount"] =
      json::Value(static_cast<int64_t>(outDet.onsets.size()));
  r.values["matched"] = json::Value(static_cast<int64_t>(match.matchedExpected.size()));
  r.values["missed"] = json::Value(match.missed);
  r.values["extra"] = json::Value(match.extra);
  r.values["onsetErrorsFrames"] = json::Value(std::move(errFrames));
  r.values["onsetErrorsMs"] = json::Value(std::move(errMs));
  const int64_t matched = static_cast<int64_t>(match.matchedExpected.size());
  r.values["meanAbsErrorFrames"] =
      json::Value(matched > 0 ? sumAbs / static_cast<double>(matched) : 0.0);
  r.values["maxAbsErrorFrames"] = json::Value(maxAbs);
  r.hasFrameCount = true;
  r.frameCount = ctx.actualOutputFrames;
  r.status = Status::Ok;
  return r;
}

MetricResult computeTransientPreservation(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "transient-preservation";
  r.unit = "frames";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("onset-aligned windows");
  r.method["detector"] = json::Value("energy threshold 0.25*max(channel-max envelope)");
  r.method["attackWindow"] =
      json::Value("half the analysis STFT window, centred on each signal's own "
                  "measured onset");
  r.method["components"] =
      json::Value("onset-time error and attack correlation reported SEPARATELY");
  r.tolerance = ToleranceInfo::none();

  if (!ctx.output.loaded || ctx.output.wav.channels.empty() || !ctx.input.loaded ||
      ctx.input.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master or input asset not loaded";
    return r;
  }
  if (!ctx.hasReference || !ctx.reference.loaded || ctx.reference.wav.channels.empty()) {
    r.status = Status::ReferenceUnavailable;
    r.notes.push_back("no usable native.varispeed reference render in this experiment "
                      "run (§10.4 item 7) — never re-rendered");
    return r;
  }
  const double fs = static_cast<double>(ctx.sampleRate);
  const OnsetDetection inDet = detectOnsets(ctx.input.wav.channels, fs);
  if (inDet.onsets.empty()) {
    r.status = Status::NotApplicable;
    r.notes.push_back("material has no detectable onsets");
    return r;
  }
  const OnsetDetection outDet = detectOnsets(ctx.output.wav.channels, fs);
  const OnsetDetection refDet = detectOnsets(ctx.reference.wav.channels, fs);

  const std::vector<int64_t> expected = expectedOnsetPositions(ctx, inDet.onsets);
  const int64_t window = static_cast<int64_t>(std::llround(0.05 * fs));
  const OnsetMatch matchIn = matchOnsets(expected, outDet.onsets, window);
  const OnsetMatch matchRef = matchOnsets(expected, refDet.onsets, window);

  // Onset-time error vs input (as onset-timing).
  json::Array errInFrames;
  for (std::size_t i = 0; i < matchIn.matchedExpected.size(); ++i) {
    errInFrames.push_back(
        json::Value(matchIn.matchedMeasured[i] - matchIn.matchedExpected[i]));
  }
  r.values["onsetTimeErrorVsInputFrames"] = json::Value(std::move(errInFrames));

  // Onset-time error vs reference: output onsets vs reference onsets, matched
  // through the same expected grid (pair i vs pair j when same expected).
  json::Array errRefFrames;
  json::Array attackCorr;
  const int64_t halfWin = analysisStftFrames(fs) / 2;
  std::vector<double> meanCorr;  // per channel
  meanCorr.assign(ctx.output.wav.channels.size(), 0.0);
  std::vector<int64_t> corrCount;
  corrCount.assign(ctx.output.wav.channels.size(), 0);
  std::size_t pairs = 0;
  for (std::size_t i = 0; i < matchIn.matchedExpected.size(); ++i) {
    // Find the reference pair with the same expected onset (they were matched
    // against the same grid in the same order).
    std::size_t refIdx = matchRef.matchedExpected.size();
    for (std::size_t j = 0; j < matchRef.matchedExpected.size(); ++j) {
      if (matchRef.matchedExpected[j] == matchIn.matchedExpected[i]) {
        refIdx = j;
        break;
      }
    }
    if (refIdx == matchRef.matchedExpected.size()) continue;
    const int64_t qOut = matchIn.matchedMeasured[i];
    const int64_t qRef = matchRef.matchedMeasured[refIdx];
    errRefFrames.push_back(json::Value(qOut - qRef));
    ++pairs;
    // Attack correlation: windows centred on each signal's OWN measured onset.
    bool anyNull = false;
    for (std::size_t c = 0; c < ctx.output.wav.channels.size(); ++c) {
      const std::vector<double>& xo = ctx.output.wav.channels[c];
      const std::vector<double>& xr = ctx.reference.wav.channels[c];
      const int64_t lo = std::max<int64_t>(0, qOut - halfWin);
      const int64_t hi = std::min<int64_t>(static_cast<int64_t>(xo.size()), qOut + halfWin);
      const int64_t loR = std::max<int64_t>(0, qRef - halfWin);
      const int64_t hiR = std::min<int64_t>(static_cast<int64_t>(xr.size()), qRef + halfWin);
      const int64_t len = std::min(hi - lo, hiR - loR);
      if (len <= 0) {
        attackCorr.push_back(json::Value(nullptr));
        anyNull = true;
        continue;
      }
      std::vector<double> wo(static_cast<std::size_t>(len));
      std::vector<double> wr(static_cast<std::size_t>(len));
      for (int64_t j = 0; j < len; ++j) {
        wo[static_cast<std::size_t>(j)] = xo[static_cast<std::size_t>(lo + j)];
        wr[static_cast<std::size_t>(j)] = xr[static_cast<std::size_t>(loR + j)];
      }
      const double corr = normalisedCorrelation(wo, wr);
      if (corr > 1.5) {  // undefined sentinel
        attackCorr.push_back(json::Value(nullptr));
        anyNull = true;
        continue;
      }
      attackCorr.push_back(json::Value(corr));
      meanCorr[c] += corr;
      ++corrCount[c];
    }
    if (anyNull) {
      r.notes.push_back("a window pair had zero energy or was clipped to nothing: "
                        "correlation null");
    }
  }
  r.values["onsetTimeErrorVsReferenceFrames"] = json::Value(std::move(errRefFrames));
  r.values["attackCorrelation"] = json::Value(std::move(attackCorr));
  for (std::size_t c = 0; c < meanCorr.size(); ++c) {
    const std::string suffix = "Ch" + std::to_string(c);
    r.values["meanAttackCorrelation" + suffix] =
        json::Value(corrCount[c] > 0
                        ? meanCorr[c] / static_cast<double>(corrCount[c])
                        : 0.0);
    r.values["attackWindowPairs" + suffix] = json::Value(corrCount[c]);
  }
  r.values["onsetsVsInput"] = json::Value(
      static_cast<int64_t>(matchIn.matchedExpected.size()));
  r.values["missedVsInput"] = json::Value(matchIn.missed);
  r.values["extraVsInput"] = json::Value(matchIn.extra);
  r.values["onsetPairsVsReference"] = json::Value(static_cast<int64_t>(pairs));
  r.notes.push_back("comparison against the varispeed reference = distance from "
                    "reference behaviour (§10.3), not a quality ranking");
  r.status = Status::Ok;
  return r;
}

}  // namespace pitchlab::analysis
