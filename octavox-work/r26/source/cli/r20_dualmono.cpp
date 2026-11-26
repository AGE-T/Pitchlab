// OCTAVOX R20 -- deterministic dual-mono / stereo integrity tool (P3).
// ============================================================================
// PROVENANCE: our own tool (round 20). It characterises OUR independent
// harmonizer engine's stereo behaviour on DETERMINISTIC dual-mono inputs
// (L and R byte-identical), the user-reported Haas-like doubling question
// ("stereo input, Wet=100%, Global Shift=0, Voice 1 offset=0 -- a second
// zero-offset voice doubles the effect"), and the sensitivity of the
// per-channel detector architecture to TINY inter-channel input
// differences (the actual Haas mechanism hypothesis: independent
// per-channel trackers lock/commit spacing at different times -> the wet
// voices read at different delays per channel -> inter-aural delay).
//
// MEASUREMENTS per scenario (all 100% deterministic, LCG input):
//   * L/R output null difference: max |L-R| and RMS dB
//   * output cross-correlation peak lag (parabolic-refined, in samples)
//     = the inter-channel delay of the OUTPUT (Haas measure)
//   * mid/side energy ratio (stereo width measure; side RMS / mid RMS, dB)
//   * per-channel voice-0 spacing commit COUNTS (wrap/commit tallies --
//     divergence of the trackers shows up as commit-count differences)
//
// SCENARIOS (each at 48 kHz and 96 kHz, both Lagrange6 and ObsKernel6):
//   [R21 CORRECTION of an inaccurate R20 comment: these scenarios do NOT
//   set the per-voice base delays equal -- they run the engine on the
//   CORE DEFAULT table base_delay = 256 + 32*v samples (v = voice index),
//   i.e. DISTINCT per-voice delays (v0=256, v1=288, ... = 32-sample
//   inter-voice spacing = 0.667 ms @48k / 0.333 ms @96k). "equal" in the
//   scenario names below refers to the IDENTICAL L/R INPUT AND CHANNEL
//   STATE, and (with spread off) the identical per-voice PITCH ratio --
//   the property under test is channel-pair transparency, not inter-voice
//   delay equality. The equal-vs-distinct base-delay axis is measured
//   explicitly by the R21 successor tool r21_stereo.cpp.]
//   T1  dual_mono_1v_equal    L==R, 1 voice, default delays, fb=0,
//                             spread off, wet=1 -> EXPECT bit-identical L/R
//   T2  dual_mono_2v_equal    L==R, 2 voices (identical pitch ratio,
//                             DISTINCT default per-voice delays) -> EXPECT
//                             bit-identical
//   T3  dual_mono_8v_equal    L==R, 8 voices (same law) -> EXPECT
//                             bit-identical
//   T4  amp_0.1pct            L = 1.001 * R -> the divergence probe
//   T5  amp_1pct              L = 1.01  * R
//   T6  one_sample_offset     R delayed by exactly 1 sample
//   T7  true_stereo_decorr    independent L/R noise (the honest stereo case)
//
// The Haas verdict: T1-T3 null bit-exactly (the engine is deterministic
// dual-mono transparent); the T4-T6 rows QUANTIFY how little inter-channel
// difference it takes to decorrelate the wet paths (spacing-commit
// divergence), and the correlation-lag column gives the resulting
// inter-aural delay in samples/ms.
//
// Usage: octavox_r20_dualmono [--json <path>] [--seconds s]
// Exit 0 on success.
// ============================================================================
#include <algorithm>
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

/// The non-unity shift used by the tracker-divergence scenarios
/// (2^(5/12) -- dual-tap voices, spacing commits actually fire).
constexpr double kShiftRatio = 1.3348398541700344;  // 2^(5/12)

struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed ? seed : 1) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(static_cast<int32_t>(uint32_t(s >> 33))) /
               2147483648.0;
    }
};

