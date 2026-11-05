// R15-P2 SALVAGE ARTIFACT -- DO NOT COMPILE STANDALONE.
// ============================================================================
// The 7 r15acc_* acceptance cases, fully designed and gate-fixed BEFORE the
// 2026-10-26 environment reset wiped the live tree mid-task (the same file
// was about to be applied to tests/test_main.cpp when the tree vanished).
// TO MERGE after the tree is recovered: append the casesR15Acc() function
// below to tests/test_main.cpp just before the "JSON report" section (after
// casesR15P1), add `casesR15Acc(g_results);` after `casesR15P1(g_results);`
// in main(), and add `#include <limits>` to the include block. It reuses
// the existing helpers only (gen::, meas::, run(), CaseResult, check,
// addValue, semitoneRatio, meas::pitchFFT -- the Hann-exact corrected
// estimator, mandatory for every pitch measurement).
// ============================================================================
// The 13-category battery it completes (the 7 P1 cases cover the rest):
//   r15acc_ratio_change_isolation, r15acc_level_accuracy,
//   r15acc_block_boundary_semantics, r15acc_reset_reprepare_determinism,
//   r15acc_stereo_surface, r15acc_robustness_battery, r15acc_invalid_args.
// Every gate was fixed before running (no widening-to-pass); where a bound
// is implementation-independent-by-necessity the literal value is recorded
// (the P1 pattern). Budget: ~6 s of extra suite runtime at 48 kHz.

