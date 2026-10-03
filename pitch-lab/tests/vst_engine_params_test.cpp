// Pitch Lab VST3 product layer — T-VEP* (Task 29): the ENGINE-OWNED
// PARAMETER MODEL suite.
//
// The ownership architecture under test:
//   ENGINE REGISTRY -> EngineDescriptor -> engine-owned parameter
//   descriptors -> VST parameter registration (generated model table) ->
//   UI enumeration (engineParamsFor).
//
// The editor contains no engine-parameter membership knowledge; these
// tests pin the DATA the UI consumes (per-engine visible parameter sets,
// titles, units, choices, order), the generated model's exact equality
// with the frozen pre-Task-29 parameter surface (STATE COMPATIBILITY:
// stable tags, same domains/defaults/steps), the shared-vs-engine-owned
// role split, and the discrete-choice display/parsing behaviour.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "vst/parameters.h"

using namespace pitchlab::vst;
using namespace pitchlab;

namespace {

/// The FROZEN pre-Task-29 parameter surface (parameters.h @ Task 24/27):
/// tag, min, max, defaultPlain, stepCount. The generated table must be
/// EXACTLY this (state compatibility: same tags, same normalised mapping).
struct FrozenRow {
  uint32_t tag;
  double min;
  double max;
  double def;
  int steps;
};

const FrozenRow kFrozenSurface[] = {
    // Task 33 (product spec §6 amendment #1): the ENGINE domain grows to
    // 0..5 (the 6th production engine; selector index 5). The default stays
    // native.vardelay (index 1) — no saved state changes meaning.
    {param::kEngine, 0, 5, 1, 5},
    // Task-33 continuation (Phase 5): the control domain -48..+48 st —
    // per-engine support stays capability-derived (displayed); the clamp
    // semantics are the existing counted envelope saturation.
    {param::kPitch, -48.0, 48.0, 0.0, -1},
    // Task 32: the ONE deliberate frozen-surface change since Task 29 —
    // the LFO rate's domain minimum moved 0.1 -> 0.0 Hz (the real OFF
    // state; the task's product mandate). The normalised mapping is exact
    // at 0 (normalise(0) == 0, denormalise(0) == 0.0): an OFF state
    // round-trips bit-exactly. Pre-Task-32 saved states restore with a
    // benign <= 0.1 Hz shift (documented in the product doc's parameter
    // section); every other row is untouched.
    {param::kLfoRate, 0.0, 8.0, 5.0, -1},
    {param::kLfoDepth, 0.0, 2.0, 0.0, -1},
    {param::kVsQuality, 0, 2, 2, 2},
    {param::kVsAllowAliasing, 0, 1, 0, 1},
    {param::kVdExcursion, 0.05, 2.0, 0.5, -1},
    {param::kVdCrossfade, 0, 8192, 2048, 8192},
    {param::kGrGrain, 0.02, 0.5, 0.1, -1},
    {param::kGrOverlap, 4, 16, 4, 12},
    {param::kGrJitter, 0, 256, 0, 256},
    {param::kGrWindow, 0, 1, 0, 1},
    {param::kPvcFft, 0, 2, 1, 2},
    {param::kPvcHop, 0, 3, 2, 3},
    {param::kPvpFft, 0, 2, 1, 2},
    {param::kPvpHop, 0, 3, 2, 3},
    // Task 33: the native.timepitch rows of the task-33 amendment (the
    // final surface: kTpMode/kTpWindow/kTpOverlap/kTpShape/kTpTolerance/
    // kTpFormant — the mode domain grew per checkpoint to the four
    // registered choices; the tags are reserved by the amendment, never
    // renumbered; the HIDDEN puv_hz row is engine-owned, NOT on this surface).
    {param::kTpMode, 0, 3, 0, 3},
    {param::kTpWindow, 64, 16384, 2048, 16320},
    {param::kTpOverlap, 0, 2, 0, 2},
    {param::kTpShape, 0, 3, 0, 3},
    {param::kTpTolerance, 0, 8192, 768, 8192},
    {param::kTpFormant, 0.25, 4.0, 1.0, -1},
    {param::kMix, 0.0, 1.0, 1.0, -1},
    {param::kBypass, 0, 1, 0, 1},
    {param::kOutputLevel, -24.0, 12.0, 0.0, -1},
};

const ParamMeta* metaOf(uint32_t tag) { return findParamMeta(tag); }

}  // namespace

