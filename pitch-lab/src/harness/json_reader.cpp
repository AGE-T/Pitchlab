#include "harness/json_reader.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "core/errors.h"

namespace pitchlab::json {
namespace {

class Parser {
 public:
  Parser(const std::string& text, const std::string& fileName)
      : text_(text), fileName_(fileName) {}

  [[nodiscard]] Value parseRoot() {
    skipWs();
    Value v = parseValue();
    skipWs();
    if (pos_ != text_.size()) {
      fail("trailing content after the root value");
    }
    return v;
  }

 private:
  const std::string& text_;
  const std::string& fileName_;
  std::size_t pos_ = 0;
  int line_ = 1;

  [[noreturn]] void fail(const std::string& reason) {
    throw ConfigError(fileName_, "", reason + " (line " + std::to_string(line_) + ")");
  }

  [[nodiscard]] char peek() const {
    return pos_ < text_.size() ? text_[pos_] : '\0';
  }

  char advance() {
    const char c = text_[pos_++];
    if (c == '\n') ++line_;
    return c;
  }

  void skipWs() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        advance();
      } else {
        break;
      }
    }
  }

  void expect(char c) {
    if (pos_ >= text_.size() || text_[pos_] != c) {
      fail(std::string("expected '") + c + "'");
    }
    advance();
  }

  [[nodiscard]] bool consumeLiteral(const char* lit) {
    const std::size_t n = std::strlen(lit);
    if (text_.compare(pos_, n, lit) == 0) {
      pos_ += n;
      return true;
    }
    return false;
  }

  [[nodiscard]] Value parseValue() {
    if (pos_ >= text_.size()) fail("unexpected end of input");
    const char c = peek();
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') return Value(parseString());
    if (consumeLiteral("true")) return Value(true);
    if (consumeLiteral("false")) return Value(false);
    if (consumeLiteral("null")) return Value(nullptr);
    return parseNumber();
  }

  [[nodiscard]] Value parseObject() {
    expect('{');
    Object obj;
    skipWs();
    if (peek() == '}') {
      advance();
      return Value(std::move(obj));
    }
    for (;;) {
      skipWs();
      if (peek() != '"') fail("expected object key string");
      std::string key = parseString();
      skipWs();
      expect(':');
      skipWs();
      Value v = parseValue();
      if (!obj.emplace(std::move(key), std::move(v)).second) {
        fail("duplicate object key");
      }
      skipWs();
      if (peek() == ',') {
        advance();
        continue;
      }
      if (peek() == '}') {
        advance();
        break;
      }
      fail("expected ',' or '}' in object");
    }
    return Value(std::move(obj));
  }

  [[nodiscard]] Value parseArray() {
    expect('[');
    Array arr;
    skipWs();
    if (peek() == ']') {
      advance();
      return Value(std::move(arr));
    }
    for (;;) {
      skipWs();
      arr.push_back(parseValue());
      skipWs();
      if (peek() == ',') {
        advance();
        continue;
      }
      if (peek() == ']') {
        advance();
        break;
      }
      fail("expected ',' or ']' in array");
    }
    return Value(std::move(arr));
  }

  [[nodiscard]] std::string parseString() {
    expect('"');
    std::string out;
    for (;;) {
      if (pos_ >= text_.size()) fail("unterminated string");
      const char c = advance();
      if (c == '"') break;
      if (c == '\\') {
        if (pos_ >= text_.size()) fail("unterminated escape");
        const char e = advance();
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'u': {
            // The writer's form: \u00XX with XX hex (control chars < 0x20).
            if (pos_ + 4 > text_.size()) fail("truncated \\u escape");
            if (text_[pos_] != '0' || text_[pos_ + 1] != '0') {
              fail("only \\u00XX escapes (the writer's control-char form) are accepted");
            }
            unsigned int byte = 0;
            for (int i = 2; i < 4; ++i) {
              const char h = text_[pos_ + static_cast<std::size_t>(i)];
              int d;
              if (h >= '0' && h <= '9') d = h - '0';
              else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
              else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
              else { fail("bad hex digit in \\u escape"); }
              byte = byte * 16u + static_cast<unsigned int>(d);
            }
            pos_ += 4;
            if (byte >= 0x20u) fail("\\u escape above 0x1F never emitted by the writer");
            out += static_cast<char>(byte);
            break;
          }
          default: fail("unknown string escape");
        }
      } else if (static_cast<unsigned char>(c) < 0x20u) {
        fail("raw control character in string (the writer escapes these)");
      } else {
        out += c;
      }
    }
    return out;
  }

  [[nodiscard]] Value parseNumber() {
    const std::size_t start = pos_;
    bool isDouble = false;
    if (peek() == '-' || peek() == '+') advance();
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c >= '0' && c <= '9') {
        advance();
      } else if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
        isDouble = isDouble || c == '.' || c == 'e' || c == 'E';
        // Signs only legal right after e/E for doubles; the writer never
        // emits other sign positions mid-number — validate cheaply:
        if ((c == '-' || c == '+') && pos_ > start) {
          const char prev = text_[pos_ - 1];
          if (prev != 'e' && prev != 'E') fail("bad number");
        }
        advance();
      } else {
        break;
      }
    }
    if (pos_ == start) fail("expected a value");
    const std::string token = text_.substr(start, pos_ - start);
    if (!isDouble) {
      errno = 0;
      char* end = nullptr;
      const long long v = std::strtoll(token.c_str(), &end, 10);
      if (errno != 0 || end == nullptr || *end != '\0') {
        isDouble = true;  // fall through to the double path (e.g. overflow)
      } else {
        return Value(static_cast<int64_t>(v));
      }
    }
    errno = 0;
    char* end = nullptr;
    const double d = std::strtod(token.c_str(), &end);
    if (errno != 0 || end == nullptr || *end != '\0') {
      fail("bad number '" + token + "'");
    }
    return Value(d);
  }
};

}  // namespace

Value parse(const std::string& text, const std::string& fileName) {
  Parser p(text, fileName);
  return p.parseRoot();
}

Value parseFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw ConfigError(path.string(), "", "cannot open JSON file for reading");
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (in.bad()) {
    throw ConfigError(path.string(), "", "JSON file read failed");
  }
  return parse(ss.str(), path.string());
}

}  // namespace pitchlab::json