struct Row {
    const char* scenario;
    int rate;
    const char* interp;
    double maxdiff = 0.0;
    double rmsdiff_db = 0.0;
    double xcorr_lag_samples = 0.0;
    double side_over_mid_db = 0.0;
    long commitsL = 0, commitsR = 0;
    long wrapsL = 0, wrapsR = 0;
    bool bit_identical = false;
};

// Parabolic-refined argmax of the cross-correlation over +-maxLag.
// corr(lag) = sum_{i in [maxLag, n-maxLag)} a[i] * b[i+lag] / norm;
// |lag| <= maxLag keeps every index inside [0, n).
double xcorrPeakLag(const std::vector<float>& a, const std::vector<float>& b,
                    int maxLag) {
    const size_t n = std::min(a.size(), b.size());
    if (n <= static_cast<size_t>(2 * maxLag) + 2) return 0.0;
    double ea = 0.0, eb = 0.0;
    for (size_t i = static_cast<size_t>(maxLag); i < n - static_cast<size_t>(maxLag); ++i) {
        ea += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        eb += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }
    const double denom = std::sqrt(ea * eb);
    if (denom <= 0.0) return 0.0;
    std::vector<double> r(2 * maxLag + 1, 0.0);
    double best = -2.0;
    int bestLag = 0;
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double s = 0.0;
        for (size_t i = static_cast<size_t>(maxLag);
             i < n - static_cast<size_t>(maxLag); ++i)
            s += static_cast<double>(a[i]) *
                 static_cast<double>(b[i + static_cast<size_t>(lag)]);
        r[static_cast<size_t>(lag + maxLag)] = s / denom;
        if (r[static_cast<size_t>(lag + maxLag)] > best) {
            best = r[static_cast<size_t>(lag + maxLag)];
            bestLag = lag;
        }
    }
    if (bestLag > 0 && bestLag < maxLag) {
        const double y0 = r[static_cast<size_t>(bestLag - 1 + maxLag)];
        const double y1 = r[static_cast<size_t>(bestLag + maxLag)];
        const double y2 = r[static_cast<size_t>(bestLag + 1 + maxLag)];
        const double den = y0 - 2.0 * y1 + y2;
        if (std::abs(den) > 1e-15)
            return static_cast<double>(bestLag) + 0.5 * (y0 - y2) / den;
    }
    return static_cast<double>(bestLag);
}

