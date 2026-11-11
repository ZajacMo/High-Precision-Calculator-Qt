// 超越函数（sqrt / pow / exp / ln / log10）的单元测试。
//
// 这里的期望值都是**手算可核对**的常量（取自 Python decimal 的高精度结果），
// 覆盖精确值、量级极端值、定义域边界与舍入模式。
#include "test_framework.h"

#include <stdexcept>
#include <string>

#include "core/BigDecimal.h"

using hpc::BigDecimal;
using hpc::Context;
using hpc::RoundingMode;

namespace {

// 20 位有效数字的默认上下文。
Context ctx20() { return Context{20, RoundingMode::HalfEven}; }

}  // namespace

// --- sqrt ---

TEST(sqrt, exact_values) {
    // 完全平方数必须给出**精确**结果，不能带尾随零。
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("4"), ctx20()).toString(), std::string("2"));
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("9"), ctx20()).toString(), std::string("3"));
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("0"), ctx20()).toString(), std::string("0"));
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("1"), ctx20()).toString(), std::string("1"));
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("100"), ctx20()).toString(), std::string("10"));
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("10000"), ctx20()).toString(), std::string("100"));
}

TEST(sqrt, irrational_values) {
    // 20 位有效数字，与 Python decimal 一致。
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("2"), ctx20()).toString(),
             std::string("1.4142135623730950488"));
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("3"), ctx20()).toString(),
             std::string("1.7320508075688772935"));
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("0.5"), ctx20()).toString(),
             std::string("0.7071067811865475244"));
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("0.001"), ctx20()).toString(),
             std::string("0.03162277660168379332"));
}

TEST(sqrt, extreme_magnitudes) {
    // 量级极端时结果的有效位数必须正确 ——
    // 这是 brief 原实现的缺陷所在（它把 precision 当成小数位数用）。
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("100000000000000000000"),
                               ctx20()).toString(),
             std::string("10000000000"));
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("0.00000000000000000001"),
                               ctx20()).toString(),
             std::string("0.0000000001"));
}

TEST(sqrt, negative_throws_domain_error) {
    bool threw = false;
    try {
        BigDecimal::sqrt(BigDecimal::fromString("-1"), ctx20());
    } catch (const std::domain_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(sqrt, precision_scales_with_significant_digits) {
    // 精度是**有效数字位**，不是小数位数：
    // 精度 5 时 sqrt(2) 应有 5 位有效数字，精度 30 时应有 30 位。
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("2"), Context{5, RoundingMode::HalfEven}).toString(),
             std::string("1.4142"));
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("2"), Context{8, RoundingMode::HalfEven}).toString(),
             std::string("1.4142136"));
    // 精度 3 时是 3 位有效数字（1.41），不是 3 位小数（1.414）。
    CHECK_EQ(BigDecimal::sqrt(BigDecimal::fromString("2"), Context{3, RoundingMode::HalfEven}).toString(),
             std::string("1.41"));
}

// --- exp ---

TEST(exp, exact_values) {
    CHECK_EQ(BigDecimal::exp(BigDecimal::fromString("0"), ctx20()).toString(), std::string("1"));
    CHECK_EQ(BigDecimal::exp(BigDecimal::fromString("1"), ctx20()).toString(),
             std::string("2.7182818284590452354"));
    CHECK_EQ(BigDecimal::exp(BigDecimal::fromString("-1"), ctx20()).toString(),
             std::string("0.3678794411714423216"));
}

TEST(exp, large_and_small_arguments) {
    CHECK_EQ(BigDecimal::exp(BigDecimal::fromString("10"), ctx20()).toString(),
             std::string("22026.465794806716517"));
    // 负参数的结果很小：有效位仍在小数点后，不能因为「整数部分是 0」而丢位。
    CHECK_EQ(BigDecimal::exp(BigDecimal::fromString("-10"), ctx20()).toString(),
             std::string("0.000045399929762484851536"));
    CHECK_EQ(BigDecimal::exp(BigDecimal::fromString("-0.5"), ctx20()).toString(),
             std::string("0.6065306597126334236"));
}

