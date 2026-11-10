//
// Created by 17272 on 2024/2/15.
//

#ifndef TEST03_NODE_H
#define TEST03_NODE_H


class Node {
    friend class Number;       //便于Number类成员函数访问Node类的私有成员
private:
    int Data = 0;              //节点数据
    Node* next = nullptr;      //指向下一个节点的指针
    Node* prior = nullptr;     //指向上一个节点的指针
public:
    explicit Node(int);                 //构造函数，初始化节点数据
    Node();                    //构造函数
    Node* insertBefore(int);  //在当前节点前插入一个节点，参数为插入的数据
    Node* insertAfter(int);   //在当前节点后插入一个节点，参数为插入的数据
};



#endif //TEST03_NODE_H
