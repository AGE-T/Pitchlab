// ---------------------------------------------------------------------------
// OCTAVOX native core -- VST3 MODULE INTEGRATION HARNESS (R18-P6)
// ============================================================================
// PROVENANCE: this harness tests OUR OWN independent harmonizer plugin
// (OctavoxNative.vst3, built from native/vst3 around the octavox native DSP
// core). It is NOT a reproduction of, and contains no code from, the original
// Eventide Octavox plug-in; the original binary was never executed.
//
// SCOPE (labelled honestly): this harness loads the ACTUAL COMPILED .vst3
// BUNDLE through the pinned Steinberg SDK's module loader
// (VST3::Hosting::Module::create -- a real dlopen of the bundle's .so and a
// real GetPluginFactory() call) and drives the exported factory's instances
// through the standard VST3 host interfaces: component creation, initialize
// with a real SDK HostApplication context, bus negotiation (mono and
// stereo), parameter queues, process() with deterministic buffers, state
// save/restore through IBStream, latency reporting and proper
// termination/unload.  The outputs are compared BIT-EXACTLY against the
// hand-driven DSP core reference (the same octavox core library, driven
// directly) under identical conditions.
//
// WHAT THIS IS NOT: this is an SDK-module-load + interface-contract test on
// ONE host implementation (the SDK's own hosting utilities on Linux).  It is
// NOT a DAW compatibility claim: no DAW was involved, and passing here does
// not certify behaviour in any specific DAW.  The SDK validator run remains
// the separate complementary check (see VST3_WRAPPER.md).
//
// No GUI/editor is exercised (the wrapper deliberately has none).
// ---------------------------------------------------------------------------
#include "octavox_params.h"
#include "octavox_ids.h"
#include "octavox/dsp.hpp"

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/base/funknown.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

//==========================================================================
// harness plumbing (the same shape as the project's other test mains)
//==========================================================================
namespace {

struct CaseResult {
    std::string name;
    bool pass = false;
    std::string note;
    std::vector<std::pair<std::string, double>> values;
};

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
        else if ((unsigned char)ch < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", (unsigned)ch);
            o += b;
        } else o += ch;
    }
    return o;
}

void addValue(CaseResult& c, const std::string& k, double v) {
    c.values.emplace_back(k, v);
}

bool check(CaseResult& c, bool ok, const std::string& what) {
    if (!ok) {
        if (!c.note.empty()) c.note += "; ";
        c.note += what;
    }
    return ok;
}

int g_failures = 0;

void pushCase(std::vector<CaseResult>& R, CaseResult&& c) {
    if (!c.pass) ++g_failures;
    std::printf("[%-34s] %-4s %s\n", c.name.c_str(), c.pass ? "ok" : "FAIL",
                c.note.c_str());
    for (const auto& v : c.values)
        std::printf("%-38s  %s = %.10g\n", "", v.first.c_str(), v.second);
    std::fflush(stdout);
    R.push_back(std::move(c));
}

// deterministic LCG (same spec as the core's test harness)
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed ? seed : 1) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return (static_cast<double>(s >> 11) / 9007199254740992.0) * 0.5 - 0.25;
    }
    void fill(std::vector<float>& v) {
        for (auto& x : v) x = static_cast<float>(next());
    }
};

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 1e30;
    double d = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double x = std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        if (x > d) d = x;
    }
    return d;
}

bool bitIdentical(const float* a, const float* b, size_t n) {
    return std::memcmp(a, b, n * sizeof(float)) == 0;
}

double toEnginePlain(uint32_t id, double plain) {
    return octavox::vst3wrap::plainFromNormalized(
        id, octavox::vst3wrap::normalizedFromPlain(id, plain));
}

//==========================================================================
// the loaded-module session (the harness's "host")
//==========================================================================
VST3::Hosting::Module::Ptr g_module;
IPtr<IComponent> g_component;
IPtr<IAudioProcessor> g_audio;
IPtr<IEditController> g_edit;
HostApplication* g_hostContext = nullptr;

struct Setup {
    double sampleRate = 48000.0;
    int32 maxBlock = 4096;
};

