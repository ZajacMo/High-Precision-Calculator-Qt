#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include<QMainWindow>
#include<QAction>
#include<QShortcut>
#include<QTextBrowser>
#include"tempnum.h"
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:


private:
    void calculate();
    void operation(const QChar&);
    void showNumber(),showSign(const QChar&);
    void clearAll();
    void makeBlank(int,int=1);
    void rightShowText(int,QString);

    const QChar Signs[4]{'-','+','*','/'};//标记符号——+ - * /
    QString strBuffer;
    Ui::MainWindow *ui;
    //Number *Num1=new Number(0),*Num2=new Number(0), *result=new Number(0);
    TempNum *DB=new TempNum;
    QTextBrowser **TextGroup;
    QPushButton **SignGroup,**BackGroup,**NumGroup;
    const Qt::Key NumKeys[10]{Qt::Key_0,Qt::Key_1,Qt::Key_2,Qt::Key_3,Qt::Key_4,Qt::Key_5,Qt::Key_6,Qt::Key_7,Qt::Key_8,Qt::Key_9},//标记键盘——数字0~9
                  SignKeys[6]{Qt::Key_Enter,Qt::Key_Minus,Qt::Key_Plus,Qt::Key_Asterisk,Qt::Key_Slash,Qt::Key_Period},             //标记键盘——符号 + - * / . =
                  BackKeys[2]{Qt::Key_Backspace,Qt::Key_Delete};                                                                   //标记键盘——Back、AC
    template<typename T>void dealDialog(T); //处理对话框

};
#endif // MAINWINDOW_H
