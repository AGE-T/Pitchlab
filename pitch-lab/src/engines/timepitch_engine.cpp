// Pitch Lab — native.timepitch, the unified time-pitch engine (see
// timepitch_engine.h for the frozen model + §6.6/§6.6.1 references).
//
// IMPLEMENTATION NOTES (task-33 checkpoint discipline):
//   * This translation unit ports the VALIDATED Task28 prototype arithmetic
//     into the production PitchEngine contract — the §6.6.1 items name the
//     normative prototype references per mode (Fixed/OLA: candidate_ola.cpp,
//     the placeGrainsUpTo/emitFinalFrames/compaction forms verbatim). The
//     arithmetic (accumulation orders, normalisation, compaction, latency
//     formulas) is copied EXACTLY — production plumbing (the EngineConfiguration
//     bag, the ProcessContext geometry, the ProcessReport accounting) wraps it.
//   * The mode seam: placeGrainsUpTo() calls grainAnalysisAdjustment() (the
//     placement law — Fixed returns 0; WSOLA overrides with the similarity
//     search), the grain path runs the content hook (the FD-PSOLA per-voiced-
//     grain transform attaches here), and accumulation goes through
//     accumulateGrain() (the mode-selected policy — window-product for
//     Fixed/Adaptive, raw OLA + s/P for Pitch-Synced modes). The policies are
//     NEVER merged (§6.6.1 item 6 — the owner architectural principle).
//   * All job state is allocated in prepare(); process()/finish()/reset()
//     never allocate (audited by T-A1/T-D3).

#include "engines/timepitch_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "core/errors.h"
#include "core/resampler.h"

namespace pitchlab {
namespace {

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

class TimePitchEngine final : public PitchEngine {
 public:
  const char* engineId() const override { return "native.timepitch"; }

  void configure(const EngineConfiguration& cfg) override {
    // §6.6.1 item 1: the seven-descriptor model, domains/defaults VERBATIM
    // from the validated prototypes. Invalid values ⇒ CONFIG ERROR here;
    // the product layer clamps, never rejects (the house rule) — a rejection
    // at this layer is by definition outside the declared descriptor domain.
    // Defaults are applied first, then any present key overrides.
    mode_ = "fixed";
    windowFrames_ = 2048;
    overlap_ = 2;
    shape_ = "hann";

    for (const auto& [key, value] : cfg.parameters) {
      if (key == "mode") {
        if (const auto* s = std::get_if<std::string>(&value)) {
          // Mode checkpoint discipline (see the header): an unimplemented
          // mode is a CONFIG ERROR — never a disguised substitute. The
          // choice list grows as each checkpoint lands; the frozen final
          // set is fixed|adaptive|pitch_synced|pitch_formant (§6.6).
          if (*s != "fixed") {
            throw ConfigError("", "mode",
                              "mode '" + *s + "' is not available in this build (Fixed=OLA)");
          }
          mode_ = *s;
        } else {
          throw ConfigError("", "mode", "mode must be a string");
        }
      } else if (key == "window_frames") {
        if (const auto* i = std::get_if<int64_t>(&value)) {
          if (*i < 64 || *i > 16384) {
            throw ConfigError("", "window_frames",
                              "window_frames must be an integer in [64, 16384]");
          }
          windowFrames_ = static_cast<int>(*i);
        } else {
          throw ConfigError("", "window_frames", "window_frames must be an integer");
        }
      } else if (key == "overlap") {
        if (const auto* i = std::get_if<int64_t>(&value)) {
          if (*i != 2 && *i != 4 && *i != 8) {
            throw ConfigError("", "overlap", "overlap must be one of {2, 4, 8}");
          }
          overlap_ = static_cast<int>(*i);
        } else {
          throw ConfigError("", "overlap", "overlap must be an integer");
        }
      } else if (key == "window_shape") {
        if (const auto* s = std::get_if<std::string>(&value)) {
          if (*s != "hann" && *s != "hamming" && *s != "bartlett" && *s != "rect") {
            throw ConfigError("", "window_shape",
                              "window_shape must be one of hann|hamming|bartlett|rect");
          }
          shape_ = *s;
        } else {
          throw ConfigError("", "window_shape", "window_shape must be a string");
        }
      } else {
        // The key set is the descriptor's parameterKeys (validated both
        // sides); an unknown key here is a harness/registry coherence bug.
        throw ConfigError("", key, "unknown parameter for native.timepitch");
      }
    }
    if (windowFrames_ / overlap_ < 8) {
      throw ConfigError("", "overlap",
                        "synthesis hop Hs = N/overlap must be >= 8 frames");
    }
    prepared_ = false;  // a configuration change invalidates prepared state
  }

