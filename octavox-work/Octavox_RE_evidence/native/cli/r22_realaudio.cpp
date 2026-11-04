// PITCHLAB R22 -- the real-audio detector comparison harness (PRIORITY 2).
// ============================================================================
// PROVENANCE: our own tool (round 22), extending R21's octavox_r21_realaudio
// (which remains preserved in the evidence tree). It measures OUR independent
// engine's three pitch-detector architectures (FullRate / Decimated /
// Hybrid) on IDENTICAL audio and settings, with an explicit CATEGORY
// discipline and full processed-audio comparison.
//
// CATEGORY DISCIPLINE (the R22 contract):
//   * Every input is labelled with one of:
//       synthetic                 honestly-labelled synthetic material
//                                 (the R20 corpora family / --demo-synthetic)
//       recorded:<class>          genuine recorded material, no reference
//       annotated:<class>         genuine recorded material + a reference
//                                 trajectory (manually annotated or supplied
//                                 externally) -- the ONLY category whose
//                                 cent numbers are ground-truth accuracy
//   * <class> is one of the recommended real test set's classes:
//       male_singing female_singing spoken bass acoustic_guitar
//       electric_guitar sustained vibrato portamento breath_noise
//       polyphonic other
//     An unknown class is a hard error (exit 2) -- mislabelled material is
//     worse than no material.
//   * The manifest declares one entry per line:
//       path,category[,annotation_csv[,label]]
//     (--input/--category still work for the single-file convenience case.)
//
// HONESTY CONTRACT (unchanged from R21, restated):
//   * Without a reference trajectory the cent numbers are INTER-MODE
//     AGREEMENT metrics (vs the FullRate reference mode), never accuracy.
//   * --annotate / manifest annotation columns supply true references:
//     rows  t_start_s,t_end_s,f0_hz  (spans; per-entry).
//   * The tool never fabricates a reference.
//
// PER ENTRY x MODE (identical audio + settings) recorded:
//   * raw estimate trajectory + a median-5 SMOOTHED trajectory (CSV)
//   * voicing ratio (estimate != 0 over the voiced-ish mask -- our
//     detector's only confidence output, stated as such)
//   * median/max cent difference vs reference (annotation when present)
//   * pitch TRANSITION events (|delta cents| > 100 between consecutive
//     voiced estimates): count + median magnitude + first 8 timestamps
//   * octave-disagreement ratio, dropout ratio, jitter (R21 laws)
//   * transient recovery, relative lock latency (R21 laws)
//   * CPU (ns/input sample) + steady-state ALLOCATION COUNT (the global
//     new/delete override from the core's harness; 0 expected after
//     prepare -- the R12 steady-state contract, now measured per mode)
//   * output discontinuity scan (max wet step)
//   * OUTPUT CROSS-MODE COMPARISON: RMS of the wet-output difference vs
//     the FullRate mode's output (the processed-audio agreement metric --
//     the spec's "compare the final processed audio, not just estimates")
//   * final offline output WAV per mode (--outdir; 32-bit float, the
//     deterministic R19 null-test format)
//   * STEREO: the engine runs at the file's channel count (per-channel
//     analysis voices); per-channel metrics are recorded for channel 0
//     and the L/R ESTIMATE DIVERGENCE (median |cents(L) - cents(R)| over
//     voiced, both-nonzero boundaries) is recorded for stereo files.
//
// DEFAULTS: Lagrange6 interpolation (the default; --interpolation
// obskernel6 selects the research kernel). The default detector is NOT
// changed by this tool's evidence (the R19-R22 default stays Decimated;
// the comparison is the deliverable, not a default switch).
//
// --demo-synthetic: generates the three R21-family synthetic WAVs under
// --outdir (labelled category "synthetic") and runs the full comparison --
// the workflow demonstration on honestly-labelled material (the sandbox
// holds no real recordings; synthetic demo results are NEVER presented as
// real-recording quality).
//
// Usage:
//   octavox_r22_realaudio --manifest <csv> [--outdir <dir>] [--json <path>]
//       [--trajdir <dir>] [--ratio <st>] [--maxseconds <s>]
//       [--interpolation lagrange6|obskernel6]
//   octavox_r22_realaudio --input <a.wav> --category recorded:bass ...
//   octavox_r22_realaudio --demo-synthetic --outdir <dir> [--json <path>] ...
// ============================================================================
#include "octavox/dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "../tests/wav_io.hpp"

