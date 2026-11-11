#include "core/Format.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hpc {
namespace {

constexpr std::uint64_t kBase = 1000000000ULL;   // 10^9
constexpr int kBlockDigits = 9;

// 进制字符表。
const char kDigitsLower[] = "0123456789abcdefghijklmnopqrstuvwxyz";
const char kDigitsUpper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

// 小数部分默认输出的最大位数：超过就截断并标记。
//
// 位数由「值的十进制小数位数」推出（见 fractionDigitCount），
// 但要设上限：一个小数位很多的值在二进制下可能需要成千上万位。
constexpr int kMaxFractionDigits = 4096;

// 左移位数的上限：2^bits 的十进制位数约是 bits 的 0.301 倍，
// 位数过大说明调用方失控，应当报错而不是继续消耗内存。
constexpr int kMaxShiftBits = 1000000;

// 小端 base-10^9 块序列：blocks[0] 是最低位块。
using Blk = std::vector<std::uint32_t>;

// --- 块序列的基础运算 -------------------------------------------------

// 去掉高位的零块，但至少保留一个块表示 0。
Blk trimBlk(Blk blocks) {
    while (blocks.size() > 1 && blocks.back() == 0) blocks.pop_back();
    if (blocks.empty()) blocks.push_back(0);
    return blocks;
}

// 块序列是否表示 0。
bool isZeroBlk(const Blk& blocks) {
    return blocks.size() == 1 && blocks[0] == 0;
}

// 比较两块序列（均为小端）：<0 / 0 / >0。
int compareBlk(const Blk& lhs, const Blk& rhs) {
    if (lhs.size() != rhs.size()) return lhs.size() < rhs.size() ? -1 : 1;
    for (std::size_t i = lhs.size(); i-- > 0;) {
        if (lhs[i] != rhs[i]) return lhs[i] < rhs[i] ? -1 : 1;
    }
    return 0;
}

// 块序列乘以 base（base <= 36，块内不会溢出 64 位）。
Blk multiplyBlkByBase(const Blk& blocks, std::uint64_t base) {
    Blk result(blocks.size(), 0);
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const std::uint64_t current = static_cast<std::uint64_t>(blocks[i]) * base + carry;
        result[i] = static_cast<std::uint32_t>(current % kBase);
        carry = current / kBase;
    }
    while (carry) {
        result.push_back(static_cast<std::uint32_t>(carry % kBase));
        carry /= kBase;
    }
    return trimBlk(std::move(result));
}

// 块序列加一个小的非负数（addend < kBase）。
void addToBlk(Blk& blocks, std::uint64_t addend) {
    std::uint64_t add = addend;
    for (std::size_t k = 0; k < blocks.size() && add; ++k) {
        const std::uint64_t current = static_cast<std::uint64_t>(blocks[k]) + add;
        blocks[k] = static_cast<std::uint32_t>(current % kBase);
        add = current / kBase;
    }
    if (add) blocks.push_back(static_cast<std::uint32_t>(add));
}

// 块序列除以 base，返回 {商, 余数}。商为小端块，余数 < base。
//
// 这是长除法的块级实现：把整个块序列当作一个大端数字，
// 逐块做「余数 * 10^9 + 块」再除以 base。
std::pair<Blk, std::uint64_t> divideBlkByBase(const Blk& blocks, std::uint64_t base) {
    Blk quotient(blocks.size(), 0);
    std::uint64_t remainder = 0;
    for (std::size_t i = blocks.size(); i-- > 0;) {
        const std::uint64_t current = remainder * kBase + blocks[i];
        quotient[i] = static_cast<std::uint32_t>(current / base);
        remainder = current % base;
    }
    return {trimBlk(std::move(quotient)), remainder};
}

// --- 块序列与字符串互转 -----------------------------------------------

// 把小端 base-10^9 块转为十进制数字串（高位在前，无前导零）。
std::string blocksToDecimalString(const Blk& blocks) {
    if (blocks.empty()) return "0";
    std::string digits = std::to_string(blocks.back());
    char buffer[16];
    for (std::size_t i = blocks.size() - 1; i-- > 0;) {
        std::snprintf(buffer, sizeof buffer, "%09u", static_cast<unsigned>(blocks[i]));
        digits += buffer;
    }
    return digits;
}

