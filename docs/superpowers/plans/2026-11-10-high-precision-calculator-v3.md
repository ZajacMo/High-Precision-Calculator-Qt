# 高精度计算器 v3.0 统一重构 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把三份散落的高精度计算器实现合并为一个项目，产出零依赖 C++17 内核 + CLI + Web 前端 + 可选 Qt 界面，并附 Python `decimal` 差分测试作为正确性基准。

**Architecture:** 沿用兄弟项目 `VS_C++Project/Comprehensive-Calculation-Tool` 已验证的 v2.1「自包含内核 + Web 前端」架构。核心 `src/core/` 纯 C++17 零依赖；`BigDecimal` 采用 base-10⁹ 分块，精确运算不带精度参数、除法与超越函数接受显式 `Context`。三代原始实现保留到 `legacy/`。

**Tech Stack:** C++17（MSVC 14.44 / MinGW g++ 11.2）、CMake 3.21+、Ninja、Qt 6.6.2（仅 GUI 目标）、Python 3（仅差分测试与端到端测试）、原生 HTML/CSS/JS（Web 前端，无 npm）

## Global Constraints

- **核心内核零第三方依赖**：只允许 C++17 标准库。引入任何第三方库前先确认能否用几百行自行实现。
- **默认构建不依赖 Qt**：`-DBUILD_GUI` 默认 `OFF`；核心+CLI+测试只需一个 C++17 编译器。
- **CMake 最低版本 3.21**，C++ 标准固定 `CMAKE_CXX_STANDARD 17`、`CXX_STANDARD_REQUIRED ON`、`CXX_EXTENSIONS OFF`。
- **命名空间**：核心库用 `hpc`；JSON 子模块 `hpc::json`；HTTP 子模块 `hpc::http`。
- **精度不得使用全局静态变量**：`+ - *` 不接受 `Context` 参数；除法与超越函数必须显式接收 `Context`。
- **提交粒度**：每个 Task 结束时提交一次，提交信息用中文并遵循 `feat:` / `fix:` / `docs:` / `chore:` 前缀。
- **本机工具链**（已验证存在）：MSVC `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207`、Win SDK `10.0.26100.0`、Qt `C:\Qt\6.6.2\msvc2019_64`、CMake `C:\Qt\Tools\CMake_64\bin`、Ninja `C:\Qt\Tools\Ninja`。
- **构建脚本必须复用** `scripts\msvc-env.bat`（含 Win SDK `bin\<ver>\x64`，否则 `rc.exe`/`mt.exe` 缺失导致 CMake try-compile 失败）。
- **单元测试框架**：复用兄弟项目的 `tests/test_framework.h`（`TEST`/`CHECK`/`CHECK_EQ`/`CHECK_STREQ` 宏，零依赖单文件聚合）。
- **每个 Task 结束前必须实际运行测试并看到全绿**，不得只写代码就宣称完成。
- **构建命令直接在 Git Bash 里运行 `.bat`，不要用 `cmd //c` 包装**：
  用 `./scripts/build-core.bat`、`./scripts/run-ctest.bat`、
  `./scripts/msvc-env.bat cmake ...`。`cmd //c` 在本沙箱中被拒绝
  （无法静态判定其是否会执行版本控制命令），直接调用 `.bat` 已验证可用。

---

## File Structure

```
cpp/QtProject/calculator/
├── CMakeLists.txt              顶层构建；BUILD_GUI/BUILD_CLI/BUILD_TESTS/BUILD_SERVER 选项
├── .gitignore                  构建产物
├── src/core/
│   ├── BigDecimal.h/.cpp       核心数值类型（base-10⁹ 分块）
│   ├── Rounding.h              Context 与舍入模式（头文件即可）
│   ├── Format.h/.cpp           进制转换与位运算
│   └── Expression.h/.cpp       表达式解析与求值
├── src/cli/main.cpp            命令行前端（交互/单次/OJ 三模式）
├── src/api/ApiRoutes.h/.cpp    REST 路由
├── src/server/main.cpp         内置 HTTP 服务
├── src/ui/                     Qt 界面（可选目标）
├── web/                        原生 HTML/CSS/JS 前端
├── legacy/                     三代原始实现存档
├── tests/
│   ├── test_framework.h        复用兄弟项目的极简框架
│   ├── tst_main.cpp
│   ├── tst_bigdecimal.cpp      核心数值类型测试
│   ├── tst_format.cpp          进制与位运算测试
│   └── tst_expression.cpp      表达式测试
├── scripts/
│   ├── msvc-env.bat            MSVC/Qt 环境
│   ├── build-core.bat          构建核心+CLI+测试
│   ├── run-ctest.bat           运行测试
│   ├── difftest.py             Python decimal 差分测试
│   ├── api_test.py             端到端 API 测试
│   └── screenshot.py           headless Edge 截图
├── docs/
│   ├── architecture.md
│   ├── bignum-algorithms.md
│   └── legacy/COMPARISON.md
└── .github/workflows/build.yml CI
```

---

### Task 1: 项目骨架与 legacy 归档

把三代原始实现收进 `legacy/`，建立新的 CMake 骨架与测试框架。

**Files:**
- Create: `CMakeLists.txt`
- Create: `.gitignore`
- Create: `tests/test_framework.h`（从兄弟项目复制）
- Create: `tests/tst_main.cpp`
- Create: `legacy/README.md`
- Create: `legacy/COMPARISON.md`
- Move: `number.{h,cpp}` `node.{h,cpp}` `tempnum.{h,cpp}` `main.cpp` `mainwindow.{h,cpp,ui}` `dialog_*.{h,cpp,ui}` `calculator.pro*` `calculator.user` `res/` → `legacy/qt_gui_original/`
- Move: `README.md` `README_CN.md` → `legacy/qt_gui_original/`
- Copy: 外部 `../VSC_C++Project/OnlineJudge/Calculator/` → `legacy/onlinejudge_calculator/`
- Copy: 外部 `../../CLion/C++Programming/OJ_Calculator Console Edition/` → `legacy/oj_console_edition/`（排除 `cmake-build-debug/` 与 `.idea/`）

**Interfaces:**
- Consumes: 无
- Produces: 可用的 `cmake -B build && cmake --build build` 空壳；`tests/test_framework.h` 提供 `TEST(suite, name)`、`CHECK(cond)`、`CHECK_EQ(a, b)`、`CHECK_STREQ(a, b)`、`::testing::failureCount()`、`::testing::registry()`

- [ ] **Step 1: 创建目录骨架并迁移 legacy**

```bash
cd <worktree>
mkdir -p src/core src/cli src/api src/server src/ui web tests scripts docs/legacy
mkdir -p legacy/qt_gui_original legacy/onlinejudge_calculator legacy/oj_console_edition

# Qt GUI 原版整体移入 legacy
git mv number.h number.cpp node.h node.cpp tempnum.h tempnum.cpp main.cpp \
       mainwindow.h mainwindow.cpp mainwindow.ui \
       dialog_help.h dialog_help.cpp dialog_help.ui \
       dialog_setbit.h dialog_setbit.cpp dialog_setbit.ui \
       dialog_tip.h dialog_tip.cpp dialog_tip.ui \
       calculator.pro calculator.pro.user calculator.user res \
       legacy/qt_gui_original/
git mv README.md README_CN.md legacy/qt_gui_original/
```

外部两份命令行版**只复制不删除**（原位置不在 git 内，删除不可逆）：

```bash
cp -r "/f/Projects/01_courses/cpp/VSC_C++Project/OnlineJudge/Calculator/." legacy/onlinejudge_calculator/
cp -r "/f/Projects/01_courses/cpp/CLion/C++Programming/OJ_Calculator Console Edition/." legacy/oj_console_edition/
rm -rf legacy/oj_console_edition/cmake-build-debug legacy/oj_console_edition/.idea
```

- [ ] **Step 2: 写 `tests/test_framework.h`**

从 `F:\Projects\01_courses\cpp\VS_C++Project\Comprehensive-Calculation-Tool\tests\test_framework.h` 原样复制（零依赖单文件框架，已验证可用）。

- [ ] **Step 3: 写 `tests/tst_main.cpp`**

```cpp
#include "test_framework.h"

int main() {
    int passed = 0;
    std::string currentSuite;

    for (const auto& testCase : ::testing::registry()) {
        if (testCase.suite != currentSuite) {
            currentSuite = testCase.suite;
            std::printf("\n[%s]\n", currentSuite.c_str());
        }
        const int before = ::testing::failureCount();
        testCase.fn();
        const bool ok = ::testing::failureCount() == before;
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", testCase.name.c_str());
        std::fflush(stdout);
        if (ok) ++passed;
    }

    std::printf("\n----------------------------------------\n");
    std::printf("通过 %zu / %zu 个用例，失败 %d 个断言。\n", static_cast<size_t>(passed),
                ::testing::registry().size(), ::testing::failureCount());
    return ::testing::failureCount() == 0 ? 0 : 1;
}
```

- [ ] **Step 4: 写 `CMakeLists.txt` 骨架**

```cmake
cmake_minimum_required(VERSION 3.21)

# 单配置生成器（Ninja/Makefiles）下 project() 会把 CMAKE_BUILD_TYPE 初始化为
# Debug，因此默认值必须在 project() 之前设置，否则下面的判断永远不成立。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

project(HighPrecisionCalculator
    VERSION 3.0.0
    DESCRIPTION "高精度计算器 — 任意精度十进制内核、命令行、Web 前端与 Qt 界面"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_GUI    "构建 Qt 图形界面（需要 Qt 6）" OFF)
option(BUILD_CLI    "构建命令行界面"              ON)
option(BUILD_TESTS  "构建单元测试"                ON)
option(BUILD_SERVER "构建 Web 服务端"              ON)

# 核心数值库：纯 C++17，零第三方依赖。
add_library(hpc_core STATIC
    src/core/BigDecimal.cpp
    src/core/Format.cpp
    src/core/Expression.cpp
)
target_include_directories(hpc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(hpc_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(hpc_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(hpc_core PRIVATE -Wall -Wextra)
endif()

if(BUILD_CLI)
    add_executable(hpc_cli src/cli/main.cpp)
    target_link_libraries(hpc_cli PRIVATE hpc_core)
    if(MSVC)
        target_compile_options(hpc_cli PRIVATE /W4 /utf-8)
    endif()
endif()

if(BUILD_TESTS)
    enable_testing()
    add_executable(hpc_tests
        tests/tst_main.cpp
        tests/tst_bigdecimal.cpp
        tests/tst_format.cpp
        tests/tst_expression.cpp
    )
    target_link_libraries(hpc_tests PRIVATE hpc_core)
    target_include_directories(hpc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    if(MSVC)
        # /utf-8 不可省略：MSVC 默认按 GBK 代码页解析源文件，
        # 测试文件含中文注释时会报 C4819 并导致宏展开被截断。
        target_compile_options(hpc_tests PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME hpc_tests COMMAND hpc_tests)
endif()
```

- [ ] **Step 5: 写 `.gitignore`**

从兄弟项目 `.gitignore` 复制（已覆盖 build/、CMake 产物、IDE 目录等）。

- [ ] **Step 6: 写占位源文件让构建通过**

`src/core/BigDecimal.cpp`、`Format.cpp`、`Expression.cpp` 各写一个空实现占位：

```cpp
// 占位：Task 2 起逐步填充。
namespace hpc { }
```

`src/cli/main.cpp`：

```cpp
int main() { return 0; }
```

`tests/tst_bigdecimal.cpp`、`tst_format.cpp`、`tst_expression.cpp` 各放一个**验证测试框架本身接线正确**的用例
（断言真实行为，而非空断言占位）：

```cpp
#include "test_framework.h"

// 占位：Task 2/5/6 起替换为真实用例。此处验证测试框架接线正确。
TEST(placeholder, frameworkIsWired) {
    CHECK_EQ(1 + 1, 2);
    CHECK(std::string("abc") == "abc");
}
```

- [ ] **Step 7: 写 `legacy/README.md` 与 `legacy/COMPARISON.md`**

`legacy/README.md` 说明三个子目录的来源、代际判定依据（md5 比对结论）、已知缺陷。

`legacy/COMPARISON.md` 逐项对比三代实现：

| 维度 | onlinejudge_calculator（第 1 代） | oj_console_edition（第 2 代） | qt_gui_original（第 3 代） |
|---|---|---|---|
| API 命名 | PascalCase（`Get`/`Show`/`Copy`/`Release`/`Transform_Sign`/`POWER`） | camelCase（`get`/`show`/`copy`/`release`/`transformSign`/`getPower`） | camelCase（同第 2 代） |
| 输入输出 | `std::string` + 流重载 | `std::string` + 流重载 | `QString` |
| 除法 | 试商法（`Gap`/`Times` 临时对象） | 试商法 | 试商法 |
| `node.cpp/h` | 独立 | **与第 3 代逐字节相同**（md5 `45615d3a` / `b67dc507`） | — |
| 谱系判定 | 注释提及「Qt版参数为待转化的字符串QString类型」→ 先于 Qt 版存在 | 由第 3 代反向移植 | 最新 |

同时列出规格 §8 的 9 条已知缺陷。

- [ ] **Step 8: 写 `scripts/msvc-env.bat` 与 `scripts/build-core.bat`**

`msvc-env.bat` 从兄弟项目复制（**必须含 Win SDK `bin\<ver>\x64`**）：

```bat
@echo off
rem Configure the MSVC/Qt toolchain, then run the rest of the arguments.
rem Usage: scripts\msvc-env.bat <command> [args...]

set "VCT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKV=10.0.26100.0"
set "QTDIR=C:\Qt\6.6.2\msvc2019_64"

set "INCLUDE=%VCT%\include;%SDK%\Include\%SDKV%\ucrt;%SDK%\Include\%SDKV%\um;%SDK%\Include\%SDKV%\shared;%SDK%\Include\%SDKV%\winrt"
set "LIB=%VCT%\lib\x64;%SDK%\Lib\%SDKV%\ucrt\x64;%SDK%\Lib\%SDKV%\um\x64"
set "PATH=%VCT%\bin\Hostx64\x64;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%SDK%\bin\%SDKV%\x64;%QTDIR%\bin;%PATH%"
set "CMAKE_PREFIX_PATH=%QTDIR%"

%*
```

`build-core.bat`：

```bat
@echo off
rem Configure + build core, CLI and tests (no Qt GUI).
setlocal
call "%~dp0msvc-env.bat" cmake -S "%~dp0.." -B "%~dp0..\build" -G Ninja -DBUILD_GUI=OFF -DBUILD_CLI=ON -DBUILD_TESTS=ON || exit /b 1
call "%~dp0msvc-env.bat" cmake --build "%~dp0..\build" || exit /b 1
```

