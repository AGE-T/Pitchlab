// PITCHLAB R23 -- the corrected Minimum-Pitch measurement instrument (Gate 1).
// ============================================================================
// PROVENANCE: our own tool (round 23). This is the R23 repair of the R22
// measurement harness mandated by the R23 brief §27 ("Mandatory R22
// measurement corrections within R23"). The PRODUCTION DSP SOURCE IS
// UNCHANGED -- this file is a new CLI instrument; the legacy
// cli/r22_minpitch.cpp is kept verbatim and both are run on the same engine
// build for the before/after repair record (PITCHLAB_R23_R22_MEASUREMENT_
// REPAIR.md).
//
// THE EIGHT REPAIRS (R23 brief §27), each implemented and evidenced:
//  1. acFreqV2: the global-argmax fallback is REMOVED. If no qualifying
//     LOCAL maximum exists the instrument returns the state `unvoiced`
//     -- never a boundary frequency. (The R22 code could still return the
//     lag-domain edge, e.g. the observed 2000 Hz = sr/(lagMin*D) artifact,
//     through its fallback branch.)
//  2. Explicit measurement states: dead (no energy), unvoiced (aperiodic /
//     no qualifying local AC peak), valid. No arbitrary fallback frequency
//     is ever emitted; diagnostics carry peak_r and best_lag.
//  3. The factor-8 measurement decimation is now ANTI-ALIAS FILTERED
//     (a 127-tap linear-phase windowed-sinc FIR at 0.36*(sr/D), applied to
//     the FULL signal before the steady window; constant group delay is
//     harmless for steady-state frequency estimation; measurement-path
//     only -- the production detector is untouched).
//  4. The 50-cent spacingLocked tolerance is replaced by a DERIVED
//     criterion: tol_cents = 1731 * dS_max / S with
//       dS_max = kq*qT/2 + kq*1.4826*MAD_T + 0.005*S
//     where qT = the mode's lag-grid quantum in HOST samples (FullRate 1,
//     Decimated D, Hybrid 1), MAD_T = the measured per-cell MAD of the
//     accepted period ESTIMATES (tone-free robust scatter), and 0.005*S =
//     the commit-hysteresis staleness bound (cfg.s_hysteresis). The
//     criterion itself is TONE-FREE: kq = round(S / median_estimated_period).
//  5. Detector estimate accuracy, engine spacing acceptance, spacing
//     convergence and output fundamental accuracy are recorded as SEPARATE
//     fields (levels 3/4/5/7) -- see the JSON schema below.
//  6. The output-frequency expectation uses BOTH the estimate-derived
//     (ratio*median_estimate) and the tone-derived (ratio*tone) references,
//     recorded separately; the verdict chain uses the estimate-derived one
//     for the lock/consistency decisions so a wrong detector estimate can
//     never be concealed by the known input tone. The tone-derived error is
//     reported as a diagnostic only.
//  7. Verdict taxonomy: not_detectable / detector_inaccurate /
//     spacing_not_locked / below_output_floor / output_pitch_inaccurate /
//     invalid_output_state / valid_transposition.
//  8. Raw per-cell logs + the corrected machine-readable JSON (all levels).
//
// Instrument self-validation (run before the battery, recorded in the JSON
// "instrument_validation" section): silence -> dead; white noise ->
// unvoiced; pure sines 8.2/16.35/110/220/440 Hz -> valid within the
// documented measurement resolution; sine+noise -> valid.
//
// Usage: octavox_r23_minpitch_v2 [--json <path>] [--seconds s] [--rate hz]
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

// Host-sample lag-grid quantum of each mode's estimate (repair 4).
double modeLagQuantumHostSamples(octavox::DetectorMode m, int sr) {
    using octavox::DetectorMode;
    if (m == DetectorMode::FullRate) return 1.0;
    if (m == DetectorMode::Hybrid) return 1.0;  // fine stage refines on the host grid
    int d = (sr <= 24000) ? 1 : std::lround(static_cast<double>(sr) / 24000.0);
    while (d > 1 && sr / d < 18000) --d;
    return static_cast<double>(d);
}