TEST(exp, reciprocal_identity) {
    // exp(x) * exp(-x) = 1。
    //
    // 在 20 位精度下两个因子各有 1 ulp 的相对误差，乘积不严格等于 1 ——
    // 但低 10 位精度下必须完全一致（误差 5e-22 远小于 1e-10）。
    const BigDecimal x = BigDecimal::fromString("3.25");
    const BigDecimal product = BigDecimal::exp(x, ctx20()) * BigDecimal::exp(BigDecimal(0) - x, ctx20());
    CHECK_EQ(BigDecimal::round(product, Context{10, RoundingMode::HalfEven}).toString(),
             std::string("1"));
}

// --- ln ---

TEST(ln, exact_values) {
    // ln(1) 必须**精确**为 0。
    CHECK_EQ(BigDecimal::ln(BigDecimal::fromString("1"), ctx20()).toString(), std::string("0"));
    // 10 的整数次幂：ln(10) 是无理数，但 1/e、1/2 是精确的。
    CHECK_EQ(BigDecimal::ln(BigDecimal::fromString("0.5"), ctx20()).toString(),
             std::string("-0.69314718055994530942"));
}

TEST(ln, common_values) {
    CHECK_EQ(BigDecimal::ln(BigDecimal::fromString("2"), ctx20()).toString(),
             std::string("0.69314718055994530942"));
    CHECK_EQ(BigDecimal::ln(BigDecimal::fromString("10"), ctx20()).toString(),
             std::string("2.302585092994045684"));
    CHECK_EQ(BigDecimal::ln(BigDecimal::fromString("1000"), ctx20()).toString(),
             std::string("6.9077552789821370521"));
    CHECK_EQ(BigDecimal::ln(BigDecimal::fromString("0.001"), ctx20()).toString(),
             std::string("-6.9077552789821370521"));
}

TEST(ln, near_one_keeps_relative_precision) {
    // x 极接近 1 时 ln(x) 极接近 0 ——
    // 这里考验的是**相对精度**（不是绝对精度）。
    CHECK_EQ(BigDecimal::ln(BigDecimal::fromString("0.9"), ctx20()).toString(),
             std::string("-0.10536051565782630123"));
    // x = 1.0000000001 时 ln(x) ≈ 1e-10，必须保留这 10 位。
    CHECK_EQ(BigDecimal::ln(BigDecimal::fromString("1.0000000001"), ctx20()).toString(),
             std::string("0.000000000099999999995"));
}

TEST(ln, non_positive_throws_domain_error) {
    bool threw = false;
    try {
        BigDecimal::ln(BigDecimal::fromString("0"), ctx20());
    } catch (const std::domain_error&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        BigDecimal::ln(BigDecimal::fromString("-1"), ctx20());
    } catch (const std::domain_error&) {
        threw = true;
    }
    CHECK(threw);
}

// --- log10 ---

TEST(log10, exact_powers_of_ten) {
    // 10 的整数次幂必须给出**精确**整数。
    CHECK_EQ(BigDecimal::log10(BigDecimal::fromString("1"), ctx20()).toString(), std::string("0"));
    CHECK_EQ(BigDecimal::log10(BigDecimal::fromString("10"), ctx20()).toString(), std::string("1"));
    CHECK_EQ(BigDecimal::log10(BigDecimal::fromString("100"), ctx20()).toString(), std::string("2"));
    CHECK_EQ(BigDecimal::log10(BigDecimal::fromString("1000"), ctx20()).toString(), std::string("3"));
    CHECK_EQ(BigDecimal::log10(BigDecimal::fromString("0.001"), ctx20()).toString(), std::string("-3"));
}

TEST(log10, common_values) {
    CHECK_EQ(BigDecimal::log10(BigDecimal::fromString("2"), ctx20()).toString(),
             std::string("0.30102999566398119521"));
    CHECK_EQ(BigDecimal::log10(BigDecimal::fromString("12345"), ctx20()).toString(),
             std::string("4.0914910942679510818"));
}

TEST(log10, non_positive_throws_domain_error) {
    bool threw = false;
    try {
        BigDecimal::log10(BigDecimal::fromString("0"), ctx20());
    } catch (const std::domain_error&) {
        threw = true;
    }
    CHECK(threw);
}

// --- pow ---

