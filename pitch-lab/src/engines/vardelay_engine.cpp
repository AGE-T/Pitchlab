#include "engines/vardelay_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/engine_registry.h"
#include "core/errors.h"
#include "core/resampler.h"

namespace pitchlab {
namespace {

// ---------------------------------------------------------------------------
// §6.2.1 item 5: absolute-indexed sliding retention window (the sheet's
// "circular delay buffer" in honest general form; the varispeed §6.1.1
// item 8 pattern, private to this TU). Pre-allocated in prepare();
// front-compaction via memmove only — NO allocation after prepare().
// Invariant: [start, start+count); front-drops PRESERVE the window end.
// ---------------------------------------------------------------------------
struct SlidingWindow {
  std::vector<double> data;
  int64_t start = 0;  // absolute stream index of data[0]
  int64_t count = 0;  // valid frames [start, start+count)

  void allocate(int64_t capacity, int64_t startIdx) {
    data.assign(static_cast<std::size_t>(capacity), 0.0);
    start = startIdx;
    count = 0;
  }

  /// Append n frames at [start+count, start+count+n). Throws EngineException
  /// on overflow (unreachable by the sizing + drop policy — never silent).
  void append(const double* src, int64_t n) {
    if (n <= 0) return;
    if (count + n > static_cast<int64_t>(data.size())) {
      throw EngineException(
          "native.vardelay",
          "internal retention window overflow (sizing bug; report as engine defect)");
    }
    std::memcpy(data.data() + count, src, sizeof(double) * static_cast<std::size_t>(n));
    count += n;
  }

  /// Drop frames below `absIdx` (front compaction; preserves the end).
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

// ---------------------------------------------------------------------------
// The engine (§6.2 sheet + §6.2.1 frozen implementation behaviour)
// ---------------------------------------------------------------------------
class VardelayEngine final : public PitchEngine {
 public:
  const char* engineId() const override { return "native.vardelay"; }

  void configure(const EngineConfiguration& cfg) override {
    excursionSeconds_ = kDefaultExcursionSeconds;
    if (const ParameterValue* v = cfg.find("excursion_seconds")) {
      if (const auto* d = std::get_if<double>(v)) {
        if (!std::isfinite(*d) || *d <= 0.0) {
          throw ConfigError("", "excursion_seconds",
                            "excursion_seconds must be finite and > 0 (§6.2.1 item 1)");
        }
        excursionSeconds_ = *d;
      } else {
        throw ConfigError("", "excursion_seconds", "excursion_seconds must be a number");
      }
    }
    crossfadeFrames_ = kDefaultCrossfadeFrames;
    if (const ParameterValue* v = cfg.find("crossfade_frames")) {
      if (const auto* i = std::get_if<int64_t>(v)) {
        if (*i < 0) {
          throw ConfigError("", "crossfade_frames",
                            "crossfade_frames must be >= 0 (§6.2.1 item 1)");
        }
        crossfadeFrames_ = *i;
      } else {
        throw ConfigError("", "crossfade_frames",
                          "crossfade_frames must be an integer (frame count at the job rate)");
      }
    }
    if (const ParameterValue* v = cfg.find("read_kernel")) {
      if (const auto* s = std::get_if<std::string>(v)) {
        if (*s != "small-sinc") {
          throw ConfigError("", "read_kernel",
                            "unknown kernel '" + *s + "' (small-sinc, §6.2.1 item 1)");
        }
      } else {
        throw ConfigError("", "read_kernel", "read_kernel must be a string (small-sinc)");
      }
    }
    // The seed is accepted and unused (Deterministic engine, §6.2.1 item 1).
    prepared_ = false;  // a configuration change invalidates prepared state
  }

