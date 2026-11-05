// OCTAVOX R22 -- the Minimum Pitch signal-path battery (P1).
// ============================================================================
// PROVENANCE: our own tool (round 22). The R22-P1 engine change unified the
// spacing-accept gate to max(fmin, f0_min) (bit-exact by detector
// dominance; see dsp.hpp) and exposed the per-estimate distinction via
// lastEstimateSpacingAccepted() / spacingGateHz() / spacingHistoryCount() /
// spacingTargetSamples(). This battery walks the WHOLE 8-level chain
//
//   1. detector lag domain      (tau_max = rate / f0_min)
//   2. estimate validity        ([f0_min, 1200] Hz)
//   3. committed estimate       (lastEstimateHz)
//   4. ENGINE spacing gate      (max(fmin, f0_min) -- the R22 unification;
//                                lastEstimateSpacingAccepted = the explicit
//                                detectable-vs-spacing-trackable distinction)
//   5. period history + S lock  (spacingHistoryCount, spacingTargetSamples,
//                                voiceStats().spacing vs k*T)
//   6. read position            (max output step through the commits)
//   7. voice pitch ratio        (measured output fundamental vs ratio*f0)
//   8. low-frequency safety     (the VERDICT -- a harness/docs concern; the
//                                engine itself never clamps, proven by the
//                                minpitch_lowfreq_transposition_honesty test)
//
// over the 10-frequency matrix x floors x transpositions x detectors:
//   tones: 32.7 (C1) 41.2 (E1) 49.0 (G1) 55.0 (A1) 58.3 (Bb1) 62.0
//          65.4 (C2) 82.4 (E2) 110.0 (A2) 220.0 (A3, in-range control)
//   floors: 40 / 55 / 60 (the default) / 80
//   ratios: 1.0 (unity control) / 0.5 (-12 st, down octave) /
//           0.25 (-24 st)
//   modes:  Decimated (default) / Hybrid / FullRate
//
// VERDICT taxonomy per cell (the P1 goal: distinguish "detectable" from
// "safely transposable"):
//   not_detectable      -- detection success < 50%
//   detectable_only     -- detected but the spacing path never locked
//                          (level 4/5 rejected: below the engine floor, or
//                          the history never filled)
//   below_audible_floor -- detected, spacing locked, but the measured
//                          output fundamental < 20 Hz (ratio*f0 sank under
//                          the audible floor -- honest record, NO engine
//                          clamp; the docs carry the recommendation)
//   safely_transposable -- detected, spacing locked, output >= 20 Hz and
//                          within 150 cents of ratio*f0
//
// The corpus is SYNTHETIC (harmonic stacks + jitter + noise floor, the
// r21_fmin generator, honestly labelled). Real recordings are measured by
// octavox_r21_realaudio (the separate category). THE DEFAULT f0_min IS NOT
// CHANGED by this battery (60.0 Hz stands; the R21 static Low Note
// evidence and this matrix are both recorded for any future decision).
//
// Usage: octavox_r22_minpitch [--json <path>] [--seconds s] [--rate hz]
// Exit 0 on success.
// ============================================================================
#include "octavox/dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

double nowSec() {
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
}

struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed ? seed : 1) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int32_t>(uint32_t(s >> 33)) * (1.0 / 2147483648.0);
    }
};

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

const char* modeName(octavox::DetectorMode m) {
    using octavox::DetectorMode;
    switch (m) {
        case DetectorMode::FullRate: return "fullrate";
        case DetectorMode::Decimated: return "decimated";
        case DetectorMode::Hybrid: return "hybrid";
    }
    return "?";
}

// Harmonic stack at a static F0 with mild human jitter + noise floor
// (the r21_fmin generator, reused verbatim for comparability).
std::vector<float> synthTone(double f0, int sr, double seconds, uint64_t seed) {
    const int n = static_cast<int>(seconds * sr);
    std::vector<float> out(n);
    Lcg rng(seed);
    const double dt = 1.0 / sr;
    double phase = 0.0, jit = 0.0, jitT = 0.0;
    const double period = 1.0 / f0;
    const int harm = std::max(6, static_cast<int>(std::min(16.0, 800.0 / f0)));
    for (int i = 0; i < n; ++i) {
        const double t = i * dt;
        if (std::fmod(t, period) < dt) jitT = (rng.next() * 2.0 - 1.0) * 2.0;
        jit += (jitT - jit) * (dt / period) * 4.0;
        phase += 2.0 * M_PI * f0 * std::pow(2.0, jit / 1200.0) * dt;
        double a = 0.0;
        for (int k = 1; k <= harm; ++k) a += (1.0 / k) * std::sin(k * phase);
        out[i] = static_cast<float>(0.5 * a / harm + 0.0015 * rng.next());
    }
    return out;
}