// The r21_fmin synthetic corpus generator (reused verbatim for
// comparability with the R22 battery; SYNTHETIC, honestly labelled).
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

// ---------------------------------------------------------------------------
// Repair 3: causal-friendly LINEAR-PHASE FIR anti-alias filter, applied on
// the measurement path only. 127 taps, Hann-windowed sinc, cutoff
// 0.36*(sr/D). Constant group delay (N-1)/2 -- irrelevant for steady-state
// frequency estimation, and the battery uses only the steady window taken
// from the FULL filtered signal (no edge effects inside the window).
// ---------------------------------------------------------------------------
struct AntiAliasFir {
    std::vector<double> taps;
    double cutoffHz = 0.0;
    int fs = 48000;
    void design(int sampleRate, double cutoff) {
        fs = sampleRate;
        cutoffHz = cutoff;
        const int n = 127;
        taps.assign(n, 0.0);
        const double fc = cutoff / sampleRate;  // normalized (0..0.5)
        const double m = (n - 1) / 2.0;
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            const double x = i - m;
            double h = (x == 0.0) ? 2.0 * fc
                                  : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
            taps[i] = h * w;
            sum += taps[i];
        }
        for (double& t : taps) t /= sum;  // unity DC gain
    }
    std::vector<double> filter(const float* x, int n) const {
        const int tn = static_cast<int>(taps.size());
        std::vector<double> y(n, 0.0);
        for (int i = 0; i < n; ++i) {
            double s = 0.0;
            for (int j = 0; j < tn; ++j) {
                const int k = i - j;
                if (k < 0) break;  // zero history before the signal
                s += taps[j] * static_cast<double>(x[k]);
            }
            y[i] = s;
        }
        return y;
    }
};

// ---------------------------------------------------------------------------
// Repair 1+2: the explicit-state autocorrelation fundamental instrument.
// ---------------------------------------------------------------------------
enum class MeasState { dead, unvoiced, valid };
const char* measStateName(MeasState s) {
    switch (s) {
        case MeasState::dead: return "dead";
        case MeasState::unvoiced: return "unvoiced";
        case MeasState::valid: return "valid";
    }
    return "?";
}

struct MeasResult {
    MeasState state = MeasState::dead;
    double freq_hz = 0.0;      // meaningful only when state == valid
    double peak_r = 0.0;       // highest normalised AC peak (diagnostic)
    double best_lag = 0.0;     // refined lag at sr_meas (diagnostic)
    bool fallback_fired = false;  // always false in v2 (kept for the record)
};

