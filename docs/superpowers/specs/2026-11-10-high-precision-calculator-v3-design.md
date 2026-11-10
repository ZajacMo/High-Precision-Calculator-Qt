# 高精度计算器 v3.0 —— 统一重构设计规格

日期：2026-11-10
状态：已获用户批准，待实施
仓库：`cpp/QtProject/calculator`（远程 `ZajacMo/Qt-High-Precision-Calculator`）
分支：`unified`（从 `console`/`main` 同一提交 `eebe3ba` 切出）

---

## 1. 目标

把工作目录下三份互不相干的高精度计算器实现合并为一个项目，并大幅提升其工程质量、
复用价值与研究价值。

三份来源（已通过 md5 逐一比对确认谱系）：

| 代 | 位置 | 特征 |
|---|---|---|
| 1（最老） | `cpp/VSC_C++Project/OnlineJudge/Calculator/` | API 用 PascalCase（`Get`/`Show`/`Copy`/`Release`/`Transform_Sign`/`POWER`），注释里写「Qt版参数为待转化的字符串QString类型」——证明它**先于** Qt 版存在 |
| 2 | `cpp/CLion/C++Programming/OJ_Calculator Console Edition/` | `node.cpp`/`node.h` 与 Qt 版**逐字节相同**；`number.cpp` 相对 Qt 版仅差 `QString`→`std::string` 四处替换——证明它**由 Qt 版反向移植**而来 |
| 3（最新） | `cpp/QtProject/calculator/` | 唯一的 Qt GUI 实现 |

三代全部保留到 `legacy/`，作为研究与教学素材。

### 成功标准

1. 三份代码在一个项目内可查、可编译、可运行
2. 核心算法**正确性有独立基准证明**（Python `decimal` 差分测试）
3. 零第三方依赖即可构建核心内核（不强制 Qt）
4. 同一套核心服务于 CLI / Web / Qt 三种前端
5. 保留原实现的全部能力，并扩展出可复用价值

---

## 2. 架构

### 2.1 总体形态

沿用兄弟项目 `cpp/VS_C++Project/Comprehensive-Calculation-Tool` 已落地并验证的 **v2.1「自包含内核 + Web 前端」**架构
（提交 `362a2f7`），而非新造轮子。该架构是用户此前明确认可的方向，其理由记录在案：
Qt SDK（2GB+、仅三种桌面平台）是可移植性负担。

```
cpp/QtProject/calculator/
├── src/core/        纯 C++17，零第三方依赖
│   ├── BigDecimal.{h,cpp}      高精度十进制数（base-10⁹ 分块）
│   ├── Expression.{h,cpp}      表达式解析与求值
│   ├── Format.{h,cpp}         进制格式化、位运算
│   ├── Rounding.{h,cpp}        舍入策略
│   └── Json.{h,cpp}            最小 JSON 读写
├── src/cli/         命令行前端（交互 / 批量 / OJ 三合一）
├── src/api/         REST 路由
├── src/server/      内置 HTTP 服务
├── src/ui/          Qt 桌面界面（可选目标，默认 OFF）
├── web/             原生 HTML/CSS/JS 前端（无 npm、无构建步骤）
├── legacy/          三代原始实现存档（见 §7）
├── tests/           单元测试
├── scripts/         构建 / 差分测试 / 截图脚本
└── docs/            架构与算法研究笔记
```

### 2.2 核心数值类型

**内部表示**：base-10⁹ 分块（GMP 风格），小端存储。

```cpp
namespace hpc {

// 值 = sign_ * Σ digits_[i] * 10^(9·i) · 10^(-scale_)
class BigDecimal {
public:
    BigDecimal();                              // 0
    explicit BigDecimal(int64_t);
    static BigDecimal fromString(std::string_view);   // 精确解析
    std::string toString() const;

    // 精确运算：无需精度参数，结果总是精确的
    BigDecimal operator+(const BigDecimal&) const;
    BigDecimal operator-(const BigDecimal&) const;
    BigDecimal operator*(const BigDecimal&) const;
    // 不精确运算：需要显式精度
    BigDecimal operator/(const BigDecimal&, const Context&) const;

    static BigDecimal sqrt(const BigDecimal&, const Context&);
    static BigDecimal pow (const BigDecimal&, const BigDecimal&, const Context&);
    static BigDecimal exp (const BigDecimal&, const Context&);
    static BigDecimal ln  (const BigDecimal&, const Context&);
    static BigDecimal log10(const BigDecimal&, const Context&);
    static BigDecimal abs (const BigDecimal&);
    static BigDecimal floor/ceil/round(const BigDecimal&, const Context&);
    static BigDecimal modulo(const BigDecimal&, const BigDecimal&);

    int compare(const BigDecimal&) const;      // <0 / 0 / >0
    bool equals(const BigDecimal&) const;

private:
    int sign_;                    // -1 / 0 / +1
    std::vector<uint32_t> digits_; // 小端 base-10⁹ 分块
    int scale_;                   // 小数位数（以 10 为单位）
};

}
```

