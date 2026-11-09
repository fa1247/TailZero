/**
 * TailZero - minimal self-contained unit test harness (no external framework).
 * SPDX-License-Identifier: MIT
 *
 * Usage:
 *   TZ_TEST(name) { ... TZ_CHECK(expr); ... }
 *   TZ_CHECK        - records a failure and continues
 *   TZ_REQUIRE      - records a failure and returns from the test
 *   TZ_CHECK_EQ     - checks ==, prints both values on failure
 *
 * The harness prints "file:line: message" for every failure and returns a
 * single process exit code (0 = all green).
 */
#pragma once

#include <cstddef>
#include <string>
#include <type_traits>

namespace tztest {

void register_test(const char* name, void (*function)());
void fail(const char* file, int line, const std::string& message);
int run_all(int argc, char** argv);

/** Directory containing the fixtures (`tests/fixtures`); "" when not found. */
const std::string& fixtures_dir();
bool read_file(const std::string& path, std::string& contents);
/** Read `tests/fixtures/<relative>`; returns false when missing. */
bool read_fixture(const std::string& relative, std::string& contents);
/** Read `tests/fixtures/<relative>`; empty string when missing. */
std::string fixture_text(const std::string& relative);

std::string describe(const std::string& value);
std::string describe(const char* value);
std::string describe(bool value);
std::string describe(int value);
std::string describe(unsigned int value);
std::string describe(long value);
std::string describe(unsigned long value);
std::string describe(long long value);
std::string describe(unsigned long long value);
std::string describe(double value);

template <typename T, typename std::enable_if<std::is_enum<T>::value, int>::type = 0>
std::string describe(T value) {
    return std::to_string(static_cast<long long>(value));
}

/** Read-only map lookup that never inserts (const-safe operator[] stand-in). */
template <typename Map, typename Key>
typename Map::mapped_type map_value(const Map& map, const Key& key) {
    const auto iterator = map.find(key);
    return iterator == map.end() ? typename Map::mapped_type() : iterator->second;
}

}  // namespace tztest

#define TZ_TEST(name)                                                             \
    static void tz_test_##name();                                                 \
    [[maybe_unused]] static const bool tz_test_registered_##name =                \
        (::tztest::register_test(#name, &tz_test_##name), true);                  \
    static void tz_test_##name()

#define TZ_FAIL(message) ::tztest::fail(__FILE__, __LINE__, (message))

#define TZ_CHECK(condition)                                                       \
    do {                                                                          \
        if (!(condition)) {                                                       \
            ::tztest::fail(__FILE__, __LINE__, "check failed: " #condition);      \
        }                                                                         \
    } while (false)

#define TZ_REQUIRE(condition)                                                     \
    do {                                                                          \
        if (!(condition)) {                                                       \
            ::tztest::fail(__FILE__, __LINE__,                                    \
                           "requirement failed: " #condition);                    \
            return;                                                               \
        }                                                                         \
    } while (false)

#define TZ_CHECK_EQ(actual, expected)                                             \
    do {                                                                          \
        auto&& tz_actual = (actual);                                              \
        auto&& tz_expected = (expected);                                          \
        if (!(tz_actual == tz_expected)) {                                        \
            ::tztest::fail(__FILE__, __LINE__,                                    \
                           std::string("expected " #actual " == " #expected       \
                                       " but got ") +                             \
                               ::tztest::describe(tz_actual) + " vs " +           \
                               ::tztest::describe(tz_expected));                  \
        }                                                                         \
    } while (false)
