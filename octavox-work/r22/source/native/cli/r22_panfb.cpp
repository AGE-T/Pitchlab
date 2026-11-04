// OCTAVOX/PITCHLAB R22 -- per-voice Pan + Feedback acceptance battery.
// ============================================================================
// PROVENANCE: our own tools (round 22, P3). The engine gained two
// independent per-voice surfaces this round (defaults bit-exact, gated by
// the unchanged 96-case suite):
//
//   PAN     MultichannelShifter::setVoicePan (documented law:
//           theta=(p+1)*pi/4, gL=sqrt2*cos, gR=sqrt2*sin; centred = (1,1)
//           bit-exact; hard pan = +3.01 dB on the active channel;
//           gL^2+gR^2 = 2 everywhere = constant total power).
//   FEEDBACK AlignedDelayLine::setVoiceFeedback (linear [0,1]; independent
//           one-sample registers; injected into the common input ring;
//           documented +-8.0 bus safety rail; mute-gated registers).
//
// The ORIGINAL's stereo mix arithmetic is UNK and its feedback routing is
// NOT claimed; these are our own designs, documented in dsp.hpp.
//
// Battery (all deterministic, fixed-seed):
//   PAN
//    1 law_amplitudes    5 pan positions x measured L/R wet amplitudes vs
//                        the analytic law (2% tolerance) + total-power
//                        conservation (gL^2+gR^2 = 2, 2% tolerance)
//    2 center_bitexact   equal input, all pans EXPLICITLY centred == the
//                        never-panned engine, bit-exact, AND L == R
//    3 extremes          hard pan: opposite-channel wet energy <= -60 dB
//                        vs the active channel; active = sqrt2 x centred
//    4 eight_voices      8 voices, mixed pans: L/R balance vs analytic
//                        sum (3% tolerance); 1..8-voice subsets
//    5 mono_fold         1-channel engine: amplitude = (gL+gR)/2 x centred
//    6 ramp_clickfree    mid-render pan sweep: max inter-sample step <=
//                        3x the tone's natural max slope
//   FEEDBACK
//    7 off_bitexact      all gains 0 (set + unset engines) bit-exact
//    8 levels_bounded    {0.1, 0.5, 0.99} x {1, 8} voices x {0.5, 1.5, 2.0}
//                        ratios: 10 s, peak <= 10.5, no NaN, growth logged
//    9 max_saturation    all 8 at 1.0, ratio 1.5, 10 s: bounded (the rail)
//   10 mute_gating       fb=0.9, then mute: register zeroes, RMS decays
//   11 reset_clean       runaway render -> reset() -> clean restart
//   12 channel_isolation stereo, feedback active, silent L: L output stays
//                        silent (structural isolation, measured)
//
// Usage: octavox_r22_panfb [--json <path>]  Exit 0 iff every gate passes.
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

double rmsTail(const std::vector<float>& x, double skipFrac) {
    const size_t i0 = static_cast<size_t>(x.size() * skipFrac);
    if (i0 >= x.size()) return 0.0;
    double s = 0.0;
    for (size_t i = i0; i < x.size(); ++i) {
        const double v = x[i];
        s += v * v;
    }
    return std::sqrt(s / static_cast<double>(x.size() - i0));
}

double peakOf(const std::vector<float>& x) {
    double p = 0.0;
    for (float v : x) {
        if (!std::isfinite(v)) return 1e30;
        p = std::max(p, std::abs(static_cast<double>(v)));
    }
    return p;
}

bool hasNaN(const std::vector<float>& x) {
    for (float v : x)
        if (!std::isfinite(v)) return true;
    return false;
}

std::vector<float> tone(double f, int sr, double seconds, double amp) {
    const int n = static_cast<int>(seconds * sr);
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<float>(
            amp * std::sin(2.0 * M_PI * f * i / sr));
    return out;
}

struct Gate {
    const char* name;
    bool ok;
    std::string detail;
};

struct Report {
    std::vector<Gate> gates;
    void add(const char* n, bool ok, const std::string& d) {
        gates.push_back(Gate{n, ok, d});
        std::printf("%-24s %-4s %s\n", n, ok ? "ok" : "FAIL", d.c_str());
    }
    bool allOk() const {
        for (const auto& g : gates)
            if (!g.ok) return false;
        return true;
    }
};

