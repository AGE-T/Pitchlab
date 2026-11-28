// ---------------------------------------------------------------------------
// OCTAVOX independent native DSP core -- R34 stereo width stage (16.4.0)
// ============================================================================
// The full design contract lives in include/octavox/stereo_width.hpp and the
// option analysis + derivations in OCTAVOX_R34_STEREO_WIDTH_RESEARCH.md.
//
// Implementation notes (mirroring the header contract exactly):
//   * process() runs in TWO passes over the block: (1) the feed-forward
//     correlation measurement (seed or per-sample one-pole), (2) the
//     slewed symmetric-matrix transform. Both passes are plain fixed-order
//     double arithmetic; the only non-basic operations are std::sqrt and
//     division (both IEEE-exact) -- no libm transcendentals, so the active
//     stage is bit-reproducible on any IEEE-754 double platform.
//   * The inert early-out is checked BEFORE any read: at width target 1
//     with the matrix settled at the exact identity, process() touches
//     nothing (the bit-neutral default; pinned by the r34 core gates and
//     the whole pre-R34 suite passing unchanged).
// ---------------------------------------------------------------------------

#include "octavox/stereo_width.hpp"

#include <cmath>

namespace octavox {

//==========================================================================
// prepare / reset / setters
//==========================================================================

void StereoWidthStage::prepare(double sampleRate) {
    if (!(sampleRate > 0.0) || std::isinf(sampleRate)) sampleRate = 48000.0;
    // Euler one-pole coefficient: lambda = 1 - 1/(tau*fs), clamped into
    // (0, 1). Pure arithmetic -- no exp() (the research record, section 4).
    const double nTau = kTauSeconds * sampleRate;
    double pole = 1.0 - 1.0 / (nTau >= 1.0 ? nTau : 1.0);
    if (!(pole > 0.0) || pole >= 1.0) pole = 0.999;  // defensive
    pole_ = pole;
    emaGain_ = 1.0 - pole;
    // Full-scale slew: the matrix-element span is 2.0 (a in [0,1] x b in
    // [-1,1] worst case); kRampSeconds covers that span => per-sample
    // |delta| bound = 2 / (rampSeconds * fs).
    double rampSamples = kRampSeconds * sampleRate;
    if (!(rampSamples >= 1.0)) rampSamples = 1.0;
    slew_ = 2.0 / rampSamples;
    reset();
}

void StereoWidthStage::reset() {
    // Parameters (width_/bypass_) persist (the core's reset() contract);
    // everything else returns to the freshly-prepared state.
    mLL_ = 0.0;
    mRR_ = 0.0;
    mLR_ = 0.0;
    seeded_ = false;
    rho_ = 0.0;
    a_ = 1.0;
    b_ = 0.0;
    aTgt_ = 1.0;
    bTgt_ = 0.0;
    k_ = 1.0;
}

void StereoWidthStage::setWidth(double w) {
    if (std::isnan(w) || std::isinf(w)) w = 1.0;  // defensive: identity
    if (w < 0.0) w = 0.0;
    if (w > 1.0) w = 1.0;
    width_ = w;
}

void StereoWidthStage::setBypass(bool on) { bypass_ = on; }

bool StereoWidthStage::inert() const {
    // The exact-identity check makes the bit-neutral guarantee structural:
    // the matrix only ever reaches (1,0) by an EXACT assignment (the slew
    // snaps onto the target when the remaining delta fits one step).
    return effectiveWidth() >= 1.0 && a_ == 1.0 && b_ == 0.0;
}

//==========================================================================
// process
//==========================================================================

void StereoWidthStage::process(float* const* ch, int channels, int n) {
    if (channels < 2 || n <= 0 || ch == nullptr || ch[0] == nullptr ||
        ch[1] == nullptr)
        return;
    if (inert()) return;  // the bit-neutral default: touch nothing

    float* cl = ch[0];
    float* cr = ch[1];
    const double w = effectiveWidth();

    // ---- pass 1: the feed-forward correlation measurement ---------------
    if (!seeded_) {
        // ACTIVATION SEED: this block's aggregate means (the compensation
        // is correct from the first post-activation sample; the seed value
        // carries this block's size -- the documented block-boundary
        // semantic, see the header).
        double sLL = 0.0, sRR = 0.0, sLR = 0.0;
        for (int i = 0; i < n; ++i) {
            const double l = cl[i], r = cr[i];
            sLL += l * l;
            sRR += r * r;
            sLR += l * r;
        }
        const double inv = 1.0 / static_cast<double>(n);
        mLL_ = sLL * inv;
        mRR_ = sRR * inv;
        mLR_ = sLR * inv;
        seeded_ = true;
    } else {
        // Exact per-sample one-pole on the three running means (block-size
        // independent after the seed block).
        const double g = emaGain_;
        for (int i = 0; i < n; ++i) {
            const double l = cl[i], r = cr[i];
            mLL_ += (l * l - mLL_) * g;
            mRR_ += (r * r - mRR_) * g;
            mLR_ += (l * r - mLR_) * g;
        }
    }

    // ---- the correlation + the compensation gain ------------------------
    // Freeze below the energy floor (silence): no 0/0, no NaN, no drift.
    const double e = mLL_ + mRR_;
    if (e >= kEnergyFloor) {
        double rho = (2.0 * mLR_) / e;
        if (rho > 1.0) rho = 1.0;
        if (rho < -1.0) rho = -1.0;
        rho_ = rho;
    }
    // THE LAW (mid-only-gain structure; see the header note + the research
    // record section 2): the matrix applies k to the MID path only and w to
    // the SIDE path only, so the output energy is
    //     E_out = k^2*E[M^2] + w^2*E[S^2]
    // and the unique energy-preserving mid gain is
    //     k(w,rho) = sqrt( (2 - w^2*(1-rho)) / (1+rho) ).
    // [R34 correction, caught by the r34_energy_uncorrelated gate at
    // w=0.5: the first derivation folded k into BOTH paths (the global-
    // gain structure, k = sqrt(2)/sqrt(1+rho+w^2(1-rho))) -- correct for
    // THAT structure, wrong for this one; the two agree only at w=0 and
    // w=1. The mid-only structure is kept deliberately: w stays the EXACT
    // side ratio (L'-R' = w*(L-R)) and k the pure mid compensation.]
    // For w <= 1 the law always yields k >= 1 (narrowing with
    // compensation never attenuates); the kMaxGain clamp is the honest
    // rho -> -1 bound (out-of-phase content has zero mid).
    double k = 1.0;
    if (w < 1.0) {
        const double denom = 1.0 + rho_;
        if (denom > 0.0) {
            // numerator >= 2 - w^2*2 > 0 for w < 1 (never negative)
            k = std::sqrt((2.0 - w * w * (1.0 - rho_)) / denom);
            if (!(k >= 1.0)) k = 1.0;       // defensive (cannot happen)
            if (k > kMaxGain) k = kMaxGain;
        } else {
            k = kMaxGain;  // rho_ == -1 exactly: the honest clamp
        }
    }
    k_ = k;

    // ---- the target matrix ----------------------------------------------
    // w >= 1 (or bypass): the EXACT identity -- no measurement-dependent
    // target (k(1,rho) is 1 only in theory; floating point could return
    // 1+-1ulp and the slew would never settle onto the exact identity).
    if (w >= 1.0) {
        aTgt_ = 1.0;
        bTgt_ = 0.0;
    } else {
        aTgt_ = (k + w) * 0.5;
        bTgt_ = (k - w) * 0.5;
    }

    // ---- pass 2: the slewed symmetric-matrix transform ------------------
    // Per-sample: (a,b) move toward (aTgt_,bTgt_) by at most slew_; the
    // snap is EXACT when the remaining delta fits one step (so the matrix
    // reaches (1,0) bit-exactly and the inert early-out re-arms).
    const double slew = slew_;
    for (int i = 0; i < n; ++i) {
        double da = aTgt_ - a_;
        if (da > slew) da = slew;
        else if (da < -slew) da = -slew;
        double db = bTgt_ - b_;
        if (db > slew) db = slew;
        else if (db < -slew) db = -slew;
        a_ += da;
        b_ += db;
        const double l = cl[i];
        const double r = cr[i];
        cl[i] = static_cast<float>(a_ * l + b_ * r);
        cr[i] = static_cast<float>(b_ * l + a_ * r);
    }

    // A settled deactivation re-arms the activation seed: the next
    // activation measures the CURRENT content, not a stale frozen rho
    // (the stage was inert -- possibly for a long time -- and its EMA
    // state is stale by definition).
    if (inert()) seeded_ = false;
}

}  // namespace octavox
