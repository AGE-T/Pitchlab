#!/usr/bin/env python3
"""R21 re-apply patch script (idempotent). Re-applies all R21 edits to the
freshly extracted R20 tree after a sandbox reset. Run from the tree root
(Octavox_RE_evidence)."""
import sys, os

def patch(path, old, new, must=True):
    src = open(path).read()
    if old in src:
        src = src.replace(old, new, 1)
        open(path, 'w').write(src)
        print(f"  patched {path}")
        return True
    if new in src:
        print(f"  already patched {path}")
        return True
    if must:
        print(f"  FAILED: pattern not found in {path}")
        sys.exit(1)
    return False

def patchall(path, old, new):
    src = open(path).read()
    if old in src:
        src = src.replace(old, new)
        open(path, 'w').write(src)
        print(f"  patched(all) {path}")
    elif new in src:
        print(f"  already patched(all) {path}")
    else:
        print(f"  FAILED(all): pattern not found in {path}")
        sys.exit(1)

# ---------------------------------------------------------------- vst3 CMake
patch('native/vst3/CMakeLists.txt', '''set(OCTAVOX_VST3_INTERPOLATION "LAGRANGE6" CACHE STRING
    "VST3 wrapper default interpolation kernel: LAGRANGE6 | LAGRANGE6Q | OBSKERNEL6")
string(TOUPPER "${OCTAVOX_VST3_INTERPOLATION}" OCTAVOX_VST3_INTERPOLATION)
if(NOT OCTAVOX_VST3_INTERPOLATION MATCHES "^(LAGRANGE6|LAGRANGE6Q|OBSKERNEL6)$")
    message(FATAL_ERROR "OCTAVOX_VST3_INTERPOLATION must be one of "
        "LAGRANGE6 | LAGRANGE6Q | OBSKERNEL6 (got: '${OCTAVOX_VST3_INTERPOLATION}')")
endif()
set(OCTAVOX_VST3_VARIANT_DEF "OCTAVOX_VST3_INTERP_${OCTAVOX_VST3_INTERPOLATION}=1")
if(OCTAVOX_VST3_INTERPOLATION STREQUAL "OBSKERNEL6")
    set(OCTAVOX_VST3_BUNDLE_NAME "OctavoxRE_ObsKernel6")
elseif(OCTAVOX_VST3_INTERPOLATION STREQUAL "LAGRANGE6Q")
    set(OCTAVOX_VST3_BUNDLE_NAME "OctavoxRE_Lagrange6Q")
else()
    set(OCTAVOX_VST3_BUNDLE_NAME "OctavoxRE_Lagrange6")
endif()
message(STATUS "OCTAVOX VST3 build variant: ${OCTAVOX_VST3_INTERPOLATION} "
               "(bundle ${OCTAVOX_VST3_BUNDLE_NAME}.vst3; define ${OCTAVOX_VST3_VARIANT_DEF})")''',
'''set(OCTAVOX_VST3_INTERPOLATION "LAGRANGE6" CACHE STRING
    "VST3 wrapper default interpolation kernel: LAGRANGE6 | LAGRANGE6Q | OBSKERNEL6")
string(TOUPPER "${OCTAVOX_VST3_INTERPOLATION}" OCTAVOX_VST3_INTERPOLATION)
if(NOT OCTAVOX_VST3_INTERPOLATION MATCHES "^(LAGRANGE6|LAGRANGE6Q|OBSKERNEL6)$")
    message(FATAL_ERROR "OCTAVOX_VST3_INTERPOLATION must be one of "
        "LAGRANGE6 | LAGRANGE6Q | OBSKERNEL6 (got: '${OCTAVOX_VST3_INTERPOLATION}')")
endif()

set(OCTAVOX_VST3_DETECTOR "DECIMATED" CACHE STRING
    "VST3 wrapper default pitch detector: DECIMATED | HYBRID | FULLRATE (offline/benchmark only)")
string(TOUPPER "${OCTAVOX_VST3_DETECTOR}" OCTAVOX_VST3_DETECTOR)
if(NOT OCTAVOX_VST3_DETECTOR MATCHES "^(DECIMATED|HYBRID|FULLRATE)$")
    message(FATAL_ERROR "OCTAVOX_VST3_DETECTOR must be one of "
        "DECIMATED | HYBRID | FULLRATE (got: '${OCTAVOX_VST3_DETECTOR}')")
endif()

set(OCTAVOX_VST3_VARIANT_DEF
    "OCTAVOX_VST3_INTERP_${OCTAVOX_VST3_INTERPOLATION}=1"
    "OCTAVOX_VST3_DET_${OCTAVOX_VST3_DETECTOR}=1")

# The bundle name mirrors build::kBundleName in octavox_build_config.h
# (the header is the single identity authority; a mismatch fails the
# module-integration tests, which locate the bundle via the header).
# NOTE: display-case mapping back from the validated uppercase selection.
string(TOUPPER "${OCTAVOX_VST3_DETECTOR}" _det_upper)
set(_interp_disp "Lagrange6")
if(OCTAVOX_VST3_INTERPOLATION STREQUAL "LAGRANGE6Q")
    set(_interp_disp "Lagrange6Q")
elseif(OCTAVOX_VST3_INTERPOLATION STREQUAL "OBSKERNEL6")
    set(_interp_disp "ObsKernel6")
endif()
set(_det_disp "Hybrid")
if(_det_upper STREQUAL "FULLRATE")
    set(_det_disp "FullRate")
elseif(_det_upper STREQUAL "DECIMATED")
    set(_det_disp "")
endif()
set(OCTAVOX_VST3_BUNDLE_NAME "PitchLab_${_interp_disp}_${_det_disp}")
if(_det_upper STREQUAL "DECIMATED" AND (OCTAVOX_VST3_INTERPOLATION STREQUAL "LAGRANGE6" OR
                                          OCTAVOX_VST3_INTERPOLATION STREQUAL "OBSKERNEL6"))
    # the two recommended "Balanced" variants use the short suffix
    set(OCTAVOX_VST3_BUNDLE_NAME "PitchLab_${_interp_disp}_Balanced")
elseif(_det_upper STREQUAL "DECIMATED")
    # LAGRANGE6Q auxiliary keeps its historical short name
    set(OCTAVOX_VST3_BUNDLE_NAME "PitchLab_${_interp_disp}")
endif()
message(STATUS "OCTAVOX VST3 build variant: ${OCTAVOX_VST3_INTERPOLATION} + "
               "${OCTAVOX_VST3_DETECTOR} "
               "(bundle ${OCTAVOX_VST3_BUNDLE_NAME}.vst3; defines "
               "${OCTAVOX_VST3_VARIANT_DEF})")''')

