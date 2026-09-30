// Pitch Lab — Task 28 Phase E candidate: transient-aware PV (see
// candidate_pvtransient.h for the frozen model).
//
// The per-frame processing (single pass per channel, allocation-free):
//   forward r2c -> (channel 0: flux vs the PREVIOUS magnitudes) ->
//   the shared transient decision (median of the previous 8 fluxes) ->
//   per-bin phase propagation/reset (reading the PREVIOUS analysis phase
//   before overwriting) -> the synthesis spectrum written into the packed
//   layout -> c2r (1/N) -> the grain replaced in the scratch buffer.

#include "prototypes/candidate_pvtransient.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/errors.h"
#include <pocketfft/pocketfft_hdronly.h>

namespace pitchlab::proto {
namespace {

constexpr double kPi = 3.14159265358979323846;

double princarg(double x) {
  double v = std::fmod(x + kPi, 2.0 * kPi);
  if (v < 0.0) v += 2.0 * kPi;
  return v - kPi;
}

}  // namespace

void PvTransientPrototype::configure(const ProtoParams& params) {
  ProtoParams baseParams;
  for (const auto& [key, value] : params) {
    if (key == "reset_mode") {
      if (const auto* s = std::get_if<std::string>(&value)) {
        if (*s != "full" && *s != "band" && *s != "off") {
          throw ConfigError("", "reset_mode", "must be one of full|band|off");
        }
        resetMode_ = *s;
      } else {
        throw ConfigError("", "reset_mode", "must be a string");
      }
    } else if (key == "sensitivity") {
      if (const auto* d = std::get_if<double>(&value)) {
        if (!std::isfinite(*d) || *d < 0.5 || *d > 100.0) {
          throw ConfigError("", "sensitivity", "must be in [0.5, 100]");
        }
        sensitivity_ = *d;
      } else {
        throw ConfigError("", "sensitivity", "must be a number");
      }
    } else if (key == "window_shape") {
      if (const auto* s = std::get_if<std::string>(&value)) {
        if (*s != "hann") {
          throw ConfigError("", "window_shape",
                            "proto.pvtransient requires 'hann'");
        }
        baseParams.emplace(key, value);
      } else {
        throw ConfigError("", "window_shape", "must be a string");
      }
    } else {
      baseParams.emplace(key, value);
    }
  }
  OlaPrototype::configure(baseParams);
  if (overlap_ != 2 && overlap_ != 4) {
    throw ConfigError("", "overlap",
                      "proto.pvtransient requires overlap 2 or 4 "
                      "(50/75% PV overlap)");
  }
}

void PvTransientPrototype::prepare(double fs, int channels, int maxBlockFrames,
                                   FrameCount totalInputFrames,
                                   const double* ratioCurve) {
  OlaPrototype::prepare(fs, channels, maxBlockFrames, totalInputFrames,
                        ratioCurve);
  const int N = windowFrames_;
  const int halfN = N / 2;
  const std::size_t bins = static_cast<std::size_t>(halfN) + 1;
  plan_ = std::make_unique<pocketfft::detail::pocketfft_r<double>>(
      static_cast<std::size_t>(N));
  packed_.assign(static_cast<std::size_t>(N), 0.0);
  magPrev_.assign(static_cast<std::size_t>(channels_) * bins, 0.0);
  phasePrev_.assign(static_cast<std::size_t>(channels_) * bins, 0.0);
  synthPhase_.assign(static_cast<std::size_t>(channels_) * bins, 0.0);
  fluxHist_.assign(8, 0.0);
  havePrev_ = false;
  framesProcessed_ = 0;
  resets_ = 0;
  frames_ = 0;
}

OlaPrototype::Latency PvTransientPrototype::latency() const {
  Latency lat = OlaPrototype::latency();
  // The flux median uses the previous 8 frames: 8 synthesis hops of extra
  // lookahead (deterministic causal detection).
  lat.inputLookahead += 8 * static_cast<FrameCount>(hs_);
  lat.outputFlush += 8 * static_cast<FrameCount>(hs_);
  return lat;
}

void PvTransientPrototype::modifyGrain(const GrainContext& ctx) {
  const int N = ctx.grainLen;
  const int halfN = N / 2;
  const std::size_t bins = static_cast<std::size_t>(halfN) + 1;
  if (static_cast<std::size_t>(N) > packed_.size()) return;  // defensive

  // The analysis hop actually used by this grain (the OLA engine's
  // stretch grid): Ha = Hs / rho at the grain's analysis position.
  const double rho = ratioAtInput(ctx.analysisPos);
  const double ha = static_cast<double>(hs_) / rho;

  // ---- the shared transient decision (after channel 0's forward
  // transform, before ANY propagation — the flux needs the full frame) ----
  bool transient = false;
  if (channels_ >= 1) {
    std::memcpy(packed_.data(),
                grainScratch_.data() +
                    static_cast<std::size_t>(0) * static_cast<std::size_t>(N),
                static_cast<std::size_t>(N) * sizeof(double));
    plan_->exec(packed_.data(), 1.0, true);
    double flux = 0.0;
    if (havePrev_) {
      for (int k = 0; k <= halfN; ++k) {
        double re, im;
        if (k == 0) {
          re = packed_[0];
          im = 0.0;
        } else if (k == halfN && (N % 2) == 0) {
          re = packed_[static_cast<std::size_t>(N - 1)];
          im = 0.0;
        } else {
          re = packed_[static_cast<std::size_t>(2 * k - 1)];
          im = packed_[static_cast<std::size_t>(2 * k)];
        }
        const double mag = std::sqrt(re * re + im * im);
        const double d =
            mag - magPrev_[static_cast<std::size_t>(k)];  // ch 0 offset 0
        if (d > 0.0) flux += d;
      }
    }
    if (havePrev_ && resetMode_ != "off") {
      double s[8];
      std::memcpy(s, fluxHist_.data(), sizeof(s));
      std::sort(s, s + 8);
      const double med = (s[3] + s[4]) * 0.5;  // median of the PREVIOUS 8
      transient = med > 1.0e-12 && flux > sensitivity_ * med;
    }
    // Shift the current flux into the history.
    for (int i = 7; i > 0; --i) {
      fluxHist_[static_cast<std::size_t>(i)] =
          fluxHist_[static_cast<std::size_t>(i - 1)];
    }
    fluxHist_[static_cast<std::size_t>(0)] = flux;
  }

  // ---- per-channel propagation + synthesis ----
  for (int c = 0; c < channels_; ++c) {
    const std::size_t chOff = static_cast<std::size_t>(c) * bins;
    std::memcpy(packed_.data(),
                grainScratch_.data() +
                    static_cast<std::size_t>(c) * static_cast<std::size_t>(N),
                static_cast<std::size_t>(N) * sizeof(double));
    plan_->exec(packed_.data(), 1.0, true);
    for (int k = 0; k <= halfN; ++k) {
      double re, im;
      if (k == 0) {
        re = packed_[0];
        im = 0.0;
      } else if (k == halfN && (N % 2) == 0) {
        re = packed_[static_cast<std::size_t>(N - 1)];
        im = 0.0;
      } else {
        re = packed_[static_cast<std::size_t>(2 * k - 1)];
        im = packed_[static_cast<std::size_t>(2 * k)];
      }
      const double mag = std::sqrt(re * re + im * im);
      const double phi = std::atan2(im, re);
      const std::size_t kb = chOff + static_cast<std::size_t>(k);
      const double prevPhi = phasePrev_[kb];  // read BEFORE overwrite
      double synthPhi;
      if (!havePrev_) {
        synthPhi = phi;  // frame 0: synthesis := analysis
      } else if (transient && resetMode_ != "off") {
        const double binHz =
            static_cast<double>(k) * fs_ / static_cast<double>(N);
        const bool inBand =
            resetMode_ == "full" || (binHz >= 150.0 && binHz <= 1000.0);
        synthPhi = inBand ? phi : synthPhase_[kb];
      } else {
        const double dphi = princarg(
            phi - prevPhi -
            2.0 * kPi * static_cast<double>(k) * ha / static_cast<double>(N));
        const double omega =
            2.0 * kPi * static_cast<double>(k) / static_cast<double>(N) +
            dphi / ha;
        synthPhi =
            princarg(synthPhase_[kb] + omega * static_cast<double>(hs_));
      }
      // Write the synthesis bin (magnitude preserved, phase replaced).
      if (k == 0) {
        packed_[0] = mag * std::cos(synthPhi);
      } else if (k == halfN && (N % 2) == 0) {
        packed_[static_cast<std::size_t>(N - 1)] = mag * std::cos(synthPhi);
      } else {
        packed_[static_cast<std::size_t>(2 * k - 1)] = mag * std::cos(synthPhi);
        packed_[static_cast<std::size_t>(2 * k)] = mag * std::sin(synthPhi);
      }
      // Update the state AFTER use.
      phasePrev_[kb] = phi;
      magPrev_[kb] = mag;
      synthPhase_[kb] = synthPhi;
    }
    // Inverse transform: the phase-modified grain (c2r, 1/N).
    plan_->exec(packed_.data(), 1.0 / static_cast<double>(N), false);
    std::memcpy(grainScratch_.data() +
                    static_cast<std::size_t>(c) * static_cast<std::size_t>(N),
                packed_.data(), static_cast<std::size_t>(N) * sizeof(double));
  }
  if (transient) ++resets_;
  ++frames_;
  havePrev_ = true;
}

json::Value PvTransientPrototype::diagnostics() const {
  json::Object o;
  json::Value base = OlaPrototype::diagnostics();
  if (base.kind() == json::Value::Kind::Object) {
    for (const auto& [k, v] : base.asObject()) o[k] = v;
  }
  o["pv_frames"] = json::Value(static_cast<int64_t>(frames_));
  o["pv_resets"] = json::Value(static_cast<int64_t>(resets_));
  o["reset_mode"] = json::Value(resetMode_);
  o["sensitivity"] = json::Value(sensitivity_);
  return json::Value(std::move(o));
}

}  // namespace pitchlab::proto
