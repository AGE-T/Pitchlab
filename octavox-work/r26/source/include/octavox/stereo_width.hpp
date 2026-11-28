// OCTAVOX independent native DSP core -- R34 stereo width stage (16.4.0)
// ============================================================================
// PROVENANCE / MANDATE: our own design (see OCTAVOX_R34_STEREO_WIDTH_RESEARCH.md
// -- the user-requested round-34 feature: an adjustable stereo image width at
// the END of the chain, collapsible all the way to mono, WITH the sound's
// energy maintained). No code from the original Eventide Octavox plug-in;
// the original binary was never executed; the original's reconstructed
// 38-parameter surface has no width control (this is a NEW, own-design
// parameter -- id 38 in the wrapper).
//
// THE LAW (full derivation in the research record, section 2):
//   orthonormal M/S:  M=(L+R)/sqrt(2), S=(L-R)/sqrt(2)  (energy-preserving)
//   width rescale:    S -> w*S, w in [0,1]  (1 = original, 0 = mono)
//   correlation:      rho = 2*E[LR]/(E[L^2]+E[R^2])  in [-1,+1]
//   output energy:    E_out = k^2*E[M^2] + w^2*E[S^2]  (mid-only gain!)
//   mid gain that preserves total energy (E_out = E[M^2]+E[S^2]):
//       k(w,rho) = sqrt( (2 - w^2*(1-rho)) / (1+rho) )
//   [R34 CORRECTION, caught by the r34_energy_uncorrelated gate at w=0.5:
//   the first derivation folded k into BOTH paths (the global-gain
//   structure, k = sqrt(2)/sqrt(1+rho+w^2(1-rho))) -- correct for THAT
//   structure, wrong for this one; they agree only at w=0 and w=1. The
//   mid-only structure is the deliberate choice: w stays the EXACT side
//   ratio and k the pure mid compensation.]
//   applied as the SYMMETRIC matrix (exact structural identities):
//       L' = a*L + b*R,  R' = b*L + a*R,  a=(k+w)/2, b=(k-w)/2
//       => L'-R' = w*(L-R)   (side scaling EXACT; w=0 collapses to mono
//                             BITWISE: L'==R')
//       => L'+R' = k*(L+R)   (mid scaling)
//
// DESIGN CONTRACT:
//   * BIT-NEUTRAL DEFAULT: at width == 1.0 (and no bypass transition in
//     flight) the stage is INERT -- process() returns without touching a
//     single byte (the same discipline as the R31 observers; the default
//     plugin output is bit-identical to the pre-R34 engine, gated by the
//     four r15 reference hashes and the whole 115-case pre-R34 suite).
//   * The correlation is measured FEED-FORWARD on the stage's INPUT (the
//     engine output), never on the stage's own output (no feedback loop).
//   * Measurement: three running means (E[L^2], E[R^2], E[LR]) with an
//     exact PER-SAMPLE one-pole Euler smoother (tau = kTauSeconds, the
//     standard audio-engineering coefficient lambda = 1 - 1/(tau*fs); NO
//     transcendental anywhere in the block path; sqrt/division are the
//     only non-basic ops and both are IEEE-exact) => the ACTIVE stage is
//     deterministic and reproducible bit-for-bit on any IEEE-754 platform.
//   * ACTIVATION SEED: the first block after (re)activation seeds the
//     smoother with THAT BLOCK's aggregate means, so the compensation is
//     correct from the first post-activation sample. The seed value
//     depends on the activation block's size -- a block-boundary semantic
//     of the same documented class as the engine's parameter
//     block-boundary law (r15acc_block_boundary_semantics). After the
//     seed block the smoother is an exact per-sample one-pole and the
//     state is block-size independent.
//   * The measured rho is applied with END-OF-BLOCK semantics (the block
//     is transformed with the coefficient computed from the state that
//     already includes this block's samples): the activation block is
//     compensated exactly; the coefficient is a 200 ms-smoothed statistic,
//     so the intra-block coefficient look-ahead is inaudible and
//     deterministic (the engine's control-rate detector commits estimates
//     with the same class of block-boundary semantics).
//   * (a,b) are SLEW-RATE LIMITED (kRampSeconds full-scale span 2.0), so
//     width drags, bypass toggles and activation are click-free (bounded
//     per-sample matrix motion => bounded output slope; the same class of
//     linear-ramp guarantee as the engine's engagement/gain ramps).
//   * kMaxGain CLAMP (+6.02 dB): rho -> -1 at w -> 0 diverges (the mid
//     signal is zero -- out-of-phase content cannot survive a mono
//     collapse at ANY finite gain; documented honestly, clamped, tested).
//   * k >= 1 always (for w <= 1): compensated narrowing never attenuates.
//   * BYPASS GATING: while the wrapper's bypass is engaged the stage
//     targets the EXACT identity matrix (bypass = the plugin does
//     nothing), slewing out/in click-free, and goes fully inert once
//     settled.
//   * channels < 2: structural no-op. channels >= 2: the FIRST TWO
//     channels (L/R); further channels pass through untouched
//     (documented; the wrapper exposes a stereo bus).
//   * RT-safety: process() and both setters are allocation-free,
//     lock-free, single-threaded audio-thread code (gated by the
//     allocation counter).
//   * Silence: below kEnergyFloor (per-sample mean-square sum) the
//     correlation measurement FREEZES (no 0/0, no NaN, no drift); the
//     matrix stays finite; digital silence in => exact silence out.
// ---------------------------------------------------------------------------
#ifndef OCTAVOX_STEREO_WIDTH_HPP
#define OCTAVOX_STEREO_WIDTH_HPP

