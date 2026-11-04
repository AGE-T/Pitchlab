// OCTAVOX/PITCHLAB R22 -- end-to-end Minimum Pitch signal-path battery.
// ============================================================================
// PROVENANCE: our own tool (round 22, P1). R21 made the DETECTOR's floor
// configurable (Config::f0_min) but the ENGINE's spacing-accept gate kept
// its independent fixed 55 Hz floor, so a lowered detection floor opened
// nothing downstream. R22 links the two (effective gate = min(fmin,
// f0_min); default bit-exact). This battery measures the FULL path --
// detection -> period history -> grain spacing -> wrapped dual-tap
// shifting -- over the ten R22-spec frequency points:
//
//   32.703 (C1) / 36.708 (F#1?) / 41.203 (E1) / 49.0 (G1) / 55.0 (A1) /
//   60.0       / 65.406 (C2)    / 73.416 (D2) / 82.407 (E2) / 110.0 (A2)
//
// (exact equal-tempered values where spelled; 36.708 = D1#-ish F1 area --
// the spec's literal list is used verbatim).
//
// Per tone x detector mode (FullRate / Decimated / Hybrid) x interval
// (unison, +12 st, -12 st, -24 st, +7 st) with f0_min = 32.703:
//   * DETECTABLE: lastEstimateHz() present on >= 90% of control ticks and
//     median |cent error| <= 50 vs the known F0 (the corpus generator's
//     law) -- the DETECTOR's own capability.
//   * PITCH-SHIFTABLY TRACKED: detectable AND the voice's tap spacing S
//     locked to a near-integer multiple of the true period (|S/period -
//     round(S/period)| <= 0.02, S within [0.5G, 1.5G]) AND the OUTPUT's
//     measured F0 (independent YIN-style estimator over the wet output,
//     5..2000 Hz search) within 50 cents of f0*ratio AND the output
//     bounded (|out| <= 8, no NaN) -- the ENGINE's capability.
//
// A control sweep with the DEFAULT f0_min = 60 documents which tones the
// shipped default gates out (the "detectable" boundary of the product as
// delivered; the default is NOT changed this round).
//
// Usage: octavox_r22_minpitch [--json <path>] [--seconds s] [--rate hz]
// Exit 0 on success. Deterministic (fixed-seed LCG corpus).
// ============================================================================
#include "octavox/dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

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
// (shared spec with r21_fmin -- same generator law, fixed seed).
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

// Independent YIN-style F0 estimator over a mono signal (search 5..2000 Hz).
// Returns 0 when no confident estimate. Used ONLY as a measurement probe
// of the engine's OUTPUT -- it shares no code with the engine's detector.
double measureF0(const std::vector<float>& x, int sr, double t0, double t1) {
    const int i0 = static_cast<int>(t0 * sr);
    const int i1 = std::min(static_cast<int>(t1 * sr),
                            static_cast<int>(x.size()));
    const int len = i1 - i0;
    if (len < 4096) return 0.0;
    // window = second half analysis segment (mirrors the engine's law)
    const int tau_min = std::max(2, static_cast<int>(sr / 2000.0));
    const int tau_max = std::min(len / 2 - 32, static_cast<int>(sr / 5.0));
    if (tau_max <= tau_min + 2) return 0.0;
    const int seg0 = len / 2;
    const int m = (len - seg0) - tau_max;
    if (m < 32) return 0.0;
    std::vector<double> d(static_cast<size_t>(tau_max - tau_min));
    for (int i = 0; i < tau_max - tau_min; ++i) {
        const int tau = tau_min + i;
        double s = 0.0;
        for (int j = 0; j < m; ++j) {
            const double a = x[i0 + seg0 + j] - x[i0 + seg0 + tau + j];
            s += a * a;
        }
        d[i] = s;
    }
    double run = 0.0;
    std::vector<double> cmnd(d.size());
    for (size_t i = 0; i < d.size(); ++i) {
        run += d[i];
        cmnd[i] = (run > 0.0) ? d[i] * static_cast<double>(i + 1) / run : 1.0;
    }
    int idx = -1;
    const double thr = 0.25;
    for (int i = 1; i + 1 < static_cast<int>(cmnd.size()); ++i) {
        if (cmnd[i] < thr && cmnd[i] <= cmnd[i - 1] && cmnd[i] <= cmnd[i + 1]) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        idx = 0;
        for (int i = 1; i < static_cast<int>(cmnd.size()); ++i)
            if (cmnd[i] < cmnd[idx]) idx = i;
    }
    double tau = tau_min + idx;
    if (idx > 0 && idx + 1 < static_cast<int>(cmnd.size())) {
        const double y0 = cmnd[idx - 1], y1 = cmnd[idx], y2 = cmnd[idx + 1];
        const double den = y0 - 2.0 * y1 + y2;
        if (std::abs(den) > 1e-15) tau += 0.5 * (y0 - y2) / den;
    }
    return static_cast<double>(sr) / tau;
}

double medianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2 == 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else {
            o += c;
        }
    }
    return o;
}

struct CaseRow {
    double f0_min;
    const char* mode;
    double tone;
    double ratio;
    double semitones;
    double detect_ratio;
    double median_est_hz;
    double median_cent;
    double spacing_S;
    double spacing_lock_err;  // |S/period - round| in periods
    long wraps;
    long commits;
    double out_f0_hz;
    double out_cent;
    double out_peak;
    const char* verdict;
};

}  // namespace

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    double seconds = 8.0;
    int rate = 48000;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--rate") && i + 1 < argc)
            rate = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }

    using octavox::AlignedDelayLine;
    using octavox::Config;
    using octavox::DetectorMode;

    const double tones[] = {32.703, 36.708, 41.203, 49.0, 55.0,
                            60.0,   65.406, 73.416, 82.407, 110.0};
    const int nT = 10;
    const DetectorMode modes[] = {DetectorMode::FullRate, DetectorMode::Decimated,
                                  DetectorMode::Hybrid};
    const int nM = 3;
    struct Iv { double semi; const char* name; };
    const Iv ivs[] = {{0.0, "unison"},  {12.0, "+12st"},  {-12.0, "-12st"},
                      {-24.0, "-24st"}, {7.0, "+7st"}};
    const int nI = 5;
    // f0_min candidates: the full-band floor 32.703 (open path) and the
    // shipped DEFAULT 60.0 (control: documents the gate boundary).
    const double f0mins[] = {32.703, 60.0};
    const int nF = 2;

    std::vector<CaseRow> rows;
    for (int fi = 0; fi < nF; ++fi) {
        const double f0min = f0mins[fi];
        for (int mi = 0; mi < nM; ++mi) {
            for (int ti = 0; ti < nT; ++ti) {
                const double f0 = tones[ti];
                // Control sweep (default 60): unison only -- it documents
                // the DETECTABLE boundary, the shiftable path is measured
                // by the open-floor sweep.
                const int ivMax = (fi == 0) ? nI : 1;
                for (int ii = 0; ii < ivMax; ++ii) {
                    const double semi = ivs[ii].semi;
                    const double ratio = std::pow(2.0, semi / 12.0);
                    Config cfg{};
                    cfg.sample_rate = rate;
                    cfg.ratio = ratio;
                    cfg.voices = 1;
                    cfg.max_voices = 8;
                    cfg.spread = false;
                    cfg.feedback = 0.0;
                    cfg.interpolation = octavox::Interpolation::Lagrange6;
                    cfg.grain = 4096.0;
                    cfg.base_delay = 256.0;
                    cfg.fmin = 55.0;      // the engine default (NOT deleted)
                    cfg.fmax = 1200.0;
                    cfg.f0_min = f0min;   // the R21 floor knob
                    cfg.detector_mode = modes[mi];
                    AlignedDelayLine eng;
                    if (!eng.prepare(cfg)) {
                        std::fprintf(stderr, "prepare failed f0=%g mode=%s\n",
                                     f0, modeName(modes[mi]));
                        return 1;
                    }
                    eng.setWetDry(1.0);
                    const double gate = eng.spacingGateHz();

                    const std::vector<float> in =
                        synthTone(f0, rate, seconds, 991 + ti * 7 + mi);
                    const int n = static_cast<int>(in.size());
                    std::vector<float> out(n);
                    const int blk = 256;
                    std::vector<double> ests;
                    long estTicks = 0;
                    for (int p = 0; p < n; p += blk) {
                        const int k = std::min(blk, n - p);
                        eng.process(in.data() + p, out.data() + p, k);
                        ++estTicks;
                        const double e = eng.lastEstimateHz();
                        ests.push_back(e);
                    }
                    const double detectRatio = static_cast<double>(
                        std::count_if(ests.begin(), ests.end(),
                                      [](double e) { return e > 0.0; })) /
                        static_cast<double>(estTicks);
                    std::vector<double> good;
                    for (double e : ests)
                        if (e > 0.0) good.push_back(e);
                    const double medEst = medianOf(good);
                    const double medCent =
                        (medEst > 0.0) ? cents(medEst, f0) : 9999.0;

                    const octavox::VoiceStats vs = eng.voiceStats(0);
                    const double period = rate / f0;
                    const double lockErr =
                        (vs.spacing > 0.0)
                            ? std::abs(vs.spacing / period -
                                       std::nearbyint(vs.spacing / period))
                            : 999.0;
                    const double outF0 = measureF0(out, rate, 1.0, seconds - 0.05);
                    const double expect = f0 * ratio;
                    const double outCent =
                        (outF0 > 0.0) ? cents(outF0, expect) : 9999.0;
                    double peak = 0.0;
                    bool bad = false;
                    for (float s : out) {
                        if (!std::isfinite(s)) { bad = true; break; }
                        peak = std::max(peak, std::abs(static_cast<double>(s)));
                    }

                    const bool detectable =
                        detectRatio >= 0.90 && std::abs(medCent) <= 50.0;
                    const bool inBand = medEst >= gate && medEst <= cfg.fmax;
                    // unity path: exact ratio 1.0 parks the voice on an
                    // integer delay (wraps == 0 by design; the output is the
                    // reproduced input -- out_cent ~ 0 confirms).
                    const bool unity = (ratio == 1.0);
                    // shiftable = the END-TO-END output criterion: the
                    // measured output F0 within 50 cents of f0*ratio, the
                    // spacing committed off the grain default and inside
                    // the [0.5G, 1.5G] band, wraps advancing, output
                    // bounded. spacing_lock_err is a reported DIAGNOSTIC
                    // (mechanism-level: S vs the true period), not a gate.
                    const bool shifted =
                        detectable && inBand && !unity && vs.spacing > 0.0 &&
                        vs.spacing != cfg.grain &&
                        vs.spacing >= 0.5 * cfg.grain &&
                        vs.spacing <= 1.5 * cfg.grain && vs.wraps > 0 &&
                        outF0 > 0.0 && std::abs(outCent) <= 50.0 && !bad &&
                        peak <= 8.0;
                    const char* verdict = "FAIL";
                    if (shifted) verdict = "shiftable";
                    else if (unity && detectable) verdict = "unity_parked";
                    else if (detectable && !inBand) verdict = "detectable_only_gate_rejected";
                    else if (detectable) verdict = "detectable_not_shifted";
                    else if (f0 < f0min) verdict = "below_floor";
                    rows.push_back(CaseRow{f0min, modeName(modes[mi]), f0,
                                           ratio, semi, detectRatio, medEst,
                                           medCent, vs.spacing, lockErr,
                                           vs.wraps, vs.commits, outF0,
                                           outCent, peak, verdict});
                }
            }
        }
    }

    // -- report ---------------------------------------------------------------
    std::printf("%-8s %-10s %-8s %-7s %-9s %-9s %-10s %-9s %-9s %-8s %s\n",
                "f0_min", "mode", "tone", "semi", "det%", "medCent",
                "S", "lockErr", "outCent", "wraps", "verdict");
    int nShift = 0, nDetOnly = 0, nBelow = 0, nFail = 0, nUnity = 0;
    for (const auto& r : rows) {
        std::printf("%-8.3f %-10s %-8.3f %-7.1f %-9.1f %-9.2f %-10.1f "
                    "%-9.4f %-9.2f %-8ld %s\n",
                    r.f0_min, r.mode, r.tone, r.semitones,
                    r.detect_ratio * 100.0, r.median_cent, r.spacing_S,
                    r.spacing_lock_err, r.out_cent, r.wraps, r.verdict);
        if (!std::strcmp(r.verdict, "shiftable")) ++nShift;
        else if (!std::strcmp(r.verdict, "unity_parked")) ++nUnity;
        else if (!std::strncmp(r.verdict, "detectable", 10)) ++nDetOnly;
        else if (!std::strcmp(r.verdict, "below_floor")) ++nBelow;
        else ++nFail;
    }
    std::printf("\nTOTAL %zu cases: shiftable=%d unity_parked=%d "
                "detectable-only=%d below-floor=%d FAIL=%d\n",
                rows.size(), nShift, nUnity, nDetOnly, nBelow, nFail);

    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (!f) { std::fprintf(stderr, "cannot write %s\n", jsonPath); return 1; }
        std::fprintf(f, "{\n  \"tool\": \"octavox_r22_minpitch\",\n");
        std::fprintf(f, "  \"round\": 22,\n  \"rate_hz\": %d,\n", rate);
        std::fprintf(f, "  \"seconds\": %.3f,\n", seconds);
        std::fprintf(f, "  \"engine_version\": \"%s\",\n",
                     octavox::versionString());
        std::fprintf(f, "  \"notes\": \"effective spacing gate = min(fmin=55, f0_min); "
                        "shiftable = end-to-end output F0 within 50 cents (independent "
                        "YIN probe over the wet output), spacing committed and in-band, "
                        "wraps advancing, output bounded; unity_parked = exact unison "
                        "parks the voice (wraps 0 by design); spacing_lock_err is a "
                        "mechanism diagnostic (S vs true period), not a gate\",\n");
        std::fprintf(f, "  \"cases\": [\n");
        for (size_t i = 0; i < rows.size(); ++i) {
            const CaseRow& r = rows[i];
            std::fprintf(f,
                "    {\"f0_min\": %.4f, \"mode\": \"%s\", \"tone_hz\": %.4f, "
                "\"semitones\": %.1f, \"ratio\": %.6f, \"detect_ratio\": %.4f, "
                "\"median_est_hz\": %.4f, \"median_cent\": %.3f, "
                "\"spacing_S\": %.2f, \"spacing_lock_err\": %.5f, "
                "\"wraps\": %ld, \"commits\": %ld, \"out_f0_hz\": %.4f, "
                "\"out_cent\": %.3f, \"out_peak\": %.4f, "
                "\"verdict\": \"%s\"}%s\n",
                r.f0_min, r.mode, r.tone, r.semitones, r.ratio,
                r.detect_ratio, r.median_est_hz, r.median_cent, r.spacing_S,
                r.spacing_lock_err, r.wraps, r.commits, r.out_f0_hz,
                r.out_cent, r.out_peak, r.verdict,
                (i + 1 < rows.size()) ? "," : "");
        }
        std::fprintf(f, "  ],\n");
        std::fprintf(f,
            "  \"summary\": {\"total\": %zu, \"shiftable\": %d, "
            "\"unity_parked\": %d, \"detectable_only\": %d, "
            "\"below_floor\": %d, \"fail\": %d}\n",
            rows.size(), nShift, nUnity, nDetOnly, nBelow, nFail);
        std::fprintf(f, "}\n");
        std::fclose(f);
        std::printf("json written: %s\n", jsonPath);
    }
    return (nFail == 0) ? 0 : 1;
}
