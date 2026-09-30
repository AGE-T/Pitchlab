#pragma once

// Pitch Lab — Task 28 Phase A candidate: OLA + resampling.
//
// CLEAN-ROOM SOURCE: own implementation from the published conventional
// structure (fixed-hop overlap-add time-stretch with window-product
// normalisation, then pitch-ratio resampling through the project's shared
// §7 resampler). No DAFx MATLAB code, no external implementation, no
// external library (the DAFx M-files carry an educational-only licence
// header — reimplemented from the published math only).
//
// MODEL (frozen here before coding):
//   * Stage A (OLA stretch): synthesis grains of `window_frames` N at the
//     FIXED synthesis hop Hs = N/overlap on the stretch timeline; grain m
//     is centred at s_m = N/2 + m*Hs and reads the input centred at
//     a_m = N/2 + sum_{j<m} Hs/rho_j  (stretch factor rho = commanded
//     pitch ratio, curve-indexed at floor(a_m), clamped). Window-product
//     normalisation: y[p] = accum[p]/wsum[p] with a 0-guard (edges fade
//     in/out; non-COLA overlap/shape combinations keep their ripple,
//     measured and reported as the COLA diagnostic).
//   * Stage B (resample): reads the finalised stretch signal at absolute
//     double position q with per-output-frame ratio rho(t) = curve[t]
//     (output timeline == input timeline, the composite duration-
//     preserving map), cutoff antiAliasCutoff(rho), Standard preset —
//     the same primitive and policy as the frozen PV engines.
//   * Ratio indexing design note (documented limitation): stage A indexes
//     rho at the INPUT position, stage B at the OUTPUT position; for
//     slowly varying curves these coincide; for fast curves there is a
//     bounded (window-scale) local mismatch — characterised by the
//     ramp/reversal records, not hidden.
//   * Block-streaming: single grain schedule + absolute double
//     accumulators + absolute-position resampler reads => bit-identical
//     output for any block split (asserted by the RT probe).
//   * WSOLA (Phase B) subclasses this engine and only replaces the
//     per-grain ANALYSIS-POSITION CHOICE (the waveform-similarity search,
//     Verhelst & Roelands 1993 — patents expired, clean-room from the
//     published description; no SoundTouch code).
//
// Engine parameters (validated in configure()):
//   window_frames  int    >= 64, <= 16384 (default 2048)
//   overlap        int    one of {2, 4, 8}; Hs = N/overlap (default 2)
//   window_shape   str    "hann" | "hamming" | "bartlett" | "rect"
//                         (default "hann"; periodic windows)

#include <string>
#include <vector>

#include "prototypes/proto_engine.h"

namespace pitchlab::proto {

class OlaPrototype : public ProtoEngine {
 public:
  const char* engineId() const override { return "proto.ola"; }
  void configure(const ProtoParams& params) override;
  void prepare(double fs, int channels, int maxBlockFrames,
               FrameCount totalInputFrames,
               const double* ratioCurve) override;
  Latency latency() const override;
  ProcessOutcome process(const AudioBlockView& in, int inFrames,
                          AudioBlockOut& out, int outCapacity,
                          FrameCount inputFrameIndex) override;
  ProcessOutcome finish(AudioBlockOut& out, int outCapacity) override;
  json::Value diagnostics() const override;

 protected:
  // Per-grain analysis-position adjustment: OLA places every grain at the
  // nominal schedule position (returns 0). WSOLA overrides this with the
  // similarity search. `aNominal` is the nominal analysis centre; the
  // returned delta shifts the read centre (the schedule re-anchors at the
  // chosen position, the WSOLA drift semantics).
  [[nodiscard]] virtual double grainAnalysisAdjustment(double aNominal);

  // Built-output estimate at absolute stretch position p (the WSOLA
  // comparison target): the normalised accumulator where already final.
  [[nodiscard]] double builtEstimateAt(FrameCount stretchPos) const;

  // configuration
  int windowFrames_ = 2048;
  int overlap_ = 2;
  std::string shape_ = "hann";

  // job state (all allocated in prepare())
  double fs_ = 0.0;
  int channels_ = 0;
  int maxBlock_ = 0;
  FrameCount nIn_ = 0;
  const double* curve_ = nullptr;
  int hs_ = 0;                       // synthesis hop
  std::vector<double> window_;       // length N
  std::vector<std::vector<double>> stretch_;  // normalised-in-place (see .cpp)
  std::vector<double> wsum_;         // window sum per stretch position
  std::vector<std::vector<double>> inBuf_;    // sliding input window
  FrameCount inBase_ = 0;            // absolute index of inBuf_[c][0]
  FrameCount inAvail_ = 0;           // absolute end of delivered input
  int inputBackMargin_ = 0;          // extra history margin below a - N/2
                                     // (WSOLA search reach; 0 for OLA)
  FrameCount stretchBase_ = 0;       // absolute index of stretch_[c][0]
  FrameCount stretchCapacity_ = 0;
  FrameCount finalFrontier_ = 0;     // absolute: < finalFrontier_ is final
  double aNext_ = 0.0;               // next grain analysis centre (input tl)
  double sNext_ = 0.0;               // next grain synthesis centre (stretch)
  int64_t grainsPlaced_ = 0;
  double q_ = 0.0;                   // resampler absolute stretch position
  FrameCount tOut_ = 0;              // absolute output position (== produced)
  bool finishing_ = false;
  double stretchEndD_ = 0.0;         // finish() drain target (stretch tl)
  // diagnostics
  double wsumRippleDb_ = 0.0;
  int64_t maxLiveStretch_ = 0;

 private:
  bool placeGrainsUpTo(FrameCount inputAvailableEnd);
  void emitFinalFrames(AudioBlockOut& out, int outCapacity,
                       FrameCount& produced);
  [[nodiscard]] double ratioAtInput(double pos) const;
  [[nodiscard]] double ratioAtOutput(FrameCount t) const;
  void compactStretch();
  void compactInput();
  [[nodiscard]] double readInput(int channel, FrameCount pos) const;
};

}  // namespace pitchlab::proto