patch('native/vst3/CMakeLists.txt', '''# The plug-in module (bundle name per VARIANT: OctavoxRE_<Variant>.vst3)''',
'''# The plug-in module (bundle name per VARIANT: PitchLab_<Variant>.vst3)''')

# ---------------------------------------------------------------- params.cpp
patch('native/vst3/octavox_params.cpp', '''    {kBypass,     "Bypass",      "Byp",   "",          kDomBool,    0.0, kAutomate | ParameterInfo::kIsBypass, Steinberg::Vst::kRootUnitId},
    {kPitch,      "Pitch Shift", "Pitch", "semitones", kDomSemi,    0.0, kAutomate, kUnitGlobal},
    {kVoices,     "Voices",      "Voices","",           kDomVoices,  1.0, kAutomate, kUnitGlobal},
    {kSpread,     "Spread",      "Spread","",           kDomBool,    1.0, kAutomate, kUnitGlobal},''',
'''    // R21-P0 (parameter semantics): the host-visible TITLES were corrected
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
    {kSpread,     "Harmony Map",        "HarmMap",   "",           kDomBool,    1.0, kAutomate, kUnitGlobal},''')

for v in range(1, 9):
    patch('native/vst3/octavox_params.cpp',
          f'    {{voiceOffsetId({v-1}), "Voice {v} Pitch Offset", "V{v} Pitch",  "semitones", kDomSemi, 0.0, kAutomate, kUnitVoiceBase + {v-1}}},',
          f'    {{voiceOffsetId({v-1}), "Voice {v} Transpose", "V{v} Transp", "semitones", kDomSemi, 0.0, kAutomate, kUnitVoiceBase + {v-1}}},')

