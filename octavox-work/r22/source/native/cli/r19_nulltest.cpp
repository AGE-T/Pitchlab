// OCTAVOX round 19, task R19-P3 -- deterministic A/B null-test CLI:
// Interpolation::Lagrange6 (A)  vs  Interpolation::ObsKernel6 (B).
// ============================================================================
//
// PROVENANCE (independence statement)
// -----------------------------------
// This file is OUR OWN round-19 evidence tooling. It contains NO target
// code and performs no target-binary introspection at run time: it drives
// the independent OCTAVOX native engine (native/src/dsp.cpp, our own
// clean-room reimplementation) exclusively through the public API declared
// in native/include/octavox/dsp.hpp. The Interpolation::ObsKernel6 mode it
// exercises is the OBSERVED static 256x6 kernel table recovered from the
// target binary's .rdata section (native/src/obs_kernel_table.h) wired into
// OUR OWN independent engine as an OPT-IN comparison mode. Nothing in this
// tool links, loads, calls or decompiles target code.
//
// PURPOSE
// -------
// Render the SAME deterministic input through two engine instances
// configured IDENTICALLY except for the interpolation mode (A = Lagrange6,
// B = ObsKernel6), write both outputs plus the null residual as 32-bit
// IEEE-float WAVs (writeWavF32: exact float bits, no quantisation) and
// compute the full null-test metric set. A third reference instance
// C = Interpolation::Linear is rendered in the same lockstep purely as an
// event-verification control: max|A-C| > 0 and max|B-C| > 0 prove that both
// A and B took fractional-read interpolation paths rather than the parked
// integer-tap path.
//
// CANONICAL COMPARISON SETTINGS (task table; identical for A, B and C)
// --------------------------------------------------------------------
//   mono, 48 kHz, voices = 1, global pitch shift per case,
//   Voice 1 offset = 0 (spread = false and voices = 1: no spread offsets),
//   voice gain = 1, voice mute = off, feedback = 0, wet/dry = 100%,
//   bypass = off, identical block sizes, identical initial states (fresh
//   prepare()), identical input samples. Everything else = the engine's
//   16.0.0 defaults (grain 4096, base_delay 256, decimated detector,
//   click-free commits). No parameter setters are called after prepare():
//   the task semantics are all config-field defaults, and the global
//   cfg.ratio already sets voice 0's target (deliberately NOT double-set
//   through setVoiceRatio).
//
// CASES
// -----
//   unity_exact              ratio = 2^(0/1200)    = 1.0 (exactly).
//                            EXPECTED BIT-IDENTICAL: a unity voice parks on
//                            the integer-tap path and never touches the
//                            interpolation tables. The tool VERIFIES this
//                            and flags a defect if it fails.
//   moderate_shift_plus400c  ratio = 2^(+400/1200) = 2^(1/3).
//   shift_minus24st          ratio = 2^(-2400/1200) = 0.25 -- the task's
//                            headline case.
//
// INPUT (identical for every render)
// ----------------------------------
//   3.000 s = 144000 samples at 48000 Hz, mono:
//     x[k] = 0.35*sin(2*pi*220*k/48000)
//          + 0.20*sin(2*pi*330*k/48000)
//          + 0.05 * n[k]
//   where n[k] is deterministic LCG noise, seed 987654321:
//     s = 987654321;  per sample:  s = s*6364136223846793005
//                                     + 1442695040888963407;
//     d = ((s >> 11) & (2^53-1)) / 2^53;  n[k] = 2*d - 1.
//   (State initialised to the RAW seed; each draw advances the state first,
//   then extracts bits 11..63.) The composite is harmonically rich so both
//   6-tap kernels are exercised across many fractional phases.
//
// ANALYSIS REGION AND METRICS
// ---------------------------
//   All metrics are computed over k in [analysis_start, N) with
//   analysis_start = latencySamples() + 4096 (skip cold start + first
//   grain events; the canonical config gives 2304 + 4096 = 6400, so the
//   region is 137600 samples). analysis_start uses instance A's latency;
//   A and B must agree (latency_equal, folded into meets_expectation).
//
//   max_abs_diff            max |a[k] - b[k]|                     (double)
//   rms_diff                sqrt(mean((a-b)^2))
//   rms_a, rms_b            sqrt(mean(x^2)) of each output
//   diff_dbfs               20*log10(rms_diff)         (-inf -> JSON null)
//   rel_diff_db             20*log10(rms_diff / rms_a) (-inf -> JSON null)
//   correlation             Pearson r of a and b over the region (two-pass,
//                           clamped to [-1, 1])
//   bit_identical_fraction  fraction of samples whose FLOAT BITS are
//                           exactly equal (memcmp of the 4-byte floats)
//   latency_samples_a/b     engine latencySamples() (must be equal)
//   best_shift              alignment check: for s in {-8..+8},
//                           D(s) = max |a[k] - b[k+s]| over the COMMON
//                           subrange k in [start+8, N-9] (every shift
//                           compares the same sample set). Shifts are
//                           evaluated in the order 0, +1, -1, ..., +8, -8
//                           and only a STRICTLY smaller D replaces the
//                           incumbent, so ties resolve to the smallest |s|.
//                           Expected 0.
//   diff_spectrum_peak_hz / _rel_db
//                           own radix-2 FFT (double precision) of the
//                           difference signal, size 65536, PERIODIC Hann
//                           window, over the FIRST 65536 samples of the
//                           analysis region (fixed, documented choice; the
//                           canonical region is 137600 samples; a shorter
//                           region would be zero-padded). Peak bin searched
//                           in [20, 12000] Hz; the reported frequency is
//                           parabolically interpolated on the log
//                           magnitude (vertex clamped to +-0.5 bin).
//                           DEFINITION of diff_spectrum_peak_rel_db
//                           (stable, documented):
//                             20*log10( peak_mag / (rms_diff*sqrt(65536/2)) )
//                           with peak_mag the unnormalised windowed DFT bin
//                           magnitude. Interpretation: a difference signal
//                           that were a PURE sinusoid of RMS equal to
//                           rms_diff would score 20*log10(sqrt(N)/2) =
//                           +42.14 dB at N = 65536 (Hann coherent gain 1/2,
//                           two-sided split 1/2); broadband noise scores
//                           near 0 dB -- i.e. the metric is a
//                           tonality/peakiness measure of the null
//                           residual. Undefined (JSON null) when
//                           rms_diff = 0.
//   thdn                    deliberately NOT computed here: the input's
//                           intentional broadband LCG noise would dominate
//                           the figure; the separate P4 comparison tooling
//                           owns THD+N.
//   max_abs_diff_vs_linear_a/b
//                           max |A-C| / max |B-C| over the region (C = the
//                           Linear reference instance): the fractional-read
//                           event verification.
//
//   meets_expectation (the task's exact definition; latency equality is a
//   hard invariant of the A/B configuration and is folded in):
//     expected case (unity_exact): bit_identical_fraction == 1.0
//                                  AND latency_a == latency_b.
//     non-unity cases:             bit_identical_fraction < 1.0
//                                  AND max_abs_diff > 0
//                                  AND max_abs_diff_vs_linear_a > 0
//                                  AND max_abs_diff_vs_linear_b > 0
//                                  AND latency_a == latency_b.
//     A bit-identical non-unity case is the build-configuration-defect
//     flag the round-19 task demands.
//
// OUTPUT FILES
// ------------
//   <out>/<case>_lagrange6_b<block>.wav   A output (32-bit float, mono)
//   <out>/<case>_obskernel6_b<block>.wav  B output (32-bit float, mono)
//   <out>/<case>_diff_b<block>.wav        residual a[k]-b[k] over the FULL
//                                         render (may exceed +-1; the format
//                                         carries raw float bits)
//   <json path>                           the machine-readable report
//
// CLI
// ---
//   r19_nulltest [--out <dir>] [--json <path>] [--case <name>]
//                [--block 64|512] [--list]
//   defaults: out = nulltest_out, json = <out>/nulltest.json.
//   Exit code 0 iff every row meets its expectation (1 = a row failed,
//   2 = usage / IO / self-test error).
//
// DETERMINISM
// -----------
//   Pure formulas + the fixed LCG seed above; no clocks, no randomness, no
//   environment-dependent iteration order. The FFT is our own radix-2
//   implementation, guarded by an in-process self-test (impulse /
//   bin-centred Hann tone / Parseval) before any measurement runs. Case
//   ratios are computed in double as pow(2, cents/1200) (the -24 st case
//   is the exact power 2^-2 = 0.25); the libm result is used as computed.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "octavox/dsp.hpp"
#include "wav_io.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif
#if defined(_WIN32)
#include <direct.h>
#endif

