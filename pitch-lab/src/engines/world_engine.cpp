#include "engines/world_engine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/errors.h"

#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/dio.h"
#include "world/harvest.h"
#include "world/stonemask.h"
#include "world/synthesis.h"

namespace pitchlab {

namespace {

constexpr const char* kEngineId = "native.world";
constexpr double kChunkSeconds = 0.08;      // analysis/synthesis chunk (output core)
constexpr double kCrossfadeSeconds = 0.01;  // chunk-to-chunk crossfade
constexpr double kContextSeconds = 0.045;   // analysis context on each side (>= the
                                            // CheapTrick/D4C half-window at the 71 Hz floor)
constexpr double kHeadroomSeconds = 0.04;   // worker headroom inside the latency
constexpr double kFramePeriodMs = 5.0;      // WORLD frame period
constexpr double kPi = 3.14159265358979323846;

struct Geometry {
  int64_t S = 0;  // chunk
  int64_t X = 0;  // crossfade
  int64_t C = 0;  // context
  int64_t L = 0;  // declared latency (S + X + C + worker headroom W)
};

Geometry geometryFor(double fs) {
  Geometry g;
  g.S = std::max<int64_t>(256, std::llround(kChunkSeconds * fs));
  g.X = std::max<int64_t>(16, std::llround(kCrossfadeSeconds * fs));
  g.C = std::max<int64_t>(64, std::llround(kContextSeconds * fs));
  // Kept <= ~0.18 s: the realtime adapter's 3-slot job grid behind a
  // 0.2 s splice engine (varispeed) must cover the emission lag after an
  // engine switch (measured: a 0.29 s Lambda under-ran WORLD -> varispeed).
  g.L = g.S + g.X + g.C + std::max<int64_t>(64, std::llround(kHeadroomSeconds * fs));
  return g;
}

/// A 2-D [rows][cols] double matrix with stable row pointers (WORLD's
/// `double**` interface). Allocated once (prepare/worker start).
struct Matrix {
  std::vector<double> data;
  std::vector<double*> rows;
  void allocate(int nRows, int nCols) {
    data.assign(static_cast<std::size_t>(nRows) * static_cast<std::size_t>(nCols), 0.0);
    rows.resize(static_cast<std::size_t>(nRows));
    for (int r = 0; r < nRows; ++r) {
      rows[static_cast<std::size_t>(r)] = data.data() + static_cast<std::size_t>(r) * nCols;
    }
  }
};

class WorldEngine final : public PitchEngine {
 public:
  WorldEngine() = default;
  ~WorldEngine() override { stopWorker(); }

  const char* engineId() const override { return kEngineId; }

  void configure(const EngineConfiguration& cfg) override {
    harvest_ = false;
    if (const ParameterValue* v = cfg.find("f0_method")) {
      const auto* s = std::get_if<std::string>(v);
      if (s == nullptr) throw ConfigError("", "f0_method", "f0_method must be a string (dio|harvest)");
      if (*s == "dio") {
        harvest_ = false;
      } else if (*s == "harvest") {
        harvest_ = true;
      } else {
        throw ConfigError("", "f0_method", "unknown f0_method '" + *s + "' (dio|harvest)");
      }
    }
    formantRatio_ = readReal(cfg, "formant_ratio", 1.0, 0.5, 2.0);
    f0Floor_ = readReal(cfg, "f0_floor_hz", 71.0, 40.0, 200.0);
    f0Ceil_ = readReal(cfg, "f0_ceil_hz", 800.0, 200.0, 1200.0);
    if (!(f0Floor_ < f0Ceil_)) {
      throw ConfigError("", "f0_ceil_hz", "f0_ceil_hz must be greater than f0_floor_hz");
    }
    prepared_ = false;
  }

