// 表达式探针：读入一行表达式与可选的精度/舍入模式，输出求值结果。
//
// 输入格式（每行一条）：
//   <precision> <mode> <expression>
//   precision 是有效数字位，mode 取 HalfEven/HalfUp/Down/Up/Ceil/Floor。
// 表达式的其余部分原样传给 hpc::evaluate —— 表达式里可以含空格，
// 所以模式与表达式之间用一个制表符分隔。
//
// 输出：成功时是十进制结果；失败时是 "ERROR"。
//
// **错误消息不输出**：消息是中文的，Windows 控制台默认的 GBK 代码页下
// 管道解码会失败（scripts/verify_expression.py 读 stdout 时抛
// UnicodeDecodeError）。差分脚本只需要知道「成功还是失败」，
// 错误消息的细节由单元测试覆盖。
//
// 供 scripts/verify_expression.py 做 Python 差分验证。
#include <iostream>
#include <sstream>
#include <string>

#include "core/Expression.h"

namespace {

hpc::RoundingMode parseMode(const std::string& name) {
    if (name == "HalfUp") return hpc::RoundingMode::HalfUp;
    if (name == "Down") return hpc::RoundingMode::Down;
    if (name == "Up") return hpc::RoundingMode::Up;
    if (name == "Ceil") return hpc::RoundingMode::Ceil;
    if (name == "Floor") return hpc::RoundingMode::Floor;
    return hpc::RoundingMode::HalfEven;
}

}  // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);

    // 缺省精度。
    int defaultPrecision = 20;
    if (argc > 1) {
        try {
            defaultPrecision = std::stoi(argv[1]);
        } catch (const std::exception&) {
            defaultPrecision = 20;
        }
    }
    if (defaultPrecision <= 0) defaultPrecision = 20;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        // 头是两个制表符分隔的字段：<precision>、<mode>，其余是表达式。
        //
        // 表达式里可以有空格，所以分隔符必须用制表符（表达式本身不含制表符）。
        const std::size_t firstTab = line.find('\t');
        if (firstTab == std::string::npos) {
            std::cout << "ERROR\n";
            continue;
        }
        const std::size_t secondTab = line.find('\t', firstTab + 1);
        if (secondTab == std::string::npos) {
            std::cout << "ERROR\n";
            continue;
        }

        std::istringstream header(line.substr(firstTab + 1, secondTab - firstTab - 1));
        std::string modeName;
        header >> modeName;
        if (modeName.empty()) modeName = "HalfEven";

        int precision = defaultPrecision;
        std::istringstream precisionField(line.substr(0, firstTab));
        precisionField >> precision;
        if (precision <= 0) precision = defaultPrecision;

        const std::string expression = line.substr(secondTab + 1);

        try {
            const hpc::Context context{precision, parseMode(modeName)};
            const hpc::BigDecimal result = hpc::evaluate(expression, context);
            std::cout << result.toString() << "\n";
        } catch (const hpc::ExpressionError&) {
            std::cout << "ERROR\n";
        } catch (const std::exception&) {
            std::cout << "ERROR\n";
        }
    }
    return 0;
}
