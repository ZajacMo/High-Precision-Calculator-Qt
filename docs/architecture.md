# 架构

本文说明高精度计算器 v3.0 的分层、数据流与接口边界，以及如何扩展它。

---

## 一句话概括

**一个零依赖的 C++17 十进制内核，三个前端（命令行 / Web / Qt），一份独立基准。**

```
                   ┌─────────────────────────────────────────┐
                   │            src/core/                    │
   CLI ──────────► │                                         │
   (src/cli/)      │   BigDecimal   任意精度十进制数         │
                   │   Format       进制转换与位运算         │
   HTTP ─────────► │   Expression   表达式解析与求值           │
   (src/api/ +     │   Json         最小 JSON 读写            │
    src/server/)   │   HttpServer   跨平台 HTTP/1.1 服务端     │
                   │   Rounding     舍入模式与 Context        │
   Qt ───────────► │                                         │
   (src/ui/)       └─────────────────────────────────────────┘
                                     ▲
                                     │ 纯 C++17，零第三方依赖
                                     │ 核心不依赖任何前端
```

---

## 分层与依赖方向

依赖**只能从上往下**。核心不知道前端的存在。

| 层 | 目录 | 依赖 | 说明 |
|---|---|---|---|
| 核心 | `src/core/` | 仅标准库 | 纯 C++17，**零第三方依赖** |
| API 路由 | `src/api/` | `core` | 把内核能力映射为 JSON 接口 |
| 服务端 | `src/server/` | `core`、`api` | 内置 HTTP 服务 |
| 命令行 | `src/cli/` | `core` | 三种模式合一 |
| 界面 | `src/ui/` | `core`、Qt 6 | **可选**目标 |

### 为什么核心必须零依赖

这不是洁癖，是**可移植性的前提**：

- 默认构建（`BUILD_GUI=OFF`）只要求一个 C++17 编译器。
  `find_package(Qt6)` 出现在 `if(BUILD_GUI)` 之内 —— 装了 Qt 的机器与没装的机器
  走的是**同一条**默认路径。
- 核心可以被独立拿去做库用：链接 `hpc_core` 就得到一个任意精度十进制内核，
  不用继承这个项目的构建系统。
- 依赖越少，被复用的可能性越大。这是本项目「提高复用价值」这个目标
  在架构上的落地方式。

`src/core/Json.*` 与 `src/core/HttpServer.*` 也在核心里 —— 它们是本项目**自己的代码**
（从兄弟项目复制而来并改了命名空间），不是第三方依赖。

---

## 核心的类型与精度语义

### 精度是显式参数，不是全局状态

```cpp
struct Context {
    int precision = 30;                  // 有效数字位数
    RoundingMode mode = RoundingMode::HalfEven;
};
```

- `Context::precision` 是**有效数字位**（与 Python `decimal.prec` 同义），
  **不是**小数位数。`1/7` 在精度 10 下是 `0.1428571429`（10 位有效数字），
  而 `100/7` 在精度 10 下是 `14.28571429` —— 整数部分占掉了有效数字位。
- 只有**本质上不精确**的运算才接受 `Context`：除法、开方、幂、指数、对数。
- 加、减、乘**完全精确**，**不接受** `Context` —— 编译期即杜绝精度被隐式改变。

这一条是对原实现最重要的修正之一。原实现用 `static int Number::FracBit`
决定所有运算精度，GUI 里改一次设置会**静默影响**之后所有计算。

### 除法为什么用静态形式而不是运算符重载

```cpp
static BigDecimal divide(const BigDecimal& lhs, const BigDecimal& rhs,
                          const Context& context);
```

二元运算符不能带 `Context` 参数（MSVC 报 C2804），而且静态形式对
「精度是第二个操作数」这件事更贴切。

### 舍入模式

`HalfEven`（银行家舍入）、`HalfUp`、`Down`（向零截断）、`Up`（远离零截断）、
`Ceil`（向 +∞）、`Floor`（向 -∞）。语义与 Python `decimal` 的名字对齐。

