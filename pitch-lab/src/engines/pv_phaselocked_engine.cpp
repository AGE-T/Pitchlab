#include "engines/pv_phaselocked_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// The vendored pocketfft header (external/pocketfft/, BSD-3-Clause,
// ORIGIN.toml-pinned; the documented "lab binary" dependency of build/CI
// spec §7). POCKETFFT_CACHE_SIZE 0: no global plan cache — the engine owns
// its persistent plan object (§6.4.1 item 18); the public convenience
// functions allocate per call and are NOT used in the processing path.
#define POCKETFFT_CACHE_SIZE 0
#include <pocketfft/pocketfft_hdronly.h>

#include "core/engine_registry.h"
#include "core/errors.h"

namespace pitchlab {
namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

/// princarg (§6.4.1 item 5, the §6.3.1 item 5 frozen wrap): wrap to (−π, π]
/// via x − 2π·round(x/2π).
inline double princarg(double x) {
  return x - kTwoPi * static_cast<double>(std::llround(x / kTwoPi));
}

// ---------------------------------------------------------------------------
// Absolute-indexed windows (the §6.3.1 private-TU pattern, item 3/8).
// ---------------------------------------------------------------------------
struct SlidingWindow {  // sequential appends (input stream)
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
    if (count + n > static_cast<int64_t>(data.size())) {
      throw EngineException("native.pv.phaselocked",
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

/// OLA accumulator (§6.4.1 item 11, the §6.3.1 item 8 ring): absolute-indexed,
/// zero-extendable, add-at-position, front-compaction (memmove only — no
/// allocation after prepare()).
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
      throw EngineException("native.pv.phaselocked",
                            "internal accumulator overflow (sizing bug; report as engine defect)");
    }
    for (int64_t i = start + count; i < absEnd; ++i) {
      data[static_cast<std::size_t>(i - start)] = 0.0;
    }
    count = absEnd - start;
  }
  void addAt(int64_t absIdx, double v) {
    if (absIdx < start || absIdx >= start + count) {
      throw EngineException("native.pv.phaselocked",
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
// The engine (§6.4 sheet + §6.4.1 frozen implementation behaviour)
// ---------------------------------------------------------------------------
class PvPhaseLockedEngine final : public PitchEngine {
 public:
  const char* engineId() const override { return "native.pv.phaselocked"; }

  void configure(const EngineConfiguration& cfg) override {
    fftSize_ = kDefaultFftSize;
    if (const ParameterValue* v = cfg.find("fft_size")) {
      if (const auto* i = std::get_if<int64_t>(v)) {
        if (*i < 32 || (*i & (*i - 1)) != 0) {
          throw ConfigError("", "fft_size",
                            "fft_size must be a power of two >= 32 (§6.4.1 item 1)");
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
                        "hop must be in [1, fft_size/2] (>= 50% overlap; §6.4.1 item 1)");
    }
    if (const ParameterValue* v = cfg.find("window")) {
      if (const auto* s = std::get_if<std::string>(v)) {
        if (*s != "hann") {
          throw ConfigError("", "window", "unknown window '" + *s + "' (hann, §6.4.1 item 1)");
        }
      } else {
        throw ConfigError("", "window", "window must be a string (hann)");
      }
    }
    if (const ParameterValue* v = cfg.find("locking_mode")) {
      if (const auto* s = std::get_if<std::string>(v)) {
        if (*s != "identity") {
          throw ConfigError("", "locking_mode",
                            "unknown locking_mode '" + *s +
                                "' — v0.1 implements identity locking only (scaled locking "
                                "is specified but NOT implemented; §6.4.1 item 1)");
        }
      } else {
        throw ConfigError("", "locking_mode", "locking_mode must be a string (identity)");
      }
    }
    // The seed is accepted and unused (Deterministic, §6.4.1 item 1).
    prepared_ = false;
  }

  void prepare(const ProcessContext& ctx) override {
    if (!(ctx.sampleRate > 0.0) || !std::isfinite(ctx.sampleRate)) {
      throw ConfigError("", "sampleRate", "sample rate must be finite and positive");
    }
    if (ctx.channels < 1 || ctx.channels > 2) {
      throw ConfigError("", "channels",
                        "native.pv.phaselocked supports 1-2 channels (declared MonoAndStereo)");
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
    // The §6.3.1-item-16-style prepare-time curve scan (§6.4.1 item 16):
    // every effective ratio finite and strictly positive.
    const double* ratio = ctx.curve->ratio;
    for (FrameCount i = 0; i < ctx.totalInputFrames; ++i) {
      const double v = ratio[static_cast<std::size_t>(i)];
      if (!std::isfinite(v) || v <= 0.0) {
        throw ConfigError("", "curve", "curve ratio at frame " + std::to_string(i) +
                                           " is non-finite or non-positive (§4.4.1)");
      }
    }

    ctx_ = ctx;
    N_ = fftSize_;
    H_ = hop_;
    bins_ = N_ / 2 + 1;
    latency_.inputLatencyFrames = N_ + H_;  // sheet verbatim ("As pv.classic")
    latency_.outputLatencyFrames = N_;

    // Window tables (§6.4.1 item 11): analysis, synthesis, product.
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

    // The persistent FFT plan (§6.4.1 item 18) and per-channel spectral state.
    plan_ = std::make_unique<pocketfft::detail::pocketfft_r<double>>(
        static_cast<std::size_t>(N_));
    fftBuf_.assign(static_cast<std::size_t>(N_), 0.0);
    const std::size_t ch = static_cast<std::size_t>(ctx.channels);
    const std::size_t nb = static_cast<std::size_t>(bins_);
    re_.assign(ch * nb, 0.0);       // current analysis spectrum (complex)
    im_.assign(ch * nb, 0.0);
    mag_.assign(ch * nb, 0.0);      // magnitudes (peak detection)
    prevPhase_.assign(ch * nb, 0.0);  // previous frame's phases (IF)
    zRe_.assign(ch * nb, 0.0);      // the cumulated rotation Z (per bin)
    zIm_.assign(ch * nb, 0.0);
    sRe_.assign(ch * nb, 0.0);      // per-frame synthesis spectrum
    sIm_.assign(ch * nb, 0.0);
    havePrev_.assign(ch, 0);
    // Peak/region scratch (fixed capacity; reused per channel — §6.4.1 items
    // 6/7: one ascending scan, bins_ is a safe upper bound for the count).
    peakBins_.assign(nb, 0);
    peakCount_ = 0;

    // Buffers: the input sliding windows and the OLA rings. Span bounds
    // (§6.4.1 items 12/13): the output read trails the synthesis head by at
    // most the latency + a max block in flight (process) / N + H (finish);
    // the input window holds [aNext_, consumedTotal) < N + maxBlock.
    const int64_t inCap = static_cast<int64_t>(ctx.maxBlockFrames) + 2 * N_ + 256;
    const int64_t accSpan =
        static_cast<int64_t>(ctx.maxBlockFrames) + 2 * N_ + H_ + 512;
    inputWin_.resize(ch);
    ola_.resize(ch);
    for (ChannelCount c = 0; c < ctx.channels; ++c) {
      inputWin_[static_cast<std::size_t>(c)].allocate(inCap, 0);
      ola_[static_cast<std::size_t>(c)].allocate(accSpan, 0);
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
      throw EngineException("native.pv.phaselocked", "process() called before prepare()");
    }
    if (inFrames < 0 || outCapacity < 0) {
      throw EngineException("native.pv.phaselocked", "negative inFrames/outCapacity");
    }
    if (inputFrameIndex != consumedTotal_) {
      throw EngineException("native.pv.phaselocked",
                            "inputFrameIndex != cumulative consumed (contract precondition, §4.2)");
    }
    if (in.channelCount != ctx_.channels || out.channelCount != ctx_.channels) {
      throw EngineException("native.pv.phaselocked", "channel count mismatch in block views");
    }
    if (curve.ratio != ctx_.curve->ratio || curve.frames != ctx_.curve->frames) {
      throw EngineException("native.pv.phaselocked",
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

    // ---- 2) produce output frames m ∈ [0, N_in) (§6.4.1 item 13) ----
    const int produced = produceInto(out, outCapacity, /*finishMode=*/false);

    // ---- 3) exhaustion flag (§4.2.1 item 7) ----
    const FrameCount streamEnd = ctx_.totalInputFrames + latency_.inputLatencyFrames;
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= streamEnd);

    return ProcessReport{consumed, produced, inputExhausted_};
  }

  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    if (!prepared_) {
      throw EngineException("native.pv.phaselocked", "finish() called before prepare()");
    }
    if (finished_) {
      throw EngineException("native.pv.phaselocked", "finish() called twice (contract: exactly once)");
    }
    if (outCapacity < 0) {
      throw EngineException("native.pv.phaselocked", "negative outCapacity");
    }
    if (out.channelCount != ctx_.channels) {
      throw EngineException("native.pv.phaselocked", "channel count mismatch in output view");
    }
    finished_ = true;

    // The tail [N_in, N_in + N) — exactly N frames (§6.4.1 item 13); the
    // analysis completes with semantic-zero taps beyond the padded stream.
    const int produced = produceInto(out, outCapacity, /*finishMode=*/true);
    return ProcessReport{0, produced, inputExhausted_};
  }

  void reset() override {
    if (!prepared_) return;
    for (ChannelCount c = 0; c < ctx_.channels; ++c) {
      inputWin_[static_cast<std::size_t>(c)].allocate(
          inputWin_[static_cast<std::size_t>(c)].capacity(), 0);
      ola_[static_cast<std::size_t>(c)].allocate(
          static_cast<int64_t>(ola_[static_cast<std::size_t>(c)].data.size()), 0);
    }
    winSum_.allocate(static_cast<int64_t>(winSum_.data.size()), 0);
    resetStreaming();
  }

 private:
  // ---- configuration (§6.4.1 item 1) ----
  static constexpr int64_t kDefaultFftSize = 2048;
  static constexpr int64_t kDefaultHop = 512;
  int64_t fftSize_ = kDefaultFftSize;
  int64_t hop_ = kDefaultHop;

  // ---- per-job state ----
  ProcessContext ctx_{};
  int64_t N_ = 0, H_ = 0, bins_ = 0;
  Latency latency_{};
  std::vector<double> wA_, wS_, wP_, nominal_;
  std::unique_ptr<pocketfft::detail::pocketfft_r<double>> plan_;
  std::vector<double> fftBuf_;
  std::vector<double> re_, im_, mag_, prevPhase_, zRe_, zIm_, sRe_, sIm_;
  std::vector<char> havePrev_;
  std::vector<int64_t> peakBins_;
  int64_t peakCount_ = 0;
  std::vector<SlidingWindow> inputWin_;  // per channel (analysis input)
  std::vector<OlapWindow> ola_;          // per channel (synthesis signal ring)
  OlapWindow winSum_;                    // engine-level (window-product ring)
  bool prepared_ = false;

  // ---- streaming state ----
  // The analysis grid and the synthesis grid are THE SAME grid: aNext_ ==
  // sCur_ == n_·H at every step (§6.4.1 items 2/11). Both are kept as
  // separate named members to mirror the pv.classic structure and make the
  // gating conditions read as their own invariants.
  int64_t n_ = 0;         // the next analysis frame index
  int64_t aNext_ = 0;     // = n_·H (the next analysis position)
  int64_t sCur_ = 0;      // the next synthesis placement position s_n
  int64_t outM_ = 0;      // the next output frame
  FrameCount consumedTotal_ = 0;
  bool inputExhausted_ = false;
  bool finished_ = false;
  FrameCount flushEmitted_ = 0;

  void resetStreaming() {
    n_ = 0;
    aNext_ = 0;
    sCur_ = 0;
    outM_ = 0;
    consumedTotal_ = 0;
    inputExhausted_ = false;
    finished_ = false;
    flushEmitted_ = 0;
    std::fill(havePrev_.begin(), havePrev_.end(), 0);
    // Z initialised to 1 at reset (§6.4.1 item 8).
    std::fill(zRe_.begin(), zRe_.end(), 1.0);
    std::fill(zIm_.begin(), zIm_.end(), 0.0);
    peakCount_ = 0;
  }

  /// Process analysis frame n (§6.4.1 items 3-11): one STFT frame per
  /// channel, peak detection, region assignment, the rigid rotation +
  /// translated placement, IFFT, OLA into the rings. Requires the frame's
  /// input region delivered (or finishMode: taps beyond the stream read
  /// semantic zeros).
  void processAnalysisFrame(bool finishMode) {
    const double* ratio = ctx_.curve->ratio;
    const FrameCount nIn = ctx_.totalInputFrames;
    const int64_t a = aNext_;                      // the frame's analysis start
    const int64_t half = N_ / 2;
    const int64_t lastBin = half - 1;              // the interior band top
    // §6.4.1 item 4: rho at the analysis boundary (the shared Preserving
    // timeline position); the ratio belongs to the CURRENT frame.
    const int64_t idx = std::min<int64_t>(a, static_cast<int64_t>(nIn) - 1);
    const double rho = ratio[static_cast<std::size_t>(idx)];

    for (ChannelCount c = 0; c < ctx_.channels; ++c) {
      const std::size_t chOff =
          static_cast<std::size_t>(c) * static_cast<std::size_t>(bins_);
      const SlidingWindow& win = inputWin_[static_cast<std::size_t>(c)];

      // ---- windowed analysis frame (semantic zeros beyond the stream) ----
      for (int64_t j = 0; j < N_; ++j) {
        const int64_t p = a + j;
        double x = 0.0;
        if (p >= win.start && p < win.end()) {
          x = win.at(p);
        } else if (p >= win.end()) {
          // Beyond the delivered stream: semantic zeros (§7.6) — legal in
          // finish mode only (process mode is gated on full delivery).
          if (!finishMode) {
            throw EngineException("native.pv.phaselocked",
                                  "internal analysis window underrun (gating bug; report as "
                                  "engine defect)");
          }
        } else {
          throw EngineException("native.pv.phaselocked",
                                "internal analysis window drop-policy bug; report as engine "
                                "defect");
        }
        fftBuf_[static_cast<std::size_t>(j)] = wA_[static_cast<std::size_t>(j)] * x;
      }
      // ---- forward transform (packed halfcomplex; unnormalised) ----
      plan_->exec(fftBuf_.data(), 1.0, true);
      // ---- unpack: complex values + magnitudes + current phases ----
      for (int64_t k = 0; k < bins_; ++k) {
        double re, im;
        if (k == 0) {
          re = fftBuf_[0];
          im = 0.0;
        } else if (k == half) {
          re = fftBuf_[static_cast<std::size_t>(N_ - 1)];
          im = 0.0;
        } else {
          re = fftBuf_[static_cast<std::size_t>(2 * k - 1)];
          im = fftBuf_[static_cast<std::size_t>(2 * k)];
        }
        re_[chOff + static_cast<std::size_t>(k)] = re;
        im_[chOff + static_cast<std::size_t>(k)] = im;
        mag_[chOff + static_cast<std::size_t>(k)] = std::hypot(re, im);
      }
      const bool havePrev = havePrev_[static_cast<std::size_t>(c)] != 0;

      // ---- build the synthesis spectrum (§6.4.1 items 7-10) ----
      if (!havePrev) {
        // Frame 0: VERBATIM (the one-frame transient; Z stays 1). The
        // previous-frame phases are recorded for frame 1's IF estimation.
        for (int64_t k = 0; k < bins_; ++k) {
          const std::size_t kk = chOff + static_cast<std::size_t>(k);
          sRe_[kk] = re_[kk];
          sIm_[kk] = im_[kk];
          prevPhase_[kk] = std::atan2(im_[kk], re_[kk]);
        }
        havePrev_[static_cast<std::size_t>(c)] = 1;
      } else {
        // Zero the synthesis spectrum; DC/Nyquist verbatim (item 10).
        for (int64_t k = 0; k < bins_; ++k) {
          const std::size_t kk = chOff + static_cast<std::size_t>(k);
          sRe_[kk] = 0.0;
          sIm_[kk] = 0.0;
        }
        sRe_[chOff] = re_[chOff];
        sIm_[chOff] = 0.0;
        sRe_[chOff + static_cast<std::size_t>(half)] = re_[chOff + static_cast<std::size_t>(half)];
        sIm_[chOff + static_cast<std::size_t>(half)] = 0.0;

        // ---- peak detection (item 6): one ascending scan, strict both
        // sides, candidates [1, N/2−1] (DC/Nyquist are neighbours only). ----
        const double* m = mag_.data() + chOff;
        peakCount_ = 0;
        for (int64_t k = 1; k <= lastBin; ++k) {
          if (m[static_cast<std::size_t>(k)] > m[static_cast<std::size_t>(k - 1)] &&
              m[static_cast<std::size_t>(k)] > m[static_cast<std::size_t>(k + 1)]) {
            peakBins_[static_cast<std::size_t>(peakCount_++)] = k;
          }
        }

        if (peakCount_ == 0) {
          // M = 0: the verbatim fallback (item 7) — no regions, no locking,
          // no translation; the interior passes the analysis values.
          for (int64_t k = 1; k <= lastBin; ++k) {
            const std::size_t kk = chOff + static_cast<std::size_t>(k);
            sRe_[kk] = re_[kk];
            sIm_[kk] = im_[kk];
          }
        } else {
          // ---- regions (item 7): b_i = floor((k_i + k_{i+1})/2); region_i
          // = [b_{i-1}+1, b_i]; b_0 = 0, b_M = N/2−1 (ties go LEFT). ----
          // ---- per region (item 8): read Z at the peak, rotate by
          // e^{j·Δω·H}, synthesize X·z at l+δ (item 9), propagate Z. ----
          int64_t lo = 1;  // b_0 + 1
          for (int64_t i = 0; i < peakCount_; ++i) {
            const int64_t kPeak = peakBins_[static_cast<std::size_t>(i)];
            const int64_t hi =
                (i + 1 < peakCount_)
                    ? (kPeak + peakBins_[static_cast<std::size_t>(i + 1)]) / 2
                    : lastBin;
            // IF at the peak bin (item 5, the frozen expression order).
            const std::size_t kp = chOff + static_cast<std::size_t>(kPeak);
            const double phi = std::atan2(im_[kp], re_[kp]);
            const double dphi = princarg(phi - prevPhase_[kp] -
                                         nominal_[static_cast<std::size_t>(kPeak)]);
            const double omega = kTwoPi * static_cast<double>(kPeak) /
                                     static_cast<double>(N_) +
                                 dphi / static_cast<double>(H_);
            // The frequency translation (item 8b) and its bin equivalent
            // (item 9: deltaBins = deltaOmega·N/2π, the frozen order).
            const double deltaOmega = (rho - 1.0) * omega;
            const double deltaBins = deltaOmega * static_cast<double>(N_) / kTwoPi;
            // The rotation update (item 8c): z ← z·e^{jΔω·H}.
            const double theta = deltaOmega * static_cast<double>(H_);
            const double cs = std::cos(theta), sn = std::sin(theta);
            const double zr = zRe_[kp] * cs - zIm_[kp] * sn;
            const double zi = zRe_[kp] * sn + zIm_[kp] * cs;
            // Synthesize + propagate (single pass, the frozen order).
            for (int64_t l = lo; l <= hi; ++l) {
              const std::size_t ll = chOff + static_cast<std::size_t>(l);
              // X'_l = X_l · z (the rigid rotation — item 8).
              const double vr = re_[ll] * zr - im_[ll] * zi;
              const double vi = re_[ll] * zi + im_[ll] * zr;
              // Placement t = l + δ (item 9): linear interpolation, floor
              // target first, per-target-bin range check to [1, N/2−1].
              const double t = static_cast<double>(l) + deltaBins;
              const double ft = std::floor(t);
              const int64_t k0 = static_cast<int64_t>(ft);
              const double f = t - ft;
              if (k0 >= 1 && k0 <= lastBin) {
                const std::size_t t0 = chOff + static_cast<std::size_t>(k0);
                sRe_[t0] += (1.0 - f) * vr;
                sIm_[t0] += (1.0 - f) * vi;
              }
              if (f > 0.0) {
                const int64_t k1 = k0 + 1;
                if (k1 >= 1 && k1 <= lastBin) {
                  const std::size_t t1 = chOff + static_cast<std::size_t>(k1);
                  sRe_[t1] += f * vr;
                  sIm_[t1] += f * vi;
                }
              }
              // Z propagation (item 8d): the region's bins receive the
              // rotated value (what the next frame's peaks read).
              zRe_[ll] = zr;
              zIm_[ll] = zi;
            }
            lo = hi + 1;
          }
        }

        // Record the previous-frame phases for the NEXT frame's IF (the
        // per-bin analysis state — item 5).
        for (int64_t k = 0; k < bins_; ++k) {
          const std::size_t kk = chOff + static_cast<std::size_t>(k);
          prevPhase_[kk] = std::atan2(im_[kk], re_[kk]);
        }
      }

      // ---- pack the synthesis spectrum (c2r, factor 1/N; the exact mirror
      // of the r2h unpack — slots [0, N) covered exactly once) ----
      for (int64_t k = 0; k < bins_; ++k) {
        const std::size_t kk = chOff + static_cast<std::size_t>(k);
        const double vr = sRe_[kk];
        const double vi = sIm_[kk];
        if (k == 0) {
          fftBuf_[0] = vr;
        } else if (k == half) {
          fftBuf_[static_cast<std::size_t>(N_ - 1)] = vr;
        } else {
          fftBuf_[static_cast<std::size_t>(2 * k - 1)] = vr;
          fftBuf_[static_cast<std::size_t>(2 * k)] = vi;
        }
      }
      plan_->exec(fftBuf_.data(), 1.0 / static_cast<double>(N_), false);
      // ---- windowed OLA at [sCur_, sCur_+N) (item 11) ----
      OlapWindow& acc = ola_[static_cast<std::size_t>(c)];
      acc.extendTo(sCur_ + N_);
      for (int64_t j = 0; j < N_; ++j) {
        acc.addAt(sCur_ + j, wS_[static_cast<std::size_t>(j)] *
                                 fftBuf_[static_cast<std::size_t>(j)]);
      }
    }
    // The window-sum ring (engine-level, shared grid; item 11).
    winSum_.extendTo(sCur_ + N_);
    for (int64_t j = 0; j < N_; ++j) {
      winSum_.addAt(sCur_ + j, wP_[static_cast<std::size_t>(j)]);
    }

    // The grid advance: analysis and synthesis advance TOGETHER (item 2).
    sCur_ += H_;
    ++n_;
    aNext_ += H_;
  }

  /// Produce output frames (§6.4.1 items 12/13). The analysis advances
  /// FIRST — as far as the input delivery permits and the synthesis need
  /// horizon allows, INDEPENDENT of the output cap (the pv.classic lesson:
  /// an advance placed behind the output-cap break froze the synthesis and
  /// deadlocked the padded-stream consumption at small blocks); then the
  /// production loop with the OLA completion gate `sCur_ > m` (the other
  /// pv.classic lesson: gate on the COMPLETION boundary, never on a
  /// sequential-stream assumption).
  int produceInto(AudioBlockOut& out, int outCapacity, bool finishMode) {
    const FrameCount nIn = ctx_.totalInputFrames;
    const FrameCount outputEnd = nIn + N_;
    // The synthesis need horizon (item 13): frames with a_n < N_in + N.
    // Process mode additionally requires full delivery of the frame's input.
    while (aNext_ < static_cast<int64_t>(outputEnd) &&
           (finishMode || aNext_ + N_ <= consumedTotal_)) {
      processAnalysisFrame(finishMode);
    }
    if (outCapacity <= 0) {
      // Still advance (the state must progress even when the renderer offers
      // no output capacity this call).
      dropPass();
      return 0;
    }

    int produced = 0;
    while (outM_ < static_cast<int64_t>(outputEnd) && produced < outCapacity) {
      const int64_t m = outM_;
      if (!finishMode && m >= static_cast<int64_t>(nIn)) break;  // m < N_in only
      if (finishMode && flushEmitted_ >= static_cast<FrameCount>(N_)) break;  // defensive

      // ---- OLA completion gate (item 12): cell m is complete iff the
      // synthesis head has passed it (all frames covering m are placed). ----
      if (sCur_ <= m) {
        if (finishMode) {
          // In finish mode the analysis loop has already run to the need
          // horizon; an incomplete cell here is an internal invariant bug.
          throw EngineException("native.pv.phaselocked",
                                "internal finish-mode completion underrun (gating bug; report "
                                "as engine defect)");
        }
        break;  // more input needed before cell m is complete
      }

      // ---- integer read + guarded normalisation (item 12) ----
      const double yWin = winSum_.at(m);
      for (ChannelCount c = 0; c < ctx_.channels; ++c) {
        const OlapWindow& acc = ola_[static_cast<std::size_t>(c)];
        const double ySig = acc.at(m);
        out.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(produced)] =
            (std::fabs(yWin) > 1e-12) ? (ySig / yWin) : 0.0;
      }
      ++produced;
      ++outM_;
      if (finishMode) ++flushEmitted_;
    }

    dropPass();
    return produced;
  }

  /// The retention-floor drop pass (§6.4.1 items 11/12: the read floor is the
  /// output position minus a margin — integer reads need no kernel radius).
  void dropPass() {
    const int64_t readFloor = outM_ - 8;
    for (ChannelCount c = 0; c < ctx_.channels; ++c) {
      inputWin_[static_cast<std::size_t>(c)].dropBefore(aNext_);
      ola_[static_cast<std::size_t>(c)].dropBefore(readFloor);
    }
    winSum_.dropBefore(readFloor);
  }
};

}  // namespace

std::unique_ptr<PitchEngine> makePvPhaseLockedEngine() {
  return std::make_unique<PvPhaseLockedEngine>();
}

// Engine-owned discrete choices (Task 29): the same FFT/hop domains as
// pv.classic (§6.4.1 item 1), the window (hann) and the locking mode
// (identity — §6.4.1; engine-internal fixed values for both).
const char* const kPvpFftChoiceNames[] = {"1024", "2048", "4096"};
const double kPvpFftChoiceValues[] = {1024.0, 2048.0, 4096.0};
const char* const kPvpHopChoiceNames[] = {"128", "256", "512", "1024"};
const double kPvpHopChoiceValues[] = {128.0, 256.0, 512.0, 1024.0};
const char* const kPvpWindowChoices[] = {"hann"};
const char* const kPvpLockingChoices[] = {"identity"};

EngineDescriptor pvPhaseLockedEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = "native.pv.phaselocked";
  d.info.displayName = "Phase vocoder — phase-locked (L-D'99 peak shift)";
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
      "identity locking removes phasiness, not transient smearing; per-region "
      "frequency-domain translation truncates above Nyquist and below DC "
      "(no wrap, no clamp — the honest band); linear sideband interpolation "
      "at fractional shifts (L-D'99 family behaviour); no resampler and no "
      "AA pre-filter in this engine (duration-preserving by construction)";
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.determinism = Determinism::Deterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
  d.parameterKeys = {"window", "fft_size", "hop", "locking_mode"};
  // Engine-owned parameter descriptors (Task 29): fft/hop are product-exposed
  // (discrete Int choices — the plain domain is the choice INDEX,
  // choiceValues holds the engine-facing sizes); window and locking_mode are
  // engine-internal fixed (hann / identity). hop declares the engine-domain
  // constraint hop <= fft_size/2 (§6.4.1 item 1).
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
          .choiceNames = kPvpFftChoiceNames,
          .choiceValues = kPvpFftChoiceValues,
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
          .choiceNames = kPvpHopChoiceNames,
          .choiceValues = kPvpHopChoiceValues,
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
          .exposed = false,  // engine-internal fixed value (hann, §6.4.1)
          .min = 0.0,
          .max = 0.0,
          .defaultPlain = 0.0,  // choiceNames[0] == "hann"
          .unit = "",
          .stepCount = 0,
          .choiceNames = kPvpWindowChoices,
          .choiceValues = nullptr,
          .choiceCount = 1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
      },
      {
          .key = "locking_mode",
          .displayName = "Locking Mode",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = false,  // engine-internal fixed value (identity, §6.4.1)
          .min = 0.0,
          .max = 0.0,
          .defaultPlain = 0.0,  // choiceNames[0] == "identity"
          .unit = "",
          .stepCount = 0,
          .choiceNames = kPvpLockingChoices,
          .choiceValues = nullptr,
          .choiceCount = 1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
      },
  };
  d.factory = &makePvPhaseLockedEngine;
  return d;
}

}  // namespace pitchlab