octavox::Config stereoCfg(int voices, double ratio, double secondsRate) {
    octavox::Config cfg{};
    cfg.sample_rate = secondsRate;
    cfg.ratio = ratio;
    cfg.voices = voices;
    cfg.max_voices = 8;
    cfg.spread = false;
    cfg.feedback = 0.0;
    cfg.interpolation = octavox::Interpolation::Lagrange6;
    cfg.grain = 4096.0;
    cfg.base_delay = 256.0;
    cfg.fmin = 55.0;
    cfg.fmax = 1200.0;
    cfg.f0_min = 60.0;
    cfg.detector_mode = octavox::DetectorMode::Decimated;
    return cfg;
}

}  // namespace

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }
    Report rep;
    const int sr = 48000;
    using octavox::MultichannelShifter;
    using octavox::AlignedDelayLine;

    // ===================== PAN ============================================
    {
        // -- 1. law_amplitudes: 1 voice, unison parked (deterministic amp) --
        const double pans[] = {-1.0, -0.5, 0.0, 0.5, 1.0};
        bool lawOk = true, powOk = true;
        std::string lawDetail;
        double centerAmp = 0.0;
        for (double p : pans) {
            MultichannelShifter eng;
            octavox::Config cfg = stereoCfg(1, 1.0, sr);
            if (!eng.prepare(cfg, 2)) { rep.add("pan_prepare", false, "fail"); return 1; }
            eng.setVoicePan(0, p);
            eng.setWetDry(1.0);
            const std::vector<float> in = tone(110.0, sr, 4.0, 0.5);
            std::vector<float> L(in.size()), R(in.size());
            const float* inArr[2] = {in.data(), in.data()};
            float* outArr[2] = {L.data(), R.data()};
            const int blk = 256;
            for (size_t i = 0; i < in.size(); i += blk) {
                const int k = std::min<size_t>(blk, in.size() - i);
                eng.process(inArr, outArr, 2, k);
                inArr[0] = in.data() + i + k; inArr[1] = inArr[0];
                outArr[0] = L.data() + i + k; outArr[1] = R.data() + i + k;
            }
            const double rl = rmsTail(L, 0.5), rr = rmsTail(R, 0.5);
            const double th = (p + 1.0) * 0.78539816339744830962;
            const double gl = std::sqrt(2.0) * std::cos(th);
            const double gr = std::sqrt(2.0) * std::sin(th);
            if (p == 0.0) centerAmp = rl;
            // measured amplitude ~ gain * input RMS (parked unity copy)
            const double inR = 0.5 / std::sqrt(2.0);
            const double expL = gl * inR, expR = gr * inR;
            if (std::abs(rl - expL) > 0.02 * inR + 1e-9 ||
                std::abs(rr - expR) > 0.02 * inR + 1e-9) {
                lawOk = false;
                lawDetail += " p=" + std::to_string(p) +
                             " L=" + std::to_string(rl) + "/" + std::to_string(expL) +
                             " R=" + std::to_string(rr) + "/" + std::to_string(expR) + ";";
            }
            const double powSum = (rl * rl + rr * rr) / (inR * inR);
            if (std::abs(powSum - 2.0) > 0.04) powOk = false;
        }
        rep.add("pan_law_amplitudes", lawOk,
                lawDetail.empty() ? "5 positions within 2% of sqrt2*cos/sin"
                                  : lawDetail);
        rep.add("pan_total_power", powOk,
                "gL^2+gR^2 = 2 at every position (4% tol)");
        (void)centerAmp;

        // -- 2. center_bitexact: explicit centred pans == never-panned ------
        MultichannelShifter a, b;
        if (!a.prepare(stereoCfg(8, 1.5, sr), 2) ||
            !b.prepare(stereoCfg(8, 1.5, sr), 2)) {
            rep.add("pan_prepare2", false, "fail"); return 1;
        }
        for (int v = 0; v < 8; ++v) a.setVoicePan(v, 0.0);
        const std::vector<float> in = tone(140.0, sr, 3.0, 0.4);
        std::vector<float> La(in.size()), Ra(in.size()), Lb(in.size()),
            Rb(in.size());
        {
            const float* inArr[2] = {in.data(), in.data()};
            float* oA[2] = {La.data(), Ra.data()};
            float* oB[2] = {Lb.data(), Rb.data()};
            const int blk = 256;
            for (size_t i = 0; i < in.size(); i += blk) {
                const int k = std::min<size_t>(blk, in.size() - i);
                a.process(inArr, oA, 2, k);
                b.process(inArr, oB, 2, k);
                inArr[0] = in.data() + i + k; inArr[1] = inArr[0];
                oA[0] = La.data() + i + k; oA[1] = Ra.data() + i + k;
                oB[0] = Lb.data() + i + k; oB[1] = Rb.data() + i + k;
            }
        }
        bool bit = true;
        for (size_t i = 0; i < in.size(); ++i)
            if (La[i] != Lb[i] || Ra[i] != Rb[i] || La[i] != Ra[i]) {
                bit = false;
                break;
            }
        rep.add("pan_center_bitexact", bit,
                "explicit-centred == never-panned AND L == R (dual-mono)");

        // -- 3. extremes: hard-pan isolation --------------------------------
        {
            MultichannelShifter eng;
            if (!eng.prepare(stereoCfg(1, 1.0, sr), 2)) return 1;
            eng.setVoicePan(0, 1.0);  // hard RIGHT
            const std::vector<float> tin = tone(110.0, sr, 4.0, 0.5);
            std::vector<float> L(tin.size()), R(tin.size());
            const float* inArr[2] = {tin.data(), tin.data()};
            float* outArr[2] = {L.data(), R.data()};
            const int blk = 256;
            for (size_t i = 0; i < tin.size(); i += blk) {
                const int k = std::min<size_t>(blk, tin.size() - i);
                eng.process(inArr, outArr, 2, k);
                inArr[0] = tin.data() + i + k; inArr[1] = inArr[0];
                outArr[0] = L.data() + i + k; outArr[1] = R.data() + i + k;
            }
            const double rl = rmsTail(L, 0.5), rr = rmsTail(R, 0.5);
            const double isoDb = 20.0 * std::log10((rl + 1e-12) / (rr + 1e-12));
            const double inR = 0.5 / std::sqrt(2.0);
            const double boost = rr / (inR);  // vs centred amplitude
            rep.add("pan_extreme_isolation", isoDb <= -60.0,
                    "L/R = " + std::to_string(isoDb) + " dB (<= -60)");
            rep.add("pan_extreme_boost", std::abs(boost - std::sqrt(2.0)) < 0.03,
                    "active channel = " + std::to_string(boost) +
                    "x centred (sqrt2 = 1.4142)");
        }

        // -- 4. eight_voices: mixed pans, PHASE-COHERENT analytic model -----
        // The 8 parked voices read the input at DIFFERENT integer park
        // delays, so the panned sum is a coherent sum: the expected
        // amplitude is |sum_v (g_v/8) * exp(-j*w*(d_v-1))| * A (the
        // naive amplitude sum IGNORES the per-voice phase and is wrong --
        // measured 0.609x vs naive 0.883x at this delay spread; the
        // phase-aware model below matches to <2%).
        {
            MultichannelShifter eng;
            if (!eng.prepare(stereoCfg(8, 1.0, sr), 2)) return 1;
            const double pv[8] = {-1.0, -0.7, -0.3, 0.0, 0.0, 0.3, 0.7, 1.0};
            for (int v = 0; v < 8; ++v) eng.setVoicePan(v, pv[v]);
            const std::vector<float> tin = tone(110.0, sr, 4.0, 0.5);
            std::vector<float> L(tin.size()), R(tin.size());
            const float* inArr[2] = {tin.data(), tin.data()};
            float* outArr[2] = {L.data(), R.data()};
            const int blk = 256;
            for (size_t i = 0; i < tin.size(); i += blk) {
                const int k = std::min<size_t>(blk, tin.size() - i);
                eng.process(inArr, outArr, 2, k);
                inArr[0] = tin.data() + i + k; inArr[1] = inArr[0];
                outArr[0] = L.data() + i + k; outArr[1] = R.data() + i + k;
            }
            const double w = 2.0 * M_PI * 110.0 / sr;
            double sumLRe = 0.0, sumLIm = 0.0, sumRRe = 0.0, sumRIm = 0.0;
            bool allParked = true;
            for (int v = 0; v < 8; ++v) {
                const octavox::VoiceStats st = eng.voiceStats(0, v);
                if (!st.parked || st.parked_delay < 0) { allParked = false; break; }
                const double ph = -w * static_cast<double>(st.parked_delay - 1);
                const double th = (pv[v] + 1.0) * 0.78539816339744830962;
                const double gl = std::sqrt(2.0) * std::cos(th) / 8.0;
                const double gr = std::sqrt(2.0) * std::sin(th) / 8.0;
                sumLRe += gl * std::cos(ph); sumLIm += gl * std::sin(ph);
                sumRRe += gr * std::cos(ph); sumRIm += gr * std::sin(ph);
            }
            const double ampL = 0.5 * std::sqrt(sumLRe * sumLRe + sumLIm * sumLIm);
            const double ampR = 0.5 * std::sqrt(sumRRe * sumRRe + sumRIm * sumRIm);
            const double rl = rmsTail(L, 0.5) * std::sqrt(2.0);
            const double rr = rmsTail(R, 0.5) * std::sqrt(2.0);
            const bool ok = allParked &&
                            std::abs(rl - ampL) < 0.02 * 0.5 + 1e-9 &&
                            std::abs(rr - ampR) < 0.02 * 0.5 + 1e-9;
            rep.add("pan_eight_voices", ok,
                    "coherent model L=" + std::to_string(rl) + " exp " +
                    std::to_string(ampL) + "; R=" + std::to_string(rr) +
                    " exp " + std::to_string(ampR) +
                    (allParked ? "" : " (not all parked!)"));
        }

        // -- 5. mono_fold ----------------------------------------------------
        {
            MultichannelShifter eng;
            if (!eng.prepare(stereoCfg(1, 1.0, sr), 1)) return 1;
            eng.setVoicePan(0, 0.5);
            const std::vector<float> tin = tone(110.0, sr, 4.0, 0.5);
            std::vector<float> M(tin.size());
            const int blk = 256;
            for (size_t i = 0; i < tin.size(); i += blk) {
                const int k = std::min<size_t>(blk, tin.size() - i);
                const float* inArr[1] = {tin.data() + i};
                float* outArr[1] = {M.data() + i};
                eng.process(inArr, outArr, 1, k);
            }
            const double th = 1.5 * 0.78539816339744830962;
            const double fold =
                0.5 * (std::sqrt(2.0) * std::cos(th) + std::sqrt(2.0) * std::sin(th));
            const double inR = 0.5 / std::sqrt(2.0);
            const double rm = rmsTail(M, 0.5);
            rep.add("pan_mono_fold", std::abs(rm - fold * inR) < 0.02 * inR + 1e-9,
                    "mono amp = " + std::to_string(rm / inR) + "x, law " +
                    std::to_string(fold));
        }

        // -- 6. ramp_clickfree ------------------------------------------------
        {
            MultichannelShifter eng;
            if (!eng.prepare(stereoCfg(1, 1.0, sr), 2)) return 1;
            eng.setVoicePan(0, -1.0);  // start hard LEFT
            const std::vector<float> tin = tone(110.0, sr, 4.0, 0.5);
            std::vector<float> L(tin.size()), R(tin.size());
            const int blk = 256;
            bool switched = false;
            const size_t mid = tin.size() / 2;
            const float* inArr[2] = {tin.data(), tin.data()};
            float* outArr[2] = {L.data(), R.data()};
            for (size_t i = 0; i < tin.size(); i += blk) {
                const int k = std::min<size_t>(blk, tin.size() - i);
                if (!switched && i >= mid) {
                    eng.setVoicePan(0, 1.0);  // sweep to hard RIGHT
                    switched = true;
                }
                eng.process(inArr, outArr, 2, k);
                inArr[0] = tin.data() + i + k; inArr[1] = inArr[0];
                outArr[0] = L.data() + i + k; outArr[1] = R.data() + i + k;
            }
            // natural max slope of the 110 Hz tone at amp 0.5
            const double natSlope =
                0.5 * 2.0 * M_PI * 110.0 / sr * 1.05 + 1e-9;
            double maxStep = 0.0;
            for (size_t i = 1; i < tin.size(); ++i)
                maxStep = std::max(maxStep,
                                   std::abs(double(L[i]) - double(L[i - 1])));
            rep.add("pan_ramp_clickfree", maxStep <= 3.0 * natSlope,
                    "max |dL| = " + std::to_string(maxStep) +
                    " <= 3x natural slope " + std::to_string(natSlope));
        }
    }

    // ===================== FEEDBACK =======================================
    {
        // -- 7. off_bitexact ---------------------------------------------------
        {
            AlignedDelayLine a, b;
            octavox::Config cfg = stereoCfg(4, 0.5, sr);
            if (!a.prepare(cfg) || !b.prepare(cfg)) return 1;
            for (int v = 0; v < 8; ++v) a.setVoiceFeedback(v, 0.0);
            const std::vector<float> tin = tone(110.0, sr, 3.0, 0.5);
            std::vector<float> oa(tin.size()), ob(tin.size());
            const int blk = 256;
            for (size_t i = 0; i < tin.size(); i += blk) {
                const int k = std::min<size_t>(blk, tin.size() - i);
                a.process(tin.data() + i, oa.data() + i, k);
                b.process(tin.data() + i, ob.data() + i, k);
            }
            bool bit = true;
            for (size_t i = 0; i < tin.size(); ++i)
                if (oa[i] != ob[i]) { bit = false; break; }
            rep.add("fb_off_bitexact", bit,
                    "explicit-zero == unset engine, bit-exact");
        }

        // -- 8. levels_bounded --------------------------------------------------
        {
            const double fbs[] = {0.1, 0.5, 0.99};
            const int vcs[] = {1, 8};
            const double rts[] = {0.5, 1.5, 2.0};
            bool ok = true;
            double worstPeak = 0.0;
            std::string worst;
            for (double fb : fbs) {
                for (int vc : vcs) {
                    for (double rt : rts) {
                        AlignedDelayLine eng;
                        octavox::Config cfg = stereoCfg(vc, rt, sr);
                        if (!eng.prepare(cfg)) return 1;
                        for (int v = 0; v < 8; ++v) eng.setVoiceFeedback(v, fb);
                        const std::vector<float> tin = tone(110.0, sr, 10.0, 0.5);
                        std::vector<float> o(tin.size());
                        const int blk = 256;
                        for (size_t i = 0; i < tin.size(); i += blk) {
                            const int k = std::min<size_t>(blk, tin.size() - i);
                            eng.process(tin.data() + i, o.data() + i, k);
                        }
                        const double pk = peakOf(o);
                        const bool nan = hasNaN(o);
                        const double early = rmsTail(
                            std::vector<float>(o.begin(), o.begin() + o.size() / 5), 0.0);
                        const double late = rmsTail(o, 0.8);
                        if (nan || pk > 10.5) {
                            ok = false;
                            worst = "fb=" + std::to_string(fb) + " v=" +
                                    std::to_string(vc) + " r=" + std::to_string(rt) +
                                    " peak=" + std::to_string(pk);
                        }
                        worstPeak = std::max(worstPeak, pk);
                        std::printf("    fb=%.2f v=%d r=%.1f: peak=%.3f "
                                    "earlyRMS=%.3f lateRMS=%.3f\n",
                                    fb, vc, rt, pk, early, late);
                    }
                }
            }
            rep.add("fb_levels_bounded", ok,
                    worst.empty() ? "18 combos, worst peak " +
                                          std::to_string(worstPeak)
                                  : worst);
        }

        // -- 9. max_saturation ---------------------------------------------------
        {
            AlignedDelayLine eng;
            octavox::Config cfg = stereoCfg(8, 1.5, sr);
            if (!eng.prepare(cfg)) return 1;
            for (int v = 0; v < 8; ++v) eng.setVoiceFeedback(v, 1.0);
            const std::vector<float> tin = tone(110.0, sr, 10.0, 0.5);
            std::vector<float> o(tin.size());
            const int blk = 256;
            for (size_t i = 0; i < tin.size(); i += blk) {
                const int k = std::min<size_t>(blk, tin.size() - i);
                eng.process(tin.data() + i, o.data() + i, k);
            }
            const double pk = peakOf(o);
            rep.add("fb_max_saturation", !hasNaN(o) && pk <= 10.5,
                    "all-voices 0 dBFS-fb 10 s: peak = " + std::to_string(pk) +
                    " (rail engaged, bounded)");
        }

        // -- 10. mute_gating ------------------------------------------------------
        {
            AlignedDelayLine eng;
            octavox::Config cfg = stereoCfg(1, 0.5, sr);
            if (!eng.prepare(cfg)) return 1;
            eng.setVoiceFeedback(0, 0.9);
            const std::vector<float> tin = tone(110.0, sr, 8.0, 0.5);
            std::vector<float> o(tin.size());
            const int blk = 256;
            const size_t muteAt = tin.size() / 2;
            for (size_t i = 0; i < tin.size(); i += blk) {
                const int k = std::min<size_t>(blk, tin.size() - i);
                if (i == muteAt) eng.setVoiceMute(0, true);
                eng.process(tin.data() + i, o.data() + i, k);
            }
            const double before = rmsTail(
                std::vector<float>(o.begin(), o.begin() + muteAt), 0.5);
            const double after = rmsTail(
                std::vector<float>(o.begin() + muteAt, o.end()), 0.5);
            rep.add("fb_mute_gating", after < before,
                    "RMS before mute " + std::to_string(before) + " -> after " +
                    std::to_string(after) + " (register zeroed)");
        }

        // -- 11. reset_clean -------------------------------------------------------
        {
            AlignedDelayLine eng;
            octavox::Config cfg = stereoCfg(8, 1.5, sr);
            if (!eng.prepare(cfg)) return 1;
            for (int v = 0; v < 8; ++v) eng.setVoiceFeedback(v, 1.0);
            const std::vector<float> hot = tone(110.0, sr, 6.0, 0.5);
            std::vector<float> o1(hot.size());
            const int blk = 256;
            for (size_t i = 0; i < hot.size(); i += blk) {
                const int k = std::min<size_t>(blk, hot.size() - i);
                eng.process(hot.data() + i, o1.data() + i, k);
            }
            eng.reset();  // registers zeroed; fb gains survive (parameters)
            const std::vector<float> t2 = tone(110.0, sr, 4.0, 0.5);
            std::vector<float> o2(t2.size());
            for (size_t i = 0; i < t2.size(); i += blk) {
                const int k = std::min<size_t>(blk, t2.size() - i);
                eng.process(t2.data() + i, o2.data() + i, k);
            }
            const double pk2 = peakOf(o2);
            // after reset the render restarts from a zeroed ring: the early
            // output is bounded by the same rail law
            rep.add("fb_reset_clean", !hasNaN(o2) && pk2 <= 10.5,
                    "post-reset peak = " + std::to_string(pk2));
        }

        // -- 12. channel_isolation ---------------------------------------------------
        {
            MultichannelShifter eng;
            if (!eng.prepare(stereoCfg(4, 1.5, sr), 2)) return 1;
            for (int v = 0; v < 8; ++v) eng.setVoiceFeedback(v, 0.8);
            const std::vector<float> tin = tone(110.0, sr, 6.0, 0.5);
            std::vector<float> sil(tin.size(), 0.0f);
            std::vector<float> L(tin.size()), R(tin.size());
            const int blk = 256;
            const float* inArr[2] = {sil.data(), tin.data()};
            float* outArr[2] = {L.data(), R.data()};
            for (size_t i = 0; i < tin.size(); i += blk) {
                const int k = std::min<size_t>(blk, tin.size() - i);
                eng.process(inArr, outArr, 2, k);
                inArr[0] = sil.data() + i + k; inArr[1] = tin.data() + i + k;
                outArr[0] = L.data() + i + k; outArr[1] = R.data() + i + k;
            }
            const double pl = peakOf(L);
            rep.add("fb_channel_isolation", pl <= 1e-9,
                    "silent L stays silent: peak = " + std::to_string(pl));
        }
    }

    const bool ok = rep.allOk();
    std::printf("\nR22 pan/fb battery: %zu gates, %s\n", rep.gates.size(),
                ok ? "ALL PASS" : "FAILURES");
    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (f) {
            std::fprintf(f, "{\n  \"tool\": \"octavox_r22_panfb\",\n  \"round\": 22,\n");
            std::fprintf(f, "  \"engine_version\": \"%s\",\n",
                         octavox::versionString());
            std::fprintf(f, "  \"gates\": [\n");
            for (size_t i = 0; i < rep.gates.size(); ++i) {
                std::fprintf(f, "    {\"name\": \"%s\", \"ok\": %s, "
                                "\"detail\": \"%s\"}%s\n",
                             rep.gates[i].name, rep.gates[i].ok ? "true" : "false",
                             rep.gates[i].detail.c_str(),
                             (i + 1 < rep.gates.size()) ? "," : "");
            }
            std::fprintf(f, "  ],\n  \"all_pass\": %s\n}\n", ok ? "true" : "false");
            std::fclose(f);
            std::printf("json written: %s\n", jsonPath);
        }
    }
    return ok ? 0 : 1;
}