### 运算上限是公开契约

```cpp
kMaxPrecision                = 100000   // 除法与舍入
kMaxTranscendentalPrecision  = 1000     // 超越函数
kMaxIntegerPower             = 100000   // pow 的整数次幂
kMaxPowerResultDigits        = 300000   // pow 的结果位数
kMaxExponentMagnitude        = 1000000  // 科学计数法的指数
```

前端要靠它们判断能请求多大的精度。**精度越界是被拒绝的，不是静默夹取的** ——
用户要的是他请求的那个精度，悄悄换个精度等于给出一个他没要求的答案。

### `kMaxTranscendentalPrecision` 是算力上限，不是接口契约上限

这一条最容易误解，值得单独说：

- 超越函数（sqrt / exp / ln / log10，以及 pow 的**非整数指数**分支）
  的工作精度是**平方级**的，所以另设一个 1000 的上限。
- 但这个上限**只在真的用到超越函数时**才生效。纯加减乘除的表达式在
  100000 位以内**完全合法** —— `1/7` 在 100000 位精度下算得完。
- 所以「上限 1000」**不是**「接口最多支持 1000 位」。把上限设成 1000
  会让「纯算术表达式要 5000 位」这个合法请求**够不着**。

`pow` 的**整数指数分支**不受这个上限约束：它走精确的快速幂，
代价由 `kMaxIntegerPower`（指数）与 `kMaxPowerResultDigits`（结果位数）
单独约束。`pow(10, 100000)` 在 `precision=2000` 下完全合法。

### `scale_` 与 `precision` 不是一回事

| | 含义 | 例子 |
|---|---|---|
| `Context::precision` | **有效数字位**（与 Python `decimal.prec` 同义） | 精度 10 的 `1/7` → `0.1428571429` |
| `BigDecimal::scale()` | **十进制小数位**（小数点后的位数） | `0.125` 的 scale 是 3 |

`scale_` 是**值的属性**（存下来的），`precision` 是**运算的参数**
（算的时候给）。归一化会把多余的尾随零去掉并同步收缩 `scale_`，
所以 `1.500` 存成 `scale_ = 1` 而不是 3。

### `%` 是 fmod 语义，不是 Python 的 `%`

表达式里的 `%` 的语义是 **fmod**（C 的 `fmod`），**不是** Python 的
`%`。差别有两条：

- **符号跟随被除数**（与 C 一致）：`-7 % 3 == -1`。
- **左操作数带小数时，余数也带小数**：`5.5 % 2 == 1.5`，
  而不是「先把 5.5 取成 5 再取余」得到的 1。

内核的实现是把两个数对齐到共同的小数位再做**整数**长除法，所以
`10 % 0.3 == 0.1`（0.3 的二进制展开是无限循环小数，对齐到共同小数位
后余数是 0.1）。这与 C 的 `fmod` 一致。

### 进制转换的诚实性：`toBaseString`

```cpp
std::string toBaseString(const BigDecimal& value, int base, bool uppercase = false,
                          int fractionDigits = 0);
```

- **整数部分精确无损**。
- 小数部分按目标进制逐位提取；该进制下**无法有限表示**时，末尾用
  **`"..."`** 标记截断 —— 不做无声的近似。
- `"..."` 表示「展开**被截断了**」，且前面那些数字是展开的**真实前缀**
  （不是四舍五入的近似值），末位数字也是真实数字 —— 删掉它会改变数值。
- 该进制下**能**有限表示的值**通常**不带 `"..."`；但如果展开长度太大、
  超出工作量上限，它同样会带 `"..."` —— 那时它确实没写完。
- 工作量超限时 `toBaseString` **不抛异常**：它算出算得动的那部分位，
  截断并如实加上 `"..."`。理由：调用方要的是一个答案（哪怕带标记的
  近似值），而不是一个空洞的异常。