using namespace octavox;

namespace {

// ---------------------------------------------------------------------------
// Fixed constants (the canonical R19-P3 comparison protocol)
// ---------------------------------------------------------------------------
constexpr double kSampleRate = 48000.0;
constexpr int kNumSamples = 144000;  // 3.000 s at 48 kHz, mono
constexpr int kAnalysisPad = 4096;   // cold-start + first-grain skip
constexpr int kFftSize = 65536;
constexpr int kShiftRange = 8;       // best_shift search range (see header)
constexpr double kPi = 3.14159265358979323846;
constexpr uint64_t kLcgMult = 6364136223846793005ULL;
constexpr uint64_t kLcgInc = 1442695040888963407ULL;
constexpr uint64_t kLcgSeed = 987654321ULL;
constexpr double kNoiseAmp = 0.05;
constexpr double kTone1Hz = 220.0, kTone1Amp = 0.35;
constexpr double kTone2Hz = 330.0, kTone2Amp = 0.20;

const char* const kToolName = "octavox_r19_nulltest";
const char* const kToolVersion = "1.0.0";

struct CaseDef {
    const char* name;
    int cents;              // pitch shift; ratio = 2^(cents/1200)
    bool expect_identical;  // true ONLY for unity_exact
    const char* note;
};

const CaseDef kCaseTable[] = {
    {"unity_exact", 0, true,
     "ratio exactly 1.0 -- the voice parks on the integer-tap path and never "
     "touches the interpolation tables; A and B must be BIT-IDENTICAL"},
    {"moderate_shift_plus400c", 400, false,
     "pitch shift +4.00 semitones (ratio 2^(400/1200) = 2^(1/3))"},
    {"shift_minus24st", -2400, false,
     "pitch shift -24 semitones (ratio 2^(-2400/1200) = 0.25) -- the R19 "
     "headline case"},
};

const int kBlockTable[] = {64, 512};

double caseRatio(const CaseDef& cs) {
    // Documented: computed in double as 2^(cents/1200); the -24 st case is
    // the exact power 2^-2 = 0.25.
    return std::pow(2.0, static_cast<double>(cs.cents) / 1200.0);
}

bool validCaseName(const std::string& s) {
    for (const CaseDef& c : kCaseTable)
        if (s == c.name) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Deterministic input signal (identical for every render)
// ---------------------------------------------------------------------------
std::vector<float> buildInput() {
    std::vector<float> x(static_cast<size_t>(kNumSamples));
    uint64_t s = kLcgSeed;  // raw seed; each draw advances the state first
    for (int k = 0; k < kNumSamples; ++k) {
        s = s * kLcgMult + kLcgInc;
        const uint64_t m = (s >> 11) & ((1ULL << 53) - 1);
        const double d = static_cast<double>(m) / 9007199254740992.0;  // 2^53
        const double noise = (2.0 * d - 1.0) * kNoiseAmp;
        const double t = static_cast<double>(k);
        const double v = kTone1Amp * std::sin(2.0 * kPi * kTone1Hz * t / kSampleRate)
                       + kTone2Amp * std::sin(2.0 * kPi * kTone2Hz * t / kSampleRate)
                       + noise;
        x[static_cast<size_t>(k)] = static_cast<float>(v);
    }
    return x;
}

// ---------------------------------------------------------------------------
// Own radix-2 FFT: iterative Cooley-Tukey, bit-reversal permutation,
// precomputed twiddle table, double precision. Forward, unnormalised:
//   X[p] = sum_k x[k] * exp(-2*pi*i*p*k/n);  n must be a power of two.
// ---------------------------------------------------------------------------
void fftRadix2(double* re, double* im, int n) {
    for (int i = 1, j = 0; i < n; ++i) {  // bit-reversal permutation
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    std::vector<double> wr(static_cast<size_t>(n) / 2),
        wi(static_cast<size_t>(n) / 2);  // w[t] = exp(-2*pi*i*t/n)
    for (int t = 0; t < n / 2; ++t) {
        const double a =
            -2.0 * kPi * static_cast<double>(t) / static_cast<double>(n);
        wr[static_cast<size_t>(t)] = std::cos(a);
        wi[static_cast<size_t>(t)] = std::sin(a);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len >> 1, stride = n / len;
        for (int base = 0; base < n; base += len) {
            for (int k = 0; k < half; ++k) {
                const int tw = k * stride;
                const double ar = re[base + k + half], ai = im[base + k + half];
                const double tr = ar * wr[tw] - ai * wi[tw];
                const double ti = ar * wi[tw] + ai * wr[tw];
                re[base + k + half] = re[base + k] - tr;
                im[base + k + half] = im[base + k] - ti;
                re[base + k] += tr;
                im[base + k] += ti;
            }
        }
    }
}

// In-process guard for the FFT above: (1) impulse -> flat unit magnitude,
// (2) bin-centred tone under the periodic Hann -> peak at that bin with
// magnitude N/4 (coherent gain 1/2 x two-sided split 1/2), (3) Parseval.
bool fftSelfTest() {
    {  // (1) impulse at k=1 -> |X[p]| = 1 for every p
        std::vector<double> re(static_cast<size_t>(kFftSize), 0.0),
            im(static_cast<size_t>(kFftSize), 0.0);
        re[1] = 1.0;
        fftRadix2(re.data(), im.data(), kFftSize);
        for (int p = 0; p < kFftSize; ++p) {
            const double m = std::sqrt(re[p] * re[p] + im[p] * im[p]);
            if (std::fabs(m - 1.0) > 1e-9) return false;
        }
    }
    {  // (2) bin-centred Hann-windowed tone
        const int bin = 1000;
        std::vector<double> re(static_cast<size_t>(kFftSize)),
            im(static_cast<size_t>(kFftSize), 0.0);
        for (int k = 0; k < kFftSize; ++k) {
            const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * k / kFftSize));
            re[k] = std::cos(2.0 * kPi * bin * k / kFftSize) * w;
        }
        fftRadix2(re.data(), im.data(), kFftSize);
        int arg = -1;
        double best = -1.0;
        for (int p = 1; p < kFftSize / 2; ++p) {
            const double m = std::sqrt(re[p] * re[p] + im[p] * im[p]);
            if (m > best) {
                best = m;
                arg = p;
            }
        }
        if (arg != bin) return false;
        if (std::fabs(best - kFftSize / 4.0) > 1e-6 * kFftSize) return false;
    }
    {  // (3) Parseval: sum|x|^2 == (1/N) sum|X|^2
        uint64_t s = 1;
        double sx = 0.0;
        std::vector<double> re(static_cast<size_t>(kFftSize)),
            im(static_cast<size_t>(kFftSize), 0.0);
        for (int k = 0; k < kFftSize; ++k) {
            s = s * kLcgMult + kLcgInc;
            const double v = static_cast<double>((s >> 11) & ((1ULL << 53) - 1))
                             / 9007199254740992.0 - 0.5;
            re[k] = v;
            sx += v * v;
        }
        fftRadix2(re.data(), im.data(), kFftSize);
        double sX = 0.0;
        for (int p = 0; p < kFftSize; ++p)
            sX += re[p] * re[p] + im[p] * im[p];
        return std::fabs(sX / kFftSize - sx) <= 1e-9 * sx;
    }
}

// ---------------------------------------------------------------------------
// Environment report (best effort; the JSON "environment" block)
// ---------------------------------------------------------------------------
struct EnvInfo {
    std::string os = "unknown";
    std::string cpu = "unknown";
    int cores = 0;
    std::string compiler = "unknown";
};

EnvInfo detectEnv() {
    EnvInfo e;
#if defined(__clang__)
    e.compiler = std::string("clang ") + __VERSION__;
#elif defined(__GNUC__) || defined(__GNUG__)
    e.compiler = std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
    e.compiler = "MSVC " + std::to_string(_MSC_VER);
#endif
#if defined(_WIN32)
    e.os = "Windows";
#endif
#if defined(__unix__) || defined(__APPLE__)
    struct utsname u;
    if (::uname(&u) == 0)
        e.os = std::string(u.sysname) + " " + u.release + " (" + u.machine + ")";
    const long sc = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (sc > 0) e.cores = static_cast<int>(sc);
#endif
#if defined(__linux__)
    if (std::FILE* f = std::fopen("/proc/cpuinfo", "r")) {
        char line[512];
        while (std::fgets(line, sizeof line, f)) {
            if (e.cpu == "unknown" && std::strncmp(line, "model name", 10) == 0) {
                const char* c = std::strchr(line, ':');
                if (c) {
                    const std::string t = c + 1;
                    const size_t b = t.find_first_not_of(" \t\r\n");
                    const size_t en = t.find_last_not_of(" \t\r\n");
                    if (b != std::string::npos) e.cpu = t.substr(b, en - b + 1);
                }
            }
        }
        std::fclose(f);
    }
#endif
    return e;
}

// ---------------------------------------------------------------------------
// Portable best-effort directory creation (avoids <filesystem> link
// dependencies on the dual-build targets)
// ---------------------------------------------------------------------------
void makeDirOne(const std::string& p) {
#if defined(_WIN32)
    (void)_mkdir(p.c_str());
#elif defined(__unix__) || defined(__APPLE__)
    (void)::mkdir(p.c_str(), 0777);
#else
    (void)p;
#endif
    // Errors (including EEXIST) are deliberately ignored here; any real
    // failure surfaces as an exception when the WAV/JSON write fails.
}

void ensureDir(const std::string& dir) {
    if (dir.empty()) return;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < dir.size()) {
        size_t j = dir.find_first_of("/\\", i);
        if (j == std::string::npos) j = dir.size();
        if (j > i) parts.push_back(dir.substr(i, j - i));
        i = j + 1;
    }
    if (parts.empty()) return;
    std::string cur = (dir[0] == '/' || dir[0] == '\\') ? "/" : "";
    for (const std::string& p : parts) {
        cur = cur.empty() ? p : (cur == "/" ? "/" + p : cur + "/" + p);
        makeDirOne(cur);
    }
}

