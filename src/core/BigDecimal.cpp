#include "core/BigDecimal.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hpc {
namespace {

constexpr std::uint64_t kBase = 1000000000ULL;   // 10^9
constexpr int kBlockDigits = 9;

// 允许的最大有效数字位数。超过这个量级的运算量已大到不实用，
// 通常说明调用方用了一个写死的巨大精度，而不是按量的级计算。
constexpr int kMaxPrecision = 100000;

// 科学计数法的指数上限（|exponent| 的**精确**上限）。
//
// 指数为正时结果是 10^exponent 量级的整数，为负时是极小的分数。
// 两个方向都要有上限，否则 "1e999999999" 会把内存吃光。
//
// 上限取 10^6：正指数产生 100 万位整数（约 450 KB），负指数产生
// 100 万位小数 —— 都是「能算但没必要」的量级，再大就没有实用价值。
//
// 检查必须**在把当前位折进 magnitude 之后**做：先检查再折的话，
// 最后一位会把 magnitude 顶到 10 × 上限，实际接受的上限就是 10 倍。
constexpr long long kMaxExponentMagnitude = 1000000;

// 去除高位的零块，但至少保留一个块表示 0。
Blk trimBlk(Blk value) {
    while (value.size() > 1 && value.back() == 0) value.pop_back();
    if (value.empty()) value.push_back(0);
    return value;
}

// 10 的小幂表（10^0 .. 10^8）。
constexpr std::uint32_t kPow10[kBlockDigits] = {
    1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u};

// 把小端 base-10^9 块序列原地除以 10（去掉一个十进制尾随零）。
// 小端表示下，最高位在末尾，所以进位要从高位往低位传。
void divideBy10(Blk& blocks) {
    std::uint64_t remainder = 0;
    for (std::size_t i = blocks.size(); i-- > 0;) {
        const std::uint64_t current = remainder * kBase + blocks[i];
        blocks[i] = static_cast<std::uint32_t>(current / 10);
        remainder = current % 10;
    }
    blocks = trimBlk(std::move(blocks));
}

// 比较两块序列（均为小端）：<0 / 0 / >0。
int compareBlk(const Blk& lhs, const Blk& rhs) {
    if (lhs.size() != rhs.size()) return lhs.size() < rhs.size() ? -1 : 1;
    for (std::size_t i = lhs.size(); i-- > 0;) {
        if (lhs[i] != rhs[i]) return lhs[i] < rhs[i] ? -1 : 1;
    }
    return 0;
}

// a += b
void addBlk(Blk& a, const Blk& b) {
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < a.size() || i < b.size() || carry; ++i) {
        if (i >= a.size()) a.push_back(0);
        std::uint64_t sum = carry;
        if (i < a.size()) sum += a[i];
        if (i < b.size()) sum += b[i];
        a[i] = static_cast<std::uint32_t>(sum % kBase);
        carry = sum / kBase;
    }
}

// a -= b，要求 a >= b
void subBlk(Blk& a, const Blk& b) {
    std::int64_t borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::int64_t diff = static_cast<std::int64_t>(a[i]) - borrow;
        if (i < b.size()) diff -= static_cast<std::int64_t>(b[i]);
        if (diff < 0) { diff += static_cast<std::int64_t>(kBase); borrow = 1; }
        else borrow = 0;
        a[i] = static_cast<std::uint32_t>(diff);
    }
    while (a.size() > 1 && a.back() == 0) a.pop_back();
}

// a * b
Blk mulBlk(const Blk& a, const Blk& b) {
    Blk result(a.size() + b.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            std::uint64_t cur = result[i + j] + static_cast<std::uint64_t>(a[i]) * b[j] + carry;
            result[i + j] = static_cast<std::uint32_t>(cur % kBase);
            carry = cur / kBase;
        }
        std::size_t k = i + b.size();
        // 进位最多只会再向后延伸一块：真实乘积 < 10^(9*(a.size()+b.size()))。
        while (carry) {
            std::uint64_t cur = result[k] + carry;
            result[k] = static_cast<std::uint32_t>(cur % kBase);
            carry = cur / kBase;
            ++k;
        }
    }
    return trimBlk(std::move(result));
}

// 把十进制字符串切成小端 base-10^9 块。
Blk blocksFromDecimal(std::string_view text) {
    Blk blocks;
    std::size_t end = text.size();
    while (end > 0) {
        const std::size_t start = end >= kBlockDigits ? end - kBlockDigits : 0;
        std::uint32_t value = 0;
        for (std::size_t i = start; i < end; ++i) {
            value = value * 10 + static_cast<std::uint32_t>(text[i] - '0');
        }
        blocks.push_back(value);
        end = start;
    }
    return trimBlk(std::move(blocks));
}

// 把小端 base-10^9 块转成十进制数字串（不含小数点，无前导零）。
std::string blockDigitsToString(const Blk& blocks) {
    std::string digits = std::to_string(blocks.back());
    char buffer[16];
    for (std::size_t i = blocks.size() - 1; i-- > 0;) {
        std::snprintf(buffer, sizeof buffer, "%09u", static_cast<unsigned>(blocks[i]));
        digits += buffer;
    }
    return digits;
}

// 把小端 base-10^9 块转成十进制字符串，scale 为小数位数。
std::string decimalFromBlocks(const Blk& blocks, int scale) {
    std::string digits = blockDigitsToString(blocks);
    if (scale <= 0) return digits;
    if (static_cast<int>(digits.size()) <= scale) {
        digits.insert(0, static_cast<std::size_t>(scale) + 1 - digits.size(), '0');
    }
    digits.insert(digits.size() - static_cast<std::size_t>(scale), 1, '.');
    return digits;
}

// 数字串的十进制位数（块序列表示的整数位数）。
int decimalDigitCount(const Blk& blocks) {
    const int top = static_cast<int>(std::to_string(blocks.back()).size());
    return top + static_cast<int>(blocks.size() - 1) * kBlockDigits;
}

// 10^exponent 的块表示（小端）。exponent >= 0。
Blk powOfTenBlocks(int exponent) {
    Blk blocks(static_cast<std::size_t>(exponent / kBlockDigits), 0);
    blocks.push_back(kPow10[exponent % kBlockDigits]);
    return blocks;
}

// 块序列乘以 10^places（places 为十进制位数）。
// 小端表示下整体左移若干块 = 在**最低位一侧**补零。
Blk shiftLeftDecimal(Blk blocks, int places) {
    if (places <= 0) return blocks;
    const int rest = places % kBlockDigits;
    const int whole = places / kBlockDigits;
    if (rest > 0) blocks = mulBlk(blocks, Blk{kPow10[rest]});
    // 小端：索引 0 是最低位块，乘以 10^9 相当于在索引 0 处插入一个零块。
    if (whole > 0) blocks.insert(blocks.begin(), static_cast<std::size_t>(whole), 0);
    return blocks;
}

// 块序列是否表示 0。
bool isZeroBlk(const Blk& blocks) {
    return blocks.size() == 1 && blocks[0] == 0;
}

// 被丢弃部分相对「半个 ulp」的位置。
enum class DiscardTail {
    Zero,            // 没有丢弃任何非零内容
    LessThanHalf,    // 丢弃部分非零，但小于半个 ulp
    ExactlyHalf,     // 恰好半个 ulp
    GreaterThanHalf, // 大于半个 ulp
};

// 是否需要在**绝对值方向**上加一（即远离零进位）。
//
// lastKeptDigit 是保留部分的末位（银行家舍入要看它的奇偶）。
// negative 表示原值为负：Ceil/Floor 的方向与符号相关。
bool roundsUpInMagnitude(RoundingMode mode, DiscardTail tail, int lastKeptDigit,
                         bool negative) {
    switch (mode) {
        case RoundingMode::Down:
            return false;                          // 向零截断
        case RoundingMode::Up:
            return tail != DiscardTail::Zero;       // 远离零
        case RoundingMode::Ceil:
            return tail != DiscardTail::Zero && !negative;   // 向 +∞
        case RoundingMode::Floor:
            return tail != DiscardTail::Zero && negative;     // 向 -∞
        case RoundingMode::HalfUp:
            return tail == DiscardTail::ExactlyHalf ||
                   tail == DiscardTail::GreaterThanHalf;
        case RoundingMode::HalfEven:
            return tail == DiscardTail::GreaterThanHalf ||
                   (tail == DiscardTail::ExactlyHalf && (lastKeptDigit % 2 == 1));
    }
    return false;
}

