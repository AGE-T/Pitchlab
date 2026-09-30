#pragma once

// Pitch Lab — Task 28 Phase B candidate: WSOLA + resampling.
//
// CLEAN-ROOM SOURCE: own implementation from the published waveform-
// similarity overlap-add description (Verhelst & Roelands, ICASSP 1993 —
// patents expired). No SoundTouch code (LGPL-2.1+), no DAFx MATLAB code.
//
// MODEL (frozen before coding):
//   * Same two-stage engine as proto.ola (see candidate_ola.h): grain
//     stretch with window-product normalisation + the shared §7
//     resampler. ONLY the per-grain ANALYSIS-POSITION CHOICE changes.
//   * Per grain m (nominal analysis centre a, synthesis centre s): choose
//     delta in [-tolerance_frames, +tolerance_frames] (integer grid)
//     minimising the sum of squared differences between the candidate's
//     overlap content and the OUTPUT BUILT SO FAR over the overlap region
//     O = [s - N/2, s - Hs + N/2) (the region where the new grain will
//     overlap the already-placed grains):
//         SSE(delta) = sum_{p in O} ( x[ a + delta + (p - s) ] - yhat[p] )^2
//     with yhat[p] = accum[p]/wsum[p] (the normalised built estimate;
//     0-guard). Grain 0 has no built output => delta = 0.
//   * Deterministic search: delta evaluated in the order 0, +1, -1, +2,
//     -2, ...; strictly-smaller SSE replaces the best, so ties resolve to
//     the delta CLOSEST TO ZERO (the nominal schedule is preferred among
//     equal candidates — on near-silent material every SSE ties, and a
//     -tolerance-prefering tie-break was a MEASURED defect: systematic
//     backward drift until the input window overflowed). Channel 0 drives
//     the shared decision for stereo (documented: the scheduling decision
//     is shared, per-channel audio identical => coherence kept).
//   * Drift-free schedule: the tolerance region is centred on the
//     TIME-SCALING LAW's nominal position; the chosen delta perturbs only
//     the current grain and the next nominal advances from the LAW
//     (never from the chosen position). The content mapping therefore
//     cannot wander beyond +-tolerance per grain — structurally bounded,
//     matching the published formulation's intent while keeping the
//     synthesis grid exact (s += Hs; duration preserved on the stretch
//     grid by construction).
//   * Block-streaming invariance: the search depends only on the grain
//     schedule and the output-so-far (both block-independent) => the
//     output is bit-identical for any block split (asserted by the RT
//     probe).
//
// Engine parameters (in addition to the OLA set):
//   tolerance_frames  int  >= 0, <= 8192 (default 768 ~ 16 ms @ 48 kHz)

#include <vector>

#include "prototypes/candidate_ola.h"

namespace pitchlab::proto {

class WsolaPrototype : public OlaPrototype {
 public:
  const char* engineId() const override { return "proto.wsola"; }
  void configure(const ProtoParams& params) override;
  void prepare(double fs, int channels, int maxBlockFrames,
               FrameCount totalInputFrames,
               const double* ratioCurve) override;
  [[nodiscard]] double grainAnalysisAdjustment(double aNominal) override;
  json::Value diagnostics() const override;

 private:
  int tolerance_ = 768;
  // Search scratch (allocated in prepare; process never allocates).
  std::vector<double> builtScratch_;
  // Search statistics (the character evidence).
  int64_t searches_ = 0;
  int64_t sumDelta_ = 0;
  int64_t sumAbsDelta_ = 0;
  int64_t maxAbsDelta_ = 0;
  int64_t zeroDeltas_ = 0;
};

}  // namespace pitchlab::proto
