/**
 * TailZero - tiny self-contained JSON reader (DOM only, read-only).
 * SPDX-License-Identifier: MIT
 *
 * Why not a third party library: the app ships to a memory constrained ARM
 * device and only needs to *read* small Tailscale JSON documents. This reader
 * is dependency free, allocation-light and tolerant in the same way the CLI
 * is: unknown fields are simply ignored, and a malformed document yields a
 * precise error instead of a crash or an exception.
 *
 * Rules:
 *  - the parser never throws; every failure is reported through ParseError;
 *  - strict JSON grammar (RFC 8259) for structure, but leading BOMs, raw UTF-8
 *    bytes and larger nesting are tolerated where it is harmless;
 *  - object keys are compared byte-wise (Tailscale emits CamelCase ASCII keys);
 *  - duplicate keys keep the last value (JSON parsers commonly do this);
 *  - numbers are stored as double (Tailscale integers fit exactly below 2^53);
 *  - a nesting depth limit and an input size limit prevent hostile input from
 *    exhausting the stack / memory.
 *
 * The DOM is intentionally tiny: object members are stored in a vector of
 * key/value pairs (linear lookup) because every Tailscale object we read has
 * fewer than a hundred keys. Values are copyable and movable.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tailzero {
namespace json {

/** JSON value kind. */
enum class Type {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

/**
 * One immutable-ish JSON value. Builders (make_*, push, set) exist so the
 * reader and unit tests can construct values; application code only reads.
 */
class Value {
public:
    Value() = default;  // Null

    // ---- construction ----
    static Value make_null();
    static Value make_bool(bool value);
    static Value make_number(double value);
    static Value make_string(std::string value);
    static Value make_array();
    static Value make_object();

    // ---- type inspection ----
    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    // ---- scalar access (returns the fallback for a mismatched type) ----
    bool bool_value(bool fallback = false) const;
    double number_value(double fallback = 0.0) const;
    int64_t int64_value(int64_t fallback = 0) const;
    /** String bytes; an empty string for every non-string value. */
    const std::string& string_value() const;
    std::string string_or(const std::string& fallback) const;

    // ---- containers ----
    /** Array element count / object member count; 0 for scalars. */
    std::size_t size() const;
    /** Array element; a null value when out of range or not an array. */
    const Value& at(std::size_t index) const;
    /** Array elements; an empty vector for non-arrays. */
    const std::vector<Value>& elements() const;
    /** Object member lookup; nullptr when absent or not an object. */
    const Value* find(const std::string& key) const;
    /** Object members in document order; empty for non-objects. */
    const std::vector<std::pair<std::string, Value>>& members() const;

    // ---- typed member access (object only) ----
    bool get_bool(const std::string& key, bool fallback = false) const;
    double get_number(const std::string& key, double fallback = 0.0) const;
    int64_t get_int64(const std::string& key, int64_t fallback = 0) const;
    std::string get_string(const std::string& key,
                           const std::string& fallback = std::string()) const;

    // ---- builders (reader / tests) ----
    /** Append one array element. Also promotes the value to Array. */
    Value& push(Value value);
    /** Add or replace one object member. Also promotes the value to Object. */
    Value& set(std::string key, Value value);

    /** Shared null value (the reference returned for missing members). */
    static const Value& null_value();

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> elements_;
    std::vector<std::pair<std::string, Value>> members_;
};

/** Result of parsing: `message` is empty on success, 1-based position on error. */
struct ParseError {
    std::string message;
    int line = 0;
    int column = 0;

    bool ok() const { return message.empty(); }
};

/**
 * Parse exactly one JSON document.
 * Returns a null Value on failure and fills `error` (when non-null) with a
 * human readable message plus a 1-based line/column.
 */
Value parse(const std::string& text, ParseError* error = nullptr);

}  // namespace json
}  // namespace tailzero