// Zero-crossing fundamental estimate on a window (hysteresis-free; the
// harmonic stack's fundamental dominates, so crossings ~ 2 per period).
// -- RETIRED (kept for reference): zero crossings overestimate ~20% on
// harmonic stacks (extra crossings near zero from the 2nd/3rd harmonic);
// the battery uses acFreq() below instead.

// Autocorrelation fundamental estimate on a window: decimate by 8 (all
// stack harmonics stay far below the decimated Nyquist), remove DC, then
// pick the fundamental as the SMALLEST LOCAL maximum of the normalized
// autocorrelation within 0.25 of the highest peak (>= 0.30 absolute;
// parabolic sub-lag refine). -- R22 INSTRUMENT FIX (recorded in the
// findings): the first battery run used the GLOBAL argmax, which on smooth
// low-frequency outputs lands on the trivial near-zero-lag shoulder
// (observed: r[3] = 0.9896 beating the true fundamental peak r[367] =
// 0.9382, returning 2000 Hz for a 16.35 Hz output). A fundamental is
// always a LOCAL maximum; the shoulder of a smooth waveform is monotone
// and produces none. Dead/aperiodic outputs (peak < 0.30) return 0.0.
double acFreq(const float* x, int n, int sr) {
    const int D = 8;
    const int m = n / D;
    if (m < 16) return 0.0;
    std::vector<double> v(m);
    for (int i = 0; i < m; ++i) v[i] = double(x[i * D]);
    double mean = 0.0;
    for (double s : v) mean += s;
    mean /= m;
    for (double& s : v) s -= mean;
    double e0 = 0.0;
    for (double s : v) e0 += s * s;
    if (e0 <= 0.0) return 0.0;
    const int lagMin = std::max(2, static_cast<int>((double(sr) / D) / 2000.0));
    const int lagMax = std::min(m / 2, static_cast<int>((double(sr) / D) / 5.0));
    std::vector<double> r(lagMax + 2, 0.0);
    for (int lag = lagMin; lag <= lagMax + 1; ++lag) {
        double e = 0.0;
        for (int i = lag; i < m; ++i) e += v[i] * v[i - lag];
        r[lag] = e / e0;
    }
    // highest peak over the searchable range
    double pmax = -2.0;
    for (int lag = lagMin; lag <= lagMax; ++lag)
        if (r[lag] > pmax) pmax = r[lag];
    if (pmax < 0.30) return 0.0;  // aperiodic / dead output
    // smallest LOCAL maximum within 0.25 of pmax; skip the first few lags
    // (>= 750 Hz "fundamentals" do not exist in this battery, and the
    // exclusion kills residual shoulder wiggles)
    const int scan0 = std::max(lagMin + 1, 8);
    int bestLag = -1;
    for (int lag = scan0; lag < lagMax; ++lag) {
        if (r[lag] >= r[lag - 1] && r[lag] > r[lag + 1] &&
            r[lag] >= pmax - 0.25 && r[lag] >= 0.30) {
            bestLag = lag;
            break;
        }
    }
    if (bestLag < 0) {
        // fallback: the global argmax (no qualifying local peak)
        for (int lag = lagMin; lag <= lagMax; ++lag)
            if (r[lag] == pmax) { bestLag = lag; break; }
    }
    if (bestLag < 0 || r[bestLag] <= 0.0) return 0.0;
    // parabolic refine using r[bestLag-1..bestLag+1]
    double lag = double(bestLag);
    if (bestLag > lagMin && bestLag + 1 <= lagMax + 1) {
        const double ra = r[bestLag - 1], rb = r[bestLag], rc = r[bestLag + 1];
        const double den = ra - 2.0 * rb + rc;
        if (den != 0.0) {
            const double delta = 0.5 * (ra - rc) / den;
            if (delta > -1.0 && delta < 1.0) lag += delta;
        }
    }
    return double(sr) / (lag * double(D));
}

double maxStep(const float* x, int n) {
    double m = 0.0;
    for (int i = 1; i < n; ++i) {
        const double d = std::abs(double(x[i]) - double(x[i - 1]));
        if (d > m) m = d;
    }
    return m;
}

