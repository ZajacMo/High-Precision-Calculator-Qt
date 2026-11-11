#include "core/Expression.h"

#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/Format.h"

namespace hpc {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isAlpha(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool isAlnum(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

char toLower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// 该字符在 base 进制下是否是一个合法数位。
bool isBaseDigit(char c, int base) {
    int digit;
    if (isDigit(c)) {
        digit = c - '0';
    } else if (c >= 'a' && c <= 'z') {
        digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
        digit = c - 'A' + 10;
    } else {
        return false;
    }
    return digit < base;
}

// 一个函数实参：值 + 它在原串中的起始位置。
//
// 位置用于报「参数超出定义域」这类错误：出错的确实是**这个实参**，
// 所以位置指向实参的起点比指向函数名更有用。
struct Argument {
    BigDecimal value;
    int position;
};

// 递归下降解析器。
class Parser {
public:
    Parser(std::string_view text, const Context& context)
        : text_(text), context_(context) {}

    BigDecimal parse() {
        skipSpaces();
        if (position_ >= text_.size()) {
            fail("表达式为空");
        }
        BigDecimal value = parseExpression();
        skipSpaces();
        if (position_ != text_.size()) {
            fail("多余的字符");
        }
        return value;
    }

private:
    // 报错的**位置**指向出错的字符本身。
    [[noreturn]] void fail(const std::string& message) const {
        throw ExpressionError(message, static_cast<int>(position_));
    }

    [[noreturn]] void failAt(const std::string& message, std::size_t position) const {
        throw ExpressionError(message, static_cast<int>(position));
    }

    void skipSpaces() {
        while (position_ < text_.size() && isSpace(text_[position_])) {
            ++position_;
        }
    }

    bool consume(char c) {
        skipSpaces();
        if (position_ < text_.size() && text_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!consume(c)) {
            fail(std::string("期望 '") + c + "'");
        }
    }

    // 嵌套深度的守卫：构造时 +1，析构时 -1。
    //
    // 异常路径上也要 -1，否则中途抛错后深度计数会失真。用 RAII 而不是
    // 「正常返回时手写 -1」正是为此 —— 异常展开时析构函数照样会跑。
    class DepthGuard {
    public:
        explicit DepthGuard(Parser& parser) : parser_(parser) { ++parser_.depth_; }
        ~DepthGuard() { --parser_.depth_; }
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;

    private:
        Parser& parser_;
    };

    // expression := term (('+' | '-') term)*
    BigDecimal parseExpression() {
        BigDecimal value = parseTerm();
        while (true) {
            skipSpaces();
            if (consume('+')) {
                value = value + parseTerm();
            } else if (consume('-')) {
                value = value - parseTerm();
            } else {
                return value;
            }
        }
    }

    // term := unary (('*' | '/' | '%') unary)*
    BigDecimal parseTerm() {
        BigDecimal value = parseUnary();
        while (true) {
            skipSpaces();
            if (consume('*')) {
                value = value * parseUnary();
            } else if (consume('/')) {
                skipSpaces();
                const std::size_t divisorStart = position_;
                const BigDecimal divisor = parseUnary();
                if (divisor.isZero()) {
                    failAt("除数为零", divisorStart);
                }
                value = BigDecimal::divide(value, divisor, context_);
            } else if (consume('%')) {
                skipSpaces();
                const std::size_t divisorStart = position_;
                const BigDecimal divisor = parseUnary();
                if (divisor.isZero()) {
                    failAt("取余的除数为零", divisorStart);
                }
                // 取余用的是内核的**整数余数**语义（符号跟随被除数）。
                // 左操作数带小数时，内核把两个数对齐到共同的小数位再做
                // 整数长除法 —— 等价于 C 的 fmod(5.5, 2) = 1.5，
                // 而不是「先取整再取余」（那是 5.5 → 5，5 % 2 = 1）。
                value = BigDecimal::modulo(value, divisor);
            } else {
                return value;
            }
        }
    }

    // unary := ('+' | '-')* primary
    //
    // 写成**迭代**而不是递归：一串一元符号（"----5"）用递归实现会
    // 无限深地调用自己，一个几百 KB 的输入就能把栈跑穿 —— 而栈溢出是
    // **崩溃**，前端连错误提示都来不及显示。
    //
    // 一元符号的链一定是**平的**：语法里 unary 的操作数要么是另一个
    // unary（于是链继续延伸），要么是 primary（链到此为止）。所以
    // 「若干个符号 + 一个 primary」这个形式覆盖了全部情况 ——
    // "-(-5)" 里的内层 unary 是 primary '(' 的内部，由 parsePrimary
    // 负责，不在这条链上。
    BigDecimal parseUnary() {
        bool negate = false;
        while (true) {
            skipSpaces();
            if (position_ < text_.size() && text_[position_] == '+') {
                ++position_;
                continue;
            }
            if (position_ < text_.size() && text_[position_] == '-') {
                ++position_;
                negate = !negate;
                continue;
            }
            break;
        }
        BigDecimal value = parsePrimary();
        return negate ? BigDecimal(0) - value : value;
    }

    // primary := number | '(' expression ')' | name '(' args ')'
    BigDecimal parsePrimary() {
        skipSpaces();
        if (position_ >= text_.size()) {
            fail("表达式意外结束");
        }

        // 嵌套深度上限：超限时报错，而不是让递归把栈跑穿。
        if (depth_ >= kMaxExpressionNestingDepth) {
            fail("表达式的嵌套层数超过上限 " +
                 std::to_string(kMaxExpressionNestingDepth));
        }

        const char c = text_[position_];
        if (c == '(') {
            ++position_;
            DepthGuard guard(*this);
            BigDecimal value = parseExpression();
            expect(')');
            return value;
        }
        if (isDigit(c) || c == '.') {
            return parseNumber();
        }
        if (isAlpha(c) || c == '_') {
            return parseIdentifier();
        }
        fail("意外的字符");
    }

    // 数字字面量。
    //
    // 两类：
    //   - 0x / 0b / 0o 前缀的**整数**（该进制下的小数无法精确表示，
    //     所以进制字面量只支持整数 —— 与 fromBaseString 的契约一致）；
    //   - 十进制：[.][数字][.数字][(e|E)[+|-]数字]。
    //
    // 前缀路径与十进制路径**互斥**：先看前缀（0x/0b/0o），没有前缀才走
    // 十进制。于是 "0x1e5" 是十六进制的 1e5（485），而 "1e5" 是科学
    // 计数法的 100000 —— 两条路不会互相误认。
    BigDecimal parseNumber() {
        const std::size_t start = position_;
        if (text_[position_] == '0' && position_ + 1 < text_.size()) {
            const char marker = toLower(text_[position_ + 1]);
            int base = 0;
            if (marker == 'x') base = 16;
            else if (marker == 'b') base = 2;
            else if (marker == 'o') base = 8;
            if (base != 0) {
                return parseBaseInteger(base);
            }
        }
        return parseDecimalLiteral(start);
    }

    // 0x/0b/0o 前缀的整数。
    BigDecimal parseBaseInteger(int base) {
        position_ += 2;                       // 跳过 "0x" / "0b" / "0o"
        const std::size_t digitsStart = position_;
        while (position_ < text_.size() && isBaseDigit(text_[position_], base)) {
            ++position_;
        }
        const std::size_t digitsEnd = position_;
        if (digitsEnd == digitsStart) {
            // 一个数位都没有：要么后面跟的是非法字符，要么根本没有了。
            if (position_ < text_.size() && isAlnum(text_[position_])) {
                fail("非法的进制数字");
            }
            fail("进制前缀后缺少数字");
        }
        // 数位之后紧跟字母数字 → 那个字符就是出错的字符。
        if (position_ < text_.size() && isAlnum(text_[position_])) {
            fail("非法的进制数字");
        }
        try {
            return fromBaseString(
                std::string(text_.substr(digitsStart, digitsEnd - digitsStart)), base);
        } catch (const std::exception&) {
            fail("非法的进制数字");
        }
    }

    // 十进制字面量（含科学计数法）。
    BigDecimal parseDecimalLiteral(std::size_t start) {
        bool seenDot = false;
        bool seenDigit = false;

        if (text_[position_] == '.') {
            seenDot = true;
            ++position_;
        }
        while (position_ < text_.size() && isDigit(text_[position_])) {
            seenDigit = true;
            ++position_;
        }
        if (position_ < text_.size() && text_[position_] == '.') {
            if (seenDot) {
                fail("非法的数字：小数点重复");
            }
            seenDot = true;
            ++position_;
            while (position_ < text_.size() && isDigit(text_[position_])) {
                seenDigit = true;
                ++position_;
            }
        }
        // 小数部分之后**又**出现小数点：那个点就是出错的字符。
        //
        // 不在这里报的话，字面量会停在第一个点（"1.2"），剩下的 ".3" 由
        // parse() 当成「多余的字符」报出来 —— 位置对，但信息误导
        // （用户看到的是「多余」，实际是「多点」）。
        if (position_ < text_.size() && text_[position_] == '.') {
            fail("非法的数字：小数点重复");
        }
        if (!seenDigit) {
            failAt("非法的数字：没有数字", start);
        }

        // 科学计数法：'e'/'E' 后面必须跟（可选符号）加数字，否则它就是
        // 一个非法字符 —— 把它算进字面量让 fromString 报错，位置指向
        // 那个 'e'。
        if (position_ < text_.size() &&
            (text_[position_] == 'e' || text_[position_] == 'E')) {
            const std::size_t mark = position_;
            std::size_t cursor = mark + 1;
            if (cursor < text_.size() && (text_[cursor] == '+' || text_[cursor] == '-')) {
                ++cursor;
            }
            if (cursor < text_.size() && isDigit(text_[cursor])) {
                while (cursor < text_.size() && isDigit(text_[cursor])) {
                    ++cursor;
                }
                position_ = cursor;
            } else {
                position_ = mark + 1;
                try {
                    return BigDecimal::fromString(text_.substr(start, position_ - start));
                } catch (const std::exception&) {
                    failAt("非法的数字：指数部分非法", mark);
                }
            }
        }

        try {
            return BigDecimal::fromString(text_.substr(start, position_ - start));
        } catch (const std::exception&) {
            fail("非法的数字");
        }
    }

    // 标识符：只能是函数名（后面必须紧跟 '('）。
    BigDecimal parseIdentifier() {
        const std::size_t start = position_;
        while (position_ < text_.size() &&
               (isAlnum(text_[position_]) || text_[position_] == '_')) {
            ++position_;
        }
        const std::string name(text_.substr(start, position_ - start));

        skipSpaces();
        if (position_ < text_.size() && text_[position_] == '(') {
            ++position_;
            DepthGuard guard(*this);
            std::vector<Argument> args;
            skipSpaces();
            if (!(position_ < text_.size() && text_[position_] == ')')) {
                args.push_back(parseArgument());
                while (consume(',')) {
                    args.push_back(parseArgument());
                }
            }
            expect(')');
            return callFunction(name, args, start);
        }
        failAt("未知的标识符 '" + name + "'", start);
    }

    Argument parseArgument() {
        skipSpaces();
        const std::size_t start = position_;
        BigDecimal value = parseExpression();
        return Argument{value, static_cast<int>(start)};
    }

    // 把内核抛出的异常统一转换成 ExpressionError。
    //
    // 前端因此只需要捕获**一种**异常类型 —— 定义域错误（sqrt(-1)、
    // ln(0)、pow(0,0)）、溢出（exp 参数过大）、精度越界在内核里是三种
    // 不同的异常类型，让它们原样漏到前端会让每个前端都写三段 catch。
    BigDecimal callFunction(const std::string& name, const std::vector<Argument>& args,
                            std::size_t nameStart) {
        auto need = [&](std::size_t count) {
            if (args.size() != count) {
                failAt("函数 '" + name + "' 需要 " + std::to_string(count) + " 个参数",
                       nameStart);
            }
        };

        if (name == "pow") {
            need(2);
            // pow 的**整数指数**分支是精确运算，不受超越函数的精度上限
            // 约束；只有非整数指数才受。判定条件与内核一致（scale == 0）。
            if (!args[1].value.isInteger() &&
                context_.precision > kMaxTranscendentalPrecision) {
                failAt("精度 " + std::to_string(context_.precision) +
                           " 超出超越函数的上限 " +
                           std::to_string(kMaxTranscendentalPrecision) +
                           "（函数 'pow' 的非整数指数分支）",
                       nameStart);
            }
            try {
                return BigDecimal::pow(args[0].value, args[1].value, context_);
            } catch (const std::domain_error& e) {
                throw ExpressionError(
                    std::string("函数 'pow' 的参数超出定义域：") + e.what(),
                    static_cast<int>(nameStart));
            } catch (const std::overflow_error& e) {
                throw ExpressionError(
                    std::string("函数 'pow' 的结果超出可计算范围：") + e.what(),
                    static_cast<int>(nameStart));
            } catch (const std::invalid_argument& e) {
                throw ExpressionError(
                    std::string("函数 'pow' 无法计算：") + e.what(),
                    static_cast<int>(nameStart));
            }
        }

        if (name == "sqrt" || name == "exp" || name == "ln" || name == "log10") {
            need(1);
            if (context_.precision > kMaxTranscendentalPrecision) {
                failAt("精度 " + std::to_string(context_.precision) +
                           " 超出超越函数的上限 " +
                           std::to_string(kMaxTranscendentalPrecision) + "（函数 '" +
                           name + "'）",
                       nameStart);
            }
            try {
                if (name == "sqrt") return BigDecimal::sqrt(args[0].value, context_);
                if (name == "exp") return BigDecimal::exp(args[0].value, context_);
                if (name == "ln") return BigDecimal::ln(args[0].value, context_);
                return BigDecimal::log10(args[0].value, context_);
            } catch (const std::domain_error& e) {
                throw ExpressionError(
                    std::string("函数 '") + name + "' 的参数超出定义域：" + e.what(),
                    args[0].position);
            } catch (const std::overflow_error& e) {
                throw ExpressionError(
                    std::string("函数 '") + name + "' 的结果超出可计算范围：" + e.what(),
                    args[0].position);
            } catch (const std::invalid_argument& e) {
                throw ExpressionError(
                    std::string("函数 '") + name + "' 无法计算：" + e.what(),
                    args[0].position);
            }
        }

        if (name == "abs") {
            need(1);
            return BigDecimal::abs(args[0].value);
        }

        failAt("未知的函数 '" + name + "'", nameStart);
    }

    std::string_view text_;
    const Context& context_;
    std::size_t position_ = 0;
    int depth_ = 0;
};

}  // namespace

BigDecimal evaluate(std::string_view expression, const Context& context) {
    // 精度本身的合法性在**进解析器之前**检查：精度越界是「上下文错误」，
    // 不是「某个字符错了」，所以位置取 0（表达式的起点）。
    //
    // 超越函数的上限**不能**在这里检查 —— 那要等到真的用上超越函数才知道，
    // 而只做加减乘除的表达式在 kMaxPrecision（100000）位以内都是合法的。
    if (context.precision <= 0) {
        throw ExpressionError("精度必须是正整数", 0);
    }
    if (context.precision > kMaxPrecision) {
        throw ExpressionError("精度 " + std::to_string(context.precision) +
                                  " 超出上限 " + std::to_string(kMaxPrecision),
                              0);
    }

    try {
        Parser parser(expression, context);
        return parser.parse();
    } catch (const ExpressionError&) {
        throw;   // 已经是统一后的类型，原样抛出
    } catch (const std::exception& e) {
        // 兜底：内核抛出的其它异常（理论上不该漏到这里）也统一成
        // ExpressionError，保证前端只需要 catch 一种类型。
        throw ExpressionError(std::string("求值失败：") + e.what(), 0);
    }
}

}  // namespace hpc
