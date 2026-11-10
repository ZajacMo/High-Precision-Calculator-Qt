# legacy/ —— 三代原始实现归档

本目录保存同一个高精度计算器的**三代原始实现**，仅作历史参考与算法对照之用。
v3.0 的新代码不引用、不依赖这里的任何代码；本目录中的文件按**原始字节**归档，
不做重构、不做格式化、不做"改良"。

---

## 三个子目录

| 子目录 | 代际 | 来源 | 归档方式 |
|---|---|---|---|
| `onlinejudge_calculator/` | 第 1 代（最古老） | `F:\Projects\01_courses\cpp\VSC_C++Project\OnlineJudge\Calculator\` | 复制（原位置保留不动） |
| `oj_console_edition/` | 第 2 代（Console 形态） | `F:\Projects\01_courses\cpp\CLion\C++Programming\OJ_Calculator Console Edition\` | 复制（原位置保留不动） |
| `qt_gui_original/` | 第 3 代（Qt GUI 形态） | 本仓库原根目录 | `git mv`（保留 git 历史） |

> 代际编号按**形态**（Console / Qt GUI）而非时间排列，真实时间顺序见下文
> [5. 谱系结论](#5-谱系结论)。

外部两份命令行版的源码**只复制、不删除**：它们原本就不在 git 内，删除会造成不可逆的
历史丢失。第 2 代复制时排除了四类内容：`cmake-build-debug/` 与 `.idea/`（构建产物与
IDE 配置，无归档价值且体积巨大）、`.git/` 与 `.gitignore`。

其中 `.git/` 是**有意排除**的：第 2 代本身就是一个独立的 git 仓库（`git log` 只有
一条提交 `e408eb8 "chore: track the project files"`）。把一个 `.git` 目录嵌套进另一个
仓库，会让 git 把归档副本当成子模块 / 嵌套仓库对待，是明确的隐患；因此归档副本只
保留源码快照，不携带其提交历史。

---

## 代际判定依据

### 1. md5 比对

对三代各自的 `node.{h,cpp}` 与 `number.{h,cpp}` 计算 md5：

```
45615d3a6d17b734ecd9ef8619275ea0  oj_console_edition/node.cpp
45615d3a6d17b734ecd9ef8619275ea0  qt_gui_original/node.cpp      ← 逐字节相同
b67dc5073df0c5ce0882549955c04c69  oj_console_edition/node.h
b67dc5073df0c5ce0882549955c04c69  qt_gui_original/node.h         ← 逐字节相同

df11d0c221a555bb570cb683196aafa8  onlinejudge_calculator/node.cpp   ← 不同
fc2f0b612c06ba5a1db429a5419b5e5a  onlinejudge_calculator/node.h      ← 不同
```

**第 2 代与第 3 代的 `node.{h,cpp}` 逐字节相同**，而第 1 代不同 —— 这说明第 2 代是
从第 3 代**反向移植**回去的（保留了第 3 代已经完成的 `node` 重构：默认值初始化、
`explicit` 构造等），而不是独立演化的产物。

### 2. 注释中的自述（决定性证据）

第 1 代 `number.h` 中：

```cpp
void Get(std::string);   //获取输入的数据，Qt版参数为待转化的字符串QString类型
std::string Show();      //输出数据，Qt版返回值为QString类型
```

第 1 代的作者在注释里明确提到"Qt 版"的存在，并说明自己这一版**尚待转化为 QString
类型** —— 这直接证明第 1 代**早于** Qt 版存在。

### 3. API 命名演化

```
第 1 代（PascalCase）        第 2 代（camelCase）              第 3 代（camelCase）
Get / Show                   get / show                        get / showQstr
Copy / Release               copy / release                    copy / release
Transform_Sign               transformSign                     transformSign
POWER                        getPower                          getPower
Check_Zero / Check_Bit       checkZero / checkBit              checkZero / checkBit
```

命名从 PascalCase 改为 camelCase 是 C++ 社区惯例的一次性重命名，第 2 代与第 3 代
共用同一套 camelCase 名字，进一步印证两者的同源关系。

### 4. 输入输出类型

| 代际 | 类型 |
|---|---|
| 第 1 代 | `std::string` + `Get`/`Show` 显式调用 |
| 第 2 代 | `std::string`（`typedef std::string STR`）+ `operator<<` / `operator>>` 流重载 |
| 第 3 代 | `QString`（`typedef QString STR`），`get()` 内做 `toStdString()` 转换 |

### 5. 谱系结论

```
第 3 代 (Qt GUI, 2024-02-28 建项)
      │
      │  node.{h,cpp} 逐字节保留 + camelCase 命名 + QString 化
      ▼
第 2 代 (Console Edition, 2024-03-22 ~ 03-23，反向移植)
      │
      │  独立的 node 重构 + PascalCase 命名 + 注释提及"Qt版"
      ▼
