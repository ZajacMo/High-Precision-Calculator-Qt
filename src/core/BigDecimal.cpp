#include "core/BigDecimal.h"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace hpc {
namespace {

constexpr std::uint64_t kBase = 1000000000ULL;   // 10^9
constexpr int kBlockDigits = 9;

using Blk = std::vector<std::uint32_t>;

// 去除高位的零块，但至少保留一个块表示 0。
Blk trimBlk(Blk value) {
    while (value.size() > 1 && value.back() == 0) value.pop_back();
    if (value.empty()) value.push_back(0);
    return value;
}

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

// 把小端 base-10^9 块转成十进制字符串，scale 为小数位数。
std::string decimalFromBlocks(const Blk& blocks, int scale) {
    std::string digits = std::to_string(blocks.back());
    char buffer[16];
    for (std::size_t i = blocks.size() - 1; i-- > 0;) {
        std::snprintf(buffer, sizeof buffer, "%09u", static_cast<unsigned>(blocks[i]));
        digits += buffer;
    }
    if (scale <= 0) return digits;
    if (static_cast<int>(digits.size()) <= scale) {
        digits.insert(0, static_cast<std::size_t>(scale) + 1 - digits.size(), '0');
    }
    digits.insert(digits.size() - static_cast<std::size_t>(scale), 1, '.');
    return digits;
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
    // 逐次除以 10 直到最低位块不再是 10 的倍数。
    while (scale_ > 0 && (digits_.size() > 1 || digits_[0] != 0)) {
        if (digits_[0] % 10 != 0) break;
        divideBy10(digits_);
        --scale_;
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

}  // namespace hpc
