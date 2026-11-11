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
// 这个上限只用来兜住「该进制下**无法**有限表示」时的输出长度 ——
// 那种情况下展开是无限的，必须在某处停下并标记。
//
// 能有限表示的值不受这个上限约束：它的展开长度是可以算出来的
// （见 exactFractionDigits），必须给完整，否则「...」标记就成了谎报。
constexpr int kMaxFractionDigits = 4096;

// 展开工作量的上限（单位：块运算次数）。
//
// 展开的代价是 O(位数 × 块数)：每提取一位都要把整个余数乘一次进制。
// 一个 10^6 位小数的值转成二进制需要 10^11 次块运算 —— 那不是「慢」，
// 而是永远算不完。
//
// 触及上限时抛 std::overflow_error：这与「截断」不同 —— 截断要输出一个
// 不精确的结果并标记它，而这里直接拒绝，因为该值本来是**可以**精确表示的，
// 只是算不起。谎报截断比报错更糟。
constexpr long long kMaxExpansionWork = 100000000LL;

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

// 10 的小幂表（10^0 .. 10^8）。
constexpr std::uint32_t kPow10[kBlockDigits] = {
    1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u};

// 块序列就地除以一个小的正整数（divisor <= 5）。
//
// 块内不会溢出 64 位：余数 < divisor，当前值 < 5 * 10^9。
void divideBlkBySmall(Blk& blocks, std::uint32_t divisor) {
    std::uint64_t remainder = 0;
    for (std::size_t i = blocks.size(); i-- > 0;) {
        const std::uint64_t current = remainder * kBase + blocks[i];
        blocks[i] = static_cast<std::uint32_t>(current / divisor);
        remainder = current % divisor;
    }
    blocks = trimBlk(std::move(blocks));
}

// 分子（绝对值）能被 prime 整除的次数（上限 cap）。
//
// prime 只取 2 或 5，而块的基数 10^9 = 2^9 · 5^9 —— 因此**整除性只看
// 最低块**：M = m_0 + 10^9 · H，v_p(10^9 · H) >= 9，所以当
// v_p(m_0) < 9 时 v_p(M) = v_p(m_0)（两个加数的 2-adic 赋值不同）。
//
// 常见情况只查最低块就够了（v_2 的命中率 511/512）。
//
// 罕见情况（最低块恰好含 9 个以上 prime 因子）才走逐次相除，那条路是
// O(赋值 × 块数)，因此设一道工作量闸。
int valuationCapped(const Blk& numerator, std::uint32_t prime, int cap) {
    if (cap <= 0) return 0;

    // 第一级：只看最低块。
    std::uint32_t low = numerator[0];
    int quick = 0;
    while (quick < 31 && low % prime == 0) { ++quick; low /= prime; }
    if (quick >= cap) return cap;
    if (quick < 9) return quick;

    // 第二级：最低两块合成一个 64 位数。v_p(D) < 18 时同样是答案。
    if (numerator.size() >= 2) {
        const std::uint64_t pair =
            static_cast<std::uint64_t>(numerator[0]) +
            static_cast<std::uint64_t>(numerator[1]) * kBase;
        std::uint64_t rest = pair;
        int count = 0;
        while (count < 63 && rest % prime == 0) { ++count; rest /= prime; }
        if (count >= cap) return cap;
        if (count < 18) return count;
    }

    // 罕见路径：逐次相除。
    Blk value = numerator;
    const long long blockCount = static_cast<long long>(value.size());
    int count = 0;
    while (count < cap) {
        if (value[0] % prime != 0) break;
        divideBlkBySmall(value, prime);
        ++count;
        if (static_cast<long long>(count) * blockCount > kMaxExpansionWork) {
            throw std::overflow_error("toBaseString: 小数位过多，无法展开");
        }
    }
    return count;
}

// base 中 prime 的指数（base 能被 prime 整除的次数）。
int factorExponent(int base, std::uint32_t prime) {
    int count = 0;
    while (base % prime == 0) {
        base /= prime;
        ++count;
    }
    return count;
}

