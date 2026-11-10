//
// Created by 17272 on 2024/2/15.
//
#include"node.h"
#include<string>
#include<iostream>
typedef std::string STR;
#ifndef NUMBER_H
#define NUMBER_H

class Number {
    //重载输入输出流，便于调用cin,cout
    friend std::ostream& operator<<(std::ostream&, Number&);
    friend std::istream& operator>>(std::istream&, Number&);

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

    static int FracBit;                //小数位数
    static bool transformSign(Number&, Number&, char&);//符号转换

private:
    Node *Head=nullptr;                  //头指针
    Node *Tail = Head, *Unit=Tail;       //尾指针，个位指针
    bool isNegative = false;             //符号位
    int Len= 1,Len_frac = 0;             //总长度，小数位长度

    static Number getPower(int);         //获取10的n次方
    static void complement(Number*, Number*);//补0，使两操作数位数相同

    void moveBackwards(Node*&);         //使指针安全地向前移动一位，参数为被移动的指针
    void deleteZero();                  //删除多余的0
    void carry();                        //进位
    void release();                     //释放内存,防止仅释放指针造成内存泄漏
    void copy(const Number&);           //复制数据，防止指针指向同一地址，参数为被复制的数
    void reset(const bool& =false, const int& =1, const int& =0, const bool& =true);//重置数据
    void get(STR);                      //获取输入的数据
    [[nodiscard]] bool checkZero()const;    //检查是否为0
    [[nodiscard]] STR show()const;                            //输出数据
};

#endif //NUMBER_H
