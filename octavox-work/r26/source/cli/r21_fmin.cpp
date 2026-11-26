// OCTAVOX R21 -- configurable minimum detection frequency (Low Note)
// investigation battery.
// ============================================================================
// PROVENANCE: our own tool (round 21). The independent detector's lower
// validity gate / lag-domain floor became a Config field this round
// (Config::f0_min, default 60.0 Hz = the R12-R20 fixed law BIT-EXACTLY;
// see dsp.hpp). This battery measures the effect of the candidate floor
// values on BOTH detection quality and cost, at both canonical rates and
// all three detector modes, on a LOW-TONE corpus that spans the
// candidates' neighbourhoods:
//
//   tones (static F0, harmonic stacks, 6 s each):
//     27.5 (A0 -- far below every candidate; the always-reject probe)
//     32.7 (C1), 36.7 (F1), 41.2 (E1), 49.0 (G1), 55.0 (A1),
//     58.3 (Bb1, just under the default 60), 62.0 (below-60 edge),
//     65.4 (C2, just above the default 60), 73.4 (D2), 82.4 (E2),
//     98.0 (G2), 110.0 (A2), 220.0 (A3, in-range control)
//
// Per (f0_min candidate in {32.7, 41.2, 55, 60, 80, 110}) x tone x mode x
// rate, measured:
//   * detection success ratio (estimate present and |cent error| <= 50)
//   * median + max |cent error| (the corpus has a KNOWN F0 law)
//   * octave-error ratio (|err| > 600 cents)
//   * CPU (ns per input sample; includes the window-growth cost of the
//     lower floor: tau_max = rate/f0_min grows the analysis window)
//   * engine-side spacing acceptance (the estimate ALSO passing the
//     engine's [fmin=55, fmax=1200] spacing gate -- recorded per tone:
//     a floor below 55 only reaches the SPACING path when fmin is lowered
//     too; the relationship is documented, not silently changed)
//
// VERDICT BLOCK: the tool's JSON feeds the findings document's trade-off
// table (which floor first detects which tone). THE DEFAULT IS NOT
// CHANGED by this evidence alone: the original's Low Note static
// semantics (R21 static findings, item 5) and the accuracy results here
// are BOTH recorded; the default 60.0 Hz stays until a separately
// justified change.
//
// Usage: octavox_r21_fmin [--json <path>] [--seconds s]
// Exit 0 on success.
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

