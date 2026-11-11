#include "test_framework.h"

#include <exception>
#include <string>

#include "core/Expression.h"

using hpc::BigDecimal;
using hpc::Context;
using hpc::ExpressionError;
using hpc::RoundingMode;

namespace {

// 求值并返回结果。
//
// **异常不外泄**：抛出的 ExpressionError 会被记成一个失败的断言，
// 返回 0 作为哨兵。
//
// 理由：测试框架的 main 循环**不捕获**异常，一个用例抛出异常会直接
// 终止整个测试二进制 —— 后面几十个用例一个都跑不到，失败原因也被
// 淹没。测试辅助函数把异常变成断言失败，才能看到**全部**结果。
BigDecimal eval(const std::string& text) {
    try {
        return hpc::evaluate(text, Context{20, RoundingMode::HalfEven});
    } catch (const ExpressionError& e) {
        ::testing::reportFailure(__FILE__, __LINE__,
                                 "求值 " + text + " 意外失败：" + e.what());
        return BigDecimal(0);
    }
}

// 指定精度与舍入模式的版本。
BigDecimal eval(const std::string& text, int precision, RoundingMode mode) {
    try {
        return hpc::evaluate(text, Context{precision, mode});
    } catch (const ExpressionError& e) {
        ::testing::reportFailure(__FILE__, __LINE__,
                                 "求值 " + text + " 意外失败：" + e.what());
        return BigDecimal(0);
    }
}

// 求值并返回结果字符串；出错时返回 "!" + 错误信息。
std::string evalOrError(const std::string& text) {
    try {
        return hpc::evaluate(text, Context{20, RoundingMode::HalfEven}).toString();
    } catch (const ExpressionError& e) {
        return std::string("!") + e.what();
    }
}

// 捕获 ExpressionError 并回填位置与信息。
bool tryEval(const std::string& text, int& position, std::string& message) {
    try {
        hpc::evaluate(text, Context{20, RoundingMode::HalfEven});
    } catch (const ExpressionError& e) {
        position = e.position();
        message = e.what();
        return false;
    }
    position = -1;
    message.clear();
    return true;
}

// 指定精度下的版本。
bool tryEvalAt(const std::string& text, int precision, RoundingMode mode, int& position,
                std::string& message) {
    try {
        hpc::evaluate(text, Context{precision, mode});
    } catch (const ExpressionError& e) {
        position = e.position();
        message = e.what();
        return false;
    }
    position = -1;
    message.clear();
    return true;
}

// 断言「求值失败，且位置指向指定的那个字符」。
void checkErrorAt(const std::string& text, int expectedPosition) {
    int position = -1;
    std::string message;
    const bool ok = tryEval(text, position, message);
    CHECK(!ok);
    if (!ok) {
        CHECK_EQ(position, expectedPosition);
        CHECK(!message.empty());
    }
}

}  // namespace

// --- 算术与优先级 ---

TEST(expression, arithmetic) {
    CHECK_EQ(eval("1+2").toString(), std::string("3"));
    CHECK_EQ(eval("2*3+4").toString(), std::string("10"));
    CHECK_EQ(eval("2+3*4").toString(), std::string("14"));
    CHECK_EQ(eval("(2+3)*4").toString(), std::string("20"));
    CHECK_EQ(eval("100/4").toString(), std::string("25"));
    CHECK_EQ(eval("2*(3+4)").toString(), std::string("14"));
    CHECK_EQ(eval("1+2*3-4/2").toString(), std::string("5"));
}

TEST(expression, leftAssociativity) {
    // 左结合：10-4-3 = (10-4)-3 = 3，不是 10-(4-3) = 9。
    CHECK_EQ(eval("10-4-3").toString(), std::string("3"));
    // 8/2/2 = (8/2)/2 = 2，不是 8/(2/2) = 8。
    CHECK_EQ(eval("8/2/2").toString(), std::string("2"));
    // 20/5*2 = (20/5)*2 = 8，不是 20/(5*2) = 2。
    CHECK_EQ(eval("20/5*2").toString(), std::string("8"));
}