// 块序列乘以 2。
Blk doubleBlk(Blk blocks) {
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const std::uint64_t current = static_cast<std::uint64_t>(blocks[i]) * 2 + carry;
        blocks[i] = static_cast<std::uint32_t>(current % kBase);
        carry = current / kBase;
    }
    if (carry) blocks.push_back(static_cast<std::uint32_t>(carry));
    return blocks;
}

// 块序列加一（小端：进位从最低位块向**最高位块**传播，即索引增大的方向）。
void incrementBlk(Blk& blocks) {
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i] < kBase - 1) {
            ++blocks[i];
            return;
        }
        blocks[i] = 0;
    }
    blocks.push_back(1);
}

// 比较 N/D 与 10^exponent，返回 <0 / 0 / >0。
int compareQuotientToPowerOfTen(const Blk& numerator, const Blk& denominator, int exponent) {
    if (exponent >= 0) {
        return compareBlk(numerator, shiftLeftDecimal(denominator, exponent));
    }
    return compareBlk(shiftLeftDecimal(numerator, -exponent), denominator);
}

// 求商的十进制指数 e：满足 10^e <= N/D < 10^(e+1)。
//
// 商可能**恰好**是 10 的幂（如 0.001 = 10^-3），因此两个边界都要检查。
int quotientExponent(const Blk& numerator, const Blk& denominator) {
    int e = decimalDigitCount(numerator) - decimalDigitCount(denominator) - 1;
    while (compareQuotientToPowerOfTen(numerator, denominator, e + 1) >= 0) ++e;
    while (compareQuotientToPowerOfTen(numerator, denominator, e) < 0) --e;
    return e;
}

}  // namespace

BigDecimal::BigDecimal() : digits_{0} {}

BigDecimal::BigDecimal(std::int64_t value) {
    if (value == 0) { sign_ = 0; digits_ = {0}; scale_ = 0; return; }
    sign_ = value < 0 ? -1 : 1;
    // 先取 -(value+1) 再加 1，避免 INT64_MIN 取负溢出。
    std::uint64_t magnitude = value < 0
        ? static_cast<std::uint64_t>(-(value + 1)) + 1
        : static_cast<std::uint64_t>(value);
    digits_.clear();
    while (magnitude > 0) {
        digits_.push_back(static_cast<std::uint32_t>(magnitude % kBase));
        magnitude /= kBase;
    }
    scale_ = 0;
    normalize();
}

BigDecimal::BigDecimal(std::string_view text) {
    *this = fromString(text);
}

BigDecimal BigDecimal::fromString(std::string_view text) {
    BigDecimal result;
    std::size_t index = 0;
    result.sign_ = 1;

    if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
        result.sign_ = (text[index] == '-') ? -1 : 1;
        ++index;
    }

    // 科学计数法：把指数部分分离出来。
    //
    // 指数之前的尾数用与原来完全相同的规则解析（可选小数点、前导零），
    // 因此**已有输入的行为不变**：不含 'e'/'E' 的串走的是同一条路径。
    //
    // 'e' 只有在**前面已经出现过数字**时才是指数标记；否则（"abe" 这类
    // 输入）它只是尾数里的一个非法字符 —— 交给下面的尾数解析去报错，
    // 错误信息才准确（"非法字符 'e'" 而不是"指数部分非法"）。
    //
    // 指数本身必须是一个十进制整数（可带正负号），否则抛 invalid_argument。
    int exponent = 0;
    bool hasExponent = false;

    // 先找到指数标记的位置：第一个「前面有数字」的 'e'/'E'。
    std::size_t exponentMark = text.size();
    bool seenDigitBefore = false;
    for (std::size_t i = index; i < text.size(); ++i) {
        const char c = text[i];
        if (c == 'e' || c == 'E') {
            if (seenDigitBefore) {
                exponentMark = i;
                break;
            }
            continue;   // 尾数里的非法字符，交给尾数解析报错
        }
        if (std::isdigit(static_cast<unsigned char>(c))) seenDigitBefore = true;
    }

    if (exponentMark != text.size()) {
        std::size_t cursor = exponentMark + 1;
        bool exponentNegative = false;
        if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-')) {
            exponentNegative = (text[cursor] == '-');
            ++cursor;
        }
        if (cursor >= text.size() || !std::isdigit(static_cast<unsigned char>(text[cursor]))) {
            throw std::invalid_argument("BigDecimal: 指数部分非法");
        }
        long long magnitude = 0;
        while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor]))) {
            // 上限保护：避免 "1e99999999999" 之类的输入把内存吃光。
            //
            // 检查放在**折进当前位之后**：先检查再折的话，最后一位会把
            // magnitude 顶到 10 × 上限，实际接受的上限就是 10 倍
            // （1e1000009 会被接受，而文档写的是 1e6）。
            const long long digit = text[cursor] - '0';
            if (magnitude > (kMaxExponentMagnitude - digit) / 10) {
                throw std::invalid_argument("BigDecimal: 指数过大");
            }
            magnitude = magnitude * 10 + digit;
            ++cursor;
        }
        // 指数后面不允许再有别的字符（"1e5e6" 必须报错而不是丢掉 "e6"）。
        if (cursor != text.size()) {
            throw std::invalid_argument("BigDecimal: 指数后面有多余的字符");
        }
        exponent = static_cast<int>(exponentNegative ? -magnitude : magnitude);
        hasExponent = true;
        text = text.substr(0, exponentMark);
    }

    std::string integral;
    std::string fractional;
    bool seenDot = false;
    bool seenDigit = false;
    for (; index < text.size(); ++index) {
        const char c = text[index];
        if (c == '.') {
            if (seenDot) throw std::invalid_argument("BigDecimal: 多个小数点");
            seenDot = true;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            throw std::invalid_argument(std::string("BigDecimal: 非法字符 '") + c + "'");
        }
        seenDigit = true;
        (seenDot ? fractional : integral).push_back(c);
    }
    if (!seenDigit) throw std::invalid_argument("BigDecimal: 没有数字");

    result.scale_ = static_cast<int>(fractional.size());
    std::string combined = integral + fractional;
    if (combined.empty()) combined = "0";
    result.digits_ = blocksFromDecimal(combined);
    result.normalize();

    // 应用指数：value * 10^exponent。
    //
    // 移位是**精确**操作（十进制下 10 的整数次幂就是移小数点），
    // 因此 1e20、2.5e-3 都能被精确表示，不引入任何舍入。
    if (hasExponent) {
        if (exponent > 0) {
            result.digits_ = shiftLeftDecimal(result.digits_, exponent);
        } else if (exponent < 0) {
            result.scale_ += -exponent;
        }
        result.normalize();
    }
    return result;
}

void BigDecimal::normalize() {
    digits_ = trimBlk(std::move(digits_));
    // 去掉小数部分的尾随零（可跨块边界），并同步收缩 scale。
    //
    // 注意：digits_ 是**小端**存储，digits_[0] 是最低位块，
    // 尾随零位于 digits_[0] 而不是 digits_.back()。
    //
    // 性能：尾随零可能成千上万（高精度除法的商形如 0.5000…0）。
    // 逐个剥离是 O(零个数 × 块数) 的二次方开销，因此先整块剥离
    // （每次去掉 9 个零），块内不足 9 个时再逐个处理。
    while (scale_ > 0 && (digits_.size() > 1 || digits_[0] != 0)) {
        // 整块剥离：最低位块为 0，说明它 9 个十进制位全为零。
        //
        // 性能：尾随零可能成千上万（高精度除法的商形如 0.5000…0）。
        // 逐块剥离每次都要移动整个数组，退化成 O(块数²)；因此先数出
        // 能剥多少块，再**一次性** erase，避免重复搬移。
        std::size_t zeroBlocks = 0;
        while (zeroBlocks + 1 < digits_.size() && digits_[zeroBlocks] == 0) ++zeroBlocks;
        const std::size_t affordable = static_cast<std::size_t>(scale_ / kBlockDigits);
        const std::size_t strip = zeroBlocks < affordable ? zeroBlocks : affordable;
        if (strip > 0) {
            digits_.erase(digits_.begin(), digits_.begin() + static_cast<std::ptrdiff_t>(strip));
            scale_ -= static_cast<int>(strip) * kBlockDigits;
            continue;
        }
        // 块内零：最低位块非零，但它的末尾可能有零（最多 8 个）。
        int zeros = 0;
        std::uint32_t probe = digits_[0];
        while (probe % 10 == 0 && zeros < kBlockDigits) { ++zeros; probe /= 10; }
        if (zeros == 0) break;
        const int removed = zeros < scale_ ? zeros : scale_;
        for (int i = 0; i < removed; ++i) divideBy10(digits_);
        scale_ -= removed;
        break;
    }
    digits_ = trimBlk(std::move(digits_));
    if (digits_.size() == 1 && digits_[0] == 0) {
        sign_ = 0;
        scale_ = 0;
    }
}