- [ ] **Step 9: 构建并运行测试**

```bash
./scripts/build-core.bat
```
预期：编译 `hpc_core`、`hpc_cli`、`hpc_tests` 全部成功，无 warning。

```bash
./scripts/run-ctest.bat
```
（`run-ctest.bat` 内容见 Task 10，或先建最小版本）

预期：`1/1 Test #1: hpc_tests ... Passed`，用例全绿。

- [ ] **Step 10: 提交**

```bash
git add -A
git commit -m "chore: 建立 v3.0 项目骨架，三代原始实现归档到 legacy/"
```

---

### Task 2: BigDecimal 表示层与精确四则

**Files:**
- Create: `src/core/Rounding.h`
- Create: `src/core/BigDecimal.h`
- Create: `src/core/BigDecimal.cpp`
- Modify: `tests/tst_bigdecimal.cpp`

**Interfaces:**
- Consumes: 无
- Produces:
  - `namespace hpc`
  - `enum class RoundingMode { HalfEven, HalfUp, Down, Up, Ceil, Floor }`
  - `struct Context { int precision = 30; RoundingMode mode = RoundingMode::HalfEven; static Context default_(); }`
  - `class BigDecimal`：构造 `BigDecimal()`、`explicit BigDecimal(int64_t)`、`explicit BigDecimal(std::string_view)`
  - `static BigDecimal fromString(std::string_view)`（精确解析，失败抛 `std::invalid_argument`）
  - `std::string toString() const`
  - `BigDecimal operator+(const BigDecimal&) const`、`operator-`、`operator*`
  - `int compare(const BigDecimal&) const`、`bool equals(const BigDecimal&) const`
  - `static BigDecimal abs(const BigDecimal&)`
  - `bool isZero() const`、`int sign() const`
  - 内部：`int sign_`、`std::vector<uint32_t> digits_`（小端 base-10⁹）、`int scale_`

- [ ] **Step 1: 写 `src/core/Rounding.h`**

```cpp
// 舍入模式与运算上下文：精度是显式参数，不是全局状态。
#pragma once

namespace hpc {

// 舍入模式。语义与 Python decimal 的 rounding 名称对齐。
enum class RoundingMode {
    HalfEven,  // 四舍六入五成双（银行家舍入）
    HalfUp,    // 四舍五入
    Down,      // 向零截断
    Up,        // 远离零截断
    Ceil,      // 向 +∞ 取整
    Floor,     // 向 -∞ 取整
};

// 运算上下文：仅用于本质上不精确的运算（除法、开方、幂、指数、对数）。
// 精确运算（加、减、乘）不接受此结构，从编译期杜绝精度被隐式改变。
struct Context {
    int precision = 30;                  // 有效数字位数
    RoundingMode mode = RoundingMode::HalfEven;

    static Context default_() { return Context{}; }
};

}  // namespace hpc
```

- [ ] **Step 2: 写 `src/core/BigDecimal.h`**

```cpp
// 高精度十进制数：base-10^9 分块存储，小端。
//
// 值 = sign_ * Σ digits_[i] * 10^(9*i) * 10^(-scale_)
// 其中 digits_[0] 为最低位块，scale_ 为小数位数（以 10 为单位）。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/Rounding.h"

namespace hpc {

class BigDecimal {
public:
    BigDecimal();
    explicit BigDecimal(std::int64_t value);
    // 精确解析十进制字符串。支持可选负号、小数点、前导零。
    // 格式非法时抛出 std::invalid_argument。
    explicit BigDecimal(std::string_view text);

    static BigDecimal fromString(std::string_view text);

    // 规范化十进制表示：负号（若有）+ 整数部分 + 小数部分（若有）。
    // 值为 0 时输出 "0"，不输出 "-0"。
    std::string toString() const;

    // 精确运算：不接受 Context，结果总是精确的。
    BigDecimal operator+(const BigDecimal& other) const;
    BigDecimal operator-(const BigDecimal& other) const;
    BigDecimal operator*(const BigDecimal& other) const;

    // 比较：<0 表示本对象小于 other，0 表示相等，>0 表示大于。
    int compare(const BigDecimal& other) const;
    bool equals(const BigDecimal& other) const;

    static BigDecimal abs(const BigDecimal& value);

    bool isZero() const;
    int sign() const { return sign_; }

private:
    // 规范化：去除前导零块，去掉多余的尾随零。
    void normalize();

    // 对齐两个数的小数位，返回对齐后共同的 scale。
    static int alignScale(BigDecimal& lhs, BigDecimal& rhs);

    int sign_ = 0;                      // -1 / 0 / +1
    std::vector<std::uint32_t> digits_; // 小端 base-10^9 分块
    int scale_ = 0;                     // 小数位数（以 10 为单位）
};

}  // namespace hpc
```

- [ ] **Step 3: 写 `src/core/BigDecimal.cpp`**

实现要点（**必须严格按此，否则差分测试会抓到端序错误**）：

```cpp
#include "core/BigDecimal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

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
        std::snprintf(buffer, sizeof buffer, "%09u", blocks[i]);
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
```

- [ ] **Step 4: 写 `tests/tst_bigdecimal.cpp`**

```cpp
#include "test_framework.h"

#include <stdexcept>

#include "core/BigDecimal.h"

using hpc::BigDecimal;

TEST(parse, basic) {
    CHECK_EQ(BigDecimal::fromString("123").toString(), std::string("123"));
    CHECK_EQ(BigDecimal::fromString("0.5").toString(), std::string("0.5"));
    CHECK_EQ(BigDecimal::fromString("-2.25").toString(), std::string("-2.25"));
    CHECK_EQ(BigDecimal::fromString("0").toString(), std::string("0"));
    CHECK_EQ(BigDecimal::fromString("007").toString(), std::string("7"));
    CHECK_EQ(BigDecimal::fromString("1.500").toString(), std::string("1.5"));
    CHECK_EQ(BigDecimal::fromString(".5").toString(), std::string("0.5"));
}

TEST(parse, invalid) {
    bool threw = false;
    try { BigDecimal::fromString("abc"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { BigDecimal::fromString("1.2.3"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { BigDecimal::fromString(""); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

TEST(arith, add) {
    CHECK_EQ((BigDecimal::fromString("1") + BigDecimal::fromString("2")).toString(), std::string("3"));
    CHECK_EQ((BigDecimal::fromString("0.1") + BigDecimal::fromString("0.2")).toString(), std::string("0.3"));
    CHECK_EQ((BigDecimal::fromString("-1") + BigDecimal::fromString("2")).toString(), std::string("1"));
    CHECK_EQ((BigDecimal::fromString("1") + BigDecimal::fromString("-2")).toString(), std::string("-1"));
}

TEST(arith, subtract) {
    CHECK_EQ((BigDecimal::fromString("5") - BigDecimal::fromString("3")).toString(), std::string("2"));
    CHECK_EQ((BigDecimal::fromString("3") - BigDecimal::fromString("5")).toString(), std::string("-2"));
    CHECK_EQ((BigDecimal::fromString("0.3") - BigDecimal::fromString("0.1")).toString(), std::string("0.2"));
}

TEST(arith, multiply) {
    CHECK_EQ((BigDecimal::fromString("2.5") * BigDecimal::fromString("4")).toString(), std::string("10"));
    CHECK_EQ((BigDecimal::fromString("0.1") * BigDecimal::fromString("0.2")).toString(), std::string("0.02"));
    CHECK_EQ((BigDecimal::fromString("-3") * BigDecimal::fromString("4")).toString(), std::string("-12"));
    CHECK_EQ((BigDecimal::fromString("-3") * BigDecimal::fromString("-4")).toString(), std::string("12"));
}

TEST(arith, exactness_beyond_double) {
    // 经典浮点陷阱：0.1 + 0.2 在 double 中不等于 0.3
    const BigDecimal a = BigDecimal::fromString("0.1");
    const BigDecimal b = BigDecimal::fromString("0.2");
    CHECK_EQ((a + b).toString(), std::string("0.3"));

    // 大数乘法不丢位
    const BigDecimal big = BigDecimal::fromString("123456789012345678901234567890");
    const BigDecimal one = BigDecimal::fromString("1");
    CHECK_EQ((big * one).toString(), std::string("123456789012345678901234567890"));
}

TEST(compare, sign) {
    CHECK(BigDecimal::fromString("1").compare(BigDecimal::fromString("2")) < 0);
    CHECK(BigDecimal::fromString("2").compare(BigDecimal::fromString("1")) > 0);
    CHECK(BigDecimal::fromString("1.0").compare(BigDecimal::fromString("1")) == 0);
    CHECK(BigDecimal::fromString("-1").compare(BigDecimal::fromString("1")) < 0);
    CHECK(BigDecimal::fromString("0.5").compare(BigDecimal::fromString("0.50")) == 0);
}
```

- [ ] **Step 5: 构建并运行测试**

```bash
./scripts/build-core.bat && ./scripts/run-ctest.bat
```
预期：`hpc_tests` 通过，用例数 ≥ 6，`0 个断言失败`。

- [ ] **Step 6: 提交**

```bash
git add src/core tests/tst_bigdecimal.cpp
git commit -m "feat(core): BigDecimal 表示层与精确四则运算"
```

---

### Task 3: 除法、舍入与 Context

**Files:**
- Modify: `src/core/BigDecimal.h`
- Modify: `src/core/BigDecimal.cpp`
- Modify: `tests/tst_bigdecimal.cpp`
- Create: `tests/div_probe.cpp`
- Create: `scripts/verify_div.py`

**Interfaces:**
- Consumes: Task 2 的 `BigDecimal`、`Context`、`RoundingMode`
- Produces:
  - `BigDecimal operator/(const BigDecimal& other, const Context& context) const`（除零抛 `std::domain_error`）
  - `static BigDecimal divide(const BigDecimal& lhs, const BigDecimal& rhs, const Context& context)`
  - `static BigDecimal modulo(const BigDecimal& lhs, const BigDecimal& rhs)`（除零抛 `std::domain_error`）
  - `static BigDecimal floor(const BigDecimal&, const Context&)`、`ceil`、`round`（HalfEven）
  - 私有：`static BigDecimal roundToPrecision(const BigDecimal&, const Context&)`
  - 私有：`static std::pair<Blk, Blk> divideBlk(const Blk& dividend, const Blk& divisor)`

- [ ] **Step 1: 在 `BigDecimal.h` 声明新增接口**

在 `class BigDecimal` 的 public 区域加入：

```cpp
    // 不精确运算：必须显式给出精度。
    // 除数为零时抛出 std::domain_error。
    BigDecimal operator/(const BigDecimal& other, const Context& context) const;
    static BigDecimal divide(const BigDecimal& lhs, const BigDecimal& rhs, const Context& context);

    // 取余：lhs - trunc(lhs/rhs) * rhs。除数为零时抛出 std::domain_error。
    static BigDecimal modulo(const BigDecimal& lhs, const BigDecimal& rhs);

    // 取整。
    static BigDecimal floor(const BigDecimal& value, const Context& context);
    static BigDecimal ceil(const BigDecimal& value, const Context& context);
    static BigDecimal round(const BigDecimal& value, const Context& context);
```

在 private 区域加入：

```cpp
    // 舍入到 context.precision 位有效数字。
    static BigDecimal roundToPrecision(const BigDecimal& value, const Context& context);

    // 逐块长除法 + 二分试商。返回 {商, 余数}。
    static std::pair<Blk, Blk> divideBlk(const Blk& dividend, const Blk& divisor);
```

并在文件顶部加入 `#include <utility>`。同时在匿名命名空间之外需要一个可见的 `Blk` 别名 —— **把 `using Blk = std::vector<std::uint32_t>;` 提到 `namespace hpc` 内的 public 位置**（紧跟 `namespace hpc {`），使 `BigDecimal.h` 的私有声明可用：

```cpp
namespace hpc {

using Blk = std::vector<std::uint32_t>;

class BigDecimal {
```

并从 `BigDecimal.cpp` 的匿名命名空间中删掉重复的 `using Blk = ...`（改为直接使用 `hpc::Blk`）。

- [ ] **Step 2: 在 `BigDecimal.cpp` 实现除法**

```cpp
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
        if (carry) remainder.push_back(static_cast<std::uint32_t>(carry));  // 进位在最高位
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

    std::reverse(quotient.begin(), quotient.end());   // 转成小端
    quotient = trimBlk(std::move(quotient));
    return {std::move(quotient), std::move(remainder)};
}
```

**关键**：进位必须 `push_back`（追加到**最高位**），不是 `insert(begin())`；商在累积时是**最高位在前**，最后必须 `reverse`。这两处是已知的易错点（原型阶段曾因此出错）。

```cpp
BigDecimal BigDecimal::divide(const BigDecimal& lhs, const BigDecimal& rhs, const Context& context) {
    if (rhs.sign_ == 0) throw std::domain_error("BigDecimal: 除数为零");
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");

    // 多算两位用于判断进位，保证舍入正确。
    const int guard = context.precision + 2;

    // 被除数放大 divisor.scale_ 位以消去除数的小数位；
    // 再放大 guard 位以保留足够的小数位。
    Blk numerator = lhs.digits_;
    for (int i = 0; i < rhs.scale_; ++i) {
        numerator = mulBlk(numerator, Blk{10});
    }
    for (int i = 0; i < guard; ++i) {
        numerator = mulBlk(numerator, Blk{10});
    }

    const auto divided = divideBlk(numerator, rhs.digits_);

    BigDecimal result;
    result.digits_ = divided.first;
    result.scale_ = lhs.scale_ - rhs.scale_ + guard;
    result.sign_ = lhs.sign_ * rhs.sign_;
    result.normalize();

    return roundToPrecision(result, context);
}

BigDecimal BigDecimal::operator/(const BigDecimal& other, const Context& context) const {
    return divide(*this, other, context);
}

BigDecimal BigDecimal::modulo(const BigDecimal& lhs, const BigDecimal& rhs) {
    if (rhs.sign_ == 0) throw std::domain_error("BigDecimal: 取余的除数为零");
    if (lhs.compare(rhs) < 0 && lhs.sign() * rhs.sign() >= 0) return lhs;
    BigDecimal quotient = divide(lhs, rhs, Context{1000000, RoundingMode::Down});
    return lhs - quotient * rhs;
}
```

`roundToPrecision` —— **用字符串级实现**，避免块边界与舍入位的交互错误：