**关键设计：精度是显式参数，不是全局状态。**

原实现用 `static int Number::FracBit` 决定所有运算精度——GUI 里改一次设置会静默影响
之后所有计算。新设计把精度放进 `Context`：

```cpp
struct Context {
    int precision = 30;              // 有效数字位数
    RoundingMode mode = RoundingMode::HalfEven;
};
```

`+ - *` 完全精确，**不接受** `Context`（编译期即杜绝误用）。只有除法、开方、幂、
指数、对数才需要它——这些运算本质上不精确。

### 2.3 为什么用 base-10⁹ 分块

实测数据（MinGW g++ 11.2 `-O2`，本机）：

| 位数 | 单数字位乘法 | base-10⁹ 分块乘法 | 倍数 |
|---|---|---|---|
| 100 | 0.03 ms | 0.00 ms | 19× |
| 1000 | 2.46 ms | 0.02 ms | 108× |
| 10000 | 231 ms | 1.92 ms | 120× |

差距随位数放大（皆为 O(n²)，但常数差约 100×）。在 20–50 位这类常规场景两者都是
微秒级、无实际差异；到 800–1600 位时分块方案是压倒性的。分块方案在小数场景不吃亏、
在大数场景不落后，是唯一在所有场景都不落下风的选项。

**代价**：核心算法复杂度上升，`docs/bignum-algorithms.md` 的研究笔记需额外说明
块对齐与进位边界。这是接受的代价——它换来的是「所有场景最优」。

### 2.4 已验证的算法

除法实现已在原型中完成并**对着 Python `decimal` 验证 507/507 用例通过**：

- **主实现**：逐块长除法 + 二分试商。实现简单、可审计、易验证。
- **进阶项**：Knuth Algorithm D（归一化 + 试商估计 + 加回修正），作为
  `docs/bignum-algorithms.md` 的对照实现与优化方向，附基准数据。

原型阶段踩过的 4 个坑（块序大小端、进位插入位置、试商估计溢出、加回越界）
全部由差分测试捕获——这直接证明了「必须有独立基准」的必要性，写入 §6。

### 2.5 多进制与位运算

**核心始终是 base-10**。进制是**表示层**问题，不是算术问题。

- 解析：`0x` / `0b` / `0o` 前缀 → 十进制内部表示
- 输出：可指定 base（2/8/10/16/36），整数部分**精确无损**
- 转换经由反复除法实现，已验证往返无损（原型实测）
- **精度损失**：二进制/十六进制下 `1/3` 无法有限表示。处理方式：按目标进制的
  精度边界输出，并**显式标注截断**，不做无声的近似。

**位运算**（`&` `|` `^` `~` `<<` `>>`）**仅对整数精确支持**，通过二进制表示实现。
浮点位运算一律拒绝并给出明确错误。

### 2.6 表达式求值

递归下降解析，支持：`+ - * / %` 优先级、括号嵌套、一元正负号，
函数 `sqrt() abs() pow() exp() ln() log10()`。错误信息带字符位置。

---

## 3. 前端

三个前端共用 `src/core`，互不重复实现逻辑。

### 3.1 CLI（`src/cli/`）

三种模式合一：

```
hpc_cli                          # 交互式
hpc_cli -e "1/3 + 2*sqrt(9)"     # 单次求值
hpc_cli --oj < input.txt         # 批量（OJ 兼容格式）
```

OJ 模式**保持与原命令行版完全一致的输入输出协议**，以便直接替换原 OJ 提交。
原协议：先读测试组数，每组读 `运算符 数字1 数字2`；非法运算符或除数过小输出 `ERROR`。

