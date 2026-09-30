#pragma once

// Pitch Lab — Task 28 Phase D candidate: FD-PSOLA.
//
// CLEAN-ROOM SOURCE: own implementation from the published frequency-
// domain PSOLA description (Moulines & Charpentier 1990 FD variant; the
// DAFx book's psolaF.m structure — educational-only licence, NOT used;
// no external code). Built ON the TD-PSOLA prototype through its
// transformGrain hook: everything else (marks, modes, raw-OLA synthesis,
// block streaming) is inherited unchanged.
//
// MODEL (frozen before coding):
//   * Per VOICED grain (length gLen = 2P, already Hann-windowed in the
//     grain buffer): forward r2c FFT (vendored pocketfft, persistent
//     plans cached per distinct grain length at prepare — zero process
//     allocation), then a SPECTRAL INTERPOLATION of the complex
//     half-spectrum by the FORMANT RATIO gamma:
//         X'(k') = X(k'/gamma)   (linear in re/im; zero beyond Nyquist)
//     i.e. the grain's spectrum (harmonic comb AND envelope) stretches
//     by gamma — the psolaF formant-factor semantics. Then c2r with
//     1/gLen, written back into the grain. The OUTPUT PITCH still comes
//     from the mark re-spacing (the PSOLA machinery); gamma moves the
//     spectral envelope INDEPENDENTLY of the commanded pitch ratio.
//   * gamma = 1.0 (default): the transform is a no-op => FD-PSOLA is
//     bit-identical to TD-PSOLA (the inheritance keeps this exact).
//   * Unvoiced grains: not transformed (the passthrough semantics).
//   * The distinct-capability questions this answers by measurement:
//     (a) formant-only shifting (identity pitch ratio + gamma != 1 —
//         a transformation NO current Pitch Lab engine has);
//     (b) whether explicit envelope control tightens the TD-PSOLA
//         pitch-mode formant drift (centroid 538 vs input 653);
//     (c) the spectral-interpolation character itself (complex-linear
//         phase mixing — the metallic sheen hypothesis).
//
// Engine parameters (in addition to the TD-PSOLA set):
//   formant_ratio  dbl  in [0.25, 4.0] (default 1.0 = TD-PSOLA)

#include <memory>
#include <vector>

#include <pocketfft/pocketfft_hdronly.h>

#include "prototypes/candidate_tdpsola.h"

namespace pitchlab::proto {

class FdPsolaPrototype : public TdPsolaPrototype {
 public:
  const char* engineId() const override { return "proto.fdpsola"; }
  void configure(const ProtoParams& params) override;
  void prepare(double fs, int channels, int maxBlockFrames,
               FrameCount totalInputFrames,
               const double* ratioCurve) override;
  json::Value diagnostics() const override;

 protected:
  void transformGrain(GrainContext ctx) override;

 private:
  double formantRatio_ = 1.0;
  // FFT plans per distinct grain length (cached at prepare).
  std::vector<std::pair<int, std::unique_ptr<pocketfft::detail::pocketfft_r<double>>>>
      planCache_;
  // Scratch (allocated at prepare; process never allocates).
  std::vector<double> packedBuf_;
  std::vector<double> specRe_;
  std::vector<double> specIm_;
  // Diagnostics.
  int64_t grainsTransformed_ = 0;
};

}  // namespace pitchlab::proto
