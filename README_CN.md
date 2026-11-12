# 高精度计算器

一个**任意精度十进制计算内核**（纯 C++17、零第三方依赖），配三个前端：
命令行、零构建步骤的 Web 界面、可选的 Qt 6 桌面界面。

[English](README.md)

---

## 为什么是十进制，而不是二进制浮点

因为 `double` 表示不了大部分十进制分数，而且它的运算本身不精确：

```console
$ hpc_cli -e "0.1 + 0.2"
0.3
```

在 IEEE 754 双精度下，这个表达式的结果是 `0.30000000000000004`。
这里的内核是 **base-10** 的，而且加、减、乘**完全精确** ——
精度参数根本不会作用到它们身上。

`double` 还有一个毛病：它没法告诉你自己错了多少。这个项目可以：

```console
$ hpc_cli -e "1/7" -p 30
0.142857142857142857142857142857
$ hpc_cli -e "exp(1)" -p 40
2.718281828459045235360287471352662497757
```

`-p` 是**有效数字位**（与 Python 的 `decimal.prec` 同义），不是小数位数。

---

## 快速开始

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

**只需要一个 C++17 编译器**，不需要 Qt。

```bash
# 交互式命令行
./build/hpc_cli

# 单次求值
./build/hpc_cli -e "1/3 + 2*sqrt(9)"
# 6.333333333333333333333333333333

# Web 前端（服务默认只监听回环地址）
./build/hpc_server
# 打开 http://127.0.0.1:8098/
```

### 构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `BUILD_GUI` | **OFF** | 构建 Qt 6 桌面界面（需要 Qt 6 SDK） |
| `BUILD_CLI` | ON | 构建命令行前端 |
| `BUILD_SERVER` | ON | 构建 Web 服务端 |
| `BUILD_TESTS` | ON | 构建单元测试与探针 |

要构建 Qt 界面：

```bash
cmake -S . -B build -DBUILD_GUI=ON -DCMAKE_PREFIX_PATH=<Qt6 安装路径>
cmake --build build
```

### Windows 一键脚本

```bat
scripts\build-core.bat       REM 构建除 Qt 外的全部
scripts\build-gui.bat        REM 带 Qt 界面一起构建
scripts\run-ctest.bat        REM 跑全部 CTest 用例
```

`scripts/msvc-env.bat` 负责把编译器、Windows SDK 与 Qt 的环境配好。

> **`.bat` 文件必须保持纯 ASCII。** 它们按系统 ANSI 代码页解析，
> 注释里的非 ASCII 字符会破坏脚本解析，导致 CMake 报
> 「No CMAKE_CXX_COMPILER could be found」。

---

## 命令行

```
hpc_cli                        进入交互模式（Ctrl+D 退出）
hpc_cli -e "<表达式>"          求值单个表达式后退出
hpc_cli --oj                   批量模式（OJ 协议）
```

| 选项 | 说明 |
|---|---|
| `-e, --expression` | 求值单个表达式 |
| `-p, --precision` | 有效数字位（默认 30） |
| `-b, --base` | 输出进制，2..36（默认 10） |
| `-h, --help` | 帮助 |

表达式语法：数字（支持 `0xFF`、`0b1010`、`0o777`、`1e20`）、
运算符 `+ - * / %`（含括号与一元正负号）、
函数 `sqrt` `abs` `pow` `exp` `ln` `log10`。

```console
$ hpc_cli -e "pow(2, 0.5)"
1.41421356237309504880168872421
$ hpc_cli -e "255" -b 16
ff
```

精度超过上限会被**拒绝**，而不是静默夹取 ——
用户要的是他请求的那个精度，悄悄换个精度等于给出一个他没要求的答案。

---

## Web 前端

REST API + 原生 HTML/CSS/JS 前端。**无 npm、零构建步骤。**

| 端点 | 请求体 | 说明 |
|---|---|---|
| `GET /api/health` | — | 健康检查 |
| `POST /api/evaluate` | `{"expression": "...", "precision": 30}` | 表达式求值 |
| `POST /api/arithmetic` | `{"op": "+", "lhs": "...", "rhs": "...", "precision": 30}` | 直接四则 |
| `POST /api/convert` | `{"value": "...", "from": 16, "to": 10}` | 进制转换 |
| `POST /api/bitwise` | `{"op": "&", "lhs": "...", "rhs": "..."}` | 整数位运算 |

服务默认**只监听回环地址**，并且**从不**自动打开浏览器。

---

## 目录结构

