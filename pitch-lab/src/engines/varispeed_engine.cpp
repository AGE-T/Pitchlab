#include "engines/varispeed_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/engine_registry.h"
#include "core/errors.h"

namespace pitchlab {
namespace {

// ---------------------------------------------------------------------------
// §6.1.1 item 8: absolute-indexed sliding window (the sheet's "ring buffer"
// in honest general form). Pre-allocated in prepare(); front-compaction via
// memmove only — NO allocation after prepare(). Invariant: [start,
// start+count); front-drops PRESERVE the window end (start+count), appends
// advance it.
// ---------------------------------------------------------------------------
struct SlidingWindow {
  std::vector<double> data;
  int64_t start = 0;  // absolute index of data[0]
  int64_t count = 0;  // valid frames [start, start+count)

  void allocate(int64_t capacity, int64_t startIdx) {
    data.assign(static_cast<std::size_t>(capacity), 0.0);
    start = startIdx;
    count = 0;
  }

  [[nodiscard]] double at(int64_t absIdx) const {
    return data[static_cast<std::size_t>(absIdx - start)];
  }

  /// Append n frames at [start+count, start+count+n). Throws EngineException
  /// on overflow (unreachable by the sizing + drop policy — never silent).
  void append(const double* src, int64_t n) {
    if (n <= 0) return;
    if (count + n > static_cast<int64_t>(data.size())) {
      throw EngineException(
          "native.varispeed",
          "internal raw window overflow (sizing bug; report as engine defect)");
    }
    std::memcpy(data.data() + count, src, sizeof(double) * static_cast<std::size_t>(n));
    count += n;
  }

  /// Drop frames below `absIdx` (front compaction; memmove only; preserves
  /// the window end).
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
// The engine (§6.1 sheet + §6.1.1 frozen implementation behaviour)
// ---------------------------------------------------------------------------
class VarispeedEngine final : public PitchEngine {
 public:
  const char* engineId() const override { return "native.varispeed"; }

  void configure(const EngineConfiguration& cfg) override {
    quality_ = ResampleQuality::Reference;
    if (const ParameterValue* v = cfg.find("resample_quality")) {
      if (const auto* s = std::get_if<std::string>(v)) {
        if (*s == "small") {
          quality_ = ResampleQuality::Small;
        } else if (*s == "standard") {
          quality_ = ResampleQuality::Standard;
        } else if (*s == "reference") {
          quality_ = ResampleQuality::Reference;
        } else {
          throw ConfigError("", "resample_quality",
                            "unknown quality '" + *s +
                                "' (small|standard|reference, §6.1.1 item 1)");
        }
      } else {
        throw ConfigError("", "resample_quality",
                          "resample_quality must be a string (small|standard|reference)");
      }
    }
    allowAliasing_ = false;
    if (const ParameterValue* v = cfg.find("allow_aliasing")) {
      if (const auto* b = std::get_if<bool>(v)) {
        allowAliasing_ = *b;
      } else {
        throw ConfigError("", "allow_aliasing", "allow_aliasing must be a boolean");
      }
    }
    // The seed is accepted and unused (Deterministic engine, §6.1.1 item 1).
    prepared_ = false;  // a configuration change invalidates prepared state
  }