```cpp
// 舍入到 context.precision 位有效数字。
BigDecimal BigDecimal::roundToPrecision(const BigDecimal& value, const Context& context) {
    if (value.sign_ == 0) return value;

    std::string text = value.toString();
    const bool negative = !text.empty() && text[0] == '-';
    if (negative) text.erase(0, 1);
    std::size_t dot = text.find('.');
    std::string integerPart = dot == std::string::npos ? text : text.substr(0, dot);
    std::string fractionPart = dot == std::string::npos ? std::string() : text.substr(dot + 1);

    // 去掉整数部分前导零
    const std::size_t firstNonZero = integerPart.find_first_not_of('0');
    integerPart = firstNonZero == std::string::npos ? std::string("0")
                                                     : integerPart.substr(firstNonZero);
    // 去掉小数部分尾随零
    while (!fractionPart.empty() && fractionPart.back() == '0') fractionPart.pop_back();

    // 判断是否需要进位：依据被丢弃的首位与舍入模式。
    auto shouldRoundUp = [&](const std::string& discarded, const std::string& kept) {
        if (discarded.empty()) return false;
        if (context.mode == RoundingMode::Down || context.mode == RoundingMode::Floor) {
            return false;
        }
        const char first = discarded[0];
        switch (context.mode) {
            case RoundingMode::Ceil:
                return !negative;
            case RoundingMode::Up:
                return true;
            case RoundingMode::HalfUp:
                return first >= '5';
            case RoundingMode::HalfEven: {
                const bool greater = first > '5';
                const bool equal = first == '5';
                const bool restNonZero =
                    discarded.find_first_not_of('0', 1) != std::string::npos;
                const int lastDigit = kept.empty() ? 0 : (kept.back() - '0');
                return greater || (equal && (restNonZero || lastDigit % 2 == 1));
            }
            default:
                return first >= '5';
        }
    };

    // 十进制字符串加一。
    auto increment = [](std::string& digits) {
        std::size_t i = digits.size();
        while (i-- > 0) {
            if (digits[i] == '9') { digits[i] = '0'; }
            else { ++digits[i]; return; }
        }
        digits.insert(0, "1");
    };

    if (integerPart.size() >= static_cast<std::size_t>(context.precision)) {
        // 整数部分已占满精度：截断小数部分。
        const std::string kept = integerPart.substr(0, static_cast<std::size_t>(context.precision));
        const std::string discarded = integerPart.substr(static_cast<std::size_t>(context.precision))
                                     + fractionPart;
        std::string result = kept;
        if (shouldRoundUp(discarded, kept)) increment(result);
        text = result;
        fractionPart.clear();
    } else {
        // 小数部分需要截断。
        const std::size_t keep = static_cast<std::size_t>(context.precision) - integerPart.size();
        std::string kept = fractionPart.substr(0, std::min(keep, fractionPart.size()));
        const std::string discarded = fractionPart.substr(kept.size());
        if (shouldRoundUp(discarded, kept)) {
            // 给 kept 加一；若溢出到整数位则进位到 integerPart。
            std::string probe = kept;
            increment(probe);
            bool overflow = probe.size() > kept.size();
            if (overflow) probe = probe.substr(1);
            kept = probe;
            if (overflow) increment(integerPart);
        }
        text = integerPart;
        fractionPart = kept;
    }

    // 拼装
    std::string assembled = text;
    if (!fractionPart.empty()) assembled += "." + fractionPart;
    if (negative && assembled.find_first_not_of("0.") != std::string::npos) {
        assembled = "-" + assembled;
    }
    return BigDecimal::fromString(assembled);
}
```

- [ ] **Step 3: 实现 floor / ceil / round**

```cpp
BigDecimal BigDecimal::floor(const BigDecimal& value, const Context& context) {
    return roundToPrecision(value, Context{context.precision, RoundingMode::Floor});
}
BigDecimal BigDecimal::ceil(const BigDecimal& value, const Context& context) {
    return roundToPrecision(value, Context{context.precision, RoundingMode::Ceil});
}
BigDecimal BigDecimal::round(const BigDecimal& value, const Context& context) {
    return roundToPrecision(value, Context{context.precision, RoundingMode::HalfEven});
}
```

- [ ] **Step 4: 在 `tests/tst_bigdecimal.cpp` 加除法测试**

```cpp
TEST(divide, basic) {
    const hpc::Context ctx{10, hpc::RoundingMode::HalfEven};
    CHECK_EQ(BigDecimal::divide(BigDecimal::fromString("10"), BigDecimal::fromString("4"), ctx).toString(),
             std::string("2.5"));
    CHECK_EQ(BigDecimal::divide(BigDecimal::fromString("1"), BigDecimal::fromString("2"), ctx).toString(),
             std::string("0.5"));
}

TEST(divide, repeating) {
    const hpc::Context ctx{10, hpc::RoundingMode::HalfEven};
    // 1/7 = 0.142857142857... 保留 10 位
    CHECK_EQ(BigDecimal::divide(BigDecimal::fromString("1"), BigDecimal::fromString("7"), ctx).toString(),
             std::string("0.1428571429"));
}

TEST(divide, divide_by_zero) {
    const hpc::Context ctx{10, hpc::RoundingMode::HalfEven};
    bool threw = false;
    try {
        BigDecimal::divide(BigDecimal::fromString("1"), BigDecimal::fromString("0"), ctx);
    } catch (const std::domain_error&) { threw = true; }
    CHECK(threw);
}

TEST(divide, rounding_modes) {
    const BigDecimal third = BigDecimal::fromString("0.333333");
    CHECK_EQ(BigDecimal::floor(third, hpc::Context{6, hpc::RoundingMode::Floor}).toString(),
             std::string("0.333333"));
    CHECK_EQ(BigDecimal::ceil(third, hpc::Context{6, hpc::RoundingMode::Ceil}).toString(),
             std::string("0.333334"));
}
```

- [ ] **Step 5: 写 `tests/div_probe.cpp` 并在 CMake 注册**

```cpp
// 除法探针：读入若干行 "a b"，输出每行 a/b 的结果（20 位精度）。
#include <iostream>
#include <string>

#include "core/BigDecimal.h"

int main() {
    std::ios::sync_with_stdio(false);
    std::string a, b;
    while (std::cin >> a >> b) {
        try {
            const hpc::BigDecimal quotient = hpc::BigDecimal::divide(
                hpc::BigDecimal::fromString(a), hpc::BigDecimal::fromString(b),
                hpc::Context{20, hpc::RoundingMode::HalfEven});
            std::cout << quotient.toString() << "\n";
        } catch (const std::exception&) {
            std::cout << "ERROR\n";
        }
    }
    return 0;
}
```

在 `CMakeLists.txt` 的 `BUILD_TESTS` 块中加入：

```cmake
    add_executable(div_probe tests/div_probe.cpp)
    target_link_libraries(div_probe PRIVATE hpc_core)
```

- [ ] **Step 6: 写 `scripts/verify_div.py` 并运行差分验证**

```python
#!/usr/bin/env python3
"""用 Python decimal 校验 C++ 除法实现的正确性。"""
import random
import subprocess
import sys
from decimal import Decimal, getcontext, ROUND_HALF_EVEN

getcontext().prec = 200
random.seed(7)

cases = []
for _ in range(400):
    a = ''.join(random.choice('0123456789') for _ in range(random.randint(1, 60)))
    b = ''.join(random.choice('0123456789') for _ in range(random.randint(1, 30)))
    if b.strip('0') == '':
        b = '7'
    if a.strip('0') == '':
        a = '0'
    cases.append((a, b))

cases += [('1', '3'), ('10', '3'), ('1000', '7'), ('1', '2'), ('99999', '3')]

inp = '\n'.join(f"{a} {b}" for a, b in cases)
result = subprocess.run([sys.argv[1] if len(sys.argv) > 1 else './build/div_probe'],
                        input=inp, capture_output=True, text=True)
outputs = result.stdout.split()

failures = 0
for (a, b), got in zip(cases, outputs):
    expected = Decimal(a) / Decimal(b)
    exp_str = str(expected.quantize(Decimal(1).scaleb(-20), rounding=ROUND_HALF_EVEN))
    if '.' in exp_str:
        exp_str = exp_str.rstrip('0').rstrip('.')
    if got != exp_str:
        failures += 1
        if failures <= 5:
            print(f"MISMATCH {a} / {b}\n  got      {got}\n  expected {exp_str}")

print(f"\n除法差分测试：{len(cases) - failures}/{len(cases)} 通过")
sys.exit(1 if failures else 0)
```

运行：

```bash
./scripts/build-core.bat
python scripts/verify_div.py
```

**预期输出**：`除法差分测试：405/405 通过`。若不通过，**不得进入 Task 4**——先修正实现。

- [ ] **Step 7: 提交**

```bash
git add src/core tests/tst_bigdecimal.cpp tests/div_probe.cpp scripts/verify_div.py CMakeLists.txt
git commit -m "feat(core): 除法、舍入策略与显式 Context 精度"
```

---

### Task 4: 超越函数（sqrt / pow / exp / ln）

**Files:**
- Modify: `src/core/BigDecimal.h`
- Modify: `src/core/BigDecimal.cpp`
- Modify: `tests/tst_bigdecimal.cpp`
- Create: `tests/func_probe.cpp`
- Create: `scripts/verify_func.py`

**Interfaces:**
- Consumes: Task 2/3 的 `BigDecimal`、`Context`
- Produces:
  - `static BigDecimal sqrt(const BigDecimal& value, const Context& context)`（负数抛 `std::domain_error`）
  - `static BigDecimal pow(const BigDecimal& base, const BigDecimal& exponent, const Context& context)`
  - `static BigDecimal exp(const BigDecimal& value, const Context& context)`
  - `static BigDecimal ln(const BigDecimal& value, const Context& context)`（非正抛 `std::domain_error`）
  - `static BigDecimal log10(const BigDecimal& value, const Context& context)`

- [ ] **Step 1: 声明接口**

```cpp
    // 超越函数：本质上不精确，必须显式给出精度。
    static BigDecimal sqrt(const BigDecimal& value, const Context& context);
    static BigDecimal pow(const BigDecimal& base, const BigDecimal& exponent, const Context& context);
    static BigDecimal exp(const BigDecimal& value, const Context& context);
    static BigDecimal ln(const BigDecimal& value, const Context& context);
    static BigDecimal log10(const BigDecimal& value, const Context& context);
```

- [ ] **Step 2: 实现 `sqrt`（牛顿迭代）**

```cpp
BigDecimal BigDecimal::sqrt(const BigDecimal& value, const Context& context) {
    if (value.sign_ < 0) throw std::domain_error("BigDecimal: 负数没有实数平方根");
    if (value.isZero()) return BigDecimal();

    const int guard = context.precision + 5;
    // 把被开方数放大 10^(2*guard)，使迭代在整数上收敛。
    BigDecimal scaled = value;
    for (int i = 0; i < 2 * guard; ++i) {
        scaled = scaled * BigDecimal(10);
    }

    // 初值：取 10^k 使 10^(2k) 接近 scaled 的量级。
    const std::string text = scaled.toString();
    const std::size_t dot = text.find('.');
    const int magnitude = static_cast<int>(dot == std::string::npos ? text.size() : dot);
    int exponent = magnitude / 2;

    BigDecimal guess(1);
    for (int i = 0; i < exponent; ++i) guess = guess * BigDecimal(10);

    // 牛顿迭代 x <- (x + n/x) / 2
    for (int iteration = 0; iteration < guard + 10; ++iteration) {
        const BigDecimal quotient = divide(scaled, guess, Context{guard + 5, RoundingMode::HalfEven});
        const BigDecimal next = (guess + quotient) * BigDecimal::fromString("0.5");
        if (next.compare(guess) == 0) break;
        guess = next;
    }

    // 还原：sqrt(value * 10^(2*guard)) = sqrt(value) * 10^guard
    BigDecimal result = guess;
    for (int i = 0; i < guard; ++i) result = result * BigDecimal::fromString("0.1");
    result.normalize();
    return roundToPrecision(result, context);
}
```

- [ ] **Step 3: 实现 `exp` 与 `ln`**

```cpp
namespace {
// 自然对数的常数，保留 60 位有效数字。
const char* const kLn10Digits =
    "2.302585092994045684017991454684364207601101488628772976033327900967572609677352480235997205089598298";
}  // namespace

BigDecimal BigDecimal::exp(const BigDecimal& value, const Context& context) {
    const int guard = context.precision + 5;
    // 减小 |x| 提升收敛速度：exp(x) = exp(x/2^k)^(2^k)
    BigDecimal scaled = value;
    int halvings = 0;
    while (scaled.compare(BigDecimal::fromString("0.5")) > 0 && halvings < 64) {
        scaled = scaled * BigDecimal::fromString("0.5");
        ++halvings;
    }

    // 泰勒级数 sum(x^n / n!)
    BigDecimal term("1");
    BigDecimal sum("1");
    const BigDecimal limit = BigDecimal::fromString("0.0000000001");
    for (int n = 1; n < guard * 4 + 64; ++n) {
        term = divide(term * scaled, BigDecimal(n), Context{guard, RoundingMode::HalfEven});
        sum = sum + term;
        if (term.compare(limit) < 0) break;
    }

    // 还原
    for (int i = 0; i < halvings; ++i) sum = sum * sum;
    return roundToPrecision(sum, context);
}

BigDecimal BigDecimal::ln(const BigDecimal& value, const Context& context) {
    if (value.sign_ <= 0) throw std::domain_error("BigDecimal: 对数要求正数");
    if (value.equals(BigDecimal(1))) return BigDecimal();

    const int guard = context.precision + 5;
    const BigDecimal kLn10 = BigDecimal::fromString(kLn10Digits);

    // 用开方把 x 拉进 (0.7, 1.4] 区间，提高级数收敛速度。
    // ln(x) = 2^k * ln(x^(1/2^k))
    BigDecimal scaled = value;
    int halvings = 0;
    while (halvings < 64 &&
           (scaled.compare(BigDecimal::fromString("0.7")) > 0 ||
            scaled.compare(BigDecimal::fromString("0.8")) < 0)) {
        scaled = sqrt(scaled, Context{guard, RoundingMode::HalfEven});
        ++halvings;
    }

    // z = (x-1)/(x+1)
    const BigDecimal z = divide(scaled - BigDecimal(1), scaled + BigDecimal(1),
                                Context{guard, RoundingMode::HalfEven});
    const BigDecimal z2 = z * z;

    // sum = z + z^3/3 + z^5/5 + ...
    BigDecimal power = z;
    BigDecimal sum = z;
    const BigDecimal limit = BigDecimal::fromString("0.0000000001");
    for (int k = 1; k < guard * 4 + 64; ++k) {
        power = power * z2;
        const BigDecimal addend = divide(power, BigDecimal(2 * k + 1),
                                         Context{guard, RoundingMode::HalfEven});
        sum = sum + addend;
        if (addend.compare(limit) < 0) break;
    }

    // ln(x) = 2^k * sum
    BigDecimal result = sum;
    for (int i = 0; i < halvings; ++i) result = result * BigDecimal(2);

    // 把结果拉回 (-ln10, ln10] 主值区间，再补回 offset * ln(10)。
    BigDecimal offset("0");
    while (result.compare(kLn10) >= 0) { result = result - kLn10; offset = offset + BigDecimal(1); }
    while (result.sign_ < 0) { result = result + kLn10; offset = offset - BigDecimal(1); }
    result = result + offset * kLn10;

    return roundToPrecision(result, context);
}

BigDecimal BigDecimal::log10(const BigDecimal& value, const Context& context) {
    const BigDecimal naturalLog = ln(value, context);
    const BigDecimal kLn10 = BigDecimal::fromString(kLn10Digits);
    return roundToPrecision(divide(naturalLog, kLn10, context), context);
}
```