// ceil(a / b)，a >= 0，b > 0。
int ceilDiv(int a, int b) { return (a + b - 1) / b; }

// 该值在 base 进制下的**精确**小数位数；返回 -1 表示无法有限表示。
//
// 值 = ±N / 10^s（N 是分子的绝对值，s 是十进制小数位）。
// base 进制下存在有限展开 ⟺ ∃k: 10^s | N · base^k，而
//
//   v2(N · base^k) = v2(N) + k · v2(base) >= s
//   v5(N · base^k) = v5(N) + k · v5(base) >= s
//
// 必须**同时**成立。满足这两条的最小 k 就是精确位数。
//
// 关于分子的选取：整数部分不影响展开长度 —— 对 p ∈ {2, 5}，
// v_p(整数部分 × 10^s) >= s，而 v_p(N) < s 时 v_p(分子) = v_p(N)，
// 所以「分子的 2/5 因子个数（上限 s）」与「小数部分的分子」相同。
int exactFractionDigits(const BigDecimal& value, int base) {
    const int scale = value.scale();
    if (scale <= 0) return 0;

    const int baseTwo = factorExponent(base, 2);
    const int baseFive = factorExponent(base, 5);

    const Blk& numerator = value.numeratorBlocks();
    const int two = valuationCapped(numerator, 2, scale);
    const int five = valuationCapped(numerator, 5, scale);

    // base 里没有的质因子必须已经被分子消掉，否则展开是无限的。
    if (baseTwo == 0 && two < scale) return -1;
    if (baseFive == 0 && five < scale) return -1;

    int digits = 0;
    if (baseTwo > 0 && two < scale) {
        digits = std::max(digits, ceilDiv(scale - two, baseTwo));
    }
    if (baseFive > 0 && five < scale) {
        digits = std::max(digits, ceilDiv(scale - five, baseFive));
    }
    return digits;
}

// 取块序列的最低 scale 位十进制数字（**定宽**）。
//
// 块是**小端**的（blocks[0] 是最低位块），所以最低 scale 位十进制数字
// 就在 blocks[0..] 里，而不是最高的几块。
//
// 两处必须讲究：
//
// 1. **掩掉 scale 以上的位**。scale 不是 9 的倍数时，最高的那个块里只有
//    低 rest 位属于小数部分，高位必须被掩掉 —— 否则余数比 10^scale 大，
//    展开出来的数字会整体偏移。
//
// 2. **定宽**（高位补零而不是裁剪）。余数的宽度决定了展开时
//    「除以 10^scale」的切分位置，裁掉高位零会让余数在下一步少掉一块，
//    于是切分点错位 —— 展开出的数字会**逐位偏移**。
Blk lowDecimalDigits(const Blk& blocks, int scale) {
    const int whole = scale / kBlockDigits;
    const int rest = scale % kBlockDigits;
    const int width = whole + (rest > 0 ? 1 : 0);

    Blk low(static_cast<std::size_t>(width), 0);
    for (int i = 0; i < whole; ++i) {
        if (i < static_cast<int>(blocks.size())) {
            low[static_cast<std::size_t>(i)] = blocks[static_cast<std::size_t>(i)];
        }
    }
    if (rest > 0) {
        const std::uint32_t block = (whole < static_cast<int>(blocks.size()))
                                        ? blocks[static_cast<std::size_t>(whole)] : 0u;
        low[static_cast<std::size_t>(whole)] = block % kPow10[rest];
    }
    return low;
}

// 块序列乘以 base（base <= 36），**不裁剪**高位零块。
//
// 展开时余数必须保持定宽（见 lowDecimalDigits），因此这里不能用会裁剪
// 高位零块的 multiplyBlkByBase —— 裁掉一块，下一步「除以 10^scale」的
// 切分点就会错位。
void multiplyBlkByBaseKeepingWidth(Blk& blocks, std::uint64_t base) {
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const std::uint64_t current = static_cast<std::uint64_t>(blocks[i]) * base + carry;
        blocks[i] = static_cast<std::uint32_t>(current % kBase);
        carry = current / kBase;
    }
    if (carry) blocks.push_back(static_cast<std::uint32_t>(carry));
}

