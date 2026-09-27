#pragma once

// Pitch Lab — minimal canonical-JSON reader (implementation specification
// §10.4 item 5/21, frozen cycle 5).
//
// ROLE: parse the manifests (and only manifests) that the project's OWN
// canonical JSON writer (harness/json_writer) produces, so the Analyzer can
// read render provenance back without a third-party JSON dependency (the
// v0.1 dependency set stays {doctest, pocketfft}).
//
// GRAMMAR (exactly the writer's output grammar, frozen):
//   * objects {"k": v, ...} — keys are the writer's sorted keys; the reader
//     accepts any order but rejects DUPLICATE keys (malformed);
//   * arrays [v, ...];
//   * strings with escapes \" \\ \n \t \r \b \f and \u00XX (the writer's
//     control-character form; the code point must be < 0x20 — everything
//     else the writer emits verbatim);
//   * numbers: integers (optionally signed) parse as Int; any number with
//     '.', 'e' or 'E' parses as Double (%.17g output round-trips exactly);
//   * true / false / null;
//   * whitespace: space, tab, newline, carriage return.
// Anything else => ConfigError{file, field, reason} (reason carries the
// 1-based line). Trailing content after the root value => ConfigError.
//
// Determinism: pure function of the input text; hand-rolled number parsing
// via strtod (no locale dependence beyond the C default the writer already
// relies on); no time, no randomness. Same input => same value tree.

#include <filesystem>
#include <string>

#include "harness/json_writer.h"

namespace pitchlab::json {

/// Parse canonical-JSON text. Throws ConfigError on malformed input.
[[nodiscard]] Value parse(const std::string& text, const std::string& fileName);

/// Read + parse a file (binary read; the writer emits UTF-8 passthrough).
/// Throws ConfigError on I/O failure or malformed content.
[[nodiscard]] Value parseFile(const std::filesystem::path& path);

}  // namespace pitchlab::json
