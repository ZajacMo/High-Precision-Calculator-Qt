# Qt高精度计算器 / Qt High-Precision Calculator

## 项目简介 / Project Introduction
这是一个基于Qt框架开发的**高精度**计算器应用程序，支持基本的四则运算（加、减、乘、除），并提供可配置的小数位数设置功能。该计算器使用自定义的高精度数值类实现精确计算，**避免了浮点数计算中的精度误差问题**。

This is a **high-precision** calculator application developed based on the Qt framework, supporting basic arithmetic operations (addition, subtraction, multiplication, division), and providing configurable decimal places settings. The calculator uses a custom high-precision numeric class to achieve accurate calculations, **avoiding precision error issues in floating-point calculations**.

<div align=center><img src="res/fig1.png" width=70% style="margin:auto"/></div>

## 功能特性 / Features

### 基本计算功能 / Basic Calculation Features
- 支持加（+）、减（-）、乘（*）、除（/）四则运算
- Support for four arithmetic operations: addition (+), subtraction (-), multiplication (*), division (/)
- 支持小数点输入
- Support for decimal point input
- 支持负数运算
- Support for negative number operations
- 支持连续计算
- Support for continuous calculations
- 高精度计算，避免浮点数精度误差
- High-precision calculation to avoid floating-point precision errors

### 用户界面功能 / User Interface Features
- 直观的图形用户界面
- Intuitive graphical user interface
- 清晰的计算过程显示
- Clear calculation process display
- 键盘快捷键支持
- Keyboard shortcut support
- 右键菜单操作
- Right-click menu operation
- 状态栏信息提示
- Status bar information prompts

### 配置功能 / Configuration Features
- 可自定义设置计算结果的小数位数
- Customizable decimal places for calculation results
- 支持重置和清除操作
- Support for reset and clear operations

### 帮助功能 / Help Features
- 提供操作指南对话框
- Provide operation guide dialog box
- 包含关于软件的信息对话框
- Include software information dialog box

## 技术栈 / Technology Stack
- C++
- Qt框架（Qt Widgets）/ Qt Framework (Qt Widgets)
- 自定义数据结构实现高精度计算 / Custom data structure for high-precision calculation

## 项目结构 / Project Structure

```
calculator/
├── calculator.pro       # Qt项目配置文件 / Qt project configuration file
├── calculator.pro.user  # 用户项目配置 / User project configuration
├── main.cpp             # 程序入口文件 / Program entry file
├── mainwindow.cpp       # 主窗口实现 / Main window implementation
├── mainwindow.h         # 主窗口头文件 / Main window header file
├── mainwindow.ui        # 主窗口UI设计 / Main window UI design
├── node.cpp             # 链表节点实现 / Linked list node implementation
├── node.h               # 链表节点头文件 / Linked list node header file
├── number.cpp           # 高精度数值类实现 / High-precision numeric class implementation
├── number.h             # 高精度数值类头文件 / High-precision numeric class header file
├── tempnum.cpp          # 临时数据管理实现 / Temporary data management implementation
├── tempnum.h            # 临时数据管理头文件 / Temporary data management header file
├── dialog_setbit.cpp    # 小数位数设置对话框实现 / Decimal places setting dialog implementation
├── dialog_setbit.h      # 小数位数设置对话框头文件 / Decimal places setting dialog header file
├── dialog_setbit.ui     # 小数位数设置对话框UI设计 / Decimal places setting dialog UI design
├── dialog_help.cpp      # 关于对话框实现 / About dialog implementation
├── dialog_help.h        # 关于对话框头文件 / About dialog header file
├── dialog_help.ui       # 关于对话框UI设计 / About dialog UI design
├── dialog_tip.cpp       # 帮助提示对话框实现 / Help tip dialog implementation
├── dialog_tip.h         # 帮助提示对话框头文件 / Help tip dialog header file
├── dialog_tip.ui        # 帮助提示对话框UI设计 / Help tip dialog UI design
└── res/                 # 资源文件夹 / Resource folder
    ├── ICON.jpg         # 应用程序图标 / Application icon
    └── res.qrc          # 资源配置文件 / Resource configuration file
```

## 核心类说明 / Core Class Description

### MainWindow 类 / MainWindow Class
主窗口类，负责处理用户界面交互和计算逻辑调度。
Main window class responsible for handling user interface interaction and calculation logic scheduling.
- 管理按钮点击事件和键盘快捷键
- Manage button click events and keyboard shortcuts
- 协调数值输入和计算操作
- Coordinate numeric input and calculation operations
- 显示计算过程和结果
- Display calculation process and results
- 管理对话框和菜单操作
- Manage dialog boxes and menu operations

### Number 类 / Number Class
高精度数值类，使用双向链表实现高精度数值的存储和计算。
High-precision numeric class that uses a doubly linked list to implement high-precision numeric storage and calculation.
- 支持四则运算操作符重载
- Support for arithmetic operation operator overloading
- 提供高精度数值的输入输出功能
- Provide input/output functionality for high-precision numbers
- 自动处理进位和小数位
- Automatically handle carry and decimal places
- 支持自定义小数位数
- Support for custom decimal places

### Node 类 / Node Class
链表节点类，构成Number类的基本数据单元。
Linked list node class that forms the basic data unit of the Number class.
- 存储单个数字位
- Store single digit
- 支持节点的插入和遍历
- Support node insertion and traversal
- 维护前后节点的引用
- Maintain references to previous and next nodes

