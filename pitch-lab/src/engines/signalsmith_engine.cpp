#include "engines/signalsmith_engine.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4305)  // third-party header: narrowing conversions
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <signalsmith-stretch/signalsmith-stretch.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "core/errors.h"

namespace pitchlab {

int64_t signalsmithLatencyBoundFrames(double sampleRate, int presetIndex) {
  // The library's presets (signalsmith-stretch.h presetDefault/presetCheaper)
  // and its symmetric-window latency: analysisLatency = block - block/2,
  // synthesisLatency = block/2 (+ one interval with split computation).
  // The bound adds the interval unconditionally plus a small margin.
  const double blockSec = presetIndex == 1 ? 0.10 : 0.12;
  const double intervalSec = presetIndex == 1 ? 0.04 : 0.03;
  const int64_t block = static_cast<int64_t>(sampleRate * blockSec);
  const int64_t interval = static_cast<int64_t>(sampleRate * intervalSec);
  return block + interval + 8;
}

namespace {

constexpr const char* kEngineId = "native.signalsmith";
constexpr int kControlBlock = 128;  // FixedBlock control rate (frames)
constexpr double kMinFormant = 0.25;
constexpr double kMaxFormant = 4.0;

using Stretch = signalsmith::stretch::SignalsmithStretch<float>;

class SignalsmithEngine final : public PitchEngine {
 public:
  const char* engineId() const override { return kEngineId; }

  void configure(const EngineConfiguration& cfg) override {
    presetIndex_ = 0;
    if (const ParameterValue* v = cfg.find("preset")) {
      const auto* s = std::get_if<std::string>(v);
      if (s == nullptr) throw ConfigError("", "preset", "preset must be a string (default|cheaper)");
      if (*s == "default") {
        presetIndex_ = 0;
      } else if (*s == "cheaper") {
        presetIndex_ = 1;
      } else {
        throw ConfigError("", "preset", "unknown preset '" + *s + "' (default|cheaper)");
      }
    }
    preserveFormants_ = false;
    if (const ParameterValue* v = cfg.find("preserve_formants")) {
      const auto* b = std::get_if<bool>(v);
      if (b == nullptr) throw ConfigError("", "preserve_formants", "preserve_formants must be a boolean");
      preserveFormants_ = *b;
    }
    formantRatio_ = 1.0;
    if (const ParameterValue* v = cfg.find("formant_ratio")) {
      const auto* d = std::get_if<double>(v);
      if (d == nullptr || !std::isfinite(*d) || *d < kMinFormant || *d > kMaxFormant) {
        throw ConfigError("", "formant_ratio", "formant_ratio must be a number in [0.25, 4.0]");
      }
      formantRatio_ = *d;
    }
    seed_ = cfg.seed;
    prepared_ = false;
  }

