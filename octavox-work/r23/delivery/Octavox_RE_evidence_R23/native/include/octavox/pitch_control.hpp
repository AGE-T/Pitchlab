// PITCHLAB R23 -- Input Pitch/Period Tracking & Spacing-Control Gate (Gate 6).
// ============================================================================
// PROVENANCE: our own module (round 23). An INDEPENDENT, self-contained
// control layer that sits between a pitch detector's estimate stream and the
// engine's spacing-target path (the 8-entry period median + S_target commit
// audited in PITCHLAB_R23_CURRENT_ENGINE_YIN_AUDIT.md §3).
//
// ARCHITECTURE CONTRACT (R23 brief §6 + §16.2): this layer is an INPUT
// pitch/period TRACKING and SPACING-CONTROL GATE. It consumes detector
// estimates (f0 + a periodicity score) and produces, per cadence frame, the
// decision whether the estimate is trustworthy enough to update the engine's
// period history, plus a validated/slew-limited tracked period. It NEVER
// emits per-voice transposition ratios and never touches grain/delay state.
//
// Pure double-in/double-out: NO dependency on the DSP engine -- testable
// without instantiating the pitch shifter (the R23 brief's requirement).
//
// Implemented policies (each documented + measured in the Gate-6 battery):
//   * confidence gating        -- periodicity score vs a gate;
//   * invalid-frame handling   -- invalid/NaN frames never overwrite state;
//   * outlier rejection        -- |Δcents| vs the tracked value, with the
//                                 octave-jump case handled SEPARATELY;
//   * octave-jump detection    -- a genuine octave jump is accepted only
//                                 after N consecutive confirming frames
//                                 (a musical octave leap is not rejected
//                                 by a hard continuity rule);
//   * lock acquisition        -- N consistent frames;
//   * lock retention          -- hysteresis + slew-limited tracking;
//   * loss-of-lock policy     -- bounded HOLD (documented seconds), then
//                                 release: no indefinite freeze;
//   * recovery                -- fast re-lock after short dropouts;
//   * temporal smoothing      -- EMA in linear-Hz or cents (log) space,
//                                 switchable + measured both ways.
//
// Confidence semantics: the layer's `confidence` output is a documented
// heuristic SCORE in [0,1] (periodicity persistence), NOT a calibrated
// probability (the R23 brief §17.4/§23 rule).
// ============================================================================
#ifndef OCTAVOX_PITCH_CONTROL_HPP_INCLUDED
#define OCTAVOX_PITCH_CONTROL_HPP_INCLUDED

namespace octavox {

struct PitchControlConfig {
    double sample_rate = 48000.0;
    double cadence_samples = 64.0;    ///< estimate cadence (stream samples)

    double period_min_hz = 20.0;      ///< tracked range lower bound
    double period_max_hz = 1200.0;    ///< tracked range upper bound

    /// Confidence gate: an estimate enters tracking only when its
    /// periodicity score >= gate. Default 0.80 corresponds to
    /// cmndf_min <= 0.20 (the engine detector's threshold law).
    double periodicity_gate = 0.80;

    /// |Δcents| beyond the tracked value that triggers outlier handling
    /// (non-octave). The octave case (±1200c) is governed separately.
    double outlier_cents = 200.0;

    /// Octave-jump confirmation: an estimate near tracked*2 or /2 is a
    /// candidate jump; it takes this many CONSECUTIVE confirming frames to
    /// accept (single-frame octave flips are treated as outliers).
    int octave_confirm_frames = 3;
    double octave_tolerance_cents = 90.0;

    /// Lock acquisition: consecutive consistent frames required.
    int lock_confirm_frames = 3;

    /// Slew limit of the tracked value (cents/second). 0 = unlimited.
    double slew_cents_per_second = 900.0;

    /// Loss-of-lock policy: hold the last tracked period for this long
    /// (seconds) after confidence loss; then RELEASE (tracked_hz -> 0 in the
    /// decision, update_history false). 0 = release immediately.
    double hold_seconds = 0.100;

    /// Temporal smoothing EMA time constant (seconds). 0 = off.
    double ema_tau_seconds = 0.030;

