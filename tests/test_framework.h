// 极简测试框架：无外部依赖，单文件聚合运行，便于在任意工具链下执行。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    std::string suite;
    std::string name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& failureCount() {
    static int count = 0;
    return count;
}

inline void reportFailure(const std::string& file, int line, const std::string& message) {
    ++failureCount();
    std::printf("  FAIL %s:%d  %s\n", file.c_str(), line, message.c_str());
}

struct Registrar {
    Registrar(const char* suite, const char* name, void (*fn)()) {
        registry().push_back({suite, name, fn});
    }
};

inline bool nearlyEqual(double a, double b, double tol = 1e-9) {
    if (a == b) return true;
    double diff = std::fabs(a - b);
    double scale = std::max({std::fabs(a), std::fabs(b), 1.0});
    return diff <= tol * scale;
}

}  // namespace testing

#define TEST(suite, name)                                                     \
    static void suite##_##name##_body();                                      \
    static ::testing::Registrar suite##_##name##_registrar(                   \
        #suite, #name, &suite##_##name##_body);                               \
    static void suite##_##name##_body()

#define CHECK(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                   \
            ::testing::reportFailure(__FILE__, __LINE__,                      \
                                      "CHECK failed: " #condition);            \
        }                                                                      \
    } while (false)

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                       \
        const auto actualValue = (actual);                                     \
        const auto expectedValue = (expected);                                 \
        if (!(actualValue == expectedValue)) {                                \
            ::testing::reportFailure(                                         \
                __FILE__, __LINE__,                                            \
                std::string("CHECK_EQ failed: " #actual " == " #expected));   \
        }                                                                      \
    } while (false)

#define CHECK_NEAR(actual, expected, tol)                                     \
    do {                                                                       \
        const double actualValue = (actual);                                   \
        const double expectedValue = (expected);                               \
        if (!::testing::nearlyEqual(actualValue, expectedValue, (tol))) {     \
            ::testing::reportFailure(                                         \
                __FILE__, __LINE__,                                            \
                std::string("CHECK_NEAR failed: " #actual " ~= " #expected)); \
        }                                                                      \
    } while (false)

#define CHECK_STREQ(actual, expected)                                         \
    do {                                                                       \
        const std::string actualValue = (actual);                              \
        const std::string expectedValue = (expected);                          \
        if (actualValue != expectedValue) {                                   \
            ::testing::reportFailure(                                         \
                __FILE__, __LINE__,                                            \
                "CHECK_STREQ failed: got \"" + actualValue +                   \
                "\" expected \"" + expectedValue + "\"");                      \
        }                                                                      \
    } while (false)