  void prepare(const ProcessContext& ctx) override {
    if (!(ctx.sampleRate > 0.0) || !std::isfinite(ctx.sampleRate)) {
      throw ConfigError("", "sampleRate", "sample rate must be finite and positive");
    }
    if (ctx.channels < 1 || ctx.channels > 2) {
      throw ConfigError("", "channels",
                        "native.varispeed supports 1-2 channels (declared MonoAndStereo)");
    }
    if (ctx.maxBlockFrames <= 0) {
      throw ConfigError("", "maxBlockFrames", "max block frames must be positive");
    }
    if (ctx.totalInputFrames < 0) {
      throw ConfigError("", "totalInputFrames", "total input frames must be >= 0");
    }
    if (ctx.curve == nullptr || ctx.curve->ratio == nullptr ||
        ctx.curve->frames != ctx.totalInputFrames) {
      throw ConfigError("", "curve",
                        "curve view missing or frame count mismatch (view frames must equal N_in)");
    }

    // §4.4.3.1 item 12 guarantees finite/positive upstream, but the engine is
    // independently reusable: verify while scanning r_min/r_max (§6.1.1 item 3).
    const double* ratio = ctx.curve->ratio;
    double rMin = ratio[0];
    double rMax = ratio[0];
    for (FrameCount i = 1; i < ctx.totalInputFrames; ++i) {
      const double v = ratio[static_cast<std::size_t>(i)];
      if (!std::isfinite(v) || v <= 0.0) {
        throw ConfigError("", "curve", "curve ratio at frame " + std::to_string(i) +
                                           " is non-finite or non-positive (§4.4.1)");
      }
      rMin = std::min(rMin, v);
      rMax = std::max(rMax, v);
    }

    ctx_ = ctx;
    k_ = resampleKernelSpec(quality_).halfWidthTaps;
    firActive_ = (rMax > 1.0) && !allowAliasing_;
    cutoff_ = antiAliasCutoff(rMax);  // §7.2.1 item 6 (whole-curve max)
    fir_.clear();
    if (firActive_) {
      fir_ = designAntiAliasFir(cutoff_, quality_);  // shared design (§7.2.1 item 8)
    }
    lookahead_ = k_ + (firActive_ ? k_ : 0);  // §6.1.1 item 5
    latency_.inputLatencyFrames = lookahead_;
    latency_.outputLatencyFrames =
        static_cast<FrameCount>(std::ceil(static_cast<double>(k_) / rMin));

    // Windows (§6.1.1 item 8), sized with wide margins: raw holds
    // [filterNext-K, delivered); the interpolation-side window holds
    // [round(r)-K, windowEnd).
    const int64_t windowCapacity = static_cast<int64_t>(ctx.maxBlockFrames) + 8 * k_ + 64;
    raw_.resize(static_cast<std::size_t>(ctx.channels));
    filtered_.resize(static_cast<std::size_t>(ctx.channels));
    for (ChannelCount c = 0; c < ctx.channels; ++c) {
      raw_[static_cast<std::size_t>(c)].allocate(windowCapacity, 0);
      // The filtered window starts at -K when the FIR is active: x̂[n<0] is
      // the partial FIR with leading zero taps — NOT zero itself.
      filtered_[static_cast<std::size_t>(c)].allocate(windowCapacity, firActive_ ? -k_ : 0);
    }
    resetStreaming();
    prepared_ = true;
  }

  Latency latency() const override { return latency_; }

  ProcessReport process(const AudioBlockView& in, int inFrames, AudioBlockOut& out,
                        int outCapacity, const PitchCurveView& curve,
                        FrameCount inputFrameIndex) override {
    if (!prepared_) {
      throw EngineException("native.varispeed", "process() called before prepare()");
    }
    if (inFrames < 0 || outCapacity < 0) {
      throw EngineException("native.varispeed", "negative inFrames/outCapacity");
    }
    if (inputFrameIndex != consumedTotal_) {
      throw EngineException("native.varispeed",
                            "inputFrameIndex != cumulative consumed (contract precondition, §4.2)");
    }
    if (in.channelCount != ctx_.channels || out.channelCount != ctx_.channels) {
      throw EngineException("native.varispeed", "channel count mismatch in block views");
    }
    if (curve.ratio != ctx_.curve->ratio || curve.frames != ctx_.curve->frames) {
      throw EngineException("native.varispeed",
                            "curve view differs from the prepared one (contract: same object)");
    }

    // ---- 1) consume offered input up to window capacity (§6.1.1 item 6) ----
    int64_t consumed = 0;
    if (inFrames > 0) {
      // Compact raw below the oldest frame the filter will need next
      // (filterNext-K; the read-low bound when the FIR is bypassed).
      const int64_t rawLow = firActive_ ? (filterNext_ - k_) : readLowBound();
      for (ChannelCount c = 0; c < ctx_.channels; ++c) {
        raw_[static_cast<std::size_t>(c)].dropBefore(rawLow);
      }
      int64_t space = raw_[0].freeSpace();
      for (ChannelCount c = 1; c < ctx_.channels; ++c) {
        space = std::min(space, raw_[static_cast<std::size_t>(c)].freeSpace());
      }
      consumed = std::min<int64_t>(inFrames, space);
      if (consumed > 0) {
        for (ChannelCount c = 0; c < ctx_.channels; ++c) {
          raw_[static_cast<std::size_t>(c)].append(in.channels[static_cast<std::size_t>(c)],
                                                   consumed);
        }
        consumedTotal_ += consumed;
      }
    }

    // ---- 2) extend the filtered signal as far as raw availability permits ----
    extendFiltered(/*finishMode=*/false);

    // ---- 3) produce output frames (strict-delivery gating) ----
    const int produced = produceInto(out, outCapacity, /*inFinish=*/false);

    // ---- 4) exhaustion flag (§4.2.1 item 7) ----
    const FrameCount streamEnd = ctx_.totalInputFrames + latency_.inputLatencyFrames;
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= streamEnd);

