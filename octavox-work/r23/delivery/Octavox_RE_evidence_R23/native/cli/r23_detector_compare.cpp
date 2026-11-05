// PITCHLAB R23 -- detector comparison battery (Gate 5).
// ============================================================================
// PROVENANCE: our own tool (round 23). Compares, on IDENTICAL signals and
// frame timestamps (the engine's 64-sample cadence boundaries), the R22
// engine detector modes (FullRate / Decimated / Hybrid via the public
// AlignedDelayLine::lastEstimateHz API) against the canonical reference
// variants (paper fixed-W YIN at W = T_max / 1.5·T_max / 2·T_max, and the
// exact FFT acceleration), over the R23 brief §9 synthetic corpus at
// 44.1 / 48 / 96 / 192 kHz.
//
// Signals (all deterministic, fixed-seed, SYNTHETIC -- honestly labelled;
// ground truth f0 per sample where defined):
//   sine, rich harmonics, strong-H2, missing fundamental, octave trap,
//   vibrato, glissando, abrupt steps, AM, white noise, SNR-10dB, silence,
//   DC, clipped, attack/release transients.
//
// Metrics per (signal, rate, variant): frames, valid_ratio, median/p95
// cents error (tonal signals, steady region), gross-error rate (>50c),
// octave-error rate, jitter (MAD of frame-to-frame cents on steady tones),
// CPU ns per detector update (canonical variants) / per sample (engine).
//
// Usage: octavox_r23_detector_compare [--json <path>] [--max-rate hz]
// Exit 0 on success.
// ============================================================================
#include "octavox/dsp.hpp"
#include "r23_yin_core.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

double nowSec() { return static_cast<double>(std::clock()) / CLOCKS_PER_SEC; }
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

// ---------------------------------------------------------------------------
// Corpus generator: returns the signal + per-sample ground-truth f0 (0 where
// undefined). Phase-accumulating oscillator: the instantaneous law is exact.
// ---------------------------------------------------------------------------
struct Sig {
    std::vector<float> x;
    std::vector<double> gt;   // per-sample true f0 (0 = unvoiced/undefined)
    const char* name;
    bool tonal;               // cents metrics meaningful
    bool steady;              // jitter metric meaningful
};

Lcg& corpusRng() {
    static Lcg r(20261104);
    return r;
}

double gtAt(const Sig& s, int i) {
    if (s.gt.empty()) return 0.0;
    return s.gt[static_cast<size_t>(std::min(std::max(i, 0),
                                             static_cast<int>(s.gt.size()) - 1))];
}

