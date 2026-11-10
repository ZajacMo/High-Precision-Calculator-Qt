#include "tempnum.h"


bool TempNum::calculate(){                           //计算结果，相当于控制台版本的main函数部分
    if(mode=='+'||mode=='-'){
        *result = Number::transformSign(*Num1, *Num2, mode) ? (*Num1 + *Num2) : (*Num1 - *Num2);
    }
    else if(mode=='*')
        *result = *Num1 * *Num2;
    else if(mode=='/'){
        if(Num2->checkBit()) {                          //检查除数是否过小
            QMessageBox(QMessageBox::Warning,"Warning","The absolute value of the entered number must be greater than 0.000001!",QMessageBox::Ok).exec();
            return true;                                //返回true，表示计算异常
        }
        *result = *Num1 / *Num2;
    }
    else if(mode=='!')
        *result = *Num2;                                //若没有输入运算符，则将操作数2赋值给结果
    str = result->showQstr();                           //将结果转化为字符串
    inSecond = true;                                    //切换到第二行
    return false;                                       //返回false，表示计算正常
}
TempNum::TempNum(){
    Num1=new Number;
    Num2=new Number;
    result=new Number;
}
TempNum::~TempNum(){
    delete Num1;
    delete Num2;
    delete result;
}