// 对齐小数位：把 scale 较小的数末尾补零，使两者 scale 相同。
int BigDecimal::alignScale(BigDecimal& lhs, BigDecimal& rhs) {
    if (lhs.scale_ == rhs.scale_) return lhs.scale_;
    if (lhs.scale_ < rhs.scale_) {
        const int delta = rhs.scale_ - lhs.scale_;
        lhs.digits_ = mulBlk(lhs.digits_, blocksFromDecimal("1" + std::string(static_cast<std::size_t>(delta), '0')));
        lhs.scale_ += delta;
    } else {
        const int delta = lhs.scale_ - rhs.scale_;
        rhs.digits_ = mulBlk(rhs.digits_, blocksFromDecimal("1" + std::string(static_cast<std::size_t>(delta), '0')));
        rhs.scale_ += delta;
    }
    return lhs.scale_;
}

BigDecimal BigDecimal::operator+(const BigDecimal& other) const {
    BigDecimal lhs = *this;
    BigDecimal rhs = other;
    alignScale(lhs, rhs);

    BigDecimal result;
    result.scale_ = lhs.scale_;
    if (lhs.sign_ == 0) { result = rhs; result.normalize(); return result; }
    if (rhs.sign_ == 0) { result = lhs; result.normalize(); return result; }

    if (lhs.sign_ == rhs.sign_) {
        result.digits_ = lhs.digits_;
        addBlk(result.digits_, rhs.digits_);
        result.sign_ = lhs.sign_;
    } else {
        const int cmp = compareBlk(lhs.digits_, rhs.digits_);
        if (cmp == 0) { result.sign_ = 0; result.digits_ = {0}; }
        else if (cmp > 0) {
            result.digits_ = lhs.digits_;
            subBlk(result.digits_, rhs.digits_);
            result.sign_ = lhs.sign_;
        } else {
            result.digits_ = rhs.digits_;
            subBlk(result.digits_, lhs.digits_);
            result.sign_ = rhs.sign_;
        }
    }
    result.normalize();
    return result;
}

BigDecimal BigDecimal::operator-(const BigDecimal& other) const {
    BigDecimal negated = other;
    if (negated.sign_ != 0) negated.sign_ = -negated.sign_;
    return *this + negated;
}

BigDecimal BigDecimal::operator*(const BigDecimal& other) const {
    BigDecimal result;
    result.scale_ = scale_ + other.scale_;
    result.digits_ = mulBlk(digits_, other.digits_);
    result.sign_ = sign_ * other.sign_;
    result.normalize();
    return result;
}

int BigDecimal::compare(const BigDecimal& other) const {
    BigDecimal lhs = *this;
    BigDecimal rhs = other;
    alignScale(lhs, rhs);
    if (lhs.sign_ != rhs.sign_) return lhs.sign_ < rhs.sign_ ? -1 : 1;
    if (lhs.sign_ == 0) return 0;
    const int cmp = compareBlk(lhs.digits_, rhs.digits_);
    return lhs.sign_ > 0 ? cmp : -cmp;
}

bool BigDecimal::equals(const BigDecimal& other) const { return compare(other) == 0; }

BigDecimal BigDecimal::abs(const BigDecimal& value) {
    BigDecimal result = value;
    if (result.sign_ != 0) result.sign_ = 1;
    return result;
}

std::string BigDecimal::toString() const {
    if (sign_ == 0) return "0";
    std::string body = decimalFromBlocks(digits_, scale_);
    return sign_ < 0 ? "-" + body : body;
}

bool BigDecimal::isZero() const { return sign_ == 0; }

const std::vector<std::uint32_t>& BigDecimal::integerBlocks() const {
    if (scale_ != 0) throw std::domain_error("BigDecimal: 该运算只支持整数");
    return digits_;
}

BigDecimal BigDecimal::truncateToInteger() const {
    if (scale_ == 0) return *this;

    // 向零截断 = 取绝对值后丢掉最低的 scale 个十进制位。
    //
    // 性能：逐位 divideBy10 是 O(scale × 块数) 的二次方开销 ——
    // 高精度除法的结果 scale 可达上万位（块数千个），逐位除会退化。
    // 因此分两步：
    //   1) 丢掉最低 scale/9 个**整块**（一次 erase，O(块数)）；
    //   2) 剩下的 scale%9 位用 divideBy10 逐位去掉（最多 8 次，O(块数)）。
    // 合计 O(块数)，与 scale 的大小基本无关。
    BigDecimal magnitude = abs(*this);

    const int wholeBlocks = scale_ / kBlockDigits;
    const int rest = scale_ % kBlockDigits;

    // 第一步：丢掉最低的整块。
    //
    // 正确性：值 = 低位部分 + 高位部分 × 10^(9w)，而低位部分 < 10^(9w)，
    // 因此 floor(值 / 10^(9w)) 恰好等于高位部分。
    const std::size_t drop = std::min(static_cast<std::size_t>(wholeBlocks),
                                      magnitude.digits_.size());
    Blk kept(magnitude.digits_.begin() + static_cast<std::ptrdiff_t>(drop),
             magnitude.digits_.end());
    if (kept.empty()) kept.push_back(0);

    // 第二步：再丢掉 rest 个十进制位（rest < 9）。
    //
    // 注意**不能**只把最低块除以 10^rest：块边界与十进制位边界不对齐时
    // （例如 9999999999 = 9×10^9 + 999999999），除以 10^5 会让**所有**块
    // 一起右移 5 位，而不只是最低块。因此这里用整块级的长除法逐位除。
    for (int i = 0; i < rest; ++i) {
        divideBy10(kept);
    }

    BigDecimal result;
    result.digits_ = std::move(kept);
    result.scale_ = 0;
    result.sign_ = sign_;
    result.normalize();

    // 符号跟随原值：向零截断，-1.5 -> -1（不是 floor 的 -2）。
    return result;
}

// --- 除法、舍入与取整 ---

// 逐块长除法 + 二分试商。
// 返回 {商, 余数}，均为小端 base-10^9 块。
std::pair<Blk, Blk> BigDecimal::divideBlk(const Blk& dividend, const Blk& divisor) {
    Blk remainder{0};
    Blk quotient;               // 先按最高位在前累积，最后反转
    quotient.reserve(dividend.size());

    for (std::size_t i = dividend.size(); i-- > 0;) {
        // remainder = remainder * 10^9 + dividend[i]
        std::uint64_t carry = dividend[i];
        for (std::size_t k = 0; k < remainder.size(); ++k) {
            const std::uint64_t cur = static_cast<std::uint64_t>(remainder[k]) * kBase + carry;
            remainder[k] = static_cast<std::uint32_t>(cur % kBase);
            carry = cur / kBase;
        }
        // 进位必须追加到**最高位**（小端表示的末尾），不能 insert 到开头。
        if (carry) remainder.push_back(static_cast<std::uint32_t>(carry));
        remainder = trimBlk(std::move(remainder));

        // 二分试商：找出最大的 q 使得 divisor * q <= remainder。
        std::uint64_t low = 0, high = kBase - 1, digit = 0;
        while (low <= high) {
            const std::uint64_t mid = low + (high - low) / 2;
            const Blk trial = mulBlk(divisor, Blk{static_cast<std::uint32_t>(mid)});
            if (compareBlk(trial, remainder) <= 0) {
                digit = mid;
                low = mid + 1;
            } else {
                if (mid == 0) break;
                high = mid - 1;
            }
        }
        if (digit) {
            const Blk trial = mulBlk(divisor, Blk{static_cast<std::uint32_t>(digit)});
            subBlk(remainder, trial);
        }
        quotient.push_back(static_cast<std::uint32_t>(digit));
    }

    // 商在累积时是**最高位在前**，必须反转成小端。
    std::reverse(quotient.begin(), quotient.end());
    quotient = trimBlk(std::move(quotient));
    return {std::move(quotient), std::move(remainder)};
}