TEST(expression, exactness) {
    // 加减乘是精确运算，不受精度影响。
    CHECK_EQ(eval("0.1+0.2").toString(), std::string("0.3"));
    CHECK_EQ(eval("1.5*2").toString(), std::string("3"));
    // 精确的除法不该被精度截断。
    CHECK_EQ(eval("1/8").toString(), std::string("0.125"));
    CHECK_EQ(eval("1/2").toString(), std::string("0.5"));
    CHECK_EQ(eval("1/4").toString(), std::string("0.25"));
}

// --- 一元运算符 ---

TEST(expression, unary) {
    CHECK_EQ(eval("-5").toString(), std::string("-5"));
    CHECK_EQ(eval("--5").toString(), std::string("5"));
    CHECK_EQ(eval("---5").toString(), std::string("-5"));
    CHECK_EQ(eval("+5").toString(), std::string("5"));
    CHECK_EQ(eval("-+5").toString(), std::string("-5"));
    CHECK_EQ(eval("2*-3").toString(), std::string("-6"));
    CHECK_EQ(eval("-(3+4)").toString(), std::string("-7"));
    CHECK_EQ(eval("-(3+4)*2").toString(), std::string("-14"));
    CHECK_EQ(eval("-2*-3").toString(), std::string("6"));
}

// 一长串一元符号：迭代实现，不能把栈跑穿。
TEST(expression, longUnaryChainDoesNotOverflowStack) {
    std::string text(100001, '-');
    text += "5";
    CHECK_EQ(eval(text).toString(), std::string("-5"));

    std::string even(100000, '-');
    even += "5";
    CHECK_EQ(eval(even).toString(), std::string("5"));
}

// --- 空白与空表达式 ---

TEST(expression, whitespaceTolerated) {
    CHECK_EQ(eval("1 + 2").toString(), std::string("3"));
    CHECK_EQ(eval("  1+2  ").toString(), std::string("3"));
    CHECK_EQ(eval("sqrt( 9 )").toString(), std::string("3"));
    CHECK_EQ(eval("\t1 +\n2").toString(), std::string("3"));
    CHECK_EQ(eval("pow( 2 , 10 )").toString(), std::string("1024"));
    CHECK_EQ(eval("( 1 + 2 ) * 3").toString(), std::string("9"));
}

TEST(expression, emptyExpressionIsAnError) {
    checkErrorAt("", 0);
    checkErrorAt("   ", 3);
    checkErrorAt("\t\n", 2);
}

// --- 函数 ---

TEST(expression, functions) {
    CHECK_EQ(eval("sqrt(9)").toString(), std::string("3"));
    CHECK_EQ(eval("abs(-3)").toString(), std::string("3"));
    CHECK_EQ(eval("abs(3)").toString(), std::string("3"));
    CHECK_EQ(eval("pow(2,10)").toString(), std::string("1024"));
    CHECK_EQ(eval("exp(0)").toString(), std::string("1"));
    CHECK_EQ(eval("ln(1)").toString(), std::string("0"));
    CHECK_EQ(eval("log10(100)").toString(), std::string("2"));
    // sqrt(2) 在 20 位有效数字下是 1.4142135623730950488。
    CHECK_EQ(eval("sqrt(2)").toString(), std::string("1.4142135623730950488"));
    // sqrt(2)*sqrt(2) **不是** 2：两个 20 位近似值的乘积在第 20 位
    // 上有 1 ulp 的偏差。这是精度语义的正确表现，不是缺陷。
    CHECK_EQ(eval("sqrt(2)*sqrt(2)").toString(),
             std::string("1.99999999999999999999522356663907438144"));
}

TEST(expression, nestedFunctions) {
    CHECK_EQ(eval("sqrt(abs(-16))").toString(), std::string("4"));
    CHECK_EQ(eval("pow(pow(2,3),2)").toString(), std::string("64"));
    CHECK_EQ(eval("exp(ln(5))").toString(), std::string("5"));
    CHECK_EQ(eval("log10(pow(10,3))").toString(), std::string("3"));
}

// --- 进制字面量与科学计数法 ---

