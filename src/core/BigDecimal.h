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

// --- 运算上限 ---
//
// 这些上限是**公开契约**：调用方（尤其是前端）需要知道自己能请求多大的
// 精度，否则只能靠试错发现某个值会抛异常。
//
// 分成两层，因为两类运算的代价模型完全不同：

// 除法与舍入的精度上限。
//
// 除法要按位做长除法，代价随精度线性增长（再乘上被除数的位数）。
// 实测 p=100000 时单次除法要几十秒 —— 已经很慢，但能算完。
inline constexpr int kMaxPrecision = 100000;

// 超越函数的精度上限。
//
// 与 kMaxPrecision 不同：超越函数的工作精度是**平方级**的。
// 实测（本机 GCC -O2，最坏情况是 pow(2, 0.5)）：
//
//   p=200  →    413 ms
//   p=400  →   3.4 s
//   p=800  →    22 s
//   p=1600 →   151 s
//   p=1000（exp）→  23 s
//
// 也就是说 kMaxPrecision = 100000 对超越函数而言远远不够 ——
// 它们在 p=1600 时就已经要两分半，p=99990 更是永远算不完。
//
// 所以超越函数单独用一个上限，取 1000：最坏情况约 40 秒，
// 属于「可接受的慢」而不是「永远算不完」。
//
// 这是**产品决策**而非数学限制 —— 真正需要更高精度的调用方
// 应该分批求值（例如先用 1000 位求出前导部分再细化）。
//
// **注**：上表的实测数据是**优化前**的。超越函数曾经是三次方复杂度
// （级数每项一次全精度除法，而通用除法用二分试商是 O(块数²)），
// 优化后「除以小整数」与「舍入」都走线性路径，整体降到接近平方级：
//
//   p=50  →  exp(1)    1 ms    ln(2)   0.4 ms
//   p=100 →  exp(1)  1.3 ms    ln(2)   0.5 ms
//   p=200 →  exp(1)  3.1 ms    ln(2)   1.2 ms
//   p=400 →  exp(1) 9.6 ms    ln(2)   3.9 ms
//   p=800 →  exp(1)   38 ms    ln(2)    18 ms
//   p=1000 → exp(1)   65 ms    ln(2)    35 ms
//
// 上限**保持 1000 不变** —— 它是公开契约，改动要与调用方协调；
// 但现在这个上限是「保守」而非「不得不然」，可以另行讨论是否提高。
inline constexpr int kMaxTranscendentalPrecision = 1000;

// 整数幂的指数上限。
inline constexpr long long kMaxIntegerPower = 100000;

// 整数幂结果的位数上限（防止 10^100000 这类天文数字）。
//
// 上限按**结果的十进制位数**估算，整数位与小数位都算在内。
//
// 估算式：结果位数 <= power × (底数的有效位数 + 底数的小数位数)。
//
// 底数 |b| 有 s 位有效数字、t 位小数时，|b| ∈ [10^(s-1-t), 10^(s-t))，
// 于是 |b^power| 的位数不超过 power × (s + t)：
//   - 值 >= 1 时：整数位 <= power × (s - t) <= power × (s + t)
//   - 值 <  1 时：小数位 <= power × (t - s + 1) <= power × (s + t)
//
// 所以这个估算**总是 >= 实际位数**（最多高估 3 倍），上限因此是硬约束。
//
// 只看底数的整数位数会完全漏掉小数底数：0.001 的整数位数是 -2，
// 那个「整数位数 > 0」的判据会把整个护栏跳过，而结果的**小数位数**
// 恰恰是按 power 线性增长的 —— 0.001^100000 需要 30 万位小数。
//
// 实测（本机 GCC -O2，precision=20）：
//
//   pow(2, 100000)              511 ms     结果 3 万位
//   pow(3, 100000)             1325 ms     结果 4.8 万位
//   pow(10, 100000)            8041 ms     结果 10 万位
//   pow(0.001, 100000)        67598 ms     结果 30 万位小数
//
// 30 万位已经要一分多钟，所以上限取 30 万：
// 估算 <= 30 万 ⟹ 实际位数 <= 30 万 ⟹ 单次不超过一分多钟。
inline constexpr int kMaxPowerResultDigits = 300000;

