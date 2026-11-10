
#include"number.h"
int Number::FracBit = 10;                                    //设置小数位数
int main() {
    std::ios::sync_with_stdio(false);
    int allTimes;
    std::cin >> allTimes;                                    //总循环次数
    for (int times = 0 ; times < allTimes ; times++) {
        char mode = '+';
        std::cin >> mode;
        if(mode != '+' && mode != '-' && mode != '*' && mode != '/'){     //检查运算符是否合法
            std::cout<<"ERROR\n";
            continue;
        }
        Number* Num = new Number[2],result;
        std::cin >> Num[0] >> Num[1];
        if(mode == '+' || mode == '-'){
            result = Number::transformSign(Num[0], Num[1], mode) ? (Num[0] + Num[1]) : (Num[0] - Num[1]);
        }
        else if(mode == '*')
            result = Num[0] * Num[1];
        else if(Num[1].checkBit()) {                           //检测除数是否过小
            std::cout << "ERROR\n";
            continue;
        }
        else{
            result = Num[0] / Num[1];
        }
        std::cout << result << "\n";
        delete[] Num;
    }
    return 0;
}
