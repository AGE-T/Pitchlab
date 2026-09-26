#include "harness/json_writer.h"

#include <cmath>
#include <cstdio>

namespace pitchlab::json {
namespace {

void appendEscapedString(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
          out += buf;
        } else {
          out += static_cast<char>(c);  // UTF-8 passthrough
        }
    }
  }
  out += '"';
}

void appendDouble(std::string& out, double d) {
  // %.17g: round-trip exact for double; deterministic output (no locale use
  // — snprintf with a literal format and no thousands separators).
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.17g", d);
  out += buf;
}

void serializeInto(std::string& out, const Value& v, int indent) {
  const std::string pad(static_cast<std::size_t>(2 * indent), ' ');
  const std::string padInner(static_cast<std::size_t>(2 * (indent + 1)), ' ');
  switch (v.kind()) {
    case Value::Kind::Null:
      out += "null";
      break;
    case Value::Kind::Bool:
      out += v.asBool() ? "true" : "false";
      break;
    case Value::Kind::Int: {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.asInt()));
      out += buf;
      break;
    }
    case Value::Kind::Double:
      appendDouble(out, v.asDouble());
      break;
    case Value::Kind::String:
      appendEscapedString(out, v.asString());
      break;
    case Value::Kind::Array: {
      const Array& arr = v.asArray();
      if (arr.empty()) {
        out += "[]";
        break;
      }
      out += "[\n";
      for (std::size_t i = 0; i < arr.size(); ++i) {
        out += padInner;
        serializeInto(out, arr[i], indent + 1);
        out += (i + 1 < arr.size()) ? ",\n" : "\n";
      }
      out += pad;
      out += ']';
      break;
    }
    case Value::Kind::Object: {
      const Object& obj = v.asObject();
      if (obj.empty()) {
        out += "{}";
        break;
      }
      out += "{\n";
      bool first = true;
      for (const auto& kv : obj) {
        if (!first) {
          out += ",\n";
        }
        first = false;
        out += padInner;
        appendEscapedString(out, kv.first);
        out += ": ";
        serializeInto(out, kv.second, indent + 1);
      }
      out += '\n';
      out += pad;
      out += '}';
      break;
    }
  }
}

}  // namespace

std::string serialize(const Value& value) {
  std::string out;
  out.reserve(1024);
  serializeInto(out, value, 0);
  return out;
}

}  // namespace pitchlab::json
