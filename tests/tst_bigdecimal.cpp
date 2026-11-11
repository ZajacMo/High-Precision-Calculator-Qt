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

// --- Task 3：除法、舍入与 Context ---
//
// Context::precision 是**有效数字位**（与 Python decimal 的 prec 同义），
// 不是小数位数。

namespace {

using hpc::Context;
using hpc::RoundingMode;

BigDecimal div(const std::string& a, const std::string& b, int precision,
               RoundingMode mode = RoundingMode::HalfEven) {
    return BigDecimal::divide(BigDecimal::fromString(a), BigDecimal::fromString(b),
                               Context{precision, mode});
}

}  // namespace

TEST(divide, basic) {
    CHECK_EQ(div("10", "4", 10).toString(), std::string("2.5"));
    CHECK_EQ(div("1", "2", 10).toString(), std::string("0.5"));
    CHECK_EQ(div("7", "1", 10).toString(), std::string("7"));
}

TEST(divide, repeating) {
    // 1/7 = 0.142857142857... 保留 10 位有效数字
    CHECK_EQ(div("1", "7", 10).toString(), std::string("0.1428571429"));
    CHECK_EQ(div("2", "3", 10).toString(), std::string("0.6666666667"));
}

TEST(divide, significant_digits_not_decimal_places) {
    // 精度是**有效数字位**：整数部分会占掉有效数字位。
    CHECK_EQ(div("100", "7", 10).toString(), std::string("14.28571429"));
    CHECK_EQ(div("1", "7", 3).toString(), std::string("0.143"));
    // 精度 1 时只剩 1 位有效数字。
    CHECK_EQ(div("1", "7", 1).toString(), std::string("0.1"));
    CHECK_EQ(div("100", "7", 1).toString(), std::string("10"));
    // 精度 1000 也不能丢位：1/7 要给到 1000 位有效数字。
    const std::string big = div("1", "7", 1000).toString();
    CHECK_EQ(big.substr(0, 22), std::string("0.14285714285714285714"));
    CHECK_EQ(big.size(), std::size_t(1002));   // "0." + 1000 位
}

TEST(divide, divide_by_zero) {
    bool threw = false;
    try {
        div("1", "0", 10);
    } catch (const std::domain_error&) { threw = true; }
    CHECK(threw);

    threw = false;
    try {
        BigDecimal::modulo(BigDecimal::fromString("1"), BigDecimal::fromString("0"));
    } catch (const std::domain_error&) { threw = true; }
    CHECK(threw);
}

