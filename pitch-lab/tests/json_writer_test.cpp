// T-J1 — canonical JSON writer (spec §4.8.1 item 8, frozen cycle 3).
//
// Pins the canonical form: sorted object keys, 2-space pretty indentation,
// "%.17g" doubles (round-trip), integer/bool/null/string forms, string
// escaping (incl. control characters), determinism (same tree => identical
// bytes; map insertion order irrelevant), and the value-tree accessors.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "harness/json_writer.h"

using pitchlab::json::Array;
using pitchlab::json::Object;
using pitchlab::json::Value;

TEST_CASE("json: scalars") {
  CHECK(pitchlab::json::serialize(Value(nullptr)) == "null");
  CHECK(pitchlab::json::serialize(Value(true)) == "true");
  CHECK(pitchlab::json::serialize(Value(false)) == "false");
  CHECK(pitchlab::json::serialize(Value(int64_t{-42})) == "-42");
  CHECK(pitchlab::json::serialize(Value(int64_t{4096})) == "4096");
  CHECK(pitchlab::json::serialize(Value(std::string("plain"))) == "\"plain\"");
}

TEST_CASE("json: doubles use %.17g (round-trip exact, deterministic)") {
  // 0.95 is NOT representable exactly; %.17g shows the true double value.
  CHECK(pitchlab::json::serialize(Value(0.95)) == "0.94999999999999996");
  CHECK(pitchlab::json::serialize(Value(1.0)) == "1");
  CHECK(pitchlab::json::serialize(Value(-0.5)) == "-0.5");
  CHECK(pitchlab::json::serialize(Value(48000.0)) == "48000");
  // Round-trip: strtod(parse(serialize(d))) == d for a probe set.
  const double probes[] = {0.95, 0.0625, 16.0, 1.0 / 3.0, 2.0 / 3.0, -1.5e-9, 123456789.123456789};
  for (double d : probes) {
    const std::string s = pitchlab::json::serialize(Value(d));
    const double back = std::strtod(s.c_str(), nullptr);
    CHECK(back == d);
    // Bit-level round trip (stronger than ==).
    uint64_t a = 0, b = 0;
    std::memcpy(&a, &d, 8);
    std::memcpy(&b, &back, 8);
    CHECK(a == b);
  }
}

TEST_CASE("json: arrays and nesting") {
  Array arr{Value(1), Value(2), Value(3)};
  CHECK(pitchlab::json::serialize(Value(arr)) ==
        "[\n  1,\n  2,\n  3\n]");
  CHECK(pitchlab::json::serialize(Value(Array{})) == "[]");

  Object inner;
  inner["b"] = Value(2);
  inner["a"] = Value(1);
  Object outer;
  outer["x"] = Value(inner);
  CHECK(pitchlab::json::serialize(Value(outer)) ==
        "{\n  \"x\": {\n    \"a\": 1,\n    \"b\": 2\n  }\n}");
}

TEST_CASE("json: object keys are sorted regardless of insertion order") {
  Object o1;
  o1["alpha"] = Value(1);
  o1["Beta"] = Value(2);   // byte order: 'B'(0x42) < 'a'(0x61)
  o1["gamma"] = Value(3);
  Object o2;
  o2["gamma"] = Value(3);
  o2["Beta"] = Value(2);
  o2["alpha"] = Value(1);
  const std::string s1 = pitchlab::json::serialize(Value(o1));
  const std::string s2 = pitchlab::json::serialize(Value(o2));
  CHECK(s1 == s2);  // determinism: insertion order is irrelevant
  CHECK(s1 == "{\n  \"Beta\": 2,\n  \"alpha\": 1,\n  \"gamma\": 3\n}");
}

TEST_CASE("json: string escaping") {
  CHECK(pitchlab::json::serialize(Value(std::string("quote\"backslash"))) ==
        "\"quote\\\"backslash\"");
  CHECK(pitchlab::json::serialize(Value(std::string("tab\tnl\n"))) ==
        "\"tab\\tnl\\n\"");
  // Control characters as \u00XX.
  CHECK(pitchlab::json::serialize(Value(std::string("c\x01x"))) ==
        "\"c\\u0001x\"");
  // UTF-8 passthrough (no escaping of >= 0x20).
  CHECK(pitchlab::json::serialize(Value(std::string("caf\xc3\xa9"))) ==
        "\"caf\xc3\xa9\"");
}

TEST_CASE("json: empty containers and mixed trees") {
  Object o;
  o["empty_arr"] = Value(Array{});
  o["empty_obj"] = Value(Object{});
  o["null_member"] = Value(nullptr);
  CHECK(pitchlab::json::serialize(Value(o)) ==
        "{\n  \"empty_arr\": [],\n  \"empty_obj\": {},\n  \"null_member\": null\n}");
}
