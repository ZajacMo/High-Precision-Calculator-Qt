// 命令行前端：与 Web 前端、Qt 界面共用 src/core 的同一套内核。
//
// 三种模式：
//   hpc_cli                    交互式（REPL）
//   hpc_cli -e "<表达式>"      单次求值后退出
//   hpc_cli --oj               批量模式（OJ 协议，与 legacy 对齐）
//
// 本文件只做**参数处理、I/O 与错误呈现** —— 所有算术都在 src/core 里，
// 前端不改写内核的语义。
#include <iostream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
// windows.h 会定义 min/max 宏，与 <algorithm> 的 std::min/std::max 冲突。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/BigDecimal.h"
#include "core/Expression.h"
#include "core/Format.h"
#include "core/Rounding.h"

namespace {

using hpc::BigDecimal;
using hpc::Context;
using hpc::RoundingMode;

// --- 协议常量 ---
//
// OJ 模式要与 legacy/oj_console_edition 对齐，这两个常量就是那套协议
// 里的两个「魔数」：
//
//   kOjFractionDigits = 10
//       原实现里是 Number::FracBit = 10，注释写的是「小数位数」——
//       **小数位**，不是「有效数字位」。两者在 |商| >= 1（整数部分占位）
//       或 |商| < 0.1（整数部分为空）时结果不同，所以必须区分。
//
//   kOjMinimumDivisor = 1e-6
//       原实现的 checkBit()：「检查绝对值是否小于 0.000001」。
//
//   kOjMaxGroups = 1000000
//       一次运行允许声明的最大组数。协议的首行是一个**声明**的组数，
//       legacy 照着它一行一组地输出 —— 一个坏的输入可以声明 2e9 组。
//
//       legacy 在这一档是**资源耗尽**，不是行为兼容：C++11 规定
//       `std::cin >> int` 在数值超出 int 范围时把目标置为 INT_MAX，于是
//       legacy 声明 2e9 组时实际输出 **21 亿行** ERROR（实测 >25 s 超时）。
//       实测 1e8 组 → 22 s / 700 MB。
//
//       取 1e6：最大的公开 OJ 单文件用例在**几十万条**量级，1e6 有 10 倍
//       以上的余量；而 1e6 组的最坏输出（全是 ERROR 行）也就 6 MB、实测
//       0.10 s。超过上限就报错并停止，而不是继续消耗资源。
constexpr int kOjFractionDigits = 10;
const char* const kOjMinimumDivisor = "0.000001";
constexpr long long kOjMaxGroups = 1000000;

// Windows 控制台默认是 GBK（代码页 936），而本项目的提示信息是 UTF-8。
// 不切换代码页的话，中文提示在控制台上是乱码。
//
// 只影响**控制台**：输出被重定向到文件或管道时写的是原始字节，不受代码页
// 影响（所以脚本化调用拿到的仍是 UTF-8）。
void useUtf8Console() {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

// 解析十进制整数。
//
// **不用 std::stoi**：它对非数字输入抛 std::invalid_argument、对溢出抛
// std::out_of_range，两者都是**未捕获的异常** —— 命令行参数是用户输入，
// 不能让它把进程带崩。
bool parseInteger(const std::string& text, long long& out) {
    if (text.empty()) return false;
    long long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        if (value > (1LL << 40)) return false;   // 早退，避免溢出
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

// 空白行判定（只含空格与制表符）。
bool isBlank(const std::string& text) {
    for (const char c : text) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\v' && c != '\f') return false;
    }
    return true;
}

// 去掉千位分隔符。
//
// legacy 的 Number::get() 会忽略整数部分里的逗号（输入 "1,234" 当成 1234），
// 而它的输出又带逗号 —— 所以输入侧必须同样接受逗号，否则往返不闭合。
std::string stripThousandsSeparators(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (const char c : text) {
        if (c != ',') result.push_back(c);
    }
    return result;
}

// 给十进制串的整数部分每三位插一个逗号。
//
// 规则与 legacy 的 Number::show() 一致：逗号插在「从右往左数第 3、6、9…
// 位之前」，小数部分不带逗号。
//
// 例："1234567.89" → "1,234,567.89"。
std::string withThousandSeparators(const std::string& text) {
    std::size_t begin = 0;
    if (begin < text.size() && text[begin] == '-') ++begin;
    const std::size_t dot = text.find('.', begin);
    const std::size_t integerEnd = (dot == std::string::npos) ? text.size() : dot;
    const std::size_t integerDigits = integerEnd - begin;

    std::string result;
    result.reserve(text.size() + integerDigits / 3);
    result.append(text, 0, begin);
    for (std::size_t i = 0; i < integerDigits; ++i) {
        if (i > 0 && (integerDigits - i) % 3 == 0) result.push_back(',');
        result.push_back(text[begin + i]);
    }
    if (dot != std::string::npos) result.append(text, dot, std::string::npos);
    return result;
}

// |value| 的十进制指数 e：10^e <= |value| < 10^(e+1)。零值按 0 处理。
//
// 从 toString() 的十进制串反推：先去掉小数点，再找第一个非零数字。
//
// 为什么需要它：OJ 的除法要按「小数位」舍入，所以必须知道商的**整数位**
// 有多少 —— 而整数位是由两边的十进制指数之差决定的（商的量级约等于
// |lhs| / |rhs|）。只用「整数部分的位数」不够：0.05 的整数部分只有一位，
// 但它的指数是 -2（10^-2 <= 0.05 < 10^-1）。
int decimalExponentOf(const BigDecimal& value) {
    const std::string text = BigDecimal::abs(value).toString();
    const std::size_t dot = text.find('.');
    const std::size_t integerDigits = (dot == std::string::npos) ? text.size() : dot;
    const std::string digits =
        text.substr(0, integerDigits) +
        (dot == std::string::npos ? std::string() : text.substr(dot + 1));
    for (std::size_t k = 0; k < digits.size(); ++k) {
        if (digits[k] != '0') {
            return static_cast<int>(integerDigits) - static_cast<int>(k) - 1;
        }
    }
    return 0;   // 零
}

// OJ 模式的除法：四舍五入到 kOjFractionDigits 位小数，远离零。
//
// **不能**直接用 BigDecimal::divide：它的 context.precision 是**有效数字位**，
// 而原协议要的是**小数位**（Number::FracBit = 10）。两者在 |商| >= 1 或
// |商| < 0.1 时结果不同。
//
// 也**不能**「先按有效数字舍入一次、再按小数位舍入一次」：那是双重舍入，
// 会把 0.4999…9 的尾巴先舍成 0.5，再当成「正好一半」处理 —— 结果错在
// 最后一位。
//
// 这里的做法：把被除数精确地放大 10^10 倍，再用**精确的长除法余数**
// 判定舍入方向。余数是精确的，不存在双重舍入。
//
// **上界**：结果的位数被内核的 kMaxPrecision（100000 位有效数字）限制。
// 于是「被除数 / 除数」的量级超过约 10^99987 时，ojDivide 抛
// std::overflow_error，OJ 模式把它转成 ERROR。
// legacy 在这一档**不会**报错（它用自己的一套任意精度的实现，能给出
// 精确的值），所以这是一个**刻意保留的差异**：OJ 的除法路径有上界，
// 超上界时报告 ERROR 而不是继续算下去。
// 触发上界需要操作数的十进制指数达到 ~1e5 —— 任何现实的 OJ 测试数据
// 都远在这个量级之下。
BigDecimal ojDivide(const BigDecimal& lhs, const BigDecimal& rhs) {
    const BigDecimal scale = BigDecimal::fromString("1e10");
    const BigDecimal scaled = BigDecimal::abs(lhs) * scale;   // 精确乘法
    const BigDecimal divisor = BigDecimal::abs(rhs);

    // 商的量级：scaled / divisor ≈ |lhs| / |rhs| × 10^10。
    //
    // log10(商) < 11 + e_lhs - e_rhs，所以商的整数位最多 11 + e_lhs - e_rhs 位。
    // 精度取「整数位 + 2 位保护位」，于是近似值与精确商之差 < 0.01，近似值的
    // 整数部分与精确商的整数部分至多差 1 —— 下面的余数修正会把它纠正回来。
    const int required = 13 + decimalExponentOf(lhs) - decimalExponentOf(rhs);
    if (required > hpc::kMaxPrecision) {
        throw std::overflow_error("OJ 除法的结果位数超出可计算范围");
    }
    const Context context{required < 2 ? 2 : required, RoundingMode::HalfEven};
    const BigDecimal approximate = BigDecimal::divide(scaled, divisor, context);

    BigDecimal truncated = approximate.truncateToInteger();

    // 精确余数：scaled - truncated × divisor。用它把 truncated 修正成
    // 精确商的整数部分（余数落在 [0, divisor) 里）。
    BigDecimal remainder = scaled - truncated * divisor;
    if (remainder.sign() < 0) {
        truncated = truncated - BigDecimal(1);
        remainder = remainder + divisor;
    } else if (remainder.compare(divisor) >= 0) {
        truncated = truncated + BigDecimal(1);
        remainder = remainder - divisor;
    }

    // 远离零的四舍五入：余数 >= divisor/2 时进位。
    if (remainder.compare(divisor * BigDecimal("0.5")) >= 0) {
        truncated = truncated + BigDecimal(1);
    }

    // 结果 = truncated × 10^-10。
    BigDecimal result = BigDecimal::fromString(truncated.toString() + "e-10");
    if (lhs.sign() * rhs.sign() < 0) {
        result = BigDecimal(0) - result;   // 核心没有一元负号
    }
    return result;
}

// 按目标进制输出结果。
void printResult(const BigDecimal& value, int base) {
    if (base == 10) {
        std::cout << value.toString() << "\n";
    } else {
        // toBaseString 只对**整数部分**精确无损；小数部分按目标进制逐位
        // 提取，该进制下无法有限表示时末尾带 "..."。那个标记是如实的：它表示
        // 「展开被截断了」，不会把截断过的值说成精确值。
        std::cout << hpc::toBaseString(value, base) << "\n";
    }
}

// 求值并输出。出错时把信息写到 stderr（stdout 只放数据，便于脚本处理）。
bool evaluateAndPrint(const std::string& expression, const Context& context, int base) {
    try {
        const BigDecimal result = hpc::evaluate(expression, context);
        printResult(result, base);
        return true;
    } catch (const hpc::ExpressionError& error) {
        std::cerr << "错误：" << error.what() << "（位置 " << error.position() << "）\n";
    } catch (const std::exception& error) {
        std::cerr << "错误：" << error.what() << "\n";
    }
    return false;
}

void printUsage() {
    std::cout <<
        R"(高精度计算器 · 命令行前端

用法:
  hpc_cli                        进入交互模式（Ctrl+D 退出）
  hpc_cli -e "<表达式>"          求值单个表达式后退出
  hpc_cli --oj                   批量模式（OJ 协议）

表达式语法:
  数字        123  0.5  1e20  0xFF  0b1010  0o777
  运算符      + - * / %   以及括号与一元正负号
  函数        sqrt(x) abs(x) pow(x,y) exp(x) ln(x) log10(x)

选项:
  -e, --expression <表达式>   求值单个表达式
  -p, --precision <n>         有效数字位数（默认 30）
  -b, --base <n>              输出进制，2..36（默认 10）
  -h, --help                  显示本帮助

精度上限（内核的公开契约，见 src/core/BigDecimal.h）:
  除法 / 舍入   kMaxPrecision                = 100000
  超越函数      kMaxTranscendentalPrecision = 1000
  （sqrt / exp / ln / log10，以及 pow 的**非整数指数**分支）

-p 超过 100000 会被**拒绝**（而不是静默夹到上限）：用户要的是他请求的
那个精度，悄悄换个精度等于给出一个他没要求的答案。
表达式里用到超越函数时精度还不能超过 1000 —— 内核会报「精度 N 超出
超越函数的上限 1000」（工作精度是平方级的，p=1600 时 pow(2, 0.5) 要跑
151 s）。pow 的**整数指数**分支不受这个限制（那是精确的快速幂）。
纯加减乘除的表达式在 100000 位以内都合法 —— 超越函数的上限**只在真的
用到超越函数时**才生效。

输出进制（-b）:
  整数部分精确无损；小数部分按目标进制逐位提取，该进制下无法有限表示
  时末尾用 "..." 显式标记截断 —— 不会把截断过的值说成精确值。
  例：-b 2 求值 1/3 得到 0.0101...0101...（末尾带 "..."）。

OJ 批量协议（与 legacy/oj_console_edition 对齐）:
  首行是测试组数，随后每行是 "运算符 数字1 数字2"。
  运算符非法或除数过小（绝对值 < 0.000001）时输出 ERROR。

  与原实现对齐的几条细节:
    - 结果的整数部分每三位插一个逗号（1,234,567），小数部分不带逗号。
    - 输入的操作数里可以有千位逗号（1,234 当成 1234）。
    - 除法结果四舍五入到 **10 位小数**（远离零）—— 这是原实现的
      Number::FracBit = 10（「小数位数」）语义，不是「有效数字位」。

  与原实现**不同**的两处（刻意，都是原实现的缺陷）:
    - 运算符非法时本实现仍把该组的两个操作数读掉。原实现不读，输入流
      因此错位、后面每一组都读错（实测原实现在 "x 1 2" 之后的所有组都
      给出 ERROR）。本实现保持输入流对齐，后面的组给出正确结果。
      需要**逐字节复现**原实现时加 --oj-legacy-sync（见下）。
    - 原实现的除法在被除数比除数小一个数量级以上时会丢掉小数点后的前导
      零（1/100 输出 0.1 而不是 0.01，1/2048 输出 0.04882813 而不是
      0.00048828125）。本实现给出正确的算术结果。

  输入被截断时（组数比实际数据多），剩下的每一组都输出 ERROR。

  首行的组数是**声明**的，一次运行最多接受 1000000 组 —— 超过上限就报错
  并停止（正常的 OJ 数据只有几万组）。不设上限的话，一个声明 2e9 组的
  输入会让本实现输出 21 亿行 ERROR（legacy 也是这样）。

  --oj-legacy-sync
    逐字节复现原实现的**输入流错位**行为：运算符非法时不读走该组的两个
    操作数，于是后面每一组都读到错的位置、给出 ERROR。默认**不**启用 ——
    原实现的行为是缺陷，不是协议的一部分。

  另外：原实现在 "* 0 0.0001" 这类输入上会**崩溃**（SIGSEGV，legacy 的
  deleteZero() 空指针解引用，见 legacy/COMPARISON.md 的缺陷 #5）。
  本实现不会崩。
)";
}

// OJ 模式：与 legacy/oj_console_edition 的协议对齐。
//
// legacySync 选**复现原实现的行为**（运算符非法时不读走该组的两个操作数，
// 输入流因此错位、后面每一组都读到错的位置）。默认 false —— 保持输入流
// 对齐，见 printUsage() 里的说明。
int runOjMode(bool legacySync) {
    // 首行是**声明**的组数。
    //
    // 不用 `std::cin >> int`：C++11 规定它在数值超出 int 范围时把目标置为
    // INT_MAX —— 于是 legacy 声明 2e9 组时实际输出 **21 亿行** ERROR
    // （实测 >25 s 超时），而「解析失败」与「组数就是 0」在输出上完全
    // 一样：都是静悄悄的空输出，用户看不到任何诊断。
    //
    // 改法：跳过前导空白，读掉一个可选的正负号，然后**逐位**收下连续的
    // 数字位，**一旦超过 kOjMaxGroups 就立刻停**（于是组数永远不会溢出）。
    // 非数字的那一位**留在输入流里** —— 与 legacy 的 `std::cin >> int` 的
    // 提取语义一致（"12abc" 在 legacy 那里也是把 12 取走、把 "abc" 留给
    // 后面的组）。
    std::cin >> std::ws;
    std::string countText;
    bool negative = false;
    const int first = std::cin.peek();
    if (first == '+' || first == '-') {
        negative = (first == '-');
        std::cin.get();
    }
    while (std::cin.good()) {
        const int c = std::cin.peek();
        if (c < '0' || c > '9') break;
        countText.push_back(static_cast<char>(std::cin.get()));
    }
    if (negative) return 0;                    // 负数：没有组（与 legacy 一致）

    long long totalCases = 0;
    bool tooLarge = false;
    for (const char c : countText) {
        totalCases = totalCases * 10 + (c - '0');
        if (totalCases > kOjMaxGroups) {
            tooLarge = true;
            break;
        }
    }
    if (tooLarge) {
        std::cerr << "错误：声明的测试组数 " << countText
                  << " 超过上限 " << kOjMaxGroups << "，已停止处理\n";
        return 1;
    }
    if (totalCases <= 0) return 0;

    const BigDecimal minimumDivisor = BigDecimal::fromString(kOjMinimumDivisor);

    for (long long i = 0; i < totalCases; ++i) {
        char mode = '+';
        std::string leftText;
        std::string rightText;

        if (!(std::cin >> mode)) {
            // 输入被截断：这一组和剩下的每一组都只能给 ERROR。
            for (long long j = i; j < totalCases; ++j) std::cout << "ERROR\n";
            break;
        }

        const bool modeValid = (mode == '+' || mode == '-' || mode == '*' || mode == '/');
        if (!modeValid && legacySync) {
            // 原实现的行为：不读走该组的两个操作数。输入流因此错位 ——
            // 后面每一组都读到错的位置，给出的都是 ERROR。
            std::cout << "ERROR\n";
            continue;
        }

        // 运算符非法时**也要**把两个操作数读掉：不读的话输入流会错位，
        // 后面每一组都读到错的位置（原实现就有这个缺陷）。
        const bool leftRead = static_cast<bool>(std::cin >> leftText);
        const bool rightRead = static_cast<bool>(std::cin >> rightText);
        if (!leftRead || !rightRead) {
            // 输入被截断：这一组和剩下的每一组都只能给 ERROR。
            for (long long j = i; j < totalCases; ++j) std::cout << "ERROR\n";
            break;
        }

        if (!modeValid) {
            std::cout << "ERROR\n";
            continue;
        }

        try {
            const BigDecimal left = BigDecimal::fromString(stripThousandsSeparators(leftText));
            const BigDecimal right = BigDecimal::fromString(stripThousandsSeparators(rightText));
            BigDecimal result;
            if (mode == '+') {
                result = left + right;
            } else if (mode == '-') {
                result = left - right;
            } else if (mode == '*') {
                result = left * right;
            } else {
                // 除数过小则报错，与原实现一致（checkBit：绝对值 < 0.000001）。
                if (BigDecimal::abs(right).compare(minimumDivisor) < 0) {
                    std::cout << "ERROR\n";
                    continue;
                }
                result = ojDivide(left, right);
            }
            std::cout << withThousandSeparators(result.toString()) << "\n";
        } catch (const std::exception&) {
            // 操作数不是合法数字等情况。
            std::cout << "ERROR\n";
        }
    }
    return 0;
}

int runInteractive(const Context& context, int base) {
    std::cout << "高精度计算器 · 交互模式（输入表达式，Ctrl+D 退出；空行会被忽略）\n";
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        // 空白行**不**当作「求值空表达式」（那是错误），也不当作退出 ——
        // 管道输入里常有空行，把它当成退出会静默丢掉后面的输入。
        if (isBlank(line)) continue;
        // 出错后继续接受下一行输入，不退出。
        evaluateAndPrint(line, context, base);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    useUtf8Console();

    Context context{30, RoundingMode::HalfEven};
    int base = 10;
    std::string expression;
    bool hasExpression = false;
    bool ojMode = false;
    bool ojLegacySync = false;
    bool precisionGiven = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            printUsage();
            return 0;
        }
        if (argument == "--oj") {
            ojMode = true;
        } else if (argument == "--oj-legacy-sync") {
            // 复现原实现的输入流错位行为（见 printUsage()）。
            ojLegacySync = true;
            ojMode = true;
        } else if (argument == "-e" || argument == "--expression") {
            if (i + 1 >= argc) {
                std::cerr << "错误：选项 " << argument << " 缺少参数\n";
                return 2;
            }
            expression = argv[++i];
            // 用**独立的标志位**记录「给了 -e」，而不是判断表达式是否为空 ——
            // `-e ""` 是一个合法的（虽然错误的）参数，不能因此掉进交互模式。
            hasExpression = true;
        } else if (argument == "-p" || argument == "--precision") {
            if (i + 1 >= argc) {
                std::cerr << "错误：选项 " << argument << " 缺少参数\n";
                return 2;
            }
            const std::string value = argv[++i];
            long long precision = 0;
            if (!parseInteger(value, precision)) {
                std::cerr << "错误：精度必须是十进制整数，得到 \"" << value << "\"\n";
                return 2;
            }
            if (precision < 1 || precision > hpc::kMaxPrecision) {
                std::cerr << "错误：精度必须在 1.." << hpc::kMaxPrecision
                          << " 之间，得到 " << precision << "\n";
                return 2;
            }
            context.precision = static_cast<int>(precision);
            precisionGiven = true;
        } else if (argument == "-b" || argument == "--base") {
            if (i + 1 >= argc) {
                std::cerr << "错误：选项 " << argument << " 缺少参数\n";
                return 2;
            }
            const std::string value = argv[++i];
            long long parsed = 0;
            if (!parseInteger(value, parsed)) {
                std::cerr << "错误：进制必须是十进制整数，得到 \"" << value << "\"\n";
                return 2;
            }
            if (parsed < 2 || parsed > 36) {
                std::cerr << "错误：进制必须在 2..36 之间，得到 " << parsed << "\n";
                return 2;
            }
            base = static_cast<int>(parsed);
        } else {
            std::cerr << "错误：未知参数 \"" << argument << "\"\n\n";
            printUsage();
            return 2;
        }
    }

    if (ojMode) {
        // OJ 模式是**固定协议**：输出恒为十进制、除法精度恒为 10 位小数。
        // 静默忽略这些选项等于骗用户，所以直接报错。
        if (hasExpression) {
            std::cerr << "错误：--oj 与 -e 不能同时使用\n";
            return 2;
        }
        if (precisionGiven) {
            std::cerr << "错误：--oj 模式的除法精度固定为 10 位小数（与原协议一致），"
                         "不接受 -p\n";
            return 2;
        }
        if (base != 10) {
            std::cerr << "错误：--oj 模式的输出固定为十进制，不接受 -b\n";
            return 2;
        }
        return runOjMode(ojLegacySync);
    }

    if (hasExpression) {
        return evaluateAndPrint(expression, context, base) ? 0 : 1;
    }

    return runInteractive(context, base);
}