void ensureDirParent(const std::string& file) {
    const size_t j = file.find_last_of("/\\");
    if (j == std::string::npos || j == 0) return;
    ensureDir(file.substr(0, j));
}

// ---------------------------------------------------------------------------
// Minimal JSON emission (stdlib-only; fixed report structure)
// ---------------------------------------------------------------------------
std::string jesc(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned>(c));
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    o += "\"";
    return o;
}

// Non-finite values (including the -inf of a bit-identical null) are
// emitted as JSON null -- the only faithful representation available.
std::string jnum(double v) {
    if (!std::isfinite(v)) return "null";
    char b[48];
    std::snprintf(b, sizeof b, "%.17g", v);  // lossless round-trip
    return b;
}

std::string jbool(bool v) { return v ? "true" : "false"; }

void writeFile(const std::string& path, const std::string& content) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    const bool ok = content.empty() ||
                    std::fwrite(content.data(), 1, content.size(), f) == content.size();
    std::fclose(f);
    if (!ok) throw std::runtime_error("short write to " + path);
}

// ---------------------------------------------------------------------------
// The A/B/C lockstep render
// ---------------------------------------------------------------------------
struct RenderOut {
    std::vector<float> a, b, c;  // Lagrange6 / ObsKernel6 / Linear outputs
    int latency_a = 0, latency_b = 0;
};

