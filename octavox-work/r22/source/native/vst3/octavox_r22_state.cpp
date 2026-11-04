// OCTAVOX/PITCHLAB R22 -- state migration + harmony-mapper integration
// battery (the VST3 wrapper layer).
// ============================================================================
// PROVENANCE: our own tool (round 22, P4/P6). The wrapper surface grew from
// 38 to 74 parameters this round (ids 38..73: per-voice pan, per-voice
// feedback dB, the harmony-mapper block) and the state blob from v2 to v3
// (the 36 extension plain doubles appended after the legacy 38).
//
// Battery (all deterministic):
//   STATE
//    1 v3_roundtrip        surface with non-trivial extension values ->
//                          writeStateData -> readStateData -> exact equality
//                          of all 74 values + the Config
//    2 v2_migration        hand-built v2 blob (the R19 layout: 21 Config
//                          fields + channels + the legacy 38 doubles) ->
//                          readStateData restores the legacy values EXACTLY
//                          and the extension at the INERT defaults
//    3 v1_migration        hand-built v1 blob (17 Config fields + 38) ->
//                          R19 fields at their defaults + extension inert
//    4 v2_processor_restore a v2-shaped blob restored into a live processor:
//                          audio bit-identical to the same session replayed
//                          through the current build with an inert extension
//    5 legacy_inert_bitexact extension explicitly set to inert values on one
//                          processor vs untouched defaults on another:
//                          bit-identical audio
//   MAPPER
//    6 mapper_armed_ratios  arm + key/scale/temperament/interval/fine set;
//                          after the glide converges every voice's ratio
//                          equals globalRatio * mapRow(...).ratio[v]
//    7 mapper_all_scales    all 19 scale rows x 5 temperaments: armed ratio
//                          == the library's mapRow output (311 combos)
//    8 mapper_all_intervals all 29 interval selections on voice 0 (with the
//                          fine-tune cents composition at +-50)
//    9 mapper_disarmed      arm OFF: the spread-table/transpose path is
//                          UNCHANGED (the ratios match the legacy law)
//   META
//   10 param_surface_74     kNumParams == 74, ids contiguous, defaults in
//                          domain, help texts non-empty
//
// Usage: octavox_r22_state [--json <path>]  Exit 0 iff every gate passes.
// ============================================================================
#include "octavox_native_processor.h"
#include "octavox_params.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include "octavox/dsp.hpp"
#include "octavox/harmony.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using octavox::vst3wrap::OctavoxNativeProcessor;
namespace vw = octavox::vst3wrap;

