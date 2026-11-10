#include "node.h"
Node* Node::insertBefore(int numBit) {
    Node* temp = new Node(numBit);          //开辟新节点的内存
    temp->prior = prior;                    //1.新节点的prior指向当前节点的prior
    temp->next = this;                      //2.新节点的next指向当前节点
    this->prior = temp;                     //3.当前节点的prior指向新节点
    if (temp->prior != nullptr)             //防止访问空指针
        temp->prior->next = temp;           //4.原节点的前一个节点的next指向新节点
    return temp;                            //返回新节点的地址
}
Node* Node::insertAfter(int numBit) {
    Node* temp = new Node(numBit);          //开辟新节点的内存
    temp->prior = this;                     //1.新节点的prior指向当前节点
    temp->next = next;                      //2.新节点的next指向当前节点的next
    this->next = temp;                      //3.当前节点的next指向新节点
    if (temp->next != nullptr)              //防止访问空指针
        temp->next->prior = temp;           //4.原节点的后一个节点的prior指向新节点
    return temp;                            //返回新节点的地址
}
Node::Node(int NumBit) {
    Data = NumBit;                          //初始化数据
    prior = next = nullptr;                 //初始化指针
}
