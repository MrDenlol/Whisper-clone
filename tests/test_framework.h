#pragma once

// Minimal dependency-free test harness. No third-party framework means no extra
// license to justify in LICENSES.md.

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace wftest {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> body) {
        registry().push_back(TestCase{std::move(name), std::move(body)});
    }
};

inline void reportFailure(const char* file, int line, const std::string& message) {
    ++failureCount();
    std::cerr << "  FAIL " << file << ':' << line << "  " << message << '\n';
}

inline int runAll() {
    int passed = 0;
    for (const TestCase& testCase : registry()) {
        const int before = failureCount();
        std::cout << "[ RUN  ] " << testCase.name << '\n';
        testCase.body();
        if (failureCount() == before) {
            ++passed;
            std::cout << "[  OK  ] " << testCase.name << '\n';
        } else {
            std::cout << "[FAILED] " << testCase.name << '\n';
        }
    }

    const int total = static_cast<int>(registry().size());
    std::cout << "\n" << passed << "/" << total << " test cases passed";
    if (failureCount() > 0) {
        std::cout << ", " << failureCount() << " assertion(s) failed";
    }
    std::cout << '\n';
    return failureCount() == 0 ? 0 : 1;
}

}  // namespace wftest

#define WF_TEST(name)                                                     \
    static void name();                                                   \
    [[maybe_unused]] static ::wftest::Registrar registrar_##name(#name, &name); \
    static void name()

#define WF_CHECK(cond)                                                              \
    do {                                                                            \
        if (!(cond)) {                                                              \
            ::wftest::reportFailure(__FILE__, __LINE__, "expected true: " #cond);   \
        }                                                                           \
    } while (false)

#define WF_CHECK_EQ(lhs, rhs)                                                       \
    do {                                                                            \
        const auto& wfLhs = (lhs);                                                  \
        const auto& wfRhs = (rhs);                                                  \
        if (!(wfLhs == wfRhs)) {                                                    \
            std::ostringstream wfStream;                                            \
            wfStream << "expected " #lhs " == " #rhs " (got '" << wfLhs << "' vs '" \
                     << wfRhs << "')";                                              \
            ::wftest::reportFailure(__FILE__, __LINE__, wfStream.str());            \
        }                                                                           \
    } while (false)