TEST(expression, bases) {
    CHECK_EQ(eval("0xFF").toString(), std::string("255"));
    CHECK_EQ(eval("0xFF + 1").toString(), std::string("256"));
    CHECK_EQ(eval("0b1010").toString(), std::string("10"));
    CHECK_EQ(eval("0o777").toString(), std::string("511"));
    CHECK_EQ(eval("0XFF").toString(), std::string("255"));
    CHECK_EQ(eval("0x0").toString(), std::string("0"));
    CHECK_EQ(eval("0x10 + 0b10 + 0o10").toString(), std::string("26"));
    CHECK_EQ(eval("-0xFF").toString(), std::string("-255"));
}

// 0x1e5 是十六进制的 1e5（485），不是科学计数法。
TEST(expression, hexPrefixIsNotScientificNotation) {
    CHECK_EQ(eval("0x1e5").toString(), std::string("485"));
    CHECK_EQ(eval("0xE5").toString(), std::string("229"));
    // 而 "0b1e5" 里 'e' 不是二进制数位 → 报错，位置指向 'e'。
    checkErrorAt("0b1e5", 3);
}

// 1e5 是科学计数法（100000），不是「1 后面跟着非法的 e」。
TEST(expression, scientificNotation) {
    CHECK_EQ(eval("1e5").toString(), std::string("100000"));
    CHECK_EQ(eval("1E5").toString(), std::string("100000"));
    CHECK_EQ(eval("2.5e-3").toString(), std::string("0.0025"));
    CHECK_EQ(eval("2.5E-3").toString(), std::string("0.0025"));
    CHECK_EQ(eval("1e5 + 1").toString(), std::string("100001"));
    CHECK_EQ(eval("1.5e2").toString(), std::string("150"));
    CHECK_EQ(eval("0e5").toString(), std::string("0"));
    CHECK_EQ(eval("1e0").toString(), std::string("1"));
    CHECK_EQ(eval("1e+3").toString(), std::string("1000"));
    CHECK_EQ(eval("1e-3").toString(), std::string("0.001"));
}

// 科学计数法的指数上限：|exponent| 的上限是 10^6（内核的
// kMaxExponentMagnitude）。
//
// 上限之内要能算，上限之外要报错 —— 而且报错的位置指向那个 'e'，
// 与 "1e" / "1e+" 的处理一致。
TEST(expression, scientificNotationExponentLimit) {
    // 正好在上限之内：正指数（上百万位的整数）与负指数（上百万位的
    // 小数）都要能算出来。
    const std::string big = eval("1e1000000").toString();
    CHECK_EQ(big.size(), static_cast<std::size_t>(1000001));
    CHECK_EQ(big.substr(0, 2), std::string("10"));
    CHECK_EQ(big.substr(big.size() - 1), std::string("0"));

    const std::string small = eval("1e-1000000").toString();
    CHECK_EQ(small.size(), static_cast<std::size_t>(1000002));
    CHECK_EQ(small.substr(0, 2), std::string("0."));
    CHECK_EQ(small.substr(small.size() - 1), std::string("1"));

    // 越界就是错误，位置指向那个 'e'。
    checkErrorAt("1e1000001", 1);
    checkErrorAt("1e-1000001", 1);
    checkErrorAt("1e999999999", 1);
}

TEST(expression, decimalLiterals) {
    CHECK_EQ(eval("0.5").toString(), std::string("0.5"));
    CHECK_EQ(eval(".5").toString(), std::string("0.5"));
    CHECK_EQ(eval("5.").toString(), std::string("5"));
    CHECK_EQ(eval("0.0000001").toString(), std::string("0.0000001"));
    CHECK_EQ(eval("123456789012345678901234567890").toString(),
             std::string("123456789012345678901234567890"));
}

// --- 取余 ---

TEST(expression, modulo) {
    CHECK_EQ(eval("7%3").toString(), std::string("1"));
    CHECK_EQ(eval("10%3").toString(), std::string("1"));
    CHECK_EQ(eval("10%1").toString(), std::string("0"));
    CHECK_EQ(eval("10%20").toString(), std::string("10"));
}

