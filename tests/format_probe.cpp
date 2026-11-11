// 格式/位运算探针：读入一行命令，输出结果。
//
// 命令格式（用 | 分隔字段）：
//   tobase <base> <uppercase 0|1> <fractionDigits> <十进制值>
//   frombase <base> <base进制字符串>
//   and|or|xor <十进制值A> <十进制值B>
//   not <十进制值>
//   shl <十进制值> <位数>
//   shr <十进制值> <位数>
//
// 供 scripts/verify_format.py 做 Python 差分验证。
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/Format.h"

namespace {

std::vector<std::string> split(const std::string& line, char separator) {
    std::vector<std::string> parts;
    std::string current;
    std::istringstream input(line);
    while (std::getline(input, current, separator)) {
        parts.push_back(current);
    }
    return parts;
}

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> parts = split(line, '|');
        try {
            if (parts[0] == "tobase") {
                const int base = std::stoi(parts[1]);
                const bool uppercase = parts[2] != "0";
                const int fractionDigits = std::stoi(parts[3]);
                const hpc::BigDecimal value = hpc::BigDecimal::fromString(parts[4]);
                std::cout << hpc::toBaseString(value, base, uppercase, fractionDigits) << "\n";
            } else if (parts[0] == "frombase") {
                const int base = std::stoi(parts[1]);
                const hpc::BigDecimal value = hpc::fromBaseString(parts[2], base);
                std::cout << value.toString() << "\n";
            } else if (parts[0] == "and" || parts[0] == "or" || parts[0] == "xor") {
                const hpc::BigDecimal a = hpc::BigDecimal::fromString(parts[1]);
                const hpc::BigDecimal b = hpc::BigDecimal::fromString(parts[2]);
                const hpc::BitOp op = (parts[0] == "and") ? hpc::BitOp::And
                                    : (parts[0] == "or") ? hpc::BitOp::Or
                                                       : hpc::BitOp::Xor;
                std::cout << hpc::bitwise(a, b, op).toString() << "\n";
            } else if (parts[0] == "not") {
                const hpc::BigDecimal value = hpc::BigDecimal::fromString(parts[1]);
                std::cout << hpc::bitwiseNot(value).toString() << "\n";
            } else if (parts[0] == "shl") {
                const hpc::BigDecimal value = hpc::BigDecimal::fromString(parts[1]);
                const int bits = std::stoi(parts[2]);
                std::cout << hpc::shiftLeft(value, bits).toString() << "\n";
            } else if (parts[0] == "shr") {
                const hpc::BigDecimal value = hpc::BigDecimal::fromString(parts[1]);
                const int bits = std::stoi(parts[2]);
                std::cout << hpc::shiftRight(value, bits).toString() << "\n";
            } else {
                std::cout << "ERROR\n";
            }
        } catch (const std::exception&) {
            std::cout << "ERROR\n";
        }
    }
    return 0;
}
