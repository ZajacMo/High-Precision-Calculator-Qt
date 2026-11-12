// Qt 桌面界面：仅是核心的前端之一，不含任何计算逻辑。
//
// 所有算术都在 src/core 里 —— 这里只做输入、参数选择、错误呈现与结果显示。
//
// 与命令行前端（src/cli/main.cpp）保持同一套语义：
//   - 精度是**有效数字位**（不是小数位）；
//   - 精度越界是**明确的报错**，不是静默夹到上限；
//   - '%' 是 fmod 语义（符号跟随被除数）；
//   - 进制转换的 "..." 标记如实呈现，不把截断值说成精确值。
#pragma once

#include <QMainWindow>
#include <QString>

// 前向声明：Qt 的控件类型。
//
// **不能**写成 `class QLineEdit*` 那种形式 —— 那个 `class` 关键字会在**当前
// 命名空间**里声明一个全新的不完整类型 `hpc::ui::QLineEdit`，把真正的
// `::QLineEdit` 遮住。于是 `findChild<QLineEdit*>()` 找不到任何东西，
// 而 `new QLineEdit(...)` 造出来的对象类型也不是 Qt 那个 —— 运行时必炸。
// 写成不带 `class` 的裸名字，声明的就是全局命名空间里的那一个。
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace hpc {
namespace ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    // 触发一次求值。「计算」按钮与输入框的回车都接到这里。
    void evaluate();
    // 把一次求值结果追加到历史列表。
    void appendHistory(const QString& expression, const QString& result);
    // 显示一条错误。position >= 0 时还要在输入框里指出出错的字符 ——
    // 那个字符**存在**就选中它；位置指向**串尾**（缺失的字符本该出现
    // 的地方）就只把光标放到末尾，注解也如实这么说。
    void showError(const QString& message, int position);
    // 结果区下方的注解（截断标记说明、出错位置说明）。空文本时隐藏。
    void setNote(const QString& text);

    QLineEdit* expression_{nullptr};
    QSpinBox* precision_{nullptr};
    QComboBox* base_{nullptr};
    QPushButton* button_{nullptr};
    QPlainTextEdit* output_{nullptr};
    QLabel* note_{nullptr};
    QListWidget* history_{nullptr};
};

}  // namespace ui
}  // namespace hpc