TEST(expression, moduloSignFollowsDividend) {
    // 符号跟随被除数（与 C 的 % 一致）。
    CHECK_EQ(eval("-7%3").toString(), std::string("-1"));
    CHECK_EQ(eval("7%-3").toString(), std::string("1"));
    CHECK_EQ(eval("-7%-3").toString(), std::string("-1"));
}

TEST(expression, moduloWithFractionalOperands) {
    // 内核的取余是「整数余数」语义，但实现上是把两个数对齐到共同的小数位
    // 再做整数长除法 —— 等价于 C 的 fmod：
    //   5.5 = 2 * 2 + 1.5  → 5.5 % 2 = 1.5
    //   10 = 0.3 * 33 + 0.1 → 10 % 0.3 = 0.1
    CHECK_EQ(eval("5.5%2").toString(), std::string("1.5"));
    CHECK_EQ(eval("10%0.3").toString(), std::string("0.1"));
    CHECK_EQ(eval("-5.5%2").toString(), std::string("-1.5"));
}

// --- 错误 ---

TEST(expression, syntaxErrors) {
    CHECK(evalOrError("1+").rfind("!", 0) == 0);
    CHECK(evalOrError("(1+2").rfind("!", 0) == 0);
    CHECK(evalOrError("1+*2").rfind("!", 0) == 0);
    CHECK(evalOrError("1.2.3").rfind("!", 0) == 0);
    CHECK(evalOrError("*").rfind("!", 0) == 0);
    CHECK(evalOrError("()").rfind("!", 0) == 0);
    CHECK(evalOrError("1 2").rfind("!", 0) == 0);
    CHECK(evalOrError(")").rfind("!", 0) == 0);
    CHECK(evalOrError("1)").rfind("!", 0) == 0);
}

TEST(expression, divisionByZero) {
    CHECK(evalOrError("1/0").rfind("!", 0) == 0);
    CHECK(evalOrError("1/(2-2)").rfind("!", 0) == 0);
    CHECK(evalOrError("1%0").rfind("!", 0) == 0);
    // 抛的必须是 **ExpressionError**，而不是内核的 domain_error。
    //
    // 这里不能只用 tryEval —— 它只捕获 ExpressionError，内核的
    // domain_error 若漏出来会**一路传播出去**，而测试框架的 main
    // **不捕获**异常：整个测试二进制直接终止，后面几十个用例一个都
    // 跑不到，失败原因也被淹没。所以要显式地 catch 住 std::exception，
    // 断言「漏出来的不是别的东西」。
    int position = -1;
    std::string message;
    CHECK(!tryEval("1/0", position, message));

    bool threwExpressionError = false;
    try {
        hpc::evaluate("1/0", Context{20, RoundingMode::HalfEven});
    } catch (const ExpressionError&) {
        threwExpressionError = true;
    } catch (const std::exception&) {
        threwExpressionError = false;
    }
    CHECK(threwExpressionError);
}

TEST(expression, domainErrorsAreExpressionErrors) {
    // sqrt(-1)、ln(0)、log10(0) 在内核里是 std::domain_error，
    // 表达式层必须把它们变成 ExpressionError。
    int position = -1;
    std::string message;

    CHECK(!tryEval("sqrt(-1)", position, message));
    CHECK(!tryEval("ln(0)", position, message));
    CHECK(!tryEval("log10(-1)", position, message));
    CHECK(!tryEval("pow(0,0)", position, message));
    CHECK(!tryEval("pow(-2,0.5)", position, message));

    // 内核的其它异常类型也必须被转换。
    bool caughtAsExpressionError = false;
    try {
        hpc::evaluate("exp(99999999)", Context{20, RoundingMode::HalfEven});
    } catch (const ExpressionError&) {
        caughtAsExpressionError = true;
    } catch (const std::exception&) {
        caughtAsExpressionError = false;
    }
    CHECK(caughtAsExpressionError);
}

