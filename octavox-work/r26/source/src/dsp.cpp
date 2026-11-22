// OCTAVOX 16.0.0 -- independent native DSP core (implementation).
// ============================================================================
// Rebuilt R15-REB1 from the round-12 12.0.0 base against the salvaged
// NORMATIVE header (include/octavox/dsp.hpp -- its comment blocks are the
// contract). See this file's history in the worklog (R15-R0 / R15-P1 /
// R15-P2P5 / R15-REB1): the 15.0.0 sources were lost to two environment
// resets; this file re-implements them. With the DEFAULT parameter surface
// (wet = 1, bypass off, no per-voice overrides, all gains 1) the engine is
// BIT-IDENTICAL to 12.0.0 at 44.1/48 kHz -- gated by the four FNV-1a-64
// reference hashes in native/r15_reference_hashes.txt (captured by
// cli/r15ref_capture.cpp against the 12.0.0 core before this rebuild).
//
// R19 (16.0.0) deltas -- every one config-gated, legacy bit-exact (see the
// header's ROUND-19 CONTRACT):
//   * R19-a  decimated control-rate pitch detector (DetectorMode::Decimated,
//     the new default) with the FullRate reference mode retained VERBATIM;
//     estimate cadence / median / hysteresis / validity laws unchanged.
//   * R19-b  click-free spacing commits: a frozen SHADOW grain crossfaded
//     over commit_crossfade_seconds (0 = the legacy instant commit, which
//     stepped the read position by (S_new - S_old) at every wrap commit --
//     the dominant click source, classified and measured this round).
//   * R19-c  voice activation/deactivation ramps through the existing
//     per-voice gain ramp machinery (setVoices transitions were instant
//     amplitude steps in 15.0.0).
//   * R19-d  crossfaded per-voice base-delay changes over
//     delay_crossfade_seconds (0 = the legacy instant re-align).
//   * R19-e  read suppression when the output is exactly the dry signal and
//     feedback is 0 (bit-identical by construction; gated by an explicit
//     bit-identity test).
//   * R19-f  stage-attribution profiler (compile-time gated
//     OCTAVOX_STAGE_TIMERS; pure observer) + VoiceStats wrap/commit stream
//     timestamps for the click scanner.
//
// Deltas implemented here (12.0.0 -> 15.0.0):
//   * R13-a  cold-start alignment law: VERIFIED, not changed -- the 12.0.0
//     core already satisfies every clause (see the law restated below).
//   * R13-b  pitch-detector analysis window is SAMPLE-RATE RELATIVE:
//     win = max(2048, ceil(2.5*tau_max)) (implies win >= 2*tau_max + 128
//     with the 2048 floor, keeping the difference support
//     m = win/2 - tau_max >= tau_max/4 -- value-mirror lag immunity).
//     44.1/48 kHz behaviour is bit-identical to round 12 (win == 2048);
//     above ~49 kHz the window grows with the rate so the detector keeps
//     tracking (round 12 was blind, m < 32, above 59.5 kHz -- measured:
//     the r15ref_capture --probe96 run on the 12.0.0 core returns
//     estimate 0.000000 Hz for a steady 110 Hz sine at 96 kHz).
//   * R13-c  zero-allocation re-prepare: prepare() whose buffer-determining
//     fields (sample_rate, max_voices, f0_history, detection_interval)
//     match the live engine reconfigures IN PLACE (no allocation, no
//     locks, identical result to a fresh engine). Other prepare() calls
//     take the allocate-and-rebuild path. reset() is allocation-free.
//   * R14    VoiceStats::ratio exposes the current gliding ratio.
//   * R15-P1 per-voice parameter surface: per-voice ABSOLUTE ratio / gain /
//     mute / base-delay overrides, latency-aligned wet/dry mix and a
//     click-free bypass -- all audio-thread safe, allocation-free,
//     block-boundary applied (see the header's ROUND-15 contract for the
//     normative text; the implementation map is below).
//
// COLD-START ALIGNMENT LAW (R13-a; maintained as an invariant):
//   L = latencySamples() = base_delay + grain/2. Every voice starts at
//   grain phase frac = 0.5. A unity voice parks on d_park =
//   round(base + grain/2) and outputs the bit-exact delayed copy
//   out[k] == in[k - d_park + 1] (ring convention read(d) = the sample
//   pushed d-1 samples ago); with the default config d_park == L so the
//   first non-zero sample for an impulse at t=0 is exactly k = L-1, and
//   the output is exactly zero before the kernel can reach the input (the
//   rings are pre-zeroed = "silence before t = 0"). A voice with ratio
//   r != 1 initialises its gliding ratio AT its target (no cold-start
//   glide: v.ratio = v.ratio_t in reset()) and starts fully engaged
//   (e = 1, a = 0), so before the first grain wrap its read delay is
//   d(k) = L + (1 - r)*(k + 1) and output sample k interpolates the input
//   at time tau(k) = r*(k + 1) - L.
//
// ROUND-15 P1 IMPLEMENTATION MAP (where the contract lives in this file):
//   VoiceState        ratio_override / ratio_ov (setVoiceRatio),
//                     gain_t / gain / ramp_tgt / gain_rate (setVoiceGain,
//                     setVoiceMute; linear-in-amplitude ramp with exact
//                     snap, armed at the process() block start),
//                     mute, base_delay_ov / base_pending (setVoiceBaseDelay).
//   Impl              dry_ring + pushDry()/readDry() (raw-input ring; the
//                     dry tap reads delay latencySamples() through the
//                     read(d) = d-1 law -> dry[k] = in[k-(L-1)], bit-exact),
//                     wet (setWetDry, clamped [0,1]),
//                     bypass_on / bypass_b / b_step (setBypass blend),
//                     gain_ramp_n, dry_delay.
//   Impl::applyPendingSurface()   the process() block-start stage: pending
//                     base-delay application (voice-v-only re-align, parked
//                     voices re-park) + gain-ramp (re)arming.
//   Impl::slewGain()  per-sample gain ramp (0 rate = exact snap).
//   Impl::retargetVoice()  shared ratio re-target + unity-exit spacing
//                     commit (setRatio / setSpread / setVoiceRatio).
//   Impl::resetParamSurface()  prepare() = fresh-engine defaults;
//                     reset() preserves overrides and snaps ramps instead.
//   AlignedDelayLine::process()  per-voice gain multiplies AFTER the
//                     voice-count normalisation (gain==1 bit-neutral,
//                     gain==0 removes the voice exactly, sample not even
//                     added); feedback taps the PRE-MIX wet signal
//                     (wet-path-only -> wet state invariant under mix and
//                     bypass); the output stage special-cases
//                     wet == 1 && !bypass-blend as the BIT-EXACT legacy
//                     path (no dry read happens there).
//   MultichannelShifter  every new setter broadcasts to the per-channel
//                     engines; prepare() reuses the engine array when the
//                     channel count matches (per-engine in-place prepare).
//
// All code here is our own (provenance: see the header).

#include "octavox/dsp.hpp"

// R23 (Gate 6/8): the Input Pitch/Period Tracking & Spacing-Control Gate --
// an independent control layer between the detector and the spacing path.
// Default OFF (bit-exact legacy R22 behaviour); see Config::tracking_gate.
#include "octavox/pitch_control.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

#include "obs_kernel_table.h"  // OBS data table (see header comment)
#include "interp_tables.hpp"    // R26: shared interpolation tables + mode codes

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ===========================================================================
// R19-f: stage-attribution profiler (compile-time gated; pure observer).
// ===========================================================================
#if defined(__x86_64__) || defined(__i386) || defined(_M_X64) || defined(_M_IX86)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#define OCTAVOX_PROF_TICK_SOURCE "tsc"
#else
#define OCTAVOX_PROF_TICK_SOURCE "steady_clock"
#endif

namespace octavox {
namespace stageprof {

const char* stageName(int stage) {
    switch (stage) {
        case kDetBuffering: return "det_buffering";
        case kDetEstimate:  return "det_estimate";
        case kDetMedian:    return "det_median";
        case kVoiceLoop:    return "voice_loop";
        case kInterpReads:  return "interp_reads";
        case kOutputStage:  return "output_stage";
        case kIoInput:      return "io_input";
        default:            return "?";
    }
}

#if defined(OCTAVOX_STAGE_TIMERS)

bool active() { return true; }

namespace {

struct Cnt { uint64_t t = 0; uint64_t c = 0; };
Cnt g_cnt[kStageCount];

inline uint64_t profTick() {
#if defined(__x86_64__) || defined(__i386) || defined(_M_X64) || defined(_M_IX86)
    return __rdtsc();
#else
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

struct Scoped {
    int s;
    uint64_t t0;
    explicit Scoped(int s_) : s(s_), t0(profTick()) {}
    ~Scoped() {
        const uint64_t t1 = profTick();
        g_cnt[s].t += t1 - t0;
        ++g_cnt[s].c;
    }
};

}  // namespace

void reset() {
    for (int i = 0; i < kStageCount; ++i) g_cnt[i] = Cnt{};
}

uint64_t ticks(int stage) {
    return (stage >= 0 && stage < kStageCount) ? g_cnt[stage].t : 0;
}

uint64_t calls(int stage) {
    return (stage >= 0 && stage < kStageCount) ? g_cnt[stage].c : 0;
}

uint64_t timerOverheadTicks() {
    // Calibration: the minimum empty-scope cost over a warm loop (the
    // deterministic floor; the bench subtracts calls * overhead).
    static uint64_t ov = 0;
    if (ov == 0) {
        reset();
        uint64_t best = ~0ull;
        for (int r = 0; r < 64; ++r) {
            const uint64_t t0 = profTick();
            { Scoped s(kIoInput); }
            const uint64_t d = profTick() - t0;
            if (d < best) best = d;
        }
        reset();
        ov = best;
    }
    return ov;
}

// The scoped guard is visible to this translation unit only; the DSP code
// below brackets its stages with OCTAVOX_PROF(Var, Stage).
#define OCTAVOX_PROF(var, stage) ::octavox::stageprof::Scoped var(stage)

#else  // collection off: the call sites vanish at preprocessing time

bool active() { return false; }
void reset() {}
uint64_t ticks(int) { return 0; }
uint64_t calls(int) { return 0; }
uint64_t timerOverheadTicks() { return 0; }
#define OCTAVOX_PROF(var, stage) ((void)0)

#endif  // OCTAVOX_STAGE_TIMERS

}  // namespace stageprof
}  // namespace octavox

#if !defined(OCTAVOX_PROF)
#define OCTAVOX_PROF(var, stage) ((void)0)
#endif

namespace octavox {
namespace {

// ---------------------------------------------------------------------------
// Musical spread table (reference parity: poc/octavox_offline.py)
// ---------------------------------------------------------------------------
constexpr double kSpreadSemitones[8] = {0.0, 7.0, 4.0, 12.0, -5.0, -12.0, 3.0, -3.0};

// ---------------------------------------------------------------------------
// Interpolator tables -- R26: MOVED to src/simd_dispatch.cpp (interp::tables())
// so the SIMD read kernels read the SAME objects. The construction code was
// moved VERBATIM (identical values); the engine's readInterp below uses
// interp::tables() exactly as it used the file-local tables.
// ---------------------------------------------------------------------------
constexpr int kLag6Taps = 6;

// R26: the mode codes must match the Interpolation enum (static contract
// between dsp.hpp's enum and the kernel interface's plain ints).
static_assert(static_cast<int>(Interpolation::Linear) == interp::kLinear &&
                  static_cast<int>(Interpolation::Lagrange6) == interp::kLagrange6 &&
                  static_cast<int>(Interpolation::Lagrange6Q) == interp::kLagrange6Q &&
                  static_cast<int>(Interpolation::ObsKernel6) == interp::kObsKernel6,
              "interp mode codes must match the Interpolation enum");

// ---------------------------------------------------------------------------
// YIN-style pitch detector (port of poc/octavox_poc.py PitchDetector).
// Detector defaults fmin 60 / fmax 1200 Hz (the v2 engine's detector
// defaults); the spacing gate in the engine uses the Config fmin/fmax
// (55/1200) exactly as the Python does.
//
// R13-b: the analysis window is SAMPLE-RATE RELATIVE:
//     win = max(2048, ceil(2.5 * tau_max)),  tau_max = int(sr / 60)
// which implies win >= 2*tau_max + 128 whenever the 2048 floor applies
// (tau_max <= 819.2 <=> sr <= 49152 Hz) and keeps the difference support
//     m = win/2 - tau_max >= tau_max/4
// (value-mirror lag immunity) at every rate. At 44.1/48 kHz win == 2048
// and every arithmetic step is identical to the round-12 fixed-window
// detector (bit-identical estimates); above ~49 kHz the window grows with
// the rate so the detector keeps tracking (round 12 was blind, m < 32,
// above 59.5 kHz).
//
// R19-a: DetectorMode::Decimated -- the SAME estimator on a low-pass
// filtered, decimated view of the input (see dsp.hpp's DetectorMode for
// the contract). D = max(1, lround(sr/24000)) with a floor correction that
// keeps sr_dec >= 18000 where possible; the anti-alias filter is a
// 6th-order Butterworth (three cascaded TDF2 biquads, Q = 0.517638 /
// 0.707107 / 1.931852, fc = 0.35 * sr_dec); the window law is the
// TIME-EQUIVALENT win_dec = max(1024, ceil(2.5 * tau_dec)) at sr_dec
// (same ~42.7 ms span at every canonical rate, same m >= tau_max/4
// immunity); the estimate cadence is unchanged (every detection_interval
// STREAM samples -- the window receives 64/D decimated samples per fire at
// the default interval). The FullRate mode's code path is UNCHANGED
// VERBATIM (bit-identical to 15.0.0).
// ---------------------------------------------------------------------------
class PitchDetector {
public:
    // R21: f0_min (Hz) = the detector's lower validity gate / lag-domain
    // floor. Default 60.0 reproduces the R12-R20 fixed law bit-exactly
    // (int(rate/60.0) and the 60.0<=f0 gate are the same arithmetic).
    void configure(int sr, DetectorMode mode, double f0_min,
                   simd::Mode simd_mode) {
        mode_ = mode;
        sr_ = sr;
        // R24: resolve the kernel table ONCE here, at prepare() time --
        // never inside process(). The estimate paths below then perform
        // only an indirect call through the stored function pointers (no
        // CPUID, no table resolution, no allocation in the audio callback;
        // see include/octavox/simd.hpp's RT-safety contract). At
        // Mode::Scalar the table's kernels are the scalar reference
        // implementations, whose op sequence is IDENTICAL to the historical
        // inline loops they replace (sub, mul, sequential add in ascending
        // j) -- the engine output at the default configuration is therefore
        // BIT-EXACT against every recorded reference hash (gated by the
        // r15/r18/r19/r20 records and the full suite every round).
        kern_ = simd::kernels(simd::resolve(simd_mode));
        simd_level_ = kern_.level;
        f0_min_ = (f0_min >= 20.0 && f0_min <= 200.0) ? f0_min : 60.0;
        if (mode == DetectorMode::Decimated || mode == DetectorMode::Hybrid) {
            decim_ = decimationFor(sr);
            sr_dec_ = sr / decim_;  // (exact at the canonical rates)
            tau_min_ = std::max(2, static_cast<int>(sr_dec_ / 1200.0));
            const int tau_max = static_cast<int>(sr_dec_ / f0_min_);
            win_ = std::max(1024, static_cast<int>(std::ceil(2.5 * tau_max)));
            tau_max_ = std::min(win_ - 2, tau_max);
            // R19-a: warm the cumulative mean 18 lags below the search floor
            // (bounded by the absolute floor 2) -- see estimate().
            tau_lo_ = std::max(2, tau_min_ - 18);
            designAntiAlias(sr, sr_dec_);
            if (mode == DetectorMode::Hybrid) {
                // R20-P2: the FINE stage's own window -- the full-rate law
                // at the HOST rate (the FullRate detector's own window),
                // holding the UNFILTERED stream samples. Difference support
                // m_raw = win_raw/2 - tau_max_raw keeps the refinement
                // inside the window for every clamped neighborhood lag.
                tau_min_raw_ = std::max(2, static_cast<int>(sr / 1200.0));
                const int tau_max_raw = static_cast<int>(sr / f0_min_);
                win_raw_ = std::max(2048, static_cast<int>(std::ceil(2.5 * tau_max_raw)));
                tau_max_raw_ = std::min(win_raw_ - 2, tau_max_raw);
                m_raw_ = (win_raw_ / 2) - tau_max_raw_;
                if (m_raw_ < 32) m_raw_ = 0;  // degenerate (never at the
                                              // accepted rates; guards only)
                buf_raw_.assign(win_raw_, 0.0);
                // R24: the fine stage's kernel scratch. The clamped
                // neighborhood [i0, i1] spans at most 2W+1 = 2*decim_+5 lags
                // (W = decim_ + 2); the +3 margin is documented slack.
                // fine_nb_ holds the parabolic-refinement neighbourhood
                // (t0, best, t2) -- three consecutive lags, one kernel call.
                fine_d_.assign(2 * decim_ + 8, 0.0);
                fine_nb_.assign(3, 0.0);
            } else {
                win_raw_ = 0;
                tau_min_raw_ = 2;
                tau_max_raw_ = 2;
                m_raw_ = 0;
                buf_raw_.clear();
                fine_d_.clear();
                fine_nb_.clear();
            }
        } else {
            decim_ = 1;
            sr_dec_ = sr;
            tau_min_ = std::max(2, static_cast<int>(sr_ / 1200.0));
            const int tau_max = static_cast<int>(sr_ / f0_min_);
            win_ = std::max(2048, static_cast<int>(std::ceil(2.5 * tau_max)));
            tau_max_ = std::min(win_ - 2, tau_max);
            tau_lo_ = tau_min_;  // FullRate: the exact 15.00 range
            win_raw_ = 0;
            tau_min_raw_ = 2;
            tau_max_raw_ = 2;
            m_raw_ = 0;
            buf_raw_.clear();
            fine_d_.clear();
            fine_nb_.clear();
        }
        buf_.assign(win_, 0.0);
        d_.assign(std::max(0, tau_max_ - tau_lo_), 0.0);
        cmnd_.assign(d_.size(), 0.0);
        last_f0_ = 0.0;
        last_cmndf_min_ = 1.0;
    }

    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.0);
        if (!buf_raw_.empty()) std::fill(buf_raw_.begin(), buf_raw_.end(), 0.0);
        last_f0_ = 0.0;
        last_cmndf_min_ = 1.0;
        pos_ = 0;
        for (int s = 0; s < 3; ++s) {
            z_[s][0] = 0.0;
            z_[s][1] = 0.0;
        }
    }

