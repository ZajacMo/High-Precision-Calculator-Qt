// 性能基准探针：为 docs/bignum-algorithms.md 里的数字提供**可复现**的来源。
//
// 为什么它要进仓库：文档里写「实测数据」的人必须能自己重跑一遍，
// 否则那些数字只是断言。配套的 Python 驱动脚本是 scripts/bench.py
// （它负责编译这个探针并整理输出）。
//
// 测三组：
//   A. 乘法：base-10^9 分块 vs 单数字位（base=10）
//   B. 除法：逐块长除法 + 二分试商
//   C. 运行时 base 参数 vs 编译期常量
//   D. 超越函数：exp / ln / sqrt
//
// 输出 CSV（stdout），便于脚本处理与文档引用。
//
// 测量口径（三条，每条都对应一个会骗人的坑）：
//  1. **只计运算本身**，解析在计时之外 —— 否则分块版会凭空多背一次 O(n) 解析。
//  2. **重复多次取最小值** —— 单次计时会被调度抖动主导；最小值最接近算法本身的代价。
//  3. **C 组的 base 必须不可知** —— 若编译器能折叠 `%` 与 `/`，两版就完全一样，
//     量出来会是「运行时 base 没有代价」的假绿。base 取自 volatile。
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/BigDecimal.h"
#include "core/Format.h"

namespace {

using Clock = std::chrono::steady_clock;

// 让结果不被编译器优化掉。
volatile std::size_t g_sink = 0;

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

// 造一个 n 位有效数字的十进制数（首位非零，其余位由下标决定，便于复现）。
std::string makeNumber(int digits) {
    std::string text;
    text.reserve(static_cast<std::size_t>(digits));
    text.push_back('7');
    for (int i = 1; i < digits; ++i) {
        text.push_back(static_cast<char>('0' + ((i * 7919) % 10)));
    }
    return text;
}

// 重复跑 fn，返回**最小**耗时（毫秒）。
template <typename Fn>
double bestOf(Fn fn, int repeats) {
    double best = 1e30;
    for (int i = 0; i < repeats; ++i) {
        const auto start = Clock::now();
        fn();
        best = std::min(best, milliseconds(Clock::now() - start));
    }
    return best;
}

// --- 对照实现：单数字位（每个元素一位十进制数）---

// base 是**运行时参数**。
std::vector<std::uint32_t> mulDigitwise(const std::vector<std::uint32_t>& a,
                                         const std::vector<std::uint32_t>& b,
                                         std::uint64_t base) {
    std::vector<std::uint32_t> result(a.size() + b.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const std::uint64_t cur = result[i + j] + static_cast<std::uint64_t>(a[i]) * b[j] + carry;
            result[i + j] = static_cast<std::uint32_t>(cur % base);
            carry = cur / base;
        }
        std::size_t k = i + b.size();
        while (carry) {
            const std::uint64_t cur = result[k] + carry;
            result[k] = static_cast<std::uint32_t>(cur % base);
            carry = cur / base;
            ++k;
        }
    }
    return result;
}

// base 是**编译期常量**。
std::vector<std::uint32_t> mulDigitwiseFixed(const std::vector<std::uint32_t>& a,
                                              const std::vector<std::uint32_t>& b) {
    constexpr std::uint64_t kBase = 10;
    std::vector<std::uint32_t> result(a.size() + b.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const std::uint64_t cur = result[i + j] + static_cast<std::uint64_t>(a[i]) * b[j] + carry;
            result[i + j] = static_cast<std::uint32_t>(cur % kBase);
            carry = cur / kBase;
        }
        std::size_t k = i + b.size();
        while (carry) {
            const std::uint64_t cur = result[k] + carry;
            result[k] = static_cast<std::uint32_t>(cur % kBase);
            carry = cur / kBase;
            ++k;
        }
    }
    return result;
}

// 把十进制字符串切成单数字位（低位在前）。
std::vector<std::uint32_t> toDigits(const std::string& text) {
    std::vector<std::uint32_t> digits;
    digits.reserve(text.size());
    for (std::size_t i = text.size(); i-- > 0;) {
        digits.push_back(static_cast<std::uint32_t>(text[i] - '0'));
    }
    return digits;
}

}  // namespace

