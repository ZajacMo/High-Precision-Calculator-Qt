//
// Created by 17272 on 2024/2/15.
//
#include"node.h"
#include<string>
#ifndef TEST03_NUMBER_H
#define TEST03_NUMBER_H


class Number {
    friend bool Transform_Sign(Number&,Number&,char &Mode);
    friend void Complement(Number*,Number*);
    friend Number POWER(int);            //设置友元函数，便于访问私有内容

private:
    Node *Head=nullptr;                  //头指针
    Node *Tail = Head, *Unit=Tail;       //尾指针，个位指针
    bool isNegative = false;             //符号位
    int Len= 1,Len_frac = 0;             //总长度，小数位长度

    void Move_Backwards(Node*&);         //使指针安全地向前移动一位，参数为被移动的指针
    void Delete_zero();                  //删除多余的0
    void Carry();                        //进位

public:
    Number();                           //构造空数
    explicit Number(int);               //构造整数
    void Get(std::string);                         //获取输入的数据，Qt版参数为待转化的字符串QString类型
    std::string Show();                 //输出数据，Qt版返回值为QString类型
    void Release();                     //释放内存,防止仅释放指针造成内存泄漏
    void Copy(const Number&);           //复制数据，防止指针指向同一地址，参数为被复制的数
    Number operator+(Number&);
    Number operator-(Number&);
    Number operator*(const Number&);
    Number operator/(Number&);          //运算符重载
    bool Check_Zero();                  //检查是否为0
    bool Check_Bit();                   //检查绝对值是否小于0.000001
};


#endif //TEST03_NUMBER_H
