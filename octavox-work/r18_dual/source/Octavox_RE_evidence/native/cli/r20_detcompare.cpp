// OCTAVOX R20 -- real-material three-detector comparison tool.
// ============================================================================
// PROVENANCE: our own tool (round 20). It measures OUR independent
// harmonizer engine's three pitch-detector architectures (FullRate /
// Decimated / Hybrid) on REAL-MATERIAL-STYLE corpora -- synthetic signals
// DESIGNED to carry the acoustic characteristics of voiced sources
// (harmonic stacks with 1/k rolloff, formant weighting, vibrato,
// portamento, tremolo, breath noise, pluck transients, octave-dual
// sources), NOT pure sines. Pure-sine precision alone never selects a
// detector (the R20 specification's explicit instruction); these corpora
// exercise the failure modes real material shows.
//
// Per input x mode x rate, measured and recorded:
//   * raw estimate trajectory (every control boundary, 64 samples) -- the
//     raw detector output BEFORE the engine's median/hysteresis law,
//   * the engine's own smoothed spacing trajectory (voice 0's S,
//     the quantity the audio path actually consumes),
//   * median + max |cent error| vs the generator's KNOWN F0 law (voiced
//     segments only), octave-error ratio (|err| > 600 cents),
//     dropout ratio (estimate == 0 on voiced segments),
//     jitter (median |cent delta| between consecutive estimates),
//     tracking latency (mean cent error right after each big F0 step,
//     before re-lock),
//   * CPU (ns per input sample, steady state),
//   * output discontinuity scan (max sample-to-sample step of the wet
//     output; detector-caused output steps show up here),
//   * memory (engine prepare RSS delta, page-granular, informational).
//
// corpora (each 4 s, both 48 kHz and 96 kHz):
//   01 voice_female_220   220 Hz stack + 5 Hz vibrato +-35 cent + tremolo
//   02 voice_male_110     110 Hz stack + 4 Hz vibrato +-25 cent
//   03 speech_like_140    140 Hz formant-weighted stack, +-8% noisy drift,
//                         300 ms voiced/unvoiced gating
//   04 bass_guitar_55     55 Hz + strong 2nd harmonic, per-pluck decay
//   05 electric_guitar_196 sawtooth-ish stack, +-3 cent slow detune, pluck
//   06 strings_293_glide  293 Hz stack, 2 s portamento to 330 Hz and back
//   07 vibrato_fast_330   330 Hz, 6 Hz +-60 cent (tracking stress)
//   08 portamento_150_300 1.5 s glide 150->300 Hz, then back
//   09 transient_260      260 Hz, strong re-pluck transient every 0.5 s
//   10 breathy_180        180 Hz, -20 dB breath noise, weak 2nd harmonic
//   11 octave_pair_147    147 + 294 Hz equal voices (octave ambiguity)
//   12 low_edge_62        62 Hz (near the 60 Hz validity floor)
//
// Usage: octavox_r20_detcompare [--json <path>] [--trajdir <dir>] [--seconds s]
// Exit 0 on success. Trajectory CSVs (for visualisation) are written for
// every corpus x mode at 48 kHz (10 ms decimation) when --trajdir is given.
// ============================================================================
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "octavox/dsp.hpp"

using namespace octavox;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

double nowSec() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// -- deterministic noise (shared 64-bit LCG, the project's law) -----------
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed ? seed : 1) {}
    double next() {  // [-1, 1)
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(static_cast<int32_t>(uint32_t(s >> 33))) /
               2147483648.0;
    }
};

// -- corpus definition -----------------------------------------------------
// f0Law: returns the KNOWN F0 in Hz at time t (seconds); ampLaw: the
// amplitude envelope (0 = unvoiced/silent); the waveform is synthesised
// from a harmonic stack with per-corpus weighting.
struct Corpus {
    const char* name;
    int harmonics;
    double rolloff;      // amplitude of harmonic k ~ 1/k^rolloff
    double jitter_cents; // random per-period detune (breath/human-ness)
    double noise_amp;    // additive white noise floor
    double tremolo_hz;
    double tremolo_depth;
    double (*f0)(double t);
    double (*amp)(double t);
};