// 把精确分数 N/D 舍入到 p 位有效数字。
//
// 这是舍入的唯一入口：除法与 roundToPrecision 都走这里。
//
// 关键点：用**长除法得到的精确余数**判断被丢弃部分与半个 ulp 的关系，
// 而不是"多算几位再猜"。因此「正好一半」（银行家舍入要走偶数分支）
// 与「超过一半」永远能被正确区分 —— 这正是 brief 里固定保护位数做法做不到的。
//
// negative 表示原值为负：Ceil/Floor 的进位方向与符号相关。
std::pair<Blk, int> BigDecimal::roundQuotient(const Blk& numerator, const Blk& denominator,
                                               int precision, RoundingMode mode, bool negative) {
    const int exponent = quotientExponent(numerator, denominator);
    // 需要把商放大到 10^(precision-1-exponent) 才能取到 precision 位有效数字。
    const int shift = precision - 1 - exponent;

    Blk scaledNumerator;
    Blk scaledDenominator;
    if (shift >= 0) {
        scaledNumerator = shiftLeftDecimal(numerator, shift);
        scaledDenominator = denominator;
    } else {
        scaledNumerator = numerator;
        scaledDenominator = shiftLeftDecimal(denominator, -shift);
    }

    const auto divided = divideBlk(scaledNumerator, scaledDenominator);
    Blk quotient = divided.first;
    const Blk& remainder = divided.second;

    // 余数 / 除数 与 0.5 的关系。
    DiscardTail tail;
    if (isZeroBlk(remainder)) {
        tail = DiscardTail::Zero;
    } else {
        const int cmp = compareBlk(doubleBlk(remainder), scaledDenominator);
        tail = cmp > 0 ? DiscardTail::GreaterThanHalf
             : cmp == 0 ? DiscardTail::ExactlyHalf
                        : DiscardTail::LessThanHalf;
    }

    const std::string quotientDigits = blockDigitsToString(quotient);
    const int lastKeptDigit = quotientDigits.back() - '0';
    if (roundsUpInMagnitude(mode, tail, lastKeptDigit, negative)) {
        incrementBlk(quotient);
    }

    // 值 = 商 * 10^(exponent+1-precision)，改写成 整数 * 10^(-小数位数)。
    int scale = precision - 1 - exponent;
    if (scale < 0) {
        quotient = shiftLeftDecimal(std::move(quotient), -scale);
        scale = 0;
    }
    return {std::move(quotient), scale};
}

BigDecimal BigDecimal::divide(const BigDecimal& lhs, const BigDecimal& rhs,
                               const Context& context) {
    if (rhs.sign_ == 0) throw std::domain_error("BigDecimal: 除数为零");
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");
    // 上限：precision 直接决定运算量，写死一个巨大值（例如 1000000）会让
    // 单次除法做十万位运算，实测以分钟计。真正的需求应按量的级给精度，
    // 而不是靠一个荒谬的上限来"保险"。
    if (context.precision > kMaxPrecision) {
        throw std::invalid_argument("BigDecimal: precision 超出上限");
    }
    if (lhs.sign_ == 0) return BigDecimal();

    // 被除数与除数先对齐到共同的小数位：
    // (L·10^-a) / (R·10^-b) = (L·10^(k-a)) / (R·10^(k-b))，其中 k = max(a, b)。
    const int commonScale = std::max(lhs.scale_, rhs.scale_);
    const Blk numerator = shiftLeftDecimal(lhs.digits_, commonScale - lhs.scale_);
    const Blk denominator = shiftLeftDecimal(rhs.digits_, commonScale - rhs.scale_);

    const auto rounded = roundQuotient(numerator, denominator, context.precision,
                                       context.mode, lhs.sign_ * rhs.sign_ < 0);

    BigDecimal result;
    result.digits_ = rounded.first;
    result.scale_ = rounded.second;
    result.sign_ = lhs.sign_ * rhs.sign_;
    result.normalize();
    return result;
}

// 舍入到 context.precision 位有效数字。
//
// 走"把精确分数交给 roundQuotient"的路径：舍入判定基于长除法得到的
// **精确余数**，而不是"多算几位再猜"。因此「正好一半」与「超过一半」
// 永远能被正确区分 —— 银行家舍入的偶数分支才可靠。
//
// 结构安全：全程只用块数组与数字串，不做任何"向前回退一格"的链式操作，
// 因此单块/单元素结构天然安全（legacy 缺陷 #5 与 #10 的共同根因）。
BigDecimal BigDecimal::roundToPrecision(const BigDecimal& value, const Context& context) {
    if (value.sign_ == 0) return value;
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");
    // 上限：precision 直接决定运算量，写死一个巨大值（例如 1000000）会让
    // 单次除法做十万位运算，实测以分钟计。真正的需求应按量的级给精度，
    // 而不是靠一个荒谬的上限来"保险"。
    if (context.precision > kMaxPrecision) {
        throw std::invalid_argument("BigDecimal: precision 超出上限");
    }

    // 把精确值表示成分数：numerator/denominator = value。
    // value = digits * 10^-scale，所以分母是 10^scale，分子是 digits。
    const Blk numerator = value.digits_;
    const Blk denominator = powOfTenBlocks(value.scale_);

    const auto rounded = roundQuotient(numerator, denominator, context.precision,
                                       context.mode, value.sign_ < 0);

    BigDecimal result;
    result.digits_ = rounded.first;
    result.scale_ = rounded.second;
    result.sign_ = value.sign_;
    result.normalize();
    return result;
}

BigDecimal BigDecimal::floor(const BigDecimal& value, const Context& context) {
    return roundToPrecision(value, Context{context.precision, RoundingMode::Floor});
}

BigDecimal BigDecimal::ceil(const BigDecimal& value, const Context& context) {
    return roundToPrecision(value, Context{context.precision, RoundingMode::Ceil});
}

BigDecimal BigDecimal::round(const BigDecimal& value, const Context& context) {
    return roundToPrecision(value, Context{context.precision, RoundingMode::HalfEven});
}

BigDecimal BigDecimal::modulo(const BigDecimal& lhs, const BigDecimal& rhs) {
    if (rhs.sign_ == 0) throw std::domain_error("BigDecimal: 取余的除数为零");
    if (lhs.sign_ == 0) return BigDecimal();

    // 取余按**绝对值**做除法，结果符号跟随被除数（与 C 的 % 一致）。
    const BigDecimal dividend = abs(lhs);
    const BigDecimal divisor = abs(rhs);

    // 对齐到共同的小数位，把两个数都变成整数：
    // 被除数 = numerator / 10^commonScale，除数 = denominator / 10^commonScale。
    const int commonScale = std::max(dividend.scale_, divisor.scale_);
    const Blk numerator = shiftLeftDecimal(dividend.digits_, commonScale - dividend.scale_);
    const Blk denominator = shiftLeftDecimal(divisor.digits_, commonScale - divisor.scale_);

    // 整数长除法：商向下取整（向零），余数即结果。
    const auto divided = divideBlk(numerator, denominator);

    BigDecimal remainder;
    remainder.digits_ = divided.second;
    remainder.scale_ = commonScale;
    remainder.sign_ = lhs.sign_ < 0 ? -1 : 1;
    remainder.normalize();
    return remainder;
}
// --- 超越函数 ---

