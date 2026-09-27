// Pitch Lab — modulation metric (implementation specification §10.4 item
// 16, frozen cycle 5 BEFORE coding): amplitude-modulation — Hilbert-envelope
// spectrum; the envelope peak and the engine-declared characteristic rates.
// Modulation is MEASURED, never suppressed (grain-rate/crossfade AM is
// intentional experimental behaviour, §10.1).

// The vendored pocketfft header (analysis-path FFT; per-computation plans —
// §10.4 item 1; NOT an engine processing path). POCKETFFT_CACHE_SIZE 0: no
// hidden global plan cache.
#define POCKETFFT_CACHE_SIZE 0
#include <pocketfft/pocketfft_hdronly.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "analysis/analysis_context.h"
#include "analysis/analysis_types.h"
#include "analysis/spectral.h"

namespace pitchlab::analysis {
namespace {

[[nodiscard]] std::string chSuffix(int c) { return "Ch" + std::to_string(c); }

/// Envelope spectrum magnitudes of (e - mean(e)) windowed by the full-length
/// periodic Hann. Returns bin spacing and magnitudes for bins 0..N/2.
struct EnvelopeSpectrum {
  std::vector<double> mag;   // |E_k|, k = 0..N/2
  double binHz = 0.0;
};

[[nodiscard]] EnvelopeSpectrum envelopeSpectrum(const std::vector<double>& e, double fs) {
  EnvelopeSpectrum s;
  const int64_t n = static_cast<int64_t>(e.size());
  if (n <= 1) return s;
  double mean = 0.0;
  for (double v : e) mean += v;
  mean /= static_cast<double>(n);
  const std::vector<double> w = periodicHann(n);
  std::vector<double> buf(static_cast<std::size_t>(n));
  for (int64_t i = 0; i < n; ++i) {
    buf[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(i)] * (e[static_cast<std::size_t>(i)] - mean);
  }
  pocketfft::detail::pocketfft_r<double> plan(static_cast<std::size_t>(n));
  plan.exec(buf.data(), 1.0, true);
  const int64_t half = n / 2;
  s.mag.assign(static_cast<std::size_t>(half + 1), 0.0);
  for (int64_t k = 0; k <= half; ++k) {
    double re, im;
    if (k == 0) {
      re = buf[0];
      im = 0.0;
    } else if (k == half && n % 2 == 0) {
      re = buf[static_cast<std::size_t>(n - 1)];
      im = 0.0;
    } else {
      re = buf[static_cast<std::size_t>(2 * k - 1)];
      im = buf[static_cast<std::size_t>(2 * k)];
    }
    s.mag[static_cast<std::size_t>(k)] = std::hypot(re, im);
  }
  s.binHz = fs / static_cast<double>(n);
  return s;
}

/// Sum of squared magnitudes over the ±3 bins around the bin nearest fHz.
[[nodiscard]] double energyAround(const EnvelopeSpectrum& s, double fHz) {
  if (s.mag.empty()) return 0.0;
  const int64_t center = static_cast<int64_t>(std::llround(fHz / s.binHz));
  double sum = 0.0;
  for (int64_t k = std::max<int64_t>(1, center - 3);
       k <= std::min<int64_t>(static_cast<int64_t>(s.mag.size()) - 1, center + 3); ++k) {
    const double m = s.mag[static_cast<std::size_t>(k)];
    sum += m * m;
  }
  return sum;
}

/// The frozen per-engine expected-modulation-rate table (§10.4 item 16),
/// derived from the manifest parameter mirror + effective-curve extrema —
/// NEVER from engine internals.
[[nodiscard]] bool expectedModulationHz(const AnalysisContext& ctx, double fs,
                                        double* outHz) {
  *outHz = 0.0;
  const json::Object& params = ctx.engineParameters.asObject();
  auto getDouble = [&params](const char* key, double* v) {
    const auto it = params.find(key);
    if (it == params.end() || it->second.kind() != json::Value::Kind::Double) return false;
    *v = it->second.asDouble();
    return true;
  };
  auto getInt = [&params](const char* key, double* v) {
    const auto it = params.find(key);
    if (it == params.end()) return false;
    if (it->second.kind() == json::Value::Kind::Int) {
      *v = static_cast<double>(it->second.asInt());
      return true;
    }
    if (it->second.kind() == json::Value::Kind::Double) {
      *v = it->second.asDouble();
      return true;
    }
    return false;
  };
  if (ctx.engineId == "native.granular") {
    double grainSec = 0.0, overlap = 0.0;
    if (!getDouble("grain_seconds", &grainSec) || !getInt("overlap", &overlap)) return false;
    if (grainSec <= 0.0 || overlap < 1.0) return false;
    const double hop = std::llround(grainSec * fs / overlap);
    if (hop < 1.0) return false;
    *outHz = fs / hop;
    return true;
  }
  if (ctx.engineId == "native.pv.classic" || ctx.engineId == "native.pv.phaselocked") {
    double hop = 0.0;
    if (!getInt("hop", &hop) || hop < 1.0) return false;
    *outHz = fs / hop;
    return true;
  }
  if (ctx.engineId == "native.vardelay") {
    if (!ctx.curveStatic) return false;  // static curves only (§10.4 item 16)
    double excursionSec = 0.0;
    if (!getDouble("excursion_seconds", &excursionSec) || excursionSec <= 0.0) return false;
    const double E = std::llround(excursionSec * fs);
    if (E < 1.0) return false;
    const double rate = std::fabs(ctx.effectiveMax - 1.0) * fs / E;
    if (rate <= 0.0) return false;
    *outHz = rate;
    return true;
  }
  return false;  // varispeed: no periodic AM by construction (documented)
}

}  // namespace

MetricResult computeAmplitudeModulation(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "amplitude-modulation";
  r.unit = "Hz";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("none");
  r.method["envelope"] = json::Value("Hilbert analytic-signal magnitude (full length)");
  r.method["envelopeSpectrum"] =
      json::Value("r2c FFT of (envelope - mean), full-length periodic Hann");
  r.method["searchBandHz"] = json::Value("0.5 .. 500");
  r.method["bandIntegration"] = json::Value("peak/expected ±3 bins");
  r.tolerance = ToleranceInfo::none();

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  const double fs = static_cast<double>(ctx.sampleRate);
  double fExpected = 0.0;
  const bool hasExpected = expectedModulationHz(ctx, fs, &fExpected);
  if (hasExpected) {
    r.values["expectedModulationHz"] = json::Value(fExpected);
  } else {
    r.values["expectedModulationHz"] = json::Value(nullptr);
    if (ctx.engineId == "native.varispeed") {
      r.notes.push_back("varispeed declares no characteristic AM rate (modulation rates "
                        "scale with the curve — documented physics)");
    } else {
      r.notes.push_back("no declared characteristic rate derivable from the manifest "
                        "parameters for this engine/curve combination");
    }
  }

  bool anyMeasured = false;
  for (std::size_t c = 0; c < ctx.output.wav.channels.size(); ++c) {
    const std::vector<double> env =
        hilbertEnvelope(ctx.output.wav.channels[c]);
    // Constant/DC envelope => nothing to measure on this channel.
    double minE = 0.0, maxE = 0.0;
    if (!env.empty()) {
      minE = maxE = env[0];
      for (double v : env) {
        if (v < minE) minE = v;
        if (v > maxE) maxE = v;
      }
    }
    const std::string s = chSuffix(static_cast<int>(c));
    if (env.size() < 2 || (maxE - minE) == 0.0) {
      r.values["envelopePeakHz" + s] = json::Value(nullptr);
      r.values["relativeEnergyAtPeak" + s] = json::Value(nullptr);
      if (hasExpected) r.values["relativeEnergyAtExpected" + s] = json::Value(nullptr);
      r.notes.push_back("channel " + std::to_string(c) + ": constant envelope (silent or "
                        "DC) — modulation undefined");
      continue;
    }
    const EnvelopeSpectrum spec = envelopeSpectrum(env, fs);
    // Search band [0.5, 500] Hz over bins k >= 1.
    int64_t bestK = -1;
    double bestMag = -1.0;
    const int64_t half = static_cast<int64_t>(spec.mag.size()) - 1;
    for (int64_t k = 1; k <= half; ++k) {
      const double f = static_cast<double>(k) * spec.binHz;
      if (f < 0.5 || f > 500.0) continue;
      if (spec.mag[static_cast<std::size_t>(k)] > bestMag) {
        bestMag = spec.mag[static_cast<std::size_t>(k)];
        bestK = k;
      }
    }
    double total = 0.0;
    for (int64_t k = 1; k <= half; ++k) {
      const double m = spec.mag[static_cast<std::size_t>(k)];
      total += m * m;
    }
    if (bestK < 0 || total == 0.0) {
      r.values["envelopePeakHz" + s] = json::Value(nullptr);
      r.values["relativeEnergyAtPeak" + s] = json::Value(nullptr);
      if (hasExpected) r.values["relativeEnergyAtExpected" + s] = json::Value(nullptr);
      continue;
    }
    const double peakHz = static_cast<double>(bestK) * spec.binHz;
    const double ePeak = energyAround(spec, peakHz);
    r.values["envelopePeakHz" + s] = json::Value(peakHz);
    r.values["relativeEnergyAtPeak" + s] = json::Value(ePeak / total);
    if (hasExpected) {
      const double eExp = energyAround(spec, fExpected);
      r.values["relativeEnergyAtExpected" + s] = json::Value(eExp / total);
    }
    anyMeasured = true;
  }
  r.hasSampleCount = true;
  r.sampleCount = ctx.actualOutputFrames;
  r.notes.push_back("AM at grain/crossfade/hop rates is intentional engine behaviour — "
                    "measured, never suppressed (§10.1)");
  r.status = anyMeasured ? Status::Ok : Status::NotApplicable;
  if (!anyMeasured) {
    r.notes.push_back("no channel produced a measurable envelope modulation");
  }
  return r;
}

}  // namespace pitchlab::analysis
