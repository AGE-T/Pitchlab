// Pitch Lab — Task 28 Phase C candidate: TD-PSOLA (see
// candidate_tdpsola.h for the frozen model).

#include "prototypes/candidate_tdpsola.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

#include "analysis/pitch_tracker.h"
#include "core/errors.h"
#include "core/resampler.h"

namespace pitchlab::proto {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<double> hannGrain(int n) {
  std::vector<double> w(static_cast<std::size_t>(n));
  // Periodic Hann (centred grains: symmetric around the grain centre is
  // fine for window-product normalisation).
  for (int i = 0; i < n; ++i) {
    w[static_cast<std::size_t>(i)] =
        0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) /
                              static_cast<double>(n)));
  }
  return w;
}
}  // namespace

void TdPsolaPrototype::configure(const ProtoParams& params) {
  for (const auto& [key, value] : params) {
    if (key == "mode") {
      if (const auto* s = std::get_if<std::string>(&value)) {
        if (*s != "pitch" && *s != "stretch") {
          throw ConfigError("", "mode", "must be 'pitch' or 'stretch'");
        }
        mode_ = *s;
      } else {
        throw ConfigError("", "mode", "must be a string");
      }
    } else if (key == "puv_hz") {
      if (const auto* d = std::get_if<double>(&value)) {
        if (!std::isfinite(*d) || *d < 50.0 || *d > 500.0) {
          throw ConfigError("", "puv_hz", "must be in [50, 500]");
        }
        puvHz_ = *d;
      } else {
        throw ConfigError("", "puv_hz", "must be a number");
      }
    } else {
      throw ConfigError("", key, "unknown parameter for proto.tdpsola");
    }
  }
}

void TdPsolaPrototype::analyzeSignal(
    const std::vector<std::vector<double>>& input) {
  marks_.clear();
  analysisMsPerSecond_ = 0.0;
  analysisFrames_ = 0;
  if (input.empty() || input[0].empty()) return;
  const int64_t nIn = static_cast<int64_t>(input[0].size());
  analysisFrames_ = nIn;
  const double t0 = static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count()) *
      1.0e-9;
  const auto track = analysis::trackPitch(input[0], fs_ > 0.0 ? fs_ : 48000.0,
                                          analysis::kTrackerFminHz,
                                          analysis::kTrackerFmaxHz);
  const double t1 = static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count()) *
      1.0e-9;
  if (nIn > 0) {
    analysisMsPerSecond_ = (t1 - t0) * 1000.0 * 48000.0 /
                           static_cast<double>(nIn) * (fs_ / 48000.0);
  }

  // Period/voicing lookup: nearest tracker frame center.
  const double fs = fs_ > 0.0 ? fs_ : 48000.0;
  const auto frameAt = [&](int64_t t) -> const analysis::PitchTrackFrame* {
    if (track.frames.empty()) return nullptr;
    // frames are ordered by center; binary search
    int64_t lo = 0, hi = static_cast<int64_t>(track.frames.size()) - 1;
    while (lo < hi) {
      const int64_t mid = lo + (hi - lo) / 2;
      if (track.frames[static_cast<std::size_t>(mid)].center < t) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo > 0) {
      const auto& a = track.frames[static_cast<std::size_t>(lo - 1)];
      const auto& b = track.frames[static_cast<std::size_t>(lo)];
      const int64_t da = std::abs(a.center - t);
      const int64_t db = std::abs(b.center - t);
      return da <= db ? &a : &b;
    }
    return &track.frames[static_cast<std::size_t>(lo)];
  };

  const double puv = fs / puvHz_;
  const double pLo = fs / analysis::kTrackerFmaxHz;
  const double pHi = fs / analysis::kTrackerFminHz;

  // Mark generation with zero-crossing epoch refinement.
  int64_t t = 0;
  while (t < nIn) {
    const analysis::PitchTrackFrame* fr = frameAt(t);
    bool voiced = fr != nullptr && fr->voiced && fr->f0Hz > 0.0;
    double P = puv;
    if (voiced) {
      P = std::clamp(fs / fr->f0Hz, pLo, pHi);
    }
    FrameCount mark = t;
    if (voiced) {
      // Nearest positive-going zero crossing within +-P/4.
      const int64_t span = std::max<int64_t>(4, static_cast<int64_t>(P / 4.0));
      const int64_t lo = std::max<int64_t>(0, t - span);
      const int64_t hi = std::min<int64_t>(nIn - 2, t + span);
      int64_t best = -1;
      int64_t bestDist = span + 1;
      for (int64_t i = lo + 1; i <= hi; ++i) {
        const double a = input[0][static_cast<std::size_t>(i - 1)];
        const double b = input[0][static_cast<std::size_t>(i)];
        if (a <= 0.0 && b > 0.0) {
          const int64_t dist = std::abs(i - t);
          if (dist < bestDist) {
            bestDist = dist;
            best = i;
          }
        }
      }
      if (best >= 0) mark = best;
    }
    marks_.push_back(Mark{mark, P, voiced});
    t += std::max<int64_t>(8, static_cast<int64_t>(std::round(P)));
  }
}

