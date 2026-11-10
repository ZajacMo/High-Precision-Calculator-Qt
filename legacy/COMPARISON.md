# 三代实现逐项对比

对比对象：本仓库 `legacy/` 下的三份归档实现。

- **第 1 代** = `legacy/onlinejudge_calculator/`（2024-02，原始命令行版）
- **第 2 代** = `legacy/oj_console_edition/`（2024-03，由第 3 代反向移植）
- **第 3 代** = `legacy/qt_gui_original/`（2024-03，Qt GUI 版）

代际判定依据见 [`README.md`](README.md#代际判定依据)。

---

## 一、总览对比

| 维度 | onlinejudge_calculator（第 1 代） | oj_console_edition（第 2 代） | qt_gui_original（第 3 代） |
|---|---|---|---|
| API 命名 | PascalCase（`Get`/`Show`/`Copy`/`Release`/`Transform_Sign`/`POWER`） | camelCase（`get`/`show`/`copy`/`release`/`transformSign`/`getPower`） | camelCase（同第 2 代） |
| 输入输出 | `std::string` + 流重载 | `std::string` + 流重载 | `QString` |
| 除法 | 试商法（`Gap`/`Times` 临时对象） | 试商法 | 试商法 |
| `node.cpp/h` | 独立 | **与第 3 代逐字节相同**（md5 `45615d3a` / `b67dc507`） | — |
| 谱系判定 | 注释提及「Qt版参数为待转化的字符串QString类型」→ 先于 Qt 版存在 | 由第 3 代反向移植 | 最新 |

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

第 2 代与第 3 代在 `number.cpp` 上的差异**仅有 4 处**，全部是 Qt 适配：
`#include <string>`、`get(STR qstr)` 内的 `toStdString()` 转换、`show()` → `showQstr()`、
`transformSign` 的 `char` → `QChar`，以及删掉两个流重载函数。

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
while(Head!=nullptr && Head->Data == 0 && Len-Len_frac>1) {
    Head = Head->next;
    delete Head->prior;      // Head 可能已是 nullptr
    Head->prior = nullptr;
    Len--;
}
```

当 `Len == Len_frac` 时，`Head->next` 可能越界（`Head` 已被置空后再取 `->next`）。

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
    if (str[0] == '-') {        // 空串时 str[0] 越界
        isNegative = true;
        str.erase(0, 1);
    }
```

输入 `"-"` 时，`erase(0,1)` 后 `str` 变为空串，但 `str[0]` 在**进入 if 之前**已被求值；
而 `"-"` 之后的空串在后续 `str.find('.')` / `str.size()` 路径上同样缺乏边界处理。
第 1 代 `Get()`（`number.cpp:169`）有完全相同的问题。

> v3.0：解析前做完整校验（空串、非法字符、仅符号等），返回明确的错误而非 UB。

---

## 三、三代共享的算法缺陷小结

| 缺陷 | 第 1 代 | 第 2 代 | 第 3 代 |
|---|:---:|:---:|:---:|
| 1 头文件守卫冲突 | 独立守卫名 | 与第 3 代冲突 | 与第 2 代冲突 |
| 2 `operator-` 递归借位 | ✓ | ✓ | ✓ |
| 3 `operator/` 试商法 | ✓ | ✓ | ✓ |
| 4 `copy()`/`release()` 双重释放风险 | ✓（无拷贝构造，更原始） | ✓ | ✓ |
| 5 `deleteZero()` 边界 | ✓（`Delete_zero`） | ✓ | ✓ |
| 6 全局静态精度 | ✓（文件级全局变量） | ✓ | ✓ |
| 7 除法舍入判定 | ✓ | ✓ | ✓ |
| 8 资源路径错误 | 不适用 | 不适用 | ✓ |
| 9 `get()` 解析负号后的空串 | ✓（`Get`） | ✓ | ✓ |

三代的核心数值算法是**同一份代码的三次复制**，缺陷也随复制传播 —— 这正是 v3.0
重写的根本理由：三代积累下来的不是能力，而是同一批 bug 的三个副本。
