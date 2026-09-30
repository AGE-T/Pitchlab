#include "engines/pv_classic_engine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// The vendored pocketfft header (external/pocketfft/, BSD-3-Clause,
// ORIGIN.toml-pinned; the documented "lab binary" dependency of build/CI
// spec §7). POCKETFFT_CACHE_SIZE 0: no global plan cache — the engine owns
// its persistent plan object (§6.3.1 item 16); the public convenience
// functions allocate per call and are NOT used in the processing path.
#define POCKETFFT_CACHE_SIZE 0
#include <pocketfft/pocketfft_hdronly.h>

#include "core/engine_registry.h"
#include "core/errors.h"
#include "core/resampler.h"

namespace pitchlab {
namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

/// princarg (§6.3.1 item 5): wrap to (−π, π] via x − 2π·round(x/2π).
inline double princarg(double x) {
  return x - kTwoPi * static_cast<double>(std::llround(x / kTwoPi));
}

// ---------------------------------------------------------------------------
// Absolute-indexed windows (the §6.1.1 item 8 pattern, private to this TU).
// ---------------------------------------------------------------------------
struct SlidingWindow {  // sequential appends (input stream, filtered stream)
  std::vector<double> data;
  int64_t start = 0, count = 0;

  void allocate(int64_t capacity, int64_t startIdx) {
    data.assign(static_cast<std::size_t>(capacity), 0.0);
    start = startIdx;
    count = 0;
  }
  [[nodiscard]] double at(int64_t absIdx) const {
    return data[static_cast<std::size_t>(absIdx - start)];
  }
  void append(const double* src, int64_t n) {
    if (n <= 0) return;
    if (count + n > static_cast<int64_t>(data.size())) {
      throw EngineException("native.pv.classic",
                            "internal window overflow (sizing bug; report as engine defect)");
    }
    std::memcpy(data.data() + count, src, sizeof(double) * static_cast<std::size_t>(n));
    count += n;
  }
  void dropBefore(int64_t absIdx) {
    if (absIdx <= start) return;
    const int64_t drop = std::min(absIdx - start, count);
    if (drop <= 0) return;
    const std::size_t keep = static_cast<std::size_t>(count - drop);
    if (keep > 0) {
      std::memmove(data.data(), data.data() + static_cast<std::size_t>(drop),
                   keep * sizeof(double));
    }
    start += drop;
    count = static_cast<int64_t>(keep);
  }
  [[nodiscard]] int64_t freeSpace() const {
    return static_cast<int64_t>(data.size()) - count;
  }
  [[nodiscard]] int64_t end() const { return start + count; }
  [[nodiscard]] int64_t capacity() const { return static_cast<int64_t>(data.size()); }
};

/// OLA accumulator (§6.3.1 item 8): absolute-indexed, zero-extendable,
/// add-at-position, front-compaction (memmove only — no allocation after
/// prepare()).
struct OlapWindow {
  std::vector<double> data;
  int64_t start = 0, count = 0;

  void allocate(int64_t capacity, int64_t startIdx) {
    data.assign(static_cast<std::size_t>(capacity), 0.0);
    start = startIdx;
    count = 0;
  }
  /// Zero-fill the region [start+count, absEnd).
  void extendTo(int64_t absEnd) {
    if (absEnd <= start + count) return;
    if (absEnd - start > static_cast<int64_t>(data.size())) {
      throw EngineException("native.pv.classic",
                            "internal accumulator overflow (sizing bug; report as engine defect)");
    }
    for (int64_t i = start + count; i < absEnd; ++i) {
      data[static_cast<std::size_t>(i - start)] = 0.0;
    }
    count = absEnd - start;
  }
  void addAt(int64_t absIdx, double v) {
    if (absIdx < start || absIdx >= start + count) {
      throw EngineException("native.pv.classic",
                            "internal accumulator add out of range (sizing bug; report as engine defect)");
    }
    data[static_cast<std::size_t>(absIdx - start)] += v;
  }
  [[nodiscard]] double at(int64_t absIdx) const {
    return data[static_cast<std::size_t>(absIdx - start)];
  }
  void dropBefore(int64_t absIdx) {
    if (absIdx <= start) return;
    const int64_t drop = std::min(absIdx - start, count);
    if (drop <= 0) return;
    const std::size_t keep = static_cast<std::size_t>(count - drop);
    if (keep > 0) {
      std::memmove(data.data(), data.data() + static_cast<std::size_t>(drop),
                   keep * sizeof(double));
    }
    start += drop;
    count = static_cast<int64_t>(keep);
  }
  [[nodiscard]] int64_t end() const { return start + count; }
};

// ---------------------------------------------------------------------------
// The engine (§6.3 sheet + §6.3.1 frozen implementation behaviour)
// ---------------------------------------------------------------------------
class PvClassicEngine final : public PitchEngine {
 public:
  const char* engineId() const override { return "native.pv.classic"; }

