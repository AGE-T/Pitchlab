#!/usr/bin/env python3
"""R21 re-apply part 2: the wrapper test case 21 + native CMakeLists tool
registration. Idempotent. Run from the tree root."""
import sys

def patch(path, old, new):
    src = open(path).read()
    if old in src:
        open(path, 'w').write(src.replace(old, new, 1))
        print(f"  patched {path}")
    elif new[:80] in src:
        print(f"  already patched {path}")
    else:
        print(f"  FAILED: pattern not found in {path}")
        sys.exit(1)

CASE = '''//==========================================================================
// R21 case 21: product identity, approved parameter-title corrections,
// help-text metadata, and variant authority over saved state (both the
// interpolation and the detector selection).
//==========================================================================
static void caseIdentityR21(CaseResult& c) {
    using octavox::vst3wrap::paramSpec;
    using octavox::vst3wrap::paramHelp;
    using octavox::vst3wrap::ParamId;
    namespace wrap = octavox::vst3wrap;

    // (a) P0 rebrand: no "Octavox" in the user-facing product strings; the
    //     neutral vendor; deliberately EMPTY contact fields.
    {
        const char* prod = wrap::kPluginName;
        check(c, std::strstr(prod, "PitchLab Harmonizer") != nullptr,
              "display name must carry the 'PitchLab Harmonizer' identity");
        check(c, std::strstr(prod, "Octavox") == nullptr,
              "user-facing display name must not contain 'Octavox' (R21 P0)");
        check(c, std::strcmp(wrap::kVendorName, "PitchLab Project") == 0,
              "vendor must be the neutral 'PitchLab Project'");
        check(c, wrap::kVendorWeb[0] == '\\0' && wrap::kVendorEmail[0] == '\\0',
              "optional contact fields must stay empty (no invented values)");
        check(c, std::strstr(prod, wrap::build::kInterpolationName) != nullptr,
              "display name must name the interpolation variant");
        check(c,
              std::strstr(prod, wrap::build::kDetectorModeName) != nullptr ||
                  std::strstr(prod, "Balanced") != nullptr,
              "display name must name the detector variant ('Balanced' = "
              "Decimated)");
        check(c, std::strstr(wrap::build::kBundleName, "PitchLab_") ==
                  wrap::build::kBundleName,
              "bundle base name must use the PitchLab_ prefix");
        check(c, wrap::build::bundleFileName().rfind("PitchLab_", 0) == 0 &&
                  wrap::build::bundleFileName().size() > 9,
              "bundle file name must be PitchLab_<Variant>.vst3");
    }

    // (b) Approved title corrections (R21 P0) with everything else about
    //     the three renamed specs byte-identical: domain, step, default,
    //     flags, unit id.
    {
        const auto& sp = paramSpec(ParamId::kSpread);
        check(c, std::strcmp(sp.title, "Harmony Map") == 0,
              "parameter 3 title must be 'Harmony Map' (was 'Spread')");
        check(c, std::strcmp(sp.shortTitle, "HarmMap") == 0,
              "parameter 3 short title must be 'HarmMap'");
        check(c, sp.domain.minPlain == 0.0 && sp.domain.maxPlain == 1.0 &&
                  sp.domain.stepPlain == 1.0 && sp.defaultPlain == 1.0 &&
                  sp.flags == ParameterInfo::kCanAutomate &&
                  sp.unitId == wrap::kUnitGlobal,
              "parameter 3 spec must be otherwise unchanged (bool, default "
              "ON, automate, Global unit)");
        const auto& pg = paramSpec(ParamId::kPitch);
        check(c, std::strcmp(pg.title, "Global Pitch Shift") == 0,
              "parameter 1 title must be 'Global Pitch Shift'");
        check(c, pg.domain.minPlain == -24.0 && pg.domain.maxPlain == 24.0 &&
                  pg.domain.stepPlain == 0.01 && pg.defaultPlain == 0.0,
              "parameter 1 spec must be otherwise unchanged");
        for (int v = 0; v < 8; ++v) {
            const auto& po = paramSpec(wrap::voiceOffsetId(v));
            char want[32];
            std::snprintf(want, sizeof(want), "Voice %d Transpose", v + 1);
            check(c, std::strcmp(po.title, want) == 0,
                  std::string("voice offset title mismatch at v=") +
                      std::to_string(v));
            check(c, po.domain.minPlain == -24.0 && po.domain.maxPlain == 24.0 &&
                      po.domain.stepPlain == 0.01 && po.defaultPlain == 0.0,
                  std::string("voice offset spec changed at v=") +
                      std::to_string(v));
        }
    }

    // (c) R21-P0 help text: non-empty for all 38 ids; the composition law
    //     and the Harmony Map semantics are stated.
    {
        int empty = 0;
        for (uint32_t id = 0; id < 38u; ++id)
            if (!paramHelp(id) || paramHelp(id)[0] == '\\0') ++empty;
        addValue(c, "help_texts_empty", empty);
        check(c, empty == 0, "help text must be present for all 38 ids");
        check(c, std::strstr(paramHelp(ParamId::kSpread), "NOT stereo") != nullptr,
              "Harmony Map help must state it is not stereo widening");
        check(c, std::strstr(paramHelp(ParamId::kPitch), "EVERY voice") != nullptr,
              "Global Pitch Shift help must state the composition law");
        check(c, std::strstr(paramHelp(wrap::voiceOffsetId(0)), "ratios multiply") !=
                  nullptr,
              "voice Transpose help must state the composition law");
    }

    // (d) Variant authority over saved state: a blob carrying FOREIGN
    //     interpolation + detector values must restore every other field
    //     verbatim while the module's compile-time selections stay
    //     authoritative (neither is ever silently overridden by state).
    {
        using octavox::Interpolation;
        using octavox::DetectorMode;
        constexpr int32 kInterpOff = 44;   // R19 v2 blob layout (self-checked
        constexpr int32 kDetectOff = 120;  // by case_state_invalid_blobs)
        constexpr int32 kSurfaceOff = 152;

        MemoryStream state;
        float in[512]{}, out[512]{};
        {
            auto* p = makeProcessor(Setup{});
            standardSetup(p);
            check(c, p->setActive(true) == kResultOk, "activate failed");
            // a distinctive surface: voices=5, pitch=+3, feedback=0.25
            // (delivered through the parameter queue exactly like host
            // automation; picked up at the block boundary)
            ParameterChanges ch;
            addEvent(ch, ParamId::kVoices, 0,
                     wrap::normalizedFromPlain(ParamId::kVoices, 5.0));
            addEvent(ch, ParamId::kPitch, 0,
                     wrap::normalizedFromPlain(ParamId::kPitch, 3.0));
            addEvent(ch, ParamId::kFeedback, 0,
                     wrap::normalizedFromPlain(ParamId::kFeedback, 0.25));
            float* inP[2] = {in, in};
            float* outP[2] = {out, out};
            driveBlock(p, inP, outP, 2, 512, &ch);
            check(c, p->getState(&state) == kResultOk, "getState failed");
            check(c, p->setActive(false) == kResultOk, "deactivate failed");
            p->terminate();
            p->release();
        }
        const int L = static_cast<int>(state.getSize());
        check(c, L > 280, "implausibly small blob");
        std::vector<char> blob(L);
        {
            int64 spos = 0;
            state.seek(0, IBStream::kIBSeekSet, &spos);
            int32 got = 0;
            check(c, state.read(blob.data(), L, &got) == kResultOk && got == L,
                  "blob readback failed");
        }
        // the surface we set (spot fields)
        const double voicesSaved =
            *reinterpret_cast<const double*>(&blob[kSurfaceOff + 8 * 2]);
        const double pitchSaved =
            *reinterpret_cast<const double*>(&blob[kSurfaceOff + 8 * 1]);
        check(c, voicesSaved == 5.0 && pitchSaved == 3.0,
              "blob surface self-check failed");

        // tamper the two variant fields with FOREIGN-but-valid values
        const int32 foreignInterp =
            (wrap::build::kDefaultInterpolation == Interpolation::Lagrange6)
                ? static_cast<int32>(Interpolation::ObsKernel6)
                : static_cast<int32>(Interpolation::Lagrange6);
        const int32 foreignDet =
            (wrap::build::kDefaultDetectorMode == DetectorMode::Decimated)
                ? static_cast<int32>(DetectorMode::FullRate)
                : static_cast<int32>(DetectorMode::Decimated);
        *reinterpret_cast<int32_t*>(&blob[kInterpOff]) = foreignInterp;
        *reinterpret_cast<int32_t*>(&blob[kDetectOff]) = foreignDet;

        // restore into a fresh instance, re-save, verify authority + surface
        {
            auto* p = makeProcessor(Setup{});
            MemoryStream ms;
            int32 written = 0;
            ms.write(blob.data(), L, &written);
            int64 spos = 0;
            ms.seek(0, IBStream::kIBSeekSet, &spos);
            check(c, p->setState(&ms) == kResultOk,
                  "tampered-variant blob must still restore (authority)");

            MemoryStream out2;
            check(c, p->getState(&out2) == kResultOk, "re-save failed");
            const int L2 = static_cast<int>(out2.getSize());
            check(c, L2 == L, "blob size changed across the foreign restore");
            std::vector<char> blob2(L2);
            int64 spos2 = 0;
            out2.seek(0, IBStream::kIBSeekSet, &spos2);
            int32 got = 0;
            out2.read(blob2.data(), L2, &got);
            const int32 interpBack =
                *reinterpret_cast<const int32_t*>(&blob2[kInterpOff]);
            const int32 detBack =
                *reinterpret_cast<const int32_t*>(&blob2[kDetectOff]);
            addValue(c, "restored_interpolation", interpBack);
            addValue(c, "restored_detector", detBack);
            check(c,
                  interpBack == static_cast<int32>(wrap::build::kDefaultInterpolation),
                  "the BUILD's interpolation is authoritative on restore");
            check(c,
                  detBack == static_cast<int32>(wrap::build::kDefaultDetectorMode),
                  "the BUILD's detector is authoritative on restore");
            // surface fields round-trip verbatim
            const double voicesBack =
                *reinterpret_cast<const double*>(&blob2[kSurfaceOff + 8 * 2]);
            const double pitchBack =
                *reinterpret_cast<const double*>(&blob2[kSurfaceOff + 8 * 1]);
            check(c, voicesBack == 5.0 && pitchBack == 3.0,
                  "surface values must round-trip through the foreign restore");
            p->terminate();
            p->release();
        }
    }
}

'''

