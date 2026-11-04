// OCTAVOX R21 -- deterministic stereo / voice-delay / harmony-map matrix.
// ============================================================================
// PROVENANCE: our own tool (round 21). It extends the R20 dual-mono study
// (r20_dualmono.cpp -- whose INACCURATE "equal default base delays" comment
// is corrected in place this round) into the full R21-specified matrix, and
// it DISTINGUISHES four different behaviours that the R20 report collapsed:
//
//   (1) EXACT DUAL-MONO TRANSPARENCY: identical L/R input + identical
//       per-channel state -> bit-identical L/R output (channel isolation
//       is structural: one engine per channel; NO hidden widening, and
//       none is added).
//   (2) SAME-PITCH VOICES, DIFFERENT BASE DELAYS: with Spread OFF and all
//       offsets 0 every voice sounds the SAME PITCH, but the core's
//       DEFAULT base-delay table 256+32*v gives adjacent voices a 32-sample
//       spacing (0.667 ms @48k / 0.333 ms @96k) -- a TEMPORAL inter-voice
//       doubling inside the Haas fusion window, with NO L/R divergence.
//   (3) DIFFERENT-PITCH VOICES VIA THE HARMONY MAP: Spread ON + zero
//       explicit offsets -> voice 2 follows the spread table (+7 st),
//       still with the 32-sample inter-voice base-delay spacing.
//   (4) TRUE L/R DIVERGENCE: only from NON-IDENTICAL channel input (a
//       1-sample offset, a 0.1% amplitude imbalance, or genuinely
//       decorrelated stereo) + independent per-channel trackers.
//
// MATRIX AXES (deterministic, seeded):
//   * spread ON / OFF  x  1 and 2 voices  x  Global Pitch = 0  x  offsets 0
//   * base delays: the DEFAULT table (256+32*v, DISTINCT) vs FORCED EQUAL
//     (all 256) -- the axis the R20 comment wrongly denied
//   * input: identical dual-mono / 1-sample R offset / 0.1% amp imbalance /
//     decorrelated true stereo (+ optional real stereo WAV via --input)
//   * both Lagrange6 and ObsKernel6, at 48 and 96 kHz
//
// MEASUREMENTS per scenario:
//   * L/R bit-identity, maxdiff, RMS-diff dB, side/mid dB
//   * the per-voice PITCH CLASS actually realised: voiceStats(ch,v).ratio
//     (the gliding ratio) -> unison (1.0) vs spread (+7 st = 2^(7/12))
//   * the INTER-VOICE TEMPORAL LAG (voice isolation cross-correlation:
//     render with only voice 0 active, then only voice 1; the output
//     cross-correlation peak = voice 1's delay behind voice 0) -- 0 for
//     forced-equal delays, ~32 samples for the default table
//   * per-channel tracker commit/wrap counts (divergence visibility)
//
// The optional LINKED-STEREO TRACKER design is EVALUATED in the round
// findings (not implemented): L/R audio processing stays separate while
// the pitch-control estimate is shared. The measured basis is recorded
// here: the one-sample-offset scenarios show the per-channel tracker
// divergence (commit-count deltas) that a linked tracker would eliminate,
// at the cost of estimate coupling on anti-phase material.
//
// Usage: octavox_r21_stereo [--json <path>] [--input <stereo.wav>]
//                           [--seconds s]
// Exit 0 on success (nonzero if any dual-mono transparency check fails).
// ============================================================================
#include "octavox/dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../tests/wav_io.hpp"

namespace {

struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed ? seed : 1) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int32_t>(uint32_t(s >> 33)) * (1.0 / 2147483648.0);
    }
};