bool renderAbc(const CaseDef& cs, int block, const std::vector<float>& in,
               RenderOut& out) {
    Config cfg;  // all 16.0.0 defaults, then the task-table settings
    cfg.sample_rate = kSampleRate;
    cfg.ratio = caseRatio(cs);
    cfg.voices = 1;
    cfg.spread = false;   // voices == 1 -> no spread offsets (Voice 1 offset = 0)
    cfg.feedback = 0.0;
    // voice gain 1 / mute off / wet 100% / bypass off are all defaults.
    // NOTE: no setter calls after prepare(); the global cfg.ratio already
    // sets voice 0's target (it is deliberately NOT double-set through
    // setVoiceRatio).

    Config cfgA = cfg;
    cfgA.interpolation = Interpolation::Lagrange6;
    Config cfgB = cfg;
    cfgB.interpolation = Interpolation::ObsKernel6;
    Config cfgC = cfg;
    cfgC.interpolation = Interpolation::Linear;

    AlignedDelayLine engA, engB, engC;
    if (!engA.prepare(cfgA) || !engB.prepare(cfgB) || !engC.prepare(cfgC))
        return false;

    const int n = static_cast<int>(in.size());
    out.a.assign(static_cast<size_t>(n), 0.0f);
    out.b.assign(static_cast<size_t>(n), 0.0f);
    out.c.assign(static_cast<size_t>(n), 0.0f);

    // Lockstep: identical block size, A's block then B's block (then the C
    // control) per block index; identical input samples; identical initial
    // states (fresh prepare()). The final partial block is processed at its
    // natural size.
    for (int off = 0; off < n; off += block) {
        const int m = std::min(block, n - off);
        engA.process(in.data() + off, out.a.data() + off, m);
        engB.process(in.data() + off, out.b.data() + off, m);
        engC.process(in.data() + off, out.c.data() + off, m);
    }
    out.latency_a = engA.latencySamples();
    out.latency_b = engB.latencySamples();
    return true;
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
struct RowMetrics {
    std::string case_name;
    int block = 0;
    double ratio = 0.0;
    std::string note;
    long long analysis_start = 0;
    long long region_len = 0;
    double max_abs_diff = 0.0;
    double rms_diff = 0.0;
    double rms_a = 0.0, rms_b = 0.0;
    double diff_dbfs = 0.0;
    double rel_diff_db = 0.0;
    double correlation = 0.0;
    double bit_identical_fraction = 0.0;
    int latency_samples_a = 0, latency_samples_b = 0;
    bool latency_equal = true;
    int best_shift = 0;
    double diff_spectrum_peak_hz = std::numeric_limits<double>::quiet_NaN();
    double diff_spectrum_peak_rel_db = std::numeric_limits<double>::quiet_NaN();
    double max_abs_diff_vs_linear_a = 0.0;
    double max_abs_diff_vs_linear_b = 0.0;
    bool expected_bit_identical = false;
    bool meets_expectation = false;
    std::vector<std::string> wav_paths;
};

void computeMetrics(RowMetrics& m, const CaseDef& cs, const RenderOut& r) {
    const long n = static_cast<long>(r.a.size());
    const long start = static_cast<long>(r.latency_a) + kAnalysisPad;
    if (start >= n)
        throw std::runtime_error(
            "analysis region empty (latency + pad >= render length)");
    const long len = n - start;
    m.analysis_start = start;
    m.region_len = len;
    m.latency_samples_a = r.latency_a;
    m.latency_samples_b = r.latency_b;
    m.latency_equal = (r.latency_a == r.latency_b);

    // Pass 1: means / diff energy / extrema / bit equality.
    long bitEq = 0;
    double sumA = 0.0, sumB = 0.0, sumDD = 0.0;
    double maxD = 0.0, maxAC = 0.0, maxBC = 0.0;
    for (long k = start; k < n; ++k) {
        const double a = r.a[static_cast<size_t>(k)];
        const double b = r.b[static_cast<size_t>(k)];
        const double c = r.c[static_cast<size_t>(k)];
        const double d = a - b;
        sumA += a;
        sumB += b;
        sumDD += d * d;
        const double ad = std::fabs(d);
        if (ad > maxD) maxD = ad;
        const double eA = std::fabs(a - c);
        if (eA > maxAC) maxAC = eA;
        const double eB = std::fabs(b - c);
        if (eB > maxBC) maxBC = eB;
        if (std::memcmp(&r.a[static_cast<size_t>(k)], &r.b[static_cast<size_t>(k)],
                        sizeof(float)) == 0)
            ++bitEq;
    }
    // Pass 2 (centered): covariance / variances; raw second moments for RMS.
    const double dl = static_cast<double>(len);
    const double meanA = sumA / dl, meanB = sumB / dl;
    double cov = 0.0, varA = 0.0, varB = 0.0, sumAA = 0.0, sumBB = 0.0;
    for (long k = start; k < n; ++k) {
        const double a = r.a[static_cast<size_t>(k)];
        const double b = r.b[static_cast<size_t>(k)];
        const double da = a - meanA, db = b - meanB;
        cov += da * db;
        varA += da * da;
        varB += db * db;
        sumAA += a * a;
        sumBB += b * b;
    }
    m.max_abs_diff = maxD;
    m.rms_diff = std::sqrt(sumDD / dl);
    m.rms_a = std::sqrt(sumAA / dl);
    m.rms_b = std::sqrt(sumBB / dl);
    m.diff_dbfs = 20.0 * std::log10(m.rms_diff);  // -inf when bit-identical
    m.rel_diff_db = (m.rms_a > 0.0)
                        ? 20.0 * std::log10(m.rms_diff / m.rms_a)
                        : std::numeric_limits<double>::quiet_NaN();
    if (varA > 0.0 && varB > 0.0) {
        m.correlation = cov / std::sqrt(varA * varB);
        if (m.correlation > 1.0) m.correlation = 1.0;
        if (m.correlation < -1.0) m.correlation = -1.0;
    } else {
        m.correlation = (bitEq == len) ? 1.0 : 0.0;  // degenerate variance
    }
    m.bit_identical_fraction = static_cast<double>(bitEq) / dl;
    m.max_abs_diff_vs_linear_a = maxAC;
    m.max_abs_diff_vs_linear_b = maxBC;

    // best_shift: alignment check by max-abs-difference minimisation (see
    // the header comment for the exact, documented definition).
    {
        const long ks0 = start + kShiftRange;
        const long ks1 = n - 1 - kShiftRange;
        int bestShift = 0;
        double bestD = 0.0;
        bool have = false;
        if (ks0 <= ks1) {
            for (int s0 = 0; s0 <= kShiftRange; ++s0) {
                const int reps = (s0 == 0) ? 1 : 2;
                for (int si = 0; si < reps; ++si) {
                    const int s = (s0 == 0) ? 0 : (si == 0 ? s0 : -s0);
                    double dmax = 0.0;
                    for (long k = ks0; k <= ks1; ++k) {
                        const double d = std::fabs(
                            static_cast<double>(r.a[static_cast<size_t>(k)]) -
                            static_cast<double>(r.b[static_cast<size_t>(k + s)]));
                        if (d > dmax) dmax = d;
                    }
                    if (!have || dmax < bestD) {
                        bestD = dmax;
                        bestShift = s;
                        have = true;
                    }
                }
            }
        }
        m.best_shift = bestShift;
    }

    // Difference-spectrum peak (see the header comment for the documented
    // DEFINITION of diff_spectrum_peak_rel_db).
    if (m.rms_diff > 0.0) {
        std::vector<double> re(static_cast<size_t>(kFftSize), 0.0),
            im(static_cast<size_t>(kFftSize), 0.0);
        const long wlen = std::min(len, static_cast<long>(kFftSize));
        for (long i = 0; i < wlen; ++i) {
            const double d =
                static_cast<double>(r.a[static_cast<size_t>(start + i)]) -
                static_cast<double>(r.b[static_cast<size_t>(start + i)]);
            const double w = 0.5 *
                (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) /
                                static_cast<double>(kFftSize)));
            re[static_cast<size_t>(i)] = d * w;
        }
        fftRadix2(re.data(), im.data(), kFftSize);
        const auto mag = [&](int p) {
            return std::sqrt(re[static_cast<size_t>(p)] * re[static_cast<size_t>(p)] +
                             im[static_cast<size_t>(p)] * im[static_cast<size_t>(p)]);
        };
        const int pLo = std::max(
            1, static_cast<int>(std::ceil(20.0 * kFftSize / kSampleRate)));
        const int pHi = std::min(
            kFftSize / 2 - 1,
            static_cast<int>(std::floor(12000.0 * kFftSize / kSampleRate)));
        int p = -1;
        double best = -1.0;
        for (int q = pLo; q <= pHi; ++q) {
            const double mg = mag(q);
            if (mg > best) {
                best = mg;
                p = q;
            }
        }
        if (p > 0 && best > 0.0) {
            double delta = 0.0;  // parabolic interpolation on log magnitude
            const double mm = mag(p - 1), mp = mag(p + 1);
            if (mm > 0.0 && mp > 0.0) {
                const double ym = std::log(mm), y0 = std::log(best),
                             yp = std::log(mp);
                const double den = ym - 2.0 * y0 + yp;
                if (den != 0.0) delta = 0.5 * (ym - yp) / den;
                if (delta > 0.5) delta = 0.5;
                if (delta < -0.5) delta = -0.5;
            }
            m.diff_spectrum_peak_hz = (static_cast<double>(p) + delta) *
                                      kSampleRate / static_cast<double>(kFftSize);
            m.diff_spectrum_peak_rel_db = 20.0 * std::log10(
                best / (m.rms_diff *
                        std::sqrt(static_cast<double>(kFftSize) / 2.0)));
        }
    }

    // Expectation gate (the task's exact definition; latency equality is a
    // hard invariant of the A/B configuration and is folded in).
    if (cs.expect_identical) {
        m.meets_expectation = (m.bit_identical_fraction == 1.0) && m.latency_equal;
    } else {
        m.meets_expectation = (m.bit_identical_fraction < 1.0) &&
                              (m.max_abs_diff > 0.0) &&
                              (m.max_abs_diff_vs_linear_a > 0.0) &&
                              (m.max_abs_diff_vs_linear_b > 0.0) &&
                              m.latency_equal;
    }
}

