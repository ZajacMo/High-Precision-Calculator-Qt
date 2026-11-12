#include "ui/MainWindow.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <exception>
#include <string>

#include "core/BigDecimal.h"
#include "core/Expression.h"
#include "core/Format.h"
#include "core/Rounding.h"

namespace hpc {
namespace ui {

namespace {

// UTF-8 字节下标 → UTF-16 码元下标。
//
// 内核报的错误位置是**字节**下标（它按 UTF-8 字节处理表达式），而
// QLineEdit::setSelection 用的是 **UTF-16 码元**下标。两者只在纯 ASCII
// 时相等；非 ASCII 的表达式必须做这个换算，否则选中的是错的字符。
//
// 逐个码元地累加字节数是**不够**的：非 BMP 字符（代理对）占 2 个 UTF-16
// 码元但只占 4 个 UTF-8 字节，逐码元累加会算成 6。所以这里按码点走。
int utf8OffsetToUtf16(const QString& text, int byteOffset) {
    if (byteOffset <= 0) return 0;
    int byte = 0;
    int utf16 = 0;
    while (byte < byteOffset && utf16 < text.size()) {
        const QChar ch = text.at(utf16);
        const char16_t unit = ch.unicode();
        int codeUnits = 1;
        int bytes = 1;
        if (unit < 0x80) {
            codeUnits = 1;
            bytes = 1;
        } else if (unit < 0x800) {
            codeUnits = 1;
            bytes = 2;
        } else if (unit >= 0xD800 && unit <= 0xDBFF && utf16 + 1 < text.size() &&
                   text.at(utf16 + 1).unicode() >= 0xDC00 && text.at(utf16 + 1).unicode() <= 0xDFFF) {
            // 高代理 + 低代理 = 一个码点：2 个 UTF-16 码元，4 个 UTF-8 字节。
            codeUnits = 2;
            bytes = 4;
        } else {
            codeUnits = 1;
            bytes = 3;
        }
        byte += bytes;
        utf16 += codeUnits;
    }
    return utf16;
}

// 该位置上的字符占几个 UTF-16 码元。
//
// BMP 字符占 1 个；非 BMP 字符（代理对）占 2 个。选区长度必须按**码点**
// 算 —— 硬编码成 1 会在代理对上只选中高代理那半个字符：既不是用户看到的
// 字符，复制出去也不是那个码点。
int utf16LengthAt(const QString& text, int index) {
    if (index < 0 || index >= text.size()) return 0;
    const QChar first = text.at(index);
    if (first.isHighSurrogate() && index + 1 < text.size() && text.at(index + 1).isLowSurrogate()) {
        return 2;
    }
    return 1;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("高精度计算器");
    resize(680, 620);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    layout->addWidget(new QLabel("表达式", central));
    expression_ = new QLineEdit(central);
    expression_->setObjectName("expression");
    expression_->setText("1/3 + 2*sqrt(9)");
    expression_->setPlaceholderText("支持 + - * / %、括号与 sqrt/abs/pow/exp/ln/log10");
    layout->addWidget(expression_);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel("有效数字位", central));
    precision_ = new QSpinBox(central);
    precision_->setObjectName("precision");
    // 上限取 kMaxPrecision（100000）—— 与 Web 前端一致。
    //
    // 超越函数的上限（1000）是**算力**上限，不是**接口契约**上限：它
    // **只在真的用到超越函数时**才生效。纯加减乘除的表达式在 100000 位
    // 以内都合法。把上限设成 1000 会让「纯算术表达式要 5000 位」这个
    // 合法请求**够不着** —— 用户在界面上根本选不到那个精度。
    //
    // 越界时的行为是**明确的报错**而不是静默降级：
    //   - 选到 100000 以上 → 界面层直接拒绝（并说明上限）；
    //   - 选到 1000..100000 之间但用到了超越函数 → **内核抛异常**，
    //     这里把它翻译成一条说清楚「是哪个函数越的界、上限是多少」的错误。
    // 两者都不会给用户「悄悄换了个精度」的第三种结果。
    precision_->setRange(1, hpc::kMaxPrecision);
    precision_->setValue(30);
    precision_->setSuffix(" 位");
    controls->addWidget(precision_);