**已知的性能上限**：展开代价是 O(位数 × 块数) —— 也就是近似平方级。
实测（MinGW g++ 11.2 `/O2`，本机，`python scripts/bench.py` 的 E 组）：

| 十进制位数 | 转二进制耗时 | 相对上一行的耗时倍数 | 复杂度指数 |
|---|---|---|---|
| 10000 | 0.079 秒 | — | — |
| 100000 | 8.17 秒 | 106.9× | 2.03 |
| 300000 | 77.19 秒 | 9.6× | 2.06 |

100000 位是**界面上那个 `max="100000"` 够得着的** —— 所以必须写明，
不能让人以为「高精度就等于快」。

### 位运算：无限位二进制补码，只对整数有意义

```cpp
enum class BitOp { And, Or, Xor };
BigDecimal bitwise(const BigDecimal& lhs, const BigDecimal& rhs, BitOp op);
BigDecimal bitwiseNot(const BigDecimal& value);
BigDecimal shiftLeft(const BigDecimal& value, int bits);
BigDecimal shiftRight(const BigDecimal& value, int bits);
```

- 采用**无限位**二进制补码语义（与 Python 的 `int` 运算一致）：
  负数按**符号扩展**参与运算，运算结果仍是精确整数。
- `~x == -x - 1`。
- 右移向 **−∞** 取整（与 Python 的 `>>` 一致）：`-1 >> 100 == -1`。
- **任一操作数非整数时抛 `std::domain_error`** —— 位运算与进制转换
  的整数部分都用「精确向零截断」的语义（`truncateToInteger`），
  而**不是** `floor`（`-1.5` 的截断是 −1，`floor` 是 −2）。

---

## 数据流：一次求值走过的路

以命令行的 `hpc_cli -e "1/3 + 2*sqrt(9)"` 为例：

```
  字符串 "1/3 + 2*sqrt(9)"
        │
        ▼
  Expression::evaluate(text, context)          src/core/Expression.cpp
        │  递归下降解析；深度上限 256（RAII 守卫）
        │  产出：除法 / sqrt 两个运算节点
        ▼
  BigDecimal::divide(lhs, rhs, context)        src/core/BigDecimal.cpp
        │  逐块长除法 + 二分试商
        │  **精确余数**判定舍入方向（不做双重舍入）
        ▼
  BigDecimal::sqrt(value, context)             src/core/BigDecimal.cpp
        │  牛顿迭代
        ▼
  BigDecimal::toString()                       十进制字符串
        │
        ▼
  stdout
```

**误差只在一处产生**：除法与超越函数的最后舍入。加减乘全程精确。

### 错误处理

| 情况 | 抛出 | 说明 |
|---|---|---|
| 表达式语法错误 | `ExpressionError` | 携带 `position()`，指向出错字符 |
| 除以零、负数开方、非正数取对数 | `std::domain_error` | |
| 非法输入 | `std::invalid_argument` | |
| 结果不可计算（参数过大） | `std::overflow_error` | |

三个前端各自把这些异常翻译成自己的错误呈现方式：CLI 打印中文提示，
HTTP 层转成 400 + JSON，Qt 层显示带位置的注解并在输入框里选中出错字符。

---

## 接口边界

### 核心对外暴露什么

```cpp
// src/core/BigDecimal.h
BigDecimal::fromString(std::string_view)      // 精确解析
BigDecimal::toString()                        // 十进制字符串
operator+ operator- operator*                // 精确
static divide / modulo / floor / ceil / round // 需要 Context
static sqrt / pow / exp / ln / log10          // 需要 Context
compare / equals / abs

// src/core/Format.h
toBaseString(value, base, uppercase, fractionDigits)
fromBaseString(text, base)                    // 只接受整数
bitwise / bitwiseNot / shiftLeft / shiftRight // 仅整数

// src/core/Expression.h
evaluate(std::string_view, const Context&)     // 抛 ExpressionError
```