```
.
├── src/core/          内核：纯 C++17，零第三方依赖
│   ├── BigDecimal.*   任意精度十进制数（base-10⁹ 分块）
│   ├── Format.*       进制转换与整数位运算
│   ├── Expression.*   表达式解析与求值
│   ├── Json.*         最小 JSON 读写
│   ├── HttpServer.*   跨平台 HTTP/1.1 服务端
│   └── Rounding.h     舍入模式与精度上下文
├── src/cli/           命令行前端（交互 / 单次 / OJ 三模式）
├── src/api/           REST 路由
├── src/server/        Web 服务可执行文件
├── src/ui/            Qt 6 桌面界面（可选目标）
├── web/               原生 HTML/CSS/JS 前端
├── tests/             单元测试、差分探针、界面冒烟测试
├── scripts/           构建、差分测试与基准测试脚本
├── docs/
│   ├── architecture.md        分层、精度语义、如何扩展
│   └── bignum-algorithms.md   核心算法的研究笔记
└── legacy/            三代前身实现的存档
```

---

## 测试

正确性是**对着独立基准**建立的 —— Python 的 `decimal` 与
`fractions.Fraction`，两侧**没有任何共享代码**。

| 套件 | 覆盖内容 |
|---|---|
| `hpc_tests` | 145 个核心单元测试用例 |
| `hpc_api_tests` | 26 个 API 层单元测试用例 |
| `gui_smoke` | 界面离屏渲染冒烟测试（无需显示器）|
| 8 个差分套件 | 26,019 个用例，以 Python 为基准 |
| `api_test.py` | 125 项 HTTP 端到端检查 |
| `api_stall_test.py` | 16 项「停滞客户端不能挂死服务端」检查 |

```bash
# 全部差分套件
python scripts/run_differential.py --build build

# 端到端
python scripts/api_test.py 8099
python scripts/api_stall_test.py 8097
```

差分套件同时注册成 CTest 用例。

> `verify_cli_oj.py` 会在 legacy 可执行文件存在时与它逐例对比。
> CI 上没有这个文件，那一部分会**跳过**并打印原因，脚本仍然 exit 0。

### 性能基准

```bash
python scripts/bench.py --build build --rebuild
```

**故意不注册成 CTest 用例**：耗时随机器而异，放进 CI 只会制造 flaky 失败。
CI 跑的是正确性测试；性能数字由读它的人自己复现。

---

## 设计要点

- **精度是显式参数，绝不是全局状态。** 只有本质上不精确的运算
  （除法、开方、幂、指数、对数）才接受 `Context`；
  加减乘完全精确、**不接受**它 —— 编译期即杜绝精度被静默改变。
- **`Context::precision` 是有效数字位**，与 Python 的 `decimal.prec` 同义，
  不是小数位数。
- **进制是表示层的问题。** 内核始终是 base-10。整数部分转换精确无损；
  该进制下无法有限表示的小数部分会被截断，并带上明确的 `...` 标记，
  **不做无声的近似**。
- **位运算只对整数精确**，采用无限位二进制补码语义（与 Python 的 `int` 一致）。
- **错误带位置。** 语法错误报出错的字符下标，界面会选中那个字符。
- **`%` 是 fmod 语义，不是 Python 的 `%`。** 符号跟随被除数
  （与 C 一致：`-7 % 3 == -1`），左操作数带小数时余数**也**带小数
  （`5.5 % 2 == 1.5`，不是先取整得到的 1）。
- **超越函数的精度上限是*算力*上限，不是接口契约上限。**
  `kMaxTranscendentalPrecision = 1000` **只在真的用到超越函数时**
  才生效；纯加减乘除在 100000 位以内完全合法 —— `1/7` 在
  100000 位精度下算得完。
- **超大值的进制转换很慢。** 100000 位的整数转二进制约 8 秒，
  300000 位约 77 秒。这个上限**从界面上那个 `max="100000"` 就够得着**，
  所以它是已知限制而不是理论上的极端情况。详见
  [`docs/bignum-algorithms.md`](docs/bignum-algorithms.md)。

完整的分层、接口边界与扩展方式见 [`docs/architecture.md`](docs/architecture.md)；
算法细节见 [`docs/bignum-algorithms.md`](docs/bignum-algorithms.md)。

---

## `legacy/`

本项目把同一计算器的**三代**前身实现合并为一个项目，全部保存在 `legacy/` 下：

| 目录 | 代 | 说明 |
|---|---|---|
| `legacy/onlinejudge_calculator/` | 1（最老）| PascalCase API，**先于** Qt 版存在 |
| `legacy/oj_console_edition/` | 2 | `node.cpp`/`node.h` 与 Qt 版**逐字节相同** —— 由 Qt 版反向移植而来 |
| `legacy/qt_gui_original/` | 3 | 唯一的 Qt GUI 实现 |

三代的**核心数值算法是同一份代码的三次复制**，同一批十个缺陷也随复制传播。
十个缺陷逐条记录在 [`legacy/COMPARISON.md`](legacy/COMPARISON.md) 里
（每条都有复现步骤），并已在新内核中全部消除。

`legacy/` 下的代码**保留其原有的版权与许可证条款** —— 它是存档材料，
不属于新实现的一部分。

---

## 许可证

本项目为学习用途开发，仅供个人学习和参考使用。

`legacy/` 下的存档代码保留原作者的版权与许可证条款。