// full standard lifecycle on the loaded instance; returns false on failure
bool setupLoadedInstance(const Setup& st, int channels) {
    if (!g_audio) return false;
    SpeakerArrangement in = channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
    SpeakerArrangement out = in;
    if (g_audio->setBusArrangements(&in, 1, &out, 1) != kResultOk) return false;
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = st.sampleRate;
    ps.maxSamplesPerBlock = st.maxBlock;
    if (g_audio->setupProcessing(ps) != kResultOk) return false;
    if (g_component->setActive(true) != kResultOk) return false;
    return g_audio->setProcessing(true) == kResultOk;
}

void teardownLoadedInstance() {
    if (g_audio) g_audio->setProcessing(false);
    if (g_component) g_component->setActive(false);
}

// one host process() call through the loaded module
void driveLoaded(float* const* in, float* const* out, int channels, int n,
                 IParameterChanges* changes) {
    AudioBusBuffers ib{}, ob{};
    ib.numChannels = channels;
    ib.channelBuffers32 = const_cast<float**>(in);
    ob.numChannels = channels;
    ob.channelBuffers32 = const_cast<float**>(out);
    AudioBusBuffers inB[1] = {ib}, outB[1] = {ob};
    ProcessData pd{};
    pd.processMode = kRealtime;
    pd.symbolicSampleSize = kSample32;
    pd.numSamples = n;
    pd.inputs = inB;
    pd.numInputs = 1;
    pd.outputs = outB;
    pd.numOutputs = 1;
    pd.inputParameterChanges = changes;
    g_audio->process(pd);
}

void addEvent(ParameterChanges& changes, uint32_t id,
              int32 offset, double norm) {
    int32 idx = 0;
    IParamValueQueue* q = changes.addParameterData(id, idx);
    if (q) {
        int32 pidx = 0;
        q->addPoint(offset, norm, pidx);
    }
}

}  // namespace