TEST(pow, integer_exponent_is_exact) {
    CHECK_EQ(BigDecimal::pow(BigDecimal::fromString("2"), BigDecimal::fromString("10"),
                              ctx20()).toString(),
             std::string("1024"));
    CHECK_EQ(BigDecimal::pow(BigDecimal::fromString("3"), BigDecimal::fromString("0"),
                              ctx20()).toString(),
             std::string("1"));
    CHECK_EQ(BigDecimal::pow(BigDecimal::fromString("0"), BigDecimal::fromString("2"),
                              ctx20()).toString(),
             std::string("0"));
}

TEST(pow, negative_integer_exponent) {
    CHECK_EQ(BigDecimal::pow(BigDecimal::fromString("2"), BigDecimal::fromString("-3"),
                              ctx20()).toString(),
             std::string("0.125"));
    CHECK_EQ(BigDecimal::pow(BigDecimal::fromString("10"), BigDecimal::fromString("-2"),
                              ctx20()).toString(),
             std::string("0.01"));
}

TEST(pow, non_integer_exponent) {
    CHECK_EQ(BigDecimal::pow(BigDecimal::fromString("2"), BigDecimal::fromString("0.5"),
                              ctx20()).toString(),
             std::string("1.4142135623730950488"));
    CHECK_EQ(BigDecimal::pow(BigDecimal::fromString("1.5"), BigDecimal::fromString("2.5"),
                              ctx20()).toString(),
             std::string("2.7556759606310753605"));
    // 平方根的恒等式：a^0.5 = sqrt(a)。
    CHECK_EQ(BigDecimal::pow(BigDecimal::fromString("100"), BigDecimal::fromString("0.5"),
                              ctx20()).toString(),
             std::string("10"));
}

