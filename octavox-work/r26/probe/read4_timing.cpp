// R26 direct-timing probe: the interpolated-read kernels (scalar reference
// vs the lane-per-read AVX2 batch kernel) on a representative ring + delay
// distribution -- the independent timing confirmation for the R26 decision
// record (mirrors probe/timing_check.cpp of R24 for the diff kernels).
//
//   which = 0 -> scalar reference (readInterpRef, one read per call)
//           1 -> AVX2 batch kernel (readInterp4AVX2, four reads per call)
//
// Ring: 2^18 doubles, w0 swept per pass; delays: the engine's law
// (base ~ 240..720 + frac*S, S ~ 220) so the reads hit the same delay
// DISTRIBUTION the production voice path produces (engaged dual taps).
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "octavox/simd.hpp"
#include "interp_tables.hpp"

struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed)
        : s(seed * 6364136223846793005ULL + 1442695040888963407ULL) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint64_t m = (s >> 11) & ((1ULL << 53) - 1);
        return (double)m / 9007199254740992.0;
    }
};

int main(int argc, char** argv) {
    const int which = argc > 1 ? atoi(argv[1]) : 0;
    const int mode = argc > 2 ? atoi(argv[2]) : (int)octavox::interp::kLagrange6;
    const int reads_per_pass = argc > 3 ? atoi(argv[3]) : 1 << 20;

    const int kRing = 1 << 18;
    std::vector<double> ring(kRing);
    Lcg rng(7);
    for (auto& v : ring) v = 2.0 * rng.next() - 1.0;

    // the engine's delay distribution (base + frac*S, S = sr/220):
    std::vector<double> delays(reads_per_pass);
    for (int i = 0; i < reads_per_pass; ++i)
        delays[i] = 240.0 + 720.0 * rng.next() + 0.37 * 220.0 * rng.next();

    const octavox::simd::ReadKernels rk =
        octavox::simd::readKernels(octavox::simd::Level::AVX2);
    const char* name = "scalar_read";
    if (which == 1) name = "avx2_read4";

    // warm-up + checksum accumulator (prevents dead-code elimination)
    double acc = 0.0;
    if (which == 0) {
        for (int i = 0; i < 4096; ++i)
            acc += rk.read(ring.data(), i, mode, delays[i]);
    } else {
        double o[4];
        for (int i = 0; i < 1024; ++i)
            rk.read4(ring.data(), i, mode, delays.data() + 4 * i, o);
        acc += o[0] + o[1] + o[2] + o[3];
    }

    auto t0 = std::chrono::steady_clock::now();
    if (which == 0) {
        for (int i = 0; i < reads_per_pass; ++i)
            acc += rk.read(ring.data(), (i * 17) & (kRing - 1), mode,
                           delays[i]);
    } else {
        const int batches = reads_per_pass / 4;
        double o[4];
        for (int b = 0; b < batches; ++b) {
            rk.read4(ring.data(), (b * 17) & (kRing - 1), mode,
                     delays.data() + 4 * b, o);
            acc += o[0];
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double reads = (double)reads_per_pass;
    std::printf("%-12s mode %d  wall %.4f s  ns/read %.3f  acc %.6f\n",
                name, mode, sec, sec * 1e9 / reads, acc);
    return 0;
}
