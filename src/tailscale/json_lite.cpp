/**
 * TailZero - tiny JSON reader implementation.
 * SPDX-License-Identifier: MIT
 */
#include "tailscale/json_lite.h"

#include <charconv>
#include <cstdint>
#include <system_error>
#include <utility>

namespace tailzero {
namespace json {

namespace {

constexpr int kMaxDepth = 200;
constexpr std::size_t kMaxInputBytes = 8u * 1024u * 1024u;

bool is_json_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** Recursive descent reader. One instance per document, never reused. */
class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    /** Parse the whole document; on failure `error` holds a precise message. */
    bool parse_document(Value& out, ParseError& error) {
        if (text_.size() > kMaxInputBytes) {
            error.message = "document is too large";
            return false;
        }
        // Tolerate a UTF-8 BOM: several "cat" style captures keep one.
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB &&
            static_cast<unsigned char>(text_[2]) == 0xBF) {
            pos_ = 3;
        }
        skip_ws();
        if (pos_ >= text_.size()) {
            error.message = "empty JSON document";
            return false;
        }
        Value value;
        if (!parse_value(value, 0)) {
            error.message = message_;
            error.line = line_at(err_pos_);
            error.column = column_at(err_pos_);
            return false;
        }
        skip_ws();
        if (pos_ != text_.size()) {
            fail("trailing characters after JSON value");
            error.message = message_;
            error.line = line_at(pos_);
            error.column = column_at(pos_);
            return false;
        }
        out = std::move(value);
        return true;
    }

private:
    bool fail(const char* message) {
        if (message_.empty()) {
            message_ = message;
            err_pos_ = pos_;
        }
        return false;
    }

    void skip_ws() {
        while (pos_ < text_.size() && is_json_ws(text_[pos_])) ++pos_;
    }

    char peek() const {
        return pos_ < text_.size() ? text_[pos_] : '\0';
    }

    bool match_literal(const char* literal, std::size_t length) {
        if (text_.compare(pos_, length, literal) != 0) {
            return fail("invalid literal");
        }
        pos_ += length;
        return true;
    }

    bool parse_value(Value& out, int depth) {
        if (depth > kMaxDepth) return fail("JSON nesting too deep");
        skip_ws();
        if (pos_ >= text_.size()) return fail("unexpected end of JSON input");
        const char c = text_[pos_];
        switch (c) {
            case '{':
                return parse_object(out, depth);
            case '[':
                return parse_array(out, depth);
            case '"': {
                std::string text;
                if (!parse_string(text)) return false;
                out = Value::make_string(std::move(text));
                return true;
            }
            case 't':
                if (match_literal("true", 4)) {
                    out = Value::make_bool(true);
                    return true;
                }
                return false;
            case 'f':
                if (match_literal("false", 5)) {
                    out = Value::make_bool(false);
                    return true;
                }
                return false;
            case 'n':
                if (match_literal("null", 4)) {
                    out = Value::make_null();
                    return true;
                }
                return false;
            default:
                if (c == '-' || is_digit(c)) return parse_number(out);
                return fail("unexpected character in JSON value");
        }
    }

    bool parse_object(Value& out, int depth) {
        Value object = Value::make_object();
        ++pos_;  // '{'
        skip_ws();
        if (peek() == '}') {
            ++pos_;
            out = std::move(object);
            return true;
        }
        while (true) {
            skip_ws();
            if (peek() != '"') return fail("expected object key string");
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (peek() != ':') return fail("expected ':' after object key");
            ++pos_;
            Value value;
            if (!parse_value(value, depth + 1)) return false;
            object.set(std::move(key), std::move(value));
            skip_ws();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                out = std::move(object);
                return true;
            }
            return fail("expected ',' or '}' in object");
        }
    }

