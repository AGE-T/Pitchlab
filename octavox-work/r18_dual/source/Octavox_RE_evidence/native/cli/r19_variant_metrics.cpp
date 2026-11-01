// OCTAVOX R19 -- dual-variant performance/quality comparison tool.
// ============================================================================
// PROVENANCE: our own tool (round 19). It measures OUR independent
// harmonizer engine under the two interpolation variants (Lagrange6 vs
// ObsKernel6 -- the latter uses the static 256x6 kernel OBSERVED in the
// original target's .rdata as an opt-in comparison mode) under IDENTICAL
// settings, and reports:
//   * interpolation-specific + total CPU cost (ns per output frame),
//   * output pitch accuracy (parabolic FFT peak vs the expected
//     input_pitch * ratio, in cents),
//   * THD+N of the output for a pure-sine input (fundamental-removed
//     residual; NOTE the engine's dual-tap grain overlap legitimately
//     contributes amplitude-modulation sidebands -- the metric is a
//     like-for-like VARIANT COMPARISON, not an absolute quality claim),
//   * max sample-to-sample step (click-scale context).
// The ONLY intended difference between the two columns is the interpolation
// kernel choice (see dsp.hpp); the detector, engine and parameter surface
// are identical. The 38-parameter host surface is untouched by this tool.
//
// Usage: octavox_r19_variant_metrics [--json <path>] [--seconds <n>]
// Exit 0 on success.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "octavox/dsp.hpp"

using namespace octavox;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

double nowSec() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

void fft(double* re, double* im, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / len;
        const double wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < len / 2; ++k) {
                const double ur = re[i + k], ui = im[i + k];
                const double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                const double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                const double nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}

struct Metrics {
    double ns_per_frame;
    double realtime_factor;
    double pitch_hz_est;
    double pitch_err_cents;
    double thdn_db;
    double max_step;
    double rms_out;
};

