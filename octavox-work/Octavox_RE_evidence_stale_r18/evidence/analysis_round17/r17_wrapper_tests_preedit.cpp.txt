// ---------------------------------------------------------------------------
// OCTAVOX native core -- VST3 wrapper: in-process test suite (R16-P4b)
// ============================================================================
// PROVENANCE: our own independent harmonizer implementation around the
// octavox native DSP core (see octavox_native_processor.h). NOT a
// reproduction of the original Eventide Octavox plug-in; no target code;
// the original binary was never executed.
//
// This program drives the ACTUAL VST3 classes through their COM interfaces
// in-process (no module loading). Host-side objects are the SDK's own
// hosting utilities (ParameterChanges, MemoryStream) -- the same
// infrastructure the validator uses. The DSP reference in every
// equivalence case is the core library driven DIRECTLY by hand
// (MultichannelShifter), i.e. the tested block-boundary implementation.
//
// Conversion discipline: wherever a test compares the wrapper against a
// hand-driven core, the reference uses the SAME parameter-conversion chain
// as the wrapper (toEnginePlain below = plainFromNormalized o
// normalizedFromPlain), so bit-exactness compares the SEMANTICS, not the
// float rounding of two different conversion paths.
//
// Cases:
//   1. lifecycle_smoke                     factory -> init -> buses -> setup
//                                          -> activate -> process -> teardown
//   2. param_roundtrip                     38-parameter conversion roundtrip
//                                          + observable engine effects
//   3. segmentation_equivalence_BITEXACT   wrapper sample-accurate path ==
//                                          core block-boundary path (the
//                                          key equivalence proof)
//   4. state_roundtrip_BITEXACT            getState -> setState -> replay ==
//                                          hand-driven reference (fresh and
//                                          same-instance paths)
//   5. silence_blocks_reset                zero input / n=0 / varying block
//                                          sizes / reset cycle determinism
//   6. inplace_safety                      in == out host buffers handled
//   7. no_alloc_steady_state               global-new counter == 0 across
//                                          steady-state process() calls
//   8. latency_report                      getLatencySamples == core value
//
// Exit code 0 iff every case passes. Every measurement is printed.
// ---------------------------------------------------------------------------
#include "octavox_ids.h"
#include "octavox_native_processor.h"
#include "octavox_params.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include "octavox/dsp.hpp"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// The project's steady-state allocation counter (tests/alloc_counter.cpp).
namespace octavox {
namespace allocs {
void arm(bool on);
void reset();
long count();
}
}  // namespace octavox

using namespace Steinberg;
using namespace Steinberg::Vst;
using octavox::vst3wrap::OctavoxNativeProcessor;
using octavox::vst3wrap::OctavoxNativeProcessorUID;

