// ---------------------------------------------------------------------------
// OCTAVOX native core -- VST3 wrapper: the single-component effect impl
// (R16-P4b). See octavox_native_processor.h for the design note.
// ============================================================================
#include "octavox_native_processor.h"

#include "octavox_build_config.h"
#include "octavox_ids.h"

#include "base/source/fstreamer.h"
#include "base/source/fobject.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstbus.h"

#include <climits>
#include <cmath>
#include <cstring>

namespace octavox {
namespace vst3wrap {

using Steinberg::int32;
using Steinberg::tresult;
using Steinberg::uint32;
using Steinberg::TBool;
using Steinberg::IBStream;
using namespace Steinberg::Vst;

// Fresh UIDs minted for this wrapper (not copied from any SDK sample);
// per-variant FUIDs since R18-WDB (see octavox_build_config.h):
//   LAGRANGE6  processor {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60} (lineage)
//              factory   {7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44} (lineage)
//   OBSKERNEL6 processor {E3E7FA70-6A38-0D35-B05F-64B91F1E2689} (minted)
//              factory   {3D4CDBC6-ACAC-BB09-077F-D5B80BD2F550} (minted)
// (declared as constexpr TUIDs in octavox_build_config.h -- DECLARE_UID form)

//==========================================================================
// Construction / lifecycle
//==========================================================================

OctavoxNativeProcessor::OctavoxNativeProcessor() {
    surface_ = defaultSurface();
    cfg_ = defaultConfig();
    processSetup.sampleRate = 0;
    processSetup.maxSamplesPerBlock = 0;
}

OctavoxNativeProcessor::~OctavoxNativeProcessor() = default;

tresult PLUGIN_API OctavoxNativeProcessor::initialize(Steinberg::FUnknown* context) {
    const tresult r = SingleComponentEffect::initialize(context);
    if (r != Steinberg::kResultOk) return r;

    // Buses: symmetric main in/out, stereo by default (mono negotiated via
    // setBusArrangements).
    addAudioInput(STR16("Audio Input"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Audio Output"), SpeakerArr::kStereo);

    // Parameters + units (the host-managed container from EditControllerEx1).
    registerParameters(parameters);
    registerUnits(*this);
    return Steinberg::kResultOk;
}

tresult PLUGIN_API OctavoxNativeProcessor::terminate() {
    engineReady_ = false;
    active_ = false;
    return SingleComponentEffect::terminate();
}

//==========================================================================
// Bus negotiation: symmetric mono or stereo only
//==========================================================================

tresult PLUGIN_API OctavoxNativeProcessor::setBusArrangements(SpeakerArrangement* inputs,
                                                              int32 numIns,
                                                              SpeakerArrangement* outputs,
                                                              int32 numOuts) {
    if (numIns != 1 || numOuts != 1) return Steinberg::kResultFalse;
    if (inputs == nullptr || outputs == nullptr) return Steinberg::kResultFalse;
    const int32 nIn = SpeakerArr::getChannelCount(inputs[0]);
    const int32 nOut = SpeakerArr::getChannelCount(outputs[0]);
    if (nIn != nOut) return Steinberg::kResultFalse;
    if (nIn != 1 && nIn != 2) return Steinberg::kResultFalse;

    // Accept the arrangement and update the bus objects (the host then reads
    // the negotiated layout back via getBusArrangement). The bus lists are
    // protected members of the SingleComponentEffect base (FCast pattern as
    // in the SDK's again sample).
    if (auto* inBus = Steinberg::FCast<AudioBus>(audioInputs.at(0)))
        inBus->setArrangement(inputs[0]);
    if (auto* outBus = Steinberg::FCast<AudioBus>(audioOutputs.at(0)))
        outBus->setArrangement(outputs[0]);
    channels_ = nIn;
    if (active_) {
        // Bus change while active: re-prepare for the new channel count
        // (allocate-and-rebuild path -- off the audio thread by contract).
        cfg_.sample_rate = sampleRate();
        engineReady_ = engine_.prepare(cfg_, channels_);
        if (engineReady_) applyFullSurface();
    }
    return engineReady_ || !active_ ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

tresult PLUGIN_API OctavoxNativeProcessor::canProcessSampleSize(int32 symbolicSampleSize) {
    return symbolicSampleSize == SymbolicSampleSizes::kSample32 ? Steinberg::kResultTrue
                                                                : Steinberg::kResultFalse;
}

//==========================================================================
// Processing setup / activation
//==========================================================================

tresult PLUGIN_API OctavoxNativeProcessor::setupProcessing(ProcessSetup& setup) {
    if (setup.symbolicSampleSize != SymbolicSampleSizes::kSample32)
        return Steinberg::kResultFalse;
    // The core's documented sample-rate domain: (0, 1e6] Hz (the R13-b
    // detector window law's int-cast safety bound -- dsp.hpp/dsp.cpp).
    // Refuse out-of-domain setups BEFORE accepting them into processSetup
    // (a rejected setup must not poison the last accepted one).
    if (!(setup.sampleRate > 0.0) || setup.sampleRate > 1.0e6)
        return Steinberg::kResultFalse;
    processSetup = setup;

    // Preallocate the in-place scratch for the host's maximum block size
    // (plus a safety margin; process() chunks if a host ever exceeds it --
    // no allocation in process()).
    const int32 want = setup.maxSamplesPerBlock > 0 ? setup.maxSamplesPerBlock : 1024;
    ensureScratch(static_cast<int>(want) + 64);

    if (active_) {
        cfg_.sample_rate = static_cast<double>(setup.sampleRate);
        engineReady_ = engine_.prepare(cfg_, channels_);
        if (engineReady_) applyFullSurface();
        if (!engineReady_) return Steinberg::kResultFalse;
    }
    return Steinberg::kResultOk;
}

tresult PLUGIN_API OctavoxNativeProcessor::setActive(TBool state) {
    if (state) {
        if (processSetup.sampleRate <= 0) return Steinberg::kResultFalse;
        cfg_.sample_rate = static_cast<double>(processSetup.sampleRate);
        // prepare() with matching buffer-determining fields reconfigures in
        // place (zero allocation -- the R13-c fast path); a changed rate /
        // channel count takes the allocate-and-rebuild path (here, not on
        // the audio thread).
        engineReady_ = engine_.prepare(cfg_, channels_);
        if (!engineReady_) return Steinberg::kResultFalse;
        applyFullSurface();
        active_ = true;
    } else {
        active_ = false;  // suspend: engine state is kept; reset = the
                          // setActive(false)->(true) cycle (documented)
    }
    return Steinberg::kResultOk;
}

tresult PLUGIN_API OctavoxNativeProcessor::setProcessing(TBool /*state*/) {
    // Pure suspend/resume notification: the engine state keeps advancing by
    // design (the core's state transparency contract); no action needed.
    return Steinberg::kResultOk;
}

uint32 PLUGIN_API OctavoxNativeProcessor::getLatencySamples() {
    return engineReady_ ? static_cast<uint32>(engine_.latencySamples()) : 0u;
}

uint32 PLUGIN_API OctavoxNativeProcessor::getTailSamples() {
    if (!engineReady_) return kNoTail;
    // Safe upper bound on the wet-path delay extent: the core's ring-range
    // law is  base + 32*(max_voices-1) + 3*grain + 8 < 2^18  samples; the
    // tail adds the intrinsic latency (the dry path is latency aligned, so
    // it ends with the wet). Deliberately conservative (a host hint, not a
    // DSP-critical value); see VST3_WRAPPER.md.
    const double extent = cfg_.base_delay + 32.0 * (cfg_.max_voices - 1) +
                          3.0 * cfg_.grain + 8.0 + engine_.latencySamples();
    return static_cast<uint32>(extent);
}

//==========================================================================
// Parameter application (audio thread; wait-free, allocation-free)
//==========================================================================

double OctavoxNativeProcessor::globalRatio() const {
    return std::pow(2.0, surface_.pitchSemi / 12.0);
}

void OctavoxNativeProcessor::retargetRatios() {
    // setRatio()/setSpread() clear the per-voice overrides (core contract);
    // re-apply every non-zero offset on top of the fresh global target.
    engine_.setRatio(globalRatio());
    for (int v = 0; v < kMaxVoices; ++v) {
        if (surface_.offset[v] != 0.0)
            engine_.setVoiceRatio(v, globalRatio() * std::pow(2.0, surface_.offset[v] / 12.0));
    }
}

void OctavoxNativeProcessor::applyVoiceRatio(int v) {
    if (surface_.offset[v] != 0.0) {
        engine_.setVoiceRatio(v, globalRatio() * std::pow(2.0, surface_.offset[v] / 12.0));
    } else {
        // Clearing one override requires the setRatio()+re-apply dance (the
        // core has no per-voice clear API).
        retargetRatios();
    }
}

void OctavoxNativeProcessor::applyParamToEngine(uint32_t id, double plain) {
    if (!std::isfinite(plain)) return;
    switch (id) {
    case ParamId::kBypass:
        surface_.bypass = plain;
        engine_.setBypass(plain >= 0.5);
        return;
    case ParamId::kPitch:
        surface_.pitchSemi = plain;
        retargetRatios();
        return;
    case ParamId::kVoices: {
        surface_.voices = plain;
        const int n = static_cast<int>(std::lround(plain));
        engine_.setVoices(n >= 1 && n <= kMaxVoices ? n : 1);
        return;
    }
    case ParamId::kSpread:
        surface_.spread = plain;
        engine_.setSpread(plain >= 0.5);
        retargetRatios();
        return;
    case ParamId::kFeedback:
        surface_.feedback = plain;
        engine_.setFeedback(plain);
        return;
    case ParamId::kWetDry:
        surface_.wetDry = plain;
        engine_.setWetDry(plain);
        return;
    default: break;
    }
    const uint32_t rel = id - static_cast<uint32_t>(ParamId::kVoiceOffsetBase);
    if (id < static_cast<uint32_t>(ParamId::kVoiceOffsetBase) || rel >= 4u * kMaxVoices) return;
    const int v = static_cast<int>(rel / 4u);
    switch (rel % 4u) {
    case 0:
        surface_.offset[v] = plain;
        applyVoiceRatio(v);
        break;
    case 1:
        surface_.gain[v] = plain;
        engine_.setVoiceGain(v, plain);
        break;
    case 2:
        surface_.mute[v] = plain;
        engine_.setVoiceMute(v, plain >= 0.5);
        break;
    case 3:
        surface_.baseMs[v] = plain;
        engine_.setVoiceBaseDelay(v, msToBaseDelaySamples(plain, sampleRate(), cfg_.grain));
        break;
    default: break;
    }
}

void OctavoxNativeProcessor::applyFullSurface() {
    // Canonical order (mirrored by the wrapper test's hand-driven
    // reference): spread -> ratio -> per-voice overrides -> gains -> mutes
    // -> delays -> voices -> feedback -> wet/dry -> bypass.
    engine_.setSpread(surface_.spread >= 0.5);
    retargetRatios();
    for (int v = 0; v < kMaxVoices; ++v)
        engine_.setVoiceGain(v, surface_.gain[v]);
    for (int v = 0; v < kMaxVoices; ++v)
        engine_.setVoiceMute(v, surface_.mute[v] >= 0.5);
    for (int v = 0; v < kMaxVoices; ++v)
        engine_.setVoiceBaseDelay(
            v, msToBaseDelaySamples(surface_.baseMs[v], sampleRate(), cfg_.grain));
    {
        const int n = static_cast<int>(std::lround(surface_.voices));
        engine_.setVoices(n >= 1 && n <= kMaxVoices ? n : 1);
    }
    engine_.setFeedback(surface_.feedback);
    engine_.setWetDry(surface_.wetDry);
    engine_.setBypass(surface_.bypass >= 0.5);
}

//==========================================================================
// process(): sample-accurate segmentation + aliasing-safe chunking
//==========================================================================

void OctavoxNativeProcessor::ensureScratch(int n) {
    if (n > scratchCap_) {
        for (int c = 0; c < 2; ++c) scratch_[c].assign(static_cast<size_t>(n), 0.0f);
        scratchCap_ = n;
    }
}

void OctavoxNativeProcessor::processChunk(const float* const* in, float* const* out,
                                          int pos, int end) {
    const int len = end - pos;
    if (len <= 0 || channels_ <= 0) return;
    bool alias = false;
    for (int c = 0; c < channels_; ++c)
        if (in[c] == out[c]) { alias = true; break; }
    if (!alias) {
        const float* src[2] = {in[0] + pos, channels_ > 1 ? in[1] + pos : nullptr};
        float* dst[2] = {out[0] + pos, channels_ > 1 ? out[1] + pos : nullptr};
        engine_.process(src, dst, channels_, len);
    } else {
        // Copy this chunk's input into scratch first (the core requires
        // non-aliasing in/out); scratch is chunk-sized, never grown here.
        int p = pos;
        while (p < end) {
            const int seg = std::min(end - p, scratchCap_);
            for (int c = 0; c < channels_; ++c)
                std::memcpy(scratch_[c].data(), in[c] + p, sizeof(float) * seg);
            const float* src[2] = {scratch_[0].data(),
                                   channels_ > 1 ? scratch_[1].data() : nullptr};
            float* dst[2] = {out[0] + p, channels_ > 1 ? out[1] + p : nullptr};
            engine_.process(src, dst, channels_, seg);
            p += seg;
        }
    }
}

tresult PLUGIN_API OctavoxNativeProcessor::process(ProcessData& data) {
    if (!active_ || !engineReady_) {
        // Not prepared: honest zero output (never garbage), silence flags
        // cleared -- hosts only reach here in exceptional setups.
        if (data.outputs && data.numOutputs > 0 && data.outputs[0].channelBuffers32) {
            for (int32 c = 0; c < data.outputs[0].numChannels; ++c)
                std::memset(data.outputs[0].channelBuffers32[c], 0,
                            sizeof(float) * size_t(data.numSamples > 0 ? data.numSamples : 0));
            data.outputs[0].silenceFlags = 0;
        }
        return Steinberg::kResultOk;
    }
    if (data.symbolicSampleSize != SymbolicSampleSizes::kSample32)
        return Steinberg::kResultFalse;

    const int n = data.numSamples;
    const int ch = channels_;
    const float* inArr[2] = {nullptr, nullptr};
    float* outArr[2] = {nullptr, nullptr};
    if (n > 0) {
        if (!data.inputs || data.numInputs < 1 || !data.inputs[0].channelBuffers32 ||
            !data.outputs || data.numOutputs < 1 || !data.outputs[0].channelBuffers32)
            return Steinberg::kResultFalse;
        if (data.inputs[0].numChannels < ch || data.outputs[0].numChannels < ch)
            return Steinberg::kResultFalse;
        for (int c = 0; c < ch; ++c) {
            inArr[c] = data.inputs[0].channelBuffers32[c];
            outArr[c] = data.outputs[0].channelBuffers32[c];
        }
    }

    // ---- collect the parameter queues (host-owned, lock-free) ------------
    struct QueueCursor {
        IParamValueQueue* q;
        int32 idx;
    };
    QueueCursor curs[kMaxQueues];
    int nCurs = 0;
    IParameterChanges* changes = data.inputParameterChanges;
    if (changes) {
        const int32 qc = changes->getParameterCount();
        for (int32 i = 0; i < qc && nCurs < kMaxQueues; ++i) {
            IParamValueQueue* q = changes->getParameterData(i);
            if (q && q->getPointCount() > 0) curs[nCurs++] = {q, 0};
        }
    }

    // applyEventsAt(o): consume+apply every point with offset <= o. Points
    // arrive sorted per queue; late/stale points are applied too (defensive,
    // value semantics identical).
    auto applyEventsAt = [&](int32 o) {
        for (int i = 0; i < nCurs; ++i) {
            IParamValueQueue* q = curs[i].q;
            const ParamID id = q->getParameterId();
            int32 off = 0;
            ParamValue val = 0;
            while (curs[i].idx < q->getPointCount()) {
                if (q->getPoint(curs[i].idx, off, val) != Steinberg::kResultTrue) break;
                if (off > o) break;
                applyParamToEngine(id, plainFromNormalized(id, val));
                ++curs[i].idx;
            }
        }
    };
    // nextBoundary(): the smallest unconsumed point offset (all <= pos were
    // consumed by applyEventsAt(pos)); INT32_MAX when none remain.
    auto nextBoundary = [&]() -> int32 {
        int32 m = INT32_MAX;
        for (int i = 0; i < nCurs; ++i) {
            IParamValueQueue* q = curs[i].q;
            if (curs[i].idx >= q->getPointCount()) continue;
            int32 off = 0;
            ParamValue val = 0;
            if (q->getPoint(curs[i].idx, off, val) == Steinberg::kResultTrue && off < m) m = off;
        }
        return m;
    };

    // ---- segmentation loop: apply at boundaries, process in between ------
    // Invariant: applyEventsAt(pos) has consumed every point with offset <=
    // pos, so nextBoundary() always returns an offset > pos: every iteration
    // processes at least one sample (or applies the block-end events and
    // exits). Bit-exact equivalence with the core's block-boundary contract
    // is gated by the wrapper test segmentation_equivalence_BITEXACT.
    applyEventsAt(0);
    int pos = 0;
    while (pos < n) {
        const int32 b = nextBoundary();
        const int segEnd = (b == INT32_MAX || b > n) ? n : b;
        if (segEnd > pos) processChunk(inArr, outArr, pos, segEnd);
        pos = segEnd;
        if (b != INT32_MAX && b <= n) applyEventsAt(b);
    }
    // Defensive drain: a misbehaving host may deliver points with offsets
    // beyond numSamples; apply them now (they take effect for the next
    // block -- same semantics a block-boundary implementation would give).
    applyEventsAt(INT32_MAX);

    // We cannot guarantee silence at the output (delay/feedback tails):
    // clear the host's silence hint honestly.
    if (data.outputs && data.numOutputs > 0) data.outputs[0].silenceFlags = 0;
    return Steinberg::kResultOk;
}

//==========================================================================
// State (IComponent::get/setState = the authoritative full-state blob)
//==========================================================================

tresult PLUGIN_API OctavoxNativeProcessor::getState(IBStream* state) {
    if (!state) return Steinberg::kInvalidArgument;
    StateData st;
    st.cfg = cfg_;
    st.cfg.sample_rate = sampleRate();
    st.channels = channels_;
    st.surface = surface_;
    return writeStateData(state, st) ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

tresult PLUGIN_API OctavoxNativeProcessor::setState(IBStream* state) {
    if (!state) return Steinberg::kInvalidArgument;
    StateData st;
    if (!readStateData(state, st)) return Steinberg::kResultFalse;

    cfg_ = st.cfg;
    // R18-WDB (variant authoritativeness): the compile-time interpolation
    // selection of THIS module build is authoritative on the state-restore
    // path. A state blob saved by the OTHER variant (or by an R16-R18
    // "OctavoxNative" module) still restores every other field verbatim,
    // but the interpolation kernel stays this build's selection -- the
    // variant's DSP difference is a compile-time property, never a
    // host-visible or state-switchable parameter. The blob's own
    // interpolation field (same variant: identical value) round-trips
    // bit-unchanged.
    cfg_.interpolation = build::kDefaultInterpolation;
    // R19 (detector authoritativeness): exactly the same law for the
    // detector architecture -- the module's compile-time selection (the
    // decimated control-rate detector) is authoritative; a v1 blob (which
    // carries no detector field) restores with the module's selection, and
    // a v2 blob saved by a future module with a different default still
    // restores every other field verbatim.
    cfg_.detector_mode = build::kDefaultDetectorMode;
    // The engine is prepared at the HOST's current rate (a state saved at
    // another rate re-prepares here; the time-domain parameters -- grain,
    // base delay in the Config are sample-domain fields -- are restored
    // verbatim per the core's Config semantics).
    cfg_.sample_rate = sampleRate();
    surface_ = st.surface;

    // Adapt the bus arrangement to the saved channel count (a fresh
    // instance defaults to stereo; a restored mono session must come back
    // mono, or the engine would be prepared for the wrong channel count).
    // Only while NOT active (the host pauses processing during state
    // exchange -- documented VST3 contract).
    if (!active_ && (st.channels == 1 || st.channels == 2) && st.channels != channels_) {
        const SpeakerArrangement arr =
            (st.channels == 1) ? SpeakerArr::kMono : SpeakerArr::kStereo;
        if (auto* inBus = Steinberg::FCast<AudioBus>(audioInputs.at(0)))
            inBus->setArrangement(arr);
        if (auto* outBus = Steinberg::FCast<AudioBus>(audioOutputs.at(0)))
            outBus->setArrangement(arr);
        channels_ = st.channels;
    }

    // Sync the host-visible parameter objects (the surface is authoritative
    // now; silent updates -- no performEdit notifications during restore).
    {
        double vals[kNumParams];
        surfaceToPlainArray(surface_, vals);
        for (uint32_t id = 0; id < static_cast<uint32_t>(kNumParams); ++id)
            setParamNormalized(id, normalizedFromPlain(id, vals[id]));
    }

    if (active_) {
        engineReady_ = engine_.prepare(cfg_, channels_);
        if (engineReady_) applyFullSurface();
    }
    return Steinberg::kResultOk;
}

tresult PLUGIN_API OctavoxNativeProcessor::setEditorState(IBStream* state) {
    // Alias of the component state for hosts that use the controller path
    // on a single-component effect (idempotent with setState).
    return setState(state);
}

tresult PLUGIN_API OctavoxNativeProcessor::getEditorState(IBStream* state) {
    return getState(state);
}

}  // namespace vst3wrap
}  // namespace octavox
