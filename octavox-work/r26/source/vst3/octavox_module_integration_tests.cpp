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
// R18-WDB (Windows dual build): the harness is VARIANT-AWARE.  The bundle
// it looks for, the class FUID it queries and the hand-driven core
// reference all come from the central build configuration helper
// (octavox_build_config.h): an OBSKERNEL6 module is compared against an
// OBSKERNEL6 reference -- NEVER against a LAGRANGE6 reference.  The module
// path can be given explicitly (--module <path>); the default candidates
// use this build's bundle name (OctavoxRE_<Variant>.vst3).
//
// WHAT THIS IS NOT: this is an SDK-module-load + interface-contract test on
// ONE host implementation (the SDK's own hosting utilities on Linux).  It is
// NOT a DAW compatibility claim: no DAW was involved, and passing here does
// not certify behaviour in any specific DAW.  The SDK validator run remains
// the separate complementary check (see VST3_WRAPPER.md).
//
// R33: the minimal editor IS exercised (createView through the loaded
// module, and -- when DISPLAY is available -- a real X11 attach + painted
// -pixel probe; the deep logic/pixel-identity proof lives in the dedicated
// octavox_ui_tests suite).
// ---------------------------------------------------------------------------
#include "octavox_params.h"
#include "octavox_ids.h"
#include "octavox_telemetry_interface.h"
#include "octavox/dsp.hpp"
#include "octavox/telemetry.hpp"

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/base/funknown.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#if defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/select.h>
#include <unistd.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

// R31: the vendor telemetry types (kept explicit; the file is in the
// global namespace).
using octavox::vst3wrap::IPitchTelemetry;
using octavox::kTelIdCount;
using octavox::kTelCentsFromC4;
using octavox::kTelFreqRatio;
using octavox::kTelQ23Ratio;
using octavox::kTelPhaseConf;
using octavox::kTelConfidence;
using octavox::kTelNegCents;
using octavox::kTelPosCents;
using octavox::kTelemetryMaxVoices;

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

// --------------------------------------------------------------------------
// R31: a HostApplication that ALSO answers IComponentHandler and counts
// restartComponent(kLatencyChanged) -- the observer for the latency-truth
// notification test (the SDK's plain HostApplication does not implement
// IComponentHandler, so the wrapper's componentHandler stays null there).
// --------------------------------------------------------------------------
class LatencyCountingHost : public HostApplication, public IComponentHandler {
public:
    int latencyRestarts = 0;

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) SMTG_OVERRIDE {
        QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
        return HostApplication::queryInterface(iid, obj);
    }
    tresult PLUGIN_API restartComponent(int32 flags) SMTG_OVERRIDE {
        if (flags & kLatencyChanged) ++latencyRestarts;
        return kResultOk;
    }
    tresult PLUGIN_API beginEdit(ParamID /*id*/) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID /*id*/, ParamValue /*v*/) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID /*id*/) SMTG_OVERRIDE { return kResultOk; }

    // the SDK pattern for an extra FUnknown-derived interface on an
    // FObject-based class (both FUnknown subobjects get the overrider)
    REFCOUNT_METHODS(HostApplication)
};

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
            // ADOPTS the queryInterface reference (addRef=false): the raw
            // pointer already owns its query ref.
            g_audio = IPtr<IAudioProcessor>(ap, false);
    }
    check(c, g_audio != nullptr, "component does not export IAudioProcessor");
    {
        IEditController* ec = nullptr;
        if (g_component->queryInterface(IEditController::iid,
                                        reinterpret_cast<void**>(&ec)) == kResultTrue &&
            ec)
            g_edit = IPtr<IEditController>(ec, false);
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
    // R18-WDB: the reference uses THIS build variant's interpolation
    // (octavox_build_config.h via defaultConfig()); an OBSKERNEL6 module is
    // never compared against a LAGRANGE6 reference.
    {
        octavox::MultichannelShifter ref;
        check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 1), "reference prepare failed");
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
        check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 2), "reference prepare failed");
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
        // R18-WDB: the reference engine uses THIS build variant's kernel.
        octavox::Config cfg = octavox::vst3wrap::defaultConfig();
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
            audio2 = IPtr<IAudioProcessor>(ap2, false);  // adopt the query ref
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
    // core library's own value for the variant's config (the exact contract).
    octavox::MultichannelShifter ref;
    check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 2), "reference prepare failed");
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

