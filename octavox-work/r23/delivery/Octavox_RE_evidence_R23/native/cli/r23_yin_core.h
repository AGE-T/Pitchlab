// PITCHLAB R23 -- canonical YIN core (shared reference library).
// ============================================================================
// PROVENANCE: our own tool (round 23). A SLOW, CLEAR, TEST-ONLY reference
// implementation of the YIN family, independent of the optimized production
// detector in src/dsp.cpp (which is a private class there; this file shares
// NO code with it). Its purpose:
//   * validate the exact canonical equations (difference function support
//     conventions, CMNDF from lag 1, threshold rule, parabolic variants)
//     against independently computed expected vectors (Python-side,
//     embedded below);
//   * provide the accuracy reference for the Gate-5 detector comparison;
//   * test every boundary the R23 brief §3 mandates: flat minima,
//     asymmetric minima, zero/near-zero denominators, boundary minima,
//     non-finite inputs;
//   * prove the FFT-acceleration (yinfast-type) equivalence to the direct
//     computation within a documented tolerance.
//
// Variants implemented (see PITCHLAB_R23_YIN_MATHEMATICAL_DERIVATION.md):
//   A paper_fixedW      -- de Cheveigne & Kawahara 2002: fixed integration
//                          W (one-sided frame), CMNDF from lag 1, threshold
//                          on the (interpolated) local minimum, global-min
//                          fallback, parabolic refinement (two laws: the
//                          paper's raw-d abscissa vs the common CMNDF
//                          abscissa -- both switchable).
//   B librosa_shrink    -- fixed N-sample frame, shrinking support (N-tau
//                          terms, zero-extended shifted window), CMNDF from
//                          lag 1, first-local-min-below-threshold with
//                          global-min fallback, CMNDF parabola (the librosa
//                          convention, per the R23 source research).
//   C legacy_r22        -- the delivered engine's law, reimplemented here
//                          independently for same-frame comparison:
//                          second-half segment, m = win/2 - tau_max terms,
//                          truncated CMNDF ((i+1) multiplier, cumulative
//                          from tau_lo), threshold 0.20, first-local-min +
//                          argmin fallback, CMNDF parabola (FullRate law).
//   D fft_exact         -- variant A's difference function computed via
//                          zero-padded FFT linear correlation (the
//                          yinfast-type acceleration): mathematically exact
//                          up to floating-point round-off.
//   E paper_step6       -- the original paper's Step 6 (best local
//                          estimate) as an OFFLINE reference: temporal
//                          neighbourhood search + restricted-lag re-run.
//
// Usage: octavox_r23_yin_reference [--json <path>]
// Exit 0 iff every check passes.
// ============================================================================
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Cd = std::complex<double>;

int g_checks = 0, g_fails = 0;

void check(bool ok, const char* what, double got = 0.0, double want = 0.0) {
    ++g_checks;
    if (!ok) {
        ++g_fails;
        std::printf("FAIL %-58s got %.9g want %.9g\n", what, got, want);
    }
}

// ---------------------------------------------------------------------------
// Difference functions
// ---------------------------------------------------------------------------

// Variant A support: d(tau) = sum_{j=0}^{W-1} (x[t+j] - x[t+tau+j])^2.
// Input support required: [t, t + W + tau_max).
void diffFixedW(const double* x, int t, int W, int tauMin, int tauMax,
                std::vector<double>& d /* size tauMax-tauMin+1, d[0] = lag tauMin */) {
    d.assign(static_cast<size_t>(tauMax - tauMin + 1), 0.0);
    for (int tau = tauMin; tau <= tauMax; ++tau) {
        double s = 0.0;
        for (int j = 0; j < W; ++j) {
            const double a = x[t + j] - x[t + tau + j];
            s += a * a;
        }
        d[static_cast<size_t>(tau - tauMin)] = s;
    }
}

