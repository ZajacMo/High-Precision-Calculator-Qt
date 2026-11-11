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
- **结构安全优先于输入校验**：legacy 的两条崩溃缺陷（#5 `deleteZero()` 循环二、
  #10 除法舍入分支）同源 —— 都是「`->prior` 回退时假定链表不止一个节点」。
  v3.0 的任何代码都不得依赖链表长度假设；单节点/空集合必须安全。
  输入校验（如 `precision <= 0`）是第二道防线，不能替代结构安全。
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
│   └── bignum-algorithms.md
├── legacy/                    三代原始实现存档
│   ├── README.md              来源与谱系判定
│   ├── COMPARISON.md          三代逐项对比与缺陷清单
│   ├── qt_gui_original/       第 3 代（Qt GUI）
│   ├── onlinejudge_calculator/ 第 1 代（最老，命令行）
│   └── oj_console_edition/    第 2 代（命令行）
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
mkdir -p src/core src/cli src/api src/server src/ui web tests scripts docs
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

// 把小端 base-10^9 块转成十进制字符串（不含小数点）。
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
//
// 补零就是「乘以 10^delta」，而 10 的整数次幂在十进制下就是移小数点 ——
// 用 shiftLeftDecimal 直接移块，**O(块数)**。
//
// 原来的写法是 mulBlk(digits, 10^delta)：那是一次通用的大数乘法，O(块数²)，
// 而且每次都要先把 "1" + delta 个 '0' 拼成字符串再解析成块。
// 超越函数的级数每一项都要做加法与比较，两次 alignScale 一次，
// 于是这个二次方开销把整个级数拖回三次方。
int BigDecimal::alignScale(BigDecimal& lhs, BigDecimal& rhs) {
    if (lhs.scale_ == rhs.scale_) return lhs.scale_;
    if (lhs.scale_ < rhs.scale_) {
        const int delta = rhs.scale_ - lhs.scale_;
        lhs.digits_ = shiftLeftDecimal(std::move(lhs.digits_), delta);
        lhs.scale_ += delta;
    } else {
        const int delta = lhs.scale_ - rhs.scale_;
        rhs.digits_ = shiftLeftDecimal(std::move(rhs.digits_), delta);
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
  - `static BigDecimal divide(const BigDecimal& lhs, const BigDecimal& rhs, const Context& context)`（除零抛 `std::domain_error`）
    —— **不要写 `operator/`**：二元运算符不能带 `Context` 参数（MSVC 报 C2804）。
    精度是第二个操作数，用静态形式更贴切。
  - `static BigDecimal modulo(const BigDecimal& lhs, const BigDecimal& rhs)`（除零抛 `std::domain_error`）
  - `static BigDecimal floor(const BigDecimal&, const Context&)`、`ceil`、`round`（HalfEven）
  - 私有：`static BigDecimal roundToPrecision(const BigDecimal&, const Context&)`
  - 私有：`static std::pair<Blk, Blk> divideBlk(const Blk& dividend, const Blk& divisor)`

- [ ] **Step 1: 在 `BigDecimal.h` 声明新增接口**

在 `class BigDecimal` 的 public 区域加入：

```cpp
    // 不精确运算：必须显式给出精度。
    // 除数为零时抛出 std::domain_error。
    // 只提供静态 divide —— 不要写 operator/：二元运算符不能带 Context 参数。
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

    // 把分子分母对齐到**共同的小数位**后再相除。
    //
    // 不要在这里额外乘 10^rhs.scale_ —— 那会与下面的
    // result.scale_ = lhs.scale_ - rhs.scale_ + guard 重复计算，
    // 使结果整整大 10 倍（1/0.5 会得到 20 而不是 2）。
    const int commonScale = lhs.scale_ > rhs.scale_ ? lhs.scale_ : rhs.scale_;
    Blk numerator = lhs.digits_;
    for (int i = 0; i < commonScale - lhs.scale_; ++i) {
        numerator = mulBlk(numerator, Blk{10});
    }
    Blk denominator = rhs.digits_;
    for (int i = 0; i < commonScale - rhs.scale_; ++i) {
        denominator = mulBlk(denominator, Blk{10});
    }
    // 再放大 guard 位以保留足够的小数位。
    for (int i = 0; i < guard; ++i) {
        numerator = mulBlk(numerator, Blk{10});
    }

    const auto divided = divideBlk(numerator, denominator);

    BigDecimal result;
    result.digits_ = divided.first;
    result.scale_ = guard;   // 分子分母已对齐到 commonScale，商的小数位就是 guard
    result.sign_ = lhs.sign_ * rhs.sign_;
    result.normalize();

    return roundToPrecision(result, context);
}

// 注意：不要提供 operator/ —— 二元运算符不能带 Context 参数（MSVC 报 C2804）。
// 除法一律走 static divide(lhs, rhs, context)。

BigDecimal BigDecimal::modulo(const BigDecimal& lhs, const BigDecimal& rhs) {
    if (rhs.sign_ == 0) throw std::domain_error("BigDecimal: 取余的除数为零");
    if (lhs.compare(rhs) < 0 && lhs.sign() * rhs.sign() >= 0) return lhs;
    // 求商时向零截断。精度按**操作数的量级**给，而不是写死一个巨大值 ——
    // 写死 1000000 会让每次除法都做十万位运算，实测单次超过 3 分钟。
    // 商的整数部分不超过 lhs 的位数，取这么多位足够精确。
    const int quotientDigits = lhs.scale_ > rhs.scale_
        ? lhs.scale_ - rhs.scale_ + 1
        : 1;
    BigDecimal quotient = divide(lhs, rhs, Context{quotientDigits, RoundingMode::Down});
    return lhs - quotient * rhs;
}
```

`roundToPrecision` —— **走块级路径**（不是字符串级），避免块边界与舍入位的交互错误：

> **设计要点（来自 legacy 缺陷 #5 与 #10 的共同根因）**：这两条缺陷同源 ——
> 都是「用 `->prior` 向前走时，假定链表不止一个节点」：
> #5 是 `deleteZero()` 的循环二（`0 * 0.11` → SIGSEGV），
> #10 是除法收尾的舍入分支（`FracBit == 0` 且 `start == -2` → SIGSEGV）。
> #5 的修复使结果退化为**单节点链表**，#10 随后对单节点链表做了同样的错误假设。
>
> 因此 v3.0 的舍入步骤必须**结构上安全**，而不仅仅是校验 `precision`：
> 本实现全程只用块数组，**完全不做 `->prior` 回退**，单节点/空链表都安全。
> `precision <= 0` 的校验是第二道防线（实测 `precision >= 1` 即安全）。
>
> **块级路径同时也是性能路径**：舍入判定基于长除法得到的**精确余数**，
> 因此「正好一半」（银行家舍入要走偶数分支）与「超过一半」永远能被正确区分。
> 除数是 **10 的整数次幂**（d = 1, s = scale），可以分两段做长除法
> （先除 10^m 再除 d），每段都是 O(块数) —— 而通用的「二分试商」是 O(块数²)。
> 这一步是超越函数提速的关键：级数每一项都要 roundToPrecision。

```cpp
// 舍入到 context.precision 位有效数字。
//
// 走"把精确分数交给 roundQuotientSmall"的路径：舍入判定基于长除法得到的
// **精确余数**，而不是"多算几位再猜"。因此「正好一半」与「超过一半」
// 永远能被正确区分 —— 银行家舍入的偶数分支才可靠。
//
// 结构安全：全程只用块数组，不做任何"向前回退一格"的链式操作，
// 因此单块/单元素结构天然安全（legacy 缺陷 #5 与 #10 的共同根因）。
BigDecimal BigDecimal::roundToPrecision(const BigDecimal& value, const Context& context) {
    if (value.sign_ == 0) return value;
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");
    if (context.precision > kMaxPrecision) {
        throw std::invalid_argument("BigDecimal: precision 超出上限");
    }

    // 把精确值表示成分数：numerator/denominator = value。
    // value = digits * 10^-scale，所以分母是 10^scale，分子是 digits。
    //
    // 除数是 **10 的整数次幂**（d = 1, s = scale），走线性路径：
    // roundQuotientSmall 与 roundQuotient 在这个形状上的结果**完全一致**
    // （同一个 exponent、同一套尾数判定、同一个结果位数），
    // 但除法从 O(块数²) 降到 O(块数)。
    const auto rounded = roundQuotientSmall(value.digits_, 1, value.scale_,
                                            context.precision, context.mode, value.sign_ < 0);

    BigDecimal result;
    result.digits_ = rounded.first;
    result.scale_ = rounded.second;
    result.sign_ = value.sign_;
    result.normalize();
    return result;
}
```

配套的线性时间工具（匿名 namespace 内）：

```cpp
// 单块长除法：把块序列除以一个单块整数（0 < divisor < 10^9）。
// 每块只要一次 64 位除法：current = 余数 * 10^9 + 块 < 10^18，装得下。
std::pair<Blk, std::uint32_t> divideBlkBySingle(const Blk& dividend, std::uint32_t divisor) {
    Blk quotient(dividend.size(), 0);
    std::uint64_t remainder = 0;
    for (std::size_t i = dividend.size(); i-- > 0;) {
        const std::uint64_t current = remainder * kBase + dividend[i];
        quotient[i] = static_cast<std::uint32_t>(current / divisor);
        remainder = current % divisor;
    }
    quotient = trimBlk(std::move(quotient));
    return {std::move(quotient), static_cast<std::uint32_t>(remainder)};
}

// 把块序列右移 rest 个十进制位（1 <= rest <= 8），即丢掉最低的 rest 位。
Blk shiftRightDecimalDigits(const Blk& blocks, int rest);

// 把块序列除以 10^places（十进制位），返回 {商, 余数}。
std::pair<Blk, Blk> divideBlkByPowerOfTen(const Blk& blocks, int places);

// 把精确分数 N / (d * 10^s) 舍入到 precision 位有效数字。
// 与 roundQuotient(N, d * 10^s, ...) **完全等价**，但除法走线性路径。
std::pair<Blk, int> roundQuotientSmall(const Blk& numerator, std::uint32_t d, int s,
                                       int precision, RoundingMode mode, bool negative);
```

以及公开的「除以小整数」入口（级数专用，语义与 `divide` 完全一致）：

```cpp
// 除以一个小整数。
//
// 语义与 divide(value, BigDecimal(divisor), context) **完全一致** ——
// 同样的舍入模式、同样的精度处理、同样的结果位数。区别只在**速度**：
// 除数是单块整数时可以一步长除法算出来，不必用通用路径的「二分试商」。
//
// divisor >= 10^9 时除数不止一块，回退到通用路径。
BigDecimal BigDecimal::divideBySmallInt(const BigDecimal& value, std::uint64_t divisor,
                                         const Context& context);
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

> **本节代码已在实现中验证并修正。** 下面给出的是**实际通过测试的**版本，
> 不是初稿。代码块逐字取自 `src/core/BigDecimal.{h,cpp}` 的实现，
> 初稿的 7 处缺陷记录在文末「初稿缺陷清单」。

**Files:**
- Modify: `src/core/BigDecimal.h`
- Modify: `src/core/BigDecimal.cpp`
- Create: `tests/tst_transcendental.cpp`
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

- [x] **Step 1: 声明接口**

```cpp
    // --- 超越函数 ---
    //
    // 这些函数本质上不精确，必须显式给出精度：结果按 context.precision
    // 位有效数字舍入，舍入方式取 context.mode。
    //
    // 定义域之外抛 std::domain_error：
    //   sqrt：负数
    //   ln / log10：非正数
    //   pow：负底数的非整数次幂、零的负数次幂、零的零次幂
    //
    // 结果规模超出可计算范围时抛 std::overflow_error（exp 的参数过大、
    // pow 的整数指数过大、pow 的结果位数过大）。
    //
    // 精度超出上限时抛 std::invalid_argument：
    //
    //   - sqrt / exp / ln / log10 与 pow 的**非整数指数**分支：
    //     context.precision > kMaxTranscendentalPrecision。
    //     这五个入口的工作精度是平方级的（见 kMaxTranscendentalPrecision
    //     的实测数据），p=1600 时 pow(2, 0.5) 要跑 151 s，p=99990 更是
    //     永远算不完 —— 而且失败方式**不一致**（有的精度下内层的 divide
    //     恰好抛异常，有的精度下则一路挂起）。统一的 std::invalid_argument
    //     让调用方拿到确定的行为。
    //
    //   - divide / roundToPrecision / floor / ceil / round：
    //     context.precision > kMaxPrecision。除法的代价随精度线性增长，
    //     p=100000 时单次要几十秒 —— 能算完，但已经不值得。
    //
    // 注意两者的区别：**pow 的整数指数分支不受
    // kMaxTranscendentalPrecision 约束** —— 整数次幂走的是精确的快速幂，
    // 代价由 kMaxIntegerPower（指数）与 kMaxPowerResultDigits（结果位数）
    // 单独约束，与超越函数的工作精度毫无关系。pow(10, 100000) 在
    // precision=2000 下完全合法。

    // 平方根。
    static BigDecimal sqrt(const BigDecimal& value, const Context& context);

    // 幂。整数指数走精确的快速幂；非整数指数用 exp(b·ln a)。
    static BigDecimal pow(const BigDecimal& base, const BigDecimal& exponent,
                          const Context& context);

    // 自然指数 e^x。
    static BigDecimal exp(const BigDecimal& value, const Context& context);

    // 自然对数 ln(x)。
    static BigDecimal ln(const BigDecimal& value, const Context& context);

    // 常用对数 log10(x)。
    static BigDecimal log10(const BigDecimal& value, const Context& context);
```

- [x] **Step 2: 内部工具与常量**

```cpp
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

// 超越函数的精度上限、整数幂的指数与结果位数上限、科学计数法的指数上限
// 都是**公开契约**，声明在 BigDecimal.h（见那里的实测数据）。
//
//   inline constexpr int      kMaxPrecision = 100000;
//   inline constexpr int      kMaxTranscendentalPrecision = 1000;
//   inline constexpr long long kMaxIntegerPower = 100000;
//   inline constexpr int      kMaxPowerResultDigits = 300000;
//   inline constexpr long long kMaxExponentMagnitude = 1000000;

// 十进制整数的最大位数（用于 long long 溢出保护）。
constexpr int kMaxIntegerDigits = 18;

constexpr std::int64_t kMaxInt64 = 9223372036854775807LL;

// floor(e / 2)：C++ 的整数除法向零取整，负数需要修正。
int floorHalf(int e) { return e >= 0 ? e / 2 : (e - 1) / 2; }

}  // namespace
```

头文件里的私有声明：

```cpp
    // --- 超越函数的内部工具 ---

    // 值的十进制指数 e：10^e <= |value| < 10^(e+1)。零值按 0 处理。
    int decimalExponent() const;

    // 精确地做 value * 10^exponent（指数可正可负，不引入舍入）。
    static BigDecimal shiftByPowerOfTen(const BigDecimal& value, int exponent);

    // 精确地乘以 2（十进制下 2 是有限的，除以 2 用乘 5 再移小数点）。
    static BigDecimal multiplyByTwo(const BigDecimal& value);

    // 精确地除以 2。
    static BigDecimal divideByTwo(const BigDecimal& value);

    // 向下取整到整数（scale 归零）。
    static BigDecimal floorToInteger(const BigDecimal& value);

    // scale 为 0 的整数值转成 long long；超出范围时抛 std::overflow_error。
    static long long toInteger(const BigDecimal& value);

    // 精确的整数幂 base^power（power >= 1）；结果位数超限时抛 std::overflow_error。
    static BigDecimal integerPower(const BigDecimal& base, long long power);

    // atanh 级数：z + z^3/3 + z^5/5 + ...（|z| <= 1/3 时收敛很快）。
    static BigDecimal atanhSeries(const BigDecimal& z, const Context& work);

    // exp(g) 的泰勒级数（0 <= g < ln(10) 时收敛很快）。
    static BigDecimal expSeries(const BigDecimal& g, const Context& work);

    // 除以一个小整数（级数里的 2k+1 与 n 都是这种形状）。
    //
    // 语义与 divide(value, BigDecimal(divisor), context) **完全一致**
    // （同样的舍入模式、同样的精度处理），但走的是线性时间的路径：
    // 除数是单块整数时，一步长除法就能算出来，不必用通用路径的
    // 「二分试商」逐位试商（那是 O(块数²)）。
    //
    // divisor >= 10^9 时回退到通用路径（那时除数已经不止一块了）。
    static BigDecimal divideBySmallInt(const BigDecimal& value, std::uint64_t divisor,
                                       const Context& context);

    // 把 value 分解成 mantissa * 10^exponent * 2^halvings，
    // 其中 mantissa ∈ [1, 2)，exponent 与 halvings 由输出参数给出。
    //
    // 移位与折半都是精确操作，不引入任何舍入。
    static BigDecimal decomposeForLog(const BigDecimal& value, int& exponent, int& halvings);

    // ln(2) 与 ln(10)：按 work 精度**实时计算**（硬编码常量在高精度下不够用）。
    // ln2 由调用方传入 —— 一次运算里 ln(2) 会被用到好几处。
    static BigDecimal ln2Constant(const Context& work);
    static BigDecimal ln10Constant(const BigDecimal& ln2, const Context& work);

    // 超越函数的内部实现：work 是工作精度（含保护位），final 是最终舍入上下文。
    static BigDecimal expInternal(const BigDecimal& value, const Context& work,
                                  const Context& final);
    static BigDecimal lnInternal(const BigDecimal& value, const Context& work);
    static BigDecimal log10Internal(const BigDecimal& value, const Context& work,
                                    const Context& final);
```

- [x] **Step 3: 实现 `sqrt`（牛顿迭代）**

```cpp
// 值的十进制指数 e：10^e <= |value| < 10^(e+1)。
int BigDecimal::decimalExponent() const {
    if (sign_ == 0) return 0;
    // digits_ 是小端：最高位块在末尾。
    // 整数部分的十进制位数 = 总位数 - 小数位数。
    const int integerDigits = decimalDigitCount(digits_) - scale_;
    // 10^e <= |v| < 10^(e+1) 的 e = 位数 - 1。
    return integerDigits - 1;
}
```

```cpp
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
```

```cpp
// 精确地乘以 2。
BigDecimal BigDecimal::multiplyByTwo(const BigDecimal& value) {
    BigDecimal result = value;
    result.digits_ = doubleBlk(result.digits_);
    result.normalize();
    return result;
}
```

```cpp
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
```

```cpp
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
```

```cpp


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
```

- [x] **Step 4: 实现 `exp` 与 `ln`**

```cpp
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
```

```cpp

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
```

```cpp
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
        // 除以 2k+1：走「除以小整数」的线性路径。
        //
        // 语义与 divide(power, BigDecimal(2*k+1), ...) 完全一致，
        // 但除法从 O(块数²) 降到 O(块数) —— 这一项是级数里的主项。
        const BigDecimal addend = divideBySmallInt(power, static_cast<std::uint64_t>(2 * k + 1),
                                                   Context{work.precision, RoundingMode::HalfEven});
        if (addend.isZero()) break;
        sum = sum + addend;
        if (abs(addend).compare(epsilon) < 0) break;
    }
    return sum;
}
```

```cpp
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
        // 除以 n：走「除以小整数」的线性路径（语义与通用路径一致）。
        term = divideBySmallInt(term, static_cast<std::uint64_t>(n),
                                Context{work.precision, RoundingMode::HalfEven});
        if (term.isZero()) break;
        sum = sum + term;
        if (abs(term).compare(epsilon) < 0) break;
    }
    return sum;
}
```

```cpp
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
```

```cpp
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
```

```cpp
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
```

```cpp

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
```

```cpp
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
```

```cpp
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
```

- [x] **Step 5: 实现 `pow`**

```cpp