  void prepare(const ProcessContext& ctx) override {
    if (!(ctx.sampleRate >= 8000.0) || !std::isfinite(ctx.sampleRate) ||
        ctx.sampleRate > 384000.0 ||
        std::fabs(ctx.sampleRate - std::round(ctx.sampleRate)) > 1e-9) {
      throw ConfigError("", "sampleRate", "native.world needs an integral sample rate in [8000, 384000]");
    }
    if (ctx.channels < 1 || ctx.channels > 2) {
      throw ConfigError("", "channels", "native.world supports 1-2 channels");
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
    stopWorker();
    ctx_ = ctx;
    channels_ = static_cast<int>(ctx.channels);
    fs_ = static_cast<int>(std::lround(ctx.sampleRate));
    geo_ = geometryFor(ctx.sampleRate);
    nIn_ = ctx.totalInputFrames;
    nPad_ = nIn_ + geo_.L;
    chunks_ = (nIn_ + geo_.S - 1) / geo_.S;
    latency_.inputLatencyFrames = geo_.L;
    latency_.outputLatencyFrames = geo_.L;

    // rings (absolute-indexed, modulo R)
    const int64_t maxRegion = geo_.S + geo_.X + 2 * geo_.C;
    ring_ = geo_.L + 2 * maxRegion + static_cast<int64_t>(ctx.maxBlockFrames) + 1024;
    for (int c = 0; c < channels_; ++c) {
      inRing_[c].assign(static_cast<std::size_t>(ring_), 0.0);
      outRing_[c].assign(static_cast<std::size_t>(ring_), 0.0);
      tail_[c].assign(static_cast<std::size_t>(geo_.X), 0.0);
    }

    // WORLD analysis sizing (worker-owned scratch, allocated here)
    InitializeCheapTrickOption(fs_, &ctOpt_);
    ctOpt_.f0_floor = f0Floor_;
    ctOpt_.fft_size = GetFFTSizeForCheapTrick(fs_, &ctOpt_);
    fftSize_ = ctOpt_.fft_size;
    bins_ = fftSize_ / 2 + 1;
    InitializeD4COption(&d4cOpt_);
    maxF0Len_ = GetSamplesForDIO(fs_, static_cast<int>(maxRegion), kFramePeriodMs) + 2;
    xBuf_.assign(static_cast<std::size_t>(maxRegion), 0.0);
    yBuf_.assign(static_cast<std::size_t>(maxRegion), 0.0);
    tpos_.assign(static_cast<std::size_t>(maxF0Len_), 0.0);
    f0Raw_.assign(static_cast<std::size_t>(maxF0Len_), 0.0);
    f0_.assign(static_cast<std::size_t>(maxF0Len_), 0.0);
    f0Mod_.assign(static_cast<std::size_t>(maxF0Len_), 0.0);
    sp_.allocate(maxF0Len_, bins_);
    ap_.allocate(maxF0Len_, bins_);
    warpRow_.assign(static_cast<std::size_t>(bins_), 0.0);

    resetStreaming();
    prepared_ = true;
    startWorker();
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
    // 1) consume input into the ring (bounded by the oldest unfinished
    //    chunk). NEVER waits: when the ring is full (the caller is not taking
    //    output) the block is partially consumed and re-offered (§4.2).
    int consumed = 0;
    while (consumed < inFrames) {
      const int64_t space = inputSpace();
      if (space <= 0) break;
      const int n = static_cast<int>(std::min<int64_t>(inFrames - consumed, space));
      for (int c = 0; c < channels_; ++c) {
        const double* src = in.channels[c] + consumed;
        for (int i = 0; i < n; ++i) {
          inRing_[c][static_cast<std::size_t>((consumedTotal_ + i) % ring_)] = src[i];
        }
      }
      consumedTotal_ += n;
      consumed += n;
      submitReadyChunks();
    }
    // 2) emit aligned output
    const int produced = emit(out, 0, outCapacity, /*finishing=*/false);
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= nPad_);
    return ProcessReport{consumed, produced, inputExhausted_};
  }

  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    if (!prepared_) throw EngineException(kEngineId, "finish() called before prepare()");
    if (finished_) throw EngineException(kEngineId, "finish() called twice (contract: exactly once)");
    if (outCapacity < 0) throw EngineException(kEngineId, "negative outCapacity");
    if (out.channelCount != ctx_.channels) throw EngineException(kEngineId, "channel count mismatch in output view");
    finished_ = true;
    // Semantic zeros for any padding the renderer did not deliver,
    // interleaved with emission (so the ring never blocks on un-taken output).
    int produced = 0;
    while (true) {
      const int64_t space = inputSpace();
      if (consumedTotal_ < nPad_ && space > 0) {
        const int64_t n = std::min<int64_t>(nPad_ - consumedTotal_, space);
        for (int c = 0; c < channels_; ++c) {
          for (int64_t i = 0; i < n; ++i) {
            inRing_[c][static_cast<std::size_t>((consumedTotal_ + i) % ring_)] = 0.0;
          }
        }
        consumedTotal_ += n;
        submitReadyChunks();
      }
      produced += emit(out, produced, outCapacity - produced, /*finishing=*/true);
      if (outM_ >= nIn_ || produced >= outCapacity) break;
      if (consumedTotal_ >= nPad_) break;  // everything submitted and emitted that fits
      if (inputSpace() <= 0) waitForChunkProgress();
    }
    return ProcessReport{0, produced, inputExhausted_};
  }

