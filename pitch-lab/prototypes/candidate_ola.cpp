// Pitch Lab — Task 28 Phase A candidate: OLA + resampling (see
// candidate_ola.h for the frozen model). Implementation notes:
//   * stretch_ / wsum_ hold the OVERLAP-ADD accumulator; positions below
//     finalFrontier_ have been normalised IN PLACE (accum[p]/wsum[p]);
//     positions >= finalFrontier_ are still raw accumulations.
//   * the resampler (interpolateAtAbs) reads the same buffers: it only
//     ever reads at q + taps strictly below the final frontier, so it
//     always sees normalised samples.
//   * all state is allocated in prepare(); process()/finish() never
//     allocate (audited by the RT probe).

#include "prototypes/candidate_ola.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/errors.h"
#include "core/resampler.h"

namespace pitchlab::proto {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<double> makeWindow(int n, const std::string& shape) {
  std::vector<double> w(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double u = static_cast<double>(i) / static_cast<double>(n);
    double v = 0.0;
    if (shape == "hann") {
      v = 0.5 * (1.0 - std::cos(2.0 * kPi * u));  // periodic
    } else if (shape == "hamming") {
      v = 0.54 - 0.46 * std::cos(2.0 * kPi * u);  // periodic
    } else if (shape == "bartlett") {
      v = 1.0 - std::abs(2.0 * u - 1.0);
    } else {  // rect
      v = 1.0;
    }
    w[static_cast<std::size_t>(i)] = v;
  }
  return w;
}

}  // namespace

void OlaPrototype::configure(const ProtoParams& params) {
  for (const auto& [key, value] : params) {
    if (key == "window_frames") {
      if (const auto* d = std::get_if<double>(&value)) {
        if (!std::isfinite(*d) || *d != std::floor(*d) || *d < 64.0 ||
            *d > 16384.0) {
          throw ConfigError("", "window_frames",
                            "must be an integer in [64, 16384]");
        }
        windowFrames_ = static_cast<int>(*d);
      } else {
        throw ConfigError("", "window_frames", "must be a number");
      }
    } else if (key == "overlap") {
      if (const auto* d = std::get_if<double>(&value)) {
        if (*d != 2.0 && *d != 4.0 && *d != 8.0) {
          throw ConfigError("", "overlap", "must be one of {2, 4, 8}");
        }
        overlap_ = static_cast<int>(*d);
      } else {
        throw ConfigError("", "overlap", "must be a number");
      }
    } else if (key == "window_shape") {
      if (const auto* s = std::get_if<std::string>(&value)) {
        if (*s != "hann" && *s != "hamming" && *s != "bartlett" &&
            *s != "rect") {
          throw ConfigError("", "window_shape",
                            "must be one of hann|hamming|bartlett|rect");
        }
        shape_ = *s;
      } else {
        throw ConfigError("", "window_shape", "must be a string");
      }
    } else {
      throw ConfigError("", key, "unknown parameter for proto.ola");
    }
  }
  if (windowFrames_ / overlap_ < 8) {
    throw ConfigError("", "overlap",
                      "synthesis hop Hs = N/overlap must be >= 8 frames");
  }
}

