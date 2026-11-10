// 函数探针：读入 "op a b [mode]"，输出 sqrt/exp/ln/log10/pow 的结果。
//
// mode 可选，缺省为 HalfEven；取值与 hpc::RoundingMode 的名字一致。
// 供 scripts/verify_func.py 做 Python decimal 差分验证。
#include <iostream>
#include <sstream>
#include <string>

#include "core/BigDecimal.h"

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

    // 精度从命令行取（有效数字位），缺省 20。
    // scripts/verify_func.py 靠它做不同精度下的差分验证。
    int precision = 20;
    if (argc > 1) {
        try {
            precision = std::stoi(argv[1]);
        } catch (const std::exception&) {
            precision = 20;
        }
    }
    if (precision <= 0) precision = 20;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string op, a, b, modeName;
        if (!(input >> op >> a >> b)) continue;
        input >> modeName;   // 读不到就是空串
        const hpc::RoundingMode mode = modeName.empty() ? hpc::RoundingMode::HalfEven
                                                         : parseMode(modeName);
        try {
            const hpc::Context context{precision, mode};
            hpc::BigDecimal result;
            if (op == "sqrt") {
                result = hpc::BigDecimal::sqrt(hpc::BigDecimal::fromString(a), context);
            } else if (op == "exp") {
                result = hpc::BigDecimal::exp(hpc::BigDecimal::fromString(a), context);
            } else if (op == "ln") {
                result = hpc::BigDecimal::ln(hpc::BigDecimal::fromString(a), context);
            } else if (op == "log10") {
                result = hpc::BigDecimal::log10(hpc::BigDecimal::fromString(a), context);
            } else if (op == "pow") {
                result = hpc::BigDecimal::pow(hpc::BigDecimal::fromString(a),
                                              hpc::BigDecimal::fromString(b), context);
            } else {
                std::cout << "ERROR\n";
                continue;
            }
            std::cout << result.toString() << "\n";
        } catch (const std::exception&) {
            std::cout << "ERROR\n";
        }
    }
    return 0;
}
