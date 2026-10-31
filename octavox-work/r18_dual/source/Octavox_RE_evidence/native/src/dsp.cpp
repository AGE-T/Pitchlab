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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

#include "obs_kernel_table.h"  // OBS data table (see header comment)

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
// Interpolator tables (built once, shared, immutable)
// ---------------------------------------------------------------------------
constexpr int kLag6Taps = 6;

// 6-point Lagrange weights for phase mu, nodes -2,-1,0,1,2,3 (our own design;
// same formula as the Python reference).
void lagrange6Weights(double mu, double* w) {
    static const double xs[6] = {-2.0, -1.0, 0.0, 1.0, 2.0, 3.0};
    for (int i = 0; i < 6; ++i) {
        double v = 1.0;
        for (int j = 0; j < 6; ++j) {
            if (i != j) v *= (mu - xs[j]) / (xs[i] - xs[j]);
        }
        w[i] = v;
    }
}

struct InterpTables {
    double lag2048[2048][6];
    double lag256[256][6];

    InterpTables() {
        double w[6];
        for (int r = 0; r < 2048; ++r) {
            lagrange6Weights((r + 0.5) / 2048.0, w);  // bin centres (Python parity)
            for (int k = 0; k < 6; ++k) lag2048[r][k] = w[k];
        }
        for (int r = 0; r < 256; ++r) {
            lagrange6Weights((r + 0.5) / 256.0, w);
            for (int k = 0; k < 6; ++k) lag256[r][k] = w[k];
        }
    }
};

const InterpTables& interpTables() {
    static const InterpTables t;  // thread-safe magic static
    return t;
}

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
    void configure(int sr, DetectorMode mode) {
        mode_ = mode;
        sr_ = sr;
        if (mode == DetectorMode::Decimated) {
            decim_ = decimationFor(sr);
            sr_dec_ = sr / decim_;  // (exact at the canonical rates)
            tau_min_ = std::max(2, static_cast<int>(sr_dec_ / 1200.0));
            const int tau_max = static_cast<int>(sr_dec_ / 60.0);
            win_ = std::max(1024, static_cast<int>(std::ceil(2.5 * tau_max)));
            tau_max_ = std::min(win_ - 2, tau_max);
            // R19-a: warm the cumulative mean 18 lags below the search floor
            // (bounded by the absolute floor 2) -- see estimate().
            tau_lo_ = std::max(2, tau_min_ - 18);
            designAntiAlias(sr, sr_dec_);
        } else {
            decim_ = 1;
            sr_dec_ = sr;
            tau_min_ = std::max(2, static_cast<int>(sr_ / 1200.0));
            const int tau_max = static_cast<int>(sr_ / 60.0);
            win_ = std::max(2048, static_cast<int>(std::ceil(2.5 * tau_max)));
            tau_max_ = std::min(win_ - 2, tau_max);
            tau_lo_ = tau_min_;  // FullRate: the exact 15.00 range
        }
        buf_.assign(win_, 0.0);
        d_.assign(std::max(0, tau_max_ - tau_lo_), 0.0);
        cmnd_.assign(d_.size(), 0.0);
        last_f0_ = 0.0;
    }

    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.0);
        last_f0_ = 0.0;
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
    void pushDecimated(const float* x, int n) {
        OCTAVOX_PROF(pftim, stageprof::kDetBuffering);
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
        const std::vector<double>& x = buf_;
        double peak = 0.0;
        for (int i = 0; i < win_; ++i) peak = std::max(peak, std::abs(x[i]));
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
        for (int i = 0; i < ntau; ++i) {
            const int tau = tau_lo_ + i;
            double s = 0.0;
            for (int j = 0; j < m; ++j) {
                const double a = x[seg0 + j] - x[seg0 + tau + j];
                s += a * a;
            }
            d_[i] = s;
        }
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
        if (!(60.0 <= f0 && f0 <= 1200.0)) f0 = 0.0;
        last_f0_ = f0;
        return f0;
    }

    double lastF0() const { return last_f0_; }

    // R19 diagnostics: the effective control rate (sr_dec) and D.
    int decimation() const { return decim_; }
    int controlRate() const { return sr_dec_; }

