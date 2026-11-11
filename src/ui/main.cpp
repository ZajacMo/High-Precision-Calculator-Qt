// Qt 桌面界面的入口点：只负责建 QApplication 与窗口。
//
// 应用的**逻辑**都在 src/ui/MainWindow.cpp 与 src/core 里。
#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    hpc::ui::MainWindow window;
    window.show();
    return app.exec();
}