    controls->addWidget(new QLabel("输出进制", central));
    base_ = new QComboBox(central);
    base_->setObjectName("base");
    base_->addItem("10（十进制）", 10);
    base_->addItem("2（二进制）", 2);
    base_->addItem("8（八进制）", 8);
    base_->addItem("16（十六进制）", 16);
    base_->addItem("36（三十六进制）", 36);
    base_->setCurrentIndex(0);
    controls->addWidget(base_);

    button_ = new QPushButton("计算", central);
    button_->setObjectName("evaluate");
    controls->addWidget(button_);
    layout->addLayout(controls);

    output_ = new QPlainTextEdit(central);
    output_->setObjectName("output");
    output_->setReadOnly(true);
    layout->addWidget(output_);

    note_ = new QLabel(central);
    note_->setObjectName("note");
    note_->setWordWrap(true);
    note_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    note_->hide();
    layout->addWidget(note_);

    layout->addWidget(new QLabel("历史", central));
    history_ = new QListWidget(central);
    history_->setObjectName("history");
    layout->addWidget(history_);

    // 用法说明：把命令行与 Web 前端里那些「不说清楚就会踩坑」的语义
    // 摆在界面上，而不是让人从报错里反推。
    auto* help = new QGroupBox("用法", central);
    auto* helpText = new QLabel(
        "<ul>"
        "<li>运算符：<code>+ - * / %</code>；<code>%</code> 是 <b>fmod 语义</b>，"
        "符号跟随<b>被除数</b>（-7 % 3 == -1），左操作数带小数时余数也带小数"
        "（5.5 % 2 == 1.5）—— 不是「先取整再取余」。</li>"
        "<li>函数：<code>sqrt</code>、<code>abs</code>、<code>pow</code>、"
        "<code>exp</code>、<code>ln</code>、<code>log10</code>。</li>"
        "<li>数字：十进制小数、科学计数法（<code>1e20</code>），以及 "
        "<code>0x</code> / <code>0b</code> / <code>0o</code> 前缀的整数。</li>"
        "<li>精度是<b>有效数字位</b>。上限 100000 位；但表达式一旦用到超越函数"
        "（<code>sqrt</code> / <code>exp</code> / <code>ln</code> / "
        "<code>log10</code>，以及 <code>pow</code> 的<b>非整数指数</b>分支），"
        "精度另受 <b>1000 位</b>约束 —— 那是内核的算力上限，与界面无关。"
        "纯加减乘除的表达式不受这条约束。</li>"
        "<li>进制转换的整数部分<b>精确无损</b>；小数部分若在该进制下无法有限"
        "表示，会截断并用 <code>...</code> 标记 —— 那是「展开被截断了」，"
        "<b>不是</b>精确值。</li>"
        "<li>高精度除法很慢：100000 位需要几十秒。</li>"
        "</ul>", central);
    helpText->setWordWrap(true);
    helpText->setTextFormat(Qt::RichText);
    helpText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* helpLayout = new QVBoxLayout(help);
    helpLayout->addWidget(helpText);
    layout->addWidget(help);

    setCentralWidget(central);