// ---------------------------------------------------------------------------
// One (case, block) run: render A/B/C, write the three WAVs, compute metrics
// ---------------------------------------------------------------------------
RowMetrics runOne(const CaseDef& cs, int block, const std::vector<float>& in,
                  const std::string& outDir) {
    RowMetrics m;
    m.case_name = cs.name;
    m.block = block;
    m.ratio = caseRatio(cs);
    m.note = cs.note;
    m.expected_bit_identical = cs.expect_identical;

    RenderOut r;
    if (!renderAbc(cs, block, in, r))
        throw std::runtime_error(
            std::string("engine prepare() rejected the config for case ") + cs.name);

    const int n = static_cast<int>(in.size());
    const std::string btag = "_b" + std::to_string(block);
    const std::string wavA = outDir + "/" + cs.name + "_lagrange6" + btag + ".wav";
    const std::string wavB = outDir + "/" + cs.name + "_obskernel6" + btag + ".wav";
    const std::string wavD = outDir + "/" + cs.name + "_diff" + btag + ".wav";
    writeWavF32(wavA, r.a.data(), n, 1, static_cast<int>(kSampleRate));
    writeWavF32(wavB, r.b.data(), n, 1, static_cast<int>(kSampleRate));
    {  // full-render residual a[k]-b[k] (may exceed +-1; raw float bits)
        std::vector<float> diff(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k)
            diff[static_cast<size_t>(k)] = static_cast<float>(
                static_cast<double>(r.a[static_cast<size_t>(k)]) -
                static_cast<double>(r.b[static_cast<size_t>(k)]));
        writeWavF32(wavD, diff.data(), n, 1, static_cast<int>(kSampleRate));
    }
    m.wav_paths = {wavA, wavB, wavD};

    computeMetrics(m, cs, r);

    std::fprintf(stderr,
                 "[r19_nulltest] %s b%d: max|A-B|=%.6e bitident=%.6f "
                 "best_shift=%d meets=%s\n",
                 cs.name, block, m.max_abs_diff, m.bit_identical_fraction,
                 m.best_shift, m.meets_expectation ? "yes" : "NO");
    return m;
}