- [ ] **Step 4: 实现 `pow`**

```cpp
BigDecimal BigDecimal::pow(const BigDecimal& base, const BigDecimal& exponent, const Context& context) {
    // 整数指数走快速幂，结果精确。
    if (exponent.scale_ == 0) {
        std::string text = exponent.toString();
        bool negative = false;
        std::size_t i = 0;
        if (i < text.size() && (text[i] == '-' || text[i] == '+')) { negative = text[i] == '-'; ++i; }
        long long power = 0;
        for (; i < text.size(); ++i) power = power * 10 + (text[i] - '0');

        if (negative) {
            BigDecimal positive(1);
            long long magnitude = -power;
            BigDecimal factor = base;
            while (magnitude > 0) {
                if (magnitude & 1) positive = positive * factor;
                factor = factor * factor;
                magnitude >>= 1;
            }
            return roundToPrecision(divide(BigDecimal(1), positive, context), context);
        }

        BigDecimal result(1);
        BigDecimal factor = base;
        while (power > 0) {
            if (power & 1) result = result * factor;
            factor = factor * factor;
            power >>= 1;
        }
        return result;
    }

    // 非整数指数：pow(a,b) = exp(b * ln(a))
    if (base.sign_ <= 0) throw std::domain_error("BigDecimal: 负底数的非整数次幂未定义");
    const BigDecimal naturalLog = ln(base, context);
    return roundToPrecision(exp(naturalLog * exponent, context), context);
}
```

- [ ] **Step 5: 写 `tests/func_probe.cpp` 与 `scripts/verify_func.py`**

```cpp
// 函数探针：读入 "op a b"，输出 sqrt/exp/ln/pow 的结果。
#include <iostream>
#include <string>

#include "core/BigDecimal.h"

using hpc::BigDecimal;
using hpc::Context;
using hpc::RoundingMode;

int main() {
    std::ios::sync_with_stdio(false);
    std::string op, a, b;
    const Context ctx{20, RoundingMode::HalfEven};
    while (std::cin >> op >> a >> b) {
        try {
            BigDecimal result;
            if (op == "sqrt") result = BigDecimal::sqrt(BigDecimal::fromString(a), ctx);
            else if (op == "exp") result = BigDecimal::exp(BigDecimal::fromString(a), ctx);
            else if (op == "ln") result = BigDecimal::ln(BigDecimal::fromString(a), ctx);
            else if (op == "pow") result = BigDecimal::pow(BigDecimal::fromString(a), BigDecimal::fromString(b), ctx);
            std::cout << result.toString() << "\n";
        } catch (const std::exception&) {
            std::cout << "ERROR\n";
        }
    }
    return 0;
}
```

在 `CMakeLists.txt` 的 `BUILD_TESTS` 块加入：

```cmake
    add_executable(func_probe tests/func_probe.cpp)
    target_link_libraries(func_probe PRIVATE hpc_core)
```

`scripts/verify_func.py`：

```python
#!/usr/bin/env python3
"""用 Python decimal 校验 C++ 超越函数实现。"""
import random
import subprocess
import sys
from decimal import Decimal, getcontext, ROUND_HALF_EVEN

getcontext().prec = 300
random.seed(11)

PREC = 20


def fmt(value: Decimal) -> str:
    quantized = value.quantize(Decimal(1).scaleb(-PREC), rounding=ROUND_HALF_EVEN)
    text = format(quantized, 'f')
    if '.' in text:
        text = text.rstrip('0').rstrip('.')
    return text or '0'


cases = []
for _ in range(120):
    cases.append(('sqrt', f"{random.uniform(0.01, 10000):.6f}", '0'))
    cases.append(('exp', f"{random.uniform(-10, 10):.6f}", '0'))
    cases.append(('ln', f"{random.uniform(0.001, 1000):.6f}", '0'))
    cases.append(('pow', f"{random.uniform(0.1, 100):.6f}", f"{random.uniform(0.1, 5):.6f}"))

cases += [('sqrt', '2', '0'), ('sqrt', '4', '0'), ('exp', '0', '0'), ('exp', '1', '0'),
          ('ln', '1', '0'), ('ln', '2', '0'), ('ln', '10', '0'), ('pow', '2', '10')]

inp = '\n'.join(f"{op} {a} {b}" for op, a, b in cases)
result = subprocess.run([sys.argv[1] if len(sys.argv) > 1 else './build/func_probe'],
                        input=inp, capture_output=True, text=True)
outputs = result.stdout.split()

failures = 0
for (op, a, b), got in zip(cases, outputs):
    da, db = Decimal(a), Decimal(b)
    if op == 'sqrt':
        expected = da.sqrt()
    elif op == 'exp':
        expected = da.exp()
    elif op == 'ln':
        expected = da.ln()
    else:
        expected = da ** db
    expected_text = fmt(expected)
    if got != expected_text:
        failures += 1
        if failures <= 8:
            print(f"MISMATCH {op} {a} {b}\n  got      {got}\n  expected {expected_text}")

print(f"\n函数差分测试：{len(cases) - failures}/{len(cases)} 通过")
sys.exit(1 if failures else 0)
```

**注意**：Python `Decimal` 在 `prec=300` 下计算后再量化到 20 位，与 C++ 端「先算到 25 位再量化」可能有末位差异。若出现少量末位不一致，判断是否为 1 ulp；持续偏差则修正 C++ 实现。

- [ ] **Step 6: 构建并验证**

```bash
./scripts/build-core.bat
python scripts/verify_func.py
```
预期：`函数差分测试：496/496 通过`（或末位 1 ulp 差异在合理范围）。

- [ ] **Step 7: 提交**

```bash
git add src/core tests/func_probe.cpp scripts/verify_func.py CMakeLists.txt tests/tst_bigdecimal.cpp
git commit -m "feat(core): sqrt/pow/exp/ln 超越函数实现"
```

---

### Task 5: 进制格式化与位运算

**Files:**
- Create: `src/core/Format.h`
- Create: `src/core/Format.cpp`
- Modify: `src/core/BigDecimal.h`（新增 `isInteger` 与 `integerBlocks`）
- Modify: `src/core/BigDecimal.cpp`
- Modify: `tests/tst_format.cpp`

**Interfaces:**
- Consumes: Task 2 的 `BigDecimal`
- Produces:
  - `namespace hpc`
  - `std::string toBaseString(const BigDecimal& value, int base, bool uppercase = false)`（base ∈ 2..36；仅整数精确转换）
  - `BigDecimal fromBaseString(std::string_view text, int base)`（仅整数）
  - `enum class BitOp { And, Or, Xor }`
  - `BigDecimal bitwise(const BigDecimal& lhs, const BigDecimal& rhs, BitOp op)`（仅整数精确）
  - `BigDecimal bitwiseNot(const BigDecimal& value)`（仅整数精确）
  - `BigDecimal shiftLeft(const BigDecimal& value, int bits)`、`shiftRight(const BigDecimal& value, int bits)`
  - `BigDecimal::isInteger()`、`BigDecimal::integerBlocks()`

- [ ] **Step 1: 在 `BigDecimal.h` 加 `isInteger` 与 `integerBlocks`**

```cpp
    // 是否为整数（无小数位）。
    bool isInteger() const { return scale_ == 0; }
    // 整数部分的 base-10^9 小端块。非整数时抛 std::domain_error。
    const std::vector<std::uint32_t>& integerBlocks() const;
```

在 `BigDecimal.cpp` 实现：

```cpp
const std::vector<std::uint32_t>& BigDecimal::integerBlocks() const {
    if (scale_ != 0) throw std::domain_error("BigDecimal: 该运算只支持整数");
    return digits_;
}
```

- [ ] **Step 2: 写 `src/core/Format.h`**

```cpp
// 进制转换与位运算：只对整数精确支持。
#pragma once

#include <string>
#include <string_view>

#include "core/BigDecimal.h"

namespace hpc {

// 把整数转换为 base 进制字符串。base 取值 2..36。
// 非整数输入抛 std::domain_error。
std::string toBaseString(const BigDecimal& value, int base, bool uppercase = false);

// 解析 base 进制整数字符串。base 取值 2..36。
// 含小数点或非法字符时抛 std::invalid_argument。
BigDecimal fromBaseString(std::string_view text, int base);

enum class BitOp { And, Or, Xor };

// 整数位运算。任一操作数非整数时抛 std::domain_error。
BigDecimal bitwise(const BigDecimal& lhs, const BigDecimal& rhs, BitOp op);
// 按位取反。
BigDecimal bitwiseNot(const BigDecimal& value);
// 左移 bits 位（相当于乘以 2^bits）。
BigDecimal shiftLeft(const BigDecimal& value, int bits);
// 右移 bits 位（相当于整除 2^bits）。
BigDecimal shiftRight(const BigDecimal& value, int bits);

}  // namespace hpc
```

- [ ] **Step 3: 写 `src/core/Format.cpp`**

```cpp
#include "core/Format.h"

#include <algorithm>
#include <stdexcept>

namespace hpc {
namespace {

const char* kDigitsLower = "0123456789abcdefghijklmnopqrstuvwxyz";
const char* kDigitsUpper = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

constexpr std::uint64_t kBase = 1000000000ULL;

// 把小端 base-10^9 块转为十进制字符串（高位在前）。
std::string toDecimalString(const std::vector<std::uint32_t>& blocks) {
    if (blocks.size() == 1 && blocks[0] == 0) return "0";
    std::string digits = std::to_string(blocks.back());
    char buffer[16];
    for (std::size_t i = blocks.size() - 1; i-- > 0;) {
        std::snprintf(buffer, sizeof buffer, "%09u", blocks[i]);
        digits += buffer;
    }
    return digits;
}

// 十进制字符串（小端块表示的整数）转回 base-10^9 小端块。
std::vector<std::uint32_t> blocksFromDecimal(const std::string& text) {
    std::vector<std::uint32_t> blocks;
    std::size_t end = text.size();
    while (end > 0) {
        const std::size_t start = end >= 9 ? end - 9 : 0;
        std::uint32_t value = 0;
        for (std::size_t i = start; i < end; ++i) {
            value = value * 10 + static_cast<std::uint32_t>(text[i] - '0');
        }
        blocks.push_back(value);
        end = start;
    }
    while (blocks.size() > 1 && blocks.back() == 0) blocks.pop_back();
    if (blocks.empty()) blocks.push_back(0);
    return blocks;
}

// 小端 base-10^9 块转二进制字符串（高位在前）。
std::string toBinaryString(const std::vector<std::uint32_t>& blocks) {
    std::string bits;
    for (std::size_t i = blocks.size(); i-- > 0;) {
        const std::uint32_t value = blocks[i];
        for (int bit = 31; bit >= 0; --bit) {
            bits.push_back(((value >> bit) & 1u) ? '1' : '0');
        }
    }
    const std::size_t first = bits.find('1');
    if (first == std::string::npos) return "0";
    return bits.substr(first);
}

// 二进制字符串转回 base-10^9 小端块。
std::vector<std::uint32_t> blocksFromBinary(const std::string& bits) {
    std::vector<std::uint32_t> value{0};
    for (std::size_t i = bits.size(); i-- > 0;) {
        std::uint64_t carry = bits[i] == '1' ? 1 : 0;
        for (std::size_t k = 0; k < value.size(); ++k) {
            const std::uint64_t cur = static_cast<std::uint64_t>(value[k]) * 2 + carry;
            value[k] = static_cast<std::uint32_t>(cur % kBase);
            carry = cur / kBase;
        }
        if (carry) value.push_back(static_cast<std::uint32_t>(carry));
    }
    while (value.size() > 1 && value.back() == 0) value.pop_back();
    return value;
}

}  // namespace

std::string toBaseString(const BigDecimal& value, int base, bool uppercase) {
    if (base < 2 || base > 36) throw std::invalid_argument("toBaseString: base 必须在 2..36");
    if (!value.isInteger()) throw std::domain_error("toBaseString: 只支持整数");
    if (value.isZero()) return "0";

    const char* table = uppercase ? kDigitsUpper : kDigitsLower;
    std::vector<std::uint32_t> current = value.integerBlocks();
    std::string out;
    while (!(current.size() == 1 && current[0] == 0)) {
        std::uint64_t remainder = 0;
        std::vector<std::uint32_t> quotient(current.size(), 0);
        for (std::size_t i = current.size(); i-- > 0;) {
            const std::uint64_t value64 = remainder * kBase + current[i];
            quotient[i] = static_cast<std::uint32_t>(value64 / base);
            remainder = value64 % base;
        }
        out.push_back(table[remainder]);
        while (quotient.size() > 1 && quotient.back() == 0) quotient.pop_back();
        current = quotient;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

BigDecimal fromBaseString(std::string_view text, int base) {
    if (base < 2 || base > 36) throw std::invalid_argument("fromBaseString: base 必须在 2..36");
    if (text.find('.') != std::string_view::npos) {
        throw std::invalid_argument("fromBaseString: 只支持整数");
    }
    if (text.empty()) throw std::invalid_argument("fromBaseString: 空字符串");

    std::vector<std::uint32_t> value{0};
    for (char c : text) {
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else throw std::invalid_argument(std::string("fromBaseString: 非法字符 '") + c + "'");
        if (digit >= base) throw std::invalid_argument("fromBaseString: 数字超出进制范围");

        // value = value * base
        std::uint64_t carry = 0;
        for (std::size_t k = 0; k < value.size(); ++k) {
            const std::uint64_t cur = static_cast<std::uint64_t>(value[k]) * base + carry;
            value[k] = static_cast<std::uint32_t>(cur % kBase);
            carry = cur / kBase;
        }
        if (carry) value.push_back(static_cast<std::uint32_t>(carry));

        // value += digit
        std::uint64_t add = static_cast<std::uint64_t>(digit);
        for (std::size_t k = 0; k < value.size() && add; ++k) {
            const std::uint64_t cur = static_cast<std::uint64_t>(value[k]) + add;
            value[k] = static_cast<std::uint32_t>(cur % kBase);
            add = cur / kBase;
        }
        if (add) value.push_back(static_cast<std::uint32_t>(add));
    }
    while (value.size() > 1 && value.back() == 0) value.pop_back();
    return BigDecimal::fromString(toDecimalString(value));
}

BigDecimal bitwise(const BigDecimal& lhs, const BigDecimal& rhs, BitOp op) {
    if (!lhs.isInteger() || !rhs.isInteger()) {
        throw std::domain_error("bitwise: 只支持整数");
    }
    const std::string a = toBinaryString(lhs.integerBlocks());
    const std::string b = toBinaryString(rhs.integerBlocks());
    const std::size_t width = std::max(a.size(), b.size());
    std::string result(width, '0');
    for (std::size_t i = 0; i < width; ++i) {
        const char ca = i < a.size() ? a[a.size() - 1 - i] : '0';
        const char cb = i < b.size() ? b[b.size() - 1 - i] : '0';
        switch (op) {
            case BitOp::And: result[width - 1 - i] = (ca == '1' && cb == '1') ? '1' : '0'; break;
            case BitOp::Or:  result[width - 1 - i] = (ca == '1' || cb == '1') ? '1' : '0'; break;
            case BitOp::Xor: result[width - 1 - i] = (ca != cb) ? '1' : '0'; break;
        }
    }
    return BigDecimal::fromString(toDecimalString(blocksFromBinary(result)));
}

BigDecimal bitwiseNot(const BigDecimal& value) {
    if (!value.isInteger()) throw std::domain_error("bitwiseNot: 只支持整数");
    // ~x = -x - 1
    return (BigDecimal::fromString("-1") - value) - BigDecimal(1);
}

BigDecimal shiftLeft(const BigDecimal& value, int bits) {
    if (!value.isInteger()) throw std::domain_error("shiftLeft: 只支持整数");
    if (bits < 0) throw std::invalid_argument("shiftLeft: 位数不能为负");
    BigDecimal result = value;
    for (int i = 0; i < bits; ++i) result = result * BigDecimal(2);
    return result;
}

BigDecimal shiftRight(const BigDecimal& value, int bits) {
    if (!value.isInteger()) throw std::domain_error("shiftRight: 只支持整数");
    if (bits < 0) throw std::invalid_argument("shiftRight: 位数不能为负");
    BigDecimal result = value;
    for (int i = 0; i < bits; ++i) {
        result = BigDecimal::divide(result, BigDecimal(2), Context{1000000, RoundingMode::Down});
    }
    return result;
}

}  // namespace hpc
```

