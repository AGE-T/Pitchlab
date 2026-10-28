// OCTAVOX round 12 -- native benchmark (release build).
//
// Deterministic input: the shared 64-bit LCG spec (identical implementation
// in evidence/analysis_round12/r12_native_metrics.py on the Python side), so
// the native and Python reference benchmarks consume byte-identical input.
//
// Measures per configuration: median ns per output sample, realtime factor,
// peak RSS (getrusage), and steady-state allocation count. JSON output via
// --json <path>. This measures OUR implementation only.
//
// R15-P2 extensions (round-15 P5 regression battery):
//   * 96 kHz variants of mono_1v_b128 / mono_8v_b128 (the detector's
//     analysis window is sample-rate relative since R13-b, so control costs
//     scale with the rate -- both rates are reported side by side);
//   * one "surface" case: mono_8v_b128 at 48 kHz with the round-15 P1
//     per-voice surface ACTIVE (per-voice ratios != default, two voices
//     muted, wet 0.5, gain ramps issued mid-run inside the timed loop) to
//     measure the per-voice-control overhead on the RT path.
// NOTE (R15-P2 salvage): restored from the intact 15.0.0 tree during the
// 2026-10-26 environment-reset recovery, with the P5 extensions applied.
// Requires src/dsp.cpp to be recovered before building (see
// evidence/analysis_round15/NOTES_R15P2.md).
//
// R16-P2 extensions (reproducible workflow):
//   * case selection: --case <name> (repeatable) and/or --cases a,b,c
//     (union); --list prints the case names; unknown name exits 2; the
//     default (no flags) still runs ALL cases;
//   * NEW 13th case mono_8v_b128_96k_ctrl512 (the R16-P3 detector-cost
//     isolation row at 96 kHz: mono_8v_b128_96k with detection_interval
//     512).  The delivered round-15 bench had 12 cases -- this row is NEW
//     in round 16 (noted in the stdout header and the JSON "note" field);
//   * JSON: an "environment" object (timestamp/compiler/build type/
//     sanitizers/os/cpu/cores) and a per-row "wall_ms" (wall duration of
//     the measured loop) were ADDED; every round-15 field (incl.
//     sample_rate and surface) survives unchanged.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <sys/resource.h>
#include <unistd.h>

#include "octavox/dsp.hpp"

using namespace octavox;

namespace octavox {
namespace allocs {
long count();
void arm(bool on);
void reset();
}  // namespace allocs
}  // namespace octavox

namespace {

struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed)
        : s(seed * 6364136223846793005ULL + 1442695040888963407ULL) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint64_t m = (s >> 11) & ((1ULL << 53) - 1);
        return static_cast<double>(m) / 9007199254740992.0;
    }
    float noise() { return static_cast<float>(2.0 * next() - 1.0); }
};

double nowSec() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

long peakRssKb() {
    struct rusage ru;
    std::memset(&ru, 0, sizeof(ru));
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

struct BenchRow {
    std::string name;
    int voices;
    int channels;
    int block;
    int detection_interval;
    int sample_rate;         // R15-P2: 48000 (default) or 96000
    bool surface;            // R15-P2: per-voice surface active
    double ns_per_sample;   // per OUTPUT sample (channel-summed work / frames)
    double realtime_factor; // audio seconds per wall second
    long rss_kb;
    long steady_allocs;
    double seconds;
    double wall_ms;          // R16-P2: wall duration of the measured loop (ms)
};

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') {
            o += '\\';
            o += ch;
        } else {
            o += ch;
        }
    }
    return o;
}

