// OCTAVOX native core -- R24 AVX2 KERNEL UNIT (translation unit: -mavx2,
// NO FMA; compiled with -ffp-contract=off in CMakeLists.txt)
// ============================================================================
// Every instruction in this unit is AVX2-only (no FMA, no AVX-512): the unit
// is safe to dispatch on any CPU reporting AVX2. The kernels are written
// exclusively with explicit intrinsics, so the compiler never introduces a
// fused multiply-add (and -ffp-contract=off additionally forbids scalar
// contraction in the tail code).
//
// THE LAG-AXIS STRATEGY (bit-exact acceleration). The difference function
//   d[tau] = sum_j (x[seg0+j] - x[seg0+tau+j])^2
// is computed with the LAG dimension in the 256-bit lanes: one batch covers
// four CONSECUTIVE taus (their subtrahend streams x[seg0+tau0+i .. +3] are
// contiguous in memory, so each lane's operand is one unaligned vector load)
// while the minuend x[seg0+j] is broadcast. Each lane therefore executes
// EXACTLY the reference sequence for its tau -- subtract, square (one mul
// rounding), accumulate (one add rounding) -- with the accumulation in
// strictly ascending j order: the result is BIT-IDENTICAL to the scalar
// reference (verified empirically by the R24 numerical battery as a hard
// exact-equality gate over the full corpus).
//
// To break the 4-cycle accumulator dependency chain WITHOUT reassociating
// the sum, up to FOUR batches (16 taus) run in ONE j loop with four
// independent accumulators: every accumulator still sums its own taus' terms
// in ascending j order (the batches interleave in j, each lane's chain keeps
// every term in order) -- order preservation is per (tau, accumulator), and
// the horizontal combination never mixes partial sums of the same tau. This
// reaches the port-bound throughput of the machine while staying bit-exact.
//
// THE SAMPLE-AXIS VARIANT (diffSampleAxisAVX2, reassociating) is provided
// for the R24 benchmark comparison (vectorisation over j with two lane
// accumulators + sequential scalar tail): its summation order differs from
// the reference (documented association ((P0+P2)+(P1+P3)) + tail) and it is
// NOT the production choice; it exists so the two vectorisation axes can be
// measured side by side, as the R24 brief requires.
// ---------------------------------------------------------------------------
#include "octavox/simd.hpp"

#include <immintrin.h>

