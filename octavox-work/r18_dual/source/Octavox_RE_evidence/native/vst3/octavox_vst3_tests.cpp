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
//   9. param_metadata_table (R17-P3)       38-id uniqueness, spec-table
//                                          integrity, processor<->controller
//                                          ParameterInfo agreement, boundary
//                                          + middle conversion roundtrips
//  10. events_block_positions (R17-P3)     events at block start / middle /
//                                          last sample / block end (offset
//                                          == numSamples) vs the core
//  11. simultaneous_multivoice_events      several voices + one global
//      (R17-P3)                            changed at the SAME sample
//                                          position, bit-exact vs the core
//  12. bypass_wetdry_clickless_noalloc     bypass toggles + a smooth host
//      (R17-P3)                            wet/dry ramp: bounded per-sample
//                                          output step (no click), zero
//                                          allocations
//  13. state_full_surface_restore          ALL 38 parameters non-default ->
//      (R17-P3)                            save -> fresh restore: every
//                                          value restored + replay bit-exact
//  14. state_cross_rate_layout             state saved at 48 kHz stereo,
//      (R17-P3)                            restored at 96 kHz mono and at
//                                          44.1 kHz stereo (bit-exact vs the
//                                          hand-driven core at the new rate)
//  15. state_invalid_blobs (R17-P3)        every truncation prefix, wrong
//                                          magic / version, NaN / ring-law
//                                          violations: rejected, processor
//                                          stays usable
//
// Exit code 0 iff every case passes. Every measurement is printed.
// --dump-params prints the 38-parameter metadata table and exits 0.
// ---------------------------------------------------------------------------
#include "octavox_ids.h"
#include "octavox_native_processor.h"
#include "octavox_params.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include "octavox/dsp.hpp"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
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
        check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 1), "reference prepare failed");
        float* inP[1] = {in.data()};
        float* oP[1] = {refOut.data()};
        ref.process(inP, oP, 1, N);
    }
    {
        // A FRESH reference for the bypass run (a fresh engine + setBypass
        // before processing = the wrapper's event-at-position-0 path).
        octavox::MultichannelShifter ref2;
        check(c, ref2.prepare(octavox::vst3wrap::defaultConfig(), 1), "reference prepare failed");
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
        check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 1), "reference prepare failed");
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
        check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 1), "reference prepare failed");
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
            check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 1), "reference prepare failed");
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
    check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 1), "reference prepare failed");
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
// 9. param_metadata_table (R17-P3)
//==========================================================================
static void caseParamMetadata(CaseResult& c) {
    using octavox::vst3wrap::kNumParams;
    using octavox::vst3wrap::paramSpec;
    using octavox::vst3wrap::domainOf;
    using octavox::vst3wrap::normalizedFromPlain;
    using octavox::vst3wrap::plainFromNormalized;

    // (a) Parameter ID uniqueness: the spec table covers ids 0..37 exactly
    //     once (a 38-bit mask, no holes, no duplicates).
    uint64_t mask = 0;
    int nSpecs = 0;
    for (uint32_t id = 0; id < 64u; ++id) {
        const auto& sp = paramSpec(id);
        if (sp.id != id) continue;  // paramSpec() folds out-of-range to 0
        ++nSpecs;
        mask |= (1ULL << id);
    }
    addValue(c, "spec_table_entries", nSpecs);
    check(c, nSpecs == 38, "spec table must have exactly 38 entries");
    const uint64_t wantMask = (kNumParams == 64) ? ~0ULL : ((1ULL << 38) - 1ULL);
    addValue(c, "id_mask_complete", mask == wantMask ? 1.0 : 0.0);
    check(c, mask == wantMask,
          "parameter ids must be unique and contiguous 0..37 (mask)");

    // (b) Spec-table integrity: domain sanity, default in domain, stepCount
    //     consistency.
    for (uint32_t id = 0; id < 38u; ++id) {
        const auto& sp = paramSpec(id);
        const auto& d = sp.domain;
        check(c, d.maxPlain > d.minPlain, "bad domain (max<=min) at id " + std::to_string(id));
        check(c, d.stepPlain >= 0.0, "negative step at id " + std::to_string(id));
        check(c, sp.defaultPlain >= d.minPlain && sp.defaultPlain <= d.maxPlain,
              "default outside domain at id " + std::to_string(id));
        if (d.stepPlain > 0.0) {
            const double steps = (d.maxPlain - d.minPlain) / d.stepPlain;
            const double rounded = std::floor(steps + 0.5);
            const double stepCount = static_cast<double>(
                static_cast<int32>(std::floor(steps + 0.5)));
            check(c, std::fabs(steps - rounded) < 1e-9 && stepCount >= 1.0,
                  "span/step not integral at id " + std::to_string(id));
        }
        check(c, sp.title && sp.title[0] != '\0',
              "empty title at id " + std::to_string(id));
    }

    // (c) Processor<->controller mapping agreement (SingleComponentEffect:
    //     one object serves both interfaces; the host-visible ParameterInfo
    //     must match the spec table the audio path consults).
    auto* p = makeProcessor(Setup{});
    check(c, p->getParameterCount() == 38, "processor must expose 38 parameters");
    int mismatches = 0;
    for (uint32_t id = 0; id < 38u; ++id) {
        ParameterInfo info;
        if (p->getParameterInfo(id, info) != kResultOk) {
            check(c, false, "getParameterInfo failed at id " + std::to_string(id));
            ++mismatches;
            continue;
        }
        const auto& sp = paramSpec(id);
        if (info.id != id) ++mismatches;
        char title[128], shortT[128], units[128];
        Steinberg::UString(info.title, str16BufferSize(String128)).toAscii(title, sizeof(title));
        Steinberg::UString(info.shortTitle, str16BufferSize(String128)).toAscii(shortT, sizeof(shortT));
        Steinberg::UString(info.units, str16BufferSize(String128)).toAscii(units, sizeof(units));
        if (std::strcmp(title, sp.title) != 0) ++mismatches;
        if (std::strcmp(shortT, sp.shortTitle) != 0) ++mismatches;
        if (std::strcmp(units, sp.units ? sp.units : "") != 0) ++mismatches;
        const int32 wantStep = (sp.domain.stepPlain > 0.0)
            ? static_cast<int32>(std::floor((sp.domain.maxPlain - sp.domain.minPlain) /
                                            sp.domain.stepPlain + 0.5))
            : 0;
        if (info.stepCount != wantStep) ++mismatches;
        const double wantDef = normalizedFromPlain(id, sp.defaultPlain);
        if (std::fabs(info.defaultNormalizedValue - wantDef) > 0.0) ++mismatches;
        if (info.flags != sp.flags) ++mismatches;
        if (info.unitId != sp.unitId) ++mismatches;
    }
    addValue(c, "parameterinfo_mismatches", mismatches);
    check(c, mismatches == 0,
          "processor/controller ParameterInfo must agree with the spec table "
          "for all 38 ids (title, short, units, stepCount, default, flags, unit)");
    p->terminate();
    p->release();

    // (d) Conversion roundtrips at boundaries and representative middles,
    //     all 38 ids. On STEP domains the probes are GRID-ALIGNED (a
    //     non-grid plain legitimately snaps onto the grid, so only grid
    //     points can round-trip exactly); continuous domains probe the
    //     fractional positions. Step domains must round-trip EXACTLY;
    //     continuous domains within 4 ulp of the plain value.
    int exactFail = 0, ulpFail = 0;
    for (uint32_t id = 0; id < 38u; ++id) {
        const auto& d = domainOf(id);
        const double span = d.maxPlain - d.minPlain;
        double probes[7];
        int np = 0;
        if (d.stepPlain > 0.0) {
            const double nSteps = std::floor(span / d.stepPlain + 0.5);
            probes[np++] = d.minPlain;                            // grid 0
            probes[np++] = d.maxPlain;                            // grid N
            probes[np++] = d.minPlain + std::floor(nSteps * 0.25) * d.stepPlain;
            probes[np++] = d.minPlain + std::floor(nSteps * 0.50) * d.stepPlain;
            probes[np++] = d.minPlain + std::floor(nSteps * 0.75) * d.stepPlain;
            probes[np++] = d.minPlain + d.stepPlain;                          // grid 1
            probes[np++] = d.minPlain + (nSteps - 1.0) * d.stepPlain;          // grid N-1
        } else {
            probes[np++] = d.minPlain;
            probes[np++] = d.maxPlain;
            probes[np++] = d.minPlain + 0.5 * span;
            probes[np++] = d.minPlain + 0.25 * span;
            probes[np++] = d.minPlain + 0.75 * span;
        }
        for (int i = 0; i < np; ++i) {
            const double plain = probes[i];
            const double norm = normalizedFromPlain(id, plain);
            const double back = plainFromNormalized(id, norm);
            if (d.stepPlain > 0.0) {
                if (back != plain) ++exactFail;
            } else {
                const double tol = 4.0 * std::fabs(plain) * 2.220446049250313e-16 +
                                   4.0 * 2.2250738585072014e-308;
                if (!(std::fabs(back - plain) <= tol)) ++ulpFail;
            }
        }
        // Normalized boundary behaviour: 0 -> min, 1 -> max, clamping of
        // out-of-range and NaN inputs.
        if (plainFromNormalized(id, 0.0) != d.minPlain) ++exactFail;
        if (plainFromNormalized(id, 1.0) != d.maxPlain) ++exactFail;
        if (plainFromNormalized(id, -0.25) != d.minPlain) ++exactFail;
        if (plainFromNormalized(id, 1.75) != d.maxPlain) ++exactFail;
        if (!(plainFromNormalized(id, std::nan("1")) == d.minPlain)) ++exactFail;
        if (!(normalizedFromPlain(id, std::nan("1")) ==
              normalizedFromPlain(id, d.minPlain))) ++exactFail;
    }
    addValue(c, "step_exact_roundtrip_failures", exactFail);
    addValue(c, "continuous_ulp_roundtrip_failures", ulpFail);
    check(c, exactFail == 0, "step-domain boundary roundtrips must be exact");
    check(c, ulpFail == 0, "continuous-domain roundtrips must hold to 4 ulp");
}