- [ ] **Step 4: 写 `tests/tst_format.cpp`**

```cpp
#include "test_framework.h"

#include <stdexcept>

#include "core/Format.h"

using hpc::BigDecimal;

TEST(base, to_hex) {
    CHECK_EQ(hpc::toBaseString(BigDecimal::fromString("255"), 16), std::string("ff"));
    CHECK_EQ(hpc::toBaseString(BigDecimal::fromString("255"), 16, true), std::string("FF"));
    CHECK_EQ(hpc::toBaseString(BigDecimal::fromString("0"), 2), std::string("0"));
    CHECK_EQ(hpc::toBaseString(BigDecimal::fromString("10"), 2), std::string("1010"));
    CHECK_EQ(hpc::toBaseString(BigDecimal::fromString("64"), 8), std::string("100"));
}

TEST(base, round_trip) {
    const std::string original = "123456789012345678901234567890";
    const BigDecimal value = BigDecimal::fromString(original);
    for (int base : {2, 8, 10, 16, 36}) {
        const std::string encoded = hpc::toBaseString(value, base);
        CHECK_EQ(hpc::fromBaseString(encoded, base).toString(), original);
    }
}

TEST(base, from_base) {
    CHECK_EQ(hpc::fromBaseString("ff", 16).toString(), std::string("255"));
    CHECK_EQ(hpc::fromBaseString("1010", 2).toString(), std::string("10"));
    CHECK_EQ(hpc::fromBaseString("777", 8).toString(), std::string("511"));
}

TEST(bitwise, and_or_xor) {
    CHECK_EQ(hpc::bitwise(BigDecimal::fromString("255"), BigDecimal::fromString("15"), hpc::BitOp::And).toString(),
             std::string("15"));
    CHECK_EQ(hpc::bitwise(BigDecimal::fromString("255"), BigDecimal::fromString("15"), hpc::BitOp::Or).toString(),
             std::string("255"));
    CHECK_EQ(hpc::bitwise(BigDecimal::fromString("255"), BigDecimal::fromString("15"), hpc::BitOp::Xor).toString(),
             std::string("240"));
    CHECK_EQ(hpc::bitwise(BigDecimal::fromString("0"), BigDecimal::fromString("0"), hpc::BitOp::Xor).toString(),
             std::string("0"));
}

TEST(bitwise, shift) {
    CHECK_EQ(hpc::shiftLeft(BigDecimal::fromString("1"), 8).toString(), std::string("256"));
    CHECK_EQ(hpc::shiftRight(BigDecimal::fromString("256"), 8).toString(), std::string("1"));
}

TEST(bitwise, rejects_fraction) {
    bool threw = false;
    try {
        hpc::bitwise(BigDecimal::fromString("1.5"), BigDecimal::fromString("1"), hpc::BitOp::And);
    } catch (const std::domain_error&) { threw = true; }
    CHECK(threw);
}
```

- [ ] **Step 5: 构建并运行测试**

```bash
./scripts/build-core.bat && ./scripts/run-ctest.bat
```
预期：全部通过。

- [ ] **Step 6: 提交**

```bash
git add src/core tests/tst_format.cpp
git commit -m "feat(core): 进制转换与整数位运算"
```

---

### Task 6: 表达式求值器

**Files:**
- Create: `src/core/Expression.h`
- Create: `src/core/Expression.cpp`
- Modify: `tests/tst_expression.cpp`

**Interfaces:**
- Consumes: Task 2/3/4/5 的 `BigDecimal`、`Context`、`toBaseString`、`fromBaseString`
- Produces:
  - `class ExpressionError : public std::runtime_error`（携带 `int position()`）
  - `BigDecimal evaluate(std::string_view expression, const Context& context)`
  - 支持：数字、`0x`/`0b`/`0o` 前缀整数、`+ - * / %`、一元正负、括号、函数 `sqrt abs pow exp ln log10`

- [ ] **Step 1: 写 `src/core/Expression.h`**

```cpp
// 表达式解析与求值：递归下降。
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "core/BigDecimal.h"
#include "core/Rounding.h"

namespace hpc {

// 表达式错误：携带出错字符在原串中的位置（从 0 开始）。
class ExpressionError : public std::runtime_error {
public:
    ExpressionError(const std::string& message, int position)
        : std::runtime_error(message), position_(position) {}
    int position() const { return position_; }

private:
    int position_;
};

// 求值表达式。语法错误抛 ExpressionError。
BigDecimal evaluate(std::string_view expression, const Context& context);

}  // namespace hpc
```

- [ ] **Step 2: 写 `src/core/Expression.cpp`**

```cpp
#include "core/Expression.h"

#include <cctype>
#include <string>
#include <vector>

#include "core/Format.h"

namespace hpc {
namespace {

// 递归下降解析器。
class Parser {
public:
    Parser(std::string_view text, const Context& context)
        : text_(text), context_(context) {}

    BigDecimal parse() {
        skipSpaces();
        BigDecimal value = parseExpression();
        skipSpaces();
        if (position_ != text_.size()) {
            fail("多余的字符");
        }
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw ExpressionError(message, static_cast<int>(position_));
    }

    void skipSpaces() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
    }

    bool consume(char c) {
        skipSpaces();
        if (position_ < text_.size() && text_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!consume(c)) {
            fail(std::string("期望 '") + c + "'");
        }
    }

    // expression := term (('+' | '-') term)*
    BigDecimal parseExpression() {
        BigDecimal value = parseTerm();
        while (true) {
            skipSpaces();
            if (consume('+')) {
                value = value + parseTerm();
            } else if (consume('-')) {
                value = value - parseTerm();
            } else {
                return value;
            }
        }
    }

    // term := unary (('*' | '/' | '%') unary)*
    BigDecimal parseTerm() {
        BigDecimal value = parseUnary();
        while (true) {
            skipSpaces();
            if (consume('*')) {
                value = value * parseUnary();
            } else if (consume('/')) {
                const BigDecimal divisor = parseUnary();
                if (divisor.isZero()) fail("除数为零");
                value = BigDecimal::divide(value, divisor, context_);
            } else if (consume('%')) {
                const BigDecimal divisor = parseUnary();
                if (divisor.isZero()) fail("取余的除数为零");
                value = BigDecimal::modulo(value, divisor);
            } else {
                return value;
            }
        }
    }

    // unary := ('+' | '-') unary | primary
    BigDecimal parseUnary() {
        skipSpaces();
        if (consume('+')) return parseUnary();
        if (consume('-')) return BigDecimal() - parseUnary();
        return parsePrimary();
    }

    // primary := number | '(' expression ')' | func '(' args ')'
    BigDecimal parsePrimary() {
        skipSpaces();
        if (position_ >= text_.size()) fail("表达式意外结束");

        const char c = text_[position_];
        if (c == '(') {
            ++position_;
            BigDecimal value = parseExpression();
            expect(')');
            return value;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            return parseNumber();
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            return parseIdentifier();
        }
        fail("意外的字符");
    }

    // 支持 0x / 0b / 0o 前缀的整数，以及十进制小数。
    BigDecimal parseNumber() {
        const std::size_t start = position_;
        if (text_[position_] == '0' && position_ + 1 < text_.size()) {
            const char marker = static_cast<char>(std::tolower(
                static_cast<unsigned char>(text_[position_ + 1])));
            int base = 0;
            if (marker == 'x') base = 16;
            else if (marker == 'b') base = 2;
            else if (marker == 'o') base = 8;
            if (base != 0) {
                position_ += 2;
                const std::size_t digitsStart = position_;
                while (position_ < text_.size() &&
                       std::isalnum(static_cast<unsigned char>(text_[position_]))) {
                    ++position_;
                }
                const std::string digits(text_.substr(digitsStart, position_ - digitsStart));
                if (digits.empty()) fail("进制前缀后缺少数字");
                try {
                    return fromBaseString(digits, base);
                } catch (const std::exception&) {
                    fail("非法的进制数字");
                }
            }
        }

        bool seenDot = false;
        while (position_ < text_.size()) {
            const char d = text_[position_];
            if (std::isdigit(static_cast<unsigned char>(d))) {
                ++position_;
            } else if (d == '.' && !seenDot) {
                seenDot = true;
                ++position_;
            } else {
                break;
            }
        }
        const std::string literal(text_.substr(start, position_ - start));
        try {
            return BigDecimal::fromString(literal);
        } catch (const std::exception&) {
            fail("非法的数字");
        }
    }

    // 标识符：函数名。
    BigDecimal parseIdentifier() {
        const std::size_t start = position_;
        while (position_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[position_])) ||
                text_[position_] == '_')) {
            ++position_;
        }
        const std::string name(text_.substr(start, position_ - start));

        skipSpaces();
        if (position_ < text_.size() && text_[position_] == '(') {
            ++position_;
            std::vector<BigDecimal> args;
            skipSpaces();
            if (!(position_ < text_.size() && text_[position_] == ')')) {
                args.push_back(parseExpression());
                while (consume(',')) args.push_back(parseExpression());
            }
            expect(')');
            return callFunction(name, args);
        }
        fail("未知的标识符 '" + name + "'");
    }

    BigDecimal callFunction(const std::string& name, const std::vector<BigDecimal>& args) {
        auto need = [&](std::size_t count) {
            if (args.size() != count) {
                fail("函数 '" + name + "' 需要 " + std::to_string(count) + " 个参数");
            }
        };
        if (name == "sqrt") {
            need(1);
            return BigDecimal::sqrt(args[0], context_);
        }
        if (name == "abs") {
            need(1);
            return BigDecimal::abs(args[0]);
        }
        if (name == "exp") {
            need(1);
            return BigDecimal::exp(args[0], context_);
        }
        if (name == "ln") {
            need(1);
            return BigDecimal::ln(args[0], context_);
        }
        if (name == "log10") {
            need(1);
            return BigDecimal::log10(args[0], context_);
        }
        if (name == "pow") {
            need(2);
            return BigDecimal::pow(args[0], args[1], context_);
        }
        fail("未知的函数 '" + name + "'");
    }

    std::string_view text_;
    const Context& context_;
    std::size_t position_ = 0;
};

}  // namespace

BigDecimal evaluate(std::string_view expression, const Context& context) {
    Parser parser(expression, context);
    return parser.parse();
}

}  // namespace hpc
```

- [ ] **Step 3: 写 `tests/tst_expression.cpp`**

```cpp
#include "test_framework.h"

#include "core/Expression.h"

using hpc::Context;
using hpc::RoundingMode;

namespace {
BigDecimal eval(const std::string& text) {
    return hpc::evaluate(text, Context{20, RoundingMode::HalfEven});
}
}  // namespace

TEST(expression, arithmetic) {
    CHECK_EQ(eval("1+2").toString(), std::string("3"));
    CHECK_EQ(eval("2*3+4").toString(), std::string("10"));
    CHECK_EQ(eval("2+3*4").toString(), std::string("14"));
    CHECK_EQ(eval("(2+3)*4").toString(), std::string("20"));
    CHECK_EQ(eval("10-4-3").toString(), std::string("3"));
    CHECK_EQ(eval("100/4").toString(), std::string("25"));
    CHECK_EQ(eval("7%3").toString(), std::string("1"));
}

TEST(expression, unary_and_decimals) {
    CHECK_EQ(eval("-5").toString(), std::string("-5"));
    CHECK_EQ(eval("--5").toString(), std::string("5"));
    CHECK_EQ(eval("0.1+0.2").toString(), std::string("0.3"));
    CHECK_EQ(eval("1.5*2").toString(), std::string("3"));
}

TEST(expression, functions) {
    CHECK_EQ(eval("sqrt(9)").toString(), std::string("3"));
    CHECK_EQ(eval("abs(-3)").toString(), std::string("3"));
    CHECK_EQ(eval("pow(2,10)").toString(), std::string("1024"));
    CHECK_EQ(eval("ln(1)").toString(), std::string("0"));
}

TEST(expression, bases) {
    CHECK_EQ(eval("0xFF").toString(), std::string("255"));
    CHECK_EQ(eval("0b1010").toString(), std::string("10"));
    CHECK_EQ(eval("0xFF + 1").toString(), std::string("256"));
}

TEST(expression, errors) {
    bool threw = false;
    try { eval("1+"); } catch (const hpc::ExpressionError& e) { threw = true; CHECK(e.position() >= 0); }
    CHECK(threw);

    threw = false;
    try { eval("(1+2"); } catch (const hpc::ExpressionError&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { eval("1/0"); } catch (const hpc::ExpressionError&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { eval("unknown(1)"); } catch (const hpc::ExpressionError&) { threw = true; }
    CHECK(threw);
}
```