// ===== R31: the m02-style telemetry + latency-truth cases =================

// helper: fill `n` samples of a sine at `hz` (mono) starting at stream
// position `off0` (continuous phase across blocks).
void fillSine(float* dst, int n, int off0, double hz, double sr) {
    for (int i = 0; i < n; ++i)
        dst[i] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * hz *
                                                   static_cast<double>(off0 + i) / sr));
}

// The vendor interface is served through queryInterface (the R29 pattern).
void caseTelemetryVendorInterface(CaseResult& c) {
    if (!g_audio) { c.note = "no processor"; return; }
    IPitchTelemetry* tel = nullptr;
    if (g_audio->queryInterface(IPitchTelemetry::iid, (void**)&tel) != kResultOk || !tel) {
        c.note = "queryInterface(IPitchTelemetry) failed";
        c.pass = false;
        return;
    }
    bool idsOk = true;
    for (int cell = 0; cell < kTelIdCount; ++cell) {
        int32 id = -2;
        if (tel->getCellId(cell, &id) != kResultOk) { idsOk = false; break; }
        const int32 expect = (cell >= kTelCentsFromC4) ? cell : -1;
        if (id != expect) { idsOk = false; break; }
    }
    check(c, idsOk, "getCellId mapping wrong (cells 0..6 must be -1, 7..13 = id)");
    double frame[kTelIdCount];
    check(c, tel->getVoiceFrame(0, frame) == kResultOk, "getVoiceFrame(0) failed");
    check(c, tel->getVoiceFrame(-1, frame) == kResultFalse, "voice -1 accepted");
    check(c, tel->getVoiceFrame(kTelemetryMaxVoices, frame) == kResultFalse,
          "voice kTelemetryMaxVoices accepted");
    check(c, tel->getVoiceFrame(0, nullptr) == kResultFalse, "null frame accepted");
    tel->release();  // QUERY_INTERFACE add-ref'd
    c.pass = c.note.empty();
}