TEST(divide, invalid_precision) {
    bool threw = false;
    try {
        div("1", "3", 0);
    } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    threw = false;
    try {
        div("1", "3", -1);
    } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

TEST(divide, zero_dividend) {
    CHECK_EQ(div("0", "7", 10).toString(), std::string("0"));
    CHECK_EQ(div("0", "0.000001", 10).toString(), std::string("0"));
    // 0 除以 0 仍然要抛除零错误。
    bool threw = false;
    try { div("0", "0", 10); } catch (const std::domain_error&) { threw = true; }
    CHECK(threw);
}

TEST(divide, negative_operands) {
    CHECK_EQ(div("-1", "7", 10).toString(), std::string("-0.1428571429"));
    CHECK_EQ(div("1", "-7", 10).toString(), std::string("-0.1428571429"));
    CHECK_EQ(div("-1", "-7", 10).toString(), std::string("0.1428571429"));
}

TEST(divide, fractional_operands) {
    // 除数带小数位时不能多出一个 10^b（brief 原实现会算成 20）。
    CHECK_EQ(div("1", "0.5", 10).toString(), std::string("2"));
    CHECK_EQ(div("0.5", "0.25", 10).toString(), std::string("2"));
    CHECK_EQ(div("1", "2.5", 10).toString(), std::string("0.4"));
    CHECK_EQ(div("3", "0.5", 10).toString(), std::string("6"));
}

TEST(divide, magnitude_far_from_one) {
    // 量级远离 1 的商：有效数字位要按**商**的量级算，不能按被除数。
    CHECK_EQ(div("1", "1000", 10).toString(), std::string("0.001"));
    CHECK_EQ(div("1", "0.000000001", 10).toString(), std::string("1000000000"));
    CHECK_EQ(div("0.0000000001", "3", 10).toString(),
             std::string("0.00000000003333333333"));
}

TEST(divide, rounding_modes) {
    // 1/3 在 10 位有效数字下，不同模式给出不同结果。
    CHECK_EQ(div("1", "3", 10, RoundingMode::HalfEven).toString(),
             std::string("0.3333333333"));
    CHECK_EQ(div("1", "3", 10, RoundingMode::HalfUp).toString(),
             std::string("0.3333333333"));
    CHECK_EQ(div("1", "3", 10, RoundingMode::Down).toString(),
             std::string("0.3333333333"));
    CHECK_EQ(div("1", "3", 10, RoundingMode::Up).toString(),
             std::string("0.3333333334"));
    CHECK_EQ(div("1", "3", 10, RoundingMode::Ceil).toString(),
             std::string("0.3333333334"));
    CHECK_EQ(div("1", "3", 10, RoundingMode::Floor).toString(),
             std::string("0.3333333333"));
}

TEST(divide, rounding_direction_on_negatives) {
    // 负数时 Ceil/Floor 的方向必须反过来。
    CHECK_EQ(div("-1", "3", 10, RoundingMode::Ceil).toString(),
             std::string("-0.3333333333"));
    CHECK_EQ(div("-1", "3", 10, RoundingMode::Floor).toString(),
             std::string("-0.3333333334"));
    // 向零截断与远离零截断不区分符号。
    CHECK_EQ(div("-1", "3", 10, RoundingMode::Down).toString(),
             std::string("-0.3333333333"));
    CHECK_EQ(div("-1", "3", 10, RoundingMode::Up).toString(),
             std::string("-0.3333333334"));
}

TEST(divide, bankers_rounding_exact_half) {
    // 正好一半时走偶数分支：
    // 0.25 保留 1 位有效数字 -> 0.2（2 是偶数）
    // 0.35 保留 1 位有效数字 -> 0.4（3 是奇数，要进位成 4）
    CHECK_EQ(div("0.25", "1", 1, RoundingMode::HalfEven).toString(), std::string("0.2"));
    CHECK_EQ(div("0.35", "1", 1, RoundingMode::HalfEven).toString(), std::string("0.4"));
    // HalfUp 在正好一半时总是进位。
    CHECK_EQ(div("0.25", "1", 1, RoundingMode::HalfUp).toString(), std::string("0.3"));
    CHECK_EQ(div("0.35", "1", 1, RoundingMode::HalfUp).toString(), std::string("0.4"));
}

TEST(divide, bankers_rounding_more_than_half) {
    // 超过一半（不是正好一半）时必须进位，不能被误判成"正好一半"。
    // 2/3 = 0.666... 保留 1 位 -> 0.7
    CHECK_EQ(div("2", "3", 1, RoundingMode::HalfEven).toString(), std::string("0.7"));
    // 1/6 = 0.1666... 保留 1 位 -> 0.2
    CHECK_EQ(div("1", "6", 1, RoundingMode::HalfEven).toString(), std::string("0.2"));
    // 1/2 = 0.5 正好一半，保留 1 位 -> 0.5（末位 5 是奇数，要进位）
    CHECK_EQ(div("1", "2", 1, RoundingMode::HalfEven).toString(), std::string("0.5"));
    // 3/2 = 1.5 正好一半，保留 1 位 -> 2（1 是奇数，要进位）
    CHECK_EQ(div("3", "2", 1, RoundingMode::HalfEven).toString(), std::string("2"));
    // 5/2 = 2.5 正好一半，保留 1 位 -> 2（2 是偶数，不进位）
    CHECK_EQ(div("5", "2", 1, RoundingMode::HalfEven).toString(), std::string("2"));
}

TEST(floor_ceil, value) {
    // 0.333333 有 6 位有效数字，精度 6 时原样保留（不需要任何进位）。
    const BigDecimal third = BigDecimal::fromString("0.333333");
    CHECK_EQ(BigDecimal::floor(third, Context{6, RoundingMode::Floor}).toString(),
             std::string("0.333333"));
    CHECK_EQ(BigDecimal::ceil(third, Context{6, RoundingMode::Ceil}).toString(),
             std::string("0.333333"));
    // 精度降到 5 位时才有舍入发生：向下舍、向上舍都进位。
    CHECK_EQ(BigDecimal::floor(third, Context{5, RoundingMode::Floor}).toString(),
             std::string("0.33333"));
    CHECK_EQ(BigDecimal::ceil(third, Context{5, RoundingMode::Ceil}).toString(),
             std::string("0.33334"));
}

TEST(floor_ceil, direction_on_negatives) {
    // Floor 向 -∞，Ceil 向 +∞ —— 负数时方向与直觉相反。
    const BigDecimal value = BigDecimal::fromString("-2.555");
    CHECK_EQ(BigDecimal::floor(value, Context{3, RoundingMode::Floor}).toString(),
             std::string("-2.56"));
    CHECK_EQ(BigDecimal::ceil(value, Context{3, RoundingMode::Ceil}).toString(),
             std::string("-2.55"));
    CHECK_EQ(BigDecimal::round(value, Context{3, RoundingMode::HalfEven}).toString(),
             std::string("-2.56"));
    // 正数时方向相反。
    const BigDecimal positive = BigDecimal::fromString("2.555");
    CHECK_EQ(BigDecimal::floor(positive, Context{3, RoundingMode::Floor}).toString(),
             std::string("2.55"));
    CHECK_EQ(BigDecimal::ceil(positive, Context{3, RoundingMode::Ceil}).toString(),
             std::string("2.56"));
}

TEST(floor_ceil, carry_across_integer_part) {
    // 进位溢出整数位时要正确进位（9.99 向上取整 -> 10）。
    CHECK_EQ(BigDecimal::ceil(BigDecimal::fromString("9.99"), Context{2}).toString(),
             std::string("10"));
    CHECK_EQ(BigDecimal::floor(BigDecimal::fromString("9.99"), Context{2}).toString(),
             std::string("9.9"));
    CHECK_EQ(BigDecimal::ceil(BigDecimal::fromString("0.999"), Context{2}).toString(),
             std::string("1"));
    CHECK_EQ(BigDecimal::ceil(BigDecimal::fromString("99.9"), Context{2}).toString(),
             std::string("100"));
}

TEST(floor_ceil, integers_are_unchanged) {
    const BigDecimal value = BigDecimal::fromString("42");
    CHECK_EQ(BigDecimal::floor(value, Context{6, RoundingMode::Floor}).toString(),
             std::string("42"));
    CHECK_EQ(BigDecimal::ceil(value, Context{6, RoundingMode::Ceil}).toString(),
             std::string("42"));
    CHECK_EQ(BigDecimal::round(value, Context{6, RoundingMode::HalfEven}).toString(),
             std::string("42"));
}

TEST(round_, bankers_rounding) {
    // round 固定用 HalfEven。
    CHECK_EQ(BigDecimal::round(BigDecimal::fromString("2.5"), Context{2}).toString(),
             std::string("2.5"));
    CHECK_EQ(BigDecimal::round(BigDecimal::fromString("0.125"), Context{3}).toString(),
             std::string("0.125"));
    CHECK_EQ(BigDecimal::round(BigDecimal::fromString("0.135"), Context{3}).toString(),
             std::string("0.135"));
    // 精度不足以容纳全部有效数字时才发生舍入：2.555 保留 2 位 -> 2.6（进位）。
    CHECK_EQ(BigDecimal::round(BigDecimal::fromString("2.555"), Context{2}).toString(),
             std::string("2.6"));
    CHECK_EQ(BigDecimal::round(BigDecimal::fromString("-2.555"), Context{2}).toString(),
             std::string("-2.6"));
    // 正好一半时取偶：1.5 保留 1 位 -> 2（1 是奇数）；2.5 保留 1 位 -> 2（2 是偶数）。
    CHECK_EQ(BigDecimal::round(BigDecimal::fromString("1.5"), Context{1}).toString(),
             std::string("2"));
    CHECK_EQ(BigDecimal::round(BigDecimal::fromString("2.5"), Context{1}).toString(),
             std::string("2"));
    CHECK_EQ(BigDecimal::round(BigDecimal::fromString("-2.5"), Context{1}).toString(),
             std::string("-2"));
}

TEST(modulo, sign_follows_dividend) {
    // 取余结果符号跟随被除数（与 C 的 % 一致）。
    CHECK_EQ(BigDecimal::modulo(BigDecimal::fromString("7"), BigDecimal::fromString("2")).toString(),
             std::string("1"));
    CHECK_EQ(BigDecimal::modulo(BigDecimal::fromString("-7"), BigDecimal::fromString("2")).toString(),
             std::string("-1"));
    CHECK_EQ(BigDecimal::modulo(BigDecimal::fromString("7"), BigDecimal::fromString("-2")).toString(),
             std::string("1"));
    CHECK_EQ(BigDecimal::modulo(BigDecimal::fromString("-7"), BigDecimal::fromString("-2")).toString(),
             std::string("-1"));
}

TEST(modulo, fractional_operands) {
    CHECK_EQ(BigDecimal::modulo(BigDecimal::fromString("5.5"), BigDecimal::fromString("2")).toString(),
             std::string("1.5"));
    CHECK_EQ(BigDecimal::modulo(BigDecimal::fromString("-5.5"), BigDecimal::fromString("2")).toString(),
             std::string("-1.5"));
    // 除数带小数位时余数必须小于除数。
    CHECK_EQ(BigDecimal::modulo(BigDecimal::fromString("1"), BigDecimal::fromString("0.3")).toString(),
             std::string("0.1"));
}

TEST(modulo, exact_multiple_and_zero) {
    CHECK_EQ(BigDecimal::modulo(BigDecimal::fromString("6"), BigDecimal::fromString("3")).toString(),
             std::string("0"));
    CHECK_EQ(BigDecimal::modulo(BigDecimal::fromString("0"), BigDecimal::fromString("3")).toString(),
             std::string("0"));
    // 比除数小的被除数，余数就是它自己。
    CHECK_EQ(BigDecimal::modulo(BigDecimal::fromString("2"), BigDecimal::fromString("5")).toString(),
             std::string("2"));
}

TEST(divide, legacy_crash_inputs_are_safe) {
    // legacy 缺陷 #10 的触发输入：除法收尾的舍入分支对单节点链表做 ->prior 回退，
    // 退到 nullptr 后解引用崩溃。v3 的舍入走块数组路径，这些输入必须全部安全。
    const char* numerators[] = {"1.5", "2.5", "-02.881332", "12.34", "0.1", "1"};
    const char* denominators[] = {"474", "474", "0474", "56", "7", "3"};
    for (const char* n : numerators) {
        for (const char* d : denominators) {
            // 只要求不崩溃且结果可解析。
            const std::string result = div(n, d, 1).toString();
            CHECK(!result.empty());
        }
    }
    // 精度 1 是 legacy 里 FracBit = 0 的等价场景。
    CHECK_EQ(div("1.5", "474", 1).toString(), std::string("0.003"));
    CHECK_EQ(div("2.5", "474", 1).toString(), std::string("0.005"));
    CHECK_EQ(div("-02.881332", "474", 1).toString(), std::string("-0.006"));
}

TEST(divide, no_negative_zero_output) {
    CHECK_EQ(div("0", "3", 10).toString(), std::string("0"));
    CHECK_EQ(div("-0", "3", 10).toString(), std::string("0"));
    // 精度 >= 1 至少保留 1 位有效数字，所以非零值永远不会舍入成 0，
    // 更不会输出 "-0"。
    CHECK_EQ(div("-1", "1000000", 1, RoundingMode::Down).toString(),
             std::string("-0.000001"));
    CHECK_EQ(div("-0.4", "1", 1, RoundingMode::Floor).toString(), std::string("-0.4"));
}

// 回归测试：precision 曾无上限，写死一个巨大值（如 1000000）会让单次除法
// 以分钟计。核心现在拒绝超出上限的精度，逼迫调用方按量的级给精度。
TEST(divide, rejects_absurd_precision) {
    bool threw = false;
    try {
        BigDecimal::divide(BigDecimal::fromString("1"), BigDecimal::fromString("2"),
                           Context{1000000, RoundingMode::Down});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        BigDecimal::divide(BigDecimal::fromString("1"), BigDecimal::fromString("2"),
                           Context{0, RoundingMode::Down});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// 回归测试：normalize() 的尾随零剥离曾有两个缺陷。
//
// 1. 逐个剥离尾随零是 O(零个数 × 块数) 的二次方开销。高精度除法的商
//    形如 0.5000…0，precision=1000000 时单次除法要以分钟计。
//    现改为整块批量剥离。
// 2. 批量剥离时若 scale_ 少减了 9，会把 10.00 归一化成 1 ——
//    值被除了 10^9 而小数位只减了 1。
TEST(normalize, trailing_zero_strip) {
    // 跨块边界的尾随零：块内不足 9 个 + 整块为零
    CHECK_EQ(BigDecimal::fromString("1.500").toString(), std::string("1.5"));
    CHECK_EQ(BigDecimal::fromString("10.00").toString(), std::string("10"));
    CHECK_EQ(BigDecimal::fromString("0.000").toString(), std::string("0"));

    // 整整一块零（9 个零）后跟非零位：零块在**非零位之前**，不能被剥掉
    std::string wide = "12.5";
    wide.append(9, '0');
    wide += "3";
    CHECK_EQ(BigDecimal::fromString(wide).toString(), std::string("12.50000000003"));

    // 整整一块尾随零（9 个零）必须被剥掉，且小数位同步收缩 9
    std::string trailingBlock = "12.5";
    trailingBlock.append(9, '0');
    CHECK_EQ(BigDecimal::fromString(trailingBlock).toString(), std::string("12.5"));

    // 两个整块尾随零（18 个零）
    std::string twoBlocks = "12.5";
    twoBlocks.append(18, '0');
    CHECK_EQ(BigDecimal::fromString(twoBlocks).toString(), std::string("12.5"));

    // 值不变、只归一化：除法结果尾随零被剥离后数值必须一致
    const BigDecimal half = BigDecimal::divide(BigDecimal::fromString("1"),
                                               BigDecimal::fromString("2"),
                                               Context{20, RoundingMode::Down});
    CHECK_EQ(half.toString(), std::string("0.5"));
    CHECK(half.compare(BigDecimal::fromString("0.5")) == 0);
}

// 回归：科学计数法的指数上限曾被**放松了 10 倍**。
//
// 旧的守卫在把当前数字折进 magnitude **之前**检查：
//
//     if (magnitude > kMaxExponentMagnitude) throw ...;
//     magnitude = magnitude * 10 + digit;
//
// 于是最后一位会把 magnitude 顶到 10 × 上限 —— 实际上限是 10^7 而不是
// 文档写的 10^6，1e1000009 因此被接受。
//
// 现在检查放在折进之后，实际接受的上限与文档一致。
TEST(scientific, enforces_exponent_limit_exactly) {
    // 上限之内：接受，且 scale 正好等于指数。
    CHECK(BigDecimal::fromString("1e1000000").scale() == 0);
    CHECK(BigDecimal::fromString("1e1000000").toString().size() == 1000001);

    // 上限之内、但离上限还有余量：必须接受（曾经被误拒的那一类）。
    // 999999 < 1000000，所以 1e999999 是合法的。
    CHECK(BigDecimal::fromString("1e999999").scale() == 0);
    CHECK(BigDecimal::fromString("1e999999").toString().size() == 1000000);

    // 超过上限：拒绝。
    const char* tooLarge[] = {"1e1000001", "1e1000009", "1e1000010", "1e9999999"};
    for (const char* text : tooLarge) {
        bool threw = false;
        try { BigDecimal::fromString(text); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }

    // 负指数同样受限。
    bool threw = false;
    try { BigDecimal::fromString("1e-1000001"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // 上限之内的负指数可以接受。
    CHECK(BigDecimal::fromString("1e-1000000").scale() == 1000000);
    CHECK(BigDecimal::fromString("1e-999999").scale() == 999999);
}

// 指数上限必须同时挡住「荒唐的指数」与「荒唐的分配」。
//
// 1e-1000000 有 100 万位小数 —— scale 很大，但那是**允许**的（上限之内）。
// 真正要挡住的是超过上限的那些：否则 scale 会到 10^7，
// 一个字面量就能撑爆内存。
TEST(scientific, exponent_limit_bounds_the_scale) {
    // 超上限：抛异常，而不是分配 1000 万位的 scale。
    bool threw = false;
    try { BigDecimal::fromString("1e-10000000"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // 超上限的正指数同样被挡住。
    threw = false;
    try { BigDecimal::fromString("1e10000000"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}
