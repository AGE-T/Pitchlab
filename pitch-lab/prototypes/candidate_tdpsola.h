#pragma once

// Pitch Lab — Task 28 Phase C candidate: TD-PSOLA.
//
// CLEAN-ROOM SOURCE: own implementation from the published pitch-
// synchronous overlap-add description (Moulines & Charpentier 1990,
// Speech Communication 9:453-467 — read in full during Task 3; also the
// DAFx book psola.m structure, educational-only licence — NOT used, no
// code imported; no sannawag/maxrmorrison Python (MIT/GPL-3/Praat-wrap)
// code read or linked).
//
// MODEL (frozen before coding; CORRECTED after a measured design error —
// the correction and its evidence are part of the research record):
//   * ANALYSIS (offline pre-pass, the analyzeSignal extension): the
//     project's clean-room pYIN tracker (analysis layer, reuse) gives F0
//     + voicing per frame (window 2048 / hop 512 @ 48k). Pitch marks:
//     nominal marks every P(t) (voiced) or P_uv (unvoiced, default 200 Hz
//     spacing), each refined to the nearest positive-going zero crossing
//     within +-P/4 (the minimal epoch alignment; a true epoch detector is
//     a documented future refinement, not silently assumed).
//   * SYNTHESIS (the corrected MC90 semantics):
//         s_k = P(a_k)/beta   (output mark spacing => output pitch = beta x F0)
//       mode "pitch" (default): u_k = P — cycle-accurate input consumption;
//         the DURATION changes by 1/beta over voiced spans (the classic
//         MC90 / DAFx psola.m PITCH MODIFICATION: formant-preserving,
//         duration-changing). NOT duration-preserving — by design, per the
//         published formulation.
//       mode "stretch": u_k = s_k/alpha (mark reuse/skip = the PSOLA
//         time-scale by alpha), then the shared §7 resampler by alpha =>
//         the conventional duration-preserving pitch shifter (formants
//         shift with the resampler).
//       [DESIGN ERROR, root-caused by measurement: the first version used
//       u = s in "pitch" mode (attempting duration preservation); with
//       mark reuse the output became 2s-periodic — the measured 444 Hz on
//       BOTH +12 and -12 of a 440 Hz sine instead of 880/220 — the
//       sub-multiple periodicity of the repeated-mark pattern. The direct
//       duration-preserving pure-PSOLA pitch shift is NOT a valid mark
//       schedule; the published algorithms either change duration (pitch
//       mode) or resample (stretch mode). Fixed before any quality
//       conclusion was drawn.]
//       Unvoiced: s = u = P_uv regardless of beta (noise has no pitch to
//       scale; documented passthrough — duration of unvoiced spans is
//       unchanged in BOTH modes).
//   * DYNAMIC: beta/alpha indexed at a_k per mark — per-period control
//     rate (2.5-10 ms on voice), the family's documented fast lane; the
//     F0/voicing analysis lookahead (tracker window+hop ~ 53 ms @ 48k) is
//     the realtime cost driver (RT probe reports analysis and synthesis
//     separately).
//   * BLOCK-STREAMING: marks and grain schedule are block-independent
//     absolute schedules; the accumulators/normalisation follow the OLA
//     engine's in-place pattern => bit-identical output for any block
//     split (asserted).
//
// Engine parameters:
//   mode      str  "pitch" (default) | "stretch"
//   puv_hz    dbl  unvoiced mark spacing in Hz (default 200; >= 50, <= 500)

#include <vector>

#include "prototypes/proto_engine.h"

namespace pitchlab::proto {

class TdPsolaPrototype : public ProtoEngine {
 public:
  const char* engineId() const override { return "proto.tdpsola"; }
  void configure(const ProtoParams& params) override;
  void analyzeSignal(const std::vector<std::vector<double>>& input) override;
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
  // FD-PSOLA hook: the windowed grain (length 2*P, channels planar in
  // grainBuf_) may be transformed in place before accumulation.
  struct GrainContext {
    double beta = 1.0;    // pitch factor applied to this grain's spacing
    double periodFrames = 0.0;  // the analysis period P at this grain
    bool voiced = false;
  };
  virtual void transformGrain(GrainContext ctx) { (void)ctx; }

  // configuration
  std::string mode_ = "pitch";
  double puvHz_ = 200.0;

  // analysis results (from analyzeSignal)
  struct Mark {
    FrameCount pos = 0;      // refined mark position (input timeline)
    double period = 0.0;     // period at the mark (frames)
    bool voiced = false;
  };
  std::vector<Mark> marks_;
  double analysisMsPerSecond_ = 0.0;  // measured analysis cost (evidence)
  int64_t analysisFrames_ = 0;

  // job state (allocated in prepare())
  double fs_ = 0.0;
  int channels_ = 0;
  int maxBlock_ = 0;
  FrameCount nIn_ = 0;
  const double* curve_ = nullptr;
  bool stretchMode_ = false;
  double puvFrames_ = 240.0;
  double pMax_ = 960.0;  // period bound (fs / 50 Hz)
  double pMin_ = 48.0;   // fs / 1000 Hz
  // input sliding window
  std::vector<std::vector<double>> inBuf_;
  FrameCount inBase_ = 0;
  FrameCount inAvail_ = 0;
  // output accumulator (pitch mode: output grid; stretch mode: stretch
  // grid + resample stage) — the OLA engine's in-place pattern
  std::vector<std::vector<double>> accum_;
  std::vector<double> wsum_;
  std::vector<double> grainBuf_;  // scratch: one grain, planar channels
  // Hann cache keyed by grain length (built in prepare from the mark
  // periods — zero allocation in the process path).
  std::vector<std::pair<int, std::vector<double>>> windowCache_;
  const std::vector<double>& windowFor(int gLen);
  FrameCount accumBase_ = 0;
  FrameCount accumCapacity_ = 0;
  FrameCount finalFrontier_ = 0;
  double aNext_ = 0.0;   // next output mark's input-time position
  double tNext_ = 0.0;   // next output mark's output-grid centre
  int64_t grainsPlaced_ = 0;
  int64_t reusedGrains_ = 0;  // consecutive marks hitting the same analysis
                              // mark (the beta>1 repetition family)
  double q_ = 0.0;             // stretch-mode resampler position
  FrameCount tOut_ = 0;        // absolute output position (== produced)
  bool finishing_ = false;
  double drainEnd_ = 0.0;
  // mark lookup: index of the last mark <= position (advancing cursor)
  std::size_t markCursor_ = 0;
  // diagnostics
  int64_t voicedGrains_ = 0;
  int64_t unvoicedGrains_ = 0;
  int64_t maxLiveAccum_ = 0;

 private:
  bool placeGrainsUpTo(FrameCount inputAvailableEnd);
  void emitFinalFrames(AudioBlockOut& out, int outCapacity,
                       FrameCount& produced);
  [[nodiscard]] double ratioAt(double pos) const;
  void compactInput();
  void compactAccum();
  [[nodiscard]] double readInput(int channel, FrameCount pos) const;
  [[nodiscard]] std::size_t markIndexNear(double a) const;
};

}  // namespace pitchlab::proto