// ---------------------------------------------------------------------------
// JSON report
// ---------------------------------------------------------------------------
std::string buildJson(const EnvInfo& env, const std::vector<RowMetrics>& rows) {
    std::string j;
    j += "{\n";
    j += "  \"tool\": " + jesc(kToolName) + ",\n";
    j += "  \"tool_version\": " + jesc(kToolVersion) + ",\n";
    j += "  \"version\": " + jesc(versionString()) + ",\n";
    j += "  \"environment\": {\n";
    j += "    \"os\": " + jesc(env.os) + ",\n";
    j += "    \"cpu\": " + jesc(env.cpu) + ",\n";
    j += "    \"cores\": " + std::to_string(env.cores) + ",\n";
    j += "    \"compiler\": " + jesc(env.compiler) + "\n";
    j += "  },\n";
    j += "  \"sample_rate\": " + std::to_string(static_cast<int>(kSampleRate)) + ",\n";
    j += "  \"num_samples\": " + std::to_string(kNumSamples) + ",\n";
    j += "  \"duration_seconds\": 3.0,\n";
    j += "  \"signal\": " +
         jesc("0.35*sin(2*pi*220*k/48000) + 0.20*sin(2*pi*330*k/48000) + "
              "0.05*lcg(seed=987654321)") + ",\n";
    j += "  \"analysis\": {\n";
    j += "    \"pad_samples\": " + std::to_string(kAnalysisPad) + ",\n";
    j += "    \"fft_size\": " + std::to_string(kFftSize) + ",\n";
    j += "    \"fft_window\": \"periodic Hann\",\n";
    j += "    \"fft_region\": \"first 65536 samples of the analysis region\",\n";
    j += "    \"peak_band_hz\": [20, 12000]\n";
    j += "  },\n";
    j += "  \"cases\": [\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        const RowMetrics& m = rows[i];
        j += "    {\n";
        j += "      \"case\": " + jesc(m.case_name) + ",\n";
        j += "      \"block\": " + std::to_string(m.block) + ",\n";
        j += "      \"ratio\": " + jnum(m.ratio) + ",\n";
        j += "      \"note\": " + jesc(m.note) + ",\n";
        j += "      \"analysis_start\": " + std::to_string(m.analysis_start) + ",\n";
        j += "      \"analysis_region_samples\": " + std::to_string(m.region_len) + ",\n";
        j += "      \"max_abs_diff\": " + jnum(m.max_abs_diff) + ",\n";
        j += "      \"rms_diff\": " + jnum(m.rms_diff) + ",\n";
        j += "      \"rms_a\": " + jnum(m.rms_a) + ",\n";
        j += "      \"rms_b\": " + jnum(m.rms_b) + ",\n";
        j += "      \"diff_dbfs\": " + jnum(m.diff_dbfs) + ",\n";
        j += "      \"rel_diff_db\": " + jnum(m.rel_diff_db) + ",\n";
        j += "      \"correlation\": " + jnum(m.correlation) + ",\n";
        j += "      \"bit_identical_fraction\": " + jnum(m.bit_identical_fraction) + ",\n";
        j += "      \"latency_samples_a\": " + std::to_string(m.latency_samples_a) + ",\n";
        j += "      \"latency_samples_b\": " + std::to_string(m.latency_samples_b) + ",\n";
        j += "      \"latency_equal\": " + jbool(m.latency_equal) + ",\n";
        j += "      \"best_shift\": " + std::to_string(m.best_shift) + ",\n";
        j += "      \"diff_spectrum_peak_hz\": " + jnum(m.diff_spectrum_peak_hz) + ",\n";
        j += "      \"diff_spectrum_peak_rel_db\": " + jnum(m.diff_spectrum_peak_rel_db) + ",\n";
        j += "      \"max_abs_diff_vs_linear_a\": " + jnum(m.max_abs_diff_vs_linear_a) + ",\n";
        j += "      \"max_abs_diff_vs_linear_b\": " + jnum(m.max_abs_diff_vs_linear_b) + ",\n";
        j += "      \"expected_bit_identical\": " + jbool(m.expected_bit_identical) + ",\n";
        j += "      \"meets_expectation\": " + jbool(m.meets_expectation) + ",\n";
        j += "      \"wavs\": [\n";
        for (size_t w = 0; w < m.wav_paths.size(); ++w)
            j += "        " + jesc(m.wav_paths[w]) +
                 (w + 1 == m.wav_paths.size() ? "\n" : ",\n");
        j += "      ]\n";
        j += std::string("    }") + ((i + 1 == rows.size()) ? "\n" : ",\n");
    }
    j += "  ]\n";
    j += "}\n";
    return j;
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------
std::string fmtDb(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0.0 ? "-inf" : "+inf";
    char b[64];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}

