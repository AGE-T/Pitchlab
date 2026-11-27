// PITCHLAB R23 -- Pitch Control Layer implementation (Gate 6).
// ============================================================================
// PROVENANCE: our own module (round 23). See pitch_control.hpp for the
// architecture contract and the policy list. Every branch below maps to a
// documented, battery-measured policy (cli/r23_control_battery.cpp):
//   acquisition latency, hold/release, octave acceptance latency, outlier
//   rejection, jitter attenuation, glissando following, dropout recovery.
// ============================================================================
#include "octavox/pitch_control.hpp"

// R32-W: <algorithm> is required for std::max (lines ~83-84). libstdc++
// provides it transitively via <cmath>; libc++ (the zig 0.13.0 cross
// toolchain) correctly does not -- include-what-you-use, no semantic change.
#include <algorithm>
#include <cmath>

namespace octavox {

namespace {
constexpr double kTiny = 1e-12;

bool finiteHz(double f) { return std::isfinite(f) && f > 0.0; }

double centsBetween(double a, double b) { return 1200.0 * std::log2(a / b); }
}  // namespace

void PitchControlLayer::configure(const PitchControlConfig& cfg) {
    cfg_ = cfg;
    if (!(cfg_.sample_rate > 0.0) || !std::isfinite(cfg_.sample_rate))
        cfg_.sample_rate = 48000.0;
    if (!(cfg_.cadence_samples > 0.0)) cfg_.cadence_samples = 64.0;
    if (!(cfg_.period_min_hz > 0.0) || cfg_.period_min_hz >= cfg_.period_max_hz) {
        cfg_.period_min_hz = 20.0;
        cfg_.period_max_hz = 1200.0;
    }
    if (!(cfg_.periodicity_gate >= 0.0) || cfg_.periodicity_gate > 1.0)
        cfg_.periodicity_gate = 0.80;
    if (!(cfg_.outlier_cents >= 0.0)) cfg_.outlier_cents = 200.0;
    if (cfg_.octave_confirm_frames < 1) cfg_.octave_confirm_frames = 1;
    if (!(cfg_.octave_tolerance_cents >= 0.0)) cfg_.octave_tolerance_cents = 90.0;
    if (cfg_.lock_confirm_frames < 1) cfg_.lock_confirm_frames = 1;
    if (!(cfg_.slew_cents_per_second >= 0.0)) cfg_.slew_cents_per_second = 0.0;
    if (!(cfg_.hold_seconds >= 0.0)) cfg_.hold_seconds = 0.0;
    if (!(cfg_.ema_tau_seconds >= 0.0)) cfg_.ema_tau_seconds = 0.0;
    if (cfg_.smoothing_space != 0 && cfg_.smoothing_space != 1)
        cfg_.smoothing_space = 1;
    reset();
}

void PitchControlLayer::reset() {
    state_ = ControlState::kNoLock;
    tracked_ = 0.0;
    ema_ = 0.0;
    confirm_run_ = 0;
    candidate_ = 0.0;
    octave_run_ = 0;
    octave_cand_ = 0.0;
    hold_frames_left_ = 0;
    diag_ = ControlDiagnostics{};
}

long PitchControlLayer::holdFrames() const {
    return static_cast<long>(cfg_.hold_seconds * cfg_.sample_rate /
                             cfg_.cadence_samples);
}

double PitchControlLayer::clampSlew(double targetHz, double currentHz) const {
    if (cfg_.slew_cents_per_second <= 0.0) return targetHz;  // unlimited
    const double cadSec = cfg_.cadence_samples / cfg_.sample_rate;
    const double maxCents = cfg_.slew_cents_per_second * cadSec;
    if (maxCents <= 0.0) return targetHz;
    const double dc = centsBetween(targetHz, currentHz);
    if (std::abs(dc) <= maxCents) return targetHz;
    const double r = std::pow(2.0, (std::signbit(dc) ? -maxCents : maxCents) / 1200.0);
    return currentHz * r;
}

void PitchControlLayer::emaUpdate(double acceptedHz) {
    if (cfg_.ema_tau_seconds <= 0.0) {
        ema_ = acceptedHz;
        return;
    }
    const double cadSec = cfg_.cadence_samples / cfg_.sample_rate;
    const double alpha = cadSec / (cfg_.ema_tau_seconds + cadSec);
    if (cfg_.smoothing_space == 1) {
        // cents (log-frequency) space: EMA on log2(f)
        const double lg = std::log2(std::max(acceptedHz, kTiny));
        const double lgPrev = std::log2(std::max(ema_, kTiny));
        ema_ = std::pow(2.0, lgPrev + alpha * (lg - lgPrev));
    } else {
        ema_ += alpha * (acceptedHz - ema_);
    }
}

ControlDecision PitchControlLayer::push(const PitchEstimate& e) {
    ControlDecision d;
    d.state = state_;
    ++diag_.frames;

    // The shared loss-handling tail: called for invalid / low-confidence /
    // out-of-range frames. NEVER lets a bad frame overwrite the state.
    auto lossTail = [&]() -> ControlDecision {
        if (state_ == ControlState::kLocked || state_ == ControlState::kHold) {
            state_ = ControlState::kHold;
            if (hold_frames_left_ > 0) --hold_frames_left_;
            if (hold_frames_left_ <= 0) {
                state_ = ControlState::kLost;
                tracked_ = 0.0;
                d.reason = ControlReason::kRelease;
                ++diag_.releases;
            } else {
                d.reason = ControlReason::kHold;
                ++diag_.held;
            }
            d.state = state_;
            d.tracked_hz = tracked_;  // last held value (0 after release)
            d.update_history = false;
            d.confidence = 0.2;
            d.tracked_period =
                tracked_ > 0.0 ? cfg_.sample_rate / tracked_ : 0.0;
            return d;
        }
        d.state = state_;
        d.tracked_hz = 0.0;
        d.update_history = false;
        if (d.reason != ControlReason::kInvalid &&
            d.reason != ControlReason::kLowConf)
            d.reason = ControlReason::kNoLockYet;
        return d;
    };

    // ---- 1. numeric validity -----------------------------------------------
    if (!e.numeric_valid || !finiteHz(e.f0)) {
        d.reason = ControlReason::kInvalid;
        ++diag_.invalid;
        confirm_run_ = 0;
        octave_run_ = 0;
        return lossTail();
    }
    // ---- 2. confidence gate --------------------------------------------------
    if (e.periodicity > 0.0 && e.periodicity < cfg_.periodicity_gate) {
        d.reason = ControlReason::kLowConf;
        ++diag_.low_conf;
        confirm_run_ = 0;
        octave_run_ = 0;
        return lossTail();
    }
    // ---- 3. range validity ---------------------------------------------------
    if (e.f0 < cfg_.period_min_hz || e.f0 > cfg_.period_max_hz) {
        d.reason = ControlReason::kRangeReject;
        confirm_run_ = 0;
        octave_run_ = 0;
        return lossTail();
    }

    // ---- acquisition (NoLock / Lost / Locking) -------------------------------
    if (state_ == ControlState::kNoLock || state_ == ControlState::kLost ||
        state_ == ControlState::kLocking) {
        if (confirm_run_ == 0 ||
            std::abs(centsBetween(e.f0, candidate_)) <= cfg_.outlier_cents) {
            ++confirm_run_;
        } else {
            confirm_run_ = 1;  // restart at the new candidate
        }
        candidate_ = e.f0;
        if (confirm_run_ >= cfg_.lock_confirm_frames) {
            state_ = ControlState::kLocked;
            tracked_ = candidate_;
            ema_ = candidate_;
            hold_frames_left_ = holdFrames();
            d.reason = ControlReason::kOk;
            d.state = ControlState::kLocked;
            d.tracked_hz = tracked_;
            d.update_history = true;
            d.confidence = 1.0;
            d.tracked_period = cfg_.sample_rate / tracked_;
            ++diag_.accepted;
            diag_.tracked_hz = tracked_;
            diag_.state = state_;
            return d;
        }
        d.reason = ControlReason::kNoLockYet;
        d.state = ControlState::kLocking;
        d.tracked_hz = 0.0;
        d.update_history = false;
        return d;
    }

    // ---- Locked / Hold: consistency machinery --------------------------------
    const double dc = centsBetween(e.f0, tracked_);
    const bool nearOctave =
        std::abs(std::abs(dc) - 1200.0) <= cfg_.octave_tolerance_cents;
    const bool outlier = std::abs(dc) > cfg_.outlier_cents && !nearOctave;

    if (nearOctave) {
        // octave candidate: confirm across N consecutive frames
        if (octave_run_ == 0 ||
            std::abs(centsBetween(e.f0, octave_cand_)) <= cfg_.outlier_cents) {
            ++octave_run_;
        } else {
            octave_run_ = 1;
        }
        octave_cand_ = e.f0;
        if (octave_run_ >= cfg_.octave_confirm_frames) {
            tracked_ = clampSlew(e.f0, tracked_);
            emaUpdate(e.f0);
            octave_run_ = 0;
            state_ = ControlState::kLocked;
            hold_frames_left_ = holdFrames();
            d.reason = ControlReason::kOctaveAccept;
            d.state = ControlState::kLocked;
            d.tracked_hz = tracked_;
            d.update_history = true;
            d.confidence = 0.75;
            d.tracked_period = cfg_.sample_rate / tracked_;
            ++diag_.accepted;
            ++diag_.octave_jumps;
            return d;
        }
        d.reason = ControlReason::kOctavePending;
        d.state = state_;
        d.tracked_hz = tracked_;
        d.update_history = false;  // do not poison the engine history yet
        d.confidence = 0.5;
        d.tracked_period = cfg_.sample_rate / tracked_;
        return d;
    }

    if (outlier) {
        d.reason = ControlReason::kOutlier;
        ++diag_.outliers;
        confirm_run_ = 0;
        octave_run_ = 0;
        d.state = state_;
        d.tracked_hz = tracked_;
        d.update_history = false;
        d.confidence = 0.4;
        d.tracked_period = cfg_.sample_rate / tracked_;
        return d;
    }

    octave_run_ = 0;
    // accepted: slew-limited tracking (+ optional EMA smoothing)
    {
        const double slewed = clampSlew(e.f0, tracked_);
        d.reason = (slewed != e.f0) ? ControlReason::kSlewClamped
                                    : ControlReason::kOk;
        tracked_ = slewed;
        emaUpdate(e.f0);
        if (cfg_.ema_tau_seconds > 0.0 && ema_ > 0.0 &&
            std::abs(centsBetween(ema_, tracked_)) <= cfg_.outlier_cents) {
            tracked_ = clampSlew(ema_, tracked_);
        }
        state_ = ControlState::kLocked;
        hold_frames_left_ = holdFrames();
        d.state = ControlState::kLocked;
        d.tracked_hz = tracked_;
        d.update_history = true;
        d.confidence = 0.9;
        d.tracked_period = cfg_.sample_rate / tracked_;
        ++diag_.accepted;
        diag_.tracked_hz = tracked_;
        diag_.state = state_;
    }
    return d;
}

}  // namespace octavox
