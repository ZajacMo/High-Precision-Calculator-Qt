# 三代实现逐项对比

对比对象：本仓库 `legacy/` 下的三份归档实现。

- **第 1 代** = `legacy/onlinejudge_calculator/`（2024-02-15，原始命令行版）
- **第 2 代** = `legacy/oj_console_edition/`（2024-03-22 ~ 03-23，由第 3 代反向移植）
- **第 3 代** = `legacy/qt_gui_original/`（2024-02-28 建项，Qt GUI 版）

> **编号按形态而非时间排列**：真实的时间顺序是 第 1 代 → 第 3 代（2024-02-28）→
> 第 2 代（2024-03-22），即**第 2 代反而晚于第 3 代**。这一"倒置"恰好印证了本文档
> 主张的"第 2 代由第 3 代反向移植"——若是 Qt 版的前身，第 2 代应当更早。
> 判定依据见 [`README.md`](README.md#5-谱系结论)。

代际判定依据见 [`README.md`](README.md#代际判定依据)。

---

## 一、总览对比

| 维度 | onlinejudge_calculator（第 1 代） | oj_console_edition（第 2 代） | qt_gui_original（第 3 代） |
|---|---|---|---|
| API 命名 | PascalCase（`Get`/`Show`/`Copy`/`Release`/`Transform_Sign`/`POWER`） | camelCase（`get`/`show`/`copy`/`release`/`transformSign`/`getPower`） | camelCase（同第 2 代） |
| 输入输出 | `std::string` + 流重载 | `std::string` + 流重载 | `QString` |
| 除法 | 试商法（`Gap`/`Times` 临时对象） | 试商法 | 试商法 |
| `node.cpp/h` | 独立 | **与第 3 代逐字节相同**（md5 `45615d3a` / `b67dc507`） | — |
| 谱系判定 | 注释提及「Qt版参数为待转化的字符串QString类型」→ 先于 Qt 版存在 | 由第 3 代反向移植（时间戳晚于第 3 代） | Qt 版的源头 |

### 补充说明（与上表等价的逐项展开）

| 维度 | 第 1 代 | 第 2 代 | 第 3 代 |
|---|---|---|---|
| 数值表示 | 双向链表，每节点一个十进制**位**（0–9） | 同第 1 代 | 同第 1 代 |
| 精度控制 | 文件级全局 `int FracBit = 10;` | 静态成员 `Number::FracBit`（定义于 `main.cpp`） | 静态成员 `Number::FracBit`（定义于 `mainwindow.cpp`，可由对话框动态设置） |
| 复制构造 | **无**（靠 `Copy()` 成员函数） | 有 `Number(const Number&)` | 有 `Number(const Number&)` |
| 析构函数 | **无**（靠 `Release()` 成员函数） | 有 `~Number()` | 有 `~Number()` |
| 赋值运算符 | **无** | 有 `operator=` | 有 `operator=` |
| 乘除法 | `operator*` / `operator/` | 同 | 同 |
| 流重载 | **无** | `operator<<` / `operator>>` | **无**（改用 `showQstr()`） |
| 入口 | `main.cpp`（OJ 协议：先读用例数） | `main.cpp`（OJ 协议，同） | `mainwindow.cpp` + `main.cpp`（QApplication 事件循环） |
| 构建方式 | 无 `CMakeLists.txt` | 有 `CMakeLists.txt` | `calculator.pro`（qmake） |
| 附加文件 | — | — | `dialog_help` / `dialog_setbit` / `dialog_tip` 三个对话框、`res/` 资源、`README.md` / `README_CN.md` |

### 谱系证据（md5）

```
node.cpp   45615d3a6d17b734ecd9ef8619275ea0   oj_console_edition   ← 相同
node.cpp   45615d3a6d17b734ecd9ef8619275ea0   qt_gui_original
node.cpp   df11d0c221a555bb570cb683196aafa8   onlinejudge_calculator ← 不同

node.h     b67dc5073df0c5ce0882549955c04c69   oj_console_edition   ← 相同
node.h     b67dc5073df0c5ce0882549955c04c69   qt_gui_original
node.h     fc2f0b612c06ba5a1db429a5419b5e5a   onlinejudge_calculator ← 不同
```

第 2 代与第 3 代在 `number.cpp` 上的差异共 **5 处**（`diff -u` 得到 5 个 hunk），
全部是 Qt 适配：

1. `number.cpp` 顶部新增 `#include <string>`（第 2 代只在 `number.h` 里包含 `<string>`，
   第 3 代的 `number.h` 不再包含它，改在 .cpp 中自带）；
2. `get(STR str)` 改名为 `get(STR qstr)`，并在函数体开头新增
   `std::string str = qstr.toStdString();` 与 `release();`（Qt 版需要先转换类型，
   并在重建链表前释放旧数据）；
3. `show()` 改名为 `showQstr()`（返回类型从 `std::string` 变为 `QString`）；
4. `transformSign` 的第三参数类型由 `char&` 改为 `QChar&`；
5. 删除两个流重载函数 `operator<<` 与 `operator>>`（Qt 版改用 `showQstr()` 输出）。

### 命名演化对照

| 第 1 代 | 第 2 代 | 第 3 代 |
|---|---|---|
| `Get` / `Show` | `get` / `show` | `get` / `showQstr` |
| `Copy` / `Release` | `copy` / `release` | `copy` / `release` |
| `Transform_Sign` | `transformSign` | `transformSign` |
| `POWER` | `getPower` | `getPower` |
| `Check_Zero` / `Check_Bit` | `checkZero` / `checkBit` | `checkZero` / `checkBit` |
| `Move_Backwards` | `moveBackwards` | `moveBackwards` |
| `Delete_zero` | `deleteZero` | `deleteZero` |
| `Complement`（友元） | `complement`（私有静态） | `complement`（私有静态） |
| `Carry` | `carry` | `carry` |
| — | `reset` | `reset` |

---

## 二、已知缺陷清单（重写理由）

以下 9 条来自设计规格 §8，均已对照源码核实。v3.0 重写中逐一消除。

### 1. 头文件守卫冲突

| 目录 | `number.h` 守卫 | `node.h` 守卫 |
|---|---|---|
| `onlinejudge_calculator/` | `TEST03_NUMBER_H` | `TEST03_NODE_H` |
| `oj_console_edition/` | `NUMBER_H` | `NODE_H` |
| `qt_gui_original/` | `NUMBER_H` | `NODE_H` |

第 2、3 代守卫宏**完全相同**，两个目录无法同时包含进一个编译单元。

> v3.0：`src/core/` 下的头文件使用 `hpc::` 命名空间 + 唯一守卫，天然无此问题。

### 2. `operator-` 递归借位

第 3 代 `number.cpp:113`：

```cpp
num1 = N1 - N2;    // 递归调用减法
```

减法在结果为负时递归调用自身。递归深度与被减数位数成正比，大数场景有**栈溢出**风险，
且整体复杂度为 **O(n²)**。

> v3.0：`BigDecimal` 用 base-10⁹ 分块的 `std::vector` 表示，借位为迭代实现，无递归。

### 3. `operator/` 试商法

第 3 代 `number.cpp:26`：

```cpp
Number Gap((*this) - getPower(i) * Num2);   // 做差法比较尝试值与被除数的大小
```

每轮试商构造临时 `Number` 对象（并触发一次完整减法）。第 1 代更甚，每次试商
`new Number(j)` 并 `Copy` 三次临时对象。

> v3.0：`BigDecimal` 的除法为 base-10⁹ 长除法，无临时对象构造。

### 4. `copy()` / `release()` 双重释放风险

第 3 代 `number.cpp:246-276`：`copy()` 先 `release()` 再 `reset()` 重建链表，
`release()` 遍历链表逐个 `delete`。手写链表生命周期完全靠 `reset()` 手工维护
`Head`/`Tail`/`Unit` 三个指针的一致性，一旦顺序出错即 double free。

> v3.0：改用值语义 + `std::vector`，拷贝/移动由编译器生成，无手写生命周期。

### 5. `deleteZero()` 边界

第 3 代 `number.cpp:174-188`：

```cpp
while(Head!=nullptr && Head->Data == 0 && Len-Len_frac>1) {  // 循环一
    Head = Head->next;
    delete Head->prior;
    Head->prior = nullptr;
    Len--;
}
while(Tail!=nullptr && Tail->Data == 0 && Len_frac>0) {      // 循环二
    Tail = Tail->prior;
    delete Tail->next;
    Tail->next = nullptr;
    Len_frac--;
    Len--;
}
```

**循环一不可达**：其守卫是 `Len-Len_frac>1`，而 `Len == Len_frac` 时该式为 0，
根本不满足条件，循环体不会执行。因此"当 `Len == Len_frac` 时 `Head->next` 越界"
的旧描述有误。

**可达的空指针解引用在循环二**：当循环二的条件（`Tail->Data == 0 && Len_frac>0`）
成立时，`Tail` 可能恰好是链表的**首节点**（`Tail->prior == nullptr`），此时
`Tail = Tail->prior;` 把它置为 `nullptr`，紧接着的 `delete Tail->next` 即解引用空指针。

实测（第 3 代，`Number::FracBit = 10`）：输入 `0` 与 `0.11` 相乘即崩溃 ——
乘法结果 `0` 的链表只剩一个节点且 `Len_frac = 1`，`deleteZero()` 的循环二把 `Tail`
一路退到首节点之外（`nullptr`）后再解引用。

插桩 fuzz 的可达性统计（对 `deleteZero()` 两个循环分别打点，故循环一的可达性
也已一并验证）：

| 代际 | 随机用例数 | 循环一可达 | 循环二可达（空指针解引用） |
|---|---:|:---:|:---:|
| 第 1 代（`Delete_zero`） | 150 万 | 未观察到 | 观察到（19196 次） |
| 第 2 代 | 240 万 | 未观察到 | 观察到（30722 次） |
| 第 3 代 | 300 万 | 未观察到 | 观察到（38738 次） |

因此这是**三代共有的同一处缺陷**，而非仅第 3 代独有；且循环一在三代的这批用例中
均未出现可达的越界/空指针路径。

> v3.0：表示层规范化时统一处理前导/末尾零，不存在原地修改链表的操作。

### 6. 全局静态精度

第 1 代是文件级全局 `int FracBit = 10;`（`number.cpp:2`），第 2、3 代是静态成员
`Number::FracBit`。它**隐式影响所有运算**，同一进程内无法让两次不同精度的运算并存。

> v3.0：精度下沉为 `Context` 显式参数，按调用传递。

### 7. 除法舍入判定

第 3 代 `number.cpp:43`（第 1 代 `number.cpp:52` 同）：

```cpp
if (i == -(FracBit+2)) {                    // 若精度超过预设值
    result.Tail = result.Tail->prior;
    if (result.Tail->next->Data >= 5)       // 四舍五入
        result.Tail->Data++;
    ...
}
```

依赖循环变量 `i` 恰好落在 `-(FracBit+2)` 这一**特定值**上判定"精度已达标"，
边界条件脆弱：一旦 `FracBit` 与循环区间不匹配，舍入就会漏做或多做。

> v3.0：`Context` 中显式枚举舍入模式（HalfEven/HalfUp/Down/Up/Ceil/Floor），
> 舍入在统一的收尾步骤执行。

### 8. 资源路径错误

第 3 代四个文件均为：

```cpp
setWindowIcon(QIcon("://ICON.jpg"));    // dialog_help.cpp:10
                                      // dialog_setbit.cpp:9
                                      // dialog_tip.cpp:10
                                      // mainwindow.cpp:14
```

缺少 `qrc:` 前缀（应为 `":/ICON.jpg"` 或 `"qrc:/ICON.jpg"`），图标无法从
`res/res.qrc` 加载。

> v3.0：Qt 界面为可选目标（`BUILD_GUI`，默认 OFF），资源路径届时一并修正。

### 9. `get()` 解析负号后的空串

第 3 代 `number.cpp:142-147`：

```cpp
void Number::get(STR qstr){
    std::string str = qstr.toStdString();
    release();
    Unit = Tail = Head = new Node;
    if (str[0] == '-') {        // 注意：此处 str[0] 的求值发生在 erase 之前
        isNegative = true;
        str.erase(0, 1);
    }
```

**真正的缺陷不是越界访问，而是静默接受**。`str[0]` 的求值发生在 `erase` **之前**，
此时 `str` 仍非空，因此这一步并不越界；`erase(0,1)` 之后 `str` 变为空串，但代码
**从不再次索引 `str[0]`** —— 后续走的是 `str.find('.')`（返回 `npos`）与
`str.size()`（返回 0），二者对空串都是良定义的。

自 C++11 起 `std::string::operator[](size())` 返回 `'\0'`（而非 UB），因此整个过程
**不会崩溃**。实测（第 3 代与第 1 代一致）：

```
输入 "-"   → 结果 "0"     // 静默当作 0 接受
输入 ""    → 结果 "0"
输入 "-0"  → 结果 "0"
```

真正的危害是**语义错误**：一个非法的输入被静默转换成了合法的 0，用户无从察觉，
且 `"-"` 与真正的 `"0"` 在下游完全无法区分。第 1 代 `Get()`（`number.cpp:169`）有
完全相同的问题。

> v3.0：解析前做完整校验（空串、非法字符、仅符号等），返回明确的错误而非 UB。

### 10. 除法在精度为 0 且 `start == -2` 时崩溃（可复现的空指针解引用）

第 2 代 `number.cpp:43-45`（第 1 代 `:53-55`、第 3 代 `:44-46` 同构）：

```cpp
if (i == -(FracBit+2)) {                    // 若精度超过预设值
    result.Tail = result.Tail->prior;        // ← 退到链表首节点之外，得到 nullptr
    if (result.Tail->next->Data >= 5)       // ← 崩溃点：解引用 nullptr->next
        result.Tail->Data++;
```

**触发条件比"精度为 0"更窄**：必须同时满足 `FracBit == 0` **且**
`start == -(FracBit+2)`（即 `start == -2`）。

成因链条（由插桩 trace 逐步确认）：

1. `FracBit == 0` 时，试商循环的边界退化为 `for( i = start ; i >= -1 ; i-- )`。
2. 若 `start == -2`，该条件**一开始就为假** —— 循环体**一次都没有执行**，
   `i` 停在 `-2`，`result` 仍是刚构造的单节点 `Number(0)`，其 `Tail == Head`。
3. 循环后的 `if (i == -(FracBit+2))` 恰好命中（`-2 == -2`），进入舍入分支。
4. `result.Tail = result.Tail->prior` —— 单节点链表的首节点 `prior` 为 `nullptr`，
   于是 `result.Tail` 被置为 `nullptr`。
5. **紧随其后的** `result.Tail->next->Data` 解引用空指针 → SIGSEGV。
   （`result.Tail->Data++` 那一行根本执行不到。）

插桩 trace（实测输出，第 2 代，`FracBit = 0`，输入 `-02.881332` ÷ `0474`）：

```
[div] Len=7 Len_frac=6 Num2.Len=3 start=-2 bound=-1
[div] after loop: i=-2 rounding-branch=1 tail==head=1
[div] rounding branch entered; result.Tail->prior=0000000000000000
[div] after walk: result.Tail=0000000000000000
```

注意 trace 中**没有** "loop body ran" 一行 —— 印证了「循环体从未执行」。

实测崩溃（三代均复现，退出码 139 = SIGSEGV）：

```
第 1 代：exit 139   （已用 Gen-1 源码独立复现）
第 2 代：exit 139
```

**触发范围确实很窄**：同样在 `FracBit = 0` 下，`1/2`、`1/3`、`100/7`、`1/1000`、
`12.34/56`、`5/2`、`10/4` 全部正常退出（exit 0），只有 `start == -2` 的输入崩溃
（如 `1.5/474`、`2.5/474`）。用 `FracBit = 1,2,3,4` 重跑同样的输入也都不崩。

**与缺陷 #5 同源**：两者都是「`->prior` 向前走时假定链表不止一个节点」——
#5 是 `deleteZero()` 的循环二，#10 是除法收尾的舍入分支。#5 的修复使
`result` 退化为单节点，#10 随后对这个单节点链表做了同样的错误假设。

**GUI 可达性**：`FracBit` 是全局静态成员，而「设置精度」对话框只校验 `bit < 0`
（`dialog_setbit.cpp:20`），`mainwindow.cpp:94-95` 又在 `frac_bit >= 0` 时直接写入
`Number::FracBit`，所以 0 确实会被放行。

> v3.0：精度通过 `Context` 显式传入，且 `precision <= 0` 时直接拒绝
> （抛 `std::invalid_argument`），从根上消除「精度为 0」这一非法状态。
> 实测 `FracBit = 1` 起即安全，故 `precision >= 1` 是正确的下界。

---

## 三、三代共享的算法缺陷小结

| 缺陷 | 第 1 代 | 第 2 代 | 第 3 代 |
|---|:---:|:---:|:---:|
| 1 头文件守卫冲突 | 独立守卫名 | 与第 3 代冲突 | 与第 2 代冲突 |
| 2 `operator-` 递归借位 | ✓ | ✓ | ✓ |
| 3 `operator/` 试商法 | ✓ | ✓ | ✓ |
| 4 `copy()`/`release()` 双重释放风险 | ✓（无拷贝构造，更原始） | ✓ | ✓ |
| 5 `deleteZero()` 边界 | ✓（`Delete_zero`，空指针解引用） | ✓（空指针解引用） | ✓（空指针解引用） |
| 6 全局静态精度 | ✓（文件级全局变量） | ✓ | ✓ |
| 7 除法舍入判定 | ✓ | ✓ | ✓ |
| 8 资源路径错误 | 不适用 | 不适用 | ✓ |
| 9 `get()` 解析负号后的空串 | ✓（`Get`，静默接受为 0） | ✓（静默接受为 0） | ✓（静默接受为 0） |
| 10 除法在 `FracBit = 0` 且 `start == -2` 时崩溃 | ✓（实测 SIGSEGV） | ✓（实测 SIGSEGV） | ✓ |

三代的核心数值算法是**同一份代码的三次复制**，缺陷也随复制传播 —— 这正是 v3.0
重写的根本理由：三代积累下来的不是能力，而是同一批 bug 的三个副本。