void TdPsolaPrototype::prepare(double fs, int channels, int maxBlockFrames,
                               FrameCount totalInputFrames,
                               const double* ratioCurve) {
  if (!(fs > 0.0) || channels < 1 || channels > 2 || maxBlockFrames < 1 ||
      totalInputFrames < 1 || ratioCurve == nullptr) {
    throw ConfigError("", "prepare", "invalid job geometry");
  }
  if (marks_.empty() && totalInputFrames > 0) {
    throw ConfigError("", "prepare",
                      "analyzeSignal was not called before prepare()");
  }
  fs_ = fs;
  channels_ = channels;
  maxBlock_ = maxBlockFrames;
  nIn_ = totalInputFrames;
  curve_ = ratioCurve;
  stretchMode_ = (mode_ == "stretch");
  puvFrames_ = fs / puvHz_;
  pMax_ = fs / analysis::kTrackerFminHz;
  pMin_ = fs / analysis::kTrackerFmaxHz;

  const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
  // The input window covers: the analysis history (2 pMax) + the declared
  // lookahead (2 pMax + 2K + 128) + the driver blocks + margin — sized so
  // compaction NEVER needs a capacity-driven drop (the schedule-dependent
  // corruption class; see compactInput).
  const FrameCount inCap = static_cast<FrameCount>(4.0 * pMax_) + 2 * K +
                           2 * static_cast<FrameCount>(maxBlockFrames) +
                           512;
  accumCapacity_ = static_cast<FrameCount>(2.0 * pMax_ + pMax_ + 2 * K +
                                           2 * maxBlock_ + 512);
  accum_.assign(static_cast<std::size_t>(channels_),
                std::vector<double>(static_cast<std::size_t>(accumCapacity_),
                                    0.0));
  wsum_.assign(static_cast<std::size_t>(accumCapacity_), 0.0);
  grainBuf_.assign(static_cast<std::size_t>(channels_) *
                       static_cast<std::size_t>(2.0 * pMax_) + 16,
                   0.0);
  inBuf_.assign(
      static_cast<std::size_t>(channels_),
      std::vector<double>(static_cast<std::size_t>(inCap), 0.0));
  inBase_ = 0;
  inAvail_ = 0;
  accumBase_ = 0;
  finalFrontier_ = 0;
  aNext_ = 0.0;
  tNext_ = 0.0;
  grainsPlaced_ = 0;
  reusedGrains_ = 0;
  q_ = 0.0;
  tOut_ = 0;
  finishing_ = false;
  drainEnd_ = 0.0;
  markCursor_ = 0;
  voicedGrains_ = 0;
  unvoicedGrains_ = 0;
  maxLiveAccum_ = 0;

  // Build the Hann window cache for every distinct grain length the mark
  // periods can produce (all known at prepare — process never allocates).
  windowCache_.clear();
  for (const Mark& mk : marks_) {
    const int gLen =
        std::max(4, static_cast<int>(std::round(2.0 * mk.period)));
    bool found = false;
    for (const auto& [len, w] : windowCache_) {
      if (len == gLen) {
        found = true;
        break;
      }
    }
    if (!found) {
      windowCache_.emplace_back(gLen, hannGrain(gLen));
    }
  }
}

