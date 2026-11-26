// Pitch Lab — native.bungee: the Bungee (Basic edition, MPL-2.0, Parabola
// Research; vendored external/bungee) phase-vocoder pitch shifter in the
// v0.1 PitchEngine contract, through the shared fixed-latency streaming
// wrapper (stream_engine_base.h) and Bungee's own Stream API (speed 1,
// pitch = the curve ratio).

#include "engines/bungee_engine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <bungee/Bungee.h>
#include <bungee/Stream.h>

#include "core/errors.h"
#include "engines/stream_engine_base.h"

namespace pitchlab {

namespace {

constexpr const char* kEngineId = "native.bungee";
using Stretcher = Bungee::Stretcher<Bungee::Basic>;
using Stream = Bungee::Stream<Bungee::Basic>;

/// granularity choice index -> Bungee's log2SynthesisHopAdjust
int hopAdjustFor(int granularity) { return granularity == 0 ? -1 : granularity == 2 ? 1 : 0; }

/// The library latency for (fs, granularity): an impulse through a fresh
/// stretcher at pitch 1, the output peak's offset. Deterministic.
int64_t measureLatency(double fs, int granularity) {
  const int rate = static_cast<int>(std::lround(fs));
  Stretcher stretcher(Bungee::SampleRates{rate, rate}, 1, hopAdjustFor(granularity));
  Stream stream(stretcher, kStreamControlBlock, 1);
  std::vector<float> in(kStreamControlBlock, 0.0f);
  std::vector<float> out(kStreamControlBlock, 0.0f);
  const float* ip[1] = {in.data()};
  float* op[1] = {out.data()};
  int64_t produced = 0;
  int64_t peakAt = 0;
  float peak = 0.0f;
  const int64_t limit = static_cast<int64_t>(fs);  // 1 s is far beyond any grain
  for (int64_t pos = 0; pos < limit; pos += kStreamControlBlock) {
    std::fill(in.begin(), in.end(), 0.0f);
    if (pos == 0) in[0] = 1.0f;
    const int got = stream.process(ip, op, kStreamControlBlock, kStreamControlBlock, 1.0);
    for (int i = 0; i < got; ++i) {
      if (std::fabs(out[static_cast<std::size_t>(i)]) > peak) {
        peak = std::fabs(out[static_cast<std::size_t>(i)]);
        peakAt = produced + i;
      }
    }
    produced += got;
  }
  return peakAt;
}

class BungeeEngine final : public StreamEngineBase {
 public:
  const char* engineId() const override { return kEngineId; }

  void configure(const EngineConfiguration& cfg) override {
    granularity_ = 1;
    if (const ParameterValue* v = cfg.find("granularity")) {
      const auto* s = std::get_if<std::string>(v);
      if (s == nullptr) throw ConfigError("", "granularity", "granularity must be a string");
      if (*s == "fine") {
        granularity_ = 0;
      } else if (*s == "normal") {
        granularity_ = 1;
      } else if (*s == "dense") {
        granularity_ = 2;
      } else {
        throw ConfigError("", "granularity", "unknown granularity '" + *s + "' (fine|normal|dense)");
      }
    }
  }

 protected:
  void buildLibrary() override {
    stream_.reset();
    stretcher_.reset();
    const int rate = static_cast<int>(std::lround(ctx_.sampleRate));
    stretcher_ = std::make_unique<Stretcher>(Bungee::SampleRates{rate, rate}, channels_,
                                             hopAdjustFor(granularity_));
    stream_ = std::make_unique<Stream>(*stretcher_, kStreamControlBlock, channels_);
    for (int c = 0; c < 2; ++c) {
      inF_[c].assign(kStreamControlBlock, 0.0f);
      outF_[c].assign(kStreamControlBlock, 0.0f);
    }
    latency_ = bungeeLatencyBoundFrames(ctx_.sampleRate, granularity_) - kBoundMargin;
    lag_ = stretcher_->maxInputFrameCount() / 2;
  }
  [[nodiscard]] int64_t libLatency() const override { return latency_; }
  [[nodiscard]] int64_t latencyBound(double fs) const override {
    return bungeeLatencyBoundFrames(fs, granularity_);
  }
  [[nodiscard]] int64_t processingLag() const override { return lag_; }