namespace octavox {

//==========================================================================
// StereoWidthStage -- correlation-adaptive width with energy preservation
// (the full contract in the header note above).
//==========================================================================
class StereoWidthStage {
public:
    StereoWidthStage() = default;
    StereoWidthStage(const StereoWidthStage&) = delete;
    StereoWidthStage& operator=(const StereoWidthStage&) = delete;

    /// Compute the per-sample pole and slew rate for this sample rate and
    /// reset all state (identity matrix, cold measurement). A non-finite
    /// or non-positive rate falls back to 48000 (defensive; the wrapper
    /// always passes a prepared rate). Allocation-free.
    void prepare(double sampleRate);

    /// Back to the freshly-prepared state; the width/bypass TARGETS keep
    /// their values (the core's reset() contract: parameters persist).
    void reset();

    /// Target width w in [0,1] (clamped; NaN/inf defensively map to the
    /// identity 1.0). 1.0 = the original stereo image (the stage is/becomes
    /// inert). 0.0 = full mono collapse. Values above 1 clamp to 1 (the
    /// exposed surface is [0,1]; the law extends trivially but widening
    /// beyond the original image is deliberately not exposed this round).
    void setWidth(double w);

    /// Bypass gating (the wrapper's bypass parameter): while on, the target
    /// matrix is the EXACT identity; once settled the stage is inert.
    void setBypass(bool on);

    /// Apply the width law IN PLACE to the first two channels of `ch`
    /// (planar). channels < 2: exact no-op. Inert: exact no-op (returns
    /// without reading or writing anything -- the bit-neutral default).
    /// RT-safe: no allocation, no locks, no transcendentals.
    void process(float* const* ch, int channels, int n);

    // -- diagnostics (tests + reports only; no audio-path role) ----------
    /// The target width (last setWidth, post-clamp).
    double width() const { return width_; }
    /// The bypass gate state.
    bool bypass() const { return bypass_; }
    /// The smoothed correlation the compensation currently uses.
    double correlation() const { return rho_; }
    /// The last computed mid gain k (the compensation actually applied;
    /// constant per block apart from the slew of (a,b)).
    double gain() const { return k_; }
    /// True when the stage is fully inert (identity matrix, width target
    /// 1): process() will be a no-op.
    bool inert() const;

    // -- documented constants (see the header contract) -------------------
    /// Correlation smoother time constant (nominal, Euler one-pole).
    static constexpr double kTauSeconds = 0.200;
    /// Full-scale (matrix-element span 2.0) slew time for (a,b).
    static constexpr double kRampSeconds = 0.010;
    /// Compensation clamp: +6.02 dB (the rho->-1 honesty bound).
    static constexpr double kMaxGain = 2.0;
    /// Per-sample mean-square sum below which the measurement freezes.
    static constexpr double kEnergyFloor = 1.0e-12;

private:
    /// The effective width target (bypass forces the identity 1.0).
    double effectiveWidth() const { return bypass_ ? 1.0 : width_; }

    double width_ = 1.0;    ///< target width (post-clamp)
    bool bypass_ = false;   ///< bypass gate
    double pole_ = 0.0;     ///< per-sample EMA coefficient lambda
    double emaGain_ = 0.0;  ///< 1 - pole_ (the per-sample update gain)
    double slew_ = 0.0;     ///< per-sample |delta| bound on a and b
    // measurement state (per-sample running means)
    double mLL_ = 0.0, mRR_ = 0.0, mLR_ = 0.0;
    bool seeded_ = false;   ///< false until the first active block seeds
    double rho_ = 0.0;      ///< smoothed correlation in [-1,1]
    // matrix state
    double a_ = 1.0, b_ = 0.0;       ///< current matrix (per-sample slewed)
    double aTgt_ = 1.0, bTgt_ = 0.0; ///< target matrix for this block
    double k_ = 1.0;                 ///< last computed compensation gain
};

}  // namespace octavox

#endif  // OCTAVOX_STEREO_WIDTH_HPP