// 块序列是否全为零。
bool isAllZeroBlk(const Blk& blocks) {
    for (const std::uint32_t block : blocks) {
        if (block != 0) return false;
    }
    return true;
}

// 从 T 中取出数字位与余数：T = digit · 10^scale + remainder。
//
// 调用方保证 T < base · 10^scale，因此 digit 是一个**数字位**（< base），
// 且 remainder < 10^scale。
//
// 10^scale = 10^(9·whole) · 10^rest。令 high = floor(T / 10^(9·whole))，
// 则 high 至多占两块（high < base · 10^rest <= 36 · 10^8 < 10^18），于是
//
//   digit     = floor(high / 10^rest)
//   remainder = (T 的低 whole 块) + (high mod 10^rest) · 10^(9·whole)
//
// 余数在「10^rest 的倍数」这一层上被切掉，因此 remainder < 10^scale。
// 余数保持**定宽**（scale 位十进制数字所需的块数），以便下一步的切分点
// 与本步一致。
std::pair<int, Blk> splitDigitAndRemainder(const Blk& t, int scale, int base) {
    const int whole = scale / kBlockDigits;
    const int rest = scale % kBlockDigits;

    // high = floor(T / 10^(9·whole))，至多占两块。
    std::uint64_t high = 0;
    if (whole < static_cast<int>(t.size())) {
        high = t[static_cast<std::size_t>(whole)];
        if (whole + 1 < static_cast<int>(t.size())) {
            high += static_cast<std::uint64_t>(t[static_cast<std::size_t>(whole + 1)]) * kBase;
        }
    }

    const std::uint32_t divisor = kPow10[rest];
    const std::uint64_t digit = high / divisor;
    if (digit >= static_cast<std::uint64_t>(base)) {
        throw std::overflow_error("toBaseString: 小数展开异常");
    }
    const std::uint32_t lowPart = static_cast<std::uint32_t>(high % divisor);

    // 余数 = 低 whole 块 + lowPart 放在第 whole 块（定宽）。
    Blk low(static_cast<std::size_t>(whole), 0);
    for (int i = 0; i < whole; ++i) {
        if (i < static_cast<int>(t.size())) {
            low[static_cast<std::size_t>(i)] = t[static_cast<std::size_t>(i)];
        }
    }
    if (rest > 0) low.push_back(lowPart);

    return {static_cast<int>(digit), std::move(low)};
}

// 十进制下的小数位：分子的最后 scale 位十进制数字（不足时左侧补零）。
//
// 十进制是内核的**原生**表示，这个特判把「逐位展开」换成「直接取数字」：
// 十进制展开的第 k 位就是分子第 (scale - k) 位的十进制数字，因此结果与
// 逐位展开**完全一致**，代价却从 O(位数 × 块数) 降到 O(位数)。
//
// 十进制下任何有限小数都能精确表示，所以这条路径永远不带截断标记。
std::string decimalFractionDigits(const BigDecimal& value) {
    const int scale = value.scale();
    const std::string digits = blocksToDecimalString(value.numeratorBlocks());
    if (static_cast<int>(digits.size()) >= scale) {
        return digits.substr(digits.size() - static_cast<std::size_t>(scale));
    }
    return std::string(static_cast<std::size_t>(scale) - digits.size(), '0') + digits;
}

// 展开 digits 位小数的工作量检查。
//
// 每提取一位都要把整个余数乘一次进制并按十进制位切开，代价 O(块数)。
//
// 块数按**最坏情况**估：余数始终 < 1，它的小数位不超过 scale，整数位最多
// 1 位，因此块数不超过 scale/9 + 1。用分子的块数估会严重低估 ——
// 分子的块数可能只有 1，而余数的块数会随展开线性增长。
void checkExpansionWork(const BigDecimal& value, int digits) {
    const long long blocks = static_cast<long long>(value.scale()) / 9 + 2;
    if (static_cast<long long>(digits) * blocks > kMaxExpansionWork) {
        throw std::overflow_error("toBaseString: 小数位过多，无法展开");
    }
}

