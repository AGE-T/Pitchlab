#pragma once

// Pitch Lab — pitch curve model: spec parsing, validation, deterministic
// compilation to the dense signal (implementation specification §4.4;
// frozen implementation behaviour §4.4.3.1, recorded 2026-09-26 BEFORE
// coding, cycle 2).
//
// CONTRACT (v0.1):
//   * PitchCurveSpec  — authoritative TOML (experiments/curves/*.toml),
//     schema §15.3 + §4.4.3.1. Parsed + validated in one step; ANY
//     violation => ConfigError{file, field, reason} (§4.4.4 hard errors;
//     unknown keys rejected — typo protection).
//   * PitchCurveSignal — derived dense std::vector<double>, one ratio per
//     INPUT audio frame at the job's fs (§4.4.1 canonical representation:
//     linear pitch ratio, double, input-timeline indexed; ratio 1.0 =
//     identity; strictly positive and finite — post-validated §4.4.3.1
//     item 12). NEVER persisted as truth (hash lands in the manifest,
//     §17 step-3 machinery).
//   * Compilation is a pure function of (spec, sampleRate, totalFrames);
//     stochastic kinds reseed their consumer stream per compilation
//     (§4.7: makeConsumerStream(seed, "curve.random-walk")).
//   * Domain choices (frozen §4.4.3.1): ramp_lin/reversal interpolate
//     LINEARLY in the ratio domain; ramp_exp is geometric in the ratio
//     domain (endpoint form) or multiplicative-per-frame (rate form, the
//     §4.4.3 sentence verbatim); LFOs operate in the log2/semitone domain;
//     random walk = zero-order-hold steps clamped (not reflected) at the
//     declared bounds; saw LFO REQUIRES allow_discontinuity; the leading
//     uncovered region always holds the first defined value, the trailing
//     region follows `extend` (hold-last default / hold-first).
//
// The manifest-side signal hash and the engine-facing view/access-report
// (§4.4.5) are §17 step-3 machinery — deliberately not pre-built here.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/toml_lite.h"

namespace pitchlab {

/// An authored pitch value: { semitones = x } XOR { ratio = y } (§4.4.3.1
/// item 2). Resolved to a ratio via exp2(st/12) when authored in semitones.
struct CurveValue {
  double semitones = 0.0;
  double ratio = 1.0;
  bool isRatio = false;  // false => authored in semitones

  /// Resolve to a linear ratio (throws ConfigError on non-finite /
  /// non-positive results; `file`/`field` for the message).
  [[nodiscard]] double toRatio(const std::string& file, const std::string& field) const;
};

/// One breakpoint (kind "breakpoints"): time in seconds + target ratio +
/// the interpolation law of the segment ENDING here (§4.4.3.1 item 9).
struct CurveBreakPoint {
  double timeSec = 0.0;
  double ratio = 1.0;
  bool lawExp = false;  // false = "lin", true = "exp"
};

/// The parsed, validated curve spec (single struct for all kinds; fields
/// not used by a kind are defaults and are REJECTED at parse time when
/// present in the TOML for that kind — typo protection is total).
struct PitchCurveSpec {
  std::string id;
  std::string kind;  // static|ramp_lin|ramp_exp|lfo|random|reversal|breakpoints|external
  std::string file;  // source path (error messages, external resolution)

  CurveValue value;              // static
  CurveValue from, to;           // ramps / reversal
  double timeSec = 0.0;          // ramp/reversal window
  double holdSec = 0.0;          // pre-hold at `from` (default 0)
  double stps = 0.0;             // ramp_exp rate form: semitones/second
  bool hasRateForm = false;      // ramp_exp parameterization selector
  bool hasHold = false;

  std::string wave;              // lfo: sine|triangle|saw|random-walk
  double rateHz = 0.0;           // lfo: oscillation Hz; random: steps/second
  bool hasRateHz = false;
  CurveValue depth;              // lfo/random: semitones (REQUIRED authored in st)
  CurveValue centre;             // lfo (default identity) / random (REQUIRED)
  CurveValue minValue, maxValue; // random / lfo random-walk bounds (REQUIRED)

  std::vector<CurveBreakPoint> points;  // breakpoints

  std::string sourceFile;        // external: CSV file name
  std::string sourceMode;        // external: "dense" | "pairs"
  double denseRateHz = 0.0;      // external dense: declared rate
  bool hasDenseRate = false;

  uint64_t seed = 0;             // stochastic kinds (REQUIRED there)
  bool hasSeed = false;
  bool allowDiscontinuity = false;
  bool extendHoldLast = true;    // extend = "hold-last" (default) | "hold-first"
};

/// The compiled dense signal: one ratio per input frame (§4.4.2).
struct PitchCurveSignal {
  std::string specId;
  double sampleRate = 0.0;
  int64_t totalFrames = 0;
  std::vector<double> ratios;  // size == totalFrames, all finite and > 0
};

/// Parse + validate a curve spec TOML file (§15.3 schema + §4.4.3.1 exact
/// reading). Throws ConfigError{file, field, reason} on any violation
/// (syntax, unknown keys, per-kind missing/invalid fields, discontinuity
/// rule, seed rules).
[[nodiscard]] PitchCurveSpec parseCurveSpec(const std::filesystem::path& tomlFile);

/// Compile a validated spec into the dense signal at (sampleRate,
/// totalFrames). Pure + deterministic (stochastic kinds reseed their
/// consumer stream). Throws ConfigError on non-positive totalFrames and on
/// any post-compilation non-finite/non-positive frame (§4.4.3.1 item 12 —
/// the frame index is named in the reason).
[[nodiscard]] PitchCurveSignal compileCurveSignal(const PitchCurveSpec& spec,
                                                  double sampleRate, int64_t totalFrames);

/// Load every *.toml curve spec in `dir` (sorted by filename — deterministic
/// order). Throws ConfigError on any invalid file. Used by the battery gate
/// test and (later, §17 step 3) the ExperimentCompiler.
[[nodiscard]] std::vector<PitchCurveSpec> loadCurveBattery(const std::filesystem::path& dir);

}  // namespace pitchlab