struct Args {
    std::string out = "nulltest_out";
    std::string json;         // empty -> <out>/nulltest.json
    std::string case_filter;  // empty -> all cases
    int block_filter = 0;     // 0 -> all blocks
    bool list = false;
    bool help = false;
};

void printUsage(std::FILE* f) {
    std::fprintf(f,
        "usage: r19_nulltest [--out <dir>] [--json <path>] [--case <name>] "
        "[--block 64|512] [--list]\n"
        "\n"
        "  --out <dir>    output directory for the WAV files (default: nulltest_out)\n"
        "  --json <path>  JSON report path (default: <out>/nulltest.json)\n"
        "  --case <name>  single case: unity_exact | moderate_shift_plus400c | "
        "shift_minus24st\n"
        "  --block <n>    single block size: 64 | 512\n"
        "  --list         print the case names and exit\n"
        "  --help         this text\n"
        "\n"
        "Exit code 0 iff every (case, block) row meets its expectation.\n");
}

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto needValue = [&](const char* flag, std::string& dst) -> bool {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "r19_nulltest: %s requires a value\n", flag);
                return false;
            }
            dst = argv[++i];
            return true;
        };
        if (arg == "--out") {
            if (!needValue("--out", a.out)) return false;
        } else if (arg == "--json") {
            if (!needValue("--json", a.json)) return false;
        } else if (arg == "--case") {
            std::string v;
            if (!needValue("--case", v)) return false;
            if (!validCaseName(v)) {
                std::fprintf(stderr, "r19_nulltest: unknown case '%s' (valid:",
                             v.c_str());
                for (const CaseDef& c : kCaseTable)
                    std::fprintf(stderr, " %s", c.name);
                std::fprintf(stderr, ")\n");
                return false;
            }
            a.case_filter = v;
        } else if (arg == "--block") {
            std::string v;
            if (!needValue("--block", v)) return false;
            if (v != "64" && v != "512") {
                std::fprintf(stderr,
                             "r19_nulltest: --block must be 64 or 512 (got '%s')\n",
                             v.c_str());
                return false;
            }
            a.block_filter = std::atoi(v.c_str());
        } else if (arg == "--list") {
            a.list = true;
        } else if (arg == "--help" || arg == "-h") {
            a.help = true;
        } else {
            std::fprintf(stderr, "r19_nulltest: unknown argument '%s'\n", arg.c_str());
            return false;
        }
    }
    return true;
}