Sig makeSignal(const char* name, int sr, double seconds, int kind) {
    Sig s;
    s.name = name;
    const int n = static_cast<int>(seconds * sr);
    s.x.assign(static_cast<size_t>(n), 0.0f);
    s.gt.assign(static_cast<size_t>(n), 0.0);
    Lcg& rng = corpusRng();
    const double dt = 1.0 / sr;
    double phase = 0.0;
    auto addTone = [&](int i0, int i1, double f0, double am = 1.0,
                      double vibCents = 0.0, double vibHz = 0.0,
                      const std::vector<double>& harms = {1.0, 0.5, 0.33, 0.25,
                                                           0.2, 0.16, 0.14, 0.125},
                      double noiseAmp = 0.0, bool gtOn = true) {
        for (int i = i0; i < i1; ++i) {
            double f = f0;
            if (vibCents > 0.0)
                f = f0 * std::pow(2.0, vibCents / 1200.0 *
                                       std::sin(2.0 * M_PI * vibHz * i * dt));
            if (gtOn) s.gt[static_cast<size_t>(i)] = f;
            phase += 2.0 * M_PI * f * dt;
            double a = 0.0;
            for (size_t k = 0; k < harms.size() && k * f < 16000.0; ++k)
                a += harms[k] * std::sin(static_cast<double>(k + 1) * phase);
            s.x[static_cast<size_t>(i)] += static_cast<float>(
                am * a / static_cast<double>(harms.size()) +
                noiseAmp * rng.next());
        }
    };
    switch (kind) {
        case 0:  // pure sine 110
            s.tonal = s.steady = true;
            addTone(0, n, 110.0, 0.8, 0, 0, {1.0});
            break;
        case 1:  // rich harmonics 220
            s.tonal = s.steady = true;
            addTone(0, n, 220.0, 0.8);
            break;
        case 2:  // strong second harmonic (fund weak, H2 strong)
            s.tonal = s.steady = true;
            addTone(0, n, 110.0, 0.8, 0, 0, {0.15, 1.0, 0.2});
            break;
        case 3:  // missing fundamental (H2..H5 of 220)
            s.tonal = s.steady = true;
            addTone(0, n, 220.0, 0.8, 0, 0, {0.0, 1.0, 0.7, 0.5, 0.35});
            break;
        case 4:  // octave trap: 110 fundamental + strong 220
            s.tonal = s.steady = true;
            addTone(0, n, 110.0, 0.8, 0, 0, {1.0, 1.4, 0.4, 0.2});
            break;
        case 5:  // vibrato 220 +-50c @5 Hz
            s.tonal = true; s.steady = false;
            addTone(0, n, 220.0, 0.8, 50.0, 5.0, {1.0, 0.5, 0.33});
            break;
        case 6: {  // glissando 110 -> 220 (log-linear)
            s.tonal = true; s.steady = false;
            for (int i = 0; i < n; ++i) {
                const double f = 110.0 * std::pow(220.0 / 110.0,
                                                  static_cast<double>(i) / n);
                s.gt[static_cast<size_t>(i)] = f;
                phase += 2.0 * M_PI * f * dt;
                double a = 0.0;
                for (int k = 1; k <= 4; ++k) a += std::sin(k * phase) / k;
                s.x[static_cast<size_t>(i)] = static_cast<float>(0.8 * a / 4.0);
            }
            break;
        }
        case 7: {  // abrupt steps 110 <-> 220
            s.tonal = true; s.steady = false;
            double f = 110.0;
            for (int i = 0; i < n; ++i) {
                if (i % (n / 4) == 0 && i > 0) f = (f < 200.0) ? 220.0 : 110.0;
                s.gt[static_cast<size_t>(i)] = f;
                phase += 2.0 * M_PI * f * dt;
                double a = 0.0;
                for (int k = 1; k <= 4; ++k) a += std::sin(k * phase) / k;
                s.x[static_cast<size_t>(i)] = static_cast<float>(0.8 * a / 4.0);
            }
            break;
        }
        case 8:  // AM 220 (50% depth @ 5 Hz)
            s.tonal = s.steady = true;
            for (int i = 0; i < n; ++i) {
                s.gt[static_cast<size_t>(i)] = 220.0;
                phase += 2.0 * M_PI * 220.0 * dt;
                const double am = 0.4 + 0.4 * std::sin(2.0 * M_PI * 5.0 * i * dt);
                double a = 0.0;
                for (int k = 1; k <= 4; ++k) a += std::sin(k * phase) / k;
                s.x[static_cast<size_t>(i)] = static_cast<float>(am * 0.8 * a / 4.0);
            }
            break;
        case 9:  // white noise (unvoiced)
            s.tonal = s.steady = false;
            for (int i = 0; i < n; ++i)
                s.x[static_cast<size_t>(i)] = static_cast<float>(0.4 * rng.next());
            break;
        case 10:  // 220 stack + noise at SNR 10 dB
            s.tonal = s.steady = true;
            {
                double sigP = 0.0;
                for (size_t k = 0; k < 8; ++k) sigP += 1.0 / (k + 1) / (k + 1);
                // tone RMS ~= 0.5/sqrt(2)*sqrt(sigP)... approximate: set noise
                // for ~10 dB relative to the stack by calibration loop
                addTone(0, n, 220.0, 0.8, 0, 0,
                        {1.0, 0.5, 0.33, 0.25, 0.2, 0.16, 0.14, 0.125},
                        0.05);
            }
            break;
        case 11:  // silence
            s.tonal = s.steady = false;
            break;
        case 12:  // DC 0.5
            s.tonal = s.steady = false;
            for (int i = 0; i < n; ++i) s.x[static_cast<size_t>(i)] = 0.5f;
            break;
        case 13: {  // clipped 220 stack
            s.tonal = s.steady = true;
            for (int i = 0; i < n; ++i) {
                s.gt[static_cast<size_t>(i)] = 220.0;
                phase += 2.0 * M_PI * 220.0 * dt;
                double a = 0.0;
                for (int k = 1; k <= 4; ++k) a += std::sin(k * phase) / k;
                double v = 0.8 * a / 4.0;
                if (v > 0.25) v = 0.25;
                if (v < -0.25) v = -0.25;
                s.x[static_cast<size_t>(i)] = static_cast<float>(v);
            }
            break;
        }
        case 14: {  // transient bursts (220 stack, fast AR, gaps)
            s.tonal = true; s.steady = false;
            const int seg = n / 6;
            for (int b = 0; b < 6; ++b) {
                const int i0 = b * seg, i1 = i0 + seg;
                for (int i = i0; i < i1; ++i) {
                    s.gt[static_cast<size_t>(i)] = 220.0;
                    phase += 2.0 * M_PI * 220.0 * dt;
                    double env = 1.0;
                    const double rel = static_cast<double>(i - i0) / seg;
                    if (rel < 0.05) env = rel / 0.05;
                    else if (rel > 0.9) env = (1.0 - rel) / 0.1;
                    double a = 0.0;
                    for (int k = 1; k <= 4; ++k) a += std::sin(k * phase) / k;
                    s.x[static_cast<size_t>(i)] =
                        static_cast<float>(env * 0.8 * a / 4.0);
                }
            }
            break;
        }
        default:
            break;
    }
    return s;
}

