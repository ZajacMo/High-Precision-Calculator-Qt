#include"node.h"
#include<qDebug>

#ifndef QSTRING
#define QSTRING
#include<QString>
#endif


#include"node.h"

typedef QString STR;
#ifndef NUMBER_H
#define NUMBER_H

class Number {

public:
    explicit Number(int=0);               //构造整数
    Number(const Number&);              //复制构造函数
    ~Number();                          //析构函数
    Number& operator=(const Number&);    //赋值运算符重载
    Number operator+(Number);
    Number operator-(Number);
    Number operator*(const Number&);
    Number operator/(Number&);          //运算符重载
    bool checkBit();                   //检查绝对值是否小于0.000001
    void get(STR);                      //获取输入的数据
    void release();                     //释放内存,防止仅释放指针造成内存泄漏
    [[nodiscard]] STR showQstr()const;       //输出数据

    static int FracBit;                //小数位数
    static bool transformSign(Number&, Number&, QChar&);//符号转换
private:
    Node *Head=nullptr;                  //头指针
    Node *Tail = Head, *Unit=Tail;       //尾指针，个位指针
    bool isNegative = false;             //符号位
    int Len= 1,Len_frac = 0;             //总长度，小数位长度

    static Number getPower(int);         //获取10的n次方
    static void complement(Number*, Number*);

    void moveBackwards(Node*&);         //使指针安全地向前移动一位，参数为被移动的指针
    void deleteZero();                  //删除多余的0
    void carry();                        //进位

    void copy(const Number&);           //复制数据，防止指针指向同一地址，参数为被复制的数
    void reset(const bool& =false, const int& =1, const int& =0, const bool& =true);//重置数据
    [[nodiscard]] bool checkZero()const;    //检查是否为0
};

#endif //NUMBER_H