namespace {

// 超越函数工作精度相对用户精度的保护位。
//
// 关键区分：**有效数字位**与**十进制小数位**是两个概念。
// Context::precision 是有效数字位（与 Python decimal 的 prec 同义），
// 与数值量级无关。因此保护位只能加在有效位上 ——
// brief 原实现把 precision 当成小数位数用（scaled = value * 10^(2*guard)），
// 对量级偏离 1 很大的输入会算错位数。
constexpr int kGuardDigits = 10;

// 内部工作精度相对用户精度至少要多出的位数（低于此值时用此值兜底）。
constexpr int kMinWorkPrecision = 24;

// exp 的参数上限：|x| >= 10^5 时结果已有约 4.3 万位十进制数，
// 再大则舍入本身的开销已不可接受。正负参数同此界 ——
// 负参数的 exp 结果同样有 |x|/ln(10) 位的小数。
constexpr int kMaxExpArgumentExponent = 5;

// exp 里用来**估计 k 的量级**的探针精度。
//
// k = floor(x / ln(10))，|x| < 10^5 时 |k| < 43427 —— 所以 20 位探针
// 足以把 k 定死（差 1 位会在后面被修正回来，见 expInternal）。
constexpr int kExpProbePrecision = 20;

// 超越函数的精度上限。
//
// 与 divide / roundToPrecision 的 kMaxPrecision 不同：超越函数的
// 工作精度是**平方级**的。实测（本机 GCC -O2，最坏情况是 pow(2, 0.5)）：
//
//   p=200  →    413 ms
//   p=400  →   3.4 s
//   p=800  →    22 s
//   p=1600 →   151 s
//   p=1000（exp）→  23 s
//
// 也就是说 kMaxPrecision = 100000 对超越函数而言远远不够 ——
// 它们在 p=1600 时就已经要两分半，p=99990 更是永远算不完。
//
// 所以超越函数单独用一个上限，取 1000：最坏情况约 40 秒，
// 属于「可接受的慢」而不是「永远算不完」。
//
// 这是**产品决策**而非数学限制 —— 真正需要更高精度的调用方
// 应该分批求值（例如先用 1000 位求出前导部分再细化）。
constexpr int kMaxTranscendentalPrecision = 1000;

// 整数幂的指数上限。
constexpr long long kMaxIntegerPower = 100000;

// 整数幂结果的位数上限（防止 10^100000 这类天文数字）。
//
// 上限按**结果的十进制位数**估算，整数位与小数位都算在内。
//
// 估算式：结果位数 <= power × (底数的有效位数 + 底数的小数位数)。
//
// 底数 |b| 有 s 位有效数字、t 位小数时，|b| ∈ [10^(s-1-t), 10^(s-t))，
// 于是 |b^power| 的位数不超过 power × (s + t)：
//   - 值 >= 1 时：整数位 <= power × (s - t) <= power × (s + t)
//   - 值 <  1 时：小数位 <= power × (t - s + 1) <= power × (s + t)
//
// 所以这个估算**总是 >= 实际位数**（最多高估 3 倍），上限因此是硬约束。
//
// 只看底数的整数位数会完全漏掉小数底数：0.001 的整数位数是 -2，
// 那个「整数位数 > 0」的判据会把整个护栏跳过，而结果的**小数位数**
// 恰恰是按 power 线性增长的 —— 0.001^100000 需要 30 万位小数。
//
// 实测（本机 GCC -O2，precision=20）：
//
//   pow(2, 100000)              511 ms     结果 3 万位
//   pow(3, 100000)             1325 ms     结果 4.8 万位
//   pow(10, 100000)            8041 ms     结果 10 万位
//   pow(0.001, 100000)        67598 ms     结果 30 万位小数
//
// 30 万位已经要一分多钟，所以上限取 30 万：
// 估算 <= 30 万 ⟹ 实际位数 <= 30 万 ⟹ 单次不超过一分多钟。
constexpr int kMaxPowerResultDigits = 300000;

// 十进制整数的最大位数（用于 long long 溢出保护）。
constexpr int kMaxIntegerDigits = 18;

constexpr std::int64_t kMaxInt64 = 9223372036854775807LL;

// floor(e / 2)：C++ 的整数除法向零取整，负数需要修正。
int floorHalf(int e) { return e >= 0 ? e / 2 : (e - 1) / 2; }

}  // namespace

// 值的十进制指数 e：10^e <= |value| < 10^(e+1)。
int BigDecimal::decimalExponent() const {
    if (sign_ == 0) return 0;
    // digits_ 是小端：最高位块在末尾。
    // 整数部分的十进制位数 = 总位数 - 小数位数。
    const int integerDigits = decimalDigitCount(digits_) - scale_;
    // 10^e <= |v| < 10^(e+1) 的 e = 位数 - 1。
    return integerDigits - 1;
}

// 精确地做 value * 10^exponent。
//
// 正指数：把块序列左移若干位，digits_ 不变、scale_ 不变，完全精确。
// 负指数：value / 10^k = digits * 10^-(scale+k)，digits_ 不变、scale_ 增大，
//         同样完全精确（十进制的 10 的负整数次幂就是移小数点）。
BigDecimal BigDecimal::shiftByPowerOfTen(const BigDecimal& value, int exponent) {
    if (exponent == 0 || value.sign_ == 0) return value;

    BigDecimal result = value;
    if (exponent > 0) {
        result.digits_ = shiftLeftDecimal(result.digits_, exponent);
    } else {
        result.scale_ += -exponent;
    }
    result.normalize();
    return result;
}

// 精确地乘以 2。
BigDecimal BigDecimal::multiplyByTwo(const BigDecimal& value) {
    BigDecimal result = value;
    result.digits_ = doubleBlk(result.digits_);
    result.normalize();
    return result;
}

// 精确地除以 2。
//
// 十进制下 x/2 通常不终止，但 x/2 = (x*5)/10 总是精确的：
// 先乘 5（整数运算），再把小数位加一位（*10^-1）。
BigDecimal BigDecimal::divideByTwo(const BigDecimal& value) {
    BigDecimal result = value * BigDecimal(5);
    result.scale_ += 1;
    result.normalize();
    return result;
}

// 向下取整到整数（scale 归零）。
BigDecimal BigDecimal::floorToInteger(const BigDecimal& value) {
    if (value.scale_ == 0) return value;

    // 先按绝对值截断小数部分。
    BigDecimal magnitude = abs(value);
    for (int i = 0; i < magnitude.scale_; ++i) divideBy10(magnitude.digits_);
    magnitude.scale_ = 0;
    magnitude.normalize();

    // 截断后与原值相等：本来就是整数。
    if (magnitude.compare(abs(value)) == 0) {
        return value.sign_ < 0 ? BigDecimal(0) - magnitude : magnitude;
    }
    // 有被丢掉的小数部分：正数向下取整就是截断值；负数要再往下走一格。
    if (value.sign_ < 0) return BigDecimal(0) - (magnitude + BigDecimal(1));
    return magnitude;
}

// scale 为 0 的整数值转成 long long。
long long BigDecimal::toInteger(const BigDecimal& value) {
    if (value.scale_ != 0) throw std::invalid_argument("BigDecimal: 期望整数值");
    if (value.sign_ == 0) return 0;

    const std::string text = value.toString();
    const bool negative = text[0] == '-';
    const std::size_t start = negative ? 1 : 0;
    if (text.size() - start > static_cast<std::size_t>(kMaxIntegerDigits)) {
        throw std::overflow_error("BigDecimal: 整数超出 long long 范围");
    }
    long long magnitude = 0;
    for (std::size_t i = start; i < text.size(); ++i) {
        const int digit = text[i] - '0';
        if (magnitude > (kMaxInt64 - digit) / 10) {
            throw std::overflow_error("BigDecimal: 整数超出 long long 范围");
        }
        magnitude = magnitude * 10 + digit;
    }
    return negative ? -magnitude : magnitude;
}

