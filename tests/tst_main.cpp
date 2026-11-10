#include "test_framework.h"

int main() {
    int passed = 0;
    std::string currentSuite;

    for (const auto& testCase : ::testing::registry()) {
        if (testCase.suite != currentSuite) {
            currentSuite = testCase.suite;
            std::printf("\n[%s]\n", currentSuite.c_str());
        }
        const int before = ::testing::failureCount();
        testCase.fn();
        const bool ok = ::testing::failureCount() == before;
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", testCase.name.c_str());
        std::fflush(stdout);
        if (ok) ++passed;
    }

    std::printf("\n----------------------------------------\n");
    std::printf("通过 %zu / %zu 个用例，失败 %d 个断言。\n", static_cast<size_t>(passed),
                ::testing::registry().size(), ::testing::failureCount());
    return ::testing::failureCount() == 0 ? 0 : 1;
}
