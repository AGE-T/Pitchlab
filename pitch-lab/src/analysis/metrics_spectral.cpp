// Pitch Lab — spectral/band metrics (implementation specification §10.4
// items 13-15, frozen cycle 5 BEFORE coding): hf-energy (band energy
// relative to job Nyquist + declared content band), aliasing-indicator
// (energy above expected max / band-occupancy inversion, §10.1/§I.2),
// spectral-error (log-spectral distance vs the varispeed reference with the
// frozen class-dependent alignment).

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include "analysis/analysis_context.h"
#include "analysis/analysis_types.h"
#include "analysis/spectral.h"

namespace pitchlab::analysis {
namespace {

[[nodiscard]] std::string chSuffix(int c) { return "Ch" + std::to_string(c); }

/// Accumulated per-channel band energies from the STFT power spectra:
/// band edges in Hz; bin k counts iff lo <= k*fs/Nw <= hi (inclusive).
struct BandEnergies {
  std::vector<double> total;     // per channel: sum of all bin powers
  std::vector<double> inBand;    // per channel
  std::vector<double> above;     // per channel (band above the content band)
  std::vector<double> below;     // per channel (band below the content band)
  std::vector<double> above04;   // per channel (above 0.45*Nyquist)
  int64_t frameCount = 0;
};

[[nodiscard]] BandEnergies accumulateBands(const AnalysisContext& ctx, double lo, double hi,
                                           double guard) {
  BandEnergies b;
  const int C = static_cast<int>(ctx.output.wav.channels.size());
  b.total.assign(static_cast<std::size_t>(C), 0.0);
  b.inBand.assign(static_cast<std::size_t>(C), 0.0);
  b.above.assign(static_cast<std::size_t>(C), 0.0);
  b.below.assign(static_cast<std::size_t>(C), 0.0);
  b.above04.assign(static_cast<std::size_t>(C), 0.0);
  const double fs = static_cast<double>(ctx.sampleRate);
  const double ny = fs / 2.0;
  for (int c = 0; c < C; ++c) {
    const StftResult stft = computeStft(ctx.output.wav.channels[static_cast<std::size_t>(c)], fs);
    b.frameCount = std::max(b.frameCount, stft.frameCount);
    const double binHz = fs / static_cast<double>(stft.windowFrames);
    for (const StftFrame& f : stft.frames) {
      for (int64_t k = 0; k <= stft.windowFrames / 2; ++k) {
        const double p = f.power[static_cast<std::size_t>(k)];
        const double freq = static_cast<double>(k) * binHz;
        b.total[static_cast<std::size_t>(c)] += p;
        if (freq >= lo && freq <= hi) b.inBand[static_cast<std::size_t>(c)] += p;
        if (freq > hi) b.above[static_cast<std::size_t>(c)] += p;
        if (freq < lo) b.below[static_cast<std::size_t>(c)] += p;
        if (freq > 0.45 * ny) b.above04[static_cast<std::size_t>(c)] += p;
      }
    }
  }
  (void)guard;
  return b;
}

}  // namespace

MetricResult computeHfEnergy(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "hf-energy";
  r.unit = "ratio";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("none");
  r.method["stft"] = json::Value("analysis STFT (per-rate window, periodic Hann, hop Nw/4)");
  r.method["bandGuardHz"] = json::Value("4 bin spacings (Hann main lobe + sidelobes)");
  r.method["above04Ny"] =
      json::Value("context: the v0.1 engines' declared-bandwidth class — NOT a threshold");

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  const double fs = static_cast<double>(ctx.sampleRate);
  const double ny = fs / 2.0;
  const int64_t nw = analysisStftFrames(fs);
  const double guard = 4.0 * fs / static_cast<double>(nw);

  double lo = 0.0, hi = ny;
  if (ctx.asset.hasBand) {
    lo = std::max(0.0, ctx.asset.bandLo - guard);
    hi = std::min(ny, ctx.asset.bandHi + guard);
  } else {
    r.notes.push_back("asset declares no bandContentHz: band = [0, Nyquist]");
  }
  const BandEnergies b = accumulateBands(ctx, lo, hi, guard);

  json::Array edges;
  edges.push_back(json::Value(lo));
  edges.push_back(json::Value(hi));
  r.values["bandEdgesUsedHz"] = json::Value(std::move(edges));
  r.values["nyquistHz"] = json::Value(ny);
  r.values["stftWindowFrames"] = json::Value(nw);
  r.hasFrameCount = true;
  r.frameCount = b.frameCount;

  bool allSilent = true;
  for (std::size_t c = 0; c < b.total.size(); ++c) {
    if (b.total[c] > 0.0) allSilent = false;
  }
  if (allSilent) {
    r.status = Status::NotApplicable;
    r.notes.push_back("silent output: band fractions undefined (never zero-substituted)");
    return r;
  }
  for (std::size_t c = 0; c < b.total.size(); ++c) {
    const std::string s = chSuffix(static_cast<int>(c));
    if (b.total[c] > 0.0) {
      r.values["relativeEnergyInContentBand" + s] =
          json::Value(b.inBand[c] / b.total[c]);
      r.values["relativeEnergyAboveContentBand" + s] =
          json::Value(b.above[c] / b.total[c]);
      r.values["relativeEnergyBelowContentBand" + s] =
          json::Value(b.below[c] / b.total[c]);
      r.values["relativeEnergyAbove04Ny" + s] = json::Value(b.above04[c] / b.total[c]);
      r.values["totalSpectralEnergy" + s] = json::Value(b.total[c]);
    } else {
      r.values["relativeEnergyInContentBand" + s] = json::Value(nullptr);
      r.values["relativeEnergyAboveContentBand" + s] = json::Value(nullptr);
      r.values["relativeEnergyBelowContentBand" + s] = json::Value(nullptr);
      r.values["relativeEnergyAbove04Ny" + s] = json::Value(nullptr);
      r.values["totalSpectralEnergy" + s] = json::Value(0.0);
      r.notes.push_back("channel " + std::to_string(c) + " is silent: fractions null");
    }
  }
  r.tolerance = ToleranceInfo::exact();
  r.notes.push_back("band edges exact; band-energy interpretation provisional (§10.1)");
  r.status = Status::Ok;
  return r;
}

MetricResult computeAliasingIndicator(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "aliasing-indicator";
  r.unit = "ratio";
  r.alignment["axis"] = json::Value("output-timeline");
  r.alignment["method"] = json::Value("none");
  r.method["expectedMax"] = json::Value("bandContentHz[1] * curve.effective.max (manifest)");
  r.method["guardHz"] = json::Value("4 bin spacings");
  r.method["interpretation"] =
      json::Value("aliasing-indicator | band-occupancy (expected max beyond Nyquist)");

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  if (!ctx.asset.hasBand) {
    r.status = Status::NotApplicable;
    r.notes.push_back("asset declares no bandContentHz: the expected-max computation "
                      "needs the declared band (§10.4 item 14)");
    return r;
  }
  const double fs = static_cast<double>(ctx.sampleRate);
  const double ny = fs / 2.0;
  const int64_t nw = analysisStftFrames(fs);
  const double guard = 4.0 * fs / static_cast<double>(nw);
  const double fExpectedMax = ctx.asset.bandHi * ctx.effectiveMax;

  if (fExpectedMax + guard <= ny) {
    // Aliasing-indicator mode: energy above the expected maximum.
    const BandEnergies b = accumulateBands(ctx, -1.0, fExpectedMax + guard, guard);
    json::Array edges;
    edges.push_back(json::Value(fExpectedMax + guard));
    edges.push_back(json::Value(ny));
    r.values["interpretation"] = json::Value("aliasing-indicator");
    r.values["expectedMaxHz"] = json::Value(fExpectedMax);
    r.values["bandEdgesUsedHz"] = json::Value(std::move(edges));
    r.values["stftWindowFrames"] = json::Value(nw);
    r.hasFrameCount = true;
    r.frameCount = b.frameCount;
    bool allSilent = true;
    for (std::size_t c = 0; c < b.total.size(); ++c) {
      if (b.total[c] > 0.0) allSilent = false;
    }
    if (allSilent) {
      r.status = Status::NotApplicable;
      r.notes.push_back("silent output: fractions undefined");
      return r;
    }
    for (std::size_t c = 0; c < b.total.size(); ++c) {
      const std::string s = chSuffix(static_cast<int>(c));
      if (b.total[c] > 0.0) {
        // above band = total - inBand (the [−1, fExpectedMax+guard] band
        // covers everything below the edge; the residual is above it).
        const double above = b.total[c] - b.inBand[c];
        r.values["relativeEnergyAboveExpectedMax" + s] = json::Value(above / b.total[c]);
      } else {
        r.values["relativeEnergyAboveExpectedMax" + s] = json::Value(nullptr);
        r.notes.push_back("channel " + std::to_string(c) + " silent: fraction null");
      }
    }
  } else {
    // Band-occupancy mode (the §10.1/§I.2 documented inversion).
    const double lo = std::max(0.0, ctx.asset.bandLo * ctx.effectiveMin - guard);
    const BandEnergies b = accumulateBands(ctx, lo, ny, guard);
    json::Array edges;
    edges.push_back(json::Value(lo));
    edges.push_back(json::Value(ny));
    r.values["interpretation"] = json::Value("band-occupancy");
    r.values["expectedMaxHz"] = json::Value(fExpectedMax);
    r.values["bandEdgesUsedHz"] = json::Value(std::move(edges));
    r.values["stftWindowFrames"] = json::Value(nw);
    r.hasFrameCount = true;
    r.frameCount = b.frameCount;
    bool allSilent = true;
    for (std::size_t c = 0; c < b.total.size(); ++c) {
      if (b.total[c] > 0.0) allSilent = false;
    }
    if (allSilent) {
      r.status = Status::NotApplicable;
      r.notes.push_back("silent output: fractions undefined");
      return r;
    }
    for (std::size_t c = 0; c < b.total.size(); ++c) {
      const std::string s = chSuffix(static_cast<int>(c));
      if (b.total[c] > 0.0) {
        r.values["relativeEnergyInOccupiedBand" + s] = json::Value(b.inBand[c] / b.total[c]);
      } else {
        r.values["relativeEnergyInOccupiedBand" + s] = json::Value(nullptr);
      }
    }
    r.notes.push_back("expected maximum exceeds Nyquist: reported as band occupancy "
                      "(content legitimately exits the band — not an aliasing failure)");
  }
  r.tolerance = ToleranceInfo::none();
  r.status = Status::Ok;
  return r;
}

MetricResult computeSpectralError(const AnalysisContext& ctx) {
  MetricResult r;
  r.metricId = "spectral-error";
  r.unit = "dB";
  r.alignment["axis"] = json::Value("input-timeline");
  r.alignment["method"] = json::Value("class-dependent: engine master vs warped "
                                      "reference (Preserving) / output-timeline "
                                      "(RateFollowing)");
  r.method["distance"] = json::Value("log-spectral distance (own STFT)");
  r.method["logFloor"] = json::Value(1e-9);
  r.method["window"] = json::Value("analysis STFT (per-rate window, hop Nw/4)");
  r.tolerance = ToleranceInfo::none();  // provisional class; OD-9 owns values

  if (!ctx.output.loaded || ctx.output.wav.channels.empty()) {
    r.status = Status::AnalysisError;
    r.error = "output master not loaded";
    return r;
  }
  if (!ctx.hasReference || !ctx.reference.loaded || ctx.reference.wav.channels.empty()) {
    r.status = Status::ReferenceUnavailable;
    r.notes.push_back("no usable native.varispeed reference render in this experiment "
                      "run (§10.4 item 7) — never re-rendered");
    return r;
  }
  const double fs = static_cast<double>(ctx.sampleRate);
  const int C = std::min<int>(ctx.output.wav.channels.size(), ctx.reference.wav.channels.size());
  if (C <= 0) {
    r.status = Status::AnalysisError;
    r.error = "reference channel count is zero";
    return r;
  }

  // Alignment (§10.4 item 15):
  //   engine Preserving: engine signal = master; reference (RateFollowing)
  //   warped to the input timeline. engine RateFollowing: both on the OUTPUT
  //   timeline, frames [0, min(len)).
  std::vector<std::vector<double>> sigOut(C);   // engine signal, analysis grid
  std::vector<std::vector<double>> sigRef(C);   // reference signal, analysis grid
  if (ctx.isRateFollowing()) {
    for (int c = 0; c < C; ++c) {
      const std::vector<double>& e = ctx.output.wav.channels[static_cast<std::size_t>(c)];
      const std::vector<double>& v = ctx.reference.wav.channels[static_cast<std::size_t>(c)];
      const std::size_t n = std::min(e.size(), v.size());
      sigOut[static_cast<std::size_t>(c)].assign(e.begin(), e.begin() + static_cast<std::ptrdiff_t>(n));
      sigRef[static_cast<std::size_t>(c)].assign(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n));
    }
    if (ctx.output.wav.meta.frames != ctx.reference.wav.meta.frames) {
      r.notes.push_back("engine and reference lengths differ (" +
                        std::to_string(ctx.output.wav.meta.frames) + " vs " +
                        std::to_string(ctx.reference.wav.meta.frames) +
                        "): compared over the common prefix (output-timeline alignment)");
    }
  } else {
    for (int c = 0; c < C; ++c) {
      const std::vector<double>& e = ctx.output.wav.channels[static_cast<std::size_t>(c)];
      const std::vector<double>& v = ctx.reference.wav.channels[static_cast<std::size_t>(c)];
      // Engine (Preserving) is already input-timeline aligned; the reference
      // (RateFollowing) is warped onto the input grid by its own emission map.
      sigOut[static_cast<std::size_t>(c)] = e;
      sigRef[static_cast<std::size_t>(c)] =
          ctx.referenceEmissionMap
              ? ctx.referenceEmissionMap->warpToInputTimeline(v, ctx.inputFrames)
              : v;
    }
    r.notes.push_back("Preserving engine vs RateFollowing reference: reference warped "
                      "to the input timeline via the emission map (§H preamble)");
  }