    return ProcessReport{consumed, produced, inputExhausted_};
  }

  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    if (!prepared_) {
      throw EngineException("native.varispeed", "finish() called before prepare()");
    }
    if (finished_) {
      throw EngineException("native.varispeed", "finish() called twice (contract: exactly once)");
    }
    if (outCapacity < 0) {
      throw EngineException("native.varispeed", "negative outCapacity");
    }
    if (out.channelCount != ctx_.channels) {
      throw EngineException("native.varispeed", "channel count mismatch in output view");
    }
    finished_ = true;

    // The padded stream has been fully delivered (§4.3.5); the filter extends
    // through the emission max tap with beyond-stream zero taps (§6.1.1 item 7).
    extendFiltered(/*finishMode=*/true);

    // Emit the remaining emission-condition frames, bounded by the declared
    // outputLatencyFrames (§6.1.1 item 7 — the bound holds by construction;
    // the cap is the honest guard).
    const int produced = produceInto(out, outCapacity, /*inFinish=*/true);
    return ProcessReport{0, produced, inputExhausted_};
  }

  void reset() override {
    if (!prepared_) return;  // configuration only — nothing to reset
    for (ChannelCount c = 0; c < ctx_.channels; ++c) {
      raw_[static_cast<std::size_t>(c)].allocate(raw_[static_cast<std::size_t>(c)].capacity(), 0);
      filtered_[static_cast<std::size_t>(c)].allocate(
          filtered_[static_cast<std::size_t>(c)].capacity(), firActive_ ? -k_ : 0);
    }
    resetStreaming();
  }

 private:
  [[nodiscard]] int64_t readLowBound() const {
    // Lowest tap the next interpolation needs: round(r) - K.
    return static_cast<int64_t>(std::llround(readPos_)) - k_;
  }

  void resetStreaming() {
    readPos_ = 0.0;
    producedTotal_ = 0;
    consumedTotal_ = 0;
    filterNext_ = firActive_ ? -k_ : 0;
    inputExhausted_ = false;
    finished_ = false;
    flushEmitted_ = 0;
  }