    // Push a block into the sliding window (Python push_block semantics).
    // FullRate mode: the RAW samples. (VERBATIM 15.0.0 behaviour.)
    void pushBlock(const float* x, int n) {
        OCTAVOX_PROF(pftim, stageprof::kDetBuffering);
        if (n >= win_) {
            std::copy(x + n - win_, x + n, buf_.begin());
            return;
        }
        // roll left by n, append
        std::copy(buf_.begin() + n, buf_.end(), buf_.begin());
        for (int i = 0; i < n; ++i) buf_[win_ - n + i] = static_cast<double>(x[i]);
    }

    // R19-a: filter a block of RAW stream samples through the anti-alias
    // cascade, decimate by decim_ on the GLOBAL stream position grid, and
    // push the kept samples into the sliding window. Allocation-free (the
    // conversion scratch is a fixed member; window rolls happen per chunk).
    // R20-P2: the Hybrid mode ALSO feeds the UNFILTERED stream samples into
    // the fine-stage window (buf_raw_) -- one extra roll per call, the raw
    // window ends at the same stream sample as the decimated one, so the
    // coarse candidate lag maps 1:1 (x D) onto the raw window's frame.
    void pushDecimated(const float* x, int n) {
        OCTAVOX_PROF(pftim, stageprof::kDetBuffering);
        if (mode_ == DetectorMode::Hybrid && n > 0) {
            if (n >= win_raw_) {
                for (int i = 0; i < win_raw_; ++i)
                    buf_raw_[i] = static_cast<double>(x[n - win_raw_ + i]);
            } else {
                std::copy(buf_raw_.begin() + n, buf_raw_.end(), buf_raw_.begin());
                for (int i = 0; i < n; ++i)
                    buf_raw_[win_raw_ - n + i] = static_cast<double>(x[i]);
            }
        }
        if (decim_ <= 1) {  // no decimation: identical to the raw window feed
            pushBlockRaw(x, n);
            pos_ += n;
            return;
        }
        const long start = pos_;
        int k = 0;
        for (int i = 0; i < n; ++i) {
            double v = static_cast<double>(x[i]);
            for (int s = 0; s < 3; ++s) {
                const double y = bq_[s][0] * v + z_[s][0];
                z_[s][0] = bq_[s][1] * v - bq_[s][3] * y + z_[s][1];
                z_[s][1] = bq_[s][2] * v - bq_[s][4] * y;
                v = y;
            }
            if (((start + static_cast<long>(i)) % decim_) == 0) {
                scratch_[k++] = v;
                if (k == kScratch) {
                    rollAppend(scratch_, k);
                    k = 0;
                }
            }
        }
        if (k > 0) rollAppend(scratch_, k);
        pos_ += n;
    }

    double estimate() {
        OCTAVOX_PROF(petim, stageprof::kDetEstimate);
        if (mode_ == DetectorMode::Hybrid) return estimateHybrid();
        const std::vector<double>& x = buf_;
        // R24: the window peak via the resolved kernel table (the scalar
        // kernel is op-for-op the historical inline loop; max |x| is exact
        // and order-free at every level).
        double peak = kern_.peak(x.data(), win_);
        if (peak < 1e-6) {
            last_f0_ = 0.0;
            return 0.0;
        }
        if (tau_max_ <= tau_min_) {
            last_f0_ = 0.0;
            return 0.0;
        }
        // seg = second half of the window; m = len(seg) - tau_max
        const int seg0 = win_ / 2;
        const int m = (win_ - seg0) - tau_max_;
        if (m < 32) {
            last_f0_ = 0.0;
            return 0.0;
        }
        // R19-a: the difference function and its cumulative-mean
        // normalisation are computed over [tau_lo_, tau_max_). In the
        // FullRate mode tau_lo_ == tau_min_ -- the loops below then run the
        // EXACT 15.00 arithmetic (bit-identical reference). In the
        // Decimated mode tau_lo_ = max(2, tau_min_ - 18) WARMS the running
        // mean below the search floor: with the coarse lag grid a high f0
        // sits only a few lags above tau_min_, and an un-warmed normalisation
        // biased the refined minimum by 3.46 cents at 880 Hz (measured;
        // 48 kHz, D=2). The search itself still covers only
        // [tau_min_, tau_max_] in both modes.
        const int ntau = tau_max_ - tau_lo_;
        // R24: the difference function via the resolved kernel. The Scalar
        // kernel executes the EXACT op sequence of the inline loop it
        // replaces (sub, mul, sequential add in ascending j, per tau in
        // ascending i) -- bit-identical by construction; the AVX2 lag-axis
        // kernel preserves the same order per tau in the lanes (bit-ident
        // by construction, hard-gated by the R24 battery); AVX2+FMA fuses
        // the per-term rounding only (order preserved, bounded deviation).
        kern_.diff(x.data(), seg0, tau_lo_, ntau, m, d_.data());
        // cumulative mean normalisation
        double run = 0.0;
        for (int i = 0; i < ntau; ++i) {
            run += d_[i];
            cmnd_[i] = (run > 0.0) ? d_[i] * static_cast<double>(i + 1) / run : 1.0;
        }
        // first minimum below threshold (search floor = tau_min_)
        const int base = tau_min_ - tau_lo_;
        int idx = -1;
        const double thr = 0.20;
        for (int i = base + 1; i < ntau - 1; ++i) {
            if (cmnd_[i] < thr && cmnd_[i] <= cmnd_[i - 1] && cmnd_[i] <= cmnd_[i + 1]) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            // argmin (from the search floor)
            idx = base;
            for (int i = base + 1; i < ntau; ++i)
                if (cmnd_[i] < cmnd_[idx]) idx = i;
        }
        double tau = static_cast<double>(tau_lo_ + idx);
        // R23 telemetry: record the selected dip's CMNDF value (observer
        // only -- the estimate arithmetic below is untouched).
        last_cmndf_min_ = cmnd_[static_cast<size_t>(idx)];
        // parabolic refinement. FullRate: on cmnd_, VERBATIM 15.00. R19-a
        // Decimated: on the RAW difference function d_ -- the cumulative
        // mean's local slope skews the parabola vertex by up to ~1 cent on
        // the coarse grid (measured 1.11 cents at 880 Hz; on d_ it is
        // 0.14 cents). The threshold/search law itself still uses cmnd_.
        const bool refine_on_d = (mode_ == DetectorMode::Decimated);
        if (idx > 0 && idx < ntau - 1) {
            const double y0 = refine_on_d ? d_[idx - 1] : cmnd_[idx - 1];
            const double y1 = refine_on_d ? d_[idx] : cmnd_[idx];
            const double y2 = refine_on_d ? d_[idx + 1] : cmnd_[idx + 1];
            const double den = y0 - 2.0 * y1 + y2;
            if (std::abs(den) > 1e-15) tau += 0.5 * (y0 - y2) / den;
        }
        // R19-a: the decimated window's samples live at sr_dec_; the
        // FullRate mode has sr_dec_ == sr_ (identical arithmetic to 15.0.0).
        double f0 = static_cast<double>(sr_dec_) / tau;
        if (!(f0_min_ <= f0 && f0 <= 1200.0)) f0 = 0.0;
        last_f0_ = f0;
        return f0;
    }

    double lastF0() const { return last_f0_; }

    // R23 (Gate 8): side-channel telemetry -- the CMNDF value at the
    // selected minimum of the LAST estimate (1.0 = no dip / degenerate;
    // 0 = perfect periodicity). Pure observer: recorded at estimate time,
    // changes NO arithmetic (the estimate path is bit-exact vs R22).
    // Feeds the Pitch Control Layer's periodicity score (1 - cmndf_min).
    double lastCmndfMin() const { return last_cmndf_min_; }

