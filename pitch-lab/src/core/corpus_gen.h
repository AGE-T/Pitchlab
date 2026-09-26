#pragma once

// Pitch Lab — deterministic synthetic corpus generator library (implementation
// specification §11; the EXECUTABLE lives in tools/corpus_gen.cpp — a
// stand-alone tool, NOT a harness component, §4.8/§11.1).
//
// CONTRACT (v0.1, frozen from §11 + the cycle-2 implementation decisions
// recorded in §4.4.3.1's sibling conventions; every choice below is either
// spec text or a committed, owner-visible parameter in config/corpus.toml):
//   * Deterministic: every signal derives from fixed seeds via the harness
//     PCG64 (§4.7) with consumer tag "corpus" (§11.1). Per-item seed
//     (§11.4 "SplitMix64(masterSeed, itemId)", frozen exact rule):
//         itemSeed = splitMix64(masterSeed XOR fnv1a64(itemId))
//     Streams: mono/single items use makeConsumerStream(itemSeed, "corpus");
//     the decorrelated stereo class uses tags "corpus.L" / "corpus.R" (the
//     §4.7 consumer-tag scheme extended per channel; frozen here).
//   * No wall-clock, no platform entropy; same input => bit-identical WAV
//     bytes (the writer is deterministic, §9).
//   * Every recipe parameter is a committed, owner-visible value in
//     config/corpus.toml (§11.4: the file IS the generator config; changing
//     the master seed = new corpus version, owner-visible).
//   * Amplitude normalised to -12 dBFS true peak: deterministic scaling
//     computed from the EXACT signal (max |sample| over all channels — the
//     §11.2 parenthetical), shared across channels (preserves stereo
//     correlation), applied in double.
//   * Output per item: <out>/<id>/signal.wav (float64 master — L-4 "determinism
//     is precious") + metadata.toml (§15.5 fields: id, category, sampleRate,
//     channels, durationSec, sourceDescription, referenceStatus, bandContentHz;
//     §11.2: referenceStatus = "test-fixture", sourceDescription = "generated
//     by tools/corpus_gen v0.1, seed <itemSeed>").
//   * v0.1 class set (§11.2) and the "mono and stereo variants where
//     meaningful" instantiation: only the two stereo-noise classes are stereo
//     (identical-channel vs independent-stream behaviour IS the fixture);
//     every other class is mono. Channel count is validated per class.
//   * Sample rates: the §8 supported set only (44100/48000/88200/96000/
//     176400/192000) — corpus assets are authored at supported rates.
//   * Unknown class / unknown params key / wrong channel count / bad values
//     => ConfigError{file, field, reason} (typo protection).

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/toml_lite.h"
#include "core/types.h"

namespace pitchlab {

/// One [[item]] row of config/corpus.toml.
struct CorpusItemSpec {
  std::string id;          // e.g. "syn-sine-440-5s-48k" (asset directory name)
  std::string className;   // generator class (see corpus_gen.cpp table)
  std::string category;    // §11.2/§F.2 vocabulary for metadata.toml
  std::string file;        // source config path (error messages)
  double sampleRate = 0.0;
  double durationSec = 0.0;
  int channels = 1;
  toml::TomlTable params;  // class-specific, validated per class
};

struct CorpusConfig {
  std::string version;      // "v0.1"
  uint64_t masterSeed = 0;  // §11.4 committed constant
  std::vector<CorpusItemSpec> items;
  std::string file;         // source path (errors)
};

/// Per-item seed derivation (frozen rule, §11.4).
[[nodiscard]] uint64_t corpusItemSeed(uint64_t masterSeed, const std::string& itemId);

/// Parse + validate config/corpus.toml. Throws ConfigError on any violation
/// (unknown version, bad master seed, duplicate ids, unknown class/keys,
/// unsupported rate, channel-count mismatch, invalid param values).
[[nodiscard]] CorpusConfig parseCorpusConfig(const std::filesystem::path& tomlFile);

/// A generated corpus item: planar channels (pre-normalisation synthesis,
/// then normalised) + metadata text.
struct GeneratedCorpusItem {
  CorpusItemSpec spec;
  uint64_t itemSeed = 0;
  std::vector<std::vector<double>> channels;  // planar, [c][frame]
  double peakLinear = 0.0;                    // exact max |sample| before scaling
  std::vector<double> bandContentHz;          // [lo, hi] for metadata
  std::string sourceDescription;              // §11.2 formula
};

/// Generate ONE item (pure + deterministic: a function of spec + itemSeed).
/// Throws ConfigError on recipe violations (e.g. an all-zero signal).
[[nodiscard]] GeneratedCorpusItem generateCorpusItem(const CorpusItemSpec& spec,
                                                     uint64_t itemSeed);

/// Render the item's metadata.toml text (deterministic; §15.5 fields).
[[nodiscard]] std::string renderCorpusMetadata(const GeneratedCorpusItem& item);

/// Generate every item of a config (item seeds derived internally).
[[nodiscard]] std::vector<GeneratedCorpusItem> generateCorpus(const CorpusConfig& config);

/// Write one item to <outDir>/<id>/ (signal.wav float64 + metadata.toml).
void writeCorpusItem(const std::filesystem::path& outDir, const GeneratedCorpusItem& item);

}  // namespace pitchlab
