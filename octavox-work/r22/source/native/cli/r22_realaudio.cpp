// OCTAVOX/PITCHLAB R22 -- real-audio detector comparison harness v2.
// ============================================================================
// PROVENANCE: our own tool (round 22 extends round 21). It measures OUR
// independent harmonizer engine's three pitch-detector architectures
// (FullRate / Decimated / Hybrid) on material supplied by the user as WAV
// files. THE CATEGORY IS EXPLICIT AND ENFORCED (R22):
//   * --category synthetic   generated corpora (never presented as proof
//                            of real-recording quality; the R20 detcompare
//                            corpora remain the synthetic reference)
//   * --category recorded    genuine recordings, NO ground truth claimed
//   * --category annotated   genuine recordings WITH a user-supplied
//                            reference track (--annotate REQUIRED);
//                            cent numbers are then TRUE errors
// Personal recordings are NEVER embedded in the research archive without
// explicit approval (this tool only reads paths the operator passes).
//
// HONESTY CONTRACT (unchanged from R21, stated sharper):
//   * For `recorded` material there is no known-F0 law: "cent error" is
//     measured AGAINST THE FULLRATE MODE as the comparison REFERENCE (the
//     bit-exact reference detector) -- an inter-mode AGREEMENT metric.
//   * For `annotated` material the metrics use the supplied spans as the
//     reference trajectory (manual annotation or an external tracker; the
//     tool never claims ground-truth accuracy it cannot verify).
//
// R22 ADDITIONS over the R21 harness:
//   * smoothed trajectory capture (the harness mirrors the engine's
//     period-history law: median of the last 8 valid estimates) +
//     conversion timestamps (>= 50-cent changes of the smoothed track)
//   * FINAL-PROCESSED-AUDIO comparison: the three modes' output WAVs are
//     compared directly (RMS + max sample diff vs the FullRate output) --
//     the comparison is of the PROCESSED AUDIO, not just the estimates
//   * --wavdir writes the final processed WAV per mode (operator-side
//     auditioning; the archive embeds no personal recordings)
//   * peak RSS (VmHWM) per file recorded alongside CPU
//
// Per input x mode, measured and recorded (identical audio + settings):
//   (the complete R21 metric set -- see below -- plus the additions)
//
// Per input x mode, measured and recorded (identical audio + settings):
//   * raw estimate trajectory (every 64-sample control boundary),
//   * median + max |cent difference| vs the FullRate reference mode
//     (or vs annotations when given),
//   * octave-disagreement ratio (|diff| > 600 cents vs reference),
//   * dropout ratio (estimate == 0 while the input is voiced-ish:
//     RMS window above the file's 20th-percentile envelope),
//   * jitter (median |cent delta| between consecutive estimates),
//   * transient recovery (after the file's top-N input transients: ms
//     until the mode's estimate re-stabilises -- |delta cents| < 25 over
//     a 50 ms window),
//   * tracking latency (mean re-lock delay after each estimate dropout,
//     measured relative to the reference mode's own re-lock),
//   * CPU (ns per input sample, steady state),
//   * output discontinuity scan (max sample-to-sample step of the wet
//     output),
//   * estimate count + file metadata (rate, channels, seconds, category).
//
// Usage:
//   octavox_r22_realaudio --input <a.wav> [--input <b.wav> ...]
//                          [--list <file-with-paths>]
//                          [--annotate <csv>] [--json <path>]
//                          [--trajdir <dir>] [--wavdir <dir>]
//                          [--category synthetic|recorded|annotated]
//                          [--ratio <semitones>] [--maxseconds <s>]
//
// Mono analysis channel: channel 0 of the file (documented; a downmix can
// distort pitch on anti-phase material). Exit 0 on success.
// ============================================================================
#include "octavox/dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../tests/wav_io.hpp"

