// Pitch Lab — native.stft: the stftPitchShift core (MIT, Juergen Hock;
// vendored external/stftpitchshift) — STFT phase-vocoder pitch shifting
// with optional cepstral formant preservation — in a streaming STFT
// (periodic Hann analysis/synthesis, hop N/4, unity-gain OLA), through the
// shared fixed-latency wrapper (stream_engine_base.h).
//
// Streaming geometry (exact): every H = N/4 input frames one N-frame
// analysis frame (the newest N inputs) is shifted and overlap-added; the
// first H accumulated samples are then final and enter the output FIFO,
// which starts primed with H zeros — the library latency is exactly N.

#include "engines/stft_engine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <span>
#include <vector>

#include <StftPitchShift/StftPitchShiftCore.h>
#include <StftPitchShift/RFFT.h>

#include "core/errors.h"
#include "engines/stream_engine_base.h"

namespace pitchlab {

namespace {

constexpr const char* kEngineId = "native.stft";
constexpr double kQuefrencySec = 0.001;  // 1 ms cepstral lifter (formant envelope)

class StftEngine final : public StreamEngineBase {
 public:
  const char* engineId() const override { return kEngineId; }

  void configure(const EngineConfiguration& cfg) override {
    frameSize_ = 2048;
    if (const ParameterValue* v = cfg.find("frame_size")) {
      const auto* i = std::get_if<int64_t>(v);
      if (i == nullptr || (*i != 1024 && *i != 2048 && *i != 4096)) {
        throw ConfigError("", "frame_size", "frame_size must be 1024, 2048 or 4096");
      }
      frameSize_ = static_cast<int>(*i);
    }
    preserveFormants_ = false;
    if (const ParameterValue* v = cfg.find("preserve_formants")) {
      const auto* b = std::get_if<bool>(v);
      if (b == nullptr) throw ConfigError("", "preserve_formants", "preserve_formants must be a boolean");
      preserveFormants_ = *b;
    }
  }

 protected:
  void buildLibrary() override {
    const int n = frameSize_;
    hop_ = n / 4;
    fft_ = std::make_shared<stftpitchshift::RFFT>();
    for (int c = 0; c < channels_; ++c) {
      core_[c] = std::make_unique<stftpitchshift::StftPitchShiftCore<double>>(
          fft_, static_cast<std::size_t>(n), static_cast<std::size_t>(hop_), ctx_.sampleRate);
      core_[c]->quefrency(preserveFormants_ ? kQuefrencySec : 0.0);
      inRing_[c].assign(static_cast<std::size_t>(n), 0.0);
      acc_[c].assign(static_cast<std::size_t>(n), 0.0);
      fifo_[c].assign(static_cast<std::size_t>(2 * hop_ + kStreamControlBlock), 0.0);
    }
    factor_.assign(1, 1.0);
    // size the Pitcher's buffers HERE (its first factors() call allocates);
    // later same-size factor updates on the audio thread do not
    for (int c = 0; c < channels_; ++c) core_[c]->factors(factor_);
    currentRatio_ = 1.0;
    frame_.assign(static_cast<std::size_t>(n), 0.0);
    dft_.assign(static_cast<std::size_t>(n / 2 + 1), std::complex<double>{});
    // periodic Hann (the library's STFT window) + the unity-gain synthesis
    // scale hop / sum(w^2) — exact reconstruction at ratio 1
    window_.resize(static_cast<std::size_t>(n));
    double sumSq = 0.0;
    for (int i = 0; i < n; ++i) {
      const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / n);
      window_[static_cast<std::size_t>(i)] = w;
      sumSq += w * w;
    }
    synthGain_ = static_cast<double>(hop_) / sumSq;
    // warm the FFT plan cache (first use allocates) — never on the audio thread
    fft_->fft(std::span<const double>(frame_), std::span<std::complex<double>>(dft_));
    fft_->ifft(std::span<const std::complex<double>>(dft_), std::span<double>(frame_));
    std::fill(frame_.begin(), frame_.end(), 0.0);
    ringPos_ = 0;
    sinceHop_ = 0;
    // the output FIFO starts primed with H zeros (the latency's last H)
    fifoHead_ = 0;
    fifoCount_ = hop_;
  }
  [[nodiscard]] int64_t libLatency() const override { return frameSize_; }
  [[nodiscard]] int64_t latencyBound(double /*fs*/) const override { return frameSize_; }
  [[nodiscard]] int64_t processingLag() const override { return frameSize_ / 2; }

