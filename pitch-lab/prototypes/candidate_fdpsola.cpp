// Pitch Lab — Task 28 Phase D candidate: FD-PSOLA (see
// candidate_fdpsola.h for the frozen model).

#include "prototypes/candidate_fdpsola.h"

#include <cmath>
#include <cstring>

#include "core/errors.h"
#include <pocketfft/pocketfft_hdronly.h>

namespace pitchlab::proto {

void FdPsolaPrototype::configure(const ProtoParams& params) {
  // Validate/absorb formant_ratio; forward the rest to the TD-PSOLA base.
  ProtoParams baseParams;
  for (const auto& [key, value] : params) {
    if (key == "formant_ratio") {
      if (const auto* d = std::get_if<double>(&value)) {
        if (!std::isfinite(*d) || *d < 0.25 || *d > 4.0) {
          throw ConfigError("", "formant_ratio", "must be in [0.25, 4.0]");
        }
        formantRatio_ = *d;
      } else {
        throw ConfigError("", "formant_ratio", "must be a number");
      }
    } else {
      baseParams.emplace(key, value);
    }
  }
  TdPsolaPrototype::configure(baseParams);
}

void FdPsolaPrototype::prepare(double fs, int channels, int maxBlockFrames,
                               FrameCount totalInputFrames,
                               const double* ratioCurve) {
  TdPsolaPrototype::prepare(fs, channels, maxBlockFrames, totalInputFrames,
                            ratioCurve);
  // Build the FFT plan cache for every distinct grain length (the base's
  // window cache enumerates exactly the set the marks can produce).
  planCache_.clear();
  for (const auto& [gLen, w] : windowCache_) {
    (void)w;
    planCache_.emplace_back(
        gLen, std::make_unique<pocketfft::detail::pocketfft_r<double>>(
                  static_cast<std::size_t>(gLen)));
  }
  packedBuf_.assign(static_cast<std::size_t>(2.0 * pMax_) + 16, 0.0);
  specRe_.assign(static_cast<std::size_t>(pMax_) + 2, 0.0);
  specIm_.assign(static_cast<std::size_t>(pMax_) + 2, 0.0);
  grainsTransformed_ = 0;
}

void FdPsolaPrototype::transformGrain(GrainContext ctx) {
  if (!ctx.voiced || std::abs(formantRatio_ - 1.0) < 1.0e-9) {
    return;  // gamma = 1: FD-PSOLA is bit-identical to TD-PSOLA.
  }
  const int gLen = std::max(4, static_cast<int>(std::round(2.0 * ctx.periodFrames)));
  if (static_cast<std::size_t>(gLen) > packedBuf_.size() ||
      static_cast<std::size_t>(gLen / 2 + 1) > specRe_.size()) {
    return;  // defensive: grain bounds are prepare-covered.
  }
  // Find the plan for this grain length.
  pocketfft::detail::pocketfft_r<double>* plan = nullptr;
  for (const auto& [len, p] : planCache_) {
    if (len == gLen) {
      plan = p.get();
      break;
    }
  }
  if (plan == nullptr) {
    return;  // defensive: unseen lengths are window-cache-bounded.
  }
  const int halfN = gLen / 2;
  const double g = formantRatio_;
  for (int c = 0; c < channels_; ++c) {
    double* grain = grainBuf_.data() +
                    static_cast<std::size_t>(c) * static_cast<std::size_t>(gLen);
    // Forward transform (packed halfcomplex, in the scratch copy).
    std::memcpy(packedBuf_.data(), grain,
                static_cast<std::size_t>(gLen) * sizeof(double));
    plan->exec(packedBuf_.data(), 1.0, true);
    // Unpack the half-spectrum.
    for (int k = 0; k <= halfN; ++k) {
      double re, im;
      if (k == 0) {
        re = packedBuf_[0];
        im = 0.0;
      } else if (k == halfN && (gLen % 2) == 0) {
        re = packedBuf_[static_cast<std::size_t>(gLen - 1)];
        im = 0.0;
      } else {
        re = packedBuf_[static_cast<std::size_t>(2 * k - 1)];
        im = packedBuf_[static_cast<std::size_t>(2 * k)];
      }
      specRe_[static_cast<std::size_t>(k)] = re;
      specIm_[static_cast<std::size_t>(k)] = im;
    }
    // Spectral interpolation X'(k') = X(k'/gamma): the grain's spectrum
    // (comb + envelope) stretches by gamma. Linear in re/im; source
    // positions beyond the analysis half-Nyquist read zero (honest
    // band-limiting for gamma < 1).
    for (int kp = 0; kp <= halfN; ++kp) {
      const double q = static_cast<double>(kp) / g;
      const int k0 = static_cast<int>(std::floor(q));
      const double frac = q - static_cast<double>(k0);
      double re, im;
      if (k0 >= halfN) {
        re = (k0 == halfN) ? specRe_[static_cast<std::size_t>(halfN)] : 0.0;
        im = 0.0;
        if (k0 == halfN && frac > 0.0) {
          re = 0.0;  // interpolating past the Nyquist bin reads zero
        }
      } else {
        const double re0 = specRe_[static_cast<std::size_t>(k0)];
        const double im0 = specIm_[static_cast<std::size_t>(k0)];
        const double re1 = specRe_[static_cast<std::size_t>(k0 + 1)];
        const double im1 = specIm_[static_cast<std::size_t>(k0 + 1)];
        re = re0 + frac * (re1 - re0);
        im = im0 + frac * (im1 - im0);
      }
      // Repack (write the interpolated half-spectrum).
      if (kp == 0) {
        packedBuf_[0] = re;
        if ((gLen % 2) == 0) packedBuf_[static_cast<std::size_t>(gLen - 1)] = 0.0;
      } else if (kp == halfN && (gLen % 2) == 0) {
        packedBuf_[static_cast<std::size_t>(gLen - 1)] = re;
      } else {
        packedBuf_[static_cast<std::size_t>(2 * kp - 1)] = re;
        packedBuf_[static_cast<std::size_t>(2 * kp)] = im;
      }
    }
    // Inverse transform (c2r; 1/gLen scale), write the grain back.
    plan->exec(packedBuf_.data(), 1.0 / static_cast<double>(gLen), false);
    std::memcpy(grain, packedBuf_.data(),
                static_cast<std::size_t>(gLen) * sizeof(double));
  }
  ++grainsTransformed_;
}

json::Value FdPsolaPrototype::diagnostics() const {
  json::Object o;
  json::Value base = TdPsolaPrototype::diagnostics();
  if (base.kind() == json::Value::Kind::Object) {
    for (const auto& [k, v] : base.asObject()) o[k] = v;
  }
  o["formant_ratio"] = json::Value(formantRatio_);
  o["grains_transformed"] = json::Value(static_cast<int64_t>(grainsTransformed_));
  return json::Value(std::move(o));
}

}  // namespace pitchlab::proto