// 零底数的次幂：三种情形必须分清。
//
// 内核的契约（BigDecimal.h 的「定义域之外抛 std::domain_error」）：
//   - 零的**负**数次幂 → 定义域错误
//   - 零的**零**次幂   → 定义域错误（decimal 规范里也是非法运算）
//   - 零的**正**数次幂 → 0（**不是**错误！与 Python 的 decimal 一致）
//
// 最后一条最容易搞错：非整数指数走的是 exp(b·ln a) 那条超越函数的路，
// 而 ln(0) 是未定义的 —— 但 0^正数仍然有定义 0，内核为此专门在
// 「负底数未定义」之前先判零底数。
TEST(expression, zeroBasePower) {
    // 整数指数。
    CHECK_EQ(eval("pow(0,2)").toString(), std::string("0"));
    CHECK_EQ(eval("pow(0,10)").toString(), std::string("0"));
    // 非整数指数：**有定义**，结果是 0。
    CHECK_EQ(eval("pow(0,0.5)").toString(), std::string("0"));
    CHECK_EQ(eval("pow(0,1.5)").toString(), std::string("0"));
    CHECK_EQ(eval("pow(0,2.5)").toString(), std::string("0"));
    // 零次幂与负数次幂：定义域错误。
    CHECK(evalOrError("pow(0,0)").rfind("!", 0) == 0);
    CHECK(evalOrError("pow(0,-1)").rfind("!", 0) == 0);
    CHECK(evalOrError("pow(0,-0.5)").rfind("!", 0) == 0);
    // 负底数的非整数次幂仍然是错误（这条不能被零底数的规则带跑）。
    CHECK(evalOrError("pow(-2,0.5)").rfind("!", 0) == 0);
}

TEST(expression, unknownFunction) {
    CHECK(evalOrError("unknown(1)").rfind("!", 0) == 0);
    CHECK(evalOrError("foo()").rfind("!", 0) == 0);
    // 裸标识符（没有 '('）也是错误。
    CHECK(evalOrError("x").rfind("!", 0) == 0);
    CHECK(evalOrError("sqrt").rfind("!", 0) == 0);
}

TEST(expression, wrongArgumentCount) {
    CHECK(evalOrError("sqrt()").rfind("!", 0) == 0);
    CHECK(evalOrError("sqrt(1,2)").rfind("!", 0) == 0);
    CHECK(evalOrError("pow(2)").rfind("!", 0) == 0);
    CHECK(evalOrError("pow(2,3,4)").rfind("!", 0) == 0);
    CHECK(evalOrError("abs()").rfind("!", 0) == 0);
}

// --- 错误位置 ---
//
// 位置必须指向**出错的字符本身**，而不是笼统的 0。
// 「表达式意外结束」这类错误没有 offending 字符，位置指向串尾。

TEST(expression, errorPositions) {
    checkErrorAt("1+", 2);          // 串尾：缺失的操作数
    checkErrorAt("1+*2", 2);        // '*' 不是一个合法的操作数开头
    checkErrorAt("(1+2", 4);        // 串尾：缺失的 ')'
    checkErrorAt("1/0", 2);         // 除数 '0' 所在的位置
    checkErrorAt("1%0", 2);         // 除数 '0' 所在的位置
    checkErrorAt("sqrt(-1)", 5);    // 出错的实参 '-1' 的起点
    checkErrorAt("ln(0)", 3);       // 出错的实参 '0' 的起点
    checkErrorAt("unknown(1)", 0);  // 未知的函数名
    checkErrorAt("sqrt()", 0);       // 参数个数不对 → 函数名
    checkErrorAt("pow(2)", 0);      // 参数个数不对 → 函数名
    checkErrorAt("1.2.3", 3);       // 第二个小数点
    checkErrorAt("0b2", 2);          // '2' 不是二进制数位
    checkErrorAt("0x", 2);          // 前缀后没有数位
    checkErrorAt("1e", 1);          // 指数部分非法 → 'e'
    checkErrorAt("1e+", 1);         // 指数部分非法 → 'e'
    checkErrorAt("1e999999999", 1); // 指数过大 → 'e'（不是指数里的数字）
    checkErrorAt("1e-999999999", 1); // 指数过大 → 'e'
    checkErrorAt("1.5e999999999", 3); // 指数过大 → 'e'
    checkErrorAt(")", 0);           // 多余的字符
    checkErrorAt("1)2", 1);         // 多余的字符
    checkErrorAt("1+2)", 3);        // 多余的字符
    checkErrorAt("()", 1);          // 括号里什么都没有
}