// Harmonic input (phase-continuous stack + jitter), the r20 law.
void synthHarmonic(std::vector<float>& L, std::vector<float>& R, int sr,
                   double seconds, uint64_t seed, bool independentJitter,
                   double lGain, int rOffsetSamples) {
    const int n = static_cast<int>(seconds * sr);
    L.assign(n, 0.0f);
    R.assign(n, 0.0f);
    Lcg rngL(seed), rngR(seed + 7777);
    const double dt = 1.0 / sr;
    double phL = 0.0, phR = 0.0, jitL = 0.0, jitR = 0.0, jitTL = 0.0, jitTR = 0.0;
    const double period = 1.0 / 220.0;
    for (int i = 0; i < n; ++i) {
        const double t = i * dt;
        if (std::fmod(t, period) < dt) {
            jitTL = (rngL.next() * 2.0 - 1.0) * 3.0;
            jitTR = independentJitter ? (rngR.next() * 2.0 - 1.0) * 3.0 : jitTL;
        }
        jitL += (jitTL - jitL) * (dt / period) * 4.0;
        jitR += (jitTR - jitR) * (dt / period) * 4.0;
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
                b += (1.0 / k) * std::sin(k * phL);
        }
        const double nzL = rngL.next();
        const double nzR = independentJitter ? rngR.next() : nzL;
        L[i] = static_cast<float>(0.5 * a / 8.0 + 0.002 * nzL);
        R[i] = static_cast<float>(0.5 * b / 8.0 + 0.002 * nzR);
    }
    if (lGain != 1.0)
        for (int i = 0; i < n; ++i) L[i] = static_cast<float>(L[i] * lGain);
    if (rOffsetSamples > 0) {
        for (int i = n - 1; i >= rOffsetSamples; --i) R[i] = R[i - rOffsetSamples];
        for (int i = 0; i < rOffsetSamples; ++i) R[i] = 0.0f;
    }
}

struct Row {
    const char* scenario;
    const char* behaviour;  // the distinguished class (1)-(4)
    int sr;
    const char* interp;
    bool spread;
    int voices;
    bool equalDelays;
    const char* input;
    bool bitIdentical;
    double maxdiff;
    double rmsDiffDb;
    double sideMidDb;
    double voice1Ratio;    // realised pitch class of voice 2 (index 1)
    double interVoiceLag;  // samples (voice-isolation cross-correlation)
    long commitsL, commitsR;
};

// Render through the stereo engine with the given surface.
struct RenderOut {
    std::vector<float> L, R;
    double v1ratio = 0.0;
    long commitsL = 0, commitsR = 0, wrapsL = 0, wrapsR = 0;
};

RenderOut render(const std::vector<float>& inL, const std::vector<float>& inR,
                 int sr, octavox::Interpolation interp, bool spread, int voices,
                 bool equalDelays, double voice1Gain = 1.0) {
    using octavox::Config;
    using octavox::DetectorMode;
    using octavox::MultichannelShifter;
    MultichannelShifter eng;
    Config cfg;
    cfg.sample_rate = sr;
    cfg.interpolation = interp;
    cfg.ratio = 1.0;             // Global Pitch = 0 (the R21 spec's setting)
    cfg.voices = voices;
    cfg.spread = spread;         // the Harmony Map switch
    cfg.feedback = 0.0;
    cfg.detector_mode = DetectorMode::Decimated;  // the shipping default
    if (!eng.prepare(cfg, 2)) std::exit(1);
    if (equalDelays) {
        // FORCE all base delays equal (voice 0's default)
        for (int v = 0; v < voices; ++v) eng.setVoiceBaseDelay(v, 256.0);
    }
    // per-voice offsets are ZERO (the reported configuration); with spread
    // ON the voices follow the spread table, with spread OFF they are all
    // unison. Gains 1 (except the voice-isolation probes).
    if (voice1Gain != 1.0) eng.setVoiceGain(1, voice1Gain);

    RenderOut out;
    out.L.assign(inL.size(), 0.0f);
    out.R.assign(inR.size(), 0.0f);
    int done = 0;
    while (done < static_cast<int>(inL.size())) {
        const int blk = std::min(1024, static_cast<int>(inL.size()) - done);
        const float* inP[2] = {inL.data() + done, inR.data() + done};
        float* outP[2] = {out.L.data() + done, out.R.data() + done};
        eng.process(inP, outP, 2, blk);
        done += blk;
    }
    if (voices > 1) out.v1ratio = eng.voiceStats(0, 1).ratio;
    out.commitsL = eng.voiceStats(0, 0).commits;
    out.commitsR = eng.voiceStats(1, 0).commits;
    out.wrapsL = eng.voiceStats(0, 0).wraps;
    out.wrapsR = eng.voiceStats(1, 0).wraps;
    return out;
}