// Variant B support: d(k) = sum_{m=0}^{N-k-1} (x[m] - x[m+k])^2 over an
// N-sample frame (the shrinking-support convention; equivalent to comparing
// the frame against its zero-extended k-shifted copy).
void diffShrinking(const double* x, int N, int tauMin, int tauMax,
                   std::vector<double>& d) {
    d.assign(static_cast<size_t>(tauMax - tauMin + 1), 0.0);
    for (int k = tauMin; k <= tauMax; ++k) {
        double s = 0.0;
        const int terms = N - k;
        for (int m = 0; m < terms; ++m) {
            const double a = x[m] - x[m + k];
            s += a * a;
        }
        d[static_cast<size_t>(k - tauMin)] = s;
    }
}

// ---------------------------------------------------------------------------
// CMNDF laws
// ---------------------------------------------------------------------------

// Canonical: cm[0] = 1 (lag 0); cm[tau] = d(tau) / ((1/tau) sum_{j=1..tau} d(j)).
// Input d[0] = lag-1 difference (size n covers lags 1..n). Output cm has the
// same indexing (cm[0] = lag 1's value). Guards: run <= 0 -> cm = 1.
void cmndfCanonical(const std::vector<double>& d, std::vector<double>& cm) {
    const size_t n = d.size();
    cm.assign(n, 1.0);
    double run = 0.0;
    for (size_t i = 0; i < n; ++i) {
        run += d[i];
        cm[i] = (run > 0.0) ? d[i] * static_cast<double>(i + 1) / run : 1.0;
    }
}

// Legacy R22 truncated law: d indices i = 0..n-1 correspond to lags
// tau = tau_lo..tau_max; running sum starts at tau_lo and the multiplier is
// the accumulated COUNT (i+1), not the lag.
void cmndfTruncated(const std::vector<double>& d, std::vector<double>& cm) {
    const size_t n = d.size();
    cm.assign(n, 1.0);
    double run = 0.0;
    for (size_t i = 0; i < n; ++i) {
        run += d[i];
        cm[i] = (run > 0.0) ? d[i] * static_cast<double>(i + 1) / run : 1.0;
    }
}

// ---------------------------------------------------------------------------
// Candidate selection: first local minimum below threshold (paper Step 4),
// global-minimum fallback. Non-strict plateau comparisons on both sides.
// Returns the array index (into cm) and sets accepted.
int selectFirstLocalMinBelow(const std::vector<double>& cm, int iMin, int iMax,
                              double thr, bool& accepted) {
    accepted = false;
    for (int i = std::max(iMin, 1); i < std::min<int>(iMax, static_cast<int>(cm.size()) - 1); ++i) {
        if (cm[static_cast<size_t>(i)] < thr &&
            cm[static_cast<size_t>(i)] <= cm[static_cast<size_t>(i - 1)] &&
            cm[static_cast<size_t>(i)] <= cm[static_cast<size_t>(i + 1)]) {
            accepted = true;
            return i;
        }
    }
    // fallback: global argmin over [iMin, iMax]
    int best = iMin;
    for (int i = iMin; i <= iMax && i < static_cast<int>(cm.size()); ++i)
        if (cm[static_cast<size_t>(i)] < cm[static_cast<size_t>(best)]) best = i;
    return best;
}

// Scale-aware parabolic interpolation guard: the raw difference values scale
// with amplitude^2, so a fixed absolute epsilon is level-dependent. We use a
// RELATIVE guard: |den| <= 1e-12 * (|y0|+|y1|+|y2|) rejects flat/degenerate
// valleys (the R23 brief's scale-aware numerical guard).
double parabolicOffset(const double* y, int i) {
    const double y0 = y[i - 1], y1 = y[i], y2 = y[i + 1];
    const double den = y0 - 2.0 * y1 + y2;
    const double scale = std::abs(y0) + std::abs(y1) + std::abs(y2);
    if (!(std::abs(den) > 1e-12 * scale) || scale == 0.0) return 0.0;
    const double off = 0.5 * (y0 - y2) / den;
    if (!(off > -1.0 && off < 1.0)) return 0.0;  // non-convex guard
    return off;
}