TEST(expression, errorPositionForUnknownIdentifier) {
    // 裸标识符的位置指向标识符的起点。
    checkErrorAt("   x", 3);
    checkErrorAt("1 + foo", 4);
}

TEST(expression, errorPositionForBaseDigits) {
    // 进制字面量的非法数位：位置指向那个字符。
    checkErrorAt("0b2", 2);
    checkErrorAt("0xzz", 2);
    checkErrorAt("0b1e5", 3);
    checkErrorAt("0o8", 2);
}

// --- 嵌套深度 ---

TEST(expression, deepNestingIsRejectedCleanly) {
    // 300 层括号：超过上限，必须是**干净的错误**而不是崩溃。
    std::string text(300, '(');
    text += "1";
    text += std::string(300, ')');
    int position = -1;
    std::string message;
    CHECK(!tryEval(text, position, message));
    // 位置要**钉死**：第 256 个 '(' 之后那个字符（实测 256）。
    //
    // 只断言 position >= 0 的话，将来深度的计数方式一变（少算一层、
    // 多算一层），这个用例照样通过 —— 而错误位置是前端用来高亮
    // 出错字符的，漂了就用不了了。
    CHECK_EQ(position, 256);

    // 正好在上限之内（255 层）必须能正常算出。
    std::string atLimit(255, '(');
    atLimit += "1";
    atLimit += std::string(255, ')');
    CHECK_EQ(eval(atLimit).toString(), std::string("1"));

    // 100 层（低于上限）必须能正常算出。
    std::string shallow(100, '(');
    shallow += "1+2";
    shallow += std::string(100, ')');
    CHECK_EQ(eval(shallow).toString(), std::string("3"));
}

TEST(expression, deepFunctionNestingIsRejectedCleanly) {
    std::string text;
    for (int i = 0; i < 300; ++i) text += "sqrt(";
    text += "4";
    for (int i = 0; i < 300; ++i) text += ")";
    int position = -1;
    std::string message;
    CHECK(!tryEval(text, position, message));
    // 位置钉死：第 257 次 "sqrt(" 的那个 's'（实测 1280）。
    //
    // 每次函数调用消耗一层深度（与括号一样），所以上限 256 层对应第
    // 257 次调用被拒；位置指向**被拒的那次调用的第一个字符**。
    // 1280 = 256 × 5（"sqrt(" 五个字符）。
    CHECK_EQ(position, 1280);

    // 正好在上限之内（255 次调用 = 255 层）必须能正常算出。
    std::string atLimit;
    for (int i = 0; i < 255; ++i) atLimit += "sqrt(";
    atLimit += "1";
    for (int i = 0; i < 255; ++i) atLimit += ")";
    CHECK_EQ(eval(atLimit).toString(), std::string("1"));
}

// --- 精度 ---

TEST(expression, precisionIsSignificantDigits) {
    // 精度是**有效数字位**，不是小数位数。
    // 1/3 @ 20 位有效数字 → 20 位小数（因为结果在 0 与 1 之间）。
    CHECK_EQ(eval("1/3").toString(), std::string("0.33333333333333333333"));
    // 12345/1 @ 3 位有效数字 → 12300（3 位有效数字）。
    CHECK_EQ(eval("12345/1", 3, RoundingMode::HalfEven).toString(),
             std::string("12300"));
    // 1000/3 @ 3 位有效数字 → 333（3 位有效数字，不是 3 位小数）。
    CHECK_EQ(eval("1000/3", 3, RoundingMode::HalfEven).toString(), std::string("333"));
    // 纯字面量不受精度影响（没有运算就没有舍入）。
    CHECK_EQ(eval("12345", 3, RoundingMode::HalfEven).toString(),
             std::string("12345"));
    // 12345 + 0 仍是精确的加法。
    CHECK_EQ(eval("12345+0", 3, RoundingMode::HalfEven).toString(),
             std::string("12345"));
}

