#include "test_framework.h"

namespace {
// volatile 读取不会被常量折叠，从而保证断言在运行时求值，
// 否则 MSVC /W4 会因常量条件表达式报 C4127。
volatile int kOne = 1;
}

// 占位：本文件将在 **Task 5**（进制格式化与位运算）中被真实的 Format 用例
// 整体替换。
//
// 说明：当前三个占位测试文件（tst_bigdecimal / tst_format / tst_expression）
// 刻意保持为近乎逐字相同的重复内容 —— 它们的存在只是为了验证测试框架的接线，
// 由 Task 2 / 5 / 6 分别替换为真实用例，并非疏漏。

// 占位：Task 2/5/6 起替换为真实用例。此处验证测试框架接线正确。
TEST(format_placeholder, frameworkIsWired) {
    CHECK_EQ(kOne + 1, 2);
    CHECK(std::string("abc") == "abc");
    CHECK_STREQ("abc", "abc");
}

// 负向自测：验证断言失败**确实会被计数**。
// 若 reportFailure 退化成空操作，上面的用例照样全绿，而这套测试的
// 全部意义就在于「失败能被检出」。failureCount() 返回引用，可存可复原；
// 复原后 main 的 before/after 差值为 0，本用例仍判为 ok。
TEST(format_placeholder, frameworkDetectsFailures) {
    const int before = ::testing::failureCount();
    CHECK(kOne == 2);                                     // 故意失败
    CHECK(::testing::failureCount() == before + 1);
    ::testing::failureCount() = before;                   // 复原，避免污染全局计数
}