  void prepare(const ProcessContext& ctx) override {
    if (!(ctx.sampleRate > 0.0) || !std::isfinite(ctx.sampleRate) ||
        ctx.channels < 1 || ctx.channels > 2 || ctx.maxBlockFrames < 1 ||
        ctx.totalInputFrames < 1 || ctx.curve == nullptr ||
        ctx.curve->ratio == nullptr || ctx.curve->frames != ctx.totalInputFrames) {
      throw ConfigError("", "prepare", "invalid job geometry (§6.6.1 item 14)");
    }
    fs_ = ctx.sampleRate;
    channels_ = static_cast<int>(ctx.channels);
    maxBlock_ = ctx.maxBlockFrames;
    nIn_ = ctx.totalInputFrames;
    curve_ = *ctx.curve;
    hs_ = windowFrames_ / overlap_;
    window_ = makeWindow(windowFrames_, shape_);

    // Sliding input window: the analysis reach below the next grain centre
    // (N/2, plus the WSOLA back-margin when that mode lands — 0 for Fixed)
    // + the block arrival quantum + margin (the candidate_ola.cpp sizing).
    inputBackMargin_ = 0;  // Fixed/OLA: the search reach lands with Adaptive
    const int64_t inputCapacity =
        static_cast<int64_t>(windowFrames_) + inputBackMargin_ +
        2 * static_cast<int64_t>(maxBlock_) + 256;

    // Live stretch window bound: unfinalised accum tail (N beyond the last
    // placed centre) + final region not yet resampled (the final-frontier
    // lead, N/2 + Hs) + resampler taps + margin (candidate_ola.cpp sizing).
    const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
    stretchCapacity_ = static_cast<int64_t>(windowFrames_) * 2 +
                       static_cast<int64_t>(hs_) * 2 + 2 * static_cast<int64_t>(K) +
                       2 * static_cast<int64_t>(maxBlock_) + 256;

    stretch_.assign(static_cast<std::size_t>(channels_),
                    std::vector<double>(static_cast<std::size_t>(stretchCapacity_), 0.0));
    wsum_.assign(static_cast<std::size_t>(stretchCapacity_), 0.0);
    inBuf_.assign(static_cast<std::size_t>(channels_),
                  std::vector<double>(static_cast<std::size_t>(inputCapacity), 0.0));
    grainScratch_.assign(
        static_cast<std::size_t>(channels_) * static_cast<std::size_t>(windowFrames_) + 16,
        0.0);
    resetJobState();

    // Declared latency (§6.6.1 item 10 — the Fixed/Adaptive composition,
    // verbatim from candidate_ola.cpp:164-179): the OUTPUT side drains the
    // tail stretch content through the slowest rate the effective curve can
    // produce (the whole-curve min — prepare-time, constant per job).
    latencyIn_ = static_cast<FrameCount>(windowFrames_) / 2 + hs_ + 2 * K + 64;
    double minRatio = 1.0;
    for (FrameCount i = 0; i < curve_.frames; ++i) {
      minRatio = std::min(minRatio, curve_.ratio[static_cast<std::size_t>(i)]);
    }
    if (!(minRatio > 0.0)) minRatio = 0.5;
    latencyOut_ = static_cast<FrameCount>(
        static_cast<double>(static_cast<FrameCount>(windowFrames_) + hs_ + 2 * K + 256) /
        minRatio) + 64;
    prepared_ = true;
  }

  Latency latency() const override {
    return Latency{latencyIn_, latencyOut_};
  }

