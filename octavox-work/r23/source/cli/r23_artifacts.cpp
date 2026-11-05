// PITCHLAB R23 -- artifact reproduction & root-cause battery (Gate 7).
// ============================================================================
// PROVENANCE: our own tool (round 23). Reproduces and measures the four
// user-reported issues on the NATIVE engine (the VST3 module level is
// verified separately by octavox_r23_vst3_module_integration; the DAW level
// is outside this Linux sandbox -- recorded honestly):
//
//  A. High-frequency crackling during NEGATIVE pitch movement:
//     harmonic input, static -12/-24 st and a swept ratio, all four
//     interpolation modes, commit-crossfade on/off. Metrics: max inter-sample
//     step, click count (adaptive threshold), HF band energy ratio
//     (8-16 kHz vs total, 8192-point Hann FFT) out vs in.
//  B. Single-voice dual-mono stereo offset: MultichannelShifter bit-identity
//     re-verified on this baseline (the VST3 module test covers the wrapper).
//  C. Silent/stuck processing: NaN/Inf/denormal input, then clean signal --
//     finiteness + recovery latency; feedback tail boundedness; alternating
//     block sizes; zero-length calls.
//  D. R21 detector/interpolation build differences: engine-level proof that
//     the runtime-selectable variants execute different code paths (output
//     bit-differences + CPU differences + estimate differences on identical
//     input); the compile-time VST3 variants additionally differ in pinned
//     config and plugin identity (OBS from octavox_build_config.h).
//
// Usage: octavox_r23_artifacts [--json <path>]
// ============================================================================
#include "octavox/dsp.hpp"
#include "r23_yin_core.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

using octavox::AlignedDelayLine;
using octavox::Config;
using octavox::DetectorMode;
using octavox::Interpolation;
using octavox::MultichannelShifter;

double nowSec() { return static_cast<double>(std::clock()) / CLOCKS_PER_SEC; }

// Harmonic stack generator (jitter optional). R23-G7 fix: 40 harmonics
// so the signal carries genuine 8-16 kHz content for the HF metric (the
// R22-era 6-harmonic stack had NO energy in the band: hfOut/hfIn were 0/0).
std::vector<float> harmonicStack(double f0, int sr, double seconds, uint64_t seed,
                                 double jitterCents = 0.0, int nHarm = 40) {
    const int n = static_cast<int>(seconds * sr);
    std::vector<float> out(n);
    Lcg rng(seed);
    const double dt = 1.0 / sr;
    double phase = 0.0, jit = 0.0, jitT = 0.0;
    const double period = 1.0 / f0;
    for (int i = 0; i < n; ++i) {
        const double t = i * dt;
        if (std::fmod(t, period) < dt) jitT = (rng.next() * 2.0 - 1.0) * jitterCents;
        jit += (jitT - jit) * (dt / period) * 4.0;
        phase += 2.0 * M_PI * f0 * std::pow(2.0, jit / 1200.0) * dt;
        double a = 0.0;
        int used = 0;
        for (int k = 1; k <= nHarm; ++k) {
            if (k * f0 >= 0.45 * sr) break;
            a += std::sin(k * phase) / k;
            ++used;
        }
        out[i] = static_cast<float>(0.8 * a / used + 0.001 * rng.next());
    }
    return out;
}

double maxStep(const float* x, int n) {
    double m = 0.0;
    for (int i = 1; i < n; ++i) {
        const double d = std::abs(double(x[i]) - double(x[i - 1]));
        if (d > m) m = d;
    }
    return m;
}

double medianAbs(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Click count: steps exceeding max(0.02, 6x the median step)
long clickCount(const float* x, int n) {
    std::vector<double> steps;
    steps.reserve(static_cast<size_t>(n - 1));
    for (int i = 1; i < n; ++i)
        steps.push_back(std::abs(double(x[i]) - double(x[i - 1])));
    const double med = medianAbs(steps);
    const double thr = std::max(0.02, 6.0 * med);
    long c = 0;
    for (double s : steps)
        if (s > thr) ++c;
    return c;
}

// HF band energy ratio via Hann-windowed FFT (reuses the core's fft()).
double hfRatio(const float* x, int n, int sr, double loHz, double hiHz) {
    int L = 1;
    while (L < n) L <<= 1;
    std::vector<Cd> a(static_cast<size_t>(L), Cd(0.0, 0.0));
    for (int i = 0; i < n; ++i)
        a[static_cast<size_t>(i)] = Cd(x[i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1))), 0.0);
    fft(a, false);
    double tot = 0.0, band = 0.0;
    const double binHz = static_cast<double>(sr) / L;
    for (int k = 1; k < L / 2; ++k) {
        const double e = std::norm(a[static_cast<size_t>(k)]);
        tot += e;
        const double f = k * binHz;
        if (f >= loHz && f <= hiHz) band += e;
    }
    return tot > 0.0 ? band / tot : 0.0;
}

