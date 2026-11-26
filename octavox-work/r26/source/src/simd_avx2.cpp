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
#include "interp_tables.hpp"
#include "obs_kernel_table.h"

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

// ---------------------------------------------------------------------------
// R26: THE INTERPOLATED-READ BATCH KERNEL (lane per read).
//
// readInterp4AVX2 computes four independent 6-tap interpolated ring reads in
// the four 256-bit lanes. Every lane performs EXACTLY the scalar reference
// op sequence for its delay:
//   clamp(delay >= 0) -> floor -> mu -> integer part i0
//   [kLagrange6]  ph = (int)(mu * 2048), clamp 2047; wv = lag2048[ph]
//   [kLagrange6Q] ph = (int)(mu * 256),  clamp 255;  wv = lag256[ph]
//   [kObsKernel6] ph = (int)(mu * 256),  clamp 255;  wv = kTable[ph] (float
//                  rows; the float->double conversion is EXACT)
//   then s = ((w0*r0 + w1*r1) + w2*r2) + ... strictly ascending k, one mul
//   rounding + one add rounding per tap (NO FMA: this TU is compiled without
//   FMA and -ffp-contract=off; the accumulation order and operand order
//   (acc + term) match the reference loop's `s += w[k] * ring[...]`).
// The lane result is therefore BIT-IDENTICAL to the scalar reference by
// construction (the same proof class as the lag-axis difference kernel;
// gated as a hard exact-equality battery in the R26 test suite).
//
// THE CALLER CONTRACT (documented in simd.hpp): all four lanes carry VALID
// fractional-read requests; integer-delay requests (mu == 0) never enter
// the kernel (the engine's fast path issues them as direct integer ring
// reads -- bit-exact by the mu == 0 path of the reference). Short batches
// are padded by repeating a real request; the surplus lanes' results are
// discarded. Gathers (vgatherdpd/vgatherdps) load the per-lane weights and
// ring taps -- the gather VALUES equal the scalar loads' values by
// construction, so the arithmetic above is unchanged.
// ---------------------------------------------------------------------------
void readInterp4AVX2(const double* ring, int w0, int mode,
                     const double* delays, double* out) {
    const __m256d zero = _mm256_setzero_pd();
    __m256d d = _mm256_loadu_pd(delays);
    d = _mm256_max_pd(d, zero);          // clamp delay >= 0 (finite inputs:
                                         // scalar branch `if (delay < 0.0)`
                                         // agrees for every finite delay)
    const __m256d d0 = _mm256_floor_pd(d);
    const __m256d mu = _mm256_sub_pd(d, d0);  // mu = delay - floor(delay)
    const __m128i i0 = _mm256_cvttpd_epi32(d0);  // i0 per lane (fits int32)

    if (mode == interp::kLinear) {
        const __m128i idxA =
            _mm_and_si128(_mm_add_epi32(i0, _mm_set1_epi32(w0)),
                          _mm_set1_epi32(0x3FFFF));
        // idxB = (w0 + i0 + 1) & mask -- masked independently (idxA + 1
        // could step past kRingMask; the scalar's ring[(w+i0+1)&mask] law).
        const __m128i idxB =
            _mm_and_si128(_mm_add_epi32(i0, _mm_set1_epi32(w0 + 1)),
                          _mm_set1_epi32(0x3FFFF));
        const __m256d a =
            _mm256_i32gather_pd(ring, idxA, 8);
        const __m256d b =
            _mm256_i32gather_pd(ring, idxB, 8);
        // out = a + (b - a) * mu  (reference op order)
        const __m256d res =
            _mm256_add_pd(a, _mm256_mul_pd(_mm256_sub_pd(b, a), mu));
        _mm256_storeu_pd(out, res);
        return;
    }

    // Phase index per mode (identical arithmetic to the reference).
    __m256d sacc = zero;   // s = 0.0 (the reference's initial accumulator)
    if (mode == interp::kLagrange6) {
        __m256d phv = _mm256_mul_pd(mu, _mm256_set1_pd(2048.0));
        __m128i ph = _mm256_cvttpd_epi32(phv);
        ph = _mm_min_epi32(ph, _mm_set1_epi32(2047));
        const __m128i row = _mm_mullo_epi32(ph, _mm_set1_epi32(6));
        const double* base = &interp::tables().lag2048[0][0];
        for (int k = 0; k < 6; ++k) {
            // weight per lane: lag2048[ph][k] (gather doubles, index row + k)
            const __m256d w =
                _mm256_i32gather_pd(base + k, row, 8);
            // ring tap per lane: ring[(w0 + i0 + k - 2) & mask]
            const __m128i idx = _mm_and_si128(
                _mm_add_epi32(i0,
                              _mm_set1_epi32(w0 - 2 + k)),
                _mm_set1_epi32(0x3FFFF));
            const __m256d r = _mm256_i32gather_pd(ring, idx, 8);
            // s += w * r  ->  acc = acc + (w * r)  (operand order preserved)
            sacc = _mm256_add_pd(sacc, _mm256_mul_pd(w, r));
        }
        _mm256_storeu_pd(out, sacc);
        return;
    }
    // kLagrange6Q / kObsKernel6: 256 phases.
    __m256d phv = _mm256_mul_pd(mu, _mm256_set1_pd(256.0));
    __m128i ph = _mm256_cvttpd_epi32(phv);
    ph = _mm_min_epi32(ph, _mm_set1_epi32(255));
    if (mode == interp::kLagrange6Q) {
        const __m128i row = _mm_mullo_epi32(ph, _mm_set1_epi32(6));
        const double* base = &interp::tables().lag256[0][0];
        for (int k = 0; k < 6; ++k) {
            const __m256d w = _mm256_i32gather_pd(base + k, row, 8);
            const __m128i idx = _mm_and_si128(
                _mm_add_epi32(i0, _mm_set1_epi32(w0 - 2 + k)),
                _mm_set1_epi32(0x3FFFF));
            const __m256d r = _mm256_i32gather_pd(ring, idx, 8);
            sacc = _mm256_add_pd(sacc, _mm256_mul_pd(w, r));
        }
        _mm256_storeu_pd(out, sacc);
        return;
    }
    // interp::kObsKernel6: float weight rows; the float->double lane
    // conversion (_mm256_cvtps_pd) is EXACT, so the lane then multiplies the
    // same double value the scalar `static_cast<double>(wv[k])` produces.
    {
        const __m128i row = _mm_mullo_epi32(ph, _mm_set1_epi32(6));
        const float* base = &obs_kernel::kTable[0][0];
        for (int k = 0; k < 6; ++k) {
            const __m128 wf = _mm_i32gather_ps(base + k, row, 4);
            const __m256d w = _mm256_cvtps_pd(wf);
            const __m128i idx = _mm_and_si128(
                _mm_add_epi32(i0, _mm_set1_epi32(w0 - 2 + k)),
                _mm_set1_epi32(0x3FFFF));
            const __m256d r = _mm256_i32gather_pd(ring, idx, 8);
            sacc = _mm256_add_pd(sacc, _mm256_mul_pd(w, r));
        }
        _mm256_storeu_pd(out, sacc);
    }
}

}  // namespace simd
}  // namespace octavox