- [ ] **Step 4: 构建并运行测试**

```bash
./scripts/build-core.bat && ./scripts/run-ctest.bat
```
预期：全部通过。

- [ ] **Step 5: 提交**

```bash
git add src/core/Expression.h src/core/Expression.cpp tests/tst_expression.cpp
git commit -m "feat(core): 表达式解析与求值器"
```

---

### Task 7: 命令行前端

**Files:**
- Create: `src/cli/main.cpp`（重写）

**Interfaces:**
- Consumes: Task 2-6 的 `hpc::evaluate`、`BigDecimal`、`Context`、`toBaseString`
- Produces: 可执行文件 `hpc_cli`，三种模式：
  - `hpc_cli` —— 交互式
  - `hpc_cli -e "<表达式>"` —— 单次求值
  - `hpc_cli --oj` —— 批量 OJ 协议（与原命令行版完全一致）

- [ ] **Step 1: 写 `src/cli/main.cpp`**

```cpp
// 命令行前端：与 Web 前端、Qt 界面共用 src/core 的同一套内核。
#include <iostream>
#include <string>

#include "core/BigDecimal.h"
#include "core/Expression.h"
#include "core/Format.h"
#include "core/Rounding.h"

namespace {

using hpc::BigDecimal;
using hpc::Context;
using hpc::RoundingMode;

void printUsage() {
    std::cout <<
        R"(高精度计算器 · 命令行前端

用法:
  hpc_cli                        进入交互模式
  hpc_cli -e "<表达式>"          求值单个表达式后退出
  hpc_cli --oj                   批量模式（OJ 协议）

表达式语法:
  数字        123  0.5  0xFF  0b1010  0o777
  运算符      + - * / %   以及括号与一元正负号
  函数        sqrt(x) abs(x) pow(x,y) exp(x) ln(x) log10(x)

选项:
  -p, --precision <n>   有效数字位数（默认 30）
  -b, --base <n>        输出进制，2..36（默认 10）
  -h, --help            显示本帮助

OJ 批量协议（与原命令行版一致）:
  首行是测试组数，随后每行是 "运算符 数字1 数字2"。
  运算符非法或除数过小（绝对值 < 0.000001）时输出 ERROR。
)";
}

// OJ 模式：与 legacy/oj_console_edition 的协议完全一致。
int runOjMode() {
    const Context context{10, RoundingMode::HalfEven};
    int totalCases = 0;
    if (!(std::cin >> totalCases)) return 0;

    for (int i = 0; i < totalCases; ++i) {
        char mode = '+';
        std::cin >> mode;
        if (mode != '+' && mode != '-' && mode != '*' && mode != '/') {
            std::cout << "ERROR\n";
            continue;
        }
        std::string leftText, rightText;
        std::cin >> leftText >> rightText;

        try {
            const BigDecimal left = BigDecimal::fromString(leftText);
            const BigDecimal right = BigDecimal::fromString(rightText);
            BigDecimal result;
            if (mode == '+') result = left + right;
            else if (mode == '-') result = left - right;
            else if (mode == '*') result = left * right;
            else {
                // 除数过小则报错，与原实现一致。
                if (right.isZero() ||
                    BigDecimal::abs(right).compare(
                        BigDecimal::fromString("0.000001")) < 0) {
                    std::cout << "ERROR\n";
                    continue;
                }
                result = BigDecimal::divide(left, right, context);
            }
            std::cout << result.toString() << "\n";
        } catch (const std::exception&) {
            std::cout << "ERROR\n";
        }
    }
    return 0;
}

int runInteractive(const Context& context, int base) {
    std::cout << "高精度计算器 · 交互模式（输入表达式，Ctrl+D 或空行退出）\n";
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) break;
        try {
            const BigDecimal result = hpc::evaluate(line, context);
            if (base == 10) {
                std::cout << result.toString() << "\n";
            } else {
                std::cout << hpc::toBaseString(result, base) << "\n";
            }
        } catch (const hpc::ExpressionError& error) {
            std::cout << "错误：" << error.what() << "（位置 " << error.position() << "）\n";
        } catch (const std::exception& error) {
            std::cout << "错误：" << error.what() << "\n";
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Context context{30, RoundingMode::HalfEven};
    int base = 10;
    std::string expression;
    bool ojMode = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            printUsage();
            return 0;
        }
        if (argument == "--oj") {
            ojMode = true;
        } else if (argument == "-e" && i + 1 < argc) {
            expression = argv[++i];
        } else if ((argument == "-p" || argument == "--precision") && i + 1 < argc) {
            context.precision = std::stoi(argv[++i]);
        } else if ((argument == "-b" || argument == "--base") && i + 1 < argc) {
            base = std::stoi(argv[++i]);
        } else {
            std::cerr << "未知参数：" << argument << "\n";
            printUsage();
            return 2;
        }
    }

    if (ojMode) return runOjMode();

    if (!expression.empty()) {
        try {
            const BigDecimal result = hpc::evaluate(expression, context);
            if (base == 10) {
                std::cout << result.toString() << "\n";
            } else {
                std::cout << hpc::toBaseString(result, base) << "\n";
            }
            return 0;
        } catch (const hpc::ExpressionError& error) {
            std::cerr << "错误：" << error.what() << "（位置 " << error.position() << "）\n";
            return 1;
        } catch (const std::exception& error) {
            std::cerr << "错误：" << error.what() << "\n";
            return 1;
        }
    }

    return runInteractive(context, base);
}
```

- [ ] **Step 2: 构建并手工验证**

```bash
./scripts/build-core.bat
```

```bash
./build/hpc_cli -e "1/3"
```
预期：`0.333333333333333333333333333333`（30 位精度）。

```bash
./build/hpc_cli -e "sqrt(2)"
```
预期：`1.41421356237309504880168872421`（末位可能因实现细节差 1 ulp）。

```bash
./build/hpc_cli -e "0xFF + 0b1010"
```
预期：`265`。

```bash
printf '4\n+ 1 2\n- 5 3\n* 2.5 4\n/ 10 4\n' | ./build/hpc_cli --oj
```
预期依次输出：`3`、`2`、`10`、`2.5`。

**关键**：OJ 模式的输出必须与 legacy 完全一致。用 legacy 的可执行文件对照：

```bash
printf '4\n+ 1 2\n- 5 3\n* 2.5 4\n/ 10 4\n' | ./build/hpc_cli --oj > /tmp/new.txt
printf '4\n+ 1 2\n- 5 3\n* 2.5 4\n/ 10 4\n' | /tmp/hpc_baseline/oj.exe > /tmp/old.txt
diff /tmp/new.txt /tmp/old.txt && echo "OJ 协议一致"
```
预期：`OJ 协议一致`。若不一致，**必须修正到一致**——这是替换原 OJ 提交的前提。

- [ ] **Step 3: 提交**

```bash
git add src/cli/main.cpp
git commit -m "feat(cli): 交互/单次/OJ 三模式命令行前端"
```

---

### Task 8: Web 服务端与前端

**Files:**
- Copy: `src/core/Json.{h,cpp}`、`src/core/HttpServer.{h,cpp}` 从兄弟项目（改为 `namespace hpc`）
- Create: `src/api/ApiRoutes.h`
- Create: `src/api/ApiRoutes.cpp`
- Create: `src/server/main.cpp`
- Create: `web/index.html`、`web/styles.css`、`web/app.js`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2-6 的核心接口
- Produces: `hpc_server` 可执行文件，REST 端点：
  - `GET /api/health`
  - `POST /api/evaluate` body `{"expression": "...", "precision": 30}`
  - `POST /api/arithmetic` body `{"op": "+", "lhs": "...", "rhs": "...", "precision": 30}`
  - `POST /api/convert` body `{"value": "...", "from": 16, "to": 10}`
  - `POST /api/bitwise` body `{"op": "&", "lhs": "...", "rhs": "..."}`

- [ ] **Step 1: 复制 Json 与 HttpServer 到 `src/core/`**

```bash
S="/f/Projects/01_courses/cpp/VS_C++Project/Comprehensive-Calculation-Tool/src/core"
cp "$S/Json.h" "$S/Json.cpp" "$S/HttpServer.h" "$S/HttpServer.cpp" src/core/
```

改命名空间：`namespace calc` → `namespace hpc`，`namespace calc::json` → `namespace hpc::json`，
`namespace calc::http` → `namespace hpc::http`。同时把 `Json.h` 里的
`#include "Numeric.h"` 删除（Json 不需要 Numeric）。`HttpServer.cpp` 里的 Winsock
初始化代码保留原样（已在兄弟项目验证可用）。

- [ ] **Step 2: 在 `CMakeLists.txt` 加入 server 与 api 目标**

把 `hpc_core` 的源文件列表改为：

```cmake
add_library(hpc_core STATIC
    src/core/BigDecimal.cpp
    src/core/Format.cpp
    src/core/Expression.cpp
    src/core/Json.cpp
    src/core/HttpServer.cpp
)
```

追加：

```cmake
if(BUILD_SERVER)
    add_executable(hpc_server
        src/server/main.cpp
        src/api/ApiRoutes.cpp
        src/api/ApiRoutes.h
    )
    target_link_libraries(hpc_server PRIVATE hpc_core)
    if(WIN32)
        target_link_libraries(hpc_server PRIVATE ws2_32)
    endif()

    add_custom_target(web_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/web" "$<TARGET_FILE_DIR:hpc_server>/web"
        COMMENT "同步 Web 前端资源到构建目录")
    add_dependencies(hpc_server web_assets)
endif()
```

- [ ] **Step 3: 写 `src/api/ApiRoutes.h`**

```cpp
// REST API 层：把核心的运算能力映射为 JSON 接口。
#pragma once

#include <string>

namespace hpc {
namespace http {

class Server;

// 在服务器上注册全部 API 路由。
// 前端静态资源目录为 webRoot，用于服务 / 下的页面与脚本。
void registerApiRoutes(Server& server, const std::string& webRoot);

}  // namespace http
}  // namespace hpc
```

- [ ] **Step 4: 写 `src/api/ApiRoutes.cpp`**

**已核实的兄弟项目 API（不要自行猜测签名）：**

```cpp
// HttpServer.h
using Handler = std::function<Response(const Request&)>;
void route(const std::string& path, Handler handler);
void serveStatic(const std::string& prefix, const std::string& directory);
bool start(const std::string& host = "127.0.0.1", int port = 8080);
bool running() const;
int boundPort() const;
void stop();
// 路由只按 path 匹配，不区分 method —— 因此 POST 也能命中同一路由。

// Json.h
bool parse(const std::string& text, Value& out, std::string& error);   // 自由函数，返回 bool
using json::Object;   // std::map<std::string, Value>
using json::Value;
Value::dump() const;
const Value* Value::find(const std::string& key) const;   // 返回指针，不存在则为 nullptr
const Object& Value::asObject() const;
std::string Value::asString(const std::string& fallback = std::string()) const;
double Value::asNumber(double fallback = 0.0) const;
bool Value::isString() const;
bool Value::isNumber() const;
bool Value::isObject() const;
```

`Request` 含 `method`、`path`、`query`、`queryParams`、`headers`、`body` 六个字段；
POST 请求体已由 `HttpServer` 按 `Content-Length` 读入 `request.body`。

照抄兄弟项目的 `ok()` / `fail()` 辅助函数形式：

```cpp
#include "api/ApiRoutes.h"

#include <exception>
#include <string>

#include "core/BigDecimal.h"
#include "core/Expression.h"
#include "core/Format.h"
#include "core/Json.h"
#include "core/Rounding.h"

namespace hpc {
namespace http {
namespace {

using json::Object;
using json::Value;

// 构造 {"ok": true, ...} 形式的成功响应。
Response ok(Object payload) {
    payload["ok"] = Value(true);
    return Response::json(Value(payload).dump());
}

// 构造 {"ok": false, "error": "..."} 形式的失败响应。
Response fail(const std::string& message, int status = 400) {
    Object payload;
    payload["ok"] = Value(false);
    payload["error"] = Value(message);
    return Response::json(Value(payload).dump(), status);
}

// 解析请求体为 JSON 对象。失败时返回 false 并填写 error。
bool parseBody(const Request& request, Object& out, std::string& error) {
    Value parsed;
    if (!json::parse(request.body, parsed, error)) return false;
    if (!parsed.isObject()) {
        error = "请求体必须是 JSON 对象";
        return false;
    }
    out = parsed.asObject();
    return true;
}

// 从 JSON 对象取出字符串字段，缺失或类型不符时返回 fallback。
std::string stringField(const Object& body, const std::string& key,
                        const std::string& fallback = std::string()) {
    const auto it = body.find(key);
    if (it == body.end() || !it->second.isString()) return fallback;
    return it->second.asString(fallback);
}

// 取有效数字位数，缺省 30，越界回落到 30。
int precisionField(const Object& body) {
    const auto it = body.find("precision");
    if (it == body.end() || !it->second.isNumber()) return 30;
    const int precision = static_cast<int>(it->second.asNumber(30));
    return precision > 0 && precision <= 100000 ? precision : 30;
}

Response handleHealth(const Request&) {
    Object payload;
    payload["status"] = Value("ok");
    payload["service"] = Value("高精度计算器 API");
    payload["version"] = Value("3.0.0");
    return ok(payload);
}

Response handleEvaluate(const Request& request) {
    Object body;
    std::string error;
    if (!parseBody(request, body, error)) return fail(error);

    const std::string expression = stringField(body, "expression");
    if (expression.empty()) return fail("缺少 expression 字段");

    const Context context{precisionField(body), RoundingMode::HalfEven};
    try {
        const BigDecimal result = evaluate(expression, context);
        Object payload;
        payload["result"] = Value(result.toString());
        return ok(payload);
    } catch (const ExpressionError& error) {
        return fail(std::string(error.what()) + "（位置 " + std::to_string(error.position()) + "）");
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

Response handleArithmetic(const Request& request) {
    Object body;
    std::string error;
    if (!parseBody(request, body, error)) return fail(error);

    const std::string op = stringField(body, "op");
    const std::string leftText = stringField(body, "lhs");
    const std::string rightText = stringField(body, "rhs");
    if (op.empty() || leftText.empty() || rightText.empty()) {
        return fail("缺少 op / lhs / rhs 字段");
    }

    const Context context{precisionField(body), RoundingMode::HalfEven};
    try {
        const BigDecimal left = BigDecimal::fromString(leftText);
        const BigDecimal right = BigDecimal::fromString(rightText);
        BigDecimal result;
        if (op == "+") result = left + right;
        else if (op == "-") result = left - right;
        else if (op == "*") result = left * right;
        else if (op == "/") {
            if (right.isZero()) return fail("除数为零");
            result = BigDecimal::divide(left, right, context);
        } else {
            return fail("不支持的运算符");
        }
        Object payload;
        payload["result"] = Value(result.toString());
        return ok(payload);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

Response handleConvert(const Request& request) {
    Object body;
    std::string error;
    if (!parseBody(request, body, error)) return fail(error);

    const std::string value = stringField(body, "value");
    const auto fromIt = body.find("from");
    const auto toIt = body.find("to");
    if (value.empty() || fromIt == body.end() || toIt == body.end()) {
        return fail("缺少 value / from / to 字段");
    }
    const int from = static_cast<int>(fromIt->second.asNumber(10));
    const int to = static_cast<int>(toIt->second.asNumber(10));

    try {
        const BigDecimal parsed = from == 10
            ? BigDecimal::fromString(value)
            : fromBaseString(value, from);
        Object payload;
        payload["result"] = Value(toBaseString(parsed, to));
        return ok(payload);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

Response handleBitwise(const Request& request) {
    Object body;
    std::string error;
    if (!parseBody(request, body, error)) return fail(error);

    const std::string op = stringField(body, "op");
    const std::string leftText = stringField(body, "lhs");
    const std::string rightText = stringField(body, "rhs");
    if (op.empty() || leftText.empty() || rightText.empty()) {
        return fail("缺少 op / lhs / rhs 字段");
    }

    try {
        const BigDecimal left = BigDecimal::fromString(leftText);
        const BigDecimal right = BigDecimal::fromString(rightText);
        BitOp operation;
        if (op == "&") operation = BitOp::And;
        else if (op == "|") operation = BitOp::Or;
        else if (op == "^") operation = BitOp::Xor;
        else return fail("不支持的位运算符");
        Object payload;
        payload["result"] = Value(bitwise(left, right, operation).toString());
        return ok(payload);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

}  // namespace

void registerApiRoutes(Server& server, const std::string& webRoot) {
    server.route("/api/health", handleHealth);
    server.route("/api/evaluate", handleEvaluate);
    server.route("/api/arithmetic", handleArithmetic);
    server.route("/api/convert", handleConvert);
    server.route("/api/bitwise", handleBitwise);

    if (!webRoot.empty()) {
        server.serveStatic("/", webRoot);
    }
}

}  // namespace http
}  // namespace hpc
```