    /// Smoothing space: 1 = cents/log-frequency, 0 = linear Hz.
    int smoothing_space = 1;
};

/// One detector estimate entering the layer.
struct PitchEstimate {
    long sample = 0;          ///< boundary stream position
    double f0 = 0.0;          ///< Hz; 0/<=0 = invalid estimate
    double periodicity = 1.0; ///< score in [0,1] (e.g. 1 - cmndf_min);
                              ///< <=0 = unknown (treated as passing the gate
                              ///< only if the gate <= 0)
    bool numeric_valid = true;///< the detector's numeric validity flag
};

/// Reason codes (diagnostics; never collapsed into the state).
enum class ControlReason {
    kOk = 0,             ///< estimate accepted into tracking
    kFirstFrame = 1,     ///< accepted as the initial tracked value
    kInvalid = 2,        ///< f0 <= 0 / non-finite / !numeric_valid
    kLowConf = 3,        ///< periodicity below the gate
    kRangeReject = 4,    ///< f0 outside [period_min, period_max]
    kOutlier = 5,        ///< deviation beyond outlier_cents (non-octave)
    kOctavePending = 6,  ///< octave candidate awaiting confirmation
    kOctaveAccept = 7,   ///< octave jump accepted after confirmation
    kHold = 8,           ///< holding the last tracked value (loss in progress)
    kRelease = 9,        ///< hold expired: tracked released this frame
    kNoLockYet = 10,     ///< acquiring: not enough confirming frames
    kSlewClamped = 11,   ///< accepted but the tracked value moved at the limit
};

enum class ControlState {
    kNoLock = 0,  ///< no trustworthy estimate yet
    kLocking,     ///< candidates seen, awaiting confirmation
    kLocked,      ///< tracking active
    kHold,        ///< confidence lost, holding the last value (bounded)
    kLost,        ///< hold expired; awaiting re-acquisition
};

/// The decision produced per estimate frame.
struct ControlDecision {
    ControlState state = ControlState::kNoLock;
    ControlReason reason = ControlReason::kNoLockYet;
    double tracked_hz = 0.0;        ///< validated tracked input F0 (0 = none)
    double tracked_period = 0.0;   ///< sample_rate / tracked_hz
    /// Whether THIS estimate should update the ENGINE's period history
    /// (the spacing-gate contract; the per-voice ratio path is untouched).
    bool update_history = false;
    /// Heuristic persistence score in [0,1] (NOT a calibrated probability).
    double confidence = 0.0;
};

/// Diagnostics snapshot.
struct ControlDiagnostics {
    long frames = 0;
    long accepted = 0;
    long outliers = 0;
    long octave_jumps = 0;
    long invalid = 0;
    long low_conf = 0;
    long held = 0;
    long releases = 0;
    double tracked_hz = 0.0;
    ControlState state = ControlState::kNoLock;
};

class PitchControlLayer {
public:
    void configure(const PitchControlConfig& cfg);
    void reset();

    /// Push one detector estimate; returns the decision for this frame.
    ControlDecision push(const PitchEstimate& e);

    ControlDiagnostics diagnostics() const { return diag_; }
    const PitchControlConfig& config() const { return cfg_; }

private:
    double clampSlew(double targetHz, double currentHz) const;
    void emaUpdate(double acceptedHz);
    long holdFrames() const;

    PitchControlConfig cfg_{};
    ControlState state_ = ControlState::kNoLock;
    double tracked_ = 0.0;      ///< current tracked Hz (0 = none)
    double ema_ = 0.0;          ///< smoothing state (in the smoothing space)
    int confirm_run_ = 0;      ///< consecutive confirming frames
    double candidate_ = 0.0;   ///< the candidate being confirmed
    int octave_run_ = 0;       ///< octave confirmation run
    double octave_cand_ = 0.0; ///< octave candidate Hz
    long hold_frames_left_ = 0;
    ControlDiagnostics diag_{};
};

}  // namespace octavox

#endif  // OCTAVOX_PITCH_CONTROL_HPP_INCLUDED
