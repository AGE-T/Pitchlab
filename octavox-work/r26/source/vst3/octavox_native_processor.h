// ---------------------------------------------------------------------------
// OCTAVOX native core -- VST3 wrapper: the single-component effect (R16-P4b)
// ============================================================================
// PROVENANCE: this wrapper is OUR OWN independent harmonizer implementation
// built around the octavox native DSP core (our own design; see
// native/include/octavox/dsp.hpp). It is NOT a reproduction of the original
// Eventide Octavox plug-in; no target code is contained here and the original
// binary was never executed. The VST3 SDK (v3.7.12_build_20, Steinberg dual
// license) is used as-is and is NOT redistributed with this evidence package.
//
// DESIGN (see VST3_WRAPPER.md for the full note):
//   * SingleComponentEffect base: one object = IComponent + IAudioProcessor
//     + IEditController -- the SDK's supported minimal lifecycle for a
//     non-distributable effect; proper initialize/terminate, bus lists and
//     the host-managed parameter container come from the base.
//   * Buses: symmetric Main In/Out only, mono (1) or stereo (2) channels;
//     the DSP is a MultichannelShifter (one AlignedDelayLine engine per
//     channel = structural channel isolation).
//   * 38 parameters (6 global + 4 per voice x 8) defined in octavox_params.h;
//     all host-automatable; the audio thread reads them EXCLUSIVELY through
//     the host's IParameterChanges queues (lock-free by design -- no shared
//     mutable state with the UI thread except the host-managed parameter
//     objects themselves).
//   * Sample-accurate automation: process() splits the host block into
//     segments at the parameter-event sample offsets and applies the events
//     at each segment start (the core's tested BLOCK-BOUNDARY semantics at
//     finer granularity). Bit-exact equivalence with the core's tested
//     block-boundary implementation is gated by the wrapper test suite
//     (octavox_vst3_tests.cpp, case segmentation_equivalence_BITEXACT).
//   * State: a versioned binary blob ('OVR1' v1) = full Config + the live
//     38-value surface (octavox_params.cpp writeStateData/readStateData).
//     Restore = prepare(Config) + re-apply the surface = the documented
//     deterministic-from-stream-position-0 contract. IComponent::setState /
//     getState are the authoritative paths; the EditController-state methods
//     are aliased to the same blob for hosts that use them.
//   * Latency: the engine's latencySamples() (the dry path is latency
//     aligned). Tail: a config-derived safe upper bound (the ring-range
//     law), documented in VST3_WRAPPER.md.
//   * In-place safety: hosts may pass in == out channel buffers; the core
//     requires non-aliasing, so aliased blocks are copied through
//     preallocated per-channel scratch (chunked, never grown, in process()).
//   * NO allocation and NO blocking locks in process(); everything is
//     preallocated in setActive()/setupProcessing (gated by the wrapper
//     test no_alloc_steady_state with the project's global-new counter).
// ---------------------------------------------------------------------------
#pragma once

#include "octavox_params.h"

#include "octavox/dsp.hpp"
#include "octavox/telemetry.hpp"

#include "octavox_telemetry_interface.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
// (public.sdk/source/vst/vstsinglecomponenteffect.h is included FIRST by
// octavox_params.h -- the setState/getState rename ordering)

#include <vector>

namespace octavox {
namespace vst3wrap {

class OctavoxNativeProcessor : public Steinberg::Vst::SingleComponentEffect,
                               public IPitchTelemetry {
public:
    OctavoxNativeProcessor();
    ~OctavoxNativeProcessor() override;

    //--- R31: vendor queryInterface (the R29 vendor-interface pattern) -----
    // The m02-style telemetry is served through IPitchTelemetry (a custom
    // IID answered HERE, before the base chain), keeping the 38-parameter
    // automation surface and the state blob byte-frozen.
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                                 void** obj) SMTG_OVERRIDE;

    //--- from IPitchTelemetry (R31; see octavox_telemetry_interface.h) -----
    Steinberg::tresult PLUGIN_API getVoiceFrame(Steinberg::int32 voice,
                                                double* frame14) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getCellId(Steinberg::int32 cell,
                                            Steinberg::int32* idOut) SMTG_OVERRIDE;
    Steinberg::uint64 PLUGIN_API consumeDirtyBits() SMTG_OVERRIDE;
    Steinberg::uint64 PLUGIN_API getDirtyBits() SMTG_OVERRIDE;

