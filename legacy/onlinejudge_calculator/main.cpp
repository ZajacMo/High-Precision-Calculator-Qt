#include<iostream>
#include"number.h"

int main() {
    std::ios::sync_with_stdio(false);
    int allTimes;
    std::cin >> allTimes;                                    //总循环次数
    for (int times = 0; times < allTimes; times++) {
        char mode = '+';
        std::cin >> mode;
        if(mode!='+'&&mode!='-'&&mode!='*'&&mode!='/'){     //检查运算符是否合法
            std::cout<<"ERROR\n";
            continue;
        }
        Number* Num = new Number[2],Result;
        std::string temp;
        std::cin >> temp;
        Num[0].Get(temp);                                       //获取输入的数据
        std::cin >> temp;
        Num[1].Get(temp);
        if(mode=='+'||mode=='-'){
            Result.Copy(Transform_Sign(Num[0],Num[1],mode)?(Num[0]+Num[1]):(Num[0]-Num[1]));
        }
        else if(mode=='*')
            Result.Copy(Num[0]*Num[1]);
        else if(Num[1].Check_Bit()) {                           //检测除数是否过小
            std::cout << "ERROR\n";
            continue;
        }
        else{
            Result.Copy(Num[0]/Num[1]);
        }
        std::cout<<Result.Show()<<"\n";
        Num[0].Release();                                       //释放链表，防止内存泄漏
        Num[1].Release();
        delete[] Num;
    }
    //TEMP.Release();                                             //释放链表，防止内存泄漏
    return 0;
}