// 十进制数字串（小端块表示的整数）转回 base-10^9 小端块。
Blk blocksFromDecimalString(const std::string& text) {
    Blk blocks;
    std::size_t end = text.size();
    while (end > 0) {
        const std::size_t start = end >= static_cast<std::size_t>(kBlockDigits)
                                      ? end - static_cast<std::size_t>(kBlockDigits) : 0;
        std::uint32_t value = 0;
        for (std::size_t i = start; i < end; ++i) {
            value = value * 10 + static_cast<std::uint32_t>(text[i] - '0');
        }
        blocks.push_back(value);
        end = start;
    }
    return trimBlk(std::move(blocks));
}

// 块序列乘以 2^bits（bits <= 32）。
//
// 块内不会溢出 64 位：块值 < 10^9 < 2^30，2^32 × 2^30 = 2^62。
Blk multiplyBlkByPowerOfTwo(const Blk& blocks, int bits) {
    const std::uint64_t factor = 1ULL << bits;
    Blk result(blocks.size(), 0);
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const std::uint64_t current = static_cast<std::uint64_t>(blocks[i]) * factor + carry;
        result[i] = static_cast<std::uint32_t>(current % kBase);
        carry = current / kBase;
    }
    while (carry) {
        result.push_back(static_cast<std::uint32_t>(carry % kBase));
        carry /= kBase;
    }
    return trimBlk(std::move(result));
}

// 块序列加一个小整数（addend < 2^32）。
void addSmallToBlk(Blk& blocks, std::uint64_t addend) {
    std::uint64_t add = addend;
    for (std::size_t k = 0; k < blocks.size() && add; ++k) {
        const std::uint64_t current = static_cast<std::uint64_t>(blocks[k]) + add;
        blocks[k] = static_cast<std::uint32_t>(current % kBase);
        add = current / kBase;
    }
    while (add) {
        blocks.push_back(static_cast<std::uint32_t>(add % kBase));
        add /= kBase;
    }
}

// 块序列除以 2^bits（bits <= 32），返回 {商, 余数}。
//
// 块内不会溢出 64 位：余数 < 2^32，2^32 × 10^9 + 10^9 < 2^63。
std::pair<Blk, std::uint64_t> divideBlkByPowerOfTwo(const Blk& blocks, int bits) {
    const std::uint64_t divisor = 1ULL << bits;
    Blk quotient(blocks.size(), 0);
    std::uint64_t remainder = 0;
    for (std::size_t i = blocks.size(); i-- > 0;) {
        const std::uint64_t current = remainder * kBase + blocks[i];
        quotient[i] = static_cast<std::uint32_t>(current / divisor);
        remainder = current % divisor;
    }
    return {trimBlk(std::move(quotient)), remainder};
}

// --- 二进制字符串工具 -------------------------------------------------

// 小端块序列转成二进制字符串（高位在前，无前导零）。
//
// 反复除以 2^32，每次得到 32 个二进制位 —— 复杂度 O(位数/32 × 块数)，
// 比逐位除以 2（O(位数 × 块数)）快一个数量级。
//
// 注意：块的基数是 **10^9**，不是 2^32，因此不能「每块取 32 位」直接拼；
// 必须真的做除法。
std::string blocksToBinary(const Blk& blocks) {
    std::vector<std::uint32_t> chunks;   // 最低位块在前
    Blk current = blocks;
    while (!isZeroBlk(current)) {
        const auto divided = divideBlkByPowerOfTwo(current, 32);
        chunks.push_back(static_cast<std::uint32_t>(divided.second));
        current = divided.first;
    }

    // 从高位块到低位块输出，每块 32 位（高位在前）。
    std::string bits;
    bits.reserve(chunks.size() * 32);
    for (std::size_t i = chunks.size(); i-- > 0;) {
        const std::uint32_t chunk = chunks[i];
        for (int bit = 31; bit >= 0; --bit) {
            bits.push_back(((chunk >> bit) & 1u) ? '1' : '0');
        }
    }
    const std::size_t first = bits.find('1');
    if (first == std::string::npos) return "0";
    return bits.substr(first);
}