// Render + measure one variant. shiftSemis: the pitch shift; input 440 Hz
// pure sine. The expected output fundamental = 440 * 2^(shift/12).
Metrics measureVariant(Interpolation interp, double shiftSemis, int seconds,
                       double& warmupUsed) {
    const int sr = 48000;
    const int n = seconds * sr;
    std::vector<float> in(n);
    const double fin = 440.0;
    for (int i = 0; i < n; ++i)
        in[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * fin * i / sr));

    Config cfg;
    cfg.ratio = std::pow(2.0, shiftSemis / 12.0);
    cfg.voices = 1;
    cfg.spread = false;
    cfg.feedback = 0.0;
    cfg.interpolation = interp;
    AlignedDelayLine e;
    if (!e.prepare(cfg)) {
        std::fprintf(stderr, "prepare failed\n");
        std::exit(1);
    }
    std::vector<float> out(n, 0.0f);
    // warm-up (tables/caches; excluded from the timing)
    {
        int done = 0;
        while (done < sr) {
            const int k = std::min(128, sr - done);
            e.process(in.data() + done, out.data() + done, k);
            done += k;
        }
        e.reset();
    }
    // timed render (best of 3)
    double best = 1e300;
    for (int rep = 0; rep < 3; ++rep) {
        e.reset();
        const double t0 = nowSec();
        int done = 0;
        while (done < n) {
            const int k = std::min(128, n - done);
            e.process(in.data() + done, out.data() + done, k);
            done += k;
        }
        const double el = nowSec() - t0;
        if (el < best) best = el;
    }
    // final render kept for analysis (deterministic: identical to the timed
    // ones)
    e.reset();
    {
        int done = 0;
        while (done < n) {
            const int k = std::min(128, n - done);
            e.process(in.data() + done, out.data() + done, k);
            done += k;
        }
    }
    warmupUsed = 1.0;

    Metrics m{};
    m.ns_per_frame = best * 1e9 / n;
    m.realtime_factor = static_cast<double>(n) / sr / best;

    // analysis region: past latency + first grain
    const int start = e.latencySamples() + 4096;

    // pitch + THD+N via FFT of the analysis region (Hann window)
    int N = 1;
    while (N * 2 <= (n - start)) N *= 2;
    std::vector<double> re(N, 0.0), im(N, 0.0);
    for (int i = 0; i < N; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / N);
        re[i] = out[start + i] * w;
    }
    fft(re.data(), im.data(), N);
    const double fExpect = fin * cfg.ratio;
    // dominant peak in [30, 6000] Hz
    int kpeak = -1;
    double bestMag = 0.0;
    for (int k = (int)(30.0 * N / sr); k <= (int)(6000.0 * N / sr); ++k) {
        const double mg = re[k] * re[k] + im[k] * im[k];
        if (mg > bestMag) { bestMag = mg; kpeak = k; }
    }
    if (kpeak > 0 && kpeak > 1 && kpeak < N - 2) {
        const double a = std::sqrt(re[kpeak - 1] * re[kpeak - 1] + im[kpeak - 1] * im[kpeak - 1]);
        const double b = std::sqrt(re[kpeak] * re[kpeak] + im[kpeak] * im[kpeak]);
        const double c = std::sqrt(re[kpeak + 1] * re[kpeak + 1] + im[kpeak + 1] * im[kpeak + 1]);
        const double delta = 2.0 * (c - a) / (a + 2.0 * b + c);  // Hann-exact
        m.pitch_hz_est = (kpeak + delta) * sr / N;
    } else {
        m.pitch_hz_est = 0.0;
    }
    m.pitch_err_cents = 1200.0 * std::log2(m.pitch_hz_est / fExpect);

    // THD+N: total windowed power minus the fundamental's windowed power
    // (plus its AM sidebands? NO -- like-for-like: remove ONLY the
    // fundamental peak bin +/- 2 bins; everything else counts as distortion
    // + noise + AM sidebands; identical treatment for both variants).
    double pTot = 0.0;
    for (int k = 1; k < N / 2; ++k) pTot += re[k] * re[k] + im[k] * im[k];
    double pFund = 0.0;
    for (int k = std::max(1, kpeak - 2); k <= kpeak + 2; ++k)
        pFund += re[k] * re[k] + im[k] * im[k];
    const double thdn = (pTot > 0.0) ? std::sqrt(std::max(0.0, pTot - pFund) / pFund) : 0.0;
    m.thdn_db = 20.0 * std::log10(std::max(thdn, 1e-12));

    // step + rms
    double mx = 0.0, ss = 0.0;
    for (int k = start + 1; k < n; ++k) {
        mx = std::max(mx, std::abs((double)out[k] - (double)out[k - 1]));
        ss += (double)out[k] * out[k];
    }
    m.max_step = mx;
    m.rms_out = std::sqrt(ss / (n - start));
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    std::string jsonPath;
    int seconds = 4;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json" && i + 1 < argc) jsonPath = argv[++i];
        else if (a == "--seconds" && i + 1 < argc) seconds = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "usage: %s [--json <path>] [--seconds <n>]\n", argv[0]); return 2; }
    }
    struct Case { const char* name; double shiftSemis; };
    const Case cases[] = {
        {"shift_minus24st", -24.0},
        {"shift_plus4st", 4.0},
        {"shift_minus1st", -1.0},
    };
    std::printf("octavox_r19_variant_metrics (%s): Lagrange6 vs ObsKernel6 under"
                " identical settings (48 kHz mono, 1 voice, spread off, fb 0,\n"
                " wet 1, bypass off, block 128, input 440 Hz pure sine amp 0.5;"
                " %d s per render, best of 3)\n\n",
                versionString(), seconds);
    std::printf("%-16s %-11s %11s %11s %10s %9s %9s %9s\n", "CASE", "VARIANT",
                "ns/frame", "xrealtime", "pitch_c", "THD+N dB", "max step", "rms out");
    std::string j = "{\n  \"tool\": \"octavox_r19_variant_metrics\",\n  \"version\": \"";
    j += versionString();
    j += "\",\n  \"rows\": [\n";
    size_t row = 0;
    double wu;
    for (const auto& cs : cases) {
        const Metrics a = measureVariant(Interpolation::Lagrange6, cs.shiftSemis, seconds, wu);
        const Metrics b = measureVariant(Interpolation::ObsKernel6, cs.shiftSemis, seconds, wu);
        const char* nm[2] = {"Lagrange6", "ObsKernel6"};
        const Metrics* mm[2] = {&a, &b};
        for (int v = 0; v < 2; ++v) {
            const Metrics& m = *mm[v];
            std::printf("%-16s %-11s %11.2f %11.2f %+10.3f %9.2f %9.5f %9.5f\n",
                        cs.name, nm[v], m.ns_per_frame, m.realtime_factor,
                        m.pitch_err_cents, m.thdn_db, m.max_step, m.rms_out);
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                          "    {\"case\": \"%s\", \"variant\": \"%s\", "
                          "\"ns_per_frame\": %.3f, \"realtime_factor\": %.4f, "
                          "\"pitch_hz_est\": %.6f, \"pitch_err_cents\": %.4f, "
                          "\"thdn_db\": %.3f, \"max_step\": %.6f, "
                          "\"rms_out\": %.6f}%s\n",
                          cs.name, nm[v], m.ns_per_frame, m.realtime_factor,
                          m.pitch_hz_est, m.pitch_err_cents, m.thdn_db,
                          m.max_step, m.rms_out,
                          (row < 2 * 3 - 1) ? "," : "");
            j += buf;
            ++row;
        }
    }
    j += "  ]\n}\n";
    std::printf("\nnote: THD+N here includes the dual-tap grain overlap's"
                " amplitude-modulation sidebands (the engine's documented"
                " architecture); it is a like-for-like VARIANT comparison"
                " metric, not an absolute quality claim.\n");
    if (!jsonPath.empty()) {
        FILE* f = std::fopen(jsonPath.c_str(), "wb");
        if (f) { std::fwrite(j.data(), 1, j.size(), f); std::fclose(f); std::printf("json written: %s\n", jsonPath.c_str()); }
    }
    return 0;
}
