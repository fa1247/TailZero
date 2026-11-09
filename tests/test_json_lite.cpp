/**
 * TailZero - json_lite reader tests.
 * SPDX-License-Identifier: MIT
 */
#include <string>

#include "tailscale/json_lite.h"
#include "test_harness.h"

using tailzero::json::ParseError;
using tailzero::json::Value;

TZ_TEST(json_parse_object) {
    ParseError error;
    const Value value = tailzero::json::parse(
        R"({"a": 1, "b": "text", "c": true, "d": null, "e": [1, 2, 3]})", &error);
    TZ_REQUIRE(error.ok());
    TZ_REQUIRE(value.is_object());
    TZ_CHECK_EQ(value.get_int64("a", -1), 1);
    TZ_CHECK_EQ(value.get_string("b"), std::string("text"));
    TZ_CHECK(value.get_bool("c", false));
    TZ_REQUIRE(value.find("d") != nullptr);
    TZ_CHECK(value.find("d")->is_null());
    TZ_REQUIRE(value.find("e") != nullptr);
    TZ_CHECK(value.find("e")->is_array());
    TZ_CHECK_EQ(value.find("e")->size(), std::size_t(3));
    TZ_CHECK_EQ(value.find("e")->at(1).int64_value(-1), 2);
    TZ_CHECK(value.find("missing") == nullptr);
}

TZ_TEST(json_parse_numbers) {
    ParseError error;
    TZ_REQUIRE(tailzero::json::parse("42", &error).int64_value(-1) == 42);
    TZ_REQUIRE(tailzero::json::parse("-7", &error).int64_value(0) == -7);
    TZ_CHECK(tailzero::json::parse("1e3", &error).number_value(0.0) == 1000.0);
    TZ_CHECK(tailzero::json::parse("-2.5e2", &error).number_value(0.0) == -250.0);
    TZ_CHECK(tailzero::json::parse("0.125", &error).number_value(0.0) == 0.125);
    TZ_CHECK(tailzero::json::parse("58521206", &error).int64_value(0) == 58521206);
}

TZ_TEST(json_parse_escapes_and_unicode) {
    ParseError error;
    const Value value = tailzero::json::parse(
        R"({"s": "line\nbreak\t\"quoted\"\\slash", "u": "\u00e9\u4e2d", "emoji": "\ud83d\ude00"})",
        &error);
    TZ_REQUIRE(error.ok());
    TZ_CHECK_EQ(value.get_string("s"), std::string("line\nbreak\t\"quoted\"\\slash"));
    TZ_CHECK_EQ(value.get_string("u"), std::string("\xC3\xA9\xE4\xB8\xAD"));
    TZ_CHECK_EQ(value.get_string("emoji"), std::string("\xF0\x9F\x98\x80"));
}

TZ_TEST(json_parse_duplicate_keys_last_wins) {
    ParseError error;
    const Value value = tailzero::json::parse(R"({"a": 1, "a": 2})", &error);
    TZ_REQUIRE(error.ok());
    TZ_CHECK_EQ(value.get_int64("a", -1), 2);
}

TZ_TEST(json_parse_array_and_nesting) {
    ParseError error;
    const Value value = tailzero::json::parse(R"([{"x": [true, null]}, "top"])", &error);
    TZ_REQUIRE(error.ok());
    TZ_REQUIRE(value.is_array());
    TZ_CHECK_EQ(value.size(), std::size_t(2));
    TZ_CHECK(value.at(0).is_object());
    TZ_CHECK(value.at(1).is_string());
    TZ_CHECK_EQ(value.at(1).string_value(), std::string("top"));
}

TZ_TEST(json_parse_bom_tolerated) {
    ParseError error;
    const Value value = tailzero::json::parse("\xEF\xBB\xBF{\"a\": 1}", &error);
    TZ_REQUIRE(error.ok());
    TZ_CHECK_EQ(value.get_int64("a", -1), 1);
}

TZ_TEST(json_parse_failures_are_reported) {
    const char* const bad_documents[] = {
        "",
        "{",
        "[1,]",
        "{\"a\":1}x",
        "\"unterminated",
        "tru",
        "[1 2]",
        "{\"a\"}",
        "{\"a\": }",
    };
    for (const char* document : bad_documents) {
        ParseError error;
        const Value value = tailzero::json::parse(document, &error);
        TZ_CHECK(!error.ok());
        TZ_CHECK(value.is_null());
        TZ_CHECK(!error.message.empty());
    }
}

TZ_TEST(json_parse_depth_limit) {
    std::string deep;
    for (int i = 0; i < 400; ++i) deep += "[";
    for (int i = 0; i < 400; ++i) deep += "]";
    ParseError error;
    tailzero::json::parse(deep, &error);
    TZ_CHECK(!error.ok());
    TZ_CHECK(error.message.find("deep") != std::string::npos);
}

TZ_TEST(json_parse_error_position) {
    ParseError error;
    tailzero::json::parse("{\n  \"a\": 1,\n  \"b\": bad\n}", &error);
    TZ_REQUIRE(!error.ok());
    TZ_CHECK_EQ(error.line, 3);
    TZ_CHECK(error.column > 0);
}

TZ_TEST(json_scalar_accessors_default_on_mismatch) {
    ParseError error;
    const Value object = tailzero::json::parse(R"({"s": 5})", &error);
    TZ_REQUIRE(error.ok());
    TZ_CHECK_EQ(object.get_string("s", "fallback"), std::string("fallback"));
    const Value text = tailzero::json::parse("\"hello\"", &error);
    TZ_REQUIRE(error.ok());
    TZ_CHECK_EQ(text.int64_value(7), 7);
    TZ_CHECK_EQ(text.string_value(), std::string("hello"));
    TZ_CHECK_EQ(text.size(), std::size_t(0));
    TZ_CHECK(text.at(3).is_null());
    TZ_CHECK_EQ(text.elements().size(), std::size_t(0));
    TZ_CHECK_EQ(text.members().size(), std::size_t(0));
}