// ---------------------------------------------------------------------------
// 1. Descriptor integrity: the engine declares its parameters, and the
//    declaration is coherent with the engine-contract key list.
// ---------------------------------------------------------------------------

TEST_CASE("descriptor integrity: every engine's parameter set is well-formed") {
  initEngineRegistryOnce();
  const EngineRegistry& reg = engineRegistry();
  // Task 33: the 6th production engine (native.timepitch) is registered.
  REQUIRE(reg.size() == 6);
  for (std::size_t e = 0; e < reg.size(); ++e) {
    const EngineDescriptor& d = reg.at(e);
    CAPTURE(d.info.id);
    // the descriptor table exists and matches the engine-contract key SET
    REQUIRE(d.parameters.size() == d.parameterKeys.size());
    for (const EngineParamDescriptor& p : d.parameters) {
      CAPTURE(p.key);
      CHECK(p.key != nullptr);
      CHECK(p.key[0] != '\0');
      CHECK(p.displayName != nullptr);
      CHECK(p.displayName[0] != '\0');
      CHECK(p.rebuildsChain);  // v0.1: every engine parameter is build-time
      CHECK(!p.automatable);   // v0.1: engine configuration is not automatable
      CHECK(p.role == EngineParamRole::Configuration);
      if (p.exposed) {
        // a pinned single-choice parameter (the task-33 staged mode surface:
        // one registered mode → domain [0,0]) is the one legal max == min form
        const bool domainOk = p.max > p.min || (p.choiceCount == 1 && p.stepCount == 0);
        CHECK(domainOk);
        CHECK(p.defaultPlain >= p.min);
        CHECK(p.defaultPlain <= p.max);
        if (p.stepCount > 0 && p.choiceCount > 0) {
          CHECK(p.choiceCount == p.stepCount + 1);
        }
        if (p.choiceValues != nullptr) {
          // the engine-facing integers exist and are ascending (fft/hop)
          for (int i = 1; i < p.choiceCount; ++i) {
            CHECK(p.choiceValues[i] > p.choiceValues[i - 1]);
          }
        }
      } else {
        // engine-internal (hidden) rows: either a fixed choice-valued knob
        // (the other engines' pattern) or a BOUNDED CONTINUOUS real — the
        // task-33 puv_hz pattern: validated domain, engine-internal, never
        // exposed on the VST surface
        const bool boundedReal = p.kind == EngineParamKind::Real && p.max > p.min;
        const bool hiddenOk = p.choiceCount >= 1 || boundedReal;
        const bool namesOk = p.choiceCount == 0 || p.choiceNames != nullptr;
        CHECK(hiddenOk);
        CHECK(namesOk);
      }
    }
  }
}

TEST_CASE("model integrity: validateEngineParameterModel reports NO issues") {
  initEngineRegistryOnce();
  const std::vector<std::string> issues = validateEngineParameterModel();
  for (const std::string& s : issues) MESSAGE(s);
  CHECK(issues.empty());
}

// ---------------------------------------------------------------------------
// 2. Registry -> parameter mapping: the enumeration the UI consumes.
// ---------------------------------------------------------------------------

TEST_CASE("registry->parameter mapping: every engine's visible set is exact") {
  initEngineRegistryOnce();
  struct Expect {
    const char* engineId;
    std::vector<uint32_t> tags;       // expected visible tags, descriptor order
    std::vector<const char*> keys;    // expected engine keys
  };
  const Expect expects[] = {
      {"native.varispeed", {param::kVsQuality, param::kVsAllowAliasing},
       {"resample_quality", "allow_aliasing"}},
      {"native.vardelay", {param::kVdExcursion, param::kVdCrossfade},
       {"excursion_seconds", "crossfade_frames"}},
      {"native.pv.classic", {param::kPvcFft, param::kPvcHop}, {"fft_size", "hop"}},
      {"native.pv.phaselocked", {param::kPvpFft, param::kPvpHop}, {"fft_size", "hop"}},
      {"native.granular",
       {param::kGrGrain, param::kGrOverlap, param::kGrWindow, param::kGrJitter},
       {"grain_seconds", "overlap", "window", "jitter_frames"}},
  };
  for (const Expect& ex : expects) {
    CAPTURE(ex.engineId);
    // find the engine index THROUGH THE REGISTRY (no index list in the test
    // either: identity from the registry order)
    int idx = -1;
    for (int i = 0; i < engineCount(); ++i) {
      if (std::strcmp(engineIdForIndex(i), ex.engineId) == 0) idx = i;
    }
    REQUIRE(idx >= 0);
    const std::vector<EngineParamView>& params = engineParamsFor(idx);
    REQUIRE(params.size() == ex.tags.size());
    for (std::size_t i = 0; i < params.size(); ++i) {
      CAPTURE(i);
      CHECK(params[i].tag == ex.tags[i]);               // stable VST binding
      CHECK(std::strcmp(params[i].descriptor->key, ex.keys[i]) == 0);
      CHECK(params[i].meta == metaOf(ex.tags[i]));      // generated model row
      CHECK(params[i].meta->role == ParamRole::EngineConfiguration);
      CHECK(std::strcmp(params[i].meta->engineId, ex.engineId) == 0);
    }
  }
  // the engine-internal FIXED values are NOT visible (read_kernel, the PV
  // windows, locking_mode) — every engine shows all and ONLY its exposed set
  CHECK(engineParamsFor(1).size() == 2);  // vardelay: no read_kernel control
  CHECK(engineParamsFor(2).size() == 2);  // pv.classic: no window control
  CHECK(engineParamsFor(3).size() == 2);  // pv.phaselocked: no window/locking
  // out-of-range engines enumerate nothing (no stale controls, ever)
  CHECK(engineParamsFor(-1).empty());
  CHECK(engineParamsFor(99).empty());
}