    // R19 diagnostics: the effective control rate (sr_dec) and D.
    int decimation() const { return decim_; }
    int controlRate() const { return sr_dec_; }
    /// R21: the configured lower validity gate (Hz).
    double f0Min() const { return f0_min_; }
    /// R24: the SIMD level resolved at the LAST configure() (diagnostics;
    /// Level::Scalar whenever the mode resolved to the scalar kernels).
    simd::Level simdLevel() const { return simd_level_; }

private:
    // R20-P2 (Hybrid): the coarse-to-fine estimate. The COARSE stage below
    // duplicates the Decimated estimator's arithmetic VERBATIM (same peak
    // gate on the filtered window, same difference function over
    // [tau_lo_, tau_max_), same warmed cumulative mean, same threshold law
    // with the same fallback argmin, same d-based parabolic refine) -- it
    // must NOT share code with estimate() at the risk of instruction-level
    // drift: the FullRate and Decimated paths above stay byte-for-byte the
    // R19 code, bit-exact against their recorded reference hashes.
    double estimateHybrid() {
        // ---- stage 1: coarse (the decimated estimator) --------------------
        const std::vector<double>& x = buf_;
        // R24: peak via the resolved kernel (see estimate()).
        double peak = kern_.peak(x.data(), win_);
        if (peak < 1e-6 || tau_max_ <= tau_min_ || m_raw_ <= 0) {
            last_f0_ = 0.0;
            return 0.0;
        }
        const int seg0 = win_ / 2;
        const int m = (win_ - seg0) - tau_max_;
        if (m < 32) {
            last_f0_ = 0.0;
            return 0.0;
        }
        const int ntau = tau_max_ - tau_lo_;
        // R24: the coarse difference function via the resolved kernel (the
        // same call as estimate()'s -- identical arithmetic at Scalar/AVX2,
        // fused per-term rounding at AVX2+FMA).
        kern_.diff(x.data(), seg0, tau_lo_, ntau, m, d_.data());
        double run = 0.0;
        for (int i = 0; i < ntau; ++i) {
            run += d_[i];
            cmnd_[i] = (run > 0.0) ? d_[i] * static_cast<double>(i + 1) / run : 1.0;
        }
        const int base = tau_min_ - tau_lo_;
        int idx = -1;
        const double thr = 0.20;
        for (int i = base + 1; i < ntau - 1; ++i) {
            if (cmnd_[i] < thr && cmnd_[i] <= cmnd_[i - 1] && cmnd_[i] <= cmnd_[i + 1]) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            idx = base;
            for (int i = base + 1; i < ntau; ++i)
                if (cmnd_[i] < cmnd_[idx]) idx = i;
        }
        double tau_dec = static_cast<double>(tau_lo_ + idx);
        // R23 telemetry (Hybrid coarse stage -- the fine stage's own
        // selection is recorded after the refinement below).
        last_cmndf_min_ = cmnd_[static_cast<size_t>(idx)];
        {  // coarse parabolic refine on d_ (the Decimated law)
            if (idx > 0 && idx < ntau - 1) {
                const double y0 = d_[idx - 1];
                const double y1 = d_[idx];
                const double y2 = d_[idx + 1];
                const double den = y0 - 2.0 * y1 + y2;
                if (std::abs(den) > 1e-15) tau_dec += 0.5 * (y0 - y2) / den;
            }
        }
        // ---- stage 2: fine (full-rate refinement on the RAW window) -------
        // The candidate is first CLAMPED into the raw search domain (exactly
        // the law the Decimated estimator follows when its own argmin hits
        // a domain edge): a candidate below tau_min_raw_ searches at the
        // floor, one above tau_max_raw_-1 (see the bound note below) at the
        // ceiling -- so out-of-range signals (e.g. 55 Hz, below the 60 Hz
        // validity gate) yield the SAME clamped-edge estimate the Decimated
        // mode yields, instead of dropping to 0 (measured: the pre-clamp
        // version dropped 51-61% of estimates on the 55 Hz corpus while
        // Decimated held its edge-clamped ~60 Hz behaviour).
        double tau_c = tau_dec * static_cast<double>(decim_);
        if (tau_c < static_cast<double>(tau_min_raw_)) tau_c = static_cast<double>(tau_min_raw_);
        if (tau_c > static_cast<double>(tau_max_raw_ - 1))
            tau_c = static_cast<double>(tau_max_raw_ - 1);
        const int W = decim_ + 2;
        int i0 = static_cast<int>(std::ceil(tau_c - static_cast<double>(W)));
        int i1 = static_cast<int>(std::floor(tau_c + static_cast<double>(W)));
        if (i0 < tau_min_raw_) i0 = tau_min_raw_;
        // Upper bound ONE below tau_max_raw_: the parabolic refine reads
        // best+1 with the FULL m_raw difference support, whose last index
        // is seg0r + (best+1) + m_raw - 1; at best == tau_max_raw_ that is
        // exactly win_raw_ (one past the end -- caught by ASan at 48 kHz).
        // Clamping the search one below keeps every neighbour read inside
        // the window and costs only the topmost 1/799th of the lag domain
        // (the [60, 1200] Hz gate is applied afterwards anyway).
        if (i1 > tau_max_raw_ - 1) i1 = tau_max_raw_ - 1;
        if (i1 - i0 < 2) {  // degenerate neighborhood (cannot happen at the
            // accepted rates); keep the coarse-period mapping honest
            const double f0c = static_cast<double>(sr_) / tau_c;
            last_f0_ = (f0_min_ <= f0c && f0c <= 1200.0) ? f0c : 0.0;
            return last_f0_;
        }
        const int seg0r = win_raw_ / 2;
        const std::vector<double>& xr = buf_raw_;
        // R24: the fine-stage neighborhood difference values via ONE kernel
        // call (lag-axis kernel over [i0, i1]); the argmin scan below keeps
        // the historical tie law (first strictly-smaller value wins, i.e.
        // the LOWEST lag on ties). At Scalar/AVX2 every value is bit-ident-
        // ical to the inline loop it replaces; at AVX2+FMA the per-term
        // rounding is fused (order preserved).
        const int ntau_f = i1 - i0 + 1;
        kern_.diff(xr.data(), seg0r, i0, ntau_f, m_raw_, fine_d_.data());
        int best = i0;
        double bestD = fine_d_[0];
        for (int i = 1; i < ntau_f; ++i) {
            if (fine_d_[i] < bestD) {
                bestD = fine_d_[i];
                best = i0 + i;
            }
        }
        double tau_host = static_cast<double>(best);
        {  // full-resolution parabolic refine on the RAW difference values
            // (evaluates the two neighbours even if the argmin sits at a
            // neighborhood edge; both are inside [i0, i1] or the window's
            // clamped domain). The vertex correction is CLAMPED to +-1 lag:
            // on nearly-linear d sequences the raw vertex can fly several
            // lags past the sample grid (measured: at a domain-edge argmin
            // the unclamped correction pushed tau_host to 800+ and the
            // [60,1200] Hz gate then rejected the estimate -- the 55 Hz
            // corpus dropped 51-61% of its estimates for exactly this
            // reason before the clamp; the Decimated mode never exhibits
            // it because its own refinement is applied at an INTERIOR
            // minimum).
            const int t0 = best - 1, t2 = best + 1;
            if (t0 >= 1 && t2 < win_raw_ - 1) {
                // R24: the three parabolic-neighbourhood lags (t0, best,
                // t2) in ONE kernel call (tau0 = t0, ntau = 3:
                // fine_nb_[0] = d(t0), fine_nb_[1] = d(best),
                // fine_nb_[2] = d(t2)). The accumulators are independent in
                // the reference too (the historical loop interleaved them,
                // which cannot mix roundings) -- the values are bit-ident-
                // ical at Scalar/AVX2. NOTE: the neighbours are TWO lags
                // apart (t0 and t2), so the call must span THREE taus;
                // fine_nb_[1] redundantly recomputes bestD and is unused
                // (kept for the single-call layout; verified equal by the
                // R24 battery).
                kern_.diff(xr.data(), seg0r, t0, 3, m_raw_, fine_nb_.data());
                const double y0 = fine_nb_[0];
                const double y2 = fine_nb_[2];
                const double den = y0 - 2.0 * bestD + y2;
                if (std::abs(den) > 1e-15) {
                    double corr = 0.5 * (y0 - y2) / den;
                    if (corr > 1.0) corr = 1.0;
                    if (corr < -1.0) corr = -1.0;
                    tau_host += corr;
                }
            }
        }
        double f0 = static_cast<double>(sr_) / tau_host;
        if (!(f0_min_ <= f0 && f0 <= 1200.0)) f0 = 0.0;
        last_f0_ = f0;
        return f0;
    }
    static int decimationFor(int sr) {
        if (sr <= 24000) return 1;
        long d = std::lround(static_cast<double>(sr) / 24000.0);
        if (d < 1) d = 1;
        // keep the control rate >= 18000 where possible (odd host rates)
        while (d > 1 && sr / static_cast<int>(d) < 18000) --d;
        return static_cast<int>(d);
    }

    void designAntiAlias(int sr, int srDec) {
        if (decim_ <= 1) return;  // no decimation -> no filter
        const double fc = 0.35 * static_cast<double>(srDec);
        static const double kQ[3] = {0.5176380902050415,
                                     0.7071067811865476,
                                     1.9318516525781366};
        const double w0 = 2.0 * M_PI * fc / static_cast<double>(sr);
        const double cw = std::cos(w0), sw = std::sin(w0);
        for (int s = 0; s < 3; ++s) {
            const double alpha = sw / (2.0 * kQ[s]);
            const double a0 = 1.0 + alpha;
            bq_[s][0] = (1.0 - cw) * 0.5 / a0;   // b0
            bq_[s][1] = (1.0 - cw) / a0;         // b1
            bq_[s][2] = (1.0 - cw) * 0.5 / a0;   // b2
            bq_[s][3] = (-2.0 * cw) / a0;        // a1
            bq_[s][4] = (1.0 - alpha) / a0;      // a2
        }
    }

    // The raw window feed (FullRate path body; kept separate so the
    // decimated D==1 fallback shares it without recursion into profiling).
    void pushBlockRaw(const float* x, int n) {
        if (n >= win_) {
            std::copy(x + n - win_, x + n, buf_.begin());
            return;
        }
        std::copy(buf_.begin() + n, buf_.end(), buf_.begin());
        for (int i = 0; i < n; ++i) buf_[win_ - n + i] = static_cast<double>(x[i]);
    }

    // roll left by n, append doubles (window feed for kept samples)
    void rollAppend(const double* x, int n) {
        if (n >= win_) {
            std::copy(x + n - win_, x + n, buf_.begin());
            return;
        }
        std::copy(buf_.begin() + n, buf_.end(), buf_.begin());
        for (int i = 0; i < n; ++i) buf_[win_ - n + i] = x[i];
    }

    static constexpr int kScratch = 256;

    DetectorMode mode_ = DetectorMode::FullRate;
    int sr_ = 48000;
    double f0_min_ = 60.0;  // R21: lower validity gate (default = legacy)
    int sr_dec_ = 48000;   // R19-a: the window's own rate (== sr_ when D == 1)
    int decim_ = 1;
    int win_ = 2048;       // R13-b: sample-rate-relative window (>= 2048)
    int tau_min_ = 2, tau_max_ = 800;
    int tau_lo_ = 2;       // R19-a: computation floor (== tau_min_ when FullRate)
    std::vector<double> buf_, d_, cmnd_;
    // R24: the resolved SIMD kernel table (function pointers set ONCE at
    // configure() = prepare() time; every kernel is a pure function over
    // caller-owned buffers -- no allocation, no locks, no CPUID, see
    // include/octavox/simd.hpp) and the resolved level (diagnostics).
    simd::Kernels kern_{};
    simd::Level simd_level_ = simd::Level::Scalar;
    // R24: the Hybrid fine stage's kernel scratch (neighborhood difference
    // values + the two parabolic neighbours; sized in configure()).
    std::vector<double> fine_d_, fine_nb_;
    // R20-P2 (Hybrid): the fine-stage full-rate window + its lag law
    int win_raw_ = 0;
    int tau_min_raw_ = 2, tau_max_raw_ = 2;
    int m_raw_ = 0;
    std::vector<double> buf_raw_;
    double last_f0_ = 0.0;
    double last_cmndf_min_ = 1.0;  // R23 telemetry (observer)
    // R19-a: anti-alias cascade state (3 TDF2 biquads: b0,b1,b2,a1,a2 + z)
    double bq_[3][5] = {};
    double z_[3][2] = {};
    long pos_ = 0;                 // global stream position (decimation grid)
    double scratch_[kScratch] = {};  // kept-sample staging (allocation-free)
};

// ---------------------------------------------------------------------------
// Per-voice state
// ---------------------------------------------------------------------------
struct VoiceState {
    double ratio_t = 1.0;   // effective target ratio (override-aware)
    double ratio = 1.0;     // gliding ratio (initialised AT target: cold start)
    double base_delay = 256.0;
    double frac = 0.5;      // grain phase [0,1)
    double S = 4096.0;      // current tap spacing
    // unity engagement
    double e = 1.0;         // 1 = dual-tap, 0 = single-tap A
    double a = 0.0;         // parking blend: 1 = parked integer read
    int d_park = -1;        // parked integer delay
    // -- round-15 P1 per-voice parameter surface --------------------------------
    bool ratio_override = false;  // setVoiceRatio set an absolute target
    double ratio_ov = 1.0;        // the absolute target (when overridden)
    double gain_t = 1.0;          // user gain target (>= 0)
    double gain = 1.0;            // current ramped gain
    double ramp_tgt = 1.0;        // effective target the ramp runs toward
    double gain_rate = 0.0;       // linear amplitude step per sample (0 = snap)
    bool mute = false;            // effective gain target forced to 0
    double base_delay_ov = 0.0;   // base-delay override (<= 0 = none set)
    bool base_pending = false;    // override awaits the next process() start
    // -- R19 machinery ---------------------------------------------------------
    bool active = true;           // R19-c: voice in the active set (setVoices);
                                  // deactivation fades the gain to 0 first
    bool was_active = true;       // R19-c: activation flag at the last block
                                  // start (selects the fade vs gain ramp rate)
    bool commits_frozen = false;  // R19-b: true ONLY inside the commit-shadow
                                  // clone (its spacing never commits)
    double xf_c = 1.0;            // R19-b: commit crossfade weight (1 = off)
    double xf_step = 0.0;         // per-sample weight advance (0 = off)
    double bxf_c = 1.0;           // R19-d: base-delay crossfade weight (1=off)
    double bxf_step = 0.0;        // per-sample weight advance (0 = off)
    double base_old = 0.0;        // R19-d: the old base delay while bxf runs
    // diagnostics
    long wraps = 0;
    long commits = 0;
    long last_wrap_at = -1;       // R19-f: stream index of the latest wrap
    long last_commit_at = -1;     // R19-f: stream index of the latest commit
};

}  // namespace

