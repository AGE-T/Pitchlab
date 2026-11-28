// ---------------------------------------------------------------------------
// OCTAVOX native core -- VST3 wrapper: parameter surface definitions (R16-P4b)
// ============================================================================
// PROVENANCE: this wrapper is OUR OWN independent harmonizer implementation
// built around the octavox native DSP core (itself our own design; see
// native/include/octavox/dsp.hpp). It is NOT a reproduction of the original
// Eventide Octavox plug-in; no target code is contained here and the original
// binary was never executed. The VST3 SDK (v3.7.12_build_20, Steinberg dual
// license) is used as-is and is NOT redistributed with this evidence package.
//
// This header defines:
//   * the 39-parameter surface (ids, titles, domains, defaults),
//   * the exact normalized<->plain conversions (reversible, step-quantized),
//   * the live Surface model (the audio-thread mirror of all 39 plain values,
//     in a fixed serialization order = parameter id order),
//   * the versioned binary state blob layout ('OVR1', version 1),
//   * the ms->samples conversion for per-voice base delays incl. the clamping
//     into the core's ring-range validity constraint.
//
// The core defaults (Config in octavox/dsp.hpp) are kept for EVERY parameter:
//   voices default 1, spread default true, feedback default 0, wet/dry
//   default 1.0 (the bit-neutral wet path), per-voice gain 1 / mute off /
//   pitch offset 0 / base delay = the core's per-voice default (256+32*v
//   samples) expressed in ms at the 48 kHz nominal rate.
// ---------------------------------------------------------------------------

#pragma once

// The SingleComponentEffect include MUST come first in any translation
// unit that also includes vstparameters.h/vsteditcontroller.h: it renames
// EditController's setState/getState to setEditorState/getEditorState
// while including them, and the rename must be active before their include
// guards are first hit.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "octavox_build_config.h"
#include "octavox/dsp.hpp"

#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <cstdint>

namespace Steinberg {
class IBStream;
namespace Vst {
class EditControllerEx1;
}
}

namespace octavox {
namespace vst3wrap {

//--------------------------------------------------------------------------
// Sizes / constants
//--------------------------------------------------------------------------

/// The core's default max_voices (Config::max_voices == 8).
constexpr int kMaxVoices = 8;
/// Total exposed parameters: 6 global + 4 per voice * 8 voices + 1 (R34).
constexpr int kNumParams = 39;
/// Fixed queue-table capacity in process(); a host creates at most one queue
/// per parameter id (39 here), so 64 can never bind (documented).
constexpr int kMaxQueues = 64;

//--------------------------------------------------------------------------
// Parameter ids (VST3 ParamID = index into the fixed serialization order)
//--------------------------------------------------------------------------

enum ParamId : uint32_t {
    kBypass = 0,   ///< bool (stepCount 1), flags kCanAutomate|kIsBypass
    kPitch = 1,    ///< semitones, [-24,+24], step 0.01 (stepCount 4800)
    kVoices = 2,   ///< integer 1..8 (stepCount 7)
    kSpread = 3,   ///< bool (core's musical spread table on/off)
    kFeedback = 4, ///< [0, 0.9] (subset of the core's clamped (-1,1) domain)
    kWetDry = 5,   ///< [0,1], 1.0 = the core's bit-neutral all-wet default

    kVoiceOffsetBase = 6, ///< + 4*v: voice v pitch offset, semitones
    kVoiceGainBase = 7,   ///< + 4*v: voice v gain, [0,2]
    kVoiceMuteBase = 8,   ///< + 4*v: voice v mute, bool
    kVoiceDelayBase = 9,  ///< + 4*v: voice v base delay, ms [0,200]