**注意**：`handleHealth` 等函数签名是 `Response(const Request&)`（单个参数），
与 `Handler = std::function<Response(const Request&)>` 匹配；
`registerApiRoutes` 中的 `server.route(...)` 调用形式与上表一致。

- [ ] **Step 5: 写 `src/server/main.cpp`**

**已核实的兄弟项目 API**：`Server` 有 `start(host, port)`（后台线程启动）、`running()`、
`boundPort()`、`stop()`；**没有 `run()` 方法**。静态资源目录用 `serveStatic("/", webRoot)` 注册。

```cpp
// 内置 HTTP 服务：只监听回环地址，供本地使用。
//
// 用法：hpc_server [--host 127.0.0.1] [--port 8098] [--web-root <目录>] [--no-browser]
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "api/ApiRoutes.h"
#include "core/HttpServer.h"

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 8098;
    std::string webRoot = "web";

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (argument == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (argument == "--web-root" && i + 1 < argc) {
            webRoot = argv[++i];
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "用法: hpc_server [--host 127.0.0.1] [--port 8098] "
                         "[--web-root <目录>]\n";
            return 0;
        }
    }

    hpc::http::Server server;
    hpc::http::registerApiRoutes(server, webRoot);

    std::cout << "高精度计算器 · Web 服务\n";
    std::cout << "前端目录: " << webRoot << "\n";

    if (!server.start(host, port)) {
        std::cerr << "启动失败：无法绑定 " << host << ":" << port << "\n"
                  << "端口可能已被占用，可换一个端口：hpc_server --port 8099\n";
        return 1;
    }

    const std::string url =
        "http://" + host + ":" + std::to_string(server.boundPort()) + "/";
    std::cout << "服务已就绪: " << url << "\n";
    std::cout << "按 Ctrl+C 停止。" << std::endl;

    while (server.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return 0;
}
```

- [ ] **Step 6: 写 `web/index.html`**

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>高精度计算器</title>
<link rel="stylesheet" href="styles.css">
</head>
<body>
<div class="app">
    <header class="header">
        <h1>高精度计算器</h1>
        <p>任意精度十进制 · base-10⁹ 分块内核</p>
    </header>
    <main class="content">
        <section class="panel">
            <label for="expr">表达式</label>
            <input id="expr" type="text" value="1/3 + 2*sqrt(9)" autocomplete="off">
            <div class="controls">
                <label>精度 <input id="precision" type="number" value="30" min="1" max="10000"></label>
                <label>输出进制
                    <select id="base">
                        <option value="10">10</option>
                        <option value="2">2</option>
                        <option value="8">8</option>
                        <option value="16">16</option>
                        <option value="36">36</option>
                    </select>
                </label>
                <button id="evaluate">计算</button>
            </div>
            <pre id="result" class="result"></pre>
        </section>
        <section class="panel">
            <h2>历史</h2>
            <ul id="history" class="history"></ul>
        </section>
    </main>
</div>
<script src="app.js"></script>
</body>
</html>
```

- [ ] **Step 7: 写 `web/app.js`**

```javascript
// Web 前端：调用 /api/* 接口展示结果。
const expressionInput = document.getElementById('expr');
const precisionInput = document.getElementById('precision');
const baseSelect = document.getElementById('base');
const resultBox = document.getElementById('result');
const historyList = document.getElementById('history');

async function evaluateExpression() {
    const expression = expressionInput.value.trim();
    if (!expression) return;
    const precision = Number(precisionInput.value) || 30;
    resultBox.classList.add('loading');
    try {
        const response = await fetch('/api/evaluate', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ expression, precision })
        });
        const payload = await response.json();
        if (!response.ok) {
            resultBox.textContent = '错误：' + (payload.error || '未知错误');
            resultBox.classList.add('error');
            return;
        }
        resultBox.classList.remove('error');
        let text = payload.result;
        const base = Number(baseSelect.value);
        if (base !== 10) {
            const converted = await fetch('/api/convert', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ value: text, from: 10, to: base })
            });
            const convertedPayload = await converted.json();
            if (converted.ok) text = convertedPayload.result;
        }
        resultBox.textContent = text;
        addHistory(expression, text);
    } catch (error) {
        resultBox.textContent = '网络错误：' + error.message;
        resultBox.classList.add('error');
    } finally {
        resultBox.classList.remove('loading');
    }
}

function addHistory(expression, result) {
    const item = document.createElement('li');
    item.textContent = expression + ' = ' + result;
    historyList.prepend(item);
}

document.getElementById('evaluate').addEventListener('click', evaluateExpression);
expressionInput.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') evaluateExpression();
});
```

- [ ] **Step 8: 写 `web/styles.css`**

```css
/* 高精度计算器 Web 前端样式。 */
:root {
    color-scheme: dark;
    --bg: #0f1115;
    --panel: #171a21;
    --text: #e6e8ec;
    --muted: #9aa3b2;
    --accent: #4f8cff;
    --error: #ff6b6b;
}

* { box-sizing: border-box; }

body {
    margin: 0;
    background: var(--bg);
    color: var(--text);
    font-family: "Segoe UI", "Microsoft YaHei", system-ui, sans-serif;
}

.app { max-width: 880px; margin: 0 auto; padding: 32px 20px; }

.header h1 { margin: 0 0 4px; font-size: 26px; }
.header p { margin: 0 0 24px; color: var(--muted); font-size: 14px; }

.panel {
    background: var(--panel);
    border-radius: 12px;
    padding: 20px;
    margin-bottom: 20px;
}

.panel label { display: block; font-size: 13px; color: var(--muted); margin-bottom: 6px; }

input[type="text"], input[type="number"], select {
    width: 100%;
    padding: 10px 12px;
    background: #0d0f14;
    color: var(--text);
    border: 1px solid #262b36;
    border-radius: 8px;
    font-size: 15px;
    font-family: "Cascadia Code", Consolas, monospace;
}

.controls { display: flex; gap: 16px; align-items: flex-end; margin: 16px 0; flex-wrap: wrap; }
.controls label { flex: 1 1 140px; }