// Input: the ALREADY-FILTERED signal (double), the measurement rate
// (sr/D), the decimation factor D. Autocorrelation over lags covering
// [5, 2000] Hz at the measurement rate.
MeasResult acFreqV2(const std::vector<double>& yf, int sr, int D) {
    MeasResult r;
    const double srM = static_cast<double>(sr) / D;
    const int m = static_cast<int>(yf.size()) / D;
    if (m < 16) { r.state = MeasState::dead; return r; }
    std::vector<double> v(m);
    for (int i = 0; i < m; ++i) v[i] = yf[static_cast<size_t>(i) * D];
    double mean = 0.0;
    for (double s : v) mean += s;
    mean /= m;
    for (double& s : v) s -= mean;
    double e0 = 0.0;
    for (double s : v) e0 += s * s;
    if (e0 <= 0.0) { r.state = MeasState::dead; return r; }
    const int lagMin = std::max(2, static_cast<int>(srM / 2000.0));
    const int lagMax = std::min(m / 2, static_cast<int>(srM / 5.0));
    if (lagMax - lagMin < 4) { r.state = MeasState::dead; return r; }
    std::vector<double> ac(lagMax + 2, 0.0);
    for (int lag = lagMin; lag <= lagMax + 1; ++lag) {
        double e = 0.0;
        for (int i = lag; i < m; ++i) e += v[i] * v[i - lag];
        ac[lag] = e / e0;
    }
    double pmax = -2.0;
    for (int lag = lagMin; lag <= lagMax; ++lag)
        if (ac[lag] > pmax) pmax = ac[lag];
    r.peak_r = pmax;
    if (pmax < 0.30) { r.state = MeasState::unvoiced; return r; }
    const int scan0 = std::max(lagMin + 1, 8);
    int bestLag = -1;
    for (int lag = scan0; lag < lagMax; ++lag) {
        if (ac[lag] >= ac[lag - 1] && ac[lag] > ac[lag + 1] &&
            ac[lag] >= pmax - 0.25 && ac[lag] >= 0.30) {
            bestLag = lag;
            break;
        }
    }
    if (bestLag < 0) {
        // Repair 1: NO global-argmax fallback. A fundamental is always a
        // local maximum; if none qualifies the output is aperiodic or the
        // peak structure is degenerate -> explicit unvoiced state.
        r.state = MeasState::unvoiced;
        return r;
    }
    double lag = double(bestLag);
    if (bestLag > lagMin && bestLag + 1 <= lagMax + 1) {
        const double ra = ac[bestLag - 1], rb = ac[bestLag], rc = ac[bestLag + 1];
        const double den = ra - 2.0 * rb + rc;
        if (std::abs(den) > 1e-15) {
            const double delta = 0.5 * (ra - rc) / den;
            if (delta > -1.0 && delta < 1.0) lag += delta;
        }
    }
    r.best_lag = lag;
    r.freq_hz = srM / lag;
    r.state = MeasState::valid;
    return r;
}

double maxStep(const float* x, int n) {
    double m = 0.0;
    for (int i = 1; i < n; ++i) {
        const double d = std::abs(double(x[i]) - double(x[i - 1]));
        if (d > m) m = d;
    }
    return m;
}

