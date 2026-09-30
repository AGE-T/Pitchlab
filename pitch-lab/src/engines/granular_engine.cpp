#include "engines/granular_engine.h"

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
// §6.5.1 item 9: absolute-indexed sliding input window (the §6.1.1 item 8
// pattern, private to this TU). Pre-allocated in prepare(); front-compaction
// via memmove only — NO allocation after prepare().
// ---------------------------------------------------------------------------
struct SlidingWindow {
  std::vector<double> data;
  int64_t start = 0;
  int64_t count = 0;

  void allocate(int64_t capacity, int64_t startIdx) {
    data.assign(static_cast<std::size_t>(capacity), 0.0);
    start = startIdx;
    count = 0;
  }

  void append(const double* src, int64_t n) {
    if (n <= 0) return;
    if (count + n > static_cast<int64_t>(data.size())) {
      throw EngineException(
          "native.granular",
          "internal input window overflow (sizing bug; report as engine defect)");
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

// ---------------------------------------------------------------------------
// The engine (§6.5 sheet + §6.5.1 frozen implementation behaviour)
// ---------------------------------------------------------------------------
class GranularEngine final : public PitchEngine {
 public:
  const char* engineId() const override { return "native.granular"; }

  void configure(const EngineConfiguration& cfg) override {
    grainSeconds_ = kDefaultGrainSeconds;
    if (const ParameterValue* v = cfg.find("grain_seconds")) {
      if (const auto* d = std::get_if<double>(v)) {
        if (!std::isfinite(*d) || *d <= 0.0) {
          throw ConfigError("", "grain_seconds",
                            "grain_seconds must be finite and > 0 (§6.5.1 item 1)");
        }
        grainSeconds_ = *d;
      } else {
        throw ConfigError("", "grain_seconds", "grain_seconds must be a number");
      }
    }
    overlap_ = kDefaultOverlap;
    if (const ParameterValue* v = cfg.find("overlap")) {
      if (const auto* i = std::get_if<int64_t>(v)) {
        if (*i < 4) {
          throw ConfigError("", "overlap",
                            "overlap must be >= 4 (§6.5 sheet: G < 4·Hg is invalid; §6.5.1 item 1)");
        }
        overlap_ = *i;
      } else {
        throw ConfigError("", "overlap", "overlap must be an integer >= 4");
      }
    }
    windowName_ = kDefaultWindow;
    if (const ParameterValue* v = cfg.find("window")) {
      if (const auto* s = std::get_if<std::string>(v)) {
        if (*s != "hann" && *s != "triangular") {
          throw ConfigError("", "window",
                            "unknown window '" + *s + "' (hann|triangular, §6.5.1 item 1)");
        }
        windowName_ = *s;
      } else {
        throw ConfigError("", "window", "window must be a string (hann|triangular)");
      }
    }
    jitterFrames_ = 0;
    if (const ParameterValue* v = cfg.find("jitter_frames")) {
      if (const auto* i = std::get_if<int64_t>(v)) {
        if (*i < 0) {
          throw ConfigError("", "jitter_frames", "jitter_frames must be >= 0 (§6.5.1 item 1)");
        }
        jitterFrames_ = *i;
      } else {
        throw ConfigError("", "jitter_frames", "jitter_frames must be an integer >= 0");
      }
    }
    seed_ = cfg.seed;  // REQUIRED and CONSUMED (SeededDeterministic, §6.5)
    prepared_ = false;
  }

  void prepare(const ProcessContext& ctx) override {
    if (!(ctx.sampleRate > 0.0) || !std::isfinite(ctx.sampleRate)) {
      throw ConfigError("", "sampleRate", "sample rate must be finite and positive");
    }
    if (ctx.channels < 1 || ctx.channels > 2) {
      throw ConfigError("", "channels",
                        "native.granular supports 1-2 channels (declared MonoAndStereo)");
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

    // §4.4.3.1 item 12 guarantees finite/positive upstream; the engine is
    // independently reusable: verify while scanning r_max (§6.5.1 item 9).
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
    G_ = std::llround(grainSeconds_ * ctx.sampleRate);  // §6.5.1 item 1
    if (G_ < 4) {
      throw ConfigError("", "grain_seconds",
                        "grain_seconds converts to fewer than 4 frames at this sample rate "
                        "(§6.5.1 item 1: G >= 4 required)");
    }
    Hg_ = std::llround(static_cast<double>(G_) / static_cast<double>(overlap_));
    if (Hg_ < 1) {
      throw ConfigError("", "overlap",
                        "overlap is too large for this grain length (Hg < 1 after rounding, "
                        "§6.5.1 item 1)");
    }
    k_ = resampleKernelSpec(ResampleQuality::Small).halfWidthTaps;  // K = 8
    latency_.inputLatencyFrames = G_;   // §6.5 sheet verbatim
    latency_.outputLatencyFrames = G_;

    // Window table (§6.5.1 item 2) — deterministic cos, precomputed.
    windowTable_.resize(static_cast<std::size_t>(G_));
    const double denom = static_cast<double>(G_ - 1);
    for (int64_t j = 0; j < G_; ++j) {
      const double dj = static_cast<double>(j);
      if (windowName_ == "hann") {
        windowTable_[static_cast<std::size_t>(j)] =
            0.5 * (1.0 - std::cos(2.0 * 3.14159265358979323846 * dj / denom));
      } else {  // triangular
        windowTable_[static_cast<std::size_t>(j)] = 1.0 - std::fabs(2.0 * dj - denom) / denom;
      }
    }

    // Grain-state ring (§6.5.1 item 9): ceil(G/Hg) + 2 entries.
    ringSize_ = static_cast<int64_t>(std::ceil(static_cast<double>(G_) /
                                               static_cast<double>(Hg_))) + 2;
    grainR_.assign(static_cast<std::size_t>(ringSize_), 0.0);
    grainRatio_.assign(static_cast<std::size_t>(ringSize_), 1.0);
    grainDelta_.assign(static_cast<std::size_t>(ringSize_), 0.0);

    // Read horizon (§6.5.1 item 9): the highest input position the OLA will
    // ever touch — simulate the (unjittered) grid once (deterministic, the
    // same accumulation as the live schedule); the last grain's read end
    // plus jitter/K/margin. Content at or beyond the horizon is NEVER read:
    // process() consume-and-discards it (counted, not stored) so the full
    // padded stream is consumable without O(N) retention for ratio < 1
    // (where the reads lag behind the output timeline by (1−ratio)·N_in).
    {
      double r = 0.0;
      double lastRatio = ratio[0];
      for (int64_t k = 0; k * Hg_ < ctx.totalInputFrames + G_; ++k) {
        const int64_t idx =
            std::min<int64_t>(std::max<int64_t>(k * Hg_, 0), ctx.totalInputFrames - 1);
        lastRatio = ratio[static_cast<std::size_t>(idx)];
        r += static_cast<double>(Hg_) * lastRatio;
      }
      horizon_ = static_cast<int64_t>(std::ceil(
                     r + static_cast<double>(G_) * lastRatio +
                         static_cast<double>(jitterFrames_ + k_ + 16))) +
                 1;
    }

    // Input windows (§6.5.1 item 9 sizing).
    //
    // TASK 30 CAPACITY CORRECTION (measured, never silent — the v0.1 cycle
    // pattern): the window must hold the STORE-WORTHY TAIL
    // [retentionFloor, horizon) through the END of the input span — i.e. it
    // must cover the FROZEN-PRODUCTION demand horizon − floor. When the
    // process-mode output range is exhausted (outN_ = N_in, which for a
    // downshifted curve happens at consumed ≈ r_max·N_in — the read grid is
    // prefill-scheduled, the same mechanism as the recorded realtime
    // envelope-edge limitation), the retention floor FREEZES at
    // ≈ r_max·N_in − margins while the store-worthy region still extends to
    // the horizon ≈ r_max·(N_in + 2G + the horizon loop's PHASE OVERSHOOT)
    // + margins. The horizon simulation loop steps k·Hg while k·Hg < N_in+G,
    // so its accumulator overshoots N_in+G by up to Hg−1 input frames — the
    // pre-Task-30 capacity formula (maxBlock + spread + 2K + 256) MISSED
    // that overshoot term (≤ r_max·Hg): at 48 kHz/block 128/grain 0.1 the
    // demand measured 5787 vs capacity 5486 (the window filled at exactly
    // floor+capacity = the permanent consumption stall → the job never
    // finishes → the slot never frees → the preparation stall → the
    // delivery-underrun cascade; the Task-29 matrix ran 96 kHz only — a
    // lucky (N_in+G) mod Hg phase where the demand 10304 fit the capacity
    // 10571). The +ceil(r_max·Hg) + 192 term covers the overshoot and the
    // floor-margin slack; audio-path behaviour is UNCHANGED (an allocation
    // bound only — the reads, the OLA and the output sequence are
    // per-frame deterministic and identical).
    const int64_t spread =
        static_cast<int64_t>(std::ceil(rMax * static_cast<double>(2 * G_))) + 2 * jitterFrames_;
    const int64_t phaseOvershoot =
        static_cast<int64_t>(std::ceil(rMax * static_cast<double>(Hg_))) + 192;
    const int64_t windowCapacity =
        static_cast<int64_t>(ctx.maxBlockFrames) + spread + 2 * k_ + 256 + phaseOvershoot;
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
      throw EngineException("native.granular", "process() called before prepare()");
    }
    if (inFrames < 0 || outCapacity < 0) {
      throw EngineException("native.granular", "negative inFrames/outCapacity");
    }
    if (inputFrameIndex != consumedTotal_) {
      throw EngineException("native.granular",
                            "inputFrameIndex != cumulative consumed (contract precondition, §4.2)");
    }
    if (in.channelCount != ctx_.channels || out.channelCount != ctx_.channels) {
      throw EngineException("native.granular", "channel count mismatch in block views");
    }
    if (curve.ratio != ctx_.curve->ratio || curve.frames != ctx_.curve->frames) {
      throw EngineException("native.granular",
                            "curve view differs from the prepared one (contract: same object)");
    }

    // ---- 1) consume the offered input (§6.5.1 item 6) ----
    // Frames below the read horizon are stored (up to window capacity — the
    // partial-consumption fallback keeps §5 rule 1 valid for any renderer);
    // frames at or beyond the horizon are NEVER read — consume-and-discard
    // (counted, not stored) so the full padded stream is always consumable.
    int64_t consumed = 0;
    if (inFrames > 0) {
      compactWindows();
      const int64_t blockStart = inputFrameIndex;
      const int64_t blockEnd = blockStart + inFrames;
      const int64_t storeEnd = std::min<int64_t>(blockEnd, horizon_);
      int64_t storeCount = std::max<int64_t>(0, storeEnd - blockStart);
      if (storeCount > 0) {
        int64_t space = windows_[0].freeSpace();
        for (ChannelCount c = 1; c < ctx_.channels; ++c) {
          space = std::min(space, windows_[static_cast<std::size_t>(c)].freeSpace());
        }
        storeCount = std::min<int64_t>(storeCount, space);
      }
      if (storeCount > 0) {
        for (ChannelCount c = 0; c < ctx_.channels; ++c) {
          windows_[static_cast<std::size_t>(c)].append(in.channels[static_cast<std::size_t>(c)],
                                                       storeCount);
        }
      }
      if (storeCount == std::max<int64_t>(0, storeEnd - blockStart)) {
        // Everything store-worthy is stored: the rest of the block (beyond
        // the horizon) is consumed without storage.
        consumed = inFrames;
        consumedTotal_ += consumed;
      } else {
        // Window capacity bound the store: partial consumption (in order);
        // the renderer re-offers the remainder in the next call.
        consumed = storeCount;
        consumedTotal_ += consumed;
      }
    }

    // ---- 2) produce output frames n ∈ [0, N_in) (§6.5.1 item 7) ----
    const int produced = produceInto(out, outCapacity, /*finishMode=*/false);

    // ---- 3) exhaustion flag (§4.2.1 item 7) ----
    const FrameCount streamEnd = ctx_.totalInputFrames + latency_.inputLatencyFrames;
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= streamEnd);

    return ProcessReport{consumed, produced, inputExhausted_};
  }

  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    if (!prepared_) {
      throw EngineException("native.granular", "finish() called before prepare()");
    }
    if (finished_) {
      throw EngineException("native.granular", "finish() called twice (contract: exactly once)");
    }
    if (outCapacity < 0) {
      throw EngineException("native.granular", "negative outCapacity");
    }
    if (out.channelCount != ctx_.channels) {
      throw EngineException("native.granular", "channel count mismatch in output view");
    }
    finished_ = true;

    // The tail [N_in, N_in + G) — exactly G frames, bounded by the declared
    // outputLatencyFrames by construction (§6.5.1 item 7); reads beyond the
    // delivered end are §7.6 semantic zeros.
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
  // ---- configuration (§6.5.1 item 1) ----
  static constexpr double kDefaultGrainSeconds = 0.1;
  static constexpr int64_t kDefaultOverlap = 4;
  static constexpr const char* kDefaultWindow = "hann";
  double grainSeconds_ = kDefaultGrainSeconds;
  int64_t overlap_ = kDefaultOverlap;
  std::string windowName_ = kDefaultWindow;
  int64_t jitterFrames_ = 0;
  uint64_t seed_ = 0;

  // ---- per-job state (prepare()) ----
  ProcessContext ctx_{};
  int64_t G_ = 0;    // grain length in frames
  int64_t Hg_ = 0;   // grain hop in frames
  int k_ = 0;        // kernel half-width (Small preset, K = 8)
  Latency latency_{};
  std::vector<double> windowTable_;      // G entries (§6.5.1 item 2)
  int64_t ringSize_ = 0;
  std::vector<double> grainR_;      // unjittered read-grid r_k (mod ring)
  std::vector<double> grainRatio_;  // ratio_k (mod ring)
  std::vector<double> grainDelta_;  // jittered offsets δ_k (mod ring)
  std::vector<SlidingWindow> windows_;  // per channel
  int64_t horizon_ = 0;  // the read horizon (never-read content beyond)
  bool prepared_ = false;

  // ---- streaming state ----
  int64_t outN_ = 0;        // next output frame to produce
  int64_t nextGrainK_ = 0;  // next grain index to schedule
  double gridR_ = 0.0;      // the unjittered read grid accumulator r_k
  double lowestRead_ = 0.0; // the lowest ACTIVE read position at the last
                            // attempted output frame (the retention floor
                            // anchor — the content below the ADVANCING read
                            // trajectory is dead)
  FrameCount consumedTotal_ = 0;
  bool inputExhausted_ = false;
  bool finished_ = false;
  FrameCount flushEmitted_ = 0;  // defensive flush cap (§6.5.1 item 7)
  Pcg64 rng_{0, 0, 0, 0};  // re-derived in resetStreaming (seed consumer)

  void resetStreaming() {
    outN_ = 0;
    nextGrainK_ = 0;
    gridR_ = 0.0;
    lowestRead_ = 0.0;
    consumedTotal_ = 0;
    inputExhausted_ = false;
    finished_ = false;
    flushEmitted_ = 0;
    // §6.5.1 item 4: the consumer stream is re-derived so reset() reproduces
    // the identical draw sequence (T-D3 bit-identity).
    rng_ = makeConsumerStream(seed_, "engine.native.granular");
  }

  /// Schedule grain k (§6.5.1 items 3-4): ratio at the boundary, jitter draw
  /// (one per grain, grain-indexed), grid accumulation.
  void scheduleGrain(int64_t k) {
    const double* ratio = ctx_.curve->ratio;
    const int64_t boundaryIdx = std::min<int64_t>(std::max<int64_t>(k * Hg_, 0),
                                                  ctx_.totalInputFrames - 1);
    const double rk = ratio[static_cast<std::size_t>(boundaryIdx)];
    double delta = 0.0;
    if (jitterFrames_ > 0) {
      const double u = rng_.nextDouble01();
      delta = (2.0 * u - 1.0) * static_cast<double>(jitterFrames_);
    }
    const std::size_t slot = static_cast<std::size_t>(k % ringSize_);
    grainR_[slot] = gridR_;
    grainRatio_[slot] = rk;
    grainDelta_[slot] = delta;
    gridR_ += static_cast<double>(Hg_) * rk;  // r_{k+1} = r_k + Hg·ratio_k
    ++nextGrainK_;
  }

  /// Retention floor: the lowest tap any FUTURE read needs, minus margin
  /// (§6.5.1 item 9). The reads only ADVANCE (ratio > 0) and the active set
  /// only shifts to newer grains, so the floor is the lowest read of the
  /// CURRENT active set (tracked in lowestRead_) minus the kernel width, a
  /// jitter bound (a future grain's jittered start can dip below its grid
  /// position) and a fixed margin. The content below the advancing read
  /// trajectory is dead — this is what keeps the window small for ratio < 1
  /// (where the reads lag behind the output timeline and the input would
  /// otherwise pile up).
  [[nodiscard]] int64_t retentionFloor() const {
    return static_cast<int64_t>(std::floor(lowestRead_)) - k_ - jitterFrames_ - 16;
  }

  void compactWindows() {
    const int64_t floor = retentionFloor();
    for (ChannelCount c = 0; c < ctx_.channels; ++c) {
      windows_[static_cast<std::size_t>(c)].dropBefore(floor);
    }
  }

  /// Produce output frames into `out` (§6.5.1 items 5-7): the OLA with
  /// local-sum normalisation, the grain ring, the strict-delivery gate.
  /// process mode: n ∈ [0, N_in); finish mode: the tail [N_in, N_in + G)
  /// capped at G emitted frames. NO allocation.
  int produceInto(AudioBlockOut& out, int outCapacity, bool finishMode) {
    if (outCapacity <= 0) return 0;
    const FrameCount nIn = ctx_.totalInputFrames;
    const FrameCount outputEnd = nIn + G_;
    int produced = 0;

    while (outN_ < outputEnd && produced < outCapacity) {
      const int64_t n = outN_;
      if (!finishMode && n >= nIn) break;        // process emits n < N_in only
      if (finishMode && flushEmitted_ >= G_) break;  // defensive G cap

      // ---- schedule grains covering n (grid-indexed, deterministic) ----
      const int64_t kMax = n / Hg_;
      while (nextGrainK_ <= kMax) {
        scheduleGrain(nextGrainK_);
      }

      // ---- active set: kMin ≤ k ≤ kMax with k·Hg + G > n ----
      const int64_t kMinRaw = (n >= G_) ? ((n - G_) / Hg_ + 1) : 0;
      const int64_t kMin = std::max<int64_t>(kMinRaw, 0);
      // ---- one pass: read positions (track the lowest) + delivery gate ----
      // (§6.5.1 item 6): every active read p must satisfy p + K ≤ delivered−1
      // OR p ≥ N_in (the zero region is never awaited).
      double minP = 0.0;
      bool first = true;
      bool ready = true;
      for (int64_t k = kMin; k <= kMax; ++k) {
        const std::size_t slot = static_cast<std::size_t>(k % ringSize_);
        const double p = grainR_[slot] + grainDelta_[slot] +
                         grainRatio_[slot] * static_cast<double>(n - k * Hg_);
        if (first || p < minP) {
          minP = p;
          first = false;
        }
        if (!finishMode && p < static_cast<double>(nIn)) {
          // Taps at or beyond N_in are semantic zeros (§7.6) — never awaited;
          // real taps must be delivered.
          const int64_t highestTap = std::llround(p) + k_;
          if (highestTap < nIn && highestTap > consumedTotal_ - 1) ready = false;
        }
      }
      if (!first) lowestRead_ = minP;  // the floor anchor (even when blocked)
      if (!ready) break;

      // ---- OLA with local-sum normalisation (§6.5.1 item 5) ----
      for (ChannelCount c = 0; c < ctx_.channels; ++c) {
        const SlidingWindow& w = windows_[static_cast<std::size_t>(c)];
        double acc = 0.0;
        double wsum = 0.0;
        for (int64_t k = kMin; k <= kMax; ++k) {
          const int64_t j = n - k * Hg_;  // grain-local frame ∈ [0, G)
          if (j < 0 || j >= G_) continue;  // safety (set membership is exact)
          const double win = windowTable_[static_cast<std::size_t>(j)];
          if (win == 0.0) continue;
          const std::size_t slot = static_cast<std::size_t>(k % ringSize_);
          const double p = grainR_[slot] + grainDelta_[slot] +
                           grainRatio_[slot] * static_cast<double>(j);
          acc += win * interpolateAt(w.data.data(), w.count,
                                     p - static_cast<double>(w.start), 1.0,
                                     ResampleQuality::Small);
          wsum += win;
        }
        out.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(produced)] =
            (wsum > 1e-12) ? (acc / wsum) : 0.0;
      }
      ++produced;
      ++outN_;
      if (finishMode) ++flushEmitted_;
    }

    compactWindows();
    return produced;
  }
};

}  // namespace

