// 表达式解析与求值：递归下降。
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "core/BigDecimal.h"
#include "core/Rounding.h"

namespace hpc {

// 表达式错误：携带出错字符在原串中的位置（从 0 开始）。
//
// **位置约定**：position 指向**出错的字符本身**，而不是它后面的位置。
// 「表达式意外结束」这类错误没有 offending 字符，position 指向**串尾**
// —— 即缺失的字符本该出现的地方。
//
// 位置是**字节**下标（不是字符下标）：表达式按字节处理，非 ASCII 输入
// 也能用这个下标做「高亮第 n 个字节」。
class ExpressionError : public std::runtime_error {
public:
    ExpressionError(const std::string& message, int position)
        : std::runtime_error(message), position_(position) {}
    int position() const { return position_; }

private:
    int position_;
};

// 表达式的最大嵌套层数（括号与函数调用都算）。
//
// 递归下降解析器的每一层嵌套要占若干个栈帧，层数无上限时一个几百 KB
// 的输入就能把栈跑穿 —— 而栈溢出是**崩溃**，不是可捕获的异常：
// 前端连错误提示都来不及显示。
//
// 取 256：正常表达式远远用不到，它是「防呆」而不是「限制能力」。
// 超过上限时抛 ExpressionError（带位置），而不是崩溃。
inline constexpr int kMaxExpressionNestingDepth = 256;

// 求值表达式。语法错误抛 ExpressionError。
//
// **语法**（优先级由低到高，左结合）：
//
//   expression := term (('+' | '-') term)*
//   term       := unary (('*' | '/' | '%') unary)*
//   unary      := ('+' | '-')* primary
//   primary    := number | '(' expression ')' | name '(' args ')'
//
// 数字支持十进制小数、**科学计数法**（1e20、2.5E-3）与 0x/0b/0o 前缀
// 的整数。函数：sqrt abs pow exp ln log10。
//
// **取余（'%'）的语义**：**fmod**，不是「先取整再取余」。
//
//   - 符号跟随**被除数**（与 C 的 % 一致）：-7 % 3 == -1。
//   - 左操作数带小数时，余数**也带小数**：5.5 % 2 == 1.5（而不是
//     先把 5.5 取成 5 再取余得到的 1）。
//
// 内核的实现是把两个数对齐到共同的小数位再做整数长除法，所以
// 10 % 0.3 == 0.1（0.3 的二进制展开是无限循环小数，对齐到共同小数位
// 后余数是 0.1）。这与 C 的 fmod 一致。
//
// **零底数的次幂**：0^正数（含非整数指数）等于 **0**，不是错误；
// 0^0 与 0^负数才是定义域错误。负底数的非整数次幂同样是定义域错误。
//
// **精度**：context.precision 是**有效数字位数**（同 Python decimal 的
// prec），直接用于除法与超越函数；加、减、乘是精确运算，不受精度影响。
//
// **精度上限**：求值前会先检查精度本身，合法范围是 1..kMaxPrecision
// （100000）；用到超越函数（sqrt/exp/ln/log10，以及 pow 的**非整数指数**
// 分支）时还要额外满足 1..kMaxTranscendentalPrecision（1000）。
//
// 超出上限时抛 ExpressionError —— **不静默降精度**：静默地把 5000 位
// 降成 1000 位，等于给用户一个他没要求的答案，而错误信息里看不出这一点。
// 前端要「夹到上限」的行为，自己在调用前夹。
//
// **异常契约**：内核抛出的所有异常（定义域错误如 sqrt(-1)、ln(0)；
// 溢出如 exp 过大；精度越界）都在这里转换成 ExpressionError，
// 因此前端只需要捕获 **一种** 异常类型。
BigDecimal evaluate(std::string_view expression, const Context& context);

}  // namespace hpc