  void libProcess(int n, double ratio) override {
    for (int c = 0; c < channels_; ++c) {
      for (int i = 0; i < n; ++i) {
        inF_[c][static_cast<std::size_t>(i)] = static_cast<float>(in_[c][static_cast<std::size_t>(i)]);
      }
    }
    const float* ip[2] = {inF_[0].data(), inF_[1].data()};
    float* op[2] = {outF_[0].data(), outF_[1].data()};
    const int got = stream_->process(ip, op, n, n, ratio);
    for (int c = 0; c < channels_; ++c) {
      for (int i = 0; i < n; ++i) {
        out_[c][static_cast<std::size_t>(i)] =
            i < got ? static_cast<double>(outF_[c][static_cast<std::size_t>(i)]) : 0.0;
      }
    }
  }

 private:
  static constexpr int64_t kBoundMargin = 64;
  int granularity_ = 1;
  std::unique_ptr<Stretcher> stretcher_;
  std::unique_ptr<Stream> stream_;
  std::vector<float> inF_[2];
  std::vector<float> outF_[2];
  int64_t latency_ = 0;
  int64_t lag_ = 0;
};

const char* const kGranularityChoices[] = {"fine", "normal", "dense"};
const char* const kGranularityDisplay[] = {"Fine", "Normal", "Dense"};

}  // namespace

int64_t bungeeLatencyBoundFrames(double sampleRate, int granularity) {
  // measured once per (rate, granularity) and cached — called from the
  // preparation / main thread only (allocation is allowed there)
  static std::mutex mutex;
  static std::map<std::pair<long, int>, int64_t> cache;
  // Only the DECLARED rates construct a Bungee instance (the VST3 validator
  // activates at e.g. 1234.5678 Hz and 1.2 MHz, where the library is not
  // defined); any other rate scales the 48 kHz measurement (the grain
  // geometry is rate-proportional) — an engine is never prepared there
  // (the registry's supportedSampleRates gate), so this only sizes the
  // adapter's worst-case latency bound.
  const long rate = std::lround(sampleRate);
  if (rate != 44100 && rate != 48000 && rate != 88200 && rate != 96000) {
    const double scale = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate / 48000.0 : 1.0;
    return static_cast<int64_t>(
        std::ceil(static_cast<double>(bungeeLatencyBoundFrames(48000.0, granularity)) *
                  std::max(scale, 1.0 / 48.0)));
  }
  const std::pair<long, int> key{rate, granularity};
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
  }
  const int64_t bound = measureLatency(sampleRate, granularity) + 64;  // == kBoundMargin
  std::lock_guard<std::mutex> lock(mutex);
  cache.emplace(key, bound);
  return bound;
}

std::unique_ptr<PitchEngine> makeBungeeEngine() { return std::make_unique<BungeeEngine>(); }

EngineDescriptor bungeeEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = kEngineId;
  d.info.displayName = "Bungee (phase-vocoder pitch shift)";
  d.info.version = "61f52bf";
  d.info.usageClass = UsageClass::ExternalIntegration;
  d.info.origin = "external: Bungee Basic (Parabola Research) 61f52bf, vendored";
  d.info.license = "MPL-2.0 (Bungee, Eigen); BSD-like (PFFFT)";
  d.capabilities.minRatio = 0.25;
  d.capabilities.maxRatio = 4.0;
  d.capabilities.supportsDynamicRatio = true;
  d.capabilities.controlRate.kind = ControlRateSpec::Kind::FixedBlock;
  d.capabilities.controlRate.blockFrames = kStreamControlBlock;
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.5;
  d.capabilities.bandwidth.notes =
      "granular phase vocoder (Bungee Basic); block-rate (128-frame) control; float internal "
      "precision";
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.determinism = Determinism::Deterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u};
  d.parameterKeys = {"granularity"};
  d.parameters = {
      {
          .key = "granularity",
          .displayName = "Grain",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 2.0,
          .defaultPlain = 1.0,  // "normal" (the library's recommended setting)
          .unit = "",
          .stepCount = 2,
          .choiceNames = kGranularityChoices,
          .choiceDisplayNames = kGranularityDisplay,
          .choiceValues = nullptr,
          .choiceCount = 3,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
      },
  };
  d.factory = &makeBungeeEngine;
  return d;
}

}  // namespace pitchlab