  void prepare(const ProcessContext& ctx) override {
    if (!(ctx.sampleRate > 0.0) || !std::isfinite(ctx.sampleRate)) {
      throw ConfigError("", "sampleRate", "sample rate must be finite and positive");
    }
    if (ctx.channels < 1 || ctx.channels > 2) {
      throw ConfigError("", "channels",
                        "native.vardelay supports 1-2 channels (declared MonoAndStereo)");
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

    // §4.4.3.1 item 12 guarantees finite/positive upstream, but the engine is
    // independently reusable: verify while scanning s_max (§6.2.1 item 5).
    const double* ratio = ctx.curve->ratio;
    double sMax = 0.0;
    for (FrameCount i = 0; i < ctx.totalInputFrames; ++i) {
      const double v = ratio[static_cast<std::size_t>(i)];
      if (!std::isfinite(v) || v <= 0.0) {
        throw ConfigError("", "curve", "curve ratio at frame " + std::to_string(i) +
                                           " is non-finite or non-positive (§4.4.1)");
      }
      sMax = std::max(sMax, std::fabs(1.0 - v));
    }

    ctx_ = ctx;
    E_ = std::llround(excursionSeconds_ * ctx.sampleRate);  // §6.2.1 item 1
    if (E_ < 1) {
      throw ConfigError("", "excursion_seconds",
                        "excursion_seconds converts to less than one frame at this sample rate "
                        "(§6.2.1 item 1: E >= 1 required)");
    }
    W_ = crossfadeFrames_;
    k_ = resampleKernelSpec(ResampleQuality::Small).halfWidthTaps;  // K = 8
    sMax_ = sMax;
    excursionBudget_ =
        static_cast<int64_t>(std::ceil(static_cast<double>(W_) * sMax_));  // w bound
    latency_.inputLatencyFrames = E_ + k_;   // §6.2.1 item 6 (sheet verbatim)
    latency_.outputLatencyFrames = W_;

    // Retention windows (§6.2.1 item 5, the 2E end-state span).
    const int64_t windowCapacity =
        static_cast<int64_t>(ctx.maxBlockFrames) + 2 * E_ + excursionBudget_ + 2 * k_ + 128;
    windows_.resize(static_cast<std::size_t>(ctx.channels));
    for (ChannelCount c = 0; c < ctx.channels; ++c) {
      windows_[static_cast<std::size_t>(c)].allocate(windowCapacity, 0);
    }
    resetStreaming();
    prepared_ = true;
  }

  Latency latency() const override { return latency_; }

  ProcessReport process(const AudioBlockView& in, int inFrames, AudioBlockOut& out,
                        int outCapacity, const PitchCurveView& curve,
                        FrameCount inputFrameIndex) override {
    if (!prepared_) {
      throw EngineException("native.vardelay", "process() called before prepare()");
    }
    if (inFrames < 0 || outCapacity < 0) {
      throw EngineException("native.vardelay", "negative inFrames/outCapacity");
    }
    if (inputFrameIndex != consumedTotal_) {
      throw EngineException("native.vardelay",
                            "inputFrameIndex != cumulative consumed (contract precondition, §4.2)");
    }
    if (in.channelCount != ctx_.channels || out.channelCount != ctx_.channels) {
      throw EngineException("native.vardelay", "channel count mismatch in block views");
    }
    if (curve.ratio != ctx_.curve->ratio || curve.frames != ctx_.curve->frames) {
      throw EngineException("native.vardelay",
                            "curve view differs from the prepared one (contract: same object)");
    }

    // ---- 1) consume the offered input (consume-all; §6.2.1 item 9) ----
    int64_t consumed = 0;
    if (inFrames > 0) {
      compactWindows();
      int64_t space = windows_[0].freeSpace();
      for (ChannelCount c = 1; c < ctx_.channels; ++c) {
        space = std::min(space, windows_[static_cast<std::size_t>(c)].freeSpace());
      }
      consumed = std::min<int64_t>(inFrames, space);
      if (consumed > 0) {
        for (ChannelCount c = 0; c < ctx_.channels; ++c) {
          windows_[static_cast<std::size_t>(c)].append(in.channels[static_cast<std::size_t>(c)],
                                                       consumed);
        }
        consumedTotal_ += consumed;
      }
    }

    // ---- 2) produce output frames t ∈ [0, N_in) (§6.2.1 item 7) ----
    const int produced = produceInto(out, outCapacity, /*finishMode=*/false);

    // ---- 3) exhaustion flag (§4.2.1 item 7) ----
    const FrameCount streamEnd = ctx_.totalInputFrames + latency_.inputLatencyFrames;
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= streamEnd);

    return ProcessReport{consumed, produced, inputExhausted_};
  }

  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    if (!prepared_) {
      throw EngineException("native.vardelay", "finish() called before prepare()");
    }
    if (finished_) {
      throw EngineException("native.vardelay", "finish() called twice (contract: exactly once)");
    }
    if (outCapacity < 0) {
      throw EngineException("native.vardelay", "negative outCapacity");
    }
    if (out.channelCount != ctx_.channels) {
      throw EngineException("native.vardelay", "channel count mismatch in output view");
    }
    finished_ = true;

