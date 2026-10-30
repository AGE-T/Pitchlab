// OCTAVOX R15-REB1 -- default-config neutrality reference capture.
// ============================================================================
// Runs FOUR deterministic configurations through the engine and prints
// FNV-1a-64 hashes over the raw output float bits. This is the PRIMARY gate
// of the 12.0.0 -> 15.0.0 rebuild: the same tool, the same inputs, the same
// four hashes, BIT-IDENTICAL before (12.0.0) and after (15.0.0) -- proving
// the 15.0.0 core is behaviour-neutral under the default parameter surface
// (wet = 1, bypass off, no per-voice overrides, all gains 1: the engine then
// takes the special-cased legacy output path).
//
// The tool intentionally uses ONLY the round-12 public API (prepare /
// process / setRatio) so that the exact same binary logic links and runs
// against both the 12.0.0 and the 15.0.0 core. The recorded hashes live in
// native/r15_reference_hashes.txt.
//
// Modes:
//   (default)   capture the four reference hashes (core-default Lagrange6
//               kernel -- the EXISTING gate; behaviour byte-identical to
//               the R15 tool)
//   --interpolation <lagrange6|lagrange6q|obskernel6>
//               capture the four hashes with EVERY config's interpolation
//               set to the named kernel (R18-WDB: establishes the separate,
//               reproducible ObsKernel6 reference measurements WITHOUT
//               touching the default gate or r15_reference_hashes.txt)
//   --probe96   detector-at-96 kHz probe (steady 110 Hz sine, grain 8192 /
//               base 512): prints the detector estimate; documents the R13-b
//               sample-rate-relative window law (12.0.0 is blind there,
//               m < 32; 15.0.0 tracks).
//
// Determinism: the LCG, tones and chirp are pure formulas (the shared
// 64-bit LCG spec of the test harness); every block pattern is a fixed
// cycle; N = 2^18 samples per config.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "octavox/dsp.hpp"

using namespace octavox;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed)
        : s(seed * 6364136223846793005ULL + 1442695040888963407ULL) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint64_t m = (s >> 11) & ((1ULL << 53) - 1);
        return static_cast<double>(m) / 9007199254740992.0;
    }
    float noise(double amp) {
        return static_cast<float>((2.0 * next() - 1.0) * amp);
    }
};

std::vector<float> noise(uint64_t seed, int n, double amp) {
    Lcg rng(seed);
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i) x[i] = rng.noise(amp);
    return x;
}

std::vector<float> tone(double f, int sr, int n, double amp) {
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i)
        x[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * f * i / sr));
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

// FNV-1a 64 over raw float bits (little-endian bytes), same law as the
// acceptance suite's default_neutrality cases. Chainable across buffers.
uint64_t fnv1a64(const float* x, size_t n, uint64_t h = 1469598103934665603ULL) {
    for (size_t k = 0; k < n; ++k) {
        uint32_t b;
        std::memcpy(&b, &x[k], 4);
        for (int i = 0; i < 4; ++i) {
            h ^= static_cast<uint64_t>((b >> (8 * i)) & 0xffu);
            h *= 1099511628211ULL;
        }
    }
    return h;
}

uint64_t runMono(AlignedDelayLine& e, const std::vector<float>& in,
                 const int* pat, int patN) {
    std::vector<float> out(in.size(), 0.0f);
    int done = 0, pi = 0;
    while (done < static_cast<int>(in.size())) {
        const int n = std::min(pat[pi % patN],
                               static_cast<int>(in.size()) - done);
        e.process(in.data() + done, out.data() + done, n);
        done += n;
        ++pi;
    }
    return fnv1a64(out.data(), out.size());
}

// R18-WDB: the per-run interpolation override (default = none: the core's
// own default, Lagrange6, preserving the historical gate bit-exactly).
Interpolation g_interpOverride = Interpolation::Lagrange6;
bool g_interpForced = false;

Config baseConfig() {
    Config cfg;  // all defaults
    if (g_interpForced) cfg.interpolation = g_interpOverride;
    return cfg;
}