void OlaPrototype::prepare(double fs, int channels, int maxBlockFrames,
                           FrameCount totalInputFrames,
                           const double* ratioCurve) {
  if (!(fs > 0.0) || channels < 1 || channels > 2 || maxBlockFrames < 1 ||
      totalInputFrames < 1 || ratioCurve == nullptr) {
    throw ConfigError("", "prepare", "invalid job geometry");
  }
  fs_ = fs;
  channels_ = channels;
  maxBlock_ = maxBlockFrames;
  nIn_ = totalInputFrames;
  curve_ = ratioCurve;
  hs_ = windowFrames_ / overlap_;
  window_ = makeWindow(windowFrames_, shape_);

  // Live stretch window bound: unfinalised accum tail (N - Hs + Hs = N
  // beyond the last placed centre) + final region not yet resampled
  // (bounded by the final-frontier lead, N/2 + Hs) + resampler taps +
  // margin. Back-pressure caps growth beyond this.
  const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
  stretchCapacity_ = windowFrames_ * 2 + hs_ * 2 + 2 * K + 2 * maxBlock_ + 256;

  stretch_.assign(static_cast<std::size_t>(channels_),
                  std::vector<double>(static_cast<std::size_t>(stretchCapacity_), 0.0));
  wsum_.assign(static_cast<std::size_t>(stretchCapacity_), 0.0);
  inBuf_.assign(static_cast<std::size_t>(channels_),
                std::vector<double>(
                    static_cast<std::size_t>(windowFrames_ + inputBackMargin_ +
                                             2 * maxBlock_ + 256),
                    0.0));
  inBase_ = 0;
  inAvail_ = 0;
  stretchBase_ = 0;
  finalFrontier_ = 0;
  aNext_ = static_cast<double>(windowFrames_) / 2.0;  // grain 0 covers [0, N)
  sNext_ = static_cast<double>(windowFrames_) / 2.0;
  grainsPlaced_ = 0;
  q_ = 0.0;
  tOut_ = 0;
  finishing_ = false;
  stretchEndD_ = 0.0;
  maxLiveStretch_ = 0;

  // COLA ripple diagnostic: steady-state window-sum pattern over one Hs
  // period (peak deviation from the mean, dB).
  {
    double mean = 0.0;
    std::vector<double> pat(static_cast<std::size_t>(hs_), 0.0);
    for (int i = 0; i < hs_; ++i) {
      double s = 0.0;
      for (int m = -4; m <= 4; ++m) {
        const int idx = i - m * hs_;
        if (idx >= 0 && idx < windowFrames_) {
          s += window_[static_cast<std::size_t>(idx)];
        }
      }
      pat[static_cast<std::size_t>(i)] = s;
      mean += s;
    }
    mean /= static_cast<double>(hs_);
    double dev = 0.0;
    for (double v : pat) dev = std::max(dev, std::abs(v - mean));
    // Perfect COLA gives dev == 0 -> log10(0) would be -inf; the record
    // boundary wants a finite "below measurement" floor instead.
    wsumRippleDb_ =
        (mean > 1.0e-12 && dev > 1.0e-15)
            ? 20.0 * std::log10(dev / mean)
            : (mean > 1.0e-12 ? -300.0 : 0.0);
  }
}

OlaPrototype::Latency OlaPrototype::latency() const {
  const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
  Latency lat;
  lat.inputLookahead = windowFrames_ / 2 + hs_ + 2 * K + 64;
  // Worst-case flush: window edges + hop + taps at the curve's minimum
  // ratio (the flush drains the tail stretch content through the
  // slowest resample rate).
  double minRatio = 1.0;
  for (FrameCount i = 0; i < nIn_; ++i) {
    minRatio = std::min(minRatio, curve_[static_cast<std::size_t>(i)]);
  }
  if (!(minRatio > 0.0)) minRatio = 0.5;
  lat.outputFlush = static_cast<FrameCount>(
      static_cast<double>(windowFrames_ + hs_ + 2 * K + 256) / minRatio) + 64;
  return lat;
}

double OlaPrototype::grainAnalysisAdjustment(double) { return 0.0; }

double OlaPrototype::builtEstimateAt(FrameCount stretchPos) const {
  const FrameCount rel = stretchPos - stretchBase_;
  if (rel < 0 || rel >= stretchCapacity_) return 0.0;
  return stretch_[0][static_cast<std::size_t>(rel)];
}

double OlaPrototype::ratioAtInput(double pos) const {
  if (curve_ == nullptr || nIn_ <= 0) return 1.0;
  double p = std::floor(pos);
  if (p < 0.0) p = 0.0;
  if (p > static_cast<double>(nIn_ - 1)) p = static_cast<double>(nIn_ - 1);
  return curve_[static_cast<std::size_t>(p)];
}

