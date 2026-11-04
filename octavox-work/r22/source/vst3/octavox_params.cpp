// ---------------------------------------------------------------------------
// OCTAVOX native core -- VST3 wrapper: parameter surface implementation
// (R16-P4b)
// ============================================================================
// PROVENANCE: our own independent implementation (see octavox_params.h).
// ---------------------------------------------------------------------------

// must always come first (the setState/getState rename workaround for
// vsteditcontroller.h inside a SingleComponentEffect build)
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "octavox_params.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace octavox {
namespace vst3wrap {

using Steinberg::int32;
using Steinberg::uint32;
using Steinberg::Vst::ParameterInfo;
using Steinberg::Vst::kRootUnitId;

//--------------------------------------------------------------------------
// Parameter specifications (index == parameter id). Fully static (no dynamic
// strings): the table is consulted from the audio thread via domainOf() and
// must never allocate or need lazy initialization.
//--------------------------------------------------------------------------

namespace {

constexpr ParamDomain kDomSemi{-24.0, 24.0, 0.01};
constexpr ParamDomain kDomBool{0.0, 1.0, 1.0};
constexpr ParamDomain kDomVoices{1.0, 8.0, 1.0};
constexpr ParamDomain kDomFeedback{0.0, 0.9, 0.0};
constexpr ParamDomain kDomWetDry{0.0, 1.0, 0.0};
constexpr ParamDomain kDomGain{0.0, 2.0, 0.0};
constexpr ParamDomain kDomMs{0.0, 200.0, 0.0};

// The core's per-voice default base delay (256 + 32*v samples) expressed in
// ms at the 48 kHz nominal rate. At 48000 Hz the ms->samples conversion of
// these defaults reproduces the core's per-voice default table (exactly for
// voices 1,3,5,7; within one double ulp for the others -- measured by the
// wrapper test; see VST3_WRAPPER.md "Base delay parameter semantics").
constexpr double kDefaultBaseMs[kMaxVoices] = {
    (256.0 + 32.0 * 0) / 48.0, (256.0 + 32.0 * 1) / 48.0, (256.0 + 32.0 * 2) / 48.0,
    (256.0 + 32.0 * 3) / 48.0, (256.0 + 32.0 * 4) / 48.0, (256.0 + 32.0 * 5) / 48.0,
    (256.0 + 32.0 * 6) / 48.0, (256.0 + 32.0 * 7) / 48.0};

constexpr int32 kAutomate = ParameterInfo::kCanAutomate;

const ParamSpec kSpecs[kNumParams] = {
    // clang-format off
    // -- global (ids 0..5) -------------------------------------------------
    // R21-P0 (parameter semantics): the host-visible TITLES were corrected
    // to state each control's role in the pitch composition law
    //     ratio_v = global_ratio * voice_ratio_v
    // (see docs/R20_PARAMETER_SEMANTICS.md + the R21 update): "Global Pitch
    // Shift" (id 1) transposes EVERY voice; "Voice N Transpose" (id 6+4v)
    // displaces ONLY voice N (and, when the Harmony Map is on and the
    // offset is 0, the voice instead follows the musical spread table).
    // The former "Spread" title (id 3) was RENAMED to "Harmony Map": it is
    // a musical harmony-map switch (the per-voice spread-table intervals),
    // NOT stereo widening. NOTHING else changed: ids, normalized/plain
    // ranges, step counts, defaults, flags, units, the state-blob layout
    // and the exact DSP semantics are byte-identical (title-only change;
    // automation compatibility and state compatibility are unaffected --
    // VST3 hosts persist parameter IDS, not titles).
    {kBypass,     "Bypass",             "Byp",       "",          kDomBool,    0.0, kAutomate | ParameterInfo::kIsBypass, Steinberg::Vst::kRootUnitId},
    {kPitch,      "Global Pitch Shift", "Glob Pitch", "semitones", kDomSemi,   0.0, kAutomate, kUnitGlobal},
    {kVoices,     "Voices",             "Voices",    "",           kDomVoices,  1.0, kAutomate, kUnitGlobal},
    {kSpread,     "Harmony Map",        "HarmMap",   "",           kDomBool,    1.0, kAutomate, kUnitGlobal},
    {kFeedback,   "Feedback",    "Fdbk",  "",           kDomFeedback,0.0, kAutomate, kUnitGlobal},
    {kWetDry,     "Wet/Dry",     "Wet",   "",           kDomWetDry,  1.0, kAutomate, kUnitGlobal},
    // -- voice 1 (ids 6..9) ------------------------------------------------
    {voiceOffsetId(0), "Voice 1 Transpose", "V1 Transp", "semitones", kDomSemi, 0.0, kAutomate, kUnitVoiceBase + 0},
    {voiceGainId(0),   "Voice 1 Gain",         "V1 Gain",   "",          kDomGain, 1.0, kAutomate, kUnitVoiceBase + 0},
    {voiceMuteId(0),   "Voice 1 Mute",         "V1 Mute",   "",          kDomBool, 0.0, kAutomate, kUnitVoiceBase + 0},
    {voiceDelayId(0),  "Voice 1 Base Delay",   "V1 Delay",  "ms",        kDomMs,   kDefaultBaseMs[0], kAutomate, kUnitVoiceBase + 0},
    // -- voice 2 -----------------------------------------------------------
    {voiceOffsetId(1), "Voice 2 Transpose", "V2 Transp", "semitones", kDomSemi, 0.0, kAutomate, kUnitVoiceBase + 1},
    {voiceGainId(1),   "Voice 2 Gain",         "V2 Gain",   "",          kDomGain, 1.0, kAutomate, kUnitVoiceBase + 1},
    {voiceMuteId(1),   "Voice 2 Mute",         "V2 Mute",   "",          kDomBool, 0.0, kAutomate, kUnitVoiceBase + 1},
    {voiceDelayId(1),  "Voice 2 Base Delay",   "V2 Delay",  "ms",        kDomMs,   kDefaultBaseMs[1], kAutomate, kUnitVoiceBase + 1},
    // -- voice 3 -----------------------------------------------------------
    {voiceOffsetId(2), "Voice 3 Transpose", "V3 Transp", "semitones", kDomSemi, 0.0, kAutomate, kUnitVoiceBase + 2},
    {voiceGainId(2),   "Voice 3 Gain",         "V3 Gain",   "",          kDomGain, 1.0, kAutomate, kUnitVoiceBase + 2},
    {voiceMuteId(2),   "Voice 3 Mute",         "V3 Mute",   "",          kDomBool, 0.0, kAutomate, kUnitVoiceBase + 2},
    {voiceDelayId(2),  "Voice 3 Base Delay",   "V3 Delay",  "ms",        kDomMs,   kDefaultBaseMs[2], kAutomate, kUnitVoiceBase + 2},
    // -- voice 4 -----------------------------------------------------------
    {voiceOffsetId(3), "Voice 4 Transpose", "V4 Transp", "semitones", kDomSemi, 0.0, kAutomate, kUnitVoiceBase + 3},
    {voiceGainId(3),   "Voice 4 Gain",         "V4 Gain",   "",          kDomGain, 1.0, kAutomate, kUnitVoiceBase + 3},
    {voiceMuteId(3),   "Voice 4 Mute",         "V4 Mute",   "",          kDomBool, 0.0, kAutomate, kUnitVoiceBase + 3},
    {voiceDelayId(3),  "Voice 4 Base Delay",   "V4 Delay",  "ms",        kDomMs,   kDefaultBaseMs[3], kAutomate, kUnitVoiceBase + 3},
    // -- voice 5 -----------------------------------------------------------
    {voiceOffsetId(4), "Voice 5 Transpose", "V5 Transp", "semitones", kDomSemi, 0.0, kAutomate, kUnitVoiceBase + 4},
    {voiceGainId(4),   "Voice 5 Gain",         "V5 Gain",   "",          kDomGain, 1.0, kAutomate, kUnitVoiceBase + 4},
    {voiceMuteId(4),   "Voice 5 Mute",         "V5 Mute",   "",          kDomBool, 0.0, kAutomate, kUnitVoiceBase + 4},
    {voiceDelayId(4),  "Voice 5 Base Delay",   "V5 Delay",  "ms",        kDomMs,   kDefaultBaseMs[4], kAutomate, kUnitVoiceBase + 4},
    // -- voice 6 -----------------------------------------------------------
    {voiceOffsetId(5), "Voice 6 Transpose", "V6 Transp", "semitones", kDomSemi, 0.0, kAutomate, kUnitVoiceBase + 5},
    {voiceGainId(5),   "Voice 6 Gain",         "V6 Gain",   "",          kDomGain, 1.0, kAutomate, kUnitVoiceBase + 5},
    {voiceMuteId(5),   "Voice 6 Mute",         "V6 Mute",   "",          kDomBool, 0.0, kAutomate, kUnitVoiceBase + 5},
    {voiceDelayId(5),  "Voice 6 Base Delay",   "V6 Delay",  "ms",        kDomMs,   kDefaultBaseMs[5], kAutomate, kUnitVoiceBase + 5},
    // -- voice 7 -----------------------------------------------------------
    {voiceOffsetId(6), "Voice 7 Transpose", "V7 Transp", "semitones", kDomSemi, 0.0, kAutomate, kUnitVoiceBase + 6},
    {voiceGainId(6),   "Voice 7 Gain",         "V7 Gain",   "",          kDomGain, 1.0, kAutomate, kUnitVoiceBase + 6},
    {voiceMuteId(6),   "Voice 7 Mute",         "V7 Mute",   "",          kDomBool, 0.0, kAutomate, kUnitVoiceBase + 6},
    {voiceDelayId(6),  "Voice 7 Base Delay",   "V7 Delay",  "ms",        kDomMs,   kDefaultBaseMs[6], kAutomate, kUnitVoiceBase + 6},
    // -- voice 8 -----------------------------------------------------------
    {voiceOffsetId(7), "Voice 8 Transpose", "V8 Transp", "semitones", kDomSemi, 0.0, kAutomate, kUnitVoiceBase + 7},
    {voiceGainId(7),   "Voice 8 Gain",         "V8 Gain",   "",          kDomGain, 1.0, kAutomate, kUnitVoiceBase + 7},
    {voiceMuteId(7),   "Voice 8 Mute",         "V8 Mute",   "",          kDomBool, 0.0, kAutomate, kUnitVoiceBase + 7},
    {voiceDelayId(7),  "Voice 8 Base Delay",   "V8 Delay",  "ms",        kDomMs,   kDefaultBaseMs[7], kAutomate, kUnitVoiceBase + 7},
    // clang-format on
};

static_assert(voiceOffsetId(7) == 34 && voiceDelayId(7) == 37, "param id layout");

//--------------------------------------------------------------------------
// Per-parameter help text (R21-P0: "help text or equivalent metadata").
// One static ASCII line per parameter id, exposed via paramHelp(); consumed
// by the validation kit's parameter dump, the wrapper documentation and
// (in future GUI rounds) a tooltip layer. Static + allocation-free for the
// same reasons as the spec table itself (audio-thread safe domainOf()
// consults the same translation unit).
//--------------------------------------------------------------------------

const char* const kParamHelp[kNumParams] = {
    "Bypass the effect (crossfaded; the dry path is latency-aligned).",
    "Global transposition in semitones applied to EVERY voice. Composition "
    "law: each voice's pitch ratio = global ratio * voice ratio (semitones "
    "add, ratios multiply). Default 0.",
    "Number of active voices (1..8). Voices beyond the first follow their "
    "own Transpose/Base Delay; entering voices fade in and the voice-count "
    "normalisation compensates loudness.",
    "Harmony Map (formerly titled 'Spread'): a MUSICAL harmony-map switch -- "
    "NOT stereo widening. ON: a voice with a zero explicit Transpose follows "
    "the musical spread table (voice 2 = +7 semitones, voice 3 = +12, ...); "
    "a voice with a NONZERO Transpose displaces its spread-table slot while "
    "keeping the global transposition. OFF: every voice uses exactly its own "
    "Transpose value. Stereo output remains strict dual-mono for identical "
    "L/R input either way. Default ON.",
    "Global wet feedback coefficient [0, 0.9] (host-facing subset of the "
    "engine's (-1,1) domain). 0 = off.",
    "Wet/dry mix [0,1]: 1 = fully wet (default), 0 = dry only. Bypass is the "
    "separate parameter 0.",
    // per-voice entries (ids 6..37): four texts per voice
    "Voice 1's own transposition in semitones; composes with the Global Pitch "
    "Shift (ratios multiply, semitones add). Zero + Harmony Map ON: voice 1 uses its spread-table slot (unison).",
    "Voice 1 output gain, linear [0,2] (1 = unity).",
    "Mute voice 1 (ramped out; DSP state stays warm).",
    "Voice 1 base delay in ms [0,200]; default = (256+32*0)/48 ms. Changes are "
    "crossfaded. Distinct per-voice delays = the inter-voice temporal spacing.",
    "Voice 2's own transposition in semitones; composes with the Global Pitch "
    "Shift (ratios multiply, semitones add). Zero + Harmony Map ON: voice 2 follows the spread table (+7 semitones).",
    "Voice 2 output gain, linear [0,2] (1 = unity).",
    "Mute voice 2 (ramped out; DSP state stays warm).",
    "Voice 2 base delay in ms [0,200]; default = (256+32*1)/48 ms (0.667 ms "
    "behind voice 1 at 48 kHz). Changes are crossfaded.",
    "Voice 3's own transposition in semitones; composes with the Global Pitch "
    "Shift (ratios multiply, semitones add). Zero + Harmony Map ON: voice 3 follows the spread table (+12 semitones).",
    "Voice 3 output gain, linear [0,2] (1 = unity).",
    "Mute voice 3 (ramped out; DSP state stays warm).",
    "Voice 3 base delay in ms [0,200]; default = (256+32*2)/48 ms. Changes are "
    "crossfaded.",
    "Voice 4's own transposition in semitones; composes with the Global Pitch "
    "Shift (ratios multiply, semitones add). Zero + Harmony Map ON: voice 4 follows the spread table.",
    "Voice 4 output gain, linear [0,2] (1 = unity).",
    "Mute voice 4 (ramped out; DSP state stays warm).",
    "Voice 4 base delay in ms [0,200]; default = (256+32*3)/48 ms. Changes are "
    "crossfaded.",
    "Voice 5's own transposition in semitones; composes with the Global Pitch "
    "Shift (ratios multiply, semitones add). Zero + Harmony Map ON: voice 5 follows the spread table.",
    "Voice 5 output gain, linear [0,2] (1 = unity).",
    "Mute voice 5 (ramped out; DSP state stays warm).",
    "Voice 5 base delay in ms [0,200]; default = (256+32*4)/48 ms. Changes are "
    "crossfaded.",
    "Voice 6's own transposition in semitones; composes with the Global Pitch "
    "Shift (ratios multiply, semitones add). Zero + Harmony Map ON: voice 6 follows the spread table.",
    "Voice 6 output gain, linear [0,2] (1 = unity).",
    "Mute voice 6 (ramped out; DSP state stays warm).",
    "Voice 6 base delay in ms [0,200]; default = (256+32*5)/48 ms. Changes are "
    "crossfaded.",
    "Voice 7's own transposition in semitones; composes with the Global Pitch "
    "Shift (ratios multiply, semitones add). Zero + Harmony Map ON: voice 7 follows the spread table.",
    "Voice 7 output gain, linear [0,2] (1 = unity).",
    "Mute voice 7 (ramped out; DSP state stays warm).",
    "Voice 7 base delay in ms [0,200]; default = (256+32*6)/48 ms. Changes are "
    "crossfaded.",
    "Voice 8's own transposition in semitones; composes with the Global Pitch "
    "Shift (ratios multiply, semitones add). Zero + Harmony Map ON: voice 8 follows the spread table.",
    "Voice 8 output gain, linear [0,2] (1 = unity).",
    "Mute voice 8 (ramped out; DSP state stays warm).",
    "Voice 8 base delay in ms [0,200]; default = (256+32*7)/48 ms. Changes are "
    "crossfaded.",
};

static_assert(sizeof(kParamHelp) / sizeof(kParamHelp[0]) == kNumParams,
              "help text table must cover all 38 parameters");

}  // namespace

const ParamSpec& paramSpec(uint32_t id) {
    return kSpecs[id < static_cast<uint32_t>(kNumParams) ? id : 0];
}

/// R21-P0: static per-parameter help text (see kParamHelp above).
const char* paramHelp(uint32_t id) {
    return kParamHelp[id < static_cast<uint32_t>(kNumParams) ? id : 0];
}

//--------------------------------------------------------------------------
// Conversions
//--------------------------------------------------------------------------

ParamDomain domainOf(uint32_t id) { return paramSpec(id).domain; }

double plainFromNormalized(uint32_t id, double norm) {
    const ParamDomain d = domainOf(id);
    if (std::isnan(norm)) norm = 0.0;
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;
    const double span = d.maxPlain - d.minPlain;
    double plain;
    if (d.stepPlain > 0.0) {
        // Quantize to the step grid. For step-aligned normalized values the
        // round-trip is exact (the quantization absorbs <=1e-12 rounding).
        const double steps = (norm * span) / d.stepPlain;
        plain = d.minPlain + std::floor(steps + 0.5) * d.stepPlain;
    } else {
        plain = d.minPlain + norm * span;
    }
    if (plain < d.minPlain) plain = d.minPlain;
    if (plain > d.maxPlain) plain = d.maxPlain;
    return plain;
}

double normalizedFromPlain(uint32_t id, double plain) {
    const ParamDomain d = domainOf(id);
    if (std::isnan(plain)) plain = d.minPlain;
    if (plain < d.minPlain) plain = d.minPlain;
    if (plain > d.maxPlain) plain = d.maxPlain;
    const double span = d.maxPlain - d.minPlain;
    double norm = (plain - d.minPlain) / span;
    if (d.stepPlain > 0.0) {
        const double steps = std::floor((plain - d.minPlain) / d.stepPlain + 0.5);
        norm = (steps * d.stepPlain) / span;
    }
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;
    return norm;
}

double msToBaseDelaySamples(double ms, double sampleRate, double grain) {
    if (!(sampleRate > 0.0) || std::isnan(sampleRate)) sampleRate = 48000.0;
    if (std::isnan(ms) || ms < 0.0) ms = 0.0;
    double s = ms * sampleRate / 1000.0;
    const double lo = 1.0;  // the core requires delay > 0 STRICTLY
    const double hi = maxBaseDelaySamples(grain);
    if (s < lo) s = lo;
    if (s > hi) s = hi;
    return s;
}

//--------------------------------------------------------------------------
// Surface
//--------------------------------------------------------------------------

Surface defaultSurface() {
    Surface s{};
    s.bypass = 0.0;
    s.pitchSemi = 0.0;
    s.voices = 1.0;   // core Config default
    s.spread = 1.0;   // core Config default (true)
    s.feedback = 0.0; // core Config default
    s.wetDry = 1.0;   // core default: bit-neutral all-wet path
    for (int v = 0; v < kMaxVoices; ++v) {
        s.offset[v] = 0.0;
        s.gain[v] = 1.0;
        s.mute[v] = 0.0;
        s.baseMs[v] = kDefaultBaseMs[v];
    }
    return s;
}

namespace {

inline double clampPlain(uint32_t id, double plain) {
    const ParamDomain d = domainOf(id);
    if (std::isnan(plain)) plain = d.minPlain;
    if (plain < d.minPlain) plain = d.minPlain;
    if (plain > d.maxPlain) plain = d.maxPlain;
    return plain;
}

}  // namespace

void surfaceToPlainArray(const Surface& s, double out[kNumParams]) {
    out[kBypass] = s.bypass;
    out[kPitch] = s.pitchSemi;
    out[kVoices] = s.voices;
    out[kSpread] = s.spread;
    out[kFeedback] = s.feedback;
    out[kWetDry] = s.wetDry;
    for (int v = 0; v < kMaxVoices; ++v) {
        out[voiceOffsetId(v)] = s.offset[v];
        out[voiceGainId(v)] = s.gain[v];
        out[voiceMuteId(v)] = s.mute[v];
        out[voiceDelayId(v)] = s.baseMs[v];
    }
}

void plainArrayToSurface(const double in[kNumParams], Surface& s) {
    // Clamp every plain value into its parameter domain (defensive against
    // corrupt/hand-edited state blobs).
    s.bypass = clampPlain(kBypass, in[kBypass]);
    s.pitchSemi = clampPlain(kPitch, in[kPitch]);
    s.voices = clampPlain(kVoices, in[kVoices]);
    s.spread = clampPlain(kSpread, in[kSpread]);
    s.feedback = clampPlain(kFeedback, in[kFeedback]);
    s.wetDry = clampPlain(kWetDry, in[kWetDry]);
    for (int v = 0; v < kMaxVoices; ++v) {
        s.offset[v] = clampPlain(voiceOffsetId(v), in[voiceOffsetId(v)]);
        s.gain[v] = clampPlain(voiceGainId(v), in[voiceGainId(v)]);
        s.mute[v] = clampPlain(voiceMuteId(v), in[voiceMuteId(v)]);
        s.baseMs[v] = clampPlain(voiceDelayId(v), in[voiceDelayId(v)]);
    }
}

//--------------------------------------------------------------------------
// State blob
//--------------------------------------------------------------------------

namespace {

constexpr uint32_t kStateMagic = 0x3152564Fu;  // 'O','V','R','1' (LE byte order)
// R19: version 2 adds the four 16.0.0 Config fields after bypass_seconds
// (detector_mode + the three transition-time fields). Version 1 blobs (the
// R16-R18 'OctavoxNative' and R18-WDB variant blobs) remain READABLE: the
// new fields take the 16.0.0 defaults and the caller (the processor) forces
// the compile-time selections on top.
constexpr int32_t kStateVersion = 2;
constexpr int32_t kStateVersionLegacy = 1;

bool finiteD(double x) { return std::isfinite(x); }

/// Clamp the config into the core prepare() validity domain. Returns false
/// if a value is non-finite or structurally unfixable (caller rejects).
bool sanitizeConfig(Config& c) {
    if (!finiteD(c.sample_rate) || c.sample_rate <= 0.0) return false;
    if (c.sample_rate > 1.0e6) c.sample_rate = 1.0e6;
    if (!finiteD(c.ratio) || c.ratio <= 0.0) c.ratio = 1.0;
    if (c.max_voices < 1) c.max_voices = 1;
    if (c.max_voices > 64) c.max_voices = 64;
    if (c.voices < 1) c.voices = 1;
    if (c.voices > c.max_voices) c.voices = c.max_voices;
    if (!finiteD(c.grain) || c.grain <= 0.0) return false;
    if (!finiteD(c.base_delay) || c.base_delay < 8.0) c.base_delay = 8.0;
    // ring-range law: base + 32*(mv-1) + 3*grain + 8 < 2^18
    if (c.base_delay + 32.0 * (c.max_voices - 1) + 3.0 * c.grain + 8.0 >= 262144.0)
        return false;
    if (!finiteD(c.fmin) || c.fmin <= 0.0) c.fmin = 55.0;
    if (!finiteD(c.fmax) || c.fmax <= c.fmin) c.fmax = c.fmin + 1.0;
    if (!finiteD(c.s_hysteresis) || c.s_hysteresis < 0.0) c.s_hysteresis = 0.0;
    if (c.f0_history < 1) c.f0_history = 1;
    if (c.f0_history > 64) c.f0_history = 64;
    if (c.detection_interval < 1) c.detection_interval = 1;
    if (!finiteD(c.engagement_seconds) || c.engagement_seconds < 0.0)
        c.engagement_seconds = 0.0;
    if (!finiteD(c.gain_ramp_seconds) || c.gain_ramp_seconds < 0.0)
        c.gain_ramp_seconds = 0.0;
    if (!finiteD(c.bypass_seconds) || c.bypass_seconds < 0.0) c.bypass_seconds = 0.0;
    // R19 fields (v2 blobs; v1 blobs carry the 16.0.0 defaults already)
    if (!finiteD(c.commit_crossfade_seconds) || c.commit_crossfade_seconds < 0.0)
        c.commit_crossfade_seconds = 0.0;
    if (!finiteD(c.delay_crossfade_seconds) || c.delay_crossfade_seconds < 0.0)
        c.delay_crossfade_seconds = 0.0;
    if (!finiteD(c.voice_fade_seconds) || c.voice_fade_seconds < 0.0)
        c.voice_fade_seconds = 0.0;
    if (!finiteD(c.feedback)) c.feedback = 0.0;
    if (c.feedback > 0.999999) c.feedback = 0.999999;
    if (c.feedback < -0.999999) c.feedback = -0.999999;
    const int interp = static_cast<int>(c.interpolation);
    if (interp < 0 || interp > 3) return false;  // reject unknown modes
    const int dmode = static_cast<int>(c.detector_mode);
    if (dmode != static_cast<int>(DetectorMode::FullRate) &&
        dmode != static_cast<int>(DetectorMode::Decimated) &&
        dmode != static_cast<int>(DetectorMode::Hybrid))
        return false;  // reject unknown detector modes
    return true;
}

}  // namespace

bool writeStateData(Steinberg::IBStream* stream, const StateData& st) {
    if (!stream) return false;
    Steinberg::IBStreamer w(stream, kLittleEndian);

    if (!w.writeInt32u(kStateMagic)) return false;
    if (!w.writeInt32(kStateVersion)) return false;

    // Config (17 fields, fixed order)
    if (!w.writeDouble(st.cfg.sample_rate)) return false;
    if (!w.writeDouble(st.cfg.ratio)) return false;
    if (!w.writeInt32(st.cfg.voices)) return false;
    if (!w.writeInt32(st.cfg.max_voices)) return false;
    if (!w.writeInt32(st.cfg.spread ? 1 : 0)) return false;
    if (!w.writeDouble(st.cfg.feedback)) return false;
    if (!w.writeInt32(static_cast<int32>(st.cfg.interpolation))) return false;
    if (!w.writeDouble(st.cfg.grain)) return false;
    if (!w.writeDouble(st.cfg.base_delay)) return false;
    if (!w.writeDouble(st.cfg.fmin)) return false;
    if (!w.writeDouble(st.cfg.fmax)) return false;
    if (!w.writeDouble(st.cfg.s_hysteresis)) return false;
    if (!w.writeInt32(st.cfg.f0_history)) return false;
    if (!w.writeInt32(st.cfg.detection_interval)) return false;
    if (!w.writeDouble(st.cfg.engagement_seconds)) return false;
    if (!w.writeDouble(st.cfg.gain_ramp_seconds)) return false;
    if (!w.writeDouble(st.cfg.bypass_seconds)) return false;
    // R19 (version 2): the 16.0.0 fields
    if (!w.writeInt32(static_cast<int32>(st.cfg.detector_mode))) return false;
    if (!w.writeDouble(st.cfg.commit_crossfade_seconds)) return false;
    if (!w.writeDouble(st.cfg.delay_crossfade_seconds)) return false;
    if (!w.writeDouble(st.cfg.voice_fade_seconds)) return false;

    if (!w.writeInt32(st.channels)) return false;

    // The 38 plain values, fixed order (slot i == parameter id i)
    double vals[kNumParams];
    surfaceToPlainArray(st.surface, vals);
    if (!w.writeDoubleArray(vals, kNumParams)) return false;
    return true;
}

bool readStateData(Steinberg::IBStream* stream, StateData& out) {
    if (!stream) return false;
    Steinberg::IBStreamer r(stream, kLittleEndian);

    uint32_t magic = 0;
    int32_t version = 0;
    if (!r.readInt32u(magic) || magic != kStateMagic) return false;
    if (!r.readInt32(version) ||
        (version != kStateVersion && version != kStateVersionLegacy))
        return false;
    const bool v2 = (version == kStateVersion);

    Config c{};
    int32_t i32 = 0;
    if (!r.readDouble(c.sample_rate)) return false;
    if (!r.readDouble(c.ratio)) return false;
    if (!r.readInt32(c.voices)) return false;
    if (!r.readInt32(c.max_voices)) return false;
    if (!r.readInt32(i32)) return false;
    c.spread = (i32 != 0);
    if (!r.readDouble(c.feedback)) return false;
    if (!r.readInt32(i32)) return false;
    c.interpolation = static_cast<Interpolation>(i32);
    if (!r.readDouble(c.grain)) return false;
    if (!r.readDouble(c.base_delay)) return false;
    if (!r.readDouble(c.fmin)) return false;
    if (!r.readDouble(c.fmax)) return false;
    if (!r.readDouble(c.s_hysteresis)) return false;
    if (!r.readInt32(c.f0_history)) return false;
    if (!r.readInt32(c.detection_interval)) return false;
    if (!r.readDouble(c.engagement_seconds)) return false;
    if (!r.readDouble(c.gain_ramp_seconds)) return false;
    if (!r.readDouble(c.bypass_seconds)) return false;
    if (v2) {
        // R19 (version 2): the 16.0.0 fields
        if (!r.readInt32(i32)) return false;
        c.detector_mode = static_cast<DetectorMode>(i32);
        if (!r.readDouble(c.commit_crossfade_seconds)) return false;
        if (!r.readDouble(c.delay_crossfade_seconds)) return false;
        if (!r.readDouble(c.voice_fade_seconds)) return false;
    }  // else: the 16.0.0 defaults stand (Config{} initialisation)
    if (!r.readInt32(i32)) return false;
    const int32_t channels = i32;

    double vals[kNumParams];
    if (!r.readDoubleArray(vals, kNumParams)) return false;

    if (!sanitizeConfig(c)) return false;
    out.cfg = c;
    out.channels = (channels >= 0 && channels <= 64) ? channels : 0;
    plainArrayToSurface(vals, out.surface);
    return true;
}

//--------------------------------------------------------------------------
// OctavoxParameter
//--------------------------------------------------------------------------

OctavoxParameter::OctavoxParameter(uint32_t id, const Steinberg::Vst::TChar* title,
                                    const Steinberg::Vst::TChar* shortTitle,
                                    const Steinberg::Vst::TChar* units, double minPlain,
                                    double maxPlain, double stepPlain, double defaultPlain,
                                    int32 flags, Steinberg::Vst::UnitID unitId)
    : Parameter(), minPlain_(minPlain), maxPlain_(maxPlain), stepPlain_(stepPlain) {
    ParameterInfo& info = getInfo();
    info.id = id;
    Steinberg::UString(info.title, str16BufferSize(Steinberg::Vst::String128)).assign(title);
    if (shortTitle)
        Steinberg::UString(info.shortTitle, str16BufferSize(Steinberg::Vst::String128))
            .assign(shortTitle);
    if (units)
        Steinberg::UString(info.units, str16BufferSize(Steinberg::Vst::String128))
            .assign(units);
    info.stepCount = (stepPlain > 0.0)
                         ? static_cast<int32>(std::floor((maxPlain - minPlain) / stepPlain + 0.5))
                         : 0;
    info.defaultNormalizedValue = normalizedFromPlain(id, defaultPlain);
    info.flags = flags;
    info.unitId = unitId;
    // R20-P0 (default-parameter initialization): a freshly registered
    // parameter must REPORT the declared default as its CURRENT value.
    // The SDK's Parameter() default constructor initialises valueNormalized
    // {0.} -- NOT the declared default (vstparameters.cpp:48, SDK 3.7.12) --
    // and every host-visible reading path (IEditController::
    // getParamNormalized, the Generic Editor, host automation-flush
    // snapshots) reads the CURRENT value, not info.defaultNormalizedValue.
    // Before this fix a fresh instance reported 0-based values (Pitch
    // -24.00, Wet/Dry 0.00, Spread off, voice gains 0.0, voice offsets
    // -24.00, voice delays 0 ms) while the engine ran the true defaults
    // from defaultSurface() -- inconsistent until the first manual edit
    // pushed a real value through the parameter queue. setNormalized() is
    // called AFTER info is complete; the object is not yet added to any
    // container, so it has no listeners and the changed() notification is
    // a no-op (pure value initialisation, no host-visible side effect).
    setNormalized(info.defaultNormalizedValue);
}

Steinberg::Vst::ParamValue PLUGIN_API OctavoxParameter::toPlain(
    Steinberg::Vst::ParamValue valueNormalized) const {
    // Same formula as the free function (the audio path uses the free one).
    const double span = maxPlain_ - minPlain_;
    double norm = valueNormalized;
    if (std::isnan(norm)) norm = 0.0;
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;
    double plain;
    if (stepPlain_ > 0.0)
        plain = minPlain_ + std::floor((norm * span) / stepPlain_ + 0.5) * stepPlain_;
    else
        plain = minPlain_ + norm * span;
    if (plain < minPlain_) plain = minPlain_;
    if (plain > maxPlain_) plain = maxPlain_;
    return plain;
}

Steinberg::Vst::ParamValue PLUGIN_API OctavoxParameter::toNormalized(
    Steinberg::Vst::ParamValue plainValue) const {
    const double span = maxPlain_ - minPlain_;
    double plain = plainValue;
    if (std::isnan(plain)) plain = minPlain_;
    if (plain < minPlain_) plain = minPlain_;
    if (plain > maxPlain_) plain = maxPlain_;
    double norm = (plain - minPlain_) / span;
    if (stepPlain_ > 0.0) {
        const double steps = std::floor((plain - minPlain_) / stepPlain_ + 0.5);
        norm = (steps * stepPlain_) / span;
    }
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;
    return norm;
}

void PLUGIN_API OctavoxParameter::toString(Steinberg::Vst::ParamValue valueNormalized,
                                           Steinberg::Vst::String128 string) const {
    const double plain = toPlain(valueNormalized);
    char buf[32];
    if (stepPlain_ >= 1.0)
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(std::llround(plain)));
    else
        std::snprintf(buf, sizeof(buf), "%.4g", plain);
    Steinberg::UString(string, str16BufferSize(Steinberg::Vst::String128)).fromAscii(buf);
}