namespace {

double nowSec() {
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
}

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

struct RunMetrics {
    double median_diff_cents = 0.0;
    double max_diff_cents = 0.0;
    double octave_disagree_ratio = 0.0;
    double dropout_ratio = 0.0;
    double jitter_cents = 0.0;
    double transient_recovery_ms = 0.0;
    double rel_lock_delay_ms = 0.0;  // vs the reference mode
    double cpu_ns_per_sample = 0.0;
    double max_output_step = 0.0;
    long n_estimates = 0;
};

// A user annotation span (optional ground truth).
struct Span {
    double t0, t1, f0;
};

// Render one mode over the input; capture per-boundary raw estimates and
// the wet output.
void renderMode(const std::vector<float>& in, int sr, double ratio,
                octavox::DetectorMode mode, std::vector<double>& est,
                std::vector<float>& y) {
    using octavox::AlignedDelayLine;
    using octavox::Config;
    AlignedDelayLine eng;
    Config cfg;
    cfg.sample_rate = sr;
    cfg.ratio = ratio;
    cfg.detector_mode = mode;
    if (!eng.prepare(cfg)) {
        std::fprintf(stderr, "prepare failed\n");
        std::exit(1);
    }
    const int cadence = 64;
    const int nCtrl = static_cast<int>(in.size()) / cadence;
    est.assign(nCtrl, 0.0);
    y.assign(in.size(), 0.0f);
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
            ++k;
        }
    }
    while (k < nCtrl) {
        est[k] = eng.lastEstimateHz();
        ++k;
    }
}

// Peak resident set size in MiB (VmHWM from /proc/self/status; Linux).
double peakRssMiB() {
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return 0.0;
    char line[256];
    double v = 0.0;
    while (std::fgets(line, sizeof line, f)) {
        if (std::sscanf(line, "VmHWM: %lf kB", &v) == 1) { v /= 1024.0; break; }
    }
    std::fclose(f);
    return v;
}

// The engine's period-history law mirrored in the harness: the smoothed
// estimate = median of the last <= 8 VALID estimates (the same window the
// tracker's f0_history uses). Returns the smoothed track + the count and
// first-timestamp of >= 50-cent conversions.
void smoothTrajectory(const std::vector<double>& est, int sr, int cadence,
                      std::vector<double>& smooth, long& conversions,
                      double& firstConvSec) {
    smooth.assign(est.size(), 0.0);
    conversions = 0;
    firstConvSec = -1.0;
    std::vector<double> hist;
    double prev = 0.0;
    for (size_t q = 0; q < est.size(); ++q) {
        if (est[q] > 0.0) {
            hist.push_back(est[q]);
            if (hist.size() > 8) hist.erase(hist.begin());
        }
        if (hist.empty()) { smooth[q] = 0.0; prev = 0.0; continue; }
        std::vector<double> h = hist;
        std::sort(h.begin(), h.end());
        const size_t n = h.size();
        smooth[q] = (n % 2 == 1) ? h[n / 2] : 0.5 * (h[n / 2 - 1] + h[n / 2]);
        if (prev > 0.0 && smooth[q] > 0.0) {
            if (std::abs(1200.0 * std::log2(smooth[q] / prev)) >= 50.0) {
                ++conversions;
                if (firstConvSec < 0.0)
                    firstConvSec = (q * cadence + 0.5) / sr;
            }
        }
        prev = smooth[q];
    }
}

