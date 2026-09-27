// Pitch Lab — waveform/duration metrics (implementation specification §10.4
// items 8-10, frozen cycle 5 BEFORE coding): peak-rms-crest (exact),
// realised-duration (contract-linked), latency (declared + measured).
//
// These are PURE functions of the AnalysisContext (§4.10): files + manifest
// mirrors + tolerances in; named measurement values out. No engine
// knowledge, no re-render, no global quality number.

#include <algorithm>
#include <cmath>
#include <string>

#include "analysis/analysis_context.h"
#include "analysis/analysis_types.h"
#include "analysis/spectral.h"

namespace pitchlab::analysis {
namespace {

[[nodiscard]] std::string chSuffix(int c) { return "Ch" + std::to_string(c); }

[[nodiscard]] json::Value latencyMs(double frames, double fs) {
  return json::Value(frames * 1000.0 / fs);
}

}  // namespace

MetricResult computePeakRmsCrest(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "peak-rms-crest";
  r.unit = "linear-amplitude";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("none");
  r.method["peakDefinition"] = json::Value("absolute (max |x|), ascending scan");
  r.method["rmsDefinition"] = json::Value("sqrt(sum(x^2)/N), ascending accumulation");
  r.method["crestDefinition"] = json::Value("peak/rms (+ 20log10 crest)");
  r.tolerance = ToleranceInfo::exact();

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  const int C = ctx.channels;
  if (static_cast<int>(ctx.output.wav.channels.size()) != C) {
    r.status = Status::AnalysisError;
    r.error = "manifest channel count does not match the master WAV";
    return r;
  }
  const int64_t n = ctx.output.wav.meta.frames;
  if (n == 0) {
    r.status = Status::InsufficientData;
    r.notes.push_back("master has zero frames");
    return r;
  }
  r.hasSampleCount = true;
  r.sampleCount = n;

  for (int c = 0; c < C; ++c) {
    const std::vector<double>& x = ctx.output.wav.channels[static_cast<std::size_t>(c)];
    double peak = 0.0;
    double sumSq = 0.0;
    for (int64_t i = 0; i < n; ++i) {
      const double a = std::fabs(x[static_cast<std::size_t>(i)]);
      if (a > peak) peak = a;  // ascending scan; ties keep the first max
      sumSq += x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)];
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(n));
    r.values["peak" + chSuffix(c)] = json::Value(peak);
    r.values["rms" + chSuffix(c)] = json::Value(rms);
    if (rms > 0.0 && peak > 0.0) {
      const double crest = peak / rms;
      r.values["crest" + chSuffix(c)] = json::Value(crest);
      r.values["crestDb" + chSuffix(c)] = json::Value(20.0 * std::log10(crest));
    } else {
      r.values["crest" + chSuffix(c)] = json::Value(nullptr);
      r.values["crestDb" + chSuffix(c)] = json::Value(nullptr);
      r.notes.push_back("channel " + std::to_string(c) + " is silent: crest undefined (null)");
    }
  }
  r.status = Status::Ok;
  return r;
}