const std::vector<double>& TdPsolaPrototype::windowFor(int gLen) {
  for (const auto& [len, w] : windowCache_) {
    if (len == gLen) return w;
  }
  // Defensive: unseen lengths fall back to the first entry (the grain
  // guard bounds gLen to the prepared set in practice).
  return windowCache_.empty() ? wsum_ : windowCache_.front().second;
}

TdPsolaPrototype::Latency TdPsolaPrototype::latency() const {
  const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
  Latency lat;
  lat.inputLookahead = static_cast<FrameCount>(2.0 * pMax_) + 2 * K + 128;
  double minRatio = 1.0;
  for (FrameCount i = 0; i < nIn_; ++i) {
    minRatio = std::min(minRatio, curve_[static_cast<std::size_t>(i)]);
  }
  if (!(minRatio > 0.0)) minRatio = 0.5;
  // Pitch mode: the flush drains the tail through the 1/beta duration
  // change — scale by the curve's worst-case 1/minRatio (the first
  // version's fixed bound was exceeded on -12 — measured).
  const double tail =
      stretchMode_ ? (2.0 * pMax_ + 2 * K + 256)
                   : (4.0 * pMax_ + 2 * K + 512) / std::max(0.25, minRatio);
  lat.outputFlush = static_cast<FrameCount>(tail) + 64;
  return lat;
}

double TdPsolaPrototype::ratioAt(double pos) const {
  if (curve_ == nullptr || nIn_ <= 0) return 1.0;
  double p = std::floor(pos);
  if (p < 0.0) p = 0.0;
  if (p > static_cast<double>(nIn_ - 1)) p = static_cast<double>(nIn_ - 1);
  return curve_[static_cast<std::size_t>(p)];
}

std::size_t TdPsolaPrototype::markIndexNear(double a) const {
  // Advancing cursor + local search: the mark nearest to the input-time a.
  if (marks_.empty()) return 0;
  std::size_t i = markCursor_;
  while (i + 1 < marks_.size() &&
         static_cast<double>(marks_[i + 1].pos) < a) {
    ++i;
  }
  // i is the last mark with pos < a (or 0); compare i and i+1.
  if (i + 1 < marks_.size()) {
    const double d0 = std::abs(static_cast<double>(marks_[i].pos) - a);
    const double d1 = std::abs(static_cast<double>(marks_[i + 1].pos) - a);
    if (d1 < d0) return i + 1;
  }
  return i;
}

double TdPsolaPrototype::readInput(int channel, FrameCount pos) const {
  const FrameCount rel = pos - inBase_;
  if (rel < 0) return 0.0;
  if (rel >= static_cast<FrameCount>(inBuf_[0].size())) return 0.0;
  return inBuf_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(rel)];
}