  void libProcess(int n, double ratio) override {
    if (ratio != currentRatio_) {
      factor_[0] = ratio;  // same-size assignment: no allocation in Pitcher::factors
      for (int c = 0; c < channels_; ++c) core_[c]->factors(factor_);
      currentRatio_ = ratio;
    }
    const int fifoSize = static_cast<int>(fifo_[0].size());
    for (int i = 0; i < n; ++i) {
      for (int c = 0; c < channels_; ++c) {
        inRing_[c][static_cast<std::size_t>(ringPos_)] = in_[c][static_cast<std::size_t>(i)];
      }
      ringPos_ = (ringPos_ + 1) % frameSize_;
      if (++sinceHop_ == hop_) {
        sinceHop_ = 0;
        for (int c = 0; c < channels_; ++c) shiftFrame(c, fifoSize);
        fifoCount_ += hop_;
      }
      for (int c = 0; c < channels_; ++c) {
        out_[c][static_cast<std::size_t>(i)] = fifo_[c][static_cast<std::size_t>(fifoHead_)];
      }
      fifoHead_ = (fifoHead_ + 1) % fifoSize;
      --fifoCount_;
    }
  }

 private:
  /// One analysis frame (the newest N inputs, oldest first) -> shift ->
  /// overlap-add; the first H accumulated samples move to the output FIFO.
  void shiftFrame(int c, int fifoSize) {
    const int n = frameSize_;
    auto& ring = inRing_[c];
    for (int j = 0; j < n; ++j) {
      frame_[static_cast<std::size_t>(j)] =
          ring[static_cast<std::size_t>((ringPos_ + j) % n)] * window_[static_cast<std::size_t>(j)];
    }
    fft_->fft(std::span<const double>(frame_), std::span<std::complex<double>>(dft_));
    core_[c]->shiftpitch(std::span<std::complex<double>>(dft_));
    fft_->ifft(std::span<const std::complex<double>>(dft_), std::span<double>(frame_));
    auto& acc = acc_[c];
    for (int j = 0; j < n; ++j) {
      acc[static_cast<std::size_t>(j)] +=
          frame_[static_cast<std::size_t>(j)] * window_[static_cast<std::size_t>(j)] * synthGain_;
    }
    // the first H samples are final: append to the FIFO, shift the rest down
    const int tail = (fifoHead_ + fifoCount_) % fifoSize;
    for (int j = 0; j < hop_; ++j) {
      fifo_[c][static_cast<std::size_t>((tail + j) % fifoSize)] = acc[static_cast<std::size_t>(j)];
    }
    std::copy(acc.begin() + hop_, acc.end(), acc.begin());
    std::fill(acc.end() - hop_, acc.end(), 0.0);
  }

  int frameSize_ = 2048;
  bool preserveFormants_ = false;
  int hop_ = 512;
  std::shared_ptr<stftpitchshift::RFFT> fft_;
  std::unique_ptr<stftpitchshift::StftPitchShiftCore<double>> core_[2];
  std::vector<double> inRing_[2];
  std::vector<double> acc_[2];
  std::vector<double> fifo_[2];
  std::vector<double> factor_;
  std::vector<double> frame_;
  std::vector<std::complex<double>> dft_;
  std::vector<double> window_;
  double synthGain_ = 1.0;
  double currentRatio_ = -1.0;
  int ringPos_ = 0;
  int sinceHop_ = 0;
  int fifoHead_ = 0;
  int fifoCount_ = 0;
};

const char* const kFrameSizeNames[] = {"1024", "2048", "4096"};
const double kFrameSizeValues[] = {1024.0, 2048.0, 4096.0};

}  // namespace

int64_t stftLatencyFrames(int frameSize) { return frameSize; }

std::unique_ptr<PitchEngine> makeStftEngine() { return std::make_unique<StftEngine>(); }

EngineDescriptor stftEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = kEngineId;
  d.info.displayName = "STFT Pitch Shift (stftPitchShift core)";
  d.info.version = "70c96db";
  d.info.usageClass = UsageClass::ExternalIntegration;
  d.info.origin = "external: stftPitchShift (Juergen Hock) 70c96db core, vendored";
  d.info.license = "MIT (stftPitchShift)";
  d.capabilities.minRatio = 0.25;
  d.capabilities.maxRatio = 4.0;
  d.capabilities.supportsDynamicRatio = true;
  d.capabilities.controlRate.kind = ControlRateSpec::Kind::FixedBlock;
  d.capabilities.controlRate.blockFrames = kStreamControlBlock;
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.5;
  d.capabilities.bandwidth.notes =
      "STFT phase vocoder (hop N/4) with optional cepstral formant preservation; block-rate "
      "(128-frame) control";
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.determinism = Determinism::Deterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u};
  d.parameterKeys = {"frame_size", "preserve_formants"};
  d.parameters = {
      {
          .key = "frame_size",
          .displayName = "Frame",
          .kind = EngineParamKind::Integer,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 2.0,
          .defaultPlain = 1.0,  // 2048
          .unit = "",
          .stepCount = 2,
          .choiceNames = kFrameSizeNames,
          .choiceValues = kFrameSizeValues,
          .choiceCount = 3,
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
          .defaultPlain = 0.0,
          .unit = "",
          .stepCount = 1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
      },
  };
  d.factory = &makeStftEngine;
  return d;
}

}  // namespace pitchlab