MetricResult computeRealisedDuration(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "realised-duration";
  r.unit = "frames";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("manifest-accounting");
  r.method["source"] = json::Value("render manifest (expected/actual) + master WAV frames");
  r.method["preservingPolicy"] = json::Value("N_in + outputLatency (contract, §4.3.3)");
  r.method["rateFollowingPolicy"] =
      json::Value("integrated-curve expectation + flush (§4.3.4, OD-6)");

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  // Cross-check: the file must agree with the manifest's actual frames.
  if (ctx.output.wav.meta.frames != ctx.actualOutputFrames) {
    r.status = Status::AnalysisError;
    r.error = "manifest/WAV frame mismatch: manifest actualOutputFrames=" +
              std::to_string(ctx.actualOutputFrames) + ", WAV frames=" +
              std::to_string(ctx.output.wav.meta.frames);
    return r;
  }
  const double fs = static_cast<double>(ctx.sampleRate);
  const int64_t inF = ctx.inputFrames;
  const int64_t outF = ctx.actualOutputFrames;
  const int64_t expF = ctx.expectedOutputFrames;
  const double expectedRatio = expF != 0 ? static_cast<double>(expF) / static_cast<double>(inF)
                                         : 0.0;
  const double realisedRatio = static_cast<double>(outF) / static_cast<double>(inF);
  const double relativeDelta = expF != 0 ? static_cast<double>(ctx.actualOutputFrames - expF) /
                                               static_cast<double>(expF)
                                         : 0.0;

  r.values["inputFrames"] = json::Value(inF);
  r.values["outputFrames"] = json::Value(outF);
  r.values["inputDurationSec"] = json::Value(static_cast<double>(inF) / fs);
  r.values["outputDurationSec"] = json::Value(static_cast<double>(outF) / fs);
  r.values["realisedRatio"] = json::Value(realisedRatio);
  r.values["expectedRatio"] = json::Value(expectedRatio);
  r.values["expectedOutputFrames"] = json::Value(expF);
  r.values["lengthDeltaFrames"] = json::Value(ctx.actualOutputFrames - expF);
  r.values["relativeDelta"] = json::Value(relativeDelta);
  r.values["durationBehaviour"] = json::Value(ctx.durationBehaviour);
  {
    json::Object lat;
    lat["inputFrames"] = json::Value(ctx.declaredLatency.inputFrames);
    lat["outputFrames"] = json::Value(ctx.declaredLatency.outputFrames);
    r.values["declaredLatency"] = json::Value(std::move(lat));
  }
  {
    json::Object lp;
    lp["status"] = json::Value(ctx.lengthPolicyTaint ? "taint" : "ok");
    lp["toleranceFrames"] = json::Value(ctx.tolerances.lengthRateFollowingFrames);
    r.values["lengthPolicy"] = json::Value(std::move(lp));
  }
  r.hasFrameCount = true;
  r.frameCount = outF;
  if (ctx.isRateFollowing()) {
    r.tolerance = ToleranceInfo::provisional(
        static_cast<double>(ctx.tolerances.lengthRateFollowingFrames),
        "length_rate_following_frames");
  } else {
    r.tolerance = ToleranceInfo::exact();
    r.notes.push_back("Preserving engines: canonical length N_in + outputLatency "
                      "(§4.3.3) — exact contract, no tolerance band");
  }
  r.status = ctx.lengthPolicyTaint ? Status::TaintedInput : Status::Ok;
  if (ctx.lengthPolicyTaint) {
    r.notes.push_back("render carries the length-policy taint; values still measured "
                      "(OD-6 evidence discipline)");
  }
  return r;
}