Row runScenario(const char* name, int sr, Interpolation interp, int voices,
                double lGain, int rOffsetSamples, bool independentJitter,
                double seconds, double ratio = 1.0) {
    Row row;
    row.scenario = name;
    row.rate = sr;
    row.interp = interp == Interpolation::ObsKernel6 ? "obskernel6" : "lagrange6";

    const int n = static_cast<int>(seconds * sr);
    std::vector<float> L(n), R(n);
    // HARMONIC input (220 Hz stack, 8 harmonics, 1/k rolloff, +-3 cent
    // per-period jitter, small noise floor): the detector locks and COMMITS
    // spacing on this material -- the Haas mechanism (per-channel tracker
    // divergence) is only observable on pitched input; white noise never
    // fires a commit (verified: commit tallies stay 0 on noise).
    Lcg rngL(13579000 + static_cast<uint64_t>(sr));
    Lcg rngR(24680100 + static_cast<uint64_t>(sr));
    const double dt = 1.0 / sr;
    double phL = 0.0, phR = 0.0;
    double jitL = 0.0, jitLt = 0.0, jitR = 0.0, jitRt = 0.0;
    const double period = 1.0 / 220.0;
    for (int i = 0; i < n; ++i) {
        const double t = i * dt;
        if (std::fmod(t, period) < dt) {
            // ONE rngL draw drives the shared jitter when !independentJitter
            // (the old version drew separate noise for L and R even in the
            // dual-mono case, so the inputs were NOT byte-identical and the
            // null test measured the generator, not the engine).
            jitLt = (rngL.next() * 2.0 - 1.0) * 3.0;
            jitRt = independentJitter ? (rngR.next() * 2.0 - 1.0) * 3.0 : jitLt;
        }
        jitL += (jitLt - jitL) * (dt / period) * 4.0;
        jitR += (jitRt - jitR) * (dt / period) * 4.0;
        const double fL = 220.0 * std::pow(2.0, jitL / 1200.0);
        const double fR = 220.0 * std::pow(2.0, jitR / 1200.0);
        phL += 2.0 * M_PI * fL * dt;
        phR += 2.0 * M_PI * fR * dt;
        double a = 0.0, b = 0.0;
        for (int k = 1; k <= 8; ++k) {
            a += (1.0 / k) * std::sin(k * phL);
            if (independentJitter)
                b += (1.0 / k) * std::sin(k * phR);
            else
                b += (1.0 / k) * std::sin(k * phL);  // same phase stream
        }
        const double nzL = rngL.next();
        const double nzR = independentJitter ? rngR.next() : nzL;  // shared noise
        L[i] = static_cast<float>(0.5 * a / 8.0 + 0.002 * nzL);
        R[i] = static_cast<float>(0.5 * b / 8.0 + 0.002 * nzR);
    }
    if (lGain != 1.0)
        for (int i = 0; i < n; ++i) L[i] = static_cast<float>(L[i] * lGain);
    if (rOffsetSamples > 0) {
        for (int i = n - 1; i >= rOffsetSamples; --i) R[i] = R[i - rOffsetSamples];
        for (int i = 0; i < rOffsetSamples; ++i) R[i] = 0.0f;
    }

    MultichannelShifter eng;
    Config cfg;
    cfg.sample_rate = sr;
    cfg.interpolation = interp;
    cfg.ratio = ratio;  // non-unity -> dual-tap voices -> spacing commits
    cfg.voices = voices;
    cfg.spread = false;   // identical pitch ratio per voice
    cfg.feedback = 0.0;
    cfg.detector_mode = DetectorMode::Decimated;  // the shipping default
    if (!eng.prepare(cfg, 2)) {
        std::fprintf(stderr, "prepare failed\n");
        std::exit(1);
    }
    // [R21 CORRECTION: the core default per-voice base-delay table is
    // 256 + 32*v samples -- DISTINCT per voice (NOT equal; the inaccurate
    // R20 comment here said "equal"). No setVoiceBaseDelay call is made, so
    // these scenarios run the distinct default table; per-voice gains 1.]
    std::vector<float> outL(n, 0.0f), outR(n, 0.0f);
    int done = 0;
    while (done < n) {
        const int blk = std::min(1024, n - done);
        const float* inP[2] = {L.data() + done, R.data() + done};
        float* outP[2] = {outL.data() + done, outR.data() + done};
        eng.process(inP, outP, 2, blk);
        done += blk;
    }

    // stats: skip the first 0.5 s (engagement ramps / detector warm-up)
    const size_t skip = static_cast<size_t>(0.5 * sr);
    double maxd = 0.0, sd = 0.0, mid = 0.0, side = 0.0;
    long cnt = 0;
    bool ident = true;
    for (size_t i = skip; i < outL.size(); ++i) {
        const double a = outL[i], b = outR[i];
        const double d = a - b;
        if (d != 0.0) ident = false;
        maxd = std::max(maxd, std::abs(d));
        sd += d * d;
        mid += 0.25 * (a + b) * (a + b);
        side += 0.25 * (a - b) * (a - b);
        ++cnt;
    }
    const double rms = std::sqrt(sd / std::max(1.0, double(cnt)));
    const double midR = std::sqrt(mid / std::max(1.0, double(cnt)));
    const double sideR = std::sqrt(side / std::max(1.0, double(cnt)));
    row.maxdiff = maxd;
    row.rmsdiff_db = (rms > 0.0 && midR > 0.0)
                         ? 20.0 * std::log10(rms / midR)
                         : (ident ? -999.0 : 0.0);
    row.side_over_mid_db =
        (sideR > 0.0 && midR > 0.0) ? 20.0 * std::log10(sideR / midR) : -999.0;
    row.bit_identical = ident;
    // commit/wrap tallies (per channel, voice 0)
    row.commitsL = eng.voiceStats(0, 0).commits;
    row.commitsR = eng.voiceStats(1, 0).commits;
    row.wrapsL = eng.voiceStats(0, 0).wraps;
    row.wrapsR = eng.voiceStats(1, 0).wraps;
    // inter-channel output delay via cross-correlation (+-1000 samples)
    row.xcorr_lag_samples = xcorrPeakLag(outL, outR, 1000);
    return row;
}