    // The tail t ∈ [N_in, N_in + W) — exactly W frames, bounded by the
    // declared outputLatencyFrames by construction (§6.2.1 item 7); reads
    // beyond the delivered end are §7.6 semantic zeros (interpolateAt's
    // zero padding beyond frameCount).
    const int produced = produceInto(out, outCapacity, /*finishMode=*/true);
    return ProcessReport{0, produced, inputExhausted_};
  }

  void reset() override {
    if (!prepared_) return;  // configuration only — nothing to reset
    for (ChannelCount c = 0; c < ctx_.channels; ++c) {
      windows_[static_cast<std::size_t>(c)].allocate(
          windows_[static_cast<std::size_t>(c)].capacity(), 0);
    }
    resetStreaming();
  }

 private:
  // ---- configuration (§6.2.1 item 1) ----
  static constexpr double kDefaultExcursionSeconds = 0.5;
  static constexpr int64_t kDefaultCrossfadeFrames = 2048;
  double excursionSeconds_ = kDefaultExcursionSeconds;
  int64_t crossfadeFrames_ = kDefaultCrossfadeFrames;

  // ---- per-job state (prepare()) ----
  ProcessContext ctx_{};
  int64_t E_ = 0;                // excursion in frames (§6.2.1 item 1)
  int64_t W_ = 0;                // crossfade length in frames (verbatim)
  int k_ = 0;                    // kernel half-width (Small preset, K = 8)
  double sMax_ = 0.0;            // max |1 − ratio| over the effective curve
  int64_t excursionBudget_ = 0;  // ceil(W · s_max): fade excursion bound
  Latency latency_{};
  std::vector<SlidingWindow> windows_;  // per channel
  bool prepared_ = false;

  // ---- streaming state (the §6.2.1 machine) ----
  double v_ = 0.0;        // the wrapped active delay, v(0) = 0 (item 2)
  int64_t outT_ = 0;      // next output frame to produce
  FrameCount consumedTotal_ = 0;
  bool inputExhausted_ = false;
  bool finished_ = false;
  FrameCount flushEmitted_ = 0;  // defensive flush cap (item 7)

  // ---- the active crossfade (§6.2.1 item 4: at most ONE) ----
  bool fadeActive_ = false;
  double fadeOld_ = 0.0;  // pre-wrap branch delay trajectory (independent)
  double fadeNew_ = 0.0;  // post-wrap branch delay trajectory
  int64_t fadeN_ = 0;     // fade length in frames (n)
  int64_t fadeJ_ = 0;     // fade position (0-based; j = 0..n−1)

  void resetStreaming() {
    v_ = 0.0;
    outT_ = 0;
    consumedTotal_ = 0;
    inputExhausted_ = false;
    finished_ = false;
    flushEmitted_ = 0;
    fadeActive_ = false;
    fadeOld_ = 0.0;
    fadeNew_ = 0.0;
    fadeN_ = 0;
    fadeJ_ = 0;
  }

  /// Retention floor: the lowest tap any FUTURE read needs, minus margin
  /// (§6.2.1 item 5): future frames t' ≥ outT_ read at delay ≤ E + w.
  [[nodiscard]] int64_t retentionFloor() const {
    return outT_ - E_ - excursionBudget_ - k_ - 8;
  }

  void compactWindows() {
    const int64_t floor = retentionFloor();
    for (ChannelCount c = 0; c < ctx_.channels; ++c) {
      windows_[static_cast<std::size_t>(c)].dropBefore(floor);
    }
  }