### 3.2 Web（`src/api/` + `src/server/` + `web/`）

REST API + 内置 HTTP 服务 + 原生 HTML/CSS/JS 前端。零 npm、零构建步骤。

端点命名与兄弟项目保持一致的风格（`/api/health` + 按功能命名的动词端点）：

| 端点 | 说明 |
|---|---|
| `GET /api/health` | 健康检查，供端到端测试探测服务是否就绪 |
| `POST /api/evaluate` | 表达式求值，body `{"expression": "1/3 + 2*sqrt(9)"}` |
| `POST /api/arithmetic` | 直接四则，body `{"op": "+", "lhs": "...", "rhs": "...", "precision": 30}` |
| `POST /api/convert` | 进制转换，body `{"value": "...", "from": 16, "to": 10}` |
| `POST /api/bitwise` | 位运算（整数），body `{"op": "&", "lhs": "...", "rhs": "..."}` |

`/api/evaluate` 与 `/api/arithmetic` 均接受可选 `precision` 字段；缺省用
`Context::default_()`。非法表达式返回 400 与带字符位置的错误信息。

### 3.3 Qt（`src/ui/`）

**可选目标，默认 OFF**（`-DBUILD_GUI=ON` 开启）。

界面**重新设计**（用户明确选定），要点：
- 控件语义化命名，取代现有 `pushButton_2`…`pushButton_18`
- 去掉全局静态状态，精度改为界面级设置并传入 `Context`
- 修复资源加载（原 `QIcon("://ICON.jpg")` 写法有误，应为 `:/ICON.jpg`）
- 支持表达式输入、进制切换、历史记录

---

## 4. 构建与工具链

```cmake
option(BUILD_GUI   "构建 Qt 图形界面（需要 Qt 6）" OFF)
option(BUILD_CLI   "构建命令行界面"              ON)
option(BUILD_TESTS "构建单元测试"                ON)
option(BUILD_SERVER "构建 Web 服务端"            ON)
```

默认配置**只需一个 C++17 编译器**，不依赖 Qt——这是可移植性的关键。

**本机工具链**：MSVC 2022 BuildTools `14.44.35207` + Windows SDK `10.0.26100.0`
+ Qt 6.6.2 `msvc2019_64` + CMake `C:\Qt\Tools\CMake_64` + Ninja `C:\Qt\Tools\Ninja`。

**已知坑**（来自兄弟项目记忆）：`scripts/msvc-env.bat` 必须同时加入 Windows SDK 的
`bin\<ver>\x64`，否则 `rc.exe`/`mt.exe` 缺失导致 CMake try-compile 失败。
该脚本直接复用，不重新踩坑。

**可复用资产**（照搬兄弟项目）：`msvc-env.bat`、`build-core.bat`、`api_test.py`、
`screenshot.py`、`console.py`、`.github/workflows/build.yml`。

**CI 矩阵**：`ubuntu/macos/windows` 三平台构建核心+CLI+测试；
`ubuntu/windows` 额外用 `aqtinstall` 拉 Qt 6.6.2 构建 GUI 并跑离屏冒烟测试。

---

## 5. 目录迁移

现有文件在 `unified` 分支上的处置：

| 现有 | 去向 |
|---|---|
| `number.{h,cpp}` `node.{h,cpp}` | `legacy/qt_gui_original/`（原样保留） |
| `mainwindow.{h,cpp,ui}` `dialog_*.{h,cpp,ui}` | `legacy/qt_gui_original/` |
| `tempnum.{h,cpp}` `main.cpp` `calculator.pro*` | `legacy/qt_gui_original/` |
| `res/` | `legacy/qt_gui_original/res/` |
| `README.md` `README_CN.md` | `legacy/qt_gui_original/`（新的 README 另行撰写） |
| 外部 `OJ_Calculator Console Edition/` | 复制到 `legacy/oj_console_edition/` |
| 外部 `OnlineJudge/Calculator/` | 复制到 `legacy/onlinejudge_calculator/` |

**外部两份命令行版的源码只复制、不删除**，原位置保留不动（它们本就不在 git 内，
删除会不可逆地丢失历史）。

`legacy/` 下每个子目录附 `README.md` 说明其代际、来源与已知缺陷。

---

## 6. 测试与验证

### 6.1 单元测试

