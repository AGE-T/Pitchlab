#include "core/toml_lite.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace pitchlab::toml {

namespace {

[[nodiscard]] ConfigError parseError(const std::string& file, int line,
                                     const std::string& reason) {
  return ConfigError(file, "", "line " + std::to_string(line) + ": " + reason);
}

[[nodiscard]] bool isBareKeyChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

class Parser {
 public:
  Parser(const std::string& text, std::string fileName)
      : src_(text), file_(std::move(fileName)) {}

  TomlTable run() {
    TomlTable root;
    TomlTable* current = &root;
    while (!atEnd()) {
      skipWsAndCommentsAndNewlines();
      if (atEnd()) break;
      if (peek() == '[') {
        current = parseHeader(root);
      } else {
        parseKeyValueInto(*current);
        expectLineEnd("expected end of line after key/value");
      }
    }
    return root;
  }

 private:
  // --- low-level cursor -----------------------------------------------------
  [[nodiscard]] bool atEnd() const { return pos_ >= src_.size(); }
  [[nodiscard]] char peek() const { return src_[pos_]; }
  char advance() {
    const char c = src_[pos_++];
    if (c == '\n') ++line_;
    return c;
  }
  void skipInlineWs() {
    while (!atEnd() && (peek() == ' ' || peek() == '\t')) advance();
  }
  void skipWsAndCommentsAndNewlines() {
    while (!atEnd()) {
      const char c = peek();
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        advance();
      } else if (c == '#') {
        while (!atEnd() && peek() != '\n') advance();
      } else {
        break;
      }
    }
  }
  void expectLineEnd(const std::string& what) {
    skipInlineWs();
    if (!atEnd() && peek() == '#') {
      while (!atEnd() && peek() != '\n') advance();
    }
    if (atEnd()) return;
    if (peek() == '\n') {
      advance();
      return;
    }
    if (peek() == '\r' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '\n') {
      advance();
      advance();
      return;
    }
    throw parseError(file_, line_, what + ", found '" + std::string(1, peek()) + "'");
  }

  [[nodiscard]] std::string parseBareKey(const std::string& where) {
    std::string key;
    while (!atEnd() && isBareKeyChar(peek())) key += advance();
    if (key.empty()) {
      throw parseError(file_, line_, where + ": expected a bare key [A-Za-z0-9_-]+");
    }
    return key;
  }

  // --- headers ---------------------------------------------------------------
  // Returns the table that subsequent key/values land in.
  TomlTable* parseHeader(TomlTable& root) {
    advance();  // consume '['
    const bool arrayOfTables = !atEnd() && peek() == '[';
    if (arrayOfTables) advance();
    skipInlineWs();
    const std::string name = parseBareKey("[table] header");
    skipInlineWs();
    if (atEnd() || peek() != ']') {
      throw parseError(file_, line_, "unterminated [table] header");
    }
    advance();
    if (arrayOfTables) {
      if (atEnd() || peek() != ']') {
        throw parseError(file_, line_, "unterminated [[array-of-table]] header");
      }
      advance();
    }
    expectLineEnd("expected end of line after table header");

    if (arrayOfTables) {
      // [[name]]: top-level array of tables; append a fresh table and make it
      // the current key/value destination.
      auto it = root.find(name);
      if (it == root.end()) {
        TomlValue arr;
        arr.kind = TomlValue::Kind::Array;
        auto [inserted, ok] = root.emplace(name, std::move(arr));
        (void)ok;
        it = inserted;
      }
      if (!it->second.is(TomlValue::Kind::Array)) {
        throw parseError(file_, line_, "[[" + name + "]] conflicts with a non-array key");
      }
      TomlValue tbl;
      tbl.kind = TomlValue::Kind::Table;
      it->second.array.push_back(std::move(tbl));
      // vector element pointers are stable until the NEXT push_back on this
      // array, which only happens at the next [[name]] header — exactly when
      // this pointer is re-taken.
      return &it->second.array.back().table;
    }
    // [name]: single table; create once, reject re-opening (TOML rule).
    auto it = root.find(name);
    if (it == root.end()) {
      TomlValue tbl;
      tbl.kind = TomlValue::Kind::Table;
      auto [inserted, ok] = root.emplace(name, std::move(tbl));
      (void)ok;
      it = inserted;
      singleTables_.insert(name);
    } else if (!it->second.is(TomlValue::Kind::Table)) {
      throw parseError(file_, line_, "[" + name + "] conflicts with a non-table key");
    } else if (singleTables_.count(name) != 0) {
      throw parseError(file_, line_, "duplicate [" + name + "] table header");
    }
    // std::map node references are stable: safe to return a pointer into it.
    return &it->second.table;
  }