const char* kAudibleFloorHz = "20";  // documented verdict threshold

}  // namespace

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    double seconds = 4.0;
    int sr = 48000;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--rate") && i + 1 < argc)
            sr = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }

    using octavox::AlignedDelayLine;
    using octavox::Config;
    using octavox::DetectorMode;
    using octavox::VoiceStats;

    const double tones[] = {32.7, 41.2, 49.0, 55.0, 58.3,
                            62.0, 65.4, 82.4, 110.0, 220.0};
    const int nT = 10;
    const double floors[] = {40.0, 55.0, 60.0, 80.0};
    const int nF = 4;
    const double ratios[] = {1.0, 0.5, 0.25};
    const double ratioTags[] = {0.0, -12.0, -24.0};  // semitone labels
    const int nR = 3;
    const DetectorMode modes[3] = {DetectorMode::Decimated, DetectorMode::Hybrid,
                                   DetectorMode::FullRate};

    std::string json = "[\n";
    bool firstRec = true;
    int verdicts[4] = {0, 0, 0, 0};  // not_detectable/detectable_only/
                                     // below_audible_floor/safely_transposable

    for (int ti = 0; ti < nT; ++ti) {
        const double toneHz = tones[ti];
        const std::vector<float> in =
            synthTone(toneHz, sr, seconds, 4200000ULL + uint64_t(sr) + uint64_t(ti));

        for (int fi = 0; fi < nF; ++fi) {
            const double f0min = floors[fi];
            for (int ri = 0; ri < nR; ++ri) {
                const double ratio = ratios[ri];
                for (int mi = 0; mi < 3; ++mi) {
                    const DetectorMode mode = modes[mi];
                    Config cfg;
                    cfg.sample_rate = sr;
                    cfg.ratio = ratio;
                    cfg.detector_mode = mode;
                    cfg.f0_min = f0min;
                    AlignedDelayLine eng;
                    if (!eng.prepare(cfg)) {
                        std::fprintf(stderr, "prepare failed\n");
                        return 1;
                    }
                    const double gate = eng.spacingGateHz();

                    // one pass, sampling every 64-sample cadence
                    const int n = static_cast<int>(in.size());
                    std::vector<float> y(n, 0.0f);
                    const int steady0 = n - sr;  // last 1 s = the steady window
                    long voiced = 0, hit = 0, accCnt = 0;
                    std::vector<double> ests;
                    int hist0 = -1;
                    int done = 0, k = 0;
                    while (done < n) {
                        int blk = std::min(64, n - done);
                        eng.process(in.data() + done, y.data() + done, blk);
                        done += blk;
                        ++k;
                        if (done >= steady0) {
                            if (hist0 < 0) hist0 = eng.spacingHistoryCount();
                            const double est = eng.lastEstimateHz();
                            ++voiced;
                            if (est > 0.0) {
                                ests.push_back(est);
                                const double e = std::abs(cents(est, toneHz));
                                if (e <= 50.0) ++hit;
                            }
                            if (eng.lastEstimateSpacingAccepted()) ++accCnt;
                        }
                    }
                    const double success = voiced ? double(hit) / double(voiced) : 0.0;
                    double med = 0.0;
                    if (!ests.empty()) {
                        std::sort(ests.begin(), ests.end());
                        med = ests[ests.size() / 2];
                    }
                    const double accRatio =
                        voiced ? double(accCnt) / double(voiced) : 0.0;

                    // level 5: spacing lock. -- R22 INSTRUMENT FIX
                    // (recorded in the findings): the first run compared
                    // S_target against k*(CLI-side median estimate) with a
                    // 0.51-sample tolerance, conflating the ESTIMATE
                    // QUANTIZATION (one decimated lag = 4 fullrate samples;
                    // up to ~k*4 ~ 74 samples at k = 19) with lock failure
                    // -- perfectly locked cells (output within cents of
                    // ratio*f0) scored detectable_only. The honest criterion
                    // ties S to the KNOWN tone: S must sit at an integer
                    // multiple of a period within 50 cents of the tone
                    // (kq = round(S/T_true); cents(kq*sr/S, tone) <= 50),
                    // with the engine's own acceptance state (history
                    // count) and the voice-follows gate (commits > 0 or
                    // unity parking) as necessary conditions.
                    const double stgt = eng.spacingTargetSamples();
                    const VoiceStats vs0 = eng.voiceStats(0);
                    const double tTrue = double(sr) / toneHz;
                    const double kq = std::nearbyint(stgt / tTrue);
                    const bool spacingLocked =
                        (vs0.commits > 0 || cfg.ratio == 1.0) &&
                        eng.spacingHistoryCount() > 0 && kq >= 1.0 &&
                        std::abs(cents(kq * double(sr) / stgt, toneHz)) <=
                            50.0;

                    // level 6: read-position continuity (steady window)
                    const double step = maxStep(y.data() + steady0, sr);

                    // level 7: output fundamental (autocorrelation)
                    const double fOut = acFreq(y.data() + steady0, sr, sr);
                    const double fExpect = ratio * toneHz;
                    const bool outTracks =
                        fOut > 0.0 &&
                        std::abs(cents(fOut, fExpect)) <= 150.0;

                    // level 8: the verdict
                    const char* verdict;
                    int vi;
                    if (success < 0.5) {
                        verdict = "not_detectable"; vi = 0;
                    } else if (!spacingLocked) {
                        verdict = "detectable_only"; vi = 1;
                    } else if (fOut > 0.0 && fOut < 20.0) {
                        verdict = "below_audible_floor"; vi = 2;
                    } else if (outTracks) {
                        verdict = "safely_transposable"; vi = 3;
                    } else {
                        // spacing locked and audible but the measured output
                        // deviates > 150 cents: honestly classified as
                        // detectable_only (the level-7 chain broke)
                        verdict = "detectable_only"; vi = 1;
                    }
                    ++verdicts[vi];

                    double cpu = 0.0;
                    {
                        eng.reset();
                        const double tA = nowSec();
                        int d = 0;
                        while (d < n) {
                            const int blk = std::min(1024, n - d);
                            eng.process(in.data() + d, y.data() + d, blk);
                            d += blk;
                        }
                        const double tB = nowSec();
                        cpu = (tB - tA) * 1e9 / n;
                    }

                    char line[220];
                    std::snprintf(line, sizeof(line),
                                  "tone %6.1f  floor %5.1f  st %+5.1f  %-9s "
                                  "ok %5.1f%%  acc %5.1f%%  S%7.1f  step %7.5f  "
                                  "fout %7.2f  %-19s",
                                  toneHz, f0min, ratioTags[ri], modeName(mode),
                                  100.0 * success, 100.0 * accRatio, stgt, step,
                                  fOut, verdict);
                    std::printf("%s\n", line);

                    if (!firstRec) json += ",\n";
                    firstRec = false;
                    json += "  {\"tone_hz\": " + std::to_string(toneHz) +
                            ", \"f0_min\": " + std::to_string(f0min) +
                            ", \"shift_st\": " + std::to_string(ratioTags[ri]) +
                            ", \"mode\": \"" + modeName(mode) +
                            "\", \"rate\": " + std::to_string(sr) +
                            ", \"gate_hz\": " + std::to_string(gate) +
                            ", \"success_ratio\": " + std::to_string(success) +
                            ", \"est_median_hz\": " + std::to_string(med) +
                            ", \"spacing_accept_ratio\": " +
                            std::to_string(accRatio) +
                            ", \"spacing_target\": " + std::to_string(stgt) +
                            ", \"spacing_locked\": " +
                            (spacingLocked ? "true" : "false") +
                            ", \"voice0_commits\": " +
                            std::to_string(double(vs0.commits)) +
                            ", \"steady_max_step\": " + std::to_string(step) +
                            ", \"out_hz_measured\": " + std::to_string(fOut) +
                            ", \"out_hz_expected\": " + std::to_string(fExpect) +
                            ", \"cpu_ns_per_sample\": " + std::to_string(cpu) +
                            ", \"verdict\": \"" + verdict + "\"}";
                }
            }
        }
    }
    json += "\n]\n";

    std::printf(
        "\nverdict summary (audible floor %s Hz; engine never clamps -- the\n"
        "verdict is a harness/docs concern, see the R22 findings):\n"
        "  not_detectable      : %d\n"
        "  detectable_only     : %d\n"
        "  below_audible_floor : %d\n"
        "  safely_transposable : %d\n",
        kAudibleFloorHz, verdicts[0], verdicts[1], verdicts[2], verdicts[3]);

    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", jsonPath);
            return 1;
        }
        std::fwrite(json.data(), 1, json.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "json written: %s\n", jsonPath);
    }
    return 0;
}