# help table + paramHelp (after the layout static_assert)
def voice_help(v):
    n = v
    extra = {1: "voice 1 uses its spread-table slot (unison).",
             2: "voice 2 follows the spread table (+7 semitones).",
             3: "voice 3 follows the spread table (+12 semitones)."}.get(n, "voice %d follows the spread table." % n)
    off = ('    "Voice %d\'s own transposition in semitones; composes with the Global Pitch "\n'
           '    "Shift (ratios multiply, semitones add). Zero + Harmony Map ON: %s",\n') % (n, extra)
    gain = '    "Voice %d output gain, linear [0,2] (1 = unity).",\n' % n
    mute = '    "Mute voice %d (ramped out; DSP state stays warm).",\n' % n
    if n == 1:
        dl = ('    "Voice 1 base delay in ms [0,200]; default = (256+32*0)/48 ms. Changes are "\n'
              '    "crossfaded. Distinct per-voice delays = the inter-voice temporal spacing.",\n')
    elif n == 2:
        dl = ('    "Voice 2 base delay in ms [0,200]; default = (256+32*1)/48 ms (0.667 ms "\n'
              '    "behind voice 1 at 48 kHz). Changes are crossfaded.",\n')
    else:
        dl = ('    "Voice %d base delay in ms [0,200]; default = (256+32*%d)/48 ms. Changes are "\n'
              '    "crossfaded.",\n') % (n, n-1)
    return off + gain + mute + dl

per_voice = ''.join(voice_help(v) for v in range(1, 9))
help_table = '''static_assert(voiceOffsetId(7) == 34 && voiceDelayId(7) == 37, "param id layout");

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
''' + per_voice + '''};

static_assert(sizeof(kParamHelp) / sizeof(kParamHelp[0]) == kNumParams,
              "help text table must cover all 38 parameters");'''
patch('native/vst3/octavox_params.cpp',
      'static_assert(voiceOffsetId(7) == 34 && voiceDelayId(7) == 37, "param id layout");',
      help_table)

patch('native/vst3/octavox_params.cpp', '''const ParamSpec& paramSpec(uint32_t id) {
    return kSpecs[id < static_cast<uint32_t>(kNumParams) ? id : 0];
}''',
'''const ParamSpec& paramSpec(uint32_t id) {
    return kSpecs[id < static_cast<uint32_t>(kNumParams) ? id : 0];
}

/// R21-P0: static per-parameter help text (see kParamHelp above).
const char* paramHelp(uint32_t id) {
    return kParamHelp[id < static_cast<uint32_t>(kNumParams) ? id : 0];
}''')

# ---------------------------------------------------------------- params.h
patch('native/vst3/octavox_params.h', '''/// The 38 parameter specifications, indexed by parameter id (0..37).
const ParamSpec& paramSpec(uint32_t id);''',
'''/// The 38 parameter specifications, indexed by parameter id (0..37).
const ParamSpec& paramSpec(uint32_t id);

/// R21-P0: static per-parameter help text (one ASCII line per id; explains
/// the pitch composition law ratio_v = global*voice and the Harmony Map
/// semantics). Consumed by the validation kit's parameter dump, the wrapper
/// documentation and future GUI tooltip layers. Never null.
const char* paramHelp(uint32_t id);''')