double OlaPrototype::ratioAtOutput(FrameCount t) const {
  if (curve_ == nullptr || nIn_ <= 0) return 1.0;
  double p = static_cast<double>(t);
  if (p > static_cast<double>(nIn_ - 1)) p = static_cast<double>(nIn_ - 1);
  return curve_[static_cast<std::size_t>(p)];
}

double OlaPrototype::readInput(int channel, FrameCount pos) const {
  const FrameCount rel = pos - inBase_;
  if (rel < 0) return 0.0;
  if (rel >= static_cast<FrameCount>(inBuf_[0].size())) return 0.0;
  return inBuf_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(rel)];
}

void OlaPrototype::compactInput() {
  const FrameCount keepFrom = std::max<FrameCount>(
      0, static_cast<FrameCount>(aNext_) - windowFrames_ / 2 - inputBackMargin_ - 64);
  if (inBase_ >= keepFrom) return;
  const FrameCount drop = keepFrom - inBase_;
  const FrameCount size = inAvail_ - inBase_;
  if (drop >= size) {
    for (auto& b : inBuf_) std::fill(b.begin(), b.end(), 0.0);
    inBase_ = keepFrom;
    return;
  }
  for (auto& b : inBuf_) {
    std::memmove(b.data(), b.data() + static_cast<std::size_t>(drop),
                 static_cast<std::size_t>(size - drop) * sizeof(double));
    std::fill(b.begin() + static_cast<std::size_t>(size - drop), b.end(), 0.0);
  }
  inBase_ = keepFrom;
}

void OlaPrototype::compactStretch() {
  const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
  const FrameCount keepFrom = std::max<FrameCount>(
      0, static_cast<FrameCount>(q_) - K - 128);
  if (stretchBase_ >= keepFrom) return;
  const FrameCount drop = keepFrom - stretchBase_;
  const FrameCount live = static_cast<FrameCount>(
      std::max<double>(0.0, std::max(static_cast<double>(finalFrontier_),
                                      sNext_ + windowFrames_ / 2.0)) -
      static_cast<double>(stretchBase_));
  const FrameCount keep = std::min(live, stretchCapacity_);
  if (drop >= keep) {
    for (auto& b : stretch_) std::fill(b.begin(), b.end(), 0.0);
    std::fill(wsum_.begin(), wsum_.end(), 0.0);
    stretchBase_ = keepFrom;
    return;
  }
  for (auto& b : stretch_) {
    std::memmove(b.data(), b.data() + static_cast<std::size_t>(drop),
                 static_cast<std::size_t>(keep - drop) * sizeof(double));
    std::fill(b.begin() + static_cast<std::size_t>(keep - drop), b.end(), 0.0);
  }
  std::memmove(wsum_.data(), wsum_.data() + static_cast<std::size_t>(drop),
               static_cast<std::size_t>(keep - drop) * sizeof(double));
  std::fill(wsum_.begin() + static_cast<std::size_t>(keep - drop), wsum_.end(), 0.0);
  stretchBase_ = keepFrom;
}