// ---- environment record (R16-P2; same fragment shape as test_main.cpp) --
static std::string readProcFile(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return "";
    std::string s;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

#ifndef OCTAVOX_BUILD_TYPE
#define OCTAVOX_BUILD_TYPE "unknown"
#endif
#ifndef OCTAVOX_SANITIZERS
#define OCTAVOX_SANITIZERS 0
#endif

static std::string envJsonFragment() {
    char ts[48];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    gmtime_r(&t, &tmv);
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    std::string os = readProcFile("/proc/sys/kernel/ostype");
    const std::string osrel = readProcFile("/proc/sys/kernel/osrelease");
    if (!os.empty() && !osrel.empty()) os += " " + osrel;
    std::string cpu;
    {
        FILE* f = std::fopen("/proc/cpuinfo", "rb");
        if (f) {
            char line[1024];
            while (std::fgets(line, sizeof(line), f)) {
                if (std::strncmp(line, "model name", 10) == 0) {
                    const char* colon = std::strchr(line, ':');
                    if (colon) {
                        cpu = colon + 1;
                        // trim trailing newline and leading spaces
                        while (!cpu.empty() &&
                               (cpu.back() == '\n' || cpu.back() == '\r'))
                            cpu.pop_back();
                        size_t b = cpu.find_first_not_of(' ');
                        cpu = (b == std::string::npos) ? "" : cpu.substr(b);
                    }
                    break;
                }
            }
            std::fclose(f);
        }
    }
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores <= 0) cores = 0;
    std::string j = "\"timestamp_utc\": \"";
    j += ts;
    j += "\", \"compiler\": \"";
    j += jsonEscape(__VERSION__);
    j += "\", \"build_type\": \"";
    j += jsonEscape(OCTAVOX_BUILD_TYPE);
    j += "\", \"sanitizers\": ";
    j += (OCTAVOX_SANITIZERS != 0) ? "true" : "false";
    j += ", \"os\": \"";
    j += jsonEscape(os);
    j += "\", \"cpu\": \"";
    j += jsonEscape(cpu);
    j += "\", \"cores\": ";
    j += std::to_string(cores);
    return j;
}

void runCase(const std::string& name, int voices, int channels, int block,
             int detection_interval, int audio_seconds, int repeats,
             std::vector<BenchRow>& rows, int sample_rate = 48000,
             bool surface = false) {
    const int sr = sample_rate;
    const int n = audio_seconds * sr;
    // identical input for every channel (deterministic LCG, seed 2025)
    std::vector<float> xin(n);
    {
        Lcg rng(2025);
        for (int i = 0; i < n; ++i) xin[i] = rng.noise() * 0.5f;
    }
    std::vector<std::vector<float>> in(channels), out(channels);
    for (int c = 0; c < channels; ++c) {
        in[c] = xin;
        out[c].assign(n, 0.0f);
    }
    std::vector<const float*> inP(channels);
    std::vector<float*> outP(channels);
    for (int c = 0; c < channels; ++c) {
        inP[c] = in[c].data();
        outP[c] = out[c].data();
    }

    Config cfg;
    cfg.sample_rate = sample_rate;
    cfg.ratio = std::pow(2.0, 400.0 / 1200.0);  // +4 st (non-trivial path)
    cfg.voices = voices;
    cfg.detection_interval = detection_interval;

    MultichannelShifter m;
    if (!m.prepare(cfg, channels)) {
        std::fprintf(stderr, "prepare failed for %s\n", name.c_str());
        return;
    }

    // R15-P2 surface mode: the round-15 P1 per-voice surface ACTIVE --
    // per-voice ratios != the spread-table default, two voices muted,
    // wet 0.5. The gain-ramp retargets below are issued INSIDE the timed
    // loop between process() calls (they are part of the measured RT path).
    if (surface) {
        const double semis[8] = {-9.0, -6.0, -3.0, 3.0, 5.0, 7.0, -12.0, 10.0};
        for (int v = 0; v < 8; ++v)
            m.setVoiceRatio(v, std::pow(2.0, semis[v] / 12.0));
        m.setVoiceMute(1, true);
        m.setVoiceMute(6, true);
        m.setWetDry(0.5);
    }

    // warm-up (tables, cache, branch predictors)
    {
        int done = 0;
        while (done < sample_rate) {
            const int k = std::min(block, sample_rate - done);
            m.process(inP.data(), outP.data(), channels, k);
            done += k;
        }
        m.reset();
    }

    double best = 1e300;
    for (int rep = 0; rep < repeats; ++rep) {
        m.reset();
        allocs::reset();
        allocs::arm(true);
        const double t0 = nowSec();
        int done = 0;
        int rampStage = 0;
        while (done < n) {
            const int k = std::min(block, n - done);
            m.process(inP.data(), outP.data(), channels, k);
            done += k;
            if (surface && done == n / 4) {
                m.setVoiceGain(0, 0.3);
                ++rampStage;
            } else if (surface && done == n / 2) {
                m.setVoiceGain(0, 0.8);
                ++rampStage;
            } else if (surface && done == 3 * n / 4) {
                m.setVoiceGain(2, 0.5);
                ++rampStage;
            }
        }
        const double el = nowSec() - t0;
        allocs::arm(false);
        if (el < best) best = el;
    }

    BenchRow row;
    row.name = name;
    row.voices = voices;
    row.channels = channels;
    row.block = block;
    row.detection_interval = detection_interval;
    row.sample_rate = sample_rate;
    row.surface = surface;
    row.seconds = best;
    row.wall_ms = best * 1000.0;  // R16-P2: wall duration of the best loop
    // ns per output FRAME (one frame = one sample per channel)
    row.ns_per_sample = best * 1e9 / n;
    row.realtime_factor = static_cast<double>(n) / sr / best;
    row.rss_kb = peakRssKb();
    row.steady_allocs = allocs::count();
    rows.push_back(std::move(row));
    std::printf("bench: %-26s done, wall %.0f ms, %.2f ns/frame\n",
                name.c_str(), row.wall_ms, row.ns_per_sample);
    std::fflush(stdout);
}

}  // namespace