# ---------------------------------------------------------------- processor
patch('native/vst3/octavox_native_processor.cpp', '''    // R19 (detector authoritativeness): exactly the same law for the
    // detector architecture -- the module's compile-time selection (the
    // decimated control-rate detector) is authoritative; a v1 blob (which
    // carries no detector field) restores with the module's selection, and
    // a v2 blob saved by a future module with a different default still
    // restores every other field verbatim.''',
'''    // R19/R21 (detector authoritativeness): exactly the same law for the
    // detector architecture -- the module's compile-time selection (R21:
    // DECIMATED | HYBRID | FULLRATE via OCTAVOX_VST3_DETECTOR) is
    // authoritative; a v1 blob (which carries no detector field) restores
    // with the module's selection, and a v2 blob saved by a module with a
    // different detector selection still restores every other field
    // verbatim. Neither the interpolation nor the detector mode is ever
    // silently overridden by saved state.''')

# ---------------------------------------------------------------- dual nulltest
p = 'native/vst3/octavox_dual_nulltest.cpp'
patch(p, 'plugin bundles (OctavoxRE_Lagrange6.vst3 / OctavoxRE_ObsKernel6.vst3, built',
      'plugin bundles (PitchLab_Lagrange6_Balanced.vst3 /\n// PitchLab_ObsKernel6_Balanced.vst3 -- the R21 product identity; the class\n// FUIDs are UNCHANGED from the R18-R20 OctavoxRE builds, built')
patch(p, '"  --lagrange6 <path>   Lagrange6 bundle (OctavoxRE_Lagrange6.vst3)\\n"',
      '"  --lagrange6 <path>   Lagrange6 bundle (PitchLab_Lagrange6_Balanced.vst3)\\n"')
patch(p, '"  --obskernel6 <path>  ObsKernel6 bundle (OctavoxRE_ObsKernel6.vst3)\\n"',
      '"  --obskernel6 <path>  ObsKernel6 bundle (PitchLab_ObsKernel6_Balanced.vst3)\\n"')
patch(p, 'if (pathLag.empty()) pathLag = defaultModulePath(argv[0], "OctavoxRE_Lagrange6.vst3");',
      'if (pathLag.empty()) pathLag = defaultModulePath(argv[0], "PitchLab_Lagrange6_Balanced.vst3");')
patch(p, 'if (pathObs.empty()) pathObs = defaultModulePath(argv[0], "OctavoxRE_ObsKernel6.vst3");',
      'if (pathObs.empty()) pathObs = defaultModulePath(argv[0], "PitchLab_ObsKernel6_Balanced.vst3");')

# ---------------------------------------------------------------- dsp.hpp f0_min
patch('native/include/octavox/dsp.hpp', '''    double fmin = 55.0;               ///< spacing-accept gate (Hz)
    double fmax = 1200.0;             ///< spacing-accept gate (Hz)''',
'''    double fmin = 55.0;               ///< spacing-accept gate (Hz)
    double fmax = 1200.0;             ///< spacing-accept gate (Hz)
    /// R21: the DETECTOR's lower validity gate and lag-domain floor (Hz).
    /// Distinct from fmin/fmax (the ENGINE's spacing-accept gate on the
    /// tracker's committed estimate): f0_min sizes the detector's lag
    /// domain (tau_max = int(rate / f0_min), both the full-rate window law
    /// win = max(2048, ceil(2.5*tau_max)) and the decimated law
    /// win_dec = max(1024, ceil(2.5*tau_dec))) and lower-bounds the
    /// estimate validity gate [f0_min, 1200] Hz. Default 60.0 = the
    /// R12-R20 fixed law BIT-EXACTLY (every existing hash and behaviour
    /// unchanged). Buffer-determining for re-prepare (a change re-sizes the
    /// detector windows). Validated into [20, 200] Hz and < fmax. NOT
    /// serialized in the VST3 state blob (v2 layout frozen; a restored
    /// session keeps the default -- the knob is an engine-configuration
    /// API for the low-note investigation harness, not a host parameter).
    double f0_min = 60.0;''')