### 核心**不**暴露什么

- 没有任何 I/O、文件、线程、时间的依赖。
- 没有全局可变状态。精度、舍入模式都由调用方通过 `Context` 传入。
- 没有 `friend` 式的后门 —— 前端不能直接改内部表示。

---

## 如何扩展

### 加一种运算

1. 在 `src/core/BigDecimal.{h,cpp}` 里加静态方法，**接受 `Context`**。
2. 精确的算法照抄 `operator+` 的做法：不接受 `Context`，内部不丢精度。
3. 在 `src/core/Expression.cpp` 里把它接进解析器 —— 顺便想清楚**参数怎么校验**
   （负数开方要报 `std::domain_error`，而不是返回一个 NaN 之类的东西）。
4. 加**差分测试**：写一个探针（`tests/xxx_probe.cpp`）读入用例、输出结果；
   写一个 Python 脚本用**独立**的参考实现（`decimal` 或 `fractions.Fraction`）
   算出期望值并逐例比对。
5. 把探针注册进 `CMakeLists.txt`，把脚本加进 `scripts/run_differential.py` 的清单。

**第 4 步不是可选的。** 这个项目的三代前身都是「看着能跑、实际有缺陷、
没人拿独立基准验证过」。差分测试是这个项目唯一能证明正确性的东西。

### 加一个前端

1. 新建 `src/<你的前端>/`，只依赖 `hpc_core`。
2. 在 `CMakeLists.txt` 里加一个 `option(BUILD_XXX ...)`，默认 **ON**（除了 GUI），
   并且**加在 `if(BUILD_XXX)` 里** —— 核心的默认路径不能因此多出任何依赖。
3. 在 `scripts/run_differential.py` 里补上这个前端的测试套件。

### 加一种进制

进制是**表示层**的问题，不是算术问题。核心始终是 base-10。
`toBaseString` / `fromBaseString` 已经把 2..36 都覆盖了；
要加更大的进制（比如 base 62），改 `src/core/Format.cpp` 即可，核心不动。

---

## 三个前端

### 命令行（`src/cli/`）

```bash
hpc_cli                    # 交互模式（Ctrl+D 退出）
hpc_cli -e "<表达式>"      # 求值单个表达式后退出
hpc_cli --oj               # 批量模式（OJ 协议）
```

| 选项 | 说明 |
|---|---|
| `-e, --expression` | 求值单个表达式 |
| `-p, --precision` | 有效数字位数（默认 30） |
| `-b, --base` | 输出进制，2..36（默认 10） |
| `-h, --help` | 帮助 |

### Web（`src/api/` + `src/server/` + `web/`）

REST API + 内置 HTTP 服务 + 原生 HTML/CSS/JS 前端。**零 npm、零构建步骤。**

| 端点 | 说明 |
|---|---|
| `GET /api/health` | 健康检查 |
| `POST /api/evaluate` | 表达式求值，body `{"expression": "...", "precision": 30}` |
| `POST /api/arithmetic` | 直接四则，body `{"op": "+", "lhs": "...", "rhs": "...", "precision": 30}` |
| `POST /api/convert` | 进制转换，body `{"value": "...", "from": 16, "to": 10}` |
| `POST /api/bitwise` | 位运算（整数），body `{"op": "&", "lhs": "...", "rhs": "..."}` |

服务端默认**只监听回环地址**（127.0.0.1），不会暴露到局域网。
它**从不**自动打开浏览器。

### Qt（`src/ui/`）

**可选目标**，默认 `BUILD_GUI=OFF`。界面重新设计过：控件语义化命名，
去掉全局静态状态，精度改为界面级设置并传入 `Context`。

---

## 服务端的健壮性设计

`src/core/HttpServer.cpp` 有三处值得单独说明 —— 它们都是「自己写的 socket 服务」
会踩的坑：