// Voice-isolation inter-voice lag: render voice0-only and voice1-only,
// cross-correlate (peak lag over +-256 lags, post 0.8 s warm-up).
double interVoiceLagSamples(const std::vector<float>& in, int sr,
                            octavox::Interpolation interp, bool spread,
                            bool equalDelays) {
    using octavox::Config;
    using octavox::DetectorMode;
    using octavox::MultichannelShifter;
    auto renderIso = [&](int onlyVoice) {
        MultichannelShifter eng;
        Config cfg;
        cfg.sample_rate = sr;
        cfg.interpolation = interp;
        cfg.ratio = 1.0;
        cfg.voices = 2;
        cfg.spread = spread;
        cfg.feedback = 0.0;
        cfg.detector_mode = DetectorMode::Decimated;
        if (!eng.prepare(cfg, 2)) std::exit(1);
        if (equalDelays)
            for (int v = 0; v < 2; ++v) eng.setVoiceBaseDelay(v, 256.0);
        eng.setVoiceGain(0, onlyVoice == 0 ? 1.0 : 0.0);
        eng.setVoiceGain(1, onlyVoice == 1 ? 1.0 : 0.0);
        std::vector<float> o(in.size(), 0.0f), oR(in.size(), 0.0f);
        int done = 0;
        while (done < static_cast<int>(in.size())) {
            const int blk = std::min(1024, static_cast<int>(in.size()) - done);
            const float* inP[2] = {in.data() + done, in.data() + done};
            float* outP[2] = {o.data() + done, oR.data() + done};
            eng.process(inP, outP, 2, blk);
            done += blk;
        }
        return o;
    };
    const std::vector<float> a = renderIso(0);
    const std::vector<float> b = renderIso(1);
    const size_t skip = static_cast<size_t>(0.8 * sr);
    const int maxLag = 256;
    double bestLag = 0.0, bestC = -1e300;
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double c = 0.0;
        size_t count = 0;
        for (size_t i = skip; i + maxLag + 1 < a.size(); ++i) {
            const size_t j = i + static_cast<size_t>(lag);
            c += double(a[i]) * double(b[j]);
            ++count;
        }
        c /= std::max<size_t>(1, count);
        if (c > bestC) { bestC = c; bestLag = lag; }
    }
    return bestLag;
}