  void reset() override {
    if (!prepared_) return;
    stopWorker();
    for (int c = 0; c < channels_; ++c) {
      std::fill(inRing_[c].begin(), inRing_[c].end(), 0.0);
      std::fill(outRing_[c].begin(), outRing_[c].end(), 0.0);
      std::fill(tail_[c].begin(), tail_[c].end(), 0.0);
    }
    resetStreaming();
    startWorker();
  }

 private:
  static double readReal(const EngineConfiguration& cfg, const char* key, double def,
                         double lo, double hi) {
    const ParameterValue* v = cfg.find(key);
    if (v == nullptr) return def;
    const auto* d = std::get_if<double>(v);
    if (d == nullptr || !std::isfinite(*d) || *d < lo || *d > hi) {
      throw ConfigError("", key, std::string(key) + " must be a number in [" + std::to_string(lo) +
                                     ", " + std::to_string(hi) + "]");
    }
    return *d;
  }

  // ---- chunk geometry ------------------------------------------------------
  [[nodiscard]] int64_t chunkStart(int64_t k) const { return std::max<int64_t>(0, k * geo_.S - geo_.C); }
  [[nodiscard]] int64_t chunkEnd(int64_t k) const {
    return std::min<int64_t>((k + 1) * geo_.S + geo_.X + geo_.C, nPad_);
  }

  // ---- process-thread helpers -----------------------------------------------
  [[nodiscard]] int64_t inputSpace() const {
    // never overwrite input still needed by the oldest unfinished chunk
    const int64_t done = done_.load(std::memory_order_acquire);
    const int64_t minNeeded = done < chunks_ ? chunkStart(done) : nPad_;
    int64_t limit = std::min<int64_t>(minNeeded + ring_, nPad_);
    // never run more than 2L ahead of the emitted output: once the whole
    // padded stream is consumed at most L aligned frames remain, so the
    // finish() flush always fits the declared outputLatencyFrames (= L)
    limit = std::min<int64_t>(limit, outM_ + 2 * geo_.L);
    return limit - consumedTotal_;
  }

  void submitReadyChunks() {
    bool any = false;
    while (submittedLocal_ < chunks_ && consumedTotal_ >= chunkEnd(submittedLocal_)) {
      ++submittedLocal_;
      any = true;
    }
    if (any) {
      submitted_.store(submittedLocal_, std::memory_order_release);
      kick();
    }
  }

  void kick() {
    epoch_.fetch_add(1, std::memory_order_acq_rel);
    epoch_.notify_one();
  }

  void waitForChunkProgress() {
    const int64_t d = done_.load(std::memory_order_acquire);
    if (d >= chunks_) return;
    done_.wait(d, std::memory_order_acquire);
  }