// The core harness's allocation counter (tests/alloc_counter.cpp, linked
// into this tool for the steady-state memory metric). FILE-scope so it
// declares octavox::allocs (not a nested shadow inside the anon namespace).
namespace octavox {
namespace allocs {
long count();
void arm(bool on);
void reset();
}  // namespace allocs
}  // namespace octavox

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

// The category discipline ---------------------------------------------------

const char* const kClasses[] = {
    "male_singing", "female_singing", "spoken",     "bass",
    "acoustic_guitar", "electric_guitar", "sustained", "vibrato",
    "portamento",   "breath_noise",     "polyphonic", "other"};

bool validClass(const std::string& s) {
    for (const char* k : kClasses)
        if (s == k) return true;
    return false;
}

struct Entry {
    std::string path;
    std::string category;     // "synthetic" | "recorded:<class>" | "annotated:<class>"
    std::string annotation;  // optional csv path
    std::string label;       // optional free label
};

bool validCategory(const std::string& c) {
    if (c == "synthetic") return true;
    for (const char* pre : {"recorded:", "annotated:"}) {
        const size_t pl = std::strlen(pre);
        if (c.compare(0, pl, pre) == 0) return validClass(c.substr(pl));
    }
    return false;
}

// Annotations ---------------------------------------------------------------

struct Span {
    double t0, t1, f0;
};

std::vector<Span> loadSpans(const std::string& path) {
    std::vector<Span> spans;
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return spans;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        double t0, t1, hz;
        if (std::sscanf(line, "%lf,%lf,%lf", &t0, &t1, &hz) == 3 &&
            t1 > t0 && hz > 0.0)
            spans.push_back({t0, t1, hz});
    }
    std::fclose(f);
    return spans;
}

// LCG + synthetic demo material (the R21 family; SYNTHETIC, labelled) -------

struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed ? seed : 1) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int32_t>(uint32_t(s >> 33)) * (1.0 / 2147483648.0);
    }
};

void writeSyntheticDemo(const std::string& dir) {
    // demo 1: "voice_male-ish" -- 110 Hz harmonic stack + vibrato + jitter
    {
        const int sr = 48000, n = sr * 6;
        std::vector<float> x(n);
        Lcg rng(42001);
        double phase = 0.0;
        for (int i = 0; i < n; ++i) {
            const double t = i / double(sr);
            const double vib = 25.0 * std::sin(2.0 * M_PI * 5.5 * t);  // cents
            const double f = 110.0 * std::pow(2.0, vib / 1200.0);
            phase += 2.0 * M_PI * f / sr;
            double a = 0.0;
            for (int k = 1; k <= 8; ++k) a += (1.0 / k) * std::sin(k * phase);
            x[i] = float(0.5 * a / 8.0 + 0.002 * rng.next());
        }
        writeWavF32(dir + "/demo_syn_voice_110.wav", x.data(), n, 1, sr);
    }
    // demo 2: "bass-ish" -- 55 Hz stack, slow portamento 55 -> 62
    {
        const int sr = 48000, n = sr * 6;
        std::vector<float> x(n);
        Lcg rng(42002);
        double phase = 0.0;
        for (int i = 0; i < n; ++i) {
            const double t = i / double(sr);
            const double f = 55.0 * std::pow(62.0 / 55.0, t / 6.0);
            phase += 2.0 * M_PI * f / sr;
            double a = 0.0;
            for (int k = 1; k <= 10; ++k) a += (1.0 / k) * std::sin(k * phase);
            x[i] = float(0.5 * a / 10.0 + 0.0015 * rng.next());
        }
        writeWavF32(dir + "/demo_syn_bass_55.wav", x.data(), n, 1, sr);
    }
    // demo 3: "speech-like" -- 140 -> 200 -> 120 Hz steps + breath gaps
    {
        const int sr = 48000, n = sr * 6;
        std::vector<float> x(n);
        Lcg rng(42003);
        double phase = 0.0;
        for (int i = 0; i < n; ++i) {
            const double t = i / double(sr);
            double f = 140.0;
            if (t > 2.0 && t <= 2.25) f = 0.0;          // breath gap
            else if (t > 2.25 && t <= 4.0) f = 200.0;
            else if (t > 4.0 && t <= 4.4) f = 0.0;      // unvoiced gap
            else if (t > 4.4) f = 120.0;
            if (f <= 0.0) {
                x[i] = float(0.05 * rng.next());        // breath noise
                continue;
            }
            phase += 2.0 * M_PI * f / sr;
            double a = 0.0;
            for (int k = 1; k <= 6; ++k) a += (1.0 / k) * std::sin(k * phase);
            x[i] = float(0.45 * a / 6.0 + 0.003 * rng.next());
        }
        writeWavF32(dir + "/demo_syn_speech_140.wav", x.data(), n, 1, sr);
    }
}