  ProcessReport process(const AudioBlockView& in, int inFrames,
                        AudioBlockOut& out, int outCapacity,
                        const PitchCurveView& curve,
                        FrameCount inputFrameIndex) override {
    if (!prepared_) {
      throw EngineException("native.timepitch", "process() before prepare()");
    }
    if (inFrames < 0 || inFrames > maxBlock_ || inputFrameIndex != inAvail_) {
      throw EngineException("native.timepitch", "block contract violation");
    }
    // The curve view is the SAME object every call (§4.2.1 item 3); refresh
    // the stored view so finish() (which takes no curve argument) reads the
    // job's effective curve.
    curve_ = curve;

    // Absorb the input into the sliding input window (candidate_ola.cpp
    // process(): compact, bounds-check, copy, advance).
    compactInput();
    if (inFrames > 0) {
      if (in.channels == nullptr) {
        throw EngineException("native.timepitch", "null input channels");
      }
      const FrameCount rel = inAvail_ - inBase_;
      const auto cap = static_cast<FrameCount>(inBuf_[0].size());
      if (rel + inFrames > cap) {
        // compaction should prevent this; if not, it is an engine bug.
        throw EngineException("native.timepitch", "input window overflow (sizing bug; report as engine defect)");
      }
      for (int c = 0; c < channels_; ++c) {
        std::memcpy(inBuf_[static_cast<std::size_t>(c)].data() +
                        static_cast<std::size_t>(rel),
                    in.channels[static_cast<std::size_t>(c)],
                    static_cast<std::size_t>(inFrames) * sizeof(double));
      }
    }
    inAvail_ += inFrames;
    consumedTotal_ += inFrames;

    placeGrainsUpTo(inAvail_);

    FrameCount produced = 0;
    if (outCapacity > 0 && out.channels != nullptr) {
      emitFinalFrames(out, outCapacity, produced, /*phaseLimit=*/nIn_);
    }
    // Sticky: set when the cumulative consumption reaches the full padded
    // stream (the real input + the declared zero-padding lookahead).
    const FrameCount streamEnd = nIn_ + latencyIn_;
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= streamEnd);
    ProcessReport rep;
    rep.inputFramesConsumed = inFrames;
    rep.outputFramesProduced = produced;
    rep.inputExhausted = inputExhausted_;
    return rep;
  }

  ProcessReport finish(AudioBlockOut& out, int outCapacity) override {
    if (!prepared_) {
      throw EngineException("native.timepitch", "finish() before prepare()");
    }
    if (finishing_) {
      throw EngineException("native.timepitch", "finish called twice");
    }
    finishing_ = true;
    stretchEndD_ = std::numeric_limits<double>::max();  // until grains exhaust
    // Interleave grain placement with emission: the stretch live window is
    // bounded, so at small caller capacities the remaining grains can only
    // be placed as the resampler drains (back-pressure by design — the
    // candidate_ola.cpp finish() form). The finish-phase emission limit is
    // the CANONICAL length N_in + G (§6.6.1 item 7: the tail region
    // [N_in, N_in + G) — exactly G frames; the engine stops there
    // regardless of grain state).
    FrameCount produced = 0;
    for (int64_t guard = 0; guard < (1 << 24); ++guard) {
      const bool placedAny = placeGrainsUpTo(0);
      const bool grainsExhausted =
          (aNext_ - static_cast<double>(windowFrames_) / 2.0 >= static_cast<double>(nIn_));
      if (grainsExhausted) {
        stretchEndD_ = sNext_ + static_cast<double>(windowFrames_) / 2.0;
      }
      const FrameCount before = produced;
      if (outCapacity > 0 && out.channels != nullptr) {
        emitFinalFrames(out, outCapacity, produced,
                        /*phaseLimit=*/nIn_ + latencyOut_);
      }
      if (produced == before && (!placedAny || grainsExhausted)) {
        // Fully drained (the phase limit binds), or stalled on caller
        // capacity. The canonical-length guarantee does not depend on the
        // grain state: once grains are exhausted the tail normalisation
        // extends to the stretch extent and the emission runs to the phase
        // limit in every case.
        break;
      }
    }
    ProcessReport rep;
    rep.inputFramesConsumed = 0;
    rep.outputFramesProduced = produced;
    rep.inputExhausted = inputExhausted_;  // sticky (already true post-padding)
    return rep;
  }

  void reset() override {
    // T-D3: clear ALL DSP state, keep the configuration; NO allocation
    // (buffers are refilled with zeros in place; the window bank is
    // config-derived and survives — §6.6.1 item 8's tracker clause general
    // form). After reset, the same input with the same curve produces
    // bit-identical output to a fresh instance.
    resetJobState();
  }

 private:
  // --- configuration (validated in configure(); survives reset()) ---------
  std::string mode_ = "fixed";
  int windowFrames_ = 2048;
  int overlap_ = 2;
  std::string shape_ = "hann";

