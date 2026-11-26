#pragma once

// Pitch Lab — the shared PitchEngine wrapper for a 1:1 STREAMING pitch
// shifter library with a FIXED algorithmic latency (2026-11: native.bungee,
// native.stft). The library consumes n input frames and returns exactly n
// output frames per call; its output trails the input by `libLatency()`
// frames. This wrapper implements the v0.1 contract around it:
//
//   * Duration-preserving, time-aligned: output frame m is input frame m.
//     The first libLatency() library frames are discarded; the DECLARED
//     input latency is the engine's bound (>= the library latency), so the
//     renderer's / adapter's zero padding always covers it.
//   * Control rate: FixedBlock (kStreamControlBlock frames) — the ratio is
//     read from the curve at the library's PROCESSING time (the newest
//     input minus processingLag()), so automation lands on the aligned
//     output where it belongs (the native.signalsmith lesson).
//   * finish(): any aligned output still owed is produced by feeding
//     semantic zeros through the library.
//   * reset(): the library is rebuilt (fresh state; allocation happens on
//     the preparation thread, never on the audio thread).
//   * All allocation in prepare()/reset(); process()/finish() do not
//     allocate (the subclass guarantees the same for its library calls).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "core/errors.h"
#include "core/pitch_engine.h"

namespace pitchlab {

inline constexpr int kStreamControlBlock = 128;

class StreamEngineBase : public PitchEngine {
 public:
  void prepare(const ProcessContext& ctx) override {
    if (!(ctx.sampleRate > 0.0) || !std::isfinite(ctx.sampleRate)) {
      throw ConfigError("", "sampleRate", "sample rate must be finite and positive");
    }
    if (ctx.channels < 1 || ctx.channels > 2) {
      throw ConfigError("", "channels", std::string(engineId()) + " supports 1-2 channels");
    }
    if (ctx.maxBlockFrames <= 0 || ctx.totalInputFrames < 1) {
      throw ConfigError("", "prepare", "invalid job geometry (maxBlock/totalInputFrames)");
    }
    if (ctx.curve == nullptr || ctx.curve->ratio == nullptr ||
        ctx.curve->frames != ctx.totalInputFrames) {
      throw ConfigError("", "curve", "curve view missing or frame count mismatch");
    }
    for (FrameCount i = 0; i < ctx.totalInputFrames; ++i) {
      const double r = ctx.curve->ratio[static_cast<std::size_t>(i)];
      if (!std::isfinite(r) || r <= 0.0) {
        throw ConfigError("", "curve", "curve ratio at frame " + std::to_string(i) +
                                           " is non-finite or non-positive (§4.4.1)");
      }
    }
    ctx_ = ctx;
    channels_ = static_cast<int>(ctx.channels);
    for (int c = 0; c < 2; ++c) {
      in_[c].assign(kStreamControlBlock, 0.0);
      out_[c].assign(kStreamControlBlock, 0.0);
    }
    buildLibrary();
    actualLatency_ = libLatency();
    const int64_t bound = latencyBound(ctx.sampleRate);
    declaredLatency_ = std::max(bound, actualLatency_);
    latency_.inputLatencyFrames = declaredLatency_;
    latency_.outputLatencyFrames = declaredLatency_;
    resetStreaming();
    prepared_ = true;
  }

  Latency latency() const override { return latency_; }