//==========================================================================
// Minimal harness (same style as the wrapper tests)
//==========================================================================
namespace {

struct CaseResult {
    std::string name;
    bool pass = true;
    std::string note;
};
std::vector<CaseResult> g_results;

void check(CaseResult& c, bool ok, const std::string& what) {
    if (!ok) {
        c.pass = false;
        if (!c.note.empty()) c.note += "; ";
        c.note += what;
    }
}

double toEnginePlain(uint32_t id, double plain) {
    return vw::plainFromNormalized(id, vw::normalizedFromPlain(id, plain));
}

void standardSetup(OctavoxNativeProcessor* p, double sampleRate = 48000.0) {
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = sampleRate;
    ps.maxSamplesPerBlock = 4096;
    p->setupProcessing(ps);
}

OctavoxNativeProcessor* makeMono() {
    auto* p = new OctavoxNativeProcessor();
    if (p->initialize(nullptr) != kResultOk) return p;
    SpeakerArrangement in = SpeakerArr::kMono, out = SpeakerArr::kMono;
    p->setBusArrangements(&in, 1, &out, 1);
    standardSetup(p);
    p->setActive(true);
    return p;
}

void driveBlock(OctavoxNativeProcessor* p, const float* in, float* out, int n,
                IParameterChanges* changes = nullptr) {
    AudioBusBuffers ib{}, ob{};
    ib.numChannels = 1;
    ib.channelBuffers32 = const_cast<float**>(&in);
    ob.numChannels = 1;
    ob.channelBuffers32 = &out;
    AudioBusBuffers inB[1] = {ib}, outB[1] = {ob};
    ProcessData pd;
    std::memset(&pd, 0, sizeof(pd));
    pd.processMode = kRealtime;
    pd.symbolicSampleSize = kSample32;
    pd.numSamples = n;
    pd.inputs = inB;
    pd.numInputs = 1;
    pd.outputs = outB;
    pd.numOutputs = 1;
    pd.inputParameterChanges = changes;
    p->process(pd);
}

void addEvent(IParameterChanges& changes, uint32_t id, int32_t offset, double norm) {
    int32 idx = 0;
    IParamValueQueue* q = changes.addParameterData(id, idx);
    if (q) {
        int32 pidx = 0;
        q->addPoint(offset, norm, pidx);
    }
}

/// Apply a parameter list through the REAL event path (offset 0 of one
/// block -- exactly how a host delivers automation). setParamNormalized
/// alone only touches the host-visible Parameter object; the engine sees
/// values ONLY through the process queue.
void applyViaEvents(OctavoxNativeProcessor* p,
                    const std::vector<std::pair<uint32_t, double>>& params,
                    const float* in, float* out, int n) {
    ParameterChanges ch;
    for (const auto& pr : params)
        addEvent(ch, pr.first, 0,
                 vw::normalizedFromPlain(pr.first, pr.second));
    driveBlock(p, in, out, n, &ch);
}

void setPlain(OctavoxNativeProcessor* p, uint32_t id, double plain) {
    p->setParamNormalized(id, vw::normalizedFromPlain(id, plain));
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 1e30;
    double d = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        d = std::max(d, std::fabs(double(a[i]) - double(b[i])));
    return d;
}

std::vector<float> makeTone(double f, int sr, int n, double amp) {
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * f * i / sr));
    return v;
}

// Serialize a surface through the real writeStateData into raw bytes.
std::vector<char> blobOf(const vw::StateData& st) {
    MemoryStream ms;
    vw::writeStateData(&ms, st);
    const int L = static_cast<int>(ms.getSize());
    std::vector<char> b(L);
    int64 pos = 0;
    int32 got = 0;
    ms.seek(0, IBStream::kIBSeekSet, &pos);
    ms.read(b.data(), L, &got);
    return b;
}

vw::StateData blobToState(const std::vector<char>& bytes) {
    MemoryStream ms;
    int32 written = 0;
    ms.write(const_cast<char*>(bytes.data()),
             static_cast<int32>(bytes.size()), &written);
    int64 pos = 0;
    ms.seek(0, IBStream::kIBSeekSet, &pos);
    vw::StateData st;
    vw::readStateData(&ms, st);
    return st;
}