### TempNum 类 / TempNum Class
临时数据管理类，保存计算过程中的中间数据。
Temporary data management class that saves intermediate data during calculation.
- 存储输入的数值字符串
- Store input numeric string
- 跟踪计算模式和状态
- Track calculation mode and status
- 管理操作数和运算符
- Manage operands and operators

## 安装和使用 / Installation and Usage

### 前提条件 / Prerequisites
- Qt开发环境（推荐Qt 5.x或Qt 6.x）
- Qt development environment (Qt 5.x or Qt 6.x recommended)
- C++编译器（支持C++17标准）
- C++ compiler (supporting C++17 standard)

### 编译和运行 / Compilation and Execution
1. 使用Qt Creator打开项目文件 `calculator.pro`
1. Open the project file `calculator.pro` with Qt Creator
2. 构建项目（Ctrl+B）
2. Build the project (Ctrl+B)
3. 运行程序（Ctrl+R）
3. Run the program (Ctrl+R)

### 使用说明 / Usage Instructions
1. **基本计算**：点击数字按钮输入数值，点击运算符按钮选择运算类型，点击等号按钮获取结果
1. **Basic Calculation**: Click on the number buttons to input values, click on the operator buttons to select the operation type, and click on the equals button to get the result
2. **小数位数设置**：通过菜单栏选择"修改计算精度"，在弹出的对话框中输入所需的小数位数
2. **Decimal Places Setting**: Select "Modify Calculation Precision" from the menu bar, and enter the desired number of decimal places in the pop-up dialog box
3. **快捷键支持**：
3. **Keyboard Shortcuts**: 
   - 数字键 0-9：输入对应数字
   - Numeric keys 0-9: Input corresponding numbers
   - 运算符键 +, -, *, /：选择对应运算
   - Operator keys +, -, *, /: Select corresponding operations
   - Enter键：计算结果
   - Enter key: Calculate results
   - Backspace键：删除最后一个输入字符
   - Backspace key: Delete the last input character
   - Delete键：清除当前输入或所有数据
   - Delete key: Clear current input or all data
4. **帮助功能**：通过菜单栏查看操作指南和关于信息
4. **Help Functions**: View operation guides and about information through the menu bar

## 高精度计算原理 / High-Precision Calculation Principle

该计算器使用双向链表实现高精度计算，每个节点存储一个数字位。这种设计避免了浮点数计算中的精度问题，特别适用于需要精确结果的金融计算或科学计算场景。
This calculator uses a doubly linked list to implement high-precision calculations, with each node storing a single digit. This design avoids precision issues in floating-point calculations and is particularly suitable for financial or scientific calculation scenarios that require precise results.

高精度计算的核心步骤：
The core steps of high-precision calculation:
1. 将输入的数字字符串转换为链表存储结构
1. Convert the input numeric string to a linked list storage structure
2. 按照数学运算规则实现四则运算
2. Implement four arithmetic operations according to mathematical operation rules
3. 处理进位、借位和小数点位置
3. Handle carry, borrow, and decimal point positions
4. 根据设置的小数位数格式化输出结果
4. Format the output result according to the set number of decimal places

## 注意事项 / Notes
1. 除法运算中，结果会根据设置的小数位数进行四舍五入
1. In division operations, results will be rounded according to the set number of decimal places
2. 连续计算时，前一次计算的结果会自动作为下一次计算的第一个操作数
2. During continuous calculations, the result of the previous calculation will automatically become the first operand of the next calculation
3. 输入负数时，直接在空输入状态下按减号键
3. To input a negative number, directly press the minus key in an empty input state
4. 当小数位数设置为0时，所有计算结果都将取整
4. When the number of decimal places is set to 0, all calculation results will be rounded to integers

## 开发说明 / Development Instructions

### 代码规范 / Code Standards
- 遵循Qt的命名规范和编码风格
- Follow Qt's naming conventions and coding style
- 类名使用驼峰命名法（首字母大写）
- Class names use camel case (capitalized first letter)
- 成员函数和变量使用驼峰命名法（首字母小写）
- Member functions and variables use camel case (lowercase first letter)
- 常量使用全大写字母加下划线
- Constants use all uppercase letters with underscores

### 扩展建议 / Extension Suggestions
1. 增加科学计算功能（三角函数、对数等）
1. Add scientific calculation functions (trigonometric functions, logarithms, etc.)
2. 实现历史记录功能
2. Implement history record function
3. 添加主题切换功能
3. Add theme switching function
4. 支持复数运算
4. Support complex number operations
5. 增加单位换算功能
5. Add unit conversion function

## 更新日志 / Update Log

### 版本 1.0 / Version 1.0
- 初始版本
- Initial version
- 实现基本的四则运算
- Implement basic four arithmetic operations
- 支持高精度计算
- Support high-precision calculation
- 提供小数位数设置功能
- Provide decimal places setting function
- 包含帮助和关于对话框
- Include help and about dialog boxes

## License

MIT License

## 致谢 / Acknowledgements

感谢Qt框架提供的强大GUI开发能力，以及所有对本项目提供帮助的开发者。
Thank you to the Qt framework for providing powerful GUI development capabilities, and to all developers who have helped with this project.