//==========================================================================
// the cases
//==========================================================================
namespace {

void caseModuleLoad(CaseResult& c, const std::string& modulePath) {
    std::string err;
    g_module = VST3::Hosting::Module::create(modulePath, err);
    check(c, g_module != nullptr, "Module::create failed: " + err);
    if (!g_module) return;
    const auto& factory = g_module->getFactory();
    check(c, factory.get() != nullptr, "no IPluginFactory exported");
    const auto infos = factory.classInfos();
    addValue(c, "factory_class_count", (double)infos.size());
    bool found = false;
    for (const auto& ci : infos)
        if (std::memcmp(ci.ID().data(), octavox::vst3wrap::OctavoxNativeProcessorUID,
                         sizeof(TUID)) == 0)
            found = true;
    check(c, found, "the processor class ID is not among the factory classes");
    check(c, !infos.empty() && infos[0].category().find("Audio Module Class") !=
                                    std::string::npos,
          "unexpected class category");
    c.pass = c.note.empty();
}

void caseProcessorCreation(CaseResult& c) {
    if (!g_module) { c.note = "no module"; return; }
    const auto& factory = g_module->getFactory();
    g_component = factory.createInstance<IComponent>(
        VST3::UID(octavox::vst3wrap::OctavoxNativeProcessorUID));
    check(c, g_component != nullptr, "createInstance(IComponent) failed");
    if (!g_component) return;
    {
        IAudioProcessor* ap = nullptr;
        if (g_component->queryInterface(IAudioProcessor::iid,
                                        reinterpret_cast<void**>(&ap)) == kResultTrue &&
            ap)
            g_audio = IPtr<IAudioProcessor>(ap);  // adopts the add-ref'd ptr
    }
    check(c, g_audio != nullptr, "component does not export IAudioProcessor");
    {
        IEditController* ec = nullptr;
        if (g_component->queryInterface(IEditController::iid,
                                        reinterpret_cast<void**>(&ec)) == kResultTrue &&
            ec)
            g_edit = IPtr<IEditController>(ec);
    }
    check(c, g_edit != nullptr,
          "component does not export IEditController (single-component)");
    c.pass = c.note.empty();
}

void caseComponentInit(CaseResult& c) {
    if (!g_component) { c.note = "no component"; return; }
    static HostApplication hostCtx;  // the SDK hosting utility
    g_hostContext = &hostCtx;
    const tresult r = g_component->initialize(&hostCtx);
    check(c, r == kResultOk, "initialize(host context) failed");
    const int32 inBuses = g_component->getBusCount(MediaTypes::kAudio, BusDirections::kInput);
    const int32 outBuses = g_component->getBusCount(MediaTypes::kAudio, BusDirections::kOutput);
    addValue(c, "audio_input_buses", (double)inBuses);
    addValue(c, "audio_output_buses", (double)outBuses);
    check(c, inBuses == 1 && outBuses == 1,
          "expected symmetric single Main audio buses");
    // the parameter surface is published at initialization (host contract)
    check(c, g_edit->getParameterCount() == 38,
          "getParameterCount() != 38 on the loaded instance after initialize");
    addValue(c, "parameter_count", (double)g_edit->getParameterCount());
    c.pass = c.note.empty();
}

void caseBusMono(CaseResult& c) {
    if (!g_audio) { c.note = "no processor"; return; }
    SpeakerArrangement in = SpeakerArr::kMono, out = SpeakerArr::kMono;
    const tresult r = g_audio->setBusArrangements(&in, 1, &out, 1);
    check(c, r == kResultOk, "mono negotiation rejected");
    SpeakerArrangement gin = SpeakerArr::kEmpty, gout = SpeakerArr::kEmpty;
    g_audio->getBusArrangement(BusDirections::kInput, 0, gin);
    g_audio->getBusArrangement(BusDirections::kOutput, 0, gout);
    check(c, SpeakerArr::getChannelCount(gin) == 1 &&
                 SpeakerArr::getChannelCount(gout) == 1,
          "negotiated mono not reflected by getBusArrangement");
    c.pass = c.note.empty();
}

void caseBusStereo(CaseResult& c) {
    if (!g_audio) { c.note = "no processor"; return; }
    SpeakerArrangement in = SpeakerArr::kStereo, out = SpeakerArr::kStereo;
    const tresult r = g_audio->setBusArrangements(&in, 1, &out, 1);
    check(c, r == kResultOk, "stereo negotiation rejected");
    SpeakerArrangement gin = SpeakerArr::kEmpty, gout = SpeakerArr::kEmpty;
    g_audio->getBusArrangement(BusDirections::kInput, 0, gin);
    g_audio->getBusArrangement(BusDirections::kOutput, 0, gout);
    check(c, SpeakerArr::getChannelCount(gin) == 2 &&
                 SpeakerArr::getChannelCount(gout) == 2,
          "negotiated stereo not reflected");
    check(c, g_audio->canProcessSampleSize(kSample32) == kResultTrue,
          "canProcessSampleSize(kSample32) != kResultTrue");
    c.pass = c.note.empty();
}

void caseActivationLifecycle(CaseResult& c) {
    if (!g_component || !g_audio) { c.note = "no processor"; return; }
    check(c, setupLoadedInstance(Setup{}, 1), "setupProcessing/setActive/setProcessing chain failed");
    teardownLoadedInstance();
    // re-activation must work (hosts activate/deactivate around latency changes)
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = 48000.0;
    ps.maxSamplesPerBlock = 2048;
    check(c, g_audio->setupProcessing(ps) == kResultOk, "re-setupProcessing failed");
    check(c, g_component->setActive(true) == kResultOk, "re-activate failed");
    check(c, g_audio->setProcessing(true) == kResultOk, "re-setProcessing failed");
    teardownLoadedInstance();
    c.pass = c.note.empty();
}

// mono: the module's output == the hand-driven core, bit-exact
void caseProcessMono(CaseResult& c) {
    if (!g_audio) { c.note = "no processor"; return; }
    const int N = 65536;
    Lcg rng(987654321);
    std::vector<float> in(N), modOut(N), refOut(N);
    rng.fill(in);
    // ---- the hand-driven reference (the core library linked directly) ----
    {
        octavox::MultichannelShifter ref;
        check(c, ref.prepare(octavox::Config{}, 1), "reference prepare failed");
        float* inP[1] = {in.data()};
        float* outP[1] = {refOut.data()};
        ref.process(inP, outP, 1, N);
    }
    // ---- the loaded module ----
    check(c, setupLoadedInstance(Setup{}, 1), "activation failed");
    for (int off = 0; off < N; off += 4096) {
        const int n = std::min(4096, N - off);
        float* inP[1] = {in.data() + off};
        float* outP[1] = {modOut.data() + off};
        driveLoaded(inP, outP, 1, n, nullptr);
    }
    teardownLoadedInstance();
    const bool bitex = bitIdentical(modOut.data(), refOut.data(), N);
    const double d = maxAbsDiff(modOut, refOut);
    addValue(c, "mono_max_abs_diff", d);
    addValue(c, "mono_bit_exact", bitex ? 1.0 : 0.0);
    check(c, bitex, "mono stream is NOT bit-identical to the hand-driven core");
    c.pass = c.note.empty();
}

// stereo: per-channel distinct inputs, both channels bit-exact
void caseProcessStereo(CaseResult& c) {
    if (!g_audio) { c.note = "no processor"; return; }
    const int N = 49152;  // not a multiple of the block size on purpose
    Lcg rng(24680);
    std::vector<float> inL(N), inR(N), outL(N), outR(N), refL(N), refR(N);
    rng.fill(inL);
    rng.fill(inR);
    {
        octavox::MultichannelShifter ref;
        check(c, ref.prepare(octavox::Config{}, 2), "reference prepare failed");
        float* inP[2] = {inL.data(), inR.data()};
        float* outP[2] = {refL.data(), refR.data()};
        ref.process(inP, outP, 2, N);
    }
    check(c, setupLoadedInstance(Setup{}, 2), "activation failed");
    for (int off = 0; off < N; off += 3000) {  // odd block sizes
        const int n = std::min(3000, N - off);
        float* inP[2] = {inL.data() + off, inR.data() + off};
        float* outP[2] = {outL.data() + off, outR.data() + off};
        driveLoaded(inP, outP, 2, n, nullptr);
    }
    teardownLoadedInstance();
    const bool bitex = bitIdentical(outL.data(), refL.data(), N) &&
                       bitIdentical(outR.data(), refR.data(), N);
    addValue(c, "stereo_L_max_abs_diff", maxAbsDiff(outL, refL));
    addValue(c, "stereo_R_max_abs_diff", maxAbsDiff(outR, refR));
    addValue(c, "stereo_bit_exact", bitex ? 1.0 : 0.0);
    check(c, bitex, "stereo stream is NOT bit-identical to the hand-driven core");
    c.pass = c.note.empty();
}

// parameter queues: a full non-default surface applied through real
// IParameterChanges queues at stream position 0 == the hand-driven reference
// with the canonical application order (the wrapper test's proven pattern,
// now through the loaded module); plus a mid-block event drop-control.
void caseParamQueues(CaseResult& c) {
    if (!g_audio) { c.note = "no processor"; return; }
    using octavox::vst3wrap::ParamId;
    using octavox::vst3wrap::voiceOffsetId;
    using octavox::vst3wrap::voiceGainId;
    using octavox::vst3wrap::voiceMuteId;
    using octavox::vst3wrap::voiceDelayId;
    const int N = 65536;
    struct Ev { uint32_t id; double plain; };
    const std::vector<Ev> surf = {
        {ParamId::kPitch, 2.0},
        {voiceOffsetId(3), 7.0},
        {voiceGainId(5), 0.5},
        {voiceMuteId(6), 1.0},
        {voiceDelayId(2), 20.0},
        {ParamId::kVoices, 8.0},
        {ParamId::kWetDry, 0.7},
    };
    Lcg rng(555777);
    std::vector<float> in(N);
    rng.fill(in);
    // ---- hand-driven reference (canonical order) ----
    std::vector<float> refOut(N);
    {
        octavox::MultichannelShifter ref;
        octavox::Config cfg = octavox::Config{};
        check(c, ref.prepare(cfg, 1), "reference prepare failed");
        const double sr = cfg.sample_rate, grain = cfg.grain;
        const double pitch = toEnginePlain(ParamId::kPitch, 2.0);
        const double off3 = toEnginePlain(voiceOffsetId(3), 7.0);
        const double g5 = toEnginePlain(voiceGainId(5), 0.5);
        const bool m6 = toEnginePlain(voiceMuteId(6), 1.0) >= 0.5;
        const double d2ms = toEnginePlain(voiceDelayId(2), 20.0);
        const int voices = (int)std::lround(toEnginePlain(ParamId::kVoices, 8.0));
        const double wet = toEnginePlain(ParamId::kWetDry, 0.7);
        ref.setSpread(true);
        ref.setRatio(std::pow(2.0, pitch / 12.0));
        ref.setVoiceRatio(3, std::pow(2.0, pitch / 12.0) * std::pow(2.0, off3 / 12.0));
        ref.setVoiceGain(5, g5);
        ref.setVoiceMute(6, m6);
        ref.setVoiceBaseDelay(2, octavox::vst3wrap::msToBaseDelaySamples(d2ms, sr, grain));
        ref.setVoices(voices);
        ref.setFeedback(0.0);
        ref.setWetDry(wet);
        ref.setBypass(false);
        float* inP[1] = {in.data()};
        float* outP[1] = {refOut.data()};
        ref.process(inP, outP, 1, N);
    }
    // ---- the loaded module with a position-0 queue in canonical order ----
    std::vector<float> aOut(N);
    check(c, setupLoadedInstance(Setup{}, 1), "activation failed");
    {
        ParameterChanges ch;
        for (const auto& e : surf)
            addEvent(ch, e.id, 0,
                     octavox::vst3wrap::normalizedFromPlain(e.id, e.plain));
        float* inP[1] = {in.data()};
        float* outP[1] = {aOut.data()};
        driveLoaded(inP, outP, 1, 4096, &ch);
        float* in2[1] = {in.data() + 4096};
        float* out2[1] = {aOut.data() + 4096};
        driveLoaded(in2, out2, 1, N - 4096, nullptr);
    }
    teardownLoadedInstance();
    const double dLive = maxAbsDiff(aOut, refOut);
    addValue(c, "queue_surface_vs_reference_maxdiff", dLive);
    check(c, dLive == 0.0,
          "queued surface application is not bit-identical to the reference");
    // ---- drop-control: a mid-block event must change the stream vs none ----
    std::vector<float> bOut(N), cOut(N);
    check(c, setupLoadedInstance(Setup{}, 1), "re-activation failed");
    {
        ParameterChanges ch;
        addEvent(ch, ParamId::kWetDry, 2000, 0.25);  // mid-block event
        for (int off = 0; off < N; off += 4096) {
            const int n = std::min(4096, N - off);
            float* inP[1] = {in.data() + off};
            float* outP[1] = {bOut.data() + off};
            driveLoaded(inP, outP, 1, n, off == 0 ? &ch : nullptr);
        }
        for (int off = 0; off < N; off += 4096) {
            const int n = std::min(4096, N - off);
            float* inP[1] = {in.data() + off};
            float* outP[1] = {cOut.data() + off};
            driveLoaded(inP, outP, 1, n, nullptr);
        }
    }
    teardownLoadedInstance();
    const double dDrop = maxAbsDiff(bOut, cOut);
    addValue(c, "midblock_event_effect_maxdiff", dDrop);
    check(c, dDrop > 0.0,
          "the mid-block parameter event had no effect (queue not consumed)");
    c.pass = c.note.empty();
}

// state save through the module's IComponent::getState, restore into a
// SECOND factory-created instance, both continue bit-identically
void caseStateSaveRestore(CaseResult& c) {
    if (!g_module || !g_audio) { c.note = "no module"; return; }
    using octavox::vst3wrap::ParamId;
    const int N = 32768;
    Lcg rng(13579);
    std::vector<float> in(N);
    rng.fill(in);
    MemoryStream state;
    std::vector<float> run1(N), run2(N);
    check(c, setupLoadedInstance(Setup{}, 1), "activation failed");
    {
        // a non-default surface via the queue, then run, then save
        ParameterChanges ch;
        addEvent(ch, ParamId::kPitch, 0,
                 octavox::vst3wrap::normalizedFromPlain(ParamId::kPitch, -5.0));
        addEvent(ch, ParamId::kFeedback, 0,
                 octavox::vst3wrap::normalizedFromPlain(ParamId::kFeedback, 0.3));
        for (int off = 0; off < N; off += 4096) {
            const int n = std::min(4096, N - off);
            float* inP[1] = {in.data() + off};
            float* outP[1] = {run1.data() + off};
            driveLoaded(inP, outP, 1, n, off == 0 ? &ch : nullptr);
        }
        check(c, g_component->getState(&state) == kResultOk, "getState failed");
    }
    teardownLoadedInstance();
    addValue(c, "state_blob_bytes", (double)((int64_t)state.getSize()));
    check(c, state.getSize() > 4, "state blob suspiciously small");
    // ---- a SECOND instance from the same loaded factory ----
    IPtr<IComponent> comp2 = g_module->getFactory().createInstance<IComponent>(
        VST3::UID(octavox::vst3wrap::OctavoxNativeProcessorUID));
    check(c, comp2 != nullptr, "second instance creation failed");
    if (!comp2) { c.pass = false; return; }
    IPtr<IAudioProcessor> audio2;
    {
        IAudioProcessor* ap2 = nullptr;
        if (comp2->queryInterface(IAudioProcessor::iid,
                                  reinterpret_cast<void**>(&ap2)) == kResultTrue && ap2)
            audio2 = IPtr<IAudioProcessor>(ap2);
    }
    check(c, audio2 != nullptr, "second instance lacks IAudioProcessor");
    check(c, comp2->initialize(g_hostContext) == kResultOk,
          "second instance initialize failed");
    SpeakerArrangement mono = SpeakerArr::kMono;
    audio2->setBusArrangements(&mono, 1, &mono, 1);
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = 48000.0;
    ps.maxSamplesPerBlock = 4096;
    audio2->setupProcessing(ps);
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    check(c, comp2->setState(&state) == kResultOk, "setState into instance 2 failed");
    comp2->setActive(true);
    audio2->setProcessing(true);
    for (int off = 0; off < N; off += 4096) {
        const int n = std::min(4096, N - off);
        float* inP[1] = {in.data() + off};
        float* outP[1] = {run2.data() + off};
        AudioBusBuffers ib{}, ob{};
        ib.numChannels = 1;
        ib.channelBuffers32 = inP;
        ob.numChannels = 1;
        ob.channelBuffers32 = outP;
        AudioBusBuffers inB[1] = {ib}, outB[1] = {ob};
        ProcessData pd{};
        pd.processMode = kRealtime;
        pd.symbolicSampleSize = kSample32;
        pd.numSamples = n;
        pd.inputs = inB;
        pd.numInputs = 1;
        pd.outputs = outB;
        pd.numOutputs = 1;
        audio2->process(pd);
    }
    audio2->setProcessing(false);
    comp2->setActive(false);
    const bool bitex = bitIdentical(run1.data(), run2.data(), N);
    addValue(c, "restored_replay_bit_exact", bitex ? 1.0 : 0.0);
    addValue(c, "restored_replay_maxdiff", maxAbsDiff(run1, run2));
    check(c, bitex,
          "the restored second instance does not replay bit-identically");
    check(c, comp2->terminate() == kResultOk, "second instance terminate failed");
    c.pass = c.note.empty();
}

void caseLatencyReporting(CaseResult& c) {
    if (!g_audio) { c.note = "no processor"; return; }
    // the wrapper reports the core's latencySamples(); compare against the
    // core library's own value for the default config (the exact contract).
    octavox::MultichannelShifter ref;
    check(c, ref.prepare(octavox::Config{}, 2), "reference prepare failed");
    const uint32 expect = ref.latencySamples();
    const uint32 got = g_audio->getLatencySamples();
    addValue(c, "expected_latency_samples", (double)expect);
    addValue(c, "reported_latency_samples", (double)got);
    check(c, got == expect,
          "the module's getLatencySamples() != the core's latencySamples()");
    const uint32 tail = g_audio->getTailSamples();
    addValue(c, "reported_tail_samples", (double)tail);
    check(c, tail > 0, "tail samples not reported");
    c.pass = c.note.empty();
}

void caseTerminateUnload(CaseResult& c) {
    if (!g_component) { c.note = "no component"; return; }
    check(c, g_component->terminate() == kResultOk, "terminate failed");
    // release every interface, then destroy the Module (dlclose)
    g_edit = nullptr;
    g_audio = nullptr;
    g_component = nullptr;
    const std::string modName = g_module ? g_module->getName() : "";
    g_module = nullptr;  // ~Module -> library unload
    addValue(c, "module_name_len", (double)modName.size());
    check(c, true, "");
    c.pass = c.note.empty();
}

}  // namespace