// ---------------------------------------------------------------------------
// FFT machinery (variant D): iterative radix-2, size a power of two.
// ---------------------------------------------------------------------------
void fft(std::vector<Cd>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * M_PI / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const Cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            Cd w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const Cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (Cd& v : a) v /= static_cast<double>(n);
}

// FFT-accelerated fixed-W difference function (yinfast-type):
//   d(tau) = E0(t) + E0(t+tau) - 2*corr_t(tau)
// with E0 via prefix sums of x^2 and corr via zero-padded LINEAR correlation
// (FFT size >= W + tau_max). Writes d[tauMin..tauMax] like diffFixedW.
void diffFixedWFft(const double* x, int t, int W, int tauMin, int tauMax,
                   const std::vector<double>& sqPrefix,  // prefix of x^2 from 0
                   std::vector<double>& d) {
    const int span = W + tauMax;              // correlation span needed
    int L = 1;
    while (L < span + W) L <<= 1;             // linear-safe FFT size
    std::vector<Cd> A(static_cast<size_t>(L), Cd(0.0, 0.0));
    std::vector<Cd> B(static_cast<size_t>(L), Cd(0.0, 0.0));
    for (int j = 0; j < W; ++j) A[static_cast<size_t>(j)] = Cd(x[t + j], 0.0);
    for (int j = 0; j < span; ++j) B[static_cast<size_t>(j)] = Cd(x[t + j], 0.0);
    fft(A, false);
    fft(B, false);
    for (size_t i = 0; i < static_cast<size_t>(L); ++i) A[i] = std::conj(A[i]) * B[i];
    fft(A, true);
    // corr_t(tau) = Re A[tau]  (sum_{j} x[t+j] * x[t+tau+j])
    auto e0 = [&](int s) {  // energy of the W-window starting at s
        return sqPrefix[static_cast<size_t>(s + W)] - sqPrefix[static_cast<size_t>(s)];
    };
    d.assign(static_cast<size_t>(tauMax - tauMin + 1), 0.0);
    for (int tau = tauMin; tau <= tauMax; ++tau) {
        const double corr = A[static_cast<size_t>(tau)].real();
        d[static_cast<size_t>(tau - tauMin)] = e0(t) + e0(t + tau) - 2.0 * corr;
    }
}

// ---------------------------------------------------------------------------
// Result type + variant drivers
// ---------------------------------------------------------------------------
struct YinResult {
    bool valid = false;
    bool finite_input = true;
    bool threshold_accepted = false;
    bool fallback_global = false;
    int reason = 0;       // 0 ok; 1 silence/zero-energy; 2 below-range;
                          // 3 above-range; 4 non-finite input; 5 no minimum
    double tau = 0.0;     // refined period (samples)
    int tau_int = 0;      // selected integer lag
    double f0 = 0.0;      // sr / tau
    double cmndf_min = 1.0;
};

bool allFinite(const double* x, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(x[i])) return false;
    return true;
}

