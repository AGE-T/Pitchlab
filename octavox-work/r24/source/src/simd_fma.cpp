// OCTAVOX native core -- R24 AVX2+FMA KERNEL UNIT (translation unit:
// -mavx2 -mfma; -ffp-contract=off in CMakeLists.txt)
// ============================================================================
// Every vector instruction here requires AVX2 + FMA (no AVX-512): the unit
// is safe to dispatch only on CPUs reporting BOTH features (the dispatch
// table hands these entry points out only at Level::AVX2FMA, which the
// resolver never grants without both CPUID bits).
//
// THE LAG-AXIS STRATEGY (order-preserving, FUSED per-term rounding in
// the vector lanes). The structure is EXACTLY the AVX2 unit's (see
// src/simd_avx2.cpp -- four consecutive taus per 256-bit lane batch, up
// to four batches = 16 taus per j pass, every accumulator summing its
// own tau's terms in ascending j); the ONLY arithmetic difference in the
// vector lanes is the per-term update
//     acc = fma(diff, diff, acc)      // _mm256_fmadd_pd: ONE rounding
// instead of mul-then-add (two roundings). The summation ORDER is the
// reference's; the deviation from the scalar reference is therefore
// exactly the per-term rounding fusion on the VECTOR-PROCESSED taus.
//
// The scalar TAILS (the <= ntau % 4 remainder taus) use the REFERENCE
// mul+add rounding -- bit-exact vs the Scalar level -- because a serial
// std::fma chain is vfmadd231sd-LATENCY-bound and measured 0.45x the
// SLP'd reference loop (R24 measurement); the mixed rounding is
// documented in simd.hpp's semantics table (each tau's value is fully
// deterministic either way).
//
// THE SAMPLE-AXIS VARIANT (diffSampleAxisFMA, reassociating) mirrors the
// AVX2 unit's sample-axis kernel with fused updates: two lane accumulator
// chains over j with the documented association
//     d[tau] = ((P0 + P2) + (P1 + P3)) + s_tail
// and a fused scalar tail. Benchmark-comparison variant only; NOT the
// production choice for the level.
//
// peakFMA: delegates to peakAVX2 (max |x| is exact and ISA-independent;
// the AVX2 peak contains no FMA-susceptible arithmetic, and Level::AVX2FMA
// implies AVX2).
// ---------------------------------------------------------------------------
#include "octavox/simd.hpp"

#include <immintrin.h>

#include <cmath>

