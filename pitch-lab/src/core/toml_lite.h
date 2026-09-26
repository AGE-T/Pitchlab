#pragma once

// Pitch Lab — strict v0.1 TOML-subset parser ("toml_lite").
//
// ROLE (implementation specification §4.4.3.1 item 1, frozen 2026-09-26
// BEFORE coding, cycle 2): curve specs, config/corpus.toml and the later
// experiment files are parsed by this OWN strict subset parser — the v0.1
// dependency set stays exactly {doctest} (build spec §7; architecture §J
// v0.1 note: "the v0.1 bootstrap vendors nothing beyond the build toolchain
// and test framework"). Follows the project's own-primitives pattern (own
// WAV I/O instead of libsndfile, own PCG64 instead of <random>).
//
// ACCEPTED SYNTAX (frozen v0.1 authoring surface, §4.4.3.1 item 1):
//   * comments (# to end of line; not inside strings)
//   * bare keys [A-Za-z0-9_-]+ (dotted keys / quoted keys NOT accepted)
//   * basic double-quoted strings with escapes \" \\ \n \t \r only
//   * integers: decimal with optional +/- (no leading zeros) or 0x hex
//   * floats: decimal and exponent forms with optional sign; "inf"/"nan"
//     NOT accepted (spec §4.4.4 rejects non-finite values at a higher
//     level anyway; keeping them out of the value system is stricter)
//   * booleans true/false
//   * inline tables { k = v, ... }  (single line, NO trailing comma)
//   * arrays [ v, ... ]  (multi-line allowed, trailing comma allowed)
//   * [table] and [[array-of-table]] headers (single bare segment only)
//   * duplicate keys / duplicate [table] headers => CONFIG ERROR
// Anything else => ConfigError{file, field, reason} with the line number.
//
// Values are represented by TomlValue; tables use std::map (sorted keys =>
// deterministic iteration order everywhere).
//
// Determinism: parsing is a pure function of the input text; no locale use
// (number parsing is hand-rolled, NOT strtod-with-locale), no time, no
// randomness. Same input => same value tree, always.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/errors.h"

namespace pitchlab::toml {

struct TomlValue {
  enum class Kind { String, Integer, Double, Boolean, Array, Table };

  Kind kind = Kind::Table;
  std::string str;                      // Kind::String
  int64_t integer = 0;                  // Kind::Integer
  double real = 0.0;                    // Kind::Double
  bool boolean = false;                 // Kind::Boolean
  std::vector<TomlValue> array;         // Kind::Array
  std::map<std::string, TomlValue> table;  // Kind::Table (sorted keys)

  [[nodiscard]] bool is(Kind k) const { return kind == k; }

  // Typed accessors with a uniform CONFIG ERROR on type mismatch
  // (field = the key being read; file = the source file for the message).
  [[nodiscard]] const std::string& asString(const std::string& file,
                                            const std::string& field) const;
  [[nodiscard]] int64_t asInteger(const std::string& file, const std::string& field) const;
  [[nodiscard]] double asDouble(const std::string& file, const std::string& field) const;
  [[nodiscard]] bool asBoolean(const std::string& file, const std::string& field) const;
  [[nodiscard]] const std::vector<TomlValue>& asArray(const std::string& file,
                                                      const std::string& field) const;
  [[nodiscard]] const std::map<std::string, TomlValue>& asTable(const std::string& file,
                                                                const std::string& field) const;
};

/// Root table type (document = top-level table).
using TomlTable = std::map<std::string, TomlValue>;

/// Parse a TOML-subset file. Throws ConfigError{file, field, reason} on any
/// syntax error (reason carries the 1-based line number).
[[nodiscard]] TomlTable parseTomlFile(const std::filesystem::path& path);

/// Parse from an in-memory string (same grammar; `fileName` is used only in
/// error messages).
[[nodiscard]] TomlTable parseTomlText(const std::string& text, const std::string& fileName);

}  // namespace pitchlab::toml
