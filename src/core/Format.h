// 进制转换与位运算：整数部分精确无损，位运算仅对整数有意义。
#pragma once

#include <string>
#include <string_view>

#include "core/BigDecimal.h"

namespace hpc {

// 把 value 转成 base 进制字符串。base 取值 2..36，非法 base 抛 std::invalid_argument。
//
// 整数部分**精确无损**；负值带前导负号；零输出 "0"。
//
// 小数部分：按目标进制逐位提取，最多输出 fractionDigits 位。该进制下无法
// 有限表示时，末尾用 "..." 显式标记截断 —— 不做无声的近似。
// fractionDigits 为 0 时自动推算位数：
//   - 该进制下**能**有限表示 → 位数取**精确展开长度**（由值的最简分数
//     算出，而不是按 log 估算）；
//   - **不能**有限表示 → 位数取最小的 D 使 base^D >= 10^小数位数。
// 整数输入不输出小数点，也不带标记。
//
// 小数部分的**尾随零会被去掉**（与 BigDecimal::toString 的做法一致）：
// 该进制下能有限表示的值给出最短的精确形式（0.5 在二进制下是 "0.1"），
// 只有确实发生了截断时才补上 "..." 标记 —— 标记是如实的，值算得完就没有
// 标记。
//
// **「...」的契约**（截断与精度的唯一约定）：
//
//   - "..." 表示「展开**被截断了**」，且截断的是**小数部分**。
//   - 出现 "..." 时，前面的数字是展开的**真实前缀**（不是四舍五入的
//     近似值），末位数字也是真实数字 —— 删掉它会改变数值。
//   - 该进制下**不能**有限表示的值**一定**带 "..."。
//   - 该进制下**能**有限表示的值**通常**不带 "..."；但如果展开长度
//     太大、超出工作量上限（见下），它同样会带 "..." —— 那时它确实
//     没写完。
//   - 精度预算（fractionDigits，或自动推算的位数）不足时会截断并标记；
//     这**不是**错误。
//
// **工作量上限**：展开代价是 O(位数 × 块数)。当预算大到算不完时，
// toBaseString **不会**抛异常 —— 它算出算得动的那部分位，**截断**并
// 如实加上 "..." 标记。理由：计算器的调用方要的是一个答案（哪怕是带
// 标记的近似值），而不是一个空洞的异常；而且「...」的含义本来就是
// 「没算完」，如实标记不算撒谎 —— 撒谎的是「算完了却说没算完」，
// 那是相反的方向。
//
// 唯一的例外是**非法输入**：base 越界抛 std::invalid_argument。
std::string toBaseString(const BigDecimal& value, int base, bool uppercase = false,
                          int fractionDigits = 0);

// 解析 base 进制整数字符串（可带前导正负号）。base 取值 2..36。
// 空串、非法字符、数字超出进制范围、含小数点时抛 std::invalid_argument。
//
// 只支持整数：任意进制的小数在十进制内核里未必能精确表示（1/3 在三进制下
// 无法有限表示），接受它就等于引入无声的近似。输出侧的小数部分用 "..."
// 标记截断，解析侧则直接拒绝。
BigDecimal fromBaseString(std::string_view text, int base);

enum class BitOp { And, Or, Xor };

// 整数位运算，采用**无限位**二进制补码语义（与 Python 的 int 运算一致）：
// 负数按符号扩展参与运算，运算结果仍是精确整数。
// 任一操作数非整数时抛 std::domain_error。
BigDecimal bitwise(const BigDecimal& lhs, const BigDecimal& rhs, BitOp op);

// 按位取反：~x = -x - 1。非整数输入抛 std::domain_error。
BigDecimal bitwiseNot(const BigDecimal& value);

// 左移 bits 位（相当于乘以 2^bits）。非整数输入抛 std::domain_error；
// bits 为负抛 std::invalid_argument；结果位数超限抛 std::overflow_error。
BigDecimal shiftLeft(const BigDecimal& value, int bits);

// 右移 bits 位（相当于整除 2^bits），负数**向 -∞ 取整**（与 Python 的 >> 一致）：
// -1 >> 100 == -1。
BigDecimal shiftRight(const BigDecimal& value, int bits);

}  // namespace hpc