// ---------------------------------------------------------------------------
// AlignedDelayLine::Impl
// ---------------------------------------------------------------------------
struct AlignedDelayLine::Impl {
    static constexpr int kRingBits = 18;
    static constexpr int kRingSize = 1 << kRingBits;  // 262144
    static constexpr int kRingMask = kRingSize - 1;

    std::vector<double> ring;        // age-indexed delay line
    int w = 0;                       // newest sample index
    std::vector<double> dry_ring;    // R15-P1: raw-input ring (dry tap)
    int w_dry = 0;                   // newest dry sample index
    PitchDetector det;
    // R23 (Gate 8): the spacing-gate control layer (active only when
    // cfg.tracking_gate; inert otherwise -- the legacy accept law runs).
    PitchControlLayer control;
    std::vector<VoiceState> vs;      // [max_voices]
    std::vector<VoiceState> shadow;  // R19-b: commit-shadow grains [max_voices]
    std::vector<double> f0_hist;     // circular, size f0_history
    std::vector<double> med_scratch; // preallocated median scratch (no alloc)
    std::vector<float> det_pending;  // control-rate accumulator (raw input)
    int det_n = 0;
    int f0_hist_n = 0, f0_hist_i = 0;
    bool have_estimate = false;
    double S_target = 4096.0;
    // R22-P1: the explicit detectable-vs-spacing-trackable decision for
    // the LAST committed estimate (set in updateSpacingTarget).
    bool last_spacing_accepted = false;
    double fb_state = 0.0;
    double norm = 1.0;               // voice-count normalisation (ramped)
    double norm_target = 1.0;
    double last_f0 = 0.0;
    double e_step = 0.0;             // engagement slew per sample
    Config cfg{};
    // -- round-15 P1 surface state ---------------------------------------------
    double wet = 1.0;                // wet/dry mix, clamped [0,1] (1 = legacy)
    bool bypass_on = false;          // bypass target
    double bypass_b = 0.0;           // bypass blend (0 = normal, 1 = bypassed)
    double b_step = 1.0;             // bypass blend step per sample
    double gain_ramp_n = 0.0;        // gain ramp length in samples (0 = snap)
    int dry_delay = 0;               // dry tap delay == latencySamples()
    // -- R19 state --------------------------------------------------------------
    int proc_hi = 1;                 // R19-c: processing prefix (active +
                                     // still-fading voices; == voices at rest)
    long stream_pos = 0;             // R19-f: samples since reset (timestamps)
    double xf_step = 0.0;            // R19-b: 1/(commit_crossfade_seconds*sr)
    double bxf_step = 0.0;           // R19-d: 1/(delay_crossfade_seconds*sr)
    double voice_fade_n = 0.0;       // R19-c: activation fade length (samples)
    bool decimated = false;          // R19-a: detector mode cached per block
    bool reads_suppressed = false;   // R19-e: per-sample read-skip flag
    // R26: the batched interpolated-read kernel (lane-per-read AVX2,
    // bit-exact by construction; nullptr at Scalar -> the verbatim inline
    // path below runs). Resolved ONCE at prepare() time -- never in
    // process() (the R24 RT-safety contract).
    simd::Read4Fn read4_ = nullptr;

    // -- ring helpers -------------------------------------------------------
    inline void push(double x) {
        ring[w] = x;
        w = (w - 1) & kRingMask;
    }

    inline double readRaw(int delay) const {
        return ring[(w + delay) & kRingMask];
    }

    // Raw-input ring: pushDry(in[k]) then readDry(L) returns in[k-(L-1)]
    // (the read(d) = sample d-1 ago law) -- the bit-exact latency-aligned
    // dry tap. The dry ring carries the RAW input (never feedback).
    inline void pushDry(double x) {
        dry_ring[w_dry] = x;
        w_dry = (w_dry - 1) & kRingMask;
    }

    inline double readDry(int delay) const {
        return dry_ring[(w_dry + delay) & kRingMask];
    }

    // Interpolated read at `delay` (>= 0). Exact-integer delays take the
    // direct sample path (bit-exact; used by the parked unity state).
    double readInterp(double delay) const {
        OCTAVOX_PROF(ritim, stageprof::kInterpReads);
        if (delay < 0.0) delay = 0.0;
        const double d0 = std::floor(delay);
        const double mu = delay - d0;
        const int i0 = static_cast<int>(d0);
        if (mu == 0.0) {
            return ring[(w + i0) & kRingMask];  // exact integer delay
        }
        switch (cfg.interpolation) {
            case Interpolation::Linear: {
                const double a = ring[(w + i0) & kRingMask];
                const double b = ring[(w + i0 + 1) & kRingMask];
                return a + (b - a) * mu;
            }
            case Interpolation::Lagrange6: {
                int ph = static_cast<int>(mu * 2048.0);  // floor, mu < 1
                if (ph > 2047) ph = 2047;
                const double* wv = interp::tables().lag2048[ph];
                double s = 0.0;
                for (int k = 0; k < 6; ++k)
                    s += wv[k] * ring[(w + i0 + k - 2) & kRingMask];
                return s;
            }
            case Interpolation::Lagrange6Q: {
                int ph = static_cast<int>(mu * 256.0);
                if (ph > 255) ph = 255;
                const double* wv = interp::tables().lag256[ph];
                double s = 0.0;
                for (int k = 0; k < 6; ++k)
                    s += wv[k] * ring[(w + i0 + k - 2) & kRingMask];
                return s;
            }
            case Interpolation::ObsKernel6: {
                // OBS data mode: floor-indexed 256 phases (target phase law).
                int ph = static_cast<int>(mu * 256.0);
                if (ph > 255) ph = 255;
                const float* wv = obs_kernel::kTable[ph];
                double s = 0.0;
                for (int k = 0; k < 6; ++k)
                    s += static_cast<double>(wv[k]) *
                         ring[(w + i0 + k - 2) & kRingMask];
                return s;
            }
        }
        return 0.0;
    }

    // -- spacing management (v2 parity) --------------------------------------
    // R22-P1 (Minimum Pitch signal path, level 4): the accept gate's
    // lower bound is the UNIFIED floor max(fmin, f0_min). BIT-EXACTNESS
    // vs the R12--R21 law (fmin alone): every nonzero committed estimate
    // already satisfies f0 >= f0_min (the detector zeroes anything
    // below its floor), so for fmin <= f0_min the gates accept exactly
    // the same estimates (lowering fmin below the detector floor has
    // always been dead configuration; the default fmin 55 < f0_min 60 is
    // unchanged). The unification makes the DETECTOR-dominance explicit:
    // the engine never locks spacing onto a period the detector refuses
    // to commit, and the surviving distinction (f0_min lowered below
    // fmin: tones in [f0_min, fmin) detectable but NOT spacing-trackable)
    // is recorded per estimate in last_spacing_accepted.
    void updateSpacingTarget(double f0) {
        OCTAVOX_PROF(mdtim, stageprof::kDetMedian);
        const double fmin_eff = std::max(cfg.fmin, cfg.f0_min);
        const bool accept =
            (f0 != 0.0 && fmin_eff <= f0 && f0 <= cfg.fmax);
        last_spacing_accepted = accept;
        if (accept) {
            f0_hist[f0_hist_i] = static_cast<double>(cfg.sample_rate) / f0;
            f0_hist_i = (f0_hist_i + 1) % static_cast<int>(f0_hist.size());
            if (f0_hist_n < static_cast<int>(f0_hist.size())) ++f0_hist_n;
            have_estimate = true;
        }
        if (f0_hist_n == 0) {
            S_target = cfg.grain;
            return;
        }
        // median of the history (np.median parity: even count averages the
        // middle two). Uses the preallocated scratch -- NO allocation here
        // (steady-state guarantee).
        med_scratch.assign(f0_hist.begin(), f0_hist.begin() + f0_hist_n);
        std::sort(med_scratch.begin(), med_scratch.end());
        const int n = static_cast<int>(med_scratch.size());
        double t_hat = (n % 2 == 1)
                           ? med_scratch[n / 2]
                           : 0.5 * (med_scratch[n / 2 - 1] + med_scratch[n / 2]);
        // Python round() = round-half-to-even = nearbyint with default env
        const double k = std::nearbyint(cfg.grain / t_hat);
        const double kk = std::max(1.0, k);
        const double S = kk * t_hat;
        S_target = std::min(std::max(S, 0.5 * cfg.grain), 1.5 * cfg.grain);
    }

    // Wrap-time commit (v2 parity, hysteresis gated). R19-b: when the
    // commit crossfade is enabled, the OLD grain law is snapshotted into a
    // frozen shadow (never committing) and the voice crossfades to the new
    // law over commit_crossfade_seconds (clamped to 40% of the wrap period
    // so crossfades can never overlap). `frac_oldlaw` is the OLD-law grain
    // phase after the wrap ((frac-1) mod 1 downshift / (frac+1) upshift).
    // With xf_step == 0 this is the VERBATIM 15.00 commit (bit-exact).
    inline void commitSpacing(VoiceState& v, int vi, double frac_oldlaw) {
        if (v.commits_frozen || !have_estimate) return;
        if (std::abs(S_target / v.S - 1.0) > cfg.s_hysteresis) {
            if (xf_step > 0.0 && !(v.e <= 0.0 && v.a >= 1.0)) {
                VoiceState& sh = shadow[vi];
                sh = v;                 // full clone at the OLD spacing
                sh.frac = frac_oldlaw;  // old-law grain restart
                sh.commits_frozen = true;
                sh.xf_c = 1.0;
                sh.xf_step = 0.0;
                v.xf_c = 0.0;
                // clamp: finish within 40% of the current wrap period
                const double per = v.S / std::max(std::abs(1.0 - v.ratio), 1e-9);
                const double n_cfg = 1.0 / xf_step;
                const double n_max = 0.4 * per;
                v.xf_step = (n_cfg > n_max && n_max >= 1.0)
                                ? 1.0 / std::floor(n_max)
                                : xf_step;
            }
            v.S = S_target;
            ++v.commits;
            v.last_commit_at = stream_pos;
        }
    }

    // Unity-exit commit: refresh S while B's weight is ~0, preserving the
    // read point exactly (frac_new = frac*S_old/S_new in [0,1)).
    void exitCommitSpacing(VoiceState& v) {
        if (!have_estimate) return;
        if (std::abs(S_target / v.S - 1.0) <= cfg.s_hysteresis) return;
        const double frac_new = v.frac * v.S / S_target;
        if (frac_new >= 0.0 && frac_new < 1.0) {
            v.S = S_target;
            v.frac = frac_new;
            ++v.commits;
        }
        // else: keep S; the next natural wrap will commit it (v2 rule)
    }

    // Park on the nearest integer delay inside [base, base+S).
    void parkVoice(VoiceState& v) {
        const double d = v.base_delay + v.frac * v.S;
        int d_int = static_cast<int>(std::floor(d + 0.5));
        const double lo = std::ceil(v.base_delay);
        const double hi = std::floor(v.base_delay + v.S) - 1.0;
        if (static_cast<double>(d_int) > hi) d_int = static_cast<int>(hi);
        if (static_cast<double>(d_int) < lo) d_int = static_cast<int>(lo);
        v.d_park = d_int;
    }

    // R19-d: the parked integer delay the voice WOULD park on for an
    // arbitrary base delay (the old-base park target during a base-delay
    // crossfade; used only while the crossfade runs).
    int parkDelayFor(const VoiceState& v, double base) const {
        const double d = base + v.frac * v.S;
        int d_int = static_cast<int>(std::floor(d + 0.5));
        const double lo = std::ceil(base);
        const double hi = std::floor(base + v.S) - 1.0;
        if (static_cast<double>(d_int) > hi) d_int = static_cast<int>(hi);
        if (static_cast<double>(d_int) < lo) d_int = static_cast<int>(lo);
        return d_int;
    }