  // --- job state (all allocated in prepare()) -----------------------------
  double fs_ = 0.0;
  int channels_ = 0;
  int maxBlock_ = 0;
  FrameCount nIn_ = 0;
  PitchCurveView curve_{};
  bool prepared_ = false;
  int hs_ = 0;                                    // synthesis hop Hs = N/overlap
  std::vector<double> window_;                    // length N (config-derived)
  std::vector<std::vector<double>> stretch_;      // OLA accumulators (normalised
                                                  // in place below the frontier)
  std::vector<double> wsum_;                      // window sum per stretch position
  std::vector<std::vector<double>> inBuf_;        // sliding input window
  std::vector<double> grainScratch_;              // planar per-grain extraction
  FrameCount inBase_ = 0;                         // absolute index of inBuf_[c][0]
  FrameCount inAvail_ = 0;                        // absolute end of delivered input
  FrameCount consumedTotal_ = 0;                  // cumulative consumed (§4.2.1 item 7)
  bool inputExhausted_ = false;                   // sticky exhaustion flag
  int inputBackMargin_ = 0;                       // extra history margin below a − N/2
                                                  // (WSOLA search reach; 0 for Fixed)
  FrameCount stretchBase_ = 0;                    // absolute index of stretch_[c][0]
  FrameCount stretchCapacity_ = 0;
  FrameCount finalFrontier_ = 0;                  // absolute: < finalFrontier_ is final
  double aNext_ = 0.0;                            // next grain analysis centre (input tl)
  double sNext_ = 0.0;                            // next grain synthesis centre (stretch tl)
  int64_t grainsPlaced_ = 0;
  double q_ = 0.0;                                // resampler absolute stretch position
  FrameCount tOut_ = 0;                           // absolute output position (== produced)
  bool finishing_ = false;
  double stretchEndD_ = 0.0;                      // finish() drain target (stretch tl)
  FrameCount latencyIn_ = 0;
  FrameCount latencyOut_ = 0;

  // --- deterministic window bank (candidate_ola.cpp makeWindow verbatim) --
  static std::vector<double> makeWindow(int n, const std::string& shape) {
    constexpr double kPi = 3.14159265358979323846;
    std::vector<double> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      const double u = static_cast<double>(i) / static_cast<double>(n);
      double v = 0.0;
      if (shape == "hann") {
        v = 0.5 * (1.0 - std::cos(2.0 * kPi * u));  // periodic
      } else if (shape == "hamming") {
        v = 0.54 - 0.46 * std::cos(2.0 * kPi * u);  // periodic
      } else if (shape == "bartlett") {
        v = 1.0 - std::abs(2.0 * u - 1.0);
      } else {  // rect
        v = 1.0;
      }
      w[static_cast<std::size_t>(i)] = v;
    }
    return w;
  }

  /// Reset all job cursors + zero the state buffers in place (prepare() and
  /// reset() share this path; NO allocation — the buffers already exist).
  void resetJobState() {
    for (auto& b : stretch_) std::fill(b.begin(), b.end(), 0.0);
    std::fill(wsum_.begin(), wsum_.end(), 0.0);
    for (auto& b : inBuf_) std::fill(b.begin(), b.end(), 0.0);
    std::fill(grainScratch_.begin(), grainScratch_.end(), 0.0);
    inBase_ = 0;
    inAvail_ = 0;
    consumedTotal_ = 0;
    inputExhausted_ = false;
    stretchBase_ = 0;
    finalFrontier_ = 0;
    aNext_ = static_cast<double>(windowFrames_) / 2.0;  // grain 0 covers [0, N)
    sNext_ = static_cast<double>(windowFrames_) / 2.0;
    grainsPlaced_ = 0;
    q_ = 0.0;
    tOut_ = 0;
    finishing_ = false;
    stretchEndD_ = 0.0;
  }

  // --- the placement law seam (§6.6.1 item 2) ------------------------------
  // Per-grain analysis-position adjustment: Fixed places every grain at the
  // nominal schedule position (returns 0 — candidate_ola.cpp verbatim).
  // Adaptive (WSOLA) replaces this with the similarity search; Pitch-Synced
  // modes bypass the nominal grid entirely (the mark schedule).
  [[nodiscard]] double grainAnalysisAdjustment(double /*aNominal*/) const { return 0.0; }

  // --- the accumulation policy seam (§6.6.1 items 3/6) ---------------------
  // Window-product normalisation family (Fixed/Adaptive): the grain is
  // added into the accumulators + the window sum, normalised against the
  // sum at the emission frontier. Raw-OLA + s/P (Pitch-Synced modes) lands
  // with its checkpoint — the two policies are NEVER merged.
  void accumulateGrain(int /*grainLen*/, const double lo) {
    for (int i = 0; i < windowFrames_; ++i) {
      const double sp = lo + static_cast<double>(i);
      const FrameCount rel = static_cast<FrameCount>(sp) - stretchBase_;
      if (rel < 0 || rel >= stretchCapacity_) continue;  // guarded above
      const double wv = window_[static_cast<std::size_t>(i)];
      if (wv == 0.0) continue;
      for (int c = 0; c < channels_; ++c) {
        stretch_[static_cast<std::size_t>(c)][static_cast<std::size_t>(rel)] +=
            grainScratch_[static_cast<std::size_t>(c) *
                              static_cast<std::size_t>(windowFrames_) +
                          static_cast<std::size_t>(i)];
      }
      wsum_[static_cast<std::size_t>(rel)] += wv;
    }
  }

