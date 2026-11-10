#include "core/BigDecimal.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace hpc {
namespace {

constexpr std::uint64_t kBase = 1000000000ULL;   // 10^9
constexpr int kBlockDigits = 9;

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

}  // namespace hpc