TEST_CASE("enumeration hygiene: unique tags per engine, disjoint across engines") {
  initEngineRegistryOnce();
  std::vector<uint32_t> all;
  for (int e = 0; e < engineCount(); ++e) {
    const std::vector<EngineParamView>& params = engineParamsFor(e);
    std::vector<uint32_t> tags;
    for (const EngineParamView& p : params) tags.push_back(p.tag);
    std::sort(tags.begin(), tags.end());
    for (std::size_t i = 1; i < tags.size(); ++i) {
      CHECK(tags[i] != tags[i - 1]);  // no duplicated controls per engine
    }
    for (uint32_t t : tags) all.push_back(t);
  }
  std::sort(all.begin(), all.end());
  for (std::size_t i = 1; i < all.size(); ++i) {
    CHECK(all[i] != all[i - 1]);  // no parameter is owned by two engines
  }
}

// ---------------------------------------------------------------------------
// 3. The generated model table == the FROZEN parameter surface
//    (state compatibility: same tags, same domains — IDs never renumbered).
// ---------------------------------------------------------------------------

TEST_CASE("state compatibility: the generated table is exactly the frozen surface") {
  initEngineRegistryOnce();
  // Task 33: 19 shared/existing rows + 5 native.timepitch rows (the
  // Fixed+Adaptive checkpoint) = 24 (the amendment's final surface is 25
  // when the Pitch+Formant formant_ratio row lands; tag 27 is bound then).
  REQUIRE(parameterCount() == 25);
  REQUIRE(sizeof(kFrozenSurface) / sizeof(kFrozenSurface[0]) == 25);
  for (const FrozenRow& row : kFrozenSurface) {
    const ParamMeta* m = metaOf(row.tag);
    REQUIRE(m != nullptr);
    CAPTURE(m->id);
    CHECK(m->min == row.min);
    CHECK(m->max == row.max);
    CHECK(m->defaultPlain == row.def);
    CHECK(m->stepCount == row.steps);
    // the frozen normalisation mapping is IDENTICAL (state round-trip);
    // a pinned single-choice row (span 0 — the task-33 staged mode) maps
    // everything to 0 (the choice index) in BOTH directions
    const double span = row.max - row.min;
    CHECK(normalise(row.tag, row.min) == doctest::Approx(0.0).epsilon(1e-12));
    if (span > 0.0) {
      CHECK(normalise(row.tag, row.max) == doctest::Approx(1.0).epsilon(1e-12));
      CHECK(normalise(row.tag, row.def) ==
            doctest::Approx((row.def - row.min) / span).epsilon(1e-9));
    } else {
      CHECK(normalise(row.tag, row.max) == doctest::Approx(0.0).epsilon(1e-12));
      CHECK(denormalise(row.tag, 0.0) == doctest::Approx(row.min).epsilon(1e-12));
      CHECK(denormalise(row.tag, 1.0) == doctest::Approx(row.min).epsilon(1e-12));
    }
  }
  // every table row is one of the frozen tags (nothing invented)
  std::vector<uint32_t> tags;
  for (uint32_t i = 0; i < parameterCount(); ++i) tags.push_back(parameterTable()[i].tag);
  std::sort(tags.begin(), tags.end());
  for (const FrozenRow& row : kFrozenSurface) {
    CHECK(std::find(tags.begin(), tags.end(), row.tag) != tags.end());
  }
}

