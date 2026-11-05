// PITCHLAB R23 -- canonical YIN reference implementation (Gate 4).
// ============================================================================
// Test-only reference driver: validates the shared core (cli/r23_yin_core.h)
// against independently computed expected vectors and boundary cases.
// See the core header for the variant documentation.
#include "r23_yin_core.h"

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else { std::fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    std::string json = "[\n";
    bool first = true;
    auto addJson = [&](const std::string& s) {
        if (!first) json += ",\n";
        first = false;
        json += s;
    };
    const double sr = 48000.0;
    std::printf("== R23 canonical YIN reference (test-only) ==\n");

    // -- 1. expected-vector validation (variant A) -------------------------
    {
        std::vector<double> d, cm;
        diffFixedW(kX64, 0, 16, 1, 20, d);
        bool ok = true;
        for (int i = 0; i < 20; ++i)
            ok &= std::abs(d[static_cast<size_t>(i)] - kDExpected[i]) < 2e-9;
        check(ok, "A: fixed-W difference vs independent vector");
        std::vector<double> dFull, cmFull;
        diffFixedW(kX64, 0, 16, 1, 20, dFull);
        cmndfCanonical(dFull, cmFull);
        ok = true;
        for (int i = 0; i < 20; ++i)
            ok &= std::abs(cmFull[static_cast<size_t>(i)] - kCmExpected[i]) < 2e-9;
        check(ok, "A: canonical CMNDF vs independent vector");
        addJson("{\"case\": \"vector_A_fixedW\", \"d_ok\": " + std::string(ok ? "true" : "false") + "}");
    }
    // -- 2. expected-vector validation (variant B) -------------------------
    {
        std::vector<double> dB, cmB;
        diffShrinking(kX64, 24, 1, 20, dB);
        bool ok = true;
        for (int i = 0; i < 20; ++i)
            ok &= std::abs(dB[static_cast<size_t>(i)] - kDBExpected[i]) < 2e-9;
        check(ok, "B: shrinking difference vs independent vector");
        cmndfCanonical(dB, cmB);
        ok = true;
        for (int i = 0; i < 20; ++i)
            ok &= std::abs(cmB[static_cast<size_t>(i)] - kCmBExpected[i]) < 2e-9;
        check(ok, "B: canonical CMNDF vs independent vector");
        addJson("{\"case\": \"vector_B_shrink\"}");
    }
    // -- 3. FFT equivalence proof (variant D vs A, deterministic + random) --
    {
        const int W = 512, tauMin = 2, tauMax = 1200;
        const int n = W + tauMax + 64;  // frames at t <= 64 stay in bounds
        std::vector<double> x(static_cast<size_t>(n));
        Lcg rng(4242);
        double maxRel = 0.0;
        for (int trial = 0; trial < 4; ++trial) {
            if (trial == 0) {  // deterministic sine 110 Hz + harmonics
                for (int i = 0; i < n; ++i) {
                    double p = 2.0 * M_PI * 110.0 * i / sr;
                    x[static_cast<size_t>(i)] =
                        std::sin(p) + 0.5 * std::sin(2 * p) + 0.25 * std::sin(3 * p);
                }
            } else {
                for (int i = 0; i < n; ++i)
                    x[static_cast<size_t>(i)] = rng.next();
            }
            std::vector<double> sq(static_cast<size_t>(n) + 1, 0.0);
            for (int i = 0; i < n; ++i)
                sq[static_cast<size_t>(i) + 1] = sq[static_cast<size_t>(i)] +
                                                 x[static_cast<size_t>(i)] * x[static_cast<size_t>(i)];
            for (int t : {0, 17, 64}) {
                std::vector<double> dDir, dFft;
                diffFixedW(x.data(), t, W, tauMin, tauMax, dDir);
                diffFixedWFft(x.data(), t, W, tauMin, tauMax, sq, dFft);
                for (size_t i = 0; i < dDir.size(); ++i) {
                    const double rel = std::abs(dFft[i] - dDir[i]) /
                                       std::max(1e-300, std::abs(dDir[i]));
                    maxRel = std::max(maxRel, rel);
                }
            }
        }
        check(maxRel < 1e-8, "D: FFT difference == direct (max rel err)", maxRel, 1e-8);
        char b[160];
        std::snprintf(b, sizeof(b), "{\"case\": \"fft_equivalence\", \"max_rel_err\": %.3e}", maxRel);
        addJson(b);
    }
    // -- 4. boundary tests (brief §3.4/§3.7) -------------------------------
    {
        // 4a. flat minima / constant DC: d(tau) == 0 for every lag (the
        // difference function cancels DC by construction), the canonical
        // CMNDF is identically 1.0, no local minimum exists below any
        // threshold and the paper's fallback returns the global minimum =
        // the first index -> the DEGENERATE tau_min estimate. Recorded as
        // documented degenerate behaviour (not an error); the musical fix
        // is preconditioning (DC removal), studied in the R23 research doc.
        std::vector<double> flat(4096, 0.25);
        YinResult rA = yinPaperFixedW(flat.data(), 4096, 0, 1024, 40, 1200,
                                      0.10, sr, true);
        check(rA.fallback_global && !rA.threshold_accepted &&
              rA.tau_int == 40,
              "flat/DC: degenerate tau_min fallback (documented)",
              rA.tau_int, 40);
        // 4a2. DC cancellation property: DC 0.5 + 220 Hz -> 220 Hz estimate
        std::vector<double> dcac(8192);
        for (int i = 0; i < 8192; ++i)
            dcac[static_cast<size_t>(i)] =
                0.5 + 0.4 * std::sin(2.0 * M_PI * 220.0 * i / sr);
        YinResult rA2 = yinPaperFixedW(dcac.data(), 8192, 0, 2048, 40, 1200,
                                       0.10, sr, true);
        check(rA2.valid && std::abs(rA2.f0 - 220.0) < 0.05,
              "DC+AC: difference function cancels DC (estimates the AC f0)",
              rA2.f0, 220.0);
        // 4b. silence
        std::vector<double> sil(4096, 0.0);
        YinResult rB = yinPaperFixedW(sil.data(), 4096, 0, 1024, 40, 1200,
                                      0.10, sr, true);
        check(!rB.valid && rB.reason == 1, "silence: zero-energy guard", rB.reason, 1);
        // 4c. non-finite input never propagates
        std::vector<double> nanx(4096, 0.0);
        for (int i = 2000; i < 2064; ++i) nanx[static_cast<size_t>(i)] = std::nan("");
        YinResult rC = yinPaperFixedW(nanx.data(), 4096, 0, 1024, 40, 1200,
                                      0.10, sr, true);
        check(rC.reason == 4 && !rC.valid, "NaN input: reason=4, no propagation",
              rC.reason, 4);
        std::vector<double> infx(4096, 0.0);
        infx[100] = HUGE_VAL;
        YinResult rC2 = yinPaperFixedW(infx.data(), 4096, 0, 1024, 40, 1200,
                                       0.10, sr, true);
        check(rC2.reason == 4 && !rC2.valid, "Inf input: reason=4", rC2.reason, 4);
        // 4d. near-zero cumulative denominator: tiny signal (1e-12 amplitude)
        std::vector<double> tiny(4096);
        for (int i = 0; i < 4096; ++i)
            tiny[static_cast<size_t>(i)] = 1e-12 * std::sin(2 * M_PI * 220.0 * i / sr);
        YinResult rD = yinPaperFixedW(tiny.data(), 4096, 0, 1024, 40, 1200,
                                      0.10, sr, true);
        check(rD.valid && std::abs(rD.f0 - 220.0) < 1.0,
              "tiny (1e-12) signal still estimates (CMNDF scale-free)",
              rD.f0, 220.0);
        // 4e. boundary minimum: strong 40 Hz (tau = 1200 = tau_max)
        std::vector<double> s40(8192);
        for (int i = 0; i < 8192; ++i)
            s40[static_cast<size_t>(i)] =
                0.5 * std::sin(2 * M_PI * 40.0 * i / sr) +
                0.2 * std::sin(2 * M_PI * 80.0 * i / sr);
        YinResult rE = yinPaperFixedW(s40.data(), 8192, 0, 2048, 40, 1200,
                                      0.10, sr, true);
        check(rE.valid && std::abs(rE.tau_int - 1200) <= 1,
              "boundary minimum at tau_max handled", rE.tau_int, 1200);
        addJson("{\"case\": \"boundary_tests\", \"flat\": 1, \"silence\": 1, "
                "\"nan\": 1, \"inf\": 1, \"tiny\": 1, \"tau_boundary\": 1}");
    }
    // -- 5. accuracy table: variants on canonical tones --------------------
    {
        const double tones[] = {55.0, 110.0, 220.0, 440.0, 880.0};
        const int W = 2048, tauMin = 40, tauMax = 1200;
        const int winL = 3000;  // legacy law: max(2048, ceil(2.5*1200))
        const int n = W + tauMax + 64;  // the paper frame needs W+tauMax support
        for (double f : tones) {
            std::vector<double> x(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) {
                const double p = 2.0 * M_PI * f * i / sr;
                x[static_cast<size_t>(i)] =
                    std::sin(p) + 0.5 * std::sin(2 * p) + 0.33 * std::sin(3 * p) +
                    0.25 * std::sin(4 * p);
            }
            // A/B frames END at sample n (the same "now" as the engine's
            // rolling window); the legacy window is the LAST winL samples.
            const YinResult aRaw =
                yinPaperFixedW(x.data(), n, n - W - tauMax, W, tauMin, tauMax,
                               0.10, sr, true);
            const YinResult aCm =
                yinPaperFixedW(x.data(), n, n - W - tauMax, W, tauMin, tauMax,
                               0.10, sr, false);
            const YinResult b =
                yinLibrosaShrink(x.data() + n - W, W, tauMin, tauMax, 0.10, sr);
            const int seg0L = winL / 2;
            const YinResult c =
                yinLegacyR22(x.data() + n - winL, winL, 40, 40, 1200, 0.20, sr);
            (void)seg0L;
            std::printf("tone %6.1f Hz: A(rawD) %+7.2fc  A(cmndf) %+7.2fc  "
                        "B(shrink) %+7.2fc  C(legacy) %+7.2fc  "
                        "[cmndfA=%.3f cmndfC=%.3f]\n",
                        f, 1200.0 * std::log2(aRaw.f0 / f),
                        1200.0 * std::log2(aCm.f0 / f),
                        1200.0 * std::log2(b.f0 / f),
                        1200.0 * std::log2(c.f0 / f), aRaw.cmndf_min, c.cmndf_min);
            char js[640];
            std::snprintf(js, sizeof(js),
                          "{\"case\": \"tone_%.0f\", \"A_rawD\": %.4f, "
                          "\"A_cmndf\": %.4f, \"B_shrink\": %.4f, "
                          "\"C_legacy\": %.4f, \"cmndf_A\": %.4f, \"cmndf_C\": %.4f}",
                          f, 1200.0 * std::log2(aRaw.f0 / f),
                          1200.0 * std::log2(aCm.f0 / f),
                          1200.0 * std::log2(b.f0 / f),
                          1200.0 * std::log2(c.f0 / f), aRaw.cmndf_min, c.cmndf_min);
            addJson(js);
        }
    }
    // -- 6. Step-6 offline demo (noisy 220 Hz + jittered frames) -----------
    {
        const int W = 2048, tauMin = 40, tauMax = 1200, hop = 256, n = 12 * 2048;
        std::vector<double> x(static_cast<size_t>(n));
        Lcg rng(31337);
        for (int i = 0; i < n; ++i) {
            const double p = 2.0 * M_PI * 220.0 * i / sr;
            x[static_cast<size_t>(i)] = std::sin(p) + 0.4 * std::sin(2 * p) +
                                        0.2 * std::sin(3 * p) + 0.05 * rng.next();
        }
        std::vector<YinResult> s5, s6;
        for (int t = 0; t + W + tauMax <= n; t += hop)
            s5.push_back(yinPaperFixedW(x.data(), n, t, W, tauMin, tauMax, 0.10,
                                        sr, true));
        s6 = yinPaperStep6Offline(x.data(), n, hop, W, tauMin, tauMax, 0.10, sr);
        auto centsErr = [&](const std::vector<YinResult>& v) {
            std::vector<double> e;
            for (const auto& r : v)
                if (r.valid) e.push_back(1200.0 * std::log2(r.f0 / 220.0));
            std::sort(e.begin(), e.end());
            return std::make_pair(e.empty() ? 0.0 : e[e.size() / 2],
                                  e.empty() ? 0.0 : e[e.size() * 95 / 100]);
        };
        const auto e5 = centsErr(s5), e6 = centsErr(s6);
        std::printf("Step6 demo (220 Hz + noise): step5 median %+.3fc p95 %+.3fc | "
                    "step6 median %+.3fc p95 %+.3fc\n",
                    e5.first, e5.second, e6.first, e6.second);
        char js[320];
        std::snprintf(js, sizeof(js),
                      "{\"case\": \"step6_demo\", \"s5_med\": %.4f, \"s5_p95\": %.4f, "
                      "\"s6_med\": %.4f, \"s6_p95\": %.4f}",
                      e5.first, e5.second, e6.first, e6.second);
        addJson(js);
    }

    json += "\n]\n";
    std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (!f) { std::fprintf(stderr, "cannot write %s\n", jsonPath); return 1; }
        std::fwrite(json.data(), 1, json.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "json written: %s\n", jsonPath);
    }
    return g_fails == 0 ? 0 : 1;
}