  // --- shared segment core (candidate_ola.cpp arithmetic verbatim) --------

  [[nodiscard]] double ratioAtInput(double pos) const {
    if (curve_.ratio == nullptr || nIn_ <= 0) return 1.0;
    double p = std::floor(pos);
    if (p < 0.0) p = 0.0;
    if (p > static_cast<double>(nIn_ - 1)) p = static_cast<double>(nIn_ - 1);
    return curve_.ratio[static_cast<std::size_t>(p)];
  }

  [[nodiscard]] double ratioAtOutput(FrameCount t) const {
    if (curve_.ratio == nullptr || nIn_ <= 0) return 1.0;
    double p = static_cast<double>(t);
    if (p > static_cast<double>(nIn_ - 1)) p = static_cast<double>(nIn_ - 1);
    return curve_.ratio[static_cast<std::size_t>(p)];
  }

  [[nodiscard]] double readInput(int channel, FrameCount pos) const {
    const FrameCount rel = pos - inBase_;
    if (rel < 0) return 0.0;
    if (rel >= static_cast<FrameCount>(inBuf_[0].size())) return 0.0;
    return inBuf_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(rel)];
  }

  void compactInput() {
    // The keep target follows the next grain's reach below its window; the
    // §6.6.1 item 6 zero-region exemption can advance aNext_ past the
    // delivered stream (grains whose windows reach into the zero region
    // place before their full window is "delivered" — that region is never
    // awaited), so the target is CLAMPED to the delivered end: the sliding
    // window never compacts past inAvail_ (the append offset below stays
    // non-negative; reads beyond inAvail_ are the zeroed buffer tail —
    // §7.6 semantic zeros).
    const FrameCount keepFrom = std::max<FrameCount>(
        0,
        std::min(inAvail_, static_cast<FrameCount>(aNext_) - windowFrames_ / 2 -
                               inputBackMargin_ - 64));
    if (inBase_ >= keepFrom) return;
    const FrameCount drop = keepFrom - inBase_;
    const FrameCount size = inAvail_ - inBase_;
    if (drop >= size) {
      for (auto& b : inBuf_) std::fill(b.begin(), b.end(), 0.0);
      inBase_ = keepFrom;
      return;
    }
    for (auto& b : inBuf_) {
      std::memmove(b.data(), b.data() + static_cast<std::size_t>(drop),
                   static_cast<std::size_t>(size - drop) * sizeof(double));
      std::fill(b.begin() + static_cast<std::size_t>(size - drop), b.end(), 0.0);
    }
    inBase_ = keepFrom;
  }

  void compactStretch() {
    const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
    const FrameCount keepFrom = std::max<FrameCount>(
        0, static_cast<FrameCount>(q_) - K - 128);
    if (stretchBase_ >= keepFrom) return;
    const FrameCount drop = keepFrom - stretchBase_;
    const FrameCount live = static_cast<FrameCount>(
        std::max<double>(0.0, std::max(static_cast<double>(finalFrontier_),
                                       sNext_ + windowFrames_ / 2.0)) -
            static_cast<double>(stretchBase_));
    const FrameCount keep = std::min(live, stretchCapacity_);
    if (drop >= keep) {
      for (auto& b : stretch_) std::fill(b.begin(), b.end(), 0.0);
      std::fill(wsum_.begin(), wsum_.end(), 0.0);
      stretchBase_ = keepFrom;
      return;
    }
    for (auto& b : stretch_) {
      std::memmove(b.data(), b.data() + static_cast<std::size_t>(drop),
                   static_cast<std::size_t>(keep - drop) * sizeof(double));
      std::fill(b.begin() + static_cast<std::size_t>(keep - drop), b.end(), 0.0);
    }
    std::memmove(wsum_.data(), wsum_.data() + static_cast<std::size_t>(drop),
                 static_cast<std::size_t>(keep - drop) * sizeof(double));
    std::fill(wsum_.begin() + static_cast<std::size_t>(keep - drop), wsum_.end(), 0.0);
    stretchBase_ = keepFrom;
  }

  bool placeGrainsUpTo(FrameCount inputAvailableEnd) {
    // Place grains while (a) the whole analysis window [a-N/2, a+N/2) is
    // within the available input, and (b) the stretch live window fits
    // (candidate_ola.cpp placeGrainsUpTo verbatim).
    const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
    const double half = static_cast<double>(windowFrames_) / 2.0;
    bool placedAny = false;
    for (;;) {
      const double needEnd = aNext_ + half;
      const FrameCount inputLimit =
          finishing_ ? std::numeric_limits<FrameCount>::max() : inputAvailableEnd;
      // §6.6.1 item 6: the zero region beyond the real input is NEVER
      // awaited — a grain whose window extends past N_in places as soon as
      // its REAL reads are delivered (its beyond-N_in reads are §7.6
      // semantic zeros). Once every real-touching grain is placed AND the
      // real input is fully delivered (or finish() released the stream),
      // the stretch extent is final: run the tail normalisation and open
      // the emission gate (item 7's phase limit takes over).
      const bool streamFullyDelivered = finishing_ || (consumedTotal_ >= nIn_);
      const bool grainsExhausted =
          (aNext_ - half >= static_cast<double>(nIn_));
      if (grainsExhausted && streamFullyDelivered) {
        const double extent = sNext_ + half;
        normaliseUpTo(static_cast<FrameCount>(std::ceil(extent)));
        finalFrontier_ = std::numeric_limits<FrameCount>::max() / 2;
        break;
      }
      if (finishing_) {
        // In finish(): grains whose window still touches real input (the
        // exhausted+delivered case is handled above).
        if (grainsExhausted) break;
      } else if (needEnd > static_cast<double>(inputLimit)) {
        // Whole window not delivered yet. §6.6.1 item 6's zero-region
        // exemption: once the REAL input is fully delivered, a grain whose
        // window still touches real input places immediately — its reads
        // beyond N_in are §7.6 semantic zeros, never awaited. (The next
        // loop iteration hits the exhausted+delivered extension above.)
        if (!(streamFullyDelivered &&
              aNext_ - half < static_cast<double>(nIn_))) {
          break;
        }
      }

      // Stretch live-extent guard (back-pressure: stop placing).
      const FrameCount tailEnd =
          static_cast<FrameCount>(sNext_ + half) + 2 * (K + 8);
      const FrameCount liveLo = static_cast<FrameCount>(
          std::max(0.0, q_ - static_cast<double>(K) - 128.0));
      if (tailEnd - liveLo > stretchCapacity_ - 8) break;

      // Placement law (the WSOLA hook seam): the analysis-centre adjustment
      // for this grain. The tolerance region is centred on the TIME-SCALING
      // LAW's nominal position; the delta perturbs ONLY this grain — the
      // next nominal advances from the law, never from the chosen position,
      // so the content mapping cannot drift beyond ±tolerance by construction.
      const double delta = grainAnalysisAdjustment(aNext_);
      const double a = aNext_ + delta;
      const double s = sNext_;  // (synthesis centre: the exact stretch grid)

      // Extract the windowed grain into the scratch (per channel, planar);
      // the content hook (the FD-PSOLA transform seam) runs between
      // extraction and accumulation.
      const int n = windowFrames_;
      for (int c = 0; c < channels_; ++c) {
        double* g = grainScratch_.data() +
                    static_cast<std::size_t>(c) * static_cast<std::size_t>(n);
        for (int i = 0; i < n; ++i) {
          const double ap = a - half + static_cast<double>(i);
          g[static_cast<std::size_t>(i)] =
              window_[static_cast<std::size_t>(i)] *
              readInput(c, static_cast<FrameCount>(std::floor(ap)));
        }
      }

      // Accumulation policy (mode-selected; window-product for Fixed).
      compactStretch();
      const double lo = s - half;
      accumulateGrain(n, lo);
      ++grainsPlaced_;
      placedAny = true;

      // Advance: synthesis by the FIXED hop; the nominal analysis position
      // by the stretch hop (rho indexed at the nominal; the delta does NOT
      // re-anchor the schedule — the drift-free formulation).
      const double rhoNom = ratioAtInput(aNext_);
      aNext_ += static_cast<double>(hs_) / rhoNom;
      sNext_ = s + static_cast<double>(hs_);

      // Normalise everything below the new final frontier in place: the
      // next grain (centre sNext_) will write exactly from sNext_ - N/2,
      // so positions < sNext_ - N/2 are final (§6.6.1 item 3: the
      // window-product normalisation, content first then window sum — the
      // exact order is part of the determinism; 0-guard ⇒ edge fade).
      const double frontierD = std::max(0.0, sNext_ - half);
      normaliseUpTo(static_cast<FrameCount>(frontierD));
    }
    return placedAny;
  }

  /// Window-product normalisation of every position below `frontier` in
  /// place (content first then window sum — the exact order is part of the
  /// determinism; the wsum 0-guard writes semantic zeros, §6.6.1 item 3).
  void normaliseUpTo(FrameCount frontier) {
    while (finalFrontier_ < frontier) {
      const FrameCount rel = finalFrontier_ - stretchBase_;
      if (rel >= 0 && rel < stretchCapacity_) {
        const double wv = wsum_[static_cast<std::size_t>(rel)];
        if (wv > 1.0e-12) {
          const double inv = 1.0 / wv;
          for (int c = 0; c < channels_; ++c) {
            stretch_[static_cast<std::size_t>(c)][static_cast<std::size_t>(rel)] *= inv;
          }
        } else {
          for (int c = 0; c < channels_; ++c) {
            stretch_[static_cast<std::size_t>(c)][static_cast<std::size_t>(rel)] = 0.0;
          }
        }
      }
      ++finalFrontier_;
    }
  }

  void emitFinalFrames(AudioBlockOut& out, int outCapacity, FrameCount& produced,
                       FrameCount phaseLimit) {
    const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
    while (produced < static_cast<FrameCount>(outCapacity)) {
      // §6.6.1 item 7: the phase limit — process() emits exactly [0, N_in);
      // finish() emits the tail up to the canonical length N_in + G and the
      // engine stops there regardless of grain state (beyond the drained
      // content the tail is §7.6 semantic zeros). This phase limit is THE
      // finish-phase stop: the content drain target below it is covered by
      // the same emission stream (content, then zeros).
      if (tOut_ >= phaseLimit) break;
      // +1: llround(q) + K may round up to q + K + 0.5; keep every tap
      // strictly below the final frontier (raw accumulations live above).
      // After the finish-phase exhaustion extension the frontier is open
      // (everything is final) and this gate no longer binds.
      const double need = q_ + static_cast<double>(K) + 1.0;
      if (need > static_cast<double>(finalFrontier_)) break;   // not final yet
      const double rho = ratioAtOutput(tOut_);
      const double cutoff = antiAliasCutoff(rho);
      const FrameCount baseIdx = stretchBase_;
      for (int c = 0; c < channels_; ++c) {
        out.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(produced)] =
            interpolateAtAbs(stretch_[static_cast<std::size_t>(c)].data(),
                             static_cast<FrameCount>(stretch_[0].size()), baseIdx,
                             q_, cutoff, ResampleQuality::Standard);
      }
      q_ += rho;
      ++tOut_;
      ++produced;
    }
  }
};

}  // namespace