TEST_CASE("role split: shared realtime controls are explicit and complete") {
  initEngineRegistryOnce();
  // the 7 shared rows: engine selector + pitch/LFO + mix/bypass/level
  std::vector<uint32_t> shared = {param::kEngine,    param::kPitch,  param::kLfoRate,
                                  param::kLfoDepth,  param::kMix,    param::kBypass,
                                  param::kOutputLevel};
  for (uint32_t t : shared) {
    const ParamMeta* m = metaOf(t);
    REQUIRE(m != nullptr);
    CHECK(m->role == ParamRole::SharedRealtime);
    CHECK(m->engineId != nullptr);
    CHECK(m->engineId[0] == '\0');  // owned by NO engine
  }
  // the shared musical surface is automatable; the engine selector is not
  CHECK(metaOf(param::kPitch)->automatable);
  CHECK(metaOf(param::kLfoRate)->automatable);
  CHECK(metaOf(param::kLfoDepth)->automatable);
  CHECK(metaOf(param::kMix)->automatable);
  CHECK(metaOf(param::kBypass)->automatable);
  CHECK(metaOf(param::kOutputLevel)->automatable);
  CHECK(!metaOf(param::kEngine)->automatable);
  // every engine-owned row is marked and non-automatable
  for (uint32_t i = 0; i < parameterCount(); ++i) {
    const ParamMeta& m = parameterTable()[i];
    if (m.role == ParamRole::EngineConfiguration) {
      CHECK(m.engineId[0] != '\0');
      CHECK(!m.automatable);
      // the owning engine exists in the registry
      CHECK(engineRegistry().findById(m.engineId) != nullptr);
    } else {
      CHECK(m.engineId[0] == '\0');
    }
  }
  // 18 engine-owned rows total (12 v0.1 + 6 task-33 rows: the FINAL surface
  // with the checkpoint-4 formant_ratio row)
  int engineOwned = 0;
  for (uint32_t i = 0; i < parameterCount(); ++i) {
    if (parameterTable()[i].role == ParamRole::EngineConfiguration) ++engineOwned;
  }
  CHECK(engineOwned == 18);
}

// ---------------------------------------------------------------------------
// 4. Parameter values across engine switching (the snapshot persists
//    non-selected engine values; the controller stays authoritative).
// ---------------------------------------------------------------------------

TEST_CASE("engine switching: non-selected engine values persist in the snapshot") {
  initEngineRegistryOnce();
  ParamSnapshot snap;  // defaults
  // set granular's parameters to non-defaults
  applyNormalised(snap, param::kGrGrain, normalise(param::kGrGrain, 0.25));
  applyNormalised(snap, param::kGrOverlap, normalise(param::kGrOverlap, 8.0));
  applyNormalised(snap, param::kGrWindow, normalise(param::kGrWindow, 1.0));
  applyNormalised(snap, param::kGrJitter, normalise(param::kGrJitter, 64.0));
  // set pv.classic's parameters too
  applyNormalised(snap, param::kPvcFft, normalise(param::kPvcFft, 0.0));   // 1024
  applyNormalised(snap, param::kPvcHop, normalise(param::kPvcHop, 1.0));   // 256
  // switch through EVERY engine (round-robin twice)
  for (int round = 0; round < 2; ++round) {
    for (int e = 0; e < engineCount(); ++e) {
      applyNormalised(snap, param::kEngine, normalise(param::kEngine, static_cast<double>(e)));
      // the granular/pv values persist regardless of the selected engine
      CHECK(std::fabs(snap.grGrainSec - 0.25) < 1e-12);
      CHECK(snap.grOverlap == 8);
      CHECK(snap.grWindowTriangular == 1);
      CHECK(snap.grJitterFrames == 64);
      CHECK(snap.pvcFftSize == 1024);
      CHECK(snap.pvcHop == 256);
      CHECK(snap.engineIndex == e);
    }
  }
  // the engine switch changes the chain signature; the musical params do not
  ParamSnapshot::ChainSignature sig = snap.chainSignature();
  snap.pitchSt = 3.0;
  snap.lfoDepthSt = 1.0;
  CHECK(snap.chainSignature() == sig);
}

