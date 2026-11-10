// 高精度十进制数：base-10^9 分块存储，小端。
//
// 值 = sign_ * Σ digits_[i] * 10^(9*i) * 10^(-scale_)
// 其中 digits_[0] 为最低位块，scale_ 为小数位数（以 10 为单位）。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/Rounding.h"

namespace hpc {

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

    int sign_ = 0;                      // -1 / 0 / +1
    std::vector<std::uint32_t> digits_; // 小端 base-10^9 分块
    int scale_ = 0;                     // 小数位数（以 10 为单位）
};

}  // namespace hpc