// ---------------------------------------------------------------------------
// R16-P2: the bench case table (13 cases).  The first 12 are the delivered
// round-15 set (same relative order); mono_8v_b128_96k_ctrl512 is the NEW
// R16 row (detector-cost isolation at 96 kHz, detection_interval 512).
// ---------------------------------------------------------------------------
struct CaseSpec {
    const char* name;
    int voices, channels, block, detection_interval;
    int audio_seconds, repeats, sample_rate;
    bool surface;
};

static const CaseSpec kBenchCases[] = {
    // main matrix: 1/8 voices, mono/stereo, representative block sizes
    {"mono_1v_b64", 1, 1, 64, 64, 5, 3, 48000, false},
    {"mono_1v_b128", 1, 1, 128, 64, 5, 3, 48000, false},
    {"mono_1v_b512", 1, 1, 512, 64, 5, 3, 48000, false},
    {"mono_8v_b64", 8, 1, 64, 64, 5, 3, 48000, false},
    {"mono_8v_b128", 8, 1, 128, 64, 5, 3, 48000, false},
    {"mono_8v_b512", 8, 1, 512, 64, 5, 3, 48000, false},
    {"stereo_1v_b128", 1, 2, 128, 64, 5, 3, 48000, false},
    {"stereo_8v_b128", 8, 2, 128, 64, 5, 3, 48000, false},
    // detector share: same config with a sparse control rate
    {"mono_8v_b128_ctrl512", 8, 1, 128, 512, 5, 3, 48000, false},
    // R15-P2: 96 kHz variants (control costs scale with the rate)
    {"mono_1v_b128_96k", 1, 1, 128, 64, 5, 3, 96000, false},
    {"mono_8v_b128_96k", 8, 1, 128, 64, 5, 3, 96000, false},
    // R16-P2 NEW ROW: detector-cost isolation at 96 kHz (ctrl 512)
    {"mono_8v_b128_96k_ctrl512", 8, 1, 128, 512, 5, 3, 96000, false},
    // R15-P2: per-voice-surface overhead case (48 kHz, surface ACTIVE)
    {"mono_8v_b128_surface", 8, 1, 128, 64, 5, 3, 48000, true},
};
static const int kBenchCaseCount =
    static_cast<int>(sizeof(kBenchCases) / sizeof(kBenchCases[0]));

static void usageExit(const std::string& msg) {
    std::fprintf(stderr, "%s\n", msg.c_str());
    std::fprintf(stderr,
                 "usage: octavox_bench [--json <path>] [--case <name>] "
                 "[--cases a,b,c] [--list]\n");
    std::fprintf(stderr, "valid cases (default = all %d):\n", kBenchCaseCount);
    for (int i = 0; i < kBenchCaseCount; ++i)
        std::fprintf(stderr, "  %s\n", kBenchCases[i].name);
    std::exit(2);
}