void TdPsolaPrototype::compactInput() {
  if (finishing_) {
    return;  // finish-mode re-reads the last mark's window: drop nothing.
  }
  // Drop ONLY input no future grain will read. Two floors:
  //   (a) the upcoming marks' window floor: aNext_ - 2 pMax - 128;
  //   (b) the LAST analysis mark's grain start (the finish-mode repeats
  //       re-read [lastMark - P, lastMark + P]).
  // The earlier capacity-driven term was a MEASURED defect: it activated
  // only after aNext_ stalled (schedule-dependent!), dropped input the
  // flush still needed, and broke block-split invariance with a corrupted
  // tail. The window is sized in prepare() for the full span instead.
  const Mark& last = marks_.back();
  const FrameCount fromMarks = std::max<FrameCount>(
      0, static_cast<FrameCount>(aNext_) -
             static_cast<FrameCount>(2.0 * pMax_) - 128);
  const FrameCount fromLastMark = std::max<FrameCount>(
      0, last.pos - static_cast<FrameCount>(last.period));
  const FrameCount keepFrom = std::min(fromMarks, fromLastMark);
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

void TdPsolaPrototype::compactAccum() {
  const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
  const double readPos = stretchMode_ ? q_ : static_cast<double>(tOut_);
  const FrameCount keepFrom = std::max<FrameCount>(
      0, static_cast<FrameCount>(readPos) - K - 128);
  if (accumBase_ >= keepFrom) return;
  const FrameCount drop = keepFrom - accumBase_;
  const FrameCount live = static_cast<FrameCount>(
      std::max(0.0, std::max(static_cast<double>(finalFrontier_),
                             tNext_ + pMax_)) -
      static_cast<double>(accumBase_));
  const FrameCount keep = std::min(live, accumCapacity_);
  if (drop >= keep) {
    for (auto& b : accum_) std::fill(b.begin(), b.end(), 0.0);
    std::fill(wsum_.begin(), wsum_.end(), 0.0);
    accumBase_ = keepFrom;
    return;
  }
  for (auto& b : accum_) {
    std::memmove(b.data(), b.data() + static_cast<std::size_t>(drop),
                 static_cast<std::size_t>(keep - drop) * sizeof(double));
    std::fill(b.begin() + static_cast<std::size_t>(keep - drop), b.end(), 0.0);
  }
  std::memmove(wsum_.data(), wsum_.data() + static_cast<std::size_t>(drop),
               static_cast<std::size_t>(keep - drop) * sizeof(double));
  std::fill(wsum_.begin() + static_cast<std::size_t>(keep - drop), wsum_.end(),
            0.0);
  accumBase_ = keepFrom;
}

bool TdPsolaPrototype::placeGrainsUpTo(FrameCount inputAvailableEnd) {
  bool placedAny = false;
  const double pMaxD = pMax_;
  for (;;) {
    // Stop once the input-time pointer has passed the mark coverage:
    // the remaining output marks belong to the bounded flush (finish).
    // Without this, markIndexNear clamps to the LAST mark and the loop
    // re-places it unboundedly (aNext_ runs past the stream end, the
    // compaction base follows it past inAvail_, and the input memcpy
    // writes at a negative offset — the reproduced heap corruption).
    if (finishing_) {
      if (aNext_ > static_cast<double>(nIn_) + 2.0 * pMaxD) break;
    } else {
      if (aNext_ > static_cast<double>(nIn_)) break;
    }
    // The next output mark (input-time aNext_, output-grid centre tNext_).
    const std::size_t mk = markIndexNear(aNext_);
    const Mark& mark = marks_[mk];
    const double P = mark.period;
    const double half = P;  // grain length 2P, centred at the mark
    const double grainCentreIn = static_cast<double>(mark.pos);
    const double needEnd = grainCentreIn + half;
    const FrameCount inputLimit =
        finishing_ ? std::numeric_limits<FrameCount>::max() : inputAvailableEnd;
    if (finishing_) {
      if (grainCentreIn - half >= static_cast<double>(nIn_) + pMax_) break;
    } else if (needEnd > static_cast<double>(inputLimit)) {
      break;
    }

    const double beta =
        mark.voiced ? (stretchMode_ ? 1.0 : ratioAt(aNext_)) : 1.0;
    const double alpha = stretchMode_ ? ratioAt(aNext_) : 1.0;
    // Output mark spacing: s = P/beta on voiced marks (output pitch =
    // beta x F0); unvoiced marks keep the fixed P_uv spacing.
    const double s = mark.voiced ? P / beta : puvFrames_;
    // Input advance per output mark (the MODE semantics, frozen after a
    // measured design-error correction — see the header):
    //   pitch mode:   u = P  (cycle-accurate consumption; duration changes
    //                          by 1/beta over voiced spans — the classic
    //                          MC90/psola.m pitch modification)
    //   stretch mode: u = s/alpha (mark reuse/skip; duration x alpha, then
    //                          the §7 resampler restores duration and
    //                          scales pitch)
    const double u =
        stretchMode_ ? s / alpha : (mark.voiced ? P : puvFrames_);
    const double t = tNext_;
    const std::size_t mkNext = markIndexNear(aNext_ + u);
    if (mkNext == mk && u < P * 0.75) {
      ++reusedGrains_;  // consecutive output marks on one analysis mark
    }

    // Back-pressure on the accumulator live window.
    const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
    const double readPos = stretchMode_ ? q_ : static_cast<double>(tOut_);
    const FrameCount liveLo = std::max<FrameCount>(
        0, static_cast<FrameCount>(readPos) - K - 128);
    const FrameCount tailEnd = static_cast<FrameCount>(t + half) + 2 * (K + 8);
    if (tailEnd - liveLo > accumCapacity_ - 8) break;

    // Extract the grain (windowed) into scratch, then the FD hook. The
    // Hann window comes from the PREPARE-BUILT CACHE keyed by grain
    // length (all mark periods are known at prepare — zero allocation in
    // the process path).
    const int gLen = std::max(4, static_cast<int>(std::round(2.0 * P)));
    if (static_cast<std::size_t>(channels_) *
                static_cast<std::size_t>(gLen) >
            grainBuf_.size()) {
      // P is bounded by pMax_ (prepare sizing covers it); defensive only.
      break;
    }
    const std::vector<double>& w = windowFor(gLen);
    for (int c = 0; c < channels_; ++c) {
      for (int i = 0; i < gLen; ++i) {
        const double ip = grainCentreIn - P + static_cast<double>(i);
        grainBuf_[static_cast<std::size_t>(c) *
                      static_cast<std::size_t>(gLen) +
                  static_cast<std::size_t>(i)] =
            w[static_cast<std::size_t>(i)] *
            readInput(c, static_cast<FrameCount>(std::floor(ip)));
      }
    }
    transformGrain(GrainContext{beta, P, mark.voiced});

    // Accumulate at the output-grid position round(t). PSOLA synthesis is
    // the RAW overlap-add with a PER-GRAIN level scale s/P (the classic
    // formulation; see the header). The window-product normalisation used
    // by the OLA engine was a MEASURED defect here: at low overlap (beta <
    // 1, s -> 2P) it divided by the single Hann and CANCELLED the window's
    // neighbour-pulse suppression — the downshift rendered unshifted. The
    // s/P scale restores the MEAN level at every spacing (identity: s = P
    // => scale 1, Hann(2P)@P is exactly COLA => bit-exact transparency;
    // beta > 1: j overlapping grains at scale 1/j; beta < 1: the Hann
    // shape survives — the documented PSOLA downshift amplitude family).
    const double grainScale = s / P;
    compactAccum();
    const FrameCount centre = static_cast<FrameCount>(std::llround(t));
    const FrameCount gHalf = gLen / 2;
    for (int i = 0; i < gLen; ++i) {
      const FrameCount p = centre - gHalf + static_cast<FrameCount>(i);
      const FrameCount rel = p - accumBase_;
      if (rel < 0 || rel >= accumCapacity_) continue;  // guarded by pressure
      for (int c = 0; c < channels_; ++c) {
        accum_[static_cast<std::size_t>(c)][static_cast<std::size_t>(rel)] +=
            grainScale *
            grainBuf_[static_cast<std::size_t>(c) *
                          static_cast<std::size_t>(gLen) +
                      static_cast<std::size_t>(i)];
      }
      (void)w;
    }
    ++grainsPlaced_;
    if (mark.voiced) {
      ++voicedGrains_;
    } else {
      ++unvoicedGrains_;
    }
    placedAny = true;
    markCursor_ = mk;
    aNext_ += u;
    tNext_ = t + s;

    // Advance the write boundary below the new frontier (next grain
    // writes from tNext_ - its half; the next period is known from
    // analysis). Raw OLA: no normalisation pass — final positions are the
    // accumulated values as-is.
    const double halfNext = marks_[mkNext].period;
    const double frontierD = std::max(0.0, tNext_ - halfNext);
    const FrameCount frontier = static_cast<FrameCount>(frontierD);
    if (finalFrontier_ < frontier) {
      finalFrontier_ = frontier;
    }
    maxLiveAccum_ =
        std::max<int64_t>(maxLiveAccum_, static_cast<int64_t>(tailEnd - liveLo));
  }
  return placedAny;
}

void TdPsolaPrototype::emitFinalFrames(AudioBlockOut& out, int outCapacity,
                                       FrameCount& produced) {
  const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
  const double drainLimit = finishing_ ? drainEnd_
                                        : std::numeric_limits<double>::max();
  while (produced < static_cast<FrameCount>(outCapacity)) {
    if (stretchMode_) {
      const double need = q_ + static_cast<double>(K) + 1.0;
      if (need > static_cast<double>(finalFrontier_)) break;
      if (finishing_ && q_ >= drainLimit) break;
      const double rho = ratioAt(
          stretchMode_ ? static_cast<double>(tOut_) : 0.0);
      // Stretch mode: the composite output position maps to the input
      // timeline 1:1 (duration preserved); ratio indexed at the output
      // position like the OLA stage-B convention.
      const double rhoOut = ratioAt(tOut_);
      (void)rho;
      const double cutoff = antiAliasCutoff(rhoOut);
      for (int c = 0; c < channels_; ++c) {
        out.channels[c][static_cast<std::size_t>(produced)] = interpolateAtAbs(
            accum_[static_cast<std::size_t>(c)].data(),
            static_cast<FrameCount>(accum_[0].size()), accumBase_, q_, cutoff,
            ResampleQuality::Standard);
      }
      q_ += rhoOut;
      ++tOut_;
      ++produced;
    } else {
      // Pitch mode: output grid == accumulator grid; 1:1 emission.
      if (static_cast<double>(tOut_) + 1.0 >
          static_cast<double>(finalFrontier_)) {
        break;
      }
      if (finishing_ && static_cast<double>(tOut_) >= drainLimit) break;
      const FrameCount rel = tOut_ - accumBase_;
      for (int c = 0; c < channels_; ++c) {
        out.channels[c][static_cast<std::size_t>(produced)] =
            (rel >= 0 && rel < accumCapacity_)
                ? accum_[static_cast<std::size_t>(c)]
                         [static_cast<std::size_t>(rel)]
                : 0.0;
      }
      ++tOut_;
      ++produced;
    }
  }
}

ProcessOutcome TdPsolaPrototype::process(const AudioBlockView& in,
                                         int inFrames, AudioBlockOut& out,
                                         int outCapacity,
                                         FrameCount inputFrameIndex) {
  if (inFrames < 0 || inFrames > maxBlock_ || inputFrameIndex != inAvail_) {
    throw ConfigError("", "process", "block contract violation");
  }
  compactInput();
  {
    const FrameCount rel = inAvail_ - inBase_;
    const auto cap = static_cast<FrameCount>(inBuf_[0].size());
    if (rel + inFrames > cap) {
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

ProcessOutcome TdPsolaPrototype::finish(AudioBlockOut& out, int outCapacity) {
  if (finishing_) {
    throw ConfigError("", "finish", "finish called twice");
  }
  finishing_ = true;
  drainEnd_ = std::numeric_limits<double>::max();
  const double pMaxD = pMax_;

  FrameCount produced = 0;
  for (int64_t guard = 0; guard < (1 << 24); ++guard) {
    const bool placedAny = placeGrainsUpTo(0);
    const bool grainsExhausted =
        aNext_ >= static_cast<double>(nIn_) + pMaxD * 3.0;
    if (grainsExhausted) {
      drainEnd_ = tNext_ + pMaxD;
    }
    const FrameCount before = produced;
    if (outCapacity > 0 && out.channels != nullptr) {
      emitFinalFrames(out, outCapacity, produced);
    }
    if (produced == before && (!placedAny || grainsExhausted)) {
      break;
    }
  }
  ProcessOutcome rep;
  rep.inputFramesConsumed = 0;
  rep.outputFramesProduced = produced;
  return rep;
}

json::Value TdPsolaPrototype::diagnostics() const {
  json::Object o;
  o["grains_placed"] = json::Value(static_cast<int64_t>(grainsPlaced_));
  o["voiced_grains"] = json::Value(static_cast<int64_t>(voicedGrains_));
  o["unvoiced_grains"] = json::Value(static_cast<int64_t>(unvoicedGrains_));
  o["reused_grains"] = json::Value(static_cast<int64_t>(reusedGrains_));
  o["marks"] = json::Value(static_cast<int64_t>(marks_.size()));
  o["mode"] = json::Value(mode_);
  o["analysis_ms_per_second"] = json::Value(analysisMsPerSecond_);
  o["max_live_accum_frames"] =
      json::Value(static_cast<int64_t>(maxLiveAccum_));
  return json::Value(std::move(o));
}

}  // namespace pitchlab::proto
