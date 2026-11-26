// OCTAVOX native core -- R24 SIMD DISPATCH (baseline translation unit)
// ============================================================================
// Compiled with the BASELINE ISA (x86-64 SSE2) -- the same flags as
// src/dsp.cpp -- so the scalar kernels below reproduce the historical
// reference arithmetic bit-exactly (sequential j accumulation, one mul and
// one add rounding per term; FMA contraction is impossible in this TU
// because the baseline ISA has no FMA instruction).
//
// This TU also owns:
//   * the one-time CPUID feature detection (AVX2 and FMA independently),
//   * the kernel-table resolution (function pointers into the ISA units),
//   * the Mode -> Level resolution rules.
//
// The ISA units themselves (src/simd_avx2.cpp, src/simd_fma.cpp) are
// compiled with their own ISA flags and export exactly the three symbols
// declared in include/octavox/simd.hpp; on builds without those units
// (non-x86 targets) the table resolves everything to the scalar kernels.
// ---------------------------------------------------------------------------
#include "octavox/simd.hpp"
#include "interp_tables.hpp"
#include "obs_kernel_table.h"

#include <algorithm>
#include <cmath>

#if defined(__x86_64__) || defined(__i386__)
#if defined(__GNUC__) || defined(__clang__)
#define OCTAVOX_X86_GCC_CLANG 1
#elif defined(_MSC_VER)
#define OCTAVOX_X86_MSVC 1
#endif
#endif

#if defined(OCTAVOX_X86_MSVC)
#include <intrin.h>
#endif