    /// R34: the chain-end stereo image width [0,1], default 1.0 = the
    /// original image (BIT-NEUTRAL: the core's width stage is inert),
    /// 0.0 = full mono collapse, with correlation-adaptive energy
    /// preservation (see include/octavox/stereo_width.hpp + the R34
    /// research record). APPENDED after the per-voice block: all
    /// pre-R34 ids (0..37) are unchanged (DAW automation compatibility).
    /// Distinct from the Harmony Map (id 3), which is the musical
    /// per-voice interval switch, NOT stereo widening.
    kStereoWidth = 38
};

constexpr uint32_t voiceOffsetId(int v) { return kVoiceOffsetBase + 4 * v; }
constexpr uint32_t voiceGainId(int v) { return kVoiceGainBase + 4 * v; }
constexpr uint32_t voiceMuteId(int v) { return kVoiceMuteBase + 4 * v; }
constexpr uint32_t voiceDelayId(int v) { return kVoiceDelayBase + 4 * v; }

/// Unit ids (units registered by the processor; root unit is 0).
enum UnitIds : Steinberg::Vst::UnitID {
    kUnitGlobal = 1,  ///< "Global"
    kUnitVoiceBase = 10 ///< + v: "Voice v+1"
};

//--------------------------------------------------------------------------
// Domains + exact conversions
//--------------------------------------------------------------------------

struct ParamDomain {
    double minPlain;
    double maxPlain;
    double stepPlain; ///< 0 = continuous
};

/// Domain of a parameter (hardcoded table matching the definitions below).
ParamDomain domainOf(uint32_t id);

/// Normalized [0,1] -> plain (step-quantized, clamped). Exact and reversible
/// for step-aligned values (see VST3_WRAPPER.md, "Parameter conversions").
double plainFromNormalized(uint32_t id, double norm);

/// Plain -> normalized [0,1] (step-aligned). get/setParamNormalized round-trip.
double normalizedFromPlain(uint32_t id, double plain);

/// The maximum per-voice base delay in SAMPLES accepted by the core's
/// setVoiceBaseDelay: d + 3*grain + 8 < 2^18 (strict) -- keep one sample of
/// margin below the strict bound.
inline double maxBaseDelaySamples(double grain) {
    return 262144.0 - 3.0 * grain - 8.0 - 1.0;
}

/// ms -> per-voice base delay in samples at the given sample rate, clamped
/// into [1, maxBaseDelaySamples(grain)]:
///   * the core requires delay > 0 STRICTLY (0 is a rejected no-op), so the
///     lower clamp is 1 sample (a 0 ms parameter maps to the core's minimum);
///   * the upper clamp implements the ring-range law above. With the fixed
///     grain of 4096 (default) the cap is 249847 samples, which is above the
///     200 ms parameter maximum at every sample rate the core accepts
///     (<= 1e6 Hz => <= 200000 samples), so the clamp is a pure safety net.
double msToBaseDelaySamples(double ms, double sampleRate, double grain);

//--------------------------------------------------------------------------
// The live parameter surface (39 plain values; the audio-thread mirror)
//--------------------------------------------------------------------------

struct Surface {
    // global (param ids 0..5)
    double bypass;   ///< 0/1
    double pitchSemi;///< semitones
    double voices;   ///< 1..8
    double spread;   ///< 0/1
    double feedback; ///< [0,0.9]
    double wetDry;   ///< [0,1]

    // per voice (param ids 6+4v .. 9+4v)
    double offset[kMaxVoices]; ///< semitones
    double gain[kMaxVoices];   ///< linear amplitude
    double mute[kMaxVoices];   ///< 0/1
    double baseMs[kMaxVoices]; ///< milliseconds