  void configure(const EngineConfiguration& cfg) override {
    fftSize_ = kDefaultFftSize;
    if (const ParameterValue* v = cfg.find("fft_size")) {
      if (const auto* i = std::get_if<int64_t>(v)) {
        if (*i < 32 || (*i & (*i - 1)) != 0) {
          throw ConfigError("", "fft_size",
                            "fft_size must be a power of two >= 32 (§6.3.1 item 1)");
        }
        fftSize_ = *i;
      } else {
        throw ConfigError("", "fft_size", "fft_size must be an integer (power of two >= 32)");
      }
    }
    hop_ = 0;  // resolved against fft_size below
    bool hopGiven = false;
    if (const ParameterValue* v = cfg.find("hop")) {
      if (const auto* i = std::get_if<int64_t>(v)) {
        hop_ = *i;
        hopGiven = true;
      } else {
        throw ConfigError("", "hop", "hop must be an integer");
      }
    }
    if (!hopGiven) hop_ = kDefaultHop;  // the default applies only when ABSENT
    if (hop_ < 1 || hop_ > fftSize_ / 2) {
      throw ConfigError("", "hop",
                        "hop must be in [1, fft_size/2] (>= 50% overlap, the L-D'99 domain; "
                        "§6.3.1 item 1)");
    }
    if (const ParameterValue* v = cfg.find("window")) {
      if (const auto* s = std::get_if<std::string>(v)) {
        if (*s != "hann") {
          throw ConfigError("", "window", "unknown window '" + *s + "' (hann, §6.3.1 item 1)");
        }
      } else {
        throw ConfigError("", "window", "window must be a string (hann)");
      }
    }
    // The seed is accepted and unused (Deterministic, §6.3.1 item 1).
    prepared_ = false;
  }

