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

    // 是否为整数（没有小数位）。
    bool isInteger() const { return scale_ == 0; }

    // 十进制小数位的个数。
    //
    // 注意与 Context::precision 区分：precision 是**有效数字位**，
    // scale 是**小数位**（十进制小数点后的位数），两者不是一个概念。
    int scale() const { return scale_; }

    // 精确地向零截断到整数（丢弃小数部分，不做舍入）。
    //
    // -1.5 -> -1，1.5 -> 1。与 floor 的区别：floor 向 -∞ 取整，
    // -1.5 的 floor 是 -2。位运算与进制转换的整数部分都用这个语义。
    BigDecimal truncateToInteger() const;

    // 整数的**绝对值**的 base-10^9 小端块（digits_[0] 是最低位块）。
    //
    // 非整数时抛 std::domain_error —— 位运算这类「精确整数运算」的入口。
    // 符号不在这里：符号由 sign() 单独给出。
    const std::vector<std::uint32_t>& integerBlocks() const;

    // --- 超越函数 ---
    //
    // 这些函数本质上不精确，必须显式给出精度：结果按 context.precision
    // 位有效数字舍入，舍入方式取 context.mode。
    //
    // 定义域之外抛 std::domain_error：
    //   sqrt：负数
    //   ln / log10：非正数
    //   pow：负底数的非整数次幂、零的负数次幂
    //
    // 结果规模超出可计算范围时抛 std::overflow_error（exp 的参数过大、
    // pow 的整数指数过大）。

    // 平方根。
    static BigDecimal sqrt(const BigDecimal& value, const Context& context);

    // 幂。整数指数走精确的快速幂；非整数指数用 exp(b·ln a)。
    static BigDecimal pow(const BigDecimal& base, const BigDecimal& exponent,
                          const Context& context);

    // 自然指数 e^x。
    static BigDecimal exp(const BigDecimal& value, const Context& context);

    // 自然对数 ln(x)。
    static BigDecimal ln(const BigDecimal& value, const Context& context);

    // 常用对数 log10(x)。
    static BigDecimal log10(const BigDecimal& value, const Context& context);

private:
    // 规范化：去除前导零块，去掉多余的尾随零。
    void normalize();

    // 对齐两个数的小数位，返回对齐后共同的 scale。
    static int alignScale(BigDecimal& lhs, BigDecimal& rhs);

    // 舍入到 context.precision 位有效数字。
    static BigDecimal roundToPrecision(const BigDecimal& value, const Context& context);

    // --- 超越函数的内部工具 ---

    // 值的十进制指数 e：10^e <= |value| < 10^(e+1)。零值按 0 处理。
    int decimalExponent() const;

    // 精确地做 value * 10^exponent（指数可正可负，不引入舍入）。
    static BigDecimal shiftByPowerOfTen(const BigDecimal& value, int exponent);

    // 精确地乘以 2（十进制下 2 是有限的，除以 2 用乘 5 再移小数点）。
    static BigDecimal multiplyByTwo(const BigDecimal& value);

    // 精确地除以 2。
    static BigDecimal divideByTwo(const BigDecimal& value);

    // 向下取整到整数（scale 归零）。
    static BigDecimal floorToInteger(const BigDecimal& value);

    // scale 为 0 的整数值转成 long long；超出范围时抛 std::overflow_error。
    static long long toInteger(const BigDecimal& value);

    // 精确的整数幂 base^power（power >= 1）；结果位数超限时抛 std::overflow_error。
    static BigDecimal integerPower(const BigDecimal& base, long long power);

    // atanh 级数：z + z^3/3 + z^5/5 + ...（|z| <= 1/3 时收敛很快）。
    static BigDecimal atanhSeries(const BigDecimal& z, const Context& work);

    // exp(g) 的泰勒级数（0 <= g < ln(10) 时收敛很快）。
    static BigDecimal expSeries(const BigDecimal& g, const Context& work);

    // 把 value 分解成 mantissa * 10^exponent * 2^halvings，
    // 其中 mantissa ∈ [1, 2)，exponent 与 halvings 由输出参数给出。
    //
    // 移位与折半都是精确操作，不引入任何舍入。
    static BigDecimal decomposeForLog(const BigDecimal& value, int& exponent, int& halvings);

    // ln(2) 与 ln(10)：按 work 精度**实时计算**（硬编码常量在高精度下不够用）。
    // ln2 由调用方传入 —— 一次运算里 ln(2) 会被用到好几处。
    static BigDecimal ln2Constant(const Context& work);
    static BigDecimal ln10Constant(const BigDecimal& ln2, const Context& work);

    // 超越函数的内部实现：work 是工作精度（含保护位），final 是最终舍入上下文。
    static BigDecimal expInternal(const BigDecimal& value, const Context& work,
                                  const Context& final);
    static BigDecimal lnInternal(const BigDecimal& value, const Context& work);
    static BigDecimal log10Internal(const BigDecimal& value, const Context& work,
                                    const Context& final);

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