TEST(expression, roundingModesAffectDivision) {
    // 1/2 是**精确**的（二进制可表示），不需要舍入 —— 所以 p=1 下
    // 仍然是 0.5。舍入只在商**不精确**时才起作用。
    CHECK_EQ(eval("1/2", 1, RoundingMode::HalfEven).toString(), std::string("0.5"));
    CHECK_EQ(eval("-1/2", 1, RoundingMode::HalfEven).toString(), std::string("-0.5"));
    // 5/2 = 2.5，p=1 下正好是一半：HalfEven → 2（偶数），HalfUp → 3。
    CHECK_EQ(eval("5/2", 1, RoundingMode::HalfEven).toString(), std::string("2"));
    CHECK_EQ(eval("5/2", 1, RoundingMode::HalfUp).toString(), std::string("3"));
    CHECK_EQ(eval("-5/2", 1, RoundingMode::HalfEven).toString(), std::string("-2"));
    CHECK_EQ(eval("-5/2", 1, RoundingMode::HalfUp).toString(), std::string("-3"));
    // 15/10 = 1.5，p=1 下正好是一半：HalfEven → 2（偶数）。
    CHECK_EQ(eval("15/10", 1, RoundingMode::HalfEven).toString(), std::string("2"));
    CHECK_EQ(eval("15/10", 1, RoundingMode::HalfUp).toString(), std::string("2"));
    // Down 向零截断，Up 远离零。
    CHECK_EQ(eval("15/10", 1, RoundingMode::Down).toString(), std::string("1"));
    CHECK_EQ(eval("15/10", 1, RoundingMode::Up).toString(), std::string("2"));
    CHECK_EQ(eval("-15/10", 1, RoundingMode::Down).toString(), std::string("-1"));
    CHECK_EQ(eval("-15/10", 1, RoundingMode::Up).toString(), std::string("-2"));
    // Ceil 向 +∞，Floor 向 -∞。
    CHECK_EQ(eval("15/10", 1, RoundingMode::Ceil).toString(), std::string("2"));
    CHECK_EQ(eval("15/10", 1, RoundingMode::Floor).toString(), std::string("1"));
    CHECK_EQ(eval("-15/10", 1, RoundingMode::Ceil).toString(), std::string("-1"));
    CHECK_EQ(eval("-15/10", 1, RoundingMode::Floor).toString(), std::string("-2"));
    // 1/3 在 p=3 下是 0.333…，六种模式给出不同的结果。
    CHECK_EQ(eval("1/3", 3, RoundingMode::HalfEven).toString(), std::string("0.333"));
    CHECK_EQ(eval("1/3", 3, RoundingMode::HalfUp).toString(), std::string("0.333"));
    CHECK_EQ(eval("1/3", 3, RoundingMode::Down).toString(), std::string("0.333"));
    CHECK_EQ(eval("1/3", 3, RoundingMode::Up).toString(), std::string("0.334"));
    CHECK_EQ(eval("1/3", 3, RoundingMode::Ceil).toString(), std::string("0.334"));
    CHECK_EQ(eval("1/3", 3, RoundingMode::Floor).toString(), std::string("0.333"));
    // 2/3 在 p=3 下是 0.666…，向下取整时 0.666，向上取整时 0.667。
    CHECK_EQ(eval("2/3", 3, RoundingMode::Down).toString(), std::string("0.666"));
    CHECK_EQ(eval("2/3", 3, RoundingMode::Up).toString(), std::string("0.667"));
    CHECK_EQ(eval("-2/3", 3, RoundingMode::Down).toString(), std::string("-0.666"));
    CHECK_EQ(eval("-2/3", 3, RoundingMode::Up).toString(), std::string("-0.667"));
    CHECK_EQ(eval("-2/3", 3, RoundingMode::Ceil).toString(), std::string("-0.666"));
    CHECK_EQ(eval("-2/3", 3, RoundingMode::Floor).toString(), std::string("-0.667"));
}