void captureFour() {
    const int sr = 48000;
    const int N = 1 << 18;  // 262144 samples (>= 2^18)

    // (1) default unity + deterministic LCG noise; blocks 64/150/512 mixed.
    {
        const auto in = noise(987654321ULL, N, 0.5);
        AlignedDelayLine e;
        Config cfg = baseConfig();  // all defaults: ratio 1, 1 voice, spread, fb 0
        e.prepare(cfg);
        const int pat[] = {64, 150, 512};
        const uint64_t h = runMono(e, in, pat, 3);
        std::printf("cfg1_default_unity_lcg_noise_b64_150_512  0x%016llx\n",
                    static_cast<unsigned long long>(h));
    }

    // (2) ratio 2^(700/1200), 8 voices, spread on, feedback 0.25,
    //     220 Hz sine; mixed blocks 150/512/64.
    {
        const auto in = tone(220.0, sr, N, 0.5);
        AlignedDelayLine e;
        Config cfg = baseConfig();
        cfg.ratio = std::pow(2.0, 700.0 / 1200.0);
        cfg.voices = 8;
        cfg.feedback = 0.25;
        e.prepare(cfg);
        const int pat[] = {150, 512, 64};
        const uint64_t h = runMono(e, in, pat, 3);
        std::printf("cfg2_r700c_8v_spread_fb025_sine220       0x%016llx\n",
                    static_cast<unsigned long long>(h));
    }

    // (3) ratio 2^(-400/1200), 4 voices, spread off, chirp 200->800 Hz;
    //     mixed blocks 64/512/150.
    {
        const auto in = chirp(200.0, 800.0, sr, N, 0.5);
        AlignedDelayLine e;
        Config cfg = baseConfig();
        cfg.ratio = std::pow(2.0, -400.0 / 1200.0);
        cfg.voices = 4;
        cfg.spread = false;
        e.prepare(cfg);
        const int pat[] = {64, 512, 150};
        const uint64_t h = runMono(e, in, pat, 3);
        std::printf("cfg3_rm400c_4v_nospread_chirp200_800    0x%016llx\n",
                    static_cast<unsigned long long>(h));
    }

    // (4) stereo 2ch, different per-channel inputs (two LCG seeds), 4
    //     voices, unity -> non-unity mid-stream (setRatio at the first block
    //     boundary at/after N/2); blocks 1024/64/512. Hash = L bits then
    //     R bits in one FNV stream.
    {
        const auto inL = noise(1111111111ULL, N, 0.4);
        const auto inR = noise(2222222222ULL, N, 0.4);
        MultichannelShifter m;
        Config cfg = baseConfig();
        cfg.voices = 4;
        m.prepare(cfg, 2);
        std::vector<float> outL(N, 0.0f), outR(N, 0.0f);
        const float* in[2] = {inL.data(), inR.data()};
        float* out[2] = {outL.data(), outR.data()};
        const int pat[] = {1024, 64, 512};
        int done = 0, pi = 0;
        bool switched = false;
        while (done < N) {
            const int n = std::min(pat[pi % 3], N - done);
            m.process(in, out, 2, n);
            done += n;
            ++pi;
            if (!switched && done >= N / 2) {
                m.setRatio(std::pow(2.0, 400.0 / 1200.0));  // unity -> shift
                switched = true;
            }
        }
        uint64_t h = fnv1a64(outL.data(), static_cast<size_t>(N));
        h = fnv1a64(outR.data(), static_cast<size_t>(N), h);
        std::printf("cfg4_stereo_2ch_4v_unity_then_r400c     0x%016llx\n",
                    static_cast<unsigned long long>(h));
    }

    std::printf("version: %s\n", versionString());
    std::printf("interpolation: %s%s\n",
                g_interpForced ? "forced-" : "core-default-",
                g_interpForced ? (g_interpOverride == Interpolation::ObsKernel6
                                     ? "ObsKernel6"
                                     : (g_interpOverride == Interpolation::Lagrange6Q
                                            ? "Lagrange6Q" : "Lagrange6"))
                               : "Lagrange6");
}

void probe96() {
    // Detector-at-96 kHz probe: steady 110 Hz sine, grain 8192 / base 512
    // (the sample-domain rate-scaled acceptance config). The detector
    // estimate is sampled at every 1024-sample block boundary after 1 s of
    // warm-up; min/max/last are printed.
    const int sr = 96000;
    const int n = 4 * sr;
    const auto in = tone(110.0, sr, n, 0.5);
    AlignedDelayLine e;
    Config cfg;
    cfg.sample_rate = 96000.0;
    cfg.grain = 8192.0;
    cfg.base_delay = 512.0;
    e.prepare(cfg);
    std::vector<float> out(n, 0.0f);
    int done = 0;
    double lo = 1e300, hi = 0.0;
    while (done < n) {
        const int k = std::min(1024, n - done);
        e.process(in.data() + done, out.data() + done, k);
        done += k;
        if (done >= sr) {
            const double f = e.lastEstimateHz();
            lo = std::min(lo, f);
            hi = std::max(hi, f);
        }
    }
    std::printf("probe96 (110 Hz sine, grain 8192, base 512, 96 kHz):\n");
    std::printf("  estimate_min_hz = %.6f\n  estimate_max_hz = %.6f\n"
                "  estimate_last_hz = %.6f\n",
                lo, hi, e.lastEstimateHz());
    std::printf("version: %s\n", versionString());
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--probe96") == 0) {
            probe96();
            return 0;
        }
        if (std::strcmp(argv[i], "--interpolation") == 0 && i + 1 < argc) {
            const char* m = argv[++i];
            g_interpForced = true;
            if (std::strcmp(m, "obskernel6") == 0)
                g_interpOverride = Interpolation::ObsKernel6;
            else if (std::strcmp(m, "lagrange6q") == 0)
                g_interpOverride = Interpolation::Lagrange6Q;
            else if (std::strcmp(m, "lagrange6") == 0)
                g_interpOverride = Interpolation::Lagrange6;
            else {
                std::fprintf(stderr,
                             "error: --interpolation must be lagrange6 | "
                             "lagrange6q | obskernel6 (got '%s')\n",
                             m);
                return 2;
            }
        }
    }
    captureFour();
    return 0;
}