// 二进制字符串转回小端 base-10^9 块。
//
// 按 32 位一组做 Horner 法：value = value * 2^32 + chunk。
// 复杂度 O(位数/32 × 块数)。
Blk blocksFromBinary(const std::string& bits) {
    Blk value{0};
    std::size_t index = 0;

    // 头部不足 32 位的一组。
    const std::size_t head = bits.size() % 32;
    if (head != 0) {
        std::uint32_t chunk = 0;
        for (std::size_t i = 0; i < head; ++i) {
            chunk = (chunk << 1) | ((bits[i] == '1') ? 1u : 0u);
        }
        value = multiplyBlkByPowerOfTwo(value, static_cast<int>(head));
        addSmallToBlk(value, chunk);
        index = head;
    }

    for (; index < bits.size(); index += 32) {
        std::uint32_t chunk = 0;
        for (std::size_t i = 0; i < 32; ++i) {
            chunk = (chunk << 1) | ((bits[index + i] == '1') ? 1u : 0u);
        }
        value = multiplyBlkByPowerOfTwo(value, 32);
        addSmallToBlk(value, chunk);
    }
    return trimBlk(std::move(value));
}

// 左侧补零到指定宽度。
std::string padLeft(const std::string& text, std::size_t width) {
    if (text.size() >= width) return text;
    return std::string(width - text.size(), '0') + text;
}

// 逐位取反（'0' <-> '1'）。
std::string invertBits(const std::string& bits) {
    std::string result(bits.size(), '0');
    for (std::size_t i = 0; i < bits.size(); ++i) {
        result[i] = (bits[i] == '1') ? '0' : '1';
    }
    return result;
}

// 二进制字符串加一（最低位在**末尾**）。溢出时返回全零。
//
// 结果必须从**原串**出发：进位链上的 '1' 变 '0'，遇到的第一个 '0' 变 '1'，
// 更高位原样保留。若从全零开始，更高位会被错误地抹成 0。
std::string addOneBinary(const std::string& bits) {
    std::string result = bits;
    std::size_t position = bits.size();
    while (position > 0) {
        --position;
        if (bits[position] == '0') {
            result[position] = '1';
            return result;
        }
        result[position] = '0';   // 进位链上的 1 变 0
    }
    return result;   // 全是 1：溢出，结果全零
}

// 把整数的幅值（绝对值）转成 width 位的**二进制补码**表示。
//
// 负数的补码 = 幅值的补码取反加一。因为 |v| < 2^(width-1)，
// 其 width 位表示的最高位是 0，取反后最高位是 1，加一不会溢出。
//
// magnitudeBits 是幅值的二进制表示（高位在前，无前导零），
// 由调用方算好 —— 同一个操作数在运算里要用多次，不能重复转换。
std::string toTwosComplement(const std::string& magnitudeBits, int sign,
                              std::size_t width) {
    const std::string padded = padLeft(magnitudeBits, width);
    if (sign >= 0) return padded;
    return addOneBinary(invertBits(padded));
}

// 把 width 位的二进制补码解释回整数。
BigDecimal fromTwosComplement(const std::string& bits) {
    if (bits.empty()) return BigDecimal(0);
    if (bits[0] == '0') {
        return BigDecimal::fromString(blocksToDecimalString(blocksFromBinary(bits)));
    }
    // 最高位是 1：负数。取反加一得到幅值。
    const std::string magnitude = addOneBinary(invertBits(bits));
    const BigDecimal value = BigDecimal::fromString(blocksToDecimalString(blocksFromBinary(magnitude)));
    return BigDecimal(0) - value;
}

// --- 其他工具 ---------------------------------------------------------

// 校验 base 合法。
void checkBase(int base, const char* who) {
    if (base < 2 || base > 36) {
        throw std::invalid_argument(std::string(who) + ": base 必须在 2..36");
    }
}

// 取整数（0 <= 值 < 10^9）的数值。
//
// 整数部分的每一步都被限制在 [0, base)，base <= 36，因此它一定落在
// 单个块里，直接读块即可。
std::uint32_t smallIntegerValue(const BigDecimal& value) {
    const Blk& blocks = value.integerBlocks();
    return blocks.empty() ? 0u : blocks[0];
}

// 小数部分默认输出多少位（base 进制）。
//
// 目标是让输出携带的精度不少于值自身的小数位数：找出最小的 D，
// 使 base^D >= 10^scale。整除用块级运算，不依赖浮点数。
int fractionDigitCount(const BigDecimal& value, int base) {
    const int scale = value.scale();
    if (scale <= 0) return 0;

    // target = 10^scale
    const Blk target = blocksFromDecimalString("1" + std::string(static_cast<std::size_t>(scale), '0'));
    Blk power{1};
    int digits = 0;
    while (digits < kMaxFractionDigits) {
        power = multiplyBlkByBase(power, static_cast<std::uint64_t>(base));
        ++digits;
        if (compareBlk(power, target) >= 0) break;
    }
    return digits;
}

}  // namespace