TEST(pow, domain_errors) {
    // 零的零次幂未定义（与 Python decimal 一致：Decimal(0) ** Decimal(0)
    // 抛 InvalidOperation，而不是返回 1）。
    bool zeroToZero = false;
    try {
        BigDecimal::pow(BigDecimal::fromString("0"), BigDecimal::fromString("0"), ctx20());
    } catch (const std::domain_error&) {
        zeroToZero = true;
    }
    CHECK(zeroToZero);

    // 零的负数次幂未定义。
    bool threw = false;
    try {
        BigDecimal::pow(BigDecimal::fromString("0"), BigDecimal::fromString("-1"), ctx20());
    } catch (const std::domain_error&) {
        threw = true;
    }
    CHECK(threw);

    // 负底数的非整数次幂未定义。
    threw = false;
    try {
        BigDecimal::pow(BigDecimal::fromString("-2"), BigDecimal::fromString("0.5"), ctx20());
    } catch (const std::domain_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(pow, huge_integer_exponent_overflows) {
    // 指数超出可计算范围时必须报 overflow_error，而不是静默算出错误结果。
    bool threw = false;
    try {
        BigDecimal::pow(BigDecimal::fromString("2"),
                        BigDecimal::fromString("100000000000000000000"),
                        ctx20());
    } catch (const std::overflow_error&) {
        threw = true;
    }
    CHECK(threw);
}

// --- 评审修复的回归测试 ---
//
// 下面三个用例覆盖的是**挂起**而不是数值错误：修复前它们分别要
// 271 ms / 120 s / 永远算不完。修复后它们要么瞬时完成，要么抛确定的异常。
// 如果修复被回退，这些用例会挂住 —— 那正是它们的价值。

TEST(exp, tiny_argument_does_not_scale_precision_with_magnitude) {
    // exp 的保护位数曾按 |x| 的十进制指数给，于是 1e-200 要多算 205 位
    // （271 ms），1e-99000 更是永远算不完。
    //
    // 保护位真正该由 |k| = |floor(x/ln10)| 决定：x 极小时 k = 0，
    // 没有任何抵消，一位保护位都不需要。
    //
    // exp(1e-200) 的结果是 1（1e-200 远小于工作精度下的 1 ulp）。
    const std::string tiny = "0." + std::string(199, '0') + "1";   // 1e-200
    CHECK_EQ(BigDecimal::exp(BigDecimal::fromString(tiny), ctx20()).toString(),
             std::string("1"));

    // 更小的量级同样瞬时完成（结果仍是 1）。
    const std::string tinier = "0." + std::string(1999, '0') + "1";  // 1e-2000
    CHECK_EQ(BigDecimal::exp(BigDecimal::fromString(tinier), ctx20()).toString(),
             std::string("1"));
}

TEST(pow, fractional_base_with_huge_exponent_overflows) {
    // 整数幂的结果位数护栏曾只在「底数 > 1」时生效：
    // base = 0.001 的整数位数是 -2，于是整个护栏被跳过，
    // 0.001^100000 要算出 30 万位小数（实测超过 120 s）。
    //
    // 现在结果位数按「底数的整数位 + 小数位 × 指数」估算，超限即报溢出。
    bool threw = false;
    try {
        BigDecimal::pow(BigDecimal::fromString("0.001"),
                        BigDecimal::fromString("100000"),
                        ctx20());
    } catch (const std::overflow_error&) {
        threw = true;
    }
    CHECK(threw);

    // 更小的底数（结果需要 1000 万位小数）同样必须报溢出。
    threw = false;
    try {
        BigDecimal::pow(BigDecimal::fromString("0.0000000001"),
                        BigDecimal::fromString("100000"),
                        ctx20());
    } catch (const std::overflow_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(transcendental, rejects_precision_above_ceiling) {
    // 超越函数的入口曾只检查 precision > 0，工作精度因此可以超过
    // kMaxPrecision。实测 sqrt(2) @ precision=99990 会永远算不完，
    // 而且失败方式**不一致** —— 有的精度下内层的 divide 恰好抛异常，
    // 有的精度下则一路挂起。
    //
    // 现在五个入口统一抛 std::invalid_argument。
    const Context absurd{99990, RoundingMode::HalfEven};

    bool threw = false;
    try {
        BigDecimal::sqrt(BigDecimal::fromString("2"), absurd);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        BigDecimal::exp(BigDecimal::fromString("1"), absurd);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        BigDecimal::ln(BigDecimal::fromString("2"), absurd);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        BigDecimal::log10(BigDecimal::fromString("2"), absurd);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        BigDecimal::pow(BigDecimal::fromString("2"), BigDecimal::fromString("0.5"), absurd);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// --- 舍入模式 ---

TEST(transcendental, rounding_mode_is_honoured) {
    // Down（向零截断）与 Up（远离零）在末位上必须相反。
    const BigDecimal down = BigDecimal::sqrt(BigDecimal::fromString("2"),
                                              Context{5, RoundingMode::Down});
    const BigDecimal up = BigDecimal::sqrt(BigDecimal::fromString("2"),
                                           Context{5, RoundingMode::Up});
    CHECK_EQ(down.toString(), std::string("1.4142"));
    CHECK_EQ(up.toString(), std::string("1.4143"));
}

// --- 恒等式（交叉验证） ---

TEST(transcendental, identities) {
    // sqrt(x)^2 = x
    //
    // sqrt(123.456) 是无理数，20 位精度下必然带舍入误差 ——
    // 所以平方后再舍入到 15 位，必须与原值一致。
    const BigDecimal x = BigDecimal::fromString("123.456");
    const BigDecimal root = BigDecimal::sqrt(x, ctx20());
    CHECK_EQ(BigDecimal::round(root * root, Context{15, RoundingMode::HalfEven}).toString(),
             std::string("123.456"));

    // ln(a^b) = b * ln(a)
    const BigDecimal a = BigDecimal::fromString("2.5");
    const BigDecimal b = BigDecimal::fromString("3.75");
    const BigDecimal left = BigDecimal::ln(BigDecimal::pow(a, b, ctx20()), ctx20());
    const BigDecimal right = BigDecimal::ln(a, ctx20()) * b;
    CHECK_EQ(BigDecimal::round(left, Context{18, RoundingMode::HalfEven}).toString(),
             BigDecimal::round(right, Context{18, RoundingMode::HalfEven}).toString());

    // log10(x) = ln(x) / ln(10)
    const BigDecimal y = BigDecimal::fromString("37.5");
    const BigDecimal viaLog10 = BigDecimal::log10(y, ctx20());
    const BigDecimal viaLn = BigDecimal::divide(BigDecimal::ln(y, ctx20()),
                                                 BigDecimal::ln(BigDecimal::fromString("10"), ctx20()),
                                                 ctx20());
    CHECK_EQ(viaLog10.toString(), viaLn.toString());
}