    // -- per-sample voice processing -----------------------------------------
    // R19: the read law factored over the base delay (readVoiceAt) so the
    // base-delay crossfade can evaluate the SAME law at the old base; the
    // 15.00 arithmetic inside is unchanged (identical operations, identical
    // order -- bit-exact when no crossfade is running).
    inline double readVoiceAt(const VoiceState& v, double base, int d_park) const {
        const double e = v.e;
        const double a = v.a;

        if (e <= 0.0 && a >= 1.0) {
            // Fully parked: bit-exact single integer-tap read.
            return readRaw(d_park);
        }

        const double d = base + v.frac * v.S;
        const double A = readInterp(d);

        double dual;
        if (e >= 1.0) {
            const double B = readInterp(d + v.S);
            const double w = 0.5 - 0.5 * std::cos(M_PI * v.frac);
            dual = w * A + (1.0 - w) * B;
        } else if (e <= 0.0) {
            dual = A;  // single-tap A (wA_eff = 1, wB_eff = 0)
        } else {
            const double B = readInterp(d + v.S);
            const double w = 0.5 - 0.5 * std::cos(M_PI * v.frac);
            const double wA = e * w + (1.0 - e);
            const double wB = e * (1.0 - w);
            dual = wA * A + wB * B;
        }

        if (a > 0.0) {
            const double park = readInterp(static_cast<double>(d_park));
            return (1.0 - a) * dual + a * park;
        }
        return dual;
    }

    // R26: the per-sample voice STATE advance, extracted VERBATIM from
    // processVoiceSample (identical operations, identical order -- the R26
    // batched read path calls the same function). Reads never mutate voice
    // state, so state advance and read evaluation are freely reorderable
    // per voice; the reads themselves consume only the ring (static inside
    // one output sample: the ring is pushed in the io_input phase before
    // the voice loop) and the ALREADY-ADVANCED voice state below.
    inline void advanceVoice(VoiceState& v, int vi) {
        // ratio glide (geometric, reference parity)
        v.ratio = v.ratio_t + (v.ratio - v.ratio_t) * Config::kGlideMult;
        const double r = v.ratio;

        // grain phase ramp + wrap/commit (v2 parity; R19-f timestamps)
        double frac = v.frac + (1.0 - r) / v.S;
        if (r > 1.0) {
            if (frac < 0.0) {
                const double S_old = v.S;
                commitSpacing(v, vi, frac + 1.0);  // old-law phase after wrap
                frac = (frac + 1.0) * S_old / v.S;
                while (frac >= 1.0) frac -= 1.0;
                ++v.wraps;
                v.last_wrap_at = stream_pos;
            }
        } else {
            if (frac >= 1.0) {
                const double S_old = v.S;
                commitSpacing(v, vi, frac - 1.0);  // old-law phase after wrap
                frac = (frac - 1.0) * S_old / v.S;
                while (frac < 0.0) frac += 1.0;
                ++v.wraps;
                v.last_wrap_at = stream_pos;
            }
        }
        v.frac = frac;
    }

    // Returns this voice's output sample (R19: includes the commit-shadow
    // crossfade and the base-delay crossfade; both are inert when their
    // weights sit at 1, leaving the 15.00 path bit-exact).
    // R26: the state advance now lives in advanceVoice() (verbatim extract);
    // the read/combine arithmetic below is UNCHANGED VERBATIM.
    inline double processVoiceSample(VoiceState& v, int vi) {
        advanceVoice(v, vi);

        // R19-e: read suppression -- the voice state above has advanced
        // exactly as it would with reads (reads never mutate state), so
        // skipping them here is output- and state-identical whenever the
        // output stage is going to ignore the wet sample (wet == 0 or fully
        // bypassed) and the feedback path is off.
        double s = 0.0;
        if (!reads_suppressed) {
            s = readVoiceAt(v, v.base_delay, v.d_park);
            // R19-d: blend the old-base read law while the base-delay
            // crossfade runs (inert at bxf_c == 1).
            if (v.bxf_c < 1.0) {
                const int dpk_old = parkDelayFor(v, v.base_old);
                const double s_old = readVoiceAt(v, v.base_old, dpk_old);
                s = (1.0 - v.bxf_c) * s_old + v.bxf_c * s;
            }
        }
        if (v.bxf_c < 1.0) v.bxf_c = std::min(1.0, v.bxf_c + v.bxf_step);

        // R19-b: commit-shadow crossfade -- the frozen old-law grain runs
        // the same per-sample machinery (mirrored control surface) and the
        // blend runs shadow -> new law (inert at xf_c == 1).
        if (v.xf_c < 1.0) {
            VoiceState& sh = shadow[vi];
            // mirror the voice-level control surface (grain-law state is
            // the shadow's own; its spacing never commits)
            sh.ratio_t = v.ratio_t;
            sh.base_delay = v.base_delay;
            sh.base_old = v.base_old;
            sh.bxf_c = v.bxf_c;
            sh.bxf_step = v.bxf_step;
            slewEngagement(sh);
            const double s_sh = processVoiceSample(sh, vi);
            s = (1.0 - v.xf_c) * s_sh + v.xf_c * s;
            v.xf_c = std::min(1.0, v.xf_c + v.xf_step);
        }
        return s;
    }

    // -- R26: the batched voice path v2 (cross-voice request pool) ---------
    // Three phases per output sample (AVX2 / AVX2FMA levels):
    //   1. per voice: engagement/gain slews, state advance (advanceVoice,
    //      the verbatim extract), the commit-shadow mirror preparation and
    //      READ COLLECTION -- each voice's fractional interpolated reads
    //      (A/B at the primary base, Ao/Bo at the old base while a
    //      base-delay crossfade runs, plus the shadow's identical set while
    //      a commit crossfade runs) are appended to one shared request pool;
    //      integer-delay requests resolve immediately (the reference's
    //      mu == 0 fast path, a plain masked ring read);
    //   2. pool EXECUTION -- full 4-lane kernel batches (the short tail is
    //      padded by repeating a real request; surplus lanes discarded);
    //   3. per voice: the COMBINE (identical arithmetic to the verbatim
    //      readVoiceAt/processVoiceSample) and the accumulator update.
    //
    // BIT-EXACTNESS (the R26 proof, unchanged from v1):
    //   * the ring is only written in the io_input phase BEFORE the voice
    //     loop, so every read inside one output sample is PURE over an
    //     immutable ring -- deferring and REORDERING their execution
    //     changes no value (reads have no side effects);
    //   * per-voice state is private (VoiceState, shadow[vi]); the only
    //     cross-voice interaction is the accumulator, which is updated in
    //     the same ascending voice order with the same gain==0 branch
    //     semantics as the verbatim path;
    //   * the shadow mirror is prepared in phase 1 with the PREDICTED
    //     post-increment bxf_c (the value the scalar path copies AFTER its
    //     increment; identical deterministic arithmetic) -- every mirrored
    //     field is stable between the advance and the combine;
    //   * each pool lane performs the exact scalar readInterp op sequence
    //     for its delay (one mul + one add rounding per tap, ascending k).
    // Gated by the R26 battery: kernel bit-exactness, engine bit-identity
    // under control churn (parked/engaged/partial, both crossfades, all
    // four interpolation modes), long-run identity, and the re-prepare
    // level-switch fresh-engine equivalence.
    struct VoiceReads {
        int slot[4];    // pool index per logical read (A, B, Ao, Bo); -1 =
                        // integer-resolved at collect time (lval set)
        double lval[4]; // resolved values
    };

    // Phase 1: state advance + read collection for one voice (and its
    // shadow, when the commit crossfade is open).
    inline void collectVoiceBatched(VoiceState& v, int vi, VoiceReads& rd,
                                    VoiceReads& srd, double* pool, int& npool) {
        advanceVoice(v, vi);
        const bool parked = (v.e <= 0.0 && v.a >= 1.0);
        const bool want_old = (v.bxf_c < 1.0);
        const bool wantB = (v.e > 0.0);

        // ---- the primary set ------------------------------------------------
        for (int j = 0; j < 4; ++j) rd.slot[j] = -1;
        if (!reads_suppressed && !parked) {
            const double dA = v.base_delay + v.frac * v.S;
            const double dB = dA + v.S;
            const double dAo = v.base_old + v.frac * v.S;
            const double dBo = dAo + v.S;
            const double delays[4] = {dA, dB, dAo, dBo};
            const bool active[4] = {true, wantB, want_old, want_old && wantB};
            for (int L = 0; L < 4; ++L) {
                if (!active[L]) continue;
                double dd = delays[L];
                if (dd < 0.0) dd = 0.0;  // the readInterp clamp law
                const double dj = std::floor(dd);
                if (dd == dj) {
                    rd.lval[L] = readRaw(static_cast<int>(dj));
                } else {
                    rd.slot[L] = npool;
                    pool[npool++] = dd;
                }
            }
        }

        // ---- the shadow set (only while the commit crossfade is open) ------
        for (int j = 0; j < 4; ++j) srd.slot[j] = -1;
        if (v.xf_c < 1.0) {
            VoiceState& sh = shadow[vi];
            // the mirror, with the PREDICTED post-increment bxf_c (the
            // scalar path's mirror copies the value AFTER its increment)
            const double bxf_next = (v.bxf_c < 1.0)
                                        ? std::min(1.0, v.bxf_c + v.bxf_step)
                                        : v.bxf_c;
            sh.ratio_t = v.ratio_t;
            sh.base_delay = v.base_delay;
            sh.base_old = v.base_old;
            sh.bxf_c = bxf_next;
            sh.bxf_step = v.bxf_step;
            slewEngagement(sh);
            // the shadow's OWN state advance (the scalar path's recursive
            // processVoiceSample(sh) starts with the same advanceVoice);
            // its commitSpacing is a no-op (commits_frozen).
            advanceVoice(sh, vi);
            const bool sh_parked = (sh.e <= 0.0 && sh.a >= 1.0);
            const bool sh_want_old = (sh.bxf_c < 1.0);
            const bool sh_wantB = (sh.e > 0.0);
            if (!reads_suppressed && !sh_parked) {
                const double dA = sh.base_delay + sh.frac * sh.S;
                const double dB = dA + sh.S;
                const double dAo = sh.base_old + sh.frac * sh.S;
                const double dBo = dAo + sh.S;
                const double delays[4] = {dA, dB, dAo, dBo};
                const bool active[4] = {true, sh_wantB, sh_want_old,
                                        sh_want_old && sh_wantB};
                for (int L = 0; L < 4; ++L) {
                    if (!active[L]) continue;
                    double dd = delays[L];
                    if (dd < 0.0) dd = 0.0;
                    const double dj = std::floor(dd);
                    if (dd == dj) {
                        srd.lval[L] = readRaw(static_cast<int>(dj));
                    } else {
                        srd.slot[L] = npool;
                        pool[npool++] = dd;
                    }
                }
            }
        }
    }

    // Phase 2: execute the pool (full 4-lane batches; the tail padded by
    // repeating request 0 -- the surplus lanes' results are discarded).
    inline void executePool(double* pool, double* rres, int npool) {
        if (npool <= 0) return;
        const int mode = static_cast<int>(cfg.interpolation);
        {
            OCTAVOX_PROF(ritim, stageprof::kInterpReads);
            int b = 0;
            for (; b + 4 <= npool; b += 4)
                read4_(ring.data(), w, mode, pool + b, rres + b);
            const int rem = npool - b;
            if (rem > 0) {
                double pad[4];
                for (int i = 0; i < rem; ++i) pad[i] = pool[b + i];
                for (int i = rem; i < 4; ++i) pad[i] = pool[b];
                double tmp[4];
                read4_(ring.data(), w, mode, pad, tmp);
                for (int i = 0; i < rem; ++i) rres[b + i] = tmp[i];
            }
        }
    }

    // The readVoiceAt combine with resolved read values (identical
    // arithmetic to the verbatim path; called for the primary base and the
    // old base during a base-delay crossfade).
    inline double combineReads(const VoiceState& v, const VoiceReads& rd,
                               bool old_base, int d_park) const {
        const bool parked = (v.e <= 0.0 && v.a >= 1.0);
        if (parked) return readRaw(d_park);  // bit-exact integer fast path
        const int o = old_base ? 2 : 0;      // slot window: [A,B] or [Ao,Bo]
        const double A = rd.lval[o];
        double dual;
        if (v.e >= 1.0) {
            const double B = rd.lval[o + 1];
            const double wgt = 0.5 - 0.5 * std::cos(M_PI * v.frac);
            dual = wgt * A + (1.0 - wgt) * B;
        } else if (v.e <= 0.0) {
            dual = A;  // single-tap A
        } else {
            const double B = rd.lval[o + 1];
            const double wgt = 0.5 - 0.5 * std::cos(M_PI * v.frac);
            const double wA = v.e * wgt + (1.0 - v.e);
            const double wB = v.e * (1.0 - wgt);
            dual = wA * A + wB * B;
        }
        if (v.a > 0.0) {
            const double park = readRaw(d_park);  // integer park read
            return (1.0 - v.a) * dual + v.a * park;
        }
        return dual;
    }

    // Phase 3: the combine + crossfade state updates (identical arithmetic
    // and update order to processVoiceSample; the shadow's combine is the
    // SAME function -- the shadow's xf_c is permanently 1, so its xf
    // branch is closed and the recursion is depth 1 exactly as in the
    // scalar path).
    inline double combineVoiceBatched(VoiceState& v, int vi,
                                      const VoiceReads& rd,
                                      const VoiceReads& srd) {
        double s = 0.0;
        if (!reads_suppressed) {
            s = combineReads(v, rd, false, v.d_park);
            if (v.bxf_c < 1.0) {
                const int dpk_old = parkDelayFor(v, v.base_old);
                const double s_old = combineReads(v, rd, true, dpk_old);
                s = (1.0 - v.bxf_c) * s_old + v.bxf_c * s;
            }
        }
        if (v.bxf_c < 1.0) v.bxf_c = std::min(1.0, v.bxf_c + v.bxf_step);

        if (v.xf_c < 1.0) {
            VoiceState& sh = shadow[vi];
            const double s_sh = combineVoiceBatched(sh, vi, srd, srd);
            s = (1.0 - v.xf_c) * s_sh + v.xf_c * s;
            v.xf_c = std::min(1.0, v.xf_c + v.xf_step);
        }
        return s;
    }