  void prepare(const ProcessContext& ctx) override {
    if (!(ctx.sampleRate > 0.0) || !std::isfinite(ctx.sampleRate)) {
      throw ConfigError("", "sampleRate", "sample rate must be finite and positive");
    }
    if (ctx.channels < 1 || ctx.channels > 2) {
      throw ConfigError("", "channels",
                        "native.pv.classic supports 1-2 channels (declared MonoAndStereo)");
    }
    if (ctx.maxBlockFrames <= 0) {
      throw ConfigError("", "maxBlockFrames", "max block frames must be positive");
    }
    if (ctx.totalInputFrames < 1) {
      throw ConfigError("", "totalInputFrames",
                        "total input frames must be >= 1 (an empty stream is not a job)");
    }
    if (ctx.curve == nullptr || ctx.curve->ratio == nullptr ||
        ctx.curve->frames != ctx.totalInputFrames) {
      throw ConfigError("", "curve",
                        "curve view missing or frame count mismatch (view frames must equal N_in)");
    }
    const double* ratio = ctx.curve->ratio;
    double rMax = ratio[0];
    for (FrameCount i = 1; i < ctx.totalInputFrames; ++i) {
      const double v = ratio[static_cast<std::size_t>(i)];
      if (!std::isfinite(v) || v <= 0.0) {
        throw ConfigError("", "curve", "curve ratio at frame " + std::to_string(i) +
                                           " is non-finite or non-positive (§4.4.1)");
      }
      rMax = std::max(rMax, v);
    }

    ctx_ = ctx;
    N_ = fftSize_;
    H_ = hop_;
    bins_ = N_ / 2 + 1;
    kR_ = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;  // K = 16 (§7.3)
    rMax_ = rMax;
    firActive_ = (rMax_ > 1.0);
    cutoff_ = antiAliasCutoff(rMax_);  // §6.3.1 item 9 (whole-curve max)
    if (firActive_) fir_ = designAntiAliasFir(cutoff_, ResampleQuality::Standard);
    latency_.inputLatencyFrames = N_ + H_;  // sheet verbatim (§6.3.1 item 10)
    latency_.outputLatencyFrames = N_;

    // Window tables (§6.3.1 items 3/8): analysis, synthesis, product.
    const double denom = static_cast<double>(N_ - 1);
    wA_.resize(static_cast<std::size_t>(N_));
    wS_.assign(static_cast<std::size_t>(N_), 0.0);
    wP_.assign(static_cast<std::size_t>(N_), 0.0);
    for (int64_t j = 0; j < N_; ++j) {
      const double w = 0.5 * (1.0 - std::cos(kTwoPi * static_cast<double>(j) / denom));
      wA_[static_cast<std::size_t>(j)] = w;
      wS_[static_cast<std::size_t>(j)] = w;
      wP_[static_cast<std::size_t>(j)] = w * w;
    }
    nominal_.assign(static_cast<std::size_t>(bins_), 0.0);  // 2πk·H/N
    for (int64_t k = 0; k < bins_; ++k) {
      nominal_[static_cast<std::size_t>(k)] = kTwoPi * static_cast<double>(k) *
                                              static_cast<double>(H_) / static_cast<double>(N_);
    }

    // The persistent FFT plan (§6.3.1 item 16) and per-channel spectral state.
    plan_ = std::make_unique<pocketfft::detail::pocketfft_r<double>>(
        static_cast<std::size_t>(N_));
    fftBuf_.assign(static_cast<std::size_t>(N_), 0.0);
    mag_.assign(static_cast<std::size_t>(ctx.channels) * static_cast<std::size_t>(bins_), 0.0);
    prevPhase_.assign(static_cast<std::size_t>(ctx.channels) * static_cast<std::size_t>(bins_), 0.0);
    synthPhase_.assign(static_cast<std::size_t>(ctx.channels) * static_cast<std::size_t>(bins_), 0.0);
    havePrev_.assign(static_cast<std::size_t>(ctx.channels), false);

    // pEnd_: the total read advance over the output timeline (prepare-time
    // bound for the synthesis need horizon; §6.3.1 item 10).
    const FrameCount outputEnd = ctx.totalInputFrames + N_;
    double pEnd = 0.0;
    for (FrameCount m = 0; m < outputEnd; ++m) {
      const int64_t idx = std::min<FrameCount>(m, ctx.totalInputFrames - 1);
      pEnd += ratio[static_cast<std::size_t>(idx)];
    }
    pEnd_ = pEnd;
    sNeed_ = static_cast<int64_t>(std::ceil(pEnd_)) + 2 * kR_ + N_;  // generous (§6.3.1 item 10)

    // Buffers: input windows, stretched accumulators (+AA path), window-sum.
    const int64_t inCap = static_cast<int64_t>(ctx.maxBlockFrames) + N_ + 128;
    inputWin_.resize(static_cast<std::size_t>(ctx.channels));
    // The stretched-accumulator span bound (§6.3.1 items 8/9): the synthesis
    // leads the read by the gated lag rho_max·(N + K) plus the frame span,
    // the end-state rho_max·H + N + K, and in-flight block headroom.
    const int64_t accSpan =
        static_cast<int64_t>(std::ceil(rMax_ * static_cast<double>(N_ + kR_))) +
        N_ + 2 * kR_ + static_cast<int64_t>(ctx.maxBlockFrames) +
        static_cast<int64_t>(std::ceil(rMax_ * static_cast<double>(H_))) + 2 * N_ + 256;
    if (firActive_) {
      rawAcc_.resize(static_cast<std::size_t>(ctx.channels));
      filteredWin_.resize(static_cast<std::size_t>(ctx.channels));
    } else {
      stretchAcc_.resize(static_cast<std::size_t>(ctx.channels));
    }
    for (ChannelCount c = 0; c < ctx.channels; ++c) {
      inputWin_[static_cast<std::size_t>(c)].allocate(inCap, 0);
      if (firActive_) {
        rawAcc_[static_cast<std::size_t>(c)].allocate(accSpan, 0);
        filteredWin_[static_cast<std::size_t>(c)].allocate(accSpan, 0);
      } else {
        stretchAcc_[static_cast<std::size_t>(c)].allocate(accSpan, 0);
      }
    }
    winSum_.allocate(accSpan, 0);

    resetStreaming();
    prepared_ = true;
  }

  Latency latency() const override { return latency_; }

