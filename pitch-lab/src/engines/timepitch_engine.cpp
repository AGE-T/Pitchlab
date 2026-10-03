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
#include <memory>
#include <string>
#include <vector>

#include <pocketfft/pocketfft_hdronly.h>

#include "core/errors.h"
#include "core/resampler.h"
#include "analysis/pitch_tracker.h"
#include "analysis/pitch_tracker_streaming.h"
#include "analysis/spectral.h"

namespace pitchlab {
namespace {

constexpr double kGrainPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// The Pitch-Synced mark record (§6.6.1 item 5 — the candidate_tdpsola.h
// Mark form: the refined position, the period at the mark, the voicing).
// ---------------------------------------------------------------------------
struct PsolaMark {
  FrameCount pos = 0;   // the refined mark position (input timeline)
  double period = 0.0;  // the period at the mark (frames)
  bool voiced = false;
};

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
    tolerance_ = 768;
    puvHz_ = 200.0;
    formantRatio_ = 1.0;

    for (const auto& [key, value] : cfg.parameters) {
      if (key == "mode") {
        if (const auto* s = std::get_if<std::string>(&value)) {
          // Mode checkpoint discipline (see the header): an unimplemented
          // mode is a CONFIG ERROR — never a disguised substitute. The
          // choice list grows as each checkpoint lands; the frozen final
          // set is fixed|adaptive|pitch_synced|pitch_formant (§6.6).
          if (*s != "fixed" && *s != "adaptive" && *s != "pitch_synced" &&
              *s != "pitch_formant") {
            throw ConfigError("", "mode",
                              "mode '" + *s +
                                  "' is not available in this build (Fixed=OLA, Adaptive=WSOLA, Pitch-Synced=TD-PSOLA, Pitch+Formant=FD-PSOLA)");
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
      } else if (key == "puv_hz") {
        // §6.6.1 item 1: the HIDDEN Pitch-Synced parameter (exposed=false in
        // the descriptor — the adapter writes the validated default; the
        // domain is validated here the same way).
        if (const auto* d = std::get_if<double>(&value)) {
          if (!std::isfinite(*d) || *d < 50.0 || *d > 500.0) {
            throw ConfigError("", "puv_hz", "puv_hz must be in [50, 500] Hz");
          }
          puvHz_ = *d;
        } else {
          throw ConfigError("", "puv_hz", "puv_hz must be a number");
        }
      } else if (key == "formant_ratio") {
        // §6.6.1 item 1/7: the Pitch + Formant spectral-envelope ratio —
        // the domain verbatim from the validated prototype
        // (candidate_fdpsola.cpp: formant_ratio ∈ [0.25, 4.0], default 1.0
        // = the TD-PSOLA bit-identity).
        if (const auto* d = std::get_if<double>(&value)) {
          if (!std::isfinite(*d) || *d < 0.25 || *d > 4.0) {
            throw ConfigError("", "formant_ratio",
                              "formant_ratio must be in [0.25, 4.0]");
          }
          formantRatio_ = *d;
        } else {
          throw ConfigError("", "formant_ratio", "formant_ratio must be a number");
        }
      } else if (key == "tolerance_frames") {
        // §6.6.1 item 4: the Adaptive (WSOLA) search tolerance — the domain
        // verbatim from the validated prototype (candidate_wsola.h:44).
        // Validated for BOTH modes that accept the key (the descriptor only
        // routes it; the value domain is mode-independent).
        if (const auto* i = std::get_if<int64_t>(&value)) {
          if (*i < 0 || *i > 8192) {
            throw ConfigError("", "tolerance_frames",
                              "tolerance_frames must be an integer in [0, 8192]");
          }
          tolerance_ = static_cast<int>(*i);
        } else {
          throw ConfigError("", "tolerance_frames",
                            "tolerance_frames must be an integer");
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

    if (mode_ == "pitch_synced" || mode_ == "pitch_formant") {
      preparePitchSynced();
      return;
    }

    // Sliding input window: the analysis reach below the next grain centre
    // (N/2, plus the WSOLA back-margin — the search reads down to
    // a − N/2 − tolerance) + the block arrival quantum + margin (the
    // candidate_ola.cpp sizing; candidate_wsola.cpp sets the margin BEFORE
    // the base prepare so the window is sized with it).
    inputBackMargin_ = (mode_ == "adaptive") ? tolerance_ + 64 : 0;
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
    builtScratch_.assign(static_cast<std::size_t>(windowFrames_) + 16, 0.0);
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

    if (mode_ == "pitch_synced" || mode_ == "pitch_formant") {
      return processPitchSynced(in, inFrames, out, outCapacity, inputFrameIndex);
    }

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
    if (mode_ == "pitch_synced" || mode_ == "pitch_formant") {
      return finishPitchSynced(out, outCapacity);
    }
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
    // (buffers are refilled with zeros in place; the window bank, the plan
    // trio and the tracker geometry are config/prepare-derived and survive
    // — §6.6.1 item 8's tracker clause). After reset, the same input with
    // the same curve produces bit-identical output to a fresh instance.
    if (mode_ == "pitch_synced" || mode_ == "pitch_formant") {
      resetPitchSynced();
      return;
    }
    resetJobState();
  }

 private:
  // --- configuration (validated in configure(); survives reset()) ---------
  std::string mode_ = "fixed";
  int windowFrames_ = 2048;
  int overlap_ = 2;
  std::string shape_ = "hann";
  int tolerance_ = 768;  // the Adaptive (WSOLA) search tolerance (frames)
  double puvHz_ = 200.0;  // the Pitch-Synced unvoiced period source (hidden)

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
  std::vector<double> builtScratch_;              // the WSOLA search's built
                                                  // estimate over the overlap
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

  // --- Pitch-Synced (TD-PSOLA) state (allocated in preparePitchSynced) -----
  analysis::StreamingPitchTracker tracker_;
  bool trackerDegenerate_ = false;
  double puvFrames_ = 0.0;   // P_uv = fs / puv_hz
  double pMax_ = 0.0;        // fs / 50
  double pMin_ = 0.0;        // fs / 1000
  std::vector<PsolaMark> markRing_;
  int markHead_ = 0;
  int markCount_ = 0;
  std::size_t mkCursor_ = 0;
  double tMark_ = 0.0;       // the next nominal mark position (input tl)
  std::vector<std::vector<double>> psolaAccum_;  // raw OLA (no wsum)
  FrameCount accumBase_ = 0;
  FrameCount accumCapacity_ = 0;
  std::vector<double> psolaGrainBuf_;
  double tNext_ = 0.0;       // the output-grid cursor (1:1 emission)
  double drainEnd_ = 0.0;

  // --- Pitch + Formant (FD-PSOLA) state (allocated in preparePitchSynced) --
  // §6.6.1 item 7: the per-voiced-grain spectral transform. THE RECORDED
  // STREAMING DEVIATION (checkpoint 4, ratification queued): the frozen
  // "one plan pair per distinct grain length, built in prepare()" assumed
  // the batch prototype's KNOWN marks; the streaming marks depend on the
  // realtime decode, and the full length domain pre-build measures 349 MB
  // @192 kHz (probe-measured plan footprint, even-only) — infeasible, and
  // lazy builds would allocate on the audio thread (forbidden). The
  // production engine instead pre-builds a ladder of 5-SMOOTH plan sizes
  // covering the whole domain (≤ ~75 entries, ≤ ~5 MB) and runs the frozen
  // transform on the grain zero-padded to the smallest ladder size ≥ gLen:
  // the SAME r2c → X′(k′) = X(k′/γ) (linear re/im, zero beyond the grain's
  // analysis Nyquist) → c2r·(1/L) semantics on a denser bin lattice (the
  // 5-smooth spacing bounds the padding at ~6 %). γ = 1 returns EARLY, so
  // the frozen internal-consistency gate (Pitch + Formant == Pitch-Synced
  // bit-identity at γ = 1) is EXACT.
  struct FdPlanEntry {
    int size = 0;
    std::unique_ptr<pocketfft::detail::pocketfft_r<double>> plan;
  };
  std::vector<FdPlanEntry> fdPlans_;
  std::vector<double> fdPacked_;
  std::vector<double> fdSpecRe_;
  std::vector<double> fdSpecIm_;
  double formantRatio_ = 1.0;

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

  // --- the placement law seam (§6.6.1 items 2/4) ----------------------------
  // Per-grain analysis-position adjustment. Fixed places every grain at the
  // nominal schedule position (returns 0 — candidate_ola.cpp verbatim).
  // Adaptive (WSOLA, §6.6.1 item 4 — candidate_wsola.cpp:52-120 verbatim):
  // per-grain δ ∈ [−tol, +tol] SSE search against the BUILT estimate over
  // the overlap region O = [s − N/2, s − Hs + N/2); deterministic scan
  // order 0, +1, −1, +2, −2, …; strictly-smaller SSE wins, ties → δ = 0
  // (closest to zero, positive first — the ascending-from-−tol order was a
  // MEASURED defect: systematic backward drift on near-silent input);
  // channel 0 drives the shared decision; the next NOMINAL advances from
  // the law (drift-free); grain 0 has no built output ⇒ δ = 0; ZERO
  // declared latency added (the search reaches BACKWARD into the buffered
  // back-margin). Pitch-Synced modes bypass the nominal grid entirely (the
  // mark schedule — their checkpoint).
  double grainAnalysisAdjustment(double aNominal) {
    if (mode_ != "adaptive" || grainsPlaced_ == 0 || tolerance_ == 0) {
      return 0.0;
    }
    const double half = static_cast<double>(windowFrames_) / 2.0;
    const double s = sNext_;
    const int64_t ovStart = static_cast<int64_t>(std::ceil(s - half));
    const int64_t ovEnd =
        static_cast<int64_t>(std::floor(s - static_cast<double>(hs_) + half));
    const int64_t ovLen = ovEnd - ovStart;
    if (ovLen <= 0 || ovLen + 16 > static_cast<int64_t>(builtScratch_.size())) {
      return 0.0;
    }

    // Normalised built estimate over O (the output-so-far comparison
    // target; channel 0 — the shared decision).
    for (int64_t i = 0; i < ovLen; ++i) {
      const FrameCount rel = (ovStart + i) - stretchBase_;
      double v = 0.0;
      if (rel >= 0 && rel < stretchCapacity_) {
        const double w = wsum_[static_cast<std::size_t>(rel)];
        if (w > 1.0e-12) {
          v = stretch_[0][static_cast<std::size_t>(rel)] / w;
        }
      }
      builtScratch_[static_cast<std::size_t>(i)] = v;
    }

    // Deterministic search over the integer tolerance grid, ordered 0,
    // +1, −1, +2, −2, …; strictly-smaller SSE replaces the best, so ties
    // resolve to the delta CLOSEST TO ZERO (positive first) — the nominal
    // schedule is preferred among equal candidates (the ascending-from-−tol
    // order was a measured defect: on near-silent input every SSE is equal,
    // the tie-break systematically chose −tolerance and the re-anchored
    // schedule drifted backwards until the input window overflowed).
    int64_t bestDelta = 0;
    double bestSse = std::numeric_limits<double>::max();
    const int64_t tol = static_cast<int64_t>(tolerance_);
    for (int64_t radius = 0; radius <= tol; ++radius) {
      const int64_t candidates[2] = {radius, -radius};
      for (int ci = (radius == 0 ? 0 : 1); ci < 2; ++ci) {
        const int64_t d = candidates[ci];
        double sse = 0.0;
        for (int64_t i = 0; i < ovLen; ++i) {
          const double p = static_cast<double>(ovStart + i);
          const double inputPos = aNominal + static_cast<double>(d) + (p - s);
          const double v =
              readInput(0, static_cast<FrameCount>(std::floor(inputPos)));
          const double diff = v - builtScratch_[static_cast<std::size_t>(i)];
          sse += diff * diff;
        }
        if (sse < bestSse) {
          bestSse = sse;
          bestDelta = d;
        }
      }
    }
    return static_cast<double>(bestDelta);
  }

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


  // =========================================================================
  // Pitch-Synced (TD-PSOLA) — the second accumulation family (§6.6.1
  // items 5/6/8). RAW OLA + per-grain s/P scale (the window-product
  // normalisation was a MEASURED DEFECT here — the two policies are
  // NEVER merged); the §7 resampler BYPASSED (the output grid == the
  // accumulator grid, 1:1 emission); the marks come from the STREAMING
  // fixed-lag pYIN tracker (item 8) fed on channel 0.
  // =========================================================================

  void preparePitchSynced() {
    // The period grid (the prototype's prepare, verbatim bounds):
    puvFrames_ = fs_ / puvHz_;
    pMax_ = fs_ / analysis::kTrackerFminHz;
    pMin_ = fs_ / analysis::kTrackerFmaxHz;

    // The streaming tracker (§6.6.1 item 8): the frozen band 50..1000 Hz
    // (kTrackerFmin/Fmax), the shared observation TU, the fixed-lag Viterbi
    // D = 4. A degenerate configuration (impossible on the declared rates)
    // takes the honest all-unvoiced fallback path.
    trackerDegenerate_ = !tracker_.configure(fs_, analysis::kTrackerFminHz,
                                             analysis::kTrackerFmaxHz);
    if (!trackerDegenerate_) {
      tracker_.prepare();
    }

    const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
    // The declared latency FIRST (the input window sizing depends on it):
    // the §6.6.1 item 10 Pitch-Synced composition — the synthesis part
    // verbatim from candidate_tdpsola.cpp:236-253, Λ_tr from the frozen
    // item-8 formula, plus the MEASURED streaming release margin (see
    // analysis::trackerReleaseMargin):
    //   input  = 2·pMax + 2K + 128 + Λ_tr + (2·hop + 2·pMax)
    //   Λ_tr   = [n − minTag] + D·hop + ⌈pMax/4⌉
    //   output = ⌊(4·pMax + 2K + 512)/max(0.25, minRatio)⌋ + 64
    const int64_t lamTrPre = trackerLagFrames();
    const int64_t releaseMarginPre =
        trackerDegenerate_ ? 0 : analysis::trackerReleaseMargin(fs_, pMax_);
    latencyIn_ = static_cast<FrameCount>(2.0 * pMax_) + 2 * K + 128 +
                 lamTrPre + releaseMarginPre;
    double minRatioPre = 1.0;
    for (FrameCount i = 0; i < curve_.frames; ++i) {
      minRatioPre = std::min(minRatioPre, curve_.ratio[static_cast<std::size_t>(i)]);
    }
    if (!(minRatioPre > 0.0)) minRatioPre = 0.5;
    latencyOut_ = static_cast<FrameCount>(
        static_cast<double>(static_cast<FrameCount>(4.0 * pMax_) + 2 * K + 512) /
        std::max(0.25, minRatioPre)) + 64;

    // The input window (the prototype's sizing + the tracker's pending
    // span + THE DECLARED INPUT LATENCY — the §4.3.6 zero-pad era: the mark
    // schedule stops at nIn while the driver keeps feeding the declared-
    // latency pad, so the window must hold the full pad span; compaction
    // NEVER needs a capacity-driven drop — the schedule-dependent corruption
    // class, the prototype's recorded defect):
    // 2 pMax analysis history + the declared input latency + the tracker's
    // (n + D·hop) undecoded reach + the driver blocks + margin.
    const FrameCount inCap = static_cast<FrameCount>(4.0 * pMax_) + 2 * K +
                             latencyIn_ +
                             static_cast<FrameCount>(tracker_.windowFrames()) +
                             static_cast<FrameCount>(tracker_.lagFrames()) *
                                 tracker_.hop() +
                             2 * static_cast<FrameCount>(maxBlock_) + 512;
    accumCapacity_ = static_cast<FrameCount>(2.0 * pMax_ + pMax_ + 2 * K +
                                             2 * maxBlock_ + 512);
    psolaAccum_.assign(static_cast<std::size_t>(channels_),
                       std::vector<double>(static_cast<std::size_t>(accumCapacity_), 0.0));
    psolaGrainBuf_.assign(static_cast<std::size_t>(channels_) *
                                  static_cast<std::size_t>(2.0 * pMax_) + 16,
                          0.0);
    inBuf_.assign(static_cast<std::size_t>(channels_),
                  std::vector<double>(static_cast<std::size_t>(inCap), 0.0));

    // The mark ring: bounded by the decode span + the scheduling margin
    // (the marks are >= 8 frames apart; the live span is a few hops).
    markRing_.assign(256, PsolaMark{});

    // §6.6.1 item 7 (checkpoint 4): the FD plan ladder (Pitch + Formant only;
    // see the FD state comment for the recorded streaming deviation). The
    // ladder = the even 5-smooth sizes covering [round(2·pMin), round(2·pMax)]
    // plus the first such size ≥ the domain top (the pad-out entry).
    fdPlans_.clear();
    if (mode_ == "pitch_formant") {
      auto smooth5 = [](long long n) {
        for (int p : {2, 3, 5}) {
          while (n % p == 0) n /= p;
        }
        return n == 1;
      };
      const int gLenMin = std::max(4, static_cast<int>(std::round(2.0 * pMin_)));
      const int gLenMax = std::max(gLenMin + 1, static_cast<int>(std::round(2.0 * pMax_)));
      for (int n = gLenMin; n <= gLenMax; ++n) {
        if ((n % 2) == 0 && smooth5(n)) {
          fdPlans_.push_back(FdPlanEntry{
              n, std::make_unique<pocketfft::detail::pocketfft_r<double>>(
                     static_cast<std::size_t>(n))});
        }
      }
      for (int n = gLenMax;; ++n) {
        if ((n % 2) == 0 && smooth5(n)) {
          if (fdPlans_.empty() || fdPlans_.back().size != n) {
            fdPlans_.push_back(FdPlanEntry{
                n, std::make_unique<pocketfft::detail::pocketfft_r<double>>(
                       static_cast<std::size_t>(n))});
          }
          break;
        }
      }
      const int lMax = fdPlans_.back().size;
      fdPacked_.assign(static_cast<std::size_t>(lMax), 0.0);
      fdSpecRe_.assign(static_cast<std::size_t>(lMax / 2 + 2), 0.0);
      fdSpecIm_.assign(static_cast<std::size_t>(lMax / 2 + 2), 0.0);
    } else {
      fdPacked_.clear();
      fdSpecRe_.clear();
      fdSpecIm_.clear();
    }

    resetPitchSynced();
    prepared_ = true;
  }

  /// The frozen tracker latency term (§6.6.1 item 8):
  ///   Λ_tr = [n − minTag] + D·hop + ⌈pMax/4⌉
  ///   minTag = llround((W + tauMin − 0.5)/2), W = n − tauMax
  /// (probe-measured 1480 @48k availability; the frozen per-rate totals).
  [[nodiscard]] int64_t trackerLagFrames() const {
    // The frozen item-8 formula — the ONE definition in the analysis layer
    // (analysis::trackerLagFrames); the degenerate-tracker fallback keeps
    // the same composition (the geometry helpers are band-independent).
    return analysis::trackerLagFrames(fs_, pMax_, 4);
  }

  void resetPitchSynced() {
    for (auto& b : psolaAccum_) std::fill(b.begin(), b.end(), 0.0);
    std::fill(psolaGrainBuf_.begin(), psolaGrainBuf_.end(), 0.0);
    for (auto& b : inBuf_) std::fill(b.begin(), b.end(), 0.0);
    if (!trackerDegenerate_) tracker_.reset();
    inBase_ = 0;
    inAvail_ = 0;
    consumedTotal_ = 0;
    inputExhausted_ = false;
    accumBase_ = 0;
    finalFrontier_ = 0;
    aNext_ = 0.0;
    tNext_ = 0.0;
    grainsPlaced_ = 0;
    finishing_ = false;
    drainEnd_ = 0.0;
    tMark_ = 0.0;
    markHead_ = 0;
    markCount_ = 0;
    mkCursor_ = 0;
  }

  /// The tracker feed: observe every frame whose window is complete at the
  /// delivered frontier (channel 0; the sliding-window reads are contiguous
  /// by the compaction floor's tracker term).
  void feedTracker() {
    if (trackerDegenerate_) return;
    while (tracker_.nextFrameStart() + tracker_.windowFrames() <= inAvail_) {
      const FrameCount s = tracker_.nextFrameStart();
      const FrameCount rel = s - inBase_;
      if (rel < 0 || rel + tracker_.windowFrames() >
                         static_cast<FrameCount>(inBuf_[0].size())) {
        break;  // unreachable (the compaction floor keeps the window)
      }
      tracker_.observeFrame(inBuf_[0].data() + static_cast<std::size_t>(rel), s);
    }
  }

  /// The nearest decoded tracker frame to t (the prototype's frameAt, over
  /// the decoded ring; the ring holds the unconsumed decode span).
  [[nodiscard]] bool decodedFrameNear(double t, double& f0Hz, bool& voiced) const {
    if (trackerDegenerate_ || tracker_.decodedCount() == 0) {
      voiced = false;
      f0Hz = 0.0;
      return false;
    }
    const int count = tracker_.decodedCount();
    // Linear scan over the (bounded) ring: the mark cursor consumes behind
    // the decode frontier, so the live span is a few hops.
    std::size_t bestIdx = 0;
    int64_t bestDist = std::numeric_limits<int64_t>::max();
    for (int i = 0; i < count; ++i) {
      const int64_t d =
          std::abs(tracker_.decoded(i).center - static_cast<int64_t>(std::llround(t)));
      if (d < bestDist) {
        bestDist = d;
        bestIdx = static_cast<std::size_t>(i);
      }
    }
    const auto& fr = tracker_.decoded(bestIdx);
    voiced = fr.voiced && fr.f0Hz > 0.0;
    f0Hz = fr.f0Hz;
    return true;
  }

  /// Consume decoded frames the mark cursor has passed (the ring stays
  /// bounded; the marks keep their own copies).
  void consumeDecodedBehind(double t) {
    if (trackerDegenerate_) return;
    int consumed = 0;
    while (consumed < tracker_.decodedCount() &&
           static_cast<double>(tracker_.decoded(0).center) <
               t - static_cast<double>(pMax_)) {
      ++consumed;
    }
    if (consumed > 0) tracker_.consumeDecoded(consumed);
  }

  /// Generate marks while the next nominal mark's needs are covered: the
  /// decode coverage (a decoded frame at/beyond the mark — the tail uses
  /// the last decoded frame once the real input is fully delivered) and
  /// the ZC input reach. The mark grid: t += max(8, round(P)) with the
  /// period/voicing from the tracker (§6.6.1 item 5 — the batch form
  /// verbatim, streaming-sourced).
  void generateMarksUpTo() {
    const int64_t nInClamp = nIn_;
    for (;;) {
      if (tMark_ >= static_cast<double>(nInClamp)) {
        return;
      }
      // The period/voicing at the mark: the nearest decoded frame.
      double f0 = 0.0;
      bool voiced = false;
      const bool haveFrame = decodedFrameNear(tMark_, f0, voiced);
      const bool streamFullyDelivered = (consumedTotal_ >= nIn_);
      const bool allStreamDelivered = (consumedTotal_ >= nIn_ + latencyIn_);
      // (a) the decode coverage: a decoded frame at/beyond the mark, or the
      //     settled tail (the prototype's frameAt clamping to the last
      //     frame once the full padded stream is delivered — no more decode
      //     can happen then).
      if (!haveFrame) {
        if (!allStreamDelivered) break;
      } else {
        const int count = tracker_.decodedCount();
        const double newestCenter =
            static_cast<double>(tracker_.decoded(count - 1).center);
        if (newestCenter < tMark_ && !allStreamDelivered) break;
      }
      // The period (clamped into the band — the prototype's form).
      double P = puvFrames_;
      if (voiced && f0 > 0.0) {
        P = std::clamp(fs_ / f0, pMin_, pMax_);
      }
      // (b) the ZC input reach delivered (the refinement scan; the scan
      // itself clamps to [0, nIn − 1] — the delivered stream always covers
      // the clamped range once the real input is in).
      const int64_t span = voiced ? std::max<int64_t>(4, static_cast<int64_t>(P / 4.0)) : 0;
      if (!streamFullyDelivered &&
          tMark_ + static_cast<double>(span) > static_cast<double>(inAvail_)) {
        break;
      }
      // The ZC refinement (the prototype's loop verbatim: the nearest
      // positive-going crossing within ±P/4; no crossing ⇒ the mark STAYS
      // nominal — documented, bounded).
      FrameCount mark = static_cast<FrameCount>(tMark_);
      if (voiced) {
        const int64_t lo = std::max<int64_t>(0, static_cast<int64_t>(tMark_) - span);
        const int64_t hi = std::min<int64_t>(nInClamp - 2,
                                             static_cast<int64_t>(tMark_) + span);
        int64_t best = -1;
        int64_t bestDist = span + 1;
        for (int64_t i = lo + 1; i <= hi; ++i) {
          const double a = readInput(0, i - 1);
          const double b2 = readInput(0, i);
          if (a <= 0.0 && b2 > 0.0) {
            const int64_t dist = std::abs(i - static_cast<int64_t>(tMark_));
            if (dist < bestDist) {
              bestDist = dist;
              best = i;
            }
          }
        }
        if (best >= 0) mark = static_cast<FrameCount>(best);
      }
      pushMark(PsolaMark{mark, P, voiced});
      tMark_ += static_cast<double>(std::max<int64_t>(8, static_cast<int64_t>(std::round(P))));
      consumeDecodedBehind(tMark_);
    }
  }

  void pushMark(const PsolaMark& mk) {
    const std::size_t cap = markRing_.size();
    if (markCount_ == static_cast<int>(cap)) {
      // Drop the oldest (the synthesis cursor never reads that far back).
      markHead_ = (markHead_ + 1) % static_cast<int>(cap);
      --markCount_;
      if (mkCursor_ > 0) --mkCursor_;
    }
    const std::size_t slot = static_cast<std::size_t>((markHead_ + markCount_) % static_cast<int>(cap));
    markRing_[slot] = mk;
    ++markCount_;
  }

  [[nodiscard]] const PsolaMark& markAt(std::size_t absIdx) const {
    const std::size_t cap = markRing_.size();
    const std::size_t slot =
        static_cast<std::size_t>((markHead_ + static_cast<int>(absIdx)) % static_cast<int>(cap));
    return markRing_[slot];
  }

  /// The prototype's markIndexNear over the ring (the advancing cursor +
  /// the local i/i+1 compare).
  [[nodiscard]] std::size_t markIndexNear(double a) const {
    if (markCount_ == 0) return 0;
    std::size_t i = mkCursor_;
    while (i + 1 < static_cast<std::size_t>(markCount_) &&
           static_cast<double>(markAt(i + 1).pos) < a) {
      ++i;
    }
    if (i + 1 < static_cast<std::size_t>(markCount_)) {
      const double d0 = std::fabs(static_cast<double>(markAt(i).pos) - a);
      const double d1 = std::fabs(static_cast<double>(markAt(i + 1).pos) - a);
      if (d1 < d0) return i + 1;
    }
    return i;
  }

  void compactPitchSyncedInput() {
    if (finishing_) {
      return;  // finish-mode re-reads the last mark's window: drop nothing.
    }
    // Drop ONLY input no future consumer will read (the prototype's two
    // floors + the tracker's undecoded-window floor):
    //   (a) the upcoming marks' window floor: aNext_ − 2 pMax − 128;
    //   (b) the newest generated mark's grain start (the finish re-reads);
    //   (c) the tracker's next undecoded frame start.
    FrameCount keepFrom = std::max<FrameCount>(
        0, static_cast<FrameCount>(aNext_) -
               static_cast<FrameCount>(2.0 * pMax_) - 128);
    if (markCount_ > 0) {
      const PsolaMark& newest = markAt(static_cast<std::size_t>(markCount_ - 1));
      const FrameCount fromLastMark = std::max<FrameCount>(
          0, newest.pos - static_cast<FrameCount>(newest.period));
      keepFrom = std::min(keepFrom, fromLastMark);
    }
    if (!trackerDegenerate_) {
      keepFrom = std::min(keepFrom, std::max<FrameCount>(0, tracker_.nextFrameStart()));
    }
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

  void compactAccum() {
    const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
    const FrameCount keepFrom = std::max<FrameCount>(
        0, tOut_ - K - 128);
    if (accumBase_ >= keepFrom) return;
    const FrameCount drop = keepFrom - accumBase_;
    const FrameCount live = static_cast<FrameCount>(
        std::max(0.0, std::max(static_cast<double>(finalFrontier_),
                               tNext_ + pMax_)) -
            static_cast<double>(accumBase_));
    const FrameCount keep = std::min(live, accumCapacity_);
    if (drop >= keep) {
      for (auto& b : psolaAccum_) std::fill(b.begin(), b.end(), 0.0);
      accumBase_ = keepFrom;
      return;
    }
    for (auto& b : psolaAccum_) {
      std::memmove(b.data(), b.data() + static_cast<std::size_t>(drop),
                   static_cast<std::size_t>(keep - drop) * sizeof(double));
      std::fill(b.begin() + static_cast<std::size_t>(keep - drop), b.end(), 0.0);
    }
    accumBase_ = keepFrom;
  }

  /// §6.6.1 item 7: the per-VOICED-grain spectral transform (the
  /// candidate_fdpsola semantics on the recorded streaming plan ladder — see
  /// the FD state comment). The frequency-domain rule is the candidate's
  /// X′(f) = X(f/γ) verbatim: linear in re/im, sources beyond the analysis
  /// Nyquist (fs/2) read zero — the honest γ<1 band-limit. γ = 1 (the frozen
  /// |γ−1| < 1e-9 gate) and unvoiced grains return UNTOUCHED: the γ = 1 path
  /// is BIT-IDENTICAL to Pitch-Synced (the internal-consistency gate, exact).
  void transformGrainFd(const PsolaMark& mark, int gLen) {
    if (!mark.voiced || std::fabs(formantRatio_ - 1.0) < 1.0e-9) return;
    if (fdPlans_.empty() || gLen > fdPlans_.back().size) {
      return;  // defensive: prepare covers the whole domain
    }
    const FdPlanEntry* pe = fdPlans_.data();
    for (const auto& e : fdPlans_) {
      if (e.size >= gLen) {
        pe = &e;
        break;
      }
    }
    const int L = pe->size;
    const int halfL = L / 2;
    const double g = formantRatio_;
    for (int c = 0; c < channels_; ++c) {
      double* grain = psolaGrainBuf_.data() +
                      static_cast<std::size_t>(c) * static_cast<std::size_t>(gLen);
      std::memcpy(fdPacked_.data(), grain, static_cast<std::size_t>(gLen) * sizeof(double));
      std::fill(fdPacked_.begin() + gLen, fdPacked_.end(), 0.0);
      pe->plan->exec(fdPacked_.data(), 1.0, true);
      // Unpack the half-spectrum (the packed-halfcomplex form; the
      // candidate's loop with halfL).
      for (int k = 0; k <= halfL; ++k) {
        double re, im;
        if (k == 0) {
          re = fdPacked_[0];
          im = 0.0;
        } else if (k == halfL && (L % 2) == 0) {
          re = fdPacked_[static_cast<std::size_t>(L - 1)];
          im = 0.0;
        } else {
          re = fdPacked_[static_cast<std::size_t>(2 * k - 1)];
          im = fdPacked_[static_cast<std::size_t>(2 * k)];
        }
        fdSpecRe_[static_cast<std::size_t>(k)] = re;
        fdSpecIm_[static_cast<std::size_t>(k)] = im;
      }
      // X′(k′) = X(k′/γ) on the ladder grid: linear in re/im; source
      // positions beyond the analysis Nyquist read zero (k0 >= halfL — the
      // candidate's rule; the γ<1 band-limit).
      for (int kp = 0; kp <= halfL; ++kp) {
        const double q = static_cast<double>(kp) / g;
        const int k0 = static_cast<int>(std::floor(q));
        const double frac = q - static_cast<double>(k0);
        double re, im;
        if (k0 >= halfL) {
          re = (k0 == halfL) ? fdSpecRe_[static_cast<std::size_t>(halfL)] : 0.0;
          im = 0.0;
          if (k0 == halfL && frac > 0.0) {
            re = 0.0;  // interpolating past the Nyquist bin reads zero
          }
        } else {
          const double re0 = fdSpecRe_[static_cast<std::size_t>(k0)];
          const double im0 = fdSpecIm_[static_cast<std::size_t>(k0)];
          const double re1 = fdSpecRe_[static_cast<std::size_t>(k0 + 1)];
          const double im1 = fdSpecIm_[static_cast<std::size_t>(k0 + 1)];
          re = re0 + frac * (re1 - re0);
          im = im0 + frac * (im1 - im0);
        }
        if (kp == 0) {
          fdPacked_[0] = re;
          if ((L % 2) == 0) fdPacked_[static_cast<std::size_t>(L - 1)] = 0.0;
        } else if (kp == halfL && (L % 2) == 0) {
          fdPacked_[static_cast<std::size_t>(L - 1)] = re;
        } else {
          fdPacked_[static_cast<std::size_t>(2 * kp - 1)] = re;
          fdPacked_[static_cast<std::size_t>(2 * kp)] = im;
        }
      }
      // Inverse transform (c2r; 1/L scale), the first gLen samples back.
      pe->plan->exec(fdPacked_.data(), 1.0 / static_cast<double>(L), false);
      std::memcpy(grain, fdPacked_.data(), static_cast<std::size_t>(gLen) * sizeof(double));
    }
  }

  bool placePitchSyncedGrainsUpTo(FrameCount inputAvailableEnd) {
    bool placedAny = false;
    const double pMaxD = pMax_;
    for (;;) {
      // The schedule bounds (the prototype's recorded-corruption guards).
      if (finishing_) {
        if (aNext_ > static_cast<double>(nIn_) + 2.0 * pMaxD) break;
      } else {
        if (aNext_ > static_cast<double>(nIn_)) break;
      }
      const std::size_t mk = markIndexNear(aNext_);
      if (markCount_ == 0) break;
      const PsolaMark mark = markAt(mk);
      const double P = mark.period;
      const double half = P;  // grain length 2P, centred at the mark
      const double grainCentreIn = static_cast<double>(mark.pos);
      const double needEnd = grainCentreIn + half;
      const FrameCount inputLimit =
          finishing_ ? std::numeric_limits<FrameCount>::max() : inputAvailableEnd;
      if (finishing_) {
        if (grainCentreIn - half >= static_cast<double>(nIn_) + pMax_) break;
      } else if (needEnd > static_cast<double>(inputLimit)) {
        break;
      }
      // The STREAMING schedule gate: the marks arrive decode-gated (the
      // fixed-lag tracker's Λ_tr behind the input frontier). When the
      // schedule (aNext_) has passed the newest GENERATED mark by more than
      // one period (more than the legitimate round()-drift + refinement
      // jitter), wait for the decode to produce the next one — re-placing
      // the last mark would corrupt the schedule (the batch prototype had
      // its marks precomputed; the streaming engine paces the synthesis to
      // the decode).
      if (!finishing_ && markCount_ > 0) {
        const double newestPos =
            static_cast<double>(markAt(static_cast<std::size_t>(markCount_ - 1)).pos);
        // The schedule must never place a mark BEHIND its cursor: the batch
        // prototype's markIndexNear always has the full mark list available,
        // so the mark nearest aNext_ can only be behind it by the ZC
        // refinement jitter (bounded, part of the candidate's measured
        // character). The streaming decode, however, produces marks Λ_tr
        // behind the input frontier — allowing the placement to run one
        // period past the newest GENERATED mark (the first version's
        // `newestPos < aNext_ - P` boundary) placed the STALE mark's grain
        // at the new output slot: a one-period displacement of the whole
        // window (measured: the drum anchor's impulse edges broke
        // bit-exactness at exactly w(1)·x one period late). Wait for the
        // decode to produce a mark at/beyond the cursor instead.
        if (newestPos < aNext_) break;
      }

      // The MC90 pitch schedule (pitch mode only — the stretch schedule is
      // OUT OF SCOPE, the §6.6 scope lock): voiced marks s = P/β (output
      // pitch = β·F0), u = P (cycle-accurate consumption — the duration
      // changes by 1/β over voiced spans, D.4 RateFollowing); unvoiced
      // marks keep the fixed P_uv spacing (s = u = P_uv).
      const double beta = mark.voiced ? ratioAtInput(aNext_) : 1.0;
      const double s = mark.voiced ? P / beta : puvFrames_;
      const double u = mark.voiced ? P : puvFrames_;
      const double t = tNext_;
      const std::size_t mkNext = markIndexNear(aNext_ + u);

      // Back-pressure on the accumulator live window (the prototype form).
      const int K = resampleKernelSpec(ResampleQuality::Standard).halfWidthTaps;
      const FrameCount liveLo = std::max<FrameCount>(0, tOut_ - K - 128);
      const FrameCount tailEnd = static_cast<FrameCount>(t + half) + 2 * (K + 8);
      if (tailEnd - liveLo > accumCapacity_ - 8) break;

      // Extract the windowed grain (the PREPARE-BUILT window cache is
      // replaced by direct evaluation in the streaming engine — the marks
      // depend on the realtime decode, so the lengths are not known at
      // prepare; the periodic-Hann formula is identical, deterministic).
      const int gLen = std::max(4, static_cast<int>(std::round(2.0 * P)));
      if (static_cast<std::size_t>(channels_) * static_cast<std::size_t>(gLen) >
          psolaGrainBuf_.size()) {
        break;  // P is bounded by pMax_ (prepare sizing covers it); defensive
      }
      for (int c = 0; c < channels_; ++c) {
        for (int i = 0; i < gLen; ++i) {
          const double ip = grainCentreIn - P + static_cast<double>(i);
          const double wv =
              0.5 * (1.0 - std::cos(2.0 * kGrainPi * static_cast<double>(i) /
                                    static_cast<double>(gLen)));
          psolaGrainBuf_[static_cast<std::size_t>(c) *
                             static_cast<std::size_t>(gLen) +
                         static_cast<std::size_t>(i)] =
              wv * readInput(c, static_cast<FrameCount>(std::floor(ip)));
        }
      }

      // §6.6.1 item 7 (checkpoint 4): the Pitch + Formant per-voiced-grain
      // spectral transform — the FD hook between extraction and
      // accumulation (γ = 1 and unvoiced grains pass through untouched).
      if (mode_ == "pitch_formant") {
        transformGrainFd(mark, gLen);
      }

      // RAW overlap-add with the PER-GRAIN level scale s/P (§6.6.1 item 6 —
      // the second accumulation policy; identity s = P ⇒ scale 1, Hann(2P)@P
      // is exactly COLA ⇒ bit-exact transparency).
      const double grainScale = s / P;
      compactAccum();
      const FrameCount centre = static_cast<FrameCount>(std::llround(t));
      const FrameCount gHalf = gLen / 2;
      for (int i = 0; i < gLen; ++i) {
        const FrameCount p = centre - gHalf + static_cast<FrameCount>(i);
        const FrameCount rel = p - accumBase_;
        if (rel < 0 || rel >= accumCapacity_) continue;  // guarded by pressure
        for (int c = 0; c < channels_; ++c) {
          psolaAccum_[static_cast<std::size_t>(c)][static_cast<std::size_t>(rel)] +=
              grainScale *
              psolaGrainBuf_[static_cast<std::size_t>(c) *
                                 static_cast<std::size_t>(gLen) +
                             static_cast<std::size_t>(i)];
        }
      }
      ++grainsPlaced_;
      placedAny = true;
      mkCursor_ = mk;
      aNext_ += u;
      tNext_ = t + s;

      // The write boundary below the new frontier (raw OLA: no
      // normalisation pass — final positions are the accumulated values
      // as-is; the next grain writes from tNext_ − its half).
      const double halfNext = markAt(mkNext).period;
      const double frontierD = std::max(0.0, tNext_ - halfNext);
      const FrameCount frontier = static_cast<FrameCount>(frontierD);
      if (finalFrontier_ < frontier) {
        finalFrontier_ = frontier;
      }
    }
    return placedAny;
  }

  void emitPitchSyncedFrames(AudioBlockOut& out, int outCapacity, FrameCount& produced) {
    const double drainLimit = finishing_ ? drainEnd_
                                          : std::numeric_limits<double>::max();
    while (produced < static_cast<FrameCount>(outCapacity)) {
      // Pitch mode: output grid == accumulator grid; 1:1 emission (the §7
      // resampler bypassed — §6.6.1 item 2).
      if (static_cast<double>(tOut_) + 1.0 >
          static_cast<double>(finalFrontier_)) {
        break;
      }
      if (finishing_ && static_cast<double>(tOut_) >= drainLimit) break;
      const FrameCount rel = tOut_ - accumBase_;
      for (int c = 0; c < channels_; ++c) {
        out.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(produced)] =
            (rel >= 0 && rel < accumCapacity_)
                ? psolaAccum_[static_cast<std::size_t>(c)]
                             [static_cast<std::size_t>(rel)]
                : 0.0;
      }
      ++tOut_;
      ++produced;
    }
  }

  ProcessReport processPitchSynced(const AudioBlockView& in, int inFrames,
                                   AudioBlockOut& out, int outCapacity,
                                   FrameCount inputFrameIndex) {
    (void)inputFrameIndex;
    compactPitchSyncedInput();
    if (inFrames > 0) {
      if (in.channels == nullptr) {
        throw EngineException("native.timepitch", "null input channels");
      }
      const FrameCount rel = inAvail_ - inBase_;
      const auto cap = static_cast<FrameCount>(inBuf_[0].size());
      if (rel + inFrames > cap) {
        throw EngineException("native.timepitch",
                              "input window overflow (sizing bug; report as engine defect)");
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

    feedTracker();
    generateMarksUpTo();
    placePitchSyncedGrainsUpTo(inAvail_);

    FrameCount produced = 0;
    if (outCapacity > 0 && out.channels != nullptr) {
      emitPitchSyncedFrames(out, outCapacity, produced);
    }
    const FrameCount streamEnd = nIn_ + latencyIn_;
    inputExhausted_ = inputExhausted_ || (consumedTotal_ >= streamEnd);
    ProcessReport rep;
    rep.inputFramesConsumed = inFrames;
    rep.outputFramesProduced = produced;
    rep.inputExhausted = inputExhausted_;
    return rep;
  }

  ProcessReport finishPitchSynced(AudioBlockOut& out, int outCapacity) {
    drainEnd_ = std::numeric_limits<double>::max();
    const double pMaxD = pMax_;
    FrameCount produced = 0;
    for (int64_t guard = 0; guard < (1 << 24); ++guard) {
      generateMarksUpTo();  // the settled tail marks (the last frame clamped)
      const bool placedAny = placePitchSyncedGrainsUpTo(0);
      const bool grainsExhausted =
          aNext_ >= static_cast<double>(nIn_) + pMaxD * 3.0;
      if (grainsExhausted) {
        drainEnd_ = tNext_ + pMaxD;
      }
      const FrameCount before = produced;
      if (outCapacity > 0 && out.channels != nullptr) {
        emitPitchSyncedFrames(out, outCapacity, produced);
      }
      if (produced == before && (!placedAny || grainsExhausted)) {
        break;
      }
    }
    ProcessReport rep;
    rep.inputFramesConsumed = 0;
    rep.outputFramesProduced = produced;
    rep.inputExhausted = inputExhausted_;
    return rep;
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
// landed per VST checkpoint (Fixed first, Adaptive second — the checkpoint
// discipline in the header); the choice INDEX of each mode is frozen by
// this array order.
const char* const kTimePitchModeChoices[] = {"fixed", "adaptive", "pitch_synced",
                                             "pitch_formant"};

// Window shapes (§6.6 table, verbatim from candidate_ola.h).
const char* const kTimePitchShapeChoices[] = {"hann", "hamming", "bartlett", "rect"};

// Overlap engine-facing values (candidate_ola.h: overlap ∈ {2,4,8}); the
// choice names are the display text of the engine-facing values (the PV
// descriptor pattern: Int-choice params carry BOTH arrays).
const char* const kTimePitchOverlapNames[] = {"2", "4", "8"};
const double kTimePitchOverlapValues[] = {2.0, 4.0, 8.0};

// §6.6.1 item 18 visibility-guard data (frozen final form, set once):
// window_frames / overlap / window_shape are visible iff mode ∈
// {fixed, adaptive} (choice indices 0, 1); tolerance_frames iff mode ∈
// {adaptive} (index 1). The guard VALUES are the mode choice indices — the
// frozen array order above defines them.
constexpr double kTpVisibleFixedAdaptive[] = {0.0, 1.0};
constexpr double kTpVisibleAdaptiveOnly[] = {1.0};
constexpr double kTpVisibleFormantOnly[] = {3.0};

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
  // the tags are stable). Checkpoint 2 registers the Fixed + Adaptive rows.
  d.parameterKeys = {"mode", "window_frames", "overlap", "window_shape",
                     "tolerance_frames", "formant_ratio", "puv_hz"};
  d.parameters = {
      {
          .key = "mode",
          .displayName = "Mode",
          .kind = EngineParamKind::Text,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 3.0,  // fixed | adaptive | pitch_synced | pitch_formant (final)
          .defaultPlain = 0.0,  // "fixed" (§6.6 default)
          .unit = "",
          .stepCount = 3,
          .choiceNames = kTimePitchModeChoices,
          .choiceValues = nullptr,
          .choiceCount = 4,
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
      {
          // §6.6.1 item 1: the HIDDEN Pitch-Synced row (exposed=false —
          // never in the VST surface, no binding; the adapter writes the
          // validated default; all Task28 evidence is at that default).
          .key = "puv_hz",
          .displayName = "Unvoiced Period",
          .kind = EngineParamKind::Real,
          .role = EngineParamRole::Configuration,
          .exposed = false,
          .min = 50.0,
          .max = 500.0,
          .defaultPlain = 200.0,
          .unit = "Hz",
          .stepCount = -1,
          .choiceNames = nullptr,
          .choiceValues = nullptr,
          .choiceCount = 0,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%.1f",
      },
      {
          // §6.6.1 item 18: tolerance_frames is visible iff mode ∈
          // {adaptive} (choice index 1) — registered always, shown per mode.
          .key = "tolerance_frames",
          .displayName = "Tolerance",
          .kind = EngineParamKind::Integer,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.0,
          .max = 8192.0,
          .defaultPlain = 768.0,  // ~16 ms @48 kHz (candidate_wsola.h:44)
          .unit = "frames",
          .stepCount = 8192,
          .choiceNames = nullptr,
          .choiceValues = nullptr,
          .choiceCount = 0,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%d",
          .visibleWhenKey = "mode",
          .visibleWhenValues = kTpVisibleAdaptiveOnly,
          .visibleWhenCount = 1,
      },
      {
          // Checkpoint 4: formant_ratio is visible iff mode ∈
          // {pitch_formant} (choice index 3) — registered always, shown per
          // mode (the item-18 mechanism; the FINAL 25-row surface row).
          .key = "formant_ratio",
          .displayName = "Formant",
          .kind = EngineParamKind::Real,
          .role = EngineParamRole::Configuration,
          .exposed = true,
          .min = 0.25,
          .max = 4.0,
          .defaultPlain = 1.0,  // = the TD-PSOLA bit-identity (candidate_fdpsola)
          .unit = "x",
          .stepCount = -1,
          .choiceNames = nullptr,
          .choiceValues = nullptr,
          .choiceCount = 0,
          .automatable = false,
          .rebuildsChain = true,
          .dispFmt = "%.2f",
          .visibleWhenKey = "mode",
          .visibleWhenValues = kTpVisibleFormantOnly,
          .visibleWhenCount = 1,
      },
  };
  d.factory = &makeTimePitchEngine;
  return d;
}

}  // namespace pitchlab