int main() {
    std::printf("group,param,value,milliseconds\n");

    // --- A. 乘法：分块 vs 单数字位 ---
    for (int digits : {100, 1000, 10000}) {
        const std::string text = makeNumber(digits);

        // 解析在计时之外。
        const hpc::BigDecimal left = hpc::BigDecimal::fromString(text);
        const hpc::BigDecimal right = hpc::BigDecimal::fromString(text);
        const std::vector<std::uint32_t> a = toDigits(text);

        const int repeats = digits >= 10000 ? 3 : (digits >= 1000 ? 5 : 20);

        const double blockMs = bestOf([&] {
            const hpc::BigDecimal product = left * right;
            g_sink += product.toString().size();
        }, repeats);

        const double digitMs = bestOf([&] {
            const std::vector<std::uint32_t> product = mulDigitwiseFixed(a, a);
            g_sink += product.size();
        }, repeats);

        std::printf("A,block_multiplication,%d,%.4f\n", digits, blockMs);
        std::printf("A,digitwise_multiplication,%d,%.4f\n", digits, digitMs);
        std::printf("A,speedup,%d,%.2f\n", digits, digitMs / blockMs);
    }

    // --- B. 除法 ---
    for (int digits : {20, 100, 400, 800, 1600}) {
        const std::string text = makeNumber(digits);
        const hpc::BigDecimal lhs = hpc::BigDecimal::fromString(text);
        const hpc::BigDecimal rhs = hpc::BigDecimal::fromString("7");
        const hpc::Context context{30, hpc::RoundingMode::HalfEven};

        const int repeats = digits >= 800 ? 20 : 50;
        const double ms = bestOf([&] {
            const hpc::BigDecimal quotient = hpc::BigDecimal::divide(lhs, rhs, context);
            g_sink += quotient.toString().size();
        }, repeats);
        std::printf("B,divide,%d,%.4f\n", digits, ms);
    }

    // --- C. 运行时 base 参数 vs 编译期常量 ---
    {
        const std::string text = makeNumber(4000);
        const std::vector<std::uint32_t> a = toDigits(text);

        // base 取自 volatile：编译器**无法**把它折叠成常量。
        static volatile std::uint64_t kOpaqueBase = 10;
        const std::uint64_t base = kOpaqueBase;

        const double runtimeMs = bestOf([&] {
            const std::vector<std::uint32_t> product = mulDigitwise(a, a, base);
            g_sink += product.size();
        }, 3);

        const double fixedMs = bestOf([&] {
            const std::vector<std::uint32_t> product = mulDigitwiseFixed(a, a);
            g_sink += product.size();
        }, 3);

        std::printf("C,base_runtime,4000,%.4f\n", runtimeMs);
        std::printf("C,base_compile_time,4000,%.4f\n", fixedMs);
        std::printf("C,base_penalty_percent,4000,%.1f\n",
                    (runtimeMs / fixedMs - 1.0) * 100.0);
    }

    // --- D. 超越函数 ---
    for (int precision : {50, 100, 200, 400, 800}) {
        const hpc::Context context{precision, hpc::RoundingMode::HalfEven};

        const double expMs = bestOf([&] {
            const hpc::BigDecimal r = hpc::BigDecimal::exp(hpc::BigDecimal::fromString("1"), context);
            g_sink += r.toString().size();
        }, 3);
        std::printf("D,exp_1,%d,%.4f\n", precision, expMs);

        // 用 ln(2) 而不是 ln(1)：ln(1) 有一条早退路径，量到的不是级数的代价。
        const double lnMs = bestOf([&] {
            const hpc::BigDecimal r = hpc::BigDecimal::ln(hpc::BigDecimal::fromString("2"), context);
            g_sink += r.toString().size();
        }, 3);
        std::printf("D,ln_2,%d,%.4f\n", precision, lnMs);

        const double sqrtMs = bestOf([&] {
            const hpc::BigDecimal r = hpc::BigDecimal::sqrt(hpc::BigDecimal::fromString("2"), context);
            g_sink += r.toString().size();
        }, 3);
        std::printf("D,sqrt_2,%d,%.4f\n", precision, sqrtMs);
    }

    // 极端参数：exp 的极小参数（工作精度不该按 |x| 的量级膨胀）。
    {
        const hpc::Context context{30, hpc::RoundingMode::HalfEven};
        const double ms = bestOf([&] {
            const hpc::BigDecimal r = hpc::BigDecimal::exp(
                hpc::BigDecimal::fromString("1e-200"), context);
            g_sink += r.toString().size();
        }, 10);
        std::printf("D,exp_1e-200,30,%.4f\n", ms);
    }

    // --- E. 进制转换 ---
    //
    // 展开代价是 O(位数 × 块数)。这一组数字很重要，因为界面上的
    // 精度上限（100000 位）是**够得着**的 —— 「高精度」不等于「快」。
    // 100000 位转二进制要 8 秒量级，300000 位要 1 分半量级。
    for (int digits : {10000, 100000, 300000}) {
        const std::string text = makeNumber(digits);
        const hpc::BigDecimal value = hpc::BigDecimal::fromString(text);

        const auto start = Clock::now();
        const std::string binary = hpc::toBaseString(value, 2);
        const double ms = milliseconds(Clock::now() - start);
        g_sink += binary.size();

        std::printf("E,to_base2,%d,%.4f\n", digits, ms);
    }

    return 0;
}