# ---------------------------------------------------------------- dsp.cpp f0_min
patch('native/src/dsp.cpp', '''    void configure(int sr, DetectorMode mode) {
        mode_ = mode;
        sr_ = sr;''',
'''    // R21: f0_min (Hz) = the detector's lower validity gate / lag-domain
    // floor. Default 60.0 reproduces the R12-R20 fixed law bit-exactly
    // (int(rate/60.0) and the 60.0<=f0 gate are the same arithmetic).
    void configure(int sr, DetectorMode mode, double f0_min) {
        mode_ = mode;
        sr_ = sr;
        f0_min_ = (f0_min >= 20.0 && f0_min <= 200.0) ? f0_min : 60.0;''')

patch('native/src/dsp.cpp', '''            tau_min_ = std::max(2, static_cast<int>(sr_dec_ / 1200.0));
            const int tau_max = static_cast<int>(sr_dec_ / 60.0);''',
'''            tau_min_ = std::max(2, static_cast<int>(sr_dec_ / 1200.0));
            const int tau_max = static_cast<int>(sr_dec_ / f0_min_);''')
patch('native/src/dsp.cpp', '''                tau_min_raw_ = std::max(2, static_cast<int>(sr / 1200.0));
                const int tau_max_raw = static_cast<int>(sr / 60.0);''',
'''                tau_min_raw_ = std::max(2, static_cast<int>(sr / 1200.0));
                const int tau_max_raw = static_cast<int>(sr / f0_min_);''')
patch('native/src/dsp.cpp', '''            tau_min_ = std::max(2, static_cast<int>(sr_ / 1200.0));
            const int tau_max = static_cast<int>(sr_ / 60.0);''',
'''            tau_min_ = std::max(2, static_cast<int>(sr_ / 1200.0));
            const int tau_max = static_cast<int>(sr_ / f0_min_);''')

patch('native/src/dsp.cpp', '''        double f0 = static_cast<double>(sr_dec_) / tau;
        if (!(60.0 <= f0 && f0 <= 1200.0)) f0 = 0.0;
        last_f0_ = f0;
        return f0;''',
'''        double f0 = static_cast<double>(sr_dec_) / tau;
        if (!(f0_min_ <= f0 && f0 <= 1200.0)) f0 = 0.0;
        last_f0_ = f0;
        return f0;''')
patch('native/src/dsp.cpp', '''            const double f0c = static_cast<double>(sr_) / tau_c;
            last_f0_ = (60.0 <= f0c && f0c <= 1200.0) ? f0c : 0.0;
            return last_f0_;''',
'''            const double f0c = static_cast<double>(sr_) / tau_c;
            last_f0_ = (f0_min_ <= f0c && f0c <= 1200.0) ? f0c : 0.0;
            return last_f0_;''')
patch('native/src/dsp.cpp', '''        double f0 = static_cast<double>(sr_) / tau_host;
        if (!(60.0 <= f0 && f0 <= 1200.0)) f0 = 0.0;
        last_f0_ = f0;
        return f0;
    }
    static int decimationFor(int sr) {''',
'''        double f0 = static_cast<double>(sr_) / tau_host;
        if (!(f0_min_ <= f0 && f0 <= 1200.0)) f0 = 0.0;
        last_f0_ = f0;
        return f0;
    }
    static int decimationFor(int sr) {''')

patch('native/src/dsp.cpp', '''    int decimation() const { return decim_; }
    int controlRate() const { return sr_dec_; }''',
'''    int decimation() const { return decim_; }
    int controlRate() const { return sr_dec_; }
    /// R21: the configured lower validity gate (Hz).
    double f0Min() const { return f0_min_; }''')

patch('native/src/dsp.cpp', '''        m.det.configure(static_cast<int>(cfg.sample_rate),
                        cfg.detector_mode);  // same sizes: no realloc''',
'''        m.det.configure(static_cast<int>(cfg.sample_rate),
                        cfg.detector_mode, cfg.f0_min);  // same sizes: no realloc''')