  /// Produce output frames into `out` (§6.2.1 items 2/4/7). process mode:
  /// t ∈ [0, N_in) with the strict-delivery gate; finish mode: the tail
  /// [outT_, N_in + W) capped at W emitted frames. NO allocation.
  int produceInto(AudioBlockOut& out, int outCapacity, bool finishMode) {
    if (outCapacity <= 0) return 0;
    const FrameCount nIn = ctx_.totalInputFrames;
    const FrameCount outputEnd = nIn + W_;
    const double* ratio = ctx_.curve->ratio;
    int produced = 0;

    while (outT_ < outputEnd && produced < outCapacity) {
      const int64_t t = outT_;
      if (!finishMode && t >= nIn) break;  // process emits t < N_in only (item 7)
      if (finishMode && flushEmitted_ >= W_) break;  // defensive W cap (item 7)

      // ---- fade-before detection (§6.2.1 item 4a): LOWER crossings ----
      // Runs only with no active fade; simulates the recurrence forward
      // (the curve is fully known — the result is exact); early-exit when
      // v_ cannot reach < 0 within W steps (v_ > W·s_max or s_max == 0).
      if (!finishMode && !fadeActive_ && W_ > 0 && sMax_ > 0.0 &&
          v_ <= static_cast<double>(excursionBudget_)) {
        double vSim = v_;
        int64_t k = 0;
        for (int64_t i = 0; i < W_; ++i) {
          const int64_t idx = std::min<int64_t>(t + i, nIn - 1);
          vSim += 1.0 - ratio[static_cast<std::size_t>(idx)];
          if (vSim < 0.0) {
            k = i + 1;  // crossing at frame t + k; fade spans [t, t+k)
            break;
          }
        }
        if (k > 0) {
          fadeActive_ = true;
          fadeN_ = k;      // n = min(W, k) — clipped when the crossing is imminent
          fadeJ_ = 0;
          fadeOld_ = v_;             // the pre-wrap trajectory (delay → 0)
          fadeNew_ = v_ + static_cast<double>(E_);  // the post-wrap trajectory
        }
      }

      // ---- read branches + gains for frame t ----
      double dOld = v_;
      double dNew = v_;
      double gOld = 1.0;
      double gNew = 0.0;
      if (fadeActive_) {
        const double s = 3.14159265358979323846 * 0.5 *
                         static_cast<double>(fadeJ_ + 1) / static_cast<double>(fadeN_);
        gNew = std::sin(s);
        gOld = std::cos(s);
        dOld = fadeOld_;
        dNew = fadeNew_;
      }

      // ---- strict-delivery gate (process mode; §6.2.1 item 7) ----
      // The highest read tap of frame t must be within the delivered stream.
      if (!finishMode) {
        const double dMin = std::min(dOld, dNew);
        const double highestTap = static_cast<double>(t) - dMin + static_cast<double>(k_);
        if (highestTap > static_cast<double>(consumedTotal_ - 1)) break;
      }

      // ---- read (shared absolute positions across channels; item 8) ----
      for (ChannelCount c = 0; c < ctx_.channels; ++c) {
        const SlidingWindow& w = windows_[static_cast<std::size_t>(c)];
        const double posOld = static_cast<double>(t) - dOld - static_cast<double>(w.start);
        const double yOld = interpolateAt(w.data.data(), w.count, posOld, 1.0,
                                          ResampleQuality::Small);
        if (fadeActive_) {
          const double posNew = static_cast<double>(t) - dNew - static_cast<double>(w.start);
          const double yNew = interpolateAt(w.data.data(), w.count, posNew, 1.0,
                                            ResampleQuality::Small);
          out.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(produced)] =
              gOld * yOld + gNew * yNew;
        } else {
          out.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(produced)] = yOld;
        }
      }
      ++produced;
      ++outT_;
      if (finishMode) ++flushEmitted_;

      // ---- fade position advance ----
      if (fadeActive_) {
        ++fadeJ_;
        if (fadeJ_ >= fadeN_) fadeActive_ = false;  // blend complete at this frame
      }

      // ---- advance the delay machine (AFTER the read; item 2) ----
      const int64_t idx = std::min<int64_t>(t, nIn - 1);
      const double step = 1.0 - ratio[static_cast<std::size_t>(idx)];
      v_ += step;
      if (fadeActive_) {
        fadeOld_ += step;
        fadeNew_ += step;
      }

