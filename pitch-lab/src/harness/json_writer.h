#pragma once

// Pitch Lab — canonical JSON writer (implementation specification §4.8.1
// item 8, frozen cycle 3).
//
// ROLE: deterministic serialisation for the render manifests and the
// engine-configuration canonical form (§4.5 "canonical JSON, sorted keys").
// Own implementation (v0.1 dependency set stays {doctest}).
//
// CANONICAL FORM (frozen):
//   * object keys SORTED (std::map, lexicographic byte order);
//   * pretty printing: 2-space indentation, one member per line, ": " and
//     ", " separators, trailing newline appended by the caller;
//   * strings: double-quoted, escapes for " \ \n \t \r \b \f, other control
//     characters (< 0x20) as \u00XX; all other bytes verbatim (UTF-8 passthrough);
//   * integers (int64) printed as decimal integers;
//   * doubles printed with "%.17g" (round-trip exact; deterministic across
//     libm printf implementations for these inputs — pinned by unit tests);
//   * true / false / null;
//   * no timestamps, no locale, no map-order variance: the same value tree
//     serialises to byte-identical output, always.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pitchlab::json {

class Value;
using Object = std::map<std::string, Value>;  // sorted keys by construction
using Array = std::vector<Value>;

class Value {
 public:
  enum class Kind { Null, Bool, Int, Double, String, Array, Object };

  Value() : kind_(Kind::Null) {}
  Value(std::nullptr_t) : kind_(Kind::Null) {}
  Value(bool b) : kind_(Kind::Bool), bool_(b) {}
  Value(int64_t i) : kind_(Kind::Int), int_(i) {}
  Value(int i) : kind_(Kind::Int), int_(i) {}
  Value(double d) : kind_(Kind::Double), double_(d) {}
  Value(const char* s) : kind_(Kind::String), str_(s) {}
  Value(std::string s) : kind_(Kind::String), str_(std::move(s)) {}
  Value(Object o) : kind_(Kind::Object), object_(std::move(o)) {}
  Value(Array a) : kind_(Kind::Array), array_(std::move(a)) {}

  [[nodiscard]] Kind kind() const { return kind_; }
  [[nodiscard]] bool isNull() const { return kind_ == Kind::Null; }

  [[nodiscard]] bool asBool() const { return bool_; }
  [[nodiscard]] int64_t asInt() const { return int_; }
  // Total numeric read: an Int-kind value yields its integer as double.
  // REQUIRED by the canonical round-trip: %.17g serialises Double(1.0) as
  // "1", the reader parses that back as Int — a consumer calling asDouble()
  // on the round-tripped value must still read the numeric value (not the
  // uninitialised union member).
  [[nodiscard]] double asDouble() const {
    return kind_ == Kind::Int ? static_cast<double>(int_) : double_;
  }
  [[nodiscard]] const std::string& asString() const { return str_; }
  [[nodiscard]] const Object& asObject() const { return object_; }
  [[nodiscard]] Object& asObject() { return object_; }
  [[nodiscard]] const Array& asArray() const { return array_; }
  [[nodiscard]] Array& asArray() { return array_; }

 private:
  Kind kind_;
  bool bool_ = false;
  int64_t int_ = 0;
  double double_ = 0.0;
  std::string str_;
  Array array_;
  Object object_;
};

/// Serialise canonically (see file header). The returned string has NO
/// trailing newline (the caller appends it when writing the file).
[[nodiscard]] std::string serialize(const Value& value);

}  // namespace pitchlab::json