std::string toBaseString(const BigDecimal& value, int base, bool uppercase, int fractionDigits) {
    checkBase(base, "toBaseString");
    const char* table = uppercase ? kDigitsUpper : kDigitsLower;

    // 整数部分：精确无损。负值带前导负号。
    const BigDecimal integer = value.truncateToInteger();
    Blk current = integer.integerBlocks();

    std::string out;
    while (!isZeroBlk(current)) {
        const auto divided = divideBlkByBase(current, static_cast<std::uint64_t>(base));
        out.push_back(table[divided.second]);
        current = divided.first;
    }
    if (out.empty()) out.push_back(table[0]);
    std::reverse(out.begin(), out.end());

    // 小数部分：按目标进制逐位提取。该进制下无法有限表示时，
    // 末尾用 "..." 显式标记截断 —— 不做无声的近似。
    if (value.scale() > 0) {
        const int digits = (fractionDigits > 0) ? fractionDigits
                                               : fractionDigitCount(value, base);
        if (digits > 0) {
            // 小数部分 = value - 整数部分（保留原符号），再取绝对值。
            BigDecimal remaining = BigDecimal::abs(value - integer);

            std::string fractionText;
            bool truncated = false;
            for (int i = 0; i < digits; ++i) {
                // remaining * base 的整数部分就是下一位，剩余部分继续迭代。
                const BigDecimal scaled = remaining * BigDecimal(static_cast<std::int64_t>(base));
                const BigDecimal whole = scaled.truncateToInteger();
                fractionText.push_back(table[smallIntegerValue(whole)]);
                remaining = scaled - whole;
                if (remaining.isZero()) break;
            }
            if (!remaining.isZero()) truncated = true;

            if (truncated) {
                // 发生截断时**保留全部 digits 位**：末位可能是展开中的真实
                // 数字，删掉它会改变数值（"0.0100…" 与 "0.01…" 含义不同）。
                out.push_back('.');
                out += fractionText;
                out += "...";
            } else {
                // 精确表示：去掉尾随零，给出最短的精确形式
                // （0.5 在二进制下是 "0.1"，不是 "0.1000"）。
                while (!fractionText.empty() && fractionText.back() == '0') {
                    fractionText.pop_back();
                }
                if (!fractionText.empty()) {
                    out.push_back('.');
                    out += fractionText;
                }
            }
        }
    }

    // 负号放在最前面（整数部分为 0 时也要保留：-0.5 显示为 "-0.5"）。
    if (value.sign() < 0) out.insert(out.begin(), '-');
    return out;
}

BigDecimal fromBaseString(std::string_view text, int base) {
    checkBase(base, "fromBaseString");
    if (text.empty()) throw std::invalid_argument("fromBaseString: 空字符串");

    std::size_t index = 0;
    bool negative = false;
    if (text[index] == '+' || text[index] == '-') {
        negative = (text[index] == '-');
        ++index;
    }
    if (index >= text.size()) throw std::invalid_argument("fromBaseString: 没有数字");

    Blk value{0};
    for (; index < text.size(); ++index) {
        const char c = text[index];
        if (c == '.') {
            throw std::invalid_argument("fromBaseString: 只支持整数，不接受小数点");
        }
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else throw std::invalid_argument(std::string("fromBaseString: 非法字符 '") + c + "'");
        if (digit >= base) throw std::invalid_argument("fromBaseString: 数字超出进制范围");

        // value = value * base + digit
        value = multiplyBlkByBase(value, static_cast<std::uint64_t>(base));
        addToBlk(value, static_cast<std::uint64_t>(digit));
    }

    const std::string decimal = blocksToDecimalString(value);
    if (negative) return BigDecimal::fromString("-" + decimal);
    return BigDecimal::fromString(decimal);
}