patch('native/src/dsp.cpp', '''        impl_->det.configure(static_cast<int>(cfg.sample_rate),
                              cfg.detector_mode);''',
'''        impl_->det.configure(static_cast<int>(cfg.sample_rate),
                              cfg.detector_mode, cfg.f0_min);''')

patch('native/src/dsp.cpp', '''    if (!(cfg.fmin > 0.0) || !(cfg.fmax > cfg.fmin)) return fail();''',
'''    if (!(cfg.fmin > 0.0) || !(cfg.fmax > cfg.fmin)) return fail();
    // R21: the detector's lower validity gate. [20, 200] Hz covers the
    // low-note investigation candidates (32.7/41.2/55/60/80/110) with
    // margin; must stay below fmax so the lag domain is non-degenerate.
    if (!(cfg.f0_min >= 20.0) || !(cfg.f0_min <= 200.0) ||
        !std::isfinite(cfg.f0_min) || !(cfg.f0_min < cfg.fmax))
        return fail();''')

patch('native/src/dsp.cpp', '''        impl_->cfg.detection_interval == cfg.detection_interval &&
        impl_->cfg.detector_mode == cfg.detector_mode) {''',
'''        impl_->cfg.detection_interval == cfg.detection_interval &&
        impl_->cfg.detector_mode == cfg.detector_mode &&
        impl_->cfg.f0_min == cfg.f0_min) {''')

# member: add next to the private members (find "int mode_ = 0;")
import re
src = open('native/src/dsp.cpp').read()
if 'double f0_min_ = 60.0;' not in src:
    m = re.search(r"(int mode_ = 0;\n    int sr_ = 48000;)", src)
    if m:
        src = src.replace(m.group(1), m.group(1) + "\n    double f0_min_ = 60.0;  // R21: lower validity gate (default = legacy)")
        open('native/src/dsp.cpp', 'w').write(src)
        print("  patched member f0_min_")
    else:
        # fallback: insert after "int sr_ = 48000;" anywhere
        m2 = re.search(r"(int sr_ = 48000;)", src)
        assert m2, "sr_ member not found"
        src = src.replace(m2.group(1), m2.group(1) + "\n    double f0_min_ = 60.0;  // R21: lower validity gate (default = legacy)", 1)
        open('native/src/dsp.cpp', 'w').write(src)
        print("  patched member f0_min_ (fallback)")
else:
    print("  already patched member f0_min_")

# ---------------------------------------------------------------- r20_dualmono comment
patch('native/cli/r20_dualmono.cpp', '''// SCENARIOS (each at 48 kHz and 96 kHz, both Lagrange6 and ObsKernel6):
//   T1  dual_mono_1v_equal    L==R, 1 voice, all delays equal, fb=0,
//                             spread off, wet=1 -> EXPECT bit-identical L/R
//   T2  dual_mono_2v_equal    L==R, 2 voices (identical pitch ratio),
//                             equal per-voice delays -> EXPECT bit-identical
//   T3  dual_mono_8v_equal    L==R, 8 voices equal -> EXPECT bit-identical''',
'''// SCENARIOS (each at 48 kHz and 96 kHz, both Lagrange6 and ObsKernel6):
//   [R21 CORRECTION of an inaccurate R20 comment: these scenarios do NOT
//   set the per-voice base delays equal -- they run the engine on the
//   CORE DEFAULT table base_delay = 256 + 32*v samples (v = voice index),
//   i.e. DISTINCT per-voice delays (v0=256, v1=288, ... = 32-sample
//   inter-voice spacing = 0.667 ms @48k / 0.333 ms @96k). "equal" in the
//   scenario names below refers to the IDENTICAL L/R INPUT AND CHANNEL
//   STATE, and (with spread off) the identical per-voice PITCH ratio --
//   the property under test is channel-pair transparency, not inter-voice
//   delay equality. The equal-vs-distinct base-delay axis is measured
//   explicitly by the R21 successor tool r21_stereo.cpp.]
//   T1  dual_mono_1v_equal    L==R, 1 voice, default delays, fb=0,
//                             spread off, wet=1 -> EXPECT bit-identical L/R
//   T2  dual_mono_2v_equal    L==R, 2 voices (identical pitch ratio,
//                             DISTINCT default per-voice delays) -> EXPECT
//                             bit-identical
//   T3  dual_mono_8v_equal    L==R, 8 voices (same law) -> EXPECT
//                             bit-identical''')