bool PLUGIN_API OctavoxParameter::fromString(const Steinberg::Vst::TChar* string,
                                             Steinberg::Vst::ParamValue& valueNormalized) const {
    if (!string) return false;
    Steinberg::UString u(const_cast<Steinberg::Vst::TChar*>(string), -1);
    double plain = 0.0;
    if (!u.scanFloat(plain)) return false;
    valueNormalized = toNormalized(plain);
    return true;
}

//--------------------------------------------------------------------------
// Registration
//--------------------------------------------------------------------------

void registerParameters(Steinberg::Vst::ParameterContainer& parameters) {
    parameters.init(kNumParams + 8, 16);
    for (uint32_t id = 0; id < static_cast<uint32_t>(kNumParams); ++id) {
        const ParamSpec& sp = paramSpec(id);
        Steinberg::Vst::String128 title, shortT, units;
        Steinberg::UString(title, str16BufferSize(Steinberg::Vst::String128))
            .fromAscii(sp.title);
        Steinberg::UString(shortT, str16BufferSize(Steinberg::Vst::String128))
            .fromAscii(sp.shortTitle);
        Steinberg::UString(units, str16BufferSize(Steinberg::Vst::String128))
            .fromAscii(sp.units ? sp.units : "");
        parameters.addParameter(new OctavoxParameter(
            id, title, shortT, units, sp.domain.minPlain, sp.domain.maxPlain,
            sp.domain.stepPlain, sp.defaultPlain, sp.flags, sp.unitId));
    }
}

void registerUnits(Steinberg::Vst::EditControllerEx1& controller) {
    auto addUnit = [&controller](int id, const char* name, int parent) {
        Steinberg::Vst::String128 n;
        Steinberg::UString(n, str16BufferSize(Steinberg::Vst::String128)).fromAscii(name);
        controller.addUnit(new Steinberg::Vst::Unit(
            n, static_cast<Steinberg::Vst::UnitID>(id),
            static_cast<Steinberg::Vst::UnitID>(parent)));
    };
    addUnit(kUnitGlobal, "Global", Steinberg::Vst::kRootUnitId);
    for (int v = 0; v < kMaxVoices; ++v) {
        char name[32];
        std::snprintf(name, sizeof(name), "Voice %d", v + 1);
        addUnit(kUnitVoiceBase + v, name, kUnitGlobal);
    }
}

}  // namespace vst3wrap
}  // namespace octavox