  /// Extend the filtered windows. Process mode: x̂[n] computable iff
  /// n+K <= rawNext-1 (§6.1.1 items 4/6). Finish mode: the emission max tap
  /// is N_in+2K-1 (§6.1.1 item 7), so x̂ is extended through streamEnd-1
  /// with raw taps beyond the padded stream reading ZERO (semantic zeros —
  /// the stream is over). Ascending-j FIR summation — the SAME formula and
  /// tap order as applyFirZeroPhase (bit-identical to whole-buffer filtering
  /// over the padded stream). Window invariant: filtered.end() ==
  /// filterNext_ (front-drops preserve the end; appends advance it).
  void extendFiltered(bool finishMode) {
    if (!firActive_) return;  // bypass: x̂ ≡ raw (interpolation reads raw)
    const int64_t rawNext = raw_[0].end();
    const int64_t streamEnd = ctx_.totalInputFrames + latency_.inputLatencyFrames;
    int64_t filterTarget = rawNext - k_;  // process mode: n <= rawNext-1-K
    if (finishMode) {
      // x̂ through the emission max tap N_in+2K-1 (== streamEnd-1 when the
      // FIR is active: padding is 2K).
      filterTarget = std::max(filterTarget, ctx_.totalInputFrames + 2 * k_);
    }
    while (filterNext_ < filterTarget) {
      for (ChannelCount c = 0; c < ctx_.channels; ++c) {
        SlidingWindow& fil = filtered_[static_cast<std::size_t>(c)];
        if (fil.count >= fil.capacity()) {
          fil.dropBefore(readLowBound());  // make space (preserves the end)
        }
        if (fil.count >= fil.capacity()) {
          throw EngineException(
              "native.varispeed",
              "filtered window exhausted (renderer capacity starvation beyond design; "
              "the frozen renderer capacity policy of §4.2.1 item 6 prevents this)");
        }
        const SlidingWindow& raw = raw_[static_cast<std::size_t>(c)];
        double acc = 0.0;
        for (int j = -k_; j <= k_; ++j) {  // ascending tap order (determinism)
          const int64_t p = filterNext_ + j;
          if (p < 0) continue;  // leading semantic zeros (§7.6)
          if (p >= raw.end()) {
            if (finishMode && p >= streamEnd) continue;  // beyond-stream zeros
            throw EngineException("native.varispeed",
                                  "internal FIR window bounds violated (sizing/gating bug; "
                                  "report as engine defect)");
          }
          if (p < raw.start) {
            throw EngineException("native.varispeed",
                                  "internal FIR window underrun (drop-policy bug; report as "
                                  "engine defect)");
          }
          acc += fir_[static_cast<std::size_t>(j + k_)] * raw.at(p);
        }
        fil.data[static_cast<std::size_t>(fil.count)] = acc;  // slot = start+count == filterNext_
        fil.count += 1;
      }
      ++filterNext_;
    }
  }

  /// Produce output frames into `out` (up to outCapacity; emission condition;
  /// strict-delivery gate in process, flush bound in finish). NO allocation.
  int produceInto(AudioBlockOut& out, int outCapacity, bool inFinish) {
    if (outCapacity <= 0) return 0;
    const FrameCount nIn = ctx_.totalInputFrames;
    const double* ratio = ctx_.curve->ratio;
    int produced = 0;

    while (produced < outCapacity) {
      const int64_t rr = std::llround(readPos_);
      if (rr > nIn - 1 + k_) break;  // emission end (§6.1.1 item 6a)
      if (!inFinish) {
        // Strict-delivery gate (§6.1.1 item 6b): round(r) + lookahead <=
        // delivered-1 — every REAL frame the output depends on is delivered.
        const int64_t delivered = raw_[0].end();
        if (rr + lookahead_ > delivered - 1) break;
      }
      if (inFinish && flushEmitted_ >= latency_.outputLatencyFrames) {
        break;  // defensive flush-bound cap (§6.1.1 item 7); flushEmitted_
                // counts every frame emitted by finish() calls
      }
      // Coverage gate: every tap in [rr-K, rr+K] that is INSIDE the padded
      // stream must be present in the interpolation window (taps at or past
      // streamEnd are semantic zeros — interpolateAt's zero padding is the
      // correct reading; this gate guards only against in-stream taps the
      // window does not cover yet, e.g. under renderer capacity starvation).
      const int64_t streamEnd = nIn + latency_.inputLatencyFrames;
      const SlidingWindow& w0 = firActive_ ? filtered_[0] : raw_[0];
      const int64_t tapMax = rr + k_;
      if (tapMax >= w0.end() && tapMax < streamEnd) break;

      // Interpolate each channel at the read position on the filtered signal
      // (shared primitive §7 — no resampling code duplicated here; the read
      // position is SHARED across channels, §6.1.1 item 9).
      for (ChannelCount c = 0; c < ctx_.channels; ++c) {
        const SlidingWindow& w = firActive_ ? filtered_[static_cast<std::size_t>(c)]
                                           : raw_[static_cast<std::size_t>(c)];
        const double local = readPos_ - static_cast<double>(w.start);
        out.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(produced)] =
            interpolateAt(w.data.data(), w.count, local, cutoff_, quality_);
      }

      // Advance the read position: left-edge rectangular rule (§6.1).
      double idx = std::floor(readPos_);
      if (idx < 0.0) idx = 0.0;
      if (idx > static_cast<double>(nIn - 1)) idx = static_cast<double>(nIn - 1);
      readPos_ += ratio[static_cast<std::size_t>(static_cast<int64_t>(idx))];

      ++produced;
      if (inFinish) ++flushEmitted_;
    }
    producedTotal_ += produced;