//==========================================================================
// main
//==========================================================================
int main(int argc, char** argv) {
    std::string modulePath, jsonPath;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--module" && i + 1 < argc) modulePath = argv[++i];
        else if (a == "--json" && i + 1 < argc) jsonPath = argv[++i];
        else {
            std::fprintf(stderr, "usage: %s [--module <path/to/OctavoxNative.vst3>] "
                                 "[--json <path>]\n", argv[0]);
            return 2;
        }
    }
    // default: <exe_dir>/../../VST3/Release/OctavoxNative.vst3
    if (modulePath.empty()) {
        std::string exe = argv[0];
        const size_t slash = exe.find_last_of('/');
        const std::string dir = slash == std::string::npos ? "." : exe.substr(0, slash);
        const char* cands[] = {
            "/../../VST3/Release/OctavoxNative.vst3",
            "/../../../VST3/Release/OctavoxNative.vst3",
            "/OctavoxNative.vst3",
        };
        for (const char* suff : cands) {
            std::string p = dir + suff;
            FILE* f = std::fopen(p.c_str(), "rb");
            if (f) { std::fclose(f); modulePath = p; break; }
        }
        if (modulePath.empty()) {
            // last resort: relative to the CWD
            const char* rel[] = {"build/VST3/Release/OctavoxNative.vst3",
                                 "VST3/Release/OctavoxNative.vst3"};
            for (const char* p : rel) {
                FILE* f = std::fopen(p, "rb");
                if (f) { std::fclose(f); modulePath = p; break; }
            }
        }
    }
    if (modulePath.empty()) {
        std::fprintf(stderr, "error: OctavoxNative.vst3 not found "
                             "(build it first, or pass --module)\n");
        return 2;
    }
    std::printf("module: %s\n", modulePath.c_str());
    std::printf("harness scope: SDK module loader + factory + full interface "
                "contract vs the hand-driven core (NOT a DAW test)\n\n");

    std::vector<CaseResult> R;
    {
        CaseResult c{"module_load_factory"};
        caseModuleLoad(c, modulePath);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"processor_creation"};
        caseProcessorCreation(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"component_initialize_buses"};
        caseComponentInit(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"bus_negotiation_mono"};
        caseBusMono(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"bus_negotiation_stereo"};
        caseBusStereo(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"activation_lifecycle"};
        caseActivationLifecycle(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"process_mono_bitexact"};
        caseProcessMono(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"process_stereo_bitexact"};
        caseProcessStereo(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"param_queue_events"};
        caseParamQueues(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"state_save_restore_second_instance"};
        caseStateSaveRestore(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"latency_reporting"};
        caseLatencyReporting(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"terminate_unload"};
        caseTerminateUnload(c);
        pushCase(R, std::move(c));
    }

    int passed = 0, failed = 0;
    for (const auto& c : R) (c.pass ? passed : failed)++;
    std::printf("\n%s: %d passed, %d failed, %d total\n",
                "octavox-module-integration", passed, failed, (int)R.size());
    std::printf("TOTAL_CASES %d\n", (int)R.size());

    if (!jsonPath.empty()) {
        FILE* f = std::fopen(jsonPath.c_str(), "w");
        if (f) {
            std::fprintf(f, "{\n  \"suite\": \"octavox-module-integration\",\n");
            std::fprintf(f, "  \"module_path\": \"%s\",\n",
                         jsonEscape(modulePath).c_str());
            std::fprintf(f, "  \"cases\": [\n");
            bool first = true;
            for (const auto& c : R) {
                if (!first) std::fprintf(f, ",\n");
                first = false;
                std::fprintf(f, "    {\"name\": \"%s\", \"pass\": %s",
                             jsonEscape(c.name).c_str(), c.pass ? "true" : "false");
                if (!c.values.empty()) {
                    std::fprintf(f, ", \"values\": {");
                    for (size_t i = 0; i < c.values.size(); ++i) {
                        if (i) std::fprintf(f, ", ");
                        std::fprintf(f, "\"%s\": %.10g",
                                     jsonEscape(c.values[i].first).c_str(),
                                     c.values[i].second);
                    }
                    std::fprintf(f, "}");
                }
                std::fprintf(f, ", \"note\": \"%s\"}",
                             jsonEscape(c.note).c_str());
            }
            std::fprintf(f, "\n  ],\n");
            std::fprintf(f, "  \"summary\": {\"passed\": %d, \"failed\": %d, "
                            "\"total\": %d}\n}\n",
                         passed, failed, (int)R.size());
            std::fclose(f);
        }
    }
    return failed == 0 ? 0 : 1;
}
