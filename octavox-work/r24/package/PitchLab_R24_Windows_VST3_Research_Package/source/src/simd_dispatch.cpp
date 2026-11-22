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
// FEATURE-DETECTION PATH SELECTION (R24-W):
//   * Linux/ELF builds (GCC/Clang): __builtin_cpu_supports -- the
//     R24-VALIDATED path, UNCHANGED (the bit-exact regression record was
//     produced with it).
//   * Windows builds (any compiler): the EXPLICIT CPUID+XGETBV sequence
//     from src/simd_cpuid_explicit.h (see that file's R24-W defect record:
//     zig 0.13.0 x86_64-windows-gnu provides no compiler-rt cpu_model, so
//     __builtin_cpu_supports fails to LINK there; genuine MSVC uses its
//     <intrin.h> intrinsics). -DOCTAVOX_FORCE_EXPLICIT_CPUID forces the
//     explicit path on Linux for on-hardware validation of the exact
//     Windows algorithm (the octavox_r24w_cpuid_check target).
//
// The ISA units themselves (src/simd_avx2.cpp, src/simd_fma.cpp) are
// compiled with their own ISA flags and export exactly the three symbols
// declared in include/octavox/simd.hpp; on builds without those units
// (non-x86 targets) the table resolves everything to the scalar kernels.
// ---------------------------------------------------------------------------
#include "octavox/simd.hpp"

#include <algorithm>
#include <cmath>

#if defined(__x86_64__) || defined(__i386__)
#if defined(_WIN32) || defined(OCTAVOX_FORCE_EXPLICIT_CPUID)
#define OCTAVOX_X86_EXPLICIT 1  // Windows compilers + forced validation builds
#elif defined(__GNUC__) || defined(__clang__)
#define OCTAVOX_X86_GCC_CLANG 1  // Linux/ELF: the R24-validated builtin path
#endif
#endif

#if defined(OCTAVOX_X86_EXPLICIT)
#include "simd_cpuid_explicit.h"  // cpuid_explicit::detect() (internal)
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
// GCC/Clang (Linux/ELF): __builtin_cpu_supports (the compiler's own CPUID
// resolver -- it also verifies OSXSAVE/YMM state save support, not just
// the leaf bits).
// Windows (any compiler) + forced validation builds: the explicit
// CPUID+XGETBV sequence from src/simd_cpuid_explicit.h (leaf 1 ecx: FMA
// bit 12, OSXSAVE bit 27 + XGETBV XCR0 bits 1:2 for XMM+YMM state; leaf
// 7.0 ebx: AVX2 bit 5; BOTH features gated on the YMM OS-support check --
// the R24-W fix; the original R24 MSVC branch gated only FMA).

Capabilities detectOnce() {
    Capabilities caps{false, false};
#if defined(OCTAVOX_X86_GCC_CLANG)
    __builtin_cpu_init();
    caps.avx2 = __builtin_cpu_supports("avx2");
    caps.fma = __builtin_cpu_supports("fma");
#elif defined(OCTAVOX_X86_EXPLICIT)
    caps = cpuid_explicit::detect();
#endif
    return caps;
}

}  // namespace

Capabilities detect() {
    static const Capabilities caps = detectOnce();  // thread-safe magic static
    return caps;
}

// The ISA units are compiled into every supported x86-64 build of this
// project (see CMakeLists.txt: the units are added on x86 targets only, so
// a non-x86 build reports false here and every non-Scalar mode is rejected
// at prepare()).
bool isaUnitsCompiled() {
#if defined(OCTAVOX_X86_GCC_CLANG) || defined(OCTAVOX_X86_EXPLICIT)
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
#if defined(OCTAVOX_X86_GCC_CLANG) || defined(OCTAVOX_X86_EXPLICIT)

// src/simd_avx2.cpp (compiled -mavx2, no FMA instructions anywhere in it)
void diffLagAxisAVX2(const double* x, int seg0, int tau0, int ntau, int m,
                     double* d);
void diffSampleAxisAVX2(const double* x, int seg0, int tau0, int ntau, int m,
                        double* d);
double peakAVX2(const double* x, int n);

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
#if defined(OCTAVOX_X86_GCC_CLANG) || defined(OCTAVOX_X86_EXPLICIT)
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

}  // namespace simd
}  // namespace octavox