  ProcessReport process(const AudioBlockView& in, int inFrames, AudioBlockOut& out,
                        int outCapacity, const PitchCurveView& curve,
                        FrameCount inputFrameIndex) override {
    if (!prepared_) {
      throw EngineException("native.pv.classic", "process() called before prepare()");
    }
    if (inFrames < 0 || outCapacity < 0) {
      throw EngineException("native.pv.classic", "negative inFrames/outCapacity");
    }
    if (inputFrameIndex != consumedTotal_) {
      throw EngineException("native.pv.classic",
                            "inputFrameIndex != cumulative consumed (contract precondition, §4.2)");
    }
    if (in.channelCount != ctx_.channels || out.channelCount != ctx_.channels) {
      throw EngineException("native.pv.classic", "channel count mismatch in block views");
    }
    if (curve.ratio != ctx_.curve->ratio || curve.frames != ctx_.curve->frames) {
      throw EngineException("native.pv.classic",
                            "curve view differs from the prepared one (contract: same object)");
    }

    // ---- 1) consume the offered input (consume-all; §6.1.1 item 6 pattern) ----
    int64_t consumed = 0;
    if (inFrames > 0) {
      for (ChannelCount c = 0; c < ctx_.channels; ++c) {
        inputWin_[static_cast<std::size_t>(c)].dropBefore(aNext_);
      }
      int64_t space = inputWin_[0].freeSpace();
      for (ChannelCount c = 1; c < ctx_.channels; ++c) {
        space = std::min(space, inputWin_[static_cast<std::size_t>(c)].freeSpace());
      }
      consumed = std::min<int64_t>(inFrames, space);
      if (consumed > 0) {
        for (ChannelCount c = 0; c < ctx_.channels; ++c) {
          inputWin_[static_cast<std::size_t>(c)].append(in.channels[static_cast<std::size_t>(c)],
                                                        consumed);
        }
        consumedTotal_ += consumed;
      }
    }

    // ---- 2) produce output frames m ∈ [0, N_in) (§6.3.1 item 11) ----
    const int produced = produceInto(out, outCapacity, /*finishMode=*/false);

    // ---- 3) exhaustion flag (§4.2.1 item 7) ----
    const FrameCount streamEnd = ctx_.totalInputFrames + latency_.inputLatencyFrames;
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= streamEnd);

    return ProcessReport{consumed, produced, inputExhausted_};
  }

  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    if (!prepared_) {
      throw EngineException("native.pv.classic", "finish() called before prepare()");
    }
    if (finished_) {
      throw EngineException("native.pv.classic", "finish() called twice (contract: exactly once)");
    }
    if (outCapacity < 0) {
      throw EngineException("native.pv.classic", "negative outCapacity");
    }
    if (out.channelCount != ctx_.channels) {
      throw EngineException("native.pv.classic", "channel count mismatch in output view");
    }
    finished_ = true;

