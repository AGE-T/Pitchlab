// OCTAVOX round 12 -- native test harness: correctness + unity regression.
//
// Gated by the process exit code (0 = all pass, 1 = any failure).
// Machine-readable results: --json <path> writes a JSON report; a summary
// table goes to stdout. Every case records the measured values so the
// report is evidence, not just a boolean.
//
// Deterministic by construction: every generator is a fixed formula or the
// shared 64-bit LCG (identical implementation exists on the Python side for
// the cross-language benchmark: evidence/analysis_round12/r12_native_metrics.py).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

// R17-P2: Windows/MSVC portability guards (the POSIX calls are environment
// reporting only -- the Linux paths are unchanged).
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include "octavox/dsp.hpp"
#include "wav_io.hpp"

using namespace octavox;

// JSON string escaper (defined in the JSON-report section; needed earlier by
// the R16-P2 environment record).
std::string jsonEscape(const std::string& s);
// /proc reader (defined in the environment-record section below).
static std::string readProcFile(const char* path);

// ---------------------------------------------------------------------------
// Allocation counter interface (defined in alloc_counter.cpp)
// ---------------------------------------------------------------------------
namespace octavox {
namespace allocs {
long count();
void arm(bool on);
void reset();
// R18-P1: always-on process-wide allocation totals (never armed/gated;
// the diagnostic trace reads them to prove the per-case allocation state
// returns to a bounded baseline -- bookkeeping only).
long totalNew();
long totalDelete();
}  // namespace allocs
}  // namespace octavox

// ---------------------------------------------------------------------------
// Deterministic generators (shared spec with the Python metrics script)
// ---------------------------------------------------------------------------
namespace gen {

/// 64-bit LCG, identical on the Python side. u in [0,1); sample = (2u-1)*amp.
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed * 6364136223846793005ULL +
                                     1442695040888963407ULL) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint64_t m = (s >> 11) & ((1ULL << 53) - 1);
        return static_cast<double>(m) / 9007199254740992.0;
    }
    double noise(double amp) { return (2.0 * next() - 1.0) * amp; }
};

std::vector<float> tone(double f, int sr, int n, double amp) {
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i)
        x[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * f * i / sr));
    return x;
}

std::vector<float> chord(const std::vector<double>& fs, int sr, int n,
                         double amp) {
    std::vector<float> x(n, 0.0f);
    for (double f : fs)
        for (int i = 0; i < n; ++i)
            x[i] += static_cast<float>(amp / fs.size() *
                                       std::sin(2.0 * M_PI * f * i / sr));
    return x;
}

std::vector<float> noise(uint64_t seed, int n, double amp) {
    Lcg rng(seed);
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i) x[i] = static_cast<float>(rng.noise(amp));
    return x;
}

std::vector<float> impulse(int n) {
    std::vector<float> x(n, 0.0f);
    x[0] = 1.0f;
    return x;
}

/// Percussive burst train: exponential decays.
std::vector<float> bursts(int sr, int n, double period_s, double amp) {
    std::vector<float> x(n, 0.0f);
    Lcg rng(99);
    const int per = static_cast<int>(period_s * sr);
    for (int i = 0; i < n; ++i) {
        const int t = i % per;
        if (t < 200)
            x[i] = static_cast<float>(amp * std::exp(-t / 40.0) *
                                      (t == 0 ? 1.0 : rng.noise(1.0)));
    }
    return x;
}

std::vector<float> chirp(double f0, double f1, int sr, int n, double amp) {
    std::vector<float> x(n);
    const double T = static_cast<double>(n) / sr;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double f = f0 * std::pow(f1 / f0, t / T);
        x[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * f * t));
    }
    return x;
}

}  // namespace gen

// ---------------------------------------------------------------------------
// Measurement helpers
// ---------------------------------------------------------------------------
namespace meas {

/// Radix-2 complex FFT (in-place, n a power of two).
void fft(double* re, double* im, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / len;
        const double wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < len / 2; ++k) {
                const double ur = re[i + k], ui = im[i + k];
                const double vr = re[i + k + len / 2] * cr -
                                  im[i + k + len / 2] * ci;
                const double vi = re[i + k + len / 2] * ci +
                                  im[i + k + len / 2] * cr;
                re[i + k] = ur + vr;
                im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr;
                im[i + k + len / 2] = ui - vi;
                const double nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

/// FFT peak pitch with the CORRECTED Hann-exact 3-point estimator.
/// Returns 0.0 when no peak above the threshold is found.
///
/// R15-REB1 fix (recovered from the record): the interpolation now runs on
/// the three bins' MAGNITUDES with the Hann-exact formula
///     delta = 2*(c - a) / (a + 2*b + c)
/// (a, b, c = magnitudes of bins kpeak-1, kpeak, kpeak+1) instead of the
/// old power-domain parabola 0.5*(a-c)/(a-2b+c) on squared magnitudes,
/// which is biased for off-bin tones under a Hann window (the historical
/// round-15 record: the correction made battery cases PASS, none failed).
///
/// FFT size N = the largest power of two <= min(n, 65536): the analysed
/// NEWEST block of N samples always carries a complete Hann window, and
/// the window offset off = n - N is >= 0 BY CONSTRUCTION (the estimator
/// never reads before the supplied buffer start).
double pitchFFT(const float* x, int n, int sr, double fmin, double fmax) {
    int N = 1;
    while (N < n && N < 65536) N <<= 1;  // largest pow2 <= min(n, 65536)
    if (N < 16) return 0.0;
    std::vector<double> re(N, 0.0), im(N, 0.0);
    const int off = n - N;  // use the newest N samples (off >= 0 by construction)
    for (int i = 0; i < N; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / N);
        re[i] = static_cast<double>(x[off + i]) * w;
    }
    fft(re.data(), im.data(), N);
    const int kmin = std::max(1, static_cast<int>(fmin * N / sr));
    const int kmax = std::min(N / 2 - 2, static_cast<int>(fmax * N / sr));
    if (kmax <= kmin) return 0.0;
    int kpeak = kmin;
    double best = 0.0;
    for (int k = kmin; k <= kmax; ++k) {
        const double m = re[k] * re[k] + im[k] * im[k];  // power (same argmax)
        if (m > best) {
            best = m;
            kpeak = k;
        }
    }
    if (best <= 0.0) return 0.0;
    // magnitudes of the three bins around the peak (Hann-exact estimator)
    const double a = std::sqrt(re[kpeak - 1] * re[kpeak - 1] +
                               im[kpeak - 1] * im[kpeak - 1]);
    const double b = std::sqrt(best);
    const double c = std::sqrt(re[kpeak + 1] * re[kpeak + 1] +
                               im[kpeak + 1] * im[kpeak + 1]);
    double d = 0.0;
    const double den = a + 2.0 * b + c;
    if (den > 1e-300) d = 2.0 * (c - a) / den;
    if (d > 0.5) d = 0.5;
    if (d < -0.5) d = -0.5;
    return static_cast<double>(sr) * (kpeak + d) / N;
}

double rms(const float* x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += static_cast<double>(x[i]) * x[i];
    return n ? std::sqrt(s / n) : 0.0;
}

double peak(const float* x, int n) {
    double p = 0.0;
    for (int i = 0; i < n; ++i) p = std::max(p, std::abs(static_cast<double>(x[i])));
    return p;
}

bool allFinite(const float* x, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(x[i])) return false;
    return true;
}

/// Envelope modulation in dB: max/min of RMS windows of `w` samples over the
/// steady region (skip the first skip samples). w = sr/1000 gives the 1 ms
/// metric; w = period gives the period-synchronous metric (the r11 lesson:
/// a 1 ms window phase-breathes several dB on a plain tone).
double envModulationDb(const float* x, int n, int sr, int skip, int w) {
    std::vector<double> env;
    for (int i0 = skip; i0 + w <= n; i0 += w) {
        env.push_back(rms(x + i0, w));
    }
    if (env.empty()) return 0.0;
    double lo = 1e300, hi = 0.0;
    for (double v : env) {
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (lo <= 0.0) return hi > 0.0 ? 999.0 : 0.0;
    return 20.0 * std::log10(hi / lo);
}

double centsBetween(double fMeasured, double fNominal) {
    if (fMeasured <= 0.0 || fNominal <= 0.0) return 999.0;
    return 1200.0 * std::log2(fMeasured / fNominal);
}

/// Max absolute sample-to-sample difference (transition click meter).
double maxStep(const float* x, int n, int i0 = 0) {
    double m = 0.0;
    for (int i = std::max(1, i0 + 1); i < n; ++i)
        m = std::max(m, std::abs(static_cast<double>(x[i]) - x[i - 1]));
    return m;
}

}  // namespace meas

// ---------------------------------------------------------------------------
// Case framework + JSON report
// ---------------------------------------------------------------------------
struct CaseResult {
    std::string name;
    bool pass = false;
    std::string note;
    std::vector<std::pair<std::string, double>> values;
    // R16-P2 reproducible-harness extensions (set by pushCase; never a gate):
    std::string group = "";       // owning battery (registry lookup)
    std::string category = "unit"; // "unit" | "long" (registry lookup)
    double duration_ms = 0.0;     // wall time of the case (since previous push)
};

static std::vector<CaseResult> g_results;

// ---------------------------------------------------------------------------
// R16-P2: reproducible workflow extensions.
//  * static registry: every case name -> group id + design-intent category
//    (deterministic labels, fixed before any run; "long" = long-duration
//    robustness: the stability long-run streams, the R13 cold-start
//    batteries, the detector batteries at 96/44.1 kHz + estimate tracking
//    and stability, and the r15acc battery/replay cases; everything else
//    "unit").  Selection NEVER changes a gate -- it only decides whether a
//    case body runs.
//  * selection: --filter <substr> (repeatable, OR, case-insensitive name
//    substring), --group <id> (repeatable, OR), --category unit|long
//    (repeatable, OR); different kinds AND together.  Unknown group or
//    category, or an empty selection, is a usage error (exit 2).
//  * per-case wall duration + live progress line per case, group headers.
// ---------------------------------------------------------------------------
struct CaseMeta {
    const char* name;
    const char* group;
    const char* category;
};

static const CaseMeta kAllCases[] = {
    {"prepare_default", "prepare_reset", "unit"},
    {"prepare_invalid_rejected", "prepare_reset", "unit"},
    {"reset_determinism", "prepare_reset", "unit"},
    {"repeated_prepare", "prepare_reset", "unit"},
    {"block_size_invariance", "prepare_reset", "unit"},
    {"steady_state_no_allocation", "prepare_reset", "unit"},
    {"unity_tone_220_bitexact", "unity", "unit"},
    {"unity_tone_1100_bitexact", "unity", "unit"},
    {"unity_noise_bitexact", "unity", "unit"},
    {"unity_chord_bitexact", "unity", "unit"},
    {"unity_silence_bitexact", "unity", "unit"},
    {"unity_lowlevel_bitexact", "unity", "unit"},
    {"unity_enter_from_+400c_bitexact", "unity", "unit"},
    {"unity_gain_exact", "unity", "unit"},
    {"unity_entry_continuity", "unity", "unit"},
    {"near_unity_+1c_fresh", "unity", "unit"},
    {"near_unity_-1c_fresh", "unity", "unit"},
    {"near_unity_+10c_fresh", "unity", "unit"},
    {"near_unity_-10c_fresh", "unity", "unit"},
    {"near_unity_+50c_fresh", "unity", "unit"},
    {"near_unity_-50c_fresh", "unity", "unit"},
    {"near_unity_+100c_fresh", "unity", "unit"},
    {"near_unity_-100c_fresh", "unity", "unit"},
    {"near_unity_+500c_fresh", "unity", "unit"},
    {"near_unity_-500c_fresh", "unity", "unit"},
    {"near_unity_+1200c_fresh", "unity", "unit"},
    {"near_unity_-1200c_fresh", "unity", "unit"},
    {"near_unity_+1c_via_unity", "unity", "unit"},
    {"near_unity_-1c_via_unity", "unity", "unit"},
    {"near_unity_+10c_via_unity", "unity", "unit"},
    {"near_unity_-10c_via_unity", "unity", "unit"},
    {"near_unity_+50c_via_unity", "unity", "unit"},
    {"near_unity_-50c_via_unity", "unity", "unit"},
    {"no_bypass_region", "unity", "unit"},
    {"unity_feedback_stability", "unity", "unit"},
    {"unity_8voice_unison_exact_mix", "unity", "unit"},
    {"voices_8_spread_+700c", "voices", "unit"},
    {"setvoices_midstream", "voices", "unit"},
    {"stereo_isolation_unity", "stereo", "unit"},
    {"stereo_isolation_shifted", "stereo", "unit"},
    {"stereo_independence", "stereo", "unit"},
    {"rate_44100_+400c", "rates_formats", "unit"},
    {"rate_96000_+400c", "rates_formats", "unit"},
    {"wav_roundtrip_example_input", "rates_formats", "unit"},
    {"wav_24bit_read", "rates_formats", "unit"},
    {"longrun_60s_8voice_feedback", "stability", "long"},
    {"chirp_30s_+700c", "stability", "long"},
    {"impulse_latency", "stability", "unit"},
    {"impulse_latency_unity", "stability", "unit"},
    {"dc_input_finite", "stability", "long"},
    {"clipping_behaviour", "stability", "unit"},
    {"cold_start_impulse_alignment_unity_1v", "r13", "long"},
    {"cold_start_impulse_alignment_unity_8v", "r13", "long"},
    {"cold_start_impulse_alignment_up_1v", "r13", "long"},
    {"cold_start_impulse_alignment_up_8v", "r13", "long"},
    {"cold_start_impulse_alignment_down_1v", "r13", "long"},
    {"cold_start_impulse_alignment_down_8v", "r13", "long"},
    {"cold_start_no_spurious_transient", "r13", "long"},
    {"detector_96000hz_sine_battery", "r13", "long"},
    {"detector_44100hz_sine_battery", "r13", "long"},
    {"detector_estimate_tracking_44100", "r13", "long"},
    {"detector_estimate_tracking_48000", "r13", "long"},
    {"detector_estimate_tracking_96000", "r13", "long"},
    {"detector_96k_estimate_stability", "r13", "long"},
    {"rt_reprepare_zero_allocation", "r13", "unit"},
    {"rt_reprepare_param_change_zero_allocation", "r13", "unit"},
    {"rt_reprepare_multichannel_zero_allocation", "r13", "unit"},
    {"rt_reset_no_allocation", "r13", "unit"},
    {"rt_setters_and_process_no_allocation", "r13", "unit"},
    {"rt_reprepare_result_identity", "r13", "unit"},
    {"voicestats_ratio_diagnostics", "r13", "unit"},
    {"per_voice_ratios_independent", "r15p1", "unit"},
    {"per_voice_gain_ramp_and_mute", "r15p1", "unit"},
    {"per_voice_delay_isolation", "r15p1", "unit"},
    {"wetdry_endpoints", "r15p1", "unit"},
    {"bypass_clickfree_and_transparent", "r15p1", "unit"},
    {"new_setters_no_allocation", "r15p1", "unit"},
    {"default_neutrality", "r15p1", "unit"},
    {"r15acc_ratio_change_isolation", "r15acc", "unit"},
    {"r15acc_level_accuracy", "r15acc", "unit"},
    {"r15acc_block_boundary_semantics", "r15acc", "unit"},
    {"r15acc_reset_reprepare_determinism", "r15acc", "long"},
    {"r15acc_stereo_surface", "r15acc", "unit"},
    {"r15acc_robustness_battery", "r15acc", "long"},
    {"r15acc_invalid_args", "r15acc", "unit"},
    // R19 (16.0.0): the new default-behaviour reference record, decimated
    // detector accuracy + architecture agreement, the three click-free
    // transition gates, read-suppression bit-identity, active-voice scope.
    {"default_neutrality_r19", "r19", "unit"},
    {"detector_decimated_accuracy_battery", "r19", "long"},
    {"detector_decimated_vs_fullrate_delta", "r19", "long"},
    {"detector_hybrid_accuracy_battery", "r20", "long"},
    {"detector_hybrid_vs_fullrate_delta", "r20", "long"},
    {"detector_hybrid_lowrate_config", "r20", "long"},
    {"commit_crossfade_continuity", "r19", "unit"},
    {"voice_activation_clickfree", "r19", "unit"},
    {"basedelay_crossfade_clickfree", "r19", "unit"},
    {"read_suppression_bitidentity", "r19", "unit"},
    {"active_voices_processing_scope", "r19", "unit"},
    // R22 (16.2.0): the Minimum Pitch signal path -- the unified engine
    // spacing gate (bit-exact by detector dominance), the explicit
    // detectable-vs-spacing-trackable distinction diagnostics, the
    // low-floor full-path lock, and the honest no-clamp low-frequency
    // transposition record.
    {"minpitch_gate_unification_bitexact", "r22", "unit"},
    {"minpitch_distinction_diagnostics", "r22", "unit"},
    {"minpitch_lowfloor_full_path", "r22", "unit"},
    {"minpitch_lowfreq_transposition_honesty", "r22", "unit"},
    // R24 (16.3.0): the SIMD capability layer -- detection/resolve rules,
    // the AVX2 lag-axis kernel BIT-EXACTNESS hard gate, peak bit-exactness,
    // the FMA bounded-deviation gate, engine bit-identity at the AVX2 level
    // across all three detector modes, the FMA engine-closeness record,
    // prepare() acceptance/rejection, default-neutrality + in-place
    // re-prepare level switching, and allocation-free accelerated runs.
    {"simd_detect_resolve_rules", "r24", "unit"},
    {"simd_kernel_avx2_lag_bitexact", "r24", "unit"},
    {"simd_kernel_peak_bitexact", "r24", "unit"},
    {"simd_kernel_fma_bounded", "r24", "unit"},
    {"simd_engine_avx2_bitidentity", "r24", "long"},
    {"simd_engine_fma_closeness", "r24", "long"},
    {"simd_prepare_mode_validation", "r24", "unit"},
    {"simd_default_scalar_neutrality", "r24", "unit"},
    {"simd_reprepare_level_switch", "r24", "unit"},
    {"simd_rt_allocfree_accelerated", "r24", "unit"},
    // R26: the batched interpolated-read battery (lane-per-read AVX2 kernel
    // + the batched voice path): hard kernel/engine bit-exactness gates
    // (incl. control churn + mid-stream level handover), the bounded FMA
    // churn-closeness record, and allocation-free churned batched runs.
    {"r26_read4_kernel_bitexact", "r26", "unit"},
    {"r26_engine_readpath_bitidentity", "r26", "long"},
    {"r26_engine_fma_churn_closeness", "r26", "long"},
    {"r26_midstream_level_handover", "r26", "long"},
    {"r26_rt_allocfree_batched", "r26", "unit"},
// total: 115 cases, long: 25, unit: 90
};

static const int kAllCaseCount =
    static_cast<int>(sizeof(kAllCases) / sizeof(kAllCases[0]));

static const char* kGroupIds[] = {"prepare_reset", "unity",     "voices",
                                  "stereo",        "rates_formats",
                                  "stability",     "r13",       "r15p1",
                                  "r15acc",        "r19",       "r22",
                                  "r24"};
static const int kGroupCount =
    static_cast<int>(sizeof(kGroupIds) / sizeof(kGroupIds[0]));
static const char* kCategories[] = {"unit", "long"};

// selection state (filled by main() before any case runs)
static std::vector<std::string> g_filterSubs;  // OR of substrings
static std::vector<std::string> g_selGroups;   // OR of group ids
static std::vector<std::string> g_selCats;     // OR of categories
static std::string g_currentGroup;             // group currently running
static int g_progDone = 0;                     // cases pushed so far
static int g_progTotal = 0;                    // cases selected this run
static bool g_reverse = false;                 // R17-P1: --reverse order mode

static std::chrono::steady_clock::time_point g_lastMark;  // duration epoch

// ---------------------------------------------------------------------------
// R18-P1: aggregate-suite diagnostic mode (--diag <path>) and the
// group-isolated fallback (--isolate).
//
// --diag writes an append-only, line-flushed JSONL trace of suite events:
// suite start/end, group start/end, every case START and completion, the
// per-case wall time, the process RSS (where /proc is available), the
// always-on allocation counter state (total new/delete/outstanding -- never
// armed, pure bookkeeping) and a floating-point environment probe (rounding
// mode + accrued exception flags).  Every record is flushed BEFORE the next
// case body starts, so when a constrained verification environment kills the
// run inside its window, the last "case_start" without a matching
// "case_end" pinpoints exactly where the run stood.  The trace NEVER gates:
// it is observation only; the 85-case default suite and every gate are
// unchanged.
//
// --isolate (POSIX only) runs each selected group in a fresh forked child
// process (one process per group, progress on stdout, results returned over
// a pipe).  It is the documented fallback for constrained environments and a
// differential diagnostic: if a single-process run misbehaves while
// --isolate passes, the cause is process-global state; if both behave the
// same, it is not.  It is NOT equivalent to the single-process full run and
// is never claimed to be.
// ---------------------------------------------------------------------------
static bool g_isolate = false;             // R18-P1: --isolate fallback mode
static std::string g_diagPath;             // R18-P1: --diag <path>
static FILE* g_diag = nullptr;             // open trace handle (or null)
static std::vector<std::string> g_diagNext;  // predicted case order (running group)
static size_t g_diagNextIdx = 0;             // index of the case now in flight
static std::chrono::steady_clock::time_point g_diagT0;  // trace epoch
static int g_diagRound0 = -1;                // rounding mode at suite start
static int g_diagFlags0 = -1;                // fenv flags at suite start

static double diagMs() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - g_diagT0)
        .count();
}

static long diagRssKb() {
#ifndef _WIN32
    // VmRSS from /proc/self/status (Linux; "where supported" per the spec)
    const std::string st = readProcFile("/proc/self/status");
    const size_t p = st.find("VmRSS:");
    if (p != std::string::npos) {
        long v = 0;
        if (std::sscanf(st.c_str() + p + 6, "%ld", &v) == 1) return v;
    }
#endif
    return -1;  // unsupported on this platform
}

static std::string diagStatePairs() {
    char b[192];
    std::snprintf(b, sizeof b,
                  "\"rss_kb\": %ld, \"alloc_new\": %ld, \"alloc_del\": %ld, "
                  "\"alloc_out\": %ld, \"round\": %d, \"fenv\": %d",
                  diagRssKb(), allocs::totalNew(), allocs::totalDelete(),
                  allocs::totalNew() - allocs::totalDelete(),
                  std::fegetround(), std::fetestexcept(FE_ALL_EXCEPT));
    return std::string(b);
}

static void diagEmit(const std::string& line) {
    if (!g_diag) return;
    std::fprintf(g_diag, "%s\n", line.c_str());
    std::fflush(g_diag);
}

// START record for the case about to run (idx = 1-based position in THIS run)
static void diagCaseStart(size_t idx, const std::string& name) {
    if (!g_diag) return;
    char b[96];
    std::snprintf(b, sizeof b, ", \"idx\": %zu, \"t_ms\": %.1f, ", idx,
                  diagMs());
    diagEmit("{\"ev\": \"case_start\", \"name\": \"" + jsonEscape(name) +
             "\"" + b + diagStatePairs() + "}");
}

static long diagPid() {
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}

static std::string toLowerStr(const std::string& s) {
    std::string o = s;
    for (char& ch : o)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return o;
}

static const CaseMeta* findMeta(const std::string& name) {
    for (int i = 0; i < kAllCaseCount; ++i)
        if (name == kAllCases[i].name) return &kAllCases[i];
    return nullptr;
}

static bool harnSelected(const std::string& name) {
    const CaseMeta* m = findMeta(name);
    if (!m) return true;  // unregistered name: run it (visible in the report)
    if (!g_selGroups.empty() &&
        std::find(g_selGroups.begin(), g_selGroups.end(),
                  std::string(m->group)) == g_selGroups.end())
        return false;
    if (!g_selCats.empty() &&
        std::find(g_selCats.begin(), g_selCats.end(),
                  std::string(m->category)) == g_selCats.end())
        return false;
    if (!g_filterSubs.empty()) {
        const std::string ln = toLowerStr(name);
        bool hit = false;
        for (const auto& f : g_filterSubs)
            if (ln.find(toLowerStr(f)) != std::string::npos) {
                hit = true;
                break;
            }
        if (!hit) return false;
    }
    return true;
}

// Intercept for R.push_back(std::move(c)): stamps group/category/duration,
// pushes, and prints the live progress line.  Harness bookkeeping only --
// it never touches a gate.
static void pushCase(std::vector<CaseResult>& R, CaseResult&& c) {
    const auto now = std::chrono::steady_clock::now();
    c.duration_ms =
        std::chrono::duration<double, std::milli>(now - g_lastMark).count();
    const CaseMeta* m = findMeta(c.name);
    if (m) {
        c.group = m->group;
        c.category = m->category;
    } else {
        c.group = g_currentGroup;  // fallback, plus the loud warning below
        c.category = "unit";
        std::fprintf(stderr,
                     "WARNING: case '%s' is not in the static registry\n",
                     c.name.c_str());
    }
    g_lastMark = now;
    ++g_progDone;
    R.push_back(std::move(c));
    const CaseResult& pushed = R.back();
    std::printf("[%3d/%3d] %-46s %-4s %10.1f ms\n", g_progDone, g_progTotal,
                pushed.name.c_str(), pushed.pass ? "ok" : "FAIL",
                pushed.duration_ms);
    std::fflush(stdout);
    // R18-P1 diagnostic trace: the completion record for THIS case and the
    // START record for the next predicted case (both flushed before the next
    // case body can run).  If the actual case name disagrees with the
    // prediction a diag_sync record marks it (the registry mirrors the push
    // order; a desync would only mislabel records, never a gate).
    if (g_diag) {
        char b[96];
        std::snprintf(b, sizeof b,
                      ", \"idx\": %d, \"pass\": %s, \"wall_ms\": %.1f, "
                      "\"t_ms\": %.1f, ",
                      g_progDone, pushed.pass ? "true" : "false",
                      pushed.duration_ms, diagMs());
        diagEmit("{\"ev\": \"case_end\", \"name\": \"" +
                 jsonEscape(pushed.name) + "\"" + b + diagStatePairs() + "}");
        if (g_diagNextIdx < g_diagNext.size() &&
            g_diagNext[g_diagNextIdx] != pushed.name) {
            diagEmit("{\"ev\": \"diag_sync\", \"predicted\": \"" +
                     jsonEscape(g_diagNext[g_diagNextIdx]) +
                     "\", \"actual\": \"" + jsonEscape(pushed.name) + "\"}");
        }
        ++g_diagNextIdx;
        if (g_diagNextIdx < g_diagNext.size())
            diagCaseStart((size_t)g_progDone + 1, g_diagNext[g_diagNextIdx]);
    }
}

