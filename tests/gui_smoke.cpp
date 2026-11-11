// 离屏渲染冒烟测试：无需显示器即可验证界面能构建、求值并绘制。
//
// 这个测试**不**只验证「窗口能构造出来」—— 它验证：
//   1. 控件存在且类型正确（按 objectName 找，不靠 findChild 的第一个）；
//   2. 点「计算」后结果区的文本**等于**内核算出的那个值（逐字比较，
//      不是「含有小数点」那种弱断言）；
//   3. 历史条目写的是「表达式 = 结果」；
//   4. 出错时显示的是错误消息，且**带位置**、并在输入框里选中了那个字符；
//   5. 换进制后显示的是该进制下的展开，截断时带 "..." 标记；
//   6. 真的能画出一张非空的图（证明绘制链路通）。
//
// 异常安全：本文件的 main **不**捕获异常 —— 一个未捕获的异常会直接
// abort 掉测试二进制（而不是让测试失败）。所以每个可能抛异常的调用都
// 各自包在 try/catch 里。
#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include <cstdio>
#include <exception>
#include <string>

#include "ui/MainWindow.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (condition) {
        std::printf("  ok   %s\n", what);
    } else {
        std::printf("  FAIL %s\n", what);
        ++g_failures;
    }
}

void checkEqual(const QString& actual, const QString& expected, const char* what) {
    if (actual == expected) {
        std::printf("  ok   %s\n", what);
    } else {
        std::printf("  FAIL %s\n    期望：%s\n    实际：%s\n", what,
                    expected.toUtf8().constData(), actual.toUtf8().constData());
        ++g_failures;
    }
}