double f0_static_220(double) { return 220.0; }
double f0_static_110(double) { return 110.0; }
double f0_static_140(double) { return 140.0; }
double f0_static_55(double) { return 55.0; }
double f0_static_196(double) { return 196.0; }
double f0_static_293(double) { return 293.0; }
double f0_static_330(double) { return 330.0; }
double f0_static_260(double) { return 260.0; }
double f0_static_180(double) { return 180.0; }
double f0_static_62(double) { return 62.0; }
double f0_speech_140(double t) {
    // +-8% band-limited-ish drift via two slow sinusoids
    return 140.0 * (1.0 + 0.04 * std::sin(2.0 * M_PI * 0.7 * t) +
                    0.04 * std::sin(2.0 * M_PI * 1.9 * t + 1.3));
}
double f0_guitar_196(double t) {
    return 196.0 * (1.0 + 0.0017 * std::sin(2.0 * M_PI * 0.23 * t));
}
double f0_glide_293_330(double t) {
    if (t < 1.0) return 293.0;
    if (t < 3.0) return 293.0 + (330.0 - 293.0) * (t - 1.0) / 2.0;
    return 330.0;
}
double f0_glide_150_300(double t) {
    if (t < 1.5) return 150.0 + (300.0 - 150.0) * (t / 1.5);
    if (t < 3.0) return 300.0 - (300.0 - 150.0) * ((t - 1.5) / 1.5);
    return 150.0;
}
double f0_steps_220_440(double t) {
    // discrete 0.4 s steps: 220 -> 440 -> 330 -> 220 -> ... (lock-lag probe)
    const int step = static_cast<int>(t / 0.4);
    static const double f[] = {220.0, 440.0, 330.0, 220.0, 165.0, 293.0};
    return f[step % 6];
}
double amp_static(double) { return 1.0; }
double amp_speech(double t) {
    // 300 ms voiced / 200 ms unvoiced gating after a 250 ms lead-in
    if (t < 0.25) return 1.0;
    const double m = std::fmod(t - 0.25, 0.5);
    return (m < 0.3) ? 1.0 : 0.0;
}
double amp_pluck_05(double t) {
    // re-pluck every 0.5 s: fast attack + exponential decay
    const double m = std::fmod(t, 0.5);
    return std::min(1.0, m * 400.0) * std::exp(-2.2 * m);
}
double amp_pluck_10(double t) {
    const double m = std::fmod(t, 1.0);
    return std::min(1.0, m * 200.0) * std::exp(-1.5 * m);
}

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

struct RunMetrics {
    double median_err_cents = 0.0;
    double max_err_cents = 0.0;
    double octave_err_ratio = 0.0;
    double dropout_ratio = 0.0;
    double jitter_cents = 0.0;
    double lock_lag_ms = 0.0;   // mean time to re-lock after big F0 steps
    double cpu_ns_per_sample = 0.0;
    double max_output_step = 0.0;
    long n_estimates = 0;
};

// Synthesise the corpus waveform. Phase-continuous harmonic stack driven by
// the f0 law (phase = integral of instantaneous frequency), so glides are
// true portamenti, not step-swept sines.
std::vector<float> synthCorpus(const Corpus& c, int sr, double seconds,
                               uint64_t seed) {
    const int n = static_cast<int>(seconds * sr);
    std::vector<float> out(n);
    Lcg rng(seed);
    const double dt = 1.0 / sr;
    double phase = 0.0;
    double jitter_target = 0.0, jitter_current = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i * dt;
        const double a = c.amp(t);
        if (a <= 0.0) {
            out[i] = static_cast<float>(c.noise_amp * 0.5 * rng.next());
            continue;
        }
        // per-period random detune: hold a target for one nominal period
        const double f0 = c.f0(t);
        const double period = 1.0 / f0;
        if (std::fmod(t, period) < dt) {
            jitter_target = c.jitter_cents == 0.0
                                ? 0.0
                                : (rng.next() * 2.0 - 1.0) * c.jitter_cents;
        }
        jitter_current += (jitter_target - jitter_current) * (dt / period) * 4.0;
        const double fi = f0 * std::pow(2.0, jitter_current / 1200.0);
        phase += 2.0 * M_PI * fi * dt;
        if (phase > 2.0 * M_PI * 1e6) phase -= 2.0 * M_PI * 1e6;
        double x = 0.0;
        for (int k = 1; k <= c.harmonics; ++k) {
            const double g = 1.0 / std::pow(double(k), c.rolloff);
            x += g * std::sin(k * phase);
        }
        // tremolo
        const double trem =
            1.0 - 0.5 * c.tremolo_depth *
                      (1.0 - std::cos(2.0 * M_PI * c.tremolo_hz * t));
        x *= a * trem / c.harmonics;
        out[i] = static_cast<float>(x + c.noise_amp * rng.next());
    }
    return out;
}