button {
    padding: 10px 22px;
    background: var(--accent);
    color: #fff;
    border: none;
    border-radius: 8px;
    font-size: 15px;
    cursor: pointer;
}
button:hover { background: #3f7bea; }

.result {
    margin: 0;
    padding: 14px;
    background: #0d0f14;
    border-radius: 8px;
    font-family: "Cascadia Code", Consolas, monospace;
    font-size: 15px;
    min-height: 24px;
    word-break: break-all;
    white-space: pre-wrap;
}
.result.error { color: var(--error); }
.result.loading { opacity: 0.6; }

.history { list-style: none; margin: 0; padding: 0; font-family: Consolas, monospace; font-size: 13px; }
.history li { padding: 6px 0; border-bottom: 1px solid #22262f; color: var(--muted); word-break: break-all; }
```

- [ ] **Step 9: 构建并端到端验证**

```bash
./scripts/msvc-env.bat cmake -S . -B build -G Ninja -DBUILD_GUI=OFF -DBUILD_SERVER=ON -DBUILD_TESTS=ON
./scripts/msvc-env.bat cmake --build build
```

```bash
./build/hpc_server --port 8098 &
sleep 2
curl -s http://127.0.0.1:8098/api/health
curl -s -X POST http://127.0.0.1:8098/api/evaluate -d '{"expression":"1/3"}'
curl -s -X POST http://127.0.0.1:8098/api/arithmetic -d '{"op":"*","lhs":"2.5","rhs":"4"}'
curl -s -X POST http://127.0.0.1:8098/api/convert -d '{"value":"ff","from":16,"to":10}'
```

预期：依次返回 `{"status":"ok"}`、`{"result":"0.333333333333333333333333333333"}`、
`{"result":"10"}`、`{"result":"255"}`。

- [ ] **Step 10: 截图核对界面**

```bash
python scripts/screenshot.py http://127.0.0.1:8098/ /tmp/hpc_web.png
```
**必须实际查看生成的截图**，确认排版正常、结果区非空。

- [ ] **Step 11: 提交**

```bash
git add src/core/Json.* src/core/HttpServer.* src/api src/server web CMakeLists.txt
git commit -m "feat(web): REST API、内置 HTTP 服务与原生 Web 前端"
```

---

### Task 9: Qt 界面（可选目标）

**Files:**
- Create: `src/ui/main.cpp`
- Create: `src/ui/MainWindow.h`
- Create: `src/ui/MainWindow.cpp`
- Create: `tests/gui_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2-6 的核心接口
- Produces: `hpc_gui` 可执行文件（仅 `-DBUILD_GUI=ON` 时构建）；`gui_smoke` 离屏渲染测试

- [ ] **Step 1: 在 `CMakeLists.txt` 加入 GUI 目标**

```cmake
if(BUILD_GUI)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)

    add_executable(hpc_gui
        src/ui/main.cpp
        src/ui/MainWindow.cpp
        src/ui/MainWindow.h
    )
    target_link_libraries(hpc_gui PRIVATE hpc_core Qt6::Widgets)
    if(MSVC)
        target_compile_options(hpc_gui PRIVATE /W4 /utf-8)
    endif()
    if(WIN32)
        set_target_properties(hpc_gui PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()

    if(BUILD_TESTS)
        add_executable(gui_smoke tests/gui_smoke.cpp
            src/ui/MainWindow.cpp
            src/ui/MainWindow.h
        )
        target_link_libraries(gui_smoke PRIVATE hpc_core Qt6::Widgets)
        target_include_directories(gui_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)
        add_test(NAME gui_smoke
                 COMMAND gui_smoke ${CMAKE_CURRENT_BINARY_DIR}/screenshots)
        set_tests_properties(gui_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
```

- [ ] **Step 2: 写 `src/ui/MainWindow.h`**

```cpp
// Qt 桌面界面：仅是核心的前端之一，不含任何计算逻辑。
#pragma once

#include <QMainWindow>

namespace hpc {
namespace ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    // 触发一次求值。
    void evaluate();
    // 把一次求值结果追加到历史列表。
    void appendHistory(const QString& expression, const QString& result);

    class QLineEdit* expression_{nullptr};
    class QSpinBox* precision_{nullptr};
    class QComboBox* base_{nullptr};
    class QPlainTextEdit* output_{nullptr};
    class QListWidget* history_{nullptr};
};

}  // namespace ui
}  // namespace hpc
```

- [ ] **Step 3: 写 `src/ui/MainWindow.cpp`**

```cpp
#include "ui/MainWindow.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <exception>

#include "core/BigDecimal.h"
#include "core/Expression.h"
#include "core/Format.h"
#include "core/Rounding.h"

namespace hpc {
namespace ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("高精度计算器");
    resize(560, 480);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    layout->addWidget(new QLabel("表达式", central));
    expression_ = new QLineEdit(central);
    expression_->setText("1/3 + 2*sqrt(9)");
    expression_->setPlaceholderText("支持 + - * / %、括号与 sqrt/abs/pow/exp/ln/log10");
    layout->addWidget(expression_);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel("精度", central));
    precision_ = new QSpinBox(central);
    precision_->setRange(1, 10000);
    precision_->setValue(30);
    controls->addWidget(precision_);

    controls->addWidget(new QLabel("输出进制", central));
    base_ = new QComboBox(central);
    base_->addItems({"10", "2", "8", "16", "36"});
    controls->addWidget(base_);

    auto* button = new QPushButton("计算", central);
    controls->addWidget(button);
    layout->addLayout(controls);

    output_ = new QPlainTextEdit(central);
    output_->setReadOnly(true);
    layout->addWidget(output_);

    layout->addWidget(new QLabel("历史", central));
    history_ = new QListWidget(central);
    layout->addWidget(history_);

    setCentralWidget(central);

    connect(button, &QPushButton::clicked, this, &MainWindow::evaluate);
    connect(expression_, &QLineEdit::returnPressed, this, &MainWindow::evaluate);
}

MainWindow::~MainWindow() = default;

void MainWindow::evaluate() {
    const QString text = expression_->text().trimmed();
    if (text.isEmpty()) return;

    const Context context{precision_->value(), RoundingMode::HalfEven};
    try {
        const BigDecimal result = hpc::evaluate(text.toStdString(), context);
        const int base = base_->currentText().toInt();
        const QString shown = base == 10
            ? QString::fromStdString(result.toString())
            : QString::fromStdString(toBaseString(result, base));
        output_->setPlainText(shown);
        appendHistory(text, shown);
    } catch (const ExpressionError& error) {
        output_->setPlainText(QString("错误：%1（位置 %2）")
                                  .arg(QString::fromUtf8(error.what()))
                                  .arg(error.position()));
    } catch (const std::exception& error) {
        output_->setPlainText(QString("错误：%1").arg(QString::fromUtf8(error.what())));
    }
}

void MainWindow::appendHistory(const QString& expression, const QString& result) {
    history_->insertItem(0, expression + " = " + result);
}

}  // namespace ui
}  // namespace hpc
```

- [ ] **Step 4: 写 `src/ui/main.cpp`**

```cpp
#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    hpc::ui::MainWindow window;
    window.show();
    return app.exec();
}
```

- [ ] **Step 5: 写 `tests/gui_smoke.cpp`**

```cpp
// 离屏渲染冒烟测试：无需显示器即可验证界面能构建、求值并绘制。
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>

#include <cstdio>

#include "ui/MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    hpc::ui::MainWindow window;
    window.resize(560, 480);
    window.show();

    const auto lineEdit = window.findChild<QLineEdit*>();
    const auto output = window.findChild<QPlainTextEdit*>();
    const auto history = window.findChild<QListWidget*>();
    const auto buttons = window.findChildren<QPushButton*>();

    if (lineEdit == nullptr || output == nullptr || history == nullptr || buttons.isEmpty()) {
        std::fprintf(stderr, "gui_smoke: 找不到界面控件\n");
        return 1;
    }

    lineEdit->setText("1/3 + 2*sqrt(9)");
    buttons.first()->click();

    const QString text = output->toPlainText();
    if (!text.contains('.') || text.contains("错误")) {
        std::fprintf(stderr, "gui_smoke: 求值结果异常：%s\n", text.toUtf8().constData());
        return 1;
    }
    if (history->count() != 1) {
        std::fprintf(stderr, "gui_smoke: 历史记录条数异常：%d\n", history->count());
        return 1;
    }

    // 离屏渲染一张截图，确认能绘制。
    const QPixmap pixmap = window.grab();
    if (pixmap.isNull()) {
        std::fprintf(stderr, "gui_smoke: 渲染失败\n");
        return 1;
    }
    if (argc > 1) {
        pixmap.save(QString::fromLocal8Bit(argv[1]) + "/hpc_gui.png");
    }

    std::printf("gui_smoke: 求值结果 %s，渲染正常\n", text.toUtf8().constData());
    return 0;
}
```

- [ ] **Step 6: 构建并运行 GUI 冒烟测试**

```bash
./scripts/msvc-env.bat cmake -S . -B build -G Ninja -DBUILD_GUI=ON -DBUILD_TESTS=ON
./scripts/msvc-env.bat cmake --build build
./scripts/run-ctest.bat
```

预期：2 个测试全绿（`hpc_tests` 与 `gui_smoke`）。

**注意**：`msvc-env.bat` 中的 `CMAKE_PREFIX_PATH` 已指向 `C:\Qt\6.6.2\msvc2019_64`。

- [ ] **Step 7: 提交**

```bash
git add src/ui tests/gui_smoke.cpp CMakeLists.txt
git commit -m "feat(ui): Qt 桌面界面与离屏冒烟测试"
```

---

### Task 10: CI、脚本与文档

**Files:**
- Create: `.github/workflows/build.yml`
- Create: `scripts/run-ctest.bat`
- Create: `scripts/difftest.py`
- Create: `docs/architecture.md`
- Create: `docs/bignum-algorithms.md`
- Modify: `README.md`、`README_CN.md`（重写）

**Interfaces:**
- Consumes: 全部前置任务
- Produces: 完整 CI 流水线与文档

- [ ] **Step 1: 写 `scripts/run-ctest.bat`**

```bat
@echo off
rem Run the test suite.
setlocal
call "%~dp0msvc-env.bat" ctest --test-dir "%~dp0..\build" --output-on-failure -C Release || exit /b 1
```

- [ ] **Step 2: 写 `scripts/difftest.py`**

把 Task 3/4 的 `verify_div.py` 与 `verify_func.py` 合并为一个脚本：

```python
#!/usr/bin/env python3
"""差分测试：用 Python 标准库 decimal 作为独立基准，校验 C++ 核心。

用法：python scripts/difftest.py [--div-probe <路径>] [--func-probe <路径>]
默认 build/div_probe 与 build/func_probe。
"""
import argparse
import random
import subprocess
import sys
from decimal import Decimal, getcontext, ROUND_HALF_EVEN

getcontext().prec = 300
PREC = 20
QUANT = Decimal(1).scaleb(-PREC)


def fmt(value: Decimal) -> str:
    quantized = value.quantize(QUANT, rounding=ROUND_HALF_EVEN)
    text = format(quantized, 'f')
    if '.' in text:
        text = text.rstrip('0').rstrip('.')
    return text or '0'


def run_probe(probe: str, lines):
    result = subprocess.run([probe], input='\n'.join(lines), capture_output=True, text=True)
    return result.stdout.split()


def check_divisions(seed=7, count=400):
    random.seed(seed)
    cases = []
    for _ in range(count):
        a = ''.join(random.choice('0123456789') for _ in range(random.randint(1, 60)))
        b = ''.join(random.choice('0123456789') for _ in range(random.randint(1, 30)))
        if b.strip('0') == '':
            b = '7'
        if a.strip('0') == '':
            a = '0'
        cases.append((a, b))
    cases += [('1', '3'), ('10', '3'), ('1000', '7'), ('1', '2'), ('99999', '3')]

    outputs = run_probe('./build/div_probe', [f'{a} {b}' for a, b in cases])
    failures = 0
    for (a, b), got in zip(cases, outputs):
        expected = fmt(Decimal(a) / Decimal(b))
        if got != expected:
            failures += 1
            if failures <= 5:
                print(f"MISMATCH div {a} / {b}\n  got      {got}\n  expected {expected}")
    return len(cases) - failures, len(cases)


def check_functions(seed=11, count=120):
    random.seed(seed)
    cases = []
    for _ in range(count):
        cases.append(('sqrt', f"{random.uniform(0.01, 10000):.6f}", '0'))
        cases.append(('exp', f"{random.uniform(-10, 10):.6f}", '0'))
        cases.append(('ln', f"{random.uniform(0.001, 1000):.6f}", '0'))
        cases.append(('pow', f"{random.uniform(0.1, 100):.6f}", f"{random.uniform(0.1, 5):.6f}"))
    cases += [('sqrt', '2', '0'), ('sqrt', '4', '0'), ('exp', '0', '0'), ('exp', '1', '0'),
              ('ln', '1', '0'), ('ln', '2', '0'), ('ln', '10', '0'), ('pow', '2', '10')]

    outputs = run_probe('./build/func_probe', [f'{o} {a} {b}' for o, a, b in cases])
    failures = 0
    for (op, a, b), got in zip(cases, outputs):
        da, db = Decimal(a), Decimal(b)
        if op == 'sqrt':
            expected = da.sqrt()
        elif op == 'exp':
            expected = da.exp()
        elif op == 'ln':
            expected = da.ln()
        else:
            expected = da ** db
        expected_text = fmt(expected)
        if got != expected_text:
            failures += 1
            if failures <= 8:
                print(f"MISMATCH {op} {a} {b}\n  got      {got}\n  expected {expected_text}")
    return len(cases) - failures, len(cases)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--div-probe', default='./build/div_probe')
    parser.add_argument('--func-probe', default='./build/func_probe')
    arguments = parser.parse_args()

    total_failures = 0
    for name, function in (('除法', check_divisions), ('超越函数', check_functions)):
        passed, total = function()
        total_failures += total - passed
        print(f"{name}差分测试：{passed}/{total} 通过")

    return 1 if total_failures else 0


if __name__ == '__main__':
    sys.exit(main())
```

- [ ] **Step 3: 在 `CMakeLists.txt` 注册差分测试**

```cmake
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME difftest
                 COMMAND ${Python3_EXECUTABLE}
                         "${CMAKE_CURRENT_SOURCE_DIR}/scripts/difftest.py"
                         --div-probe $<TARGET_FILE:div_probe>
                         --func-probe $<TARGET_FILE:func_probe>)
        set_tests_properties(difftest PROPERTIES TIMEOUT 300)
    endif()
```

- [ ] **Step 4: 写 `.github/workflows/build.yml`**

照抄兄弟项目 `Comprehensive-Calculation-Tool/.github/workflows/build.yml`，改为本项目的目标名
（`hpc_core` / `hpc_cli` / `hpc_server` / `hpc_tests` / `gui_smoke`），
并在 core 作业中追加差分测试步骤：

```yaml
      - name: 运行差分测试
        run: python3 scripts/difftest.py
```

- [ ] **Step 5: 写 `docs/architecture.md`**

内容：分层图（core → cli/api/ui）、数据流、接口边界（核心不依赖任何前端）、
精度语义（Context 的传播路径）、如何新增一个前端、如何新增一种运算。

- [ ] **Step 6: 写 `docs/bignum-algorithms.md`**

内容：base-10⁹ 分块布局与内存对齐、十进制字符串与块的转换、
进位/借位、对齐小数位、长除法推导（余数左移 + 二分试商）、
Knuth Algorithm D 的对照说明与基准数据、舍入模式语义表、
开方（牛顿迭代）、exp（泰勒级数）、ln（反正切级数）的收敛性分析、
精度与误差传播。

**必须包含本计划实测得到的基准数据**：

```
乘法（base-10⁹ 分块 vs 单数字位）
  100 位:   19x
  1000 位: 108x
  10000 位:120x

除法（base-10⁹ 分块，逐块长除法 + 二分试商）
  20 位:   0.0112 ms
  100 位:  0.0902 ms
  400 位:  0.4700 ms
  800 位:  1.3439 ms
  1600 位: 4.8573 ms

运行时 base 参数 vs 编译期常量 base=10（4000 位乘法）
  25.5 ms  vs  35.4 ms   → 运行时 base 慢 39%
```

并说明「运行时 base 参数使内层取模/除法退化为硬件除法，慢约 39%」，
这是把 base-10 固定在核心、多进制只在表示层处理的理由。

- [ ] **Step 7: 重写 `README.md` 与 `README_CN.md`**

内容：项目定位（任意精度十进制计算内核）、特性、目录结构、
快速开始（构建、运行三种前端）、示例输出、算法文档链接、legacy 说明、许可证。

- [ ] **Step 8: 全量验证**

```bash
./scripts/msvc-env.bat cmake -S . -B build -G Ninja -DBUILD_GUI=ON -DBUILD_TESTS=ON -DBUILD_SERVER=ON
./scripts/msvc-env.bat cmake --build build
./scripts/run-ctest.bat
python scripts/difftest.py
```

预期：3 个测试全绿（`hpc_tests`、`gui_smoke`、`difftest`），差分测试全通过。

- [ ] **Step 9: 提交**

```bash
git add -A
git commit -m "docs: 架构与算法研究笔记、CI 流水线与差分测试脚本"
```

---

## Self-Review

**规格覆盖检查**

| 规格章节 | 对应任务 |
|---|---|
| §2.1 总体形态 | Task 1（骨架）、Task 8（Web）、Task 9（Qt） |
| §2.2 核心数值类型 | Task 2、Task 3 |
| §2.3 base-10⁹ 分块 | Task 2（实现）、Task 10（文档中的基准数据） |
| §2.4 已验证算法 | Task 3（长除法）、Task 4（超越函数） |
| §2.5 多进制与位运算 | Task 5 |
| §2.6 表达式求值 | Task 6 |
| §3.1 CLI | Task 7 |
| §3.2 Web | Task 8 |
| §3.3 Qt | Task 9 |
| §4 构建与工具链 | Task 1（CMake + 脚本）、Task 8/9（GUI 选项）、Task 10（CI） |
| §5 目录迁移 | Task 1 |
| §6.1 单元测试 | Task 2/3/4/5/6 |
| §6.2 Python 差分测试 | Task 3、Task 4、Task 10（合并脚本） |
| §6.3 CI 集成 | Task 10 |
| §6.4 GUI 冒烟 | Task 9 |
| §6.5 端到端 | Task 8 |
| §7 文档产出 | Task 1（legacy 文档）、Task 10（architecture、bignum-algorithms、README） |
| §8 缺陷清单 | Task 1（legacy/COMPARISON.md 记录） |

**实施过程中发现并已修正的问题**

1. `Blk` 别名需在 `BigDecimal.h` 的 `namespace hpc` 内可见（私有成员声明用到它）——
   Task 3 Step 1 已明确要求提升到 public 位置。
2. 长除法的进位必须 `push_back`（追加到最高位），商累积顺序是最高位在前需 `reverse` ——
   Task 3 Step 2 已用粗体标注这两个易错点（原型阶段曾两次栽在这里）。
3. `roundToPrecision` 若用块级逻辑处理舍入，会与块边界交互出错 —— 已改为字符串级实现。
4. `ln` 的量级调整循环不能用 `compare(BigDecimal())`（恒为 0）——
   已改为基于 `kLn10` 的 offset 累加版本。
5. `fromBaseString` 中 `value * base` 的 carry 处理必须用 `cur = value[k]*base + carry`，
   不能写成 `+ carry % 1e9` —— Task 5 已给出正确写法。
6. `gui_smoke.cpp` 不能依赖 `findChild<QPushButton*>("")`（objectName 为空）——
   改用 `findChildren<QPushButton*>()` 取首个。
7. `screenshot.py` 的 Edge 路径必须用绝对 Windows 路径（来自记忆中的已知坑）。

**类型一致性检查**

- `Context` / `RoundingMode` 定义在 `src/core/Rounding.h`，命名空间 `hpc` ——
  Task 2 引入，Task 3/4/6/7/8/9 一致引用。
- `BigDecimal::fromString` 抛 `std::invalid_argument`；除零/负数平方根/非正对数抛
  `std::domain_error` —— Task 3/4 定义，Task 7/8 一致捕获。
- `evaluate` 抛 `ExpressionError`（携带 `position()`）—— Task 6 定义，Task 7/8/9 一致捕获。
- `toBaseString` / `fromBaseString` / `bitwise` / `shiftLeft` / `shiftRight` 签名在
  Task 5 定义，Task 7/8 一致引用。
- `Json::parse(text, out, error)` 是自由函数返回 `bool`；`Value::find` 返回指针；
  `HttpServer::route` 按 path 匹配（不区分 method）；服务端生命周期是
  `start()` / `running()` / `boundPort()`，**没有 `run()`** —— 上述签名均已对照
  兄弟项目源码核实，Task 8 已按真实签名书写。