// A fresh instance: silence -> the -65535 sentinel; then a 220 Hz sine ->
// id 7 = -300 cents (A3), the Q23 identity for id 9, ids 10/11 in range,
// and the +/-cents twins consistent with the unity default (both 0).
void caseTelemetryValues(CaseResult& c) {
    if (!g_module) { c.note = "no module"; return; }
    IPtr<IComponent> comp2 = g_module->getFactory().createInstance<IComponent>(
        VST3::UID(octavox::vst3wrap::OctavoxNativeProcessorUID));
    check(c, comp2 != nullptr, "instance creation failed");
    if (!comp2) { c.pass = false; return; }
    IPtr<IAudioProcessor> audio2;
    {
        IAudioProcessor* ap2 = nullptr;
        if (comp2->queryInterface(IAudioProcessor::iid, (void**)&ap2) == kResultOk && ap2)
            audio2 = IPtr<IAudioProcessor>(ap2, false);  // adopt the query ref
    }
    IPitchTelemetry* tel2 = nullptr;
    check(c, comp2->queryInterface(IPitchTelemetry::iid, (void**)&tel2) == kResultOk && tel2,
          "IPitchTelemetry not served by the fresh instance");
    if (!audio2 || !tel2) { c.pass = false; return; }
    check(c, comp2->initialize(g_hostContext) == kResultOk, "initialize failed");
    SpeakerArrangement mono = SpeakerArr::kMono;
    audio2->setBusArrangements(&mono, 1, &mono, 1);
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = 48000.0;
    ps.maxSamplesPerBlock = 4096;
    check(c, audio2->setupProcessing(ps) == kResultOk, "setupProcessing failed");
    check(c, comp2->setActive(true) == kResultOk, "setActive failed");
    audio2->setProcessing(true);

    const int B = 4096;
    std::vector<float> buf(B), out(B, 0.0f);
    auto run = [&](bool sine, int blocks) {
        for (int b = 0; b < blocks; ++b) {
            if (sine) fillSine(buf.data(), B, b * B, 220.0, 48000.0);
            else std::fill(buf.begin(), buf.end(), 0.0f);
            float* inP[1] = {buf.data()};
            float* outP[1] = {out.data()};
            AudioBusBuffers ib{}, ob{};
            ib.numChannels = 1;
            ib.channelBuffers32 = inP;
            ob.numChannels = 1;
            ob.channelBuffers32 = outP;
            AudioBusBuffers inB[1] = {ib}, outB[1] = {ob};
            ProcessData pd{};
            pd.processMode = kRealtime;
            pd.symbolicSampleSize = kSample32;
            pd.numSamples = B;
            pd.inputs = inB;
            pd.numInputs = 1;
            pd.outputs = outB;
            pd.numOutputs = 1;
            audio2->process(pd);
        }
    };

    // 1) silence: the sentinel
    run(false, 4);
    double frame[kTelIdCount];
    tel2->getVoiceFrame(0, frame);
    addValue(c, "silence_id7_cents", frame[kTelCentsFromC4]);
    check(c, frame[kTelCentsFromC4] == octavox::kTelemetryNoPitchSentinel,
          "silence did not serve the -65535 sentinel on id 7");

    // 2) a 220 Hz sine (1 s): the tracker locks -> id 7 = -300 cents
    run(true, 12);
    tel2->getVoiceFrame(0, frame);
    addValue(c, "sine220_id7_cents", frame[kTelCentsFromC4]);
    check(c, std::fabs(frame[kTelCentsFromC4] - (-300.0)) < 5.0,
          "id 7 not within 5 cents of -300 for the 220 Hz sine");

    // 3) the internal identities of the other ids on the same frame
    const double ratio = frame[kTelFreqRatio];
    addValue(c, "sine220_id8_ratio", ratio);
    check(c, ratio > 0.0, "id 8 ratio non-positive");
    const double n23 = std::llround(ratio * 8388608.0);
    check(c, std::fabs(frame[kTelQ23Ratio] - n23 / (n23 + 8.0)) < 1e-12,
          "id 9 != n/(n+8) with n = llround(ratio*2^23)");
    check(c, frame[kTelPhaseConf] >= 0.0 && frame[kTelPhaseConf] <= 1.0,
          "id 10 outside [0,1]");
    check(c, frame[kTelConfidence] >= 0.0 && frame[kTelConfidence] <= 1.0,
          "id 11 outside [0,1]");
    // the default surface is unity (pitch 0): the +/-cents twins read 0
    check(c, frame[kTelNegCents] == 0.0 && frame[kTelPosCents] == 0.0,
          "id 12/13 not 0 at the unity default");
    addValue(c, "sine220_id11_confidence", frame[kTelConfidence]);

    audio2->setProcessing(false);
    comp2->setActive(false);
    check(c, comp2->terminate() == kResultOk, "terminate failed");
    tel2->release();
    c.pass = c.note.empty();
}

