#pragma once

// Pitch Lab — Task 28 Phase E candidate: transient-aware phase vocoder.
//
// CLEAN-ROOM SOURCE: own implementation from the published classic-PV
// time-stretch pipeline (Flanagan & Golden 1966 / Portnoff 1981 lineage —
// the phase-propagation equations as documented in the old research
// digest) + the published transient-aware phase-reset concept (Röbel 2003
// DAFx; Rubber Band R2's OptionTransientsCrisp behaviour — header docs
// only, no code). No Rubber Band, no phaseret, no DAFx M-files code.
//
// MODEL (frozen before coding; built ON the OLA prototype's stretch
// machinery through the modifyGrain content hook — the block-streaming,
// window-product normalisation, stretch grid and the §7 resample stage
// are inherited unchanged):
//   * Frames: the input read at the FIXED analysis hop Ha_n = Hs/rho_n
//     (the OLA engine's own stretch grid) with the FIXED synthesis hop
//     Hs = N/overlap — the "fixed synthesis grid" classic-PV formulation
//     (stretch factor rho on the analysis hops; duration preserved by the
//     inherited resample stage).
//   * Per bin k (per channel):
//       dphi_k(n) = princarg(phi_a_k(n) - phi_a_k(n-1) - 2 pi k Ha_n / N)
//       omega_k(n) = 2 pi k / N + dphi_k(n) / Ha_n        (true IF)
//       phi_s_k(n) = phi_s_k(n-1) + omega_k(n) * Hs       (propagation)
//     The grain is resynthesised with |X_k| and phi_s_k (r2c + c2r,
//     persistent pocketfft plan — the same pattern as the frozen PV
//     engines), replacing the windowed input grain in the scratch buffer.
//   * TRANSIENT DETECTION (deterministic, per frame): spectral flux
//     flux(n) = sum_k max(0, |X_k(n)| - |X_k(n-1)|) over channel 0;
//     transient when flux > sensitivity * median(flux over the last 8
//     frames) AND the frame energy is above a silence floor.
//   * PHASE RESET at a transient frame (Röbel / Rubber Band semantics):
//     phi_s_k := phi_a_k for the bins in the reset band —
//     reset_mode "full": all bins; "band": 150 Hz..1 kHz only (the
//     Rubber Band 'mixed' band-limited reset); "off": never (the
//     classic-PV equivalence — the internal baseline for measuring what
//     the reset buys).
//   * Frame 0: synthesis phases := analysis phases (the standard init).
//   * Channels: per-channel phase propagation; the transient decision is
//     SHARED (detected on channel 0) — inter-channel coherence on the
//     resets, per-bin drift elsewhere (the classic PV behaviour).
//
// Engine parameters (in addition to the OLA set — window_shape must be
// "hann", overlap 2 or 4 (75% recommended for PV)):
//   reset_mode   str  "full" (default) | "band" | "off"
//   sensitivity  dbl  flux threshold multiplier (default 2.5, >= 0.5)

#include <memory>
#include <vector>

#include <pocketfft/pocketfft_hdronly.h>

#include "prototypes/candidate_ola.h"

namespace pitchlab::proto {

class PvTransientPrototype : public OlaPrototype {
 public:
  const char* engineId() const override { return "proto.pvtransient"; }
  void configure(const ProtoParams& params) override;
  void prepare(double fs, int channels, int maxBlockFrames,
               FrameCount totalInputFrames,
               const double* ratioCurve) override;
  Latency latency() const override;
  json::Value diagnostics() const override;

 protected:
  void modifyGrain(const GrainContext& ctx) override;

 private:
  std::string resetMode_ = "full";
  double sensitivity_ = 2.5;
  // FFT state (one persistent plan — the frame length is fixed).
  std::unique_ptr<pocketfft::detail::pocketfft_r<double>> plan_;
  std::vector<double> packed_;
  std::vector<double> magPrev_;   // per channel: |X_k| of frame n-1
  std::vector<double> phasePrev_; // per channel: phi_a_k(n-1)
  std::vector<double> synthPhase_;// per channel: phi_s_k(n-1)
  std::vector<double> fluxHist_;  // last 8 flux values (shared detection)
  bool havePrev_ = false;
  int framesProcessed_ = 0;
  // diagnostics
  int64_t resets_ = 0;
  int64_t frames_ = 0;
};

}  // namespace pitchlab::proto