//==========================================================================
// 10. events_block_positions (R17-P3): parameter events at the START, the
// MIDDLE and the LAST sample of a host block, plus one event at offset ==
// numSamples (block end -- must take effect for the NEXT block).
//==========================================================================
static void caseEventsBlockPositions(CaseResult& c) {
    using octavox::vst3wrap::ParamId;
    using octavox::vst3wrap::normalizedFromPlain;

    const int HOST_BLOCK = 1024;
    const int N = 6 * HOST_BLOCK;
    Lcg rng(1122334455);
    std::vector<float> in(N), wrapOut(N);
    rng.fill(in);

    // Event schedule over the whole stream (absolute stream positions).
    struct Ev {
        int pos;
        uint32_t id;
        double plain;
    };
    // Regular in-block events (delivered at their true in-block offsets).
    const std::vector<Ev> evs = {
        // block 1: START (offset 0), MIDDLE (offset 512), LAST (offset 1023)
        {1 * HOST_BLOCK + 0, ParamId::kPitch, -6.0},
        {1 * HOST_BLOCK + 512, ParamId::kFeedback, 0.35},
        {1 * HOST_BLOCK + HOST_BLOCK - 1, ParamId::kVoices, 3.0},
        // block 5: a mid-block per-voice delay change
        {5 * HOST_BLOCK + 777, octavox::vst3wrap::voiceDelayId(0), 12.0},
    };
    // The BLOCK-END event: delivered in block 3's queue with in-block offset
    // == numSamples (1024). The VST3 convention does not require hosts to
    // apply such points in that block; the wrapper drains them at the end of
    // process() so they take effect for the NEXT block -- i.e. at stream
    // position 4*HOST_BLOCK (block 4's first sample).
    const Ev blockEnd = {4 * HOST_BLOCK, octavox::vst3wrap::voiceGainId(1), 0.3};
    const int blockEndBlock = 3;  // the block whose queue carries it

    // ---- run A: the WRAPPER (host blocks of 1024, true in-block offsets;
    //      the block-end event rides block 3's queue at offset == n) ----
    {
        auto* p = makeProcessor(Setup{}, 1);
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
            if (b == blockEndBlock) {
                addEvent(ch, blockEnd.id, n,  // offset == numSamples (block end)
                         normalizedFromPlain(blockEnd.id, blockEnd.plain));
                any = true;
            }
            driveBlock(p, inP, outP, 1, n, any ? &ch : nullptr);
            inP[0] += n;
            outP[0] += n;
        }
        p->terminate();
        p->release();
    }

    // The complete event list in STREAM-POSITION order (the block-end event
    // at 4096 interleaves BETWEEN the voices event at 2047 and the delay
    // event at 5897 -- exactly where the wrapper's end-of-block drain
    // applies it).
    std::vector<Ev> allEvs = evs;
    allEvs.push_back(blockEnd);
    std::sort(allEvs.begin(), allEvs.end(),
              [](const Ev& a, const Ev& b) { return a.pos < b.pos; });

    // ---- run B: hand-driven core, setters at the SAME stream positions.
    //      The block-end event is issued at stream position 4*HOST_BLOCK,
    //      i.e. BETWEEN the process() calls of block 3 and block 4 --
    //      exactly where the wrapper applies it via the end-of-block drain. ----
    std::vector<float> refOut(N);
    {
        octavox::MultichannelShifter ref;
        check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 1), "reference prepare failed");
        float* inP[1] = {in.data()};
        float* outP[1] = {refOut.data()};
        int pos = 0;
        auto applyRef = [&](const Ev& e) {
            const double plain = toEnginePlain(e.id, e.plain);
            switch (e.id) {
            case ParamId::kPitch:
                ref.setRatio(std::pow(2.0, plain / 12.0));
                break;
            case ParamId::kFeedback:
                ref.setFeedback(plain);
                break;
            case ParamId::kVoices:
                ref.setVoices(static_cast<int>(std::lround(plain)));
                break;
            default:
                if (e.id == octavox::vst3wrap::voiceGainId(1))
                    ref.setVoiceGain(1, plain);
                else if (e.id == octavox::vst3wrap::voiceDelayId(0))
                    ref.setVoiceBaseDelay(
                        0, octavox::vst3wrap::msToBaseDelaySamples(
                               plain, octavox::Config{}.sample_rate,
                               octavox::Config{}.grain));
                break;
            }
        };
        for (const auto& e : allEvs) {
            if (e.pos > pos) {
                ref.process(inP, outP, 1, e.pos - pos);
                inP[0] += e.pos - pos;
                outP[0] += e.pos - pos;
                pos = e.pos;
            }
            applyRef(e);
        }
        if (N > pos) ref.process(inP, outP, 1, N - pos);
    }

    const bool same = bitIdentical(wrapOut, refOut);
    const double d = maxAbsDiff(wrapOut, refOut);
    addValue(c, "maxdiff", d);
    addValue(c, "bit_identical", same ? 1.0 : 0.0);
    check(c, same && d == 0.0,
          "events at block start/middle/last/block-end must be bit-identical "
          "to the core with setters at the same stream positions");

    // ---- control: vs a host that drops the block-END event entirely --
    // (proves the drain path genuinely applies the block-end event at the
    //  next block's first sample: dropping it must differ)
    std::vector<float> noEnd(N);
    {
        auto* p = makeProcessor(Setup{}, 1);
        float* inP[1] = {in.data()};
        float* outP[1] = {noEnd.data()};
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
            // b == blockEndBlock: the block-end event is NOT added (dropped)
            driveBlock(p, inP, outP, 1, n, any ? &ch : nullptr);
            inP[0] += n;
            outP[0] += n;
        }
        p->terminate();
        p->release();
    }
    const double dDrop = maxAbsDiff(wrapOut, noEnd);
    addValue(c, "blockend_event_effect_maxdiff", dDrop);
    check(c, dDrop > 0.0, "the block-end event must have an observable effect");
}

