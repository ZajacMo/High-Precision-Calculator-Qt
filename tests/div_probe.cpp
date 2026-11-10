// 除法探针：读入若干行 "a b [mode]"，输出每行 a/b 的结果（默认 20 位精度）。
//
// mode 可选，缺省为 HalfEven；取值与 hpc::RoundingMode 的名字一致。
// 供 scripts/verify_div.py 做 Python decimal 差分验证。
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

int main() {
    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string a, b, modeName;
        if (!(input >> a >> b)) continue;
        input >> modeName;   // 读不到就是空串
        const hpc::RoundingMode mode = modeName.empty() ? hpc::RoundingMode::HalfEven
                                                         : parseMode(modeName);
        try {
            const hpc::BigDecimal quotient = hpc::BigDecimal::divide(
                hpc::BigDecimal::fromString(a), hpc::BigDecimal::fromString(b),
                hpc::Context{20, mode});
            std::cout << quotient.toString() << "\n";
        } catch (const std::exception&) {
            std::cout << "ERROR\n";
        }
    }
    return 0;
}
