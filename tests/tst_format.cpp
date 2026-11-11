// 进制转换与位运算的单元测试。
#include "test_framework.h"

#include <stdexcept>
#include <string>

#include "core/Format.h"

using hpc::BigDecimal;
using hpc::BitOp;

namespace {

BigDecimal make(const std::string& text) { return BigDecimal::fromString(text); }

}  // namespace

// --- 进制转换 ---------------------------------------------------------

TEST(base, to_base_known_values) {
    CHECK_EQ(hpc::toBaseString(make("0"), 2), std::string("0"));
    CHECK_EQ(hpc::toBaseString(make("10"), 2), std::string("1010"));
    CHECK_EQ(hpc::toBaseString(make("64"), 8), std::string("100"));
    CHECK_EQ(hpc::toBaseString(make("255"), 16), std::string("ff"));
    CHECK_EQ(hpc::toBaseString(make("255"), 16, true), std::string("FF"));
    CHECK_EQ(hpc::toBaseString(make("35"), 36), std::string("z"));
    CHECK_EQ(hpc::toBaseString(make("35"), 36, true), std::string("Z"));
    CHECK_EQ(hpc::toBaseString(make("123456789"), 36), std::string("21i3v9"));
}

TEST(base, to_base_negative) {
    CHECK_EQ(hpc::toBaseString(make("-255"), 16), std::string("-ff"));
    CHECK_EQ(hpc::toBaseString(make("-10"), 2), std::string("-1010"));
    CHECK_EQ(hpc::toBaseString(make("-1"), 16), std::string("-1"));
}