namespace octavox {
namespace simd {

// Defined in src/simd_avx2.cpp (Level::AVX2FMA implies AVX2; the peak
// kernel uses no FMA and is exact at every level).
double peakAVX2(const double* x, int n);

namespace {

// One batch = 4 consecutive taus in the lanes; NB (the batch count per j
// pass) is a TEMPLATE parameter so the b-loops unroll and the accumulators
// stay in ymm registers (same reason as the AVX2 unit -- see the template
// note there; the runtime-count version spilled acc[] to the stack).
// The ntau % 4 tail uses std::fma (the level's uniform per-term rounding).
template <int NB>
void diffLagAxisBatchesFMA(const double* x, int seg0, int tau0, int ntau,
                           int m, double* d) {
    const double* xb = x + seg0;
    int i = 0;
    for (; i + 4 * NB <= ntau; i += 4 * NB) {
        const double* sub[NB];
        __m256d acc[NB];
        for (int b = 0; b < NB; ++b) {
            sub[b] = xb + tau0 + i + 4 * b;
            acc[b] = _mm256_setzero_pd();
        }
        for (int j = 0; j < m; ++j) {
            const __m256d a = _mm256_broadcast_sd(xb + j);
            for (int b = 0; b < NB; ++b) {
                const __m256d bv = _mm256_loadu_pd(sub[b] + j);
                const __m256d diff = _mm256_sub_pd(a, bv);
                acc[b] = _mm256_fmadd_pd(diff, diff, acc[b]);
            }
        }
        for (int b = 0; b < NB; ++b) {
            _mm256_storeu_pd(d + i + 4 * b, acc[b]);
        }
    }
    // remaining full batches of 4 with a single accumulator
    if (NB > 1 && i + 4 <= ntau) {
        diffLagAxisBatchesFMA<1>(x, seg0, tau0 + i, ntau - i, m, d + i);
        return;
    }
    // reference-rounded scalar tail (ntau % 4 taus): bit-exact vs the
    // Scalar level (see the file header -- a fused std::fma tail measured
    // 0.45x here, latency-bound; the reference rounding is both faster
    // and closer to the reference)
    for (; i < ntau; ++i) {
        const double* sb = xb + tau0 + i;
        double s = 0.0;
        for (int j = 0; j < m; ++j) {
            const double a = xb[j] - sb[j];
            s += a * a;
        }
        d[i] = s;
    }
}

}  // namespace

void diffLagAxisFMA(const double* x, int seg0, int tau0, int ntau, int m,
                    double* d) {
    // Adaptive batch count as a COMPILE-TIME choice (mirrors the AVX2
    // unit: >= 64 lags uses four batches = 16 taus per j pass; smaller
    // domains -- the Hybrid fine stage's ~9-21 lags, the 3-lag parabolic
    // neighbourhood -- use one batch = 4 taus per pass, vectorising all
    // but the last ntau % 4 taus; the R24 first cut routed ntau < 16 to a
    // pure scalar std::fma loop and measured 0.44x there -- the serial
    // vfmadd231sd chain is latency-bound, so the batch route is strictly
    // better).
    if (ntau >= 64)
        diffLagAxisBatchesFMA<4>(x, seg0, tau0, ntau, m, d);
    else
        diffLagAxisBatchesFMA<1>(x, seg0, tau0, ntau, m, d);
}

// Sample-axis variant (REASSOCIATING -- benchmark comparison only). The
// documented association is the AVX2 unit's:
//   d[tau] = ((P0 + P2) + (P1 + P3)) + s_tail
// with fused vector updates and a fused sequential scalar tail.
void diffSampleAxisFMA(const double* x, int seg0, int tau0, int ntau, int m,
                       double* d) {
    const double* xb = x + seg0;
    for (int i = 0; i < ntau; ++i) {
        const double* sb = xb + tau0 + i;
        __m256d acc0 = _mm256_setzero_pd();
        __m256d acc1 = _mm256_setzero_pd();
        int j = 0;
        for (; j + 8 <= m; j += 8) {
            const __m256d a0 = _mm256_loadu_pd(xb + j);
            const __m256d b0 = _mm256_loadu_pd(sb + j);
            const __m256d diff0 = _mm256_sub_pd(a0, b0);
            acc0 = _mm256_fmadd_pd(diff0, diff0, acc0);
            const __m256d a1 = _mm256_loadu_pd(xb + j + 4);
            const __m256d b1 = _mm256_loadu_pd(sb + j + 4);
            const __m256d diff1 = _mm256_sub_pd(a1, b1);
            acc1 = _mm256_fmadd_pd(diff1, diff1, acc1);
        }
        for (; j + 4 <= m; j += 4) {
            const __m256d a0 = _mm256_loadu_pd(xb + j);
            const __m256d b0 = _mm256_loadu_pd(sb + j);
            const __m256d diff0 = _mm256_sub_pd(a0, b0);
            acc0 = _mm256_fmadd_pd(diff0, diff0, acc0);
        }
        // horizontal reduction (fixed association, see the file header)
        const __m128d lo = _mm256_castpd256_pd128(acc0);
        const __m128d hi = _mm256_extractf128_pd(acc0, 1);
        __m128d s01 = _mm_add_pd(lo, hi);  // (P0+P2, P1+P3)
        const __m128d lo1 = _mm256_castpd256_pd128(acc1);
        const __m128d hi1 = _mm256_extractf128_pd(acc1, 1);
        const __m128d s23 = _mm_add_pd(lo1, hi1);
        s01 = _mm_add_pd(s01, s23);
        const __m128d shuf = _mm_unpackhi_pd(s01, s01);
        double s = _mm_cvtsd_f64(_mm_add_sd(s01, shuf));
        for (; j < m; ++j) {  // fused sequential scalar tail
            const double a = xb[j] - sb[j];
            s = std::fma(a, a, s);
        }
        d[i] = s;
    }
}

// Window peak: max |x| delegates to the AVX2 peak (exact, order-free, no
// FMA arithmetic -- bit-identical to the scalar peak at every level).
double peakFMA(const double* x, int n) { return peakAVX2(x, n); }

}  // namespace simd
}  // namespace octavox