std::unique_ptr<PitchEngine> makeGranularEngine() {
  return std::make_unique<GranularEngine>();
}

// Engine-owned discrete choices (Task 29): the window shapes the engine's
// own configure() accepts (hann | triangular, §6.5.1 item 1).
const char* const kGranularWindowChoices[] = {"hann", "triangular"};

EngineDescriptor granularEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = "native.granular";
  d.info.displayName = "Granular — textural pitch engine (creative-leaning)";
  d.info.version = "0.1.0";
  d.info.usageClass = UsageClass::Prototype;
  d.info.origin = "own implementation";
  d.info.license = "own code, no dependency";
  d.capabilities.minRatio = 0.125;
  d.capabilities.maxRatio = 8.0;
  d.capabilities.supportsDynamicRatio = true;
  // §6.5.1 item 9: the honest static declaration — the control hop is
  // Hg = G/overlap, parameter- and rate-dependent (blockFrames = 0 records
  // "per-grain hop; derive from the recorded parameters"); the analyzer
  // measures the effective control rate independently (§10).
  d.capabilities.controlRate.kind = ControlRateSpec::Kind::FixedBlock;
  d.capabilities.controlRate.blockFrames = 0;
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.45;
  d.capabilities.bandwidth.notes =
      "grain-rate AM and comb coloration are the family artefacts (intentional, "
      "not normalised away); no AA pre-filter (sheet §6.5 specifies none): "
      "ratio > 1 excursions alias above Nyquist/ratio; jitter_frames breaks "
      "the comb (seeded dither of the grain read start)";
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.determinism = Determinism::SeededDeterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
  d.parameterKeys = {"grain_seconds", "overlap", "window", "jitter_frames"};
  // Engine-owned parameter descriptors (Task 29): all four grain parameters
  // are product-exposed (the creative control surface); window is a Text
  // choice (hann | triangular — the engine's own domain, §6.5.1 item 1).
  d.parameters = {
      {
          .key = "grain_seconds",
          .displayName = "Grain Length",
          .kind = EngineParamKind::Real,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.02,
          .max = 0.5,
          .defaultPlain = 0.1,  // v0.1 default
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
          .key = "overlap",
          .displayName = "Overlap",
          .kind = EngineParamKind::Integer,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 4.0,
          .max = 16.0,
          .defaultPlain = 4.0,  // v0.1 default
          .unit = "x",
          .stepCount = 12,
          .choiceNames = nullptr,
          .choiceValues = nullptr,
          .choiceCount = 0,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%d",
      },
      {
          .key = "window",
          .displayName = "Window",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 1.0,
          .defaultPlain = 0.0,  // "hann" (v0.1 default)
          .unit = "",
          .stepCount = 1,
          .choiceNames = kGranularWindowChoices,
          .choiceValues = nullptr,
          .choiceCount = 2,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
      },
      {
          .key = "jitter_frames",
          .displayName = "Jitter",
          .kind = EngineParamKind::Integer,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 256.0,
          .defaultPlain = 0.0,  // v0.1 default
          .unit = "frames",
          .stepCount = 256,
          .choiceNames = nullptr,
          .choiceValues = nullptr,
          .choiceCount = 0,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%d",
      },
  };
  d.factory = &makeGranularEngine;
  return d;
}

}  // namespace pitchlab