// 精确的整数幂 base^power（power >= 1）。
BigDecimal BigDecimal::integerPower(const BigDecimal& base, long long power) {
    if (power < 0) throw std::invalid_argument("BigDecimal: 整数幂的指数必须非负");

    // 结果位数的估算：底数的**整数位数 + 小数位数**再乘以指数。
    //
    // 只看整数位数是不够的：底数小于 1 时（base = 0.001 的整数位数为 -2），
    // 那个「整数位数 > 0」的判据会把整个防护跳过，而结果的**小数位数**
    // 恰恰是按 power 线性增长的 —— 0.001^100000 需要 30 万位小数。
    //
    // 底数 |b| 的十进制位数记为 d（整数部分位数 + 小数位数），
    // 则 |b^power| 的十进制位数约等于 power × d。
    const int baseDigits = decimalDigitCount(base.digits_);
    const int baseScale = base.scale_;
    const long long resultDigits =
        power * static_cast<long long>(baseDigits + baseScale);
    if (resultDigits > static_cast<long long>(kMaxPowerResultDigits)) {
        throw std::overflow_error("BigDecimal: 幂的结果位数过大");
    }

    BigDecimal result(1);
    BigDecimal factor = base;
    long long remaining = power;
    while (remaining > 0) {
        if (remaining & 1) result = result * factor;
        remaining >>= 1;
        if (remaining > 0) factor = factor * factor;
    }
    return result;
}

// atanh 级数：z + z^3/3 + z^5/5 + ...
//
// atanh(z) = 0.5 * ln((1+z)/(1-z))，故 ln(x) = 2 * atanh((x-1)/(x+1))。
// |z| <= 1/3 时每项约按 1/9 的比例衰减，收敛很快；同号项相加，无抵消。
BigDecimal BigDecimal::atanhSeries(const BigDecimal& z, const Context& work) {
    // 收敛判据：加数绝对值小于 10^-(work.precision+2) 即停止 ——
    // 比要求的精度再低两位，保证末位不受截断影响。
    const BigDecimal epsilon = shiftByPowerOfTen(BigDecimal(1), -(work.precision + 2));

    // **每一项都要限制在工作精度内**，否则小数位会无限增长：
    // z 的小数位约为 work.precision，而 z*z 的小数位是它的两倍，
    // 第 k 次迭代的 power 小数位达到 (2k+1) * work.precision。
    // 在 work.precision = 110、k = 230 时中间数会有 5 万位十进制数 ——
    // 除法的开销随之从毫秒级涨到几十秒。
    //
    // 舍入到 work.precision 位有效数字是安全的：级数的项单调递减，
    // 每一项的**相对**精度才是关键，截掉的是远低于 ulp 的低位。
    const BigDecimal zRounded = roundToPrecision(z, work);
    const BigDecimal zSquared = roundToPrecision(zRounded * zRounded, work);

    BigDecimal power = zRounded;             // z^(2k+1)
    BigDecimal sum = zRounded;
    // 项数上界：|z| <= 1/3 时 z^(2k+1) <= 3^-(2k+1)，需要约 1.05 * precision 项。
    for (int k = 1; k <= work.precision * 2 + 8; ++k) {
        power = roundToPrecision(power * zSquared, work);
        const BigDecimal addend = divide(power, BigDecimal(2 * k + 1),
                                         Context{work.precision, RoundingMode::HalfEven});
        if (addend.isZero()) break;
        sum = sum + addend;
        if (abs(addend).compare(epsilon) < 0) break;
    }
    return sum;
}

// exp(g) 的泰勒级数：1 + g + g^2/2! + g^3/3! + ...
//
// 调用方保证 0 <= g < ln(10) ≈ 2.303，项全部为正 —— 没有抵消，数值稳定。
// 需要的项数约 1.4 * precision。
BigDecimal BigDecimal::expSeries(const BigDecimal& g, const Context& work) {
    const BigDecimal epsilon = shiftByPowerOfTen(BigDecimal(1), -(work.precision + 2));

    // 同样要把中间结果限制在工作精度内：term 的小数位会随迭代增长，
    // 不限制的话 exp(1) 在 100 位精度下也会拖出几秒。
    const BigDecimal gRounded = roundToPrecision(g, work);

    BigDecimal term(1);
    BigDecimal sum(1);
    for (int n = 1; n <= work.precision * 3 + 16; ++n) {
        term = roundToPrecision(term * gRounded, work);
        term = divide(term, BigDecimal(n), Context{work.precision, RoundingMode::HalfEven});
        if (term.isZero()) break;
        sum = sum + term;
        if (abs(term).compare(epsilon) < 0) break;
    }
    return sum;
}

// ln(2)：按 work 精度实时计算。
//
// ln(2) = 2 * atanh(1/3)。
//
// 不用硬编码常量：brief 里的 60 位常量在 precision > 60 时不够用，
// 而高精度计算器正是本项目的存在意义。
BigDecimal BigDecimal::ln2Constant(const Context& work) {
    const BigDecimal z = divide(BigDecimal(1), BigDecimal(3),
                                Context{work.precision, RoundingMode::HalfEven});
    return multiplyByTwo(atanhSeries(z, work));
}

// ln(10)：按 work 精度实时计算。
//
// ln(10) = 3*ln(2) + ln(5/4)，其中 ln(5/4) = 2*atanh(1/9)。
//
// ln2 由调用方传入：一次 ln 运算里 ln(2) 会被用到好几处，
// 重算一遍 atanh 级数的代价不小。
BigDecimal BigDecimal::ln10Constant(const BigDecimal& ln2, const Context& work) {
    const BigDecimal z = divide(BigDecimal(1), BigDecimal(9),
                                Context{work.precision, RoundingMode::HalfEven});
    const BigDecimal lnOneAndAQuarter = multiplyByTwo(atanhSeries(z, work));
    return ln2 * BigDecimal(3) + lnOneAndAQuarter;
}

// 把 value 分解成 mantissa * 10^exponent，其中 mantissa ∈ [1, 2)。
//
// 具体地：先取 exponent = value 的十进制指数（10^e <= |v| < 10^(e+1)），
// 再把尾数反复折半。halvings 记录折半次数，
// 于是 原尾数 = mantissa * 2^halvings。
//
// 移位与折半都是精确操作，不引入任何舍入。
BigDecimal BigDecimal::decomposeForLog(const BigDecimal& value, int& exponent, int& halvings) {
    exponent = value.decimalExponent();
    BigDecimal mantissa = shiftByPowerOfTen(value, -exponent);   // ∈ [1, 10)
    halvings = 0;
    while (mantissa.compare(BigDecimal(2)) >= 0) {
        mantissa = divideByTwo(mantissa);
        ++halvings;
    }
    return mantissa;                                            // ∈ [1, 2)
}