TEST(expression, transcendentalPrecisionCeiling) {
    // 默认精度 30 必须能算 sqrt(2)。
    CHECK_EQ(eval("sqrt(2)", 30, RoundingMode::HalfEven).toString(),
             std::string("1.41421356237309504880168872421"));

    // 精度超过 kMaxTranscendentalPrecision（1000）时抛 ExpressionError，
    // 而不是静默降精度，也不是漏出 std::invalid_argument。
    int position = -1;
    std::string message;
    CHECK(!tryEvalAt("sqrt(2)", 1001, RoundingMode::HalfEven, position, message));
    CHECK(position >= 0);
    CHECK(!tryEvalAt("exp(1)", 1001, RoundingMode::HalfEven, position, message));
    CHECK(!tryEvalAt("ln(2)", 1001, RoundingMode::HalfEven, position, message));
    CHECK(!tryEvalAt("log10(2)", 1001, RoundingMode::HalfEven, position, message));
    // pow 的**非整数指数**分支同样受限。
    CHECK(!tryEvalAt("pow(2,0.5)", 1001, RoundingMode::HalfEven, position, message));

    // 上限之内（正好 1000）必须能算。
    CHECK(tryEvalAt("sqrt(2)", 1000, RoundingMode::HalfEven, position, message));

    // 只做加减乘除的表达式不受这个上限约束（kMaxPrecision = 100000）。
    // 1/3 @ 5000 位有效数字 → "0." + 5000 个 3。
    const std::string third = eval("1/3", 5000, RoundingMode::HalfEven).toString();
    CHECK_EQ(third.size(), static_cast<std::size_t>(5002));
    CHECK_EQ(third.substr(0, 4), std::string("0.33"));
    CHECK_EQ(third.substr(third.size() - 3), std::string("333"));

    // pow 的**整数指数**分支是精确运算，同样不受这个上限约束。
    CHECK_EQ(eval("pow(2,10)", 2000, RoundingMode::HalfEven).toString(),
             std::string("1024"));
}

TEST(expression, precisionOutOfRange) {
    // 精度越界是**上下文错误**，与表达式内容无关 —— 位置取 0。
    // 注意：即使表达式里没有除法，精度越界也要报错（精度是全局契约，
    // 静默忽略会让用户以为拿到了 0 位或 100001 位的答案）。
    int position = -1;
    std::string message;

    const bool threwNonPositive = [&]() {
        try {
            hpc::evaluate("1+1", Context{0, RoundingMode::HalfEven});
        } catch (const ExpressionError& e) {
            position = e.position();
            message = e.what();
            return true;
        }
        return false;
    }();
    CHECK(threwNonPositive);
    CHECK_EQ(position, 0);
    CHECK(!message.empty());

    const bool threwNegative = [&]() {
        try {
            hpc::evaluate("1+1", Context{-5, RoundingMode::HalfEven});
        } catch (const ExpressionError&) {
            return true;
        }
        return false;
    }();
    CHECK(threwNegative);

    const bool threwTooBig = [&]() {
        try {
            hpc::evaluate("1+1", Context{hpc::kMaxPrecision + 1, RoundingMode::HalfEven});
        } catch (const ExpressionError& e) {
            position = e.position();
            message = e.what();
            return true;
        }
        return false;
    }();
    CHECK(threwTooBig);
    CHECK_EQ(position, 0);

    // 正好在上限之内是合法的。
    CHECK(tryEvalAt("1+1", hpc::kMaxPrecision, RoundingMode::HalfEven, position, message));
}

// --- 结构安全：空集合 / 单元素 / 边界 ---

TEST(expression, structuralSafety) {
    // 单元素与空输入都不该崩溃。
    CHECK_EQ(eval("0").toString(), std::string("0"));
    CHECK_EQ(eval("1").toString(), std::string("1"));
    CHECK(evalOrError("").rfind("!", 0) == 0);

    // 只有一个参数、参数为空的函数。
    CHECK(evalOrError("abs()").rfind("!", 0) == 0);

    // 除数为零的两个变体。
    CHECK(evalOrError("1/0.0").rfind("!", 0) == 0);
    CHECK(evalOrError("1/(0*5)").rfind("!", 0) == 0);
}