TEST(base, to_base_rejects_invalid_base) {
    bool threw = false;
    try { hpc::toBaseString(make("1"), 1); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { hpc::toBaseString(make("1"), 37); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { hpc::toBaseString(make("1"), 0); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

// 往返性质：fromBaseString(toBaseString(v, base), base) == v，
// 对 base 2..36 的**每一个**进制都要成立。
TEST(base, round_trip_all_bases) {
    const std::string original = "123456789012345678901234567890";
    const BigDecimal value = make(original);
    for (int base = 2; base <= 36; ++base) {
        const std::string encoded = hpc::toBaseString(value, base);
        CHECK_EQ(hpc::fromBaseString(encoded, base).toString(), original);
    }
}

TEST(base, round_trip_edge_values) {
    const char* values[] = {"0", "1", "2", "9", "10", "255", "65535", "65536",
                            "999999999", "1000000000", "1", "1234567890123456789"};
    for (const char* text : values) {
        const BigDecimal value = make(text);
        for (int base = 2; base <= 36; ++base) {
            const std::string encoded = hpc::toBaseString(value, base);
            CHECK_EQ(hpc::fromBaseString(encoded, base).toString(), std::string(text));
        }
    }
}

// 负数也要往返无损。
TEST(base, round_trip_negative) {
    const std::string original = "-123456789012345678901234567890";
    const BigDecimal value = make(original);
    for (int base = 2; base <= 36; ++base) {
        const std::string encoded = hpc::toBaseString(value, base);
        CHECK_EQ(hpc::fromBaseString(encoded, base).toString(), original);
    }
}

// 大数：1000 位十进制在二进制下有 3322 位，往返必须无损。
TEST(base, round_trip_large) {
    std::string original;
    for (int i = 0; i < 1000; ++i) {
        original.push_back(static_cast<char>('1' + (i % 9)));
    }
    const BigDecimal value = make(original);
    for (int base : {2, 8, 16, 36}) {
        const std::string encoded = hpc::toBaseString(value, base);
        CHECK_EQ(hpc::fromBaseString(encoded, base).toString(), original);
    }
}

TEST(base, from_base_known_values) {
    CHECK_EQ(hpc::fromBaseString("ff", 16).toString(), std::string("255"));
    CHECK_EQ(hpc::fromBaseString("FF", 16).toString(), std::string("255"));
    CHECK_EQ(hpc::fromBaseString("1010", 2).toString(), std::string("10"));
    CHECK_EQ(hpc::fromBaseString("777", 8).toString(), std::string("511"));
    CHECK_EQ(hpc::fromBaseString("z", 36).toString(), std::string("35"));
    CHECK_EQ(hpc::fromBaseString("0", 2).toString(), std::string("0"));
}

TEST(base, from_base_negative) {
    CHECK_EQ(hpc::fromBaseString("-ff", 16).toString(), std::string("-255"));
    CHECK_EQ(hpc::fromBaseString("-1010", 2).toString(), std::string("-10"));
    CHECK_EQ(hpc::fromBaseString("+ff", 16).toString(), std::string("255"));
    CHECK_EQ(hpc::fromBaseString("-0", 16).toString(), std::string("0"));
}

TEST(base, from_base_rejects_bad_input) {
    // 空串。
    bool threw = false;
    try { hpc::fromBaseString("", 16); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // 非法字符。
    threw = false;
    try { hpc::fromBaseString("12g4", 16); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // 数字超出进制范围。
    threw = false;
    try { hpc::fromBaseString("2", 2); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { hpc::fromBaseString("1", 1); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // 小数点不支持。
    threw = false;
    try { hpc::fromBaseString("1.5", 10); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // 只有符号没有数字。
    threw = false;
    try { hpc::fromBaseString("-", 10); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // 非法进制。
    threw = false;
    try { hpc::fromBaseString("1", 0); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { hpc::fromBaseString("1", 37); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

// 小数部分：整数部分精确无损，小数部分按目标进制提取。
// 能有限表示的给出**最短的精确形式**（尾随零去掉）。
TEST(base, fraction_integer_part_is_exact) {
    // 0.5 在二进制下是 0.1（有限）。
    CHECK_EQ(hpc::toBaseString(make("0.5"), 2, false, 4), std::string("0.1"));
    CHECK_EQ(hpc::toBaseString(make("1.5"), 2, false, 4), std::string("1.1"));
    CHECK_EQ(hpc::toBaseString(make("-1.5"), 2, false, 4), std::string("-1.1"));
    // 0.25 在二进制下能有限表示（0.01）。
    CHECK_EQ(hpc::toBaseString(make("0.25"), 2, false, 4), std::string("0.01"));
    CHECK_EQ(hpc::toBaseString(make("0.5"), 2, false, 2), std::string("0.1"));
    // 0.5 在十进制下也是 0.5。
    CHECK_EQ(hpc::toBaseString(make("0.5"), 10), std::string("0.5"));
    CHECK_EQ(hpc::toBaseString(make("0.25"), 10), std::string("0.25"));
    // 0.5 在三进制下无法有限表示（0.1111...），必须标记截断。
    CHECK_EQ(hpc::toBaseString(make("0.5"), 3, false, 4), std::string("0.1111..."));
}

TEST(base, fraction_truncation_is_marked) {
    // 0.333…（25 位）在**二进制**下无法有限表示，必须带截断标记。
    const std::string binary = hpc::toBaseString(make("0.3333333333333333333333333"), 2);
    CHECK(binary.find("...") != std::string::npos);
    CHECK(binary[0] == '0');
    CHECK(binary[1] == '.');

    // 0.5 在**三进制**下无法有限表示（0.1111…），同样要标记。
    CHECK(hpc::toBaseString(make("0.5"), 3).find("...") != std::string::npos);

    // 十进制下**任何**有限小数都能精确表示，因此永远不会出现截断标记。
    // （这是十进制内核的直接推论：0.333… 本身就是一个有限十进制数。）
    const std::string decimal = hpc::toBaseString(make("0.3333333333333333333333333"), 10);
    CHECK(decimal.find("...") == std::string::npos);
    CHECK_EQ(decimal, std::string("0.3333333333333333333333333"));

    // 能在目标进制下有限表示的值不带标记。
    CHECK(hpc::toBaseString(make("0.5"), 10).find("...") == std::string::npos);
    CHECK(hpc::toBaseString(make("0.25"), 10).find("...") == std::string::npos);
}

TEST(base, integer_has_no_fraction_marker) {
    CHECK(hpc::toBaseString(make("42"), 16).find(".") == std::string::npos);
    CHECK(hpc::toBaseString(make("42"), 16).find("...") == std::string::npos);
    CHECK(hpc::toBaseString(make("0"), 10).find(".") == std::string::npos);
}

// --- 回归：虚假截断标记 -------------------------------------------------
//
// 「..." 标记的含义是「**确实**没写完」。旧实现用「最小的 D 使
// base^D >= 10^scale」估算小数位数，那个估算只在 base 是 2 或 5 的幂时
// 才等价；base 同时带 2 与 5 且指数不等时（20 = 2^2·5、30 = 2·3·5）
// 估算值短于真实展开长度，于是输出挂上了一个**并不存在**的截断标记。

TEST(base, exact_expansion_has_no_truncation_marker) {
    // 0.00001 在二十进制下精确等于 0.0001c（1/20^4 = 1/160000 = 0.00000625，
    // 而 0.00001 = 1/100000 = 1/(2^5·5^5)，在二十进制下需要 5 位）。
    CHECK_EQ(hpc::toBaseString(make("0.00001"), 20), std::string("0.0001c"));
    CHECK(hpc::toBaseString(make("0.00001"), 20).find("...") == std::string::npos);

    // 0.0001 在三十进制下精确等于 0.002l。
    CHECK_EQ(hpc::toBaseString(make("0.0001"), 30), std::string("0.002l"));
    CHECK(hpc::toBaseString(make("0.0001"), 30).find("...") == std::string::npos);

    // 显式位数给出的展开与自动位数一致。
    CHECK_EQ(hpc::toBaseString(make("0.00001"), 20, false, 5), std::string("0.0001c"));
    CHECK_EQ(hpc::toBaseString(make("0.0001"), 30, false, 4), std::string("0.002l"));
}

// 三十进制是 2、3、5 的公倍数，而十进制只带 2 与 5 —— 因此同一个值在
// 三十进制下的展开长度与十进制不同。两者都必须精确。
TEST(base, bases_with_unequal_two_and_five_exponents) {
    // 0.0001 = 1/10000 = 1/(2^4·5^4)。
    //   二十进制 = 2^2·5 → 展开长度 max(4/2, 4/1) = 4。
    //   三十进制 = 2·3·5  → 展开长度 max(4/1, 4/1) = 4。
    //   十进制   = 2·5    → 展开长度 max(4/1, 4/1) = 4。
    CHECK_EQ(hpc::toBaseString(make("0.0001"), 10), std::string("0.0001"));
    CHECK_EQ(hpc::toBaseString(make("0.0001"), 20), std::string("0.000g"));
    CHECK_EQ(hpc::toBaseString(make("0.0001"), 30), std::string("0.002l"));

    // 0.0625 = 1/16 = 1/2^4：二进制 4 位，其余进制需要更多位。
    CHECK_EQ(hpc::toBaseString(make("0.0625"), 2), std::string("0.0001"));
    CHECK(hpc::toBaseString(make("0.0625"), 20).find("...") == std::string::npos);
    CHECK(hpc::toBaseString(make("0.0625"), 30).find("...") == std::string::npos);
}

// 十进制下**任何**有限小数都精确表示 —— 即使小数位超过 4096。
//
// 旧实现把 4096 当成「十进制展开的上限」，于是 1e-4097 会被打上截断
// 标记，尽管它在十进制下是精确的（4097 位小数）。
TEST(base, terminating_decimal_never_truncated_in_base_ten) {
    const std::string at4096 = hpc::toBaseString(make("1e-4096"), 10);
    CHECK(at4096.find("...") == std::string::npos);
    CHECK_EQ(at4096, std::string("0.") + std::string(4095, '0') + "1");

    const std::string at4097 = hpc::toBaseString(make("1e-4097"), 10);
    CHECK(at4097.find("...") == std::string::npos);
    CHECK_EQ(at4097, std::string("0.") + std::string(4096, '0') + "1");
}

// 真实截断仍然要带标记：该值在目标进制下**无法**有限表示。
TEST(base, genuine_truncation_is_still_marked) {
    // 1/3 在三进制下是 0.111…（无限循环）。
    const std::string ternary = hpc::toBaseString(make("0.3333333333333333333333333"), 3);
    CHECK(ternary.find("...") != std::string::npos);

    // 0.5 在三进制下也是 0.111…（1/2 = 0.111…_3）。
    const std::string half = hpc::toBaseString(make("0.5"), 3);
    CHECK(half.find("...") != std::string::npos);
    CHECK_EQ(half, std::string("0.111..."));

    // 0.5 在二进制下是精确的 0.1，不带标记。
    CHECK(hpc::toBaseString(make("0.5"), 2).find("...") == std::string::npos);
}

// 调用方显式指定 fractionDigits 时，位数不足**就是**截断，必须标记。
TEST(base, explicit_fraction_digits_budget_truncates) {
    // 0.5 在二进制下是精确的 0.1，但要 3 位就会补零 —— 那不是截断，
    // 精确表示仍然去掉尾随零。
    CHECK_EQ(hpc::toBaseString(make("0.5"), 2, false, 3), std::string("0.1"));

    // 1/3 在十进制下需要无限位，指定 5 位就是截断。
    const std::string third = hpc::toBaseString(make("0.3333333333333333333333333"), 10, false, 5);
    CHECK(third.find("...") != std::string::npos);
    CHECK_EQ(third, std::string("0.33333..."));
}

// --- 回归：质因子赋值算错导致**提前**截断 -------------------------------
//
// 展开长度由「最简分数的分母在该进制下的赋值」决定。旧实现算赋值时用了
// 一条**不成立**的捷径：「整除性只看最低块」。
//
// 设分子 M = m_0 + 10^9·H（m_0 是最低块）。因为 v_p(10^9·H) >= 9，所以
// **当 v_p(m_0) < 9 时** v_p(M) = v_p(m_0) —— 这一步是对的。
//
// 但旧实现**在检查「quick < 9」之前就把 cap 返回了**，于是 quick >= 9 的
// 情形被当成了「赋值 >= cap」。而实际上 quick >= 9 时 m_0 与 10^9·H
// 可能**互相抵消**（赋值相同），v_p(M) 可以**大于** v_p(m_0) —— 捷径
// 的结论不可信。
//
// 后果是展开被**提前**截断（无声的精度损失），并且把本来无限的展开
// 误报成有限（exactFractionDigits 返回了错误的有限长度）。
//
// 四个复现值（期望值由 Python 的 fractions.Fraction 精确算出）：
//
//   0.1009765625 base 2  → 非有限，D = 34
//   0.1009765625 base 8  → 非有限，D = 12
//   0.1000001024  base 5  → 非有限，D = 15
//   0.1000001024  base 25 → 非有限，D = 8

namespace {

// 小数部分的位数（不含 "..." 标记）与是否带截断标记。
void splitFraction(const std::string& text, std::size_t& digits, bool& truncated) {
    const std::size_t dot = text.find('.');
    CHECK(dot != std::string::npos);
    const std::string fraction = text.substr(dot + 1);
    truncated = fraction.size() >= 3 &&
                fraction.compare(fraction.size() - 3, 3, "...") == 0;
    digits = truncated ? fraction.size() - 3 : fraction.size();
}

}  // namespace

TEST(base, valuation_underestimate_does_not_truncate_early) {
    struct Case {
        const char* text;
        int base;
        std::size_t digits;   // 期望的小数位数
    };
    // 期望值由 fractions.Fraction 精确算出（无法有限表示 → D 取最小的
    // D 使 base^D >= 10^scale）。
    const Case cases[] = {
        {"0.1009765625", 2, 34},
        {"0.1009765625", 8, 12},
        {"0.1000001024", 5, 15},
        {"0.1000001024", 25, 8},
    };

    for (const Case& c : cases) {
        const std::string text = hpc::toBaseString(make(c.text), c.base);
        std::size_t digits = 0;
        bool truncated = false;
        splitFraction(text, digits, truncated);
        // 位数必须**恰好**是期望值：早了是精度损失，晚了是多余的位。
        CHECK_EQ(digits, c.digits);
        // 这四个值在该进制下都**不能**有限表示，因此必须带截断标记。
        CHECK(truncated);
    }
}

// 同一个值在十进制下能有限表示，因此绝不带标记（回归对照）。
TEST(base, valuation_bug_does_not_affect_decimal) {
    const std::string text = hpc::toBaseString(make("0.1009765625"), 10);
    CHECK(text.find("...") == std::string::npos);
    CHECK_EQ(text, std::string("0.1009765625"));
}

// 旧实现把 0.1009765625 在**五进制**下误报成有限（长度 10，且不带
// 标记）。它实际上是无限的：分母 10^10 = 2^10·5^10，而五进制只带 5，
// 2^10 消不掉。
TEST(base, non_terminating_is_reported_as_non_terminating) {
    const std::string text = hpc::toBaseString(make("0.1009765625"), 5);
    CHECK(text.find("...") != std::string::npos);

    std::size_t digits = 0;
    bool truncated = false;
    splitFraction(text, digits, truncated);
    CHECK(truncated);
    // 期望 15 位（最小的 D 使 5^D >= 10^10）。
    CHECK_EQ(digits, static_cast<std::size_t>(15));
}

// --- 回归：工作量上限不再抛异常，而是截断并标记 -----------------------
//
// 展开代价是 O(位数 × 块数)。旧的实现触及 kMaxExpansionWork 时抛
// std::overflow_error，把一个本来能给出结果的请求整个拒掉。
//
// 现在超限时**截断**并如实标记「...」—— 「...」的含义本来就是
// 「展开被截断了」，而报错是把请求拒之门外。计算器用户更愿意看到一个
// 带标记的近似值，也不要一个空洞的异常。

TEST(base, expansion_work_limit_truncates_instead_of_throwing) {
    // scale = 100000 位的小数转成二进制需要约 100000 × 11112 次块运算，
    // 远超 kMaxExpansionWork（10^8）。
    //
    // 构造方式：1e-100000 是一个纯十进制有限小数，在二进制下**无法**有限
    // 表示（分母含 5），因此走的是「自动位数 + 截断」路径。
    const BigDecimal value = make("1e-100000");

    bool threw = false;
    std::string text;
    try {
        text = hpc::toBaseString(value, 2);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(!threw);

    // 截断时必须**带标记**（如实说明没写完）。
    CHECK(text.find("...") != std::string::npos);
    // 位数必须**小于**自动预算（10^100000 在二进制下需要 332193 位）。
    const std::size_t dot = text.find('.');
    CHECK(dot != std::string::npos);
    const std::string fraction = text.substr(dot + 1);
    CHECK(fraction.size() >= 3);
    CHECK(fraction.compare(fraction.size() - 3, 3, "...") == 0);
    CHECK(fraction.size() - 3 < 332193u);
}

// 显式请求一个远超工作量的位数时，同样是**截断 + 标记**，而不是异常。
TEST(base, explicit_huge_budget_truncates_instead_of_throwing) {
    const BigDecimal value = make("1e-50000");

    bool threw = false;
    std::string text;
    try {
        text = hpc::toBaseString(value, 2, false, 1000000);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(!threw);
    CHECK(text.find("...") != std::string::npos);
}

// 工作量之内的小数仍然**精确**展开（截断不能传染到普通用例）。
TEST(base, values_within_work_limit_stay_exact) {
    // 1e-1000 在二进制下有 3322 位展开，仍在工作量上限之内 —— 但它
    // 在二进制下**不能**有限表示，因此带标记。
    const std::string binary = hpc::toBaseString(make("1e-1000"), 2);
    CHECK(binary.find("...") != std::string::npos);
    std::size_t digits = 0;
    bool truncated = false;
    splitFraction(binary, digits, truncated);
    CHECK(truncated);
    // 1000 位十进制 → 3322 位二进制（1000 × log2(10) = 3321.9）。
    CHECK_EQ(digits, static_cast<std::size_t>(3322));

    // 十进制下永远精确。
    const std::string decimal = hpc::toBaseString(make("1e-1000"), 10);
    CHECK(decimal.find("...") == std::string::npos);
    CHECK_EQ(decimal, std::string("0.") + std::string(999, '0') + "1");
}


// --- 位运算 -----------------------------------------------------------

TEST(bitwise, and_or_xor) {
    CHECK_EQ(hpc::bitwise(make("255"), make("15"), BitOp::And).toString(), std::string("15"));
    CHECK_EQ(hpc::bitwise(make("255"), make("15"), BitOp::Or).toString(), std::string("255"));
    CHECK_EQ(hpc::bitwise(make("255"), make("15"), BitOp::Xor).toString(), std::string("240"));
    CHECK_EQ(hpc::bitwise(make("0"), make("0"), BitOp::Xor).toString(), std::string("0"));
    CHECK_EQ(hpc::bitwise(make("0"), make("0"), BitOp::And).toString(), std::string("0"));
    CHECK_EQ(hpc::bitwise(make("0"), make("0"), BitOp::Or).toString(), std::string("0"));
}

// 负数采用无限位二进制补码语义（与 Python 的 int 一致）。
TEST(bitwise, negative_operands) {
    // -1 的补码是全 1，因此 -1 & x == x，-1 | x == -1。
    CHECK_EQ(hpc::bitwise(make("-1"), make("255"), BitOp::And).toString(), std::string("255"));
    CHECK_EQ(hpc::bitwise(make("-1"), make("255"), BitOp::Or).toString(), std::string("-1"));
    CHECK_EQ(hpc::bitwise(make("-1"), make("-1"), BitOp::And).toString(), std::string("-1"));
    CHECK_EQ(hpc::bitwise(make("-1"), make("0"), BitOp::And).toString(), std::string("0"));

    // 5 | -3 == -3；5 & -3 == 5；5 ^ -3 == -8。
    CHECK_EQ(hpc::bitwise(make("5"), make("-3"), BitOp::Or).toString(), std::string("-3"));
    CHECK_EQ(hpc::bitwise(make("5"), make("-3"), BitOp::And).toString(), std::string("5"));
    CHECK_EQ(hpc::bitwise(make("5"), make("-3"), BitOp::Xor).toString(), std::string("-8"));

    // -5 & 3 == 3。
    CHECK_EQ(hpc::bitwise(make("-5"), make("3"), BitOp::And).toString(), std::string("3"));
    // -5 ^ -3 == 6。
    CHECK_EQ(hpc::bitwise(make("-5"), make("-3"), BitOp::Xor).toString(), std::string("6"));
}

TEST(bitwise, large_negative_operands) {
    // 超过一个十进制块的值：补码的宽度必须足够。
    const BigDecimal a = make("9223372036854775807");
    const BigDecimal b = make("1");
    CHECK_EQ(hpc::bitwise(a, b, BitOp::And).toString(), std::string("1"));
    CHECK_EQ(hpc::bitwise(a, b, BitOp::Or).toString(), a.toString());
    CHECK_EQ(hpc::bitwise(a, b, BitOp::Xor).toString(), std::string("9223372036854775806"));

    const BigDecimal negative = make("-9223372036854775808");
    CHECK_EQ(hpc::bitwise(negative, make("-1"), BitOp::And).toString(), negative.toString());
    CHECK_EQ(hpc::bitwise(negative, make("-1"), BitOp::Xor).toString(),
             std::string("9223372036854775807"));
}

TEST(bitwise, not) {
    // ~0 == -1，~(-1) == 0。
    CHECK_EQ(hpc::bitwiseNot(make("0")).toString(), std::string("-1"));
    CHECK_EQ(hpc::bitwiseNot(make("-1")).toString(), std::string("0"));
    CHECK_EQ(hpc::bitwiseNot(make("5")).toString(), std::string("-6"));
    CHECK_EQ(hpc::bitwiseNot(make("-5")).toString(), std::string("4"));
    // ~x == -x - 1。
    CHECK_EQ(hpc::bitwiseNot(make("255")).toString(), std::string("-256"));
}

TEST(bitwise, rejects_fraction) {
    bool threw = false;
    try {
        hpc::bitwise(make("1.5"), make("1"), BitOp::And);
    } catch (const std::domain_error&) { threw = true; }
    CHECK(threw);

    threw = false;
    try {
        hpc::bitwise(make("1"), make("1.5"), BitOp::Or);
    } catch (const std::domain_error&) { threw = true; }
    CHECK(threw);

    threw = false;
    try {
        hpc::bitwiseNot(make("1.5"));
    } catch (const std::domain_error&) { threw = true; }
    CHECK(threw);
}

// --- 移位 -------------------------------------------------------------

TEST(bitwise, shift_left) {
    CHECK_EQ(hpc::shiftLeft(make("1"), 8).toString(), std::string("256"));
    CHECK_EQ(hpc::shiftLeft(make("1"), 0).toString(), std::string("1"));
    CHECK_EQ(hpc::shiftLeft(make("0"), 100).toString(), std::string("0"));
    CHECK_EQ(hpc::shiftLeft(make("-1"), 3).toString(), std::string("-8"));
    CHECK_EQ(hpc::shiftLeft(make("3"), 1).toString(), std::string("6"));
    // 2^64 精确无误。
    CHECK_EQ(hpc::shiftLeft(make("1"), 64).toString(),
             std::string("18446744073709551616"));
    // 2^100。
    CHECK_EQ(hpc::shiftLeft(make("1"), 100).toString(),
             std::string("1267650600228229401496703205376"));
}

TEST(bitwise, shift_right) {
    CHECK_EQ(hpc::shiftRight(make("256"), 8).toString(), std::string("1"));
    CHECK_EQ(hpc::shiftRight(make("256"), 0).toString(), std::string("256"));
    CHECK_EQ(hpc::shiftRight(make("0"), 100).toString(), std::string("0"));
    // 整除丢掉的余数。
    CHECK_EQ(hpc::shiftRight(make("255"), 4).toString(), std::string("15"));
    CHECK_EQ(hpc::shiftRight(make("1"), 100).toString(), std::string("0"));
}

// 右移是精确的整除，不受精度影响。
TEST(bitwise, shift_right_is_exact_for_large_values) {
    const std::string large = "1267650600228229401496703205376";   // 2^100
    CHECK_EQ(hpc::shiftRight(make(large), 100).toString(), std::string("1"));
    CHECK_EQ(hpc::shiftRight(make(large), 50).toString(),
             std::string("1125899906842624"));           // 2^50
    CHECK_EQ(hpc::shiftRight(make(large), 64).toString(),
             std::string("68719476736"));               // 2^36
    CHECK_EQ(hpc::shiftRight(make(large), 32).toString(),
             std::string("295147905179352825856"));      // 2^68
}

// 负数右移向 -∞ 取整（与 Python 的 >> 一致）。
TEST(bitwise, shift_right_negative_rounds_down) {
    CHECK_EQ(hpc::shiftRight(make("-1"), 100).toString(), std::string("-1"));
    CHECK_EQ(hpc::shiftRight(make("-256"), 8).toString(), std::string("-1"));
    CHECK_EQ(hpc::shiftRight(make("-255"), 4).toString(), std::string("-16"));
    CHECK_EQ(hpc::shiftRight(make("-8"), 3).toString(), std::string("-1"));
}

TEST(bitwise, shift_rejects_bad_input) {
    bool threw = false;
    try { hpc::shiftLeft(make("1"), -1); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { hpc::shiftRight(make("1"), -1); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { hpc::shiftLeft(make("1.5"), 1); } catch (const std::domain_error&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { hpc::shiftRight(make("1.5"), 1); } catch (const std::domain_error&) { threw = true; }
    CHECK(threw);
}

// 左移与右移互逆（对非负数）。
TEST(bitwise, shift_round_trip) {
    const BigDecimal value = make("123456789012345678901234567890");
    CHECK_EQ(hpc::shiftRight(hpc::shiftLeft(value, 77), 77).toString(), value.toString());
}

// --- 科学计数法 -------------------------------------------------------

TEST(scientific, parses_exponent) {
    CHECK_EQ(make("1e20").toString(), std::string("100000000000000000000"));
    CHECK_EQ(make("1E5").toString(), std::string("100000"));
    CHECK_EQ(make("1e+5").toString(), std::string("100000"));
    CHECK_EQ(make("2.5e-3").toString(), std::string("0.0025"));
    CHECK_EQ(make("-1e5").toString(), std::string("-100000"));
    CHECK_EQ(make("1.5e2").toString(), std::string("150"));
    CHECK_EQ(make("0e0").toString(), std::string("0"));
    CHECK_EQ(make("5e0").toString(), std::string("5"));
    CHECK_EQ(make("123e-2").toString(), std::string("1.23"));
    CHECK_EQ(make("1e0").toString(), std::string("1"));
}

TEST(scientific, rejects_bad_exponent) {
    const char* bad[] = {"1e", "e5", "1e5e6", "1e5x", "1ee5", "1e99999999999"};
    for (const char* text : bad) {
        bool threw = false;
        try { make(text); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }
}

// 尾数里的 'e' 不是指数标记：报「非法字符」而不是「指数部分非法」。
TEST(scientific, mantissa_e_is_a_bad_character) {
    bool threw = false;
    try { make("abe"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { make("1a2e3"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

// 不含指数的输入行为不变（既有契约）。
TEST(scientific, plain_inputs_unchanged) {
    CHECK_EQ(make("123").toString(), std::string("123"));
    CHECK_EQ(make("0.5").toString(), std::string("0.5"));
    CHECK_EQ(make("007").toString(), std::string("7"));
    CHECK_EQ(make("1.500").toString(), std::string("1.5"));
    CHECK_EQ(make(".5").toString(), std::string("0.5"));
    CHECK_EQ(make("-2.25").toString(), std::string("-2.25"));
    CHECK_EQ(make("-0").toString(), std::string("0"));
}