// The dirty-bit contract (R30): the id-7 bit stays quiet while the tracked
// value is unchanged (silence), fires when the tone locks, and consume
// clears the mask.
void caseTelemetryDirtyBits(CaseResult& c) {
    if (!g_module) { c.note = "no module"; return; }
    IPtr<IComponent> comp2 = g_module->getFactory().createInstance<IComponent>(
        VST3::UID(octavox::vst3wrap::OctavoxNativeProcessorUID));
    check(c, comp2 != nullptr, "instance creation failed");
    if (!comp2) { c.pass = false; return; }
    IPtr<IAudioProcessor> audio2;
    {
        IAudioProcessor* ap2 = nullptr;
        if (comp2->queryInterface(IAudioProcessor::iid, (void**)&ap2) == kResultOk && ap2)
            audio2 = IPtr<IAudioProcessor>(ap2, false);  // adopt the query ref
    }
    IPitchTelemetry* tel2 = nullptr;
    check(c, comp2->queryInterface(IPitchTelemetry::iid, (void**)&tel2) == kResultOk && tel2,
          "IPitchTelemetry not served");
    if (!audio2 || !tel2) { c.pass = false; return; }
    check(c, comp2->initialize(g_hostContext) == kResultOk, "initialize failed");
    SpeakerArrangement mono = SpeakerArr::kMono;
    audio2->setBusArrangements(&mono, 1, &mono, 1);
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = 48000.0;
    ps.maxSamplesPerBlock = 4096;
    check(c, audio2->setupProcessing(ps) == kResultOk, "setupProcessing failed");
    check(c, comp2->setActive(true) == kResultOk, "setActive failed");
    audio2->setProcessing(true);

    const int B = 4096;
    std::vector<float> buf(B), out(B, 0.0f);
    auto run = [&](bool sine, int blocks) {
        for (int b = 0; b < blocks; ++b) {
            if (sine) fillSine(buf.data(), B, b * B, 220.0, 48000.0);
            else std::fill(buf.begin(), buf.end(), 0.0f);
            float* inP[1] = {buf.data()};
            float* outP[1] = {out.data()};
            AudioBusBuffers ib{}, ob{};
            ib.numChannels = 1;
            ib.channelBuffers32 = inP;
            ob.numChannels = 1;
            ob.channelBuffers32 = outP;
            AudioBusBuffers inB[1] = {ib}, outB[1] = {ob};
            ProcessData pd{};
            pd.processMode = kRealtime;
            pd.symbolicSampleSize = kSample32;
            pd.numSamples = B;
            pd.inputs = inB;
            pd.numInputs = 1;
            pd.outputs = outB;
            pd.numOutputs = 1;
            audio2->process(pd);
        }
    };

    const uint64 id7Bit0 = static_cast<uint64>(1) << (0 * 8 + (kTelCentsFromC4 - 7));

    // 1) initial consume (the first polls marked the sentinel transition)
    run(false, 2);
    (void)tel2->consumeDirtyBits();

    // 2) steady silence: the id-7 bit must stay quiet (no tracked change)
    run(false, 4);
    const uint64 quiet = tel2->consumeDirtyBits();
    addValue(c, "silence_id7_bit", (quiet & id7Bit0) ? 1.0 : 0.0);
    check(c, (quiet & id7Bit0) == 0, "id-7 dirty bit fired during steady silence");

    // 3) consume-clears: a second consume without processing returns 0
    check(c, tel2->consumeDirtyBits() == 0, "consumeDirtyBits did not clear the mask");

    // 4) the tone begins: once the tracker locks, the id-7 bit fires
    run(true, 12);
    uint64 fired = 0;
    // consume across a few further blocks; the lock transition must appear
    for (int k = 0; k < 4 && (fired & id7Bit0) == 0; ++k) {
        run(true, 2);
        fired |= tel2->consumeDirtyBits();
    }
    addValue(c, "tone_id7_bit", (fired & id7Bit0) ? 1.0 : 0.0);
    check(c, (fired & id7Bit0) != 0, "id-7 dirty bit never fired for the tone");

    audio2->setProcessing(false);
    comp2->setActive(false);
    check(c, comp2->terminate() == kResultOk, "terminate failed");
    tel2->release();
    c.pass = c.note.empty();
}

// ===== R33: the minimal editor surface ====================================