// Variant A: paper fixed-W. periodFromRawD: true = the paper's Step-5 nuance
// (abscissa from the raw-d parabola at the selected dip), false = the common
// CMNDF-parabola law.
YinResult yinPaperFixedW(const double* x, int n, int t, int W,
                         int tauMin, int tauMax, double thr, double sr,
                         bool periodFromRawD) {
    YinResult r;
    if (t + W + tauMax > n) { r.reason = 5; return r; }
    if (!allFinite(x + t, W + tauMax)) { r.reason = 4; return r; }
    std::vector<double> d, cm;
    diffFixedW(x, t, W, tauMin, tauMax, d);
    // canonical CMNDF needs lags 1..tauMax; recompute the full range then
    // select within [tauMin, tauMax].
    std::vector<double> dFull, cmFull;
    diffFixedW(x, t, W, 1, tauMax, dFull);
    cmndfCanonical(dFull, cmFull);  // cmFull[0] = lag 1
    double e0 = 0.0;
    for (int j = 0; j < W; ++j) e0 += x[t + j] * x[t + j];
    if (!(e0 > 0.0)) { r.reason = 1; return r; }
    // index mapping: cmFull[k] is lag k+1; lag L -> index L-1
    const int iMin = tauMin - 1, iMax = tauMax - 1;
    int idx = selectFirstLocalMinBelow(cmFull, iMin, iMax, thr, r.threshold_accepted);
    r.fallback_global = !r.threshold_accepted;
    r.tau_int = idx + 1;
    r.cmndf_min = cmFull[static_cast<size_t>(idx)];
    double off;
    if (periodFromRawD) {
        // parabola on the RAW difference function at the selected lag
        // (guard: interior lags only)
        off = (idx >= 1 && idx + 1 < static_cast<int>(dFull.size()))
                  ? parabolicOffset(dFull.data(), idx)
                  : 0.0;
    } else {
        off = (idx >= 1 && idx + 1 < static_cast<int>(cmFull.size()))
                  ? parabolicOffset(cmFull.data(), idx)
                  : 0.0;
    }
    r.tau = static_cast<double>(r.tau_int) + off;
    r.f0 = r.tau > 0.0 ? sr / r.tau : 0.0;
    r.valid = r.f0 > 0.0;
    if (!r.valid) r.reason = 5;
    return r;
}

// Variant B: librosa-style shrinking support (N-frame, zero-extended shift),
// first-local-min + global fallback, CMNDF parabola.
YinResult yinLibrosaShrink(const double* x, int N, int tauMin, int tauMax,
                           double thr, double sr) {
    YinResult r;
    if (tauMax >= N) { r.reason = 5; return r; }
    if (!allFinite(x, N)) { r.reason = 4; return r; }
    std::vector<double> dFull, cmFull;
    diffShrinking(x, N, 1, tauMax, dFull);
    cmndfCanonical(dFull, cmFull);
    double e0 = 0.0;
    for (int j = 0; j < N; ++j) e0 += x[j] * x[j];
    if (!(e0 > 0.0)) { r.reason = 1; return r; }
    const int iMin = tauMin - 1, iMax = tauMax - 1;
    int idx = selectFirstLocalMinBelow(cmFull, iMin, iMax, thr, r.threshold_accepted);
    r.fallback_global = !r.threshold_accepted;
    r.tau_int = idx + 1;
    r.cmndf_min = cmFull[static_cast<size_t>(idx)];
    const double off = (idx >= 1 && idx + 1 < static_cast<int>(cmFull.size()))
                           ? parabolicOffset(cmFull.data(), idx)
                           : 0.0;
    r.tau = static_cast<double>(r.tau_int) + off;
    r.f0 = r.tau > 0.0 ? sr / r.tau : 0.0;
    r.valid = r.f0 > 0.0;
    if (!r.valid) r.reason = 5;
    return r;
}