// The octave-pair corpus: two independent stacks (equal amplitude).
std::vector<float> synthOctavePair(int sr, double seconds, uint64_t seed) {
    const int n = static_cast<int>(seconds * sr);
    std::vector<float> out(n);
    Lcg rng(seed);
    const double dt = 1.0 / sr;
    double ph1 = 0.0, ph2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i * dt;
        double x1 = 0.0, x2 = 0.0;
        ph1 += 2.0 * M_PI * 147.0 * dt;
        ph2 += 2.0 * M_PI * 294.0 * dt;
        for (int k = 1; k <= 8; ++k) {
            x1 += (1.0 / k) * std::sin(k * ph1);
            x2 += (1.0 / k) * std::sin(k * ph2);
        }
        const double ramp = std::min(1.0, t * 50.0);
        out[i] = static_cast<float>(ramp * 0.5 * (x1 + x2) / 8.0 +
                                    0.004 * rng.next());
    }
    return out;
}

const char* modeName(DetectorMode m) {
    switch (m) {
        case DetectorMode::FullRate: return "fullrate";
        case DetectorMode::Decimated: return "decimated";
        case DetectorMode::Hybrid: return "hybrid";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    const char* trajDir = nullptr;
    double seconds = 4.0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else if (!std::strcmp(argv[i], "--trajdir") && i + 1 < argc) trajDir = argv[++i];
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }

    const Corpus corpora[] = {
        {"voice_female_220", 12, 1.0, 3.0, 0.002, 5.0, 0.25, f0_static_220, amp_static},
        {"voice_male_110", 12, 1.05, 3.0, 0.002, 4.0, 0.2, f0_static_110, amp_static},
        {"speech_like_140", 10, 0.9, 6.0, 0.006, 0.0, 0.0, f0_speech_140, amp_speech},
        {"bass_guitar_55", 10, 1.2, 2.0, 0.001, 0.0, 0.0, f0_static_55, amp_pluck_10},
        {"electric_guitar_196", 16, 0.7, 2.5, 0.0015, 0.0, 0.0, f0_guitar_196, amp_pluck_10},
        {"strings_293_glide", 8, 1.3, 2.0, 0.001, 0.0, 0.1, f0_glide_293_330, amp_static},
        {"vibrato_fast_330", 10, 1.0, 2.0, 0.001, 6.0, 0.0, f0_static_330, amp_static},
        {"portamento_150_300", 10, 1.1, 2.0, 0.001, 0.0, 0.0, f0_glide_150_300, amp_static},
        {"f0_steps_220_440", 10, 1.0, 2.0, 0.001, 0.0, 0.0, f0_steps_220_440, amp_static},
        {"transient_260", 12, 0.85, 2.0, 0.001, 0.0, 0.0, f0_static_260, amp_pluck_05},
        {"breathy_180", 8, 1.15, 5.0, 0.01, 0.0, 0.15, f0_static_180, amp_static},
        {"octave_pair_147", 8, 1.0, 2.0, 0.004, 0.0, 0.0, f0_static_62 /*unused*/, amp_static},
        {"low_edge_62", 8, 1.1, 2.0, 0.001, 0.0, 0.0, f0_static_62, amp_static},
    };
    const int nCorpora = static_cast<int>(sizeof(corpora) / sizeof(corpora[0]));
    const DetectorMode modes[3] = {DetectorMode::FullRate, DetectorMode::Decimated,
                                   DetectorMode::Hybrid};
    const int rates[2] = {48000, 96000};

    std::string json = "[\n";
    bool firstRec = true;
    std::FILE* summary = stdout;

    for (int sr : rates) {
        for (int ci = 0; ci < nCorpora; ++ci) {
            const Corpus& c = corpora[ci];
            std::vector<float> in;
            if (std::strcmp(c.name, "octave_pair_147") == 0)
                in = synthOctavePair(sr, seconds, 7770000 + uint64_t(sr));
            else
                in = synthCorpus(c, sr, seconds, 9100000 + uint64_t(sr) + uint64_t(ci));
            // reference F0 + voiced law sampled at the control cadence
            const int cadence = 64;
            const int nCtrl = static_cast<int>(in.size()) / cadence;
            std::vector<double> refF0(nCtrl), refAmp(nCtrl);
            for (int k = 0; k < nCtrl; ++k) {
                const double t = (k * cadence + 0.5) / sr;
                refF0[k] = (std::strcmp(c.name, "octave_pair_147") == 0)
                               ? 147.0  // the deeper member = the reference
                               : c.f0(t);
                refAmp[k] = c.amp(t);
            }

            for (DetectorMode mode : modes) {
                AlignedDelayLine eng;
                Config cfg;
                cfg.sample_rate = sr;
                cfg.ratio = std::pow(2.0, 5.0 / 12.0);
                cfg.detector_mode = mode;
                if (!eng.prepare(cfg)) {
                    std::fprintf(stderr, "prepare failed\n");
                    return 1;
                }
                std::vector<float> y(in.size(), 0.0f);
                std::vector<double> est(nCtrl, 0.0);
                std::vector<double> spacing(nCtrl, 0.0);
                // control-boundary capture of raw estimate + voice-0 spacing.
                // R20 fix: the render loop steps at EXACTLY the control
                // cadence (64 samples) so every est[k] is the estimate made
                // AT that boundary (a 1024-sample block would repeat one
                // block-end estimate 16x -- the raw-estimate jitter measured
                // zero on the first version for exactly this reason).
                int done = 0, k = 0;
                while (done < static_cast<int>(in.size())) {
                    const int want = (k < nCtrl) ? (k + 1) * cadence
                                                 : static_cast<int>(in.size());
                    int blk = want - done;
                    if (blk <= 0) blk = std::min(1024, static_cast<int>(in.size()) - done);
                    if (blk <= 0) break;
                    eng.process(in.data() + done, y.data() + done, blk);
                    done += blk;
                    if (k < nCtrl && (k + 1) * cadence <= done) {
                        est[k] = eng.lastEstimateHz();
                        spacing[k] = eng.voiceStats(0).spacing;
                        ++k;
                    }
                }
                while (k < nCtrl) {  // tail guard (short signals)
                    est[k] = eng.lastEstimateHz();
                    spacing[k] = eng.voiceStats(0).spacing;
                    ++k;
                }

                // -- metrics --------------------------------------------------
                RunMetrics m;
                std::vector<double> errs;
                errs.reserve(nCtrl);
                long voiced = 0, drop = 0, octErr = 0;
                // warm-up exclusion: the first 1.2 s (window + median fill)
                const int kWarm = static_cast<int>(1.2 * sr / cadence);
                for (int q = kWarm; q < nCtrl; ++q) {
                    if (refAmp[q] <= 0.0) continue;
                    ++voiced;
                    if (est[q] <= 0.0) { ++drop; continue; }
                    const double e = std::abs(cents(est[q], refF0[q]));
                    errs.push_back(e);
                    if (e > 600.0) ++octErr;
                }
                if (!errs.empty()) {
                    std::sort(errs.begin(), errs.end());
                    m.median_err_cents = errs[errs.size() / 2];
                    m.max_err_cents = errs.back();
                }
                m.n_estimates = static_cast<long>(errs.size()) + drop;
                m.octave_err_ratio = voiced ? double(octErr) / double(voiced) : 0.0;
                m.dropout_ratio = voiced ? double(drop) / double(voiced) : 0.0;
                // jitter: median |delta cents| of consecutive voiced estimates
                {
                    std::vector<double> jd;
                    for (int q = kWarm + 1; q < nCtrl; ++q) {
                        if (refAmp[q] <= 0.0 || refAmp[q - 1] <= 0.0) continue;
                        if (est[q] <= 0.0 || est[q - 1] <= 0.0) continue;
                        jd.push_back(std::abs(cents(est[q], est[q - 1])));
                    }
                    if (!jd.empty()) {
                        std::sort(jd.begin(), jd.end());
                        m.jitter_cents = jd[jd.size() / 2];
                    }
                }
                // lock lag: after each >= 200-cent step in the reference law,
                // mean time until |err| < 20 cents again (bounded 300 ms)
                {
                    std::vector<double> lags;
                    for (int q = kWarm + 1; q < nCtrl; ++q) {
                        if (refAmp[q] <= 0.0 || refAmp[q - 1] <= 0.0) continue;
                        if (std::abs(cents(refF0[q], refF0[q - 1])) < 200.0) continue;
                        int p = q;
                        while (p < nCtrl && (p - q) * cadence < 0.3 * sr) {
                            if (refAmp[p] > 0.0 && est[p] > 0.0 &&
                                std::abs(cents(est[p], refF0[p])) < 20.0)
                                break;
                            ++p;
                        }
                        lags.push_back((p - q) * cadence * 1000.0 / sr);
                        q = p;  // skip past this event
                    }
                    if (!lags.empty()) {
                        double s = 0.0;
                        for (double v : lags) s += v;
                        m.lock_lag_ms = s / lags.size();
                    }
                }
                // CPU: re-run the render purely timed (engine warm)
                {
                    AlignedDelayLine e2;
                    e2.prepare(cfg);
                    const double tA = nowSec();
                    int d2 = 0;
                    while (d2 < static_cast<int>(in.size())) {
                        const int blk =
                            std::min(1024, static_cast<int>(in.size()) - d2);
                        e2.process(in.data() + d2, y.data() + d2, blk);
                        d2 += blk;
                    }
                    const double tB = nowSec();
                    m.cpu_ns_per_sample = (tB - tA) * 1e9 / in.size();
                }
                // output discontinuity scan (post warm-up)
                {
                    double mx = 0.0;
                    for (size_t q = static_cast<size_t>(1.2 * sr);
                         q + 1 < y.size(); ++q)
                        mx = std::max(mx, std::abs(double(y[q]) - double(y[q - 1])));
                    m.max_output_step = mx;
                }

                // -- emit -----------------------------------------------------
                char line[256];
                std::snprintf(line, sizeof(line),
                              "%-22s %-9s %6d  med %8.3f  max %9.3f  jit %7.3f  "
                              "oct %5.2f%%  drop %5.2f%%  lag %6.1fms  cpu %8.1f ns/s  step %6.4f",
                              c.name, modeName(mode), sr, m.median_err_cents,
                              m.max_err_cents, m.jitter_cents,
                              100.0 * m.octave_err_ratio, 100.0 * m.dropout_ratio,
                              m.lock_lag_ms, m.cpu_ns_per_sample, m.max_output_step);
                std::fprintf(summary, "%s\n", line);

                if (!firstRec) json += ",\n";
                firstRec = false;
                json += "  {\"corpus\": \"" + std::string(c.name) +
                        "\", \"mode\": \"" + modeName(mode) +
                        "\", \"rate\": " + std::to_string(sr) +
                        ", \"median_err_cents\": " + std::to_string(m.median_err_cents) +
                        ", \"max_err_cents\": " + std::to_string(m.max_err_cents) +
                        ", \"jitter_cents\": " + std::to_string(m.jitter_cents) +
                        ", \"octave_err_ratio\": " + std::to_string(m.octave_err_ratio) +
                        ", \"dropout_ratio\": " + std::to_string(m.dropout_ratio) +
                        ", \"lock_lag_ms\": " + std::to_string(m.lock_lag_ms) +
                        ", \"cpu_ns_per_sample\": " + std::to_string(m.cpu_ns_per_sample) +
                        ", \"max_output_step\": " + std::to_string(m.max_output_step) +
                        ", \"n_estimates\": " + std::to_string(m.n_estimates) + "}";

                // trajectory CSV (48 kHz only; 10 ms decimation) for visuals
                if (trajDir && sr == 48000) {
                    std::string path = std::string(trajDir) + "/traj_" +
                                       c.name + "_" + modeName(mode) + ".csv";
                    std::FILE* f = std::fopen(path.c_str(), "w");
                    if (f) {
                        std::fprintf(f, "t_ms,ref_f0_hz,raw_est_hz,spacing_samples\n");
                        const int dec = static_cast<int>(0.01 * sr) / cadence;
                        for (int q = kWarm; q < nCtrl; q += dec) {
                            std::fprintf(f, "%.1f,%.4f,%.4f,%.2f\n",
                                         q * cadence * 1000.0 / sr,
                                         refAmp[q] > 0.0 ? refF0[q] : 0.0,
                                         est[q], spacing[q]);
                        }
                        std::fclose(f);
                    }
                }
            }
        }
    }
    json += "\n]\n";

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
