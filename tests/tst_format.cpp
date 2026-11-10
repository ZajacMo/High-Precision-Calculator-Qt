#include "test_framework.h"

namespace {
// volatile 读取不会被常量折叠，从而保证断言在运行时求值，
// 否则 MSVC /W4 会因常量条件表达式报 C4127。
volatile int kOne = 1;
}

// 占位：Task 2/5/6 起替换为真实用例。此处验证测试框架接线正确。
TEST(format_placeholder, frameworkIsWired) {
    CHECK_EQ(kOne + 1, 2);
    CHECK(std::string("abc") == "abc");
    CHECK_STREQ("abc", "abc");
}