bool OlaPrototype::placeGrainsUpTo(FrameCount inputAvailableEnd) {
  // Place grains while (a) the whole analysis window [a-N/2, a+N/2) is
  // within the available input, and (b) the stretch live window fits.
  const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
  bool placedAny = false;
  for (;;) {
    const double half = static_cast<double>(windowFrames_) / 2.0;
    const double needEnd = aNext_ + half;
    const FrameCount inputLimit =
        finishing_ ? std::numeric_limits<FrameCount>::max() : inputAvailableEnd;
    if (finishing_) {
      // In finish(): grains whose window still touches real input.
      if (aNext_ - half >= static_cast<double>(nIn_)) break;
    } else if (needEnd > static_cast<double>(inputLimit)) {
      break;
    }

    // Stretch live-extent guard (back-pressure: stop placing).
    const FrameCount tailEnd =
        static_cast<FrameCount>(sNext_ + half) + 2 * (K + 8);
    const FrameCount liveLo = static_cast<FrameCount>(
        std::max(0.0, q_ - static_cast<double>(K) - 128.0));
    if (tailEnd - liveLo > stretchCapacity_ - 8) break;

    // WSOLA hook: the analysis-centre adjustment for this grain.
    const double delta = grainAnalysisAdjustment(aNext_);
    const double a = aNext_ + delta;
    const double s = sNext_;

    // Add the windowed grain into the accumulators.
    const double rho = ratioAtInput(a);
    compactStretch();
    const double lo = s - half;
    const int n = windowFrames_;
    for (int i = 0; i < n; ++i) {
      const double sp = lo + static_cast<double>(i);
      const FrameCount rel = static_cast<FrameCount>(sp) - stretchBase_;
      if (rel < 0 || rel >= stretchCapacity_) continue;  // guarded above
      const double ap = a - half + static_cast<double>(i);
      const double wv = window_[static_cast<std::size_t>(i)];
      if (wv == 0.0) continue;
      for (int c = 0; c < channels_; ++c) {
        stretch_[static_cast<std::size_t>(c)][static_cast<std::size_t>(rel)] +=
            wv * readInput(c, static_cast<FrameCount>(std::floor(ap)));
      }
      wsum_[static_cast<std::size_t>(rel)] += wv;
    }
    ++grainsPlaced_;
    placedAny = true;

    // Advance: synthesis by the FIXED hop; analysis by the stretch hop
    // (re-anchored at the chosen position for WSOLA drift semantics).
    aNext_ = a + static_cast<double>(hs_) / rho;
    sNext_ = s + static_cast<double>(hs_);

    // Normalise everything below the new final frontier in place: the
    // next grain (centre sNext_) will write exactly from sNext_ - N/2,
    // so positions < sNext_ - N/2 are final.
    const double frontierD = std::max(0.0, sNext_ - half);
    const FrameCount frontier = static_cast<FrameCount>(frontierD);
    while (finalFrontier_ < frontier) {
      const FrameCount rel = finalFrontier_ - stretchBase_;
      if (rel >= 0 && rel < stretchCapacity_) {
        const double wv = wsum_[static_cast<std::size_t>(rel)];
        if (wv > 1.0e-12) {
          const double inv = 1.0 / wv;
          for (int c = 0; c < channels_; ++c) {
            stretch_[static_cast<std::size_t>(c)][static_cast<std::size_t>(rel)] *=
                inv;
          }
        } else {
          for (int c = 0; c < channels_; ++c) {
            stretch_[static_cast<std::size_t>(c)][static_cast<std::size_t>(rel)] = 0.0;
          }
        }
      }
      ++finalFrontier_;
    }
    maxLiveStretch_ = std::max<int64_t>(
        maxLiveStretch_, static_cast<int64_t>(tailEnd - liveLo));
  }
  return placedAny;
}

void OlaPrototype::emitFinalFrames(AudioBlockOut& out, int outCapacity,
                                   FrameCount& produced) {
  const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
  const double drainLimit =
      finishing_ ? stretchEndD_ : std::numeric_limits<double>::max();
  while (produced < static_cast<FrameCount>(outCapacity)) {
    // +1: llround(q) + K may round up to q + K + 0.5; keep every tap
    // strictly below the final frontier (raw accumulations live above).
    const double need = q_ + static_cast<double>(K) + 1.0;
    if (need > static_cast<double>(finalFrontier_)) break;   // not final yet
    if (finishing_ && q_ >= drainLimit) break;               // drained
    const double rho = ratioAtOutput(tOut_);
    const double cutoff = antiAliasCutoff(rho);
    const FrameCount baseIdx = stretchBase_;
    for (int c = 0; c < channels_; ++c) {
      out.channels[c][static_cast<std::size_t>(produced)] =
          interpolateAtAbs(stretch_[static_cast<std::size_t>(c)].data(),
                           static_cast<FrameCount>(stretch_[0].size()), baseIdx,
                           q_, cutoff, ResampleQuality::Standard);
    }
    q_ += rho;
    ++tOut_;
    ++produced;
  }
}

