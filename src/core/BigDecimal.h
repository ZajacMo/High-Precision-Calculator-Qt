// 高精度十进制数：base-10^9 分块存储，小端。
//
// 值 = sign_ * Σ digits_[i] * 10^(9*i) * 10^(-scale_)
// 其中 digits_[0] 为最低位块，scale_ 为小数位数（以 10 为单位）。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Rounding.h"

namespace hpc {

// 小端 base-10^9 分块序列：blocks[0] 是最低位块，blocks.back() 是最高位块。
using Blk = std::vector<std::uint32_t>;

class BigDecimal {
public:
    BigDecimal();
    explicit BigDecimal(std::int64_t value);
    // 精确解析十进制字符串。支持可选负号、小数点、前导零。
    // 格式非法时抛出 std::invalid_argument。
    explicit BigDecimal(std::string_view text);

    static BigDecimal fromString(std::string_view text);

    // 规范化十进制表示：负号（若有）+ 整数部分 + 小数部分（若有）。
    // 值为 0 时输出 "0"，不输出 "-0"。
    std::string toString() const;

    // 精确运算：不接受 Context，结果总是精确的。
    BigDecimal operator+(const BigDecimal& other) const;
    BigDecimal operator-(const BigDecimal& other) const;
    BigDecimal operator*(const BigDecimal& other) const;

    // 不精确运算：必须显式给出精度。
    // 除数为零时抛出 std::domain_error。
    static BigDecimal divide(const BigDecimal& lhs, const BigDecimal& rhs, const Context& context);

    // 取余：lhs - trunc(lhs/rhs) * rhs。结果符号跟随被除数（与 C 的 % 一致）。
    // 除数为零时抛出 std::domain_error。
    static BigDecimal modulo(const BigDecimal& lhs, const BigDecimal& rhs);

    // 取整：按 context.precision 位有效数字取整（floor 向 -∞，ceil 向 +∞，
    // round 为银行家舍入 HalfEven）。
    static BigDecimal floor(const BigDecimal& value, const Context& context);
    static BigDecimal ceil(const BigDecimal& value, const Context& context);
    static BigDecimal round(const BigDecimal& value, const Context& context);

    // 比较：<0 表示本对象小于 other，0 表示相等，>0 表示大于。
    int compare(const BigDecimal& other) const;
    bool equals(const BigDecimal& other) const;

    static BigDecimal abs(const BigDecimal& value);

    bool isZero() const;
    int sign() const { return sign_; }

private:
    // 规范化：去除前导零块，去掉多余的尾随零。
    void normalize();

    // 对齐两个数的小数位，返回对齐后共同的 scale。
    static int alignScale(BigDecimal& lhs, BigDecimal& rhs);

    // 舍入到 context.precision 位有效数字。
    static BigDecimal roundToPrecision(const BigDecimal& value, const Context& context);

    // 逐块长除法 + 二分试商。返回 {商, 余数}。
    static std::pair<Blk, Blk> divideBlk(const Blk& dividend, const Blk& divisor);

    // 把精确分数 numerator/denominator 舍入到 precision 位有效数字。
    // 返回 {舍入后的整数, 小数位数}，即 值 = 整数 * 10^(-小数位数)。
    //
    // 舍入判定基于长除法的**精确余数**，因此能区分「正好一半」与
    // 「超过一半」—— 银行家舍入的偶数分支才可靠。
    static std::pair<Blk, int> roundQuotient(const Blk& numerator, const Blk& denominator,
                                             int precision, RoundingMode mode, bool negative);

    int sign_ = 0;                      // -1 / 0 / +1
    std::vector<std::uint32_t> digits_; // 小端 base-10^9 分块
    int scale_ = 0;                     // 小数位数（以 10 为单位）
};

}  // namespace hpc