Row analyse(const char* scenario, const char* behaviour, int sr,
            const char* interp, bool spread, int voices, bool equalDelays,
            const char* input, const RenderOut& r,
            double lagSamples) {
    Row row{};
    row.scenario = scenario;
    row.behaviour = behaviour;
    row.sr = sr;
    row.interp = interp;
    row.spread = spread;
    row.voices = voices;
    row.equalDelays = equalDelays;
    row.input = input;
    const size_t skip = static_cast<size_t>(0.5 * sr);
    double maxd = 0.0, sd = 0.0, mid = 0.0, side = 0.0;
    long cnt = 0;
    bool ident = true;
    for (size_t i = skip; i < r.L.size(); ++i) {
        const double a = r.L[i], b = r.R[i];
        const double d = a - b;
        if (d != 0.0) ident = false;
        if (std::abs(d) > maxd) maxd = std::abs(d);
        sd += d * d;
        mid += 0.25 * (a + b) * (a + b);
        side += 0.25 * (a - b) * (a - b);
        ++cnt;
    }
    const double rms = std::sqrt(sd / std::max(1.0, double(cnt)));
    const double midR = std::sqrt(mid / std::max(1.0, double(cnt)));
    const double sideR = std::sqrt(side / std::max(1.0, double(cnt)));
    row.bitIdentical = ident;
    row.maxdiff = maxd;
    row.rmsDiffDb = (rms > 0.0 && midR > 0.0) ? 20.0 * std::log10(rms / midR)
                                              : (ident ? -999.0 : 0.0);
    row.sideMidDb = (sideR > 0.0 && midR > 0.0) ? 20.0 * std::log10(sideR / midR)
                                                : -999.0;
    row.voice1Ratio = r.v1ratio;
    row.interVoiceLag = lagSamples;
    row.commitsL = r.commitsL;
    row.commitsR = r.commitsR;
    return row;
}

}  // namespace

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    const char* wavPath = nullptr;
    double seconds = 4.0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else if (!std::strcmp(argv[i], "--input") && i + 1 < argc)
            wavPath = argv[++i];
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = std::atof(argv[++i]);
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }

    using octavox::Interpolation;
    const int rates[2] = {48000, 96000};
    const char* interps[2] = {"Lagrange6", "ObsKernel6"};
    const Interpolation interpEnum[2] = {Interpolation::Lagrange6,
                                         Interpolation::ObsKernel6};
    std::vector<Row> rows;

    for (int ri = 0; ri < 2; ++ri) {
        const int sr = rates[ri];
        std::vector<float> L, R;
        synthHarmonic(L, R, sr, seconds, 555000 + uint64_t(sr), false, 1.0, 0);
        std::vector<float> L1, R1;
        synthHarmonic(L1, R1, sr, seconds, 556000 + uint64_t(sr), false, 1.0, 1);
        std::vector<float> La, Ra;
        synthHarmonic(La, Ra, sr, seconds, 557000 + uint64_t(sr), false, 1.001, 0);
        std::vector<float> Ld, Rd;
        synthHarmonic(Ld, Rd, sr, seconds, 558000 + uint64_t(sr), true, 1.0, 0);

        for (int ii = 0; ii < 2; ++ii) {
            const Interpolation interp = interpEnum[ii];
            const char* iname = interps[ii];

            // ---- class (1): exact dual-mono transparency -----------------
            rows.push_back(analyse("dm_1v_spread_on_default_delays",
                                   "(1) dual-mono transparency", sr, iname,
                                   true, 1, false, "identical",
                                   render(L, R, sr, interp, true, 1, false),
                                   0.0));
            {
                RenderOut r = render(L, R, sr, interp, false, 2, false);
                rows.push_back(analyse("dm_2v_spread_off_default_delays",
                                       "(1)+(2) same-pitch, distinct delays", sr,
                                       iname, false, 2, false, "identical", r,
                                       interVoiceLagSamples(L, sr, interp, false, false)));
            }
            {
                RenderOut r = render(L, R, sr, interp, false, 2, true);
                rows.push_back(analyse("dm_2v_spread_off_equal_delays",
                                       "(1)+(2) same-pitch, equal delays", sr, iname,
                                       false, 2, true, "identical", r,
                                       interVoiceLagSamples(L, sr, interp, false, true)));
            }
            {
                RenderOut r = render(L, R, sr, interp, true, 2, false);
                rows.push_back(analyse("dm_2v_spread_on_default_delays",
                                       "(1)+(3) harmony-map pitch, distinct delays",
                                       sr, iname, true, 2, false, "identical", r,
                                       interVoiceLagSamples(L, sr, interp, true, false)));
            }
            rows.push_back(analyse("dm_8v_spread_on_default_delays",
                                   "(1)+(3) 8-voice harmony map", sr, iname, true, 8,
                                   false, "identical",
                                   render(L, R, sr, interp, true, 8, false), 0.0));

            // ---- class (4): true L/R divergence --------------------------
            rows.push_back(analyse("offset1_2v_spread_on",
                                   "(4) 1-sample input offset", sr, iname, true, 2,
                                   false, "r_offset_1",
                                   render(L1, R1, sr, interp, true, 2, false), 0.0));
            rows.push_back(analyse("amp01_2v_spread_on",
                                   "(4) 0.1% amplitude imbalance", sr, iname, true, 2,
                                   false, "amp_0.1pct",
                                   render(La, Ra, sr, interp, true, 2, false), 0.0));
            rows.push_back(analyse("true_stereo_2v_spread_on",
                                   "(4) decorrelated stereo", sr, iname, true, 2, false,
                                   "decorrelated",
                                   render(Ld, Rd, sr, interp, true, 2, false), 0.0));
        }
    }

    // optional real stereo WAV (class 4 on genuine material)
    if (wavPath) {
        int sr = 0, ch = 0;
        try {
            std::vector<float> interleaved = octavox::wav::readWav(wavPath, sr, ch);
            if (ch >= 2) {
                const size_t nframes = interleaved.size() / ch;
                const size_t use = std::min(nframes, static_cast<size_t>(8.0 * sr));
                std::vector<float> L(use), R(use);
                for (size_t i = 0; i < use; ++i) {
                    L[i] = interleaved[i * ch];
                    R[i] = interleaved[i * ch + 1];
                }
                for (int ii = 0; ii < 2; ++ii)
                    rows.push_back(analyse("real_stereo_wav", "(4) real recording", sr,
                                           interps[ii], true, 2, false, "wav_file",
                                           render(L, R, sr, interpEnum[ii], true, 2,
                                                  false),
                                           0.0));
            } else {
                std::fprintf(stderr, "input WAV is mono (%d ch) -- skipped\n", ch);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WAV read failed: %s\n", e.what());
        }
    }

    // ---- report -------------------------------------------------------------
    std::printf("%-34s %-38s %6s %-10s %4s %2s %3s %-13s %5s %10s %10s %9s %9s %8s %7s\n",
                "SCENARIO", "BEHAVIOUR CLASS", "SR", "INTERP", "SPRD", "V", "EQD",
                "INPUT", "BITID", "MAXDIFF", "RMSDIFF_DB", "SIDE/MID_DB",
                "V1_RATIO", "IV_LAG", "CMT_D");
    std::printf("%s\n", std::string(190, '-').c_str());
    for (const Row& r : rows) {
        std::printf("%-34s %-38s %6d %-10s %4s %2d %3s %-13s %5s %10.6f %10.2f %9.2f %9.5f %8.1f %7ld\n",
                    r.scenario, r.behaviour, r.sr, r.interp,
                    r.spread ? "on" : "off", r.voices, r.equalDelays ? "yes" : "no",
                    r.input, r.bitIdentical ? "yes" : "NO", r.maxdiff, r.rmsDiffDb,
                    r.sideMidDb, r.voice1Ratio, r.interVoiceLag,
                    r.commitsL - r.commitsR);
    }

    // gate summary: every "identical"-input scenario MUST be bit-identical
    long violations = 0;
    for (const Row& r : rows)
        if (std::strcmp(r.input, "identical") == 0 && !r.bitIdentical) ++violations;
    std::printf("\ndual-mono transparency violations: %ld (must be 0)\n", violations);

    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", jsonPath);
            return 1;
        }
        std::fprintf(f, "[\n");
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            std::fprintf(f,
                "  {\"scenario\": \"%s\", \"behaviour\": \"%s\", \"rate\": %d, "
                "\"interp\": \"%s\", \"spread\": %s, \"voices\": %d, "
                "\"equal_delays\": %s, \"input\": \"%s\", "
                "\"bit_identical\": %s, \"maxdiff\": %.9g, "
                "\"rmsdiff_db\": %.9g, \"side_over_mid_db\": %.9g, "
                "\"voice1_ratio\": %.9g, \"inter_voice_lag_samples\": %.9g, "
                "\"commits_L\": %ld, \"commits_R\": %ld}%s\n",
                r.scenario, r.behaviour, r.sr, r.interp, r.spread ? "true" : "false",
                r.voices, r.equalDelays ? "true" : "false", r.input,
                r.bitIdentical ? "true" : "false", r.maxdiff, r.rmsDiffDb,
                r.sideMidDb, r.voice1Ratio, r.interVoiceLag, r.commitsL, r.commitsR,
                i + 1 < rows.size() ? "," : "");
        }
        std::fprintf(f, "]\n");
        std::fclose(f);
        std::fprintf(stderr, "json written: %s\n", jsonPath);
    }
    return violations == 0 ? 0 : 1;
}