// Variant C: the legacy R22 law (independent reimplementation for
// same-frame comparison). x = full window (win samples); the reference
// segment is the SECOND HALF (seg0 = win/2), m = win/2 - tau_max terms.
YinResult yinLegacyR22(const double* x, int win, int tauLo, int tauMin,
                       int tauMax, double thr, double sr) {
    YinResult r;
    if (!allFinite(x, win)) { r.reason = 4; return r; }
    const int seg0 = win / 2;
    const int m = (win - seg0) - tauMax;
    if (m < 1) { r.reason = 5; return r; }
    double peak = 0.0;
    for (int i = 0; i < win; ++i) peak = std::max(peak, std::abs(x[i]));
    if (peak < 1e-6) { r.reason = 1; return r; }
    const int ntau = tauMax - tauLo;  // [tauLo, tauMax)
    std::vector<double> d(static_cast<size_t>(ntau)), cm(static_cast<size_t>(ntau));
    for (int i = 0; i < ntau; ++i) {
        const int tau = tauLo + i;
        double s = 0.0;
        for (int j = 0; j < m; ++j) {
            const double a = x[seg0 + j] - x[seg0 + tau + j];
            s += a * a;
        }
        d[static_cast<size_t>(i)] = s;
    }
    cmndfTruncated(d, cm);
    const int base = tauMin - tauLo;
    int idx = -1;
    for (int i = base + 1; i < ntau - 1; ++i) {
        if (cm[static_cast<size_t>(i)] < thr &&
            cm[static_cast<size_t>(i)] <= cm[static_cast<size_t>(i - 1)] &&
            cm[static_cast<size_t>(i)] <= cm[static_cast<size_t>(i + 1)]) {
            idx = i;
            r.threshold_accepted = true;
            break;
        }
    }
    if (idx < 0) {
        idx = base;
        for (int i = base + 1; i < ntau; ++i)
            if (cm[static_cast<size_t>(i)] < cm[static_cast<size_t>(idx)]) idx = i;
        r.fallback_global = true;
    }
    r.tau_int = tauLo + idx;
    r.cmndf_min = cm[static_cast<size_t>(idx)];
    // FullRate law: parabola on the CMNDF (unclamped vertex like estimate())
    const double off =
        (idx > 0 && idx < ntau - 1)
            ? [&] {
                  const double y0 = cm[static_cast<size_t>(idx - 1)];
                  const double y1 = cm[static_cast<size_t>(idx)];
                  const double y2 = cm[static_cast<size_t>(idx + 1)];
                  const double den = y0 - 2.0 * y1 + y2;
                  if (std::abs(den) > 1e-15) {
                      const double o = 0.5 * (y0 - y2) / den;
                      if (o > -1.0 && o < 1.0) return o;
                  }
                  return 0.0;
              }()
            : 0.0;
    r.tau = static_cast<double>(r.tau_int) + off;
    r.f0 = r.tau > 0.0 ? sr / r.tau : 0.0;
    r.valid = r.f0 > 0.0;
    if (!r.valid) r.reason = 5;
    return r;
}

// Variant E: the original Step 6 (offline, look-ahead): for analysis time t,
// search estimates in [t - Tmax/2, t + Tmax/2] (hop grid) for the best
// quality (lowest interpolated d'), then re-run the estimator with the lag
// range restricted to +/-20% around that candidate.
struct FrameEst {
    int t;             // analysis frame start
    YinResult res;
};
std::vector<YinResult> yinPaperStep6Offline(const double* x, int n, int hop,
                                            int W, int tauMin, int tauMax,
                                            double thr, double sr) {
    std::vector<FrameEst> frames;
    for (int t = 0; t + W + tauMax <= n; t += hop) {
        FrameEst fe;
        fe.t = t;
        fe.res = yinPaperFixedW(x, n, t, W, tauMin, tauMax, thr, sr, true);
        frames.push_back(fe);
    }
    const int Tmax = tauMax;  // neighbourhood = Tmax/2 around t
    std::vector<YinResult> out(frames.size());
    for (size_t fi = 0; fi < frames.size(); ++fi) {
        const int t = frames[fi].t;
        // best-quality estimate in the neighbourhood (paper: by d'_theta(T_theta))
        int bestJ = -1;
        double bestQ = 1e300;
        for (size_t fj = 0; fj < frames.size(); ++fj) {
            const int dt = std::abs(frames[fj].t - t);
            if (dt * 2 > Tmax) continue;
            if (!frames[fj].res.valid) continue;
            if (frames[fj].res.cmndf_min < bestQ) {
                bestQ = frames[fj].res.cmndf_min;
                bestJ = static_cast<int>(fj);
            }
        }
        if (bestJ < 0 || !frames[fi].res.valid) {
            out[fi] = frames[fi].res;
            continue;
        }
        // restricted re-run: lag range = candidate tau +/- 20%
        const double tauC = frames[bestJ].res.tau;
        int lo = std::max(tauMin, static_cast<int>(std::floor(tauC * 0.8)));
        int hi = std::min(tauMax, static_cast<int>(std::ceil(tauC * 1.2)));
        if (hi - lo < 2) {
            out[fi] = frames[fi].res;
            continue;
        }
        YinResult rr = yinPaperFixedW(x, n, t, W, lo, hi, thr, sr, true);
        if (rr.valid && rr.cmndf_min <= frames[fi].res.cmndf_min + 1e-12) {
            out[fi] = rr;
        } else {
            out[fi] = frames[fi].res;  // keep the unrestricted estimate
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Deterministic LCG signal (identical constants in the Python check script)
// ---------------------------------------------------------------------------
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed ? seed : 1) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int32_t>(uint32_t(s >> 33)) * (1.0 / 2147483648.0);
    }
};