ProcessOutcome OlaPrototype::process(const AudioBlockView& in, int inFrames,
                                     AudioBlockOut& out, int outCapacity,
                                     FrameCount inputFrameIndex) {
  if (inFrames < 0 || inFrames > maxBlock_ ||
      inputFrameIndex != inAvail_) {
    throw ConfigError("", "process", "block contract violation");
  }
  // Absorb the input into the sliding input window.
  compactInput();
  {
    const FrameCount rel = inAvail_ - inBase_;
    const auto cap = static_cast<FrameCount>(inBuf_[0].size());
    if (rel + inFrames > cap) {
      // compaction should prevent this; if not, it is an engine bug.
      throw ConfigError("", "process", "input window overflow");
    }
    for (int c = 0; c < channels_; ++c) {
      std::memcpy(inBuf_[static_cast<std::size_t>(c)].data() +
                      static_cast<std::size_t>(rel),
                  in.channels[static_cast<std::size_t>(c)],
                  static_cast<std::size_t>(inFrames) * sizeof(double));
    }
  }
  inAvail_ += inFrames;

  placeGrainsUpTo(inAvail_);

  FrameCount produced = 0;
  if (outCapacity > 0 && out.channels != nullptr) {
    emitFinalFrames(out, outCapacity, produced);
  }
  ProcessOutcome rep;
  rep.inputFramesConsumed = inFrames;
  rep.outputFramesProduced = produced;
  return rep;
}

ProcessOutcome OlaPrototype::finish(AudioBlockOut& out, int outCapacity) {
  if (finishing_) {
    throw ConfigError("", "finish", "finish called twice");
  }
  finishing_ = true;
  stretchEndD_ = std::numeric_limits<double>::max();  // until grains exhaust
  const double half = static_cast<double>(windowFrames_) / 2.0;

  // Interleave grain placement with emission: the stretch live window is
  // bounded, so at small caller capacities the remaining grains can only
  // be placed as the resampler drains (back-pressure by design).
  FrameCount produced = 0;
  for (int64_t guard = 0; guard < (1 << 24); ++guard) {
    const bool placedAny = placeGrainsUpTo(0);
    const bool grainsExhausted = (aNext_ - half >= static_cast<double>(nIn_));
    if (grainsExhausted) {
      stretchEndD_ = sNext_ + half;  // the full stretch extent: drain target
    }
    const FrameCount before = produced;
    if (outCapacity > 0 && out.channels != nullptr) {
      emitFinalFrames(out, outCapacity, produced);
    }
    if (produced == before && (!placedAny || grainsExhausted)) {
      break;  // fully drained, or stalled on caller capacity
    }
  }

  ProcessOutcome rep;
  rep.inputFramesConsumed = 0;
  rep.outputFramesProduced = produced;
  return rep;
}

json::Value OlaPrototype::diagnostics() const {
  json::Object o;
  o["grains_placed"] = json::Value(static_cast<int64_t>(grainsPlaced_));
  o["cola_ripple_db"] = json::Value(wsumRippleDb_);
  o["max_live_stretch_frames"] =
      json::Value(static_cast<int64_t>(maxLiveStretch_));
  o["window_frames"] = json::Value(static_cast<int64_t>(windowFrames_));
  o["hop"] = json::Value(static_cast<int64_t>(hs_));
  o["window_shape"] = json::Value(shape_);
  o["overlap"] = json::Value(static_cast<int64_t>(overlap_));
  return json::Value(std::move(o));
}

}  // namespace pitchlab::proto
