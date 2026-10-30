// OCTAVOX round 12 -- native WAV->WAV demo CLI (harness utility).
// Mirrors the Python v2 CLI surface (poc/octavox_v2.py): reads PCM WAV
// (16/24/32-bit, mono..N channels), processes EVERY CHANNEL with a FRESH
// engine (channel isolation), writes 16-bit PCM. Measures OUR implementation
// only. Use --json for machine-readable output.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "octavox/dsp.hpp"
#include "wav_io.hpp"

using namespace octavox;

static void usage() {
    std::printf(
        "usage: octavox_cli --in IN.wav --out OUT.wav [--cents C] [--voices N]\n"
        "                   [--no-spread] [--interp lagrange6|lagrange6_q|linear|obs6]\n"
        "                   [--json OUT.json]\n"
        "defaults: cents=0 voices=1 interp=lagrange6 spread=on\n");
}

static double nowSec() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    std::string inPath, outPath, jsonPath, interp = "lagrange6";
    double cents = 0.0;
    int voices = 1;
    bool spread = true;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--in") inPath = next();
        else if (a == "--out") outPath = next();
        else if (a == "--cents") cents = std::atof(next().c_str());
        else if (a == "--voices") voices = std::atoi(next().c_str());
        else if (a == "--interp") interp = next();
        else if (a == "--no-spread") spread = false;
        else if (a == "--json") jsonPath = next();
        else {
            usage();
            return 2;
        }
    }
    if (inPath.empty() || outPath.empty()) {
        usage();
        return 2;
    }

    int sr = 0, ch = 0;
    std::vector<float> x;
    try {
        x = wav::readWav(inPath, sr, ch);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "read error: %s\n", ex.what());
        return 1;
    }

    Config cfg;
    cfg.sample_rate = static_cast<double>(sr);
    cfg.ratio = std::pow(2.0, cents / 1200.0);
    cfg.voices = voices;
    cfg.spread = spread;
    if (interp == "linear") cfg.interpolation = Interpolation::Linear;
    else if (interp == "lagrange6_q") cfg.interpolation = Interpolation::Lagrange6Q;
    else if (interp == "obs6") cfg.interpolation = Interpolation::ObsKernel6;
    else cfg.interpolation = Interpolation::Lagrange6;

    const int n = static_cast<int>(x.size()) / ch;

    // planar split (x is interleaved); fresh engine per channel (isolation)
    std::vector<std::vector<float>> inP(ch), outP(ch);
    for (int c = 0; c < ch; ++c) {
        inP[c].assign(n, 0.0f);
        for (int i = 0; i < n; ++i) inP[c][i] = x[static_cast<size_t>(i) * ch + c];
        outP[c].assign(n, 0.0f);
    }
    std::vector<const float*> inPP(ch);
    std::vector<float*> outPP(ch);
    for (int c = 0; c < ch; ++c) {
        inPP[c] = inP[c].data();
        outPP[c] = outP[c].data();
    }

    MultichannelShifter m;
    if (!m.prepare(cfg, ch)) {
        std::fprintf(stderr, "engine prepare failed\n");
        return 1;
    }
    const double t0 = nowSec();
    m.process(inPP.data(), outPP.data(), ch, n);
    const double el = nowSec() - t0;

    // interleave + write
    std::vector<float> y(static_cast<size_t>(n) * ch);
    for (int c = 0; c < ch; ++c)
        for (int i = 0; i < n; ++i) y[i * ch + c] = outP[c][i];
    wav::writeWav(outPath, y.data(), n, ch, sr);

    double peak = 0.0;
    for (float v : y) peak = std::max(peak, std::abs((double)v));
    std::printf(
        "{\"demo\": \"octavox_native_cli\", \"version\": \"%s\", \"in\": \"%s\", "
        "\"out\": \"%s\", \"sr\": %d, \"channels\": %d, \"in_samples\": %d, "
        "\"cents\": %.6f, \"voices\": %d, \"interp\": \"%s\", \"spread\": %s, "
        "\"cpu_s\": %.6f, \"x_realtime\": %.4f, \"peak\": %.6f}\n",
        versionString(), inPath.c_str(), outPath.c_str(), sr, ch, n, cents,
        voices, interp.c_str(), spread ? "true" : "false", el,
        (n / (double)sr) / (el > 1e-9 ? el : 1e-9), peak);
    if (!jsonPath.empty()) {
        FILE* f = std::fopen(jsonPath.c_str(), "wb");
        if (f) {
            std::fprintf(
                f,
                "{\"demo\": \"octavox_native_cli\", \"version\": \"%s\", "
                "\"sr\": %d, \"channels\": %d, \"in_samples\": %d, "
                "\"cents\": %.6f, \"voices\": %d, \"interp\": \"%s\", "
                "\"cpu_s\": %.6f, \"peak\": %.6f}\n",
                versionString(), sr, ch, n, cents, voices, interp.c_str(), el,
                peak);
            std::fclose(f);
        }
    }
    return 0;
}
