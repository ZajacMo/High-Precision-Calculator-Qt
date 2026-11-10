#include "test_framework.h"

#include <cstdint>
#include <stdexcept>

#include "core/BigDecimal.h"

using hpc::BigDecimal;

TEST(parse, basic) {
    CHECK_EQ(BigDecimal::fromString("123").toString(), std::string("123"));
    CHECK_EQ(BigDecimal::fromString("0.5").toString(), std::string("0.5"));
    CHECK_EQ(BigDecimal::fromString("-2.25").toString(), std::string("-2.25"));
    CHECK_EQ(BigDecimal::fromString("0").toString(), std::string("0"));
    CHECK_EQ(BigDecimal::fromString("007").toString(), std::string("7"));
    CHECK_EQ(BigDecimal::fromString("1.500").toString(), std::string("1.5"));
    CHECK_EQ(BigDecimal::fromString(".5").toString(), std::string("0.5"));
}

TEST(parse, invalid) {
    bool threw = false;
    try { BigDecimal::fromString("abc"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { BigDecimal::fromString("1.2.3"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { BigDecimal::fromString(""); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

TEST(arith, add) {
    CHECK_EQ((BigDecimal::fromString("1") + BigDecimal::fromString("2")).toString(), std::string("3"));
    CHECK_EQ((BigDecimal::fromString("0.1") + BigDecimal::fromString("0.2")).toString(), std::string("0.3"));
    CHECK_EQ((BigDecimal::fromString("-1") + BigDecimal::fromString("2")).toString(), std::string("1"));
    CHECK_EQ((BigDecimal::fromString("1") + BigDecimal::fromString("-2")).toString(), std::string("-1"));
}

TEST(arith, subtract) {
    CHECK_EQ((BigDecimal::fromString("5") - BigDecimal::fromString("3")).toString(), std::string("2"));
    CHECK_EQ((BigDecimal::fromString("3") - BigDecimal::fromString("5")).toString(), std::string("-2"));
    CHECK_EQ((BigDecimal::fromString("0.3") - BigDecimal::fromString("0.1")).toString(), std::string("0.2"));
}

TEST(arith, multiply) {
    CHECK_EQ((BigDecimal::fromString("2.5") * BigDecimal::fromString("4")).toString(), std::string("10"));
    CHECK_EQ((BigDecimal::fromString("0.1") * BigDecimal::fromString("0.2")).toString(), std::string("0.02"));
    CHECK_EQ((BigDecimal::fromString("-3") * BigDecimal::fromString("4")).toString(), std::string("-12"));
    CHECK_EQ((BigDecimal::fromString("-3") * BigDecimal::fromString("-4")).toString(), std::string("12"));
}

TEST(arith, exactness_beyond_double) {
    // 经典浮点陷阱：0.1 + 0.2 在 double 中不等于 0.3
    const BigDecimal a = BigDecimal::fromString("0.1");
    const BigDecimal b = BigDecimal::fromString("0.2");
    CHECK_EQ((a + b).toString(), std::string("0.3"));

    // 大数乘法不丢位
    const BigDecimal big = BigDecimal::fromString("123456789012345678901234567890");
    const BigDecimal one = BigDecimal::fromString("1");
    CHECK_EQ((big * one).toString(), std::string("123456789012345678901234567890"));
}

TEST(compare, sign) {
    CHECK(BigDecimal::fromString("1").compare(BigDecimal::fromString("2")) < 0);
    CHECK(BigDecimal::fromString("2").compare(BigDecimal::fromString("1")) > 0);
    CHECK(BigDecimal::fromString("1.0").compare(BigDecimal::fromString("1")) == 0);
    CHECK(BigDecimal::fromString("-1").compare(BigDecimal::fromString("1")) < 0);
    CHECK(BigDecimal::fromString("0.5").compare(BigDecimal::fromString("0.50")) == 0);
}

TEST(compare, both_negative) {
    // 两个**同号**（都是负数）时，比较必须先比绝对值再翻转符号。
    // 只测异号会漏掉这条路径。
    CHECK(BigDecimal::fromString("-1").compare(BigDecimal::fromString("-2")) > 0);
    CHECK(BigDecimal::fromString("-2").compare(BigDecimal::fromString("-1")) < 0);
    CHECK(BigDecimal::fromString("-1.5").compare(BigDecimal::fromString("-1.50")) == 0);
    CHECK(BigDecimal::fromString("-0.1").compare(BigDecimal::fromString("-0.2")) > 0);
    // 同为负数时大小关系与相反数一致。
    CHECK(BigDecimal::fromString("-3").compare(BigDecimal::fromString("-3")) == 0);
    CHECK(BigDecimal::fromString("-10").compare(BigDecimal::fromString("-9")) < 0);
}

// --- 以下为边界情况补充：零操作数、负零、块边界、极值 scale ---

TEST(parse, negative_zero_is_zero) {
    // "-0" 必须规范化为 0，既不输出 "-0"，也不保留负号。
    const BigDecimal negZero = BigDecimal::fromString("-0");
    CHECK(negZero.isZero());
    CHECK_EQ(negZero.sign(), 0);
    CHECK_EQ(negZero.toString(), std::string("0"));
    CHECK_EQ(BigDecimal::fromString("-0.00").toString(), std::string("0"));
    CHECK_EQ(BigDecimal::fromString("0.000").toString(), std::string("0"));
}

TEST(parse, int64_constructor) {
    CHECK_EQ(BigDecimal(42).toString(), std::string("42"));
    CHECK_EQ(BigDecimal(-42).toString(), std::string("-42"));
    CHECK_EQ(BigDecimal(0).toString(), std::string("0"));
    CHECK(BigDecimal(0).isZero());
    // INT64_MIN 取负会溢出，必须走 -(v+1)+1 的路径。
    CHECK_EQ(BigDecimal(std::int64_t(-9223372036854775807LL - 1)).toString(),
             std::string("-9223372036854775808"));
    CHECK_EQ(BigDecimal(std::int64_t(9223372036854775807LL)).toString(),
             std::string("9223372036854775807"));
    // string_view 构造函数与 fromString 等价。
    CHECK_EQ(BigDecimal(std::string_view("12.50")).toString(), std::string("12.5"));
}

TEST(arith, zero_operands) {
    const BigDecimal zero = BigDecimal::fromString("0");
    const BigDecimal five = BigDecimal::fromString("5");
    CHECK_EQ((zero + five).toString(), std::string("5"));
    CHECK_EQ((five + zero).toString(), std::string("5"));
    CHECK_EQ((zero - five).toString(), std::string("-5"));
    CHECK_EQ((five - zero).toString(), std::string("5"));
    CHECK_EQ((zero * five).toString(), std::string("0"));
    CHECK_EQ((zero * zero).toString(), std::string("0"));
    CHECK_EQ((zero - zero).toString(), std::string("0"));
    // 零与负数相加仍不得产生 "-0"。
    CHECK_EQ((zero + BigDecimal::fromString("-5")).toString(), std::string("-5"));
    CHECK_EQ((zero + BigDecimal::fromString("-0")).toString(), std::string("0"));
}

TEST(arith, block_boundary) {
    // 跨越 10^9 块边界：进位必须正确传播到高位块。
    CHECK_EQ((BigDecimal::fromString("999999999") + BigDecimal::fromString("1")).toString(),
             std::string("1000000000"));
    CHECK_EQ((BigDecimal::fromString("999999999") + BigDecimal::fromString("999999999")).toString(),
             std::string("1999999998"));
    CHECK_EQ((BigDecimal::fromString("1000000000") - BigDecimal::fromString("1")).toString(),
             std::string("999999999"));
    CHECK_EQ((BigDecimal::fromString("12345678901234567890") * BigDecimal::fromString("2")).toString(),
             std::string("24691357802469135780"));
    // scale 之差跨块时，对齐不能丢精度。
    CHECK_EQ((BigDecimal::fromString("1") + BigDecimal::fromString("0.0000000001")).toString(),
             std::string("1.0000000001"));
}

TEST(arith, multiply_inner_carry) {
    // 乘法的**内层**进位：两个满块相乘时中间结果超过 10^9，
    // 进位必须传给下一块。只测 "大数 * 2" 覆盖不到内层进位。
    CHECK_EQ((BigDecimal::fromString("999999999") * BigDecimal::fromString("999999999")).toString(),
             std::string("999999998000000001"));
    CHECK_EQ((BigDecimal::fromString("999999999") * BigDecimal::fromString("2")).toString(),
             std::string("1999999998"));
}

TEST(parse, trailing_zeros_span_blocks) {
    // 尾随零跨块时，normalize 的逐次除以 10 必须从**高位**往低位传进位。
    // 小端表示下若方向反了，这里的结果会变成 1.000000000 而不是 12。
    CHECK_EQ(BigDecimal::fromString("12.0000000000").toString(), std::string("12"));
    CHECK_EQ(BigDecimal::fromString("1.500").toString(), std::string("1.5"));
    CHECK_EQ(BigDecimal::fromString("1000.000000000").toString(), std::string("1000"));
}

TEST(abs_, value) {
    CHECK_EQ(BigDecimal::abs(BigDecimal::fromString("-3.5")).toString(), std::string("3.5"));
    CHECK_EQ(BigDecimal::abs(BigDecimal::fromString("3.5")).toString(), std::string("3.5"));
    CHECK_EQ(BigDecimal::abs(BigDecimal::fromString("-0")).toString(), std::string("0"));
}

TEST(equals_, value) {
    CHECK(BigDecimal::fromString("1.0").equals(BigDecimal::fromString("1")));
    CHECK(BigDecimal::fromString("0.50").equals(BigDecimal::fromString("0.5")));
    CHECK(!BigDecimal::fromString("1.0").equals(BigDecimal::fromString("1.1")));
    CHECK(BigDecimal::fromString("0").equals(BigDecimal::fromString("-0")));
}