  int emit(AudioBlockOut& out, int offset, int outCapacity, bool finishing) {
    // output m is due once input m + L was delivered (or the padded stream
    // ended); never beyond the chunks already submitted to the worker
    int64_t emitLimit =
        (finishing || consumedTotal_ >= nPad_)
            ? nIn_
            : std::min<int64_t>(nIn_, std::max<int64_t>(0, consumedTotal_ - geo_.L));
    emitLimit = std::min<int64_t>(emitLimit, submittedLocal_ * geo_.S);
    int produced = 0;
    while (produced < outCapacity && outM_ < emitLimit) {
      const int64_t k = outM_ / geo_.S;
      if (done_.load(std::memory_order_acquire) <= k) {
        // publish our emission progress BEFORE waiting: the worker's ring
        // space check must never see a stale value (deadlock guard)
        outEmitted_.store(outM_, std::memory_order_release);
        kick();
      }
      while (done_.load(std::memory_order_acquire) <= k) {
        const int64_t d = done_.load(std::memory_order_acquire);
        if (d > k) break;
        done_.wait(d, std::memory_order_acquire);
      }
      const int64_t chunkEndOut = std::min<int64_t>((k + 1) * geo_.S, nIn_);
      const int64_t n = std::min<int64_t>({static_cast<int64_t>(outCapacity - produced),
                                           emitLimit - outM_, chunkEndOut - outM_});
      for (int c = 0; c < channels_; ++c) {
        double* dst = out.channels[c] + offset + produced;
        for (int64_t i = 0; i < n; ++i) {
          dst[i] = outRing_[c][static_cast<std::size_t>((outM_ + i) % ring_)];
        }
      }
      outM_ += n;
      produced += static_cast<int>(n);
    }
    if (produced > 0) {
      outEmitted_.store(outM_, std::memory_order_release);
      kick();
    }
    return produced;
  }

  void resetStreaming() {
    consumedTotal_ = 0;
    submittedLocal_ = 0;
    outM_ = 0;
    inputExhausted_ = false;
    finished_ = false;
    submitted_.store(0, std::memory_order_relaxed);
    done_.store(0, std::memory_order_relaxed);
    outEmitted_.store(0, std::memory_order_relaxed);
  }

  // ---- worker ----------------------------------------------------------------
  void startWorker() {
    stop_.store(false, std::memory_order_release);
    worker_ = std::thread([this] { workerLoop(); });
  }

  void stopWorker() {
    if (!worker_.joinable()) return;
    stop_.store(true, std::memory_order_release);
    kick();
    worker_.join();
  }

  void workerLoop() {
    while (true) {
      const uint32_t seen = epoch_.load(std::memory_order_acquire);
      if (stop_.load(std::memory_order_acquire)) return;
      const int64_t k = done_.load(std::memory_order_relaxed);  // worker is the only writer
      const int64_t sub = submitted_.load(std::memory_order_acquire);
      const int64_t emitted = outEmitted_.load(std::memory_order_acquire);
      const int64_t writeEnd = std::min<int64_t>((k + 1) * geo_.S, nIn_);
      const bool space = (writeEnd - emitted) <= ring_;
      if (k < chunks_ && k < sub && space) {
        processChunk(k);
        done_.store(k + 1, std::memory_order_release);
        done_.notify_all();
        continue;
      }
      epoch_.wait(seen, std::memory_order_acquire);
    }
  }

