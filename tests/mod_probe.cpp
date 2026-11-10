// 取余与取整探针：读入 "op a b [prec]"，输出结果。
#include <iostream>
#include <sstream>
#include <string>

#include "core/BigDecimal.h"

int main() {
    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string op, a, b;
        if (!(input >> op >> a >> b)) continue;
        std::string precText;
        input >> precText;
        const int precision = precText.empty() ? 20 : std::stoi(precText);
        try {
            const hpc::BigDecimal x = hpc::BigDecimal::fromString(a);
            const hpc::BigDecimal y = hpc::BigDecimal::fromString(b);
            hpc::BigDecimal result;
            if (op == "mod") {
                result = hpc::BigDecimal::modulo(x, y);
            } else if (op == "floor") {
                result = hpc::BigDecimal::floor(x, hpc::Context{precision});
            } else if (op == "ceil") {
                result = hpc::BigDecimal::ceil(x, hpc::Context{precision});
            } else {
                result = hpc::BigDecimal::round(x, hpc::Context{precision});
            }
            std::cout << result.toString() << "\n";
        } catch (const std::exception&) {
            std::cout << "ERROR\n";
        }
    }
    return 0;
}