// exp 的内部实现。
//
// 分解：x = k*ln(10) + g，k = floor(x / ln10)，g ∈ [0, ln10)。
// 于是 exp(x) = 10^k * exp(g)，exp(g) 的级数参数恒在 [0, 2.31) 以内 ——
// 收敛快，且与 |x| 的量级无关。
//
// 该恒等式对**任意**整数 k 都成立，所以 k 取整差一位也只是让 g 靠近 0 或
// ln(10)，不影响正确性 —— 但 g 的精度会受影响，见下面的 extraDigits。
//
// ln(10) 必须比工作精度再高若干位：g = x - k*ln(10) 是**大数相减**，
// 误差约为 |k| * 10^-(work.precision+extra)。要让它小于 10^-work.precision，
// extra 必须大于 |k| 的十进制指数。
BigDecimal BigDecimal::expInternal(const BigDecimal& value, const Context& work,
                                   const Context& final) {
    if (value.isZero()) return BigDecimal(1);

    // exp 的参数过大时结果的位数不可控，直接报溢出。
    const int magnitude = value.decimalExponent();
    if (magnitude >= kMaxExpArgumentExponent) {
        throw std::overflow_error("BigDecimal: exp 的参数过大，结果超出可计算范围");
    }

    // 多算的位数按 **|k|** 定，其中 k = floor(x / ln(10))。
    //
    // g = x - k*ln(10) 里被抵消掉的量级是 |k*ln(10)| ≈ |k|，
    // 所以「要保护几位」由 |k| 决定，**与 x 的量级无关**。
    //
    // 按 |x| 的十进制指数给保护位是错的：x 极小时（1e-200）k = 0，
    // 根本没有抵消，一位保护位都不需要，而那个公式会多算 205 位
    // （实测 exp(1e-200) 要 271 ms，exp(1e-99000) 更是永远算不完）。
    //
    // |x| < 1 时 k 直接可定，**完全绕开除法**：
    //   x >= 0 → x/ln(10) ∈ [0, 0.435) → k = 0
    //   x <  0 → x/ln(10) ∈ (-0.435, 0) → k = -1
    //
    // 绕开除法不是可有可无的优化：除法要先对齐小数位，而长除法是
    // O(位数²) —— x 的小数位有 99000 个时，实测 exp(1e-99000) 要 45 秒。
    //
    // 走这条支与走除法的结果**完全一致**：除法得到的商四舍五入后仍在
    // (-0.435, 0.435) 内，floor 必然是同一个整数，g 也逐位相同。
    const bool tinyArgument = magnitude < 0;

    // |k| 的十进制位数 —— 保护位数的依据。
    int kDigits = 0;
    BigDecimal k;
    if (tinyArgument) {
        k = value.sign_ < 0 ? BigDecimal(-1) : BigDecimal(0);
        kDigits = k.isZero() ? 0 : 1;
    } else {
        // |x| >= 1 时 k 要靠除法得到。先用 20 位探针估 |k| 的**量级**
        // （探针只用来量级，不参与最终计算）：
        // |x| < 10^5 时 |k| < 43427，所以 20 位探针足够把量级定死。
        const Context probe{kExpProbePrecision, RoundingMode::HalfEven};
        const BigDecimal probeLn10 = ln10Constant(ln2Constant(probe), probe);
        const BigDecimal probeQuotient = divide(value, probeLn10,
                                                Context{probe.precision + 3, RoundingMode::HalfEven});
        const BigDecimal probeK = floorToInteger(probeQuotient);
        kDigits = probeK.isZero() ? 0 : decimalDigitCount(abs(probeK).digits_);
    }

    // |k| 的十进制位数再加 5 位保护位。k = 0 时没有任何抵消，一位都不需要。
    const int extraDigits = std::max(2, kDigits + 5);
    const Context precise{work.precision + extraDigits, RoundingMode::HalfEven};

    const BigDecimal ln10 = ln10Constant(ln2Constant(precise), precise);

    if (!tinyArgument) {
        // k = floor(x / ln10)。除法要更高精度，否则 k 的整数部分可能因
        // 末位舍入而差 1 —— 那会让 g 从 0 跳到 ln10，级数慢一个量级。
        const BigDecimal quotient = divide(value, ln10,
                                           Context{precise.precision + 3, RoundingMode::HalfEven});
        k = floorToInteger(quotient);
    }

    BigDecimal g = value - k * ln10;

    // 把 g 拉回 [0, ln10)。恒等式 exp(x) = 10^k * exp(g) 仍然成立。
    if (g.sign_ < 0) {
        k = k - BigDecimal(1);
        g = g + ln10;
    } else if (g.compare(ln10) >= 0) {
        k = k + BigDecimal(1);
        g = g - ln10;
    }

    // exp(g)：g ∈ [0, ln10)。
    const BigDecimal mantissa = expSeries(g, work);

    const long long power = toInteger(k);
    if (power > static_cast<long long>(std::numeric_limits<int>::max()) ||
        power < static_cast<long long>(std::numeric_limits<int>::min())) {
        throw std::overflow_error("BigDecimal: exp 的参数过大，结果超出可计算范围");
    }

    // 还原：乘以 10^k。k 为负时是移小数点，同样精确。
    //
    // 顺序很关键：**先移位、后舍入**。移位是精确的位操作，先移位再按最终
    // 精度舍入，结果与「先舍入再移位」数值相同，但表示更干净 ——
    // 后者会在尾数里留下尾随零（精度 20 时 exp(100) 会打印成
    // 26881171418161354484000000000000000000000000）。
    //
    // 那串零是**精度 20 的真实值**（Python decimal 在 prec=20 下给出同一个
    // 数），不是「先舍入再移位」的 bug 产物 —— 它只是比 20 位有效数字
    // 多出来的尾随零。精度 50 时两者一致：
    // 26881171418161354484126255515800135873611118.773742
    // （与 Python decimal prec=50 完全相同）。
    const BigDecimal scaled = shiftByPowerOfTen(mantissa, static_cast<int>(power));
    return roundToPrecision(scaled, final);
}

// ln 的内部实现。
//
// 两条路径：
//   1. x ∈ [1/2, 2]：直接用 atanh 级数。z = (x-1)/(x+1) ∈ [-1/3, 1/3]，
//      收敛最快，且**没有抵消**（结果接近 0 时 z 也接近 0，相对精度保持）。
//   2. 其他：写成 x = m * 10^e，m ∈ [1, 10)。ln(x) = e*ln(10) + ln(m)。
//      ln(m) 先把 m 折半到 [1, 2)：m = r * 2^j，则 ln(m) = j*ln(2) + ln(r)。
//
// 路径 2 在 x 靠近 1 时会有抵消（-ln10 + ln(m)），所以路径 1 必须覆盖
// 「结果接近 0」的全部情形 —— 那正是 x ∈ [1/2, 2] 的内部。
//
// 路径 2 内部也不存在抵消：只有 m >= 2 才会出现 j*ln(2) 这一项，
// 而那时 ln(m) >= ln(2)，两者量级可比，结果量级不小于 ln(2)。
BigDecimal BigDecimal::lnInternal(const BigDecimal& value, const Context& work) {
    const BigDecimal one(1);
    if (value.equals(one)) return BigDecimal(0);

    const BigDecimal half = BigDecimal::fromString("0.5");
    const BigDecimal two(2);

    // 路径 1：靠近 1 的直接展开。
    if (value.compare(half) >= 0 && value.compare(two) <= 0) {
        const BigDecimal z = divide(value - one, value + one,
                                    Context{work.precision, RoundingMode::HalfEven});
        return multiplyByTwo(atanhSeries(z, work));
    }

    // 路径 2：量级分解。
    int exponent = 0;
    int halvings = 0;
    const BigDecimal mantissa = decomposeForLog(value, exponent, halvings);

    const BigDecimal ln2 = ln2Constant(work);
    const BigDecimal z = divide(mantissa - one, mantissa + one,
                                Context{work.precision, RoundingMode::HalfEven});
    BigDecimal result = multiplyByTwo(atanhSeries(z, work));
    result = result + ln2 * BigDecimal(halvings);

    // ln(x) = e*ln(10) + ln(m)。
    //
    // 这里 e*ln(10) 与 ln(m) 的**相对**误差都约 10^-precision，
    // 且两者量级可比（|e*ln10| >= ln10 > |ln(m)|），所以没有抵消放大。
    result = result + ln10Constant(ln2, work) * BigDecimal(exponent);
    return result;
}

// log10 的内部实现。
//
// 与 ln 同样的两条路径，但拆成「指数部分 + 尾数部分」，
// 这样 10 的整数次幂给出**精确**的整数（log10(1000) = 3，不是 2.999…）。
BigDecimal BigDecimal::log10Internal(const BigDecimal& value, const Context& work,
                                     const Context& final) {
    const BigDecimal one(1);
    if (value.equals(one)) return BigDecimal(0);

    const BigDecimal half = BigDecimal::fromString("0.5");
    const BigDecimal two(2);
    const BigDecimal ln2 = ln2Constant(work);
    const BigDecimal ln10 = ln10Constant(ln2, work);

    // 路径 1：靠近 1 的直接展开。
    if (value.compare(half) >= 0 && value.compare(two) <= 0) {
        const BigDecimal z = divide(value - one, value + one,
                                    Context{work.precision, RoundingMode::HalfEven});
        const BigDecimal naturalLog = multiplyByTwo(atanhSeries(z, work));
        const BigDecimal quotient = divide(naturalLog, ln10,
                                           Context{work.precision, RoundingMode::HalfEven});
        return roundToPrecision(quotient, final);
    }

    // 路径 2：量级分解。log10(x) = exponent + log10(尾数)。
    int exponent = 0;
    int halvings = 0;
    const BigDecimal mantissa = decomposeForLog(value, exponent, halvings);

    const BigDecimal z = divide(mantissa - one, mantissa + one,
                                Context{work.precision, RoundingMode::HalfEven});
    // ln(尾数) = halvings*ln(2) + 2*atanh(z)
    const BigDecimal naturalLog = multiplyByTwo(atanhSeries(z, work)) + ln2 * BigDecimal(halvings);

    const BigDecimal quotient = divide(naturalLog, ln10,
                                       Context{work.precision, RoundingMode::HalfEven});
    // 指数部分是整数，与商相加后舍入。
    return roundToPrecision(quotient + BigDecimal(exponent), final);
}