//==========================================================================
// Test framework (minimal, honest-measurement style)
//==========================================================================
namespace {

struct CaseResult {
    std::string name;
    bool pass = true;
    std::string note;
    std::vector<std::pair<std::string, double>> values;
};

std::vector<CaseResult> g_results;

void addValue(CaseResult& c, const std::string& k, double v) { c.values.emplace_back(k, v); }

bool check(CaseResult& c, bool ok, const std::string& what) {
    if (!ok) {
        c.pass = false;
        if (!c.note.empty()) c.note += "; ";
        c.note += what;
    }
    return ok;
}

//==========================================================================
// Deterministic input: shared-law LCG (the same generator law as the core
// test suite; self-contained here).
//==========================================================================
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

//==========================================================================
// Host-side helpers
//==========================================================================

/// The plain value the ENGINE receives for a logical plain value: the exact
/// normalized->plain chain the wrapper's audio path performs.
double toEnginePlain(uint32_t id, double plain) {
    return octavox::vst3wrap::plainFromNormalized(
        id, octavox::vst3wrap::normalizedFromPlain(id, plain));
}

struct Setup {
    double sampleRate = 48000.0;
    int32 maxBlock = 4096;
};

/// Create + initialize + mono negotiation (if channels==1) + setup +
/// activate a processor.
OctavoxNativeProcessor* makeProcessor(const Setup& st, int channels = 2) {
    auto* p = new OctavoxNativeProcessor();
    if (p->initialize(nullptr) != kResultOk) return p;  // caller checks
    if (channels == 1) {
        SpeakerArrangement in = SpeakerArr::kMono, out = SpeakerArr::kMono;
        p->setBusArrangements(&in, 1, &out, 1);
    }
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = st.sampleRate;
    ps.maxSamplesPerBlock = st.maxBlock;
    p->setupProcessing(ps);
    p->setActive(true);
    return p;
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

/// One host processing call.
void driveBlock(OctavoxNativeProcessor* p, float* const* in, float* const* out,
                int channels, int n, IParameterChanges* changes) {
    AudioBusBuffers ib{}, ob{};
    ib.numChannels = channels;
    ib.channelBuffers32 = const_cast<float**>(in);
    ob.numChannels = channels;
    ob.channelBuffers32 = const_cast<float**>(out);
    AudioBusBuffers inBusses[1] = {ib};
    AudioBusBuffers outBusses[1] = {ob};
    ProcessData pd;
    std::memset(&pd, 0, sizeof(pd));
    pd.processMode = kRealtime;
    pd.symbolicSampleSize = kSample32;
    pd.numSamples = n;
    pd.inputs = inBusses;
    pd.numInputs = 1;
    pd.outputs = outBusses;
    pd.numOutputs = 1;
    pd.inputParameterChanges = changes;
    p->process(pd);
}

/// Add one (offset, normalized value) point for a parameter id.
void addEvent(IParameterChanges& changes, uint32_t id, int32_t offset, double norm) {
    int32 idx = 0;
    IParamValueQueue* q = changes.addParameterData(id, idx);
    if (q) {
        int32 pidx = 0;
        q->addPoint(offset, norm, pidx);
    }
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 1e30;
    double d = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double x = std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        if (x > d) d = x;
    }
    return d;
}

bool bitIdentical(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return false;
    return std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

}  // namespace

//==========================================================================
// 1. lifecycle_smoke
//==========================================================================
static void caseLifecycle(CaseResult& c) {
    // (a) Create through the ACTUAL module factory. Everything in this case
    // goes through INTERFACE pointers only -- exactly what a host sees (the
    // factory returns an IAudioProcessor subobject pointer; it must never
    // be cast to the concrete class).
    IPluginFactory* factory = GetPluginFactory();
    if (!check(c, factory != nullptr, "GetPluginFactory() returned null")) return;
    const int32 nClasses = factory->countClasses();
    addValue(c, "factory_class_count", nClasses);
    check(c, nClasses >= 1, "factory has no classes");

    void* obj = nullptr;
    tresult r = factory->createInstance(OctavoxNativeProcessorUID, IAudioProcessor::iid, &obj);
    if (!check(c, r == kResultOk && obj != nullptr,
               "factory->createInstance(processor) failed")) {
        return;
    }
    auto* ap = static_cast<Vst::IAudioProcessor*>(obj);

    // The host's duty: IPluginBase::initialize AFTER createInstance (this is
    // what populates the buses + parameters in our initialize()).
    {
        void* baseObj = nullptr;
        check(c, ap->queryInterface(IPluginBase::iid, &baseObj) == kResultOk && baseObj != nullptr,
              "IPluginBase not exposed");
        if (baseObj) {
            check(c, static_cast<IPluginBase*>(baseObj)->initialize(nullptr) == kResultOk,
                  "IPluginBase::initialize failed");
            static_cast<FUnknown*>(baseObj)->release();
        }
    }

    void* compObj = nullptr;
    check(c, ap->queryInterface(IComponent::iid, &compObj) == kResultOk && compObj != nullptr,
          "IComponent not exposed");
    auto* comp = static_cast<IComponent*>(compObj);
    void* ctrlObj = nullptr;
    check(c, ap->queryInterface(IEditController::iid, &ctrlObj) == kResultOk &&
                 ctrlObj != nullptr,
          "IEditController not exposed");
    if (ctrlObj) static_cast<FUnknown*>(ctrlObj)->release();

    // (b) Bus inventory + mono negotiation + asymmetric rejection.
    check(c, comp->getBusCount(kAudio, kInput) == 1 && comp->getBusCount(kAudio, kOutput) == 1,
          "expected 1 main audio in/out bus");
    SpeakerArrangement monoIn = SpeakerArr::kMono, monoOut = SpeakerArr::kMono;
    check(c, ap->setBusArrangements(&monoIn, 1, &monoOut, 1) == kResultOk,
          "mono/mono bus negotiation refused");
    SpeakerArrangement badIn = SpeakerArr::kMono, badOut = SpeakerArr::kStereo;
    check(c, ap->setBusArrangements(&badIn, 1, &badOut, 1) == kResultFalse,
          "asymmetric mono->stereo must be refused");
    SpeakerArrangement quad = SpeakerArr::kMono;
    check(c, ap->setBusArrangements(&quad, 1, &quad, 1) == kResultOk,
          "mono/mono re-negotiation failed");
    SpeakerArrangement got = SpeakerArr::kEmpty;
    ap->getBusArrangement(kInput, 0, got);
    check(c, SpeakerArr::getChannelCount(got) == 1, "negotiated input bus not mono");

    // (c) Setup + activate + one block + teardown (interface-driven, manual
    // ProcessData -- a host's exact call sequence).
    {
        ProcessSetup ps;
        std::memset(&ps, 0, sizeof(ps));
        ps.processMode = kRealtime;
        ps.symbolicSampleSize = kSample32;
        ps.sampleRate = 48000.0;
        ps.maxSamplesPerBlock = 4096;
        check(c, ap->setupProcessing(ps) == kResultOk, "setupProcessing failed");
    }
    check(c, ap->canProcessSampleSize(kSample64) == kResultFalse,
          "64-bit processing must be refused (float core)");
    check(c, comp->setActive(true) == kResultOk, "setActive(true) failed");
    {
        std::vector<float> in(1024, 0.25f), out(1024, 0.0f);
        float* inCh[1] = {in.data()};
        float* outCh[1] = {out.data()};
        AudioBusBuffers ib{}, ob{};
        ib.numChannels = 1;
        ib.channelBuffers32 = inCh;
        ob.numChannels = 1;
        ob.channelBuffers32 = outCh;
        AudioBusBuffers inB[1] = {ib}, outB[1] = {ob};
        ProcessData pd;
        std::memset(&pd, 0, sizeof(pd));
        pd.processMode = kRealtime;
        pd.symbolicSampleSize = kSample32;
        pd.numSamples = 1024;
        pd.inputs = inB;
        pd.numInputs = 1;
        pd.outputs = outB;
        pd.numOutputs = 1;
        check(c, ap->process(pd) == kResultOk, "process() failed");
        bool fin = true;
        for (float x : out)
            if (!std::isfinite(x)) fin = false;
        check(c, fin, "non-finite output in the lifecycle block");
    }
    check(c, comp->setActive(false) == kResultOk, "setActive(false) failed");
    check(c, ap->setProcessing(true) == kResultOk && ap->setProcessing(false) == kResultOk,
          "setProcessing notifications failed");
    static_cast<FUnknown*>(comp)->release();  // drop the IComponent view
    {
        void* baseObj = nullptr;
        check(c, ap->queryInterface(IPluginBase::iid, &baseObj) == kResultOk && baseObj != nullptr,
              "IPluginBase not exposed");
        if (baseObj) {
            check(c, static_cast<IPluginBase*>(baseObj)->terminate() == kResultOk,
                  "terminate failed");
            static_cast<FUnknown*>(baseObj)->release();
        }
    }
    ap->release();
}

//==========================================================================
// 2. param_roundtrip
//==========================================================================
static void caseParamRoundtrip(CaseResult& c) {
    // (a) setParamNormalized -> getParamNormalized for all 38 ids.
    auto* p = makeProcessor(Setup{});
    check(c, p->engineReady(), "processor not ready");
    double worst = 0.0;
    for (uint32_t id = 0; id < 38u; ++id) {
        const double n0 = 0.37;
        if (p->setParamNormalized(id, n0) != kResultOk) {
            check(c, false, "setParamNormalized failed for id " + std::to_string(id));
            continue;
        }
        worst = std::max(worst, std::fabs(p->getParamNormalized(id) - n0));
    }
    addValue(c, "getset_normalized_worst_diff", worst);
    check(c, worst < 1e-9, "normalized roundtrip mismatch");
    p->terminate();
    p->release();

    // (b) plain<->normalized roundtrip (the conversion chain the audio path
    // uses). For step grids the chain is exactly idempotent.
    double worstPlain = 0.0;
    for (uint32_t id = 0; id < 38u; ++id) {
        for (double g = 0.0; g <= 1.0; g += 0.125) {
            const double plain = toEnginePlain(id, g);  // arbitrary probe value
            const double back = octavox::vst3wrap::normalizedFromPlain(id, plain);
            const double again = octavox::vst3wrap::plainFromNormalized(id, back);
            worstPlain = std::max(worstPlain, std::fabs(again - plain));
        }
    }
    addValue(c, "plain_chain_idempotence_worst", worstPlain);
    check(c, worstPlain == 0.0, "plain conversion chain not idempotent");

    // (c) Observable engine effect: Voices 1 (default) vs 8, fresh
    //     processors each (clean stream position).
    const int N = 8192;
    Lcg rng(424242);
    std::vector<float> in(N);
    rng.fill(in);
    std::vector<float> o1(N), o8(N);
    {
        auto* p1 = makeProcessor(Setup{}, 1);
        float* inP[1] = {in.data()};
        float* outP[1] = {o1.data()};
        driveBlock(p1, inP, outP, 1, N, nullptr);
        p1->terminate();
        p1->release();
    }
    {
        auto* p8 = makeProcessor(Setup{}, 1);
        ParameterChanges ch;
        addEvent(ch, octavox::vst3wrap::ParamId::kVoices, 0,
                 octavox::vst3wrap::normalizedFromPlain(
                     octavox::vst3wrap::ParamId::kVoices, 8.0));
        float* inP[1] = {in.data()};
        float* outP[1] = {o8.data()};
        driveBlock(p8, inP, outP, 1, N, &ch);
        p8->terminate();
        p8->release();
    }
    const double dv = maxAbsDiff(o1, o8);
    addValue(c, "voices1_vs_8_maxdiff", dv);
    check(c, dv > 1e-4, "voices 1 vs 8 must audibly differ");

    // (d) Default neutrality at the wrapper level + bypass path, each vs a
    //     hand-driven core (fresh instances).
    std::vector<float> refOut(N), refBy(N);
    {
        octavox::MultichannelShifter ref;
        check(c, ref.prepare(octavox::Config{}, 1), "reference prepare failed");
        float* inP[1] = {in.data()};
        float* oP[1] = {refOut.data()};
        ref.process(inP, oP, 1, N);
    }
    {
        // A FRESH reference for the bypass run (a fresh engine + setBypass
        // before processing = the wrapper's event-at-position-0 path).
        octavox::MultichannelShifter ref2;
        check(c, ref2.prepare(octavox::Config{}, 1), "reference prepare failed");
        ref2.setBypass(true);
        float* inP[1] = {in.data()};
        float* oB[1] = {refBy.data()};
        ref2.process(inP, oB, 1, N);
    }
    std::vector<float> wDef(N), wBy(N);
    {
        auto* pd = makeProcessor(Setup{}, 1);
        float* inP[1] = {in.data()};
        float* outP[1] = {wDef.data()};
        driveBlock(pd, inP, outP, 1, N, nullptr);
        pd->terminate();
        pd->release();
    }
    {
        auto* pb = makeProcessor(Setup{}, 1);
        ParameterChanges ch;
        addEvent(ch, octavox::vst3wrap::ParamId::kBypass, 0, 1.0);
        float* inP[1] = {in.data()};
        float* outP[1] = {wBy.data()};
        driveBlock(pb, inP, outP, 1, N, &ch);
        pb->terminate();
        pb->release();
    }
    const double dDef = maxAbsDiff(wDef, refOut);
    addValue(c, "wrapper_default_vs_core_maxdiff", dDef);
    check(c, dDef == 0.0, "default wrapper run must be bit-identical to the core");
    const double dBy = maxAbsDiff(wBy, refBy);
    addValue(c, "wrapper_bypass_vs_core_maxdiff", dBy);
    check(c, dBy == 0.0, "bypass path must be bit-identical to the core");
}

//==========================================================================
// 3. segmentation_equivalence_BITEXACT (the key proof)
//==========================================================================
static void caseSegmentation(CaseResult& c) {
    using octavox::vst3wrap::ParamId;
    using octavox::vst3wrap::normalizedFromPlain;
    struct Ev {
        int pos;
        uint32_t id;
        double plain;
    };
    const std::vector<Ev> evs = {
        {5000, ParamId::kPitch, 4.0},
        {12000, octavox::vst3wrap::voiceGainId(2), 0.25},
        {20000, ParamId::kWetDry, 0.5},
        {26000, ParamId::kBypass, 1.0},
    };

    const int N = 65536;
    const int HOST_BLOCK = 1024;
    Lcg rng(987654321);
    std::vector<float> in(N), wrapOut(N);
    rng.fill(in);

    // ---- run A: the WRAPPER. Host blocks of 1024; each event is delivered
    //      at its TRUE in-block sample offset via IParameterChanges (the
    //      sample-accurate host path). ----
    {
        auto* p = makeProcessor(Setup{}, 1);
        check(c, p->engineReady(), "processor not ready");
        float* inP[1] = {in.data()};
        float* outP[1] = {wrapOut.data()};
        for (int b = 0; b * HOST_BLOCK < N; ++b) {
            const int start = b * HOST_BLOCK;
            const int n = std::min(HOST_BLOCK, N - start);
            ParameterChanges ch;
            bool any = false;
            for (const auto& e : evs) {
                if (e.pos >= start && e.pos < start + n) {
                    addEvent(ch, e.id, e.pos - start, normalizedFromPlain(e.id, e.plain));
                    any = true;
                }
            }
            driveBlock(p, inP, outP, 1, n, any ? &ch : nullptr);
            inP[0] += n;
            outP[0] += n;
        }
        p->terminate();
        p->release();
    }

    // ---- run B: the CORE driven by hand; each event is issued BETWEEN
    //      process() calls at exactly the same STREAM position (the tested
    //      block-boundary implementation). The plain values use the SAME
    //      conversion chain as the wrapper (toEnginePlain). ----
    std::vector<float> refOut(N);
    {
        octavox::MultichannelShifter ref;
        check(c, ref.prepare(octavox::Config{}, 1), "reference prepare failed");
        float* inP[1] = {in.data()};
        float* outP[1] = {refOut.data()};
        int pos = 0;
        for (const auto& e : evs) {
            if (e.pos > pos) {
                ref.process(inP, outP, 1, e.pos - pos);
                inP[0] += e.pos - pos;
                outP[0] += e.pos - pos;
                pos = e.pos;
            }
            const double plain = toEnginePlain(e.id, e.plain);
            switch (e.id) {
            case ParamId::kPitch:
                ref.setRatio(std::pow(2.0, plain / 12.0));
                break;
            case ParamId::kBypass:
                ref.setBypass(plain >= 0.5);
                break;
            case ParamId::kWetDry:
                ref.setWetDry(plain);
                break;
            default:
                if (e.id == octavox::vst3wrap::voiceGainId(2)) ref.setVoiceGain(2, plain);
                break;
            }
        }
        if (N > pos) ref.process(inP, outP, 1, N - pos);
    }

    // ---- THE gate: bit-exact ----
    const bool same = bitIdentical(wrapOut, refOut);
    const double d = maxAbsDiff(wrapOut, refOut);
    addValue(c, "maxdiff", d);
    addValue(c, "bit_identical", same ? 1.0 : 0.0);
    check(c, same && d == 0.0,
          "wrapper sample-accurate segmentation must be BIT-IDENTICAL to the "
          "core block-boundary implementation at the same stream positions");

    // ---- run C: a host that only delivers changes at HOST BLOCK
    //      boundaries (no in-block offsets). The events at 5000/12000/...
    //      land at the NEXT 1024-boundary -- a different (coarser) stream.
    //      Recorded to demonstrate the segmentation genuinely moves the
    //      application point (the difference is the proof it is active). --
    std::vector<float> refOut2(N);
    {
        octavox::MultichannelShifter ref;
        check(c, ref.prepare(octavox::Config{}, 1), "reference prepare failed");
        float* inP[1] = {in.data()};
        float* outP[1] = {refOut2.data()};
        for (int b = 0; b * HOST_BLOCK < N; ++b) {
            const int start = b * HOST_BLOCK;
            const int n = std::min(HOST_BLOCK, N - start);
            for (const auto& e : evs) {
                if (e.pos >= start && e.pos < start + n) {  // due this block
                    const double plain = toEnginePlain(e.id, e.plain);
                    switch (e.id) {
                    case ParamId::kPitch:
                        ref.setRatio(std::pow(2.0, plain / 12.0));
                        break;
                    case ParamId::kBypass:
                        ref.setBypass(plain >= 0.5);
                        break;
                    case ParamId::kWetDry:
                        ref.setWetDry(plain);
                        break;
                    default:
                        if (e.id == octavox::vst3wrap::voiceGainId(2))
                            ref.setVoiceGain(2, plain);
                        break;
                    }
                }
            }
            ref.process(inP, outP, 1, n);
            inP[0] += n;
            outP[0] += n;
        }
    }
    const double dC = maxAbsDiff(wrapOut, refOut2);
    addValue(c, "sampleaccurate_vs_coarse_host_maxdiff", dC);
    check(c, dC > 0.0,
          "sample-accurate delivery must differ from coarse block-boundary "
          "host timing (the segmentation does something)");
}

//==========================================================================
// 4. state_roundtrip_BITEXACT
//==========================================================================
static void caseState(CaseResult& c) {
    using octavox::vst3wrap::ParamId;
    using octavox::vst3wrap::normalizedFromPlain;
    const int N = 65536;

    // A non-default surface (canonical application order).
    struct Ev {
        uint32_t id;
        double plain;
    };
    const std::vector<Ev> surf = {
        {ParamId::kPitch, 2.0},
        {octavox::vst3wrap::voiceOffsetId(3), 7.0},
        {octavox::vst3wrap::voiceGainId(5), 0.5},
        {octavox::vst3wrap::voiceMuteId(6), 1.0},
        {octavox::vst3wrap::voiceDelayId(2), 20.0},
        {ParamId::kVoices, 8.0},
        {ParamId::kWetDry, 0.7},
    };

    Lcg rng(555777);
    std::vector<float> in(N);
    rng.fill(in);

    // ---- the hand-driven core reference: the core prepared with the
    //      default Config, the surface applied in the CANONICAL order with
    //      the wrapper's conversion chain. ----
    std::vector<float> refOut(N);
    {
        octavox::MultichannelShifter ref;
        octavox::Config cfg = octavox::vst3wrap::defaultConfig();
        check(c, ref.prepare(cfg, 1), "reference prepare failed");
        const double sr = cfg.sample_rate, grain = cfg.grain;
        const double pitch = toEnginePlain(ParamId::kPitch, 2.0);
        const double off3 = toEnginePlain(octavox::vst3wrap::voiceOffsetId(3), 7.0);
        const double g5 = toEnginePlain(octavox::vst3wrap::voiceGainId(5), 0.5);
        const bool m6 = toEnginePlain(octavox::vst3wrap::voiceMuteId(6), 1.0) >= 0.5;
        const double d2ms = toEnginePlain(octavox::vst3wrap::voiceDelayId(2), 20.0);
        const int voices =
            static_cast<int>(std::lround(toEnginePlain(ParamId::kVoices, 8.0)));
        const double wet = toEnginePlain(ParamId::kWetDry, 0.7);
        // canonical order: spread -> ratio -> overrides -> gains -> mutes ->
        // delays -> voices -> feedback -> wet/dry -> bypass
        ref.setSpread(true);
        ref.setRatio(std::pow(2.0, pitch / 12.0));
        ref.setVoiceRatio(3, std::pow(2.0, pitch / 12.0) * std::pow(2.0, off3 / 12.0));
        ref.setVoiceGain(5, g5);
        ref.setVoiceMute(6, m6);
        ref.setVoiceBaseDelay(
            2, octavox::vst3wrap::msToBaseDelaySamples(d2ms, sr, grain));
        ref.setVoices(voices);
        ref.setFeedback(0.0);
        ref.setWetDry(wet);
        ref.setBypass(false);
        float* inP[1] = {in.data()};
        float* outP[1] = {refOut.data()};
        ref.process(inP, outP, 1, N);
    }

    // ---- wrapper A: apply the surface via position-0 automation (canonical
    //      queue order), run, save state; then same-instance restore. ----
    std::vector<float> aOut(N);
    MemoryStream state;
    {
        auto* p = makeProcessor(Setup{}, 1);
        float* inP[1] = {in.data()};
        float* outP[1] = {aOut.data()};
        ParameterChanges ch;
        for (const auto& e : surf)
            addEvent(ch, e.id, 0, normalizedFromPlain(e.id, e.plain));
        driveBlock(p, inP, outP, 1, 4096, &ch);
        {
            float* in2[1] = {in.data() + 4096};
            float* out2[1] = {aOut.data() + 4096};
            driveBlock(p, in2, out2, 1, N - 4096, nullptr);
        }
        const double dLive = maxAbsDiff(aOut, refOut);
        addValue(c, "live_surface_vs_reference_maxdiff", dLive);
        check(c, dLive == 0.0,
              "the wrapper's live application of the full surface must be "
              "bit-identical to the hand-driven core");

        check(c, p->getState(&state) == kResultOk, "getState failed");
        addValue(c, "state_bytes", static_cast<double>(state.getSize()));

        // Same-instance restore: deactivate -> setState -> activate.
        check(c, p->setActive(false) == kResultOk, "deactivate failed");
        int64 spos = 0;
        state.seek(0, IBStream::kIBSeekSet, &spos);
        check(c, p->setState(&state) == kResultOk, "setState failed");
        check(c, p->setActive(true) == kResultOk, "reactivate failed");
        std::vector<float> replay(N);
        {
            float* in3[1] = {in.data()};
            float* out3[1] = {replay.data()};
            driveBlock(p, in3, out3, 1, N, nullptr);
        }
        const double dReplay = maxAbsDiff(replay, refOut);
        addValue(c, "same_instance_restore_maxdiff", dReplay);
        check(c, dReplay == 0.0, "same-instance state restore replay mismatch");

        p->terminate();
        p->release();
    }

    // ---- wrapper B: a FRESH instance restored from the saved blob (the
    //      normal host flow: initialize -> setState -> setup -> activate). --
    std::vector<float> bOut(N);
    {
        auto* p = new OctavoxNativeProcessor();
        p->initialize(nullptr);
        int64 spos = 0;
        state.seek(0, IBStream::kIBSeekSet, &spos);
        check(c, p->setState(&state) == kResultOk, "fresh-instance setState failed");
        standardSetup(p);
        check(c, p->setActive(true) == kResultOk, "fresh-instance activate failed");
        float* inP[1] = {in.data()};
        float* outP[1] = {bOut.data()};
        driveBlock(p, inP, outP, 1, N, nullptr);
        const double dFresh = maxAbsDiff(bOut, refOut);
        addValue(c, "fresh_instance_restore_maxdiff", dFresh);
        check(c, dFresh == 0.0, "fresh-instance state restore mismatch");
        p->terminate();
        p->release();
    }

    // ---- corrupt-state rejection ----
    {
        auto* p = makeProcessor(Setup{});
        MemoryStream bad;
        const char garbage[] = "not an octavox state blob";
        int32 written = 0;
        bad.write(const_cast<char*>(garbage), sizeof(garbage), &written);
        int64 spos = 0;
        bad.seek(0, IBStream::kIBSeekSet, &spos);
        check(c, p->setState(&bad) == kResultFalse,
              "corrupt state must be rejected (kResultFalse)");
        p->terminate();
        p->release();
    }
}

//==========================================================================
// 5. silence_blocks_reset
//==========================================================================
static void caseSilenceBlocksReset(CaseResult& c) {
    auto* p = makeProcessor(Setup{}, 1);

    // (a) Zero input -> finite, bounded output.
    {
        const int N = 4096;
        std::vector<float> z(N, 0.0f), out(N, -1.0f);
        float* inP[1] = {z.data()};
        float* outP[1] = {out.data()};
        driveBlock(p, inP, outP, 1, N, nullptr);
        bool fin = true;
        double mx = 0.0;
        for (float x : out) {
            if (!std::isfinite(x)) fin = false;
            mx = std::max(mx, std::fabs((double)x));
        }
        addValue(c, "silence_out_maxabs", mx);
        check(c, fin, "non-finite output on silence");
        check(c, mx <= 1.0, "silence output unbounded");
    }

    // (b) n == 0 calls are safe no-ops (output untouched).
    {
        std::vector<float> in(16, 0.5f), out(16, -7.0f);
        float* inP[1] = {in.data()};
        float* outP[1] = {out.data()};
        driveBlock(p, inP, outP, 1, 0, nullptr);
        check(c, out[0] == -7.0f, "n=0 call touched the output buffer");
    }

    // (c) Varying host block sizes (1/7/64/512/3/1024) == a reset one-shot
    //     run (block-size invariance at the wrapper level).
    {
        const int N = 8192;
        Lcg rng(31337);
        std::vector<float> in(N);
        rng.fill(in);
        std::vector<float> outSplit(N, 0.0f), outOne(N, 0.0f);
        const int blocks[] = {1, 7, 64, 512, 3, 1024};
        {
            float* inP[1] = {in.data()};
            float* outP[1] = {outSplit.data()};
            int pos = 0, bi = 0;
            while (pos < N) {
                const int n = std::min(blocks[bi % 6], N - pos);
                driveBlock(p, inP, outP, 1, n, nullptr);
                inP[0] += n;
                outP[0] += n;
                pos += n;
                ++bi;
            }
        }
        p->setActive(false);
        p->setActive(true);  // reset: post-prepare state
        {
            float* inP[1] = {in.data()};
            float* outP[1] = {outOne.data()};
            driveBlock(p, inP, outP, 1, N, nullptr);
        }
        const double dSplit = maxAbsDiff(outSplit, outOne);
        addValue(c, "split_vs_oneshot_maxdiff", dSplit);
        check(c, dSplit == 0.0,
              "wrapper output must be host-block-split invariant (vs the "
              "reset one-shot run)");
    }

    // (d) Reset-cycle determinism WITH a parameter surface + replay.
    {
        const int N = 16384;
        Lcg rng(2024);
        std::vector<float> in(N);
        rng.fill(in);
        std::vector<float> run1(N), run2(N);
        ParameterChanges ch;
        addEvent(ch, octavox::vst3wrap::ParamId::kPitch, 0,
                 octavox::vst3wrap::normalizedFromPlain(
                     octavox::vst3wrap::ParamId::kPitch, -3.0));
        {
            float* inP[1] = {in.data()};
            float* outP[1] = {run1.data()};
            driveBlock(p, inP, outP, 1, 4096, &ch);
            float* in2[1] = {in.data() + 4096};
            float* out2[1] = {run1.data() + 4096};
            driveBlock(p, in2, out2, 1, N - 4096, nullptr);
        }
        p->setActive(false);
        p->setActive(true);  // reset keeps the parameter surface
        {
            float* inP[1] = {in.data()};
            float* outP[1] = {run2.data()};
            driveBlock(p, inP, outP, 1, N, nullptr);
        }
        // Reference: a FRESH hand-driven core configured with the SAME
        // surface (pitch -3) BEFORE processing. Per the core's cold-start
        // law a freshly prepared engine starts its glide AT the target, so
        // the post-reset replay (reset preserves the surface targets)
        // equals this reference -- NOT the original run1 (whose glide
        // started from the pre-event default 1.0 mid-stream).
        std::vector<float> refR(N);
        {
            octavox::MultichannelShifter ref;
            check(c, ref.prepare(octavox::Config{}, 1), "reference prepare failed");
            const double pitch = toEnginePlain(octavox::vst3wrap::ParamId::kPitch, -3.0);
            ref.setRatio(std::pow(2.0, pitch / 12.0));
            float* inP[1] = {in.data()};
            float* outP[1] = {refR.data()};
            ref.process(inP, outP, 1, N);
        }
        const double d = maxAbsDiff(run2, refR);
        const double dGlide = maxAbsDiff(run1, run2);
        addValue(c, "reset_replay_vs_fresh_surface_maxdiff", d);
        addValue(c, "reset_replay_vs_original_run_maxdiff", dGlide);
        check(c, d == 0.0,
              "the post-reset replay must equal a fresh engine configured "
              "with the preserved surface (reset keeps parameter targets)");
        check(c, dGlide > 0.0,
              "the original run must differ (its glide started mid-stream "
              "from the default ratio; the reset law re-parks at target)");
    }

    // (e) Sample-rate re-setup at 44.1 kHz (deactivate -> setup -> activate).
    {
        p->setActive(false);
        ProcessSetup ps;
        std::memset(&ps, 0, sizeof(ps));
        ps.processMode = kRealtime;
        ps.symbolicSampleSize = kSample32;
        ps.sampleRate = 44100.0;
        ps.maxSamplesPerBlock = 4096;
        check(c, p->setupProcessing(ps) == kResultOk, "44.1 kHz re-setup failed");
        check(c, p->setActive(true) == kResultOk, "44.1 kHz activate failed");
        const int N = 2048;
        Lcg rng(99);
        std::vector<float> in(N), out(N);
        rng.fill(in);
        float* inP[1] = {in.data()};
        float* outP[1] = {out.data()};
        driveBlock(p, inP, outP, 1, N, nullptr);
        bool fin = true;
        for (float x : out)
            if (!std::isfinite(x)) fin = false;
        check(c, fin, "non-finite output after 44.1 kHz re-setup");
    }

    p->terminate();
    p->release();
}

//==========================================================================
// 6. inplace_safety
//==========================================================================
static void caseInPlace(CaseResult& c) {
    const int N = 8192;
    Lcg rng(777888);
    std::vector<float> in(N);
    rng.fill(in);

    std::vector<float> sepOut(N);
    auto* p = makeProcessor(Setup{}, 1);
    {
        float* inP[1] = {in.data()};
        float* outP[1] = {sepOut.data()};
        driveBlock(p, inP, outP, 1, N, nullptr);
    }

    // Reset the engine (post-prepare state) so the in-place run starts from
    // the SAME stream position 0 (comparable outputs).
    p->setActive(false);
    p->setActive(true);

    // In-place: the host passes the SAME buffer as input and output. The
    // wrapper must copy through scratch before writing (core contract:
    // in/out may not alias).
    std::vector<float> buf = in;
    {
        float* ioP[1] = {buf.data()};
        driveBlock(p, ioP, ioP, 1, N, nullptr);
    }

    const double d = maxAbsDiff(buf, sepOut);
    addValue(c, "inplace_vs_separate_maxdiff", d);
    check(c, d == 0.0,
          "in-place (in == out) processing must be bit-identical to separate "
          "buffers (both runs from the reset post-prepare state)");
    p->terminate();
    p->release();
}

//==========================================================================
// 7. no_alloc_steady_state
//==========================================================================
static void caseNoAlloc(CaseResult& c) {
    auto* p = makeProcessor(Setup{}, 1);

    const int N = 1024, CALLS = 100;
    std::vector<float> in(N), out(N);
    Lcg rng(4242);
    rng.fill(in);

    // Pre-build the parameter events BEFORE arming the counter (the hosting
    // queue objects are TEST-side allocations and must not pollute the gate).
    std::vector<std::unique_ptr<ParameterChanges>> evs;
    for (int i = 0; i < CALLS; ++i) {
        auto ch = std::make_unique<ParameterChanges>();
        if (i % 10 == 3) {
            const double w = 0.3 + 0.4 * ((i / 10) % 3);
            addEvent(*ch, octavox::vst3wrap::ParamId::kWetDry, 256,
                     octavox::vst3wrap::normalizedFromPlain(
                         octavox::vst3wrap::ParamId::kWetDry, w));
        }
        evs.push_back(std::move(ch));
    }

    float* inP[1] = {in.data()};
    float* outP[1] = {out.data()};

    octavox::allocs::reset();
    octavox::allocs::arm(true);
    for (int i = 0; i < CALLS; ++i) driveBlock(p, inP, outP, 1, N, evs[i].get());
    octavox::allocs::arm(false);

    const long n = octavox::allocs::count();
    addValue(c, "allocations_during_100_process_calls", static_cast<double>(n));
    check(c, n == 0, "steady-state process() allocated (global-new counter)");

    p->terminate();
    p->release();
}

//==========================================================================
// 8. latency_report
//==========================================================================
static void caseLatency(CaseResult& c) {
    auto* p = makeProcessor(Setup{});
    const uint32 wl = p->getLatencySamples();

    octavox::MultichannelShifter ref;
    check(c, ref.prepare(octavox::Config{}, 1), "reference prepare failed");
    const int rl = ref.latencySamples();
    addValue(c, "wrapper_latency_samples", static_cast<double>(wl));
    addValue(c, "core_latency_samples", static_cast<double>(rl));
    check(c, wl == static_cast<uint32>(rl),
          "wrapper latency must equal the core latencySamples()");
    addValue(c, "tail_samples", static_cast<double>(p->getTailSamples()));
    p->terminate();
    p->release();
}

//==========================================================================
// main
//==========================================================================
int main() {
    std::vector<std::pair<const char*, void (*)(CaseResult&)>> cases = {
        {"lifecycle_smoke", caseLifecycle},
        {"param_roundtrip", caseParamRoundtrip},
        {"segmentation_equivalence_BITEXACT", caseSegmentation},
        {"state_roundtrip_BITEXACT", caseState},
        {"silence_blocks_reset", caseSilenceBlocksReset},
        {"inplace_safety", caseInPlace},
        {"no_alloc_steady_state", caseNoAlloc},
        {"latency_report", caseLatency},
    };

    std::printf("%-42s %-6s %s\n", "CASE", "PASS", "MEASUREMENTS");
    std::printf("%s\n", std::string(96, '-').c_str());
    for (auto& [name, fn] : cases) {
        std::fprintf(stderr, "RUN %s\n", name);
        std::fflush(stderr);
        CaseResult cr;
        cr.name = name;
        fn(cr);
        g_results.push_back(std::move(cr));
    }
    int passed = 0;
    for (const auto& cr : g_results) {
        if (cr.pass) ++passed;
        std::printf("%-42s %-6s %s\n", cr.name.c_str(), cr.pass ? "ok" : "FAIL",
                    cr.note.c_str());
        for (const auto& v : cr.values)
            std::printf("%-42s %-6s %s = %.10g\n", "", "", v.first.c_str(), v.second);
    }
    std::printf("%s\n%s: %d passed, %d failed, %d total\n",
                std::string(96, '-').c_str(),
                "octavox-native VST3 wrapper tests (R16-P4b)", passed,
                static_cast<int>(g_results.size()) - passed,
                static_cast<int>(g_results.size()));
    return passed == static_cast<int>(g_results.size()) ? 0 : 1;
}