1. **Windows 上关闭监听套接字不会唤醒阻塞的 `accept()`**。
   `shutdown()` 唤醒不了，`closesocket()` 会让 accept 返回一个**无效套接字**，
   服务于是进入「监听套接字已关但 accept 还在跑」的状态，表现为 `stop()` 卡住。
   → 解法：把阻塞 `accept()` 换成**带超时的 poll 循环**
   （Windows 用 `WSAPoll`，POSIX 用 `poll`），超时约 100 ms，
   让 `serve()` 周期性醒来检查停止标志。

2. **整条连接有总时限**（从 `accept()` 起算，默认 15 秒）。
   逐次读超时挡不住「慢速滴字节」的客户端 —— 每次单独的读都很快返回，
   静默计时器不触发。→ 解法：一个覆盖整条连接的总时限。

3. **请求体有上限**（4 MiB），超了返回 413；请求头有上限（64 KiB），
   超了返回 431。

每条连接一个线程（上限 16 个）。连接线程是 detach 的，所以进程可以在
在途请求还没算完时直接结束（见 `src/server/main.cpp` 的宽限期退出逻辑）。

---

## 构建

| 选项 | 默认 | 说明 |
|---|---|---|
| `BUILD_GUI` | **OFF** | 构建 Qt 6 桌面界面（需要 Qt 6 SDK） |
| `BUILD_CLI` | ON | 构建命令行前端 |
| `BUILD_SERVER` | ON | 构建 Web 服务端 |
| `BUILD_TESTS` | ON | 构建单元测试与探针 |

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows 上有现成的一键脚本：`scripts/build-core.bat`（不需要 Qt）、
`scripts/build-gui.bat`、`scripts/run-ctest.bat`。
`scripts/msvc-env.bat` 负责把编译器、SDK 与 Qt 的环境配好。

> `.bat` 文件必须**保持纯 ASCII**。`.bat` 按系统 ANSI 代码页解析，
> 非 ASCII 的注释会破坏脚本解析，导致 CMake 报
> 「No CMAKE_CXX_COMPILER could be found」。

---

## 测试

| 层次 | 内容 |
|---|---|
| 单元测试 | `hpc_tests`（核心，145 个用例）、`hpc_api_tests`（API 层，26 个用例） |
| 界面冒烟 | `gui_smoke`（离屏渲染，无需显示器） |
| **差分测试** | 八个套件，以 Python `decimal` / `fractions.Fraction` 为**独立基准** |
| 端到端 | `api_test.py`（125 项）、`api_stall_test.py`（16 项） |

差分测试是正确性的**硬证据**：参考实现与被测实现**没有任何共享代码**。
除法那一套用 `fractions.Fraction` 做**精确有理数**运算再手工按指定模式舍入
（`decimal` 的除法只支持 ROUND_HALF_EVEN，且用 `decimal` 会有中间精度损失
与双重舍入问题）。

跑全部差分套件：

```bash
python scripts/run_differential.py --build build
```

性能数字由 `python scripts/bench.py --build build --rebuild` 产出，
**故意不注册成 CTest 用例** —— 耗时随机器而异，放进 CI 只会制造 flaky 失败。
CI 跑的是正确性测试；性能数字由维护者在自己的机器上复现。

### 验证方法论：这套测试的**盲区**在哪

这一节是给「想改这个内核、想知道自己改坏了什么」的人看的。
差分测试很强，但**它强在什么地方、不强在什么地方**，必须写清楚 ——
否则会有人把「差分测试全绿」当成「实现一定对」。

#### 差分脚本用的是**预编译的探针二进制**

八个差分套件跑的是 `./build/div_probe` 之类的**探针可执行文件**，
不是实时编译的代码。所以：

> **改了实现之后，必须重新构建探针，否则差分测试会静默地继续通过。**

```bash
# 改了 src/core/*.cpp 之后，探针必须重建：
cmake --build build          # 这一步是必须的
python scripts/run_differential.py --build build
```

