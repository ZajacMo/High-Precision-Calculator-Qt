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
