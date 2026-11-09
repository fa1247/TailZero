/**
 * TailZero - minimal unit test harness implementation.
 * SPDX-License-Identifier: MIT
 */
#include "test_harness.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>

namespace tztest {

namespace {

struct TestCase {
    std::string name;
    void (*function)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int& failure_count() {
    static int count = 0;
    return count;
}

bool is_directory(const std::string& path) {
    struct stat info;
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

}  // namespace

void register_test(const char* name, void (*function)()) {
    registry().push_back(TestCase{name, function});
}

void fail(const char* file, int line, const std::string& message) {
    ++failure_count();
    std::cerr << file << ":" << line << ": " << message << "\n";
}

int run_all(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int failed_tests = 0;
    int ran_tests = 0;

    std::cout << "tz_tests: " << registry().size() << " test(s) registered\n";
    for (const TestCase& test : registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
        ++ran_tests;
        const int before = failure_count();
        test.function();
        const int after = failure_count();
        if (after > before) {
            ++failed_tests;
            std::cout << "[FAIL] " << test.name << " (" << (after - before) << " failure(s))\n";
        } else {
            std::cout << "[ OK ] " << test.name << "\n";
        }
    }

    std::cout << "------------------------------------------------------------\n";
    if (ran_tests == 0) {
        std::cout << "tz_tests: no test matched filter '" << filter << "'\n";
        return 1;
    }
    if (failed_tests == 0) {
        std::cout << "tz_tests: " << ran_tests << " test(s) passed\n";
        return 0;
    }
    std::cout << "tz_tests: " << failed_tests << "/" << ran_tests << " test(s) FAILED\n";
    return 1;
}

const std::string& fixtures_dir() {
    static const std::string directory = []() {
        static const char* const kCandidates[] = {
            "tests/fixtures",
            "../tests/fixtures",
            "../../tests/fixtures",
            "fixtures",
        };
        for (const char* candidate : kCandidates) {
            if (!is_directory(candidate)) continue;
            std::string probe;
            if (read_file(std::string(candidate) + "/connected/status.json", probe)) {
                return std::string(candidate);
            }
        }
        return std::string();
    }();
    return directory;
}

bool read_file(const std::string& path, std::string& contents) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    contents.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

bool read_fixture(const std::string& relative, std::string& contents) {
    const std::string& directory = fixtures_dir();
    if (directory.empty()) return false;
    return read_file(directory + "/" + relative, contents);
}

std::string fixture_text(const std::string& relative) {
    std::string contents;
    if (!read_fixture(relative, contents)) return std::string();
    return contents;
}

std::string describe(const std::string& value) {
    return "\"" + value + "\"";
}

std::string describe(const char* value) {
    return std::string("\"") + (value != nullptr ? value : "(null)") + "\"";
}

std::string describe(bool value) {
    return value ? "true" : "false";
}

std::string describe(int value) {
    return std::to_string(value);
}

std::string describe(unsigned int value) {
    return std::to_string(value);
}

std::string describe(long value) {
    return std::to_string(value);
}

std::string describe(unsigned long value) {
    return std::to_string(value);
}

std::string describe(long long value) {
    return std::to_string(value);
}

std::string describe(unsigned long long value) {
    return std::to_string(value);
}

std::string describe(double value) {
    return std::to_string(value);
}

}  // namespace tztest