  // --- key/value ---------------------------------------------------------------
  void parseKeyValueInto(TomlTable& dest) {
    const std::string key = parseBareKey("key/value");
    skipInlineWs();
    if (atEnd() || peek() != '=') {
      throw parseError(file_, line_, "expected '=' after key '" + key + "'");
    }
    advance();
    skipInlineWs();
    TomlValue value = parseValue();
    if (dest.count(key) != 0) {
      throw parseError(file_, line_, "duplicate key '" + key + "'");
    }
    dest.emplace(key, std::move(value));
  }

  TomlValue parseValue() {
    if (atEnd()) throw parseError(file_, line_, "expected a value, found end of file");
    const char c = peek();
    if (c == '"') return parseString();
    if (c == '[') return parseArray();
    if (c == '{') return parseInlineTable();
    if (c == 't' || c == 'f') return parseBoolean();
    return parseNumber();
  }

  TomlValue parseString() {
    advance();  // opening quote
    std::string out;
    while (true) {
      if (atEnd() || peek() == '\n') {
        throw parseError(file_, line_, "unterminated string");
      }
      const char c = advance();
      if (c == '"') break;
      if (c == '\\') {
        if (atEnd()) throw parseError(file_, line_, "unterminated escape sequence");
        const char e = advance();
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          default:
            throw parseError(file_, line_, std::string("unsupported escape '\\") + e +
                                             "' (allowed: \\\" \\\\ \\n \\t \\r)");
        }
      } else {
        if (static_cast<unsigned char>(c) < 0x20 && c != '\t') {
          throw parseError(file_, line_, "raw control character in string");
        }
        out += c;
      }
    }
    TomlValue v;
    v.kind = TomlValue::Kind::String;
    v.str = std::move(out);
    return v;
  }

  TomlValue parseBoolean() {
    if (src_.compare(pos_, 4, "true") == 0) {
      for (int i = 0; i < 4; ++i) advance();
      TomlValue v;
      v.kind = TomlValue::Kind::Boolean;
      v.boolean = true;
      return v;
    }
    if (src_.compare(pos_, 5, "false") == 0) {
      for (int i = 0; i < 5; ++i) advance();
      TomlValue v;
      v.kind = TomlValue::Kind::Boolean;
      v.boolean = false;
      return v;
    }
    throw parseError(file_, line_, "invalid value (expected true/false)");
  }

  TomlValue parseNumber() {
    const std::size_t start = pos_;
    const int startLine = line_;
    const bool hasSign = !atEnd() && (peek() == '+' || peek() == '-');
    if (hasSign) advance();
    if (atEnd() || !std::isdigit(static_cast<unsigned char>(peek()))) {
      if (src_.compare(start, 3, "inf") == 0 || src_.compare(start, 4, "nan") == 0 ||
          src_.compare(start, 4, "+inf") == 0 || src_.compare(start, 4, "-inf") == 0 ||
          src_.compare(start, 4, "+nan") == 0 || src_.compare(start, 4, "-nan") == 0) {
        throw parseError(file_, line_, "non-finite float literal not accepted");
      }
      throw parseError(file_, line_, "invalid value (expected a number)");
    }

    bool isFloat = false;
    bool isHex = false;
    if (peek() == '0' && pos_ + 1 < src_.size() &&
        (src_[pos_ + 1] == 'x' || src_[pos_ + 1] == 'X')) {
      if (hasSign) {
        throw parseError(file_, line_, "signed hex integer (TOML-forbidden)");
      }
      advance();
      advance();
      isHex = true;
      const std::size_t digitsStart = pos_;
      while (!atEnd() && std::isxdigit(static_cast<unsigned char>(peek()))) advance();
      if (pos_ == digitsStart) {
        throw parseError(file_, line_, "empty hex integer");
      }
    } else {
      const std::size_t digitsStart = pos_;
      while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
      if (pos_ == digitsStart) {
        throw parseError(file_, line_, "invalid value (expected a number)");
      }
      if (!atEnd() && peek() == '.') {
        isFloat = true;
        advance();
        if (atEnd() || !std::isdigit(static_cast<unsigned char>(peek()))) {
          throw parseError(file_, line_, "digit expected after decimal point");
        }
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
      }
      if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
        isFloat = true;
        advance();
        if (!atEnd() && (peek() == '+' || peek() == '-')) advance();
        if (atEnd() || !std::isdigit(static_cast<unsigned char>(peek()))) {
          throw parseError(file_, line_, "digit expected in exponent");
        }
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
      }
    }
    const std::string token = src_.substr(start, pos_ - start);

    TomlValue v;
    if (isHex) {
      v.kind = TomlValue::Kind::Integer;
      v.integer = static_cast<int64_t>(std::stoull(token, nullptr, 16));
      return v;
    }
    if (isFloat) {
      v.kind = TomlValue::Kind::Double;
      v.real = std::stod(token);
      if (!std::isfinite(v.real)) {
        throw parseError(file_, startLine, "float literal out of range '" + token + "'");
      }
      return v;
    }
    // Integer: TOML leading-zero rule (sign then "0" or non-zero-leading).
    std::string digits = token;
    if (!digits.empty() && (digits[0] == '+' || digits[0] == '-')) digits = digits.substr(1);
    if (digits.size() > 1 && digits[0] == '0') {
      throw parseError(file_, startLine, "integer with leading zero '" + token + "'");
    }
    v.kind = TomlValue::Kind::Integer;
    try {
      v.integer = std::stoll(token);
    } catch (const std::out_of_range&) {
      throw parseError(file_, startLine, "integer out of int64 range '" + token + "'");
    }
    return v;
  }

  TomlValue parseArray() {
    advance();  // '['
    TomlValue v;
    v.kind = TomlValue::Kind::Array;
    while (true) {
      skipWsAndCommentsAndNewlines();
      if (atEnd()) throw parseError(file_, line_, "unterminated array");
      if (peek() == ']') {
        advance();
        break;
      }
      v.array.push_back(parseValue());
      skipWsAndCommentsAndNewlines();
      if (atEnd()) throw parseError(file_, line_, "unterminated array");
      if (peek() == ',') {
        advance();  // trailing comma allowed before ']' (TOML-legal)
        continue;
      }
      if (peek() == ']') {
        advance();
        break;
      }
      throw parseError(file_, line_, "expected ',' or ']' in array");
    }
    return v;
  }

  TomlValue parseInlineTable() {
    advance();  // '{'
    TomlValue v;
    v.kind = TomlValue::Kind::Table;
    skipInlineWs();
    if (!atEnd() && peek() == '}') {
      advance();
      return v;
    }
    while (true) {
      skipInlineWs();
      const std::string key = parseBareKey("inline table");
      skipInlineWs();
      if (atEnd() || peek() != '=') {
        throw parseError(file_, line_, "expected '=' in inline table");
      }
      advance();
      skipInlineWs();
      TomlValue value = parseValue();
      if (v.table.count(key) != 0) {
        throw parseError(file_, line_, "duplicate key '" + key + "' in inline table");
      }
      v.table.emplace(key, std::move(value));
      skipInlineWs();
      if (atEnd() || peek() == '\n') {
        throw parseError(file_, line_,
                         "inline table must be single-line and terminated with '}'");
      }
      if (peek() == ',') {
        advance();
        skipInlineWs();
        if (!atEnd() && peek() == '}') {
          // TOML 1.0 forbids a trailing comma in inline tables (frozen rule).
          throw parseError(file_, line_, "trailing comma in inline table (TOML-forbidden)");
        }
        continue;
      }
      if (peek() == '}') {
        advance();
        break;
      }
      throw parseError(file_, line_, "expected ',' or '}' in inline table");
    }
    return v;
  }

  const std::string& src_;
  std::string file_;
  std::size_t pos_ = 0;
  int line_ = 1;
  std::set<std::string> singleTables_;  // [table] headers already opened once
};

}  // namespace