int main(int argc, char** argv) {
    std::string jsonPath;
    std::vector<std::string> wanted;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json" && i + 1 < argc) {
            jsonPath = argv[++i];
        } else if (a == "--case" && i + 1 < argc) {
            wanted.push_back(argv[++i]);
        } else if (a == "--cases" && i + 1 < argc) {
            // comma-separated list (union with the --case picks)
            std::string csv = argv[++i];
            std::string cur;
            for (char ch : csv) {
                if (ch == ',') {
                    if (!cur.empty()) wanted.push_back(cur);
                    cur.clear();
                } else {
                    cur += ch;
                }
            }
            if (!cur.empty()) wanted.push_back(cur);
        } else if (a == "--list") {
            list = true;
        } else {
            usageExit("error: unknown or incomplete option: " + a);
        }
    }

    // validate the requested names against the case table
    for (const auto& w : wanted) {
        bool ok = false;
        for (int i = 0; i < kBenchCaseCount; ++i)
            if (w == kBenchCases[i].name) {
                ok = true;
                break;
            }
        if (!ok) usageExit("error: unknown bench case '" + w + "'");
    }

    if (list) {  // static case dump; nothing runs
        for (int i = 0; i < kBenchCaseCount; ++i)
            std::printf("%s\n", kBenchCases[i].name);
        return 0;
    }

    // default: no selection flags = ALL cases (unchanged full run)
    if (wanted.empty())
        for (int i = 0; i < kBenchCaseCount; ++i)
            wanted.push_back(kBenchCases[i].name);

    std::vector<BenchRow> rows;
    for (int i = 0; i < kBenchCaseCount; ++i) {
        const CaseSpec& s = kBenchCases[i];
        bool run = false;
        for (const auto& w : wanted)
            if (w == s.name) {
                run = true;
                break;
            }
        if (!run) continue;
        std::printf("== bench case %s (%d/%d selected) ==\n", s.name,
                    (int)rows.size() + 1, (int)wanted.size());
        std::fflush(stdout);
        runCase(s.name, s.voices, s.channels, s.block, s.detection_interval,
                s.audio_seconds, s.repeats, rows, s.sample_rate, s.surface);
    }

    std::printf("%-26s %5s %3s %5s %5s %6s %3s %12s %10s %10s %8s\n",
                "CASE", "vox", "ch", "block", "ctrl", "sr", "sfc",
                "ns/frame", "xrealtime", "rss_kb", "allocs");
    std::printf("%s\n", std::string(110, '-').c_str());
    for (const auto& r : rows) {
        std::printf("%-26s %5d %3d %5d %5d %6d %3s %12.2f %10.2f %10ld %8ld\n",
                    r.name.c_str(), r.voices, r.channels, r.block,
                    r.detection_interval, r.sample_rate, r.surface ? "on" : "",
                    r.ns_per_sample, r.realtime_factor, r.rss_kb,
                    r.steady_allocs);
    }
    std::printf("%s\n%s\n", std::string(110, '-').c_str(), versionString());
    std::printf("# note: %d cases -- R16-P2 adds mono_8v_b128_96k_ctrl512 "
                "(detector-cost isolation at 96 kHz); the delivered "
                "round-15 bench had 12 cases\n",
                (int)rows.size());

    if (!jsonPath.empty()) {
        std::string j = "{\n  \"bench\": \"octavox_native\",\n  \"version\": \"";
        j += versionString();
        j += "\",\n  \"environment\": {" + envJsonFragment() + "},\n";
        j += "  \"sample_rate\": [48000, 96000],\n  \"input\": "
             "\"LCG(2025) noise, amp 0.5, 5 s per case, best of 3\",\n";
        j += "  \"note\": \"R16-P2: 13 cases -- mono_8v_b128_96k_ctrl512 "
             "(detection_interval 512 at 96 kHz) is the NEW R16 row; the "
             "round-15 delivery had 12\",\n  \"rows\": [\n";
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                          "    {\"name\": \"%s\", \"voices\": %d, "
                          "\"channels\": %d, "
                          "\"block\": %d, \"detection_interval\": %d, "
                          "\"sample_rate\": %d, \"surface\": %s, "
                          "\"ns_per_frame\": %.4f, \"realtime_factor\": %.4f, "
                          "\"rss_kb\": %ld, \"steady_allocs\": %ld, "
                          "\"seconds\": %.6f, \"wall_ms\": %.3f}",
                          jsonEscape(r.name).c_str(), r.voices, r.channels,
                          r.block, r.detection_interval, r.sample_rate,
                          r.surface ? "true" : "false", r.ns_per_sample,
                          r.realtime_factor, r.rss_kb, r.steady_allocs,
                          r.seconds, r.wall_ms);
            j += buf;
            if (i + 1 < rows.size()) j += ",";
            j += "\n";
        }
        j += "  ]\n}\n";
        FILE* f = std::fopen(jsonPath.c_str(), "wb");
        if (f) {
            std::fwrite(j.data(), 1, j.size(), f);
            std::fclose(f);
            std::printf("json written: %s\n", jsonPath.c_str());
        }
    }
    return 0;
}