跳过构建的后果不是「测试失败」而是「**假绿**」：脚本跑的是**旧**二进制，
它当然通过。这个坑**在本项目的开发过程中咬过两次** —— 两次都是
「改了实现，跑差分测试，通过了，于是以为改动是对的」，实际上一行
新代码都没被测到。

同理，改了 `tests/*_probe.cpp` 也要重建（探针自己的逻辑变了）。

#### 差分的错误通道是**二值的**

差分脚本的判定方式是「C++ 的输出与 Python 的期望**是否相同**」。
这意味着：

- 它能发现「结果不同」；
- 它**分不清**「因为**对的原因**出错」与「因为**错的原因**出错」。

举个具体的例子：C++ 抛了异常、Python 那边期望的是一个具体值，
差分只看到「不一样」。至于 C++ 是因为除数为零而抛，还是因为解析
失败而抛，还是抛了个 `std::bad_alloc` —— 差分**一概不管**。

**错误消息与错误位置由单元测试负责**，不由差分负责：
`ExpressionError::position()` 指向**出错的字符本身**（而不是它后面
的位置），串尾那种「意外结束」指向串尾；这些都由 `tst_expression.cpp`
钉死。

#### 其它几条「容易骗过自己」的地方

- **`scripts/check_plan_cmake_block.py` 用的是 Qt 自带的 CMake**
  （`C:\Qt\Tools\CMake_64\bin\cmake.exe`），**不是** PATH 上的那个。
  PATH 上的是 3.31.6，它会在 AUTOMOC 那一步失败。两者都「能跑」，
  但只有前者跟日常构建用的是同一个 —— 校验计划里的块必须用**真实
  构建环境里那个** cmake。
- **`.bat` 文件必须是纯 ASCII**。非 ASCII 的注释在 ANSI 代码页下
  （本机代码页 936）会让批处理文件出错 —— 而且报错信息指向的位置
  往往不是真正出问题的那一行。`.github/workflows/*.yml` 同理。
- **Windows 上关闭一个「监听中」的套接字不会唤醒阻塞的 `accept()`**。
  所以服务端不用「关掉套接字来停服」，而是用超时轮询
  （`poll` / `WSAPoll`）的主循环。
- **每条连接还有总时限**，不只是逐次读超时：逐次读超时挡不住
  「每几秒滴一个字节」的客户端 —— 每次单独的读都很快返回，
  静默计时器一直不触发。只有「这条连接总共能活多久」的上限才能
  断开它。请求体另有 **4 MiB** 上限，超了立刻回 413。
- **`scripts/verify_cli_oj.py` 需要 legacy 可执行文件**做基准。
  没有它时，脚本**跳过**依赖 legacy 的那几部分并打印原因，
  **仍然以 0 退出** —— 这样 CI 上「没有 legacy 基线」不会被误当成
  「测试失败」。所以 CI 覆盖的是它的第 2/3/4 部分（与正确算术对比、
  畸形输入健壮性、非法运算符之后的输入流对齐）。
- **Windows 上 Python 的 `subprocess` 拒绝「相对路径 + 正斜杠」的
  可执行文件**（即使那个文件确实存在）。所以脚本里传的一律是
  **绝对路径 + 反斜杠**，CMake 侧用 `$<TARGET_FILE:...>` 传。

---

## 相关文档

- [`bignum-algorithms.md`](bignum-algorithms.md) —— 核心算法的研究笔记
- [`../legacy/README.md`](../legacy/README.md) —— 三代前身实现的来源与谱系
- [`../legacy/COMPARISON.md`](../legacy/COMPARISON.md) —— 三代实现的逐项对比与缺陷清单
- [`../docs/superpowers/specs/2026-11-10-high-precision-calculator-v3-design.md`](../docs/superpowers/specs/2026-11-10-high-precision-calculator-v3-design.md) —— 设计规格