    // R34 (param id 38)
    double stereoWidth;        ///< [0,1], default 1.0 (bit-neutral)
};

/// The default surface: every value is the core's own default (see header
/// comment). baseMs default = (256 + 32*v) samples at 48000 Hz expressed in
/// ms (the core's per-voice default delay table).
Surface defaultSurface();

/// Fixed-order plain array (slot i = plain value of parameter id i).
void surfaceToPlainArray(const Surface& s, double out[kNumParams]);
void plainArrayToSurface(const double in[kNumParams], Surface& s);

/// The engine configuration the wrapper uses: the pure core defaults
/// (Config{} -- see octavox/dsp.hpp) with the WRAPPER's compile-time
/// interpolation selection substituted (octavox_build_config.h -- the
/// generic DSP core default stays Interpolation::Lagrange6, UNCHANGED;
/// only this wrapper configuration layer selects the variant kernel),
/// with sampleRate substituted at prepare time. All timbre/buffer fields
/// stay at the core defaults unless a restored state blob carries others.
inline Config defaultConfig() { return build::referenceConfig(); }

//--------------------------------------------------------------------------
// Versioned binary state blob ('OVR1' version 1) -- exact layout in
/// VST3_WRAPPER.md. All multi-byte fields are little-endian (IBStreamer).
//--------------------------------------------------------------------------

struct StateData {
    Config cfg;      ///< full engine config (cfg.sample_rate = the rate the
                     ///< engine is to be prepared at)
    int32_t channels;///< informational: bus channel count at save time
    Surface surface; ///< the 39 plain values
};

/// Write magic + version + Config(21 fields) + channels + 39 plain doubles
/// (v3; see the version history at the layout constants).
/// Returns false on stream errors.
bool writeStateData(Steinberg::IBStream* stream, const StateData& st);

/// Read + validate. Returns false on short/corrupt input, wrong magic or
/// unsupported version. Config/surface values are clamped into their valid
/// domains defensively. cfg.sample_rate is used as-is by the caller (the
/// processor substitutes the host's current rate).
bool readStateData(Steinberg::IBStream* stream, StateData& out);

//--------------------------------------------------------------------------
// The VST3 Parameter object with our exact conversions and plain display
//--------------------------------------------------------------------------

class OctavoxParameter : public Steinberg::Vst::Parameter {
public:
    OctavoxParameter(uint32_t id, const Steinberg::Vst::TChar* title,
                     const Steinberg::Vst::TChar* shortTitle,
                     const Steinberg::Vst::TChar* units, double minPlain,
                     double maxPlain, double stepPlain, double defaultPlain,
                     Steinberg::int32 flags, Steinberg::Vst::UnitID unitId);

    Steinberg::Vst::ParamValue PLUGIN_API toPlain(
        Steinberg::Vst::ParamValue valueNormalized) const SMTG_OVERRIDE;
    Steinberg::Vst::ParamValue PLUGIN_API toNormalized(
        Steinberg::Vst::ParamValue plainValue) const SMTG_OVERRIDE;
    void PLUGIN_API toString(Steinberg::Vst::ParamValue valueNormalized,
                             Steinberg::Vst::String128 string) const SMTG_OVERRIDE;
    bool PLUGIN_API fromString(const Steinberg::Vst::TChar* string,
                               Steinberg::Vst::ParamValue& valueNormalized) const SMTG_OVERRIDE;

private:
    double minPlain_;
    double maxPlain_;
    double stepPlain_;
};

//--------------------------------------------------------------------------
// Full descriptive metadata for one parameter (used to build the container).
//--------------------------------------------------------------------------

struct ParamSpec {
    uint32_t id;
    const char* title;      ///< ASCII (converted to UTF-16)
    const char* shortTitle; ///< ASCII
    const char* units;      ///< ASCII
    ParamDomain domain;
    double defaultPlain;
    Steinberg::int32 flags;       ///< ParameterInfo::ParameterFlags
    Steinberg::Vst::UnitID unitId;
};

/// The 39 parameter specifications, indexed by parameter id (0..38).
const ParamSpec& paramSpec(uint32_t id);

/// R21-P0: static per-parameter help text (one ASCII line per id; explains
/// the pitch composition law ratio_v = global*voice and the Harmony Map
/// semantics). Consumed by the validation kit's parameter dump, the wrapper
/// documentation and future GUI tooltip layers. Never null.
const char* paramHelp(uint32_t id);

/// Register the 39 parameter objects on a parameter container (call from
/// the processor's initialize(), which has access to the protected member).
void registerParameters(Steinberg::Vst::ParameterContainer& parameters);

/// Register the Global + per-voice units on the controller.
void registerUnits(Steinberg::Vst::EditControllerEx1& controller);

}  // namespace vst3wrap
}  // namespace octavox