    connect(button_, &QPushButton::clicked, this, &MainWindow::evaluate);
    connect(expression_, &QLineEdit::returnPressed, this, &MainWindow::evaluate);
}

MainWindow::~MainWindow() = default;

void MainWindow::evaluate() {
    const QString text = expression_->text();
    if (text.isEmpty()) return;

    // **不**把表达式 trim 掉再交给内核：内核报的错误位置是相对**它收到
    // 的那个串**的字节下标，trim 会让位置与输入框里的字符错位。内核自己
    // 会跳过空白，所以原样传进去位置就是准的。
    const hpc::Context context{precision_->value(), hpc::RoundingMode::HalfEven};

    hpc::BigDecimal result;
    try {
        result = hpc::evaluate(text.toStdString(), context);
    } catch (const hpc::ExpressionError& error) {
        showError(QString::fromUtf8(error.what()), error.position());
        return;
    } catch (const std::exception& error) {
        showError(QString::fromUtf8(error.what()), -1);
        return;
    } catch (...) {
        // 兜底：任何异常都**不能**逃进 Qt 的事件循环 —— 那会终止进程。
        showError(QStringLiteral("求值失败：未知的异常"), -1);
        return;
    }

    const QString decimal = QString::fromStdString(result.toString());
    const int base = base_->currentData().toInt();

    if (base == 10) {
        output_->setPlainText(decimal);
        setNote(QString());
        appendHistory(text.trimmed(), decimal);
        return;
    }

    try {
        const std::string converted = hpc::toBaseString(result, base);
        const QString shown = QString::fromStdString(converted);
        output_->setPlainText(shown);

        if (converted.find("...") != std::string::npos) {
            // "..." 表示「展开**被截断了**」。必须说出来 —— 把截断值当成
            // 精确值展示就是在骗用户。整数部分仍是精确的，一并说明。
            const std::size_t dot = converted.find('.');
            const QString integerPart =
                dot == std::string::npos
                    ? QString::fromStdString(converted)
                    : QString::fromStdString(converted.substr(0, dot));
            setNote(QStringLiteral("注意：末尾的 “...” 表示该进制下的小数展开被截断了，"
                                   "不是精确值；整数部分 %1 是精确的。")
                        .arg(integerPart));
        } else {
            setNote(QString());
        }
        appendHistory(text.trimmed(), shown);
    } catch (const std::exception& error) {
        // 转换失败不丢掉十进制结果 —— 那个值是对的。
        output_->setPlainText(decimal);
        setNote(QStringLiteral("进制转换失败：%1（下面显示的是十进制结果）")
                    .arg(QString::fromUtf8(error.what())));
    }
}

void MainWindow::appendHistory(const QString& expression, const QString& result) {
    history_->insertItem(0, expression + " = " + result);
}

void MainWindow::showError(const QString& message, int position) {
    QString text = QStringLiteral("错误：") + message;
    if (position >= 0) {
        text += QStringLiteral("（位置 %1）").arg(position);
    }
    output_->setPlainText(text);

    if (position >= 0) {
        const QString source = expression_->text();
        const int index = utf8OffsetToUtf16(source, position);
        if (index < source.size()) {
            // 位置指着**确实存在**的那个字符 —— 选中它。
            //
            // 选区长度按**码点**算而不是写死 1：非 BMP 字符（代理对）占
            // 2 个 UTF-16 码元，写死 1 只会选中高代理那半个 —— 既不是
            // 用户看到的字符，复制出去也不是那个码点。
            setNote(QStringLiteral("位置 %1 是出错字符在表达式里的字节下标，"
                                   "已在输入框中选中该字符。").arg(position));
            // 先给焦点再设选区：反过来时 setFocus() 可能把选区清掉。
            expression_->setFocus();
            expression_->setSelection(index, utf16LengthAt(source, index));
        } else {
            // 位置指向串尾 —— 那里**没有字符**，出错的字符本该出现在
            // 那个位置（内核的「表达式意外结束」「表达式为空」就是这种）。
            // 没有字符可选，只能把光标放到末尾。
            //
            // 所以这里**不能**说「已选中该字符」：选区是空的，说了就是
            // 告诉用户一件假话 —— 用户读完美目张望看不到任何高亮。
            setNote(QStringLiteral("位置 %1 是出错字符在表达式里的字节下标；"
                                   "该位置已经超出表达式末尾（那里本该出现这个"
                                   "字符），已把光标放到表达式末尾。").arg(position));
            expression_->setFocus();
            expression_->setCursorPosition(source.size());
        }
    } else {
        setNote(QString());
    }
}

void MainWindow::setNote(const QString& text) {
    note_->setText(text);
    note_->setVisible(!text.isEmpty());
}

}  // namespace ui
}  // namespace hpc