// The USER-REPORT doubling probe (R20-P3): Global Shift = 0, per-voice
// offsets = 0 (the reported configuration). Renders the SAME dual-mono
// harmonic input twice through a 2-voice engine -- once with voice 0's
// gain 1 / voice 1's gain 0, once the opposite -- and cross-correlates the
// two outputs: the peak lag is the SECOND voice's delay relative to the
// first (the core's default per-voice base-delay law 256 + 32*v samples
// => ~32 samples = ~0.67 ms at 48 kHz -- squarely in the Haas fusion
// window). This isolates the inter-VOICE temporal doubling, the actual
// mechanism behind "a second zero-offset voice doubles the effect".
double userReportDoublingLag(int sr, Interpolation interp, double seconds) {
    const int n = static_cast<int>(seconds * sr);
    std::vector<float> L(n);
    Lcg rng(555000 + static_cast<uint64_t>(sr));
    const double dt = 1.0 / sr;
    double ph = 0.0, jit = 0.0, jitT = 0.0;
    const double period = 1.0 / 220.0;
    for (int i = 0; i < n; ++i) {
        const double t = i * dt;
        if (std::fmod(t, period) < dt) jitT = (rng.next() * 2.0 - 1.0) * 3.0;
        jit += (jitT - jit) * (dt / period) * 4.0;
        ph += 2.0 * M_PI * 220.0 * std::pow(2.0, jit / 1200.0) * dt;
        double a = 0.0;
        for (int k = 1; k <= 8; ++k) a += (1.0 / k) * std::sin(k * ph);
        L[i] = static_cast<float>(0.5 * a / 8.0 + 0.002 * rng.next());
    }
    auto render = [&](int activeVoice) {
        MultichannelShifter eng;
        Config cfg;
        cfg.sample_rate = sr;
        cfg.interpolation = interp;
        cfg.ratio = 1.0;      // Global Shift = 0 (the reported setting)
        cfg.voices = 2;       // BOTH voices allocated (identical norm law)
        cfg.spread = false;   // both voices' pitch ratio == 1 (offset 0)
        cfg.feedback = 0.0;
        cfg.detector_mode = DetectorMode::Decimated;
        if (!eng.prepare(cfg, 2)) std::exit(1);
        eng.setVoiceGain(0, activeVoice == 0 ? 1.0 : 0.0);
        eng.setVoiceGain(1, activeVoice == 1 ? 1.0 : 0.0);
        std::vector<float> o(n, 0.0f);
        int done = 0;
        while (done < n) {
            const int blk = std::min(1024, n - done);
            const float* inP[2] = {L.data() + done, L.data() + done};
            float* o1 = o.data() + done, *o2 = o.data() + done;
            float* outP[2] = {o1, o2};
            eng.process(inP, outP, 2, blk);
            done += blk;
        }
        return o;
    };
    const std::vector<float> a = render(0), b = render(1);
    return xcorrPeakLag(a, b, 4096);
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

    std::vector<Row> rows;
    for (int sr : {48000, 96000}) {
        for (Interpolation interp :
             {Interpolation::Lagrange6, Interpolation::ObsKernel6}) {
            rows.push_back(runScenario("dual_mono_1v_equal", sr, interp, 1, 1.0, 0,
                                       false, seconds, kShiftRatio));
            rows.push_back(runScenario("dual_mono_2v_equal", sr, interp, 2, 1.0, 0,
                                       false, seconds, kShiftRatio));
            rows.push_back(runScenario("dual_mono_8v_equal", sr, interp, 8, 1.0, 0,
                                       false, seconds, kShiftRatio));
            rows.push_back(runScenario("amp_0.1pct", sr, interp, 2, 1.001, 0,
                                       false, seconds, kShiftRatio));
            rows.push_back(runScenario("amp_1pct", sr, interp, 2, 1.01, 0,
                                       false, seconds, kShiftRatio));
            rows.push_back(runScenario("one_sample_offset", sr, interp, 2, 1.0, 1,
                                       false, seconds, kShiftRatio));
            rows.push_back(runScenario("true_stereo_decorr", sr, interp, 2, 1.0, 0,
                                       true, seconds, kShiftRatio));
        }
    }

    // the USER-REPORT doubling probe (unity ratio, both zero offsets):
    // the inter-voice temporal echo = the default base-delay law
    std::printf("\nUSER-REPORT DOUBLING PROBE (Global Shift=0, offsets=0, "
                "2 voices, dual-mono input)\n");
    std::printf("%6s %-10s %14s %14s\n", "RATE", "INTERP",
                "ECHO LAG(smpl)", "ECHO LAG(ms)");
    std::vector<double> doubling;
    for (int sr : {48000, 96000}) {
        for (Interpolation interp :
             {Interpolation::Lagrange6, Interpolation::ObsKernel6}) {
            const double lag = userReportDoublingLag(sr, interp, seconds);
            doubling.push_back(lag);
            std::printf("%6d %-10s %14.3f %14.4f\n", sr,
                        interp == Interpolation::ObsKernel6 ? "obskernel6"
                                                            : "lagrange6",
                        lag, lag * 1000.0 / sr);
        }
    }

    std::printf("%-20s %6s %-10s %10s %10s %12s %10s %8s %8s %5s\n",
                "SCENARIO", "RATE", "INTERP", "MAX|L-R|", "RMS dB",
                "LAG(smpl)", "SIDE/MID", "CMTS L", "CMTS R", "BIT=");
    std::string sep(110, '-');
    std::printf("%s\n", sep.c_str());
    std::string json = "[\n";
    bool first = true;
    for (const auto& r : rows) {
        std::printf("%-20s %6d %-10s %10.6f %10.2f %12.3f %10.2f %8ld %8ld %5s\n",
                    r.scenario, r.rate, r.interp, r.maxdiff, r.rmsdiff_db,
                    r.xcorr_lag_samples, r.side_over_mid_db, r.commitsL,
                    r.commitsR, r.bit_identical ? "yes" : "no");
        if (!first) json += ",\n";
        first = false;
        json += "  {\"scenario\": \"" + std::string(r.scenario) +
                "\", \"rate\": " + std::to_string(r.rate) +
                ", \"interp\": \"" + std::string(r.interp) +
                "\", \"maxdiff\": " + std::to_string(r.maxdiff) +
                ", \"rmsdiff_db\": " + std::to_string(r.rmsdiff_db) +
                ", \"xcorr_lag_samples\": " + std::to_string(r.xcorr_lag_samples) +
                ", \"side_over_mid_db\": " + std::to_string(r.side_over_mid_db) +
                ", \"commitsL\": " + std::to_string(r.commitsL) +
                ", \"commitsR\": " + std::to_string(r.commitsR) +
                ", \"wrapsL\": " + std::to_string(r.wrapsL) +
                ", \"wrapsR\": " + std::to_string(r.wrapsR) +
                ", \"bit_identical\": " + (r.bit_identical ? "true" : "false") + "}";
    }
    json += "\n]\n";
    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (f) {
            std::fwrite(json.data(), 1, json.size(), f);
            std::fclose(f);
            std::fprintf(stderr, "json written: %s\n", jsonPath);
        }
    }
    return 0;
}