    // The tail [N_in, N_in + N) — exactly N frames (§6.3.1 item 11); the
    // analysis completes with semantic-zero taps beyond the padded stream.
    const int produced = produceInto(out, outCapacity, /*finishMode=*/true);
    return ProcessReport{0, produced, inputExhausted_};
  }

  void reset() override {
    if (!prepared_) return;
    for (ChannelCount c = 0; c < ctx_.channels; ++c) {
      inputWin_[static_cast<std::size_t>(c)].allocate(
          inputWin_[static_cast<std::size_t>(c)].capacity(), 0);
      if (firActive_) {
        rawAcc_[static_cast<std::size_t>(c)].allocate(
            rawAcc_[static_cast<std::size_t>(c)].data.size(), 0);
        filteredWin_[static_cast<std::size_t>(c)].allocate(
            filteredWin_[static_cast<std::size_t>(c)].capacity(), 0);
      } else {
        stretchAcc_[static_cast<std::size_t>(c)].allocate(
            stretchAcc_[static_cast<std::size_t>(c)].data.size(), 0);
      }
    }
    winSum_.allocate(static_cast<int64_t>(winSum_.data.size()), 0);
    resetStreaming();
  }

 private:
  // ---- configuration (§6.3.1 item 1) ----
  static constexpr int64_t kDefaultFftSize = 2048;
  static constexpr int64_t kDefaultHop = 512;
  int64_t fftSize_ = kDefaultFftSize;
  int64_t hop_ = kDefaultHop;

  // ---- per-job state ----
  ProcessContext ctx_{};
  int64_t N_ = 0, H_ = 0, bins_ = 0, kR_ = 0;
  double rMax_ = 1.0, cutoff_ = 1.0, pEnd_ = 0.0;
  bool firActive_ = false;
  std::vector<double> fir_;  // designAntiAliasFir (2K+1 taps, [−K..K])
  Latency latency_{};
  std::vector<double> wA_, wS_, wP_, nominal_;
  std::unique_ptr<pocketfft::detail::pocketfft_r<double>> plan_;
  std::vector<double> fftBuf_;
  std::vector<double> mag_, prevPhase_, synthPhase_;
  std::vector<char> havePrev_;
  int64_t sNeed_ = 0;
  std::vector<SlidingWindow> inputWin_;    // per channel (analysis input)
  std::vector<OlapWindow> stretchAcc_;    // per channel (no-AA path)
  std::vector<OlapWindow> rawAcc_;        // per channel (AA path: raw OLA)
  std::vector<SlidingWindow> filteredWin_;// per channel (AA path: FIR output)
  OlapWindow winSum_;                     // engine-level (shared grid)
  bool prepared_ = false;

  // ---- streaming state ----
  int64_t n_ = 0;          // the next analysis frame index
  int64_t aNext_ = 0;      // = n_·H (the next analysis position)
  int64_t sCur_ = 0;       // the next synthesis placement position s_n
  int64_t hPrev_ = 0;      // h_{n-1} (the hop that led into frame n)
  int64_t filterNext_ = 0; // the next stretched index to FIR-filter
  double readPos_ = 0.0;   // the resampler read position p (engine-level)
  int64_t outM_ = 0;       // the next output frame
  FrameCount consumedTotal_ = 0;
  bool inputExhausted_ = false;
  bool finished_ = false;
  FrameCount flushEmitted_ = 0;

  void resetStreaming() {
    n_ = 0;
    aNext_ = 0;
    sCur_ = 0;
    hPrev_ = 0;
    filterNext_ = 0;
    readPos_ = 0.0;
    outM_ = 0;
    consumedTotal_ = 0;
    inputExhausted_ = false;
    finished_ = false;
    flushEmitted_ = 0;
    std::fill(havePrev_.begin(), havePrev_.end(), false);
  }

  /// Process analysis frame n (§6.3.1 items 3-8): one STFT frame per
  /// channel, phase propagation, synthesis frame, OLA into the
  /// accumulators. Requires the frame's input region delivered (or
  /// finishMode: taps beyond the stream read zeros).
  void processAnalysisFrame(bool finishMode) {
    const double* ratio = ctx_.curve->ratio;
    const FrameCount nIn = ctx_.totalInputFrames;
    // rho_n at the analysis boundary (§6.3.1 item 4); the hop AFTER frame n.
    const int64_t idx = std::min<int64_t>(aNext_, nIn - 1);
    const double rho = ratio[static_cast<std::size_t>(idx)];
    const int64_t h = std::max<int64_t>(1, std::llround(rho * static_cast<double>(H_)));

    for (ChannelCount c = 0; c < ctx_.channels; ++c) {
      const std::size_t chOff = static_cast<std::size_t>(c) * static_cast<std::size_t>(bins_);
      const SlidingWindow& win = inputWin_[static_cast<std::size_t>(c)];
      // ---- windowed analysis frame (semantic zeros beyond the stream) ----
      for (int64_t j = 0; j < N_; ++j) {
        const int64_t p = aNext_ + j;
        double x = 0.0;
        if (p >= win.start && p < win.end()) {
          x = win.at(p);
        } else if (p >= win.end()) {
          // Beyond the delivered stream: semantic zeros (§7.6) — legal in
          // finish mode only (process mode is gated on full delivery).
          if (!finishMode) {
            throw EngineException("native.pv.classic",
                                  "internal analysis window underrun (gating bug; report as "
                                  "engine defect)");
          }
        } else {
          throw EngineException("native.pv.classic",
                                "internal analysis window drop-policy bug; report as engine "
                                "defect");
        }
        fftBuf_[static_cast<std::size_t>(j)] = wA_[static_cast<std::size_t>(j)] * x;
      }
      // ---- forward transform (packed halfcomplex) ----
      plan_->exec(fftBuf_.data(), 1.0, true);
      // ---- unpack: magnitudes + phases; phase propagation ----
      const bool havePrev = havePrev_[static_cast<std::size_t>(c)] != 0;
      for (int64_t k = 0; k < bins_; ++k) {
        double re, im;
        if (k == 0) {
          re = fftBuf_[0];
          im = 0.0;
        } else if (k == N_ / 2) {
          re = fftBuf_[static_cast<std::size_t>(N_ - 1)];
          im = 0.0;
        } else {
          re = fftBuf_[static_cast<std::size_t>(2 * k - 1)];
          im = fftBuf_[static_cast<std::size_t>(2 * k)];
        }
        mag_[chOff + static_cast<std::size_t>(k)] = std::hypot(re, im);
        const double phi = std::atan2(im, re);
        if (!havePrev) {
          // §6.3.1 item 5: frame 0 initialises the synthesis accumulators.
          synthPhase_[chOff + static_cast<std::size_t>(k)] = phi;
        } else {
          const double dphi =
              princarg(phi - prevPhase_[chOff + static_cast<std::size_t>(k)] -
                       nominal_[static_cast<std::size_t>(k)]);
          const double omega =
              kTwoPi * static_cast<double>(k) / static_cast<double>(N_) +
              dphi / static_cast<double>(H_);
          // §6.3.1 item 6: the advance INTO frame n uses the freshest IF
          // estimate times the hop that led here (h_{n-1}).
          synthPhase_[chOff + static_cast<std::size_t>(k)] +=
              omega * static_cast<double>(hPrev_);
        }
        prevPhase_[chOff + static_cast<std::size_t>(k)] = phi;
      }
      havePrev_[static_cast<std::size_t>(c)] = 1;
      // ---- synthesis spectrum -> time frame (c2r, factor 1/N; the packed
      // halfcomplex layout is the exact mirror of the r2h unpack above —
      // slots [0, N) are covered exactly once by k = 0..N/2) ----
      for (int64_t k = 0; k < bins_; ++k) {
        const double m = mag_[chOff + static_cast<std::size_t>(k)];
        const double ph = synthPhase_[chOff + static_cast<std::size_t>(k)];
        const double re = m * std::cos(ph);
        const double im = m * std::sin(ph);
        if (k == 0) {
          fftBuf_[0] = re;
        } else if (k == N_ / 2) {
          fftBuf_[static_cast<std::size_t>(N_ - 1)] = re;
        } else {
          fftBuf_[static_cast<std::size_t>(2 * k - 1)] = re;
          fftBuf_[static_cast<std::size_t>(2 * k)] = im;
        }
      }
      plan_->exec(fftBuf_.data(), 1.0 / static_cast<double>(N_), false);
      // ---- windowed OLA into the stretched accumulator (§6.3.1 item 8) ----
      OlapWindow& acc = firActive_ ? rawAcc_[static_cast<std::size_t>(c)]
                                   : stretchAcc_[static_cast<std::size_t>(c)];
      acc.extendTo(sCur_ + N_);
      for (int64_t j = 0; j < N_; ++j) {
        acc.addAt(sCur_ + j, wS_[static_cast<std::size_t>(j)] *
                                 fftBuf_[static_cast<std::size_t>(j)]);
      }
    }
    // The window-sum accumulator (engine-level, shared; §6.3.1 item 8).
    winSum_.extendTo(sCur_ + N_);
    for (int64_t j = 0; j < N_; ++j) {
      winSum_.addAt(sCur_ + j, wP_[static_cast<std::size_t>(j)]);
    }

    hPrev_ = h;
    sCur_ += h;
    ++n_;
    aNext_ += H_;
  }

  /// The streaming AA FIR (§6.3.1 item 9): extend EVERY channel's filtered
  /// window in lockstep over the SAME absolute stretched range (the filter
  /// progress filterNext_ is engine-level; each channel's window receives
  /// its own filtered values for the same x). GATE: the raw accumulator is
  /// an OLA, NOT a sequential stream — cells in [sCur_, sCur_+N) still
  /// receive adds from future frames (their content is INCOMPLETE and
  /// schedule-dependent); cells below sCur_ are complete (future frames
  /// only write at or above sCur_). The FIR at x is therefore computable
  /// iff x + K < sCur_ (the varispeed "extent − K" rule would read
  /// incomplete OLA content — that was the cross-schedule seed).
  /// Finish mode: through the read horizon with zero taps beyond the raw
  /// extent (the synthesis is complete by then).
  void extendFiltered(bool finishMode) {
    if (!firActive_) return;
    const int64_t K = kR_;
    const int64_t rawEnd = rawAcc_[0].end();
    int64_t target = sCur_ - K;
    if (finishMode) {
      target = std::max<int64_t>(target, static_cast<int64_t>(std::ceil(pEnd_)) + 2 * K + 1);
    }
    while (filterNext_ < target) {
      // Compute this x's filtered value per channel (ascending tap order —
      // determinism), then append to each channel's window.
      for (ChannelCount c = 0; c < ctx_.channels; ++c) {
        const OlapWindow& raw = rawAcc_[static_cast<std::size_t>(c)];
        SlidingWindow& fil = filteredWin_[static_cast<std::size_t>(c)];
        double acc = 0.0;
        for (int j = -K; j <= K; ++j) {  // ascending tap order (determinism)
          const int64_t p = filterNext_ + j;
          if (p < 0) continue;  // leading semantic zeros (§7.6)
          if (p >= rawEnd) {
            if (finishMode) continue;  // beyond-stream zeros
            throw EngineException("native.pv.classic",
                                  "internal FIR window bounds violated (sizing/gating bug; "
                                  "report as engine defect)");
          }
          if (p < raw.start) {
            throw EngineException("native.pv.classic",
                                  "internal FIR window underrun (drop-policy bug; report as "
                                  "engine defect)");
          }
          acc += fir_[static_cast<std::size_t>(j + K)] * raw.at(p);
        }
        if (fil.freeSpace() == 0) {
          // Drop below the read floor to make space (the read position has
          // passed this content).
          fil.dropBefore(static_cast<int64_t>(std::floor(readPos_)) - K - 8);
        }
        fil.append(&acc, 1);
      }
      ++filterNext_;
    }
  }

  /// Produce output frames (§6.3.1 items 9/11). The analysis + AA FIR
  /// advance FIRST (as far as the input delivery permits — independent of
  /// the output cap: the engine must keep preparing and consuming while the
  /// production is capped at m < N_in, or the padded stream could never be
  /// fully consumed); then the production loop (process mode: m ∈ [0, N_in)
  /// with the coverage gate; finish mode: the tail, capped at N frames).
  int produceInto(AudioBlockOut& out, int outCapacity, bool finishMode) {
    if (outCapacity <= 0) {
      // Still advance the analysis/FIR (the state must progress even when
      // the renderer offers no output capacity this call).
      while (sCur_ < sNeed_ && (finishMode || aNext_ + N_ <= consumedTotal_)) {
        processAnalysisFrame(finishMode);
      }
      extendFiltered(finishMode);
      dropPass();
      return 0;
    }
    const FrameCount nIn = ctx_.totalInputFrames;
    const FrameCount outputEnd = nIn + N_;
    const double* ratio = ctx_.curve->ratio;
    int produced = 0;

    // ---- 1) advance the analysis + AA FIR while input is available ----
    while (sCur_ < sNeed_ && (finishMode || aNext_ + N_ <= consumedTotal_)) {
      processAnalysisFrame(finishMode);
    }
    extendFiltered(finishMode);

    while (outM_ < outputEnd && produced < outCapacity) {
      const int64_t m = outM_;
      if (!finishMode && m >= nIn) break;         // process emits m < N_in only
      if (finishMode && flushEmitted_ >= N_) break;  // defensive N cap

      // ---- 3) coverage gate (process mode; §6.3.1 item 9): ALL synthesis
      // frames covering the read's tap region [p−K, p+K] must have been
      // PLACED — the next placement position must lie beyond p+K (the
      // extent alone is NOT sufficient: early frames' window tails extend
      // the extent far beyond the synthesis grid, and reading there would
      // emit near-zero content from the wrong analysis positions).
      if (!finishMode) {
        const int64_t tapMax = std::llround(readPos_) + kR_;
        if (sCur_ <= tapMax) break;
        bool covered = true;
        if (firActive_) {
          for (ChannelCount c = 0; c < ctx_.channels && covered; ++c) {
            if (filteredWin_[static_cast<std::size_t>(c)].end() <= tapMax) covered = false;
          }
        } else {
          for (ChannelCount c = 0; c < ctx_.channels && covered; ++c) {
            if (stretchAcc_[static_cast<std::size_t>(c)].end() <= tapMax) covered = false;
          }
        }
        if (winSum_.end() <= tapMax) covered = false;
        if (!covered) break;
      }

      // ---- 4) read (signal / window-sum) and emit ----
      const int64_t idx = std::min<int64_t>(m, nIn - 1);
      for (ChannelCount c = 0; c < ctx_.channels; ++c) {
        // §7.2.2 absolute-position reads (the exact-fraction variant): the
        // PV's cross-frame phase accumulators amplify the relative variant's
        // window-placement rounding seed to signal scale across schedules.
        double ySig;
        if (firActive_) {
          const SlidingWindow& w = filteredWin_[static_cast<std::size_t>(c)];
          ySig = interpolateAtAbs(w.data.data(), w.count, w.start, readPos_, cutoff_,
                                   ResampleQuality::Standard);
        } else {
          const OlapWindow& w = stretchAcc_[static_cast<std::size_t>(c)];
          ySig = interpolateAtAbs(w.data.data(), w.count, w.start, readPos_, cutoff_,
                                   ResampleQuality::Standard);
        }
        const double yWin = interpolateAtAbs(winSum_.data.data(), winSum_.count, winSum_.start,
                                             readPos_, cutoff_, ResampleQuality::Standard);
        out.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(produced)] =
            (std::fabs(yWin) > 1e-12) ? (ySig / yWin) : 0.0;
      }
      ++produced;
      ++outM_;
      if (finishMode) ++flushEmitted_;

      // ---- 5) advance the read position (left-edge; §6.3.1 item 9) ----
      readPos_ += ratio[static_cast<std::size_t>(idx)];

      // The analysis may have advanced further during production; keep the
      // FIR current so the next coverage check sees the latest extent.
      extendFiltered(finishMode);
    }

    dropPass();
    return produced;
  }

  /// The retention-floor drop pass (§6.3.1 items 8/9).
  void dropPass() {
    const int64_t readFloor = static_cast<int64_t>(std::floor(readPos_)) - kR_ - 8;
    for (ChannelCount c = 0; c < ctx_.channels; ++c) {
      inputWin_[static_cast<std::size_t>(c)].dropBefore(aNext_);
      if (firActive_) {
        rawAcc_[static_cast<std::size_t>(c)].dropBefore(
            std::min(readFloor, filterNext_ - kR_ - 8));
        filteredWin_[static_cast<std::size_t>(c)].dropBefore(readFloor);
      } else {
        stretchAcc_[static_cast<std::size_t>(c)].dropBefore(readFloor);
      }
    }
    winSum_.dropBefore(readFloor);
  }
};

}  // namespace