第 1 代 (OnlineJudge, 2024-02-15，最早)
```

**代际顺序不是按"第 N 代"这个编号递增的**。真实的先后顺序是
**第 1 代（2024-02-15）→ 第 3 代（2024-02-28 建项）→ 第 2 代（2024-03-22 起）**：

- 第 1 代全部文件的时间戳为 **2024-02-15**（`main.cpp` / `node.*` / `number.*`）；
- 第 3 代**建项于 2024-02-28** —— `calculator.user` 头部写明
  `<!-- Written by QtCreator 12.0.2, 2024-02-28T21:05:38. -->`，
  其界面与资源更早：`res/res.qrc`、`res/ICON.jpg`（2024-02-08）、
  `dialog_*.ui`（2024-02-11）；
- 第 2 代全部源码的时间戳落在 **2024-03-22 ~ 03-23**
  （`main.cpp` 03-22 09:31、`number.cpp` 03-22 15:06、`node.cpp` 03-22 15:12、
  `node.h` 03-23 18:45、`number.h` 03-23 21:52）。

需要说明的是：第 3 代的**数值核心文件**（`node.*`、`number.*`）本身带有
2024-03-22 / 03-23 的时间戳 —— 那是反向移植的那次会话同时改动了两侧副本所致
（第 2、3 代的 `node.cpp` 逐字节相同、时间戳仅相隔 48 秒：
15:12:44 与 15:13:32）。因此判定第 3 代的建项时间应以其**不会随移植改动的 Qt 工程
脚手架**为准（`calculator.user` 2024-02-28、`.ui` 2024-02-11、`res/` 2024-02-08），
这些文件在第 2 代出现前一个月就已存在。

也就是说，**第 2 代晚于第 3 代**，本归档中"第 2 代 / 第 3 代"的编号是按*形态*
（Console / Qt GUI）而非时间排列的。编号沿用归档目录的既有称谓，以免打乱既有引用。

这一点**强化**了"第 2 代由第 3 代反向移植"的结论：若第 2 代是 Qt 版的**前身**，
它的时间戳应当**早于** Qt 版；实测恰好相反 —— 第 2 代比第 3 代的 Qt 工程脚手架
晚了近一个月，只能解释为在 Qt 版完成之后才被移植回命令行形态。第 1 代 `number.h`
注释中"Qt版参数为待转化的字符串QString类型"的自述，与这一时间线互相印证。

---

## 已知缺陷

三代共有、或各自独有的问题，完整清单见 [`COMPARISON.md`](COMPARISON.md)。
以下是要点：

1. **头文件守卫冲突** —— 第 1 代用 `TEST03_NUMBER_H` / `TEST03_NODE_H`，第 2、3 代
   用 `NUMBER_H` / `NODE_H`。两代目录**无法同时包含进一个编译单元**，必须分目录构建。
2. **`operator-` 递归借位** —— 减法在结果为负时递归调用自身（`num1 = N1 - N2;`），
   递归深度与被减数位数成正比，大数场景有栈溢出风险，且整体为 O(n²)。
3. **`operator/` 试商法** —— 每轮试商构造临时 `Number` 对象（第 1 代更每次分配
   `new Number(j)`），常数极大。
4. **`copy()` / `release()` 双重释放风险** —— 手写链表生命周期，靠 `reset()` 手工
   维护状态，拷贝与释放的顺序一旦出错就会 double free。
5. **`deleteZero()` 边界** —— 第二个循环中 `Tail = Tail->prior;` 可能让 `Tail` 变为
   `nullptr`，随后的 `delete Tail->next` 即为空指针解引用。
6. **全局静态精度** —— `Number::FracBit` 是静态成员（第 1 代是文件级
   `int FracBit = 10;`），隐式影响所有运算，无法在同一进程内做多精度并存。
7. **除法舍入判定** —— `if (i == -(FracBit+2))` 的边界条件脆弱。
8. **资源路径错误** —— `QIcon("://ICON.jpg")` 缺少 `qrc:` 前缀，图标无法加载。
9. **`get()` 解析负号后的空串** —— 输入 `"-"` 时被**静默接受为 0**，而不是报错。
10. **除法在精度为 0 时崩溃** —— `FracBit == 0` 且 `start == -2` 时，试商循环一次都不执行，
    舍入分支对单节点链表做 `->prior` 回退得到 `nullptr`，再解引用即 SIGSEGV。
    与缺陷 #5 同源（都假定链表不止一个节点）。

上述缺陷在 v3.0 重写中逐一消除：改用 base-10⁹ 分块的 `BigDecimal`（无链表、无指针、
无手动内存管理）、精度下沉为 `Context` 显式参数、解析前置校验。

---

## 附注

- 本目录下的代码**无法用当前的 CMake 构建**：`qt_gui_original/` 依赖 Qt 6（`QString`、
  `QMainWindow`），`onlinejudge_calculator/` 与 `oj_console_edition/` 的头文件守卫互相
  冲突，且第 1 代缺少 `CMakeLists.txt`。它们的价值在于**阅读与对照**，而非参与构建。
- `qt_gui_original/` 保留了原 `README.md` / `README_CN.md`，描述的是 Qt 版的使用方式。
  v3.0 的顶层 README 将另行撰写（Task 10）。