#if defined(__linux__)
// A minimal host-side plug frame serving IRunLoop (the VST 3.6.8+ Linux
// hosting contract) so the module's editor can attach for real.
class EditorPlugFrame : public FObject, public IPlugFrame {
public:
    class Loop : public FObject, public Linux::IRunLoop {
    public:
        std::vector<Linux::IEventHandler*> handlers;
        std::vector<int> fds;
        std::vector<Linux::ITimerHandler*> timers;
        tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* h,
                                                Linux::FileDescriptor fd) SMTG_OVERRIDE {
            handlers.push_back(h);
            fds.push_back(static_cast<int>(fd));
            return kResultTrue;
        }
        tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* h) SMTG_OVERRIDE {
            for (size_t i = 0; i < handlers.size();) {
                if (handlers[i] == h) {
                    handlers.erase(handlers.begin() + i);
                    fds.erase(fds.begin() + i);
                } else {
                    ++i;
                }
            }
            return kResultTrue;
        }
        tresult PLUGIN_API registerTimer(Linux::ITimerHandler* h,
                                         Linux::TimerInterval) SMTG_OVERRIDE {
            timers.push_back(h);
            return kResultTrue;
        }
        tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* h) SMTG_OVERRIDE {
            for (size_t i = 0; i < timers.size();) {
                if (timers[i] == h)
                    timers.erase(timers.begin() + i);
                else
                    ++i;
            }
            return kResultTrue;
        }
        OBJ_METHODS(Loop, FObject)
        DEFINE_INTERFACES
            DEF_INTERFACE(Linux::IRunLoop)
        END_DEFINE_INTERFACES(FObject)
        REFCOUNT_METHODS(FObject)
    };
    Loop loop;
    tresult PLUGIN_API resizeView(IPlugView*, ViewRect*) SMTG_OVERRIDE {
        return kResultTrue;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) SMTG_OVERRIDE {
        QUERY_INTERFACE(iid, obj, Steinberg::IPlugFrame::iid, Steinberg::IPlugFrame)
        if (iid && obj &&
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid)) {
            *obj = static_cast<Steinberg::Linux::IRunLoop*>(&loop);
            loop.addRef();
            return kResultTrue;
        }
        return FObject::queryInterface(iid, obj);
    }
    OBJ_METHODS(EditorPlugFrame, FObject)
    REFCOUNT_METHODS(FObject)
};
#endif  // __linux__