  // The engine may have MORE channels than the reference or vice versa: the
  // comparison covers the common channel count (noted).
  if (ctx.output.wav.channels.size() != ctx.reference.wav.channels.size()) {
    r.notes.push_back("channel-count difference: compared over the common channels");
  }

  const double kEps = 1e-9;
  // Per-channel frame LSD values, then aggregate over frames+channels? NO —
  // per-channel aggregates stay separate (§O.3: no collapsing across
  // dimensions); a per-channel mean/median/max is the statistic of THIS
  // metric's dimension. Report per channel.
  for (int c = 0; c < C; ++c) {
    const StftResult a = computeStft(sigOut[static_cast<std::size_t>(c)], fs);
    const StftResult bb = computeStft(sigRef[static_cast<std::size_t>(c)], fs);
    const int64_t frames = std::min(a.frameCount, bb.frameCount);
    std::vector<double> frameLsd;
    frameLsd.reserve(static_cast<std::size_t>(frames));
    for (int64_t nF = 0; nF < frames; ++nF) {
      const StftFrame& fa = a.frames[static_cast<std::size_t>(nF)];
      const StftFrame& fb = bb.frames[static_cast<std::size_t>(nF)];
      const int64_t bins = std::min<int64_t>(fa.power.size(), fb.power.size());
      double sum = 0.0;
      for (int64_t k = 0; k < bins; ++k) {
        const double magA = std::sqrt(fa.power[static_cast<std::size_t>(k)]);
        const double magB = std::sqrt(fb.power[static_cast<std::size_t>(k)]);
        const double d = 20.0 * std::log10(magA + kEps) - 20.0 * std::log10(magB + kEps);
        sum += d * d;
      }
      frameLsd.push_back(std::sqrt(sum / static_cast<double>(bins)));
    }
    const std::string s = chSuffix(c);
    if (frameLsd.empty()) {
      r.values["meanLsdDb" + s] = json::Value(nullptr);
      r.values["medianLsdDb" + s] = json::Value(nullptr);
      r.values["maxLsdDb" + s] = json::Value(nullptr);
      r.notes.push_back("channel " + std::to_string(c) + ": no complete STFT frames "
                        "(signal shorter than the analysis window)");
      continue;
    }
    double mean = 0.0;
    for (double v : frameLsd) mean += v;
    mean /= static_cast<double>(frameLsd.size());
    std::vector<double> sorted = frameLsd;
    std::sort(sorted.begin(), sorted.end());
    const double median =
        sorted.size() % 2 == 1
            ? sorted[sorted.size() / 2]
            : 0.5 * (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]);
    double maxV = 0.0;
    for (double v : frameLsd) {
      if (v > maxV) maxV = v;
    }
    r.values["meanLsdDb" + s] = json::Value(mean);
    r.values["medianLsdDb" + s] = json::Value(median);
    r.values["maxLsdDb" + s] = json::Value(maxV);
    r.values["frameCount" + s] = json::Value(static_cast<int64_t>(frameLsd.size()));
    if (a.frameCount != bb.frameCount) {
      r.notes.push_back("channel " + std::to_string(c) + ": engine/reference frame "
                        "coverage differs (" + std::to_string(a.frameCount) + " vs " +
                        std::to_string(bb.frameCount) + " STFT frames)");
    }
  }
  r.hasFrameCount = true;
  r.frameCount = static_cast<int64_t>(sigOut.empty() ? 0 : sigOut[0].size());
  r.notes.push_back("distance from reference behaviour (§10.3) — a measurement, not a "
                    "quality gate; tolerance class provisional (OD-9)");
  r.status = Status::Ok;
  return r;
}

}  // namespace pitchlab::analysis