// The 64-sample expected vector signal (Python-computed, embedded).
const double kX64[64] = {
    0.986223, 0.901301, 0.783912, 0.016027, 0.295513, 0.981880, 0.246955,
    0.888022, 0.555923, 0.749699, 0.068017, 0.965877, 0.288914, 0.971864,
    0.128385, 0.543798, 0.834749, 0.767109, 0.528143, 0.917931, 0.563727,
    0.652258, 0.235274, 0.044373, 0.847698, 0.183283, 0.412926, 0.743151,
    0.463437, 0.445515, 0.463497, 0.793457, 0.078924, 0.130530, 0.076302,
    0.522131, 0.949313, 0.259173, 0.880502, 0.418524, 0.381055, 0.828554,
    0.152799, 0.534332, 0.393337, 0.028169, 0.927419, 0.157057, 0.935806,
    0.494050, 0.462857, 0.924609, 0.364918, 0.612282, 0.457749, 0.010511,
    0.132047, 0.688377, 0.832928, 0.820591, 0.670619, 0.467796, 0.168549,
    0.567164};
const double kDExpected[20] = {
    5.422991721, 3.213251562, 5.249141612, 2.634161461, 4.499662825,
    2.311743812, 3.784523462, 2.486137374, 3.886504507, 4.469547133,
    2.042860556, 3.557845069, 3.305625411, 3.654802737, 2.777497979,
    2.868930007, 3.134465202, 3.086652203, 2.277221722, 2.575867309};
const double kCmExpected[20] = {
    1.000000000, 0.744131784, 1.134100708, 0.637829007, 1.070369200,
    0.594509057, 0.976994237, 0.671892388, 1.044506001, 1.177508437,
    0.561779265, 0.980159261, 0.916975366, 1.012835600, 0.781714150,
    0.817282906, 0.898586590, 0.890575333, 0.669112688, 0.766177574};
const double kDBExpected[20] = {
    5.980223384, 3.879259988, 6.048578785, 3.569680969, 5.049847670,
    3.193461414, 4.409217683, 2.486137374, 3.794149297, 3.822164876,
    2.007302090, 2.503040317, 2.583694414, 2.244838903, 1.877004162,
    1.812269966, 1.230921201, 1.545933275, 0.247093325, 0.542332437};
const double kCmBExpected[20] = {
    1.000000000, 0.786909383, 1.140662903, 0.733078970, 1.029421869,
    0.691199177, 0.960605805, 0.574556997, 0.889009343, 0.905024527,
    0.499102876, 0.642586971, 0.680929146, 0.609400259, 0.526768928,
    0.524717015, 0.370419480, 0.479460533, 0.080548816, 0.184381675};

std::string jstr(const YinResult& r);

}  // namespace