void plainOf(const vw::Surface& s, double out[vw::kNumParams]) {
    vw::surfaceToPlainArray(s, out);
}

}  // namespace

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }

    // ===================== 1. v3_roundtrip ================================
    {
        CaseResult c{"v3_roundtrip", true, ""};
        vw::StateData st;
        st.cfg = vw::defaultConfig();
        st.cfg.sample_rate = 48000.0;
        st.channels = 2;
        st.surface = vw::defaultSurface();
        st.surface.pitchSemi = -7.31;
        st.surface.voices = 5.0;
        st.surface.feedback = 0.44;
        for (int v = 0; v < 8; ++v) {
            st.surface.pan[v] = -0.75 + 0.21 * v;
            st.surface.fbDb[v] = -95.0 + 11.0 * v;
            st.surface.harmInterval[v] = 6 + v;
            st.surface.fineCents[v] = -33.0 + 9.0 * v;
        }
        st.surface.harmArm = 1.0;
        st.surface.harmKey = 4.0;
        st.surface.harmScale = 11.0;
        st.surface.harmTemp = 2.0;
        const vw::StateData back = blobToState(blobOf(st));
        double a[vw::kNumParams], b[vw::kNumParams];
        plainOf(st.surface, a);
        plainOf(back.surface, b);
        int mism = 0;
        for (int i = 0; i < vw::kNumParams; ++i)
            if (a[i] != b[i]) ++mism;
        check(c, mism == 0, "v3 plain round-trip mismatches: " + std::to_string(mism));
        check(c, back.cfg.grain == st.cfg.grain &&
                      back.cfg.detector_mode == st.cfg.detector_mode &&
                      back.cfg.f0_min == st.cfg.f0_min,
              "v3 config round-trip mismatch");
        g_results.push_back(c);
    }

    // ===================== 2. v2_migration ================================
    {
        CaseResult c{"v2_migration", true, ""};
        // Build the R19 v2 layout by hand: magic + ver2 + 21 config fields
        // + channels + the legacy 38 doubles.
        vw::Surface legacy = vw::defaultSurface();
        legacy.pitchSemi = 4.0;
        legacy.voices = 7.0;
        legacy.spread = 0.0;
        legacy.feedback = 0.21;
        for (int v = 0; v < 8; ++v) {
            legacy.offset[v] = -3.0 + v;
            legacy.gain[v] = 0.4 + 0.1 * v;
        }
        double lp[vw::kNumParams];
        plainOf(legacy, lp);
        octavox::Config cfg = vw::defaultConfig();
        cfg.sample_rate = 44100.0;
        std::vector<char> blob;
        auto i32 = [&](int32_t v) {
            for (int k = 0; k < 4; ++k) blob.push_back(char(uint8_t(v >> (8 * k))));
        };
        auto d64 = [&](double v) {
            uint64_t bits;
            std::memcpy(&bits, &v, 8);
            for (int k = 0; k < 8; ++k)
                blob.push_back(char(uint8_t(bits >> (8 * k))));
        };
        i32(0x3152564F);   // magic
        i32(2);            // version 2 (the R19 blob)
        d64(cfg.sample_rate);
        d64(cfg.ratio);
        i32(cfg.voices);
        i32(cfg.max_voices);
        i32(cfg.spread ? 1 : 0);
        d64(cfg.feedback);
        i32(static_cast<int32_t>(cfg.interpolation));
        d64(cfg.grain);
        d64(cfg.base_delay);
        d64(cfg.fmin);
        d64(cfg.fmax);
        d64(cfg.s_hysteresis);
        i32(cfg.f0_history);
        i32(cfg.detection_interval);
        d64(cfg.engagement_seconds);
        d64(cfg.gain_ramp_seconds);
        d64(cfg.bypass_seconds);
        i32(static_cast<int32_t>(cfg.detector_mode));
        d64(cfg.commit_crossfade_seconds);
        d64(cfg.delay_crossfade_seconds);
        d64(cfg.voice_fade_seconds);
        i32(1);            // channels
        for (int i = 0; i < vw::kNumParamsLegacy; ++i) d64(lp[i]);
        const vw::StateData back = blobToState(blob);
        // legacy values exact
        double bp[vw::kNumParams];
        plainOf(back.surface, bp);
        int mism = 0;
        for (int i = 0; i < vw::kNumParamsLegacy; ++i)
            if (lp[i] != bp[i]) ++mism;
        check(c, mism == 0,
              "v2 legacy values must restore exactly; mismatches: " +
                  std::to_string(mism));
        // extension inert defaults
        const vw::Surface dfl = vw::defaultSurface();
        bool inert = true;
        for (int v = 0; v < 8; ++v) {
            if (back.surface.pan[v] != dfl.pan[v]) inert = false;
            if (back.surface.fbDb[v] != dfl.fbDb[v]) inert = false;
            if (back.surface.harmInterval[v] != dfl.harmInterval[v]) inert = false;
            if (back.surface.fineCents[v] != dfl.fineCents[v]) inert = false;
        }
        if (back.surface.harmArm != dfl.harmArm ||
            back.surface.harmKey != dfl.harmKey ||
            back.surface.harmScale != dfl.harmScale ||
            back.surface.harmTemp != dfl.harmTemp)
            inert = false;
        check(c, inert, "v2 -> v3 extension must default to inert");
        check(c, back.cfg.sample_rate == 44100.0, "v2 config rate mismatch");
        g_results.push_back(c);
    }

    // ===================== 3. v1_migration ================================
    {
        CaseResult c{"v1_migration", true, ""};
        vw::Surface legacy = vw::defaultSurface();
        legacy.pitchSemi = 12.0;
        double lp[vw::kNumParams];
        plainOf(legacy, lp);
        octavox::Config cfg = vw::defaultConfig();
        std::vector<char> blob;
        auto i32 = [&](int32_t v) {
            for (int k = 0; k < 4; ++k) blob.push_back(char(uint8_t(v >> (8 * k))));
        };
        auto d64 = [&](double v) {
            uint64_t bits;
            std::memcpy(&bits, &v, 8);
            for (int k = 0; k < 8; ++k)
                blob.push_back(char(uint8_t(bits >> (8 * k))));
        };
        i32(0x3152564F);
        i32(1);            // version 1 (R16-R18)
        d64(cfg.sample_rate);
        d64(cfg.ratio);
        i32(cfg.voices);
        i32(cfg.max_voices);
        i32(cfg.spread ? 1 : 0);
        d64(cfg.feedback);
        i32(static_cast<int32_t>(cfg.interpolation));
        d64(cfg.grain);
        d64(cfg.base_delay);
        d64(cfg.fmin);
        d64(cfg.fmax);
        d64(cfg.s_hysteresis);
        i32(cfg.f0_history);
        i32(cfg.detection_interval);
        d64(cfg.engagement_seconds);
        d64(cfg.gain_ramp_seconds);
        d64(cfg.bypass_seconds);
        i32(2);
        for (int i = 0; i < vw::kNumParamsLegacy; ++i) d64(lp[i]);
        const vw::StateData back = blobToState(blob);
        double bp[vw::kNumParams];
        plainOf(back.surface, bp);
        int mism = 0;
        for (int i = 0; i < vw::kNumParamsLegacy; ++i)
            if (lp[i] != bp[i]) ++mism;
        check(c, mism == 0, "v1 legacy values mismatch: " + std::to_string(mism));
        // R19 fields at the Config defaults
        check(c, back.cfg.detector_mode == octavox::DetectorMode::Decimated &&
                      back.cfg.commit_crossfade_seconds ==
                          octavox::Config{}.commit_crossfade_seconds,
              "v1 R19 fields must take the defaults");
        check(c, back.surface.harmArm == 0.0, "v1 extension must be inert");
        g_results.push_back(c);
    }

    // ===================== 4/5. processor restore + inert bitexact =========
    {
        CaseResult c{"v2_processor_restore", true, ""};
        const int sr = 48000, N = 48000;  // 1 s tone
        const std::vector<float> in = makeTone(220.0, sr, N, 0.4);

        // (a) current-build processor, legacy-only surface, save state
        // (the params travel the real event path)
        auto* p = makeMono();
        std::vector<float> a(N);
        {
            std::vector<std::pair<uint32_t, double>> params;
            params.emplace_back(vw::ParamId::kPitch, 7.0);
            params.emplace_back(vw::ParamId::kVoices, 4.0);
            params.emplace_back(vw::ParamId::kFeedback, 0.3);
            applyViaEvents(p, params, in.data(), a.data(), 4096);
        }
        {
            // continue the render to fill the remaining tail
            std::vector<float> outTail(N - 4096);
            driveBlock(p, in.data() + 4096, a.data() + 4096, N - 4096);
        }
        MemoryStream state;
        check(c, p->getState(&state) == kResultOk, "getState failed");
        p->terminate();
        p->release();

        // (b) splice the v3 blob down to a v2-shaped blob (strip the 36
        // extension doubles, patch version to 2)
        const int L = static_cast<int>(state.getSize());
        std::vector<char> raw(L);
        {
            int64 pos = 0;
            int32 got = 0;
            state.seek(0, IBStream::kIBSeekSet, &pos);
            state.read(raw.data(), L, &got);
        }
        std::vector<char> v2blob(raw.begin(), raw.end() - 36 * 8);
        *reinterpret_cast<int32_t*>(&v2blob[4]) = 2;

        // (c) restore the v2 blob into a fresh processor and replay
        auto* q = makeMono();
        MemoryStream ms2;
        int32 written = 0;
        ms2.write(v2blob.data(), static_cast<int32>(v2blob.size()), &written);
        int64 pos2 = 0;
        ms2.seek(0, IBStream::kIBSeekSet, &pos2);
        check(c, q->setState(&ms2) == kResultOk, "v2 blob restore failed");
        // extension must sit at inert defaults
        const vw::Surface& su = q->liveSurface();
        check(c, su.harmArm == 0.0 && su.pan[0] == 0.0 && su.fbDb[0] == -100.0,
              "restored v2 extension not inert");
        std::vector<float> b(N);
        driveBlock(q, in.data(), b.data(), N);
        const double d = maxAbsDiff(a, b);
        check(c, d == 0.0,
              "v2-restored replay must be bit-identical (maxdiff " +
                  std::to_string(d) + ")");
        q->terminate();
        q->release();
        g_results.push_back(c);

        // (5) explicit inert extension == untouched defaults, bit-exact
        CaseResult c2{"legacy_inert_bitexact", true, ""};
        auto* r1 = makeMono();
        auto* r2 = makeMono();
        std::vector<float> x1(N), x2(N);
        {
            std::vector<std::pair<uint32_t, double>> params;
            for (int v = 0; v < 8; ++v) {
                params.emplace_back(vw::voicePanId(v), 0.0);
                params.emplace_back(vw::voiceFbDbId(v), -100.0);
                params.emplace_back(vw::harmIntId(v), 14.0);
                params.emplace_back(vw::harmFineId(v), 0.0);
            }
            params.emplace_back(vw::kHarmArm, 0.0);
            applyViaEvents(r2, params, in.data(), x2.data(), 4096);
        }
        driveBlock(r1, in.data(), x1.data(), 4096);
        driveBlock(r2, in.data() + 4096, x2.data() + 4096, N - 4096);
        driveBlock(r1, in.data() + 4096, x1.data() + 4096, N - 4096);
        check(c2, maxAbsDiff(x1, x2) == 0.0,
              "explicit inert extension must be bit-exact the defaults");
        r1->terminate(); r1->release();
        r2->terminate(); r2->release();
        g_results.push_back(c2);
    }

    // ===================== 6-9. mapper integration =========================
    {
        // helper: arm the mapper on a mono processor with a given request
        // spec, drive until the glide converges, return the voice ratios.
        // The parameters travel the REAL event path (offset-0 cluster in
        // the first block -- how a host delivers them).
        auto voiceRatiosAfter = [&](double key, double scale, double temp,
                                    const int interval[8],
                                    const double fine[8], double pitchSemi,
                                    std::vector<double>& outRatios) {
            auto* p = makeMono();
            // drive 6 s of a 220 Hz tone: the geometric glide 0.9995^n
            // fully converges (0.9995^288000 ~ e^-144 ~ 0)
            const int n = 6 * 48000;
            const std::vector<float> in = makeTone(220.0, 48000, n, 0.3);
            std::vector<float> out(n);
            {
                std::vector<std::pair<uint32_t, double>> params;
                params.emplace_back(vw::ParamId::kPitch, pitchSemi);
                params.emplace_back(vw::ParamId::kVoices, 8.0);
                params.emplace_back(vw::ParamId::kSpread, 0.0);
                params.emplace_back(vw::kHarmArm, 1.0);
                params.emplace_back(vw::kHarmKey, key);
                params.emplace_back(vw::kHarmScale, scale);
                params.emplace_back(vw::kHarmTemp, temp);
                for (int v = 0; v < 8; ++v) {
                    params.emplace_back(vw::harmIntId(v), double(interval[v]));
                    params.emplace_back(vw::harmFineId(v), fine[v]);
                }
                applyViaEvents(p, params, in.data(), out.data(), 4096);
            }
            for (int off = 4096; off < n; off += 4096)
                driveBlock(p, in.data() + off, out.data() + off,
                           std::min(4096, n - off));
            outRatios.clear();
            for (int v = 0; v < 8; ++v)
                outRatios.push_back(p->engineView().voiceStats(0, v).ratio);
            p->terminate();
            p->release();
        };

        // -- 6. mapper_armed_ratios ----------------------------------------
        {
            CaseResult c{"mapper_armed_ratios", true, ""};
            const int iv[8] = {14, 21, 18, 8, 26, 11, 4, 16};
            const double fc[8] = {0.0, 5.0, -7.0, 12.0, -25.0, 31.0, -40.0, 49.0};
            std::vector<double> got;
            voiceRatiosAfter(13.0, 1.0, 0.0, iv, fc, 0.0, got);
            octavox::harmony::MapRequest req;
            req.key = 13;
            req.scale = 1;
            req.temperament = octavox::harmony::Temperament::Equal12;
            for (int v = 0; v < 8; ++v) {
                req.voices[v].interval = iv[v];
                req.voices[v].fine_cents = toEnginePlain(vw::harmFineId(v), fc[v]);
            }
            const octavox::harmony::MapResult res = octavox::harmony::mapRow(req);
            double worst = 0.0;
            for (int v = 0; v < 8; ++v)
                worst = std::max(worst, std::abs(got[v] / res.ratio[v] - 1.0));
            check(c, worst < 1e-9,
                  "armed ratios must equal mapRow output; worst rel err " +
                      std::to_string(worst));
            g_results.push_back(c);
        }

        // -- 7. mapper_all_scales (19 rows x 5 temperaments) ----------------
        {
            CaseResult c{"mapper_all_scales", true, ""};
            const int iv[8] = {14, 14, 14, 14, 14, 14, 14, 14};
            const double fc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            int combos = 0, bad = 0;
            double worst = 0.0;
            // spot-drive every row x temperament at voice 0 (unison); the
            // full 8-voice law is the same mapRow call (verified per-voice
            // in case 6). One drive per combo x 95 would cost ~8 min, so
            // cases here use 2 s convergence (0.9995^96000 ~ e^-48) with a
            // tolerance far above the residual.
            auto* p = makeMono();
            const int n = 2 * 48000;
            const std::vector<float> in = makeTone(220.0, 48000, n, 0.3);
            std::vector<float> out(n);
            // base surface via the event path; interval 21 on voice 0 is
            // SCALE-SENSITIVE (the walk descends the row's steps), so every
            // row x temperament combo is genuinely distinct
            {
                std::vector<std::pair<uint32_t, double>> params;
                params.emplace_back(vw::ParamId::kVoices, 8.0);
                params.emplace_back(vw::ParamId::kSpread, 0.0);
                params.emplace_back(vw::kHarmArm, 1.0);
                params.emplace_back(vw::kHarmKey, 13.0);
                params.emplace_back(vw::harmIntId(0), 21.0);
                applyViaEvents(p, params, in.data(), out.data(), 4096);
            }
            for (int off = 4096; off < n; off += 4096)
                driveBlock(p, in.data() + off, out.data() + off,
                           std::min(4096, n - off));
            for (int row = 0; row <= 18; ++row) {
                for (int temp = 0; temp <= 4; ++temp) {
                    // re-target scale/temperament via the event path
                    {
                        std::vector<std::pair<uint32_t, double>> params;
                        params.emplace_back(vw::kHarmScale, double(row));
                        params.emplace_back(vw::kHarmTemp, double(temp));
                        applyViaEvents(p, params, in.data(), out.data(), 4096);
                    }
                    for (int off = 4096; off < n; off += 4096)
                        driveBlock(p, in.data() + off, out.data() + off,
                                   std::min(4096, n - off));
                    const double got = p->engineView().voiceStats(0, 0).ratio;
                    octavox::harmony::MapRequest req;
                    req.key = 13;
                    req.scale = row;
                    req.temperament = static_cast<octavox::harmony::Temperament>(temp);
                    req.voices[0].interval = 21;
                    req.voices[0].fine_cents = 0.0;
                    const double want =
                        octavox::harmony::mapRow(req).ratio[0];
                    const double err = std::abs(got / want - 1.0);
                    worst = std::max(worst, err);
                    ++combos;
                    if (err > 1e-6) ++bad;   // residual glide < e^-48 ~ 1e-21
                }
            }
            check(c, bad == 0,
                  "all 19x5 scale/temperament combos must match mapRow; bad " +
                      std::to_string(bad) + "/" + std::to_string(combos) +
                      " worst " + std::to_string(worst));
            p->terminate();
            p->release();
            g_results.push_back(c);
        }

        // -- 8. mapper_all_intervals ----------------------------------------
        {
            CaseResult c{"mapper_all_intervals", true, ""};
            auto* p = makeMono();
            const int n = 2 * 48000;
            const std::vector<float> in = makeTone(220.0, 48000, n, 0.3);
            std::vector<float> out(n);
            {
                std::vector<std::pair<uint32_t, double>> params;
                params.emplace_back(vw::ParamId::kVoices, 1.0);
                params.emplace_back(vw::ParamId::kSpread, 0.0);
                params.emplace_back(vw::kHarmArm, 1.0);
                params.emplace_back(vw::kHarmKey, 13.0);
                params.emplace_back(vw::kHarmScale, 1.0);
                applyViaEvents(p, params, in.data(), out.data(), 4096);
            }
            for (int off = 4096; off < n; off += 4096)
                driveBlock(p, in.data() + off, out.data() + off,
                           std::min(4096, n - off));
            int bad = 0;
            double worst = 0.0;
            for (int iv = 0; iv <= 28; ++iv) {
                // fine cents sweep alongside: -50, 0, +50
                const double fines[3] = {-50.0, 0.0, 50.0};
                for (double fc : fines) {
                    {
                        std::vector<std::pair<uint32_t, double>> params;
                        params.emplace_back(vw::harmIntId(0), double(iv));
                        params.emplace_back(vw::harmFineId(0), fc);
                        applyViaEvents(p, params, in.data(), out.data(), 4096);
                    }
                    for (int off = 4096; off < n; off += 4096)
                        driveBlock(p, in.data() + off, out.data() + off,
                                   std::min(4096, n - off));
                    const double got = p->engineView().voiceStats(0, 0).ratio;
                    octavox::harmony::MapRequest req;
                    req.key = 13;
                    req.scale = 1;
                    req.temperament = octavox::harmony::Temperament::Equal12;
                    req.voices[0].interval = iv;
                    req.voices[0].fine_cents =
                        toEnginePlain(vw::harmFineId(0), fc);
                    const double want =
                        octavox::harmony::mapRow(req).ratio[0];
                    const double err = std::abs(got / want - 1.0);
                    worst = std::max(worst, err);
                    if (err > 1e-6) ++bad;
                }
            }
            check(c, bad == 0,
                  "all 29 intervals x 3 fine positions must match; bad " +
                      std::to_string(bad) + " worst " + std::to_string(worst));
            p->terminate();
            p->release();
            g_results.push_back(c);
        }

        // -- 9. mapper_disarmed (legacy path unchanged) ---------------------
        {
            CaseResult c{"mapper_disarmed", true, ""};
            auto* p = makeMono();
            const int n = 2 * 48000;
            const std::vector<float> in = makeTone(220.0, 48000, n, 0.3);
            std::vector<float> out(n);
            {
                // spread ON + NON-default mapper params with the arm OFF:
                // they must be ignored (the legacy spread law stands)
                std::vector<std::pair<uint32_t, double>> params;
                params.emplace_back(vw::ParamId::kVoices, 8.0);
                params.emplace_back(vw::ParamId::kSpread, 1.0);
                params.emplace_back(vw::ParamId::kPitch, 0.0);
                params.emplace_back(vw::kHarmKey, 2.0);
                params.emplace_back(vw::kHarmScale, 17.0);
                params.emplace_back(vw::kHarmTemp, 4.0);
                params.emplace_back(vw::harmIntId(0), 2.0);
                applyViaEvents(p, params, in.data(), out.data(), 4096);
            }
            for (int off = 4096; off < n; off += 4096)
                driveBlock(p, in.data() + off, out.data() + off,
                           std::min(4096, n - off));
            // legacy law: the core's recovered spread table with zero
            // offsets (dsp.cpp kSpreadSemitones, verbatim)
            static const double kSpreadSemi[8] = {0.0, 7.0, 4.0, 12.0,
                                                  -5.0, -12.0, 3.0, -3.0};
            int bad = 0;
            for (int v = 0; v < 8; ++v) {
                const double want = std::pow(2.0, kSpreadSemi[v] / 12.0);
                const double got = p->engineView().voiceStats(0, v).ratio;
                if (std::abs(got / want - 1.0) > 1e-6) ++bad;
            }
            check(c, bad == 0,
                  "disarmed mapper params must not disturb the spread law; "
                  "bad voices: " + std::to_string(bad));
            p->terminate();
            p->release();
            g_results.push_back(c);
        }
    }

    // ===================== 10. param_surface_74 ============================
    {
        CaseResult c{"param_surface_74", true, ""};
        check(c, vw::kNumParams == 74, "kNumParams must be 74");
        int bad = 0, emptyHelp = 0;
        for (uint32_t id = 0; id < static_cast<uint32_t>(vw::kNumParams); ++id) {
            const auto& sp = vw::paramSpec(id);
            if (sp.id != id) ++bad;
            if (sp.defaultPlain < sp.domain.minPlain ||
                sp.defaultPlain > sp.domain.maxPlain)
                ++bad;
            const char* h = vw::paramHelp(id);
            if (!h || !h[0]) ++emptyHelp;
        }
        check(c, bad == 0, "spec table integrity failures: " + std::to_string(bad));
        check(c, emptyHelp == 0, "empty help texts: " + std::to_string(emptyHelp));
        g_results.push_back(c);
    }

    // ---- report ------------------------------------------------------------
    int fails = 0;
    for (const auto& r : g_results) {
        std::printf("%-26s %-4s %s\n", r.name.c_str(), r.pass ? "ok" : "FAIL",
                    r.note.c_str());
        if (!r.pass) ++fails;
    }
    std::printf("\nR22 state/mapper battery: %zu cases, %d failed\n",
                g_results.size(), fails);
    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (f) {
            std::fprintf(f, "{\n  \"tool\": \"octavox_r22_state\",\n  \"round\": 22,\n");
            std::fprintf(f, "  \"cases\": [\n");
            for (size_t i = 0; i < g_results.size(); ++i)
                std::fprintf(f, "    {\"name\": \"%s\", \"ok\": %s, \"note\": \"%s\"}%s\n",
                             g_results[i].name.c_str(),
                             g_results[i].pass ? "true" : "false",
                             g_results[i].note.c_str(),
                             (i + 1 < g_results.size()) ? "," : "");
            std::fprintf(f, "  ],\n  \"all_pass\": %s\n}\n", fails ? "false" : "true");
            std::fclose(f);
            std::printf("json written: %s\n", jsonPath);
        }
    }
    return fails == 0 ? 0 : 1;
}