//==========================================================================
// 11. simultaneous_multivoice_events (R17-P3): several per-voice controls
// AND one global control changed at the SAME sample position (multiple
// queues, one point each, identical offsets). Bit-exact vs the core with
// the same setters issued in the same order at that stream position.
//==========================================================================
static void caseSimultaneousEvents(CaseResult& c) {
    using octavox::vst3wrap::ParamId;
    using octavox::vst3wrap::normalizedFromPlain;
    using octavox::vst3wrap::voiceGainId;
    using octavox::vst3wrap::voiceOffsetId;
    using octavox::vst3wrap::voiceMuteId;
    using octavox::vst3wrap::voiceDelayId;

    const int N = 24576;
    const int HOST_BLOCK = 2048;
    Lcg rng(606060);
    std::vector<float> in(N), wrapOut(N);
    rng.fill(in);

    // One simultaneous cluster at stream position 7000 (in-block offset
    // 7000 - 3*2048 = 856), a second at 15000 (offset 664), plus a lone
    // event at stream position 0 to set voices=8 first (so every per-voice
    // control is audible).
    struct Ev {
        int pos;
        uint32_t id;
        double plain;
    };
    const std::vector<Ev> cluster1 = {
        {7000, voiceGainId(2), 0.4},
        {7000, voiceOffsetId(3), 5.0},
        {7000, voiceMuteId(5), 1.0},
        {7000, voiceDelayId(7), 30.0},
        {7000, ParamId::kPitch, 3.0},
    };
    const std::vector<Ev> cluster2 = {
        {15000, voiceGainId(0), 1.7},
        {15000, voiceOffsetId(2), -4.0},
        {15000, voiceMuteId(1), 1.0},
        {15000, voiceDelayId(4), 8.0},
        {15000, ParamId::kWetDry, 0.55},
    };
    std::vector<Ev> evs;
    evs.push_back({0, ParamId::kVoices, 8.0});
    for (const auto& e : cluster1) evs.push_back(e);
    for (const auto& e : cluster2) evs.push_back(e);

    // ---- run A: the WRAPPER ----
    {
        auto* p = makeProcessor(Setup{}, 1);
        float* inP[1] = {in.data()};
        float* outP[1] = {wrapOut.data()};
        for (int b = 0; b * HOST_BLOCK < N; ++b) {
            const int start = b * HOST_BLOCK;
            const int n = std::min(HOST_BLOCK, N - start);
            ParameterChanges ch;
            bool any = false;
            // add queues in the event-list order (the wrapper applies a
            // simultaneous cluster in queue order; the reference mirrors it)
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

    // ---- run B: hand-driven core, the same setters at the same positions,
    //      clusters issued in the SAME order as the queues were added. The
    //      reference tracks the per-voice offsets exactly like the wrapper's
    //      surface_ mirror, and replicates retargetRatios() on every
    //      global pitch event (setRatio clears the overrides, then every
    //      non-zero tracked offset is re-applied on the new target). ----
    std::vector<float> refOut(N);
    {
        octavox::MultichannelShifter ref;
        check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 1), "reference prepare failed");
        const double sr = octavox::Config{}.sample_rate;
        const double grain = octavox::Config{}.grain;
        float* inP[1] = {in.data()};
        float* outP[1] = {refOut.data()};
        int pos = 0;
        double pitchPlain = 0.0;              // the wrapper's surface_.pitchSemi
        double offTrack[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // surface_.offset[]
        auto retarget = [&]() {
            ref.setRatio(std::pow(2.0, pitchPlain / 12.0));
            for (int v = 0; v < 8; ++v)
                if (offTrack[v] != 0.0)
                    ref.setVoiceRatio(v, std::pow(2.0, pitchPlain / 12.0) *
                                             std::pow(2.0, offTrack[v] / 12.0));
        };
        for (const auto& e : evs) {
            if (e.pos > pos) {
                ref.process(inP, outP, 1, e.pos - pos);
                inP[0] += e.pos - pos;
                outP[0] += e.pos - pos;
                pos = e.pos;
            }
            const double plain = toEnginePlain(e.id, e.plain);
            if (e.id == ParamId::kVoices)
                ref.setVoices(static_cast<int>(std::lround(plain)));
            else if (e.id == ParamId::kPitch) {
                pitchPlain = plain;
                retarget();
            } else if (e.id == ParamId::kWetDry)
                ref.setWetDry(plain);
            else if (e.id >= voiceOffsetId(0) && (e.id - voiceOffsetId(0)) % 4 == 0) {
                const int v = static_cast<int>((e.id - voiceOffsetId(0)) / 4);
                offTrack[v] = plain;
                if (plain != 0.0)
                    ref.setVoiceRatio(v, std::pow(2.0, pitchPlain / 12.0) *
                                             std::pow(2.0, plain / 12.0));
                else
                    retarget();  // clearing one override: the setRatio dance
            } else if (e.id >= voiceGainId(0) && (e.id - voiceGainId(0)) % 4 == 0) {
                const int v = static_cast<int>((e.id - voiceGainId(0)) / 4);
                ref.setVoiceGain(v, plain);
            } else if (e.id >= voiceMuteId(0) && (e.id - voiceMuteId(0)) % 4 == 0) {
                const int v = static_cast<int>((e.id - voiceMuteId(0)) / 4);
                ref.setVoiceMute(v, plain >= 0.5);
            } else if (e.id >= voiceDelayId(0) && (e.id - voiceDelayId(0)) % 4 == 0) {
                const int v = static_cast<int>((e.id - voiceDelayId(0)) / 4);
                ref.setVoiceBaseDelay(
                    v, octavox::vst3wrap::msToBaseDelaySamples(plain, sr, grain));
            }
        }
        if (N > pos) ref.process(inP, outP, 1, N - pos);
    }

    const bool same = bitIdentical(wrapOut, refOut);
    const double d = maxAbsDiff(wrapOut, refOut);
    addValue(c, "maxdiff", d);
    addValue(c, "bit_identical", same ? 1.0 : 0.0);
    check(c, same && d == 0.0,
          "simultaneous multi-voice events at one sample position must be "
          "bit-identical to the core with the same setter order");
}

//==========================================================================
// 12. bypass_wetdry_clickless_noalloc (R17-P3): bypass enter/exit toggles
// and a smooth host wet/dry ramp across many blocks. Gates: (a) zero
// allocations across the whole automation run (counter armed); (b) the
// maximum per-sample output step stays within the analytic no-click bound
// = reference step (same input, no automation) + the bypass ramp slope
// term + the wet/dry event step term.
//==========================================================================
static void caseBypassWetDryClickless(CaseResult& c) {
    using octavox::vst3wrap::ParamId;
    using octavox::vst3wrap::normalizedFromPlain;

    const int HOST_BLOCK = 512;
    const int BLOCKS = 120;
    const int N = HOST_BLOCK * BLOCKS;
    Lcg rng(9090909);
    std::vector<float> in(N);
    rng.fill(in);

    // Reference run: same input, NO automation (default surface, wet=1).
    double refMaxStep = 0.0, peakOut = 0.0;
    {
        auto* p = makeProcessor(Setup{}, 1);
        std::vector<float> out(N, 0.0f);
        float* inP[1] = {in.data()};
        float* outP[1] = {out.data()};
        for (int b = 0; b < BLOCKS; ++b) {
            driveBlock(p, inP, outP, 1, HOST_BLOCK, nullptr);
            inP[0] += HOST_BLOCK;
            outP[0] += HOST_BLOCK;
        }
        for (int i = 0; i < N; ++i) {
            peakOut = std::max(peakOut, std::fabs((double)out[i]));
            if (i > 0)
                refMaxStep = std::max(refMaxStep,
                                      std::fabs((double)out[i] - (double)out[i - 1]));
        }
        p->terminate();
        p->release();
    }
    addValue(c, "reference_max_step", refMaxStep);
    addValue(c, "peak_out", peakOut);

    // Automation schedule (pre-built BEFORE arming the alloc counter):
    //  - bypass ON at block 20, OFF at block 60 (10 ms core ramp at 48 kHz
    //    = 480 samples per full blend sweep);
    //  - wet/dry ramped 1.0 -> 0.5 -> 1.0 with ONE event per block over
    //    blocks 30..90 (delta-w per event = 0.5/30 ~ 0.0167).
    std::vector<std::unique_ptr<ParameterChanges>> evs;
    const double dwPerEvent = 0.5 / 30.0;
    for (int b = 0; b < BLOCKS; ++b) {
        auto ch = std::make_unique<ParameterChanges>();
        if (b == 20 || b == 60)
            addEvent(*ch, ParamId::kBypass, 0,
                     b == 20 ? 1.0 : 0.0);  // normalized: 1 = bypassed
        if (b >= 30 && b <= 90) {
            int k = b - 30;  // 0..60
            double w;
            if (k <= 30) w = 1.0 - k * dwPerEvent;        // 1.0 -> 0.5
            else w = 0.5 + (k - 30) * dwPerEvent;         // 0.5 -> 1.0
            addEvent(*ch, ParamId::kWetDry, 0, normalizedFromPlain(ParamId::kWetDry, w));
        }
        evs.push_back(std::move(ch));
    }

    // Automation run with the allocation counter armed across ALL calls.
    auto* p = makeProcessor(Setup{}, 1);
    std::vector<float> out(N, 0.0f);
    {
        float* inP[1] = {in.data()};
        float* outP[1] = {out.data()};
        octavox::allocs::reset();
        octavox::allocs::arm(true);
        for (int b = 0; b < BLOCKS; ++b) {
            driveBlock(p, inP, outP, 1, HOST_BLOCK, evs[b].get());
            inP[0] += HOST_BLOCK;
            outP[0] += HOST_BLOCK;
        }
        octavox::allocs::arm(false);
    }
    const long allocs = octavox::allocs::count();
    addValue(c, "allocations_during_automation", static_cast<double>(allocs));
    check(c, allocs == 0, "automation run allocated (global-new counter)");

    // No-click bound: per-sample output step <= reference step
    //   + bypass ramp term: |mix-dry| per blend sample <= 2*peak/(sr*0.010)
    //   + wet/dry event term: |dw| * |mix-dry| <= dw * 2*peak
    const double sr = 48000.0;
    const double bypassSeconds = octavox::Config{}.bypass_seconds;  // 0.010
    const double bypassStep = 2.0 * peakOut / (sr * bypassSeconds);
    const double wetdryStep = dwPerEvent * 2.0 * peakOut;
    const double bound = refMaxStep + bypassStep + wetdryStep + 1e-12;
    double maxStep = 0.0;
    int stepPos = 0;
    for (int i = 1; i < N; ++i) {
        const double s = std::fabs((double)out[i] - (double)out[i - 1]);
        if (s > maxStep) {
            maxStep = s;
            stepPos = i;
        }
    }
    addValue(c, "automation_max_step", maxStep);
    addValue(c, "automation_max_step_at_sample", stepPos);
    addValue(c, "no_click_bound", bound);
    check(c, maxStep <= bound,
          "per-sample output step during bypass+wet/dry automation exceeds "
          "the analytic no-click bound (click suspect)");
    p->terminate();
    p->release();
}

//==========================================================================
// 13. state_full_surface_restore (R17-P3): set ALL 38 parameters to
// non-default values, save, restore into a FRESH instance: every parameter
// value must come back exactly and the replay must be bit-identical to the
// hand-driven core configured with the same surface.
//==========================================================================
static void caseStateFullSurface(CaseResult& c) {
    using octavox::vst3wrap::ParamId;
    using octavox::vst3wrap::normalizedFromPlain;
    using octavox::vst3wrap::voiceGainId;
    using octavox::vst3wrap::voiceOffsetId;
    using octavox::vst3wrap::voiceMuteId;
    using octavox::vst3wrap::voiceDelayId;
    namespace vw = octavox::vst3wrap;

    const int N = 32768;
    Lcg rng(70707);
    std::vector<float> in(N);
    rng.fill(in);

    // The non-default surface (every one of the 38 parameters moved off its
    // default; bypass stays 0 so the processing path stays exercised -- its
    // restoration is verified through the normalized roundtrip below).
    double plainVals[38];
    plainVals[ParamId::kBypass] = 0.0;
    plainVals[ParamId::kPitch] = -7.31;
    plainVals[ParamId::kVoices] = 6.0;
    plainVals[ParamId::kSpread] = 0.0;
    plainVals[ParamId::kFeedback] = 0.45;
    plainVals[ParamId::kWetDry] = 0.62;
    for (int v = 0; v < 8; ++v) {
        plainVals[voiceOffsetId(v)] = 3.0 - v;                 // +3 .. -4 st
        plainVals[voiceGainId(v)] = 0.3 + 0.15 * v;            // 0.3 .. 1.35
        plainVals[voiceMuteId(v)] = (v % 3 == 2) ? 1.0 : 0.0;  // voices 3,6,9->8
        plainVals[voiceDelayId(v)] = 5.0 + 7.0 * v;            // 5 .. 54 ms
    }

    // ---- the hand-driven reference: canonical application order with the
    //      wrapper's conversion chain. ----
    std::vector<float> refOut(N);
    {
        octavox::MultichannelShifter ref;
        octavox::Config cfg = vw::defaultConfig();
        check(c, ref.prepare(cfg, 1), "reference prepare failed");
        const double sr = cfg.sample_rate, grain = cfg.grain;
        const double pitch = toEnginePlain(ParamId::kPitch, plainVals[ParamId::kPitch]);
        const auto off = [&](int v) {
            return toEnginePlain(voiceOffsetId(v), plainVals[voiceOffsetId(v)]);
        };
        const auto gn = [&](int v) {
            return toEnginePlain(voiceGainId(v), plainVals[voiceGainId(v)]);
        };
        const auto ms = [&](int v) {
            return toEnginePlain(voiceDelayId(v), plainVals[voiceDelayId(v)]);
        };
        ref.setSpread(toEnginePlain(ParamId::kSpread, plainVals[ParamId::kSpread]) >= 0.5);
        ref.setRatio(std::pow(2.0, pitch / 12.0));
        for (int v = 0; v < 8; ++v)
            if (off(v) != 0.0)
                ref.setVoiceRatio(v, std::pow(2.0, pitch / 12.0) * std::pow(2.0, off(v) / 12.0));
        for (int v = 0; v < 8; ++v) ref.setVoiceGain(v, gn(v));
        for (int v = 0; v < 8; ++v)
            ref.setVoiceMute(v, toEnginePlain(voiceMuteId(v), plainVals[voiceMuteId(v)]) >= 0.5);
        for (int v = 0; v < 8; ++v)
            ref.setVoiceBaseDelay(v, vw::msToBaseDelaySamples(ms(v), sr, grain));
        ref.setVoices(static_cast<int>(std::lround(
            toEnginePlain(ParamId::kVoices, plainVals[ParamId::kVoices]))));
        ref.setFeedback(toEnginePlain(ParamId::kFeedback, plainVals[ParamId::kFeedback]));
        ref.setWetDry(toEnginePlain(ParamId::kWetDry, plainVals[ParamId::kWetDry]));
        ref.setBypass(plainVals[ParamId::kBypass] >= 0.5);
        float* inP[1] = {in.data()};
        float* outP[1] = {refOut.data()};
        ref.process(inP, outP, 1, N);
    }

    // ---- wrapper: apply the full surface via ONE position-0 event cluster
    //      (canonical queue order), run, save. ----
    std::vector<float> aOut(N);
    MemoryStream state;
    {
        auto* p = makeProcessor(Setup{}, 1);
        ParameterChanges ch;
        // canonical order: spread, pitch, offsets, gains, mutes, delays,
        // voices, feedback, wet/dry, bypass
        addEvent(ch, ParamId::kSpread, 0,
                 normalizedFromPlain(ParamId::kSpread, plainVals[ParamId::kSpread]));
        addEvent(ch, ParamId::kPitch, 0,
                 normalizedFromPlain(ParamId::kPitch, plainVals[ParamId::kPitch]));
        for (int v = 0; v < 8; ++v)
            addEvent(ch, voiceOffsetId(v), 0,
                     normalizedFromPlain(voiceOffsetId(v), plainVals[voiceOffsetId(v)]));
        for (int v = 0; v < 8; ++v)
            addEvent(ch, voiceGainId(v), 0,
                     normalizedFromPlain(voiceGainId(v), plainVals[voiceGainId(v)]));
        for (int v = 0; v < 8; ++v)
            addEvent(ch, voiceMuteId(v), 0,
                     normalizedFromPlain(voiceMuteId(v), plainVals[voiceMuteId(v)]));
        for (int v = 0; v < 8; ++v)
            addEvent(ch, voiceDelayId(v), 0,
                     normalizedFromPlain(voiceDelayId(v), plainVals[voiceDelayId(v)]));
        addEvent(ch, ParamId::kVoices, 0,
                 normalizedFromPlain(ParamId::kVoices, plainVals[ParamId::kVoices]));
        addEvent(ch, ParamId::kFeedback, 0,
                 normalizedFromPlain(ParamId::kFeedback, plainVals[ParamId::kFeedback]));
        addEvent(ch, ParamId::kWetDry, 0,
                 normalizedFromPlain(ParamId::kWetDry, plainVals[ParamId::kWetDry]));
        addEvent(ch, ParamId::kBypass, 0,
                 normalizedFromPlain(ParamId::kBypass, plainVals[ParamId::kBypass]));

        float* inP[1] = {in.data()};
        float* outP[1] = {aOut.data()};
        driveBlock(p, inP, outP, 1, 4096, &ch);
        {
            float* in2[1] = {in.data() + 4096};
            float* out2[1] = {aOut.data() + 4096};
            driveBlock(p, in2, out2, 1, N - 4096, nullptr);
        }
        const double dLive = maxAbsDiff(aOut, refOut);
        addValue(c, "live_full_surface_vs_reference_maxdiff", dLive);
        check(c, dLive == 0.0, "full-surface live run must be bit-identical");

        check(c, p->getState(&state) == kResultOk, "getState failed");
        addValue(c, "state_bytes", static_cast<double>(state.getSize()));
        p->terminate();
        p->release();
    }

    // ---- fresh instance restore: initialize -> setState -> setup ->
    //      activate; verify EVERY parameter comes back exactly. ----
    std::vector<float> bOut(N);
    {
        auto* p = new OctavoxNativeProcessor();
        check(c, p->initialize(nullptr) == kResultOk, "fresh init failed");
        int64 spos = 0;
        state.seek(0, IBStream::kIBSeekSet, &spos);
        check(c, p->setState(&state) == kResultOk, "fresh setState failed");
        standardSetup(p);
        check(c, p->setActive(true) == kResultOk, "fresh activate failed");

        int valueMismatches = 0;
        for (uint32_t id = 0; id < 38u; ++id) {
            const double wantNorm = normalizedFromPlain(
                id, toEnginePlain(id, plainVals[id]));
            const double gotNorm = p->getParamNormalized(id);
            if (gotNorm != wantNorm) ++valueMismatches;
        }
        addValue(c, "restored_value_mismatches", valueMismatches);
        check(c, valueMismatches == 0,
              "every one of the 38 parameter values must be restored exactly "
              "(normalized roundtrip equality)");

        float* inP[1] = {in.data()};
        float* outP[1] = {bOut.data()};
        driveBlock(p, inP, outP, 1, N, nullptr);
        const double dFresh = maxAbsDiff(bOut, refOut);
        addValue(c, "fresh_restore_replay_maxdiff", dFresh);
        check(c, dFresh == 0.0, "full-surface fresh-restore replay mismatch");
        p->terminate();
        p->release();
    }
}

//==========================================================================
// 14. state_cross_rate_layout (R17-P3): state saved at 48 kHz stereo,
// restored at 96 kHz MONO and at 44.1 kHz stereo. The restore re-prepares
// the engine at the host's current rate/layout (documented contract); the
// replay must be bit-identical to a hand-driven core prepared at that rate
// and layout with the same surface.
//==========================================================================
static void caseStateCrossRateLayout(CaseResult& c) {
    using octavox::vst3wrap::ParamId;
    using octavox::vst3wrap::normalizedFromPlain;
    using octavox::vst3wrap::voiceGainId;
    using octavox::vst3wrap::voiceOffsetId;
    using octavox::vst3wrap::voiceMuteId;
    using octavox::vst3wrap::voiceDelayId;
    namespace vw = octavox::vst3wrap;

    const int N = 16384;
    Lcg rng(80808);
    std::vector<float> in(N);
    rng.fill(in);

    // A moderate non-default surface (2 voices, so per-voice ids 0/1 matter).
    // The reference replicates the wrapper's documented CROSS-RATE restore
    // semantics (VST3_WRAPPER.md 4): the FULL surface is re-applied at the
    // host's CURRENT rate -- including the DEFAULT per-voice base delays,
    // which are MILLISECONDS in the parameter surface (the spec-table
    // defaults) and therefore scale with the rate. (At 48 kHz they coincide
    // with the core's sample-domain default table 256+32*v; at other rates
    // the ms-domain interpretation is the documented wrapper behaviour.)
    auto applyRef = [&](octavox::MultichannelShifter& ref, double sr) {
        octavox::Config cfg = vw::defaultConfig();
        cfg.sample_rate = sr;
        check(c, ref.prepare(cfg, 1), "reference prepare failed");
        const double grain = cfg.grain;
        const double pitch = toEnginePlain(ParamId::kPitch, 4.0);
        const double off0 = toEnginePlain(voiceOffsetId(0), -2.0);
        const double g1 = toEnginePlain(voiceGainId(1), 0.5);
        const double ms0 = toEnginePlain(voiceDelayId(0), 15.0);
        ref.setSpread(true);
        ref.setRatio(std::pow(2.0, pitch / 12.0));
        if (off0 != 0.0)
            ref.setVoiceRatio(0, std::pow(2.0, pitch / 12.0) * std::pow(2.0, off0 / 12.0));
        for (int v = 0; v < 8; ++v)
            ref.setVoiceGain(v, v == 1 ? g1 : 1.0);
        for (int v = 0; v < 8; ++v) {
            const double msv = (v == 0) ? ms0 : vw::paramSpec(voiceDelayId(v)).defaultPlain;
            ref.setVoiceBaseDelay(v, vw::msToBaseDelaySamples(msv, sr, grain));
        }
        ref.setVoices(2);
        ref.setFeedback(0.2);
        ref.setWetDry(0.8);
    };

    // ---- save at 48 kHz stereo (a short run so the state is "live") ----
    MemoryStream state;
    {
        auto* p = makeProcessor(Setup{48000.0, 4096}, 2);  // stereo @ 48k
        ParameterChanges ch;
        addEvent(ch, ParamId::kPitch, 0, normalizedFromPlain(ParamId::kPitch, 4.0));
        addEvent(ch, ParamId::kVoices, 0, normalizedFromPlain(ParamId::kVoices, 2.0));
        addEvent(ch, ParamId::kFeedback, 0, normalizedFromPlain(ParamId::kFeedback, 0.2));
        addEvent(ch, ParamId::kWetDry, 0, normalizedFromPlain(ParamId::kWetDry, 0.8));
        addEvent(ch, voiceOffsetId(0), 0, normalizedFromPlain(voiceOffsetId(0), -2.0));
        addEvent(ch, voiceGainId(1), 0, normalizedFromPlain(voiceGainId(1), 0.5));
        addEvent(ch, voiceDelayId(0), 0, normalizedFromPlain(voiceDelayId(0), 15.0));
        std::vector<float> inL = in, inR = in;  // duplicate mono material
        std::vector<float> outL(N), outR(N);
        float* inP[2] = {inL.data(), inR.data()};
        float* outP[2] = {outL.data(), outR.data()};
        driveBlock(p, inP, outP, 2, N, &ch);
        check(c, p->getState(&state) == kResultOk, "getState failed");
        p->terminate();
        p->release();
    }

    // ---- restore A: 96 kHz MONO. Host flow: initialize -> setState ->
    //      setBusArrangements(mono) -> setupProcessing(96k) -> activate. ---
    {
        std::vector<float> out(N);
        auto* p = new OctavoxNativeProcessor();
        check(c, p->initialize(nullptr) == kResultOk, "init failed (96k leg)");
        int64 spos = 0;
        state.seek(0, IBStream::kIBSeekSet, &spos);
        check(c, p->setState(&state) == kResultOk, "setState failed (96k leg)");
        SpeakerArrangement m = SpeakerArr::kMono;
        check(c, p->setBusArrangements(&m, 1, &m, 1) == kResultOk,
              "mono negotiation failed (96k leg)");
        ProcessSetup ps;
        std::memset(&ps, 0, sizeof(ps));
        ps.processMode = kRealtime;
        ps.symbolicSampleSize = kSample32;
        ps.sampleRate = 96000.0;
        ps.maxSamplesPerBlock = 4096;
        check(c, p->setupProcessing(ps) == kResultOk, "96k setup failed");
        check(c, p->setActive(true) == kResultOk, "96k activate failed");
        float* inP[1] = {in.data()};
        float* outP[1] = {out.data()};
        driveBlock(p, inP, outP, 1, N, nullptr);

        std::vector<float> refOut(N);
        {
            octavox::MultichannelShifter ref;
            applyRef(ref, 96000.0);
            float* rIn[1] = {in.data()};
            float* rOut[1] = {refOut.data()};
            ref.process(rIn, rOut, 1, N);
        }
        const double d = maxAbsDiff(out, refOut);
        addValue(c, "restore_96k_mono_vs_reference_maxdiff", d);
        check(c, d == 0.0,
              "state restored at 96 kHz mono must replay bit-identically to "
              "the core prepared at 96 kHz mono with the same surface");
        p->terminate();
        p->release();
    }

    // ---- restore B: 44.1 kHz stereo. ----
    {
        std::vector<float> outL(N), outR(N);
        auto* p = new OctavoxNativeProcessor();
        check(c, p->initialize(nullptr) == kResultOk, "init failed (44.1k leg)");
        int64 spos = 0;
        state.seek(0, IBStream::kIBSeekSet, &spos);
        check(c, p->setState(&state) == kResultOk, "setState failed (44.1k leg)");
        ProcessSetup ps;
        std::memset(&ps, 0, sizeof(ps));
        ps.processMode = kRealtime;
        ps.symbolicSampleSize = kSample32;
        ps.sampleRate = 44100.0;
        ps.maxSamplesPerBlock = 4096;
        check(c, p->setupProcessing(ps) == kResultOk, "44.1k setup failed");
        check(c, p->setActive(true) == kResultOk, "44.1k activate failed");
        std::vector<float> inL = in, inR = in;
        float* inP[2] = {inL.data(), inR.data()};
        float* outP[2] = {outL.data(), outR.data()};
        driveBlock(p, inP, outP, 2, N, nullptr);

        std::vector<float> refOut(N);
        {
            octavox::MultichannelShifter ref;
            applyRef(ref, 44100.0);
            float* rIn[1] = {in.data()};
            float* rOut[1] = {refOut.data()};
            ref.process(rIn, rOut, 1, N);
        }
        const double d = maxAbsDiff(outL, refOut);
        addValue(c, "restore_44k1_stereo_vs_reference_maxdiff", d);
        check(c, d == 0.0,
              "state restored at 44.1 kHz stereo must replay bit-identically "
              "to the core prepared at 44.1 kHz with the same surface");
        p->terminate();
        p->release();
    }
}

//==========================================================================
// 15. state_invalid_blobs (R17-P3): every truncation prefix of a valid
// blob, wrong magic, wrong version, NaN config fields, ring-law violation:
// each must be REJECTED (kResultFalse), and the processor must stay usable
// afterwards (previous state intact, still processes, a valid setState
// still succeeds).
//==========================================================================
static void caseStateInvalidBlobs(CaseResult& c) {
    using octavox::vst3wrap::ParamId;
    using octavox::vst3wrap::normalizedFromPlain;

    // ---- make a valid blob with a non-default surface ----
    MemoryStream state;
    const int N = 2048;
    {
        auto* p = makeProcessor(Setup{}, 1);
        ParameterChanges ch;
        addEvent(ch, ParamId::kPitch, 0, normalizedFromPlain(ParamId::kPitch, 5.0));
        addEvent(ch, ParamId::kVoices, 0, normalizedFromPlain(ParamId::kVoices, 4.0));
        Lcg rng(1234321);
        std::vector<float> in(N), out(N);
        rng.fill(in);
        float* inP[1] = {in.data()};
        float* outP[1] = {out.data()};
        driveBlock(p, inP, outP, 1, N, &ch);
        check(c, p->getState(&state) == kResultOk, "getState failed");
        p->terminate();
        p->release();
    }
    const int L = static_cast<int>(state.getSize());
    addValue(c, "blob_bytes", L);
    check(c, L > 120, "implausibly small state blob");

    // raw bytes of the valid blob
    std::vector<char> good(L);
    {
        int64 spos = 0;
        state.seek(0, IBStream::kIBSeekSet, &spos);
        int32 read = 0;
        check(c, state.read(good.data(), L, &read) == kResultOk && read == L,
              "failed to read back the valid blob");
    }
    // Self-verify the field offsets used below against known values
    // (R19 v2 layout: magic@0, version@4, cfg@8 [sample_rate@8, ratio@16,
    // voices@24, max_voices@28, spread@32, feedback@36, interpolation@44,
    // grain@48, base_delay@56, fmin@64, ..., engagement@96, gain_ramp@104,
    // bypass@112, detector_mode@120, commit_xf@124, delay_xf@132,
    // voice_fade@140], channels@148, surface doubles@152 [slot i == param
    // id i: bypass@152, pitch@160, voices@168, ...]).
    {
        const uint32_t magic = *reinterpret_cast<const uint32_t*>(&good[0]);
        check(c, magic == 0x3152564Fu, "blob magic offset self-check failed");
        const int32_t ver = *reinterpret_cast<const int32_t*>(&good[4]);
        check(c, ver == 2, "blob version offset self-check failed (R19 v2)");
        const double sr = *reinterpret_cast<const double*>(&good[8]);
        check(c, sr == 48000.0, "blob sample_rate offset self-check failed");
        const int32_t cfgVoices = *reinterpret_cast<const int32_t*>(&good[24]);
        check(c, cfgVoices == 1,
              "blob cfg.voices offset self-check failed (got " +
                  std::to_string(cfgVoices) + ", want the core default 1)");
        const double grain = *reinterpret_cast<const double*>(&good[48]);
        check(c, grain == octavox::Config{}.grain,
              "blob grain offset self-check failed");
        const double baseDelay = *reinterpret_cast<const double*>(&good[56]);
        check(c, baseDelay == octavox::Config{}.base_delay,
              "blob base_delay offset self-check failed");
        const int32_t dmode = *reinterpret_cast<const int32_t*>(&good[120]);
        check(c, dmode == static_cast<int>(octavox::DetectorMode::Decimated),
              "blob detector_mode offset self-check failed (R19 v2)");
        const double xf = *reinterpret_cast<const double*>(&good[124]);
        check(c, xf == octavox::Config{}.commit_crossfade_seconds,
              "blob commit_crossfade offset self-check failed (R19 v2)");
        const double surfVoices = *reinterpret_cast<const double*>(&good[168]);
        check(c, surfVoices == 4.0,
              "blob surface voices offset self-check failed (got " +
                  std::to_string(surfVoices) + ", want 4)");
        const double surfPitch = *reinterpret_cast<const double*>(&good[160]);
        check(c, surfPitch == toEnginePlain(ParamId::kPitch, 5.0),
              "blob surface pitch offset self-check failed");
    }

    // helper: setState from raw bytes on a fresh processor, expect rejection
    auto rejectsWith = [&](const std::vector<char>& bytes) {
        auto* p = makeProcessor(Setup{});
        MemoryStream ms;
        int32 written = 0;
        ms.write(const_cast<char*>(bytes.data()),
                 static_cast<int32>(bytes.size()), &written);
        int64 spos = 0;
        ms.seek(0, IBStream::kIBSeekSet, &spos);
        const tresult r = p->setState(&ms);
        p->terminate();
        p->release();
        return r != kResultOk;
    };

    // (a) every truncation prefix 0..L-1 must be rejected
    int acceptedTrunc = 0;
    for (int len = 0; len < L; ++len) {
        std::vector<char> trunc(good.begin(), good.begin() + len);
        if (!rejectsWith(trunc)) ++acceptedTrunc;
    }
    addValue(c, "truncation_prefixes_accepted", acceptedTrunc);
    check(c, acceptedTrunc == 0,
          "every truncation prefix of the state blob must be rejected");

    // (b) wrong magic
    {
        std::vector<char> bad = good;
        bad[0] = static_cast<char>(bad[0] ^ 0x5A);
        check(c, rejectsWith(bad), "wrong magic must be rejected");
    }
    // (c) wrong version (2 is now the CURRENT version; 1 is the supported
    // legacy version -- an unsupported future version must be rejected)
    {
        std::vector<char> bad = good;
        *reinterpret_cast<int32_t*>(&bad[4]) = 3;
        check(c, rejectsWith(bad), "unsupported version must be rejected");
    }
    // (d) NaN sample_rate / NaN grain (non-finite, structurally unfixable:
    //     rejected); NaN ratio (fixable: the documented defensive-clamp
    //     policy sanitizes it to 1.0 and the restore proceeds)
    {
        std::vector<char> bad = good;
        *reinterpret_cast<double*>(&bad[8]) = std::nan("1");
        check(c, rejectsWith(bad), "NaN sample_rate must be rejected");
    }
    {
        std::vector<char> bad = good;
        *reinterpret_cast<double*>(&bad[16]) = std::nan("1");  // ratio @16
        // documented policy: sanitizeConfig replaces a non-finite/non-positive
        // ratio with 1.0 (a fixable field) -- the restore must SUCCEED and
        // the engine must stay usable.
        check(c, !rejectsWith(bad),
              "NaN ratio must be accepted under the documented sanitize policy");
    }
    {
        std::vector<char> bad = good;
        *reinterpret_cast<double*>(&bad[48]) = std::nan("1");  // grain @48
        check(c, rejectsWith(bad), "NaN grain must be rejected");
    }
    // (e) base_delay violating the ring-range law (reject, not clamp)
    {
        std::vector<char> bad = good;
        *reinterpret_cast<double*>(&bad[56]) = 250000.0;  // base_delay @56
        check(c, rejectsWith(bad), "ring-law violating base_delay must be rejected");
    }
    // (f) zero sample rate
    {
        std::vector<char> bad = good;
        *reinterpret_cast<double*>(&bad[8]) = 0.0;
        check(c, rejectsWith(bad), "zero sample_rate must be rejected");
    }

    // (g) after all rejections: the processor is still USABLE and the last
    //     good state is intact. A valid blob must still restore and the
    //     replay must equal the original run (pitch +5, voices 4).
    {
        auto* p = makeProcessor(Setup{}, 1);
        Lcg rng(1234321);
        std::vector<float> in(N), out(N);
        rng.fill(in);
        // the reference: core with the same surface
        std::vector<float> refOut(N);
        {
            octavox::MultichannelShifter ref;
            check(c, ref.prepare(octavox::vst3wrap::defaultConfig(), 1),
                  "reference prepare failed");
            const double pitch = toEnginePlain(ParamId::kPitch, 5.0);
            ref.setRatio(std::pow(2.0, pitch / 12.0));
            ref.setVoices(4);
            float* rIn[1] = {in.data()};
            float* rOut[1] = {refOut.data()};
            ref.process(rIn, rOut, 1, N);
        }
        int64 spos = 0;
        state.seek(0, IBStream::kIBSeekSet, &spos);
        check(c, p->setState(&state) == kResultOk,
              "a VALID setState after rejections must still succeed");
        float* inP[1] = {in.data()};
        float* outP[1] = {out.data()};
        driveBlock(p, inP, outP, 1, N, nullptr);
        const double d = maxAbsDiff(out, refOut);
        addValue(c, "usable_after_rejections_maxdiff", d);
        check(c, d == 0.0,
              "the processor must stay usable with intact state after "
              "invalid-blob rejections");
        bool fin = true;
        for (float x : out)
            if (!std::isfinite(x)) fin = false;
        check(c, fin, "non-finite output after invalid-blob rejections");
        p->terminate();
        p->release();
    }
}

//==========================================================================
// main
//==========================================================================
int main(int argc, char** argv) {
    // --dump-params: print the 38-parameter metadata table and exit.
    if (argc > 1 && std::strcmp(argv[1], "--dump-params") == 0) {
        using octavox::vst3wrap::paramSpec;
        using octavox::vst3wrap::normalizedFromPlain;
        std::printf("%-4s %-22s %-10s %-9s %-7s %-7s %-7s %-8s %-11s %-10s %-4s %-6s\n",
                    "ID", "TITLE", "SHORT", "UNITS", "MIN", "MAX", "STEP",
                    "STEPCNT", "DEFAULT", "DEFNORM", "FLAG", "UNIT");
        std::printf("%s\n", std::string(130, '-').c_str());
        for (uint32_t id = 0; id < 38u; ++id) {
            const auto& sp = paramSpec(id);
            const auto& d = sp.domain;
            const int32 stepCount = (d.stepPlain > 0.0)
                ? static_cast<int32>(std::floor((d.maxPlain - d.minPlain) /
                                                d.stepPlain + 0.5))
                : 0;
            char def[32], defn[32];
            std::snprintf(def, sizeof(def), "%.10g", sp.defaultPlain);
            std::snprintf(defn, sizeof(defn), "%.17g",
                          normalizedFromPlain(id, sp.defaultPlain));
            std::printf("%-4u %-22s %-10s %-9s %-7.4g %-7.4g %-7.4g %-8d %-11s %-10s 0x%x %-6d\n",
                        id, sp.title, sp.shortTitle, sp.units ? sp.units : "",
                        d.minPlain, d.maxPlain, d.stepPlain, stepCount, def,
                        defn, sp.flags, sp.unitId);
        }
        return 0;
    }

    std::vector<std::pair<const char*, void (*)(CaseResult&)>> cases = {
        {"lifecycle_smoke", caseLifecycle},
        {"param_roundtrip", caseParamRoundtrip},
        {"segmentation_equivalence_BITEXACT", caseSegmentation},
        {"state_roundtrip_BITEXACT", caseState},
        {"silence_blocks_reset", caseSilenceBlocksReset},
        {"inplace_safety", caseInPlace},
        {"no_alloc_steady_state", caseNoAlloc},
        {"latency_report", caseLatency},
        {"param_metadata_table", caseParamMetadata},
        {"events_block_positions", caseEventsBlockPositions},
        {"simultaneous_multivoice_events", caseSimultaneousEvents},
        {"bypass_wetdry_clickless_noalloc", caseBypassWetDryClickless},
        {"state_full_surface_restore", caseStateFullSurface},
        {"state_cross_rate_layout", caseStateCrossRateLayout},
        {"state_invalid_blobs", caseStateInvalidBlobs},
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
                "octavox-native VST3 wrapper tests (R16-P4b + R17-P3)", passed,
                static_cast<int>(g_results.size()) - passed,
                static_cast<int>(g_results.size()));
    return passed == static_cast<int>(g_results.size()) ? 0 : 1;
}