namespace octavox {
namespace simd {

namespace {

// ---- the scalar reference kernels (bit-exact vs the inline estimate
// loops: identical op sequence -- sub, mul, sequential add) ----------------

void diffScalar(const double* x, int seg0, int tau0, int ntau, int m,
                double* d) {
    for (int i = 0; i < ntau; ++i) {
        const int tau = tau0 + i;
        double s = 0.0;
        for (int j = 0; j < m; ++j) {
            const double a = x[seg0 + j] - x[seg0 + tau + j];
            s += a * a;
        }
        d[i] = s;
    }
}

double peakScalar(const double* x, int n) {
    double peak = 0.0;
    for (int i = 0; i < n; ++i) peak = std::max(peak, std::abs(x[i]));
    return peak;
}

// ---- CPU feature detection (once; cached) ---------------------------------
//
// GCC/Clang: __builtin_cpu_supports (the compiler's own CPUID resolver --
// it also verifies OSXSAVE/YMM state save support, not just the leaf bits).
// MSVC: the canonical CPUID sequence (leaf 1 ecx: FMA bit 12, OSXSAVE bit
// 27 + XGETBV YMM state; leaf 7.0 ebx: AVX2 bit 5).

Capabilities detectOnce() {
    Capabilities caps{false, false};
#if defined(OCTAVOX_X86_GCC_CLANG)
    __builtin_cpu_init();
    caps.avx2 = __builtin_cpu_supports("avx2");
    caps.fma = __builtin_cpu_supports("fma");
#elif defined(OCTAVOX_X86_MSVC)
    int regs[4];
    __cpuid(regs, 0);
    const int maxLeaf = regs[0];
    if (maxLeaf >= 1) {
        __cpuid(regs, 1);
        const bool osxsave = (regs[2] & (1 << 27)) != 0;
        caps.fma = (regs[2] & (1 << 12)) != 0;
        if (osxsave) {
            const unsigned long long xcr0 = _xgetbv(0);
            const bool ymmSaved = (xcr0 & 0x6) == 0x6;
            if (!ymmSaved) caps.fma = false;
        } else {
            caps.fma = false;
        }
        if (maxLeaf >= 7) {
            __cpuidex(regs, 7, 0);
            caps.avx2 = (regs[1] & (1 << 5)) != 0;
        }
    }
#endif
    return caps;
}

}  // namespace

Capabilities detect() {
    static const Capabilities caps = detectOnce();  // thread-safe magic static
    return caps;
}

}  // namespace simd

// ---------------------------------------------------------------------------
// R26: the shared interpolation tables (octavox::interp). Moved VERBATIM
// from src/dsp.cpp (identical constructor arithmetic, identical values);
// the engine's readInterp and the SIMD read kernels read the SAME objects.
// ---------------------------------------------------------------------------
namespace interp {
namespace {

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

}  // namespace

const Tables& tables() {
    static const Tables t = [] {
        Tables tt{};
        double w[6];
        for (int r = 0; r < 2048; ++r) {
            lagrange6Weights((r + 0.5) / 2048.0, w);  // bin centres (Python parity)
            for (int k = 0; k < 6; ++k) tt.lag2048[r][k] = w[k];
        }
        for (int r = 0; r < 256; ++r) {
            lagrange6Weights((r + 0.5) / 256.0, w);
            for (int k = 0; k < 6; ++k) tt.lag256[r][k] = w[k];
        }
        return tt;
    }();  // thread-safe magic static
    return t;
}

}  // namespace interp

namespace simd {

// ---------------------------------------------------------------------------
// R26: the scalar read reference (the VERBATIM readInterp arithmetic from
// src/dsp.cpp on an explicit ring / write-index / mode -- same ops, same
// order; the kernel battery gates this against the AVX2 batch kernel and
// the engine bit-identity matrix gates it against dsp.cpp's inline path).
// kRingMask = 0x3FFFF (kRingSize 2^18, dsp.cpp Impl) -- fixed by construction.
// ---------------------------------------------------------------------------
double readInterpRef(const double* ring, int w0, int mode, double delay) {
    if (delay < 0.0) delay = 0.0;
    const double d0 = std::floor(delay);
    const double mu = delay - d0;
    const int i0 = static_cast<int>(d0);
    if (mu == 0.0) {
        return ring[(w0 + i0) & 0x3FFFF];  // exact integer delay
    }
    switch (mode) {
        case interp::kLinear: {
            const double a = ring[(w0 + i0) & 0x3FFFF];
            const double b = ring[(w0 + i0 + 1) & 0x3FFFF];
            return a + (b - a) * mu;
        }
        case interp::kLagrange6: {
            int ph = static_cast<int>(mu * 2048.0);  // floor, mu < 1
            if (ph > 2047) ph = 2047;
            const double* wv = interp::tables().lag2048[ph];
            double s = 0.0;
            for (int k = 0; k < 6; ++k)
                s += wv[k] * ring[(w0 + i0 + k - 2) & 0x3FFFF];
            return s;
        }
        case interp::kLagrange6Q: {
            int ph = static_cast<int>(mu * 256.0);
            if (ph > 255) ph = 255;
            const double* wv = interp::tables().lag256[ph];
            double s = 0.0;
            for (int k = 0; k < 6; ++k)
                s += wv[k] * ring[(w0 + i0 + k - 2) & 0x3FFFF];
            return s;
        }
        case interp::kObsKernel6: {
            // OBS data mode: floor-indexed 256 phases (target phase law).
            int ph = static_cast<int>(mu * 256.0);
            if (ph > 255) ph = 255;
            const float* wv = obs_kernel::kTable[ph];
            double s = 0.0;
            for (int k = 0; k < 6; ++k)
                s += static_cast<double>(wv[k]) *
                     ring[(w0 + i0 + k - 2) & 0x3FFFF];
            return s;
        }
    }
    return 0.0;
}

// The ISA units are compiled into every supported x86-64 build of this
// project (see CMakeLists.txt: the units are added on x86 targets only, so
// a non-x86 build reports false here and every non-Scalar mode is rejected
// at prepare()).
bool isaUnitsCompiled() {
#if defined(OCTAVOX_X86_GCC_CLANG) || defined(OCTAVOX_X86_MSVC)
    return true;
#else
    return false;
#endif
}

const char* modeName(Mode m) {
    switch (m) {
        case Mode::Scalar: return "scalar";
        case Mode::AVX2: return "avx2";
        case Mode::AVX2FMA: return "avx2+fma";
        case Mode::Auto: return "auto";
    }
    return "?";
}

const char* levelName(Level l) {
    switch (l) {
        case Level::Scalar: return "scalar";
        case Level::AVX2: return "avx2";
        case Level::AVX2FMA: return "avx2+fma";
    }
    return "?";
}

Level resolve(Mode m) {
    const bool units = isaUnitsCompiled();
    const Capabilities caps = detect();
    const bool avx2 = units && caps.avx2;
    const bool fma = units && caps.avx2 && caps.fma;
    switch (m) {
        case Mode::Scalar:
            return Level::Scalar;
        case Mode::AVX2:
            return avx2 ? Level::AVX2 : Level::Scalar;
        case Mode::AVX2FMA:
            return fma ? Level::AVX2FMA : Level::Scalar;
        case Mode::Auto:
            // R24 decision: Auto prefers the BIT-EXACT accelerated level --
            // the measured AVX2 lag kernel is >= the FMA one on every
            // representative shape (see PITCHLAB_R24_SIMD_BENCHMARK_
            // RESULTS.md) while FMA additionally deviates from the
            // reference rounding. AVX2+FMA stays an explicit opt-in.
            if (avx2) return Level::AVX2;
            return Level::Scalar;
    }
    return Level::Scalar;
}

// ---------------------------------------------------------------------------
// Kernel tables. The ISA-unit entry points are DECLARED here and DEFINED in
// their own translation units (no static-init dependency: the pointers are
// materialised on first use of kernels(), which prepare() calls).
// ---------------------------------------------------------------------------
#if defined(OCTAVOX_X86_GCC_CLANG) || defined(OCTAVOX_X86_MSVC)

// src/simd_avx2.cpp (compiled -mavx2, no FMA instructions anywhere in it)
void diffLagAxisAVX2(const double* x, int seg0, int tau0, int ntau, int m,
                     double* d);
void diffSampleAxisAVX2(const double* x, int seg0, int tau0, int ntau, int m,
                        double* d);
double peakAVX2(const double* x, int n);
void readInterp4AVX2(const double* ring, int w0, int mode,
                      const double* delays, double* out);

// src/simd_fma.cpp (compiled -mavx2 -mfma; fused per-term rounding)
void diffLagAxisFMA(const double* x, int seg0, int tau0, int ntau, int m,
                    double* d);
void diffSampleAxisFMA(const double* x, int seg0, int tau0, int ntau, int m,
                       double* d);
double peakFMA(const double* x, int n);

#endif  // x86

Kernels kernels(Level l) {
    Kernels k{};
    k.diff = &diffScalar;
    k.diff_sample = &diffScalar;
    k.diff_lag = &diffScalar;
    k.peak = &peakScalar;
#if defined(OCTAVOX_X86_GCC_CLANG) || defined(OCTAVOX_X86_MSVC)
    switch (l) {
        case Level::Scalar:
            break;
        case Level::AVX2:
            // Production choice: the lag-axis kernel -- ORDER-PRESERVING,
            // bit-identical to the scalar reference by construction (see
            // simd.hpp's semantics table; empirically verified by the R24
            // numerical battery as a hard exact-equality gate).
            k.diff = &diffLagAxisAVX2;
            k.diff_sample = &diffSampleAxisAVX2;
            k.diff_lag = &diffLagAxisAVX2;
            k.peak = &peakAVX2;
            break;
        case Level::AVX2FMA:
            // Production choice: the lag-axis FMA kernel -- order-preserving
            // with fused per-term rounding (the smallest possible deviation
            // from the reference: only the per-term rounding differs, the
            // summation order is the reference's). The sample-axis FMA
            // variant (reassociating) is exposed as diff_sample for the
            // benchmark comparison; see PITCHLAB_R24_SIMD_BENCHMARK_RESULTS
            // for the measured decision record.
            k.diff = &diffLagAxisFMA;
            k.diff_sample = &diffSampleAxisFMA;
            k.diff_lag = &diffLagAxisFMA;
            k.peak = &peakFMA;
            break;
    }
#endif
    k.level = l;
    return k;
}

// ---------------------------------------------------------------------------
// R26: the read-kernel table. `read` is ALWAYS the scalar reference (the
// engine's own inline readInterp is the same arithmetic -- single source of
// values, gated bit-exactly through the engine matrix). `read4` is nullptr
// at Scalar (the engine's verbatim inline path runs there -- zero risk to
// the recorded hash pins) and the AVX2 lane-per-read batch kernel at BOTH
// accelerated levels: READS NEVER FUSE (the AVX2FMA level reuses the AVX2
// read kernel verbatim, so the voice path is bit-exact at EVERY level; only
// the detector carries the documented fused-rounding deviation).
// ---------------------------------------------------------------------------
ReadKernels readKernels(Level l) {
    ReadKernels r{};
    r.level = l;
    r.read = &readInterpRef;
    r.read4 = nullptr;
#if defined(OCTAVOX_X86_GCC_CLANG) || defined(OCTAVOX_X86_MSVC)
    if (l == Level::AVX2 || l == Level::AVX2FMA) r.read4 = &readInterp4AVX2;
#endif
    return r;
}

}  // namespace simd
}  // namespace octavox