    // -- engagement slew ------------------------------------------------------
    inline void slewEngagement(VoiceState& v) {
        const bool unity = (v.ratio_t == 1.0);  // bit-exact double compare
        const double e_target = unity ? 0.0 : 1.0;
        // 'a' parks only once e has fully reached 0
        const double a_target = (unity && v.e <= 0.0) ? 1.0 : 0.0;
        if (v.e < e_target) v.e = std::min(e_target, v.e + e_step);
        else if (v.e > e_target) v.e = std::max(e_target, v.e - e_step);
        if (v.a < a_target) {
            if (v.a <= 0.0) parkVoice(v);  // (re)compute the parked delay
            v.a = std::min(a_target, v.a + e_step);
        } else if (v.a > a_target) {
            v.a = std::max(a_target, v.a - e_step);
        }
    }

    // -- R15-P1: per-voice gain slew ------------------------------------------
    // Linear-in-amplitude ramp with EXACT snap: the step size was armed at
    // the block start (gain_rate = |target - start| / gain_ramp_n), so a
    // change of size D completes in exactly gain_ramp_seconds; the ramp
    // restarts from the current gain whenever the target changes (armed in
    // applyPendingSurface()). gain_rate <= 0 = snap (ramp length 0).
    inline void slewGain(VoiceState& v) {
        const double tgt = v.ramp_tgt;
        if (v.gain == tgt) return;
        if (!(v.gain_rate > 0.0)) {
            v.gain = tgt;  // exact snap
            return;
        }
        if (v.gain < tgt) v.gain = std::min(tgt, v.gain + v.gain_rate);
        else              v.gain = std::max(tgt, v.gain - v.gain_rate);
    }

    // -- R15-P1: ratio re-target (shared by the three re-deriving entries) ---
    // Handles the unity-exit spacing commit while tap B carries ~0 weight.
    inline void retargetVoice(VoiceState& v, double new_t) {
        const bool was_unity = (v.ratio_t == 1.0);
        v.ratio_t = new_t;
        if (was_unity && new_t != 1.0) {
            // Unity exit: refresh the spacing (read point preserved).
            exitCommitSpacing(v);
        }
    }

    // -- R15-P1: process() block-start stage ----------------------------------
    // (a) apply the pending per-voice base delays: ONLY voice v is
    //     re-aligned -- the fractional grain phase and tap spacing are
    //     preserved, so the read point translates by (new - old) base
    //     delay; a parked (or parking) voice re-parks on the nearest
    //     integer of its new range.
    //     R19-d: with delay_crossfade_seconds > 0 the translation is
    //     CROSSFADED (old-base read law blended into the new one) instead
    //     of stepped; the eventual state is the same.
    // (b) (re)arm the gain ramps for voices whose EFFECTIVE target
    //     (active && !mute ? user gain : 0) changed since the last block --
    //     the rate restarts from the current gain (|target - start| /
    //     ramp length). R19-c: the activation flag folds voice-count
    //     changes into the same ramp (0 = legacy instant transitions).
    void applyPendingSurface() {
        for (int i = 0; i < static_cast<int>(vs.size()); ++i) {
            VoiceState& v = vs[i];
            if (v.base_pending) {
                v.base_pending = false;
                const double nv = v.base_delay_ov;  // validated at set time
                if (bxf_step > 0.0 && nv != v.base_delay) {
                    v.base_old = v.base_delay;
                    v.base_delay = nv;
                    v.bxf_c = 0.0;
                    v.bxf_step = bxf_step;  // arm the voice-level rate
                    if (v.a > 0.0) parkVoice(v);  // park target: NEW base
                } else {
                    v.base_delay = nv;             // legacy instant re-align
                    if (v.a > 0.0) parkVoice(v);
                }
            }
            const double tgt = (v.active && !v.mute) ? v.gain_t : 0.0;
            if (tgt != v.ramp_tgt) {
                v.ramp_tgt = tgt;
                // R19-c: an ACTIVATION flip uses the voice-fade length (0 =
                // the legacy instant transition); gain/mute changes keep the
                // gain-ramp length exactly as before.
                const bool act_flip = (v.active != v.was_active);
                const double n = act_flip ? voice_fade_n : gain_ramp_n;
                v.gain_rate = (n > 0.0) ? std::abs(tgt - v.gain) / n : 0.0;
            }
            v.was_active = v.active;
        }
        // R19-c: shrink the processing prefix -- trailing voices that have
        // fully faded out (inactive AND gain exactly 0) leave the loop.
        while (proc_hi > 0) {
            const VoiceState& v = vs[proc_hi - 1];
            if (!v.active && v.gain == 0.0) --proc_hi;
            else break;
        }
    }

    // -- R15-P1: prepare() restores the fresh-engine surface defaults --------
    // (reset() deliberately does NOT call this: it preserves the overrides
    // and only snaps the ramps.) R19-c: fresh-engine gains respect the
    // ACTIVE set (inactive voices rest at gain exactly 0).
    void resetParamSurface() {
        wet = 1.0;
        bypass_on = false;
        bypass_b = 0.0;
        for (int i = 0; i < static_cast<int>(vs.size()); ++i) {
            VoiceState& v = vs[i];
            v.ratio_override = false;
            v.ratio_ov = 1.0;
            v.gain_t = 1.0;
            v.mute = false;
            v.base_delay_ov = 0.0;
            v.base_pending = false;
            v.xf_c = 1.0;
            v.xf_step = 0.0;
            v.bxf_c = 1.0;
            v.bxf_step = 0.0;
            const double tgt = (v.active && !v.mute) ? v.gain_t : 0.0;
            v.gain = tgt;
            v.ramp_tgt = tgt;
            v.gain_rate = 0.0;
        }
    }
};

// ---------------------------------------------------------------------------
// AlignedDelayLine
// ---------------------------------------------------------------------------
AlignedDelayLine::AlignedDelayLine() = default;
AlignedDelayLine::~AlignedDelayLine() { delete impl_; }

bool AlignedDelayLine::prepare(const Config& cfg) {
    // -- validation (NaN-safe forms; the new fields reject negatives exactly
    // like engagement_seconds, plus non-finite values) -----------------------
    auto fail = [this]() {
        delete impl_;
        impl_ = nullptr;
        ready_ = false;
        active_voices_ = 1;
        return false;
    };
    // sample_rate additionally bounded so the detector's int casts stay
    // defined (win <= ~2.5*sr/60 doubles; no real host exceeds 1 MHz).
    if (!(cfg.sample_rate > 0.0) || !std::isfinite(cfg.sample_rate) ||
        cfg.sample_rate > 1.0e6)
        return fail();
    if (cfg.max_voices < 1 || cfg.max_voices > 64) return fail();
    if (cfg.voices < 1 || cfg.voices > cfg.max_voices) return fail();
    if (!(cfg.grain > 0.0) || !std::isfinite(cfg.grain)) return fail();
    if (!(cfg.base_delay >= 8.0) || !std::isfinite(cfg.base_delay)) return fail();
    // Maximum read delay: B tap at d + S with d < base + 32*(mv-1) + S and
    // S <= 1.5*grain, plus 3 extra taps beyond the anchor -> require the whole
    // range to stay inside the 2^18 ring.
    if (!(cfg.base_delay + 32.0 * (cfg.max_voices - 1) + 3.0 * cfg.grain + 8.0 <
          static_cast<double>(Impl::kRingSize)))
        return fail();
    if (!(cfg.fmin > 0.0) || !(cfg.fmax > cfg.fmin)) return fail();
    // R21: the detector's lower validity gate. [20, 200] Hz covers the
    // low-note investigation candidates (32.7/41.2/55/60/80/110) with
    // margin; must stay below fmax so the lag domain is non-degenerate.
    if (!(cfg.f0_min >= 20.0) || !(cfg.f0_min <= 200.0) ||
        !std::isfinite(cfg.f0_min) || !(cfg.f0_min < cfg.fmax))
        return fail();
    if (!(cfg.s_hysteresis >= 0.0)) return fail();
    if (cfg.f0_history < 1 || cfg.f0_history > 64) return fail();
    if (cfg.detection_interval < 1) return fail();
    if (!(cfg.engagement_seconds >= 0.0)) return fail();
    if (!(cfg.gain_ramp_seconds >= 0.0) || !std::isfinite(cfg.gain_ramp_seconds))
        return fail();
    if (!(cfg.bypass_seconds >= 0.0) || !std::isfinite(cfg.bypass_seconds))
        return fail();
    if (!(cfg.feedback > -1.0 && cfg.feedback < 1.0)) return fail();
    // R19: the new time fields validate exactly like the existing ones
    // (negative or non-finite values are rejected); detector_mode must be a
    // defined enum value.
    if (!(cfg.commit_crossfade_seconds >= 0.0) ||
        !std::isfinite(cfg.commit_crossfade_seconds))
        return fail();
    if (!(cfg.delay_crossfade_seconds >= 0.0) ||
        !std::isfinite(cfg.delay_crossfade_seconds))
        return fail();
    if (!(cfg.voice_fade_seconds >= 0.0) ||
        !std::isfinite(cfg.voice_fade_seconds))
        return fail();
    {
        const int dm = static_cast<int>(cfg.detector_mode);
        if (dm != static_cast<int>(DetectorMode::FullRate) &&
            dm != static_cast<int>(DetectorMode::Decimated) &&
            dm != static_cast<int>(DetectorMode::Hybrid))
            return fail();
    }
    // R24: the SIMD mode. Enum validity first; then the RT-safety
    // contract's LOUD-FAILURE rule: an explicitly FORCED level (AVX2 /
    // AVX2FMA) that this processor or build cannot honour REJECTS the
    // configuration (prepare() returns false -- the engine never silently
    // runs a level the host did not ask for). Auto degrades silently by
    // design (it ASKS for "best available"); Scalar is always available.
    {
        const int sm = static_cast<int>(cfg.simd);
        if (sm != static_cast<int>(simd::Mode::Scalar) &&
            sm != static_cast<int>(simd::Mode::AVX2) &&
            sm != static_cast<int>(simd::Mode::AVX2FMA) &&
            sm != static_cast<int>(simd::Mode::Auto))
            return fail();
        if (cfg.simd == simd::Mode::AVX2 &&
            simd::resolve(cfg.simd) != simd::Level::AVX2)
            return fail();
        if (cfg.simd == simd::Mode::AVX2FMA &&
            simd::resolve(cfg.simd) != simd::Level::AVX2FMA)
            return fail();
    }

    // -- R13-c in-place re-prepare: matching buffer-determining fields reuse
    // the existing allocations (zero allocations, zero locks, identical
    // result to a fresh engine; param-only fields are picked up here).
    // R19-a: detector_mode joins the buffer-determining set (the detector's
    // window/lag buffers differ between modes).
    if (impl_ && ready_ &&
        impl_->cfg.sample_rate == cfg.sample_rate &&
        impl_->cfg.max_voices == cfg.max_voices &&
        impl_->cfg.f0_history == cfg.f0_history &&
        impl_->cfg.detection_interval == cfg.detection_interval &&
        impl_->cfg.detector_mode == cfg.detector_mode &&
        impl_->cfg.f0_min == cfg.f0_min) {
        Impl& m = *impl_;
        m.cfg = cfg;
        m.e_step = cfg.engagement_seconds > 0.0
                       ? 1.0 / (cfg.engagement_seconds * cfg.sample_rate)
                       : 1.0;
        m.gain_ramp_n = cfg.gain_ramp_seconds * cfg.sample_rate;
        m.b_step = cfg.bypass_seconds > 0.0
                       ? 1.0 / (cfg.bypass_seconds * cfg.sample_rate)
                       : 1.0;
        m.dry_delay = static_cast<int>(cfg.base_delay + cfg.grain * 0.5);
        m.xf_step = cfg.commit_crossfade_seconds > 0.0
                        ? 1.0 / (cfg.commit_crossfade_seconds * cfg.sample_rate)
                        : 0.0;
        m.bxf_step = cfg.delay_crossfade_seconds > 0.0
                         ? 1.0 / (cfg.delay_crossfade_seconds * cfg.sample_rate)
                         : 0.0;
        m.voice_fade_n = cfg.voice_fade_seconds * cfg.sample_rate;
        m.decimated = (cfg.detector_mode != DetectorMode::FullRate);
        // (R20-P2: Hybrid rides the SAME filtered+decimated push path as
        // Decimated -- pushDecimated additionally feeds its fine-stage raw
        // window; FullRate alone uses the raw pushBlock path.)
        for (int i = 0; i < cfg.max_voices; ++i)
            m.vs[i].active = (i < cfg.voices);
        m.proc_hi = cfg.voices;
        m.det.configure(static_cast<int>(cfg.sample_rate),
                        cfg.detector_mode, cfg.f0_min, cfg.simd);  // R24: kernel table re-resolved at prepare() time; same sizes -> no realloc
        // R26: read kernel table re-resolved on the re-prepare path too
        m.read4_ = simd::readKernels(simd::resolve(cfg.simd)).read4;
        // R23: (re)configure the control layer for this engine config
        {
            PitchControlConfig pc;
            pc.sample_rate = cfg.sample_rate;
            pc.cadence_samples = static_cast<double>(cfg.detection_interval);
            pc.period_min_hz = cfg.f0_min;
            pc.period_max_hz = cfg.fmax;
            m.control.configure(pc);
        }
        m.resetParamSurface();  // prepare = fresh-engine defaults
        cfg_ = cfg;
        active_voices_ = cfg.voices;
        ready_ = true;
        reset();  // allocation-free
        return true;
    }

    // -- allocate-and-rebuild path (mismatched buffer-determining fields) ----
    delete impl_;
    impl_ = nullptr;
    ready_ = false;
    try {
        impl_ = new Impl();
        impl_->cfg = cfg;
        impl_->ring.assign(Impl::kRingSize, 0.0);
        impl_->dry_ring.assign(Impl::kRingSize, 0.0);
        impl_->vs.resize(cfg.max_voices);
        impl_->shadow.resize(cfg.max_voices);  // R19-b: commit-shadow grains
        for (int i = 0; i < cfg.max_voices; ++i)
            impl_->vs[i].active = (i < cfg.voices);
        impl_->proc_hi = cfg.voices;
        impl_->f0_hist.assign(cfg.f0_history, 0.0);
        impl_->med_scratch.assign(cfg.f0_history, 0.0);
        impl_->det_pending.assign(cfg.detection_interval, 0.0f);
        impl_->det_n = 0;
        impl_->det.configure(static_cast<int>(cfg.sample_rate),
                             cfg.detector_mode, cfg.f0_min, cfg.simd);
        // R26: resolve the read kernel at prepare() time on the fresh path
        impl_->read4_ = simd::readKernels(simd::resolve(cfg.simd)).read4;
        // R23: configure the control layer on the fresh-prepare path too
        {
            PitchControlConfig pc;
            pc.sample_rate = cfg.sample_rate;
            pc.cadence_samples = static_cast<double>(cfg.detection_interval);
            pc.period_min_hz = cfg.f0_min;
            pc.period_max_hz = cfg.fmax;
            impl_->control.configure(pc);
        }
        impl_->e_step = cfg.engagement_seconds > 0.0
                            ? 1.0 / (cfg.engagement_seconds * cfg.sample_rate)
                            : 1.0;
        impl_->gain_ramp_n = cfg.gain_ramp_seconds * cfg.sample_rate;
        impl_->b_step = cfg.bypass_seconds > 0.0
                            ? 1.0 / (cfg.bypass_seconds * cfg.sample_rate)
                            : 1.0;
        impl_->dry_delay = static_cast<int>(cfg.base_delay + cfg.grain * 0.5);
        impl_->xf_step = cfg.commit_crossfade_seconds > 0.0
                             ? 1.0 / (cfg.commit_crossfade_seconds * cfg.sample_rate)
                             : 0.0;
        impl_->bxf_step = cfg.delay_crossfade_seconds > 0.0
                              ? 1.0 / (cfg.delay_crossfade_seconds * cfg.sample_rate)
                              : 0.0;
        impl_->voice_fade_n = cfg.voice_fade_seconds * cfg.sample_rate;
        impl_->decimated = (cfg.detector_mode != DetectorMode::FullRate);
    } catch (...) {
        delete impl_;
        impl_ = nullptr;
        return false;  // allocation failure: engine unusable, no exception
    }
    cfg_ = cfg;
    active_voices_ = cfg.voices;
    ready_ = true;
    impl_->resetParamSurface();
    reset();
    return true;
}