// --- 对外的超越函数接口 ---
//
// 五个入口都要检查 precision 的上限：
//
// 1. precision <= 0：除数为零之类的非法上下文。
// 2. precision > kMaxTranscendentalPrecision：超越函数的工作精度是
//    **平方级**的（见 kMaxTranscendentalPrecision 的实测数据）。
//    没有这道闸，sqrt(2) @ precision=99990 会永远算不完，
//    而且失败方式**不一致** —— 有的精度下内层的 divide 恰好抛异常，
//    有的精度下则一路挂起。
//
// 统一的 std::invalid_argument 让调用方拿到确定的行为。

BigDecimal BigDecimal::sqrt(const BigDecimal& value, const Context& context) {
    if (value.sign_ < 0) throw std::domain_error("BigDecimal: 负数没有实数平方根");
    if (value.isZero()) return BigDecimal(0);
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");
    if (context.precision > kMaxTranscendentalPrecision) {
        throw std::invalid_argument("BigDecimal: precision 超出超越函数的上限");
    }

    const int workPrecision = std::max(context.precision + kGuardDigits, kMinWorkPrecision);
    const Context work{workPrecision, RoundingMode::HalfEven};

    // 牛顿迭代：x <- (x + n/x) / 2。
    //
    // 初值必须来自 n 的**真实量级**。brief 原实现把 n 放大 10^(2*guard) 后
    // 用字符串的位数推初值 —— 放大后 scale_ = 2*guard，字符串里整数部分的
    // 位数与 n 的量级无关，于是 value = 2 会得到初值 1（真实答案 1.414e(guard)），
    // 而迭代上界只有 guard + 10 步，收敛不住。
    //
    // 这里把 n 分解成 m * 10^(2h)：取 e = n 的十进制指数（10^e <= n < 10^(e+1)），
    // h = floor(e/2)，则 m = n * 10^(-2h) ∈ [1, 100)，其平方根落在 [1, 10)。
    // 移位是精确的位操作，不引入任何舍入。
    const int e = value.decimalExponent();
    const int h = floorHalf(e);           // floor(e/2)，注意 C++ 的负数除法向零取整
    const BigDecimal scaled = shiftByPowerOfTen(value, -2 * h);

    // 初值取 1：牛顿法从 1 出发对 m ∈ [1, 100) 收敛良好
    // （真根在 [1, 10)，初值与真根同量级）。
    BigDecimal guess(1);

    // 收敛判据：相邻两次迭代之差小于 10^-(work.precision-1)。
    // 真根在 [1, 10)，所以工作精度下的 1 ulp 是 10^-work.precision；
    // 判据放宽到 10 ulp，兼顾舍入导致的「在两个相邻值间摆动」。
    const BigDecimal epsilon = shiftByPowerOfTen(BigDecimal(1), -(work.precision - 1));

    // 牛顿迭代。
    const int maxIterations = 64;
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        const BigDecimal quotient = divide(scaled, guess, work);
        // (guess + quotient) / 2 用精确的除以二，避免多一次舍入。
        const BigDecimal next = divideByTwo(guess + quotient);
        const BigDecimal difference = abs(next - guess);
        guess = next;
        if (difference.compare(epsilon) <= 0) break;
    }

    // 还原：sqrt(n) = sqrt(m) * 10^h。
    // 移位精确，最后按用户精度舍入。
    return roundToPrecision(shiftByPowerOfTen(guess, h), context);
}

BigDecimal BigDecimal::exp(const BigDecimal& value, const Context& context) {
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");
    if (context.precision > kMaxTranscendentalPrecision) {
        throw std::invalid_argument("BigDecimal: precision 超出超越函数的上限");
    }
    const int workPrecision = std::max(context.precision + kGuardDigits, kMinWorkPrecision);
    const Context work{workPrecision, RoundingMode::HalfEven};
    return expInternal(value, work, context);
}

BigDecimal BigDecimal::ln(const BigDecimal& value, const Context& context) {
    if (value.sign_ <= 0) throw std::domain_error("BigDecimal: 对数要求正数");
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");
    if (context.precision > kMaxTranscendentalPrecision) {
        throw std::invalid_argument("BigDecimal: precision 超出超越函数的上限");
    }
    if (value.equals(BigDecimal(1))) return BigDecimal(0);

    const int workPrecision = std::max(context.precision + kGuardDigits, kMinWorkPrecision);
    const Context work{workPrecision, RoundingMode::HalfEven};
    return roundToPrecision(lnInternal(value, work), context);
}

BigDecimal BigDecimal::log10(const BigDecimal& value, const Context& context) {
    if (value.sign_ <= 0) throw std::domain_error("BigDecimal: 对数要求正数");
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");
    if (context.precision > kMaxTranscendentalPrecision) {
        throw std::invalid_argument("BigDecimal: precision 超出超越函数的上限");
    }
    if (value.equals(BigDecimal(1))) return BigDecimal(0);

    const int workPrecision = std::max(context.precision + kGuardDigits, kMinWorkPrecision);
    const Context work{workPrecision, RoundingMode::HalfEven};
    return log10Internal(value, work, context);
}

BigDecimal BigDecimal::pow(const BigDecimal& base, const BigDecimal& exponent,
                           const Context& context) {
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");
    if (context.precision > kMaxTranscendentalPrecision) {
        throw std::invalid_argument("BigDecimal: precision 超出超越函数的上限");
    }

    // 整数指数走快速幂，结果精确。
    //
    // 判定「整数」用 scale_ == 0（值本身就是整数），而不是解析字符串 ——
    // brief 原实现手工解析字符串成 long long，超过 10^18 的指数会静默溢出。
    if (exponent.scale_ == 0) {
        // 指数过大时直接报溢出，而不是让乘法算出天量数字。
        if (abs(exponent).compare(BigDecimal(static_cast<std::int64_t>(kMaxIntegerPower))) > 0) {
            throw std::overflow_error("BigDecimal: 幂的指数过大");
        }

        if (exponent.sign_ < 0) {
            // 负整数指数：1 / base^|n|。零底数无定义。
            if (base.isZero()) {
                throw std::domain_error("BigDecimal: 零的负数次幂未定义");
            }
            const long long magnitude = -toInteger(exponent);
            const BigDecimal positive = integerPower(base, magnitude);
            return roundToPrecision(divide(BigDecimal(1), positive, context), context);
        }

        // 零底数：0^0 在 decimal 规范里是**非法运算**（Python 的
        // Decimal(0) ** Decimal(0) 抛 InvalidOperation），0^正数 = 0。
        if (base.isZero()) {
            if (exponent.isZero()) {
                throw std::domain_error("BigDecimal: 零的零次幂未定义");
            }
            return BigDecimal(0);
        }

        // 指数为 0 时结果为 1。
        const long long power = toInteger(exponent);
        return roundToPrecision(integerPower(base, power), context);
    }

    // 非整数指数：pow(a,b) = exp(b·ln a)。
    //
    // 零底数要先单独处理：0^正数 = 0 是有定义的（与 Python 一致），
    // 不能落进「负底数未定义」那条路。
    if (base.isZero()) {
        if (exponent.sign_ < 0) throw std::domain_error("BigDecimal: 零的负数次幂未定义");
        return BigDecimal(0);
    }
    if (base.sign_ < 0) throw std::domain_error("BigDecimal: 负底数的非整数次幂未定义");

    const int workPrecision = std::max(context.precision + kGuardDigits, kMinWorkPrecision);
    const Context work{workPrecision, RoundingMode::HalfEven};

    const BigDecimal naturalLog = lnInternal(base, work);
    const BigDecimal product = naturalLog * exponent;
    return expInternal(product, work, context);
}

}  // namespace hpc