namespace octavox {
namespace simd {

namespace {

// One batch = 4 consecutive taus in the lanes. NB (the batch count per j
// pass) is a TEMPLATE parameter so the b-loops below unroll at compile time
// and the accumulators live in ymm registers (the R24 measurement build
// with a runtime batch count kept acc[] on the stack -- vfmadd/vadd
// operands at 0x8(%rsp) -- costing ~40% of the kernel's throughput; the
// template restored it). NB in {1, 4}: every accumulator still sums its
// own taus' terms in ascending j order -- order preservation is per
// (tau, accumulator), and the horizontal combination never mixes partial
// sums of the same tau: BIT-IDENTICAL to the scalar reference for
// mul+add rounding (hard-gated by the R24 battery).
// The last ntau % 4 taus fall back to the scalar reference loop
// (identical semantics).
template <int NB>
void diffLagAxisBatchesAVX2(const double* x, int seg0, int tau0, int ntau,
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
                const __m256d sq = _mm256_mul_pd(diff, diff);
                acc[b] = _mm256_add_pd(acc[b], sq);
            }
        }
        for (int b = 0; b < NB; ++b) {
            _mm256_storeu_pd(d + i + 4 * b, acc[b]);
        }
    }
    // remaining full batches of 4 with a single accumulator
    if (NB > 1 && i + 4 <= ntau) {
        diffLagAxisBatchesAVX2<1>(x, seg0, tau0 + i, ntau - i, m, d + i);
        return;
    }
    // scalar reference tail (ntau % 4 taus): identical order and roundings
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

void diffLagAxisAVX2(const double* x, int seg0, int tau0, int ntau, int m,
                     double* d) {
    // Adaptive batch count as a COMPILE-TIME choice (see the template note
    // above): large lag domains (the full/decimated search ranges) use four
    // batches = 16 taus per j pass; small domains (the Hybrid fine stage
    // runs ~9-21 lags; the parabolic neighbourhood 3) use one batch = 4
    // taus per pass, which still vectorises all but the last ntau % 4 taus
    // (the R24 first cut routed ntau < 16 to a pure scalar loop and
    // measured only ~1.1x there; the batch route restored the speedup).
    if (ntau >= 64)
        diffLagAxisBatchesAVX2<4>(x, seg0, tau0, ntau, m, d);
    else
        diffLagAxisBatchesAVX2<1>(x, seg0, tau0, ntau, m, d);
}

// Sample-axis variant (REASSOCIATING -- benchmark comparison only; not the
// production kernel). Vectorises over j with two lane accumulators; the
// documented association is
//   d[tau] = ((P0 + P2) + (P1 + P3)) + s_tail
// with P0..P3 the four lane partial sums (P0: j%8 in [0,4), P1: j%8 in
// [4,8) via acc1, P2/P3 the high lanes of acc0/acc1) and s_tail the
// sequential scalar remainder.
void diffSampleAxisAVX2(const double* x, int seg0, int tau0, int ntau, int m,
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
            acc0 = _mm256_add_pd(acc0, _mm256_mul_pd(diff0, diff0));
            const __m256d a1 = _mm256_loadu_pd(xb + j + 4);
            const __m256d b1 = _mm256_loadu_pd(sb + j + 4);
            const __m256d diff1 = _mm256_sub_pd(a1, b1);
            acc1 = _mm256_add_pd(acc1, _mm256_mul_pd(diff1, diff1));
        }
        for (; j + 4 <= m; j += 4) {
            const __m256d a0 = _mm256_loadu_pd(xb + j);
            const __m256d b0 = _mm256_loadu_pd(sb + j);
            const __m256d diff0 = _mm256_sub_pd(a0, b0);
            acc0 = _mm256_add_pd(acc0, _mm256_mul_pd(diff0, diff0));
        }
        // horizontal reduction (fixed association, see the header comment)
        const __m128d lo = _mm256_castpd256_pd128(acc0);
        const __m128d hi = _mm256_extractf128_pd(acc0, 1);
        __m128d s01 = _mm_add_pd(lo, hi);  // (P0+P2, P1+P3)
        const __m128d lo1 = _mm256_castpd256_pd128(acc1);
        const __m128d hi1 = _mm256_extractf128_pd(acc1, 1);
        const __m128d s23 = _mm_add_pd(lo1, hi1);
        s01 = _mm_add_pd(s01, s23);  // lane0: (P0+P2)+(P2'+P3'), lane1: ...
        __m128d shuf = _mm_unpackhi_pd(s01, s01);
        double s = _mm_cvtsd_f64(_mm_add_sd(s01, shuf));
        for (; j < m; ++j) {  // sequential scalar tail
            const double a = xb[j] - sb[j];
            s += a * a;
        }
        d[i] = s;
    }
}

// Window peak: max |x[i]|. Max is exact and order-free -> bit-identical to
// the scalar peak at every SIMD level.
double peakAVX2(const double* x, int n) {
    const __m256d signMask =
        _mm256_set1_pd(-0.0);  // clears the sign bit (|x|)
    __m256d acc = _mm256_setzero_pd();
    int i = 0;
    for (; i + 16 <= n; i += 16) {  // four independent vmax chains
        for (int u = 0; u < 4; ++u) {
            const __m256d v = _mm256_andnot_pd(
                signMask, _mm256_loadu_pd(x + i + 4 * u));
            acc = _mm256_max_pd(acc, v);
        }
    }
    for (; i + 4 <= n; i += 4) {
        const __m256d v =
            _mm256_andnot_pd(signMask, _mm256_loadu_pd(x + i));
        acc = _mm256_max_pd(acc, v);
    }
    double pk = 0.0;
    for (; i < n; ++i) {
        const double a = std::abs(x[i]);
        if (a > pk) pk = a;
    }
    // horizontal max (exact)
    const __m128d lo = _mm256_castpd256_pd128(acc);
    const __m128d hi = _mm256_extractf128_pd(acc, 1);
    __m128d m01 = _mm_max_pd(lo, hi);
    const __m128d shuf = _mm_unpackhi_pd(m01, m01);
    m01 = _mm_max_sd(m01, shuf);
    const double vh = _mm_cvtsd_f64(m01);
    return vh > pk ? vh : pk;
}

}  // namespace simd
}  // namespace octavox