void AlignedDelayLine::reset() {
    if (!impl_) return;
    Impl& m = *impl_;
    std::fill(m.ring.begin(), m.ring.end(), 0.0);
    std::fill(m.dry_ring.begin(), m.dry_ring.end(), 0.0);
    m.w = 0;
    m.w_dry = 0;
    m.det.reset();
    const double st = (m.cfg.ratio > 0.0) ? 12.0 * std::log2(m.cfg.ratio) : 0.0;
    for (int i = 0; i < m.cfg.max_voices; ++i) {
        VoiceState& v = m.vs[i];
        // Per-voice ratio target: a live override SURVIVES reset() (header
        // law); otherwise the target is re-derived from (ratio, spread).
        if (v.ratio_override && v.ratio_ov > 0.0) {
            v.ratio_t = v.ratio_ov;
        } else {
            const double off = (m.cfg.spread && i < 8) ? kSpreadSemitones[i] : 0.0;
            v.ratio_t = std::pow(2.0, (st + off) / 12.0);
        }
        v.ratio = v.ratio_t;  // cold-start law: gliding ratio AT its target
        // Base delay: a live override (incl. a pending one) SURVIVES reset().
        if (v.base_delay_ov > 0.0) {
            v.base_delay = v.base_delay_ov;
        } else {
            v.base_delay = m.cfg.base_delay + 32.0 * i;
        }
        v.base_pending = false;  // folded in above
        v.frac = 0.5;            // cold-start law: grain phase 0.5
        v.S = m.cfg.grain;
        v.wraps = 0;
        v.commits = 0;
        v.last_wrap_at = -1;     // R19-f: fresh diagnostics
        v.last_commit_at = -1;
        v.xf_c = 1.0;            // R19: no crossfades on a fresh engine
        v.xf_step = 0.0;
        v.bxf_c = 1.0;
        v.bxf_step = 0.0;
        // Unity engagement: start parked if this voice is at exact unity.
        if (v.ratio_t == 1.0) {
            v.e = 0.0;
            v.a = 1.0;
            m.parkVoice(v);
        } else {
            v.e = 1.0;  // cold-start law: shifted starts fully engaged
            v.a = 0.0;
            v.d_park = -1;
        }
        // Gain surface: preserved, RAMP SNAPPED to the effective target
        // (active && !mute ? user gain : 0) -- reset() is allocation-free.
        const double tgt = (v.active && !v.mute) ? v.gain_t : 0.0;
        v.gain = tgt;
        v.ramp_tgt = tgt;
        v.gain_rate = 0.0;
    }
    std::fill(m.f0_hist.begin(), m.f0_hist.end(), 0.0);
    m.f0_hist_n = 0;
    m.f0_hist_i = 0;
    m.have_estimate = false;
    m.last_spacing_accepted = false;  // R22-P1 diagnostics state
    m.S_target = m.cfg.grain;
    m.fb_state = 0.0;
    m.norm = 1.0 / active_voices_;
    m.norm_target = m.norm;
    m.det_n = 0;
    m.last_f0 = 0.0;
    m.bypass_b = m.bypass_on ? 1.0 : 0.0;  // bypass blend snapped (no ramp)
    m.stream_pos = 0;                     // R19-f: timestamp epoch
    m.proc_hi = active_voices_;           // R19-c: no fading leftovers
}

void AlignedDelayLine::setRatio(double ratio) {
    if (!impl_ || !(ratio > 0.0)) return;
    Impl& m = *impl_;
    const double st = 12.0 * std::log2(ratio);
    for (int i = 0; i < m.cfg.max_voices; ++i) {
        VoiceState& v = m.vs[i];
        v.ratio_override = false;  // re-derivation clears every override
        const double off = (m.cfg.spread && i < 8) ? kSpreadSemitones[i] : 0.0;
        m.retargetVoice(v, std::pow(2.0, (st + off) / 12.0));
    }
    m.cfg.ratio = ratio;
    cfg_.ratio = ratio;
}

void AlignedDelayLine::setVoices(int n) {
    if (!impl_) return;
    if (n < 1 || n > impl_->cfg.max_voices) return;
    active_voices_ = n;
    impl_->norm_target = 1.0 / n;
    // R19-c: activation/deactivation rides the per-voice gain ramps (armed
    // at the next block start); the processing prefix grows immediately so
    // the newly activated voices keep their state warm.
    for (int i = 0; i < impl_->cfg.max_voices; ++i)
        impl_->vs[i].active = (i < n);
    if (n > impl_->proc_hi) impl_->proc_hi = n;
}

void AlignedDelayLine::setSpread(bool on) {
    if (!impl_) return;
    Impl& m = *impl_;
    m.cfg.spread = on;
    cfg_.spread = on;
    // Re-derive voice targets from the current ratio (audio-thread update);
    // like setRatio(), this CLEARS every per-voice ratio override.
    const double st = 12.0 * std::log2(m.cfg.ratio > 0.0 ? m.cfg.ratio : 1.0);
    for (int i = 0; i < m.cfg.max_voices; ++i) {
        VoiceState& v = m.vs[i];
        v.ratio_override = false;  // re-derivation clears every override
        const double off = (on && i < 8) ? kSpreadSemitones[i] : 0.0;
        m.retargetVoice(v, std::pow(2.0, (st + off) / 12.0));
    }
}

void AlignedDelayLine::setFeedback(double fb) {
    if (!impl_) return;
    impl_->cfg.feedback = std::min(0.999999, std::max(-0.999999, fb));
    cfg_.feedback = impl_->cfg.feedback;
}

// -- round-15 P1 parameter surface -------------------------------------------
void AlignedDelayLine::setVoiceRatio(int v_i, double ratio) {
    if (!impl_ || v_i < 0 || v_i >= impl_->cfg.max_voices) return;
    if (!std::isfinite(ratio) || !(ratio > 0.0)) return;  // no-op on invalid
    VoiceState& v = impl_->vs[v_i];
    v.ratio_override = true;
    v.ratio_ov = ratio;
    // The geometric glide applies per voice exactly as for the global
    // ratio (no special-casing); a target of exactly 1.0 engages this
    // voice's unity parking via the existing engagement machinery.
    impl_->retargetVoice(v, ratio);
}

void AlignedDelayLine::setVoiceGain(int v_i, double gain) {
    if (!impl_ || v_i < 0 || v_i >= impl_->cfg.max_voices) return;
    if (!std::isfinite(gain) || gain < 0.0) return;  // no-op on invalid
    impl_->vs[v_i].gain_t = gain;
    // The ramp (re)arms at the next process() block start.
}

void AlignedDelayLine::setVoiceMute(int v_i, bool mute) {
    if (!impl_ || v_i < 0 || v_i >= impl_->cfg.max_voices) return;
    impl_->vs[v_i].mute = mute;
    // Forces the EFFECTIVE gain target to 0 through the SAME ramp
    // (unmute ramps back to the user gain); armed at the block start.
}

void AlignedDelayLine::setVoiceBaseDelay(int v_i, double delay) {
    if (!impl_ || v_i < 0 || v_i >= impl_->cfg.max_voices) return;
    if (!std::isfinite(delay) || !(delay > 0.0)) return;  // no-op on invalid
    // The voice's whole read range (B tap at d + S, S <= 1.5*grain, plus
    // the 6-tap kernel margin) must stay inside the 2^18 ring -- the same
    // law prepare() applies to the last voice's base delay.
    if (!(delay + 3.0 * impl_->cfg.grain + 8.0 <
          static_cast<double>(Impl::kRingSize)))
        return;
    VoiceState& v = impl_->vs[v_i];
    v.base_delay_ov = delay;
    v.base_pending = true;  // applied at the START of the next process()
}

void AlignedDelayLine::setWetDry(double wet) {
    if (!impl_ || std::isnan(wet)) return;  // non-numbers: no-op
    // Clamped to [0, 1] per the header contract (the mix law is linear in
    // amplitude; endpoints are the bit-exact legacy / dry-copy paths).
    impl_->wet = std::min(1.0, std::max(0.0, wet));
}

void AlignedDelayLine::setBypass(bool on) {
    if (!impl_) return;
    impl_->bypass_on = on;
    // The blend ramps inside process() over bypass_seconds, in BOTH
    // directions; the engine keeps processing while bypassed.
}

int AlignedDelayLine::latencySamples() const {
    if (!impl_) return 0;
    return static_cast<int>(impl_->cfg.base_delay + impl_->cfg.grain * 0.5);
}