std::unique_ptr<PitchEngine> makePvClassicEngine() {
  return std::make_unique<PvClassicEngine>();
}

// Engine-owned discrete choices (Task 29): the FFT/hop domains the engine's
// own configure() accepts, and the window (hann only, §6.3.1 item 1).
const char* const kPvFftChoiceNames[] = {"1024", "2048", "4096"};
const double kPvFftChoiceValues[] = {1024.0, 2048.0, 4096.0};
const char* const kPvHopChoiceNames[] = {"128", "256", "512", "1024"};
const double kPvHopChoiceValues[] = {128.0, 256.0, 512.0, 1024.0};
const char* const kPvWindowChoices[] = {"hann"};

EngineDescriptor pvClassicEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = "native.pv.classic";
  d.info.displayName = "Phase vocoder — classic (benchmark baseline)";
  d.info.version = "0.1.0";
  d.info.usageClass = UsageClass::Prototype;
  d.info.origin = "own implementation (FFT: vendored pocketfft, BSD-3-Clause)";
  d.info.license = "own code + vendored pocketfft (BSD-3-Clause)";
  d.capabilities.minRatio = 0.25;
  d.capabilities.maxRatio = 4.0;
  d.capabilities.supportsDynamicRatio = true;
  d.capabilities.controlRate.kind = ControlRateSpec::Kind::FixedBlock;
  d.capabilities.controlRate.blockFrames = 512;  // §14 table (the default hop)
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.45;
  d.capabilities.bandwidth.notes =
      "phasiness (vertical phase coherence loss) and transient smearing are the "
      "family artefacts; hop-rate control (only correct for slowly varying "
      "sinusoids); whole-curve-max AA pre-filter before the post-resampler "
      "when rho_max > 1 (shared §7.4 policy)";
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.determinism = Determinism::Deterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
  d.parameterKeys = {"window", "fft_size", "hop"};
  // Engine-owned parameter descriptors (Task 29): fft/hop are product-exposed
  // (discrete Int choices — the plain domain is the choice INDEX, choiceValues
  // holds the engine-facing sizes); window is engine-internal fixed (hann).
  // hop declares the engine-domain constraint hop <= fft_size/2 (§6.3.1
  // item 1) — the product layer clamps to it (the engine contract rejects).
  d.parameters = {
      {
          .key = "fft_size",
          .displayName = "FFT Size",
          .kind = EngineParamKind::Integer,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 2.0,  // choice-index domain
          .defaultPlain = 1.0,  // 2048 (v0.1 default)
          .unit = "",
          .stepCount = 2,
          .choiceNames = kPvFftChoiceNames,
          .choiceValues = kPvFftChoiceValues,
          .choiceCount = 3,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%d",
      },
      {
          .key = "hop",
          .displayName = "Hop",
          .kind = EngineParamKind::Integer,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 3.0,  // choice-index domain
          .defaultPlain = 2.0,  // 512 (v0.1 default)
          .unit = "",
          .stepCount = 3,
          .choiceNames = kPvHopChoiceNames,
          .choiceValues = kPvHopChoiceValues,
          .choiceCount = 4,
          .automatable = false,
          .rebuildsChain = true,
          .constrainKey = "fft_size",
          .constrainDivisor = 2,  // engine domain: hop <= fft_size/2
          .dispFmt = "%d",
      },
      {
          .key = "window",
          .displayName = "Window",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = false,  // engine-internal fixed value (hann, §6.3.1)
          .min = 0.0,
          .max = 0.0,
          .defaultPlain = 0.0,  // choiceNames[0] == "hann"
          .unit = "",
          .stepCount = 0,
          .choiceNames = kPvWindowChoices,
          .choiceValues = nullptr,
          .choiceCount = 1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
      },
  };
  d.factory = &makePvClassicEngine;
  return d;
}

}  // namespace pitchlab