TomlTable parseTomlFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw ConfigError(path.string(), "", "file not found / not readable");
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return parseTomlText(ss.str(), path.string());
}

TomlTable parseTomlText(const std::string& text, const std::string& fileName) {
  Parser p(text, fileName);
  return p.run();
}

// --- TomlValue typed accessors (uniform CONFIG ERROR on mismatch) ------------

const std::string& TomlValue::asString(const std::string& file, const std::string& field) const {
  if (kind != Kind::String) {
    throw ConfigError(file, field, "expected a string value");
  }
  return str;
}

int64_t TomlValue::asInteger(const std::string& file, const std::string& field) const {
  if (kind != Kind::Integer) {
    throw ConfigError(file, field, "expected an integer value");
  }
  return integer;
}

double TomlValue::asDouble(const std::string& file, const std::string& field) const {
  if (kind == Kind::Integer) {
    return static_cast<double>(integer);
  }
  if (kind != Kind::Double) {
    throw ConfigError(file, field, "expected a number value");
  }
  return real;
}

bool TomlValue::asBoolean(const std::string& file, const std::string& field) const {
  if (kind != Kind::Boolean) {
    throw ConfigError(file, field, "expected a boolean value");
  }
  return boolean;
}

const std::vector<TomlValue>& TomlValue::asArray(const std::string& file,
                                                 const std::string& field) const {
  if (kind != Kind::Array) {
    throw ConfigError(file, field, "expected an array value");
  }
  return array;
}

const std::map<std::string, TomlValue>& TomlValue::asTable(const std::string& file,
                                                            const std::string& field) const {
  if (kind != Kind::Table) {
    throw ConfigError(file, field, "expected a table value");
  }
  return table;
}

}  // namespace pitchlab::toml