void AlignedDelayLine::process(const float* in, float* out, int n) {
    if (!impl_ || n <= 0) return;
    Impl& m = *impl_;

    // -- block-start parameter pickup (block-boundary application) ----------
    // Pending per-voice base delays + gain-ramp (re)arming. All setters
    // called since the last process() take effect HERE, atomically.
    m.applyPendingSurface();

    const bool fb_on = (m.cfg.feedback != 0.0);
    const double fb = m.cfg.feedback;
    const int nv = m.proc_hi;  // R19-c: active + still-fading voices
    const int interval = m.cfg.detection_interval;
    const double b_tgt = m.bypass_on ? 1.0 : 0.0;

    for (int k = 0; k < n; ++k) {
        // R23 (Gate 8, recorded engine delta): non-finite input guard.
        // REPRODUCED DEFECT (evidence/analysis_round23/r23_artifacts.json,
        // "nan_input_then_clean" / "inf_input_then_clean"): NaN/Inf input
        // previously propagated straight into the ring/feedback path and
        // the output stayed non-finite for the ring's memory. The guard
        // substitutes 0.0f for non-finite samples; for FINITE input the
        // value is UNCHANGED (bit-exact -- gated by the 100/100 suite).
        // This is defensive validation at the engine boundary, NOT a
        // concealment of an internal defect (there is none: the engine's
        // own arithmetic stays finite for finite input).
        const float inK = std::isfinite(in[k]) ? in[k] : 0.0f;
        // Control rate, stream-aligned: accumulate the RAW input (pre-feedback,
        // as the Python pushes xb = x) and fire push+estimate when the interval
        // boundary is crossed. The estimate applies from the boundary sample
        // onward. NOTE (documented delta): the Python reference estimates with
        // the full 64-sample block in the window BEFORE processing that block,
        // which is impossible in a streaming API that writes every output on
        // arrival; with 64-sample-aligned calls the native control timing
        // lags the Python's by at most one interval.
        // R19-a: the decimated detector consumes the SAME pending block at
        // the SAME stream positions (identical control cadence); only the
        // estimate values differ (low-passed window, coarser lag grid).
        m.det_pending[m.det_n++] = inK;
        if (m.det_n >= interval) {
            if (m.decimated)
                m.det.pushDecimated(m.det_pending.data(), m.det_n);
            else
                m.det.pushBlock(m.det_pending.data(), m.det_n);
            m.det_n = 0;
            m.last_f0 = m.det.estimate();
            // R23 (Gate 8): the tracking-gate route. With tracking_gate OFF
            // (the default) the legacy accept law runs BIT-EXACTLY below.
            // With it ON, Strategy A: the estimate passes the control layer
            // first (confidence/outlier/octave gating); only frames the
            // layer accepts (update_history) reach the engine's history.
            if (m.cfg.tracking_gate) {
                PitchEstimate pe;
                pe.sample = m.stream_pos;
                pe.f0 = m.last_f0;
                pe.periodicity = 1.0 - m.det.lastCmndfMin();
                pe.numeric_valid = (m.last_f0 > 0.0);
                const ControlDecision cd = m.control.push(pe);
                m.updateSpacingTarget(cd.update_history ? m.last_f0 : 0.0);
            } else {
                m.updateSpacingTarget(m.last_f0);
            }
        }

        // R19-e: read suppression is armed per sample (the bypass blend is
        // the only per-sample mover; wet / fb are block-constant). Exactly
        // the dry signal at the output + no feedback => reads are dead work.
        m.reads_suppressed =
            !fb_on && (m.wet == 0.0 || m.bypass_b == 1.0);

        {
            OCTAVOX_PROF(iotim, stageprof::kIoInput);
            const double xin =
                static_cast<double>(inK) + (fb_on ? fb * m.fb_state : 0.0);
            m.push(xin);
            // Raw input (never feedback) into the dry ring -- the dry tap is the
            // latency-aligned bit-exact copy; at wet == 1 && !bypass it is never
            // read (the legacy path below).
            m.pushDry(static_cast<double>(inK));
        }

        double acc = 0.0;
        {
            OCTAVOX_PROF(vltim, stageprof::kVoiceLoop);
            // R26: dual path. Scalar (the default, hash-pinned): the
            // VERBATIM inline voice loop. Accelerated levels: the batched
            // path (reads in lane-per-read AVX2 batches, bit-identical by
            // construction -- gated by the R26 engine bit-identity battery).
            if (m.read4_ != nullptr) {
                // R26 v2: collect -> execute -> combine, with one CROSS-
                // VOICE request pool (full 4-lane batches). max_voices is
                // bounded by 64 (prepare validation) -- the pool (<= 64*8
                // requests) lives on the stack; NO allocation, ever.
                constexpr int kMaxV = 64;
                constexpr int kMaxPool = kMaxV * 8;
                double pool[kMaxPool];
                double rres[kMaxPool];
                Impl::VoiceReads rds[kMaxV];
                Impl::VoiceReads srds[kMaxV];
                int npool = 0;
                for (int i = 0; i < nv; ++i) {
                    VoiceState& v = m.vs[i];
                    m.slewEngagement(v);  // engagement/parking slews (per sample)
                    m.slewGain(v);        // R15-P1: per-voice gain ramp
                    m.collectVoiceBatched(v, i, rds[i], srds[i], pool, npool);
                }
                m.executePool(pool, rres, npool);
                for (int i = 0; i < nv; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        if (rds[i].slot[j] >= 0)
                            rds[i].lval[j] = rres[rds[i].slot[j]];
                        if (srds[i].slot[j] >= 0)
                            srds[i].lval[j] = rres[srds[i].slot[j]];
                    }
                }
                for (int i = 0; i < nv; ++i) {
                    VoiceState& v = m.vs[i];
                    // The per-voice gain multiplies AFTER the voice-count
                    // normalisation; a gain of exactly 0 contributes
                    // nothing -- but the voice's state (and crossfades)
                    // keeps running (identical branch semantics to the
                    // verbatim path).
                    if (v.gain != 0.0) {
                        acc += m.combineVoiceBatched(v, i, rds[i], srds[i]) *
                               v.gain;
                    } else {
                        (void)m.combineVoiceBatched(v, i, rds[i], srds[i]);
                    }
                }
            } else {
                for (int i = 0; i < nv; ++i) {
                    VoiceState& v = m.vs[i];
                    m.slewEngagement(v);  // engagement/parking slews (per sample)
                    m.slewGain(v);        // R15-P1: per-voice gain ramp
                    // The per-voice gain multiplies AFTER the voice-count
                    // normalisation (final voice gain = norm * user gain; gain == 1
                    // is bit-neutral: x*1.0 == x). A voice whose ramped gain is
                    // exactly 0 contributes exactly nothing -- its sample is not
                    // even added -- while its DSP state keeps running (the
                    // processVoiceSample call below advances phase/glide/spacing).
                    if (v.gain != 0.0) {
                        acc += m.processVoiceSample(v, i) * v.gain;
                    } else {
                        (void)m.processVoiceSample(v, i);
                    }
                }
            }
        }
        {
            OCTAVOX_PROF(otim, stageprof::kOutputStage);
            // voice-count normalisation (ramped)
            if (m.norm != m.norm_target) {
                const double step = m.e_step;
                if (m.norm < m.norm_target)
                    m.norm = std::min(m.norm_target, m.norm + step);
                else
                    m.norm = std::max(m.norm_target, m.norm - step);
            }
            // y = the PRE-MIX WET signal. The global feedback taps it HERE
            // (wet-path-only): the wet path's state evolves identically at every
            // mix/bypass setting, and the dry path never carries feedback.
            const double y = acc * m.norm;
            m.fb_state = fb_on ? y : 0.0;

            // bypass blend slew (fixed slope, full range = exactly
            // bypass_seconds, exact snap; only touches the final output sample)
            if (m.bypass_b != b_tgt) {
                if (m.bypass_b < b_tgt)
                    m.bypass_b = std::min(b_tgt, m.bypass_b + m.b_step);
                else
                    m.bypass_b = std::max(b_tgt, m.bypass_b - m.b_step);
            }

            // -- output stage -------------------------------------------------
            double o;
            if (m.wet == 1.0 && m.bypass_b == 0.0) {
                // BIT-EXACT legacy path (the 12.0.0/14.0.0 output): no dry read
                // happens, the sample is exactly the pre-mix wet signal.
                o = y;
            } else {
                // dry = the RAW input delayed to align with the wet path's
                // intrinsic latency: dry[k] = in[k - (latencySamples() - 1)]
                // (read(d) = sample d-1 ago), a bit-exact masked integer read.
                const double dry = m.readDry(m.dry_delay);
                const double mix = (m.wet == 1.0) ? y
                                 : (m.wet == 0.0) ? dry
                                                  : m.wet * y + (1.0 - m.wet) * dry;
                if (m.bypass_b == 0.0) {
                    o = mix;
                } else if (m.bypass_b == 1.0) {
                    o = dry;  // fully bypassed: the dry delayed signal
                } else {
                    o = (1.0 - m.bypass_b) * mix + m.bypass_b * dry;
                }
            }
            out[k] = static_cast<float>(o);
        }
        ++m.stream_pos;  // R19-f: wrap/commit timestamp epoch
    }
}

const VoiceStats& AlignedDelayLine::voiceStats(int v) const {
    static VoiceStats empty;
    if (!impl_ || v < 0 || v >= impl_->cfg.max_voices) return empty;
    const VoiceState& s = impl_->vs[v];
    static thread_local VoiceStats out;  // diagnostics scratch (copy if kept)
    out.wraps = s.wraps;
    out.commits = s.commits;
    out.engaged = (s.e >= 1.0) ? 1 : 0;
    out.parked = (s.e <= 0.0 && s.a >= 1.0) ? 1 : 0;
    out.spacing = s.S;
    out.phase = s.frac;
    out.parked_delay = out.parked ? s.d_park : -1;
    out.ratio = s.ratio;  // R14: current gliding ratio (diagnostics)
    out.last_wrap_at = s.last_wrap_at;    // R19-f diagnostics
    out.last_commit_at = s.last_commit_at;
    return out;
}

// ---------------------------------------------------------------------------
// MultichannelShifter
// ---------------------------------------------------------------------------
struct MultichannelShifter::MImpl {
    std::vector<AlignedDelayLine*> engines;
    Config cfg{};
    int channels = 0;
};

MultichannelShifter::MultichannelShifter() = default;
MultichannelShifter::~MultichannelShifter() {
    if (mimpl_) {
        for (auto* e : mimpl_->engines) delete e;
        delete mimpl_;
    }
}

bool MultichannelShifter::prepare(const Config& cfg, int channels) {
    if (channels < 1 || channels > 64) return false;
    // R13-c in-place re-prepare: same channel count -> every engine
    // reconfigures itself (zero allocation when its buffer-determining
    // fields match; the engine array and channel isolation are reused).
    if (mimpl_ && mimpl_->channels == channels &&
        static_cast<int>(mimpl_->engines.size()) == channels) {
        for (auto* e : mimpl_->engines) {
            if (!e->prepare(cfg)) {
                for (auto* x : mimpl_->engines) delete x;
                delete mimpl_;
                mimpl_ = nullptr;
                return false;
            }
        }
        mimpl_->cfg = cfg;
        return true;
    }
    if (mimpl_) {
        for (auto* e : mimpl_->engines) delete e;
        delete mimpl_;
        mimpl_ = nullptr;
    }
    try {
        mimpl_ = new MImpl();
        mimpl_->cfg = cfg;
        mimpl_->channels = channels;
        for (int c = 0; c < channels; ++c) {
            auto* e = new AlignedDelayLine();
            if (!e->prepare(cfg)) {
                delete e;
                for (auto* x : mimpl_->engines) delete x;
                delete mimpl_;
                mimpl_ = nullptr;
                return false;
            }
            mimpl_->engines.push_back(e);
        }
    } catch (...) {
        if (mimpl_) {
            for (auto* e : mimpl_->engines) delete e;
            delete mimpl_;
            mimpl_ = nullptr;
        }
        return false;  // allocation failure: unusable, no exception escapes
    }
    return true;
}

void MultichannelShifter::reset() {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->reset();
}

void MultichannelShifter::setRatio(double r) {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->setRatio(r);
}
void MultichannelShifter::setVoices(int n) {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->setVoices(n);
}
void MultichannelShifter::setSpread(bool on) {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->setSpread(on);
}
void MultichannelShifter::setFeedback(double fb) {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->setFeedback(fb);
}

// -- round-15 P1 surface: broadcast to every channel engine ------------------
void MultichannelShifter::setVoiceRatio(int v, double ratio) {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->setVoiceRatio(v, ratio);
}
void MultichannelShifter::setVoiceGain(int v, double gain) {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->setVoiceGain(v, gain);
}
void MultichannelShifter::setVoiceMute(int v, bool mute) {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->setVoiceMute(v, mute);
}
void MultichannelShifter::setVoiceBaseDelay(int v, double delay) {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->setVoiceBaseDelay(v, delay);
}
void MultichannelShifter::setWetDry(double wet) {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->setWetDry(wet);
}
void MultichannelShifter::setBypass(bool on) {
    if (!mimpl_) return;
    for (auto* e : mimpl_->engines) e->setBypass(on);
}

bool MultichannelShifter::ready() const { return mimpl_ != nullptr; }
int MultichannelShifter::channels() const { return mimpl_ ? mimpl_->channels : 0; }
int MultichannelShifter::latencySamples() const {
    return mimpl_ && !mimpl_->engines.empty() ? mimpl_->engines[0]->latencySamples() : 0;
}

void MultichannelShifter::process(const float* const* in, float* const* out,
                                  int channels, int n) {
    if (!mimpl_ || channels != mimpl_->channels) return;
    for (int c = 0; c < channels; ++c) {
        mimpl_->engines[c]->process(in[c], out[c], n);
    }
}

const VoiceStats& MultichannelShifter::voiceStats(int channel, int voice) const {
    static VoiceStats empty;
    if (!mimpl_ || channel < 0 || channel >= mimpl_->channels) return empty;
    return mimpl_->engines[channel]->voiceStats(voice);
}

double AlignedDelayLine::lastEstimateHz() const {
    return impl_ ? impl_->last_f0 : 0.0;
}

simd::Level AlignedDelayLine::simdLevel() const {
    return (impl_ && ready_) ? impl_->det.simdLevel() : simd::Level::Scalar;
}

// -- R22-P1: Minimum Pitch signal-path diagnostics --------------------------
double AlignedDelayLine::spacingGateHz() const {
    if (!impl_) return std::max(Config{}.fmin, Config{}.f0_min);
    return std::max(impl_->cfg.fmin, impl_->cfg.f0_min);
}

double AlignedDelayLine::spacingTargetSamples() const {
    return impl_ ? impl_->S_target : Config{}.grain;
}

bool AlignedDelayLine::lastEstimateSpacingAccepted() const {
    return impl_ ? impl_->last_spacing_accepted : false;
}

int AlignedDelayLine::spacingHistoryCount() const {
    return impl_ ? impl_->f0_hist_n : 0;
}

const char* versionString() { return "octavox-native 16.3.0"; }

}  // namespace octavox