  ProcessReport process(const AudioBlockView& in, int inFrames, AudioBlockOut& out,
                        int outCapacity, const PitchCurveView& curve,
                        FrameCount inputFrameIndex) override {
    if (!prepared_) throw EngineException(engineId(), "process() called before prepare()");
    if (inFrames < 0 || outCapacity < 0) {
      throw EngineException(engineId(), "negative inFrames/outCapacity");
    }
    if (inputFrameIndex != consumedTotal_) {
      throw EngineException(engineId(),
                            "inputFrameIndex != cumulative consumed (contract precondition, §4.2)");
    }
    if (in.channelCount != ctx_.channels || out.channelCount != ctx_.channels) {
      throw EngineException(engineId(), "channel count mismatch in block views");
    }
    if (curve.ratio != ctx_.curve->ratio || curve.frames != ctx_.curve->frames) {
      throw EngineException(engineId(), "curve view differs from the prepared one");
    }
    int consumed = 0;
    int produced = 0;
    while (consumed < inFrames) {
      int n = std::min(kStreamControlBlock, inFrames - consumed);
      n = limitByCapacity(n, outCapacity - produced);
      if (n <= 0) break;
      for (int c = 0; c < channels_; ++c) {
        const double* src = in.channels[c] + consumed;
        std::copy(src, src + n, in_[c].data());
      }
      runBlock(n, /*zeros=*/false, out, produced);
      consumed += n;
    }
    const FrameCount streamEnd = ctx_.totalInputFrames + latency_.inputLatencyFrames;
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= streamEnd);
    return ProcessReport{consumed, produced, inputExhausted_, false};
  }

  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    if (!prepared_) throw EngineException(engineId(), "finish() called before prepare()");
    if (finished_) throw EngineException(engineId(), "finish() called twice (contract: exactly once)");
    if (outCapacity < 0) throw EngineException(engineId(), "negative outCapacity");
    if (out.channelCount != ctx_.channels) {
      throw EngineException(engineId(), "channel count mismatch in output view");
    }
    finished_ = true;
    int produced = 0;
    int guard = 0;
    while (outM_ < ctx_.totalInputFrames && produced < outCapacity && guard++ < (1 << 20)) {
      const int n = limitByCapacity(kStreamControlBlock, outCapacity - produced);
      if (n <= 0) break;
      runBlock(n, /*zeros=*/true, out, produced);
    }
    return ProcessReport{0, produced, inputExhausted_, false};
  }

  void reset() override {
    if (!prepared_) return;
    buildLibrary();
    resetStreaming();
  }

 protected:
  /// (Re)create the library for ctx_ (sample rate, channels) — allocation OK.
  virtual void buildLibrary() = 0;
  /// The built library's exact algorithmic latency (frames).
  [[nodiscard]] virtual int64_t libLatency() const = 0;
  /// The engine's closed-form latency bound at a sample rate (>= libLatency).
  [[nodiscard]] virtual int64_t latencyBound(double sampleRate) const = 0;
  /// Frames between the newest input and the content the library is
  /// processing now (the automation index lag).
  [[nodiscard]] virtual int64_t processingLag() const = 0;
  /// Process exactly n frames (in_ -> out_, per channel) at `ratio`.
  /// Must not allocate.
  virtual void libProcess(int n, double ratio) = 0;

  ProcessContext ctx_{};
  int channels_ = 1;
  std::vector<double> in_[2];
  std::vector<double> out_[2];

 private:
  void resetStreaming() {
    consumedTotal_ = 0;
    libOut_ = 0;
    outM_ = 0;
    inputExhausted_ = false;
    finished_ = false;
  }

  [[nodiscard]] int limitByCapacity(int n, int capacityLeft) const {
    const int64_t firstAligned = libOut_ - actualLatency_;
    const int64_t lo = std::max<int64_t>(firstAligned, 0);
    const int64_t hi = std::min<int64_t>(firstAligned + n, ctx_.totalInputFrames);
    const int64_t emit = hi > lo ? hi - lo : 0;
    if (emit <= capacityLeft) return n;
    const int64_t preRoll = std::max<int64_t>(0, -firstAligned);
    return static_cast<int>(std::min<int64_t>(n, preRoll + std::max(capacityLeft, 0)));
  }

  void runBlock(int n, bool zeros, AudioBlockOut& out, int& produced) {
    const FrameCount idx = std::clamp<FrameCount>(consumedTotal_ - processingLag(), 0,
                                                  ctx_.totalInputFrames - 1);
    const double ratio = ctx_.curve->ratio[static_cast<std::size_t>(idx)];
    if (zeros) {
      for (int c = 0; c < channels_; ++c) std::fill(in_[c].begin(), in_[c].begin() + n, 0.0);
    }
    libProcess(n, ratio);
    for (int i = 0; i < n; ++i) {
      const int64_t m = libOut_ + i - actualLatency_;
      if (m < 0 || m >= ctx_.totalInputFrames) continue;
      for (int c = 0; c < channels_; ++c) {
        out.channels[c][produced] = out_[c][static_cast<std::size_t>(i)];
      }
      ++produced;
      ++outM_;
    }
    libOut_ += n;
    if (!zeros) consumedTotal_ += n;
  }

  Latency latency_{};
  int64_t actualLatency_ = 0;
  int64_t declaredLatency_ = 0;
  bool prepared_ = false;
  FrameCount consumedTotal_ = 0;
  int64_t libOut_ = 0;
  FrameCount outM_ = 0;
  bool inputExhausted_ = false;
  bool finished_ = false;
};

}  // namespace pitchlab