    // The SDK's documented pattern for an FObject-based class that adds an
    // extra FUnknown-derived interface (fobject.h): declare the refcount
    // in the most-derived class so it overrides BOTH FUnknown subobjects
    // (the SingleComponentEffect chain and the IPitchTelemetry branch).
    REFCOUNT_METHODS(SingleComponentEffect)

    //--- from IPluginBase ---------------------------------------------------
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;

    //--- from IComponent ----------------------------------------------------
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    // EditController-state aliases (some hosts call these on the combined
    // component; they carry the SAME full state blob -- idempotent).
    Steinberg::tresult PLUGIN_API setEditorState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getEditorState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    // R20-P0: IEditController::setComponentState -- the base returns
    // kNotImplemented; aliased to the authoritative setState so hosts that
    // drive the controller path get the parameter-value sync (idempotent).
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;

    //--- from IAudioProcessor ----------------------------------------------
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                                     Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs,
                                                     Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE;

    //--- factory ------------------------------------------------------------
    static Steinberg::FUnknown* createInstance(void* /*context*/) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new OctavoxNativeProcessor());
    }

    OBJ_METHODS(OctavoxNativeProcessor, Steinberg::Vst::SingleComponentEffect)

    // Exposed for the in-process wrapper test (read-only diagnostics).
    const Config& engineConfig() const { return cfg_; }
    const Surface& liveSurface() const { return surface_; }
    bool engineReady() const { return engineReady_; }

private:
    // -- engine + live state (see the threading notes in VST3_WRAPPER.md) ---
    MultichannelShifter engine_;
    Config cfg_{};            // the engine configuration (core defaults until
                              // a state restore carries others)
    Surface surface_{};       // the live 38-value mirror; written ONLY from
                              // process() (audio thread; authoritative) and
                              // from setState (host guarantees processing is
                              // paused -- documented reliance)
    int channels_ = 2;        // negotiated bus channel count (1 or 2)
    bool engineReady_ = false;
    bool active_ = false;

    // -- R31: m02-style telemetry (poller + dirty bits; see telemetry.hpp) --
    TelemetryPoller telemetry_;
    /// The latency value last handed to the host via
    /// restartComponent(kLatencyChanged) (0 = none yet). Main thread only.
    uint32 lastLatencyReported_ = 0;
    /// Notify the host when the reported latency changed (the R30 U-2
    /// principle: the latency must be live-updated whenever a prepare or a
    /// configuration change alters it). MAIN THREAD ONLY (setupProcessing /
    /// setActive / setState paths -- per the VST3 spec restartComponent is
    /// a main-thread call).
    void notifyLatencyIfChanged();

    // -- preallocated scratch for in-place (aliased) host buffers -----------
    std::vector<float> scratch_[2];
    int scratchCap_ = 0;      // samples per channel currently allocated

    // -- helpers (audio thread; no allocation, no locks) --------------------
    double sampleRate() const { return processSetup.sampleRate > 0
                                        ? static_cast<double>(processSetup.sampleRate)
                                        : cfg_.sample_rate; }
    double globalRatio() const;
    /// setSpread/setRatio clear the per-voice overrides (core contract), so
    /// the wrapper re-applies every non-zero voice offset after them.
    void retargetRatios();
    void applyVoiceRatio(int v);
    /// Apply ONE parameter (plain value) to the surface mirror + the engine.
    void applyParamToEngine(uint32_t id, double plain);
    /// Apply the whole surface to a freshly prepared engine (canonical order
    /// -- mirrored by the wrapper test's hand-driven reference).
    void applyFullSurface();
    /// Ensure the in-place scratch covers `n` samples; grows only outside
    /// process() (setActive/setupProcessing); process() chunks instead.
    void ensureScratch(int n);
    /// One core process() call for [pos, end) with aliasing protection.
    void processChunk(const float* const* in, float* const* out, int pos, int end);
};

}  // namespace vst3wrap
}  // namespace octavox