    bool parse_array(Value& out, int depth) {
        Value array = Value::make_array();
        ++pos_;  // '['
        skip_ws();
        if (peek() == ']') {
            ++pos_;
            out = std::move(array);
            return true;
        }
        while (true) {
            Value value;
            if (!parse_value(value, depth + 1)) return false;
            array.push(std::move(value));
            skip_ws();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                out = std::move(array);
                return true;
            }
            return fail("expected ',' or ']' in array");
        }
    }

    bool parse_string(std::string& out) {
        ++pos_;  // '"'
        out.clear();
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) return fail("unterminated escape sequence");
                const char escape = text_[pos_++];
                switch (escape) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                        if (!parse_unicode_escape(out)) return false;
                        break;
                    default:
                        return fail("invalid escape sequence in string");
                }
                continue;
            }
            if (c < 0x20u) return fail("raw control character in string");
            out.push_back(static_cast<char>(c));
            ++pos_;
        }
        return fail("unterminated string");
    }

    /** Read one \uXXXX (pos_ points at the first hex digit). */
    bool read_hex4(std::uint32_t& value) {
        if (pos_ + 4 > text_.size()) return fail("truncated \\u escape");
        std::uint32_t result = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_value(text_[pos_ + static_cast<std::size_t>(i)]);
            if (digit < 0) return fail("invalid hex digit in \\u escape");
            result = (result << 4) | static_cast<std::uint32_t>(digit);
        }
        pos_ += 4;
        value = result;
        return true;
    }

    bool parse_unicode_escape(std::string& out) {
        std::uint32_t code = 0;
        if (!read_hex4(code)) return false;
        if (code >= 0xD800u && code <= 0xDBFFu) {
            // High surrogate: combine with a following low surrogate when
            // present. Lone surrogates decode to U+FFFD instead of failing.
            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                const std::size_t saved = pos_;
                pos_ += 2;
                std::uint32_t low = 0;
                if (!read_hex4(low)) return false;
                if (low >= 0xDC00u && low <= 0xDFFFu) {
                    const std::uint32_t combined =
                        0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
                    append_utf8(out, combined);
                    return true;
                }
                pos_ = saved;  // not a low surrogate: leave it for the next char
            }
            append_utf8(out, 0xFFFDu);
            return true;
        }
        if (code >= 0xDC00u && code <= 0xDFFFu) {
            append_utf8(out, 0xFFFDu);
            return true;
        }
        append_utf8(out, code);
        return true;
    }

    bool parse_number(Value& out) {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (pos_ >= text_.size() || !is_digit(text_[pos_])) return fail("invalid number");
        while (pos_ < text_.size() && is_digit(text_[pos_])) ++pos_;
        if (peek() == '.') {
            ++pos_;
            if (pos_ >= text_.size() || !is_digit(text_[pos_])) return fail("invalid number");
            while (pos_ < text_.size() && is_digit(text_[pos_])) ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (pos_ >= text_.size() || !is_digit(text_[pos_])) return fail("invalid number");
            while (pos_ < text_.size() && is_digit(text_[pos_])) ++pos_;
        }
        double value = 0.0;
        const char* first = text_.data() + start;
        const char* last = text_.data() + pos_;
        const std::from_chars_result result = std::from_chars(first, last, value);
        if (result.ec != std::errc() || result.ptr != last) return fail("invalid number");
        out = Value::make_number(value);
        return true;
    }

    int line_at(std::size_t position) const {
        int line = 1;
        const std::size_t end = position < text_.size() ? position : text_.size();
        for (std::size_t i = 0; i < end; ++i) {
            if (text_[i] == '\n') ++line;
        }
        return line;
    }

    int column_at(std::size_t position) const {
        const std::size_t end = position < text_.size() ? position : text_.size();
        std::size_t line_start = 0;
        for (std::size_t i = 0; i < end; ++i) {
            if (text_[i] == '\n') line_start = i + 1;
        }
        return static_cast<int>(end - line_start + 1);
    }

    const std::string& text_;
    std::size_t pos_ = 0;
    std::string message_;
    std::size_t err_pos_ = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------
Value Value::make_null() {
    return Value();
}

Value Value::make_bool(bool value) {
    Value result;
    result.type_ = Type::Bool;
    result.bool_ = value;
    return result;
}

Value Value::make_number(double value) {
    Value result;
    result.type_ = Type::Number;
    result.number_ = value;
    return result;
}

Value Value::make_string(std::string value) {
    Value result;
    result.type_ = Type::String;
    result.string_ = std::move(value);
    return result;
}

Value Value::make_array() {
    Value result;
    result.type_ = Type::Array;
    return result;
}

Value Value::make_object() {
    Value result;
    result.type_ = Type::Object;
    return result;
}

bool Value::bool_value(bool fallback) const {
    return type_ == Type::Bool ? bool_ : fallback;
}

double Value::number_value(double fallback) const {
    return type_ == Type::Number ? number_ : fallback;
}

int64_t Value::int64_value(int64_t fallback) const {
    if (type_ != Type::Number) return fallback;
    return static_cast<int64_t>(number_);
}

const std::string& Value::string_value() const {
    static const std::string kEmpty;
    return type_ == Type::String ? string_ : kEmpty;
}

std::string Value::string_or(const std::string& fallback) const {
    return type_ == Type::String ? string_ : fallback;
}

std::size_t Value::size() const {
    if (type_ == Type::Array) return elements_.size();
    if (type_ == Type::Object) return members_.size();
    return 0;
}

const Value& Value::at(std::size_t index) const {
    if (type_ == Type::Array && index < elements_.size()) return elements_[index];
    return null_value();
}

const std::vector<Value>& Value::elements() const {
    static const std::vector<Value> kEmpty;
    return type_ == Type::Array ? elements_ : kEmpty;
}

const Value* Value::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& member : members_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

const std::vector<std::pair<std::string, Value>>& Value::members() const {
    static const std::vector<std::pair<std::string, Value>> kEmpty;
    return type_ == Type::Object ? members_ : kEmpty;
}

bool Value::get_bool(const std::string& key, bool fallback) const {
    const Value* value = find(key);
    return value != nullptr ? value->bool_value(fallback) : fallback;
}

double Value::get_number(const std::string& key, double fallback) const {
    const Value* value = find(key);
    return value != nullptr ? value->number_value(fallback) : fallback;
}

int64_t Value::get_int64(const std::string& key, int64_t fallback) const {
    const Value* value = find(key);
    return value != nullptr ? value->int64_value(fallback) : fallback;
}

std::string Value::get_string(const std::string& key, const std::string& fallback) const {
    const Value* value = find(key);
    return value != nullptr ? value->string_or(fallback) : fallback;
}

Value& Value::push(Value value) {
    type_ = Type::Array;
    elements_.push_back(std::move(value));
    return *this;
}

Value& Value::set(std::string key, Value value) {
    type_ = Type::Object;
    for (auto& member : members_) {
        if (member.first == key) {
            member.second = std::move(value);
            return *this;
        }
    }
    members_.emplace_back(std::move(key), std::move(value));
    return *this;
}

const Value& Value::null_value() {
    static const Value kNull;
    return kNull;
}

// ---------------------------------------------------------------------------
// parse
// ---------------------------------------------------------------------------
Value parse(const std::string& text, ParseError* error) {
    Parser parser(text);
    Value value;
    ParseError local;
    if (!parser.parse_document(value, local)) {
        if (error != nullptr) *error = local;
        return Value();
    }
    if (error != nullptr) *error = ParseError();
    return value;
}

}  // namespace json
}  // namespace tailzero