patch('native/vst3/octavox_vst3_tests.cpp',
      '''//==========================================================================
// main
//==========================================================================''',
      CASE + '''//==========================================================================
// main
//==========================================================================''')

patch('native/vst3/octavox_vst3_tests.cpp',
      '''        {"param_defaults_reinit_cycles", caseParamDefaultsReinitCycles},
    };''',
      '''        {"param_defaults_reinit_cycles", caseParamDefaultsReinitCycles},
        {"identity_variant_authority_r21", caseIdentityR21},
    };''')

# native CMakeLists: register the R21 CLI tools
patch('native/CMakeLists.txt', '''# R20-P3: the deterministic dual-mono / stereo integrity tool.
add_executable(octavox_r20_dualmono
    cli/r20_dualmono.cpp
)
target_link_libraries(octavox_r20_dualmono PRIVATE octavox_dsp)
if(NOT MSVC)
    target_compile_options(octavox_r20_dualmono PRIVATE -Wall -Wextra)
endif()
''',
'''# R20-P3: the deterministic dual-mono / stereo integrity tool.
add_executable(octavox_r20_dualmono
    cli/r20_dualmono.cpp
)
target_link_libraries(octavox_r20_dualmono PRIVATE octavox_dsp)
if(NOT MSVC)
    target_compile_options(octavox_r20_dualmono PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# R21 tools:
#  * r21_realaudio -- the real-audio (external WAV) three-detector
#    comparison harness (the synthetic R20 corpus stays a SEPARATE
#    category via octavox_r20_detcompare).
#  * r21_fmin -- the configurable minimum detection frequency battery
#    (Config::f0_min candidates 32.7/41.2/55/60/80/110 Hz).
#  * r21_stereo -- the deterministic stereo/voice-delay/harmony-map matrix
#    (the four distinguished behaviour classes; corrects the R20
#    "equal default delays" reading).
#  * r21_harmony -- the independent harmony mapper's reference-vector
#    verifier (docs/R21_HARMONY_MAPPER_SPEC.md).
# ---------------------------------------------------------------------------
add_executable(octavox_r21_realaudio
    cli/r21_realaudio.cpp
    tests/wav_io.cpp
)
target_link_libraries(octavox_r21_realaudio PRIVATE octavox_dsp)
if(NOT MSVC)
    target_compile_options(octavox_r21_realaudio PRIVATE -Wall -Wextra)
endif()

add_executable(octavox_r21_fmin
    cli/r21_fmin.cpp
)
target_link_libraries(octavox_r21_fmin PRIVATE octavox_dsp)
if(NOT MSVC)
    target_compile_options(octavox_r21_fmin PRIVATE -Wall -Wextra)
endif()

add_executable(octavox_r21_stereo
    cli/r21_stereo.cpp
    tests/wav_io.cpp
)
target_link_libraries(octavox_r21_stereo PRIVATE octavox_dsp)
if(NOT MSVC)
    target_compile_options(octavox_r21_stereo PRIVATE -Wall -Wextra)
endif()

add_executable(octavox_r21_harmony
    cli/r21_harmony.cpp
    src/harmony.cpp
)
target_include_directories(octavox_r21_harmony PRIVATE include src)
if(NOT MSVC)
    target_compile_options(octavox_r21_harmony PRIVATE -Wall -Wextra)
endif()
''')

print("reapply_patch2.py: ALL PATCHES APPLIED")