const int kNumKinds = 15;
const char* kKindNames[kNumKinds] = {
    "sine110",     "harm220",  "strongH2_110", "missingfund_220",
    "octrap_110", "vibrato220", "gliss110_220", "steps110_220",
    "am220",       "whitenoise", "snr10_220",   "silence",
    "dc05",        "clipped220", "transients220"};

// ---------------------------------------------------------------------------
// A tracked estimate stream with per-frame ground truth.
// ---------------------------------------------------------------------------
struct Frame {
    int sample;      // boundary sample (estimate timestamp)
    double est;      // Hz (0 = invalid)
    double gt;       // Hz (0 = undefined)
};

struct Metrics {
    int frames = 0, valid = 0;
    double med_c = 0.0, p95_c = 0.0, gross = 0.0, octv = 0.0;
    double jitter_c = 0.0;
    double cpu_ns = 0.0;   // per update (canonical) or per sample (engine)
    double valid_ratio() const { return frames ? double(valid) / frames : 0.0; }
};

Metrics metricsFrom(const std::vector<Frame>& fs, bool tonal, bool steady,
                    double cpuNs) {
    Metrics m;
    m.frames = static_cast<int>(fs.size());
    m.cpu_ns = cpuNs;
    std::vector<double> errs;
    std::vector<double> seq;   // consecutive valid estimates (Hz) for jitter
    double last = 0.0;
    for (const auto& f : fs) {
        if (f.est > 0.0) {
            ++m.valid;
            if (steady && last > 0.0) {
                seq.push_back(std::abs(cents(f.est, last)));
            }
            last = f.est;
        }
        if (f.est > 0.0 && f.gt > 0.0 && tonal) errs.push_back(cents(f.est, f.gt));
    }
    if (!errs.empty()) {
        std::sort(errs.begin(), errs.end());
        const auto absErr = [](double e) { return std::abs(e); };
        std::vector<double> a(errs.size());
        for (size_t i = 0; i < errs.size(); ++i) a[i] = absErr(errs[i]);
        std::sort(a.begin(), a.end());
        m.med_c = a[a.size() / 2];
        m.p95_c = a[a.size() * 95 / 100];
        m.gross = static_cast<double>(std::count_if(a.begin(), a.end(),
                                    [](double v) { return v > 50.0; })) / a.size();
        m.octv = static_cast<double>(std::count_if(
                     errs.begin(), errs.end(), [](double e) {
                         const double v = std::abs(e);
                         return v > 1150.0 && v < 1250.0;
                     })) / errs.size();
    }
    if (!seq.empty()) {
        std::sort(seq.begin(), seq.end());
        m.jitter_c = seq[seq.size() / 2];
    }
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    int maxRate = 192000;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else if (!std::strcmp(argv[i], "--max-rate") && i + 1 < argc)
            maxRate = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    using octavox::AlignedDelayLine;
    using octavox::Config;
    using octavox::DetectorMode;

    const int rates[] = {44100, 48000, 96000, 192000};
    const int nRates = 4;
    const double seconds = 3.0;
    std::string json = "[\n";
    bool first = true;

    for (int ki = 0; ki < kNumKinds; ++ki) {
        for (int ri = 0; ri < nRates; ++ri) {
            const int sr = rates[ri];
            if (sr > maxRate) break;
            const Sig sig = makeSignal(kKindNames[ki], sr, seconds, ki);
            const int n = static_cast<int>(sig.x.size());
            const int steady0 = n / 4;   // skip the cold start
            const int cad = 64;          // engine default detection_interval
            const int tauMax = static_cast<int>(static_cast<double>(sr) / 60.0);
            const int tauMin = std::max(2, static_cast<int>(static_cast<double>(sr) / 1200.0));

            // -- engine modes ------------------------------------------------
            for (int mi = 0; mi < 3; ++mi) {
                const DetectorMode mode = static_cast<DetectorMode>(mi);
                Config cfg;
                cfg.sample_rate = static_cast<double>(sr);
                cfg.ratio = 1.0;   // transparency: detector-only comparison
                cfg.detector_mode = mode;
                AlignedDelayLine eng;
                if (!eng.prepare(cfg)) { std::fprintf(stderr, "prepare failed\n"); return 1; }
                std::vector<Frame> fs;
                int done = 0;
                const double tA = nowSec();
                std::vector<float> y(static_cast<size_t>(n), 0.0f);
                while (done < n) {
                    const int blk = std::min(cad, n - done);
                    eng.process(sig.x.data() + done, y.data() + done, blk);
                    done += blk;
                    if (done % cad == 0 && done >= steady0) {
                        Frame f;
                        f.sample = done;
                        f.est = eng.lastEstimateHz();
                        f.gt = gtAt(sig, done - 1);
                        fs.push_back(f);
                    }
                }
                const double tB = nowSec();
                const Metrics m =
                    metricsFrom(fs, sig.tonal, sig.steady,
                                (tB - tA) * 1e9 / n);
                char js[640];
                std::snprintf(js, sizeof(js),
                    "{\"signal\": \"%s\", \"rate\": %d, \"variant\": \"%s\", "
                    "\"frames\": %d, \"valid_ratio\": %.4f, \"med_c\": %.3f, "
                    "\"p95_c\": %.3f, \"gross\": %.4f, \"octave\": %.4f, "
                    "\"jitter_c\": %.3f, \"cpu\": %.1f}",
                    kKindNames[ki], sr, modeName(mode), m.frames,
                    m.valid_ratio(), m.med_c, m.p95_c, m.gross, m.octv,
                    m.jitter_c, m.cpu_ns);
                if (!first) json += ",\n";
                first = false;
                json += js;
                std::printf("%-14s %6d %-11s v %5.2f%% med %8.3fc p95 %8.3fc "
                            "gr %5.3f oct %5.3f jit %7.3f cpu %8.1f\n",
                            kKindNames[ki], sr, modeName(mode),
                            100.0 * m.valid_ratio(), m.med_c, m.p95_c, m.gross,
                            m.octv, m.jitter_c, m.cpu_ns);
            }

            // -- canonical variants (same boundary timestamps) ----------------
            // W laws: T_max, 1.5 T_max, 2 T_max (the brief §20 window study)
            const double ws[3] = {1.0, 1.5, 2.0};
            const char* wNames[3] = {"paperW1", "paperW15", "paperW2"};
            for (int wi = 0; wi < 3; ++wi) {
                const int W = static_cast<int>(ws[wi] * tauMax);
                std::vector<double> xD(static_cast<size_t>(n));
                for (int i = 0; i < n; ++i)
                    xD[static_cast<size_t>(i)] = sig.x[static_cast<size_t>(i)];
                std::vector<double> sq(static_cast<size_t>(n) + 1, 0.0);
                for (int i = 0; i < n; ++i)
                    sq[static_cast<size_t>(i) + 1] =
                        sq[static_cast<size_t>(i)] +
                        xD[static_cast<size_t>(i)] * xD[static_cast<size_t>(i)];
                std::vector<Frame> fs, fsF;
                const int stride = 8;  // canonical frames every 8th boundary:
                // 150 estimates/s is ample for the comparison; the engine
                // modes above keep their full 64-sample cadence.
                const double tA = nowSec();
                for (int b = cad; b <= n; b += cad * stride) {
                    if (b < steady0 + W + tauMax) continue;
                    const int t = b - W - tauMax;
                    const YinResult r =
                        yinPaperFixedW(xD.data(), n, t, W, tauMin, tauMax,
                                       0.10, static_cast<double>(sr), true);
                    Frame f;
                    f.sample = b;
                    f.est = r.valid ? r.f0 : 0.0;
                    f.gt = gtAt(sig, b - 1);
                    fs.push_back(f);
                }
                const double tB = nowSec();
                // FFT variant on W = T_max only (equivalence + CPU reference)
                if (wi == 0) {
                    for (int b = cad; b <= n; b += cad * stride) {
                        if (b < steady0 + W + tauMax) continue;
                        const int t = b - W - tauMax;
                        // direct FFT-difference + canonical pipeline
                        std::vector<double> d, dFull, cmFull;
                        diffFixedWFft(xD.data(), t, W, 1, tauMax, sq, dFull);
                        cmndfCanonical(dFull, cmFull);
                        YinResult r;
                        r.valid = false;
                        double e0 = sq[static_cast<size_t>(t + W)] -
                                    sq[static_cast<size_t>(t)];
                        if (e0 > 0.0) {
                            const int iMin = tauMin - 1, iMax = tauMax - 1;
                            int idx = selectFirstLocalMinBelow(
                                cmFull, iMin, iMax, 0.10, r.threshold_accepted);
                            r.fallback_global = !r.threshold_accepted;
                            r.tau_int = idx + 1;
                            r.cmndf_min = cmFull[static_cast<size_t>(idx)];
                            const double off =
                                (idx >= 1 && idx + 1 < static_cast<int>(dFull.size()))
                                    ? parabolicOffset(dFull.data(), idx) : 0.0;
                            r.tau = r.tau_int + off;
                            r.f0 = r.tau > 0.0 ? static_cast<double>(sr) / r.tau : 0.0;
                            r.valid = r.f0 > 0.0;
                        }
                        Frame f;
                        f.sample = b;
                        f.est = r.valid ? r.f0 : 0.0;
                        f.gt = gtAt(sig, b - 1);
                        fsF.push_back(f);
                    }
                }
                const double upd = double(fs.size() > 0 ? fs.size() : 1);
                const Metrics m = metricsFrom(fs, sig.tonal, sig.steady,
                                              (tB - tA) * 1e9 / upd);
                char js[640];
                std::snprintf(js, sizeof(js),
                    "{\"signal\": \"%s\", \"rate\": %d, \"variant\": \"%s\", "
                    "\"frames\": %d, \"valid_ratio\": %.4f, \"med_c\": %.3f, "
                    "\"p95_c\": %.3f, \"gross\": %.4f, \"octave\": %.4f, "
                    "\"jitter_c\": %.3f, \"cpu\": %.1f}",
                    kKindNames[ki], sr, wNames[wi], m.frames, m.valid_ratio(),
                    m.med_c, m.p95_c, m.gross, m.octv, m.jitter_c, m.cpu_ns);
                if (!first) json += ",\n";
                first = false;
                json += js;
                std::printf("%-14s %6d %-11s v %5.2f%% med %8.3fc p95 %8.3fc "
                            "gr %5.3f oct %5.3f jit %7.3f cpu %8.1f\n",
                            kKindNames[ki], sr, wNames[wi],
                            100.0 * m.valid_ratio(), m.med_c, m.p95_c, m.gross,
                            m.octv, m.jitter_c, m.cpu_ns);
                if (wi == 0 && !fsF.empty()) {
                    const Metrics mf = metricsFrom(fsF, sig.tonal, sig.steady, 0.0);
                    char js2[640];
                    std::snprintf(js2, sizeof(js2),
                        "{\"signal\": \"%s\", \"rate\": %d, \"variant\": \"fftW1\", "
                        "\"frames\": %d, \"valid_ratio\": %.4f, \"med_c\": %.3f, "
                        "\"p95_c\": %.3f, \"gross\": %.4f, \"octave\": %.4f, "
                        "\"jitter_c\": %.3f, \"cpu\": 0}",
                        kKindNames[ki], sr, mf.frames, mf.valid_ratio(),
                        mf.med_c, mf.p95_c, mf.gross, mf.octv, mf.jitter_c);
                    json += ",\n";
                    json += js2;
                    std::printf("%-14s %6d %-11s v %5.2f%% med %8.3fc p95 %8.3fc "
                                "gr %5.3f oct %5.3f jit %7.3f\n",
                                kKindNames[ki], sr, "fftW1",
                                100.0 * mf.valid_ratio(), mf.med_c, mf.p95_c,
                                mf.gross, mf.octv, mf.jitter_c);
                }
            }
        }
        // reset the corpus RNG deterministically per signal kind
        corpusRng() = Lcg(20261104 + static_cast<uint64_t>(ki) * 7919ULL);
    }
    json += "\n]\n";
    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (!f) { std::fprintf(stderr, "cannot write %s\n", jsonPath); return 1; }
        std::fwrite(json.data(), 1, json.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "json written: %s\n", jsonPath);
    }
    return 0;
}