// The per-mode render ---------------------------------------------------------
// (allocs::* prototypes: the core harness's allocation counter, defined in
// tests/alloc_counter.cpp -- linked into this tool for the memory metric.)
struct RenderResult {
    std::vector<std::vector<double>> est;   // per channel, per control boundary
    std::vector<std::vector<float>> out;    // per channel, wet samples
    double cpuNsPerSample = 0.0;
    long renderAllocs = 0;
};

// One pass over the interleaved input with the MULTICHANNEL engine (the
// file's own channel count -- stereo files are processed as stereo).
// Estimates are harvested at every 64-sample control boundary per channel
// (MultichannelShifter::lastEstimateHz(c)). CPU is timed over the render;
// the armed allocation counter measures the steady-state render (prepare
// happens BEFORE arming -- its allocations are by-design and excluded).
RenderResult renderMode(const std::vector<float>& interleaved, int sr,
                        int channels, double ratio, octavox::DetectorMode mode,
                        octavox::Interpolation interp) {
    using octavox::Config;
    using octavox::MultichannelShifter;
    const size_t nframes = interleaved.size() / channels;
    RenderResult rr;
    rr.est.resize(channels);
    rr.out.resize(channels);

    MultichannelShifter eng;
    Config cfg;
    cfg.sample_rate = sr;
    cfg.ratio = ratio;
    cfg.detector_mode = mode;
    cfg.interpolation = interp;
    if (!eng.prepare(cfg, channels)) {
        std::fprintf(stderr, "prepare failed\n");
        std::exit(1);
    }
    const int cadence = 64;
    const int nCtrl = static_cast<int>(nframes) / cadence;
    for (int c = 0; c < channels; ++c) {
        rr.est[c].assign(nCtrl, 0.0);
        rr.out[c].assign(nframes, 0.0f);
    }

    // planar staging (interleaved -> per-channel; the planar INPUT is
    // read-only for the engine; outputs go to rr.out)
    std::vector<std::vector<float>> planarIn(channels,
                                             std::vector<float>(nframes));
    for (size_t i = 0; i < nframes; ++i)
        for (int c = 0; c < channels; ++c)
            planarIn[c][i] = interleaved[i * channels + c];
    std::vector<const float*> inP(channels);
    std::vector<float*> outP(channels);

    octavox::allocs::arm(true);
    octavox::allocs::reset();
    const double tA = nowSec();
    int done = 0, k = 0;
    while (done < static_cast<int>(nframes)) {
        const int want =
            (k < nCtrl) ? (k + 1) * cadence : static_cast<int>(nframes);
        int blk = want - done;
        if (blk <= 0) blk = std::min(1024, static_cast<int>(nframes) - done);
        if (blk <= 0) break;
        for (int c = 0; c < channels; ++c) {
            inP[c] = planarIn[c].data() + done;
            outP[c] = rr.out[c].data() + done;
        }
        eng.process(inP.data(), outP.data(), channels, blk);
        done += blk;
        if (k < nCtrl && (k + 1) * cadence <= done) {
            for (int c = 0; c < channels; ++c)
                rr.est[c][k] = eng.lastEstimateHz(c);
            ++k;
        }
    }
    const double tB = nowSec();
    rr.renderAllocs = octavox::allocs::count();
    octavox::allocs::arm(false);
    rr.cpuNsPerSample = (tB - tA) * 1e9 / std::max<size_t>(1, nframes);
    return rr;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<Entry> entries;
    const char* jsonPath = nullptr;
    const char* trajDir = nullptr;
    const char* outDir = nullptr;
    const char* globalAnn = nullptr;
    double ratioSemi = 5.0;
    double maxSeconds = 30.0;
    bool demo = false;
    std::string interpName = "lagrange6";
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--input") && i + 1 < argc) {
            entries.push_back({argv[++i], "recorded:other", "", ""});
        } else if (!std::strcmp(argv[i], "--category") && i + 1 < argc) {
            if (!entries.empty()) entries.back().category = argv[++i];
        } else if (!std::strcmp(argv[i], "--annotate") && i + 1 < argc) {
            globalAnn = argv[++i];
        } else if (!std::strcmp(argv[i], "--manifest") && i + 1 < argc) {
            const char* mp = argv[++i];
            std::FILE* f = std::fopen(mp, "r");
            if (!f) {
                std::fprintf(stderr, "cannot open manifest %s\n", mp);
                return 2;
            }
            char line[2048];
            while (std::fgets(line, sizeof(line), f)) {
                std::string s(line);
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                    s.pop_back();
                if (s.empty() || s[0] == '#') continue;
                // path,category[,annotation[,label]]
                Entry e;
                size_t p1 = s.find(',');
                if (p1 == std::string::npos) {
                    e.path = s;
                    e.category = "recorded:other";
                } else {
                    e.path = s.substr(0, p1);
                    size_t p2 = s.find(',', p1 + 1);
                    if (p2 == std::string::npos) {
                        e.category = s.substr(p1 + 1);
                    } else {
                        e.category = s.substr(p1 + 1, p2 - p1 - 1);
                        size_t p3 = s.find(',', p2 + 1);
                        if (p3 == std::string::npos) {
                            e.annotation = s.substr(p2 + 1);
                        } else {
                            e.annotation = s.substr(p2 + 1, p3 - p2 - 1);
                            e.label = s.substr(p3 + 1);
                        }
                    }
                }
                while (!e.path.empty() && e.path.back() == ' ') e.path.pop_back();
                entries.push_back(e);
            }
            std::fclose(f);
        } else if (!std::strcmp(argv[i], "--json") && i + 1 < argc)
            jsonPath = argv[++i];
        else if (!std::strcmp(argv[i], "--trajdir") && i + 1 < argc)
            trajDir = argv[++i];
        else if (!std::strcmp(argv[i], "--outdir") && i + 1 < argc)
            outDir = argv[++i];
        else if (!std::strcmp(argv[i], "--ratio") && i + 1 < argc)
            ratioSemi = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--maxseconds") && i + 1 < argc)
            maxSeconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--interpolation") && i + 1 < argc)
            interpName = argv[++i];
        else if (!std::strcmp(argv[i], "--demo-synthetic"))
            demo = true;
        else {
            std::fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 2;
        }
    }

    // validate every category up front (hard errors -- mislabelling is
    // worse than absence)
    for (const Entry& e : entries) {
        if (!validCategory(e.category)) {
            std::fprintf(stderr,
                         "invalid category '%s' (want synthetic | "
                         "recorded:<class> | annotated:<class>; classes: ",
                         e.category.c_str());
            for (const char* k : kClasses)
                std::fprintf(stderr, "%s ", k);
            std::fprintf(stderr, ")\n");
            return 2;
        }
        if (e.category.compare(0, 10, "annotated:") == 0 &&
            e.annotation.empty() && !globalAnn) {
            std::fprintf(stderr,
                         "annotated: entry '%s' has no annotation csv "
                         "(manifest column 3 or --annotate)\n",
                         e.path.c_str());
            return 2;
        }
    }

    if (demo) {
        if (!outDir) {
            std::fprintf(stderr, "--demo-synthetic requires --outdir\n");
            return 2;
        }
        writeSyntheticDemo(outDir);
        const char* names[3] = {"/demo_syn_voice_110.wav",
                                "/demo_syn_bass_55.wav",
                                "/demo_syn_speech_140.wav"};
        const char* anns[3] = {"", "", ""};
        for (int i = 0; i < 3; ++i)
            entries.push_back({std::string(outDir) + names[i], "synthetic",
                               anns[i], "demo"});
    }

    if (entries.empty()) {
        std::fprintf(stderr,
                     "no entries (use --manifest, --input or --demo-synthetic)\n");
        return 2;
    }

    using octavox::DetectorMode;
    using octavox::Interpolation;
    const DetectorMode modes[3] = {DetectorMode::FullRate, DetectorMode::Decimated,
                                   DetectorMode::Hybrid};
    const Interpolation interp = (interpName == "obskernel6")
                                     ? Interpolation::ObsKernel6
                                     : Interpolation::Lagrange6;
    const double ratio = std::pow(2.0, ratioSemi / 12.0);

    std::string json = "[\n";
    bool firstRec = true;

    for (const Entry& e : entries) {
        int sr = 0, ch = 0;
        std::vector<float> interleaved;
        try {
            interleaved = octavox::wav::readWav(e.path, sr, ch);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "SKIP %s: %s\n", e.path.c_str(), ex.what());
            continue;
        }
        if (ch < 1 || ch > 8) {
            std::fprintf(stderr, "SKIP %s: channel count %d\n", e.path.c_str(), ch);
            continue;
        }
        size_t nframes = interleaved.size() / ch;
        if (nframes > static_cast<size_t>(maxSeconds * sr))
            nframes = static_cast<size_t>(maxSeconds * sr);
        interleaved.resize(nframes * ch);
        if (nframes < static_cast<size_t>(2.0 * sr)) {
            std::fprintf(stderr, "SKIP %s: shorter than 2 s\n", e.path.c_str());
            continue;
        }
        const std::string base =
            e.path.substr(e.path.find_last_of("/\\") + 1);
        std::vector<Span> spans;
        if (!e.annotation.empty())
            spans = loadSpans(e.annotation);
        else if (globalAnn && e.category.compare(0, 10, "annotated:") == 0)
            spans = loadSpans(globalAnn);
        const bool hasTruth = !spans.empty();

        // mono analysis view of channel 0 for the voiced mask
        std::vector<float> in0(nframes);
        for (size_t i = 0; i < nframes; ++i) in0[i] = interleaved[i * ch];

        // voiced-ish mask (10 ms RMS vs the file's 20th percentile)
        std::vector<char> voiced;
        {
            const int win = std::max(1, static_cast<int>(0.01 * sr));
            const int nB = static_cast<int>(nframes) / win;
            std::vector<double> env(nB, 0.0);
            for (int b = 0; b < nB; ++b) {
                double s = 0.0;
                for (int i = b * win; i < (b + 1) * win && i < (int)nframes; ++i)
                    s += double(in0[i]) * double(in0[i]);
                env[b] = std::sqrt(s / win);
            }
            std::vector<double> sorted(env);
            std::sort(sorted.begin(), sorted.end());
            const double thr = sorted[sorted.size() / 5];
            const int cadence = 64;
            const int nCtrl = static_cast<int>(nframes) / cadence;
            voiced.assign(nCtrl, 0);
            for (int k = 0; k < nCtrl; ++k) {
                const int b = (k * cadence) / win;
                if (b < nB) voiced[k] = (env[b] > thr && env[b] > 1e-4) ? 1 : 0;
            }
        }

        // transient list (top-8 input steps after warm-up, channel 0)
        std::vector<int> transients;
        {
            std::vector<std::pair<double, int>> steps;
            for (int i = static_cast<int>(1.2 * sr) + 1;
                 i + 1 < static_cast<int>(nframes); ++i)
                steps.push_back({std::abs(double(in0[i]) - double(in0[i - 1])), i});
            std::sort(steps.begin(), steps.end(),
                      [](const std::pair<double, int>& a,
                         const std::pair<double, int>& b) {
                          return a.first > b.first;
                      });
            const size_t want = std::min<size_t>(8, steps.size());
            for (size_t q = 0; q < want; ++q) transients.push_back(steps[q].second);
            std::sort(transients.begin(), transients.end());
        }

        std::printf("== %s [%s]%s\n", base.c_str(), e.category.c_str(),
                    hasTruth ? " +reference" : "");

        std::vector<RenderResult> rends(3);
        for (int m = 0; m < 3; ++m)
            rends[m] = renderMode(interleaved, sr, ch, ratio, modes[m], interp);

        const int cadence = 64;
        const int nCtrl = static_cast<int>(rends[0].est[0].size());
        const int kWarm = static_cast<int>(1.2 * sr / cadence);

        for (int m = 0; m < 3; ++m) {
            const bool isRef = (m == 0);
            const std::vector<double>& est = rends[m].est[0];
            const std::vector<double>& ref = rends[0].est[0];
            const std::vector<float>& out0 = rends[m].out[0];
            const std::vector<float>& refOut0 = rends[0].out[0];

            // metrics
            std::vector<double> diffs;
            long voicedN = 0, drop = 0, octDis = 0;
            double voicingRatio = 0.0;
            {
                long nz = 0;
                for (int q = kWarm; q < nCtrl; ++q) {
                    if (!voiced[q]) continue;
                    ++voicedN;
                    if (est[q] > 0.0) ++nz;
                    if (est[q] <= 0.0) {
                        ++drop;
                        continue;
                    }
                    if (!hasTruth && ref[q] <= 0.0) continue;
                    double d;
                    if (hasTruth) {
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
                voicingRatio = voicedN ? double(nz) / double(voicedN) : 0.0;
            }
            double medD = 0.0, maxD = 0.0;
            if (!diffs.empty()) {
                std::sort(diffs.begin(), diffs.end());
                medD = diffs[diffs.size() / 2];
                maxD = diffs.back();
            }
            double jitter = 0.0;
            {
                std::vector<double> jd;
                for (int q = kWarm + 1; q < nCtrl; ++q) {
                    if (!voiced[q] || !voiced[q - 1]) continue;
                    if (est[q] <= 0.0 || est[q - 1] <= 0.0) continue;
                    jd.push_back(std::abs(cents(est[q], est[q - 1])));
                }
                if (!jd.empty()) {
                    std::sort(jd.begin(), jd.end());
                    jitter = jd[jd.size() / 2];
                }
            }
            // pitch transition events (|delta| > 100 cents between
            // consecutive voiced, both-nonzero estimates)
            long nTrans = 0;
            double medTransMag = 0.0;
            std::vector<double> transTimesMs;
            {
                std::vector<double> mags;
                // WINDOWED delta over the detector's own median-history
                // length (f0_history = 8 boundaries ~ 10.7 ms): a real
                // pitch step resolves WITHIN the window, while the per-step
                // delta can sit just under the threshold (the 8-entry
                // median spreads a hard step over ~7 boundaries of ~97
                // cents each -- measured). One event per cluster: after a
                // hit, the following 8 boundaries are suppressed.
                const int win = 8;
                int suppress = 0;
                for (int q = kWarm + win; q < nCtrl; ++q) {
                    if (suppress > 0) { --suppress; continue; }
                    if (!voiced[q] || !voiced[q - win]) continue;
                    if (est[q] <= 0.0 || est[q - win] <= 0.0) continue;
                    const double d = std::abs(cents(est[q], est[q - win]));
                    if (d > 100.0) {
                        ++nTrans;
                        mags.push_back(d);
                        if (transTimesMs.size() < 8)
                            transTimesMs.push_back(q * cadence * 1000.0 / sr);
                        suppress = win;
                    }
                }
                if (!mags.empty()) {
                    std::sort(mags.begin(), mags.end());
                    medTransMag = mags[mags.size() / 2];
                }
            }
            // transient recovery (R21 law)
            double recMs = 0.0;
            {
                std::vector<double> rec;
                for (int tSample : transients) {
                    int q0 = tSample / cadence;
                    const int qEnd =
                        std::min(nCtrl, q0 + static_cast<int>(0.4 * sr / cadence));
                    int q = q0;
                    for (; q < qEnd; ++q) {
                        if (est[q] <= 0.0) continue;
                        const int qA = q + 1;
                        const int qB =
                            std::min(nCtrl - 1, q + static_cast<int>(0.05 * sr / cadence));
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
                    recMs = s / rec.size();
                }
            }
            // relative lock delay vs the reference after shared dropouts
            double lockMs = 0.0;
            if (!isRef) {
                std::vector<double> lags;
                for (int q = kWarm + 1; q < nCtrl; ++q) {
                    if (ref[q] > 0.0 || est[q] > 0.0) continue;
                    auto relock = [&](const std::vector<double>& e2) {
                        int p = q;
                        const int pEnd = q + static_cast<int>(0.5 * sr / cadence);
                        while (p < pEnd && p < nCtrl && e2[p] <= 0.0) ++p;
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
                    lockMs = s / lags.size();
                }
            }
            // output discontinuity + output cross-mode comparison
            double maxStep = 0.0, outDiffRms = 0.0;
            {
                double mx = 0.0;
                for (size_t q = static_cast<size_t>(1.2 * sr);
                     q + 1 < out0.size(); ++q)
                    mx = std::max(mx, std::abs(double(out0[q]) - double(out0[q - 1])));
                maxStep = mx;
                double s = 0.0;
                size_t nn = 0;
                for (size_t q = static_cast<size_t>(1.2 * sr);
                     q < out0.size() && q < refOut0.size(); ++q) {
                    s += (double(out0[q]) - double(refOut0[q])) *
                         (double(out0[q]) - double(refOut0[q]));
                    ++nn;
                }
                outDiffRms = nn ? std::sqrt(s / nn) : 0.0;
            }
            // stereo L/R estimate divergence
            double lrDiv = -1.0;
            if (ch == 2) {
                const std::vector<double>& eL = rends[m].est[0];
                const std::vector<double>& eR = rends[m].est[1];
                std::vector<double> dv;
                for (int q = kWarm; q < nCtrl; ++q) {
                    if (!voiced[q]) continue;
                    if (eL[q] <= 0.0 || eR[q] <= 0.0) continue;
                    dv.push_back(std::abs(cents(eL[q], eR[q])));
                }
                if (!dv.empty()) {
                    std::sort(dv.begin(), dv.end());
                    lrDiv = dv[dv.size() / 2];
                }
            }

            char line[320];
            std::snprintf(line, sizeof(line),
                          "  %-10s med %8.3f max %9.3f jit %7.3f oct %5.2f%% "
                          "drop %5.2f%% voic %5.1f%% trans %4ld rec %6.1fms "
                          "cpu %7.1fns allocs %3ld outdiff %8.5f step %6.4f",
                          modeName(modes[m]), medD, maxD, jitter,
                          100.0 * octDis / std::max(1L, voicedN),
                          100.0 * drop / std::max(1L, voicedN),
                          100.0 * voicingRatio, nTrans, recMs,
                          rends[m].cpuNsPerSample, rends[m].renderAllocs,
                          outDiffRms, maxStep);
            std::printf("%s\n", line);

            // JSON record
            std::string jtrans = "[";
            for (size_t q = 0; q < transTimesMs.size(); ++q) {
                if (q) jtrans += ", ";
                char b[32];
                std::snprintf(b, sizeof b, "%.1f", transTimesMs[q]);
                jtrans += b;
            }
            jtrans += "]";
            if (!firstRec) json += ",\n";
            firstRec = false;
            char rec[2048];
            std::snprintf(rec, sizeof rec,
                "  {\"file\": \"%s\", \"path\": \"%s\", \"category\": \"%s\", "
                "\"label\": \"%s\", \"mode\": \"%s\", \"interpolation\": \"%s\", "
                "\"rate\": %d, \"channels\": %d, "
                "\"seconds\": %.3f, \"reference\": \"%s\", "
                "\"median_diff_cents\": %.4f, \"max_diff_cents\": %.4f, "
                "\"jitter_cents\": %.4f, \"octave_disagree_ratio\": %.6f, "
                "\"dropout_ratio\": %.6f, \"voicing_ratio\": %.6f, "
                "\"pitch_transitions\": %ld, "
                "\"pitch_transition_median_cents\": %.4f, "
                "\"pitch_transition_times_ms\": %s, "
                "\"transient_recovery_ms\": %.4f, \"rel_lock_delay_ms\": %.4f, "
                "\"cpu_ns_per_sample\": %.2f, \"render_allocs\": %ld, "
                "\"max_output_step\": %.6f, \"output_diff_rms_vs_reference\": %.6f, "
                "\"lr_estimate_divergence_cents\": %.4f, "
                "\"n_estimates\": %ld}",
                base.c_str(), e.path.c_str(), e.category.c_str(),
                e.label.c_str(), modeName(modes[m]), interpName.c_str(), sr, ch,
                double(nframes) / sr,
                hasTruth ? "annotation" : "fullrate",
                medD, maxD, jitter,
                voicedN ? double(octDis) / double(voicedN) : 0.0,
                voicedN ? double(drop) / double(voicedN) : 0.0,
                voicingRatio, nTrans, medTransMag, jtrans.c_str(),
                recMs, lockMs, rends[m].cpuNsPerSample, rends[m].renderAllocs,
                maxStep, outDiffRms, lrDiv,
                static_cast<long>(diffs.size()) + drop);
            json += rec;

            // trajectories (raw + median-5 smoothed, per channel 0)
            if (trajDir) {
                std::string p = std::string(trajDir) + "/traj_" + base + "_" +
                                modeName(modes[m]) + ".csv";
                std::FILE* f = std::fopen(p.c_str(), "w");
                if (f) {
                    std::fprintf(f, "t_ms,est_hz,est_smoothed_hz,ref_fullrate_hz\n");
                    const int dec = std::max(1, static_cast<int>(0.01 * sr) / cadence);
                    for (int q = kWarm; q < nCtrl; q += dec) {
                        double sm = est[q];
                        if (q >= 2 && q + 2 < nCtrl) {
                            double v[5] = {est[q - 2], est[q - 1], est[q],
                                           est[q + 1], est[q + 2]};
                            double tmp;
                            for (int a = 0; a < 5; ++a)
                                for (int b = a + 1; b < 5; ++b)
                                    if (v[b] < v[a]) { tmp = v[a]; v[a] = v[b]; v[b] = tmp; }
                            sm = v[2];
                        }
                        std::fprintf(f, "%.1f,%.4f,%.4f,%.4f\n",
                                     q * cadence * 1000.0 / sr, est[q], sm,
                                     ref[q]);
                    }
                    std::fclose(f);
                }
            }
            // final output WAV (32-bit float; --outdir)
            if (outDir) {
                std::vector<float> inter(nframes * ch);
                for (size_t i = 0; i < nframes; ++i)
                    for (int c = 0; c < ch; ++c)
                        inter[i * ch + c] = rends[m].out[c][i];
                std::string p = std::string(outDir) + "/" + base + "_" +
                                modeName(modes[m]) + ".wav";
                // strip a possible ".wav" duplication: base already has it
                writeWavF32(p.c_str(), inter.data(),
                                          static_cast<int>(nframes), ch, sr);
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