BigDecimal bitwise(const BigDecimal& lhs, const BigDecimal& rhs, BitOp op) {
    if (!lhs.isInteger() || !rhs.isInteger()) {
        throw std::domain_error("bitwise: 只支持整数");
    }
    switch (op) {
        case BitOp::And:
        case BitOp::Or:
        case BitOp::Xor:
            break;
        default:
            throw std::invalid_argument("bitwise: 未知位运算");
    }

    // 采用**无限位**二进制补码语义（与 Python 的 int 运算一致）：
    // 负数的表示是符号扩展到无穷多位的补码。
    //
    // 有限串无法直接表达无穷符号扩展，但只要宽度足够，结果就与无限宽
    // 度一致。宽度取 W = max(幅值位数) + 1：
    //   and / or 的结果幅值不超过两者的最大值；
    //   xor 在「一正一负」时幅值不超过 a + |b| < 2^(W-1)；
    //   xor 在「两负」时结果为正且幅值不超过两者的最大值。
    // 因此 W 位的有符号范围足以容纳结果。
    //
    // 幅值的二进制串只转换一次：宽度要用它的长度，补码也要用它。
    const std::string bitsA = blocksToBinary(lhs.integerBlocks());
    const std::string bitsB = blocksToBinary(rhs.integerBlocks());
    const std::size_t width = std::max(bitsA.size(), bitsB.size()) + 1;

    const std::string a = toTwosComplement(bitsA, lhs.sign(), width);
    const std::string b = toTwosComplement(bitsB, rhs.sign(), width);
    std::string result(width, '0');
    for (std::size_t i = 0; i < width; ++i) {
        const bool ca = (a[i] == '1');
        const bool cb = (b[i] == '1');
        switch (op) {
            case BitOp::And: result[i] = (ca && cb) ? '1' : '0'; break;
            case BitOp::Or:  result[i] = (ca || cb) ? '1' : '0'; break;
            case BitOp::Xor: result[i] = (ca != cb) ? '1' : '0'; break;
        }
    }
    return fromTwosComplement(result);
}

BigDecimal bitwiseNot(const BigDecimal& value) {
    if (!value.isInteger()) throw std::domain_error("bitwiseNot: 只支持整数");
    // ~x = -x - 1（对任何整数都成立，包括 x = 0 与 x = -1）。
    return (BigDecimal(0) - value) - BigDecimal(1);
}

BigDecimal shiftLeft(const BigDecimal& value, int bits) {
    if (!value.isInteger()) throw std::domain_error("shiftLeft: 只支持整数");
    if (bits < 0) throw std::invalid_argument("shiftLeft: 位数不能为负");
    if (bits > kMaxShiftBits) throw std::overflow_error("shiftLeft: 位数过大");
    if (value.isZero() || bits == 0) return value;

    // 精确地乘以 2^bits：走 BigDecimal 的精确乘法（十进制表示下 2 的幂
    // 无损，只是位数增长）。用平方倍增而不是逐次乘 2，复杂度从
    // O(bits) 次乘法降到 O(log bits) 次。
    BigDecimal factor(1);
    BigDecimal two(2);
    int remaining = bits;
    while (remaining > 0) {
        if (remaining & 1) factor = factor * two;
        remaining >>= 1;
        if (remaining > 0) two = two * two;
    }
    return value * factor;
}

BigDecimal shiftRight(const BigDecimal& value, int bits) {
    if (!value.isInteger()) throw std::domain_error("shiftRight: 只支持整数");
    if (bits < 0) throw std::invalid_argument("shiftRight: 位数不能为负");
    if (value.isZero() || bits == 0) return value;

    // 右移 = 整除 2^bits。**在二进制上做**才能保证无损：十进制块上
    // 除以 2^k 是错的（10^9 不是 2 的幂）。
    //
    // 负数**向 -∞ 取整**（与 Python 的 >> 一致）：-1 >> 100 == -1。
    // 即 magnitude_result = ceil(|v| / 2^bits)。
    const std::string bitsOfValue = blocksToBinary(value.integerBlocks());
    std::string shifted;
    bool hasRemainder = false;
    if (static_cast<std::size_t>(bits) >= bitsOfValue.size()) {
        shifted = "0";
        hasRemainder = (bitsOfValue.find('1') != std::string::npos);
    } else {
        const std::size_t cut = bitsOfValue.size() - static_cast<std::size_t>(bits);
        shifted = bitsOfValue.substr(0, cut);
        hasRemainder = (bitsOfValue.find('1', cut) != std::string::npos);
    }

    BigDecimal magnitude = BigDecimal::fromString(
        blocksToDecimalString(blocksFromBinary(shifted)));
    if (value.sign() < 0) {
        // 负数向 -∞ 取整：幅值向上取整后再取负。
        if (hasRemainder) magnitude = magnitude + BigDecimal(1);
        return BigDecimal(0) - magnitude;
    }
    return magnitude;
}

}  // namespace hpc