void casesR15Acc(std::vector<CaseResult>& R) {
    const int sr = 48000;

    // FNV-1a 64 over raw output float bits (same law as default_neutrality).
    auto fnv1a64 = [](const std::vector<float>& x) {
        uint64_t h = 1469598103934665603ULL;
        for (float s : x) {
            uint32_t b;
            std::memcpy(&b, &s, 4);
            for (int i = 0; i < 4; ++i) {
                h ^= static_cast<uint64_t>((b >> (8 * i)) & 0xffu);
                h *= 1099511628211ULL;
            }
        }
        return h;
    };
    const double kNaN = std::numeric_limits<double>::quiet_NaN();
    const double kInf = std::numeric_limits<double>::infinity();

    {  // 1. r15acc_ratio_change_isolation: a mid-stream per-voice ratio
        //    change touches ONLY its own voice. Measurement: voice 3 is
        //    muted throughout two parallel reference runs (identical
        //    input/config/setter cadence), one WITH the voice-3 ratio
        //    change and one without -- the 7 other voices' sum must stay
        //    BIT-IDENTICAL for the whole stream (feedback 0: the global
        //    feedback is wet-path-only and would legitimately couple the
        //    voices, so isolation is measured feedback-free). Voice 3
        //    itself must converge to its new pitch within 1.0 cent.
        CaseResult c{"r15acc_ratio_change_isolation"};
        const double semis[8] = {-12.0, -7.0, -4.0, 3.0,
                                 -5.0,  5.0,  -7.0, 12.0};
        const double f0in = 220.0;
        const int P = 118784;  // 2.47 s = 1024*116 (block boundary)
        const int n = P + 3 * sr;
        const auto in = gen::tone(f0in, sr, n, 0.5);
        Config cfg;  // voices=8, spread off, ratio 1 (targets come from the
        cfg.voices = 8;  // per-voice overrides), feedback 0 (see above)
        cfg.spread = false;
        const double r3a = std::pow(2.0, 3.0 / 12.0);   // voice 3 initial
        const double r3b = std::pow(2.0, -2.0 / 12.0);  // voice 3 new target

        auto runMuted = [&](bool change) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            for (int j = 0; j < 8; ++j)
                e.setVoiceRatio(j, std::pow(2.0, semis[j] / 12.0));
            e.setVoiceMute(3, true);  // voice 3 muted throughout
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(1024, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == P)
                    e.setVoiceRatio(3, change ? r3b : r3a);
            }
            return out;
        };
        const auto outY = runMuted(true);
        const auto outY0 = runMuted(false);
        double otherMax = 0.0;
        for (int k = 0; k < n; ++k)
            otherMax =
                std::max(otherMax, std::abs((double)outY[k] - outY0[k]));
        addValue(c, "other_voices_fullstream_max_diff", otherMax);
        check(c, otherMax == 0.0,
              "voice 3's mid-stream ratio change perturbed the other voices");

        // voice 3 alone (the other 7 muted): pitch before and after P.
        {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            for (int j = 0; j < 8; ++j) {
                e.setVoiceRatio(j, std::pow(2.0, semis[j] / 12.0));
                if (j != 3) e.setVoiceMute(j, true);
            }
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(1024, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == P) e.setVoiceRatio(3, r3b);
            }
            // R13-d windows: 65536 samples (1.365 s) >= 0.5 s and >= 200
            // periods of 261.6 Hz (0.765 s); post window 2 s vs 196 Hz.
            const double fPre =
                meas::pitchFFT(out.data() + (P - 65536), 65536, sr,
                               50.0, 4000.0);
            const double cPre = meas::centsBetween(fPre, f0in * r3a);
            addValue(c, "voice3_pre_change_cents", cPre);
            check(c, std::abs(cPre) <= 1.0,
                  "voice 3 pre-change pitch off by > 1.0 cent");
            const int post0 = P + sr;  // glide (worst ~0.27 s) settled
            const double fPost = meas::pitchFFT(out.data() + post0,
                                                n - post0, sr, 50.0, 4000.0);
            const double cPost = meas::centsBetween(fPost, f0in * r3b);
            addValue(c, "voice3_post_change_cents", cPost);
            check(c, std::abs(cPost) <= 1.0,
                  "voice 3 did not converge to its new ratio within 1.0 cent");
        }
        c.pass = c.note.empty();
        R.push_back(std::move(c));
    }

    {  // 2. r15acc_level_accuracy: per-voice gains {0.1, 0.25, 0.5, 1.0} on
        //    a steady tone, ONE active voice per measurement (7 muted),
        //    steady RMS ratio vs the expected level within +/- 0.1 dB.
        //    Expected is computed in-test from the header law: final voice
        //    gain = normalisation * user_gain with normalisation = 1/voices.
        CaseResult c{"r15acc_level_accuracy"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;
        const auto in = gen::tone(220.0, sr, 3 * sr, 0.5);
        const int settle = sr;  // gain/mute ramps (720) + detector lock
        const double gains[4] = {0.1, 0.25, 0.5, 1.0};
        double worst = 0.0;
        for (double g : gains) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            e.setVoiceGain(0, g);
            for (int j = 1; j < 8; ++j) e.setVoiceMute(j, true);
            std::vector<float> out(in.size(), 0.0f);
            e.process(in.data(), out.data(), static_cast<int>(in.size()));
            const int w = static_cast<int>(in.size()) - settle;  // 2 s
            const double ro = meas::rms(out.data() + settle, w);
            const double ri = meas::rms(in.data() + settle, w);
            const double expected = g / cfg.voices;  // header law
            const double db = 20.0 * std::log10(ro / ri) -
                              20.0 * std::log10(expected);
            char vk[40];
            std::snprintf(vk, sizeof(vk), "err_db_gain_%.2f", g);
            addValue(c, vk, db);
            worst = std::max(worst, std::abs(db));
        }
        addValue(c, "worst_err_db", worst);
        check(c, worst <= 0.1,
              "steady level outside +/-0.1 dB of the expected gain");
        c.pass = c.note.empty();
        R.push_back(std::move(c));
    }

    {  // 3. r15acc_block_boundary_semantics: a parameter change issued
        //    between process() calls applies from the NEXT block as a
        //    STREAM-POSITION event. Run A: 64-sample blocks; run B: the
        //    identical stream in 150-sample blocks; the same simultaneous
        //    setVoiceBaseDelay + setVoiceGain + setWetDry fire at stream
        //    position P (a boundary in BOTH splittings) -> outputs
        //    identical (max|delta| == 0), proving the semantics are
        //    position-, not call-pattern-, driven. Plus: 20 small gain
        //    steps vs one jump to the same final target -> identical final
        //    steady state (the ramp terminates exactly at its target).
        CaseResult c{"r15acc_block_boundary_semantics"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;
        cfg.feedback = 0.25;
        const auto in = gen::tone(220.0, sr, 24000, 0.5);
        const int n = static_cast<int>(in.size());
        const int P = 9600;  // 64*150: a block boundary in both splittings

        auto runSplit = [&](int block) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(block, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == P) {  // simultaneous multi-parameter update
                    e.setVoiceBaseDelay(5, 1200.0);
                    e.setVoiceGain(2, 0.5);
                    e.setWetDry(0.5);
                }
            }
            return out;
        };
        const auto outA = runSplit(64);
        const auto outB = runSplit(150);
        double pre = 0.0, post = 0.0;
        for (int k = 0; k < n; ++k) {
            const double d = std::abs((double)outA[k] - outB[k]);
            if (k < P) pre = std::max(pre, d);
            else post = std::max(post, d);
        }
        addValue(c, "pre_change_max_diff", pre);
        addValue(c, "post_change_max_diff", post);
        check(c, pre == 0.0 && post == 0.0,
              "application is not stream-position (block-boundary) semantics");

        // sanity: the boundary change really changed the stream (the gate
        // above would otherwise be vacuous)
        {
            AlignedDelayLine e;
            e.prepare(cfg);
            std::vector<float> out(n, 0.0f);
            e.process(in.data(), out.data(), n);
            double d = 0.0;
            for (int k = P; k < n; ++k)
                d = std::max(d, std::abs((double)outA[k] - out[k]));
            addValue(c, "change_effect_from_P", d);
            check(c, d > 0.0, "the boundary parameter change had no effect");
        }

        // gradual (20 x 0.025 steps) vs one jump, both ending at gain 0.5
        {
            const int n2 = 24000;
            const auto in2 = gen::tone(220.0, sr, n2, 0.5);
            auto runGain = [&](bool gradual) {
                AlignedDelayLine e;
                check(c, e.prepare(cfg), "prepare failed");
                std::vector<float> out(n2, 0.0f);
                int done = 0, step = 0;
                while (done < n2) {
                    const int k = std::min(480, n2 - done);
                    e.process(in2.data() + done, out.data() + done, k);
                    done += k;
                    if (gradual) {
                        if (done >= 2400 && step < 20) {
                            e.setVoiceGain(3, 1.0 - 0.025 * (step + 1));
                            ++step;
                        }
                    } else if (done == 2400) {
                        e.setVoiceGain(3, 0.5);
                    }
                }
                return out;
            };
            const auto outG = runGain(true);
            const auto outJ = runGain(false);
            const int settled = 2400 + 19 * 480 + 720 + 64;  // last ramp done
            double steady = 0.0, trans = 0.0;
            for (int k = 0; k < n2; ++k) {
                const double d = std::abs((double)outG[k] - outJ[k]);
                if (k < settled) trans = std::max(trans, d);
                else steady = std::max(steady, d);
            }
            addValue(c, "gradual_vs_jump_transition_max_diff", trans);
            addValue(c, "gradual_vs_jump_steady_max_diff", steady);
            check(c, steady == 0.0,
                  "gradual and jumped gain differ in the final steady state");
            check(c, trans > 0.0,
                  "gradual and jumped gain were identical throughout "
                  "(vacuous comparison)");
        }
        c.pass = c.note.empty();
        R.push_back(std::move(c));
    }

    {  // 4. r15acc_reset_reprepare_determinism: a scripted event sequence
        //    (per-voice ratios + gains + mute/unmute + wet/dry moves +
        //    bypass enter/exit + base-delay change) -> full-run output hash;
        //    then reset() -> replay, prepare(same cfg) -> replay, destroy +
        //    fresh engine -> replay: all four must match BIT-EXACTLY.
        //    NOTE (semantics): reset() KEEPS the parameter values while
        //    prepare() restores the fresh-engine defaults, so the script is
        //    CLOSED -- it restores the neutral surface at t7 (setRatio()
        //    re-derives every voice target through the engine's own
        //    derivation path and clears the overrides). The closure is
        //    itself part of the gate: post-reset must be behaviourally
        //    identical to the fresh post-prepare state.
        CaseResult c{"r15acc_reset_reprepare_determinism"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;
        cfg.feedback = 0.25;
        const int n = 130048;  // 1024 * 127
        const auto in = gen::noise(4242, n, 0.5);
        const double semis[8] = {-12.0, -5.0, 0.0, 7.0,
                                 -3.0,  4.0, -7.0, 12.0};

        auto script = [&](AlignedDelayLine& e) {
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(1024, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                switch (done) {
                    case 16384:  // t1: per-voice ratios + gains
                        for (int j = 0; j < 8; ++j)
                            e.setVoiceRatio(j,
                                            std::pow(2.0, semis[j] / 12.0));
                        e.setVoiceGain(0, 0.4);
                        e.setVoiceGain(3, 0.7);
                        break;
                    case 32768: e.setVoiceMute(2, true); break;      // t2
                    case 49152: e.setWetDry(0.5); break;             // t3
                    case 65536: e.setBypass(true); break;            // t4
                    case 81920: e.setBypass(false); break;           // t5
                    case 98304: e.setVoiceBaseDelay(4, 1200.0); break;  // t6
                    case 114688:  // t7: restore the neutral surface
                        e.setRatio(cfg.ratio);  // clears every override
                        e.setVoiceMute(2, false);
                        e.setVoiceGain(0, 1.0);
                        e.setVoiceGain(3, 1.0);
                        e.setWetDry(1.0);
                        e.setVoiceBaseDelay(4, 256.0 + 32.0 * 4);
                        break;
                    default: break;
                }
            }
            return out;
        };

        AlignedDelayLine e;
        check(c, e.prepare(cfg), "prepare failed");
        const auto out1 = script(e);
        const uint64_t h1 = fnv1a64(out1);
        e.reset();
        const auto out2 = script(e);
        const uint64_t h2 = fnv1a64(out2);
        check(c, e.prepare(cfg), "re-prepare failed");  // R13-c in-place path
        const auto out3 = script(e);
        const uint64_t h3 = fnv1a64(out3);
        uint64_t h4 = 0;
        std::vector<float> out4;
        {
            AlignedDelayLine f;
            check(c, f.prepare(cfg), "fresh prepare failed");
            out4 = script(f);
            h4 = fnv1a64(out4);
        }
        std::printf("r15acc_reset_reprepare_determinism hashes: "
                    "run=0x%016llx reset=0x%016llx prepare=0x%016llx "
                    "fresh=0x%016llx\n",
                    static_cast<unsigned long long>(h1),
                    static_cast<unsigned long long>(h2),
                    static_cast<unsigned long long>(h3),
                    static_cast<unsigned long long>(h4));
        auto maxdiff = [](const std::vector<float>& a,
                          const std::vector<float>& b) {
            double d = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
                d = std::max(d, std::abs((double)a[i] - b[i]));
            return d;
        };
        const double dR = maxdiff(out1, out2);
        const double dP = maxdiff(out1, out3);
        const double dF = maxdiff(out1, out4);
        addValue(c, "reset_replay_max_diff", dR);
        addValue(c, "prepare_replay_max_diff", dP);
        addValue(c, "fresh_replay_max_diff", dF);
        addValue(c, "hashes_equal",
                 (h1 == h2 && h2 == h3 && h3 == h4) ? 1.0 : 0.0);
        check(c, dR == 0.0, "reset()+replay differs from the first run");
        check(c, dP == 0.0, "prepare()+replay differs from the first run");
        check(c, dF == 0.0, "fresh-engine replay differs from the first run");
        check(c, h1 == h2 && h2 == h3 && h3 == h4,
              "full-run output hashes differ");
        c.pass = c.note.empty();
        R.push_back(std::move(c));
    }

    {  // 5. r15acc_stereo_surface: MultichannelShifter 2ch with the full
        //    per-voice surface active (per-voice ratios + one voice muted +
        //    wet 0.5 + a bypass enter/exit mid-run + feedback). Broadcast
        //    parity: the same input on both channels -> identical outputs
        //    and identical per-voice voiceStats. Channel isolation: L_out
        //    with only-L input == L_out with L+R input, exactly.
        CaseResult c{"r15acc_stereo_surface"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;
        cfg.feedback = 0.25;
        const double semis[8] = {-12.0, -5.0, 0.0, 7.0,
                                 -3.0,  4.0, -7.0, 12.0};
        const int n = 147456;             // 1024 * 144 (3.072 s)
        const int t1 = 49152, t2 = 98304;  // bypass enter / exit
        const auto tin = gen::tone(220.0, sr, n, 0.5);
        const auto tR = gen::tone(330.0, sr, n, 0.3);

        auto runOn = [&](MultichannelShifter& m,
                         const std::vector<float>& inL,
                         const std::vector<float>& inR,
                         std::vector<float>& outL,
                         std::vector<float>& outR) {
            for (int j = 0; j < 8; ++j)
                m.setVoiceRatio(j, std::pow(2.0, semis[j] / 12.0));
            m.setVoiceMute(5, true);
            m.setWetDry(0.5);
            const float* in[2] = {inL.data(), inR.data()};
            float* out[2] = {outL.data(), outR.data()};
            int done = 0;
            while (done < n) {
                const int k = std::min(1024, n - done);
                m.process(in, out, 2, k);
                done += k;
                if (done == t1) m.setBypass(true);
                if (done == t2) m.setBypass(false);
            }
        };

        // broadcast parity
        {
            MultichannelShifter m;
            check(c, m.prepare(cfg, 2), "prepare failed");
            std::vector<float> Lo(n, 0.0f), Ro(n, 0.0f);
            runOn(m, tin, tin, Lo, Ro);
            double od = 0.0;
            for (int k = 0; k < n; ++k)
                od = std::max(od, std::abs((double)Lo[k] - Ro[k]));
            addValue(c, "broadcast_L_vs_R_max_diff", od);
            check(c, od == 0.0, "broadcast outputs differ between channels");
            int statsMismatch = 0;
            for (int v = 0; v < 8; ++v) {
                const VoiceStats a = m.voiceStats(0, v);
                const VoiceStats b = m.voiceStats(1, v);
                if (a.wraps != b.wraps || a.commits != b.commits ||
                    a.engaged != b.engaged || a.parked != b.parked ||
                    a.spacing != b.spacing || a.phase != b.phase ||
                    a.parked_delay != b.parked_delay || a.ratio != b.ratio)
                    ++statsMismatch;
            }
            addValue(c, "voicestats_mismatched_voices", statsMismatch);
            check(c, statsMismatch == 0, "voiceStats differ across channels");
        }

        // channel isolation with the full surface active
        {
            const std::vector<float> sil(n, 0.0f);
            std::vector<float> La(n, 0.0f), Ra(n, 0.0f);
            std::vector<float> Lb(n, 0.0f), Rb(n, 0.0f);
            {
                MultichannelShifter m;
                m.prepare(cfg, 2);
                runOn(m, tin, sil, La, Ra);
            }
            {
                MultichannelShifter m;
                m.prepare(cfg, 2);
                runOn(m, tin, tR, Lb, Rb);
            }
            double dL = 0.0;
            for (int k = 0; k < n; ++k)
                dL = std::max(dL, std::abs((double)La[k] - Lb[k]));
            addValue(c, "L_out_onlyL_vs_LplusR_max_diff", dL);
            check(c, dL == 0.0, "L output depends on the R input (leak)");
            const double rSil = meas::peak(Ra.data(), n);
            addValue(c, "R_out_peak_with_silent_R", rSil);
            check(c, rSil == 0.0,
                  "silent R input produced non-zero R output");
        }
        c.pass = c.note.empty();
        R.push_back(std::move(c));
    }

    {  // 6. r15acc_robustness_battery: 8 voices + the surface active
        //    (wet 0.5, voice 5 muted, one bypass enter/exit mid-run) across
        //    silence / -60 dBFS sine / 440 Hz tone / 3-tone chord / white
        //    noise / 100 Hz impulse train. Gates: all finite and bounded,
        //    silence in -> EXACTLY zero out, -60 dBFS spectral spurs <=
        //    -40 dB rel. output RMS outside the expected fundamentals, and
        //    bit-exact determinism across a replay.
        CaseResult c{"r15acc_robustness_battery"};
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);  // spread table ON: 8 intervals
        cfg.voices = 8;                    // around +400c
        const int n = 192000;              // 4 s
        const int t1 = 49152, t2 = 98304;  // bypass enter / exit

        auto runBatt = [&](const std::vector<float>& in,
                           std::vector<float>& out) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            e.setVoiceMute(5, true);
            e.setWetDry(0.5);
            int done = 0;
            while (done < n) {
                const int k = std::min(1024, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == t1) e.setBypass(true);
                if (done == t2) e.setBypass(false);
            }
        };

        // (a) digital silence: out must be EXACTLY 0 (wet 0.5 + bypass
        //     regions included: every path is exactly zero on zero input)
        {
            const std::vector<float> sil(n, 0.0f);
            std::vector<float> out(n, 0.0f);
            runBatt(sil, out);
            const double mx = meas::peak(out.data(), n);
            addValue(c, "silence_max_abs_out", mx);
            check(c, meas::allFinite(out.data(), n), "silence: non-finite");
            check(c, mx == 0.0, "silence in produced non-zero output");
        }

        // (b) -60 dBFS sine: finite + spur floor. The analysed stretch is
        //     the steady post-bypass-exit tail (65536 samples, Hann,
        //     amplitude = 4|X_k|/N). Expected components: the dry-path
        //     fundamental (the wet/dry mix legitimately carries it) and
        //     the 8 spread-table shifted fundamentals; guard +-10 bins
        //     each (covers the Hann leakage skirt); everything else must
        //     stay <= -40 dB relative to the output RMS. NOTE: single-bin
        //     amplitudes underestimate an OFF-BIN spur by up to 1.42 dB
        //     (Hann scalloping), so the gate is conservative in that
        //     direction; the measured margin is recorded.
        {
            const auto in = gen::tone(220.0, sr, n, 0.001);
            std::vector<float> out(n, 0.0f);
            runBatt(in, out);
            check(c, meas::allFinite(out.data(), n), "-60dB: non-finite");
            const int N = 65536;
            const int off = n - N;  // 126464 > t2+480: bypass fully off
            std::vector<double> re(N, 0.0), im(N, 0.0);
            for (int i = 0; i < N; ++i) {
                const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / N);
                re[i] = static_cast<double>(out[off + i]) * w;
            }
            meas::fft(re.data(), im.data(), N);
            const double stSemis[8] = {0.0,  7.0,  4.0,  12.0,
                                       -5.0, -12.0, 3.0, -3.0};
            double guard[9];
            guard[0] = 220.0;  // the dry-path fundamental (wet 0.5)
            for (int j = 0; j < 8; ++j)
                guard[j + 1] =
                    220.0 * std::pow(2.0, (4.0 + stSemis[j]) / 12.0);
            const double rmsOut = meas::rms(out.data() + off, N);
            double spur = 0.0;
            int spurBin = -1;
            for (int k = 1; k < N / 2; ++k) {
                bool g = false;
                for (int q = 0; q < 9 && !g; ++q) {
                    const double bk = guard[q] * N / static_cast<double>(sr);
                    if (std::abs(k - bk) <= 10.0) g = true;
                }
                if (g) continue;
                const double amp = 4.0 * std::sqrt(re[k] * re[k] +
                                                   im[k] * im[k]) / N;
                if (amp > spur) {
                    spur = amp;
                    spurBin = k;
                }
            }
            const double spurDb = 20.0 * std::log10(spur / rmsOut);
            addValue(c, "minus60_spur_db_rel_rms", spurDb);
            addValue(c, "minus60_spur_bin", spurBin);
            addValue(c, "minus60_spur_hz",
                     spurBin * static_cast<double>(sr) / N);
            check(c, spurDb <= -40.0,
                  "-60 dBFS spur floor above -40 dB rel. output RMS");
        }

        // (c) single 440 Hz tone, (d) 3-tone chord, (e) white noise,
        //     (f) 100 Hz impulse train: finite + bounded (+ energy in a
        //     documented window for the impulse train).
        {
            const auto in = gen::tone(440.0, sr, n, 0.5);
            std::vector<float> out(n, 0.0f);
            runBatt(in, out);
            const double pk = meas::peak(out.data(), n);
            addValue(c, "tone440_peak", pk);
            check(c, meas::allFinite(out.data(), n), "440 Hz: non-finite");
            check(c, pk <= 1.0, "440 Hz: peak above 2x input peak");
        }
        {
            const std::vector<double> fs = {220.0, 330.0, 440.0};
            const auto in = gen::chord(fs, sr, n, 0.5);
            std::vector<float> out(n, 0.0f);
            runBatt(in, out);
            const double pk = meas::peak(out.data(), n);
            const double inPk = meas::peak(in.data(), n);
            addValue(c, "chord_peak", pk);
            addValue(c, "chord_two_x_input_peak", 2.0 * inPk);
            check(c, meas::allFinite(out.data(), n), "chord: non-finite");
            check(c, pk <= 2.0 * inPk, "chord: peak above 2x input peak");
        }
        {
            const auto in = gen::noise(777, n, 0.5);
            std::vector<float> out(n, 0.0f);
            runBatt(in, out);
            const double pk = meas::peak(out.data(), n);
            addValue(c, "noise_peak", pk);
            check(c, meas::allFinite(out.data(), n), "noise: non-finite");
            check(c, pk <= 1.0, "noise: peak above 2x input peak");
        }
        {
            std::vector<float> in(n, 0.0f);  // 100 Hz impulse train
            for (int k = 0; k < n; k += 480) in[k] = 1.0f;
            std::vector<float> out(n, 0.0f);
            runBatt(in, out);
            const double pk = meas::peak(out.data(), n);
            double ein = 0.0, eout = 0.0;
            for (int k = 0; k < n; ++k) {
                ein += (double)in[k] * in[k];
                eout += (double)out[k] * out[k];
            }
            // GATE ANALYSIS: with 8 normalised voices the incoherent wet
            // mix legitimately carries ~1/8 of the input energy (each
            // voice at 1/8 amplitude) and the dry half adds its own 1/4
            // power share, so the honest robustness bound is a WINDOW,
            // not unity: no runaway growth (fb = 0) and no total collapse.
            // The literal ratio is recorded (expected ~ -5..-6 dB).
            const double ratioDb = 10.0 * std::log10(eout / ein);
            addValue(c, "impulsetrain_peak", pk);
            addValue(c, "impulsetrain_energy_ratio_db", ratioDb);
            check(c, meas::allFinite(out.data(), n),
                  "impulse train: non-finite");
            check(c, pk <= 2.0, "impulse train: peak above 2x input peak");
            check(c, ratioDb >= -12.0 && ratioDb <= 6.0,
                  "impulse-train energy ratio outside [-12, +6] dB");
        }

        // (g) determinism across a replay (fresh engine, same surface)
        {
            const auto in = gen::noise(777, n, 0.5);
            std::vector<float> o1(n, 0.0f), o2(n, 0.0f);
            runBatt(in, o1);
            runBatt(in, o2);
            double d = 0.0;
            for (int k = 0; k < n; ++k)
                d = std::max(d, std::abs((double)o1[k] - o2[k]));
            const bool heq = fnv1a64(o1) == fnv1a64(o2);
            addValue(c, "replay_max_diff", d);
            addValue(c, "replay_hash_equal", heq ? 1.0 : 0.0);
            check(c, d == 0.0 && heq, "replay is not bit-identical");
        }
        c.pass = c.note.empty();
        R.push_back(std::move(c));
    }

    {  // 7. r15acc_invalid_args: the invalid-argument matrix. Rejected
        //    calls (out-of-range voice index, non-positive ratio/gain,
        //    non-finite anything, base delay <= 0 or beyond the ring
        //    range) must be NO-OPS: a run with the invalid calls sprinkled
        //    between blocks is BIT-IDENTICAL to a run without them, and
        //    the engine stays usable afterwards. setWetDry CLAMPS to
        //    [0,1] per the header contract -- tested as identity with the
        //    clamped endpoint's own run.
        CaseResult c{"r15acc_invalid_args"};

        // (a) every entry point on an UNPREPARED engine: no crash, no
        //     effect, not ready.
        {
            AlignedDelayLine e;
            float buf[64] = {0.0f};
            e.setRatio(2.0);
            e.setVoices(4);
            e.setSpread(true);
            e.setFeedback(0.5);
            e.setVoiceRatio(0, 2.0);
            e.setVoiceGain(0, 0.5);
            e.setVoiceMute(0, true);
            e.setVoiceBaseDelay(0, 100.0);
            e.setWetDry(0.5);
            e.setBypass(true);
            e.process(buf, buf, 64);
            e.reset();
            (void)e.voiceStats(0);
            (void)e.lastEstimateHz();
            (void)e.latencySamples();
            check(c, !e.ready(), "unprepared engine reports ready");
            check(c, e.latencySamples() == 0,
                  "unprepared engine reports non-zero latency");
            MultichannelShifter m;
            m.setVoiceRatio(0, 2.0);
            m.setVoiceGain(0, 0.5);
            m.setVoiceMute(0, true);
            m.setVoiceBaseDelay(0, 100.0);
            m.setWetDry(0.5);
            m.setBypass(true);
            check(c, !m.ready(), "unprepared multichannel reports ready");
        }

        // (b) the rejection matrix on a prepared engine
        Config cfg;
        cfg.ratio = semitoneRatio(400.0);
        cfg.voices = 8;
        cfg.spread = false;
        const int n = 48000;
        const auto in = gen::tone(220.0, sr, n, 0.5);
        auto runMatrix = [&](bool withInvalid) {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(512, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == 5120) {  // valid surface change in BOTH runs
                    e.setVoiceRatio(3, std::pow(2.0, 5.0 / 12.0));
                    e.setVoiceGain(2, 0.5);
                    e.setVoiceBaseDelay(4, 700.0);
                    e.setWetDry(0.6);
                }
                if (withInvalid && done == 10240) {
                    // indices
                    e.setVoiceRatio(-1, 2.0);
                    e.setVoiceRatio(8, 2.0);
                    e.setVoiceGain(-1, 0.5);
                    e.setVoiceGain(8, 0.5);
                    e.setVoiceMute(-1, true);
                    e.setVoiceMute(8, true);
                    e.setVoiceBaseDelay(-1, 100.0);
                    e.setVoiceBaseDelay(8, 100.0);
                    // values (must not clobber live overrides either)
                    e.setVoiceRatio(3, 0.0);
                    e.setVoiceRatio(3, -1.5);
                    e.setVoiceRatio(3, kNaN);
                    e.setVoiceRatio(3, kInf);
                    e.setVoiceGain(2, -0.25);
                    e.setVoiceGain(2, kNaN);
                    e.setVoiceBaseDelay(4, 0.0);
                    e.setVoiceBaseDelay(4, -3.0);
                    e.setVoiceBaseDelay(4, kNaN);
                    e.setVoiceBaseDelay(4, 249848.0);  // ring-range edge
                    e.setVoiceBaseDelay(4, 300000.0);
                    e.setWetDry(kNaN);
                }
                if (withInvalid && done == 15360) {
                    e.setVoiceRatio(-2, -0.0);
                    e.setVoiceGain(2, -1e-9);
                    e.setVoiceBaseDelay(4, 262144.0);
                }
            }
            return out;
        };
        const auto outRef = runMatrix(false);
        const auto outInv = runMatrix(true);
        double dInv = 0.0;
        for (int k = 0; k < n; ++k)
            dInv = std::max(dInv, std::abs((double)outRef[k] - outInv[k]));
        addValue(c, "rejected_calls_output_max_diff", dInv);
        check(c, dInv == 0.0,
              "rejected calls changed the engine state or output");

        // engine still usable after the matrix: finite, non-silent output
        {
            AlignedDelayLine e;
            check(c, e.prepare(cfg), "prepare failed");
            std::vector<float> out(n, 0.0f);
            int done = 0;
            while (done < n) {
                const int k = std::min(512, n - done);
                e.process(in.data() + done, out.data() + done, k);
                done += k;
                if (done == 5120) {
                    e.setVoiceRatio(3, std::pow(2.0, 5.0 / 12.0));
                    e.setVoiceGain(2, 0.5);
                    e.setVoiceBaseDelay(4, 700.0);
                    e.setWetDry(0.6);
                }
                if (done == 10240 || done == 15360) {
                    e.setVoiceRatio(8, 2.0);
                    e.setVoiceGain(-1, -1.0);
                    e.setVoiceBaseDelay(4, 1e9);
                    e.setWetDry(kNaN);
                    e.setVoiceMute(99, true);
                }
            }
            check(c, meas::allFinite(out.data(), n),
                  "non-finite output after rejected calls");
            check(c, meas::peak(out.data(), n) > 0.0,
                  "engine silent after rejected calls");
        }

        // (c) setWetDry CLAMPS to [0,1] (the header contract): 1.7 == 1.0
        //     run, -0.3 == 0.0 run == the bit-exact delayed copy.
        {
            auto runWet = [&](double wet) {
                AlignedDelayLine e;
                e.prepare(cfg);
                e.setWetDry(wet);
                std::vector<float> out(n, 0.0f);
                e.process(in.data(), out.data(), n);
                return out;
            };
            const auto oHi = runWet(1.7);
            const auto oOne = runWet(1.0);
            double dHi = 0.0;
            for (int k = 0; k < n; ++k)
                dHi = std::max(dHi, std::abs((double)oHi[k] - oOne[k]));
            addValue(c, "wet_1p7_vs_1p0_max_diff", dHi);
            check(c, dHi == 0.0, "setWetDry(1.7) is not clamped to 1.0");

            const auto oLo = runWet(-0.3);
            AlignedDelayLine probe;
            probe.prepare(cfg);
            const int L = probe.latencySamples();
            double dLo = 0.0;
            for (int k = 0; k < n; ++k) {
                const int src = k - (L - 1);
                const double expect = (src >= 0) ? (double)in[src] : 0.0;
                dLo = std::max(dLo, std::abs((double)oLo[k] - expect));
            }
            addValue(c, "wet_m0p3_vs_delayed_copy_max_diff", dLo);
            check(c, dLo == 0.0,
                  "setWetDry(-0.3) is not clamped to the bit-exact dry "
                  "copy");
        }

        // (d) boundary sanity: the largest IN-RANGE base delay IS accepted
        //     (delay + 3*grain + 8 < 2^18): 249847 changes the output,
        //     249848 (one further) is rejected (covered above).
        {
            auto runDelay = [&](double d) {
                AlignedDelayLine e;
                e.prepare(cfg);
                std::vector<float> out(n, 0.0f);
                int done = 0;
                while (done < n) {
                    const int k = std::min(512, n - done);
                    e.process(in.data() + done, out.data() + done, k);
                    done += k;
                    if (done == 5120) e.setVoiceBaseDelay(4, d);
                }
                return out;
            };
            const auto oA = runDelay(700.0);
            const auto oB = runDelay(249847.0);
            double d = 0.0;
            for (int k = 5120; k < n; ++k)
                d = std::max(d, std::abs((double)oA[k] - oB[k]));
            addValue(c, "largest_valid_delay_effect", d);
            check(c, d > 0.0,
                  "the largest in-range base delay was not accepted");
        }
        c.pass = c.note.empty();
        R.push_back(std::move(c));
    }
}
