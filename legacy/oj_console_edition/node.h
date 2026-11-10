#ifndef NODE_H
#define NODE_H

class Node {
    friend class Number;       //便于Number类成员函数访问Node类的私有成员
private:
    int Data;              //节点数据
    Node* next= nullptr;      //指向下一个节点的指针
    Node* prior=nullptr;     //指向上一个节点的指针
public:
    explicit Node(int = 0);                 //构造函数，初始化节点数据
    Node* insertBefore(int = 0);  //在当前节点前插入一个节点，参数为插入的数据
    Node* insertAfter(int = 0);   //在当前节点后插入一个节点，参数为插入的数据
};

#endif //NODE_H