// Voiced-ish mask from a 10 ms RMS envelope vs the file's 20th percentile.
std::vector<char> voicedMask(const std::vector<float>& in, int sr) {
    const int win = std::max(1, static_cast<int>(0.01 * sr));
    const int nB = static_cast<int>(in.size()) / win;
    std::vector<double> env(nB, 0.0);
    for (int b = 0; b < nB; ++b) {
        double s = 0.0;
        for (int i = b * win; i < (b + 1) * win && i < static_cast<int>(in.size()); ++i)
            s += double(in[i]) * double(in[i]);
        env[b] = std::sqrt(s / win);
    }
    std::vector<double> sorted(env);
    std::sort(sorted.begin(), sorted.end());
    const double thr = sorted[sorted.size() / 5];  // 20th percentile
    const int cadence = 64;
    const int nCtrl = static_cast<int>(in.size()) / cadence;
    std::vector<char> mask(nCtrl, 0);
    for (int k = 0; k < nCtrl; ++k) {
        const int b = (k * cadence) / win;
        if (b < nB) mask[k] = (env[b] > thr && env[b] > 1e-4) ? 1 : 0;
    }
    return mask;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> inputs;
    const char* listPath = nullptr;
    const char* annPath = nullptr;
    const char* jsonPath = nullptr;
    const char* trajDir = nullptr;
    const char* wavDir = nullptr;
    const char* category = "recorded";
    double ratioSemi = 5.0;
    double maxSeconds = 30.0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--input") && i + 1 < argc)
            inputs.push_back(argv[++i]);
        else if (!std::strcmp(argv[i], "--list") && i + 1 < argc)
            listPath = argv[++i];
        else if (!std::strcmp(argv[i], "--annotate") && i + 1 < argc)
            annPath = argv[++i];
        else if (!std::strcmp(argv[i], "--json") && i + 1 < argc)
            jsonPath = argv[++i];
        else if (!std::strcmp(argv[i], "--trajdir") && i + 1 < argc)
            trajDir = argv[++i];
        else if (!std::strcmp(argv[i], "--wavdir") && i + 1 < argc)
            wavDir = argv[++i];
        else if (!std::strcmp(argv[i], "--category") && i + 1 < argc)
            category = argv[++i];
        else if (!std::strcmp(argv[i], "--ratio") && i + 1 < argc)
            ratioSemi = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--maxseconds") && i + 1 < argc)
            maxSeconds = std::atof(argv[++i]);
        else {
            std::fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 2;
        }
    }
    // R22: the category is EXPLICIT and ENFORCED.
    const bool catSynthetic = !std::strcmp(category, "synthetic");
    const bool catRecorded = !std::strcmp(category, "recorded");
    const bool catAnnotated = !std::strcmp(category, "annotated");
    if (!catSynthetic && !catRecorded && !catAnnotated) {
        std::fprintf(stderr,
                     "invalid category '%s' (must be synthetic | recorded | "
                     "annotated)\n",
                     category);
        return 2;
    }
    if (catAnnotated && !annPath) {
        std::fprintf(stderr,
                     "category 'annotated' requires --annotate <csv>\n");
        return 2;
    }
    if (annPath && !catAnnotated) {
        std::fprintf(stderr,
                     "--annotate given but category is '%s' (use "
                     "--category annotated)\n",
                     category);
        return 2;
    }
    if (listPath) {
        std::FILE* f = std::fopen(listPath, "r");
        if (!f) {
            std::fprintf(stderr, "cannot open list %s\n", listPath);
            return 2;
        }
        char line[1024];
        while (std::fgets(line, sizeof(line), f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
                s.pop_back();
            if (!s.empty() && s[0] != '#') inputs.push_back(s);
        }
        std::fclose(f);
    }
    if (inputs.empty()) {
        std::fprintf(stderr, "no inputs (use --input <wav> or --list <file>)\n");
        return 2;
    }

    // optional annotations (global spans applied per file by time)
    std::vector<Span> spans;
    if (annPath) {
        std::FILE* f = std::fopen(annPath, "r");
        if (f) {
            char line[1024];
            while (std::fgets(line, sizeof(line), f)) {
                double t0, t1, hz;
                if (std::sscanf(line, "%lf,%lf,%lf", &t0, &t1, &hz) == 3 &&
                    t1 > t0 && hz > 0.0)
                    spans.push_back({t0, t1, hz});
            }
            std::fclose(f);
        }
    }

    using octavox::DetectorMode;
    const DetectorMode modes[3] = {DetectorMode::FullRate, DetectorMode::Decimated,
                                   DetectorMode::Hybrid};
    const double ratio = std::pow(2.0, ratioSemi / 12.0);

    std::string json = "[\n";
    bool firstRec = true;

    for (const std::string& path : inputs) {
        int sr = 0, ch = 0;
        std::vector<float> interleaved;
        try {
            interleaved = octavox::wav::readWav(path, sr, ch);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "SKIP %s: %s\n", path.c_str(), e.what());
            continue;
        }
        // channel 0 (documented mono analysis channel)
        size_t nframes = interleaved.size() / std::max(1, ch);
        if (nframes > static_cast<size_t>(maxSeconds * sr))
            nframes = static_cast<size_t>(maxSeconds * sr);
        std::vector<float> in(nframes);
        for (size_t i = 0; i < nframes; ++i) in[i] = interleaved[i * ch];
        if (nframes < static_cast<size_t>(2.0 * sr)) {
            std::fprintf(stderr, "SKIP %s: shorter than 2 s\n", path.c_str());
            continue;
        }
        const std::string base = path.substr(path.find_last_of("/\\") + 1);

        std::vector<std::vector<double>> ests(3);
        std::vector<std::vector<float>> outs(3);
        for (int m = 0; m < 3; ++m)
            renderMode(in, sr, ratio, modes[m], ests[m], outs[m]);

        // -- R22 additions -------------------------------------------------
        // (a) smoothed trajectories (the engine's median-of-8 law mirrored)
        //     + conversion counts / first-conversion timestamps
        std::vector<std::vector<double>> smooths(3);
        long convs[3] = {0, 0, 0};
        double firstConv[3] = {-1.0, -1.0, -1.0};
        for (int m = 0; m < 3; ++m)
            smoothTrajectory(ests[m], sr, 64, smooths[m], convs[m],
                             firstConv[m]);
        // (b) FINAL-PROCESSED-AUDIO cross-mode comparison: each mode's wet
        //     output vs the FullRate output (identical settings; the
        //     estimate comparison above is supplemented by the AUDIO the
        //     modes actually produce)
        double outRmsDiff[3] = {0.0, 0.0, 0.0}, outMaxDiff[3] = {0.0, 0.0, 0.0};
        {
            const size_t i0 = static_cast<size_t>(1.2 * sr);
            double accSq[3] = {0.0, 0.0, 0.0};
            size_t cnt = 0;
            for (size_t q = i0; q < outs[0].size(); ++q) {
                ++cnt;
                for (int m = 0; m < 3; ++m) {
                    const double d = std::abs(double(outs[m][q]) -
                                              double(outs[0][q]));
                    accSq[m] += d * d;
                    if (d > outMaxDiff[m]) outMaxDiff[m] = d;
                }
            }
            for (int m = 0; m < 3; ++m)
                outRmsDiff[m] = cnt ? std::sqrt(accSq[m] / cnt) : 0.0;
        }
        // (c) peak RSS snapshot (VmHWM; the number covers the whole process
        //     at this point -- recorded per FILE, honest bookkeeping)
        const double rssMiB = peakRssMiB();
        // (d) final processed WAVs (operator-side auditioning; the archive
        //     itself embeds no personal recordings)
        if (wavDir) {
            for (int m = 0; m < 3; ++m) {
                std::string w = std::string(wavDir) + "/" + base + "_" +
                                modeName(modes[m]) + ".wav";
                try {
                    octavox::wav::writeWav(w, outs[m].data(),
                                           static_cast<int>(outs[m].size()), 1,
                                           sr);
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "wav write failed %s: %s\n",
                                 w.c_str(), e.what());
                }
            }
        }

        const int cadence = 64;
        const int nCtrl = static_cast<int>(ests[0].size());
        const std::vector<char> voiced = voicedMask(in, sr);
        const int kWarm = static_cast<int>(1.2 * sr / cadence);

        // transient list: top-8 input sample-to-sample jumps after warm-up
        std::vector<int> transients;
        {
            std::vector<std::pair<double, int>> steps;
            for (int i = static_cast<int>(1.2 * sr) + 1;
                 i < static_cast<int>(in.size()) - 1; ++i)
                steps.push_back({std::abs(double(in[i]) - double(in[i - 1])), i});
            std::sort(steps.begin(), steps.end(),
                      [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
                          return a.first > b.first;
                      });
            const size_t want = std::min<size_t>(8, steps.size());
            for (size_t q = 0; q < want; ++q) transients.push_back(steps[q].second);
            std::sort(transients.begin(), transients.end());
        }

        for (int m = 0; m < 3; ++m) {
            const bool isRef = (m == 0);
            const std::vector<double>& est = ests[m];
            const std::vector<double>& ref = ests[0];

            RunMetrics r;
            std::vector<double> diffs;
            long voicedN = 0, drop = 0, octDis = 0;
            for (int q = kWarm; q < nCtrl; ++q) {
                if (!voiced[q]) continue;
                ++voicedN;
                if (est[q] <= 0.0) {
                    ++drop;
                    continue;
                }
                if (ref[q] <= 0.0) continue;  // reference dark: no diff point
                double d;
                if (!spans.empty()) {
                    // TRUE error vs annotations (first matching span)
                    const double t = (q * cadence + 0.5) / sr;
                    const Span* hit = nullptr;
                    for (const Span& s : spans)
                        if (t >= s.t0 && t <= s.t1) { hit = &s; break; }
                    if (!hit) continue;
                    d = std::abs(cents(est[q], hit->f0));
                } else {
                    d = std::abs(cents(est[q], ref[q]));
                }
                diffs.push_back(d);
                if (d > 600.0) ++octDis;
            }
            if (!diffs.empty()) {
                std::sort(diffs.begin(), diffs.end());
                r.median_diff_cents = diffs[diffs.size() / 2];
                r.max_diff_cents = diffs.back();
            }
            r.n_estimates = static_cast<long>(diffs.size()) + drop;
            r.octave_disagree_ratio = voicedN ? double(octDis) / double(voicedN) : 0.0;
            r.dropout_ratio = voicedN ? double(drop) / double(voicedN) : 0.0;
            // jitter
            {
                std::vector<double> jd;
                for (int q = kWarm + 1; q < nCtrl; ++q) {
                    if (!voiced[q] || !voiced[q - 1]) continue;
                    if (est[q] <= 0.0 || est[q - 1] <= 0.0) continue;
                    jd.push_back(std::abs(cents(est[q], est[q - 1])));
                }
                if (!jd.empty()) {
                    std::sort(jd.begin(), jd.end());
                    r.jitter_cents = jd[jd.size() / 2];
                }
            }
            // transient recovery
            {
                std::vector<double> rec;
                for (int tSample : transients) {
                    int q0 = tSample / cadence;
                    const int qEnd = std::min(nCtrl, q0 + static_cast<int>(0.4 * sr / cadence));
                    int q = q0;
                    for (; q < qEnd; ++q) {
                        if (est[q] <= 0.0) continue;
                        const int qA = q + 1;
                        const int qB = std::min(nCtrl - 1, q + static_cast<int>(0.05 * sr / cadence));
                        if (qB <= qA) break;
                        bool stable = true;
                        for (int w = qA; w <= qB; ++w) {
                            if (est[w] <= 0.0 ||
                                std::abs(cents(est[w], est[w - 1])) >= 25.0) {
                                stable = false;
                                break;
                            }
                        }
                        if (stable) break;
                    }
                    rec.push_back((q - q0) * cadence * 1000.0 / sr);
                }
                if (!rec.empty()) {
                    double s = 0.0;
                    for (double v : rec) s += v;
                    r.transient_recovery_ms = s / rec.size();
                }
            }
            // relative lock delay vs the reference after each shared dropout
            if (!isRef) {
                std::vector<double> lags;
                for (int q = kWarm + 1; q < nCtrl; ++q) {
                    if (ref[q] > 0.0 || est[q] > 0.0) continue;  // not both dark
                    auto relock = [&](const std::vector<double>& e) {
                        int p = q;
                        const int pEnd = q + static_cast<int>(0.5 * sr / cadence);
                        while (p < pEnd && p < nCtrl && e[p] <= 0.0) ++p;
                        return p;
                    };
                    const int pr = relock(ref);
                    const int pe = relock(est);
                    lags.push_back((pe - pr) * cadence * 1000.0 / sr);
                    q = std::max(pr, pe);
                }
                if (!lags.empty()) {
                    double s = 0.0;
                    for (double v : lags) s += v;
                    r.rel_lock_delay_ms = s / lags.size();
                }
            }
            // CPU (timed re-render, engine warm)
            {
                std::vector<double> est2;
                std::vector<float> y2;
                const double tA = nowSec();
                renderMode(in, sr, ratio, modes[m], est2, y2);
                const double tB = nowSec();
                r.cpu_ns_per_sample = (tB - tA) * 1e9 / in.size();
            }
            // output discontinuity scan
            {
                double mx = 0.0;
                for (size_t q = static_cast<size_t>(1.2 * sr);
                     q + 1 < outs[m].size(); ++q)
                    mx = std::max(mx, std::abs(double(outs[m][q]) - double(outs[m][q - 1])));
                r.max_output_step = mx;
            }

            char line[256];
            std::snprintf(line, sizeof(line),
                          "%-28s %-9s %6d  med %8.3f  max %9.3f  jit %7.3f  "
                          "oct %5.2f%%  drop %5.2f%%  rec %6.1fms  lockd %6.1fms  "
                          "cpu %8.1f ns/s  step %6.4f",
                          base.c_str(), modeName(modes[m]), sr, r.median_diff_cents,
                          r.max_diff_cents, r.jitter_cents,
                          100.0 * r.octave_disagree_ratio, 100.0 * r.dropout_ratio,
                          r.transient_recovery_ms, r.rel_lock_delay_ms,
                          r.cpu_ns_per_sample, r.max_output_step);
            std::printf("%s\n", line);

            if (!firstRec) json += ",\n";
            firstRec = false;
            json += "  {\"file\": \"" + base + "\", \"path\": \"" + path +
                    "\", \"category\": \"" + std::string(category) +
                    "\", \"mode\": \"" + modeName(modes[m]) +
                    "\", \"rate\": " + std::to_string(sr) +
                    ", \"channels\": " + std::to_string(ch) +
                    ", \"seconds\": " + std::to_string(double(nframes) / sr) +
                    ", \"reference_mode\": \"" +
                    (spans.empty() ? std::string("fullrate") : std::string("annotation")) +
                    "\", \"median_diff_cents\": " + std::to_string(r.median_diff_cents) +
                    ", \"max_diff_cents\": " + std::to_string(r.max_diff_cents) +
                    ", \"jitter_cents\": " + std::to_string(r.jitter_cents) +
                    ", \"octave_disagree_ratio\": " + std::to_string(r.octave_disagree_ratio) +
                    ", \"dropout_ratio\": " + std::to_string(r.dropout_ratio) +
                    ", \"transient_recovery_ms\": " + std::to_string(r.transient_recovery_ms) +
                    ", \"rel_lock_delay_ms\": " + std::to_string(r.rel_lock_delay_ms) +
                    ", \"cpu_ns_per_sample\": " + std::to_string(r.cpu_ns_per_sample) +
                    ", \"max_output_step\": " + std::to_string(r.max_output_step) +
                    ", \"n_estimates\": " + std::to_string(r.n_estimates) +
                    // R22 additions
                    ", \"smoothed_conversions\": " + std::to_string(convs[m]) +
                    ", \"first_conversion_s\": " + std::to_string(firstConv[m]) +
                    ", \"final_audio_rms_diff_vs_fullrate\": " + std::to_string(outRmsDiff[m]) +
                    ", \"final_audio_max_diff_vs_fullrate\": " + std::to_string(outMaxDiff[m]) +
                    ", \"peak_rss_mib\": " + std::to_string(rssMiB) + "}";

            if (trajDir) {
                std::string p = std::string(trajDir) + "/traj_" + base + "_" +
                                modeName(modes[m]) + ".csv";
                std::FILE* f = std::fopen(p.c_str(), "w");
                if (f) {
                    std::fprintf(f,
                                 "t_ms,est_hz,smoothed_hz,ref_fullrate_hz\n");
                    const int dec = std::max(1, static_cast<int>(0.01 * sr) / cadence);
                    for (int q = kWarm; q < nCtrl; q += dec)
                        std::fprintf(f, "%.1f,%.4f,%.4f,%.4f\n",
                                     q * cadence * 1000.0 / sr, est[q],
                                     smooths[m][q], ref[q]);
                    std::fclose(f);
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