// Harmonic stack at a static F0 with mild human jitter + noise floor.
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

}  // namespace

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    double seconds = 6.0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = std::atof(argv[++i]);
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }

    using octavox::AlignedDelayLine;
    using octavox::Config;
    using octavox::DetectorMode;

    const double f0Mins[] = {32.7, 41.2, 55.0, 60.0, 80.0, 110.0};
    const int nF = 6;
    const double tones[] = {27.5, 32.7, 36.7, 41.2, 49.0, 55.0,
                            58.3, 62.0, 65.4, 73.4, 82.4, 98.0, 110.0, 220.0};
    const int nT = 14;
    const DetectorMode modes[3] = {DetectorMode::Decimated, DetectorMode::Hybrid,
                                   DetectorMode::FullRate};
    const int rates[2] = {48000, 96000};

    std::string json = "[\n";
    bool firstRec = true;

    for (int sr : rates) {
        std::vector<std::vector<float>> toneSig(nT);
        for (int t = 0; t < nT; ++t)
            toneSig[t] = synthTone(tones[t], sr, seconds,
                                   4200000ULL + uint64_t(sr) + uint64_t(t));

        for (int fi = 0; fi < nF; ++fi) {
            const double f0min = f0Mins[fi];
            for (int mi = 0; mi < 3; ++mi) {
                const DetectorMode mode = modes[mi];
                AlignedDelayLine eng;
                Config cfg;
                cfg.sample_rate = sr;
                cfg.ratio = std::pow(2.0, 5.0 / 12.0);
                cfg.detector_mode = mode;
                cfg.f0_min = f0min;
                if (!eng.prepare(cfg)) {
                    std::fprintf(stderr, "prepare failed (f0_min=%.1f)\n", f0min);
                    return 1;
                }

                for (int t = 0; t < nT; ++t) {
                    const std::vector<float>& in = toneSig[t];
                    const int cadence = 64;
                    const int nCtrl = static_cast<int>(in.size()) / cadence;
                    std::vector<double> est(nCtrl, 0.0);
                    std::vector<float> y(in.size(), 0.0f);
                    eng.reset();
                    int done = 0, k = 0;
                    while (done < static_cast<int>(in.size())) {
                        const int want = (k < nCtrl) ? (k + 1) * cadence
                                                     : static_cast<int>(in.size());
                        int blk = want - done;
                        if (blk <= 0)
                            blk = std::min(1024, static_cast<int>(in.size()) - done);
                        if (blk <= 0) break;
                        eng.process(in.data() + done, y.data() + done, blk);
                        done += blk;
                        if (k < nCtrl && (k + 1) * cadence <= done) {
                            est[k] = eng.lastEstimateHz();
                            ++k;
                        }
                    }
                    while (k < nCtrl) { est[k] = eng.lastEstimateHz(); ++k; }

                    const int kWarm = static_cast<int>(1.5 * sr / cadence);
                    long voiced = 0, hit = 0, oct = 0;
                    std::vector<double> errs;
                    for (int q = kWarm; q < nCtrl; ++q) {
                        ++voiced;
                        if (est[q] <= 0.0) continue;
                        const double e = std::abs(cents(est[q], tones[t]));
                        errs.push_back(e);
                        if (e <= 50.0) ++hit;
                        if (e > 600.0) ++oct;
                    }
                    double med = 0.0, mx = 0.0;
                    if (!errs.empty()) {
                        std::sort(errs.begin(), errs.end());
                        med = errs[errs.size() / 2];
                        mx = errs.back();
                    }
                    const double success = voiced ? double(hit) / double(voiced) : 0.0;
                    const double octR = voiced ? double(oct) / double(voiced) : 0.0;
                    const bool spacingOk = (tones[t] >= 55.0 && tones[t] <= 1200.0);

                    double cpu = 0.0;
                    {
                        eng.reset();
                        const double tA = nowSec();
                        int d = 0;
                        while (d < static_cast<int>(in.size())) {
                            const int blk =
                                std::min(1024, static_cast<int>(in.size()) - d);
                            eng.process(in.data() + d, y.data() + d, blk);
                            d += blk;
                        }
                        const double tB = nowSec();
                        cpu = (tB - tA) * 1e9 / in.size();
                    }

                    char line[200];
                    std::snprintf(line, sizeof(line),
                                  "f0min %6.1f  %-9s %6d  tone %6.1f Hz  "
                                  "ok %5.1f%%  med %8.2f  max %9.2f  oct %5.1f%%  "
                                  "cpu %7.1f ns/s",
                                  f0min, modeName(mode), sr, tones[t],
                                  100.0 * success, med, mx, 100.0 * octR, cpu);
                    std::printf("%s\n", line);

                    if (!firstRec) json += ",\n";
                    firstRec = false;
                    json += "  {\"f0_min\": " + std::to_string(f0min) +
                            ", \"mode\": \"" + modeName(mode) +
                            "\", \"rate\": " + std::to_string(sr) +
                            ", \"tone_hz\": " + std::to_string(tones[t]) +
                            ", \"success_ratio\": " + std::to_string(success) +
                            ", \"median_err_cents\": " + std::to_string(med) +
                            ", \"max_err_cents\": " + std::to_string(mx) +
                            ", \"octave_err_ratio\": " + std::to_string(octR) +
                            ", \"cpu_ns_per_sample\": " + std::to_string(cpu) +
                            ", \"spacing_gate_pass\": " +
                            (spacingOk ? "true" : "false") + "}";
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
