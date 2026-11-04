// PITCHLAB R22 -- the low-frequency end-to-end battery (PRIORITY 1).
// ============================================================================
// PROVENANCE: our own tool (round 22). R21 made the DETECTOR's minimum
// frequency configurable (Config::f0_min) and measured DETECTION quality
// per candidate floor; R22 traces the COMPLETE signal path (the 8 stages
// in docs/R22_MINIMUM_PITCH_PATH.md) and gates the COHERENT policy:
//
//     spacing_fmin = max(min(cfg.fmin, cfg.f0_min), sr/(1.5*grain))
//
// so that lowering f0_min makes the whole ENGINE follow, down to the
// measured SPACING SAFETY FLOOR sr/(1.5*grain) (a period longer than
// 1.5 grains cannot be represented by the grain law -- the S clamp would
// break A/B periodicity). The DETECTABLE-vs-SHIFTABLE split is counted by
// PitchPathStats (spacing_rejected = detected but not safely shiftable).
//
// BATTERY (all at 48 kHz unless noted):
//   tones: the ten specified points
//     32.703 (C1), 36.708 (F1), 41.203 (E1), 49.000 (G1), 55.000 (A1),
//     60.000,     65.406 (C2), 73.416 (D2), 82.407 (E2), 110.000 (A2)
//   input: 6 s harmonic stack (8 harmonics, +-2 cent slow jitter, noise
//     floor) -- the same generator family as r21_fmin (SYNTHETIC,
//     honestly labelled; the real-recording category is r21_realaudio's).
//   f0_min = 0.98 * tone (the floor sits just below the tone: the tone is
//     detectable by construction, exercising the ENGINE's follow-down).
//   modes: Decimated (default), Hybrid, FullRate -- identical settings.
//   intervals (semitones): +12, +7, 0 (unity control), -7, -12, -24
//     (octave down; "where mathematically supported" is REPORTED, not
//     assumed: the output frequency f0*2^(st/12) is measured and its
//     distance from the 20..1200 Hz operating band is recorded).
//
// PER RUN measured:
//   * estimate_hz_median (median of the last 1 s of lastEstimateHz polls)
//   * estimate_cents_err (vs the known synthetic F0 -- this corpus HAS a
//     ground truth by construction, unlike real recordings)
//   * pitch_path detected / spacing_accepted / spacing_rejected (the
//     detectable-vs-shiftable split; octave-error estimates are the small
//     accepted remainder -- see the findings for the measured rates)
//   * spacing lock: locked_S vs expected k*period (k = round(G/t)) and
//     spacing_rel_err; locked = spacing left the grain default
//   * output pitch (autocorrelation + parabolic refine on the last 1 s)
//     and output_cents_err vs f0*2^(st/12)
//   * max_step after the 2 s settling window (transition-click proxy)
//   * rms_out, all_finite
//   * cpu_ns_per_sample (steady-state)
//
// SPECIAL LEGS (the detectable-but-NOT-shiftable demonstration):
//   * 192 kHz, f0_min = 20, tone 25 Hz, fmax open: the safety floor is
//     192000/6144 = 31.25 Hz -- the 25 Hz majority is spacing_rejected;
//     the accepted remainder are >= 31.25 Hz octave errors (legitimately
//     shiftable). With fmax = 31.25 (EMPTY accept range) NOTHING is
//     accepted and the spacing never leaves the grain default.
//   * 48 kHz, f0_min = 20, tone 22 Hz: the extreme floor end-to-end (at
//     48 kHz the safety floor 7.8125 Hz is far below -- every detected
//     estimate is also shiftable).
//   * LONG RENDER: 30 s, 8 voices, spread ON, feedback 0.25, -12 st at
//     41.203 Hz, f0_min = 32.703 -- bounded + finite (stability).
//
// Usage: octavox_r22_lowfreq [--json <path>] [--seconds s]
// Exit 0 on success (all runs finite; the JSON carries the measurements).
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

// Harmonic stack at a static F0 with mild human jitter + noise floor (the
// r21_fmin generator family; SYNTHETIC corpus -- labelled as such).
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