BigDecimal BigDecimal::pow(const BigDecimal& base, const BigDecimal& exponent,
                           const Context& context) {
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");

    // 整数指数走快速幂，结果精确。
    //
    // 判定「整数」用 scale_ == 0（值本身就是整数），而不是解析字符串 ——
    // brief 原实现手工解析字符串成 long long，超过 10^18 的指数会静默溢出。
    //
    // 注意这里**没有** kMaxTranscendentalPrecision 的闸：整数次幂是**精确**
    // 运算，代价由 kMaxIntegerPower（指数）与 kMaxPowerResultDigits
    // （结果位数）约束，与超越函数的工作精度毫无关系。
    //
    // 早先的版本把超越函数的闸放在 pow 的最前面，于是 pow(2, 100) 在
    // precision=2000 时被拒 —— 那是一个它根本不该管的能力：
    // 「超越函数」的上限按定义就不适用于精确的整数次幂。
    // pow(10, 100000) 在 precision=2000 下完全合法（结果 10 万位，
    // 实测约 7 s）。
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
    // 这一条才是真正的超越函数路径，工作精度是平方级的，因此受
    // kMaxTranscendentalPrecision 约束 —— 闸门放在**这条分支上**，
    // 而不是 pow 的入口。
    if (context.precision > kMaxTranscendentalPrecision) {
        throw std::invalid_argument("BigDecimal: precision 超出超越函数的上限");
    }

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
```

- [x] **Step 6: 对外的超越函数接口**

```cpp

BigDecimal BigDecimal::exp(const BigDecimal& value, const Context& context) {
    if (context.precision <= 0) throw std::invalid_argument("BigDecimal: precision 必须为正");
    if (context.precision > kMaxTranscendentalPrecision) {
        throw std::invalid_argument("BigDecimal: precision 超出超越函数的上限");
    }
    const int workPrecision = std::max(context.precision + kGuardDigits, kMinWorkPrecision);
    const Context work{workPrecision, RoundingMode::HalfEven};
    return expInternal(value, work, context);
}
```

```cpp

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
```

```cpp

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
```

- [x] **Step 7: 写 `tests/func_probe.cpp` 与 `scripts/verify_func.py`**

`tests/func_probe.cpp`：读入 `op a b [mode]`，输出结果；精度从命令行第一个参数取
（缺省 20），mode 可选（缺省 HalfEven）。

`scripts/verify_func.py` 的关键点：

- **参考值必须量化到 20 位「有效数字」**，不是 20 位小数。
  `Context.precision` 是有效数字位（与 Python decimal 的 prec 同义），
  初稿的 `fmt()` 用 `scaleb(-PREC)` 量化的是**小数位**，对 `sqrt(2)` 这类
  量级接近 1 的值恰好相同，但 `ln(1000) = 6.907…` 就会差好几位。
- 不用 `decimal` 的现成 `sqrt/exp/ln` 的舍入结果，而是自己按模式量化，
  以便覆盖全部 6 种舍入模式（`decimal` 的这些函数只支持 ROUND_HALF_EVEN）。
- `0 ** 0` 在 decimal 规范里是 InvalidOperation，参考实现必须显式抛错，
  C++ 端也要报 ERROR。
- 用例里不能出现 `1e-20` 这样的科学计数法 —— `BigDecimal::fromString`
  不支持 `'e'` 记法（那是 Task 5/6 的进制格式化职责）。用纯小数字符串。

- [x] **Step 8: 构建并验证**

```bash
./scripts/build-core.bat
./scripts/run-ctest.bat
python scripts/verify_func.py ./build/func_probe.exe
```

预期：`函数差分测试：730/730 通过`。

**实测结果**（本实现）：

| 精度 | 差分测试 | 备注 |
|------|---------|------|
| 20   | 730/730  | 默认精度 |
| 30   | 730/730  | |
| 50   | 730/730  | |
| 100  | 730/730  | |
| 200  | 730/730  | |

单元测试：`67 / 67 个用例，失败 0 个断言`。

**复审后追加的回归用例**（`tests/tst_transcendental.cpp`）：

- `pow, integer_exponent_ignores_transcendental_ceiling` — 精度高于
  `kMaxTranscendentalPrecision` 时，**整数指数**的幂必须照常算出且精确
  （`pow(2, 100)` @ p=2000 得 `1267650600228229401496703205376`）。
- `pow, non_integer_exponent_still_rejects_precision_above_ceiling` — 非整数
  指数仍抛 `std::invalid_argument`；恰好在上限上（p=1000）则允许。
- `pow, integer_exponent_result_digits_limit_still_applies` — 整数次幂**真正**的
  上限仍然生效：`0.001^100000` 与 `2^(10^20)` 抛 `std::overflow_error`。

以及 `tests/tst_bigdecimal.cpp` 的 `scientific, enforces_exponent_limit_exactly`：
`1e999999` 接受、`1e1000001`/`1e1000009`/`1e1000010`/`1e9999999` 拒绝。

- [x] **Step 9: 提交**

```bash
git add src/core/BigDecimal.cpp src/core/BigDecimal.h tests/tst_transcendental.cpp \
        tests/func_probe.cpp scripts/verify_func.py CMakeLists.txt
git commit -m "feat(core): sqrt/pow/exp/ln/log10 超越函数实现"
```

---

#### 初稿缺陷清单（本任务修正的 7 处）

| # | 位置 | 缺陷 | 后果 |
|---|------|------|------|
| 1 | `sqrt` 初值 | 初值取自**放大后字符串的整数位数**，与被开方数的真实量级脱节 | `sqrt(2)` 初值取 1（真值 1.414e(guard)），而迭代上界只有 `guard+10` 步，收敛不住 |
| 2 | `sqrt` 迭代上界 | `guard + 10` 步不足以从错误的初值收敛 | 见缺陷 1 |
| 3 | `sqrt`/`exp`/`ln` 的 `guard` | `guard` 来自 `precision`（**有效数字位**），却在缩放算术里当成**十进制小数位** | 量级偏离 1 很大的输入位数错误 |
| 4 | `ln` 量级归约 | 反复调用 `sqrt` 把参数拉进 (0.7, 1.4]，是 O(precision) 次牛顿迭代 | 极慢；且归约区间的下界写成了 0.8 而非 0.7，区间不对称 |
| 5 | `ln` 主值区 | `while (result.compare(kLn10) >= 0)` 把结果减到 `[-ln10, ln10)`，但负方向用的是 `result.sign_ < 0` 而非与 `ln10` 比较 | 负结果的归约逻辑与正方向不对称 |
| 6 | `pow` 负指数 | `base == 0` 与 `0` 的负数次幂没有防护 | 除以零或返回垃圾值 |
| 7 | `pow` 整数指数 | 手工把字符串解析成 `long long`，`exponent.digits_.size() <= 2` 的判据允许到 10^18 | 解析溢出，静默给出错误结果 |

**另外修正的两处**（不在初稿缺陷表内，但同样重要）：

- **级数中间结果的小数位无限增长**：`atanhSeries` 里 `power = power * zSquared`
  每步把小数位翻倍，第 k 步达到 `(2k+1) * work.precision` 位。
  实测 `ln(2)` 在 precision=100 时耗时 **22 秒**（中间数有 5 万位十进制数）。
  加了「每项舍入到工作精度」后降到 **27 毫秒**（约 800 倍）。
- **`exp` 先舍入还是先移位**：初稿的顺序会在尾数里留下尾随零。
  改为**先移位、后舍入**。
  注意精度 20 时 `exp(100)` 打印成 `26881171418161354484000000000000000000000000`
  是**正确**的 —— 那正是 Python decimal 在 prec=20 下给出的同一个值
  （那串零是 20 位有效数字之外的尾随零，不是 bug 产物）。
  精度 50 时两者一致：
  `26881171418161354484126255515800135873611118.773742`。

**代码评审后修正的三处挂起缺陷**（评审发现；数学本身正确，差分测试全程 730/730）：

| # | 位置 | 缺陷 | 后果 |
|---|------|------|------|
| 8 | `exp` 的保护位数 | 保护位按 **\|x\| 的十进制指数**给，而真正需要保护的是 **\|k\| = \|floor(x/ln10)\|** 的抵消 | x 极小时 k = 0（没有任何抵消），却要多算 205 位 —— `exp(1e-200)` 要 271 ms，`exp(1e-99000)` 永远算不完 |
| 9 | `integerPower` 的结果位数护栏 | 护栏只在「底数整数位数 > 0」时生效，底数 < 1 时整个护栏被跳过 | `pow(0.001, 100000)` 要算 30 万位小数（实测超过 120 s），而「防止 10^100000」的保护从未生效 |
| 10 | 五个超越函数入口 | 只检查 `precision > 0`，工作精度 `precision + 10` 可以超过 `kMaxPrecision` | `sqrt(2) @ precision=99990` 永远算不完，且失败方式**不一致**（有的精度下内层 divide 恰好抛异常，有的直接挂起） |

修正要点：

- **缺陷 8**：保护位改由 `\|k\|` 定。`\|x\| < 1` 时 k 直接可定（x ≥ 0 → k = 0；x < 0 → k = -1），
  **完全绕开除法** —— 除法要先对齐小数位，而长除法是 O(位数²)，x 的小数位有 99000 个时
  实测要 45 秒。绕开除法不改变结果：商四舍五入后仍在 (-0.435, 0.435) 内，floor 必然相同。
- **缺陷 9**：结果位数按「底数的有效位数 + 小数位数」乘以指数估算，整数位与小数位都算在内。
- **缺陷 10**：新增 `kMaxTranscendentalPrecision = 1000`。超越函数的工作精度是**平方级**的
  （实测 `pow(2, 0.5)`：p=200 → 413 ms，p=1600 → 151 s），所以 100000 这个上限对它们远远不够。
  五个入口统一抛 `std::invalid_argument`。

**复审发现的 2 处 Important**（上面三处挂起缺陷的修复本身是对的，数学与
2790 个新旧用例逐字节一致；问题出在修复引入的两处副作用）：

| # | 位置 | 缺陷 | 后果 |
|---|------|------|------|
| 11 | `pow` 的精度闸门位置 | 闸门放在 `pow` 的**最前面**，在 `exponent.scale_ == 0` 的整数指数分支**之前** | 精确的整数次幂被当成超越函数而被拒。`pow(10, 100000)`（p=1000 时合法、结果精确、约 7 s）与 `pow(2, 100)`（p=2000）在修复前都能算，修复后一律抛「precision 超出超越函数的上限」。上限的名字就叫「超越函数」，而精确整数次幂不是超越函数 —— 整数次幂的代价由 `kMaxIntegerPower`（指数）与 `kMaxPowerResultDigits`（结果位数）约束，与超越函数的工作精度毫无关系 |
| 12 | `BigDecimal.cpp` 的指数守卫注释 | 注释写的是「检查必须**在把当前位折进 magnitude 之后**做」，而代码仍是折进**之前**检查 | 注释与代码互相矛盾，实际接受的上限是 10^7 而不是文档写的 10^6（`1e1000009`/`1e1000010`/`1e9999999` 全部被接受）。已随 Task 5 的评审修复一并改掉（检查改为 `magnitude > (kMaxExponentMagnitude - digit) / 10`） |

修正要点：

- **缺陷 11**：闸门下移到 `pow` 的**非整数指数**分支。那条分支才是真正的
  超越函数路径（`exp(b·ln a)`），工作精度是平方级的；整数指数分支走的是
  精确的快速幂，不受此约束。
- **缺陷 12**：见 Task 5 的「评审修复清单」第 13 条。

**另外补上的 2 处 Minor**：

- **前端给的精度上限要跟内核对齐**。Task 7/8/9 原本写的是
  「Web API 接受 precision ≤ 100000、Web UI `max="10000"`、Qt `setRange(1, 10000)`」，
  而内核对超越函数的上限是 1000 —— 用户会拿到一个用不了的取值。
  现在三处都改成 1000（`kMaxTranscendentalPrecision`），命令行帮助里把
  两个上限都写清楚。
- **头文件要写明新异常**。`BigDecimal.h` 的超越函数注释里补上
  「precision 超出上限抛 `std::invalid_argument`」，并区分
  `kMaxTranscendentalPrecision`（超越函数）与 `kMaxPrecision`（除法/舍入）。
  上限常量本身也提到头文件里 —— 那是**公开契约**，前端要靠它判断能请求
  多大的精度。

---

### Task 5: 进制格式化与位运算

**Files:**
- Create: `src/core/Format.h`
- Create: `src/core/Format.cpp`
- Modify: `src/core/BigDecimal.h`（新增 `isInteger`、`integerBlocks`、`truncateToInteger`、`scale`）
- Modify: `src/core/BigDecimal.cpp`
- Modify: `tests/tst_format.cpp`
- Create: `tests/format_probe.cpp`（差分测试探针）
- Create: `scripts/verify_format.py`（Python 差分测试）
- Modify: `CMakeLists.txt`（加入 format_probe 目标）

**Interfaces:**
- Consumes: Task 2 的 `BigDecimal`
- Produces:
  - `namespace hpc`
  - `std::string toBaseString(const BigDecimal& value, int base, bool uppercase = false, int fractionDigits = 0)`
    （base ∈ 2..36；整数部分精确无损；小数部分按目标进制截断并用 `"..."` 标记）
  - `BigDecimal fromBaseString(std::string_view text, int base)`（仅整数）
  - `enum class BitOp { And, Or, Xor }`
  - `BigDecimal bitwise(const BigDecimal& lhs, const BigDecimal& rhs, BitOp op)`（仅整数精确）
  - `BigDecimal bitwiseNot(const BigDecimal& value)`（仅整数精确）
  - `BigDecimal shiftLeft(const BigDecimal& value, int bits)`、`shiftRight(const BigDecimal& value, int bits)`
  - `BigDecimal::isInteger()`、`BigDecimal::integerBlocks()`、`BigDecimal::truncateToInteger()`、`BigDecimal::scale()`
  - `BigDecimal::fromString` 现已支持科学计数法（`1e20`、`2.5E-3`）

- [x] **Step 1: 在 `BigDecimal.h` 加 `isInteger`、`integerBlocks`、`truncateToInteger`、`scale`**

```cpp
    // 是否为整数（没有小数位）。
    bool isInteger() const { return scale_ == 0; }

    // 十进制小数位的个数。
    //
    // 注意与 Context::precision 区分：precision 是**有效数字位**，
    // scale 是**小数位**（十进制小数点后的位数），两者不是一个概念。
    int scale() const { return scale_; }

    // 精确地向零截断到整数（丢弃小数部分，不做舍入）。
    //
    // -1.5 -> -1，1.5 -> 1。与 floor 的区别：floor 向 -∞ 取整，
    // -1.5 的 floor 是 -2。位运算与进制转换的整数部分都用这个语义。
    BigDecimal truncateToInteger() const;

    // 整数的**绝对值**的 base-10^9 小端块（digits_[0] 是最低位块）。
    //
    // 非整数时抛 std::domain_error —— 位运算这类「精确整数运算」的入口。
    // 符号不在这里：符号由 sign() 单独给出。
    const std::vector<std::uint32_t>& integerBlocks() const;

    // **分子**（绝对值）的 base-10^9 小端块。
    //
    // 值 = ±numerator * 10^(-scale)。整数时与 integerBlocks() 相同。
    //
    // 与 integerBlocks() 的区别：这里**不要求**值是整数 —— 小数值的分子
    // 同样有意义（进制转换要靠它判断该值能否在目标进制下有限表示）。
    const std::vector<std::uint32_t>& numeratorBlocks() const { return digits_; }
```

在 `BigDecimal.cpp` 实现：

```cpp
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
```

- [x] **Step 2: 写 `src/core/Format.h`**

```cpp
// 进制转换与位运算：整数部分精确无损，位运算仅对整数有意义。
#pragma once

#include <string>
#include <string_view>

#include "core/BigDecimal.h"

namespace hpc {

// 把 value 转成 base 进制字符串。base 取值 2..36，非法 base 抛 std::invalid_argument。
//
// 整数部分**精确无损**；负值带前导负号；零输出 "0"。
//
// 小数部分：按目标进制逐位提取，最多输出 fractionDigits 位。该进制下无法
// 有限表示时，末尾用 "..." 显式标记截断 —— 不做无声的近似。
// fractionDigits 为 0 时自动推算位数：
//   - 该进制下**能**有限表示 → 位数取**精确展开长度**（由值的最简分数
//     算出，而不是按 log 估算）；
//   - **不能**有限表示 → 位数取最小的 D 使 base^D >= 10^小数位数。
// 整数输入不输出小数点，也不带标记。
//
// 小数部分的**尾随零会被去掉**（与 BigDecimal::toString 的做法一致）：
// 该进制下能有限表示的值给出最短的精确形式（0.5 在二进制下是 "0.1"），
// 只有确实发生了截断时才补上 "..." 标记 —— 标记是如实的，值算得完就没有
// 标记。
//
// **「...」的契约**（截断与精度的唯一约定）：
//
//   - "..." 表示「展开**被截断了**」，且截断的是**小数部分**。
//   - 出现 "..." 时，前面的数字是展开的**真实前缀**（不是四舍五入的
//     近似值），末位数字也是真实数字 —— 删掉它会改变数值。
//   - 该进制下**不能**有限表示的值**一定**带 "..."。
//   - 该进制下**能**有限表示的值**通常**不带 "..."；但如果展开长度
//     太大、超出工作量上限（见下），它同样会带 "..." —— 那时它确实
//     没写完。
//   - 精度预算（fractionDigits，或自动推算的位数）不足时会截断并标记；
//     这**不是**错误。
//
// **工作量上限**：展开代价是 O(位数 × 块数)。当预算大到算不完时，
// toBaseString **不会**抛异常 —— 它算出算得动的那部分位，**截断**并
// 如实加上 "..." 标记。理由：计算器的调用方要的是一个答案（哪怕是带
// 标记的近似值），而不是一个空洞的异常；而且「...」的含义本来就是
// 「没算完」，如实标记不算撒谎 —— 撒谎的是「算完了却说没算完」，
// 那是相反的方向。
//
// 唯一的例外是**非法输入**：base 越界抛 std::invalid_argument。
std::string toBaseString(const BigDecimal& value, int base, bool uppercase = false,
                          int fractionDigits = 0);

// 解析 base 进制整数字符串（可带前导正负号）。base 取值 2..36。
// 空串、非法字符、数字超出进制范围、含小数点时抛 std::invalid_argument。
//
// 只支持整数：任意进制的小数在十进制内核里未必能精确表示（1/3 在三进制下
// 无法有限表示），接受它就等于引入无声的近似。输出侧的小数部分用 "..."
// 标记截断，解析侧则直接拒绝。
BigDecimal fromBaseString(std::string_view text, int base);

enum class BitOp { And, Or, Xor };

// 整数位运算，采用**无限位**二进制补码语义（与 Python 的 int 运算一致）：
// 负数按符号扩展参与运算，运算结果仍是精确整数。
// 任一操作数非整数时抛 std::domain_error。
BigDecimal bitwise(const BigDecimal& lhs, const BigDecimal& rhs, BitOp op);

// 按位取反：~x = -x - 1。非整数输入抛 std::domain_error。
BigDecimal bitwiseNot(const BigDecimal& value);

// 左移 bits 位（相当于乘以 2^bits）。非整数输入抛 std::domain_error；
// bits 为负抛 std::invalid_argument；结果位数超限抛 std::overflow_error。
BigDecimal shiftLeft(const BigDecimal& value, int bits);

// 右移 bits 位（相当于整除 2^bits），负数**向 -∞ 取整**（与 Python 的 >> 一致）：
// -1 >> 100 == -1。
BigDecimal shiftRight(const BigDecimal& value, int bits);

}  // namespace hpc
```

- [x] **Step 3: 写 `src/core/Format.cpp`**

```cpp
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
// 触及上限时**不报错**，而是停在算得动的地方并如实标记「...」：
// 「...」的含义本来就是「展开被截断了」，而报错是把一个本来能给出
// （哪怕是截断的）结果的请求整个拒掉 —— 计算器用户更愿意看到一个
// 带标记的近似值，也不要一个空洞的异常。
//
// 这与「谎报截断」是相反的错误：谎报截断是把截断说成完整；这里
// 明确说「没算完」。
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
// 块序列除以一个 64 位正整数，返回余数（块序列就地变成商）。
//
// divisor 必须是 2 或 5 的幂 —— 这样每一步的「余数 · 10^9 + 块」都
// 不会溢出 64 位：余数 < divisor，当前值 < divisor · 10^9 <= 2^32 · 10^9
// = 4.3·10^18 < 1.8·10^19 = 2^64。
std::uint64_t divideBlkByPower(Blk& blocks, std::uint64_t divisor) {
    std::uint64_t remainder = 0;
    for (std::size_t i = blocks.size(); i-- > 0;) {
        const std::uint64_t current = remainder * kBase + blocks[i];
        blocks[i] = static_cast<std::uint32_t>(current / divisor);
        remainder = current % divisor;
    }
    blocks = trimBlk(std::move(blocks));
    return remainder;
}

// 块序列除以 divisor 的余数（不修改块序列）。
//
// divisor 必须是 2 或 5 的幂（见 divideBlkByPower 的溢出论证）。
std::uint64_t remainderBlk(const Blk& blocks, std::uint64_t divisor) {
    std::uint64_t remainder = 0;
    for (std::size_t i = blocks.size(); i-- > 0;) {
        const std::uint64_t current = remainder * kBase + blocks[i];
        remainder = current % divisor;
    }
    return remainder;
}

// 分子（绝对值）能被 prime 整除的次数（上限 cap）。
//
// 返回 min(v_p(M), cap)；返回 -1 表示「工作量超限，算不出来」。
//
// prime 只取 2 或 5，而块的基数 10^9 = 2^9 · 5^9 —— 因此**整除性只看
// 最低块**：M = m_0 + 10^9 · H，且 v_p(10^9 · H) >= 9。
//
// 但「只看最低块」**只在两个加数的赋值不同时**才成立：
//
//   v_p(m_0) <  9 → 与 v_p(10^9·H) >= 9 必然不同 → v_p(M) = v_p(m_0)（精确）；
//   v_p(m_0) >= 9 → 两者**可能相同**（m_0 恰好也含 9 个 p 因子），
//                    此时 m_0 与 10^9·H 可能互相抵消，v_p(M) 可以**大于**
//                    v_p(m_0) —— 便宜路径的结论不可信。
//
// 旧实现在这里直接把 cap（或 quick）当答案返回，于是**低估**了赋值：
// m_0 = 2^scale·k、10^9·H 恰好抵消的情形下会算出一个偏大的赋值，
// 于是展开被**提前**截断（无声的精度损失），并把本来无限的展开误报成
// 有限。
//
// 因此便宜路径只在**能证明**赋值就是答案时才用，其余一律走精确路径。
int valuationCapped(const Blk& numerator, std::uint32_t prime, int cap) {
    if (cap <= 0) return 0;

    // 第一级：只看最低块。
    std::uint32_t low = numerator[0];
    int quick = 0;
    while (quick < 31 && low % prime == 0) { ++quick; low /= prime; }

    // 分子只有一个块时它**就是**整个分子：赋值已知，无需再猜。
    //
    // 单块的赋值一定 < 31（2^29 < 10^9 < 2^30，5^12 < 10^9 < 5^13），
    // 所以上面的循环不会提前截断 quick。
    if (numerator.size() == 1) return quick >= cap ? cap : quick;

    // quick < 9：两个加数的赋值必不同，v_p(M) = quick（精确）。
    if (quick < 9) return quick >= cap ? cap : quick;

    // quick >= 9：只能确定 v_p(M) >= 9。cap <= 9 时答案就是 cap。
    if (cap <= 9) return cap;

    // 第二级：最低两块合成一个 64 位数（M 的低 18 位十进制数字）。
    // v_p(10^18 · H) >= 18，所以 count < 18 时 v_p(M) = count（仍是精确的）。
    const std::uint64_t pair =
        static_cast<std::uint64_t>(numerator[0]) +
        static_cast<std::uint64_t>(numerator[1]) * kBase;
    std::uint64_t rest = pair;
    int count = 0;
    while (count < 63 && rest % prime == 0) { ++count; rest /= prime; }
    if (count < 18) return count >= cap ? cap : count;

    // count >= 18：只能确定 v_p(M) >= 18。cap <= 18 时答案就是 cap。
    if (cap <= 18) return cap;

    // 第三级：精确路径 —— 逐次相除。
    //
    // 为了少跑几遍，每次尽量除以 p^k（k 取「p^k · 10^9 不溢出 64 位」
    // 的最大值）：p = 2 时 k = 32，p = 5 时 k = 13。
    const int chunk = (prime == 2) ? 32 : 13;
    std::uint64_t chunkDivisor = 1;
    for (int i = 0; i < chunk; ++i) chunkDivisor *= prime;

    Blk value = numerator;
    const long long blockCount = static_cast<long long>(value.size());
    long long work = 0;
    int exact = 0;

    // 整块相除：只有 p^chunk 整除时才除，否则退回单个 p。
    while (exact + chunk <= cap) {
        if (remainderBlk(value, chunkDivisor) != 0) break;
        divideBlkByPower(value, chunkDivisor);
        exact += chunk;
        work += blockCount;
        if (work > kMaxExpansionWork) return -1;
    }

    // 收尾：单个 p 因子。
    //
    // 整除性只看最低块：M ≡ m_0 (mod 10^9)，而 p | 10^9，所以
    // 「M 被 p 整除」等价于「最低块被 p 整除」。
    while (exact < cap) {
        if (value[0] % prime != 0) break;
        divideBlkByPower(value, prime);
        ++exact;
        work += blockCount;
        if (work > kMaxExpansionWork) return -1;
    }
    return exact;
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

    // 十进制特判：**任何**有限十进制小数在十进制下都精确表示，位数就是
    // scale 本身。
    //
    // 这条特判不只是优化 —— 它把十进制路径与「赋值计算」彻底解耦。
    // 赋值计算是唯一可能触及工作量上限的环节（见 valuationCapped），而
    // 十进制不该因为它失败就退化。内核的原生表示本来就知道答案。
    if (base == 10) return scale;

    const Blk& numerator = value.numeratorBlocks();
    const int two = valuationCapped(numerator, 2, scale);
    const int five = valuationCapped(numerator, 5, scale);

    // 赋值算不出来（工作量超限）：不能断言该值能有限表示 —— 保守地当作
    // 「不能」，让调用方走截断路径。那只是少给几位，而不会谎称精确。
    if (two < 0 || five < 0) return -1;

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
// 展开 digits 位小数的工作量上限：算出最多能算多少位。
//
// 每提取一位都要把整个余数乘一次进制并按十进制位切开，代价 O(块数)。
//
// 块数按**最坏情况**估：余数始终 < 1，它的小数位不超过 scale，整数位最多
// 1 位，因此块数不超过 scale/9 + 1。用分子的块数估会严重低估 ——
// 分子的块数可能只有 1，而余数的块数会随展开线性增长。
//
// 返回**实际能展开的位数**（<= digits）。触及上限时**不报错**，而是
// 少给几位并由调用方标记「...」—— 见 kMaxExpansionWork 的说明。
int affordableExpansionDigits(const BigDecimal& value, int digits) {
    if (digits <= 0) return 0;
    const long long blocks = static_cast<long long>(value.scale()) / 9 + 2;
    if (blocks <= 0) return digits;
    const long long affordable = kMaxExpansionWork / blocks;
    if (affordable >= digits) return digits;
    return static_cast<int>(affordable);
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
                // 预算超过工作量上限时**截断**而不是报错：算得动多少给多少，
                // 剩下的用「...」如实标记（见 kMaxExpansionWork）。
                const int affordable = affordableExpansionDigits(value, budget);
                if (affordable > 0) {
                    expandFraction(value, base, affordable, table, fractionText, truncated);
                }
                // 预算没算完 —— 展开一定没写完，标记是如实的。
                //
                // 注意这个判断**不能**放进 expandFraction 里：它会把
                // truncated 覆盖成「余数是否为零」，从而丢掉「预算本身
                // 就不够」这个事实。
                if (affordable < budget) truncated = true;
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
```

- [x] **Step 4: `BigDecimal::fromString` 支持科学计数法**

Task 4 遗留的缺口：内核原本不支持 `'e'` 记法。**扩展 `fromString`** 而不是
在 Format 层另开一路 —— 因为科学计数法是**十进制字面量**的语法，
属于数值表示层；`Format` 只负责「换一种基数表示同一个值」。

实现要点（`BigDecimal.cpp`）：

```cpp
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
```

尾数解析完后应用指数（**精确**移位，不引入舍入）：

```cpp
    if (hasExponent) {
        if (exponent > 0) {
            result.digits_ = shiftLeftDecimal(result.digits_, exponent);
        } else if (exponent < 0) {
            result.scale_ += -exponent;
        }
        result.normalize();
    }
```

指数上限 `kMaxExponentMagnitude = 1000000`（正负两个方向都要限：
正指数是天文数字，负指数是极小的分数，两者都会撑爆内存）。

守卫必须在**把当前数字折进 magnitude 之后**检查（`magnitude >
(kMaxExponentMagnitude - digit) / 10`），否则最后一位会把 magnitude 顶到
10 × 上限 —— 实际上限会变成 10^7，与文档写的 10^6 不符。

- [x] **Step 5: 写 `tests/tst_format.cpp`**

覆盖：base 2..36 的**每一个**进制的往返性质（整数、负数、1000 位大数）、
进制字符表上下两套、非法 base、空串、非法字符、数字越界、小数点、
`+`/`-` 前缀、负零、位运算的负数（无限位补码语义）、`~0 == -1`、`~(-1) == 0`、
移位 0/1/31/32/33/63/64/100/1000/200 位、负数右移向 -∞ 取整、
移位与右移互逆、小数部分的整数部分精确 + 截断标记 + 尾随零去掉、
科学计数法的解析与非法输入。

**评审修复追加的回归用例**（见「评审修复清单」的 11–13）：

- `base.exact_expansion_has_no_truncation_marker`：`toBaseString(0.00001, 20)`
  等于 `0.0001c` 且无标记；`toBaseString(0.0001, 30)` 等于 `0.002l` 且无标记。
- `base.bases_with_unequal_two_and_five_exponents`：二十进制 / 三十进制 / 十进制
  的展开长度各不相同，但都必须精确。
- `base.terminating_decimal_never_truncated_in_base_ten`：`1e-4096` 与 `1e-4097`
  在十进制下都不带标记，且小数位数量正确。
- `base.genuine_truncation_is_still_marked`：1/3 在三进制下、0.5 在三进制下仍
  带标记；0.5 在二进制下不带。
- `base.explicit_fraction_digits_budget_truncates`：显式 `fractionDigits`
  仍是**预算**，位数不足时照样标记。
- `base.valuation_underestimate_does_not_truncate_early`：四个复现值
  （`0.1009765625` 二进制/八进制、`0.1000001024` 五进制/二十五进制）的
  展开**长度**必须与 `fractions.Fraction` 精确算出的期望一致，且都带标记。
- `base.non_terminating_is_reported_as_non_terminating`：`0.1009765625`
  在五进制下必须被报成**非有限**（15 位 + 标记）—— 旧实现误报成有限。
- `base.expansion_work_limit_truncates_instead_of_throwing`：超出工作量
  上限时**截断 + 标记**，不再抛 `std::overflow_error`。
- `base.values_within_work_limit_stay_exact`：工作量之内仍然精确展开。
- `scientific.enforces_exponent_limit_exactly` / `scientific.exponent_limit_bounds_the_scale`：
  `1e1000001` 及以上（含负指数）抛 `std::invalid_argument`。

这些用例对**修复前的代码**全部失败（实测：76 个用例中 5 个用例失败、
15 处断言失败），对修复后的代码全部通过。

**往返性质**（本任务的核心不变量）：

```cpp
TEST(base, round_trip_all_bases) {
    const std::string original = "123456789012345678901234567890";
    const BigDecimal value = make(original);
    for (int base = 2; base <= 36; ++base) {
        const std::string encoded = hpc::toBaseString(value, base);
        CHECK_EQ(hpc::fromBaseString(encoded, base).toString(), original);
    }
}
```

- [x] **Step 6: 写差分测试 `tests/format_probe.cpp` 与 `scripts/verify_format.py`**

探针读入一行命令（用 `|` 分隔字段），输出结果：

```
tobase <base> <uppercase 0|1> <fractionDigits> <十进制值>
frombase <base> <base进制字符串>
and|or|xor <十进制值A> <十进制值B>
not <十进制值>
shl <十进制值> <位数>
shr <十进制值> <位数>
```

`scripts/verify_format.py` 的参考实现：

- 进制转换：手工实现 base 2..36 的格式化（语义与 CPython 的 `int(s, base)` 对齐）
- 位运算：直接用 Python 的 int（无限位二进制补码语义，正是本实现的目标语义）
- 右移：Python 的 `>>`（负数向 -∞ 取整）
- 小数部分：用 `fractions.Fraction` 精确做，独立复现「整数部分精确 +
  尾随零去掉 + 截断标记」的规则

**实测结果**（本实现）：

| 项 | 结果 |
|----|------|
| 差分测试 | **1699 / 1699 通过**（第一轮）；第二轮补上长度/终止性差分后为 **20125 / 20125 通过** |
| 单元测试 | **93 / 93 个用例，失败 0 个断言**（第二轮后为 109 / 109） |
| MSVC 构建 | 零警告 |
| GCC 构建 | 零警告 |

其余三个差分测试（除法 / 超越函数 / 取模）在本任务改动 `fromString`
之后**仍然全绿**（484/484、730/730、1323/1323）。

- [x] **Step 7: 构建并运行测试**

```bash
./scripts/build-core.bat && ./scripts/run-ctest.bat
python scripts/verify_format.py ./build/format_probe.exe
python scripts/verify_format_len.py ./build/format_probe.exe
```
预期：全部通过。

`scripts/verify_format_len.py` 是**第二轮评审后补上**的差分测试：它专门比对
展开的**长度**与**终止性**（而 `verify_format.py` 比对的是完整输出串），
期望值全部由 `fractions.Fraction` 精确算出。它覆盖约 2 万个「随机分数 ×
全部 35 个进制」的组合，并**刻意**大量构造会触发赋值缺陷的值
（最低块恰含 9 个以上 2 因子或 5 因子的分子）。

实测：**20125 / 20125 通过**；对修复前的代码则报出 **230 处**长度或
终止性不符 —— 也就是说这个差分测试确实能抓住那个缺陷。

另有 `scripts/probe_valuation_fix.py` 做聚焦对照：把四个复现值 × 若干
进制的「C++ 实际长度」与「Python 精确期望长度」并排打出来，
修复前 2/6 相符、修复后 6/6 相符。

- [x] **Step 8: 提交**

```bash
git add src/core tests/tst_format.cpp tests/format_probe.cpp scripts/verify_format.py CMakeLists.txt
git commit -m "feat(core): 进制转换与整数位运算"
```

---

#### 初稿缺陷清单（本任务修正的 8 处）

| # | 位置 | 缺陷 | 后果 |
|---|------|------|------|
| 1 | `toBinaryString` | 逐块「取 32 位」直接拼成二进制串 —— 但块的基数是 **10^9**，不是 2^32 | **静默算错**：`9223372036854775807` 的二进制被算成 `166980072253701675007` 而不是 `9223372036854775807`。改成真的做除以 2^32 的长除法 |
| 2 | `addOneBinary` | 从全零串开始加一，进位链之外的高位被抹成 0 | 负数的补码全错：`5 \| -3` 得 `5`（应为 `-3`）、`5 ^ -3` 得 `4`（应为 `-8`） |
| 3 | `shiftRight` | 用 `BigDecimal::divide` 逐位除以 2，精度按 `decimalDigits + bits/3 + 2` 估算 | 除法会**按有效数字位舍入**，右移因此不精确；且 `decimalDigits` 是用 `probe * 10` 循环数出来的，O(n²)。改成**在二进制上直接移位**，完全无损且 O(位数/32) |
| 4 | `shiftRight` 的负数语义 | brief 用 `RoundingMode::Down`（向零截断） | 与 Python 的 `>>`（向 -∞）不一致：`-1 >> 100` 得 `0` 而不是 `-1`。改为向 -∞ 取整 |
| 5 | `bitwise` 的负数 | 直接把两个数的**幅值**二进制串按位运算 | 负数参与运算的结果全错。改成**无限位二进制补码**语义（宽度取 max(幅值位数)+1） |
| 6 | `fromBaseString` | 不处理前导 `-`，且 `+` 也不支持 | `-ff` 解析成非法字符。补上符号处理 |
| 7 | `toBaseString` 的小数部分 | brief 直接拒绝非整数输入 | 与规格 §2.5「显式标注截断」冲突。实现为：整数部分精确无损，小数部分按目标进制截断并用 `"..."` 标记 |
| 8 | `toBaseString` 的负数 | 符号被忽略（只处理了幅值） | `-255` 在十六进制下输出 `ff` 而不是 `-ff` |

另外两处**实测发现的性能/正确性问题**（初稿没有、但实现过程中暴露）：

| # | 位置 | 问题 | 修法 |
|---|------|------|------|
| 9 | `truncateToInteger` | 只把最低块除以 10^rest，块边界与十进制位边界不对齐时全错 | 改为「丢掉整块 + 整块级长除法逐位除」，O(块数) |
| 10 | `blocksToBinary` / `blocksFromBinary` | 逐位 Horner 法是 O(位数 × 块数) | 改为每次处理 32 位，O(位数/32 × 块数) |

#### 评审修复清单（本任务评审后修正的 3 处 Important）

这三条是**评审**在实现完成后发现的，不在初稿里。共同点是：「`...` 标记」与
「指数上限」的**契约**被写成了文档，但实现没有真正兑现。

| # | 位置 | 缺陷 | 后果 | 修法 |
|---|------|------|------|------|
| 11 | `toBaseString` 的小数位数 | 用「最小的 D 使 `base^D >= 10^scale`」估算自动位数 | 该估算只在 base 是 2 或 5 的幂时等价。base 同时带 2 与 5 且指数不等时（20 = 2^2·5、30 = 2·3·5）估算值**短于**真实展开长度，于是输出挂上一个**并不存在**的截断标记：`toBaseString(0.00001, 20)` 得 `0.0001...`（精确值是 `0.0001c`）、`toBaseString(0.0001, 30)` 得 `0.002...`（精确值是 `0.002l`）。违反规格 §2.5「能有限表示时给出最短的精确形式」 | 位数改由**值的最简分数**算出：`exactFractionDigits` 用 `v_2`/`v_5` 的赋值求出精确展开长度（能有限表示则返回长度，否则返回 -1）。展开本身也从「BigDecimal 逐位乘」换成**块级长除法**（`lowDecimalDigits` + `splitDigitAndRemainder`），O(位数 × 块数) → O(位数) |
| 12 | `toBaseString` 的十进制路径 | 把 4096 当成「十进制展开的上限」 | `1e-4097` 在十进制下**精确可表示**（4097 位小数），却被打上截断标记。4096 是**预算**上限，不是可表示性上限；能算完就没有标记 | 自动位数时预算取精确展开长度（不受 4096 限制）；十进制另走一条特判：十进制是内核的**原生**表示，小数位直接取分子的低 scale 位，代价 O(位数) 且永远不带标记 |
| 13 | `BigDecimal::fromString` 的指数守卫 | 上限检查在把当前数字折进 magnitude **之前**做 | 最后一位会把 magnitude 顶到 10 × 上限 —— 实际上限是 10^7 而不是文档写的 10^6，`1e1000009`/`1e1000010`/`1e9999999` 全部被接受。负指数同样如此，`scale` 会到 10^7，一个字面量就能撑爆内存 | 检查改为 `magnitude > (kMaxExponentMagnitude - digit) / 10`（折进之后判定），实际接受的上限与文档一致 |
| 14 | `valuationCapped` 的质因子赋值 | 「整除性只看最低块」的捷径在 `v_p(m_0) >= 9` 时**不成立**，但代码在检查 `quick < 9` **之前**就返回了 `cap` | 分子的最低块含 9 个以上 p 因子时（`m_0 = 2^scale·k`、`10^9·H` 恰好抵消），真实赋值 `v_p(M)` **大于** `v_p(m_0)`，而代码把它当成了 `cap`。后果：展开被**提前**截断（无声的精度损失），且把本来无限的展开误报成有限。实测 `0.1009765625` 在二进制下只给 10 位（应为 34 位）、八进制下只给 4 位（应为 12 位）；`0.1000001024` 在五进制下只给 10 位（应为 15 位）、二十五进制下只给 5 位（应为 8 位） | 便宜路径只在**能证明**赋值就是答案时才用：`quick < 9`（两个加数的赋值必不同）、`count < 18`（同上），其余一律走精确的逐次相除路径；分子只有一个块时赋值已知，直接返回 |
| 15 | `kMaxExpansionWork` 超出时的行为 | 触及工作量上限时抛 `std::overflow_error` | 契约里「`...`」的含义本来就是「展开被截断了」，而抛异常是把一个本来能给出结果的请求整个拒掉 —— 与「显式 `fractionDigits` 不足时截断并标记」的既有行为**自相矛盾**。计算器用户要的是一个答案（哪怕带标记），不是一个空洞的异常 | 超限时**截断**并如实标记「...」：算出算得动的那部分位，剩余的用标记说明。`valuationCapped` 同理，改为返回 -1（算不出来）而不是抛异常；`exactFractionDigits` 收到 -1 时保守地当作「不能有限表示」，只少给几位而不会谎称精确。十进制另走特判，不受赋值计算影响 |

补充说明：

- 第 12 条**新近可达**：`scale` 原先被除法精度限制在几百位以内，科学计数法
  支持之后一个短字面量就能把 `scale` 顶到 10^7。
- 显式传入 `fractionDigits` 时它就是**预算**：值需要更多位时**确实**发生了截断，
  必须标记 —— 这条契约不变，三条修复都不影响它。
- 展开的工作量另设硬闸 `kMaxExpansionWork`：值算得起但展开代价过大时抛
  `std::overflow_error`，而不是谎报截断。

#### 语义决策（与 brief 的差异）

| 项 | brief | 本实现 | 理由 |
|----|-------|--------|------|
| 负数位运算 | 未定义 | 无限位二进制补码（与 Python 的 int 一致） | 唯一自然且与主流语言一致的定义；Python 的 `int` 是事实上的基准 |
| 负数右移 | 向零截断 | 向 -∞ 取整（与 Python 的 `>>` 一致） | 同上 |
| 小数转进制 | 抛 `domain_error` | 整数部分精确 + 小数部分截断并用 `"..."` 标记 | 规格 §2.5 明确要求「显式标注截断，不做无声的近似」 |
| 截断标记字符 | 无 | ASCII 的 `"..."` | 中文 `…` 在 Windows 的 GBK 代码页下会破坏控制台输出与 Python 子进程的解码 |
| 科学计数法 | 不支持 | 扩展 `BigDecimal::fromString` | 科学计数法是十进制**字面量**的语法，属于数值表示层；`Format` 只负责换基数 |
| 自动小数位数 | 按 `base^D >= 10^scale` 估算 | 能有限表示时取**精确展开长度**，否则才用估算值 | 估算只在 base 是 2 或 5 的幂时等价；base 同时带 2 与 5 且指数不等时估算值偏短，会挂上并不存在的截断标记 |
| 「`...` 标记」的含义 | 未定义 | **当且仅当**展开被真的截断时才出现 | 标记是给调用方的**信号**，谎报截断比不标记更糟：调用方会以为精度丢了 |

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

> **精度上限**：`-p/--precision` 的上限是 `kMaxPrecision`（100000），
> 但表达式里一旦出现超越函数（sqrt/exp/ln/log10/pow），精度就受
> `kMaxTranscendentalPrecision`（1000）约束 —— 内核会抛
> `std::invalid_argument`。帮助文本要把这两个上限都写清楚，
> 否则用户只能在撞到异常之后才知道有上限。

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
  -p, --precision <n>   有效数字位数（默认 30，上限 100000）
  -b, --base <n>        输出进制，2..36（默认 10）
  -h, --help            显示本帮助

精度上限（内核的公开契约，见 BigDecimal.h）：
  除法/舍入   kMaxPrecision                = 100000
  超越函数     kMaxTranscendentalPrecision = 1000
  （sqrt / exp / ln / log10，以及 pow 的**非整数指数**分支）

注意：表达式里只要用到超越函数，精度就不能超过 1000 —— 再高内核会抛
std::invalid_argument（工作精度是平方级的，p=1600 时 pow(2, 0.5) 要跑 151 s）。
pow 的**整数指数**分支不受这个限制（那是精确的快速幂）。

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

> **精度上限**：`precision` 字段夹到 `kMaxTranscendentalPrecision`（1000）。
> `/api/evaluate` 的表达式里可以用超越函数（sqrt/exp/ln/log10/pow），
> 那些函数的内核上限就是 1000 —— 放行更高的值只会让用户拿到一个
> 「precision 超出超越函数的上限」的 400，而不是一个算得出来的结果。
> Web UI 的 `<input type="number">` 的 `max` 同样取 1000，
> 两个界面上限必须与内核一致，否则 UI 会提供用不了的取值。

- [ ] **Step 1: 复制 Json 与 HttpServer 到 `src/core/`**

```bash
S="/f/Projects/01_courses/cpp/VS_C++Project/Comprehensive-Calculation-Tool/src/core"
cp "$S/Json.h" "$S/Json.cpp" "$S/HttpServer.h" "$S/HttpServer.cpp" src/core/
```

> **注意**：兄弟项目会独立演进（其 `Json.*` / `HttpServer.*` 已在 2026-11-10 前后
> 修改过 socket 所有权与 `stop()`/`serve()` 逻辑）。本项目持有**独立副本**，
> 与兄弟项目不再同步；差异是允许的。若要更新，以兄弟项目
> `https://github.com/ZajacMo/Comprehensive-Calculation-Tool` 的 main 分支为准。
> 本任务开始前应重新核对下文列出的 API 签名是否仍然成立。
>
> **服务端生命周期**（据兄弟项目 2026-11-10 反馈）：`stop()` 会把监听套接字置为
> 已关闭状态，`m_socketClosed` 在 `listen()` 中重置，因此**同一个 `Server` 对象
> 可以 start → stop → start**；但每次 `start()` 之间不能复用已关闭的句柄。
> `stop()` 内部会 join 线程，正常路径下无残留。本项目的端到端测试若要反复启停，
> 每次 `start()` 前确认没有残留线程即可。

改命名空间：`namespace calc` → `namespace hpc`，`namespace calc::json` → `namespace hpc::json`，
`namespace calc::http` → `namespace hpc::http`。同时把 `Json.h` 里的
`#include "Numeric.h"` 删除（Json 不需要 Numeric）。`HttpServer.cpp` 里的 Winsock
初始化代码保留原样（已在兄弟项目验证可用）。

**已核实的 API（2026-11-10 于兄弟项目源码确认，复制后请再核对一遍）**：

```cpp
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

// HttpServer.h
using Handler = std::function<Response(const Request&)>;
void route(const std::string& path, Handler handler);
void serveStatic(const std::string& prefix, const std::string& directory);
bool start(const std::string& host = "127.0.0.1", int port = 8080);
bool running() const;
int boundPort() const;
void stop();
// 路由只按 path 匹配，不区分 method —— 因此 POST 也能命中同一路由。

// Response 静态构造
static Response json(const std::string& body, int status = 200);
static Response error(int status, const std::string& message);
static Response html(const std::string& body, int status = 200);
static Response text(const std::string& body, int status = 200);
```

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

**已核实的兄弟项目 API（不要自行猜测签名）：** 见 Step 1 末尾的「已核实的 API」清单
（`Json::parse` 是返回 `bool` 的自由函数；`Value::find` 返回指针；`HttpServer::route`
按 path 匹配、不区分 method；服务端生命周期是 `start()` / `running()` / `boundPort()` /
`stop()`，**没有 `run()`**）。

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
//
// 上限取 kMaxTranscendentalPrecision（1000）而不是 kMaxPrecision（100000）：
// /api/evaluate 的表达式里可以出现 sqrt / exp / ln / log10 / pow，
// 那些函数的工作精度是平方级的 —— p=1600 时 pow(2, 0.5) 要跑 151 s，
// p=99990 更是永远算不完。内核对它们统一抛 std::invalid_argument。
//
// 这里提前夹到 1000，是为了让**请求本身**带着一个可用的精度，
// 而不是让用户拿到一个「precision 超出超越函数的上限」的 400。
// 只做加减乘除的表达式其实能到 100000 位，那条路走 /api/arithmetic。
int precisionField(const Object& body) {
    const auto it = body.find("precision");
    if (it == body.end() || !it->second.isNumber()) return 30;
    const int precision = static_cast<int>(it->second.asNumber(30));
    return precision > 0 && precision <= hpc::kMaxTranscendentalPrecision ? precision : 30;
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
                <label>精度 <input id="precision" type="number" value="30" min="1" max="1000"></label>
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

> **精度上限**：精度选择框的范围是 `1 .. kMaxTranscendentalPrecision`（1000）。
> 表达式框里可以用 sqrt/exp/ln/log10/pow，那些函数的内核上限就是 1000 ——
> 让用户能选到 10000 却拿回一个「precision 超出超越函数的上限」的错误，
> 是界面上限与内核上限不一致造成的，不是内核的问题。

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
    // 上限取 kMaxTranscendentalPrecision：表达式里可以用 sqrt/exp/ln/log10，
    // 它们的精度上限是 1000（再高内核抛 std::invalid_argument）。
    precision_->setRange(1, hpc::kMaxTranscendentalPrecision);
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
- Create: `scripts/run-ctest.bat`（Task 1 已建最小版本，本任务补齐/覆盖）
- Create: `scripts/difftest.py`
- Create: `docs/architecture.md`
- Create: `docs/bignum-algorithms.md`
- Create: `README.md`（新写。注意：Task 1 已把原 `README.md`/`README_CN.md` 移到
  `legacy/qt_gui_original/`，仓库根目录现在没有 README，需要新建而不是修改）
- Create: `README_CN.md`（同上）

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