std::unique_ptr<PitchEngine> makeTimePitchEngine() {
  return std::make_unique<TimePitchEngine>();
}

// ---------------------------------------------------------------------------
// The registry descriptor (§6.6 sheet; task-33 parameter table)
// ---------------------------------------------------------------------------

namespace {

// Mode choices (§6.6): the frozen final set is
//   fixed | adaptive | pitch_synced | pitch_formant
// landed per VST checkpoint (Fixed first — the checkpoint discipline in the
// header); the choice INDEX of each mode is frozen by this array order.
const char* const kTimePitchModeChoices[] = {"fixed"};

// Window shapes (§6.6 table, verbatim from candidate_ola.h).
const char* const kTimePitchShapeChoices[] = {"hann", "hamming", "bartlett", "rect"};

// Overlap engine-facing values (candidate_ola.h: overlap ∈ {2,4,8}); the
// choice names are the display text of the engine-facing values (the PV
// descriptor pattern: Int-choice params carry BOTH arrays).
const char* const kTimePitchOverlapNames[] = {"2", "4", "8"};
const double kTimePitchOverlapValues[] = {2.0, 4.0, 8.0};

// §6.6.1 item 18 visibility-guard data (frozen final form, set once):
// window_frames / overlap / window_shape are visible iff mode ∈
// {fixed, adaptive} (choice indices 0, 1). The guard VALUES are the mode
// choice indices — the frozen array order above defines them.
constexpr double kTpVisibleFixedAdaptive[] = {0.0, 1.0};

}  // namespace