// Autocorrelation pitch estimate with parabolic refinement, searched in a
// window around the EXPECTED period (independent measurement of the
// OUTPUT pitch -- never the engine's own detector).
double acPitchHz(const float* x, int n, int sr, double fExpected) {
    if (fExpected <= 0.0 || n < 256) return 0.0;
    const int tLo = std::max(2, static_cast<int>(0.85 * sr / fExpected));
    const int tHi = std::min(n / 2, static_cast<int>(1.18 * sr / fExpected));
    if (tHi <= tLo) return 0.0;
    int best = -1;
    double bestV = 0.0;
    for (int tau = tLo; tau <= tHi; ++tau) {
        double s = 0.0;
        for (int i = 0; i + tau < n; ++i) s += x[i] * x[i + tau];
        if (s > bestV) { bestV = s; best = tau; }
    }
    if (best < 0) return 0.0;
    // parabolic refine on the normalised AC (neighbours)
    auto ac = [&](int tau) {
        double s = 0.0;
        for (int i = 0; i + tau < n; ++i) s += x[i] * x[i + tau];
        return s;
    };
    const double ym = (best > tLo) ? ac(best - 1) : bestV;
    const double y0 = bestV;
    const double yp = (best < tHi) ? ac(best + 1) : bestV;
    const double den = ym - 2.0 * y0 + yp;
    double d = 0.0;
    if (den < 0.0) d = 0.5 * (ym - yp) / den;
    if (d > 0.5) d = 0.5;
    if (d < -0.5) d = -0.5;
    return static_cast<double>(sr) / (best + d);
}

bool allFinite(const float* x, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(x[i])) return false;
    return true;
}

double medianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const int n = static_cast<int>(v.size());
    return (n % 2 == 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double maxStep(const float* x, int n, int i0) {
    double m = 0.0;
    for (int i = std::max(1, i0 + 1); i < n; ++i)
        m = std::max(m, std::abs(static_cast<double>(x[i]) - x[i - 1]));
    return m;
}

double rmsTail(const float* x, int n, int tail) {
    const int i0 = std::max(0, n - tail);
    double s = 0.0;
    for (int i = i0; i < n; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / std::max(1, n - i0));
}

struct RunResult {
    // filled per run
    double f0 = 0.0, f0min = 0.0, st = 0.0;
    int sr = 48000;
    const char* mode = "";
    double estHz = 0.0, estCents = 0.0;
    long detected = 0, accepted = 0, rejected = 0;
    double lockedS = 0.0, expectedS = 0.0, spacingRelErr = 0.0;
    bool locked = false;
    double outHz = 0.0, outCents = 0.0;
    double maxStepV = 0.0, rmsOut = 0.0;
    bool finite = true;
    double cpuNs = 0.0;
};

RunResult runCase(int sr, double f0, double f0min, double st,
                  octavox::DetectorMode mode, double seconds,
                  const std::vector<float>& x, bool spread = false,
                  int voices = 1, double feedback = 0.0) {
    using octavox::AlignedDelayLine;
    using octavox::Config;
    RunResult r;
    r.f0 = f0; r.f0min = f0min; r.st = st; r.sr = sr; r.mode = modeName(mode);
    const int n = static_cast<int>(x.size());
    Config cfg;
    cfg.sample_rate = sr;
    cfg.f0_min = f0min;
    cfg.ratio = std::pow(2.0, st / 12.0);
    cfg.spread = spread;
    cfg.feedback = feedback;
    std::vector<float> y(n, 0.0f);
    AlignedDelayLine e;
    if (!e.prepare(cfg)) { r.finite = false; return r; }
    if (voices != 1) e.setVoices(voices);
    std::vector<double> ests;
    const int tail = std::min(n, sr);  // last 1 s
    const double t0 = nowSec();
    int done = 0;
    while (done < n) {
        const int b = std::min(4096, n - done);
        e.process(x.data() + done, y.data() + done, b);
        done += b;
        if (done > n - tail) ests.push_back(e.lastEstimateHz());
    }
    r.cpuNs = (nowSec() - t0) * 1.0e9 / std::max(1, n);
    r.estHz = medianOf(ests);
    r.estCents = (r.estHz > 0.0) ? cents(r.estHz, f0) : 999.0;
    const octavox::PitchPathStats ps = e.pitchPathStats();
    r.detected = ps.detected; r.accepted = ps.spacing_accepted;
    r.rejected = ps.spacing_rejected;
    const octavox::VoiceStats& v = e.voiceStats(0);
    r.lockedS = v.spacing;
    const double t = sr / f0;
    const double k = std::nearbyint(cfg.grain / t);
    r.expectedS = k * t;
    r.spacingRelErr = (r.lockedS > 0.0) ? std::abs(r.lockedS / r.expectedS - 1.0) : 1.0;
    r.locked = (r.lockedS != cfg.grain) || (r.expectedS == cfg.grain);
    const double fOutExp = f0 * std::pow(2.0, st / 12.0);
    r.outHz = acPitchHz(y.data() + (n - tail), tail, sr, fOutExp);
    r.outCents = (r.outHz > 0.0) ? cents(r.outHz, fOutExp) : 999.0;
    const int settle = std::min(n, 2 * sr);
    r.maxStepV = maxStep(y.data(), n, settle);
    r.rmsOut = rmsTail(y.data(), n, tail);
    r.finite = allFinite(y.data(), n);
    return r;
}

void emitRun(std::string& json, bool& first, const RunResult& r) {
    if (!first) json += ",\n";
    first = false;
    char b[1024];
    std::snprintf(b, sizeof b,
        "  {\"sr\": %d, \"f0\": %.3f, \"f0_min\": %.3f, \"st\": %.1f, "
        "\"mode\": \"%s\", \"est_hz\": %.4f, \"est_cents\": %.3f, "
        "\"detected\": %ld, \"spacing_accepted\": %ld, "
        "\"spacing_rejected\": %ld, \"locked_S\": %.3f, "
        "\"expected_S\": %.3f, \"spacing_rel_err\": %.6f, \"locked\": %s, "
        "\"out_hz\": %.4f, \"out_cents\": %.3f, \"max_step\": %.6f, "
        "\"rms_out\": %.6f, \"finite\": %s, \"cpu_ns_per_sample\": %.2f}",
        r.sr, r.f0, r.f0min, r.st, r.mode, r.estHz, r.estCents,
        r.detected, r.accepted, r.rejected, r.lockedS, r.expectedS,
        r.spacingRelErr, r.locked ? "true" : "false", r.outHz, r.outCents,
        r.maxStepV, r.rmsOut, r.finite ? "true" : "false", r.cpuNs);
    json += b;
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

    using octavox::DetectorMode;
    const double tones[10] = {32.703, 36.708, 41.203, 49.000, 55.000,
                              60.000, 65.406, 73.416, 82.407, 110.000};
    const double ivs[6] = {12.0, 7.0, 0.0, -7.0, -12.0, -24.0};
    const DetectorMode modes[3] = {DetectorMode::Decimated, DetectorMode::Hybrid,
                                   DetectorMode::FullRate};
    const int sr = 48000;

    std::string json = "[\n";
    bool first = true;
    int runs = 0, nonFinite = 0;

    std::printf("R22 low-frequency end-to-end battery (synthetic corpus, "
                "labelled; 48 kHz)\n");
    for (int ti = 0; ti < 10; ++ti) {
        const double f0 = tones[ti];
        const double f0min = 0.98 * f0;
        std::vector<float> x = synthTone(f0, sr, seconds,
                                         9100000ULL + uint64_t(ti));
        for (int mi = 0; mi < 3; ++mi) {
            for (int ii = 0; ii < 6; ++ii) {
                RunResult r = runCase(sr, f0, f0min, ivs[ii], modes[mi],
                                      seconds, x);
                emitRun(json, first, r);
                ++runs;
                if (!r.finite) ++nonFinite;
                std::printf("  f0=%7.3f f0min=%7.3f st=%+5.1f %-10s "
                            "est=%8.3fHz(%+7.2fc) det=%ld acc=%ld rej=%ld "
                            "S=%7.1f/%7.1f out=%8.3fHz(%+7.2fc) "
                            "step=%.4f cpu=%.1fns\n",
                            f0, f0min, ivs[ii], r.mode, r.estHz, r.estCents,
                            r.detected, r.accepted, r.rejected, r.lockedS,
                            r.expectedS, r.outHz, r.outCents, r.maxStepV,
                            r.cpuNs);
            }
        }
    }

    // -- special leg A: 192 kHz detectable-but-not-shiftable ---------------
    {
        const int sr2 = 192000;
        const double f0 = 25.0, f0min = 20.0;
        std::vector<float> x = synthTone(f0, sr2, 3.0, 777ULL);
        // open gate: the 25 Hz majority rejected (safety floor 31.25)
        RunResult r = runCase(sr2, f0, f0min, -12.0, DetectorMode::Hybrid,
                              3.0, x);
        emitRun(json, first, r);
        ++runs;
        if (!r.finite) ++nonFinite;
        std::printf("  [192k not-shiftable, open gate] est=%8.3fHz det=%ld "
                    "acc=%ld rej=%ld locked=%s\n",
                    r.estHz, r.detected, r.accepted, r.rejected,
                    r.locked ? "true" : "false");
        // empty accept range (fmax = floor): NOTHING accepted, no lock
        // (direct engine run with fmax pinned)
        octavox::Config cfg;
        cfg.sample_rate = sr2;
        cfg.f0_min = 20.0;
        cfg.fmin = 20.0;   // validity; effective floor is the safety 31.25
        cfg.fmax = 31.25;  // EMPTY accept range
        cfg.ratio = std::pow(2.0, -12.0 / 12.0);
        octavox::AlignedDelayLine e;
        if (e.prepare(cfg)) {
            std::vector<float> y(x.size(), 0.0f);
            int done = 0;
            while (done < (int)x.size()) {
                const int b = std::min(4096, (int)x.size() - done);
                e.process(x.data() + done, y.data() + done, b);
                done += b;
            }
            const octavox::PitchPathStats ps = e.pitchPathStats();
            char b[512];
            std::snprintf(b, sizeof b,
                "  {\"leg\": \"192k_empty_gate\", \"sr\": %d, \"f0\": %.3f, "
                "\"f0_min\": %.3f, \"st\": -12.0, \"mode\": \"hybrid\", "
                "\"est_hz\": %.4f, \"detected\": %ld, "
                "\"spacing_accepted\": %ld, \"spacing_rejected\": %ld, "
                "\"locked_S\": %.3f, \"locked\": %s, \"finite\": %s}",
                sr2, f0, f0min, e.lastEstimateHz(), ps.detected,
                ps.spacing_accepted, ps.spacing_rejected,
                e.voiceStats(0).spacing,
                e.voiceStats(0).spacing == cfg.grain ? "false" : "true",
                allFinite(y.data(), (int)y.size()) ? "true" : "false");
            if (!first) json += ",\n";
            first = false;
            ++runs;
            std::printf("  [192k empty gate] det=%ld acc=%ld rej=%ld "
                        "S==grain:%s\n",
                        ps.detected, ps.spacing_accepted,
                        ps.spacing_rejected,
                        e.voiceStats(0).spacing == cfg.grain ? "yes" : "NO");
        }
    }

    // -- special leg B: 48 kHz extreme floor end-to-end --------------------
    {
        const double f0 = 22.0, f0min = 20.0;
        std::vector<float> x = synthTone(f0, sr, seconds, 778ULL);
        for (int mi = 0; mi < 3; ++mi) {
            RunResult r = runCase(sr, f0, f0min, -12.0, modes[mi], seconds, x);
            emitRun(json, first, r);
            ++runs;
            if (!r.finite) ++nonFinite;
            std::printf("  [48k extreme 22 Hz] %-10s est=%8.3fHz det=%ld "
                        "acc=%ld rej=%ld out=%8.3fHz(%+7.2fc)\n",
                        r.mode, r.estHz, r.detected, r.accepted, r.rejected,
                        r.outHz, r.outCents);
        }
    }

    // -- special leg C: long render stability -------------------------------
    {
        const double f0 = 41.203, f0min = 32.703;
        std::vector<float> x = synthTone(f0, sr, 30.0, 779ULL);
        RunResult r = runCase(sr, f0, f0min, -12.0, DetectorMode::Decimated,
                              30.0, x, /*spread=*/true, /*voices=*/8,
                              /*feedback=*/0.25);
        emitRun(json, first, r);
        ++runs;
        if (!r.finite) ++nonFinite;
        std::printf("  [long render 30 s, 8v, spread, fb=0.25] finite=%s "
                    "rms=%.4f max_step=%.4f cpu=%.1fns\n",
                    r.finite ? "yes" : "NO", r.rmsOut, r.maxStepV, r.cpuNs);
    }

    json += "\n]\n";

    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (f) {
            std::fwrite(json.data(), 1, json.size(), f);
            std::fclose(f);
        } else {
            std::fprintf(stderr, "cannot write %s\n", jsonPath);
            return 2;
        }
    }
    std::printf("runs=%d non_finite=%d -> %s\n", runs, nonFinite,
                nonFinite == 0 ? "OK" : "NON-FINITE OUTPUTS PRESENT");
    return nonFinite == 0 ? 0 : 1;
}
