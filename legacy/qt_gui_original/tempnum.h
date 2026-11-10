#ifndef QSTRING
#define QSTRING
#include<QString>
#endif
#include <QMessageBox>
#include "number.h"
#ifndef TEMPNUM_H
#define TEMPNUM_H
class TempNum
{
    template<class T>friend void refresh(T*&); //刷新指针
    friend class MainWindow;
private:
    TempNum();
    ~TempNum();
    QString str="";                   //输入的数字
    bool DotExit=false,               //小数点是否已输入
        input=false,                  //是否已输入数字
        inSecond=false,               //是否在第二行
        ACPressed=false;              //是否已按下AC
    Number *Num1, *Num2, *result;     //操作数1，操作数2，结果
    bool calculate();                 //计算结果
    QChar mode='!';                   //运算符
};
template<typename T>
void refresh(T* &t){
    delete t;
    t=new T;
}
#endif // TEMPNUM_H