  void prepare(const ProcessContext& ctx) override {
    if (!(ctx.sampleRate > 0.0) || !std::isfinite(ctx.sampleRate)) {
      throw ConfigError("", "sampleRate", "sample rate must be finite and positive");
    }
    if (ctx.channels < 1 || ctx.channels > 2) {
      throw ConfigError("", "channels", "native.signalsmith supports 1-2 channels");
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
    buildStretch();
    declaredLatency_ = signalsmithLatencyBoundFrames(ctx.sampleRate, presetIndex_);
    if (actualLatency_ > declaredLatency_) declaredLatency_ = actualLatency_;  // defensive
    latency_.inputLatencyFrames = declaredLatency_;
    latency_.outputLatencyFrames = declaredLatency_;
    for (int c = 0; c < 2; ++c) {
      inF_[c].assign(kControlBlock, 0.0f);
      outF_[c].assign(kControlBlock, 0.0f);
    }
    resetStreaming();
    prepared_ = true;
  }

  Latency latency() const override { return latency_; }

  ProcessReport process(const AudioBlockView& in, int inFrames, AudioBlockOut& out,
                        int outCapacity, const PitchCurveView& curve,
                        FrameCount inputFrameIndex) override {
    if (!prepared_) throw EngineException(kEngineId, "process() called before prepare()");
    if (inFrames < 0 || outCapacity < 0) throw EngineException(kEngineId, "negative inFrames/outCapacity");
    if (inputFrameIndex != consumedTotal_) {
      throw EngineException(kEngineId, "inputFrameIndex != cumulative consumed (contract precondition, §4.2)");
    }
    if (in.channelCount != ctx_.channels || out.channelCount != ctx_.channels) {
      throw EngineException(kEngineId, "channel count mismatch in block views");
    }
    if (curve.ratio != ctx_.curve->ratio || curve.frames != ctx_.curve->frames) {
      throw EngineException(kEngineId, "curve view differs from the prepared one");
    }
    int consumed = 0;
    int produced = 0;
    while (consumed < inFrames) {
      int n = std::min(kControlBlock, inFrames - consumed);
      // frames of this sub-block that land on the aligned output [0, N_in):
      // bounded by the remaining output capacity (consume less otherwise)
      n = limitByCapacity(n, outCapacity - produced);
      if (n <= 0) break;
      for (int c = 0; c < channels_; ++c) {
        const double* src = in.channels[c] + consumed;
        float* dst = inF_[c].data();
        for (int i = 0; i < n; ++i) dst[i] = static_cast<float>(src[i]);
      }
      runBlock(n, /*zeros=*/false, out, produced);
      consumed += n;
    }
    const FrameCount streamEnd = ctx_.totalInputFrames + latency_.inputLatencyFrames;
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= streamEnd);
    return ProcessReport{consumed, produced, inputExhausted_};
  }

  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    if (!prepared_) throw EngineException(kEngineId, "finish() called before prepare()");
    if (finished_) throw EngineException(kEngineId, "finish() called twice (contract: exactly once)");
    if (outCapacity < 0) throw EngineException(kEngineId, "negative outCapacity");
    if (out.channelCount != ctx_.channels) throw EngineException(kEngineId, "channel count mismatch in output view");
    finished_ = true;
    // Any aligned output not yet produced (the stream was not fully padded,
    // or the capacity was short): feed semantic zeros through the library.
    int produced = 0;
    int guard = 0;
    while (outM_ < ctx_.totalInputFrames && produced < outCapacity &&
           guard++ < (1 << 20)) {
      int n = limitByCapacity(kControlBlock, outCapacity - produced);
      if (n <= 0) break;
      runBlock(n, /*zeros=*/true, out, produced);
    }
    return ProcessReport{0, produced, inputExhausted_};
  }

  void reset() override {
    if (!prepared_) return;
    buildStretch();  // fresh library state + reseeded RNG (bit-identical to a new instance)
    resetStreaming();
  }

 private:
  void buildStretch() {
    stretch_ = std::make_unique<Stretch>(static_cast<long>(seed_ & 0x7fffffffULL));
    const float fs = static_cast<float>(ctx_.sampleRate);
    if (presetIndex_ == 1) {
      stretch_->presetCheaper(channels_, fs);
    } else {
      stretch_->presetDefault(channels_, fs);
    }
    actualLatency_ = static_cast<int64_t>(stretch_->inputLatency()) +
                     static_cast<int64_t>(stretch_->outputLatency());
  }

  void resetStreaming() {
    consumedTotal_ = 0;
    stretchOut_ = 0;
    outM_ = 0;
    inputExhausted_ = false;
    finished_ = false;
  }

  /// Limit a sub-block of `n` library frames so the aligned frames it emits
  /// fit into `capacityLeft` (pre-roll and post-N_in frames are discarded,
  /// so they need no capacity).
  [[nodiscard]] int limitByCapacity(int n, int capacityLeft) const {
    const int64_t firstAligned = stretchOut_ - actualLatency_;  // aligned index of library frame 0
    int64_t emit = 0;
    const int64_t lo = std::max<int64_t>(firstAligned, 0);
    const int64_t hi = std::min<int64_t>(firstAligned + n, ctx_.totalInputFrames);
    if (hi > lo) emit = hi - lo;
    if (emit <= capacityLeft) return n;
    // reduce n so that the emitted count equals capacityLeft
    const int64_t preRoll = std::max<int64_t>(0, -firstAligned);
    return static_cast<int>(std::min<int64_t>(n, preRoll + std::max(capacityLeft, 0)));
  }