// 按 base 进制逐位提取小数部分，最多 digits 位。
//
// fractionText 是提取到的数字（未去尾随零）；truncated 表示余数非零 ——
// 也就是**确实**没写完，末尾的 "..." 是如实的标记。
//
// 算法是 base 进制下的长除法：余数 R 满足 0 <= R < 10^scale，
// 每一步 T = R · base，数字位 = floor(T / 10^scale)，新余数 = T mod 10^scale。
// 除以 10^scale 就是「按十进制位切开」，块级实现是 O(块数)。
void expandFraction(const BigDecimal& value, int base, int digits, const char* table,
                    std::string& fractionText, bool& truncated) {
    const int scale = value.scale();

    // 小数部分的分子：分子绝对值的最低 scale 位十进制数字（定宽）。
    Blk remainder = lowDecimalDigits(value.numeratorBlocks(), scale);

    fractionText.clear();
    fractionText.reserve(static_cast<std::size_t>(digits));
    for (int i = 0; i < digits; ++i) {
        if (isAllZeroBlk(remainder)) break;
        multiplyBlkByBaseKeepingWidth(remainder, static_cast<std::uint64_t>(base));
        const auto split = splitDigitAndRemainder(remainder, scale, base);
        fractionText.push_back(table[split.first]);
        remainder = split.second;
    }
    truncated = !isAllZeroBlk(remainder);
}

// 小数部分默认输出多少位（base 进制）—— 只用于**无法**有限表示的情形。
//
// 目标是让输出携带的精度不少于值自身的小数位数：找出最小的 D，
// 使 base^D >= 10^scale。整除用块级运算，不依赖浮点数。
//
// 这个位数是**显示位数**，不是「精确位数」：能有限表示的值走
// exactFractionDigits，那条路径给出的是真正算出来的长度。
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
        // 小数位的**预算**（最多输出多少位）。
        //
        // 三种情形：
        //
        // 1. 调用方显式指定了 fractionDigits：那就是预算本身。值需要更多位
        //    时**确实**发生了截断，必须标记。
        //
        // 2. 自动位数且该进制下能有限表示：预算取**精确展开长度**。
        //    这正是不能用「最小的 D 使 base^D >= 10^scale」那个估算的地方 ——
        //    估算只保证位数不少于值的十进制精度，不保证展开得完。base 同时
        //    带有 2 与 5 且指数不等时（20 = 2^2·5、30 = 2·3·5），估算值会短于
        //    真实展开长度，于是输出末尾会挂上一个**并不存在**的截断标记
        //    （0.00001 在二十进制下精确等于 0.0001c，却被打成「0.0001...」）。
        //
        // 3. 自动位数且无法有限表示：展开是无限的，必须停在某个预算上，
        //    并如实标记（见 fractionDigitCount）。
        const int exactDigits = exactFractionDigits(value, base);
        int budget;
        bool exactlyRepresentable;
        if (fractionDigits > 0) {
            budget = fractionDigits;
            exactlyRepresentable = false;
        } else if (exactDigits >= 0) {
            budget = exactDigits;
            exactlyRepresentable = true;
        } else {
            budget = fractionDigitCount(value, base);
            exactlyRepresentable = false;
        }

        if (budget > 0) {
            std::string fractionText;
            bool truncated = false;
            if (base == 10 && exactlyRepresentable) {
                // 十进制是内核的原生表示，直接取数字即可，代价 O(位数)。
                fractionText = decimalFractionDigits(value);
            } else {
                checkExpansionWork(value, budget);
                expandFraction(value, base, budget, table, fractionText, truncated);
            }

            if (truncated) {
                // 发生截断时**保留全部 budget 位**：末位可能是展开中的真实
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