// The minimal editor through the SHIPPED module: createView from the loaded
// factory, the view-type/platform contract, and (with DISPLAY) a real X11
// attach whose server-side pixels contain the editor's painted palette.
void caseMinimalEditorSurface(CaseResult& c) {
    if (!g_module) { c.note = "no module"; return; }
    IPtr<IComponent> comp2 = g_module->getFactory().createInstance<IComponent>(
        VST3::UID(octavox::vst3wrap::OctavoxNativeProcessorUID));
    check(c, comp2 != nullptr, "instance creation failed");
    if (!comp2) { c.pass = false; return; }
    IPtr<IEditController> ec2;
    {
        IEditController* ec = nullptr;
        if (comp2->queryInterface(IEditController::iid, (void**)&ec) == kResultOk && ec)
            ec2 = IPtr<IEditController>(ec, false);  // adopt the query ref
    }
    check(c, ec2 != nullptr, "IEditController not served");
    if (!ec2) { c.pass = false; return; }
    check(c, comp2->initialize(g_hostContext) == kResultOk, "initialize failed");

    // createView transfers the born-1 reference (the VST3 host contract):
    // IPtr takes ownership WITHOUT an additional addRef.
    IPtr<IPlugView> view(ec2->createView(ViewType::kEditor), false);
    check(c, view != nullptr, "createView(\"Editor\") returned no view");
    if (!view) { c.pass = false; return; }
    check(c, ec2->createView("NotAViewType") == nullptr,
          "unknown view type must return nullptr");
    check(c, view->canResize() == kResultFalse, "the minimal view is fixed-size");
    ViewRect vr{};
    check(c, view->getSize(&vr) == kResultOk, "getSize failed");
    addValue(c, "view_width", (double)(vr.right - vr.left));
    addValue(c, "view_height", (double)(vr.bottom - vr.top));
    check(c, vr.right - vr.left == 784 && vr.bottom - vr.top == 416,
          "expected the fixed 784x416 editor size");
#if defined(__linux__)
    check(c, view->isPlatformTypeSupported("X11EmbedWindowID") == kResultTrue,
          "X11EmbedWindowID must be supported");
    check(c, view->isPlatformTypeSupported("NSView") == kResultFalse,
          "NSView must not be supported");
    check(c, view->attached((void*)0x1234, "Bogus") == kResultFalse,
          "bogus platform type must be refused");

    // The real attach (only with an X server present; xvfb-run counts).
    if (const char* disp = getenv("DISPLAY")) {
        (void)disp;
        addValue(c, "attach_exercised", 1.0);
        Display* dpy = XOpenDisplay(nullptr);
        check(c, dpy != nullptr, "XOpenDisplay failed");
        if (dpy) {
            const int scr = DefaultScreen(dpy);
            Window parent = XCreateSimpleWindow(
                dpy, DefaultRootWindow(dpy), 0, 0, 800, 440, 0,
                BlackPixel(dpy, scr), 0x202020ul);
            XMapWindow(dpy, parent);
            XFlush(dpy);
            static EditorPlugFrame frame;
            view->setFrame(&frame);
            const tresult ar = view->attached(
                reinterpret_cast<void*>(static_cast<uintptr_t>(parent)),
                "X11EmbedWindowID");
            check(c, ar == kResultOk, "attach into a real parent failed");
            bool painted = false, tornDown = false;
            if (ar == kResultOk) {
                // Pump like a host for up to ~2 s: fd events + 33 ms ticks.
                const int viewFd = frame.loop.fds.empty() ? -1 : frame.loop.fds[0];
                const int testFd = ConnectionNumber(dpy);
                int maxFd = (testFd > viewFd ? testFd : viewFd) + 1;
                for (int round = 0; round < 100 && !painted; ++round) {
                    fd_set rfds;
                    FD_ZERO(&rfds);
                    if (viewFd >= 0) FD_SET(viewFd, &rfds);
                    FD_SET(testFd, &rfds);
                    struct timeval tv { 0, 20000 };
                    select(maxFd, &rfds, nullptr, nullptr, &tv);
                    if (viewFd >= 0 && FD_ISSET(viewFd, &rfds) &&
                        !frame.loop.handlers.empty())
                        frame.loop.handlers[0]->onFDIsSet(viewFd);
                    if (FD_ISSET(testFd, &rfds)) {
                        while (XPending(dpy)) {
                            XEvent e;
                            XNextEvent(dpy, &e);
                        }
                    }
                    if (!frame.loop.timers.empty())
                        frame.loop.timers[0]->onTimer();
                    XSync(dpy, False);
                    // Find the child and probe its pixels for the palette.
                    Window rootRet = 0, parentRet = 0, child = 0;
                    Window* children = nullptr;
                    unsigned n = 0;
                    if (XQueryTree(dpy, parent, &rootRet, &parentRet, &children, &n)) {
                        if (n > 0) child = children[0];
                        if (children) XFree(children);
                    }
                    if (child) {
                        XImage* img = XGetImage(dpy, child, 0, 0, 784, 416,
                                                AllPlanes, ZPixmap);
                        if (img && img->data) {
                            size_t panel = 0, accent = 0, bg = 0;
                            const uint32_t* px =
                                reinterpret_cast<const uint32_t*>(img->data);
                            for (size_t i = 0;
                                 i < 784u * 416u && i < (size_t)(img->bytes_per_line / 4) * 416u;
                                 ++i) {
                                const uint32_t v = px[i] & 0xFFFFFFu;
                                if (v == 0x1C1F24u) ++panel;
                                if (v == 0xE8A33Du) ++accent;
                                if (v == 0x17191Du) ++bg;
                            }
                            addValue(c, "painted_panel_px", (double)panel);
                            addValue(c, "painted_accent_px", (double)accent);
                            addValue(c, "painted_bg_px", (double)bg);
                            painted = (panel > 100000 && accent > 100 && bg > 10000);
                        }
                        if (img) {
                            XDestroyImage(img);
                        }
                    }
                }
                check(c, painted,
                      "the attached view painted its palette into the window");
                check(c, view->removed() == kResultOk, "removed failed");
                check(c, frame.loop.handlers.empty() && frame.loop.timers.empty(),
                      "run-loop handlers not unregistered on remove");
                XSync(dpy, False);
                Window rootRet = 0, parentRet = 0;
                Window* children = nullptr;
                unsigned n = 0;
                XQueryTree(dpy, parent, &rootRet, &parentRet, &children, &n);
                tornDown = (n == 0);
                if (children) XFree(children);
                check(c, tornDown, "child window not destroyed on remove");
            }
            XDestroyWindow(dpy, parent);
            XCloseDisplay(dpy);
        }
    } else {
        // Not a failure: the painted-pixel proof needs an X server (the
        // lifecycle contract above already passed); recorded as a value.
        addValue(c, "attach_exercised", 0.0);
    }
#else
    check(c, view->isPlatformTypeSupported("NSView") == kResultFalse,
          "no platform on this OS");
#endif
    view = nullptr;  // release the view BEFORE terminating the component
    ec2->terminate();
    c.pass = c.note.empty();
}