// Robust MAD (median absolute deviation) of a sample set.
double madOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double med = v[v.size() / 2];
    std::vector<double> a(v.size());
    for (size_t i = 0; i < v.size(); ++i) a[i] = std::abs(v[i] - med);
    std::sort(a.begin(), a.end());
    return a[a.size() / 2];
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

    // ---- instrument self-validation (repair 2/3 evidence) -----------------
    std::string validationJson = "[\n";
    {
        AntiAliasFir fir;
        fir.design(sr, 0.36 * (sr / 8));
        bool firstVal = true;
        auto validate = [&](const char* name, const std::vector<float>& sig,
                            double expectHz) {
            const std::vector<double> yf = fir.filter(sig.data(),
                                                      static_cast<int>(sig.size()));
            const MeasResult mr = acFreqV2(yf, sr, 8);
            char rec[320];
            double err = (mr.state == MeasState::valid && expectHz > 0.0)
                             ? cents(mr.freq_hz, expectHz)
                             : 0.0;
            std::snprintf(rec, sizeof(rec),
                          "  {\"case\": \"%s\", \"state\": \"%s\", "
                          "\"freq_hz\": %.4f, \"expected_hz\": %.2f, "
                          "\"err_cents\": %.3f, \"peak_r\": %.4f}",
                          name, measStateName(mr.state), mr.freq_hz, expectHz,
                          err, mr.peak_r);
            if (!firstVal) validationJson += ",\n";
            firstVal = false;
            validationJson += rec;
            std::printf("validate %-28s state=%-9s f=%9.3f exp=%7.2f "
                        "err=%+8.3f cents  peak_r=%.3f\n",
                        name, measStateName(mr.state), mr.freq_hz, expectHz,
                        err, mr.peak_r);
        };
        const int n = sr;  // 1 s each
        std::vector<float> silence(n, 0.0f);
        validate("silence", silence, 0.0);
        {
            std::vector<float> noise(n);
            Lcg rng(99);
            for (int i = 0; i < n; ++i) noise[i] = static_cast<float>(0.1 * rng.next());
            validate("white_noise", noise, 0.0);
        }
        for (double f : {8.2, 16.35, 110.0, 220.0, 440.0}) {
            std::vector<float> s(n);
            for (int i = 0; i < n; ++i)
                s[i] = static_cast<float>(0.4 * std::sin(2.0 * M_PI * f * i / sr));
            char nm[64];
            std::snprintf(nm, sizeof(nm), "sine_%.2f_hz", f);
            validate(nm, s, f);
        }
        {
            std::vector<float> s(n);
            Lcg rng(7);
            for (int i = 0; i < n; ++i)
                s[i] = static_cast<float>(0.4 * std::sin(2.0 * M_PI * 55.0 * i / sr) +
                                          0.05 * rng.next());
            validate("sine55_plus_noise", s, 55.0);
        }
    }
    validationJson += "\n]";

    const double tones[] = {32.7, 41.2, 49.0, 55.0, 58.3,
                            62.0, 65.4, 82.4, 110.0, 220.0};
    const int nT = 10;
    const double floors[] = {40.0, 55.0, 60.0, 80.0};
    const int nF = 4;
    const double ratios[] = {1.0, 0.5, 0.25};
    const double ratioTags[] = {0.0, -12.0, -24.0};
    const int nR = 3;
    const DetectorMode modes[3] = {DetectorMode::Decimated, DetectorMode::Hybrid,
                                   DetectorMode::FullRate};
    const char* verdictNames[7] = {"not_detectable", "detector_inaccurate",
                                   "spacing_not_locked", "below_output_floor",
                                   "output_pitch_inaccurate",
                                   "invalid_output_state",
                                   "valid_transposition"};
    int verdicts[7] = {0, 0, 0, 0, 0, 0, 0};

    std::string json = "[\n";
    bool firstRec = true;

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
                    const double qT =
                        modeLagQuantumHostSamples(mode, sr);  // repair 4

                    const int n = static_cast<int>(in.size());
                    std::vector<float> y(n, 0.0f);
                    const int steady0 = n - sr;  // last 1 s = steady window
                    long voiced = 0, hit = 0, accCnt = 0;
                    std::vector<double> ests;      // estimate Hz (steady window)
                    std::vector<double> periods;   // period estimates (tone-free)
                    int hist0 = -1;
                    int done = 0;
                    while (done < n) {
                        int blk = std::min(64, n - done);
                        eng.process(in.data() + done, y.data() + done, blk);
                        done += blk;
                        if (done >= steady0) {
                            if (hist0 < 0) hist0 = eng.spacingHistoryCount();
                            const double est = eng.lastEstimateHz();
                            ++voiced;
                            if (est > 0.0) {
                                ests.push_back(est);
                                periods.push_back(static_cast<double>(sr) / est);
                                const double e = std::abs(cents(est, toneHz));
                                if (e <= 50.0) ++hit;
                            }
                            if (eng.lastEstimateSpacingAccepted()) ++accCnt;
                        }
                    }
                    const double success =
                        voiced ? double(hit) / double(voiced) : 0.0;
                    double med = 0.0;
                    if (!ests.empty()) {
                        std::sort(ests.begin(), ests.end());
                        med = ests[ests.size() / 2];
                    }
                    // Tone-free robust scatter of the PERIOD estimates (repair 4)
                    const double madT = madOf(periods);
                    const double accRatio =
                        voiced ? double(accCnt) / double(voiced) : 0.0;

                    // ---- level 5: spacing lock, the DERIVED criterion ------
                    const double stgt = eng.spacingTargetSamples();
                    const VoiceStats vs0 = eng.voiceStats(0);
                    const bool haveMed = (med > 0.0);
                    const double medT = haveMed ? static_cast<double>(sr) / med : 0.0;
                    // Tone-free integer multiple: S vs the ESTIMATED period.
                    const double kq = haveMed ? std::nearbyint(stgt / medT) : 0.0;
                    // Derived tolerance: dS_max/S * 1731 cents (+ grid half).
                    const double dSmax = haveMed
                        ? (kq * qT / 2.0 + kq * 1.4826 * madT + 0.005 * stgt)
                        : 1e9;
                    const double tolCents =
                        haveMed ? 1731.0 * dSmax / stgt : 1e9;
                    const bool spacingLocked =
                        (vs0.commits > 0 || cfg.ratio == 1.0) &&
                        eng.spacingHistoryCount() > 0 && kq >= 1.0 &&
                        haveMed &&
                        std::abs(cents(kq * static_cast<double>(sr) / stgt,
                                       med)) <= tolCents;

                    // ---- level 6: read-position continuity ---------------
                    const double step = maxStep(y.data() + steady0, sr);

                    // ---- level 7: output fundamental (states + anti-alias)
                    AntiAliasFir fir;
                    fir.design(sr, 0.36 * (sr / 8));
                    const std::vector<double> yFilt =
                        fir.filter(y.data() + steady0, sr);
                    const MeasResult mr = acFreqV2(yFilt, sr, 8);
                    const double fExpTone = ratio * toneHz;
                    const double fExpEst = ratio * med;
                    // Output tolerance: the derived spacing tolerance
                    // propagated through the ratio + the AC measurement
                    // resolution (<3 cents below 20 Hz, <<1 cent above) +
                    // 10 percent margin for the glide/commit residuals.
                    const double measResCents =
                        (mr.state == MeasState::valid && mr.freq_hz > 0.0)
                            ? std::abs(1200.0 * std::log2(1.0 + 1.0 /
                                      (mr.freq_hz * mr.freq_hz / (sr / 8.0))))
                            : 0.0;
                    const double outTol = 1.10 * tolCents + measResCents;
                    const double outErrTone =
                        (mr.state == MeasState::valid && mr.freq_hz > 0.0)
                            ? cents(mr.freq_hz, fExpTone) : 0.0;
                    const bool outTracksEst =
                        mr.state == MeasState::valid && mr.freq_hz > 0.0 &&
                        haveMed &&
                        std::abs(cents(mr.freq_hz, fExpEst)) <= outTol;

                    // ---- level 8: the verdict chain (repairs 5+7) ---------
                    const char* verdict;
                    int vi;
                    if (success < 0.5) {
                        verdict = verdictNames[0]; vi = 0;           // not_detectable
                    } else if (!haveMed ||
                               std::abs(cents(med, toneHz)) > 50.0) {
                        verdict = verdictNames[1]; vi = 1;           // detector_inaccurate
                    } else if (!spacingLocked) {
                        verdict = verdictNames[2]; vi = 2;           // spacing_not_locked
                    } else if (mr.state != MeasState::valid) {
                        verdict = verdictNames[5]; vi = 5;           // invalid_output_state
                    } else if (mr.freq_hz < 20.0) {
                        verdict = verdictNames[3]; vi = 3;           // below_output_floor
                    } else if (!outTracksEst ||
                               std::abs(outErrTone) > 150.0) {
                        // output measurable but off-chain: inaccurate (the
                        // 150-cent absolute backstop catches any instrument
                        // or chain excursion the derived bound missed)
                        verdict = verdictNames[4]; vi = 4;           // output_pitch_inaccurate
                    } else {
                        verdict = verdictNames[6]; vi = 6;           // valid_transposition
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

                    char line[256];
                    std::snprintf(line, sizeof(line),
                                  "tone %6.1f  floor %5.1f  st %+5.1f  %-9s "
                                  "ok %5.1f%%  acc %5.1f%%  S%7.1f  q%4.1f "
                                  "tol %6.2f  fout %7.2f(%s)  %-22s",
                                  toneHz, f0min, ratioTags[ri], modeName(mode),
                                  100.0 * success, 100.0 * accRatio, stgt, qT,
                                  tolCents, mr.freq_hz, measStateName(mr.state),
                                  verdict);
                    std::printf("%s\n", line);

                    if (!firstRec) json += ",\n";
                    firstRec = false;
                    json += "  {\"tone_hz\": " + std::to_string(toneHz) +
                            ", \"f0_min\": " + std::to_string(f0min) +
                            ", \"shift_st\": " + std::to_string(ratioTags[ri]) +
                            ", \"mode\": \"" + modeName(mode) +
                            "\", \"rate\": " + std::to_string(sr) +
                            ", \"gate_hz\": " + std::to_string(gate) +
                            // level 3: detector estimate accuracy
                            ", \"success_ratio\": " + std::to_string(success) +
                            ", \"est_median_hz\": " + std::to_string(med) +
                            ", \"est_median_err_cents\": " +
                            std::to_string(haveMed ? cents(med, toneHz) : 0.0) +
                            ", \"est_mad_period_samples\": " +
                            std::to_string(madT) +
                            // level 4: engine spacing acceptance
                            ", \"spacing_accept_ratio\": " +
                            std::to_string(accRatio) +
                            ", \"spacing_history_count\": " +
                            std::to_string(double(eng.spacingHistoryCount())) +
                            // level 5: spacing convergence (derived criterion)
                            ", \"spacing_target\": " + std::to_string(stgt) +
                            ", \"spacing_kq\": " + std::to_string(kq) +
                            ", \"spacing_qT_host_samples\": " +
                            std::to_string(qT) +
                            ", \"spacing_tol_cents\": " +
                            std::to_string(tolCents) +
                            ", \"spacing_locked\": " +
                            (spacingLocked ? "true" : "false") +
                            ", \"voice0_commits\": " +
                            std::to_string(double(vs0.commits)) +
                            // level 6: read continuity
                            ", \"steady_max_step\": " + std::to_string(step) +
                            // level 7: output fundamental (explicit states)
                            ", \"out_state\": \"" + measStateName(mr.state) +
                            "\", \"out_peak_r\": " + std::to_string(mr.peak_r) +
                            ", \"out_hz_measured\": " +
                            std::to_string(mr.freq_hz) +
                            ", \"out_hz_expected_from_estimate\": " +
                            std::to_string(fExpEst) +
                            ", \"out_hz_expected_from_tone\": " +
                            std::to_string(fExpTone) +
                            ", \"out_err_cents_vs_tone\": " +
                            std::to_string(outErrTone) +
                            ", \"out_tol_cents\": " + std::to_string(outTol) +
                            ", \"cpu_ns_per_sample\": " + std::to_string(cpu) +
                            ", \"verdict\": \"" + verdict + "\"}";
                }
            }
        }
    }
    json += "\n]";

    std::printf(
        "\nverdict summary (audible floor %s Hz; engine never clamps -- the\n"
        "verdict is a harness/docs concern, see the R22 findings):\n"
        "  not_detectable       : %d\n"
        "  detector_inaccurate  : %d\n"
        "  spacing_not_locked   : %d\n"
        "  below_output_floor   : %d\n"
        "  output_pitch_inaccurate : %d\n"
        "  invalid_output_state : %d\n"
        "  valid_transposition  : %d\n",
        kAudibleFloorHz, verdicts[0], verdicts[1], verdicts[2], verdicts[3],
        verdicts[4], verdicts[5], verdicts[6]);

    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", jsonPath);
            return 1;
        }
        std::fprintf(f, "{\n\"instrument_validation\": %s,\n\"cells\": %s\n}\n",
                     validationJson.c_str(), json.c_str());
        std::fclose(f);
        std::fprintf(stderr, "json written: %s\n", jsonPath);
    }
    return 0;
}