  void runBlock(int n, bool zeros, AudioBlockOut& out, int& produced) {
    // control: the ratio at the sub-block's input position (FixedBlock rate)
    const FrameCount idx = std::clamp<FrameCount>(consumedTotal_, 0, ctx_.totalInputFrames - 1);
    const double ratio = ctx_.curve->ratio[static_cast<std::size_t>(idx)];
    stretch_->setTransposeFactor(static_cast<float>(ratio));
    stretch_->setFormantFactor(static_cast<float>(formantRatio_), preserveFormants_);
    if (zeros) {
      for (int c = 0; c < channels_; ++c) std::fill(inF_[c].begin(), inF_[c].begin() + n, 0.0f);
    }
    float* inPtrs[2] = {inF_[0].data(), inF_[1].data()};
    float* outPtrs[2] = {outF_[0].data(), outF_[1].data()};
    stretch_->process(inPtrs, n, outPtrs, n);
    for (int i = 0; i < n; ++i) {
      const int64_t m = stretchOut_ + i - actualLatency_;
      if (m < 0 || m >= ctx_.totalInputFrames) continue;
      for (int c = 0; c < channels_; ++c) {
        out.channels[c][produced] = static_cast<double>(outF_[c][static_cast<std::size_t>(i)]);
      }
      ++produced;
      ++outM_;
    }
    stretchOut_ += n;
    if (!zeros) consumedTotal_ += n;
  }

  // configuration
  int presetIndex_ = 0;
  bool preserveFormants_ = false;
  double formantRatio_ = 1.0;
  uint64_t seed_ = 0;

  // job state
  ProcessContext ctx_{};
  int channels_ = 1;
  std::unique_ptr<Stretch> stretch_;
  int64_t actualLatency_ = 0;
  int64_t declaredLatency_ = 0;
  Latency latency_{};
  std::vector<float> inF_[2];
  std::vector<float> outF_[2];
  bool prepared_ = false;

  // streaming state
  FrameCount consumedTotal_ = 0;
  int64_t stretchOut_ = 0;  // library output frames produced so far
  FrameCount outM_ = 0;     // aligned output frames emitted
  bool inputExhausted_ = false;
  bool finished_ = false;
};

const char* const kPresetChoices[] = {"default", "cheaper"};
const char* const kPresetDisplay[] = {"Default", "Cheaper"};

}  // namespace

std::unique_ptr<PitchEngine> makeSignalsmithEngine() { return std::make_unique<SignalsmithEngine>(); }

EngineDescriptor signalsmithEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = kEngineId;
  d.info.displayName = "Signalsmith Stretch (spectral pitch shift)";
  d.info.version = "1.4.0";
  d.info.usageClass = UsageClass::ExternalIntegration;
  d.info.origin = "external: Signalsmith Stretch 1.4.0 (vendored, header-only)";
  d.info.license = "MIT (signalsmith-stretch + signalsmith-linear)";
  d.capabilities.minRatio = 0.125;
  d.capabilities.maxRatio = 8.0;
  d.capabilities.supportsDynamicRatio = true;
  d.capabilities.controlRate.kind = ControlRateSpec::Kind::FixedBlock;
  d.capabilities.controlRate.blockFrames = kControlBlock;
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.5;
  d.capabilities.bandwidth.notes =
      "spectral (STFT) pitch shift with tonal/phase-locking heuristics and optional "
      "formant compensation; block-rate (128-frame) control; float internal precision";
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.determinism = Determinism::SeededDeterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
  d.parameterKeys = {"preset", "preserve_formants", "formant_ratio"};
  d.parameters = {
      {
          .key = "preset",
          .displayName = "Quality",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 1.0,
          .defaultPlain = 0.0,
          .unit = "",
          .stepCount = 1,
          .choiceNames = kPresetChoices,
          .choiceDisplayNames = kPresetDisplay,
          .choiceValues = nullptr,
          .choiceCount = 2,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
      },
      {
          .key = "preserve_formants",
          .displayName = "Keep Formants",
          .kind = EngineParamKind::Boolean,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 1.0,
          .defaultPlain = 0.0,  // plain transposition (the other engines' semantics)
          .unit = "",
          .stepCount = 1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
      },
      {
          .key = "formant_ratio",
          .displayName = "Formant",
          .kind = EngineParamKind::Real,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = kMinFormant,
          .max = kMaxFormant,
          .defaultPlain = 1.0,
          .unit = "x",
          .stepCount = -1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%.2f",
      },
  };
  d.factory = &makeSignalsmithEngine;
  return d;
}

}  // namespace pitchlab