MetricResult computeLatency(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "latency";
  r.unit = "frames";
  r.alignment["axis"] = json::Value("input-timeline");
  r.alignment["method"] = json::Value("impulse-aligned (detector onset, timeline-aligned)");
  r.method["declaredSource"] = json::Value("render manifest declaredLatency");
  r.method["measuredFlush"] =
      json::Value("actualOutputFrames - output frames covering real input");
  r.method["onsetDetector"] = json::Value("threshold 0.25*max envelope, 50 ms refractory");
  r.method["testSignal"] = json::Value(ctx.assetId);

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  const double fs = static_cast<double>(ctx.sampleRate);
  r.values["declaredInputLatencyFrames"] = json::Value(ctx.declaredLatency.inputFrames);
  r.values["declaredOutputLatencyFrames"] = json::Value(ctx.declaredLatency.outputFrames);
  r.values["declaredInputLatencyMs"] = latencyMs(static_cast<double>(ctx.declaredLatency.inputFrames), fs);
  r.values["declaredOutputLatencyMs"] =
      latencyMs(static_cast<double>(ctx.declaredLatency.outputFrames), fs);

  // ---- measured part 1: the flush tail (the measured counterpart of the
  // declared output latency — frames beyond the real-input timeline span).
  int64_t mInputEnd;
  if (ctx.isRateFollowing()) {
    if (!ctx.emissionMap || ctx.emissionMap->empty()) {
      r.status = Status::AnalysisError;
      r.error = "RateFollowing engine without an emission-map reconstruction";
      return r;
    }
    mInputEnd = ctx.emissionMap->framesCoveringInput(ctx.inputFrames - 1);
  } else {
    mInputEnd = ctx.inputFrames;
  }
  const int64_t measuredFlush = ctx.actualOutputFrames - mInputEnd;
  const int64_t deltaFlush = measuredFlush - ctx.declaredLatency.outputFrames;
  r.values["measuredFlushFrames"] = json::Value(measuredFlush);
  r.values["measuredFlushMs"] = latencyMs(static_cast<double>(measuredFlush), fs);
  r.values["deltaDeclaredVsMeasuredFlushFrames"] = json::Value(deltaFlush);
  r.values["deltaDeclaredVsMeasuredFlushMs"] = latencyMs(static_cast<double>(deltaFlush), fs);
  const double deltaMs = std::fabs(deltaFlush) * 1000.0 / fs;
  r.values["conformant"] =
      json::Value(deltaMs <= std::fabs(ctx.tolerances.latencyDeclaredVsMeasuredMs));
  r.notes.push_back("capability-honesty comparison at the provisional tolerance "
                    "latency_declared_vs_measured_ms (reported flag, never a gate)");

  // ---- measured part 2: the onset offset (signal-level view; the impulse/
  // transient corpus provides a detectable onset).
  if (!ctx.input.loaded || ctx.input.wav.channels.empty()) {
    r.values["measuredOnsetOffsetFrames"] = json::Value(nullptr);
    r.notes.push_back("input asset not loaded: onset offset not measured");
  } else {
    const OnsetDetection inDet = detectOnsets(ctx.input.wav.channels, fs);
    const OnsetDetection outDet = detectOnsets(ctx.output.wav.channels, fs);
    if (inDet.onsets.empty()) {
      r.values["measuredOnsetOffsetFrames"] = json::Value(nullptr);
      r.notes.push_back("no onset in the input material: onset offset not applicable "
                        "(designed for the impulse/percussive corpus)");
    } else if (outDet.onsets.empty()) {
      r.values["measuredOnsetOffsetFrames"] = json::Value(nullptr);
      r.notes.push_back("no onset detected in the output (silent or below threshold)");
    } else {
      const int64_t pIn = inDet.onsets.front();
      int64_t expectedOut;
      if (ctx.isRateFollowing()) {
        expectedOut = ctx.emissionMap->outputPositionOfInput(pIn);
      } else {
        expectedOut = pIn;  // Preserving: output frame k <-> input frame k
      }
      const int64_t measuredOut = outDet.onsets.front();
      const int64_t offset = measuredOut - expectedOut;
      r.values["expectedOnsetFrame"] = json::Value(expectedOut);
      r.values["measuredOnsetFrame"] = json::Value(measuredOut);
      r.values["measuredOnsetOffsetFrames"] = json::Value(offset);
      r.values["measuredOnsetOffsetMs"] = latencyMs(static_cast<double>(offset), fs);
      if (offset != 0) {
        r.notes.push_back("non-zero onset offset: pre-echo (negative) or delay (positive) "
                          "measured relative to the timeline-aligned expectation — a "
                          "measurement, not a failure");
      }
    }
  }
  r.tolerance =
      ToleranceInfo::provisional(ctx.tolerances.latencyDeclaredVsMeasuredMs,
                                 "latency_declared_vs_measured_ms");
  r.status = Status::Ok;
  return r;
}

}  // namespace pitchlab::analysis