  void processChunk(int64_t k) {
    const int64_t a = chunkStart(k);
    const int64_t e = chunkEnd(k);
    const int len = static_cast<int>(e - a);
    const int f0Len = std::min(GetSamplesForDIO(fs_, len, kFramePeriodMs), maxF0Len_);
    const double* ratio = ctx_.curve->ratio;
    const int64_t finalEnd = std::min<int64_t>((k + 1) * geo_.S, nIn_);
    const int64_t tailEnd = std::min<int64_t>((k + 1) * geo_.S + geo_.X, nIn_);

    for (int c = 0; c < channels_; ++c) {
      double* x = xBuf_.data();
      for (int i = 0; i < len; ++i) {
        x[i] = inRing_[c][static_cast<std::size_t>((a + i) % ring_)];
      }
      // ---- F0 ----
      if (harvest_) {
        HarvestOption opt;
        InitializeHarvestOption(&opt);
        opt.frame_period = kFramePeriodMs;
        opt.f0_floor = f0Floor_;
        opt.f0_ceil = f0Ceil_;
        Harvest(x, len, fs_, &opt, tpos_.data(), f0_.data());
      } else {
        DioOption opt;
        InitializeDioOption(&opt);
        opt.frame_period = kFramePeriodMs;
        opt.f0_floor = f0Floor_;
        opt.f0_ceil = f0Ceil_;
        opt.speed = 1;
        Dio(x, len, fs_, &opt, tpos_.data(), f0Raw_.data());
        StoneMask(x, len, fs_, tpos_.data(), f0Raw_.data(), f0Len, f0_.data());
      }
      // ---- envelope + aperiodicity ----
      CheapTrick(x, len, fs_, tpos_.data(), f0_.data(), f0Len, &ctOpt_, sp_.rows.data());
      D4C(x, len, fs_, tpos_.data(), f0_.data(), f0Len, fftSize_, &d4cOpt_, ap_.rows.data());
      // ---- transformation ----
      for (int i = 0; i < f0Len; ++i) {
        const double f = f0_[static_cast<std::size_t>(i)];
        if (f > 0.0) {
          const int64_t absIdx = a + std::llround(tpos_[static_cast<std::size_t>(i)] * fs_);
          const int64_t idx = std::clamp<int64_t>(absIdx, 0, nIn_ - 1);
          f0Mod_[static_cast<std::size_t>(i)] = f * ratio[static_cast<std::size_t>(idx)];
        } else {
          f0Mod_[static_cast<std::size_t>(i)] = 0.0;
        }
      }
      if (formantRatio_ != 1.0) {
        for (int i = 0; i < f0Len; ++i) {
          warp(sp_.rows[static_cast<std::size_t>(i)]);
          warp(ap_.rows[static_cast<std::size_t>(i)]);
        }
      }
      // ---- synthesis ----
      Synthesis(f0Mod_.data(), f0Len, sp_.rows.data(), ap_.rows.data(), fftSize_,
                kFramePeriodMs, fs_, len, yBuf_.data());
      // ---- output: crossfade the previous chunk's tail, keep our own tail ----
      for (int64_t q = k * geo_.S; q < tailEnd; ++q) {
        double v = yBuf_[static_cast<std::size_t>(q - a)];
        if (!std::isfinite(v)) v = 0.0;
        if (q < finalEnd) {
          if (k > 0 && q < k * geo_.S + geo_.X) {
            const double s = std::sin(0.5 * kPi * (static_cast<double>(q - k * geo_.S) + 0.5) /
                                      static_cast<double>(geo_.X));
            const double w = s * s;
            v = tail_[c][static_cast<std::size_t>(q - k * geo_.S)] * (1.0 - w) + v * w;
          }
          outRing_[c][static_cast<std::size_t>(q % ring_)] = v;
        } else {
          tail_[c][static_cast<std::size_t>(q - (k + 1) * geo_.S)] = v;
        }
      }
    }
  }

  /// Formant warp of one frame's spectrum: out(k) = in(k / ratio), linear
  /// interpolation, edge-clamped.
  void warp(double* row) {
    std::memcpy(warpRow_.data(), row, sizeof(double) * static_cast<std::size_t>(bins_));
    for (int b = 0; b < bins_; ++b) {
      const double src = static_cast<double>(b) / formantRatio_;
      if (src >= static_cast<double>(bins_ - 1)) {
        row[b] = warpRow_[static_cast<std::size_t>(bins_ - 1)];
      } else {
        const int i0 = static_cast<int>(src);
        const double fr = src - static_cast<double>(i0);
        row[b] = warpRow_[static_cast<std::size_t>(i0)] * (1.0 - fr) +
                 warpRow_[static_cast<std::size_t>(i0 + 1)] * fr;
      }
    }
  }

  // configuration
  bool harvest_ = false;
  double formantRatio_ = 1.0;
  double f0Floor_ = 71.0;
  double f0Ceil_ = 800.0;

  // job state
  ProcessContext ctx_{};
  int channels_ = 1;
  int fs_ = 48000;
  Geometry geo_{};
  FrameCount nIn_ = 0;
  int64_t nPad_ = 0;
  int64_t chunks_ = 0;
  int64_t ring_ = 0;
  Latency latency_{};
  bool prepared_ = false;
  std::vector<double> inRing_[2];
  std::vector<double> outRing_[2];
  std::vector<double> tail_[2];