const char* interpName(Interpolation i) {
    switch (i) {
        case Interpolation::Linear: return "linear";
        case Interpolation::Lagrange6: return "lag6";
        case Interpolation::Lagrange6Q: return "lag6q";
        case Interpolation::ObsKernel6: return "obs6";
    }
    return "?";
}

std::string g_json = "[\n";
bool g_first = true;
void addJson(const std::string& s) {
    if (!g_first) g_json += ",\n";
    g_first = false;
    g_json += s;
}

}  // namespace

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else { std::fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    const int sr = 48000;
    const double seconds = 3.0;
    const int n = static_cast<int>(seconds * sr);
    const std::vector<float> in = harmonicStack(220.0, sr, seconds, 4242);
    const int steady0 = n - sr;  // last 1 s

    std::printf("== A. negative-pitch crackle battery ==\n");
    std::printf("%-8s %-7s %-8s %-6s | step    clicks  hfOut    hfIn     hfGain\n",
                "shift", "interp", "xfade", "jit");
    struct ShiftCase { const char* name; double ratio; bool sweep; double jitC; };
    const ShiftCase shifts[] = {
        {"-24st", 0.25, false, 0.0}, {"-12st", 0.5, false, 0.0},
        {"-12jit", 0.5, false, 30.0}, {"sweep", 0.5, true, 0.0},
        {"swpjit", 0.5, true, 30.0}, {"+12st", 2.0, false, 0.0}};
    const Interpolation interps[] = {Interpolation::Linear,
                                     Interpolation::Lagrange6,
                                     Interpolation::Lagrange6Q,
                                     Interpolation::ObsKernel6};
    for (const auto& sc : shifts) {
        std::vector<float> sig = (sc.jitC > 0.0)
                                     ? harmonicStack(220.0, sr, seconds, 4242, sc.jitC)
                                     : in;
        for (Interpolation ip : interps) {
            for (int xf = 0; xf < 2; ++xf) {
                Config cfg;
                cfg.sample_rate = sr;
                cfg.ratio = sc.sweep ? 1.0 : sc.ratio;
                cfg.interpolation = ip;
                cfg.commit_crossfade_seconds = xf ? 0.008 : 0.0;
                AlignedDelayLine eng;
                if (!eng.prepare(cfg)) return 1;
                std::vector<float> y(static_cast<size_t>(n), 0.0f);
                int done = 0;
                int blk = 0;
                while (done < n) {
                    const int b = std::min(64, n - done);
                    if (sc.sweep && done % 2048 == 0 && done > 0) {
                        // glide the target down 1.0 -> 0.5 over the run
                        const double frac = static_cast<double>(done) / n;
                        eng.setRatio(1.0 - 0.5 * frac);
                    }
                    eng.process(sig.data() + done, y.data() + done, b);
                    done += b;
                    ++blk;
                }
                const double step = maxStep(y.data() + steady0, sr);
                const long clicks = clickCount(y.data() + steady0, sr);
                const double hfO = hfRatio(y.data() + steady0, sr, sr, 8000.0, 16000.0);
                const double hfI = hfRatio(sig.data() + steady0, sr, sr, 8000.0, 16000.0);
                const double gainDb = 10.0 * std::log10(std::max(hfO, 1e-300) /
                                                        std::max(hfI, 1e-300));
                std::printf("%-8s %-7s %-8s %-6s | %.4f  %6ld  %.5f  %.5f  %+6.1f dB\n",
                            sc.name, interpName(ip), xf ? "8ms" : "off",
                            sc.jitC > 0 ? "30c" : "-", step, clicks, hfO, hfI, gainDb);
                char js[400];
                std::snprintf(js, sizeof(js),
                    "{\"case\": \"crackle\", \"shift\": \"%s\", \"interp\": \"%s\", "
                    "\"xfade\": %s, \"jitter\": %s, \"max_step\": %.6f, "
                    "\"clicks\": %ld, \"hf_out\": %.6f, \"hf_in\": %.6f, "
                    "\"hf_gain_db\": %.3f}",
                    sc.name, interpName(ip), xf ? "8ms" : "off",
                    sc.jitC > 0 ? "30c" : "none", step, clicks, hfO, hfI, gainDb);
                addJson(js);
            }
        }
    }

    // -- B. dual-mono bit identity (native multichannel) --------------------
    std::printf("\n== B. dual-mono stereo bit-identity (native) ==\n");
    {
        Config cfg;
        cfg.sample_rate = sr;
        cfg.ratio = 0.5;
        cfg.voices = 1;
        MultichannelShifter mc;
        if (!mc.prepare(cfg, 2)) return 1;
        const float* chs[2] = {in.data(), in.data()};
        std::vector<float> L(static_cast<size_t>(n)), R(static_cast<size_t>(n));
        float* outs[2] = {L.data(), R.data()};
        mc.process(chs, outs, 2, n);
        double maxd = 0.0;
        bool bitIdentical = true;
        for (int i = 0; i < n; ++i) {
            if (std::memcmp(&L[static_cast<size_t>(i)], &R[static_cast<size_t>(i)],
                            sizeof(float)) != 0) {
                bitIdentical = false;
                maxd = std::max(maxd, std::abs(double(L[static_cast<size_t>(i)]) -
                                               double(R[static_cast<size_t>(i)])));
            }
        }
        std::printf("dual-mono (1 voice, -12st): %s (maxdiff %.3g)\n",
                    bitIdentical ? "BIT-IDENTICAL" : "DIVERGED", maxd);
        addJson("{\"case\": \"dual_mono_native\", \"bit_identical\": " +
                std::string(bitIdentical ? "true" : "false") +
                ", \"maxdiff\": " + std::to_string(maxd) + "}");
    }

    // -- C. silent/stuck processing states ----------------------------------
    std::printf("\n== C. silent/stuck battery ==\n");
    {
        auto stuckCase = [&](const char* name, std::vector<float> sig,
                             double fb = 0.0) {
            Config cfg;
            cfg.sample_rate = sr;
            cfg.ratio = 0.5;
            cfg.feedback = fb;
            AlignedDelayLine eng;
            if (!eng.prepare(cfg)) return;
            std::vector<float> y(sig.size(), 0.0f);
            // alternating block sizes (32/1024) + zero-length calls
            int done = 0, k = 0;
            while (done < static_cast<int>(sig.size())) {
                const int b = (k % 2 == 0) ? 32 : 1024;
                const int use = std::min(b, static_cast<int>(sig.size()) - done);
                if (k % 7 == 0) eng.process(nullptr, nullptr, 0);  // zero-length
                eng.process(sig.data() + done, y.data() + done, use);
                done += use;
                ++k;
            }
            bool finite = true;
            for (float v : y)
                if (!std::isfinite(v)) { finite = false; break; }
            // recovery: RMS of the last 100 ms after the clean part resumes
            double rms = 0.0;
            const int m = std::min(4800, static_cast<int>(y.size()));
            for (int i = static_cast<int>(y.size()) - m;
                 i < static_cast<int>(y.size()); ++i)
                rms += double(y[static_cast<size_t>(i)]) * double(y[static_cast<size_t>(i)]);
            rms = std::sqrt(rms / m);
            std::printf("%-24s finite=%-5s tail-RMS=%.5f %s\n", name,
                        finite ? "yes" : "NO", rms,
                        rms > 0.01 ? "(recovered)" : "(SILENT?)");
            char js[256];
            std::snprintf(js, sizeof(js),
                          "{\"case\": \"stuck_%s\", \"finite\": %s, \"tail_rms\": %.6f}",
                          name, finite ? "true" : "false", rms);
            addJson(js);
        };
        // NaN 0.5 s then clean 2.5 s
        {
            std::vector<float> sig = in;
            for (int i = 0; i < sr / 2; ++i)
                sig[static_cast<size_t>(i)] =
                    (i % 3 == 0) ? std::nanf("") : 0.5f;
            stuckCase("nan_input_then_clean", sig);
        }
        // Inf 0.25 s then clean
        {
            std::vector<float> sig = in;
            for (int i = 0; i < sr / 4; ++i)
                sig[static_cast<size_t>(i)] =
                    (i % 5 == 0) ? HUGE_VALF : 1.0f;
            stuckCase("inf_input_then_clean", sig);
        }
        // denormal-only 1 s then clean
        {
            std::vector<float> sig = in;
            for (int i = 0; i < sr; ++i)
                sig[static_cast<size_t>(i)] = 1e-300f;
            stuckCase("denormal_then_clean", sig);
        }
        // silence + high feedback: bounded decay?
        {
            std::vector<float> sig(sr * 3);
            for (int i = 0; i < sr; ++i)
                sig[static_cast<size_t>(i)] = in[static_cast<size_t>(i)];
            // 1 s tone then 2 s silence, fb 0.95
            stuckCase("feedback_tail_bounded", sig, 0.95);
        }
        // all-silence input
        {
            std::vector<float> sig(static_cast<size_t>(sr * 2), 0.0f);
            stuckCase("all_silence", sig);
        }
    }

    // -- D. build/variant code-path differences -----------------------------
    std::printf("\n== D. detector/interpolation variant code-path differences ==\n");
    {
        const DetectorMode dets[3] = {DetectorMode::FullRate,
                                      DetectorMode::Decimated,
                                      DetectorMode::Hybrid};
        double cpuMode[3] = {0, 0, 0};
        double estMed[3] = {0, 0, 0};
        std::vector<float> y(static_cast<size_t>(n));
        for (int mi = 0; mi < 3; ++mi) {
            Config cfg;
            cfg.sample_rate = sr;
            cfg.ratio = 1.0;
            cfg.detector_mode = dets[mi];
            AlignedDelayLine eng;
            eng.prepare(cfg);
            std::vector<double> ests;
            const double tA = nowSec();
            int done = 0;
            while (done < n) {
                const int b = std::min(64, n - done);
                eng.process(in.data() + done, y.data() + done, b);
                done += b;
                if (done % 64 == 0) {
                    const double e = eng.lastEstimateHz();
                    if (e > 0) ests.push_back(e);
                }
            }
            cpuMode[mi] = (nowSec() - tA) * 1e9 / n;
            std::sort(ests.begin(), ests.end());
            estMed[mi] = ests.empty() ? 0 : ests[ests.size() / 2];
        }
        const char* dn[3] = {"fullrate", "decimated", "hybrid"};
        for (int mi = 0; mi < 3; ++mi)
            std::printf("detector %-10s cpu %7.1f ns/sample  est-median %.3f Hz\n",
                        dn[mi], cpuMode[mi], estMed[mi]);
        addJson("{\"case\": \"detector_paths\", \"fullrate_cpu\": " +
                std::to_string(cpuMode[0]) + ", \"decimated_cpu\": " +
                std::to_string(cpuMode[1]) + ", \"hybrid_cpu\": " +
                std::to_string(cpuMode[2]) + ", \"fullrate_med\": " +
                std::to_string(estMed[0]) + ", \"decimated_med\": " +
                std::to_string(estMed[1]) + ", \"hybrid_med\": " +
                std::to_string(estMed[2]) + "}");
        // interpolation: outputs differ between kernels on identical input
        double maxd[4] = {0, 0, 0, 0};
        const Interpolation ips[4] = {Interpolation::Linear,
                                      Interpolation::Lagrange6,
                                      Interpolation::Lagrange6Q,
                                      Interpolation::ObsKernel6};
        std::vector<std::vector<float>> outs(4, std::vector<float>(
                                                     static_cast<size_t>(n)));
        for (int k = 0; k < 4; ++k) {
            Config cfg;
            cfg.sample_rate = sr;
            cfg.ratio = 0.5;
            cfg.interpolation = ips[k];
            AlignedDelayLine eng;
            eng.prepare(cfg);
            eng.process(in.data(), outs[static_cast<size_t>(k)].data(), n);
        }
        for (int k = 1; k < 4; ++k)
            for (int i = 0; i < n; ++i)
                maxd[k] = std::max(
                    maxd[k], std::abs(double(outs[0][static_cast<size_t>(i)]) -
                                      double(outs[static_cast<size_t>(k)][static_cast<size_t>(i)])));
        std::printf("interp outputs vs linear: lag6 maxdiff %.6f, lag6q %.6f, "
                    "obs6 %.6f (nonzero = different code paths)\n",
                    maxd[1], maxd[2], maxd[3]);
        addJson("{\"case\": \"interp_paths\", \"lag6_vs_linear\": " +
                std::to_string(maxd[1]) + ", \"lag6q_vs_linear\": " +
                std::to_string(maxd[2]) + ", \"obs6_vs_linear\": " +
                std::to_string(maxd[3]) + "}");
    }

    g_json += "\n]\n";
    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (!f) { std::fprintf(stderr, "cannot write %s\n", jsonPath); return 1; }
        std::fwrite(g_json.data(), 1, g_json.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "json written: %s\n", jsonPath);
    }
    return 0;
}