EngineDescriptor timePitchEngineDescriptor() {
  EngineDescriptor d;
  d.info.id = "native.timepitch";
  d.info.displayName = "Time Pitch";
  d.info.version = "0.1.0";
  d.info.usageClass = UsageClass::Prototype;  // Task28 unification provenance
  d.info.origin = "own implementation";
  d.info.license = "own code, no dependency";
  d.capabilities.minRatio = 0.25;
  d.capabilities.maxRatio = 4.0;
  d.capabilities.supportsDynamicRatio = true;
  // §6.6 ControlRateSpec: FixedBlock(Hs) for Fixed/Adaptive (Hs = N/overlap,
  // parameter-dependent — the granular precedent: blockFrames = 0 records
  // "per-grain hop; derive from the recorded parameters"); Pitch-Synced
  // modes declare FixedBlock(pMax) when they land. The analyzer measures the
  // effective control rate independently (§10).
  d.capabilities.controlRate.kind = ControlRateSpec::Kind::FixedBlock;
  d.capabilities.controlRate.blockFrames = 0;
  d.capabilities.channelMode = ChannelMode::MonoAndStereo;
  d.capabilities.maxChannels = 2;
  d.capabilities.bandwidth.nyquistFraction = 0.45;
  // §6.6 Bandwidth honesty notes: the per-mode family artefacts are
  // intentional character, not defects to normalise away.
  d.capabilities.bandwidth.notes =
      "Fixed: OLA comb/flutter coloration is the mode's intentional character; "
      "Adaptive: measured AM ~ 0.01 at the WSOLA tolerance trade; "
      "Pitch-Synced modes: no explicit output AA stage on the pitch schedule "
      "(documented prototype behaviour); TD formant drift is inherent, the FD "
      "gamma axis is the manual compensation";
  // §6.6 DurationBehaviour is MODE-SCOPED (the recorded §D.4 amendment):
  // Fixed/Adaptive Preserving, Pitch-Synced/Pitch+Formant RateFollowing.
  // The descriptor field is per-engine; while only Preserving modes are
  // registered the declared value is exact. The mode-scoped amendment is
  // recorded here and in the sheet; the per-mode value lands with the
  // Pitch-Synced checkpoint (never silently re-declared).
  d.capabilities.duration = DurationBehaviour::Preserving;
  d.capabilities.determinism = Determinism::Deterministic;
  // §6.6: 44.1/48/88.2/96/192 kHz declared (176.4 kHz deliberately absent —
  // the tracker's lag band is declared only on this set).
  d.capabilities.supportedSampleRates = {44100u, 48000u, 88200u, 96000u, 192000u};

  // §6.6.1 item 1: exactly the seven descriptor rows of the §6.6 table
  // (landed per checkpoint; the key set grows with the mode implementations,
  // the tags are stable). Checkpoint 1 registers the Fixed-mode rows.
  d.parameterKeys = {"mode", "window_frames", "overlap", "window_shape"};
  d.parameters = {
      {
          .key = "mode",
          .displayName = "Mode",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 0.0,  // single choice at this checkpoint (frozen final: 3)
          .defaultPlain = 0.0,  // "fixed" (§6.6 default)
          .unit = "",
          .stepCount = 0,
          .choiceNames = kTimePitchModeChoices,
          .choiceValues = nullptr,
          .choiceCount = 1,
          .automatable = false,
          .rebuildsChain = true,  // §6.6.1 item 17: mode ∈ ChainSignature
          .dispFmt = "%s",
      },
      {
          .key = "window_frames",
          .displayName = "Window",
          .kind = EngineParamKind::Integer,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 64.0,
          .max = 16384.0,
          .defaultPlain = 2048.0,
          .unit = "frames",
          .stepCount = 16320,
          .choiceNames = nullptr,
          .choiceValues = nullptr,
          .choiceCount = 0,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%d",
          .visibleWhenKey = "mode",
          .visibleWhenValues = kTpVisibleFixedAdaptive,
          .visibleWhenCount = 2,
      },
      {
          .key = "overlap",
          .displayName = "Overlap",
          .kind = EngineParamKind::Integer,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 2.0,  // choice-index domain
          .defaultPlain = 0.0,
          .unit = "x",
          .stepCount = 2,
          .choiceNames = kTimePitchOverlapNames,
          .choiceValues = kTimePitchOverlapValues,
          .choiceCount = 3,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%d",
          .visibleWhenKey = "mode",
          .visibleWhenValues = kTpVisibleFixedAdaptive,
          .visibleWhenCount = 2,
      },
      {
          .key = "window_shape",
          .displayName = "Shape",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 3.0,  // choice-index domain
          .defaultPlain = 0.0,  // "hann"
          .unit = "",
          .stepCount = 3,
          .choiceNames = kTimePitchShapeChoices,
          .choiceValues = nullptr,
          .choiceCount = 4,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%s",
          .visibleWhenKey = "mode",
          .visibleWhenValues = kTpVisibleFixedAdaptive,
          .visibleWhenCount = 2,
      },
  };
  d.factory = &makeTimePitchEngine;
  return d;
}

}  // namespace pitchlab