patch('native/cli/r20_dualmono.cpp', '''    // all per-voice base delays EQUAL (the core default table), gains 1''',
'''    // [R21 CORRECTION: the core default per-voice base-delay table is
    // 256 + 32*v samples -- DISTINCT per voice (NOT equal; the inaccurate
    // R20 comment here said "equal"). No setVoiceBaseDelay call is made, so
    // these scenarios run the distinct default table; per-voice gains 1.]''')

# ---------------------------------------------------------------- wav_io float32
patch('native/tests/wav_io.cpp', '''    if (audio_fmt != 1 || bits == -1 || channels <= 0)
        throw std::runtime_error(path + ": unsupported/missing fmt chunk");
    if (bits != 16 && bits != 24 && bits != 32)
        throw std::runtime_error(path + ": unsupported sample width");''',
'''    // R21: accept PCM (tag 1, 16/24/32-bit int) AND IEEE-float (tag 3,
    // 32-bit) -- DAW-exported float WAVs are common in the real-audio
    // comparison harness. Harness-only extension.
    if ((audio_fmt != 1 && audio_fmt != 3) || bits == -1 || channels <= 0)
        throw std::runtime_error(path + ": unsupported/missing fmt chunk");
    if (audio_fmt == 3 && bits != 32)
        throw std::runtime_error(path + ": unsupported float sample width");
    if (audio_fmt == 1 && bits != 16 && bits != 24 && bits != 32)
        throw std::runtime_error(path + ": unsupported sample width");''')

patch('native/tests/wav_io.cpp', '''        } else {
            const uint32_t u = rd32(p);
            int32_t s = static_cast<int32_t>(u);
            v = static_cast<double>(s) / 2147483648.0;
            p += 4;
        }''',
'''        } else if (audio_fmt == 3) {
            // IEEE 754 float32 (tag 3): the bits ARE the sample.
            uint32_t u = rd32(p);
            float fv;
            std::memcpy(&fv, &u, 4);
            v = static_cast<double>(fv);
            p += 4;
        } else {
            const uint32_t u = rd32(p);
            int32_t s = static_cast<int32_t>(u);
            v = static_cast<double>(s) / 2147483648.0;
            p += 4;
        }''')

patch('native/tests/wav_io.hpp', '''/// Read a PCM WAV (16/24/32-bit). Returns interleaved samples [n * ch] in
/// [-1, 1) and the sample rate. Throws std::runtime_error on bad input.''',
'''/// Read a PCM WAV (16/24/32-bit int) or IEEE-float WAV (32-bit, tag 3;
/// R21). Returns interleaved samples [n * ch] in [-1, 1) (int) / verbatim
/// (float) and the sample rate. Throws std::runtime_error on bad input.''')

# ---------------------------------------------------------------- wrapper tests
patch('native/vst3/octavox_vst3_tests.cpp', '''        const int32_t dmode = *reinterpret_cast<const int32_t*>(&good[120]);
        check(c, dmode == static_cast<int>(octavox::DetectorMode::Decimated),
              "blob detector_mode offset self-check failed (R19 v2)");''',
'''        const int32_t dmode = *reinterpret_cast<const int32_t*>(&good[120]);
        // R21: the blob carries THIS BUILD's compile-time detector selection
        // (variant-authoritative restore), not a hardcoded mode.
        check(c, dmode == static_cast<int>(octavox::vst3wrap::build::kDefaultDetectorMode),
              "blob detector_mode offset self-check failed (R19 v2/R21: must "
              "equal this build's selection)");''')

print("reapply_patch.py: ALL PATCHES APPLIED")
