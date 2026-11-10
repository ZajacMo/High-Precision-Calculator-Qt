#include "test_framework.h"

namespace {
// volatile 读取不会被常量折叠，从而保证断言在运行时求值，
// 否则 MSVC /W4 会因常量条件表达式报 C4127。
volatile int kOne = 1;
}

// 占位：本文件将在 **Task 2**（BigDecimal 表示层与精确四则）中被真实的
// BigDecimal 用例整体替换。
//
// 说明：当前三个占位测试文件（tst_bigdecimal / tst_format / tst_expression）
// 刻意保持为近乎逐字相同的重复内容 —— 它们的存在只是为了验证测试框架的接线，
// 由 Task 2 / Task 5 / Task 6 分别替换为真实用例，并非疏漏。

// 占位：Task 2/5/6 起替换为真实用例。此处验证测试框架接线正确。
TEST(bigdecimal_placeholder, frameworkIsWired) {
    CHECK_EQ(kOne + 1, 2);
    CHECK(std::string("abc") == "abc");
    CHECK_STREQ("abc", "abc");
}