int run(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        printUsage(stderr);
        return 2;
    }
    if (args.help) {
        printUsage(stdout);
        return 0;
    }
    if (args.list) {
        for (const CaseDef& c : kCaseTable) std::printf("%s\n", c.name);
        return 0;
    }
    if (!fftSelfTest()) {
        std::fprintf(stderr, "r19_nulltest: FATAL: FFT self-test failed\n");
        return 2;
    }

    const EnvInfo env = detectEnv();
    const std::vector<float> input = buildInput();

    ensureDir(args.out);
    const std::string jsonPath =
        args.json.empty() ? (args.out + "/nulltest.json") : args.json;
    ensureDirParent(jsonPath);

    std::printf("%s %s (engine: %s)\n", kToolName, kToolVersion, versionString());
    std::printf("mode    : A = Interpolation::Lagrange6, B = Interpolation::ObsKernel6,"
                " C (control) = Interpolation::Linear\n");
    std::printf("signal  : %d samples (3.000 s) @ %.0f Hz mono -- "
                "%.0f Hz x%.2f + %.0f Hz x%.2f + LCG noise x%.2f (seed %llu)\n",
                kNumSamples, kSampleRate, kTone1Hz, kTone1Amp, kTone2Hz, kTone2Amp,
                kNoiseAmp, static_cast<unsigned long long>(kLcgSeed));
    std::printf("env     : %s | CPU: %s | cores: %d\n", env.os.c_str(),
                env.cpu.c_str(), env.cores);
    std::printf("output  : %s (WAVs) | %s (JSON)\n\n", args.out.c_str(),
                jsonPath.c_str());

    std::vector<RowMetrics> rows;
    for (const CaseDef& cs : kCaseTable) {
        if (!args.case_filter.empty() && args.case_filter != cs.name) continue;
        for (int b : kBlockTable) {
            if (args.block_filter != 0 && args.block_filter != b) continue;
            rows.push_back(runOne(cs, b, input, args.out));
        }
    }

    writeFile(jsonPath, buildJson(env, rows));

    std::printf("%-24s %5s %14s %14s %9s %10s %9s %7s %5s\n", "case", "block",
                "maxdiff", "rmsdiff", "reldB", "corr", "bitident", "expect", "ok");
    std::printf("%s\n", std::string(105, '-').c_str());
    for (const RowMetrics& m : rows) {
        std::printf("%-24s %5d %14.6e %14.6e %9s %10.6f %9.6f %7s %5s\n",
                    m.case_name.c_str(), m.block, m.max_abs_diff, m.rms_diff,
                    fmtDb(m.rel_diff_db).c_str(), m.correlation,
                    m.bit_identical_fraction,
                    m.expected_bit_identical ? "yes" : "no",
                    m.meets_expectation ? "OK" : "FAIL");
    }
    const auto okCount = static_cast<unsigned long>(std::count_if(
        rows.begin(), rows.end(), [](const RowMetrics& m) { return m.meets_expectation; }));
    const auto total = static_cast<unsigned long>(rows.size());
    if (okCount == total) {
        std::printf("VERDICT: %lu/%lu rows meet expectations\n", okCount, total);
        return 0;
    }
    std::printf("VERDICT: %lu/%lu rows meet expectations -- DEFECT FLAGGED\n",
                okCount, total);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "r19_nulltest: FATAL: %s\n", e.what());
        return 2;
    } catch (...) {
        std::fprintf(stderr, "r19_nulltest: FATAL: unknown exception\n");
        return 2;
    }
}