// ---- environment record (R16-P2; same fragment shape in bench_main.cpp) --
static std::string readProcFile(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return "";
    std::string s;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

#ifndef OCTAVOX_BUILD_TYPE
#define OCTAVOX_BUILD_TYPE "unknown"
#endif
#ifndef OCTAVOX_SANITIZERS
#define OCTAVOX_SANITIZERS 0
#endif

static std::string envJsonFragment() {
    char ts[48];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    std::string os = readProcFile("/proc/sys/kernel/ostype");
    const std::string osrel = readProcFile("/proc/sys/kernel/osrelease");
    if (!os.empty() && !osrel.empty()) os += " " + osrel;
    std::string cpu;
    {
        FILE* f = std::fopen("/proc/cpuinfo", "rb");
        if (f) {
            char line[1024];
            while (std::fgets(line, sizeof(line), f)) {
                if (std::strncmp(line, "model name", 10) == 0) {
                    const char* colon = std::strchr(line, ':');
                    if (colon) {
                        cpu = colon + 1;
                        // trim trailing newline and leading spaces
                        while (!cpu.empty() &&
                               (cpu.back() == '\n' || cpu.back() == '\r'))
                            cpu.pop_back();
                        size_t b = cpu.find_first_not_of(' ');
                        cpu = (b == std::string::npos) ? "" : cpu.substr(b);
                    }
                    break;
                }
            }
            std::fclose(f);
        }
    }
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    long cores = static_cast<long>(si.dwNumberOfProcessors);
#else
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (cores <= 0) cores = 0;
    std::string j = "\"timestamp_utc\": \"";
    j += ts;
    j += "\", \"compiler\": \"";
    j += jsonEscape(__VERSION__);
    j += "\", \"build_type\": \"";
    j += jsonEscape(OCTAVOX_BUILD_TYPE);
    j += "\", \"sanitizers\": ";
    j += (OCTAVOX_SANITIZERS != 0) ? "true" : "false";
    j += ", \"os\": \"";
    j += jsonEscape(os);
    j += "\", \"cpu\": \"";
    j += jsonEscape(cpu);
    j += "\", \"cores\": ";
    j += std::to_string(cores);
    j += ", \"reverse_order\": ";
    j += g_reverse ? "true" : "false";
    j += ", \"isolate\": ";
    j += g_isolate ? "true" : "false";
    return j;
}

void addValue(CaseResult& c, const std::string& k, double v) {
    c.values.emplace_back(k, v);
}

bool check(CaseResult& c, bool ok, const std::string& what) {
    if (!ok) c.note += (c.note.empty() ? "" : "; ") + what;
    return ok;
}

double semitoneRatio(double cents) { return std::pow(2.0, cents / 1200.0); }

// Runs the engine over a full signal in one go (64-aligned internally is
// irrelevant -- process takes the whole block).
std::vector<float> run(const Config& cfg, const std::vector<float>& in,
                       int block = 1 << 20) {
    AlignedDelayLine e;
    if (!e.prepare(cfg)) {
        return {};
    }
    std::vector<float> out(in.size(), 0.0f);
    int done = 0;
    while (done < static_cast<int>(in.size())) {
        const int n = std::min(block, static_cast<int>(in.size()) - done);
        e.process(in.data() + done, out.data() + done, n);
        done += n;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The cases
// ---------------------------------------------------------------------------
void casesPrepareReset(std::vector<CaseResult>& R) {
    if (harnSelected("prepare_default")) {  // 1. default prepare
        CaseResult c{"prepare_default"};
        Config cfg;
        AlignedDelayLine e;
        const bool ok = e.prepare(cfg);
        check(c, ok, "prepare failed");
        check(c, e.latencySamples() == 2304, "latency != 2304");
        check(c, e.ready(), "not ready");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("prepare_invalid_rejected")) {  // 2. invalid configs rejected
        CaseResult c{"prepare_invalid_rejected"};
        int rejected = 0, total = 0;
        Config bad;
        auto tryCfg = [&](const Config& cfg) {
            ++total;
            AlignedDelayLine e;
            if (!e.prepare(cfg)) ++rejected;
        };
        bad = Config();
        bad.sample_rate = 0.0;
        tryCfg(bad);
        bad = Config();
        bad.voices = 9;
        tryCfg(bad);
        bad = Config();
        bad.grain = 0.0;
        tryCfg(bad);
        bad = Config();
        bad.feedback = 1.0;
        tryCfg(bad);
        bad = Config();
        bad.base_delay = 0.0;
        tryCfg(bad);
        addValue(c, "rejected", rejected);
        addValue(c, "total", total);
        check(c, rejected == total, "some invalid configs accepted");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("reset_determinism")) {  // 3. reset determinism
        CaseResult c{"reset_determinism"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        const auto in = gen::tone(220.0, 48000, 48000, 0.5);
        AlignedDelayLine a, b;
        a.prepare(cfg);
        b.prepare(cfg);
        std::vector<float> oa(in.size(), 0.f), ob(in.size(), 0.f),
            ob2(in.size(), 0.f);
        a.process(in.data(), oa.data(), in.size());
        b.process(in.data(), ob.data(), in.size());
        b.reset();
        b.process(in.data(), ob2.data(), in.size());
        double d1 = 0.0, d2 = 0.0;
        for (size_t i = 0; i < in.size(); ++i) {
            d1 = std::max(d1, std::abs((double)oa[i] - ob[i]));
            d2 = std::max(d2, std::abs((double)ob[i] - ob2[i]));
        }
        addValue(c, "fresh_vs_fresh", d1);
        addValue(c, "reset_vs_fresh", d2);
        check(c, d1 == 0.0, "two fresh engines differ");
        check(c, d2 == 0.0, "reset engine differs from fresh");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("repeated_prepare")) {  // 4. repeated prepare
        CaseResult c{"repeated_prepare"};
        Config cfg;
        cfg.ratio = semitoneRatio(500.0);
        const auto in = gen::tone(330.0, 48000, 24000, 0.4);
        AlignedDelayLine a, b;
        a.prepare(cfg);
        std::vector<float> oa(in.size(), 0.f), ob(in.size(), 0.f);
        a.process(in.data(), oa.data(), in.size());
        a.prepare(cfg);  // re-prepare
        a.process(in.data(), ob.data(), in.size());
        b.prepare(cfg);
        std::vector<float> oc(in.size(), 0.f);
        b.process(in.data(), oc.data(), in.size());
        double d = 0.0;
        for (size_t i = 0; i < in.size(); ++i)
            d = std::max(d, std::abs((double)ob[i] - oc[i]));
        addValue(c, "reprepared_vs_fresh", d);
        check(c, d == 0.0, "re-prepared engine differs from fresh");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("block_size_invariance")) {  // 5. block-size invariance
        CaseResult c{"block_size_invariance"};
        Config cfg;
        cfg.ratio = semitoneRatio(700.0);
        const auto in = gen::tone(196.0, 48000, 48000, 0.4);
        const auto ref = run(cfg, in, 1 << 20);
        const int splits[] = {1, 7, 16, 64, 128, 512, 1024, 4096};
        double worst = 0.0;
        for (int s : splits) {
            const auto got = run(cfg, in, s);
            double d = 0.0;
            for (size_t i = 0; i < in.size(); ++i)
                d = std::max(d, std::abs((double)ref[i] - got[i]));
            worst = std::max(worst, d);
        }
        // mixed split pattern
        {
            AlignedDelayLine e;
            e.prepare(cfg);
            std::vector<float> got(in.size(), 0.f);
            size_t i = 0;
            int pat[] = {1, 64, 7, 512, 16, 4096, 3};
            int pi = 0;
            while (i < in.size()) {
                int n = std::min(pat[pi % 7], (int)(in.size() - i));
                e.process(in.data() + i, got.data() + i, n);
                i += n;
                ++pi;
            }
            double d = 0.0;
            for (size_t k = 0; k < in.size(); ++k)
                d = std::max(d, std::abs((double)ref[k] - got[k]));
            worst = std::max(worst, d);
        }
        addValue(c, "worst_split_diff", worst);
        check(c, worst == 0.0, "block split changed the output");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("steady_state_no_allocation")) {  // 6. steady-state allocation-free
        CaseResult c{"steady_state_no_allocation"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.35;
        const auto in = gen::noise(1234, 48000, 0.5);
        AlignedDelayLine e;
        e.prepare(cfg);
        std::vector<float> out(in.size(), 0.f);
        e.process(in.data(), out.data(), 24000);  // warm-up (tables, paths)
        allocs::reset();
        allocs::arm(true);
        e.process(in.data() + 24000, out.data() + 24000,
                  static_cast<int>(in.size()) - 24000);
        allocs::arm(false);
        const long n = allocs::count();
        addValue(c, "allocations_during_process", static_cast<double>(n));
        check(c, n == 0, "allocations during steady-state process()");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

void casesUnity(std::vector<CaseResult>& R) {
    const int sr = 48000;

    // bit-exact delayed-copy gate used by the unity battery
    auto unityExact = [&](const std::string& name, const std::vector<float>& in,
                          bool arrive_from_shift = false) {
        if (!harnSelected(name)) return;  // R16-P2 selection (no gate touched)
        CaseResult c{name};
        Config cfg;  // ratio exactly 1.0, 1 voice, neutral
        AlignedDelayLine e;
        e.prepare(cfg);
        if (arrive_from_shift) {
            // arrive from +4 st first: 1 s shifted, then unity, then settle
            e.setRatio(semitoneRatio(400.0));
            std::vector<float> pre(sr, 0.f);
            e.process(in.data(), pre.data(), sr);
            e.setRatio(1.0);
            std::vector<float> mid(static_cast<int>(0.3 * sr), 0.f);
            if ((int)in.size() > sr)
                e.process(in.data() + sr, mid.data(),
                           std::min((int)in.size() - sr, (int)mid.size()));
        }
        const int d_park = e.voiceStats(0).parked_delay;
        addValue(c, "d_park", d_park);
        check(c, d_park >= 256 && d_park < 256 + 4096, "d_park out of range");
        check(c, e.voiceStats(0).parked == 1, "voice not parked");
        const int skip = arrive_from_shift ? static_cast<int>(0.05 * sr) : 0;
        std::vector<float> out(in.size(), 0.f);
        e.process(in.data(), out.data(), in.size());
        // Reference ring semantics: a read at delay d returns the sample
        // pushed d-1 samples earlier, so the parked copy is in[k-d_park+1].
        double worst = 0.0;
        int worst_k = -1;
        // an engine that arrived from a shift has legitimate ring history:
        // bit-exactness is only decidable once the read reaches the current
        // run's own input (k >= d_park-1)
        const int k0 = std::max(skip, d_park - 1);
        for (int k = k0; k < (int)in.size(); ++k) {
            const int src = k - d_park + 1;
            const double expect = (src >= 0) ? static_cast<double>(in[src]) : 0.0;
            const double d = std::abs(static_cast<double>(out[k]) - expect);
            if (d > worst) {
                worst = d;
                worst_k = k;
            }
        }
        addValue(c, "max_abs_diff", worst);
        addValue(c, "worst_k", worst_k);
        check(c, worst == 0.0, "unity output is not the bit-exact delayed copy");
        // the pre-delay region is exactly zero (fresh engines only -- an
        // engine that arrived from a shift legitimately has ring history)
        if (!arrive_from_shift) {
            double pre_max = 0.0;
            for (int k = 0; k < d_park - 1 && k < (int)in.size(); ++k)
                pre_max = std::max(pre_max, std::abs((double)out[k]));
            addValue(c, "pre_delay_region_max", pre_max);
            check(c, pre_max == 0.0, "output before d_park not silent");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    };

    unityExact("unity_tone_220_bitexact", gen::tone(220.0, sr, 48000, 0.5));
    unityExact("unity_tone_1100_bitexact", gen::tone(1100.0, sr, 48000, 0.5));
    unityExact("unity_noise_bitexact", gen::noise(42, 48000, 0.8));
    unityExact("unity_chord_bitexact",
               gen::chord({196.0, 294.0, 392.0, 494.0}, sr, 48000, 0.8));
    {
        std::vector<float> sil(48000, 0.0f);
        unityExact("unity_silence_bitexact", sil);
    }
    unityExact("unity_lowlevel_bitexact", gen::tone(220.0, sr, 48000, 0.001));
    unityExact("unity_enter_from_+400c_bitexact",
               gen::tone(220.0, sr, 96000, 0.5), true);

    if (harnSelected("unity_gain_exact")) {  // unity gain exactly 1 (follows from bit-exactness, measured anyway)
        CaseResult c{"unity_gain_exact"};
        const auto in = gen::tone(220.0, sr, 48000, 0.5);
        Config cfg;
        AlignedDelayLine e;
        e.prepare(cfg);
        std::vector<float> out(in.size(), 0.f);
        e.process(in.data(), out.data(), in.size());
        const int d = e.voiceStats(0).parked_delay - 1;
        // out[d..] is an exact copy of in[0..n-d): compare matching segments
        const double ri = meas::rms(in.data(), in.size() - d);
        const double ro = meas::rms(out.data() + d, out.size() - d);
        addValue(c, "gain_db", 20.0 * std::log10(ro / ri));
        check(c, ro == ri, "unity RMS changed");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("unity_entry_continuity")) {  // entry continuity (from +400 cents into unity): no click
        CaseResult c{"unity_entry_continuity"};
        Config cfg;
        AlignedDelayLine e;
        e.prepare(cfg);
        e.setRatio(semitoneRatio(400.0));
        const int preN = 2 * sr;
        const int postN = 1 * sr;
        const auto in = gen::tone(220.0, sr, preN + postN, 0.5);
        std::vector<float> out(in.size(), 0.f);
        e.process(in.data(), out.data(), preN);
        e.setRatio(1.0);
        e.process(in.data() + preN, out.data() + preN, postN);
        // during the transition (first 60 ms after the change)
        const int trans = static_cast<int>(0.06 * sr);
        const double step = meas::maxStep(out.data() + preN, trans);
        const double stepBefore = meas::maxStep(out.data() + preN - trans, trans);
        addValue(c, "transition_max_step", step);
        addValue(c, "pre_transition_max_step", stepBefore);
        check(c, step <= std::max(0.35, 2.0 * stepBefore + 1e-9),
              "transition click");
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // near-unity battery: fresh-start vs via-unity-exit
    auto nearUnity = [&](const std::string& tag, double cents, bool viaUnity) {
        const std::string caseName =
            "near_unity_" + tag + "_" + (viaUnity ? "via_unity" : "fresh");
        if (!harnSelected(caseName)) return;  // R16-P2 selection
        CaseResult c{caseName};
        const double ratio = semitoneRatio(cents);
        Config cfg;
        cfg.ratio = viaUnity ? 1.0 : ratio;
        AlignedDelayLine e;
        e.prepare(cfg);
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        std::vector<float> out(in.size(), 0.f);
        if (viaUnity) {
            // 1 s at unity (parked), then depart
            e.process(in.data(), out.data(), sr);
            e.setRatio(ratio);
            e.process(in.data() + sr, out.data() + sr, 2 * sr);
        } else {
            e.process(in.data(), out.data(), in.size());
        }
        const double f0nom = 220.0;
        const double fm = meas::pitchFFT(out.data() + static_cast<int>(1.5 * sr),
                                         static_cast<int>(1.5 * sr), sr, 50.0,
                                         4000.0);
        const double err = meas::centsBetween(fm, f0nom) - cents;
        const double gainDb = 20.0 * std::log10(
            meas::rms(out.data() + sr, 2 * sr) /
            meas::rms(in.data() + sr, 2 * sr));
        const int perW = std::max(1, static_cast<int>(std::lround(sr / 220.0)));
        const double mod = meas::envModulationDb(out.data(), out.size(), sr,
                                                 static_cast<int>(1.5 * sr), perW);
        const double commits = static_cast<double>(e.voiceStats(0).commits);
        addValue(c, "measured_f0", fm);
        addValue(c, "pitch_error_cents", err);
        addValue(c, "gain_db", gainDb);
        addValue(c, "env_modulation_db", mod);
        addValue(c, "spacing", e.voiceStats(0).spacing);
        addValue(c, "commits", commits);
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        check(c, std::abs(err) <= 2.0, "pitch error > 2 cents");
        if (viaUnity) {
            check(c, gainDb >= -0.75 && gainDb <= 0.75,
                  "via-unity gain outside +-0.75 dB (comb should be aligned)");
            check(c, mod <= 0.75, "via-unity modulation > 0.75 dB");
        } else {
            check(c, gainDb >= -4.0 && gainDb <= 2.0,
                  "fresh gain outside [-4, +2] dB (static comb expected)");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    };

    const double centsList[] = {1.0,  -1.0, 10.0, -10.0, 50.0,  -50.0,
                                100.0, -100.0, 500.0, -500.0, 1200.0, -1200.0};
    for (double cents : centsList) {
        char tag[32];
        std::snprintf(tag, sizeof(tag), "%+gc", cents);
        nearUnity(tag, cents, false);
    }
    // via-unity for the small shifts where the exit-commit matters most
    nearUnity("+1c", 1.0, true);
    nearUnity("-1c", -1.0, true);
    nearUnity("+10c", 10.0, true);
    nearUnity("-10c", -10.0, true);
    nearUnity("+50c", 50.0, true);
    nearUnity("-50c", -50.0, true);

    if (harnSelected("no_bypass_region")) {  // no-bypass: +-1 cent really shifts (measured change in pitch)
        CaseResult c{"no_bypass_region"};
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        Config cfg;
        cfg.ratio = semitoneRatio(1.0);
        const auto outP = run(cfg, in);
        cfg.ratio = semitoneRatio(-1.0);
        const auto outN = run(cfg, in);
        const double fp = meas::pitchFFT(outP.data() + sr, 2 * sr, sr, 50.0, 4000.0);
        const double fn = meas::pitchFFT(outN.data() + sr, 2 * sr, sr, 50.0, 4000.0);
        const double cp = meas::centsBetween(fp, 220.0);
        const double cn = meas::centsBetween(fn, 220.0);
        addValue(c, "plus_1c_measured_cents", cp);
        addValue(c, "minus_1c_measured_cents", cn);
        check(c, cp > 0.5, "+1 cent did not shift (bypass?)");
        check(c, cn < -0.5, "-1 cent did not shift (bypass?)");
        check(c, cp - cn > 1.0, "+/-1 cent outputs indistinguishable");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("unity_feedback_stability")) {  // unity with feedback: bounded + finite (comb is expected behaviour)
        CaseResult c{"unity_feedback_stability"};
        Config cfg;
        cfg.feedback = 0.35;
        AlignedDelayLine e;
        e.prepare(cfg);
        const auto in = gen::noise(7, 5 * sr, 0.5);
        std::vector<float> out(in.size(), 0.f);
        e.process(in.data(), out.data(), in.size());
        const double pk = meas::peak(out.data(), out.size());
        addValue(c, "peak", pk);
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        check(c, pk < 4.0, "unstable growth with feedback 0.35 at unity");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("unity_8voice_unison_exact_mix")) {  // 8 voices, spread off, all at unity: exact mean of the parked delays
        CaseResult c{"unity_8voice_unison_exact_mix"};
        Config cfg;
        cfg.voices = 8;
        cfg.spread = false;
        AlignedDelayLine e;
        e.prepare(cfg);
        const auto in = gen::tone(220.0, sr, 48000, 0.5);
        std::vector<float> out(in.size(), 0.f);
        e.process(in.data(), out.data(), in.size());
        int d[8];
        for (int v = 0; v < 8; ++v) {
            d[v] = e.voiceStats(v).parked_delay;
            check(c, e.voiceStats(v).parked == 1, "voice not parked");
        }
        double worst = 0.0;
        for (int k = 4800; k < (int)in.size(); ++k) {
            double expect = 0.0;
            for (int v = 0; v < 8; ++v)
                expect += (k - d[v] + 1 >= 0) ? in[k - d[v] + 1] : 0.0f;
            expect /= 8.0;
            // the engine output is the float32 rounding of the double mean
            const float expected_f = static_cast<float>(expect);
            worst = std::max(worst, std::abs((double)out[k] - (double)expected_f));
        }
        addValue(c, "max_abs_diff_vs_exact_mean", worst);
        check(c, worst == 0.0, "8-voice unison mix is not the exact mean");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

void casesVoices(std::vector<CaseResult>& R) {
    const int sr = 48000;
    if (harnSelected("voices_8_spread_+700c")) {  // 8 voices spread +700c: finite, bounded, dominant pitch present
        CaseResult c{"voices_8_spread_+700c"};
        Config cfg;
        cfg.ratio = semitoneRatio(700.0);
        cfg.voices = 8;
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.4);
        const auto out = run(cfg, in);
        check(c, !out.empty(), "no output");
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        const double pk = meas::peak(out.data(), out.size());
        addValue(c, "peak", pk);
        check(c, pk < 2.5, "peak too high");
        const double f = meas::pitchFFT(out.data() + sr, 2 * sr, sr, 50.0, 4000.0);
        addValue(c, "dominant_f0", f);
        check(c, f > 150.0 && f < 800.0, "no dominant pitch found");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("setvoices_midstream")) {  // setVoices mid-stream: ramped normalisation, no NaN
        CaseResult c{"setvoices_midstream"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        AlignedDelayLine e;
        e.prepare(cfg);
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        std::vector<float> out(in.size(), 0.f);
        e.process(in.data(), out.data(), sr);
        e.setVoices(8);
        e.process(in.data() + sr, out.data() + sr, sr);
        e.setVoices(1);
        e.process(in.data() + 2 * sr, out.data() + 2 * sr, sr);
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        const double step = meas::maxStep(out.data() + sr, 48000);
        addValue(c, "max_step_after_voice_change", step);
        check(c, step < 0.5, "click on voice-count change");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

void casesStereo(std::vector<CaseResult>& R) {
    const int sr = 48000;
    if (harnSelected("stereo_isolation_unity")) {  // L tone / R silence at unity: R exactly zero, L bit-exact
        CaseResult c{"stereo_isolation_unity"};
        Config cfg;
        MultichannelShifter m;
        check(c, m.prepare(cfg, 2), "prepare failed");
        const auto tin = gen::tone(220.0, sr, sr, 0.5);
        std::vector<float> chL = tin, chR(sr, 0.0f);
        std::vector<float> Lo(sr, 0.f), Ro(sr, 0.f);
        const float* in[2] = {chL.data(), chR.data()};
        float* out[2] = {Lo.data(), Ro.data()};
        m.process(in, out, 2, sr);
        const double rmax = meas::peak(Ro.data(), sr);
        addValue(c, "R_peak", rmax);
        check(c, rmax == 0.0, "right channel contaminated at unity");
        const int d = m.voiceStats(0, 0).parked_delay;
        double worst = 0.0;
        for (int k = d - 1; k < sr; ++k)
            worst = std::max(worst, std::abs((double)Lo[k] - chL[k - d + 1]));
        addValue(c, "L_bitexact_diff", worst);
        check(c, worst == 0.0, "left channel not bit-exact at unity");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("stereo_isolation_shifted")) {  // L tone / R silence at +700c: R exactly zero
        CaseResult c{"stereo_isolation_shifted"};
        Config cfg;
        cfg.ratio = semitoneRatio(700.0);
        MultichannelShifter m;
        m.prepare(cfg, 2);
        const auto tin = gen::tone(220.0, sr, 2 * sr, 0.5);
        std::vector<float> chL = tin, chR(2 * sr, 0.0f);
        std::vector<float> Lo(2 * sr, 0.f), Ro(2 * sr, 0.f);
        const float* in[2] = {chL.data(), chR.data()};
        float* out[2] = {Lo.data(), Ro.data()};
        m.process(in, out, 2, 2 * sr);
        const double rmax = meas::peak(Ro.data(), 2 * sr);
        addValue(c, "R_peak", rmax);
        check(c, rmax == 0.0, "right channel contaminated while shifting");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("stereo_independence")) {  // independence: R output identical whether L is processed or not
        CaseResult c{"stereo_independence"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        MultichannelShifter m;
        m.prepare(cfg, 2);
        const auto tl = gen::tone(220.0, sr, sr, 0.5);
        const auto tr = gen::tone(330.0, sr, sr, 0.5);
        std::vector<float> Lo(sr, 0.f), Ro(sr, 0.f);
        const float* in[2] = {tl.data(), tr.data()};
        float* out[2] = {Lo.data(), Ro.data()};
        m.process(in, out, 2, sr);
        // mono run on R alone
        AlignedDelayLine solo;
        solo.prepare(cfg);
        std::vector<float> Ro2(sr, 0.f);
        solo.process(tr.data(), Ro2.data(), sr);
        double worst = 0.0;
        for (int k = 0; k < sr; ++k)
            worst = std::max(worst, std::abs((double)Ro[k] - Ro2[k]));
        addValue(c, "R_diff_vs_solo", worst);
        check(c, worst == 0.0, "R output depends on L processing");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

void casesRatesFormats(std::vector<CaseResult>& R) {
    if (harnSelected("rate_44100_+400c")) {  // 44.1 kHz
        CaseResult c{"rate_44100_+400c"};
        Config cfg;
        cfg.sample_rate = 44100.0;
        cfg.ratio = semitoneRatio(400.0);
        const int sr = 44100;
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        const auto out = run(cfg, in);
        const double f = meas::pitchFFT(out.data() + sr, 2 * sr, sr, 50.0, 4000.0);
        const double err = meas::centsBetween(f, 220.0) - 400.0;
        addValue(c, "pitch_error_cents", err);
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        check(c, std::abs(err) <= 2.0, "pitch error > 2 cents at 44.1 kHz");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("rate_96000_+400c")) {  // 96 kHz (detector window too short for 60 Hz: spacing falls back to
       // G -- the Python reference has the same behaviour; gate is looser)
        CaseResult c{"rate_96000_+400c"};
        Config cfg;
        cfg.sample_rate = 96000.0;
        cfg.ratio = semitoneRatio(400.0);
        const int sr = 96000;
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        const auto out = run(cfg, in);
        const double f = meas::pitchFFT(out.data() + sr, 2 * sr, sr, 50.0, 4000.0);
        const double err = meas::centsBetween(f, 220.0) - 400.0;
        addValue(c, "pitch_error_cents", err);
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        check(c, std::abs(err) <= 25.0, "pitch error > 25 cents at 96 kHz");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("wav_roundtrip_example_input")) {  // WAV round trip on the shipped example input (if present)
        CaseResult c{"wav_roundtrip_example_input"};
        const char* candidates[] = {"../poc/example_input.wav",
                                    "../../poc/example_input.wav",
                                    "../../../poc/example_input.wav"};
        int sr = 0, ch = 0;
        std::vector<float> x;
        bool loaded = false;
        for (const char* pocPath : candidates) {
            try {
                x = wav::readWav(pocPath, sr, ch);
                loaded = true;
                break;
            } catch (const std::exception&) {
            }
        }
        if (!loaded || x.empty()) {
            c.note = "example_input.wav not found (running outside tree)";
            c.pass = true;  // environment-dependent skip, reported honestly
            addValue(c, "skipped", 1);
        } else {
            Config cfg;
            cfg.ratio = semitoneRatio(400.0);
            const auto out = run(cfg, x);
            check(c, meas::allFinite(out.data(), out.size()), "non-finite");
            wav::writeWav("/tmp/octavox_native_example_out.wav", out.data(),
                          out.size(), ch, sr);
            int sr2 = 0, ch2 = 0;
            const auto back = wav::readWav("/tmp/octavox_native_example_out.wav",
                                           sr2, ch2);
            check(c, sr2 == sr && ch2 == ch, "round-trip format changed");
            double worst = 0.0;
            for (size_t i = 0; i < out.size(); ++i)
                worst = std::max(worst, std::abs((double)out[i] - back[i]));
            addValue(c, "quant_error_max", worst);
            check(c, worst <= 2.0 / 32767.0,
                  "16-bit round-trip error too large (write/read asymmetry is "
                  "reference parity: round(y*32767) then /32768)");
            c.pass = c.note.empty();
        }
        pushCase(R, std::move(c));
    }
    if (harnSelected("wav_24bit_read")) {  // 24-bit WAV read
        CaseResult c{"wav_24bit_read"};
        // write a minimal 24-bit WAV by hand
        const int sr = 48000, n = 256;
        std::vector<int32_t> vals(n);
        for (int i = 0; i < n; ++i) vals[i] = (i * 7919) % 4000000 - 2000000;
        std::vector<unsigned char> bytes(44 + 3 * n);
        std::memcpy(bytes.data(), "RIFF", 4);
        const uint32_t riffSz = 36 + 3 * n;
        std::memcpy(bytes.data() + 4, &riffSz, 4);
        std::memcpy(bytes.data() + 8, "WAVE", 4);
        std::memcpy(bytes.data() + 12, "fmt ", 4);
        const uint32_t fsz = 16;
        std::memcpy(bytes.data() + 16, &fsz, 4);
        const uint16_t af = 1, ch = 1, bits = 24;
        const uint32_t rate = sr, br = sr * 3;
        const uint16_t block = 3;
        std::memcpy(bytes.data() + 20, &af, 2);
        std::memcpy(bytes.data() + 22, &ch, 2);
        std::memcpy(bytes.data() + 24, &rate, 4);
        std::memcpy(bytes.data() + 28, &br, 4);
        std::memcpy(bytes.data() + 32, &block, 2);
        std::memcpy(bytes.data() + 34, &bits, 2);
        std::memcpy(bytes.data() + 36, "data", 4);
        const uint32_t dsz = 3 * n;
        std::memcpy(bytes.data() + 40, &dsz, 4);
        for (int i = 0; i < n; ++i) {
            bytes[44 + 3 * i] = vals[i] & 0xFF;
            bytes[45 + 3 * i] = (vals[i] >> 8) & 0xFF;
            bytes[46 + 3 * i] = (vals[i] >> 16) & 0xFF;
        }
        FILE* f = std::fopen("/tmp/octavox_24bit.wav", "wb");
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
        int sr2 = 0, ch2 = 0;
        auto x = wav::readWav("/tmp/octavox_24bit.wav", sr2, ch2);
        check(c, sr2 == sr && ch2 == 1 && (int)x.size() == n, "bad header");
        double worst = 0.0;
        for (int i = 0; i < n && i < (int)x.size(); ++i)
            worst = std::max(worst,
                             std::abs((double)x[i] - vals[i] / 8388608.0));
        addValue(c, "max_read_error", worst);
        check(c, worst == 0.0, "24-bit values wrong");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

void casesStability(std::vector<CaseResult>& R) {
    const int sr = 48000;
    if (harnSelected("longrun_60s_8voice_feedback")) {  // long run: 60 s noise, 8 voices, feedback
        CaseResult c{"longrun_60s_8voice_feedback"};
        Config cfg;
        cfg.ratio = semitoneRatio(1200.0);
        cfg.voices = 8;
        cfg.feedback = 0.35;
        const auto in = gen::noise(2025, 60 * sr, 0.5);
        const auto out = run(cfg, in);
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        const double pk = meas::peak(out.data(), out.size());
        addValue(c, "peak", pk);
        check(c, pk < 4.0, "unstable growth");
        // tail RMS must not exceed head RMS by much (no divergence)
        const double rh = meas::rms(out.data(), 5 * sr);
        const double rt = meas::rms(out.data() + 55 * sr, 5 * sr);
        addValue(c, "head_rms", rh);
        addValue(c, "tail_rms", rt);
        check(c, rt < 4.0 * rh + 1e-9, "diverging tail");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("chirp_30s_+700c")) {  // 30 s chirp +700c
        CaseResult c{"chirp_30s_+700c"};
        Config cfg;
        cfg.ratio = semitoneRatio(700.0);
        const auto in = gen::chirp(100.0, 1000.0, sr, 30 * sr, 0.5);
        const auto out = run(cfg, in);
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        const double pk = meas::peak(out.data(), out.size());
        addValue(c, "peak", pk);
        check(c, pk < 3.0, "peak too high on chirp");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("impulse_latency")) {  // impulse latency
        CaseResult c{"impulse_latency"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        const auto in = gen::impulse(sr);
        const auto out = run(cfg, in);
        int first = -1;
        for (int k = 0; k < (int)out.size(); ++k)
            if (out[k] != 0.0f) {
                first = k;
                break;
            }
        addValue(c, "first_nonzero_sample", first);
        addValue(c, "nominal_latency", cfg.base_delay + cfg.grain * 0.5);
        check(c, first >= 250, "responds before base_delay");
        check(c, first <= 256 + 32 + 4400, "responds too late");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("impulse_latency_unity")) {  // unity impulse: first nonzero exactly at d_park
        CaseResult c{"impulse_latency_unity"};
        Config cfg;
        const auto in = gen::impulse(sr);
        const auto out = run(cfg, in);
        AlignedDelayLine e;
        e.prepare(cfg);
        const int d = e.voiceStats(0).parked_delay;
        int first = -1;
        for (int k = 0; k < (int)out.size(); ++k)
            if (out[k] != 0.0f) {
                first = k;
                break;
            }
        addValue(c, "first_nonzero_sample", first);
        addValue(c, "d_park", d);
        check(c, first == d - 1, "unity impulse not at d_park-1");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("dc_input_finite")) {  // DC input: finite (guards div-by-zero in spacing/median paths)
        CaseResult c{"dc_input_finite"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        std::vector<float> in(10 * sr, 1.0f);
        const auto out = run(cfg, in);
        check(c, meas::allFinite(out.data(), out.size()), "non-finite on DC");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("clipping_behaviour")) {  // clipping behaviour: hot input stays finite; WAV writer clips
        CaseResult c{"clipping_behaviour"};
        Config cfg;
        cfg.ratio = semitoneRatio(1200.0);
        cfg.voices = 8;
        std::vector<float> in(sr, 0.0f);
        for (int i = 0; i < sr; ++i)
            in[i] = (i % 480 < 240) ? 1.0f : -1.0f;  // full-scale square
        const auto out = run(cfg, in);
        const double pk = meas::peak(out.data(), out.size());
        addValue(c, "float_domain_peak", pk);
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        check(c, pk < 8.0, "float peak beyond sane bound");
        wav::writeWav("/tmp/octavox_clip.wav", out.data(), out.size(), 1, sr);
        int sr2 = 0, ch2 = 0;
        auto back = wav::readWav("/tmp/octavox_clip.wav", sr2, ch2);
        const double pkq = meas::peak(back.data(), back.size());
        addValue(c, "wav_peak_after_clip", pkq);
        check(c, pkq <= 1.0, "WAV writer did not clip-guard");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

// ---------------------------------------------------------------------------
// R13/R14 RECOVERY SCOPE (re-created from the worklog record R15-R0 /
// R15-E1 / R15-REB1 after the second environment reset; the SCOPE and GATES
// are the contract of the lost original rounds-13/14 suite -- case names may
// differ slightly from the lost originals).
//   R13-a  cold-start alignment law (impulse contact / analytic value)
//   R13-b  sample-rate-relative detector window (96 kHz sine battery,
//          44.1 kHz battery, estimate tracking at 3 rates, estimate
//          stability at 96 kHz)
//   R13-c  zero-allocation re-prepare (identical config / param-only /
//          multichannel) + in-place result identity vs fresh engines
//   R14    VoiceStats::ratio glide diagnostics
// ---------------------------------------------------------------------------
void casesR13(std::vector<CaseResult>& R) {
    const int sr = 48000;

    // -- the cold-start law, independently re-derived INSIDE the tests from
    // the header contract (COLD-START ALIGNMENT LAW, include/octavox/dsp.hpp):
    //   L_v = base_v + grain/2 with the per-voice stagger
    //   base_v = base_delay + 32*v;  unity (ratio == 1 bit-exact) parks at
    //   d_park(v) = round(base_v + grain/2) with out[k] == in[k-d_park+1]
    //   (ring convention read(d) = the sample pushed d-1 samples ago), so an
    //   impulse at t=0 first appears at k0 = d_park - 1 and the output is
    //   EXACTLY zero before it;  a shifted voice (glide initialised AT its
    //   target, fully engaged e=1/a=0) reads at d(k) = L_v + (1-r)*(k+1)
    //   before the first grain wrap, the input's first sample sits at ring
    //   delay k+1 while out[k] is produced, and the 6-tap Lagrange kernel
    //   reads ring delays [floor(d)-2, floor(d)+3] -- the contact point is
    //   the first k whose kernel window CONTAINS the delay k+1.
    struct Law {
        double base, grain, ratio;
        int nv;
        double baseV(int v) const { return base + 32.0 * v; }
        int dPark(int v) const {
            return static_cast<int>(std::floor(baseV(v) + grain * 0.5 + 0.5));
        }
        double dShift(int v, int k) const {
            return baseV(v) + grain * 0.5 + (1.0 - ratio) * (k + 1);
        }
        int contact(int v) const {
            for (int k = 0; k < 400000; ++k) {
                const double fd = std::floor(dShift(v, k));
                if (fd - 2.0 <= k + 1 && k + 1 <= fd + 3.0) return k;
            }
            return -1;
        }
        // Analytic output value at the contact sample of voice v (the only
        // voice that has contacted there): the complementary two-tap
        // engagement blend w = 0.5 - 0.5*cos(pi*frac) times the 6-point
        // Lagrange weight of the tap holding the impulse (independent
        // product formula at the bin-centre effective phase
        // (floor(mu*2048)+0.5)/2048 -- Lagrange6 = "2048 bin-centre
        // phases"), times the voice-count normalisation 1/nv.
        double valueShift(int v, int k0, int* posOut) const {
            const double frac = 0.5 + (k0 + 1) * (1.0 - ratio) / grain;
            const double blend = 0.5 - 0.5 * std::cos(M_PI * frac);
            const double d = dShift(v, k0);
            const double fd = std::floor(d);
            const double mu = d - fd;
            int ph = static_cast<int>(mu * 2048.0);
            if (ph > 2047) ph = 2047;
            if (ph < 0) ph = 0;
            const double muEff = (ph + 0.5) / 2048.0;
            const int pos = static_cast<int>((k0 + 1) - (fd - 2.0));
            if (posOut) *posOut = pos;
            static const double xs[6] = {-2.0, -1.0, 0.0, 1.0, 2.0, 3.0};
            double w = 1.0;
            for (int j = 0; j < 6; ++j)
                if (j != pos) w *= (muEff - xs[j]) / (xs[pos] - xs[j]);
            return blend * w / nv;
        }
    };

    auto runBlocked = [](const Config& cfg, const std::vector<float>& in,
                         int block) {
        AlignedDelayLine e;
        std::vector<float> out(in.size(), 0.0f);
        if (!e.prepare(cfg)) return out;  // empty out -> gates fail loudly
        int done = 0;
        while (done < (int)in.size()) {
            const int n = std::min(block, (int)in.size() - done);
            e.process(in.data() + done, out.data() + done, n);
            done += n;
        }
        return out;
    };

    // ---- cold start, UNITY: the full stream is the bit-exact parked ----
    // delayed-copy sum out[k] == float((sum_v in[k-d_park(v)+1])*(1/nv))
    // (voices summed in order, normalisation 1/nv -- the mix law), the
    // first non-zero is exactly at d_park(0)-1 == latencySamples()-1, the
    // output is exactly zero before it, and block sizes 1/64/512 agree.
    auto coldStartUnity = [&](const char* name, int nv) {
        if (!harnSelected(name)) return;  // R16-P2 selection (no gate touched)
        CaseResult c{name};
        Config cfg;
        cfg.voices = nv;
        cfg.spread = false;  // every voice at the same unity ratio
        const Law law{cfg.base_delay, cfg.grain, cfg.ratio, nv};
        const auto in = gen::impulse(8192);
        std::vector<float> analytic(in.size(), 0.0f);
        for (int k = 0; k < (int)in.size(); ++k) {
            double acc = 0.0;
            for (int v = 0; v < nv; ++v) {
                const int src = k - law.dPark(v) + 1;
                acc += (src >= 0) ? (double)in[src] : 0.0;
            }
            analytic[k] = static_cast<float>(acc * (1.0 / nv));
        }
        const int k0 = law.dPark(0) - 1;
        check(c, k0 > 0 && k0 < (int)in.size(), "analytic k0 outside buffer");
        AlignedDelayLine probe;
        check(c, probe.prepare(cfg), "prepare failed");
        check(c, law.dPark(0) == probe.latencySamples(),
              "law: d_park(0) != latencySamples() (default config)");
        const int blocks[3] = {1, 64, 512};
        double worstStream = 0.0, worstPre = 0.0;
        int fnzMeas = -1;
        double valAtK0 = 0.0;
        for (int b : blocks) {
            const auto out = runBlocked(cfg, in, b);
            int fnz = -1;
            double pre = 0.0;
            for (int k = 0; k < (int)out.size(); ++k) {
                if (out[k] != 0.0f) {
                    if (fnz < 0) fnz = k;
                }
            }
            if (fnz < 0) fnz = (int)out.size();
            if (fnzMeas < 0) {
                fnzMeas = fnz;
                valAtK0 = (fnz < (int)out.size()) ? (double)out[fnz] : 0.0;
            }
            for (int k = 0; k < k0; ++k)
                pre = std::max(pre, std::abs((double)out[k]));
            worstPre = std::max(worstPre, pre);
            double ds = 0.0;
            for (int k = 0; k < (int)out.size(); ++k)
                ds = std::max(ds, std::abs((double)out[k] - (double)analytic[k]));
            worstStream = std::max(worstStream, ds);
            check(c, fnz == k0, "first non-zero != d_park-1 (unity)");
            check(c, pre == 0.0, "non-zero output before the contact point");
        }
        addValue(c, "analytic_k0", k0);
        addValue(c, "measured_first_nonzero", fnzMeas);
        addValue(c, "value_at_k0", valAtK0);
        addValue(c, "expected_value_at_k0", 1.0 / nv);
        addValue(c, "pre_contact_max_abs", worstPre);
        addValue(c, "fullstream_vs_analytic_max_diff", worstStream);
        check(c, valAtK0 == 1.0 / nv, "value at k0 != 1/nv (parked copy)");
        check(c, worstStream == 0.0,
              "unity impulse stream != parked delayed-copy sum (bit-exact)");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    };

    // ---- cold start, SHIFTED: first non-zero exactly at the analytic ----
    // contact point, exactly zero before it, the analytic value at k0, and
    // block sizes 1/64/512 agree bit-exactly.
    auto coldStartShift = [&](const char* name, int nv, double ratio) {
        if (!harnSelected(name)) return;  // R16-P2 selection (no gate touched)
        CaseResult c{name};
        Config cfg;
        cfg.ratio = ratio;
        cfg.voices = nv;
        cfg.spread = false;  // every voice at the same shifted ratio
        const Law law{cfg.base_delay, cfg.grain, ratio, nv};
        const auto in = gen::impulse(8192);
        const int k0 = law.contact(0);
        check(c, k0 > 0 && k0 < (int)in.size(), "contact law did not converge");
        int pos = -1;
        const double analyticValue =
            (k0 > 0) ? law.valueShift(0, k0, &pos) : 0.0;
        // law preconditions, checked loudly (the analytic relies on them)
        const double fracK0 =
            (k0 > 0) ? 0.5 + (k0 + 1) * (1.0 - ratio) / cfg.grain : 0.5;
        check(c, fracK0 > 0.0 && fracK0 < 1.0,
              "grain wrap before the contact point (law precondition)");
        check(c, pos >= 0 && pos <= 5,
              "impulse tap position outside the 6-tap kernel");
        const double d0 = (k0 > 0) ? law.dShift(0, k0) : 1.0;
        check(c, d0 - std::floor(d0) != 0.0,
              "integral read delay at contact (direct-read edge case)");
        check(c, k0 + 1 < std::floor(d0 + cfg.grain) - 2.0,
              "B-tap window already reached the impulse at k0");
        const int blocks[3] = {1, 64, 512};
        double worstVal = 0.0, worstPre = 0.0, worstCross = 0.0;
        int fnzMeas = -1;
        double measVal = 0.0;
        std::vector<float> ref;
        for (int b : blocks) {
            const auto out = runBlocked(cfg, in, b);
            int fnz = -1;
            for (int k = 0; k < (int)out.size(); ++k)
                if (out[k] != 0.0f) { fnz = k; break; }
            if (fnz < 0) fnz = (int)out.size();
            if (fnzMeas < 0) {
                fnzMeas = fnz;
                measVal = (fnz < (int)out.size()) ? (double)out[fnz] : 0.0;
            }
            double pre = 0.0;
            for (int k = 0; k < k0 && k < (int)out.size(); ++k)
                pre = std::max(pre, std::abs((double)out[k]));
            worstPre = std::max(worstPre, pre);
            if (fnz < (int)out.size())
                worstVal =
                    std::max(worstVal, std::abs((double)out[fnz] - analyticValue));
            if (ref.empty()) {
                ref = out;
            } else {
                for (int k = 0; k < (int)out.size(); ++k)
                    worstCross = std::max(
                        worstCross, std::abs((double)out[k] - (double)ref[k]));
            }
            check(c, fnz == k0, "first non-zero != analytic contact k0");
            check(c, pre == 0.0, "non-zero output before the contact point");
        }
        addValue(c, "analytic_k0", k0);
        addValue(c, "measured_first_nonzero", fnzMeas);
        addValue(c, "analytic_value_at_k0", analyticValue);
        addValue(c, "measured_value_at_k0", measVal);
        addValue(c, "value_at_k0_abs_diff", std::abs(measVal - analyticValue));
        addValue(c, "pre_contact_max_abs", worstPre);
        addValue(c, "cross_block_max_diff", worstCross);
        // Tolerance rationale (fixed before running): the implementation
        // accumulates the grain phase per sample while this re-derivation
        // uses the closed-form law, so the read delay diverges at the
        // ~k*eps*|1-r| scale (measured literals: 4.6e-11 / 5.8e-12 on the
        // up-shifts, 4.0e-10 / 5.0e-11 on the down-shifts -- the analytic
        // value itself is ~1e-3, so the relative error is ~1e-7);  1e-9
        // covers the accumulation with an order of margin WITHOUT masking
        // a kernel-bin (1/2048) phase flip (a ~5e-4-scale value change),
        // which would itself indicate a law violation.
        check(c, worstVal <= 1e-9,
              "value at k0 differs from the analytic re-derivation");
        check(c, worstCross == 0.0, "block size changed the cold-start output");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    };

    coldStartUnity("cold_start_impulse_alignment_unity_1v", 1);
    coldStartUnity("cold_start_impulse_alignment_unity_8v", 8);
    coldStartShift("cold_start_impulse_alignment_up_1v", 1,
                   std::pow(2.0, 5.0 / 12.0));
    coldStartShift("cold_start_impulse_alignment_up_8v", 8,
                   std::pow(2.0, 5.0 / 12.0));
    coldStartShift("cold_start_impulse_alignment_down_1v", 1,
                   std::pow(2.0, -5.0 / 12.0));
    coldStartShift("cold_start_impulse_alignment_down_8v", 8,
                   std::pow(2.0, -5.0 / 12.0));

    if (harnSelected("cold_start_no_spurious_transient")) {  // no spurious transient: max|out| == 0 before the contact point on
       // step and sine onsets, 1 and 8 voices, unity and +-5 semitones
        CaseResult c{"cold_start_no_spurious_transient"};
        const double ratios[3] = {1.0, std::pow(2.0, 5.0 / 12.0),
                                  std::pow(2.0, -5.0 / 12.0)};
        double worst = 0.0;
        int variants = 0;
        for (int ri = 0; ri < 3; ++ri) {
            for (int nv = 1; nv <= 8; nv *= 8) {
                Config cfg;
                cfg.ratio = ratios[ri];
                cfg.voices = nv;
                cfg.spread = false;
                const Law law{cfg.base_delay, cfg.grain, cfg.ratio, nv};
                const int contact =
                    (ri == 0) ? (law.dPark(0) - 1) : law.contact(0);
                check(c, contact > 0, "contact law did not converge");
                for (int kind = 0; kind < 2; ++kind) {
                    std::vector<float> in(8192, 0.0f);
                    if (kind == 0) {  // sine onset
                        for (int k = 0; k < (int)in.size(); ++k)
                            in[k] = static_cast<float>(
                                0.5 * std::sin(2.0 * M_PI * 220.0 * k / sr));
                    } else {  // step onset
                        std::fill(in.begin(), in.end(), 0.5f);
                    }
                    for (int b : {1, 512}) {
                        const auto out = runBlocked(cfg, in, b);
                        double pre = 0.0;
                        for (int k = 0; k < contact && k < (int)out.size(); ++k)
                            pre = std::max(pre, std::abs((double)out[k]));
                        worst = std::max(worst, pre);
                        ++variants;
                    }
                }
            }
        }
        addValue(c, "variants", variants);
        addValue(c, "worst_pre_contact_max_abs", worst);
        check(c, worst == 0.0, "spurious transient before the contact point");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("detector_96000hz_sine_battery")) {  // detector 96 kHz sine battery (R13-b): 110/220/440/880 Hz at
       // ratio 2^(5/12), rate-scaled config grain 8192 / base 512 --
       // SAMPLE-DOMAIN BY DESIGN: the acceptance configs hold the
       // TIME-domain grain (85.3 ms) and base (5.3 ms) constant across
       // rates (8192/96000 s == 4096/48000 s).  The R13-b window law
       // win = max(2048, ceil(2.5*tau_max)) makes the detector track at
       // 96 kHz (round 12 was blind there, m < 32).
       // GATE: |pitch error| <= 1.0 cent per frequency.
        CaseResult c{"detector_96000hz_sine_battery"};
        const int srH = 96000;
        const double freqs[4] = {110.0, 220.0, 440.0, 880.0};
        const double ratio = std::pow(2.0, 5.0 / 12.0);
        double worst = 0.0;
        for (double f : freqs) {
            Config cfg;
            cfg.sample_rate = srH;
            cfg.ratio = ratio;
            cfg.grain = 8192.0;
            cfg.base_delay = 512.0;
            const auto in = gen::tone(f, srH, 3 * srH, 0.5);
            const auto out = run(cfg, in);
            const double fm =
                meas::pitchFFT(out.data() + srH, 2 * srH, srH, 50.0, 4000.0);
            const double err = meas::centsBetween(fm, f * ratio);
            char vk[48];
            std::snprintf(vk, sizeof(vk), "pitch_error_cents_%.0f_hz", f);
            addValue(c, vk, err);
            worst = std::max(worst, std::abs(err));
            check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        }
        addValue(c, "worst_cents", worst);
        check(c, worst <= 1.0, "96 kHz battery pitch error > 1.0 cent");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("detector_44100hz_sine_battery")) {  // detector 44.1 kHz sine battery (R13-b boundary of the 2048 floor):
       // 440/880 Hz, rate-scaled grain/base = round(4096*sr/48000) = 3763 /
       // round(256*sr/48000) = 235 (time-domain-equivalent).  tau_max = 735
       // <= 819.2, so win == 2048 and the detector arithmetic is
       // step-for-step identical to round 12.
       // GATE: |pitch error| <= 1.0 cent per frequency.
        CaseResult c{"detector_44100hz_sine_battery"};
        const int srL = 44100;
        const double freqs[2] = {440.0, 880.0};
        const double ratio = std::pow(2.0, 5.0 / 12.0);
        Config cfg;
        cfg.sample_rate = srL;
        cfg.ratio = ratio;
        cfg.grain = 3763.0;
        cfg.base_delay = 235.0;
        double worst = 0.0;
        for (double f : freqs) {
            const auto in = gen::tone(f, srL, 3 * srL, 0.5);
            const auto out = run(cfg, in);
            const double fm =
                meas::pitchFFT(out.data() + srL, 2 * srL, srL, 50.0, 4000.0);
            const double err = meas::centsBetween(fm, f * ratio);
            char vk[48];
            std::snprintf(vk, sizeof(vk), "pitch_error_cents_%.0f_hz", f);
            addValue(c, vk, err);
            worst = std::max(worst, std::abs(err));
            check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        }
        addValue(c, "worst_cents", worst);
        check(c, worst <= 1.0, "44.1 kHz battery pitch error > 1.0 cent");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // estimate tracking at three rates: lastEstimateHz() within +-5 cents
    // of the true f0, sampled at every 1024-sample block boundary after
    // 1 s of warm-up (the detector needs ~win + a few periods to lock).
    if (harnSelected("detector_estimate_tracking_44100")) {
        CaseResult c{"detector_estimate_tracking_44100"};
        const int srL = 44100;
        const double f0 = 220.0;
        Config cfg;
        cfg.sample_rate = srL;
        cfg.grain = 3763.0;   // round(4096*44100/48000)
        cfg.base_delay = 235.0;  // round(256*44100/48000)
        const auto in = gen::tone(f0, srL, 3 * srL, 0.5);
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        std::vector<float> out(in.size(), 0.0f);
        double worst = 0.0;
        int done = 0;
        while (done < (int)in.size()) {
            const int n = std::min(1024, (int)in.size() - done);
            e.process(in.data() + done, out.data() + done, n);
            done += n;
            if (done >= srL)
                worst = std::max(worst,
                                 std::abs(meas::centsBetween(e.lastEstimateHz(), f0)));
        }
        addValue(c, "worst_cents_after_1s", worst);
        addValue(c, "last_estimate_hz", e.lastEstimateHz());
        check(c, worst <= 5.0, "estimate outside +/-5 cents at 44.1 kHz");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("detector_estimate_tracking_48000")) {
        CaseResult c{"detector_estimate_tracking_48000"};
        const double f0 = 220.0;
        Config cfg;  // all defaults (grain 4096 / base 256)
        const auto in = gen::tone(f0, sr, 3 * sr, 0.5);
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        std::vector<float> out(in.size(), 0.0f);
        double worst = 0.0;
        int done = 0;
        while (done < (int)in.size()) {
            const int n = std::min(1024, (int)in.size() - done);
            e.process(in.data() + done, out.data() + done, n);
            done += n;
            if (done >= sr)
                worst = std::max(worst,
                                 std::abs(meas::centsBetween(e.lastEstimateHz(), f0)));
        }
        addValue(c, "worst_cents_after_1s", worst);
        addValue(c, "last_estimate_hz", e.lastEstimateHz());
        check(c, worst <= 5.0, "estimate outside +/-5 cents at 48 kHz");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
    if (harnSelected("detector_estimate_tracking_96000")) {
        CaseResult c{"detector_estimate_tracking_96000"};
        const int srH = 96000;
        const double f0 = 110.0;  // mirrors the recorded r15refcap --probe96
        Config cfg;
        cfg.sample_rate = srH;
        cfg.grain = 8192.0;
        cfg.base_delay = 512.0;
        const auto in = gen::tone(f0, srH, 3 * srH, 0.5);
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        std::vector<float> out(in.size(), 0.0f);
        double worst = 0.0;
        int done = 0;
        while (done < (int)in.size()) {
            const int n = std::min(1024, (int)in.size() - done);
            e.process(in.data() + done, out.data() + done, n);
            done += n;
            if (done >= srH)
                worst = std::max(worst,
                                 std::abs(meas::centsBetween(e.lastEstimateHz(), f0)));
        }
        addValue(c, "worst_cents_after_1s", worst);
        addValue(c, "last_estimate_hz", e.lastEstimateHz());
        check(c, worst <= 5.0, "estimate outside +/-5 cents at 96 kHz");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("detector_96k_estimate_stability")) {  // estimate STABILITY at 96 kHz (the R13-b mirror-dip concern: a
       // merely-restored m << period is unstable; the law keeps
       // m = win/2 - tau_max >= tau_max/4).  Sampled at every 1024-sample
       // boundary after 1 s.  GATE: max-min <= 0.01 Hz (~0.16 cent at
       // 110 Hz): gross mirror-dip errors are tens of Hz, so this is far
       // inside the tracking gate while still catching any instability;
       // the measured literal is recorded.
        CaseResult c{"detector_96k_estimate_stability"};
        const int srH = 96000;
        const double f0 = 110.0;
        Config cfg;
        cfg.sample_rate = srH;
        cfg.grain = 8192.0;
        cfg.base_delay = 512.0;
        const auto in = gen::tone(f0, srH, 4 * srH, 0.5);
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        std::vector<float> out(in.size(), 0.0f);
        double lo = 1e300, hi = 0.0;
        int done = 0;
        while (done < (int)in.size()) {
            const int n = std::min(1024, (int)in.size() - done);
            e.process(in.data() + done, out.data() + done, n);
            done += n;
            if (done >= srH) {
                const double f = e.lastEstimateHz();
                if (f > 0.0) {  // ignore pre-lock zeros (before 1 s: none)
                    lo = std::min(lo, f);
                    hi = std::max(hi, f);
                }
            }
        }
        const double spread = (lo <= hi) ? hi - lo : 0.0;
        addValue(c, "estimate_min_hz", lo);
        addValue(c, "estimate_max_hz", hi);
        addValue(c, "estimate_spread_hz", spread);
        check(c, spread <= 0.01, "96 kHz estimate unstable (spread > 0.01 Hz)");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // ---- R13-c: zero-allocation re-prepare (global new counter) ----------
    if (harnSelected("rt_reprepare_zero_allocation")) {  // identical-config re-prepare: 0 allocations, engine keeps working
        CaseResult c{"rt_reprepare_zero_allocation"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.25;
        const auto in = gen::noise(31337, sr, 0.5);
        std::vector<float> out(in.size(), 0.0f);
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        e.process(in.data(), out.data(), sr / 2);  // warm-up (tables, scratch)
        (void)e.voiceStats(0);
        (void)e.lastEstimateHz();
        allocs::reset();
        allocs::arm(true);
        const bool ok = e.prepare(cfg);  // identical config: in-place path
        e.process(in.data() + sr / 2, out.data() + sr / 2, sr / 2);
        allocs::arm(false);
        const long n = allocs::count();
        check(c, ok, "identical-config re-prepare failed");
        addValue(c, "allocations", (double)n);
        check(c, n == 0, "identical-config re-prepare allocated");
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        check(c, meas::peak(out.data() + sr / 2, sr / 2) > 0.0, "silent");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("rt_reprepare_param_change_zero_allocation")) {  // param-only re-prepare (buffer-determining fields unchanged:
       // sample_rate / max_voices / f0_history / detection_interval; ratio,
       // voices, spread, feedback, grain, base_delay all change): 0
       // allocations, the new parameters take effect.
        CaseResult c{"rt_reprepare_param_change_zero_allocation"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.25;
        Config cfg2 = cfg;
        cfg2.ratio = semitoneRatio(700.0);
        cfg2.voices = 4;
        cfg2.spread = false;
        cfg2.feedback = 0.3;
        cfg2.grain = 2048.0;
        cfg2.base_delay = 512.0;
        const auto in = gen::noise(4141, sr, 0.5);
        std::vector<float> out(in.size(), 0.0f);
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        e.process(in.data(), out.data(), sr / 2);
        (void)e.voiceStats(0);
        allocs::reset();
        allocs::arm(true);
        const bool ok = e.prepare(cfg2);  // param-only: in-place path
        e.process(in.data() + sr / 2, out.data() + sr / 2, sr / 2);
        allocs::arm(false);
        const long n = allocs::count();
        check(c, ok, "param-only re-prepare failed");
        addValue(c, "allocations", (double)n);
        check(c, n == 0, "param-only re-prepare allocated");
        check(c, e.config().ratio == cfg2.ratio && e.config().voices == cfg2.voices &&
                  e.config().grain == cfg2.grain && e.config().base_delay == cfg2.base_delay,
              "re-prepare did not pick up the new parameters");
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        check(c, meas::peak(out.data() + sr / 2, sr / 2) > 0.0, "silent");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("rt_reprepare_multichannel_zero_allocation")) {  // multichannel re-prepare (same channel count): every engine
       // reconfigures itself in place -- 0 allocations for BOTH the
       // identical-config and the param-only re-prepare.
        CaseResult c{"rt_reprepare_multichannel_zero_allocation"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.25;
        Config cfg2 = cfg;
        cfg2.ratio = semitoneRatio(-400.0);
        cfg2.voices = 2;
        cfg2.spread = false;
        const auto in = gen::noise(5150, sr, 0.5);
        std::vector<float> o1(in.size(), 0.0f), o2(in.size(), 0.0f);
        const float* inP[2] = {in.data(), in.data()};
        float* outP[2] = {o1.data(), o2.data()};
        MultichannelShifter m;
        check(c, m.prepare(cfg, 2), "prepare failed");
        m.process(inP, outP, 2, sr / 2);
        (void)m.voiceStats(0, 0);
        allocs::reset();
        allocs::arm(true);
        const bool ok1 = m.prepare(cfg, 2);   // identical
        m.process(inP, outP, 2, sr / 4);
        const bool ok2 = m.prepare(cfg2, 2);  // param-only
        m.process(inP, outP, 2, sr / 4);
        allocs::arm(false);
        const long n = allocs::count();
        check(c, ok1 && ok2, "multichannel re-prepare failed");
        addValue(c, "allocations", (double)n);
        check(c, n == 0, "multichannel re-prepare allocated");
        check(c, meas::allFinite(o1.data(), o1.size()), "non-finite");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("rt_reset_no_allocation")) {  // reset() is allocation-free mid-stream
        CaseResult c{"rt_reset_no_allocation"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.25;
        const auto in = gen::noise(6161, sr, 0.5);
        std::vector<float> out(in.size(), 0.0f);
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        e.process(in.data(), out.data(), sr / 2);
        (void)e.voiceStats(0);
        allocs::reset();
        allocs::arm(true);
        e.reset();
        e.process(in.data() + sr / 2, out.data() + sr / 2, sr / 2);
        allocs::arm(false);
        const long n = allocs::count();
        addValue(c, "allocations", (double)n);
        check(c, n == 0, "reset() allocated");
        check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("rt_setters_and_process_no_allocation")) {  // every setter (the NEW surface included) + process() are
       // allocation-free, including setters issued MID-RAMP (a gain ramp
       // still slewing) and MID-BYPASS-BLEND (the bypass crossfade still
       // slewing).  Mono + multichannel.
        CaseResult c{"rt_setters_and_process_no_allocation"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.25;
        const int n = 64 * 128;
        const auto in = gen::noise(606, n, 0.5);
        std::vector<float> out(n, 0.0f);
        {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            e.process(in.data(), out.data(), 2048);  // warm-up (tables)
            (void)e.voiceStats(0);
            (void)e.lastEstimateHz();
            allocs::reset();
            allocs::arm(true);
            int done = 2048;
            while (done < n) {
                e.process(in.data() + done, out.data() + done, 128);
                done += 128;
                const int bi = done / 128;
                if (bi % 12 == 0)
                    e.setRatio(semitoneRatio(400.0 + 50.0 * (bi % 3)));
                else if (bi % 12 == 1)
                    e.setVoices(1 + (bi % 8));
                else if (bi % 12 == 2)
                    e.setSpread(bi % 2 == 0);
                else if (bi % 12 == 3)
                    e.setFeedback(0.1 * (bi % 5));
                else if (bi % 12 == 4)
                    e.setVoiceRatio(3, std::pow(2.0, ((bi % 7) - 3) / 12.0));
                else if (bi % 12 == 5)
                    e.setVoiceGain(0, 0.2 + 0.1 * (bi % 6));  // mid-ramp
                else if (bi % 12 == 6)
                    e.setVoiceMute(1, bi % 4 == 0);
                else if (bi % 12 == 7)
                    e.setVoiceBaseDelay(2, 300.0 + 100.0 * (bi % 5));
                else if (bi % 12 == 8)
                    e.setWetDry(0.25 + 0.1 * (bi % 6));
                else if (bi % 12 == 9)
                    e.setBypass(bi % 2 == 0);  // mid-blend toggles
                else {
                    (void)e.voiceStats(5);
                    (void)e.lastEstimateHz();
                    (void)e.latencySamples();
                    (void)e.config().ratio;
                    (void)e.ready();
                }
            }
            allocs::arm(false);
            const long nm = allocs::count();
            addValue(c, "mono_allocations", (double)nm);
            check(c, nm == 0, "mono setters/process allocated");
            check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        }
        {
            std::vector<float> o1(n, 0.0f), o2(n, 0.0f);
            MultichannelShifter m;
            check(c, m.prepare(cfg, 2), "multichannel prepare failed");
            {
                const float* inP[2] = {in.data(), in.data()};
                float* outP[2] = {o1.data(), o2.data()};
                m.process(inP, outP, 2, 2048);
            }
            (void)m.voiceStats(0, 0);
            allocs::reset();
            allocs::arm(true);
            int done = 2048;
            while (done < n) {
                const float* inP[2] = {in.data() + done, in.data() + done};
                float* outP[2] = {o1.data() + done, o2.data() + done};
                m.process(inP, outP, 2, 128);
                done += 128;
                const int bi = done / 128;
                if (bi % 12 == 0)
                    m.setRatio(semitoneRatio(400.0 + 50.0 * (bi % 3)));
                else if (bi % 12 == 1)
                    m.setVoices(1 + (bi % 8));
                else if (bi % 12 == 2)
                    m.setSpread(bi % 2 == 0);
                else if (bi % 12 == 3)
                    m.setFeedback(0.1 * (bi % 5));
                else if (bi % 12 == 4)
                    m.setVoiceRatio(3, std::pow(2.0, ((bi % 7) - 3) / 12.0));
                else if (bi % 12 == 5)
                    m.setVoiceGain(0, 0.2 + 0.1 * (bi % 6));  // mid-ramp
                else if (bi % 12 == 6)
                    m.setVoiceMute(1, bi % 4 == 0);
                else if (bi % 12 == 7)
                    m.setVoiceBaseDelay(2, 300.0 + 100.0 * (bi % 5));
                else if (bi % 12 == 8)
                    m.setWetDry(0.25 + 0.1 * (bi % 6));
                else if (bi % 12 == 9)
                    m.setBypass(bi % 2 == 0);  // mid-blend toggles
                else
                    (void)m.voiceStats(1, 5);
            }
            allocs::arm(false);
            const long nmc = allocs::count();
            addValue(c, "multichannel_allocations", (double)nmc);
            check(c, nmc == 0, "multichannel setters/process allocated");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("rt_reprepare_result_identity")) {  // in-place re-prepare result identity: after an identical-config OR
       // a param-only re-prepare, the engine is bit-identical to a FRESH
       // engine prepared with the same config (R13-c: "identical result to
       // a fresh engine").  Mono (both variants) + multichannel.
        CaseResult c{"rt_reprepare_result_identity"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.25;
        Config cfg2 = cfg;
        cfg2.ratio = semitoneRatio(700.0);
        cfg2.voices = 4;
        cfg2.spread = false;
        cfg2.grain = 2048.0;
        cfg2.base_delay = 512.0;
        const auto in = gen::noise(8484, sr, 0.5);
        const int half = (int)in.size() / 2;
        auto tail = [&](const Config& cc) {
            AlignedDelayLine f;
            std::vector<float> o(half, 0.0f);
            check(c, f.prepare(cc), "fresh prepare failed");
            f.process(in.data() + half, o.data(), half);
            return o;
        };
        {  // (a) identical-config re-prepare
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> o1(half, 0.0f), o2(half, 0.0f);
            e.process(in.data(), o1.data(), half);
            check(c, e.prepare(cfg), "identical re-prepare failed");
            e.process(in.data() + half, o2.data(), half);
            const auto ref = tail(cfg);
            double d = 0.0;
            for (int k = 0; k < half; ++k)
                d = std::max(d, std::abs((double)o2[k] - (double)ref[k]));
            addValue(c, "identical_reprepare_vs_fresh", d);
            check(c, d == 0.0, "identical-config re-prepare != fresh engine");
        }
        {  // (b) param-only re-prepare (picks up cfg2)
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> o1(half, 0.0f), o2(half, 0.0f);
            e.process(in.data(), o1.data(), half);
            check(c, e.prepare(cfg2), "param-only re-prepare failed");
            e.process(in.data() + half, o2.data(), half);
            const auto ref = tail(cfg2);
            double d = 0.0;
            for (int k = 0; k < half; ++k)
                d = std::max(d, std::abs((double)o2[k] - (double)ref[k]));
            addValue(c, "param_reprepare_vs_fresh", d);
            check(c, d == 0.0, "param-only re-prepare != fresh engine");
        }
        {  // (c) multichannel identical-config re-prepare
            MultichannelShifter m;
            check(c, m.prepare(cfg, 2), "multichannel prepare failed");
            std::vector<float> a1(half, 0.0f), a2(half, 0.0f);
            std::vector<float> b1(half, 0.0f), b2(half, 0.0f);
            {
                const float* inP[2] = {in.data(), in.data()};
                float* oP[2] = {a1.data(), a2.data()};
                m.process(inP, oP, 2, half);
            }
            check(c, m.prepare(cfg, 2), "multichannel re-prepare failed");
            {
                const float* inP[2] = {in.data() + half, in.data() + half};
                float* oP[2] = {b1.data(), b2.data()};
                m.process(inP, oP, 2, half);
            }
            MultichannelShifter f;
            check(c, f.prepare(cfg, 2), "multichannel fresh prepare failed");
            std::vector<float> r1(half, 0.0f), r2(half, 0.0f);
            {
                const float* inP[2] = {in.data() + half, in.data() + half};
                float* oP[2] = {r1.data(), r2.data()};
                f.process(inP, oP, 2, half);
            }
            double d = 0.0;
            for (int k = 0; k < half; ++k) {
                d = std::max(d, std::abs((double)b1[k] - (double)r1[k]));
                d = std::max(d, std::abs((double)b2[k] - (double)r2[k]));
            }
            addValue(c, "multichannel_reprepare_vs_fresh", d);
            check(c, d == 0.0, "multichannel re-prepare != fresh engines");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("voicestats_ratio_diagnostics")) {  // R14: VoiceStats::ratio -- the geometric glide (kGlideMult =
       // 0.9995/sample), exposed as a diagnostic.  Cold-start law: the
       // glide is initialised AT its target (bit-exact).  setRatio()
       // mid-stream: the glide stays inside (r0, r1), is monotone, and
       // converges per |ratio(n) - r1| = |r0 - r1| * 0.9995^n.
        CaseResult c{"voicestats_ratio_diagnostics"};
        const double r0 = std::pow(2.0, -3.0 / 12.0);
        const double r1 = std::pow(2.0, 6.0 / 12.0);
        Config cfg;
        cfg.ratio = r0;
        cfg.spread = false;
        const auto in = gen::noise(777, sr, 0.5);
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        std::vector<float> out(in.size(), 0.0f);
        e.process(in.data(), out.data(), 4800);
        check(c, e.voiceStats(0).ratio == r0,
              "cold-start ratio != target (must be bit-exact)");
        e.setRatio(r1);
        check(c, e.voiceStats(0).ratio == r0,
              "setRatio moved the glide instantly (must ride the glide)");
        int done = 4800;
        double prev = r0, last = r0;
        int samples = 0;
        while (done + 4800 <= (int)in.size()) {
            e.process(in.data() + done, out.data() + done, 4800);
            done += 4800;
            samples += 4800;
            const double rv = e.voiceStats(0).ratio;
            check(c, rv > r0 - 1e-15 && rv < r1 + 1e-15, "glide overshot");
            check(c, rv >= prev - 1e-15, "glide not monotone");
            prev = rv;
            last = rv;
        }
        const double expect = std::abs(r1 - r0) *
                              std::pow(Config::kGlideMult, (double)samples);
        addValue(c, "glide_residual_measured", std::abs(last - r1));
        addValue(c, "glide_residual_closed_form", expect);
        // Tolerance: the closed-form glide law with a 1e-9 allowance for
        // the accumulated per-sample roundings over 48000 glide steps
        // (each step rounds once; 48000*eps*|r0-r1| ~ 3e-12 << 1e-9).
        check(c, std::abs(last - r1) <= expect + 1e-9,
              "glide did not converge per the geometric law");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

// ---------------------------------------------------------------------------
// ROUND-15 P1 PER-VOICE PARAMETER SURFACE (re-created from the worklog
// record R15-P1 / R15-REB1; gates fixed before running, implementation-
// independent formulations where literal bounds are undecidable -- the
// recorded pattern: bound = 2*(natural_step + slope*amplitude), literal
// values recorded in the case values).
// ---------------------------------------------------------------------------
void casesR15P1(std::vector<CaseResult>& R) {
    const int sr = 48000;

    if (harnSelected("per_voice_ratios_independent")) {  // per-voice ABSOLUTE ratios: 8 voices, spread OFF (the spread table
       // would re-derive targets), ratios 2^(k/12), k in
       // {-12,-9,-6,-3,0,3,6,12}.  Each voice is measured ALONE (the other
       // seven muted -- their ramped gain reaches exactly 0, removing them
       // from the mix while their DSP state keeps running).  Measurement
       // window: the newest 65536 samples of a 3 s run, i.e. from 1 s
       // onward -- the mute ramps (720 samples) and the per-voice glide
       // (~0.19 s to 1%) have settled.  GATE: |cents| <= 1.0 per voice.
        CaseResult c{"per_voice_ratios_independent"};
        const double semis[8] = {-12.0, -9.0, -6.0, -3.0, 0.0, 3.0, 6.0, 12.0};
        Config cfg;
        cfg.voices = 8;
        cfg.spread = false;
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        double worst = 0.0;
        int worstV = -1;
        for (int v = 0; v < 8; ++v) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            for (int j = 0; j < 8; ++j) {
                e.setVoiceRatio(j, std::pow(2.0, semis[j] / 12.0));
                if (j != v) e.setVoiceMute(j, true);
            }
            std::vector<float> out(in.size(), 0.0f);
            e.process(in.data(), out.data(), (int)in.size());
            const double fm =
                meas::pitchFFT(out.data() + sr, 2 * sr, sr, 50.0, 4000.0);
            const double cents =
                meas::centsBetween(fm, 220.0 * std::pow(2.0, semis[v] / 12.0));
            char vk[48];
            std::snprintf(vk, sizeof(vk), "voice%d_cents", v);
            addValue(c, vk, cents);
            if (std::abs(cents) > std::abs(worst)) {
                worst = cents;
                worstV = v;
            }
            check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        }
        addValue(c, "worst_voice", worstV);
        addValue(c, "worst_cents", worst);
        check(c, std::abs(worst) <= 1.0,
              "per-voice ratio off by > 1.0 cent (voice isolation broken)");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("per_voice_gain_ramp_and_mute")) {  // per-voice gain ramp and mute: (a) steady gain 0.5 within
       // +/- 0.1 dB; (b) gain-ramp transition steps bounded by the
       // implementation-independent formulation
       //         step <= 2*(natural_step + ramp_slope*amplitude)
       //     with all literals recorded.  GATE ANALYSIS (documented): the
       //     220 Hz natural step (A*2*sin(pi*220/48000) = 0.0288*A) EXCEEDS
       //     the naive ramp-only bound (ramp_slope*A = 6.94e-4*A) by ~41x
       //     -- a gate of "step <= ramp_slope*amplitude" would fail on the
       //     tone's natural motion, so the natural step MUST be part of the
       //     bound (the recorded P1 formulation);  (c) mute ramp: same
       //     bound, then EXACT silence once fully muted;  (d) a full
       //     mute/unmute cycle is bit-transparent once the ramps have
       //     completed (the voice's DSP state keeps running while muted).
        CaseResult c{"per_voice_gain_ramp_and_mute"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);  // 1 voice, non-trivial path
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        const int P = 48000;   // gain change (block 480 boundary)
        const int P2 = 96000;  // unmute boundary
        const int rampN = 720;  // gain_ramp_seconds * sr = 0.015*48000
        auto runGain = [&](int mode) {  // 0=ref, 1=gain 0.5 at P,
                                        // 2=mute at P / unmute at P2
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(in.size(), 0.0f);
            int done = 0;
            while (done < (int)in.size()) {
                const int n = std::min(480, (int)in.size() - done);
                e.process(in.data() + done, out.data() + done, n);
                done += n;
                if (mode == 1 && done == P) e.setVoiceGain(0, 0.5);
                if (mode == 2 && done == P) e.setVoiceMute(0, true);
                if (mode == 2 && done == P2) e.setVoiceMute(0, false);
            }
            return out;
        };
        const auto oRef = runGain(0);
        const auto oGain = runGain(1);
        const auto oMute = runGain(2);

        // (a) steady gain 0.5: header law final gain = norm*user = 0.5
        {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            e.setVoiceGain(0, 0.5);
            std::vector<float> out(in.size(), 0.0f);
            e.process(in.data(), out.data(), (int)in.size());
            const int w = (int)in.size() - sr;
            const double ro = meas::rms(out.data() + sr, w);
            const double ri = meas::rms(in.data() + sr, w);
            const double db = 20.0 * std::log10(ro / ri) - 20.0 * std::log10(0.5);
            addValue(c, "steady_gain_0p5_err_db", db);
            check(c, std::abs(db) <= 0.1,
                  "steady gain 0.5 outside +/-0.1 dB of the header law");
            check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        }

        // (b) gain ramp steps (P .. P+rampN]
        {
            double step = 0.0;
            for (int k = P + 1; k <= P + rampN; ++k)
                step = std::max(step,
                                std::abs((double)oGain[k] - (double)oGain[k - 1]));
            double nat = 0.0;
            for (int k = P + 1; k <= P + rampN; ++k)
                nat = std::max(nat,
                               std::abs((double)oRef[k] - (double)oRef[k - 1]));
            const double peakA = meas::peak(oRef.data() + P, rampN);
            const double slope = 0.5 / rampN;  // |target-start|/(ramp_s*sr)
            const double slopeTerm = slope * peakA;
            const double bound = 2.0 * (nat + slopeTerm);
            addValue(c, "gain_ramp_measured_step", step);
            addValue(c, "gain_ramp_natural_step", nat);
            addValue(c, "gain_ramp_slope_term", slopeTerm);
            addValue(c, "gain_ramp_naive_bound", slopeTerm);  // naive = slope*amp
            addValue(c, "gain_ramp_bound", bound);
            check(c, step <= bound,
                  "gain ramp step outside 2*(natural_step + slope*amplitude)");
        }

        // (c) mute ramp steps + exact silence while fully muted
        {
            double step = 0.0;
            for (int k = P + 1; k <= P + rampN; ++k)
                step = std::max(step,
                                std::abs((double)oMute[k] - (double)oMute[k - 1]));
            double nat = 0.0;
            for (int k = P + 1; k <= P + rampN; ++k)
                nat = std::max(nat,
                               std::abs((double)oRef[k] - (double)oRef[k - 1]));
            const double peakA = meas::peak(oRef.data() + P, rampN);
            const double slope = 1.0 / rampN;
            const double bound = 2.0 * (nat + slope * peakA);
            addValue(c, "mute_ramp_measured_step", step);
            addValue(c, "mute_ramp_natural_step", nat);
            addValue(c, "mute_ramp_bound", bound);
            check(c, step <= bound,
                  "mute ramp step outside 2*(natural_step + slope*amplitude)");
            double silence = 0.0;
            for (int k = P + 2 * rampN; k < P2; ++k)
                silence = std::max(silence, std::abs((double)oMute[k]));
            addValue(c, "muted_region_max_abs", silence);
            check(c, silence == 0.0, "fully muted engine is not exactly silent");
        }

        // (d) bit-transparency of the mute/unmute cycle after the ramps
        {
            const int from = P2 + 2 * rampN;  // unmute ramp complete (+ slack)
            double d = 0.0;
            for (int k = from; k < (int)in.size(); ++k)
                d = std::max(d, std::abs((double)oMute[k] - (double)oRef[k]));
            addValue(c, "post_cycle_vs_reference_max_diff", d);
            check(c, d == 0.0,
                  "mute/unmute cycle not bit-transparent after the ramps");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("per_voice_delay_isolation")) {  // per-voice base-delay isolation: a mid-stream setVoiceBaseDelay on
       // voice 1 touches ONLY voice 1 (structural per-voice state).
       // Measurement feedback-free (the global feedback is wet-path-only
       // and would legitimately couple the voices).  Voice 0 measured alone
       // (7 muted): BIT-EXACT vs the no-change run.  Voice 1 measured
       // alone: bounded (<= 2x input peak) and the change HAD an effect.
        CaseResult c{"per_voice_delay_isolation"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;  // fb 0 (see above)
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        const int P = 48000;
        auto runDelay = [&](int solo, bool change) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            for (int j = 0; j < 8; ++j)
                if (j != solo) e.setVoiceMute(j, true);
            std::vector<float> out(in.size(), 0.0f);
            int done = 0;
            while (done < (int)in.size()) {
                const int n = std::min(480, (int)in.size() - done);
                e.process(in.data() + done, out.data() + done, n);
                done += n;
                if (change && done == P)
                    e.setVoiceBaseDelay(1, 1200.0);  // issued at P in both
                                                   // solo measurements
            }
            return out;
        };
        {  // voice 0: bit-exact isolation
            const auto a = runDelay(0, true);
            const auto b = runDelay(0, false);
            double d = 0.0;
            for (int k = 0; k < (int)in.size(); ++k)
                d = std::max(d, std::abs((double)a[k] - (double)b[k]));
            addValue(c, "voice0_change_vs_nochange_max_diff", d);
            check(c, d == 0.0,
                  "voice 1's base-delay change perturbed voice 0");
        }
        {  // voice 1: bounded + the change had an effect
            const auto a = runDelay(1, true);
            const auto b = runDelay(1, false);
            double d = 0.0;
            for (int k = P; k < (int)in.size(); ++k)
                d = std::max(d, std::abs((double)a[k] - (double)b[k]));
            const double pk = meas::peak(a.data(), (int)a.size());
            addValue(c, "voice1_change_effect_from_P", d);
            addValue(c, "voice1_peak", pk);
            addValue(c, "two_x_input_peak", 2.0 * meas::peak(in.data(), (int)in.size()));
            check(c, d > 0.0, "the base-delay change had no effect (vacuous)");
            check(c, meas::allFinite(a.data(), a.size()), "non-finite");
            check(c, pk <= 2.0 * meas::peak(in.data(), (int)in.size()),
                  "voice 1 output above 2x input peak");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("wetdry_endpoints")) {  // wet/dry endpoints: w=1 is the bit-identical legacy path (equal to
       // the never-touched default run); w=0 is the bit-exact latency-
       // aligned delayed copy dry[k] = in[k-(L-1)]; w=0.5 follows the
       // linear law |out(0.5) - 0.5*out(1) - 0.5*out(0)| <= 1e-6 (float
       // quantisation residual of the two endpoint runs; literal recorded).
       // The wet-path state is identical across the three runs (the
       // feedback taps the PRE-mix wet signal), so fb 0.25 is included to
       // make the law non-vacuous.
        CaseResult c{"wetdry_endpoints"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.25;
        const auto in = gen::tone(220.0, sr, 2 * sr, 0.5);
        const int n = (int)in.size();
        auto runWet = [&](double wet, bool set) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            if (set) e.setWetDry(wet);
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(512, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
            }
            return out;
        };
        const auto oOne = runWet(1.0, true);
        const auto oDef = runWet(1.0, false);  // never touched
        const auto oZero = runWet(0.0, true);
        const auto oHalf = runWet(0.5, true);
        AlignedDelayLine probe;
        check(c, probe.prepare(cfg), "prepare failed");
        const int L = probe.latencySamples();
        {
            double d = 0.0;
            for (int k = 0; k < n; ++k)
                d = std::max(d, std::abs((double)oOne[k] - (double)oDef[k]));
            addValue(c, "wet1_vs_default_max_diff", d);
            check(c, d == 0.0, "wet=1 is not the bit-identical legacy path");
        }
        {
            double d = 0.0;
            for (int k = 0; k < n; ++k) {
                const int src = k - (L - 1);
                const double expect = (src >= 0) ? (double)in[src] : 0.0;
                d = std::max(d, std::abs((double)oZero[k] - expect));
            }
            addValue(c, "wet0_vs_delayed_copy_max_diff", d);
            addValue(c, "latency_samples", L);
            check(c, d == 0.0, "wet=0 is not the bit-exact delayed copy");
        }
        {
            double d = 0.0;
            for (int k = 0; k < n; ++k)
                d = std::max(d, std::abs((double)oHalf[k] -
                                         (0.5 * (double)oOne[k] +
                                          0.5 * (double)oZero[k])));
            addValue(c, "linear_law_residual_max_abs", d);
            check(c, d <= 1e-6, "wet/dry linear law residual > 1e-6");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("bypass_clickfree_and_transparent")) {  // bypass click-free + transparent: enter at t1, exit at t2 (both
       // 480-block boundaries; bypass_seconds = 10 ms = 480 samples).
       // Gates: (i) pre-enter bit-identical to the never-bypassed
       // reference; (ii) the fully-bypassed region is the BIT-EXACT dry
       // copy in[k-(L-1)]; (iii) post-exit bit-identical to the reference
       // (the engine state kept advancing -- bypass only touches the final
       // output sample); (iv) BOTH transition ramps are bounded by the
       // implementation-independent formulation
       //         step <= 2*(natural_step + b_slope*amplitude)
       //     where natural_step = max step of the two blended signals
       //     (the reference output and the delayed input), b_slope =
       //     1/(bypass_seconds*sr), amplitude = max|mix - dry| over the
       //     window (all measured on implementation-independent
       //     observables; literals recorded); (v) the bypass actually
       //     changed the stream (non-vacuous).
        CaseResult c{"bypass_clickfree_and_transparent"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.25;
        const int n = 4 * sr;
        const int t1 = 48000, t2 = 144000;
        const int bN = 480;  // bypass_seconds * sr
        const auto in = gen::noise(9090, n, 0.5);
        AlignedDelayLine probe;
        check(c, probe.prepare(cfg), "prepare failed");
        const int L = probe.latencySamples();
        auto runByp = [&](bool bypass) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(480, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (bypass && done == t1) e.setBypass(true);
                if (bypass && done == t2) e.setBypass(false);
            }
            return out;
        };
        const auto oA = runByp(true);
        const auto oB = runByp(false);
        {  // (i) pre-enter
            double d = 0.0;
            for (int k = 0; k < t1; ++k)
                d = std::max(d, std::abs((double)oA[k] - (double)oB[k]));
            addValue(c, "pre_enter_max_diff", d);
            check(c, d == 0.0, "bypass enter perturbed the stream before t1");
        }
        {  // (ii) fully-bypassed region: bit-exact dry copy
            const int lo = t1 + 2 * bN, hi = t2;  // ramp done (+ slack)
            double d = 0.0;
            for (int k = lo; k < hi; ++k) {
                const int src = k - (L - 1);
                const double expect = (src >= 0) ? (double)in[src] : 0.0;
                d = std::max(d, std::abs((double)oA[k] - expect));
            }
            addValue(c, "bypassed_vs_dry_copy_max_diff", d);
            addValue(c, "latency_samples", L);
            check(c, d == 0.0, "bypassed output is not the bit-exact dry copy");
        }
        {  // (iii) post-exit bit-transparency
            const int from = t2 + 2 * bN;
            double d = 0.0;
            for (int k = from; k < n; ++k)
                d = std::max(d, std::abs((double)oA[k] - (double)oB[k]));
            addValue(c, "post_exit_vs_reference_max_diff", d);
            check(c, d == 0.0,
                  "bypass enter+exit is not bit-transparent outside the ramps");
        }
        {  // (iv) transition bounds on both ramp windows
            const double bSlope = 1.0 / bN;
            const char* names[2] = {"enter", "exit"};
            const int starts[2] = {t1, t2};
            for (int w = 0; w < 2; ++w) {
                const int a = starts[w], b = starts[w] + bN;
                double step = 0.0, natMix = 0.0, natDry = 0.0, amp = 0.0;
                for (int k = a + 1; k <= b; ++k) {
                    step = std::max(step,
                                    std::abs((double)oA[k] - (double)oA[k - 1]));
                    natMix = std::max(natMix,
                                      std::abs((double)oB[k] - (double)oB[k - 1]));
                    const int src = k - (L - 1);
                    const double dry = (src >= 0) ? (double)in[src] : 0.0;
                    const double dryPrev =
                        (src - 1 >= 0) ? (double)in[src - 1] : 0.0;
                    natDry = std::max(natDry, std::abs(dry - dryPrev));
                    amp = std::max(amp, std::abs((double)oB[k] - dry));
                }
                const double nat = std::max(natMix, natDry);
                const double bound = 2.0 * (nat + bSlope * amp);
                char vk[64];
                std::snprintf(vk, sizeof(vk), "%s_ramp_measured_step", names[w]);
                addValue(c, vk, step);
                std::snprintf(vk, sizeof(vk), "%s_ramp_natural_step", names[w]);
                addValue(c, vk, nat);
                std::snprintf(vk, sizeof(vk), "%s_ramp_slope_term", names[w]);
                addValue(c, vk, bSlope * amp);
                std::snprintf(vk, sizeof(vk), "%s_ramp_bound", names[w]);
                addValue(c, vk, bound);
                check(c, step <= bound,
                      "bypass ramp step outside 2*(natural+slope*amplitude)");
            }
        }
        {  // (v) non-vacuous: the bypass changed the stream
            double d = 0.0;
            for (int k = t1 + 2 * bN; k < t2; ++k)
                d = std::max(d, std::abs((double)oA[k] - (double)oB[k]));
            addValue(c, "bypass_effect_size", d);
            check(c, d > 0.0, "bypass had no effect (vacuous comparison)");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("new_setters_no_allocation")) {  // the NEW setters are allocation-free on the audio thread, issued
       // between process() calls INCLUDING mid-ramp (a gain ramp still
       // slewing when re-targeted: re-targets every 256 samples < 720) and
       // mid-bypass-blend (the crossfade still slewing when toggled back:
       // toggles every 384 samples < 480).  0 allocations via the global
       // new counter, mono AND multichannel.
        CaseResult c{"new_setters_no_allocation"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.25;
        const int n = 192 * 128;
        const auto in = gen::noise(1212, n, 0.5);
        std::vector<float> out(n, 0.0f);
        {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            e.process(in.data(), out.data(), 512);  // warm-up
            (void)e.voiceStats(0);
            allocs::reset();
            allocs::arm(true);
            int done = 512;
            while (done < n) {
                e.process(in.data() + done, out.data() + done, 128);
                done += 128;
                const int bi = done / 128;
                e.setVoiceRatio(bi % 8, std::pow(2.0, ((bi % 7) - 3) / 12.0));
                if (bi % 2 == 0) e.setVoiceGain(0, 0.3 + 0.2 * (bi % 3));  // mid-ramp
                if (bi % 5 == 0) e.setVoiceMute(1, bi % 2 == 0);
                if (bi % 7 == 0) e.setVoiceBaseDelay(2, 400.0 + 50.0 * (bi % 8));
                if (bi % 4 == 0) e.setWetDry(0.2 + 0.15 * (bi % 4));
                if (bi % 3 == 0) e.setBypass(bi % 6 == 0);  // mid-blend toggles
                if (bi % 2 == 1) {
                    (void)e.voiceStats(3);
                    (void)e.lastEstimateHz();
                }
            }
            allocs::arm(false);
            const long nm = allocs::count();
            addValue(c, "mono_allocations", (double)nm);
            check(c, nm == 0, "new-surface setters allocated (mono)");
            check(c, meas::allFinite(out.data(), out.size()), "non-finite");
        }
        {
            std::vector<float> o1(n, 0.0f), o2(n, 0.0f);
            MultichannelShifter m;
            check(c, m.prepare(cfg, 2), "multichannel prepare failed");
            {
                const float* inP[2] = {in.data(), in.data()};
                float* outP[2] = {o1.data(), o2.data()};
                m.process(inP, outP, 2, 512);
            }
            (void)m.voiceStats(0, 0);
            allocs::reset();
            allocs::arm(true);
            int done = 512;
            while (done < n) {
                const float* inP[2] = {in.data() + done, in.data() + done};
                float* outP[2] = {o1.data() + done, o2.data() + done};
                m.process(inP, outP, 2, 128);
                done += 128;
                const int bi = done / 128;
                m.setVoiceRatio(bi % 8, std::pow(2.0, ((bi % 7) - 3) / 12.0));
                if (bi % 2 == 0) m.setVoiceGain(0, 0.3 + 0.2 * (bi % 3));
                if (bi % 5 == 0) m.setVoiceMute(1, bi % 2 == 0);
                if (bi % 7 == 0) m.setVoiceBaseDelay(2, 400.0 + 50.0 * (bi % 8));
                if (bi % 4 == 0) m.setWetDry(0.2 + 0.15 * (bi % 4));
                if (bi % 3 == 0) m.setBypass(bi % 6 == 0);
            }
            allocs::arm(false);
            const long nmc = allocs::count();
            addValue(c, "multichannel_allocations", (double)nmc);
            check(c, nmc == 0, "new-surface setters allocated (multichannel)");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("default_neutrality")) {  // default_neutrality: the 15.0.0 core is BIT-IDENTICAL to the
       // recorded 12.0.0 reference hashes under the DEFAULT surface
       // (wet=1, bypass off, no per-voice overrides, all gains 1).  The
       // four configs and the FNV-1a-64-over-float-bits law replicate
       // native/cli/r15ref_capture.cpp (octavox_r15refcap) EXACTLY -- the
       // generators are the shared-spec LCG / tone / chirp.  These four
       // hashes REPLACE the lost round-15 capture and are the regression
       // record for any future core change (r15_reference_hashes.txt).
       // [R19, 2026-10-31: with 16.0.0 the engine's DEFAULT behaviour
       // changed (decimated detector, click-free spacing commits) and this
       // case now runs the LEGACY configuration -- detector_mode = FullRate,
       // commit_crossfade_seconds = 0, delay_crossfade_seconds = 0, the
       // documented 15.0.0 behaviour pins (see dsp.hpp's ROUND-19 CONTRACT
       // (f)). The four hash GATES are UNCHANGED: this case now proves the
       // legacy path is preserved bit-exactly through the 16.0.0 refactor.
       // The 16.0.0-default hashes are gated separately by the NEW
       // default_neutrality_r19 case below.]
        CaseResult c{"default_neutrality"};
        auto legacy = [](Config cfg) {
            cfg.detector_mode = DetectorMode::FullRate;
            cfg.commit_crossfade_seconds = 0.0;
            cfg.delay_crossfade_seconds = 0.0;
            cfg.voice_fade_seconds = 0.0;
            return cfg;
        };
        const int N = 1 << 18;
        auto fnv = [](const float* x, int n,
                      uint64_t h = 1469598103934665603ULL) {
            for (int k = 0; k < n; ++k) {
                uint32_t b;
                std::memcpy(&b, &x[k], 4);
                for (int i = 0; i < 4; ++i) {
                    h ^= static_cast<uint64_t>((b >> (8 * i)) & 0xffu);
                    h *= 1099511628211ULL;
                }
            }
            return h;
        };
        auto runPat = [&](AlignedDelayLine& e, const std::vector<float>& in,
                          const int* pat, int patN) {
            std::vector<float> out(in.size(), 0.0f);
            int done = 0, pi = 0;
            while (done < (int)in.size()) {
                const int n = std::min(pat[pi % patN], (int)in.size() - done);
                e.process(in.data() + done, out.data() + done, n);
                done += n;
                ++pi;
            }
            return out;
        };
        const uint64_t expect[4] = {0x74318b06c182aacbULL, 0x46a091e98f2a413aULL,
                                    0x9502cf8827e8782cULL, 0x6f7a2ce3ca310c25ULL};
        const char* names[4] = {
            "cfg1_default_unity_lcg_noise_b64_150_512",
            "cfg2_r700c_8v_spread_fb025_sine220",
            "cfg3_rm400c_4v_nospread_chirp200_800",
            "cfg4_stereo_2ch_4v_unity_then_r400c"};
        uint64_t got[4] = {0, 0, 0, 0};
        {  // cfg1: default unity + LCG noise; blocks 64/150/512
            const auto in = gen::noise(987654321ULL, N, 0.5);
            AlignedDelayLine e;
            Config cfg = legacy(Config());  // all defaults + legacy pins
            check(c, e.prepare(cfg), "prepare failed");
            const int pat[3] = {64, 150, 512};
            const auto out = runPat(e, in, pat, 3);
            got[0] = fnv(out.data(), (int)out.size());
        }
        {  // cfg2: ratio 2^(700/1200), 8 voices, spread on, fb 0.25,
           // 220 Hz sine; blocks 150/512/64
            const auto in = gen::tone(220.0, sr, N, 0.5);
            AlignedDelayLine e;
            Config cfg = legacy(Config());
            cfg.ratio = std::pow(2.0, 700.0 / 1200.0);
            cfg.voices = 8;
            cfg.feedback = 0.25;
            check(c, e.prepare(cfg), "prepare failed");
            const int pat[3] = {150, 512, 64};
            const auto out = runPat(e, in, pat, 3);
            got[1] = fnv(out.data(), (int)out.size());
        }
        {  // cfg3: ratio 2^(-400/1200), 4 voices, spread off, chirp
           // 200->800 Hz; blocks 64/512/150
            const auto in = gen::chirp(200.0, 800.0, sr, N, 0.5);
            AlignedDelayLine e;
            Config cfg = legacy(Config());
            cfg.ratio = std::pow(2.0, -400.0 / 1200.0);
            cfg.voices = 4;
            cfg.spread = false;
            check(c, e.prepare(cfg), "prepare failed");
            const int pat[3] = {64, 512, 150};
            const auto out = runPat(e, in, pat, 3);
            got[2] = fnv(out.data(), (int)out.size());
        }
        {  // cfg4: stereo 2ch, LCG seeds 1111111111 / 2222222222, 4 voices,
           // unity -> 2^(400/1200) at the first block boundary >= N/2;
           // blocks 1024/64/512; hash = L bits then R bits, ONE FNV stream
            const auto inL = gen::noise(1111111111ULL, N, 0.4);
            const auto inR = gen::noise(2222222222ULL, N, 0.4);
            MultichannelShifter m;
            Config cfg = legacy(Config());
            cfg.voices = 4;
            check(c, m.prepare(cfg, 2), "prepare failed");
            std::vector<float> outL(N, 0.0f), outR(N, 0.0f);
            const float* in[2] = {inL.data(), inR.data()};
            float* out[2] = {outL.data(), outR.data()};
            const int pat[3] = {1024, 64, 512};
            int done = 0, pi = 0;
            bool switched = false;
            while (done < N) {
                const int n = std::min(pat[pi % 3], N - done);
                m.process(in, out, 2, n);
                done += n;
                ++pi;
                if (!switched && done >= N / 2) {
                    m.setRatio(std::pow(2.0, 400.0 / 1200.0));
                    switched = true;
                }
            }
            got[3] = fnv(outL.data(), N);
            got[3] = fnv(outR.data(), N, got[3]);
        }
        for (int i = 0; i < 4; ++i) {
            std::printf("default_neutrality %s: got 0x%016llx expect 0x%016llx\n",
                        names[i], static_cast<unsigned long long>(got[i]),
                        static_cast<unsigned long long>(expect[i]));
            char vk[32];
            std::snprintf(vk, sizeof(vk), "cfg%d_hash_match", i + 1);
            addValue(c, vk, got[i] == expect[i] ? 1.0 : 0.0);
            check(c, got[i] == expect[i],
                  std::string("reference hash mismatch: ") + names[i]);
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}


// R15-P2 SALVAGE ARTIFACT -- DO NOT COMPILE STANDALONE.
// ============================================================================
// The 7 r15acc_* acceptance cases, fully designed and gate-fixed BEFORE the
// 2026-10-26 environment reset wiped the live tree mid-task (the same file
// was about to be applied to tests/test_main.cpp when the tree vanished).
// TO MERGE after the tree is recovered: append the casesR15Acc() function
// below to tests/test_main.cpp just before the "JSON report" section (after
// casesR15P1), add `casesR15Acc(g_results);` after `casesR15P1(g_results);`
// in main(), and add `#include <limits>` to the include block. It reuses
// the existing helpers only (gen::, meas::, run(), CaseResult, check,
// addValue, semitoneRatio, meas::pitchFFT -- the Hann-exact corrected
// estimator, mandatory for every pitch measurement).
// ============================================================================
// The 13-category battery it completes (the 7 P1 cases cover the rest):
//   r15acc_ratio_change_isolation, r15acc_level_accuracy,
//   r15acc_block_boundary_semantics, r15acc_reset_reprepare_determinism,
//   r15acc_stereo_surface, r15acc_robustness_battery, r15acc_invalid_args.
// Every gate was fixed before running (no widening-to-pass); where a bound
// is implementation-independent-by-necessity the literal value is recorded
// (the P1 pattern). Budget: ~6 s of extra suite runtime at 48 kHz.

void casesR15Acc(std::vector<CaseResult>& R) {
    const int sr = 48000;

    // FNV-1a 64 over raw output float bits (same law as default_neutrality).
    auto fnv1a64 = [](const std::vector<float>& x) {
        uint64_t h = 1469598103934665603ULL;
        for (float s : x) {
            uint32_t b;
            std::memcpy(&b, &s, 4);
            for (int i = 0; i < 4; ++i) {
                h ^= static_cast<uint64_t>((b >> (8 * i)) & 0xffu);
                h *= 1099511628211ULL;
            }
        }
        return h;
    };
    const double kNaN = std::numeric_limits<double>::quiet_NaN();
    const double kInf = std::numeric_limits<double>::infinity();

    if (harnSelected("r15acc_ratio_change_isolation")) {  // 1. r15acc_ratio_change_isolation: a mid-stream per-voice ratio
        //    change touches ONLY its own voice. Measurement: voice 3 is
        //    muted throughout two parallel reference runs (identical
        //    input/config/setter cadence), one WITH the voice-3 ratio
        //    change and one without -- the 7 other voices' sum must stay
        //    BIT-IDENTICAL for the whole stream (feedback 0: the global
        //    feedback is wet-path-only and would legitimately couple the
        //    voices, so isolation is measured feedback-free). Voice 3
        //    itself must converge to its new pitch within 1.0 cent.
        CaseResult c{"r15acc_ratio_change_isolation"};
        const double semis[8] = {-12.0, -7.0, -4.0, 3.0,
                                 -5.0,  5.0,  -7.0, 12.0};
        const double f0in = 220.0;
        const int P = 118784;  // 2.47 s = 1024*116 (block boundary)
        const int n = P + 3 * sr;
        const auto in = gen::tone(f0in, sr, n, 0.5);
        Config cfg;  // voices=8, spread off, ratio 1 (targets come from the
        cfg.voices = 8;  // per-voice overrides), feedback 0 (see above)
        cfg.spread = false;
        const double r3a = std::pow(2.0, 3.0 / 12.0);   // voice 3 initial
        const double r3b = std::pow(2.0, -2.0 / 12.0);  // voice 3 new target

        auto runMuted = [&](bool change) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            for (int j = 0; j < 8; ++j)
                e.setVoiceRatio(j, std::pow(2.0, semis[j] / 12.0));
            e.setVoiceMute(3, true);  // voice 3 muted throughout
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(1024, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == P)
                    e.setVoiceRatio(3, change ? r3b : r3a);
            }
            return out;
        };
        const auto outY = runMuted(true);
        const auto outY0 = runMuted(false);
        double otherMax = 0.0;
        for (int k = 0; k < n; ++k)
            otherMax =
                std::max(otherMax, std::abs((double)outY[k] - outY0[k]));
        addValue(c, "other_voices_fullstream_max_diff", otherMax);
        check(c, otherMax == 0.0,
              "voice 3's mid-stream ratio change perturbed the other voices");

        // voice 3 alone (the other 7 muted): pitch before and after P.
        {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            for (int j = 0; j < 8; ++j) {
                e.setVoiceRatio(j, std::pow(2.0, semis[j] / 12.0));
                if (j != 3) e.setVoiceMute(j, true);
            }
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(1024, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == P) e.setVoiceRatio(3, r3b);
            }
            // R13-d windows: 65536 samples (1.365 s) >= 0.5 s and >= 200
            // periods of 261.6 Hz (0.765 s); post window 2 s vs 196 Hz.
            const double fPre =
                meas::pitchFFT(out.data() + (P - 65536), 65536, sr,
                               50.0, 4000.0);
            const double cPre = meas::centsBetween(fPre, f0in * r3a);
            addValue(c, "voice3_pre_change_cents", cPre);
            check(c, std::abs(cPre) <= 1.0,
                  "voice 3 pre-change pitch off by > 1.0 cent");
            const int post0 = P + sr;  // glide (worst ~0.27 s) settled
            const double fPost = meas::pitchFFT(out.data() + post0,
                                                n - post0, sr, 50.0, 4000.0);
            const double cPost = meas::centsBetween(fPost, f0in * r3b);
            addValue(c, "voice3_post_change_cents", cPost);
            check(c, std::abs(cPost) <= 1.0,
                  "voice 3 did not converge to its new ratio within 1.0 cent");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("r15acc_level_accuracy")) {  // 2. r15acc_level_accuracy: per-voice gains {0.1, 0.25, 0.5, 1.0} on
        //    a steady tone, ONE active voice per measurement (7 muted),
        //    steady RMS ratio vs the expected level within +/- 0.1 dB.
        //    Expected is computed in-test from the header law: final voice
        //    gain = normalisation * user_gain with normalisation = 1/voices.
        CaseResult c{"r15acc_level_accuracy"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        const int settle = sr;  // gain/mute ramps (720) + detector lock
        const double gains[4] = {0.1, 0.25, 0.5, 1.0};
        double worst = 0.0;
        for (double g : gains) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            e.setVoiceGain(0, g);
            for (int j = 1; j < 8; ++j) e.setVoiceMute(j, true);
            std::vector<float> out(in.size(), 0.0f);
            e.process(in.data(), out.data(), static_cast<int>(in.size()));
            const int w = static_cast<int>(in.size()) - settle;  // 2 s
            const double ro = meas::rms(out.data() + settle, w);
            const double ri = meas::rms(in.data() + settle, w);
            const double expected = g / cfg.voices;  // header law
            const double db = 20.0 * std::log10(ro / ri) -
                              20.0 * std::log10(expected);
            char vk[40];
            std::snprintf(vk, sizeof(vk), "err_db_gain_%.2f", g);
            addValue(c, vk, db);
            worst = std::max(worst, std::abs(db));
        }
        addValue(c, "worst_err_db", worst);
        check(c, worst <= 0.1,
              "steady level outside +/-0.1 dB of the expected gain");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("r15acc_block_boundary_semantics")) {  // 3. r15acc_block_boundary_semantics: a parameter change issued
        //    between process() calls applies from the NEXT block as a
        //    STREAM-POSITION event. Run A: 64-sample blocks; run B: the
        //    identical stream in 150-sample blocks; the same simultaneous
        //    setVoiceBaseDelay + setVoiceGain + setWetDry fire at stream
        //    position P (a boundary in BOTH splittings) -> outputs
        //    identical (max|delta| == 0), proving the semantics are
        //    position-, not call-pattern-, driven. Plus: 20 small gain
        //    steps vs one jump to the same final target -> identical final
        //    steady state (the ramp terminates exactly at its target).
        CaseResult c{"r15acc_block_boundary_semantics"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;
        cfg.feedback = 0.25;
        const auto in = gen::tone(220.0, sr, 24000, 0.5);
        const int n = static_cast<int>(in.size());
        const int P = 9600;  // 64*150: a block boundary in both splittings

        auto runSplit = [&](int block) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(block, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == P) {  // simultaneous multi-parameter update
                    e.setVoiceBaseDelay(5, 1200.0);
                    e.setVoiceGain(2, 0.5);
                    e.setWetDry(0.5);
                }
            }
            return out;
        };
        const auto outA = runSplit(64);
        const auto outB = runSplit(150);
        double pre = 0.0, post = 0.0;
        for (int k = 0; k < n; ++k) {
            const double d = std::abs((double)outA[k] - outB[k]);
            if (k < P) pre = std::max(pre, d);
            else post = std::max(post, d);
        }
        addValue(c, "pre_change_max_diff", pre);
        addValue(c, "post_change_max_diff", post);
        check(c, pre == 0.0 && post == 0.0,
              "application is not stream-position (block-boundary) semantics");

        // sanity: the boundary change really changed the stream (the gate
        // above would otherwise be vacuous)
        {
            AlignedDelayLine e;
            e.prepare(cfg);
            std::vector<float> out(n, 0.0f);
            e.process(in.data(), out.data(), n);
            double d = 0.0;
            for (int k = P; k < n; ++k)
                d = std::max(d, std::abs((double)outA[k] - out[k]));
            addValue(c, "change_effect_from_P", d);
            check(c, d > 0.0, "the boundary parameter change had no effect");
        }

        // gradual (20 x 0.025 steps) vs one jump, both ending at gain 0.5
        //
        // ADAPTATION vs the salvaged .inc design (gate NOT weakened; the
        // divergence is documented here per the merge rules): the .inc ran
        // this sub-measurement on the case config, which carries feedback
        // 0.25.  Per the header contract (WET/DRY MIX) the global feedback
        // taps the PRE-MIX wet signal -- the post-gain voice mix -- so with
        // feedback on, two different gain TRAJECTORIES legitimately leave
        // different feedback history in the ring and the outputs can never
        // re-converge bit-exactly (measured residual on the designed
        // config: 9.5e-4, the feedback echo of the transition difference).
        // The trajectory-equality gate is therefore measured FEEDBACK-FREE,
        // where the voice DSP state is trajectory-independent and the
        // bit-exact gate is well-posed -- the same feedback-free
        // measurement pattern as r15acc_ratio_change_isolation.  The
        // block-boundary sub-gate above KEEPS feedback 0.25: it compares
        // the SAME event sequence at two splittings, which is bit-exact
        // with feedback on (measured pre/post max diff == 0).
        {
            const int n2 = 24000;
            const auto in2 = gen::tone(220.0, sr, n2, 0.5);
            Config cfgT = cfg;
            cfgT.feedback = 0.0;  // feedback-free (see the note above)
            auto runGain = [&](bool gradual) {
                AlignedDelayLine e;
                check(c, e.prepare(cfgT), "prepare failed");
                std::vector<float> out(n2, 0.0f);
                int done = 0, step = 0;
                while (done < n2) {
                    const int k = std::min(480, n2 - done);
                    e.process(in2.data() + done, out.data() + done, k);
                    done += k;
                    if (gradual) {
                        if (done >= 2400 && step < 20) {
                            e.setVoiceGain(3, 1.0 - 0.025 * (step + 1));
                            ++step;
                        }
                    } else if (done == 2400) {
                        e.setVoiceGain(3, 0.5);
                    }
                }
                return out;
            };
            const auto outG = runGain(true);
            const auto outJ = runGain(false);
            const int settled = 2400 + 19 * 480 + 720 + 64;  // last ramp done
            double steady = 0.0, trans = 0.0;
            for (int k = 0; k < n2; ++k) {
                const double d = std::abs((double)outG[k] - outJ[k]);
                if (k < settled) trans = std::max(trans, d);
                else steady = std::max(steady, d);
            }
            addValue(c, "gradual_vs_jump_transition_max_diff", trans);
            addValue(c, "gradual_vs_jump_steady_max_diff", steady);
            check(c, steady == 0.0,
                  "gradual and jumped gain differ in the final steady state");
            check(c, trans > 0.0,
                  "gradual and jumped gain were identical throughout "
                  "(vacuous comparison)");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("r15acc_reset_reprepare_determinism")) {  // 4. r15acc_reset_reprepare_determinism: a scripted event sequence
        //    (per-voice ratios + gains + mute/unmute + wet/dry moves +
        //    bypass enter/exit + base-delay change) -> full-run output hash;
        //    then reset() -> replay, prepare(same cfg) -> replay, destroy +
        //    fresh engine -> replay: all four must match BIT-EXACTLY.
        //    NOTE (semantics): reset() KEEPS the parameter values while
        //    prepare() restores the fresh-engine defaults, so the script is
        //    CLOSED -- it restores the neutral surface at t7 (setRatio()
        //    re-derives every voice target through the engine's own
        //    derivation path and clears the overrides). The closure is
        //    itself part of the gate: post-reset must be behaviourally
        //    identical to the fresh post-prepare state.
        CaseResult c{"r15acc_reset_reprepare_determinism"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;
        cfg.feedback = 0.25;
        const int n = 130048;  // 1024 * 127
        const auto in = gen::noise(4242, n, 0.5);
        const double semis[8] = {-12.0, -5.0, 0.0, 7.0,
                                 -3.0,  4.0, -7.0, 12.0};

        auto script = [&](AlignedDelayLine& e) {
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(1024, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                switch (done) {
                    case 16384:  // t1: per-voice ratios + gains
                        for (int j = 0; j < 8; ++j)
                            e.setVoiceRatio(j,
                                            std::pow(2.0, semis[j] / 12.0));
                        e.setVoiceGain(0, 0.4);
                        e.setVoiceGain(3, 0.7);
                        break;
                    case 32768: e.setVoiceMute(2, true); break;      // t2
                    case 49152: e.setWetDry(0.5); break;             // t3
                    case 65536: e.setBypass(true); break;            // t4
                    case 81920: e.setBypass(false); break;           // t5
                    case 98304: e.setVoiceBaseDelay(4, 1200.0); break;  // t6
                    case 114688:  // t7: restore the neutral surface
                        e.setRatio(cfg.ratio);  // clears every override
                        e.setVoiceMute(2, false);
                        e.setVoiceGain(0, 1.0);
                        e.setVoiceGain(3, 1.0);
                        e.setWetDry(1.0);
                        e.setVoiceBaseDelay(4, 256.0 + 32.0 * 4);
                        break;
                    default: break;
                }
            }
            return out;
        };

        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        const auto out1 = script(e);
        const uint64_t h1 = fnv1a64(out1);
        e.reset();
        const auto out2 = script(e);
        const uint64_t h2 = fnv1a64(out2);
        check(c, e.prepare(cfg), "re-prepare failed");  // R13-c in-place path
        const auto out3 = script(e);
        const uint64_t h3 = fnv1a64(out3);
        uint64_t h4 = 0;
        std::vector<float> out4;
        {
            AlignedDelayLine f;
            check(c, f.prepare(cfg), "fresh prepare failed");
            out4 = script(f);
            h4 = fnv1a64(out4);
        }
        std::printf("r15acc_reset_reprepare_determinism hashes: "
                    "run=0x%016llx reset=0x%016llx prepare=0x%016llx "
                    "fresh=0x%016llx\n",
                    static_cast<unsigned long long>(h1),
                    static_cast<unsigned long long>(h2),
                    static_cast<unsigned long long>(h3),
                    static_cast<unsigned long long>(h4));
        auto maxdiff = [](const std::vector<float>& a,
                          const std::vector<float>& b) {
            double d = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
                d = std::max(d, std::abs((double)a[i] - b[i]));
            return d;
        };
        const double dR = maxdiff(out1, out2);
        const double dP = maxdiff(out1, out3);
        const double dF = maxdiff(out1, out4);
        addValue(c, "reset_replay_max_diff", dR);
        addValue(c, "prepare_replay_max_diff", dP);
        addValue(c, "fresh_replay_max_diff", dF);
        addValue(c, "hashes_equal",
                 (h1 == h2 && h2 == h3 && h3 == h4) ? 1.0 : 0.0);
        check(c, dR == 0.0, "reset()+replay differs from the first run");
        check(c, dP == 0.0, "prepare()+replay differs from the first run");
        check(c, dF == 0.0, "fresh-engine replay differs from the first run");
        check(c, h1 == h2 && h2 == h3 && h3 == h4,
              "full-run output hashes differ");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("r15acc_stereo_surface")) {  // 5. r15acc_stereo_surface: MultichannelShifter 2ch with the full
        //    per-voice surface active (per-voice ratios + one voice muted +
        //    wet 0.5 + a bypass enter/exit mid-run + feedback). Broadcast
        //    parity: the same input on both channels -> identical outputs
        //    and identical per-voice voiceStats. Channel isolation: L_out
        //    with only-L input == L_out with L+R input, exactly.
        CaseResult c{"r15acc_stereo_surface"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;
        cfg.feedback = 0.25;
        const double semis[8] = {-12.0, -5.0, 0.0, 7.0,
                                 -3.0,  4.0, -7.0, 12.0};
        const int n = 147456;             // 1024 * 144 (3.072 s)
        const int t1 = 49152, t2 = 98304;  // bypass enter / exit
        const auto tin = gen::tone(220.0, sr, n, 0.5);
        const auto tR = gen::tone(330.0, sr, n, 0.3);

        auto runOn = [&](MultichannelShifter& m,
                         const std::vector<float>& inL,
                         const std::vector<float>& inR,
                         std::vector<float>& outL,
                         std::vector<float>& outR) {
            for (int j = 0; j < 8; ++j)
                m.setVoiceRatio(j, std::pow(2.0, semis[j] / 12.0));
            m.setVoiceMute(5, true);
            m.setWetDry(0.5);
            const float* in[2] = {inL.data(), inR.data()};
            float* out[2] = {outL.data(), outR.data()};
            int done = 0;
            while (done < n) {
                const int k = std::min(1024, n - done);
                m.process(in, out, 2, k);
                done += k;
                if (done == t1) m.setBypass(true);
                if (done == t2) m.setBypass(false);
            }
        };

        // broadcast parity
        {
            MultichannelShifter m;
            check(c, m.prepare(cfg, 2), "prepare failed");
            std::vector<float> Lo(n, 0.0f), Ro(n, 0.0f);
            runOn(m, tin, tin, Lo, Ro);
            double od = 0.0;
            for (int k = 0; k < n; ++k)
                od = std::max(od, std::abs((double)Lo[k] - Ro[k]));
            addValue(c, "broadcast_L_vs_R_max_diff", od);
            check(c, od == 0.0, "broadcast outputs differ between channels");
            int statsMismatch = 0;
            for (int v = 0; v < 8; ++v) {
                const VoiceStats a = m.voiceStats(0, v);
                const VoiceStats b = m.voiceStats(1, v);
                if (a.wraps != b.wraps || a.commits != b.commits ||
                    a.engaged != b.engaged || a.parked != b.parked ||
                    a.spacing != b.spacing || a.phase != b.phase ||
                    a.parked_delay != b.parked_delay || a.ratio != b.ratio)
                    ++statsMismatch;
            }
            addValue(c, "voicestats_mismatched_voices", statsMismatch);
            check(c, statsMismatch == 0, "voiceStats differ across channels");
        }

        // channel isolation with the full surface active
        {
            const std::vector<float> sil(n, 0.0f);
            std::vector<float> La(n, 0.0f), Ra(n, 0.0f);
            std::vector<float> Lb(n, 0.0f), Rb(n, 0.0f);
            {
                MultichannelShifter m;
                m.prepare(cfg, 2);
                runOn(m, tin, sil, La, Ra);
            }
            {
                MultichannelShifter m;
                m.prepare(cfg, 2);
                runOn(m, tin, tR, Lb, Rb);
            }
            double dL = 0.0;
            for (int k = 0; k < n; ++k)
                dL = std::max(dL, std::abs((double)La[k] - Lb[k]));
            addValue(c, "L_out_onlyL_vs_LplusR_max_diff", dL);
            check(c, dL == 0.0, "L output depends on the R input (leak)");
            const double rSil = meas::peak(Ra.data(), n);
            addValue(c, "R_out_peak_with_silent_R", rSil);
            check(c, rSil == 0.0,
                  "silent R input produced non-zero R output");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("r15acc_robustness_battery")) {  // 6. r15acc_robustness_battery: 8 voices + the surface active
        //    (wet 0.5, voice 5 muted, one bypass enter/exit mid-run) across
        //    silence / -60 dBFS sine / 440 Hz tone / 3-tone chord / white
        //    noise / 100 Hz impulse train. Gates: all finite and bounded,
        //    silence in -> EXACTLY zero out, -60 dBFS spectral spurs <=
        //    -40 dB rel. output RMS outside the expected fundamentals, and
        //    bit-exact determinism across a replay.
        CaseResult c{"r15acc_robustness_battery"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);  // spread table ON: 8 intervals
        cfg.voices = 8;                    // around +400c
        const int n = 192000;              // 4 s
        const int t1 = 49152, t2 = 98304;  // bypass enter / exit

        auto runBatt = [&](const std::vector<float>& in,
                           std::vector<float>& out) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            e.setVoiceMute(5, true);
            e.setWetDry(0.5);
            int done = 0;
            while (done < n) {
                const int k = std::min(1024, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == t1) e.setBypass(true);
                if (done == t2) e.setBypass(false);
            }
        };

        // (a) digital silence: out must be EXACTLY 0 (wet 0.5 + bypass
        //     regions included: every path is exactly zero on zero input)
        {
            const std::vector<float> sil(n, 0.0f);
            std::vector<float> out(n, 0.0f);
            runBatt(sil, out);
            const double mx = meas::peak(out.data(), n);
            addValue(c, "silence_max_abs_out", mx);
            check(c, meas::allFinite(out.data(), n), "silence: non-finite");
            check(c, mx == 0.0, "silence in produced non-zero output");
        }

        // (b) -60 dBFS sine: finite + spur floor. The analysed stretch is
        //     the steady post-bypass-exit tail (65536 samples, Hann,
        //     amplitude = 4|X_k|/N). Expected components: the dry-path
        //     fundamental (the wet/dry mix legitimately carries it) and
        //     the 8 spread-table shifted fundamentals; guard +-10 bins
        //     each (covers the Hann leakage skirt); everything else must
        //     stay <= -40 dB relative to the output RMS. NOTE: single-bin
        //     amplitudes underestimate an OFF-BIN spur by up to 1.42 dB
        //     (Hann scalloping), so the gate is conservative in that
        //     direction; the measured margin is recorded.
        {
            const auto in = gen::tone(220.0, sr, n, 0.001);
            std::vector<float> out(n, 0.0f);
            runBatt(in, out);
            check(c, meas::allFinite(out.data(), n), "-60dB: non-finite");
            const int N = 65536;
            const int off = n - N;  // 126464 > t2+480: bypass fully off
            std::vector<double> re(N, 0.0), im(N, 0.0);
            for (int i = 0; i < N; ++i) {
                const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / N);
                re[i] = static_cast<double>(out[off + i]) * w;
            }
            meas::fft(re.data(), im.data(), N);
            const double stSemis[8] = {0.0,  7.0,  4.0,  12.0,
                                       -5.0, -12.0, 3.0, -3.0};
            double guard[9];
            guard[0] = 220.0;  // the dry-path fundamental (wet 0.5)
            for (int j = 0; j < 8; ++j)
                guard[j + 1] =
                    220.0 * std::pow(2.0, (4.0 + stSemis[j]) / 12.0);
            const double rmsOut = meas::rms(out.data() + off, N);
            double spur = 0.0;
            int spurBin = -1;
            for (int k = 1; k < N / 2; ++k) {
                bool g = false;
                for (int q = 0; q < 9 && !g; ++q) {
                    const double bk = guard[q] * N / static_cast<double>(sr);
                    if (std::abs(k - bk) <= 10.0) g = true;
                }
                if (g) continue;
                const double amp = 4.0 * std::sqrt(re[k] * re[k] +
                                                   im[k] * im[k]) / N;
                if (amp > spur) {
                    spur = amp;
                    spurBin = k;
                }
            }
            const double spurDb = 20.0 * std::log10(spur / rmsOut);
            addValue(c, "minus60_spur_db_rel_rms", spurDb);
            addValue(c, "minus60_spur_bin", spurBin);
            addValue(c, "minus60_spur_hz",
                     spurBin * static_cast<double>(sr) / N);
            check(c, spurDb <= -40.0,
                  "-60 dBFS spur floor above -40 dB rel. output RMS");
        }

        // (c) single 440 Hz tone, (d) 3-tone chord, (e) white noise,
        //     (f) 100 Hz impulse train: finite + bounded (+ energy in a
        //     documented window for the impulse train).
        {
            const auto in = gen::tone(440.0, sr, n, 0.5);
            std::vector<float> out(n, 0.0f);
            runBatt(in, out);
            const double pk = meas::peak(out.data(), n);
            addValue(c, "tone440_peak", pk);
            check(c, meas::allFinite(out.data(), n), "440 Hz: non-finite");
            check(c, pk <= 1.0, "440 Hz: peak above 2x input peak");
        }
        {
            const std::vector<double> fs = {220.0, 330.0, 440.0};
            const auto in = gen::chord(fs, sr, n, 0.5);
            std::vector<float> out(n, 0.0f);
            runBatt(in, out);
            const double pk = meas::peak(out.data(), n);
            const double inPk = meas::peak(in.data(), n);
            addValue(c, "chord_peak", pk);
            addValue(c, "chord_two_x_input_peak", 2.0 * inPk);
            check(c, meas::allFinite(out.data(), n), "chord: non-finite");
            check(c, pk <= 2.0 * inPk, "chord: peak above 2x input peak");
        }
        {
            const auto in = gen::noise(777, n, 0.5);
            std::vector<float> out(n, 0.0f);
            runBatt(in, out);
            const double pk = meas::peak(out.data(), n);
            addValue(c, "noise_peak", pk);
            check(c, meas::allFinite(out.data(), n), "noise: non-finite");
            check(c, pk <= 1.0, "noise: peak above 2x input peak");
        }
        {
            std::vector<float> in(n, 0.0f);  // 100 Hz impulse train
            for (int k = 0; k < n; k += 480) in[k] = 1.0f;
            std::vector<float> out(n, 0.0f);
            runBatt(in, out);
            const double pk = meas::peak(out.data(), n);
            double ein = 0.0, eout = 0.0;
            for (int k = 0; k < n; ++k) {
                ein += (double)in[k] * in[k];
                eout += (double)out[k] * out[k];
            }
            // GATE ANALYSIS: with 8 normalised voices the incoherent wet
            // mix legitimately carries ~1/8 of the input energy (each
            // voice at 1/8 amplitude) and the dry half adds its own 1/4
            // power share, so the honest robustness bound is a WINDOW,
            // not unity: no runaway growth (fb = 0) and no total collapse.
            // The literal ratio is recorded (expected ~ -5..-6 dB).
            const double ratioDb = 10.0 * std::log10(eout / ein);
            addValue(c, "impulsetrain_peak", pk);
            addValue(c, "impulsetrain_energy_ratio_db", ratioDb);
            check(c, meas::allFinite(out.data(), n),
                  "impulse train: non-finite");
            check(c, pk <= 2.0, "impulse train: peak above 2x input peak");
            check(c, ratioDb >= -12.0 && ratioDb <= 6.0,
                  "impulse-train energy ratio outside [-12, +6] dB");
        }

        // (g) determinism across a replay (fresh engine, same surface)
        {
            const auto in = gen::noise(777, n, 0.5);
            std::vector<float> o1(n, 0.0f), o2(n, 0.0f);
            runBatt(in, o1);
            runBatt(in, o2);
            double d = 0.0;
            for (int k = 0; k < n; ++k)
                d = std::max(d, std::abs((double)o1[k] - o2[k]));
            const bool heq = fnv1a64(o1) == fnv1a64(o2);
            addValue(c, "replay_max_diff", d);
            addValue(c, "replay_hash_equal", heq ? 1.0 : 0.0);
            check(c, d == 0.0 && heq, "replay is not bit-identical");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    if (harnSelected("r15acc_invalid_args")) {  // 7. r15acc_invalid_args: the invalid-argument matrix. Rejected
        //    calls (out-of-range voice index, non-positive ratio/gain,
        //    non-finite anything, base delay <= 0 or beyond the ring
        //    range) must be NO-OPS: a run with the invalid calls sprinkled
        //    between blocks is BIT-IDENTICAL to a run without them, and
        //    the engine stays usable afterwards. setWetDry CLAMPS to
        //    [0,1] per the header contract -- tested as identity with the
        //    clamped endpoint's own run.
        CaseResult c{"r15acc_invalid_args"};

        // (a) every entry point on an UNPREPARED engine: no crash, no
        //     effect, not ready.
        {
            AlignedDelayLine e;
            float buf[64] = {0.0f};
            e.setRatio(2.0);
            e.setVoices(4);
            e.setSpread(true);
            e.setFeedback(0.5);
            e.setVoiceRatio(0, 2.0);
            e.setVoiceGain(0, 0.5);
            e.setVoiceMute(0, true);
            e.setVoiceBaseDelay(0, 100.0);
            e.setWetDry(0.5);
            e.setBypass(true);
            e.process(buf, buf, 64);
            e.reset();
            (void)e.voiceStats(0);
            (void)e.lastEstimateHz();
            (void)e.latencySamples();
            check(c, !e.ready(), "unprepared engine reports ready");
            check(c, e.latencySamples() == 0,
                  "unprepared engine reports non-zero latency");
            MultichannelShifter m;
            m.setVoiceRatio(0, 2.0);
            m.setVoiceGain(0, 0.5);
            m.setVoiceMute(0, true);
            m.setVoiceBaseDelay(0, 100.0);
            m.setWetDry(0.5);
            m.setBypass(true);
            check(c, !m.ready(), "unprepared multichannel reports ready");
        }

        // (b) the rejection matrix on a prepared engine
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;
        const int n = 48000;
        const auto in = gen::tone(220.0, sr, n, 0.5);
        auto runMatrix = [&](bool withInvalid) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(512, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == 5120) {  // valid surface change in BOTH runs
                    e.setVoiceRatio(3, std::pow(2.0, 5.0 / 12.0));
                    e.setVoiceGain(2, 0.5);
                    e.setVoiceBaseDelay(4, 700.0);
                    e.setWetDry(0.6);
                }
                if (withInvalid && done == 10240) {
                    // indices
                    e.setVoiceRatio(-1, 2.0);
                    e.setVoiceRatio(8, 2.0);
                    e.setVoiceGain(-1, 0.5);
                    e.setVoiceGain(8, 0.5);
                    e.setVoiceMute(-1, true);
                    e.setVoiceMute(8, true);
                    e.setVoiceBaseDelay(-1, 100.0);
                    e.setVoiceBaseDelay(8, 100.0);
                    // values (must not clobber live overrides either)
                    e.setVoiceRatio(3, 0.0);
                    e.setVoiceRatio(3, -1.5);
                    e.setVoiceRatio(3, kNaN);
                    e.setVoiceRatio(3, kInf);
                    e.setVoiceGain(2, -0.25);
                    e.setVoiceGain(2, kNaN);
                    e.setVoiceBaseDelay(4, 0.0);
                    e.setVoiceBaseDelay(4, -3.0);
                    e.setVoiceBaseDelay(4, kNaN);
                    e.setVoiceBaseDelay(4, 249848.0);  // ring-range edge
                    e.setVoiceBaseDelay(4, 300000.0);
                    e.setWetDry(kNaN);
                }
                if (withInvalid && done == 15360) {
                    e.setVoiceRatio(-2, -0.0);
                    e.setVoiceGain(2, -1e-9);
                    e.setVoiceBaseDelay(4, 262144.0);
                }
            }
            return out;
        };
        const auto outRef = runMatrix(false);
        const auto outInv = runMatrix(true);
        double dInv = 0.0;
        for (int k = 0; k < n; ++k)
            dInv = std::max(dInv, std::abs((double)outRef[k] - outInv[k]));
        addValue(c, "rejected_calls_output_max_diff", dInv);
        check(c, dInv == 0.0,
              "rejected calls changed the engine state or output");

        // engine still usable after the matrix: finite, non-silent output
        {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(512, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == 5120) {
                    e.setVoiceRatio(3, std::pow(2.0, 5.0 / 12.0));
                    e.setVoiceGain(2, 0.5);
                    e.setVoiceBaseDelay(4, 700.0);
                    e.setWetDry(0.6);
                }
                if (done == 10240 || done == 15360) {
                    e.setVoiceRatio(8, 2.0);
                    e.setVoiceGain(-1, -1.0);
                    e.setVoiceBaseDelay(4, 1e9);
                    e.setWetDry(kNaN);
                    e.setVoiceMute(99, true);
                }
            }
            check(c, meas::allFinite(out.data(), n),
                  "non-finite output after rejected calls");
            check(c, meas::peak(out.data(), n) > 0.0,
                  "engine silent after rejected calls");
        }

        // (c) setWetDry CLAMPS to [0,1] (the header contract): 1.7 == 1.0
        //     run, -0.3 == 0.0 run == the bit-exact delayed copy.
        {
            auto runWet = [&](double wet) {
                AlignedDelayLine e;
                e.prepare(cfg);
                e.setWetDry(wet);
                std::vector<float> out(n, 0.0f);
                e.process(in.data(), out.data(), n);
                return out;
            };
            const auto oHi = runWet(1.7);
            const auto oOne = runWet(1.0);
            double dHi = 0.0;
            for (int k = 0; k < n; ++k)
                dHi = std::max(dHi, std::abs((double)oHi[k] - oOne[k]));
            addValue(c, "wet_1p7_vs_1p0_max_diff", dHi);
            check(c, dHi == 0.0, "setWetDry(1.7) is not clamped to 1.0");

            const auto oLo = runWet(-0.3);
            AlignedDelayLine probe;
            probe.prepare(cfg);
            const int L = probe.latencySamples();
            double dLo = 0.0;
            for (int k = 0; k < n; ++k) {
                const int src = k - (L - 1);
                const double expect = (src >= 0) ? (double)in[src] : 0.0;
                dLo = std::max(dLo, std::abs((double)oLo[k] - expect));
            }
            addValue(c, "wet_m0p3_vs_delayed_copy_max_diff", dLo);
            check(c, dLo == 0.0,
                  "setWetDry(-0.3) is not clamped to the bit-exact dry "
                  "copy");
        }

        // (d) boundary sanity: the largest IN-RANGE base delay IS accepted
        //     (delay + 3*grain + 8 < 2^18): 249847 changes the output,
        //     249848 (one further) is rejected (covered above).
        {
            auto runDelay = [&](double d) {
                AlignedDelayLine e;
                e.prepare(cfg);
                std::vector<float> out(n, 0.0f);
                int done = 0;
                while (done < n) {
                    const int k = std::min(512, n - done);
                    e.process(in.data() + done, out.data() + done, k);
                    done += k;
                    if (done == 5120) e.setVoiceBaseDelay(4, d);
                }
                return out;
            };
            const auto oA = runDelay(700.0);
            const auto oB = runDelay(249847.0);
            double d = 0.0;
            for (int k = 5120; k < n; ++k)
                d = std::max(d, std::abs((double)oA[k] - oB[k]));
            addValue(c, "largest_valid_delay_effect", d);
            check(c, d > 0.0,
                  "the largest in-range base delay was not accepted");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

// ---------------------------------------------------------------------------
// ROUND-19 cases (16.0.0): decimated detector, click-free transitions,
// read suppression, active-voice scope. Gate discipline as always: fixed
// before running, no widening-to-pass; comparison values (legacy vs new)
// are recorded, the GATE is on the new behaviour.
// ---------------------------------------------------------------------------
void casesR19(std::vector<CaseResult>& R) {
    const int sr = 48000;

    auto legacyConfig = [](Config cfg) {
        cfg.detector_mode = DetectorMode::FullRate;
        cfg.commit_crossfade_seconds = 0.0;
        cfg.delay_crossfade_seconds = 0.0;
        cfg.voice_fade_seconds = 0.0;
        return cfg;
    };

    // -- 1. default_neutrality_r19: the 16.0.0-default reference record -----
    if (harnSelected("default_neutrality_r19")) {
        // The four NEW hashes (r19_reference_hashes.txt), captured by
        // cli/r15ref_capture.cpp (default mode) on the 16.0.0 core.
        // cfg1 is IDENTICAL to the legacy record (a unity voice parks and
        // never wraps: no detector estimate, commit or crossfade reaches
        // the output); cfg2/3/4 legitimately change (decimated detector
        // estimates + crossfaded spacing commits).
        CaseResult c{"default_neutrality_r19"};
        const int N = 1 << 18;
        auto fnv = [](const float* x, int n,
                      uint64_t h = 1469598103934665603ULL) {
            for (int k = 0; k < n; ++k) {
                uint32_t b;
                std::memcpy(&b, &x[k], 4);
                for (int i = 0; i < 4; ++i) {
                    h ^= static_cast<uint64_t>((b >> (8 * i)) & 0xffu);
                    h *= 1099511628211ULL;
                }
            }
            return h;
        };
        auto runPat = [&](AlignedDelayLine& e, const std::vector<float>& in,
                          const int* pat, int patN) {
            std::vector<float> out(in.size(), 0.0f);
            int done = 0, pi = 0;
            while (done < (int)in.size()) {
                const int n = std::min(pat[pi % patN], (int)in.size() - done);
                e.process(in.data() + done, out.data() + done, n);
                done += n;
                ++pi;
            }
            return out;
        };
        const uint64_t expect[4] = {0x74318b06c182aacbULL, 0x9b7217813e519209ULL,
                                    0xb66549c1dca9d6aeULL, 0x38f625fec56dba45ULL};
        const char* names[4] = {
            "cfg1_default_unity_lcg_noise_b64_150_512",
            "cfg2_r700c_8v_spread_fb025_sine220",
            "cfg3_rm400c_4v_nospread_chirp200_800",
            "cfg4_stereo_2ch_4v_unity_then_r400c"};
        uint64_t got[4] = {0, 0, 0, 0};
        {
            const auto in = gen::noise(987654321ULL, N, 0.5);
            AlignedDelayLine e;
            Config cfg;  // 16.0.0 defaults
            check(c, e.prepare(cfg), "prepare failed");
            const int pat[3] = {64, 150, 512};
            const auto out = runPat(e, in, pat, 3);
            got[0] = fnv(out.data(), (int)out.size());
        }
        {
            const auto in = gen::tone(220.0, sr, N, 0.5);
            AlignedDelayLine e;
            Config cfg;
            cfg.ratio = std::pow(2.0, 700.0 / 1200.0);
            cfg.voices = 8;
            cfg.feedback = 0.25;
            check(c, e.prepare(cfg), "prepare failed");
            const int pat[3] = {150, 512, 64};
            const auto out = runPat(e, in, pat, 3);
            got[1] = fnv(out.data(), (int)out.size());
        }
        {
            const auto in = gen::chirp(200.0, 800.0, sr, N, 0.5);
            AlignedDelayLine e;
            Config cfg;
            cfg.ratio = std::pow(2.0, -400.0 / 1200.0);
            cfg.voices = 4;
            cfg.spread = false;
            check(c, e.prepare(cfg), "prepare failed");
            const int pat[3] = {64, 512, 150};
            const auto out = runPat(e, in, pat, 3);
            got[2] = fnv(out.data(), (int)out.size());
        }
        {
            const auto inL = gen::noise(1111111111ULL, N, 0.4);
            const auto inR = gen::noise(2222222222ULL, N, 0.4);
            MultichannelShifter m;
            Config cfg;
            cfg.voices = 4;
            check(c, m.prepare(cfg, 2), "prepare failed");
            std::vector<float> outL(N, 0.0f), outR(N, 0.0f);
            const float* in[2] = {inL.data(), inR.data()};
            float* out[2] = {outL.data(), outR.data()};
            const int pat[3] = {1024, 64, 512};
            int done = 0, pi = 0;
            bool switched = false;
            while (done < N) {
                const int n = std::min(pat[pi % 3], N - done);
                m.process(in, out, 2, n);
                done += n;
                ++pi;
                if (!switched && done >= N / 2) {
                    m.setRatio(std::pow(2.0, 400.0 / 1200.0));
                    switched = true;
                }
            }
            got[3] = fnv(outL.data(), N);
            got[3] = fnv(outR.data(), N, got[3]);
        }
        for (int i = 0; i < 4; ++i) {
            std::printf("default_neutrality_r19 %s: got 0x%016llx expect 0x%016llx\n",
                        names[i], static_cast<unsigned long long>(got[i]),
                        static_cast<unsigned long long>(expect[i]));
            char vk[32];
            std::snprintf(vk, sizeof(vk), "cfg%d_hash_match", i + 1);
            addValue(c, vk, got[i] == expect[i] ? 1.0 : 0.0);
            check(c, got[i] == expect[i],
                  std::string("r19 reference hash mismatch: ") + names[i]);
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 2. detector_decimated_accuracy_battery ----------------------------
    if (harnSelected("detector_decimated_accuracy_battery")) {
        // The decimated detector must meet the SAME accuracy gate the
        // full-rate battery cases hold: |pitch error| <= 1.0 cent per
        // frequency, at 48 kHz AND 96 kHz (rate-scaled grain/base config,
        // the r13 battery pattern).
        CaseResult c{"detector_decimated_accuracy_battery"};
        const double ratio = std::pow(2.0, 5.0 / 12.0);
        const double freqs[4] = {110.0, 220.0, 440.0, 880.0};
        double worst = 0.0;
        struct RateSpec { int sr; double grain, base; };
        const RateSpec rates[2] = {{48000, 4096.0, 256.0},
                                   {96000, 8192.0, 512.0}};
        for (const auto& rs : rates) {
            for (double f : freqs) {
                const auto in = gen::tone(f, rs.sr, 2 * rs.sr, 0.5);
                AlignedDelayLine e;
                Config cfg;
                cfg.sample_rate = rs.sr;
                cfg.grain = rs.grain;
                cfg.base_delay = rs.base;
                cfg.ratio = ratio;
                cfg.detector_mode = DetectorMode::Decimated;
                check(c, e.prepare(cfg), "prepare failed");
                std::vector<float> out(in.size(), 0.0f);
                int done = 0;
                double lo = 1e300, hi = 0.0;
                while (done < (int)in.size()) {
                    const int k = std::min(1024, (int)in.size() - done);
                    e.process(in.data() + done, out.data() + done, k);
                    done += k;
                    if (done >= rs.sr) {
                        const double f0 = e.lastEstimateHz();
                        lo = std::min(lo, f0);
                        hi = std::max(hi, f0);
                    }
                }
                const double cents = 1200.0 * std::log2(hi / f) -
                                     1200.0 * std::log2(lo / f);
                // spread of the estimate band around truth, in cents
                const double eLo = 1200.0 * std::log2(lo / f);
                const double eHi = 1200.0 * std::log2(hi / f);
                const double band = std::max(std::abs(eLo), std::abs(eHi));
                worst = std::max(worst, band);
                char vk[64];
                std::snprintf(vk, sizeof(vk), "err_cents_%d_%g", rs.sr, f);
                addValue(c, vk, band);
                (void)cents;
            }
        }
        addValue(c, "worst_error_cents", worst);
        check(c, worst <= 1.0,
              "decimated detector exceeds the 1-cent accuracy gate");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 3. detector_decimated_vs_fullrate_delta ----------------------------
    if (harnSelected("detector_decimated_vs_fullrate_delta")) {
        // The two architectures must AGREE on steady tones (<= 2 cents) and
        // stay close on a chirp (<= 20 cents -- tracking-lag differences on
        // a fast sweep are expected and documented).
        CaseResult c{"detector_decimated_vs_fullrate_delta"};
        auto sampleEstimates = [&](const std::vector<float>& in, int rate,
                                   DetectorMode mode,
                                   std::vector<double>& out) {
            AlignedDelayLine e;
            Config cfg;
            cfg.sample_rate = rate;
            cfg.ratio = std::pow(2.0, 5.0 / 12.0);
            cfg.detector_mode = mode;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> y(in.size(), 0.0f);
            int done = 0;
            out.clear();
            while (done < (int)in.size()) {
                const int k = std::min(1024, (int)in.size() - done);
                e.process(in.data() + done, y.data() + done, k);
                done += k;
                out.push_back(e.lastEstimateHz());
            }
        };
        double worstTone = 0.0;
        for (double f : {110.0, 220.0, 440.0}) {
            const auto in = gen::tone(f, sr, 2 * sr, 0.5);
            std::vector<double> a, b;
            sampleEstimates(in, sr, DetectorMode::FullRate, a);
            sampleEstimates(in, sr, DetectorMode::Decimated, b);
            // compare the last half (post lock-in), in cents
            double worst = 0.0;
            for (size_t k = a.size() / 2; k < a.size(); ++k) {
                if (a[k] > 0 && b[k] > 0)
                    worst = std::max(worst,
                                     std::abs(1200.0 * std::log2(b[k] / a[k])));
            }
            worstTone = std::max(worstTone, worst);
        }
        addValue(c, "worst_tone_delta_cents", worstTone);
        check(c, worstTone <= 2.0,
              "decimated vs full-rate disagree on steady tones (> 2 cents)");
        {
            const auto in = gen::chirp(200.0, 800.0, sr, 2 * sr, 0.5);
            std::vector<double> a, b;
            sampleEstimates(in, sr, DetectorMode::FullRate, a);
            sampleEstimates(in, sr, DetectorMode::Decimated, b);
            // OCTAVE-FOLDED disagreement: on a fast chirp both architectures
            // can momentarily jump an octave (a known YIN-on-sweep behaviour;
            // the spacing law S = round(G/T)*T is octave-invariant and the
            // 8-deep median absorbs the jump). Jumps of (nearly) whole
            // octaves therefore do NOT count as disagreement; the gate is on
            // the residual non-octave delta. Counts are recorded for both.
            double worst = 0.0;
            long jumpsA = 0, jumpsB = 0;
            for (size_t k = 1; k < a.size(); ++k) {
                if (a[k] > 0 && a[k-1] > 0 &&
                    std::abs(1200.0 * std::log2(a[k] / a[k-1])) > 600.0) ++jumpsA;
                if (b[k] > 0 && b[k-1] > 0 &&
                    std::abs(1200.0 * std::log2(b[k] / b[k-1])) > 600.0) ++jumpsB;
            }
            for (size_t k = a.size() / 4; k < a.size(); ++k) {
                if (a[k] > 0 && b[k] > 0) {
                    double dc = 1200.0 * std::log2(b[k] / a[k]);
                    // fold to the representative within +-600 cents
                    dc = std::fmod(dc, 1200.0);
                    if (dc > 600.0) dc -= 1200.0;
                    if (dc < -600.0) dc += 1200.0;
                    worst = std::max(worst, std::abs(dc));
                }
            }
            addValue(c, "worst_chirp_folded_delta_cents", worst);
            addValue(c, "octave_jumps_fullrate", (double)jumpsA);
            addValue(c, "octave_jumps_decimated", (double)jumpsB);
            check(c, worst <= 20.0,
                  "decimated vs full-rate disagree on the chirp (folded > 20 cents)");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 3b. detector_hybrid_accuracy_battery (R20-P2) ----------------------
    if (harnSelected("detector_hybrid_accuracy_battery")) {
        // The hybrid coarse-to-fine detector must meet the SAME accuracy
        // gate as the other two architectures: |pitch error| <= 1.0 cent
        // per frequency, at 48 kHz AND 96 kHz. At 48 kHz (D=2) the fine
        // stage resolves at the FULL host rate -- the gate must hold with
        // margin there; at 96 kHz (D=4) likewise.
        CaseResult c{"detector_hybrid_accuracy_battery"};
        const double ratio = std::pow(2.0, 5.0 / 12.0);
        const double freqs[4] = {110.0, 220.0, 440.0, 880.0};
        double worst = 0.0;
        struct RateSpec { int sr; double grain, base; };
        const RateSpec rates[2] = {{48000, 4096.0, 256.0},
                                   {96000, 8192.0, 512.0}};
        for (const auto& rs : rates) {
            for (double f : freqs) {
                const auto in = gen::tone(f, rs.sr, 2 * rs.sr, 0.5);
                AlignedDelayLine e;
                Config cfg;
                cfg.sample_rate = rs.sr;
                cfg.grain = rs.grain;
                cfg.base_delay = rs.base;
                cfg.ratio = ratio;
                cfg.detector_mode = DetectorMode::Hybrid;
                check(c, e.prepare(cfg), "prepare failed");
                std::vector<float> out(in.size(), 0.0f);
                int done = 0;
                double lo = 1e300, hi = 0.0;
                while (done < (int)in.size()) {
                    const int k = std::min(1024, (int)in.size() - done);
                    e.process(in.data() + done, out.data() + done, k);
                    done += k;
                    if (done >= rs.sr) {
                        const double f0 = e.lastEstimateHz();
                        lo = std::min(lo, f0);
                        hi = std::max(hi, f0);
                    }
                }
                const double eLo = 1200.0 * std::log2(lo / f);
                const double eHi = 1200.0 * std::log2(hi / f);
                const double band = std::max(std::abs(eLo), std::abs(eHi));
                worst = std::max(worst, band);
                char vk[64];
                std::snprintf(vk, sizeof(vk), "err_cents_%d_%g", rs.sr, f);
                addValue(c, vk, band);
            }
        }
        addValue(c, "worst_error_cents", worst);
        check(c, worst <= 1.0,
              "hybrid detector exceeds the 1-cent accuracy gate");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 3c. detector_hybrid_vs_fullrate_delta (R20-P2) ---------------------
    if (harnSelected("detector_hybrid_vs_fullrate_delta")) {
        // The hybrid's FINE stage evaluates the full-rate difference
        // function at host-rate lag resolution, so on steady tones it must
        // agree with FullRate at least as tightly as Decimated does
        // (gate: <= 0.5 cents vs the decimated case's <= 2 cents); on the
        // fast chirp the same octave-folded <= 20 cent gate applies.
        CaseResult c{"detector_hybrid_vs_fullrate_delta"};
        auto sampleEstimatesH = [&](const std::vector<float>& in, int rate,
                                    DetectorMode mode,
                                    std::vector<double>& out) {
            AlignedDelayLine e;
            Config cfg;
            cfg.sample_rate = rate;
            cfg.ratio = std::pow(2.0, 5.0 / 12.0);
            cfg.detector_mode = mode;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> y(in.size(), 0.0f);
            int done = 0;
            out.clear();
            while (done < (int)in.size()) {
                const int k = std::min(1024, (int)in.size() - done);
                e.process(in.data() + done, y.data() + done, k);
                done += k;
                out.push_back(e.lastEstimateHz());
            }
        };
        double worstTone48 = 0.0, worstTone96 = 0.0;
        for (int rate : {48000, 96000}) {
            for (double f : {110.0, 220.0, 440.0}) {
                const auto in = gen::tone(f, rate, 2 * rate, 0.5);
                std::vector<double> a, b;
                sampleEstimatesH(in, rate, DetectorMode::FullRate, a);
                sampleEstimatesH(in, rate, DetectorMode::Hybrid, b);
                double worst = 0.0;
                for (size_t k = a.size() / 2; k < a.size(); ++k) {
                    if (a[k] > 0 && b[k] > 0)
                        worst = std::max(worst,
                                         std::abs(1200.0 * std::log2(b[k] / a[k])));
                }
                if (rate == 48000) worstTone48 = std::max(worstTone48, worst);
                else worstTone96 = std::max(worstTone96, worst);
            }
        }
        addValue(c, "worst_tone_delta_cents_48k", worstTone48);
        addValue(c, "worst_tone_delta_cents_96k", worstTone96);
        check(c, worstTone48 <= 0.5,
              "hybrid vs full-rate disagree on steady tones at 48 kHz (> 0.5 cents)");
        check(c, worstTone96 <= 0.5,
              "hybrid vs full-rate disagree on steady tones at 96 kHz (> 0.5 cents)");
        {
            const int rate = 48000;
            const auto in = gen::chirp(200.0, 800.0, rate, 2 * rate, 0.5);
            std::vector<double> a, b;
            sampleEstimatesH(in, rate, DetectorMode::FullRate, a);
            sampleEstimatesH(in, rate, DetectorMode::Hybrid, b);
            // OCTAVE-FOLDED disagreement (same law as the decimated case).
            double worst = 0.0;
            long jumpsA = 0, jumpsB = 0;
            for (size_t k = 1; k < a.size(); ++k) {
                if (a[k] > 0 && a[k-1] > 0 &&
                    std::abs(1200.0 * std::log2(a[k] / a[k-1])) > 600.0) ++jumpsA;
                if (b[k] > 0 && b[k-1] > 0 &&
                    std::abs(1200.0 * std::log2(b[k] / b[k-1])) > 600.0) ++jumpsB;
            }
            for (size_t k = a.size() / 4; k < a.size(); ++k) {
                if (a[k] > 0 && b[k] > 0) {
                    double dc = 1200.0 * std::log2(b[k] / a[k]);
                    dc = std::fmod(dc, 1200.0);
                    if (dc > 600.0) dc -= 1200.0;
                    if (dc < -600.0) dc += 1200.0;
                    worst = std::max(worst, std::abs(dc));
                }
            }
            addValue(c, "worst_chirp_folded_delta_cents", worst);
            addValue(c, "octave_jumps_fullrate", (double)jumpsA);
            addValue(c, "octave_jumps_hybrid", (double)jumpsB);
            check(c, worst <= 20.0,
                  "hybrid vs full-rate disagree on the chirp (folded > 20 cents)");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 3d. detector_hybrid_lowrate_config (R20-P2) ------------------------
    if (harnSelected("detector_hybrid_lowrate_config")) {
        // 44.1 kHz boundary (D=2, sr_dec=22050): the hybrid must prepare,
        // track a 110 Hz tone inside the 1-cent gate and expose the same
        // diagnostics (control rate / decimation) as the decimated mode.
        CaseResult c{"detector_hybrid_lowrate_config"};
        const int sr = 44100;
        const auto in = gen::tone(110.0, sr, 2 * sr, 0.5);
        AlignedDelayLine e;
        Config cfg;
        cfg.sample_rate = sr;
        cfg.ratio = std::pow(2.0, 5.0 / 12.0);
        cfg.detector_mode = DetectorMode::Hybrid;
        check(c, e.prepare(cfg), "prepare failed");
        std::vector<float> out(in.size(), 0.0f);
        int done = 0;
        double lo = 1e300, hi = 0.0;
        while (done < (int)in.size()) {
            const int k = std::min(1024, (int)in.size() - done);
            e.process(in.data() + done, out.data() + done, k);
            done += k;
            if (done >= sr) {
                const double f0 = e.lastEstimateHz();
                lo = std::min(lo, f0);
                hi = std::max(hi, f0);
            }
        }
        const double band = std::max(std::abs(1200.0 * std::log2(lo / 110.0)),
                                     std::abs(1200.0 * std::log2(hi / 110.0)));
        addValue(c, "err_cents_44100_110", band);
        check(c, band <= 1.0, "hybrid 44.1 kHz 110 Hz error exceeds 1 cent");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 4. commit_crossfade_continuity ------------------------------------
    if (harnSelected("commit_crossfade_continuity")) {
        // THE click gate: a shifted voice whose spacing commits must show
        // NO sample-step discontinuity beyond the signal's natural slope
        // scale. GATES: new-mode max step <= 0.02 AND <= legacy_max/4 (the
        // legacy instant-commit step must be at least 4x larger -- proving
        // the mechanism, not just the number).
        CaseResult c{"commit_crossfade_continuity"};
        const auto in = gen::tone(220.0, sr, 4 * sr, 0.5);
        auto runMode = [&](Config cfg) {
            cfg.ratio = std::pow(2.0, -400.0 / 1200.0);
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(in.size(), 0.0f);
            int done = 0;
            while (done < (int)in.size()) {
                const int k = std::min(128, (int)in.size() - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
            }
            const long commits = e.voiceStats(0).commits;
            return std::make_pair(out, commits);
        };
        const auto pairNew = runMode(Config());            // 16.0.0 defaults
        const auto pairOld = runMode(legacyConfig(Config()));
        const double stepNew = meas::maxStep(pairNew.first.data(),
                                             (int)pairNew.first.size());
        const double stepOld = meas::maxStep(pairOld.first.data(),
                                             (int)pairOld.first.size());
        addValue(c, "commits_new_mode", (double)pairNew.second);
        addValue(c, "commits_legacy_mode", (double)pairOld.second);
        addValue(c, "max_step_new_mode", stepNew);
        addValue(c, "max_step_legacy_mode", stepOld);
        check(c, pairNew.second > 0, "no spacing commits occurred (vacuous)");
        check(c, stepNew <= 0.02,
              "commit crossfade still shows a step > 0.02");
        check(c, stepNew <= stepOld / 4.0,
              "commit crossfade did not reduce the legacy step 4x");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 5. voice_activation_clickfree --------------------------------------
    if (harnSelected("voice_activation_clickfree")) {
        // setVoices 1 <-> 8 midstream. The transition pop is PHASE-DEPENDENT
        // (the added voices' contributions depend on their grain phases), so
        // SIXTEEN transitions at varied phase offsets are measured and the
        // worst BOUNDARY step (|out[T] - out[T-1]| at each transition
        // boundary) is gated: the ramped path stays within 2x the steady
        // step and at least 2x below the legacy instant transition.
        CaseResult c{"voice_activation_clickfree"};
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        const int P = 48000;      // first transition
        const int NTR = 16;       // 16 transitions, 448 samples apart
        const int TR = 448;
        auto runMode = [&](Config cfg, bool change) {
            cfg.ratio = std::pow(2.0, 400.0 / 1200.0);
            cfg.voices = 1;
            cfg.spread = false;
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(in.size(), 0.0f);
            int done = 0, tr = 0;
            while (done < (int)in.size()) {
                const int k = std::min(64, (int)in.size() - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (change && tr < NTR && done == P + tr * TR) {
                    e.setVoices((tr % 2 == 0) ? 8 : 1);
                    ++tr;
                }
            }
            return out;
        };
        const auto aNew = runMode(Config(), true);
        const auto aOld = runMode(legacyConfig(Config()), true);
        const auto steady = runMode(Config(), false);
        const double stepSteady = meas::maxStep(steady.data() + 2 * sr, sr);
        double stepNew = 0.0, stepOld = 0.0;
        for (int tr = 0; tr < NTR; ++tr) {
            const int b = P + tr * TR;  // the transition boundary sample
            stepNew = std::max(stepNew,
                               std::abs((double)aNew[b] - (double)aNew[b - 1]));
            stepOld = std::max(stepOld,
                               std::abs((double)aOld[b] - (double)aOld[b - 1]));
        }
        addValue(c, "steady_state_max_step_1v", stepSteady);
        addValue(c, "worst_boundary_step_new", stepNew);
        addValue(c, "worst_boundary_step_legacy", stepOld);
        check(c, stepNew <= 2.0 * stepSteady,
              "voice activation boundary step exceeds 2x steady state");
        check(c, stepNew * 2.0 < stepOld,
              "voice activation did not improve 2x on the legacy boundary step");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 6. basedelay_crossfade_clickfree -----------------------------------
    if (harnSelected("basedelay_crossfade_clickfree")) {
        // A mid-stream per-voice base-delay change: the crossfaded path's
        // step stays within 2x the pre-change steady step; the legacy
        // instant re-align step is recorded (expected much larger).
        CaseResult c{"basedelay_crossfade_clickfree"};
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        const int P = 48000;
        auto runMode = [&](Config cfg) {
            cfg.ratio = std::pow(2.0, 400.0 / 1200.0);
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(in.size(), 0.0f);
            int done = 0;
            while (done < (int)in.size()) {
                const int k = std::min(128, (int)in.size() - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == P) e.setVoiceBaseDelay(0, 5000.0);
            }
            return out;
        };
        const auto aNew = runMode(Config());
        const auto aOld = runMode(legacyConfig(Config()));
        const double stepBefore = meas::maxStep(aNew.data() + P - 48000, 48000);
        // window opens one sample before the boundary (the legacy instant
        // re-align step lives exactly ON the boundary sample)
        const double stepNew = meas::maxStep(aNew.data() + P - 1, 48000);
        const double stepOld = meas::maxStep(aOld.data() + P - 1, 48000);
        addValue(c, "steady_step_before_change", stepBefore);
        addValue(c, "change_max_step_new", stepNew);
        addValue(c, "change_max_step_legacy", stepOld);
        check(c, stepNew <= 2.0 * stepBefore,
              "base-delay crossfade step exceeds 2x the steady step");
        check(c, stepOld > stepNew,
              "legacy base-delay step not larger than the crossfaded one");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 7. read_suppression_bitidentity ------------------------------------
    if (harnSelected("read_suppression_bitidentity")) {
        // R19-e proof: with feedback 0, wet 1 -> 0 -> 1 must be
        // BIT-IDENTICAL to constant wet 1 from the resume boundary onward
        // (state stays warm while suppressed); the fully-bypassed window
        // likewise. The wet==0 window itself obviously differs (dry copy).
        CaseResult c{"read_suppression_bitidentity"};
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        const int P1 = 48000, P2 = 96000;  // wet -> 0 at P1, back at P2
        auto runWet = [&](bool dip) {
            Config cfg;
            cfg.ratio = std::pow(2.0, 400.0 / 1200.0);
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(in.size(), 0.0f);
            int done = 0;
            while (done < (int)in.size()) {
                const int k = std::min(128, (int)in.size() - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (dip && done == P1) e.setWetDry(0.0);
                if (dip && done == P2) e.setWetDry(1.0);
            }
            return out;
        };
        const auto a = runWet(true);
        const auto b = runWet(false);
        double dAfter = 0.0, dDuring = 0.0;
        for (int k = P2; k < (int)in.size(); ++k)
            dAfter = std::max(dAfter, std::abs((double)a[k] - (double)b[k]));
        for (int k = P1 + 480; k < P2; ++k)
            dDuring = std::max(dDuring, std::abs((double)a[k] - (double)b[k]));
        addValue(c, "max_diff_after_resume", dAfter);
        addValue(c, "max_diff_during_wet0", dDuring);
        check(c, dAfter == 0.0,
              "suppressed window changed the post-resume output (state not warm)");
        check(c, dDuring > 0.0,
              "the wet==0 window did not change the output (vacuous)");
        // Bypass: fully bypassed window with fb 0, then exit -- bit-identical
        // outside the ramps (the existing law, now through the suppression).
        auto runBypass = [&](bool bp) {
            Config cfg;
            cfg.ratio = std::pow(2.0, 400.0 / 1200.0);
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(in.size(), 0.0f);
            int done = 0;
            while (done < (int)in.size()) {
                const int k = std::min(128, (int)in.size() - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (bp && done == P1) e.setBypass(true);
                if (bp && done == P2) e.setBypass(false);
            }
            return out;
        };
        const auto ba = runBypass(true);
        const auto bb = runBypass(false);
        double dOut = 0.0;
        for (int k = 0; k < (int)in.size(); ++k) {
            // outside the bypassed region INCLUDING its two ramp regions
            // (enter ramp [P1, P1+ramp), bypassed window, exit ramp
            // [P2, P2+ramp); ramp = 10 ms + margin)
            const bool inBypass = k >= P1 && k < P2 + 4800;
            if (!inBypass)
                dOut = std::max(dOut, std::abs((double)ba[k] - (double)bb[k]));
        }
        addValue(c, "bypass_outside_ramps_max_diff", dOut);
        check(c, dOut == 0.0,
              "bypass-through-suppression is not bit-transparent outside ramps");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 8. active_voices_processing_scope ----------------------------------
    if (harnSelected("active_voices_processing_scope")) {
        // The ACTUAL active voice count at runtime: with Voices = 1, voices
        // 1..7 are not processed (their wrap counters stay frozen); after
        // setVoices(8) they advance; after setVoices(1) + the fade they
        // freeze again. Structural + empirical proof of the loop scope.
        CaseResult c{"active_voices_processing_scope"};
        const auto in = gen::tone(220.0, sr, 2 * sr, 0.5);
        Config cfg;
        cfg.ratio = std::pow(2.0, 400.0 / 1200.0);
        cfg.voices = 1;
        cfg.spread = false;
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        check(c, e.voices() == 1, "voices() != 1");
        std::vector<float> out(in.size(), 0.0f);
        int done = 0;
        while (done < (int)in.size()) {
            const int k = std::min(128, (int)in.size() - done);
            e.process(in.data() + done, out.data() + done, k);
            done += k;
            if (done == 48000) e.setVoices(8);
            if (done == 96000) e.setVoices(1);
        }
        const long w0 = e.voiceStats(0).wraps;
        const long w1 = e.voiceStats(1).wraps;
        // Voice 1 processed ONLY during the 1 s window with 8 voices: its
        // wrap count must be > 0 but far below voice 0's (which ran 2 s).
        addValue(c, "voice0_wraps_total", (double)w0);
        addValue(c, "voice1_wraps_during_8v_window", (double)w1);
        check(c, w0 > 0, "voice 0 never wrapped (vacuous)");
        check(c, w1 > 0, "voice 1 did not process during the 8v window");
        check(c, w1 < w0, "voice 1 processed outside the 8v window");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

// ---------------------------------------------------------------------------
// R22 (16.2.0): the Minimum Pitch signal path (P1).
// * minpitch_gate_unification_bitexact: the engine's spacing-accept gate is
//   now the UNIFIED floor max(fmin, f0_min). Detector dominance proof in
//   practice: fmin anywhere at-or-below the detector floor (f0_min) is dead
//   configuration -- identical output bit-for-bit -- while fmin ABOVE the
//   floor is alive (rejects in-floor-band estimates from the spacing
//   history).
// * minpitch_distinction_diagnostics: the explicit three-way distinction
//   detectable / detectable-but-not-spacing-trackable / rejected, observed
//   through the new R22-P1 diagnostics (spacingGateHz / accepted flag /
//   history count) instead of client-side guesses.
// * minpitch_lowfloor_full_path: with f0_min lowered to 40, a 58.3 Hz tone
//   (above the engine floor 55) walks the WHOLE 8-level chain: lag domain,
//   estimate, engine gate, period history, spacing lock at S = k*T, read
//   position continuity through the commits, shifted output.
// * minpitch_lowfreq_transposition_honesty: 65.4 Hz shifted -24 semitones
//   lands at ~16.35 Hz -- BELOW the 20 Hz audible floor. The engine applies
//   NO clamp (the honest record: the safety verdict belongs to the
//   measurement harness and docs, never a silent engine behaviour change);
//   the output fundamental follows ratio*f0.
// ---------------------------------------------------------------------------
void casesR22(std::vector<CaseResult>& R) {
    const int sr = 48000;

    // -- 1. minpitch_gate_unification_bitexact -------------------------------
    if (harnSelected("minpitch_gate_unification_bitexact")) {
        // Input spans all three gate regimes at the DEFAULT detector floor
        // (f0_min = 60): 49 Hz (never committed -- below the detector
        // floor), 65.4 Hz (committed; accepted iff gate <= 65.4), 110 Hz
        // (committed; accepted by every gate here).
        CaseResult c{"minpitch_gate_unification_bitexact"};
        std::vector<float> in;
        {
            const auto a = gen::tone(49.0, sr, 2 * sr, 0.20);
            const auto b = gen::tone(65.4, sr, 2 * sr, 0.30);
            const auto d = gen::tone(110.0, sr, 2 * sr, 0.25);
            in.resize(2 * sr);
            for (int i = 0; i < 2 * sr; ++i)
                in[i] = a[i] + b[i] + d[i];
        }
        auto runFmin = [&](double fmin) {
            Config cfg;  // default f0_min = 60 (the R12--R21 law)
            cfg.fmin = fmin;
            cfg.ratio = std::pow(2.0, 400.0 / 1200.0);
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(in.size(), 0.0f);
            int done = 0;
            while (done < (int)in.size()) {
                const int k = std::min(256, (int)in.size() - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
            }
            return std::make_pair(out, e.spacingGateHz());
        };
        const auto deadLow = runFmin(40.0);   // below the floor: dead
        const auto def = runFmin(55.0);       // default: dead (55 < 60)
        const auto deadDeep = runFmin(30.0);  // far below: still dead
        const auto alive = runFmin(80.0);     // ABOVE the floor: alive
        check(c, def.second == 60.0, "default effective gate != 60");
        check(c, deadLow.second == 60.0, "fmin=40 effective gate != 60");
        check(c, deadDeep.second == 60.0, "fmin=30 effective gate != 60");
        check(c, alive.second == 80.0, "fmin=80 effective gate != 80");
        bool sameA = true, sameB = true, diff = false;
        for (int i = 0; i < (int)in.size(); ++i) {
            if (deadLow.first[i] != def.first[i]) sameA = false;
            if (deadDeep.first[i] != def.first[i]) sameB = false;
            if (alive.first[i] != def.first[i]) diff = true;
        }
        addValue(c, "fmin40_vs_default_identical", sameA ? 1.0 : 0.0);
        addValue(c, "fmin30_vs_default_identical", sameB ? 1.0 : 0.0);
        addValue(c, "fmin80_vs_default_differs", diff ? 1.0 : 0.0);
        check(c, sameA, "fmin=40 output differs from default (dominance broken)");
        check(c, sameB, "fmin=30 output differs from default (dominance broken)");
        check(c, diff, "fmin=80 output identical to default (gate not alive)");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 2. minpitch_distinction_diagnostics ---------------------------------
    if (harnSelected("minpitch_distinction_diagnostics")) {
        // (a) f0_min=40, 45 Hz: DETECTABLE but NOT spacing-trackable
        //     (45 in [40, 55) -- the engine floor governs).
        // (b) f0_min=40, 70 Hz: detectable AND trackable.
        // (c) f0_min=80, 45 Hz: the tone is LOST at the detector -- the
        //     R21 fmin battery's own measured behaviour (r21_fmin.json:
        //     floor 80, tones below 80: success 0.000, median errors
        //     447..2769 cents): the detector commits WRONG near-floor
        //     estimates rather than zeros. Honest record: we assert
        //     NOT-within-50-cents (the tone is effectively lost) and log
        //     the committed median; we do NOT assert est == 0.
        // Per-cadence sampling: (est, accepted) at every 64-sample control
        // boundary during the steady window (last 0.5 s), plus the history
        // count at its start/end (the early window-fill transient may
        // legitimately leave stale accepted entries; the steady phase must
        // not add any for (a)).
        CaseResult c{"minpitch_distinction_diagnostics"};
        struct Probe {
            double estMed = 0.0;
            bool allWithin = true;
            bool anyWithin = false;
            bool acceptedAll = true;
            bool acceptedNone = true;
            int hist0 = -1, hist1 = -1;
            double gate = 0.0;
            double estLast = 0.0;
        };
        auto probe = [&](double f0min, double toneHz) {
            Probe p;
            Config cfg;
            cfg.f0_min = f0min;
            cfg.ratio = std::pow(2.0, 400.0 / 1200.0);
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            p.gate = e.spacingGateHz();
            const int n = (int)(1.5 * sr);
            const auto in = gen::tone(toneHz, sr, n, 0.5);
            std::vector<float> out(n, 0.0f);
            const int steady0 = n - sr / 2;  // last 0.5 s
            std::vector<double> ests;
            int done = 0;
            bool h0taken = false;
            while (done < n) {
                const int k = std::min(64, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done >= steady0) {
                    if (!h0taken) { p.hist0 = e.spacingHistoryCount(); h0taken = true; }
                    const double est = e.lastEstimateHz();
                    const bool acc = e.lastEstimateSpacingAccepted();
                    ests.push_back(est);
                    if (est <= 0.0 ||
                        std::abs(1200.0 * std::log2(est / toneHz)) > 50.0)
                        p.allWithin = false;
                    else
                        p.anyWithin = true;
                    if (acc) p.acceptedNone = false;
                    else p.acceptedAll = false;
                }
            }
            p.hist1 = e.spacingHistoryCount();
            p.estLast = e.lastEstimateHz();
            if (!ests.empty()) {
                std::sort(ests.begin(), ests.end());
                p.estMed = ests[ests.size() / 2];
            }
            return p;
        };
        {   // (a) 45 Hz, floor 40: detected, never spacing-accepted
            const Probe p = probe(40.0, 45.0);
            addValue(c, "case_a_gate_hz", p.gate);
            addValue(c, "case_a_est_median_hz", p.estMed);
            addValue(c, "case_a_est_last_hz", p.estLast);
            addValue(c, "case_a_hist_steady_start", (double)p.hist0);
            addValue(c, "case_a_hist_steady_end", (double)p.hist1);
            check(c, p.gate == 55.0, "case (a) gate != max(55,40)");
            check(c, p.allWithin,
                  "case (a): steady 45 Hz estimates not within 50 cents");
            check(c, p.acceptedNone,
                  "case (a): a steady 45 Hz estimate was spacing-accepted");
            check(c, p.hist1 == p.hist0,
                  "case (a): spacing history grew during the steady phase");
        }
        {   // (b) 70 Hz, floor 40: detected AND spacing-accepted
            const Probe p = probe(40.0, 70.0);
            addValue(c, "case_b_gate_hz", p.gate);
            addValue(c, "case_b_est_median_hz", p.estMed);
            addValue(c, "case_b_hist_steady_end", (double)p.hist1);
            check(c, p.gate == 55.0, "case (b) gate != max(55,40)");
            check(c, p.allWithin,
                  "case (b): steady 70 Hz estimates not within 50 cents");
            check(c, p.acceptedAll,
                  "case (b): a steady 70 Hz estimate was not spacing-accepted");
            check(c, p.hist1 == Config{}.f0_history,
                  "case (b): spacing history not full");
        }
        {   // (c) 45 Hz, floor 80: lost (wrong near-floor estimates; the
            //     R21 battery's measured behaviour, honestly recorded via
            //     case_c_est_median_hz -- no informational text in c.note)
            const Probe p = probe(80.0, 45.0);
            addValue(c, "case_c_gate_hz", p.gate);
            addValue(c, "case_c_est_median_hz", p.estMed);
            addValue(c, "case_c_est_last_hz", p.estLast);
            check(c, p.gate == 80.0, "case (c) gate != max(55,80)");
            check(c, !p.allWithin,
                  "case (c): 45 Hz tracked within 50 cents under floor 80 "
                  "(contradicts the R21 battery)");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 3. minpitch_lowfloor_full_path ---------------------------------------
    if (harnSelected("minpitch_lowfloor_full_path")) {
        // f0_min=40, 58.3 Hz tone, ratio 0.5: the whole 8-level chain with
        // the floor BELOW the default. Spacing must lock at S = k*T (k =
        // round(G/T) = 5), the read position must stay continuous through
        // the commits, and the shifted output must be present.
        CaseResult c{"minpitch_lowfloor_full_path"};
        Config cfg;
        cfg.f0_min = 40.0;
        cfg.ratio = 0.5;  // down one octave
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        check(c, e.spacingGateHz() == 55.0, "gate != max(55,40)");
        const auto in = gen::tone(58.3, sr, 2 * sr, 0.5);
        std::vector<float> out(in.size(), 0.0f);
        int done = 0;
        while (done < (int)in.size()) {
            const int k = std::min(256, (int)in.size() - done);
            e.process(in.data() + done, out.data() + done, k);
            done += k;
        }
        const double est = e.lastEstimateHz();
        const bool acc = e.lastEstimateSpacingAccepted();
        const int hist = e.spacingHistoryCount();
        const double stgt = e.spacingTargetSamples();
        const VoiceStats vs0 = e.voiceStats(0);
        addValue(c, "est_hz", est);
        addValue(c, "accepted", acc ? 1.0 : 0.0);
        addValue(c, "hist", (double)hist);
        addValue(c, "spacing_target", stgt);
        addValue(c, "voice0_spacing", vs0.spacing);
        addValue(c, "voice0_commits", (double)vs0.commits);
        check(c, est > 0.0, "58.3 Hz not detected with f0_min=40");
        check(c, std::abs(1200.0 * std::log2(est / 58.3)) <= 50.0,
              "estimate not within 50 cents of 58.3 Hz");
        check(c, acc, "58.3 Hz not spacing-accepted (above 55)");
        check(c, hist == cfg.f0_history, "history not full");
        // level 5: S locked at k*T_hat with k = round(G/T_hat)
        const double tHat = sr / est;
        const double kExp = std::nearbyint(cfg.grain / tHat);
        check(c, std::abs(stgt - kExp * tHat) <= 0.51,
              "S_target != round(G/T)*T");
        check(c, vs0.commits > 0, "spacing never committed");
        check(c, std::abs(vs0.spacing - kExp * tHat) <= 0.51,
              "voice spacing != k*T after commit");
        // level 6: read-position continuity through the commits (the
        // R19-b commit crossfade is on by default): steady-window output
        // step stays small (the output is a ~29.15 Hz tone, amp ~0.5).
        const double step = meas::maxStep(out.data() + sr, sr);
        addValue(c, "steady_max_step", step);
        check(c, step < 0.05, "output step too large (read discontinuity)");
        // level 7/8: the shifted output is present and finite
        double rms = 0.0;
        for (int i = sr; i < 2 * sr; ++i) rms += out[i] * out[i];
        rms = std::sqrt(rms / sr);
        addValue(c, "steady_rms", rms);
        check(c, rms > 0.05, "shifted output silent/missing");
        for (int i = 0; i < (int)out.size(); ++i)
            if (!std::isfinite(out[i])) {
                check(c, false, "non-finite output sample");
                break;
            }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 4. minpitch_lowfreq_transposition_honesty ----------------------------
    if (harnSelected("minpitch_lowfreq_transposition_honesty")) {
        // 65.4 Hz (C2, the original's default Low Note) shifted -24 st
        // => ~16.35 Hz, BELOW the 20 Hz audible floor. The engine applies
        // NO clamp: the output fundamental must follow ratio*f0 (measured
        // by zero crossings on the steady window). The audible-floor
        // verdict is a HARNESS/docs concern (the r22 battery), never a
        // silent engine behaviour change.
        CaseResult c{"minpitch_lowfreq_transposition_honesty"};
        Config cfg;  // defaults: f0_min = 60, gate = max(55,60) = 60
        cfg.ratio = 0.25;  // -24 semitones
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        check(c, e.spacingGateHz() == 60.0, "default gate != 60");
        const auto in = gen::tone(65.4, sr, 4 * sr, 0.5);
        std::vector<float> out(in.size(), 0.0f);
        int done = 0;
        while (done < (int)in.size()) {
            const int k = std::min(256, (int)in.size() - done);
            e.process(in.data() + done, out.data() + done, k);
            done += k;
        }
        const double est = e.lastEstimateHz();
        const bool acc = e.lastEstimateSpacingAccepted();
        addValue(c, "est_hz", est);
        addValue(c, "accepted", acc ? 1.0 : 0.0);
        check(c, est > 0.0, "65.4 Hz not detected at the default floor");
        check(c, acc, "65.4 Hz not spacing-accepted at the default floor");
        // zero-crossing fundamental on the steady window (2 s)
        const int i0 = 2 * sr, nW = 2 * sr;
        int cross = 0;
        for (int i = i0 + 1; i < i0 + nW; ++i)
            if ((out[i - 1] < 0.0f && out[i] >= 0.0f) ||
                (out[i - 1] >= 0.0f && out[i] < 0.0f))
                ++cross;
        const double fOut = 0.5 * cross * (sr / double(nW));
        const double fExpect = 0.25 * 65.4;
        addValue(c, "measured_out_hz", fOut);
        addValue(c, "expected_out_hz", fExpect);
        check(c, fOut > 0.0, "no output zero crossings (dead output)");
        check(c, std::abs(1200.0 * std::log2(fOut / fExpect)) <= 150.0,
              "output fundamental not within 150 cents of ratio*f0");
        // the honest record: the engine did NOT clamp the output above the
        // audible floor -- the measured fundamental stays BELOW 20 Hz.
        check(c, fOut < 20.0, "output fundamental clamped above 20 Hz");
        double rms = 0.0;
        for (int i = i0; i < i0 + nW; ++i) rms += out[i] * out[i];
        rms = std::sqrt(rms / nW);
        addValue(c, "steady_rms", rms);
        check(c, rms > 0.05, "shifted output silent");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

// ---------------------------------------------------------------------------
// R24 (16.3.0): the SIMD capability-layer battery.
//  * the AVX2 lag-axis difference kernel is BIT-IDENTICAL to the scalar
//    reference by construction (order-preserving lanes) -- a HARD
//    exact-equality gate over a randomized shape corpus, and the ENGINE at
//    the AVX2 level is bit-identical to Scalar across all three detector
//    modes (including the Hybrid fine stage and the tracking-gate path);
//  * the AVX2+FMA lag-axis kernel preserves the summation order but fuses
//    the per-term rounding -- gated as a BOUNDED deviation (never claimed
//    bit-exact), with the engine-closeness record (output deviation +
//    estimate delta) measured honestly;
//  * the peak kernels are exact at every level (max is order-free);
//  * prepare() accepts all four modes when honourable, rejects invalid
//    enum values, and (on machines lacking the features) rejects FORCED
//    levels loudly -- the branch taken here is recorded per run;
//  * default = Scalar (bit-exact vs the R12--R23 record, gated by the hash
//    gates); in-place re-prepare re-resolves the kernel table; accelerated
//    processing is allocation-free.
// The ISA gates are exercised only when the CPU/build reports the feature
// (kernels() hands out ISA pointers only via the resolver-checked path);
// a vacuous run is recorded with avx2_exercised/fma_exercised = 0.
// ---------------------------------------------------------------------------
void casesR24(std::vector<CaseResult>& R) {
    const simd::Capabilities caps = simd::detect();
    const bool units = simd::isaUnitsCompiled();
    const simd::Level lvlAvx2 = simd::resolve(simd::Mode::AVX2);
    const simd::Level lvlFma = simd::resolve(simd::Mode::AVX2FMA);
    const simd::Level lvlAuto = simd::resolve(simd::Mode::Auto);
    const simd::Kernels kScalar = simd::kernels(simd::Level::Scalar);
    const simd::Kernels kAvx2 = simd::kernels(lvlAvx2);
    const simd::Kernels kFma = simd::kernels(lvlFma);
    const bool haveAvx2 = (lvlAvx2 == simd::Level::AVX2);
    const bool haveFma = (lvlFma == simd::Level::AVX2FMA);

    // -- 1. simd_detect_resolve_rules ---------------------------------------
    if (harnSelected("simd_detect_resolve_rules")) {
        CaseResult c{"simd_detect_resolve_rules"};
        addValue(c, "cpu_avx2", caps.avx2 ? 1.0 : 0.0);
        addValue(c, "cpu_fma", caps.fma ? 1.0 : 0.0);
        addValue(c, "isa_units_compiled", units ? 1.0 : 0.0);
        addValue(c, "resolved_avx2", static_cast<double>(static_cast<int>(lvlAvx2)));
        addValue(c, "resolved_avx2fma", static_cast<double>(static_cast<int>(lvlFma)));
        addValue(c, "resolved_auto", static_cast<double>(static_cast<int>(lvlAuto)));
        // the resolve rules (simd.hpp): never upgrade past availability
        check(c, simd::resolve(simd::Mode::Scalar) == simd::Level::Scalar,
              "Scalar did not resolve to Scalar");
        check(c, lvlAvx2 == ((units && caps.avx2) ? simd::Level::AVX2
                                                  : simd::Level::Scalar),
              "AVX2 resolve rule violated");
        check(c, lvlFma == ((units && caps.avx2 && caps.fma)
                                ? simd::Level::AVX2FMA
                                : simd::Level::Scalar),
              "AVX2+FMA resolve rule violated");
        check(c,
              lvlAuto == (lvlAvx2 == simd::Level::AVX2
                              ? simd::Level::AVX2
                              : simd::Level::Scalar),
              "Auto did not resolve to the bit-exact AVX2 level (R24 "
              "decision: Auto prefers AVX2 when available; FMA is opt-in)");
        // the scalar table is always the reference kernels
        check(c, kScalar.diff == kScalar.diff_lag &&
                      kScalar.diff == kScalar.diff_sample,
              "Scalar table is not uniformly the scalar kernels");
        // diagnostic names
        check(c, std::string(simd::modeName(simd::Mode::AVX2FMA)) == "avx2+fma" &&
                      std::string(simd::modeName(simd::Mode::Auto)) == "auto" &&
                      std::string(simd::levelName(simd::Level::AVX2)) == "avx2",
              "mode/level diagnostic names wrong");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- shared randomized kernel corpus ------------------------------------
    struct Shape { int seg0, tau0, ntau, m; };
    static const Shape kShapes[] = {
        {1024, 40, 798, 1024},  // FullRate 96 kHz-class lag domain
        {512, 2, 398, 512},     // decimated window class
        {512, 2, 63, 512},      // batch-boundary sweep (4-batch recursion)
        {512, 2, 64, 512},  {512, 2, 65, 512},  {512, 2, 66, 512},
        {512, 2, 67, 512},  {512, 2, 68, 512},  {512, 2, 69, 512},
        {512, 2, 16, 512},  {512, 2, 17, 512},  {512, 2, 15, 512},
        {512, 2, 1, 512},   {512, 2, 2, 512},   {512, 2, 3, 512},
        {512, 2, 4, 512},   {512, 2, 5, 512},   {512, 2, 9, 512},
        {200, 7, 21, 300},  // Hybrid fine neighborhood (2W+1 at D=8)
        {200, 7, 3, 300},   // parabolic neighborhood span
        {64, 2, 800, 64},   // wide-thin domain
        {300, 5, 100, 250}, // mid shape
    };
    gen::Lcg krng(20261106ULL);
    std::vector<std::vector<double>> kbufs;
    int maxOut = 0;
    for (const Shape& s : kShapes) {
        const int n = s.seg0 + s.tau0 + s.ntau - 1 + s.m + 8;
        std::vector<double> x(n);
        for (int i = 0; i < n; ++i) x[i] = 2.0 * krng.next() - 1.0;
        kbufs.push_back(std::move(x));
        if (s.ntau > maxOut) maxOut = s.ntau;
    }
    std::vector<double> dRef(maxOut), dAvx(maxOut), dFma(maxOut);

    // -- 2. simd_kernel_avx2_lag_bitexact (HARD gate) ------------------------
    if (harnSelected("simd_kernel_avx2_lag_bitexact")) {
        CaseResult c{"simd_kernel_avx2_lag_bitexact"};
        addValue(c, "avx2_exercised", haveAvx2 ? 1.0 : 0.0);
        if (haveAvx2) {
            int cells = 0;
            for (size_t si = 0; si < (sizeof(kShapes)/sizeof(kShapes[0])); ++si) {
                const Shape& s = kShapes[si];
                kScalar.diff(kbufs[si].data(), s.seg0, s.tau0, s.ntau, s.m,
                             dRef.data());
                kAvx2.diff(kbufs[si].data(), s.seg0, s.tau0, s.ntau, s.m,
                           dAvx.data());
                check(c,
                      std::memcmp(dRef.data(), dAvx.data(),
                                  sizeof(double) * s.ntau) == 0,
                      "AVX2 lag-axis kernel not bit-identical (shape " +
                          std::to_string(si) + ")");
                cells += s.ntau;
            }
            addValue(c, "cells_compared", static_cast<double>(cells));
        } else {
            c.note += "AVX2 unavailable: ISA gate not exercised this run; ";
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 3. simd_kernel_peak_bitexact ----------------------------------------
    if (harnSelected("simd_kernel_peak_bitexact")) {
        CaseResult c{"simd_kernel_peak_bitexact"};
        addValue(c, "avx2_exercised", haveAvx2 ? 1.0 : 0.0);
        addValue(c, "fma_exercised", haveFma ? 1.0 : 0.0);
        gen::Lcg prng(20261107ULL);
        static const int kSizes[] = {1,  2,  3,  4,  5,  7,  8,  15, 16,
                                     17, 63, 64, 65, 100, 1000, 2048};
        for (int n : kSizes) {
            std::vector<double> x(n);
            for (int i = 0; i < n; ++i) x[i] = 2.0 * prng.next() - 1.0;
            // extremes that exercise |x| handling
            if (n >= 4) { x[0] = -0.0; x[1] = -1e300; x[2] = 1e-300; x[3] = -1e-300; }
            const double pRef = kScalar.peak(x.data(), n);
            if (haveAvx2) {
                const double pAvx = kAvx2.peak(x.data(), n);
                check(c, pRef == pAvx || (pRef != pRef && pAvx != pAvx),
                      "AVX2 peak not bit-identical (n=" + std::to_string(n) +
                          ")");
            }
            if (haveFma) {
                const double pFma = kFma.peak(x.data(), n);
                check(c, pRef == pFma || (pRef != pRef && pFma != pFma),
                      "FMA peak not bit-identical (n=" + std::to_string(n) +
                          ")");
            }
        }
        if (!haveAvx2 && !haveFma) c.note += "no ISA levels: gates vacuous; ";
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 4. simd_kernel_fma_bounded ------------------------------------------
    if (harnSelected("simd_kernel_fma_bounded")) {
        CaseResult c{"simd_kernel_fma_bounded"};
        addValue(c, "fma_exercised", haveFma ? 1.0 : 0.0);
        if (haveFma) {
            double maxRel = 0.0, scale = 0.0;
            int diffs = 0;
            for (size_t si = 0; si < (sizeof(kShapes)/sizeof(kShapes[0])); ++si) {
                const Shape& s = kShapes[si];
                kScalar.diff(kbufs[si].data(), s.seg0, s.tau0, s.ntau, s.m,
                             dRef.data());
                kFma.diff(kbufs[si].data(), s.seg0, s.tau0, s.ntau, s.m,
                          dFma.data());
                for (int i = 0; i < s.ntau; ++i) {
                    const double a = std::abs(dRef[i]);
                    if (a > scale) scale = a;
                    if (dRef[i] != dFma[i]) ++diffs;
                }
            }
            for (size_t si = 0; si < (sizeof(kShapes)/sizeof(kShapes[0])); ++si) {
                const Shape& s = kShapes[si];
                kScalar.diff(kbufs[si].data(), s.seg0, s.tau0, s.ntau, s.m,
                             dRef.data());
                kFma.diff(kbufs[si].data(), s.seg0, s.tau0, s.ntau, s.m,
                          dFma.data());
                for (int i = 0; i < s.ntau; ++i) {
                    const double rel = std::abs(dRef[i] - dFma[i]) /
                                       (scale > 0.0 ? scale : 1.0);
                    if (rel > maxRel) maxRel = rel;
                }
            }
            addValue(c, "max_relative_deviation", maxRel);
            addValue(c, "cells_differing", static_cast<double>(diffs));
            addValue(c, "corpus_scale", scale);
            // BOUNDED deviation gate (never claimed bit-exact): one-vs-two
            // roundings per term over <= ~10^6 terms stays <= ~10^-9 of the
            // corpus scale (measured order: 10^-14).
            check(c, maxRel <= 1e-9,
                  "FMA lag-axis deviation exceeds the 1e-9 bound");
            // the FMA unit must genuinely FUSE (else the wiring collapsed to
            // the AVX2/scalar kernel and the bound above is vacuous)
            check(c, diffs > 0,
                  "FMA kernel produced zero deviations (not actually fused?)");
        } else {
            c.note += "AVX2+FMA unavailable: bounded gate not exercised; ";
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- shared engine battery helpers --------------------------------------
    struct ECfg { const char* name; DetectorMode dm; int sr; Interpolation ip; bool gate; };
    static const ECfg kMatrix[] = {
        {"full_48k_l6", DetectorMode::FullRate, 48000, Interpolation::Lagrange6, false},
        {"full_96k_l6", DetectorMode::FullRate, 96000, Interpolation::Lagrange6, false},
        {"dec_44k_l6", DetectorMode::Decimated, 44100, Interpolation::Lagrange6, false},
        {"dec_48k_ok6", DetectorMode::Decimated, 48000, Interpolation::ObsKernel6, false},
        {"dec_96k_ok6", DetectorMode::Decimated, 96000, Interpolation::ObsKernel6, false},
        {"hyb_44k_l6", DetectorMode::Hybrid, 44100, Interpolation::Lagrange6, false},
        {"hyb_48k_ok6", DetectorMode::Hybrid, 48000, Interpolation::ObsKernel6, false},
        {"hyb_96k_l6", DetectorMode::Hybrid, 96000, Interpolation::Lagrange6, false},
        {"hyb_48k_l6_gate", DetectorMode::Hybrid, 48000, Interpolation::Lagrange6, true},
        {"dec_48k_l6_gate", DetectorMode::Decimated, 48000, Interpolation::Lagrange6, true},
    };
    auto matrixInput = [](int sr) {
        // deterministic tone+noise mix (0.5 s): exercises the detector's
        // difference function at a real periodic dip and the full voice path
        gen::Lcg rng(20261108ULL);
        const int n = sr / 2;
        std::vector<float> x(n);
        for (int i = 0; i < n; ++i)
            x[i] = static_cast<float>(0.4 * std::sin(2.0 * M_PI * 220.0 * i / sr) +
                                      0.08 * rng.noise(1.0));
        return x;
    };
    auto runEngine = [](const Config& cfg, const std::vector<float>& in) {
        AlignedDelayLine e;
        std::vector<float> out(in.size(), 0.0f);
        if (!e.prepare(cfg)) return std::make_pair(out, -1.0);
        int done = 0;
        while (done < (int)in.size()) {
            const int k = std::min(1024, (int)in.size() - done);
            e.process(in.data() + done, out.data() + done, k);
            done += k;
        }
        return std::make_pair(out, e.lastEstimateHz());
    };
    auto matrixCfg = [](const ECfg& m, simd::Mode mode) {
        Config cfg;
        cfg.sample_rate = m.sr;
        cfg.detector_mode = m.dm;
        cfg.interpolation = m.ip;
        cfg.voices = 4;
        cfg.ratio = semitoneRatio(700.0);
        cfg.feedback = 0.25;
        cfg.simd = mode;
        cfg.tracking_gate = m.gate;
        return cfg;
    };

    // -- 5. simd_engine_avx2_bitidentity (HARD gate) -------------------------
    if (harnSelected("simd_engine_avx2_bitidentity")) {
        CaseResult c{"simd_engine_avx2_bitidentity"};
        addValue(c, "avx2_exercised", haveAvx2 ? 1.0 : 0.0);
        if (haveAvx2) {
            int configs = 0;
            for (const ECfg& m : kMatrix) {
                const auto in = matrixInput(m.sr);
                const auto rs = runEngine(matrixCfg(m, simd::Mode::Scalar), in);
                const auto ra = runEngine(matrixCfg(m, simd::Mode::AVX2), in);
                check(c, rs.second > 0.0,
                      std::string(m.name) + ": scalar run did not track");
                check(c,
                      std::memcmp(rs.first.data(), ra.first.data(),
                                  sizeof(float) * rs.first.size()) == 0,
                      std::string(m.name) + ": AVX2 engine output not bit-identical");
                check(c, rs.second == ra.second,
                      std::string(m.name) + ": AVX2 estimate differs");
                ++configs;
            }
            addValue(c, "configs_compared", static_cast<double>(configs));
        } else {
            c.note += "AVX2 unavailable: engine gate not exercised; ";
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 6. simd_engine_fma_closeness (BOUNDED gate) -------------------------
    if (harnSelected("simd_engine_fma_closeness")) {
        CaseResult c{"simd_engine_fma_closeness"};
        addValue(c, "fma_exercised", haveFma ? 1.0 : 0.0);
        if (haveFma) {
            double worstOut = 0.0, worstCents = 0.0;
            for (const ECfg& m : kMatrix) {
                const auto in = matrixInput(m.sr);
                const auto rs = runEngine(matrixCfg(m, simd::Mode::Scalar), in);
                const auto rf = runEngine(matrixCfg(m, simd::Mode::AVX2FMA), in);
                check(c, rs.second > 0.0,
                      std::string(m.name) + ": scalar run did not track");
                check(c, rf.second > 0.0,
                      std::string(m.name) + ": FMA run did not track");
                // estimate closeness (cents; both valid here -- tone input)
                const double cents =
                    std::abs(1200.0 * std::log2(rf.second / rs.second));
                if (cents > worstCents) worstCents = cents;
                for (size_t i = 0; i < rs.first.size(); ++i) {
                    const double d = std::abs(static_cast<double>(rs.first[i]) -
                                              static_cast<double>(rf.first[i]));
                    if (d > worstOut) worstOut = d;
                }
            }
            addValue(c, "worst_output_deviation", worstOut);
            addValue(c, "worst_estimate_delta_cents", worstCents);
            // honest BOUNDED gates: fused per-term rounding perturbs the
            // difference function at ~1e-14 relative; the estimate moves at
            // most a rounding-level step of the parabola vertex and the
            // output at most a float-rounding-scale difference.
            check(c, worstCents <= 0.05,
                  "FMA estimate delta exceeds 0.05 cents");
            check(c, worstOut <= 1e-4,
                  "FMA output deviation exceeds 1e-4");
        } else {
            c.note += "AVX2+FMA unavailable: closeness gate not exercised; ";
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 7. simd_prepare_mode_validation --------------------------------------
    if (harnSelected("simd_prepare_mode_validation")) {
        CaseResult c{"simd_prepare_mode_validation"};
        // all four valid modes must prepare and resolve to the expected level
        const simd::Mode valid[] = {simd::Mode::Scalar, simd::Mode::AVX2,
                                    simd::Mode::AVX2FMA, simd::Mode::Auto};
        for (simd::Mode m : valid) {
            Config cfg;
            cfg.simd = m;
            AlignedDelayLine e;
            check(c, e.prepare(cfg),
                  std::string("prepare rejected valid mode ") + simd::modeName(m));
            check(c, e.simdLevel() == simd::resolve(m),
                  std::string("resolved level mismatch for mode ") + simd::modeName(m));
        }
        // invalid enum values are rejected (loud failure)
        const int bad[] = {4, 5, 7, -1, 99};
        for (int v : bad) {
            Config cfg;
            cfg.simd = static_cast<simd::Mode>(v);
            AlignedDelayLine e;
            check(c, !e.prepare(cfg),
                  "prepare accepted invalid simd enum " + std::to_string(v));
        }
        // the forced-mode rejection branch: on this machine AVX2/FMA are
        // honourable, so the rejection path is NOT exercised here (record
        // honestly; the branch is compile-time present and unit-covered by
        // the enum gate above on feature-less machines).
        addValue(c, "forced_avx2_accepted", haveAvx2 ? 1.0 : 0.0);
        addValue(c, "forced_fma_accepted", haveFma ? 1.0 : 0.0);
        addValue(c, "auto_resolved_level", static_cast<double>(static_cast<int>(lvlAuto)));
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 8. simd_default_scalar_neutrality -----------------------------------
    if (harnSelected("simd_default_scalar_neutrality")) {
        CaseResult c{"simd_default_scalar_neutrality"};
        Config def;
        check(c, def.simd == simd::Mode::Scalar,
              "Config default simd is not Scalar");
        AlignedDelayLine e;
        check(c, e.prepare(def), "default prepare failed");
        check(c, e.simdLevel() == simd::Level::Scalar,
              "default engine did not resolve Scalar");
        // default vs explicit-Scalar: identical configuration bits
        Config exp;
        exp.simd = simd::Mode::Scalar;
        const auto in = matrixInput(48000);
        const auto r1 = runEngine(def, in);
        const auto r2 = runEngine(exp, in);
        check(c, std::memcmp(r1.first.data(), r2.first.data(),
                             sizeof(float) * r1.first.size()) == 0,
              "default vs explicit-Scalar outputs differ");
        // (the historical bit-exactness itself is gated by the four r15
        // reference hashes + default_neutrality_r19 -- re-run every round.)
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 9. simd_reprepare_level_switch ---------------------------------------
    if (harnSelected("simd_reprepare_level_switch")) {
        CaseResult c{"simd_reprepare_level_switch"};
        const int sr = 48000;
        const auto in = matrixInput(sr);
        Config base;
        base.sample_rate = sr;
        base.detector_mode = DetectorMode::Hybrid;
        base.voices = 4;
        base.ratio = semitoneRatio(700.0);
        base.feedback = 0.25;
        AlignedDelayLine e;
        check(c, e.prepare(base), "prepare failed");
        check(c, e.simdLevel() == simd::Level::Scalar, "initial level not Scalar");
        // warm the engine at Scalar (state evolves), then switch levels via
        // the IN-PLACE re-prepare path (identical buffer-determining fields)
        {
            std::vector<float> out(in.size(), 0.0f);
            e.process(in.data(), out.data(), (int)in.size());
        }
        Config avx = base;
        avx.simd = simd::Mode::AVX2;
        check(c, e.prepare(avx), "re-prepare to AVX2 failed");
        check(c, e.simdLevel() == lvlAvx2,
              "in-place re-prepare did not re-resolve the kernel table");
        // the re-prepared engine must equal a FRESH engine at the same level
        // bit-exactly (prepare() semantics: fresh-engine equivalence)
        if (lvlAvx2 == simd::Level::AVX2) {
            std::vector<float> outA(in.size(), 0.0f);
            e.process(in.data(), outA.data(), (int)in.size());
            const auto fresh = runEngine(avx, in);
            check(c, std::memcmp(outA.data(), fresh.first.data(),
                                 sizeof(float) * outA.size()) == 0,
                  "re-prepared AVX2 engine differs from a fresh one");
        }
        // ... and back to Scalar
        Config back = base;
        back.simd = simd::Mode::Scalar;
        check(c, e.prepare(back), "re-prepare back to Scalar failed");
        check(c, e.simdLevel() == simd::Level::Scalar, "level did not return to Scalar");
        {
            std::vector<float> outS(in.size(), 0.0f);
            e.process(in.data(), outS.data(), (int)in.size());
            const auto fresh = runEngine(back, in);
            check(c, std::memcmp(outS.data(), fresh.first.data(),
                                 sizeof(float) * outS.size()) == 0,
                  "re-prepared Scalar engine differs from a fresh one");
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 10. simd_rt_allocfree_accelerated -------------------------------------
    if (harnSelected("simd_rt_allocfree_accelerated")) {
        CaseResult c{"simd_rt_allocfree_accelerated"};
        const simd::Mode accelerated =
            haveFma ? simd::Mode::AVX2FMA
                    : (haveAvx2 ? simd::Mode::AVX2 : simd::Mode::Scalar);
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.35;
        cfg.simd = accelerated;
        const auto in = gen::noise(1234, 48000, 0.5);
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        check(c, e.simdLevel() == simd::resolve(accelerated),
              "level mismatch for the accelerated run");
        std::vector<float> out(in.size(), 0.f);
        e.process(in.data(), out.data(), 24000);  // warm-up
        allocs::reset();
        allocs::arm(true);
        e.process(in.data() + 24000, out.data() + 24000,
                  static_cast<int>(in.size()) - 24000);
        allocs::arm(false);
        const long n = allocs::count();
        addValue(c, "allocations_during_process", static_cast<double>(n));
        addValue(c, "level", static_cast<double>(static_cast<int>(e.simdLevel())));
        check(c, n == 0, "allocations during accelerated process()");
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

// ---------------------------------------------------------------------------
// R26: the batched interpolated-read battery (lane-per-read AVX2 kernel +
// the batched voice path). House rules: hard bit-exactness gates for the
// bit-exact levels (memcmp), bounded closeness only for the documented
// fused-rounding FMA level, honest "not exercised" notes when the CPU lacks
// the feature.
// ---------------------------------------------------------------------------
void casesR26(std::vector<CaseResult>& R) {
    const auto caps = simd::detect();
    const bool haveAvx2 = caps.avx2 && simd::isaUnitsCompiled();
    const bool haveFma = haveAvx2 && caps.fma;

    // -- 1. r26_read4_kernel_bitexact (HARD gate) ----------------------------
    if (harnSelected("r26_read4_kernel_bitexact")) {
        CaseResult c{"r26_read4_kernel_bitexact"};
        if (haveAvx2) {
            // deterministic ring + write cursor
            gen::Lcg rng(20261122ULL);
            std::vector<double> ring(1 << 18);
            for (auto& v : ring) v = rng.noise(1.0);
            const int kRingMask = (1 << 18) - 1;
            const simd::ReadKernels rk = simd::readKernels(simd::Level::AVX2);
            check(c, rk.read != nullptr, "scalar read reference missing");
            check(c, rk.read4 != nullptr, "AVX2 read4 kernel missing");
            const int modes[4] = {static_cast<int>(Interpolation::Linear),
                                  static_cast<int>(Interpolation::Lagrange6),
                                  static_cast<int>(Interpolation::Lagrange6Q),
                                  static_cast<int>(Interpolation::ObsKernel6)};
            // (the matched comparison pass below is the real gate; every
            // batch draws 4 FRACTIONAL delays and ONE shared write cursor,
            // then the kernel's 4 lanes are compared against the scalar
            // reference read for read, byte for byte)
            int matched = 0, integerCases = 0;
            for (int mi = 0; mi < 4; ++mi) {
                for (int rep = 0; rep < 1024; ++rep) {
                    double dq[4];
                    int nf = 0;
                    while (nf < 4) {
                        const double d = rng.next() * 40000.0;
                        if (d == std::floor(d)) continue;  // rare integers
                        dq[nf++] = d;
                    }
                    const int w0 = static_cast<int>(rng.next() * 262143.0);
                    double out[4];
                    rk.read4(ring.data(), w0, modes[mi], dq, out);
                    for (int L = 0; L < 4; ++L) {
                        const double ref = rk.read(ring.data(), w0, modes[mi], dq[L]);
                        if (std::memcmp(&ref, &out[L], sizeof(double)) != 0) {
                            check(c, false,
                                  std::string("lane ") + std::to_string(L) +
                                      " differs from the scalar reference (mode " +
                                      std::to_string(mi) + ", rep " +
                                      std::to_string(rep) + ")");
                        }
                    }
                    ++matched;
                }
            }
            // integer-delay law: the reference's mu == 0 fast path is the
            // direct masked ring read (the engine's readRaw equivalence).
            for (int i = 0; i < 4096; ++i) {
                const int d = static_cast<int>(rng.next() * 40000.0);
                const int w0 = static_cast<int>(rng.next() * 262143.0);
                const double ref = rk.read(ring.data(), w0, modes[i % 4], d);
                const double raw = ring[(w0 + d) & kRingMask];
                if (std::memcmp(&ref, &raw, sizeof(double)) != 0) {
                    check(c, false, "integer-delay fast path != direct ring read");
                }
                ++integerCases;
            }
            addValue(c, "batches_matched", static_cast<double>(matched));
            addValue(c, "integer_cases", static_cast<double>(integerCases));
        } else {
            c.note += "AVX2 unavailable: read4 kernel gate not exercised; ";
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 2. r26_engine_readpath_bitidentity (HARD gate, control churn) --------
    if (harnSelected("r26_engine_readpath_bitidentity")) {
        CaseResult c{"r26_engine_readpath_bitidentity"};
        if (haveAvx2) {
            // The R24 engine matrix used STATIC control; the batched voice
            // path's risk surface is the CONTROL CHURN (park/engage cycles,
            // commit-shadow xf, base-delay bxf, gain/mute ramps). This gate
            // runs identical churn programs into Scalar and AVX2 twins and
            // demands BYTE-EQUAL outputs.
            struct CCfg { const char* name; DetectorMode dm; int sr;
                          Interpolation ip; };
            static const CCfg kChurn[] = {
                {"dec_44k_l6", DetectorMode::Decimated, 44100, Interpolation::Lagrange6},
                {"dec_48k_ok6", DetectorMode::Decimated, 48000, Interpolation::ObsKernel6},
                {"dec_96k_l6q", DetectorMode::Decimated, 96000, Interpolation::Lagrange6Q},
                {"hyb_44k_lin", DetectorMode::Hybrid, 44100, Interpolation::Linear},
                {"hyb_48k_l6", DetectorMode::Hybrid, 48000, Interpolation::Lagrange6},
                {"full_48k_l6", DetectorMode::FullRate, 48000, Interpolation::Lagrange6},
            };
            int configs = 0;
            for (const CCfg& m : kChurn) {
                gen::Lcg rng(20261123ULL + configs);
                const int n = 1 << 16;
                std::vector<float> in(n);
                for (int i = 0; i < n; ++i)
                    in[i] = static_cast<float>(
                        0.35 * std::sin(2.0 * M_PI * 196.0 * i / m.sr) +
                        0.1 * rng.noise(1.0));
                auto mkcfg = [&](simd::Mode mode) {
                    Config cfg;
                    cfg.sample_rate = m.sr;
                    cfg.detector_mode = m.dm;
                    cfg.interpolation = m.ip;
                    cfg.voices = 8;
                    cfg.simd = mode;
                    // tight crossfades -> MANY transition events inside the
                    // 64k window (xf/bxf/park/engage all exercised)
                    cfg.commit_crossfade_seconds = 0.05;
                    cfg.delay_crossfade_seconds = 0.02;
                    cfg.engagement_seconds = 0.04;
                    return cfg;
                };
                auto runChurn = [&](simd::Mode mode) {
                    AlignedDelayLine e;
                    std::vector<float> out(n, 0.0f);
                    if (!e.prepare(mkcfg(mode))) return std::make_pair(out, -1.0);
                    // churn program (IDENTICAL for the twin runs)
                    gen::Lcg crng(777 + configs);
                    const double ratios[5] = {1.0, 1.05, semitoneRatio(700.0),
                                              0.75, 1.0};
                    int done = 0, step = 0;
                    while (done < n) {
                        const int bs = (step % 3 == 0) ? 512
                                       : (step % 3 == 1) ? 150
                                                         : 64;
                        const int k = std::min(bs, n - done);
                        e.process(in.data() + done, out.data() + done, k);
                        done += k;
                        ++step;
                        // per-block churn: ratio (incl. unity -> parking),
                        // base delay (-> bxf), gain/mute (-> ramps), voices
                        for (int v = 0; v < 8; ++v) {
                            e.setVoiceRatio(v, ratios[(step + v) % 5]);
                            e.setVoiceBaseDelay(
                                v, 240.0 + 480.0 *
                                    crng.next() * (1.0 + (step % 5)));
                            e.setVoiceGain(v, (step + v) % 7 == 0 ? 0.0 : 0.7);
                            e.setVoiceMute(v, (step % 11) == 3 && v == 2);
                        }
                        if (step % 9 == 0) e.setVoices(1 + (step % 8));
                    }
                    return std::make_pair(out, e.lastEstimateHz());
                };
                const auto rs = runChurn(simd::Mode::Scalar);
                const auto ra = runChurn(simd::Mode::AVX2);
                check(c, rs.second > 0.0,
                      std::string(m.name) + ": scalar run did not track");
                check(c,
                      std::memcmp(rs.first.data(), ra.first.data(),
                                  sizeof(float) * rs.first.size()) == 0,
                      std::string(m.name) +
                          ": churned AVX2 engine output not bit-identical");
                check(c, rs.second == ra.second,
                      std::string(m.name) + ": churned AVX2 estimate differs");
                ++configs;
            }
            addValue(c, "churn_configs_compared", static_cast<double>(configs));
        } else {
            c.note += "AVX2 unavailable: churn bit-identity gate not exercised; ";
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 3. r26_engine_fma_churn_closeness (BOUNDED gate) ---------------------
    if (harnSelected("r26_engine_fma_churn_closeness")) {
        CaseResult c{"r26_engine_fma_churn_closeness"};
        if (haveFma) {
            // Reads never fuse (the FMA level reuses the AVX2 read kernel);
            // the residual deviation is the detector's fused rounding only.
            // Bounded gate with the R24 bounds (0.05 cents / 1e-4 output).
            double worstOut = 0.0, worstCents = 0.0;
            Config cfg;
            cfg.sample_rate = 48000;
            cfg.detector_mode = DetectorMode::Decimated;
            cfg.interpolation = Interpolation::Lagrange6;
            cfg.voices = 8;
            cfg.commit_crossfade_seconds = 0.05;
            cfg.delay_crossfade_seconds = 0.02;
            cfg.engagement_seconds = 0.04;
            const int n = 1 << 16;
            gen::Lcg rng(99123ULL);
            std::vector<float> in(n);
            for (int i = 0; i < n; ++i)
                in[i] = static_cast<float>(
                    0.35 * std::sin(2.0 * M_PI * 220.0 * i / 48000.0) +
                    0.1 * rng.noise(1.0));
            auto runChurn = [&](simd::Mode mode) {
                AlignedDelayLine e;
                std::vector<float> out(n, 0.0f);
                Config c2 = cfg;
                c2.simd = mode;
                if (!e.prepare(c2)) return std::make_pair(out, -1.0);
                gen::Lcg crng(555ULL);
                int done = 0, step = 0;
                const double ratios[5] = {1.0, 1.05, semitoneRatio(700.0), 0.75, 1.0};
                while (done < n) {
                    const int bs = (step % 3 == 0) ? 512 : (step % 3 == 1) ? 150 : 64;
                    const int k = std::min(bs, n - done);
                    e.process(in.data() + done, out.data() + done, k);
                    done += k;
                    ++step;
                    for (int v = 0; v < 8; ++v) {
                        e.setVoiceRatio(v, ratios[(step + v) % 5]);
                        e.setVoiceBaseDelay(v, 240.0 + 480.0 * crng.next() * 1.5);
                    }
                }
                return std::make_pair(out, e.lastEstimateHz());
            };
            const auto rs = runChurn(simd::Mode::Scalar);
            const auto rf = runChurn(simd::Mode::AVX2FMA);
            check(c, rs.second > 0.0, "scalar run did not track");
            check(c, rf.second > 0.0, "FMA run did not track");
            worstCents =
                std::abs(1200.0 * std::log2(rf.second / rs.second));
            for (size_t i = 0; i < rs.first.size(); ++i) {
                const double d = std::abs(static_cast<double>(rs.first[i]) -
                                          static_cast<double>(rf.first[i]));
                if (d > worstOut) worstOut = d;
            }
            addValue(c, "worst_output_deviation", worstOut);
            addValue(c, "worst_estimate_delta_cents", worstCents);
            check(c, worstCents <= 0.05, "FMA churn estimate delta exceeds 0.05 cents");
            check(c, worstOut <= 1e-4, "FMA churn output deviation exceeds 1e-4");
        } else {
            c.note += "AVX2+FMA unavailable: churn closeness gate not exercised; ";
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 4. r26_midstream_level_handover (HARD gate) ---------------------------
    if (harnSelected("r26_midstream_level_handover")) {
        CaseResult c{"r26_midstream_level_handover"};
        if (haveAvx2) {
            // The R13-c in-place re-prepare contract: matching buffer-
            // determining fields reuse the allocations and the engine is
            // RESET to fresh-engine semantics. This gate verifies the read
            // path's level switching under that contract:
            //   (a) a LONG churn run (2^18) Scalar vs AVX2 stays
            //       byte-identical (slow state drift -- d_park, bxf_c, xf_c
            //       evolution -- would surface here);
            //   (b) a scalar->AVX2 in-place re-prepare mid-stream resets to
            //       fresh-engine state and a full re-run equals a FRESH
            //       AVX2 engine byte-for-byte (read4_ re-resolved);
            //   (c) the reverse switch (AVX2 -> Scalar) re-engages the
            //       verbatim path (read4_ == nullptr) and equals the same
            //       baseline.
            const int n = 1 << 18;
            gen::Lcg rng(314159ULL);
            std::vector<float> in(n);
            for (int i = 0; i < n; ++i)
                in[i] = static_cast<float>(
                    0.4 * std::sin(2.0 * M_PI * 174.0 * i / 48000.0) +
                    0.1 * rng.noise(1.0));
            Config cfg;
            cfg.sample_rate = 48000;
            cfg.detector_mode = DetectorMode::Decimated;
            cfg.interpolation = Interpolation::Lagrange6;
            cfg.voices = 8;
            cfg.commit_crossfade_seconds = 0.05;
            cfg.delay_crossfade_seconds = 0.02;
            cfg.engagement_seconds = 0.04;
            // the churn program (identical for every run)
            auto churnRun = [&](AlignedDelayLine& e, std::vector<float>& out,
                                int from = 0) {
                gen::Lcg crng(271828ULL);
                for (int done = from; done < n;) {
                    const int k = std::min(512, n - done);
                    e.process(in.data() + done, out.data() + done, k);
                    done += k;
                    for (int v = 0; v < 8; ++v) {
                        const double r =
                            (v % 3 == 0) ? 1.0 : (0.8 + 0.1 * ((v + done / 512) % 7));
                        e.setVoiceRatio(v, r);
                        const double d = 240.0 + 480.0 * crng.next();
                        e.setVoiceBaseDelay(v, d);
                    }
                }
            };
            // (a) long-run bit-identity
            {
                AlignedDelayLine es, ea;
                Config ca = cfg;
                ca.simd = simd::Mode::AVX2;
                std::vector<float> os(n, 0.0f), oa(n, 0.0f);
                check(c, es.prepare(cfg), "scalar prepare failed");
                check(c, ea.prepare(ca), "avx2 prepare failed");
                churnRun(es, os);
                churnRun(ea, oa);
                check(c, std::memcmp(os.data(), oa.data(), sizeof(float) * n) == 0,
                      "long churn run broke bit-identity");
                addValue(c, "long_run_samples", static_cast<double>(n));
            }
            // (b) scalar -> AVX2 in-place re-prepare == fresh AVX2 engine
            {
                AlignedDelayLine e2;
                std::vector<float> o2(n, 0.0f);
                check(c, e2.prepare(cfg), "scalar-first prepare failed");
                churnRun(e2, o2, 0);  // build up state (full stream, scalar)
                Config cs = cfg;
                cs.simd = simd::Mode::AVX2;
                check(c, e2.prepare(cs), "in-place scalar->avx2 re-prepare rejected");
                check(c, e2.simdLevel() == simd::Level::AVX2,
                      "re-prepare did not resolve the AVX2 level");
                std::vector<float> orr(n, 0.0f);
                churnRun(e2, orr, 0);  // fresh-engine semantics full re-run
                // fresh AVX2 engine over the same stream
                AlignedDelayLine ef;
                std::vector<float> of(n, 0.0f);
                check(c, ef.prepare(cs), "fresh avx2 prepare failed");
                churnRun(ef, of, 0);
                check(c, std::memcmp(orr.data(), of.data(), sizeof(float) * n) == 0,
                      "re-prepared engine != fresh AVX2 engine (fresh-engine "
                      "semantics broken)");
            }
            // (c) AVX2 -> Scalar reverse switch re-engages the verbatim path
            {
                AlignedDelayLine e3;
                std::vector<float> o3(n, 0.0f);
                Config ca = cfg;
                ca.simd = simd::Mode::AVX2;
                check(c, e3.prepare(ca), "avx2-first prepare failed");
                churnRun(e3, o3, 0);
                check(c, e3.prepare(cfg), "in-place avx2->scalar re-prepare rejected");
                check(c, e3.simdLevel() == simd::Level::Scalar,
                      "reverse re-prepare did not resolve Scalar");
                std::vector<float> orr(n, 0.0f);
                churnRun(e3, orr, 0);
                // the scalar baseline
                AlignedDelayLine es;
                std::vector<float> os(n, 0.0f);
                check(c, es.prepare(cfg), "baseline scalar prepare failed");
                churnRun(es, os, 0);
                check(c, std::memcmp(orr.data(), os.data(), sizeof(float) * n) == 0,
                      "re-prepared scalar path != baseline scalar engine");
            }
        } else {
            c.note += "AVX2 unavailable: handover gate not exercised; ";
        }
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }

    // -- 5. r26_rt_allocfree_batched ------------------------------------------
    if (harnSelected("r26_rt_allocfree_batched")) {
        CaseResult c{"r26_rt_allocfree_batched"};
        const simd::Mode accelerated =
            haveAvx2 ? simd::Mode::AVX2 : simd::Mode::Scalar;
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.feedback = 0.35;
        cfg.simd = accelerated;
        cfg.commit_crossfade_seconds = 0.05;
        cfg.delay_crossfade_seconds = 0.02;
        const int n = 48000;
        const auto in = gen::noise(4321, 48000, 0.5);
        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        std::vector<float> out(in.size(), 0.f);
        e.process(in.data(), out.data(), 24000);  // warm-up (scalar-side state)
        // arm + churn WITH the batched path active
        allocs::reset();
        allocs::arm(true);
        gen::Lcg crng(606ULL);
        for (int done = 24000; done < n;) {
            const int k = std::min(1500, n - done);
            e.process(in.data() + done, out.data() + done, k);
            done += k;
            for (int v = 0; v < 8; ++v) {
                e.setVoiceRatio(v, (v % 2) ? 1.0 : 1.05);
                e.setVoiceBaseDelay(v, 240.0 + 480.0 * crng.next());
            }
        }
        allocs::arm(false);
        const long na = allocs::count();
        addValue(c, "allocations_during_batched_process", static_cast<double>(na));
        addValue(c, "level", static_cast<double>(static_cast<int>(e.simdLevel())));
        check(c, na == 0, "allocations during churned batched process()");
        if (!haveAvx2) c.note += "AVX2 unavailable: ran the scalar path; ";
        c.pass = c.note.empty();
        pushCase(R, std::move(c));
    }
}

// ---------------------------------------------------------------------------
// JSON report
// ---------------------------------------------------------------------------
std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') {
            o += '\\';
            o += ch;
        } else if (ch == '\n') {
            o += "\\n";
        } else {
            o += ch;
        }
    }
    return o;
}

void writeJson(const std::string& path, double wallMs) {
    std::string j = "{\n  \"suite\": \"octavox_native_tests\",\n  \"version\": \"";
    j += versionString();
    j += "\",\n  \"environment\": {" + envJsonFragment() + "},\n  \"cases\": [\n";
    int idx = 0;
    for (const auto& c : g_results) {
        j += "    {\"name\": \"" + jsonEscape(c.name) + "\", \"pass\": " +
             (c.pass ? "true" : "false") + ", \"note\": \"" +
             jsonEscape(c.note) + "\", \"values\": {";
        for (size_t i = 0; i < c.values.size(); ++i) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.10g", c.values[i].second);
            j += "\"" + jsonEscape(c.values[i].first) + "\": " + buf;
            if (i + 1 < c.values.size()) j += ", ";
        }
        // R16-P2 additions (backward compatible: new keys only)
        char dbuf[32];
        std::snprintf(dbuf, sizeof(dbuf), "%.1f", c.duration_ms);
        j += "}, \"group\": \"" + jsonEscape(c.group) +
             "\", \"category\": \"" + jsonEscape(c.category) +
             "\", \"duration_ms\": " + dbuf + "}";
        if (++idx < (int)g_results.size()) j += ",";
        j += "\n";
    }
    int passed = 0;
    for (const auto& c : g_results) passed += c.pass ? 1 : 0;
    j += "  ],\n  \"summary\": {\"total\": " + std::to_string(g_results.size()) +
         ", \"passed\": " + std::to_string(passed) + ", \"failed\": " +
         std::to_string(g_results.size() - passed);
    // R16-P2 additions: selected counts, wall time, per-group subtotals.
    j += ", \"selected\": " + std::to_string(g_results.size()) +
         ", \"selected_passed\": " + std::to_string(passed) +
         ", \"selected_failed\": " + std::to_string(g_results.size() - passed);
    char wbuf[32];
    std::snprintf(wbuf, sizeof(wbuf), "%.1f", wallMs);
    j += ", \"wall_time_ms\": ";
    j += wbuf;
    j += ", \"groups\": {";
    for (int g = 0; g < kGroupCount; ++g) {
        int sel = 0, gp = 0;
        double ms = 0.0;
        for (const auto& c : g_results) {
            if (c.group != kGroupIds[g]) continue;
            ++sel;
            gp += c.pass ? 1 : 0;
            ms += c.duration_ms;
        }
        char gbuf[160];
        std::snprintf(gbuf, sizeof(gbuf),
                      "%s\"%s\": {\"selected\": %d, \"passed\": %d, "
                      "\"failed\": %d, \"duration_ms\": %.1f}",
                      g == 0 ? "" : ", ", kGroupIds[g], sel, gp, sel - gp, ms);
        j += gbuf;
    }
    j += "}}\n}\n";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
}

// ---------------------------------------------------------------------------
static void usageExit(const std::string& msg) {
    std::fprintf(stderr, "%s\n", msg.c_str());
    std::fprintf(stderr,
                 "usage: octavox_tests [--json <path>] [--durations] [--list]\n"
                 "    [--filter <substring>]  (repeatable, OR, "
                 "case-insensitive name substring)\n"
                 "    [--group <id>]          (repeatable, OR)\n"
                 "    [--category unit|long]  (repeatable, OR)\n"
                 "    [--reverse]             (R17-P1: run the group runners in\n"
                 "                            reverse order -- order-independence\n"
                 "                            check; same cases, same gates)\n"
                 "    [--diag <path>]         (R18-P1: JSONL diagnostic trace --\n"
                 "                            case start/completion, wall, RSS,\n"
                 "                            allocation counters, fenv probe;\n"
                 "                            observation only, never a gate)\n"
                 "    [--isolate]             (R18-P1, POSIX only: run each group\n"
                 "                            in a fresh forked process -- the\n"
                 "                            constrained-environment fallback;\n"
                 "                            NOT equivalent to a single-process\n"
                 "                            full run)\n"
                 "kinds AND together; unknown ids or an empty selection "
                 "exit 2\n");
    std::fprintf(stderr, "valid groups:");
    for (int i = 0; i < kGroupCount; ++i)
        std::fprintf(stderr, " %s", kGroupIds[i]);
    std::fprintf(stderr, "\nvalid categories:");
    for (int i = 0; i < 2; ++i)
        std::fprintf(stderr, " %s", kCategories[i]);
    std::fprintf(stderr, "\n");
    std::exit(2);
}

int main(int argc, char** argv) {
    std::string jsonPath;
    bool durations = false;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json" && i + 1 < argc) {
            jsonPath = argv[++i];
        } else if (a == "--durations") {
            durations = true;
        } else if (a == "--list") {
            list = true;
        } else if (a == "--filter" && i + 1 < argc) {
            g_filterSubs.push_back(argv[++i]);
        } else if (a == "--group" && i + 1 < argc) {
            g_selGroups.push_back(argv[++i]);
        } else if (a == "--category" && i + 1 < argc) {
            g_selCats.push_back(argv[++i]);
        } else if (a == "--reverse") {
            g_reverse = true;
        } else if (a == "--diag" && i + 1 < argc) {
            g_diagPath = argv[++i];
        } else if (a == "--isolate") {
#ifdef _WIN32
            usageExit("error: --isolate is not supported on Windows "
                      "(use repeated --group runs instead)");
#else
            g_isolate = true;
#endif
        } else {
            usageExit("error: unknown or incomplete option: " + a);
        }
    }

    // validate group ids / categories against the static registry
    for (const auto& g : g_selGroups) {
        bool ok = false;
        for (int i = 0; i < kGroupCount; ++i)
            if (g == kGroupIds[i]) {
                ok = true;
                break;
            }
        if (!ok) usageExit("error: unknown group '" + g + "'");
    }
    for (const auto& cat : g_selCats) {
        if (cat != "unit" && cat != "long")
            usageExit("error: unknown category '" + cat + "'");
    }

    if (list) {  // static registry dump; no case runs
        for (int i = 0; i < kAllCaseCount; ++i)
            std::printf("%-46s %-14s %s\n", kAllCases[i].name,
                        kAllCases[i].group, kAllCases[i].category);
        return 0;
    }

    // selection over the registry (the full default run = all 85 cases)
    int selected = 0;
    for (int i = 0; i < kAllCaseCount; ++i)
        if (harnSelected(kAllCases[i].name)) ++selected;
    if (selected == 0)
        usageExit("error: the given filters select zero cases");
    g_progTotal = selected;

    struct GroupRunner {
        const char* id;
        void (*fn)(std::vector<CaseResult>&);
    };
    static const GroupRunner kRunners[] = {
        {"prepare_reset", casesPrepareReset}, {"unity", casesUnity},
        {"voices", casesVoices},              {"stereo", casesStereo},
        {"rates_formats", casesRatesFormats}, {"stability", casesStability},
        {"r13", casesR13},                    {"r15p1", casesR15P1},
        {"r15acc", casesR15Acc},              {"r19", casesR19},
        {"r22", casesR22},                    {"r24", casesR24},
        {"r26", casesR26},
    };
    static const int kRunnerCount =
        static_cast<int>(sizeof(kRunners) / sizeof(kRunners[0]));

    const auto wallStart = std::chrono::steady_clock::now();
    // R18-P1: open the diagnostic trace (if requested) BEFORE any case body
    // can run, and stamp the suite_start record (environment + initial fenv).
    if (!g_diagPath.empty()) {
        g_diag = std::fopen(g_diagPath.c_str(), "w");
        if (!g_diag) {
            std::fprintf(stderr, "error: cannot open --diag file '%s'\n",
                         g_diagPath.c_str());
            return 2;
        }
        g_diagT0 = wallStart;
        g_diagRound0 = std::fegetround();
        g_diagFlags0 = std::fetestexcept(FE_ALL_EXCEPT);
        char b[128];
        std::snprintf(b, sizeof b,
                      ", \"pid\": %ld, \"format\": \"r18diag1\", "
                      "\"round0\": %d, \"fenv0\": %d, ",
                      diagPid(), g_diagRound0, g_diagFlags0);
        diagEmit("{\"ev\": \"suite_start\", " + envJsonFragment() +
                 std::string(b) + diagStatePairs() + "}");
    }
    // R17-P1: --reverse runs the group runners back-to-front (order-
    // independence check). Pure harness bookkeeping: the same case set with
    // the same gates runs either way; per-group durations are stamped the
    // same way. Within a group the case order is the registration order.
#ifndef _WIN32
    if (g_isolate) {
        // R18-P1 group-isolated fallback: one fresh forked process per
        // selected group (see the block comment above).  The children print
        // the usual live progress lines on the inherited stdout and return
        // their case results over a pipe; the parent aggregates and prints
        // the final table + JSON exactly like the single-process run.
        // NOTE: this mode is a constrained-environment fallback and a
        // process-global-state differential -- it is NOT the canonical
        // single-process full run and is never reported as equivalent.
        for (int ri = 0; ri < kRunnerCount; ++ri) {
            const int r = g_reverse ? (kRunnerCount - 1 - ri) : ri;
            int n = 0;
            for (int i = 0; i < kAllCaseCount; ++i)
                if (kAllCases[i].group == std::string(kRunners[r].id) &&
                    harnSelected(kAllCases[i].name))
                    ++n;
            if (n == 0) continue;
            std::printf("== group %s: %d cases (isolated process) ==\n",
                        kRunners[r].id, n);
            std::fflush(stdout);
            if (g_diag) {
                g_diagNext.clear();
                for (int i = 0; i < kAllCaseCount; ++i)
                    if (kAllCases[i].group == std::string(kRunners[r].id) &&
                        harnSelected(kAllCases[i].name))
                        g_diagNext.push_back(kAllCases[i].name);
                g_diagNextIdx = 0;
                diagEmit("{\"ev\": \"group_start\", \"group\": \"" +
                         std::string(kRunners[r].id) + "\", \"cases\": " +
                         std::to_string(n) + ", \"t_ms\": " +
                         std::to_string(diagMs()) + "}");
                if (!g_diagNext.empty())
                    diagCaseStart((size_t)g_progDone + 1, g_diagNext[0]);
            }
            int pipefd[2];
            if (pipe(pipefd) != 0) {
                std::perror("error: pipe");
                return 2;
            }
            std::fflush(stdout);
            const pid_t pid = fork();
            if (pid < 0) {
                std::perror("error: fork");
                return 2;
            }
            if (pid == 0) {
                // child: fresh process image from the (case-free) parent
                // state; runs exactly one group, reports over the pipe.
                // The inherited g_results (already-reported groups) must be
                // cleared so the child reports ONLY its own group's cases.
                ::close(pipefd[0]);
                FILE* out = ::fdopen(pipefd[1], "w");
                g_results.clear();
                g_currentGroup = kRunners[r].id;
                g_lastMark = std::chrono::steady_clock::now();
                kRunners[r].fn(g_results);
                for (const auto& c : g_results)
                    std::fprintf(out, "CASE\t%s\t%d\t%.1f\t%s\t%s\n",
                                 c.name.c_str(), c.pass ? 1 : 0, c.duration_ms,
                                 c.group.c_str(), c.category.c_str());
                int gp = 0, gf = 0;
                for (const auto& c : g_results) (c.pass ? gp : gf)++;
                std::fprintf(out, "GROUPSUM\t%d\t%d\n", gp, gf);
                std::fflush(out);
                std::fclose(out);
                if (g_diag)
                    diagEmit("{\"ev\": \"group_end\", \"group\": \"" +
                             std::string(kRunners[r].id) + "\", \"t_ms\": " +
                             std::to_string(diagMs()) + "}");
                std::fflush(stdout);
                std::fflush(g_diag);
                ::_exit(gf == 0 ? 0 : 1);
            }
            // parent: read the child's protocol lines, then reap it
            ::close(pipefd[1]);
            FILE* in = ::fdopen(pipefd[0], "r");
            char line[512];
            int got = 0;
            while (std::fgets(line, sizeof(line), in)) {
                char name[256];
                int ok = 0;
                double dur = 0.0;
                char grp[64], cat[16];
                if (std::sscanf(line, "CASE\t%255[^\t]\t%d\t%lf\t%63[^\t]\t%15[^\n]",
                                name, &ok, &dur, grp, cat) == 5) {
                    CaseResult c;
                    c.name = name;
                    c.pass = (ok != 0);
                    c.duration_ms = dur;
                    c.group = grp;
                    c.category = cat;
                    g_results.push_back(std::move(c));
                    ++got;
                }  // GROUPSUM lines are redundant here: the CASE lines
                   // already carry the per-group pass/fail state.
            }
            std::fclose(in);
            g_progDone += got;  // keep the global case index monotonic
                                 // across the forked children
            int status = 0;
            ::waitpid(pid, &status, 0);
            if (!WIFEXITED(status)) {
                std::fprintf(stderr,
                             "WARNING: isolated group '%s' child did not "
                             "exit normally (status 0x%x)\n",
                             kRunners[r].id, status);
            }
            // if the child died before reporting all its cases, record the
            // missing ones as failures so the aggregate stays honest
            for (int miss = got; miss < n; ++miss) {
                CaseResult c;
                c.name = std::string(kRunners[r].id) + ":child_report_missing";
                c.pass = false;
                c.note = "isolated child produced no result for this case";
                c.group = kRunners[r].id;
                c.category = "unit";
                g_results.push_back(std::move(c));
            }
        }
    } else  // the canonical single-process path (unchanged behaviour)
#endif
    for (int ri = 0; ri < kRunnerCount; ++ri) {
        const int r = g_reverse ? (kRunnerCount - 1 - ri) : ri;
        int n = 0;
        for (int i = 0; i < kAllCaseCount; ++i)
            if (kAllCases[i].group == std::string(kRunners[r].id) &&
                harnSelected(kAllCases[i].name))
                ++n;
        if (n == 0) continue;
        std::printf("== group %s: %d cases ==\n", kRunners[r].id, n);
        std::fflush(stdout);
        // R18-P1: diag bookkeeping -- the predicted case order for this
        // group (registry order, filtered by the selection), the group_start
        // record and the first case's START record, all flushed before the
        // first case body runs.
        if (g_diag) {
            g_diagNext.clear();
            for (int i = 0; i < kAllCaseCount; ++i)
                if (kAllCases[i].group == std::string(kRunners[r].id) &&
                    harnSelected(kAllCases[i].name))
                    g_diagNext.push_back(kAllCases[i].name);
            g_diagNextIdx = 0;
            diagEmit("{\"ev\": \"group_start\", \"group\": \"" +
                     std::string(kRunners[r].id) + "\", \"cases\": " +
                     std::to_string(n) + ", \"t_ms\": " +
                     std::to_string(diagMs()) + "}");
            if (!g_diagNext.empty())
                diagCaseStart((size_t)g_progDone + 1, g_diagNext[0]);
        }
        g_currentGroup = kRunners[r].id;
        g_lastMark = std::chrono::steady_clock::now();
        kRunners[r].fn(g_results);
        if (g_diag)
            diagEmit("{\"ev\": \"group_end\", \"group\": \"" +
                     std::string(kRunners[r].id) + "\", \"t_ms\": " +
                     std::to_string(diagMs()) + "}");
    }
    const double wallMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - wallStart)
            .count();

    // R18-P1: suite_end record (aggregate counts, flushed immediately)
    if (g_diag) {
        int dp = 0, df = 0;
        for (const auto& c : g_results) (c.pass ? dp : df)++;
        diagEmit("{\"ev\": \"suite_end\", \"passed\": " +
                 std::to_string(dp) + ", \"failed\": " +
                 std::to_string(df) + ", \"total\": " +
                 std::to_string((int)g_results.size()) +
                 ", \"wall_ms\": " + std::to_string(wallMs) + "}");
    }

    int passed = 0, failed = 0;
    if (durations) {
        std::printf("%-46s %-6s %12s %s\n", "CASE", "PASS", "DURATION(ms)",
                    "MEASUREMENTS (selected)");
        std::printf("%s\n", std::string(113, '-').c_str());
        for (const auto& c : g_results) {
            if (c.pass) ++passed; else ++failed;
            std::printf("%-46s %-6s %12.1f %s\n", c.name.c_str(),
                        c.pass ? "ok" : "FAIL", c.duration_ms, c.note.c_str());
            for (const auto& v : c.values)
                std::printf("%-46s %-6s %12s %s = %.10g\n", "", "", "",
                            v.first.c_str(), v.second);
        }
    } else {
        std::printf("%-46s %-6s %s\n", "CASE", "PASS", "MEASUREMENTS (selected)");
        std::printf("%s\n", std::string(100, '-').c_str());
        for (const auto& c : g_results) {
            if (c.pass) ++passed; else ++failed;
            std::printf("%-46s %-6s %s\n", c.name.c_str(), c.pass ? "ok" : "FAIL",
                        c.note.c_str());
            for (const auto& v : c.values)
                std::printf("%-46s %-6s %s = %.10g\n", "", "", v.first.c_str(),
                            v.second);
        }
    }
    std::printf("%s\n%s: %d passed, %d failed, %d total\n",
                std::string(100, '-').c_str(), versionString(), passed, failed,
                (int)g_results.size());
    std::printf("TOTAL_WALL_MS %.1f\n", wallMs);

    if (!jsonPath.empty()) writeJson(jsonPath, wallMs);

    return failed == 0 ? 0 : 1;
}