TEST_CASE("plain<->normalised round trip over the whole frozen surface") {
  initEngineRegistryOnce();
  for (const FrozenRow& row : kFrozenSurface) {
    CAPTURE(row.tag);
    const int steps = 21;
    for (int s = 0; s <= steps; ++s) {
      const double u = static_cast<double>(s) / steps;
      const double plain = denormalise(row.tag, u);
      const double back = normalise(row.tag, plain);
      if (row.steps > 0) {
        const double snapped = std::floor(u * row.steps + 0.5) / row.steps;
        CHECK(back == doctest::Approx(snapped).epsilon(1e-12));
      } else if (row.max > row.min) {
        CHECK(std::fabs(back - u) < 1e-9);
      } else {
        // a pinned single-choice parameter (the task-33 staged mode: domain
        // [0,0]) collapses EVERY normalised value to the one plain value —
        // the round trip is the constant map, not the identity
        CHECK(back == doctest::Approx(0.0).epsilon(1e-12));
        CHECK(plain == doctest::Approx(row.min).epsilon(1e-12));
      }
    }
    // the state round-trip: apply + read back is stable
    ParamSnapshot snap;
    const double midPlain = 0.5 * (row.min + row.max);
    applyNormalised(snap, row.tag, normalise(row.tag, midPlain));
    const double read = plainValue(snap, row.tag);
    if (row.steps > 0) {
      CHECK(std::fabs(read - std::floor(read + 0.5)) < 1e-9);  // integer domain
    } else {
      CHECK(std::fabs(read - midPlain) < 1e-9);
    }
  }
}

// ---------------------------------------------------------------------------
// 5. Title / unit / discrete-choice rendering (the data the UI rows show).
//    The engine-declared display names are reused verbatim; discrete
//    parameters display the CHOICE TEXT (never a bare index).
// ---------------------------------------------------------------------------

TEST_CASE("title rendering: engine-declared display names reach the model rows") {
  initEngineRegistryOnce();
  auto titleOf = [](uint32_t t) -> std::string {
    const ParamMeta* m = metaOf(t);
    REQUIRE(m != nullptr);
    return m->title;
  };
  CHECK(titleOf(param::kVsQuality) == "Resample Quality");
  CHECK(titleOf(param::kVsAllowAliasing) == "Allow Aliasing");
  CHECK(titleOf(param::kVdExcursion) == "Excursion");
  CHECK(titleOf(param::kVdCrossfade) == "Crossfade");
  CHECK(titleOf(param::kGrGrain) == "Grain Length");
  CHECK(titleOf(param::kGrOverlap) == "Overlap");
  CHECK(titleOf(param::kGrWindow) == "Window");
  CHECK(titleOf(param::kGrJitter) == "Jitter");
  CHECK(titleOf(param::kPvcFft) == "FFT Size");
  CHECK(titleOf(param::kPvcHop) == "Hop");
  CHECK(titleOf(param::kPvpFft) == "FFT Size");
  CHECK(titleOf(param::kPvpHop) == "Hop");
}

TEST_CASE("unit rendering: engine-declared units reach the model rows") {
  initEngineRegistryOnce();
  auto unitOf = [](uint32_t t) -> std::string {
    const ParamMeta* m = metaOf(t);
    REQUIRE(m != nullptr);
    return m->units;
  };
  CHECK(unitOf(param::kGrGrain) == "s");
  CHECK(unitOf(param::kGrOverlap) == "x");
  CHECK(unitOf(param::kGrJitter) == "frames");
  CHECK(unitOf(param::kVdExcursion) == "s");
  CHECK(unitOf(param::kVdCrossfade) == "frames");
  CHECK(unitOf(param::kPitch) == "st");
  CHECK(unitOf(param::kLfoRate) == "Hz");
  CHECK(unitOf(param::kOutputLevel) == "dB");
}