// 按 objectName 取控件；找不到就报失败并返回 nullptr。
template <typename T>
T* named(QObject* root, const char* name) {
    T* widget = root->findChild<T*>(name);
    if (widget == nullptr) {
        std::printf("  FAIL 找不到控件 %s\n", name);
        ++g_failures;
    }
    return widget;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    hpc::ui::MainWindow window;
    window.resize(680, 620);
    window.show();

    auto* lineEdit = named<QLineEdit>(&window, "expression");
    auto* spin = named<QSpinBox>(&window, "precision");
    auto* combo = named<QComboBox>(&window, "base");
    auto* button = named<QPushButton>(&window, "evaluate");
    auto* output = named<QPlainTextEdit>(&window, "output");
    auto* note = named<QLabel>(&window, "note");
    auto* history = named<QListWidget>(&window, "history");
    if (lineEdit == nullptr || spin == nullptr || combo == nullptr || button == nullptr ||
        output == nullptr || note == nullptr || history == nullptr) {
        std::fprintf(stderr, "gui_smoke: 找不到界面控件\n");
        return 1;
    }

    std::printf("[gui_smoke]\n");

    // --- 1. 求值：点按钮，结果必须**逐字**等于内核算出的值 ---
    //
    // 期望值取自 hpc_cli（同一个内核）：
    //   1/3 + 2*sqrt(9)，精度 30 → 6.333333333333333333333333333333
    lineEdit->setText("1/3 + 2*sqrt(9)");
    spin->setValue(30);
    combo->setCurrentIndex(0);           // 十进制
    button->click();

    const QString expected = QStringLiteral("6.333333333333333333333333333333");
    checkEqual(output->toPlainText(), expected, "点按钮后的十进制结果");
    check(history->count() == 1, "历史记录有 1 条");
    checkEqual(history->item(0)->text(),
               QStringLiteral("1/3 + 2*sqrt(9) = 6.333333333333333333333333333333"),
               "历史条目的文本");
    check(!note->isVisible(), "十进制结果没有截断注解");

    // --- 2. 回车键路径与按钮路径等价 ---
    lineEdit->setText("2*3");
    lineEdit->returnPressed();          // 触发 QLineEdit::returnPressed 信号
    checkEqual(output->toPlainText(), QStringLiteral("6"), "回车求值");
    check(history->count() == 2, "回车后历史有 2 条");

    // --- 3. 精度生效：位数随精度变化 ---
    lineEdit->setText("1/3");
    spin->setValue(10);
    button->click();
    checkEqual(output->toPlainText(), QStringLiteral("0.3333333333"), "精度 10 的 1/3");
    spin->setValue(50);
    button->click();
    checkEqual(output->toPlainText(),
               QStringLiteral("0.33333333333333333333333333333333333333333333333333"),
               "精度 50 的 1/3");

    // --- 4. 换进制：十六进制下 1/3 不可有限表示，必须带 "..." 标记 ---
    spin->setValue(30);
    combo->setCurrentIndex(3);          // 16（十六进制）
    button->click();
    check(output->toPlainText().contains(QStringLiteral("...")),
          "十六进制下 1/3 带截断标记");
    check(output->toPlainText().startsWith(QStringLiteral("0.")),
          "十六进制下 1/3 以 0. 开头");
    check(note->isVisible(), "截断时显示注解");
    check(note->text().contains(QStringLiteral("不是精确值")),
          "注解说明截断值不是精确值");

    // --- 5. 整数在二进制下**不**带截断标记（能有限表示就不该标） ---
    lineEdit->setText("255");
    combo->setCurrentIndex(1);          // 2（二进制）
    button->click();
    checkEqual(output->toPlainText(), QStringLiteral("11111111"), "255 的二进制");
    check(!output->toPlainText().contains(QStringLiteral("...")),
          "能有限表示的值不带截断标记");

    // --- 6. 错误：消息 + 位置 + 选中出错的字符 ---
    //
    // 期望值取自 hpc_cli：1/0 → "除数为零（位置 2）"。
    // 位置 2 是 '0' 那个字符（0-based）。
    lineEdit->setText("1/0");
    combo->setCurrentIndex(0);
    button->click();
    check(output->toPlainText().contains(QStringLiteral("错误")), "除零时显示错误");
    check(output->toPlainText().contains(QStringLiteral("除数为零")), "错误消息是「除数为零」");
    check(output->toPlainText().contains(QStringLiteral("位置 2")), "错误带位置 2");
    // QLineEdit 没有 hasSelection()：没有选区时 selectedText() 是空串，
    // 有选区时返回被选中的那几个字符。
    check(lineEdit->selectedText() == QStringLiteral("0"),
          "选中的正是出错的字符");

    // 出错**不**进历史（历史里放的是算过的表达式，不是错误）。
    const int countBeforeError = history->count();

    // --- 7. 超越函数的精度上限：内核报错，界面如实显示，不静默降级 ---
    lineEdit->setText("sqrt(2)");
    spin->setValue(2000);
    button->click();
    check(output->toPlainText().contains(QStringLiteral("错误")), "精度 2000 的超越函数报错");
    check(output->toPlainText().contains(QStringLiteral("超出超越函数的上限 1000")),
          "错误说明了超越函数的上限");
    check(history->count() == countBeforeError, "出错不进历史");

    // --- 8. 纯算术在 1000 位以上仍然合法（超越函数的上限不作用于它） ---
    lineEdit->setText("1/7");
    spin->setValue(2000);
    button->click();
    check(!output->toPlainText().contains(QStringLiteral("错误")), "精度 2000 的纯算术不报错");
    check(output->toPlainText().startsWith(QStringLiteral("0.142857")), "精度 2000 的 1/7");
    spin->setValue(30);

    // --- 9. '%' 是 fmod 语义（符号跟随被除数），与命令行一致 ---
    lineEdit->setText("-7 % 3");
    button->click();
    checkEqual(output->toPlainText(), QStringLiteral("-1"), "fmod 语义：-7 % 3");
    lineEdit->setText("5.5 % 2");
    button->click();
    checkEqual(output->toPlainText(), QStringLiteral("1.5"), "fmod 语义：5.5 % 2");

    // --- 10. 离屏渲染一张截图，确认能绘制 ---
    QPixmap pixmap;
    try {
        pixmap = window.grab();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "gui_smoke: 渲染时抛出异常：%s\n", error.what());
        return 1;
    }
    check(!pixmap.isNull(), "离屏渲染出了非空图");
    check(pixmap.width() == 680 && pixmap.height() == 620, "图的尺寸等于窗口尺寸");

    // 渲染出来的图**要有内容**：全黑（或全透明）说明什么都没画上去。
    const QImage image = pixmap.toImage();
    bool hasContent = false;
    for (int y = 0; y < image.height() && !hasContent; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).lightness() > 20) {
                hasContent = true;
                break;
            }
        }
    }
    check(hasContent, "渲染出的图有可见内容（不是全黑）");

    if (argc > 1) {
        const QString directory = QString::fromLocal8Bit(argv[1]);
        const QString path = directory + "/hpc_gui.png";
        if (!pixmap.save(path)) {
            std::fprintf(stderr, "gui_smoke: 截图保存失败：%s\n",
                         path.toUtf8().constData());
            ++g_failures;
        } else {
            std::printf("  截图已保存：%s\n", path.toUtf8().constData());
        }
    }

    std::printf("\n----------------------------------------\n");
    if (g_failures == 0) {
        std::printf("gui_smoke: 全部通过。\n");
        return 0;
    }
    std::printf("gui_smoke: 失败 %d 项。\n", g_failures);
    return 1;
}