  // WORLD scratch (worker-owned after prepare)
  CheapTrickOption ctOpt_{};
  D4COption d4cOpt_{};
  int fftSize_ = 0;
  int bins_ = 0;
  int maxF0Len_ = 0;
  std::vector<double> xBuf_, yBuf_, tpos_, f0Raw_, f0_, f0Mod_, warpRow_;
  Matrix sp_, ap_;

  // process-thread state
  FrameCount consumedTotal_ = 0;
  int64_t submittedLocal_ = 0;
  int64_t outM_ = 0;
  bool inputExhausted_ = false;
  bool finished_ = false;

  // cross-thread state
  std::atomic<int64_t> submitted_{0};   // chunks whose input is complete (process -> worker)
  std::atomic<int64_t> done_{0};        // chunks synthesised (worker -> process)
  std::atomic<int64_t> outEmitted_{0};  // output frames consumed (process -> worker)
  std::atomic<uint32_t> epoch_{0};      // wake-up counter for the worker
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

const char* const kF0Choices[] = {"dio", "harvest"};
const char* const kF0Display[] = {"DIO (fast)", "Harvest (accurate)"};

}  // namespace

int64_t worldLatencyFrames(double sampleRate) { return geometryFor(sampleRate).L; }

int64_t worldChunkFrames(double sampleRate) { return geometryFor(sampleRate).S; }

std::unique_ptr<PitchEngine> makeWorldEngine() { return std::make_unique<WorldEngine>(); }

EngineDescriptor worldEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = kEngineId;
  d.info.displayName = "WORLD vocoder (F0/envelope/aperiodicity resynthesis)";
  d.info.version = "1.0.1";
  d.info.usageClass = UsageClass::ExternalIntegration;
  d.info.origin = "external: WORLD v1.0.1 by M. Morise (vendored)";
  d.info.license = "modified BSD (WORLD)";
  d.capabilities.minRatio = 0.25;
  d.capabilities.maxRatio = 4.0;
  d.capabilities.supportsDynamicRatio = true;
  d.capabilities.controlRate.kind = ControlRateSpec::Kind::FixedBlock;
  d.capabilities.controlRate.blockFrames = 240;  // 5 ms WORLD frame @ 48 kHz
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.5;
  d.capabilities.bandwidth.notes =
      "source-filter vocoder: monophonic voice material only (polyphony/noise collapse to "
      "one F0 track); F0 scaled on voiced frames, envelope kept (formants preserved) "
      "unless the formant ratio warps it; 5 ms parameter frames; chunked analysis "
      "(80 ms chunks, 10 ms crossfades)";
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.determinism = Determinism::Deterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u};
  d.parameterKeys = {"f0_method", "formant_ratio", "f0_floor_hz", "f0_ceil_hz"};
  d.parameters = {
      {
          .key = "f0_method",
          .displayName = "F0 Method",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 1.0,
          .defaultPlain = 0.0,
          .unit = "",
          .stepCount = 1,
          .choiceNames = kF0Choices,
          .choiceDisplayNames = kF0Display,
          .choiceValues = nullptr,
          .choiceCount = 2,
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
          .min = 0.5,
          .max = 2.0,
          .defaultPlain = 1.0,
          .unit = "x",
          .stepCount = -1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%.2f",
      },
      {
          .key = "f0_floor_hz",
          .displayName = "F0 Floor",
          .kind = EngineParamKind::Real,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 40.0,
          .max = 200.0,
          .defaultPlain = 71.0,
          .unit = "Hz",
          .stepCount = -1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%.0f",
      },
      {
          .key = "f0_ceil_hz",
          .displayName = "F0 Ceiling",
          .kind = EngineParamKind::Real,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 200.0,
          .max = 1200.0,
          .defaultPlain = 800.0,
          .unit = "Hz",
          .stepCount = -1,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%.0f",
      },
  };
  d.factory = &makeWorldEngine;
  return d;
}

}  // namespace pitchlab