// The latency-truth notification (the R30 U-2 principle): the host is told
// kLatencyChanged on the first report and whenever the reported value
// changes (an active state restore with a different sample-domain config).
void caseLatencyNotification(CaseResult& c) {
    if (!g_module) { c.note = "no module"; return; }
    static LatencyCountingHost countingHost;
    countingHost.latencyRestarts = 0;
    IPtr<IComponent> comp2 = g_module->getFactory().createInstance<IComponent>(
        VST3::UID(octavox::vst3wrap::OctavoxNativeProcessorUID));
    check(c, comp2 != nullptr, "instance creation failed");
    if (!comp2) { c.pass = false; return; }
    IPtr<IAudioProcessor> audio2;
    {
        IAudioProcessor* ap2 = nullptr;
        if (comp2->queryInterface(IAudioProcessor::iid, (void**)&ap2) == kResultOk && ap2)
            audio2 = IPtr<IAudioProcessor>(ap2, false);  // adopt the query ref
    }
    if (!audio2) { c.pass = false; return; }
    // (the FUnknown* cast goes through the HostApplication branch -- the
    // IComponentHandler branch is reached by the component's own
    // queryInterface, exactly like a real host)
    check(c, comp2->initialize(static_cast<FUnknown*>(
                            static_cast<HostApplication*>(&countingHost))) == kResultOk,
          "initialize failed");
    // The VST3 host contract for the handler connection: the host hands the
    // controller its IComponentHandler via setComponentHandler (the SDK's
    // ComponentBase does NOT query it from the context). A real host does
    // exactly this call after creating the controller.
    {
        IEditController* ec2 = nullptr;
        check(c, comp2->queryInterface(IEditController::iid, (void**)&ec2) == kResultOk && ec2,
              "IEditController not served");
        if (ec2) {
            ec2->setComponentHandler(static_cast<IComponentHandler*>(&countingHost));
            ec2->release();
        }
    }
    SpeakerArrangement mono = SpeakerArr::kMono;
    audio2->setBusArrangements(&mono, 1, &mono, 1);
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = 48000.0;
    ps.maxSamplesPerBlock = 4096;
    check(c, audio2->setupProcessing(ps) == kResultOk, "setupProcessing failed");
    check(c, comp2->setActive(true) == kResultOk, "setActive failed");

    // 1) the first report: exactly one notification so far
    addValue(c, "restarts_after_first_activate", (double)countingHost.latencyRestarts);
    check(c, countingHost.latencyRestarts == 1,
          "expected exactly 1 kLatencyChanged after the first activation");

    // 2) the reported value equals the core's contract
    octavox::MultichannelShifter ref;
    ref.prepare(octavox::vst3wrap::defaultConfig(), 1);
    check(c, audio2->getLatencySamples() == ref.latencySamples(),
          "reported latency != the core contract");

    // 3) unchanged latency -> NO re-notification on a deactivate/activate
    comp2->setActive(false);
    comp2->setActive(true);
    addValue(c, "restarts_after_reactivate", (double)countingHost.latencyRestarts);
    check(c, countingHost.latencyRestarts == 1,
          "unchanged latency must not re-notify");

    // 4) a changed sample-domain config -> the host is told again.
    // Build a state blob with grain 2048 (the default is 4096): the
    // latency (base_delay + grain/2) changes by 1024 samples.
    MemoryStream state;
    {
        octavox::vst3wrap::StateData st{};
        st.cfg = octavox::vst3wrap::defaultConfig();
        st.cfg.grain = 2048.0;
        st.cfg.sample_rate = 48000.0;
        st.channels = 1;
        st.surface = octavox::vst3wrap::defaultSurface();
        check(c, octavox::vst3wrap::writeStateData(&state, st),
              "building the grain-2048 state blob failed");
    }
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    check(c, comp2->setState(&state) == kResultOk, "setState failed");
    addValue(c, "restarts_after_changed_latency", (double)countingHost.latencyRestarts);
    check(c, countingHost.latencyRestarts == 2,
          "a latency change must re-notify the host");
    addValue(c, "latency_after_change", (double)audio2->getLatencySamples());
    check(c, audio2->getLatencySamples() == ref.latencySamples() - 1024,
          "the grain-2048 latency must be 1024 samples lower");

    comp2->setActive(false);
    check(c, comp2->terminate() == kResultOk, "terminate failed");
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
            std::fprintf(stderr, "usage: %s [--module <path/to/%s>] "
                                 "[--json <path>]\n", argv[0],
                         octavox::vst3wrap::build::kBundleName);
            return 2;
        }
    }
    // R18-WDB: THIS build variant's bundle (OctavoxRE_<Variant>.vst3) from
    // the central build configuration helper.
    const std::string bundleName = octavox::vst3wrap::build::bundleFileName();
    // default: <exe_dir>/../../VST3/Release/<bundle> (both separators --
    // the harness also runs on Windows builds where argv[0] uses '\')
    if (modulePath.empty()) {
        std::string exe = argv[0];
        const size_t slash = exe.find_last_of("/\\");
        const std::string dir = slash == std::string::npos ? "." : exe.substr(0, slash);
        const std::string cands[] = {
            "/../../VST3/Release/" + bundleName,
            "/../../../VST3/Release/" + bundleName,
            "/" + bundleName,
        };
        for (const std::string& suff : cands) {
            std::string p = dir + suff;
            FILE* f = std::fopen(p.c_str(), "rb");
            if (f) { std::fclose(f); modulePath = p; break; }
        }
        if (modulePath.empty()) {
            // last resort: relative to the CWD
            const std::string rel[] = {"build/VST3/Release/" + bundleName,
                                       "VST3/Release/" + bundleName};
            for (const std::string& p : rel) {
                FILE* f = std::fopen(p.c_str(), "rb");
                if (f) { std::fclose(f); modulePath = p; break; }
            }
        }
    }
    if (modulePath.empty()) {
        std::fprintf(stderr, "error: %s not found "
                             "(build it first, or pass --module)\n",
                     bundleName.c_str());
        return 2;
    }
    std::printf("module: %s\n", modulePath.c_str());
    std::printf("build variant: %s (default interpolation: %s)\n",
                octavox::vst3wrap::build::kBundleName,
                octavox::vst3wrap::build::kInterpolationName);
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
        CaseResult c{"telemetry_vendor_interface"};
        caseTelemetryVendorInterface(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"telemetry_values_sentinel"};
        caseTelemetryValues(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"telemetry_dirty_bits"};
        caseTelemetryDirtyBits(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"latency_notification"};
        caseLatencyNotification(c);
        pushCase(R, std::move(c));
    }
    {
        CaseResult c{"minimal_editor_surface"};
        caseMinimalEditorSurface(c);
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
            std::fprintf(f, "  \"build_variant\": \"%s\",\n",
                         octavox::vst3wrap::build::kBundleName);
            std::fprintf(f, "  \"default_interpolation\": \"%s\",\n",
                         octavox::vst3wrap::build::kInterpolationName);
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