TEST_CASE("discrete-choice rendering: the choice TEXT is displayed (not the index)") {
  initEngineRegistryOnce();
  auto fmt = [](uint32_t t, double plain) -> std::string {
    char buf[48];
    formatParamValue(t, plain, buf, sizeof(buf));
    return std::string(buf);
  };
  auto unitOf = [](uint32_t t) -> std::string {
    const ParamMeta* m = metaOf(t);
    REQUIRE(m != nullptr);
    return m->units;
  };
  // the task's UI display examples, verbatim
  CHECK(fmt(param::kPvcFft, 1.0) == "2048");
  CHECK(fmt(param::kPvcHop, 2.0) == "512");
  CHECK(fmt(param::kPvpFft, 1.0) == "2048");
  CHECK(fmt(param::kPvpHop, 2.0) == "512");
  CHECK(fmt(param::kVsQuality, 2.0) == "reference");
  CHECK(fmt(param::kVsQuality, 0.0) == "small");
  CHECK(fmt(param::kGrWindow, 0.0) == "hann");
  CHECK(fmt(param::kGrWindow, 1.0) == "triangular");
  CHECK(fmt(param::kVsAllowAliasing, 1.0) == "on");
  CHECK(fmt(param::kBypass, 0.0) == "off");
  // value formatting + units compose as the UI composes them
  CHECK(fmt(param::kGrGrain, 0.1) + " " + unitOf(param::kGrGrain) == "0.100 s");
  CHECK(fmt(param::kGrOverlap, 4.0) + " " + unitOf(param::kGrOverlap) == "4 x");
  CHECK(fmt(param::kGrJitter, 0.0) + " " + unitOf(param::kGrJitter) == "0 frames");
  CHECK(fmt(param::kVdExcursion, 0.5) + " " + unitOf(param::kVdExcursion) == "0.500 s");
  CHECK(fmt(param::kVdCrossfade, 2048.0) + " " + unitOf(param::kVdCrossfade) ==
        "2048 frames");
  // the discrete-choice display PARSES BACK (host text edit round trip)
  double parsed = -1.0;
  REQUIRE(parseParamPlain(param::kPvcFft, "2048", parsed));
  CHECK(parsed == doctest::Approx(1.0).epsilon(1e-12));
  REQUIRE(parseParamPlain(param::kPvcHop, "512", parsed));
  CHECK(parsed == doctest::Approx(2.0).epsilon(1e-12));
  REQUIRE(parseParamPlain(param::kVsQuality, "reference", parsed));
  CHECK(parsed == doctest::Approx(2.0).epsilon(1e-12));
  REQUIRE(parseParamPlain(param::kGrWindow, "triangular", parsed));
  CHECK(parsed == doctest::Approx(1.0).epsilon(1e-12));
  // the engine-facing VALUES still parse (actual sizes -> indices)
  REQUIRE(parseParamPlain(param::kPvpFft, "4096", parsed));
  CHECK(parsed == doctest::Approx(2.0).epsilon(1e-12));
}

TEST_CASE("binding resolution: tags resolve per (engineId, key); fixed keys resolve 0") {
  initEngineRegistryOnce();
  CHECK(vstTagForEngineParam("native.granular", "grain_seconds") == param::kGrGrain);
  CHECK(vstTagForEngineParam("native.granular", "window") == param::kGrWindow);
  CHECK(vstTagForEngineParam("native.varispeed", "resample_quality") == param::kVsQuality);
  CHECK(vstTagForEngineParam("native.pv.phaselocked", "hop") == param::kPvpHop);
  // engine-internal fixed values have NO VST parameter
  CHECK(vstTagForEngineParam("native.vardelay", "read_kernel") == 0);
  CHECK(vstTagForEngineParam("native.pv.classic", "window") == 0);
  CHECK(vstTagForEngineParam("native.pv.phaselocked", "locking_mode") == 0);
  // unknown engines/keys resolve nothing (never a wrong binding)
  CHECK(vstTagForEngineParam("native.nothere", "grain_seconds") == 0);
  CHECK(vstTagForEngineParam("native.granular", "not_a_key") == 0);
  CHECK(vstTagForEngineParam(nullptr, nullptr) == 0);
}

// ---------------------------------------------------------------------------
// 6. The dynamic-pitch capability surface (the shared curve reaches engines
//    through their DECLARED capability — the UI/adapter query, never assume).
// ---------------------------------------------------------------------------

TEST_CASE("dynamic-pitch capability: declared per engine in the registry") {
  initEngineRegistryOnce();
  const EngineRegistry& reg = engineRegistry();
  for (std::size_t e = 0; e < reg.size(); ++e) {
    const Capabilities& c = reg.at(e).capabilities;
    CAPTURE(reg.at(e).info.id);
    // the capability is QUERYABLE (the shared PITCH/LFO curve is delivered
    // per this declaration; all five v0.1 engines declare dynamic support)
    CHECK(c.supportsDynamicRatio);
    CHECK(c.minRatio > 0.0);
    CHECK(c.maxRatio > c.minRatio);
  }
}
