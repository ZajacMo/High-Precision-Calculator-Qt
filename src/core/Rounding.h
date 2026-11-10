// 舍入模式与运算上下文：精度是显式参数，不是全局状态。
#pragma once

namespace hpc {

// 舍入模式。语义与 Python decimal 的 rounding 名称对齐。
enum class RoundingMode {
    HalfEven,  // 四舍六入五成双（银行家舍入）
    HalfUp,    // 四舍五入
    Down,      // 向零截断
    Up,        // 远离零截断
    Ceil,      // 向 +∞ 取整
    Floor,     // 向 -∞ 取整
};

// 运算上下文：仅用于本质上不精确的运算（除法、开方、幂、指数、对数）。
// 精确运算（加、减、乘）不接受此结构，从编译期杜绝精度被隐式改变。
struct Context {
    int precision = 30;                  // 有效数字位数
    RoundingMode mode = RoundingMode::HalfEven;

    static Context default_() { return Context{}; }
};

}  // namespace hpc