    // Compact the interpolation-side window below the next read's lowest tap.
    if (produced > 0) {
      for (ChannelCount c = 0; c < ctx_.channels; ++c) {
        if (firActive_) {
          filtered_[static_cast<std::size_t>(c)].dropBefore(readLowBound());
        } else {
          raw_[static_cast<std::size_t>(c)].dropBefore(readLowBound());
        }
      }
    }
    return produced;
  }

  // ---- configuration ----
  ResampleQuality quality_ = ResampleQuality::Reference;
  bool allowAliasing_ = false;

  // ---- per-job state (prepare()) ----
  ProcessContext ctx_{};
  int k_ = 0;                    // kernel half-width (preset §7.3)
  bool firActive_ = false;       // r_max > 1 && !allow_aliasing (§6.1.1 item 3)
  double cutoff_ = 1.0;          // antiAliasCutoff(r_max) (§7.2.1 item 6)
  std::vector<double> fir_;      // designAntiAliasFir(c, quality) when active
  int64_t lookahead_ = 0;        // K + (FIR active ? K : 0) (§6.1.1 item 5)
  Latency latency_{};
  std::vector<SlidingWindow> raw_;       // per channel: delivered raw input
  std::vector<SlidingWindow> filtered_;  // per channel: x̂ (unused when bypassed)
  bool prepared_ = false;

  // ---- streaming state ----
  double readPos_ = 0.0;  // r (§6.1 recurrence)
  FrameCount producedTotal_ = 0;
  FrameCount consumedTotal_ = 0;
  int64_t filterNext_ = 0;  // next filtered index to compute
  bool inputExhausted_ = false;
  bool finished_ = false;
  FrameCount flushEmitted_ = 0;
};

}  // namespace

std::unique_ptr<PitchEngine> makeVarispeedEngine() {
  return std::make_unique<VarispeedEngine>();
}

EngineDescriptor varispeedEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = "native.varispeed";
  d.info.displayName = "Varispeed (rate-following reference)";
  d.info.version = "0.1.0";
  d.info.usageClass = UsageClass::Prototype;
  d.info.origin = "own implementation";
  d.info.license = "own code, no dependency";
  d.isReferenceRole = true;  // harness reference engine (§H.2)
  d.capabilities.minRatio = 0.0625;
  d.capabilities.maxRatio = 16.0;
  d.capabilities.supportsDynamicRatio = true;
  d.capabilities.controlRate.kind = ControlRateSpec::Kind::PerSample;
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.45;
  d.capabilities.bandwidth.notes =
      "AA pre-filter at 0.95*Ny/r_max (whole-curve max, conservative); kernel "
      "cutoff 0.95/r_max for r_max>1; content legitimately moves with ratio; "
      "allow_aliasing=true disables the pre-filter (creative)";
  d.capabilities.duration = DurationBehaviour::RateFollowing;
  d.capabilities.determinism = Determinism::Deterministic;
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
  d.parameterKeys = {"resample_quality", "allow_aliasing"};
  d.factory = &makeVarispeedEngine;
  return d;
}

}  // namespace pitchlab