private:
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
    int sr_dec_ = 48000;   // R19-a: the window's own rate (== sr_ when D == 1)
    int decim_ = 1;
    int win_ = 2048;       // R13-b: sample-rate-relative window (>= 2048)
    int tau_min_ = 2, tau_max_ = 800;
    int tau_lo_ = 2;       // R19-a: computation floor (== tau_min_ when FullRate)
    std::vector<double> buf_, d_, cmnd_;
    double last_f0_ = 0.0;
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
    std::vector<VoiceState> vs;      // [max_voices]
    std::vector<VoiceState> shadow;  // R19-b: commit-shadow grains [max_voices]
    std::vector<double> f0_hist;     // circular, size f0_history
    std::vector<double> med_scratch; // preallocated median scratch (no alloc)
    std::vector<float> det_pending;  // control-rate accumulator (raw input)
    int det_n = 0;
    int f0_hist_n = 0, f0_hist_i = 0;
    bool have_estimate = false;
    double S_target = 4096.0;
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
                const double* wv = interpTables().lag2048[ph];
                double s = 0.0;
                for (int k = 0; k < 6; ++k)
                    s += wv[k] * ring[(w + i0 + k - 2) & kRingMask];
                return s;
            }
            case Interpolation::Lagrange6Q: {
                int ph = static_cast<int>(mu * 256.0);
                if (ph > 255) ph = 255;
                const double* wv = interpTables().lag256[ph];
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
    void updateSpacingTarget(double f0) {
        OCTAVOX_PROF(mdtim, stageprof::kDetMedian);
        if (f0 != 0.0 && cfg.fmin <= f0 && f0 <= cfg.fmax) {
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

    // Returns this voice's output sample (R19: includes the commit-shadow
    // crossfade and the base-delay crossfade; both are inert when their
    // weights sit at 1, leaving the 15.00 path bit-exact).
    inline double processVoiceSample(VoiceState& v, int vi) {
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
            dm != static_cast<int>(DetectorMode::Decimated))
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
        impl_->cfg.detector_mode == cfg.detector_mode) {
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
        m.decimated = (cfg.detector_mode == DetectorMode::Decimated);
        for (int i = 0; i < cfg.max_voices; ++i)
            m.vs[i].active = (i < cfg.voices);
        m.proc_hi = cfg.voices;
        m.det.configure(static_cast<int>(cfg.sample_rate),
                        cfg.detector_mode);  // same sizes: no realloc
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
                              cfg.detector_mode);
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
        impl_->decimated = (cfg.detector_mode == DetectorMode::Decimated);
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
        m.det_pending[m.det_n++] = in[k];
        if (m.det_n >= interval) {
            if (m.decimated)
                m.det.pushDecimated(m.det_pending.data(), m.det_n);
            else
                m.det.pushBlock(m.det_pending.data(), m.det_n);
            m.det_n = 0;
            m.last_f0 = m.det.estimate();
            m.updateSpacingTarget(m.last_f0);
        }

        // R19-e: read suppression is armed per sample (the bypass blend is
        // the only per-sample mover; wet / fb are block-constant). Exactly
        // the dry signal at the output + no feedback => reads are dead work.
        m.reads_suppressed =
            !fb_on && (m.wet == 0.0 || m.bypass_b == 1.0);

        {
            OCTAVOX_PROF(iotim, stageprof::kIoInput);
            const double xin =
                static_cast<double>(in[k]) + (fb_on ? fb * m.fb_state : 0.0);
            m.push(xin);
            // Raw input (never feedback) into the dry ring -- the dry tap is the
            // latency-aligned bit-exact copy; at wet == 1 && !bypass it is never
            // read (the legacy path below).
            m.pushDry(static_cast<double>(in[k]));
        }

        double acc = 0.0;
        {
            OCTAVOX_PROF(vltim, stageprof::kVoiceLoop);
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

const char* versionString() { return "octavox-native 16.0.0"; }

}  // namespace octavox