覆盖：解析（含前导零、负号、小数点、空串、非法字符）、精确四则、舍入边界
（HalfEven/HalfUp/Down/Up/Ceil/Floor）、除零、比较、进制转换往返、位运算、
表达式解析与错误位置。

### 6.2 Python `decimal` 差分测试

**独立基准**——这是正确性的硬证据。`scripts/difftest.py`：

- 随机用例 + 边界用例（0、1、-1、极大/极小数、除数为 0、超精度）
- 以 Python 标准库 `decimal` 为期望值
- 覆盖 `+ - * / sqrt pow exp ln` 与舍入模式
- 固定随机种子，结果可复现

### 6.3 CI 集成

`difftest.py` 作为 CTest 用例注册（需 Python 3；缺失时跳过而非失败）。

### 6.4 GUI 冒烟测试

`tests/gui_smoke.cpp` 离屏渲染（`QT_QPA_PLATFORM=offscreen`），无需显示器。

### 6.5 端到端

`scripts/api_test.py` 拉起服务 → 调用端点 → 校验数值。
`scripts/screenshot.py`（headless Edge）截图人工核对。

---

## 7. 文档产出

`docs/` 是「研究价值」的主要载体：

- **`architecture.md`** — 分层、数据流、接口边界、如何扩展
- **`bignum-algorithms.md`** — 核心算法研究笔记：base-10⁹ 分块布局、进位/借位、
  长除法推导、Knuth D 对照实现与基准数据、开方（牛顿迭代）、exp/ln（级数）、
  精度与舍入的语义
- **`legacy/COMPARISON.md`** — 三代实现逐项对比（API 命名、算法、缺陷、谱系证据）
- **`legacy/README.md`** — 各代来源、代际判定依据（md5 比对）、已知缺陷清单

顶层 `README.md` / `README_CN.md` 重写：项目定位、快速开始、示例、架构指引、
「研究价值」说明。

---

## 8. 已知缺陷清单（重写理由）

来自三代实现的实际问题，均在重构中消除：

1. **头文件守卫冲突** —— `OJ_Calculator Console Edition` 与 `OnlineJudge/Calculator`
   的 `number.h` 守卫宏完全相同（`NUMBER_H` / `TEST03_NUMBER_H`），两个目录无法同时
   包含进一个编译单元。
2. **`operator-` 递归借位** —— 递归深度与被减数位数成正比，大数场景栈风险与 O(n²) 开销。
3. **`operator/` 试商法** —— 每轮构造临时 `Number` 对象，常数极大。
4. **`copy()` / `release()` 双重释放风险** —— 手写链表生命周期易错。
5. **`deleteZero()` 边界** —— `Len == Len_frac` 时存在越界风险。
6. **全局静态精度** —— `static int Number::FracBit` 隐式影响所有运算。
7. **除法舍入判定** —— `i == -(FracBit+2)` 的边界条件脆弱。
8. **资源路径错误** —— `QIcon("://ICON.jpg")` 写法有误。
9. **`get()` 解析负号后的空串** —— 边界输入未处理。
10. **除法在精度为 0 时崩溃** —— 需同时满足 `FracBit == 0` 且 `start == -2`：
    试商循环一次都不执行，收尾的舍入分支对单节点链表做 `->prior` 回退得到
    `nullptr`，紧随其后的 `result.Tail->next->Data` 解引用空指针，实测 SIGSEGV
    （三代均复现；`legacy/COMPARISON.md` 缺陷 #10 有插桩 trace 与最小复现）。
    与缺陷 #5 同源——都假定链表不止一个节点。
    新设计以 `Context` 显式传参，`precision <= 0` 直接拒绝（实测 `precision >= 1` 即安全）。

---

## 9. 实施顺序

1. 建目录骨架、`CMakeLists.txt`、`.gitignore`、迁移 `legacy/`
2. `src/core/BigDecimal`：表示层 + 精确四则（先不做除法）
3. 除法 + `Context` + 舍入 → 跑 Python 差分测试
4. `sqrt/pow/exp/ln` → 差分测试
5. `Format`：进制转换 + 位运算
6. `Expression`：解析器
7. `src/cli`：三种模式，OJ 模式对齐原协议
8. `src/api` + `src/server` + `web/`
9. `src/ui`：Qt 界面重设计
10. CI + 脚本 + 文档

每步都必须可编译、可测试通过；任一步的差分测试不绿则不进入下一步。