// 科学计数法的指数上限（|exponent| 的**精确**上限）。
//
// 指数为正时结果是 10^exponent 量级的整数，为负时是极小的分数。
// 两个方向都要有上限，否则 "1e999999999" 会把内存吃光。
//
// 上限取 10^6：正指数产生 100 万位整数（约 450 KB），负指数产生
// 100 万位小数 —— 都是「能算但没必要」的量级，再大就没有实用价值。
//
// 检查必须**在把当前位折进 magnitude 之后**做：先检查再折的话，
// 最后一位会把 magnitude 顶到 10 × 上限，实际接受的上限就是 10 倍。
inline constexpr long long kMaxExponentMagnitude = 1000000;

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
    // context.precision > kMaxPrecision 时抛 std::invalid_argument。
    static BigDecimal divide(const BigDecimal& lhs, const BigDecimal& rhs, const Context& context);

    // 取余：lhs - trunc(lhs/rhs) * rhs。结果符号跟随被除数（与 C 的 % 一致）。
    // 除数为零时抛出 std::domain_error。
    //
    // 取余是**精确**运算（整数长除法），不接受 Context，因此不受
    // kMaxPrecision 约束。
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

    // **分子**（绝对值）的 base-10^9 小端块。
    //
    // 值 = ±numerator * 10^(-scale)。整数时与 integerBlocks() 相同。
    //
    // 与 integerBlocks() 的区别：这里**不要求**值是整数 —— 小数值的分子
    // 同样有意义（进制转换要靠它判断该值能否在目标进制下有限表示）。
    const std::vector<std::uint32_t>& numeratorBlocks() const { return digits_; }

    // --- 超越函数 ---
    //
    // 这些函数本质上不精确，必须显式给出精度：结果按 context.precision
    // 位有效数字舍入，舍入方式取 context.mode。
    //
    // 定义域之外抛 std::domain_error：
    //   sqrt：负数
    //   ln / log10：非正数
    //   pow：负底数的非整数次幂、零的负数次幂、零的零次幂
    //
    // 结果规模超出可计算范围时抛 std::overflow_error（exp 的参数过大、
    // pow 的整数指数过大、pow 的结果位数过大）。
    //
    // 精度超出上限时抛 std::invalid_argument：
    //
    //   - sqrt / exp / ln / log10 与 pow 的**非整数指数**分支：
    //     context.precision > kMaxTranscendentalPrecision。
    //     这五个入口的工作精度是平方级的（见 kMaxTranscendentalPrecision
    //     的实测数据），p=1600 时 pow(2, 0.5) 要跑 151 s，p=99990 更是
    //     永远算不完 —— 而且失败方式**不一致**（有的精度下内层的 divide
    //     恰好抛异常，有的精度下则一路挂起）。统一的 std::invalid_argument
    //     让调用方拿到确定的行为。
    //
    //   - divide / roundToPrecision / floor / ceil / round：
    //     context.precision > kMaxPrecision。除法的代价随精度线性增长，
    //     p=100000 时单次要几十秒 —— 能算完，但已经不值得。
    //
    // 注意两者的区别：**pow 的整数指数分支不受
    // kMaxTranscendentalPrecision 约束** —— 整数次幂走的是精确的快速幂，
    // 代价由 kMaxIntegerPower（指数）与 kMaxPowerResultDigits（结果位数）
    // 单独约束，与超越函数的工作精度毫无关系。pow(10, 100000) 在
    // precision=2000 下完全合法。

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

    // 除以一个小整数（级数里的 2k+1 与 n! 的因子都是这种形状）。
    //
    // 语义与 divide(value, BigDecimal(divisor), context) **完全一致**
    // （同样的舍入模式、同样的精度处理），但走的是线性时间的路径：
    // 除数是「单块整数 × 10 的幂」，可以一步长除法算出来，
    // 不必用通用路径的「二分试商」逐位试商（那是 O(块数²)）。
    //
    // divisor >= 10^9 时回退到通用路径（那时除数已经不止一块了）。
    static BigDecimal divideBySmallInt(const BigDecimal& value, std::uint64_t divisor,
                                       const Context& context);

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
