// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- independent harmony mapper (R21-P2)
// ============================================================================
// PROVENANCE: our own independent implementation of the harmonic MAPPING
// reconstructed from static analysis of the original Eventide Octavox
// binary (rounds 17/18 evidence; the original binary was NEVER executed).
// This module does NOT claim to reproduce the original's audio engine --
// it implements the recovered mapping RULES (documented with their exact
// instruction addresses in docs/R21_HARMONY_MAPPER_SPEC.md):
//
//   * the 5 temperament cent-tables           (.rdata 0x1803BB690, OBS)
//   * the 19-row scale-pattern table          (.rdata 0x1803BB780, OBS)
//   * the zone-run grid fill law              (0x1800E65B0..0x1800E6790, OBS)
//   * the 29-choice interval mux v=14+s+delta (records [45..47], OBS)
//   * the walk arithmetic (both directions)   (0x1800E6ED5..0x1800E7120, OBS)
//   * the key-ratio law  exp(+-cents[|k-9|])  (0x1800E6557..0x1800E6589, OBS)
//   * the chromatic/whole-tone raw-index switch (records [16..19], OBS)
//   * the authored scal-mux irregularity      (record [16], OBS)
//
// INDEPENDENT CONVENTIONS (documented differences, never claimed equal):
//   * output ratios are musical ratios with UNITY = 1.0 (the original's
//     internal page scale multiplies by 1/16 -- OBS at 0x1800E6F59;
//     reproduced nowhere here);
//   * the label/walk sign inversion is reproduced faithfully ("+" labels
//     walk DOWN, "-" labels walk UP -- the recovered sel = 14 - v law);
//     which direction is AUDIBLE in the original is UNK (the applier's
//     sign convention);
//   * cents->ratio uses exp(cents * ln2 / 1200.0) evaluated left-to-right
//     in double precision (the original multiplies by the pre-rounded
//     constant 0.00057762265 -- a <=1-ulp difference);
//   * invalid enum selections are deterministically CLAMPED (documented
//     policy; the original's out-of-range behaviour is UNK).
//
// Determinism: same inputs -> bitwise-identical outputs (pure integer
// walk arithmetic + one exp() per ratio, fixed operation order).
//
// NOT part of the VST3 host surface: parameter ids 0..37 are untouched;
// this module is a library + CLI-verified reference until its semantics
// and parameter-ID allocation pass the compatibility review.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>

namespace octavox {
namespace harmony {

/// The 5 temperament modes (recovered order: the choices string
/// 0x1803BBF60 = Equal / Just Major / Just Minor / Pythagorean / Meantone).
enum class Temperament : int {
    Equal12 = 0,
    JustMajor = 1,
    JustMinor = 2,
    Pythagorean = 3,
    Meantone14 = 4,
};

/// Number of key spellings (17; the enharmonic-collapsing key map
/// 0x1803BB610 maps them onto 12 pitch classes, A = 9).
constexpr int kNumKeys = 17;
/// Number of scale PATTERN ROWS (19; .rdata 0x1803BB780, stride 0x30).
constexpr int kNumScaleRows = 19;
/// Number of ENGINE scale CHOICES (15 in the scal-mux table; hungarian is
/// the out-of-range 16th -- see the spec's irregularity section).
constexpr int kNumScaleChoices = 15;
/// Number of musical-interval selections (29; the int1..int8 records'
/// choice list: -2oct .. -2nd, unison, +2nd .. +2oct).
constexpr int kNumIntervals = 29;
/// Voices per mapping (the walk's 8-row ratio table).
constexpr int kNumVoices = 8;

/// One voice's request: its interval selection (0..28) + fine cents.
struct VoiceRequest {
    int interval = 14;       ///< 0..28 (14 = unison); clamped deterministically
    double fine_cents = 0.0; ///< per-voice pcent (plain cents added by the walk)
};

/// A full mapping request.
struct MapRequest {
    int key = 13;                    ///< 0..16 (17 spellings; 13 = A,
                                    ///< whose key-map value is 9); clamped
    int scale = 1;                   ///< row-level: 0..18 (default 1 = ionian)
                                     ///< choice-level: 0..14 engine choices
    Temperament temperament = Temperament::Equal12;
    double global_tune_cents = 0.0;  ///< plain cents added to every voice
    VoiceRequest voices[kNumVoices];
};

/// A mapping result: per-voice ratios (unity = 1.0) + diagnostics.
struct MapResult {
    double ratio[kNumVoices];        ///< keyRatio * 2^(voiceCents/1200)
    double voice_cents[kNumVoices];  ///< the walk cents BEFORE the key ratio
    double key_ratio;                ///< the transposition factor
    int grid[12];                    ///< the filled degree-cents grid
    int grid_size;                   ///< 5..12 (diagnostics)
};

/// The row-level mapping: `req.scale` selects the PATTERN ROW 0..18
/// directly (no scal-mux irregularity, no raw-index switch). This is the
/// clean mathematical surface of the recovered rules.
MapResult mapRow(const MapRequest& req);

/// The choice-level mapping: `req.scale` selects the ENGINE CHOICE 0..14,
/// reproducing the recovered scal-mux INCLUDING the authored irregularity
/// (the last six choices load shifted pattern rows) and the
/// chromatic/whole-tone raw-index interval switch (choices whose mux
/// output is 14/15 walk with the un-bumped raw choice index).
MapResult mapChoice(const MapRequest& req);

/// The 12 temperament cents of a mode (OBS tables, verbatim).
const int* temperamentCents(Temperament t);

/// The 12 pattern values of a scale row (OBS table, verbatim).
const int* scalePatternRow(int row);

/// The 29-entry interval mux table (v = 14 + s + delta, delta = 1 iff
/// s mod 7 in {2,6}; mathematical mod -- reproduces the authored values).
const int* intervalMuxTable();

/// The interval label of selection i (0..28), ASCII (the recovered
/// 29-entry choice order).
const char* intervalLabel(int i);

/// The key map (17 spellings -> 12 pitch classes; A = 9; OBS verbatim).
const int* keyMap();

/// The 15-entry scal-mux (engine choice -> pattern row; the authored
/// irregularity verbatim; OBS).
const int* scalMuxTable();

}  // namespace harmony
}  // namespace octavox