      // ---- wrap events (item 3/4) ----
      if (v_ < 0.0) {
        // LOWER wrap: jump +E. The fade (if any) was pre-positioned BEFORE
        // the crossing (item 4a); a wrap while a fade is active (or W == 0,
        // or a missed detection) is an INSTANT jump (item 4c) — never a new
        // fade at the wrap moment.
        v_ += static_cast<double>(E_);
      } else if (v_ > static_cast<double>(E_)) {
        // UPPER wrap: jump −E; start the fade-AFTER when no fade is active
        // (item 4b): the fade spans [t+1, t+1+W), old = the pre-wrap
        // trajectory (retained past), new = the post-wrap active delay.
        const double preWrap = v_;
        v_ -= static_cast<double>(E_);
        if (!fadeActive_ && W_ > 0) {
          fadeActive_ = true;
          fadeN_ = W_;
          fadeJ_ = 0;
          fadeOld_ = preWrap;
          fadeNew_ = v_;
        }
      }
    }

    compactWindows();
    return produced;
  }
};

}  // namespace

std::unique_ptr<PitchEngine> makeVardelayEngine() {
  return std::make_unique<VardelayEngine>();
}

// Engine-owned discrete choices (Task 29): the read kernel the engine's own
// configure() accepts (§6.2.1) — the product layer's fixed choice.
const char* const kVardelayKernelChoices[] = {"small-sinc"};

EngineDescriptor vardelayEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = "native.vardelay";
  d.info.displayName = "Vardelay — variable-delay musical shifter";
  d.info.version = "0.1.0";
  d.info.usageClass = UsageClass::Prototype;
  d.info.origin = "own implementation";
  d.info.license = "own code, no dependency";
  d.capabilities.minRatio = 0.5;
  d.capabilities.maxRatio = 2.0;
  d.capabilities.supportsDynamicRatio = true;
  d.capabilities.controlRate.kind = ControlRateSpec::Kind::PerSample;
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.45;
  d.capabilities.bandwidth.notes =
      "wrap/crossfade AM at |r-1|/E cadence is the family artefact (intentional); "
      "no AA pre-filter (sheet §6.2 specifies none): upward excursions (ratio > 1) "
      "alias above Nyquist/ratio; full-band sinc reads avoid HF loss";
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.determinism = Determinism::Deterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
  d.parameterKeys = {"excursion_seconds", "crossfade_frames", "read_kernel"};
  // Engine-owned parameter descriptors (Task 29): excursion/crossfade are
  // product-exposed; read_kernel is an engine-internal FIXED value (the
  // product layer always writes the default kernel — the declaration makes
  // that fixed nature explicit instead of hiding it in an adapter if-chain).
  d.parameters = {
      {
          .key = "excursion_seconds",
          .displayName = "Excursion",
          .kind = EngineParamKind::Real,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.05,
          .max = 2.0,
          .defaultPlain = 0.5,  // v0.1 default
          .unit = "s",
          .stepCount = -1,
          .choiceNames = nullptr,
          .choiceValues = nullptr,
          .choiceCount = 0,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%.3f",
      },
      {
          .key = "crossfade_frames",
          .displayName = "Crossfade",
          .kind = EngineParamKind::Integer,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 8192.0,
          .defaultPlain = 2048.0,  // v0.1 default
          .unit = "frames",
          .stepCount = 8192,
          .choiceNames = nullptr,
          .choiceValues = nullptr,
          .choiceCount = 0,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%d",
      },
      {
          .key = "read_kernel",
          .displayName = "Read Kernel",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = false,  // engine-internal fixed value (not a product control)
          .min = 0.0,
          .max = 0.0,
          .defaultPlain = 0.0,  // choiceNames[0] == "small-sinc"
          .unit = "",
          .stepCount = 0,
          .choiceNames = kVardelayKernelChoices,
          .choiceValues = nullptr,
          .choiceCount = 1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
      },
  };
  d.factory = &makeVardelayEngine;
  return d;
}

}  // namespace pitchlab
