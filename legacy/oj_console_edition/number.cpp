#include "number.h"
Number Number::operator/(Number& Num2){
    Num2.deleteZero();                        //删除多余的0
    while(Num2.Len_frac>0){                   //将除数转化为整数，被除数扩大相同倍数
        Num2.Len_frac--;
        Num2.Unit=Num2.Unit->next;
        if(Unit->next==nullptr){              //若被除数位数不够，则补0
            Unit = Tail = Tail->insertAfter(0);
            Len++;
        }else{                                //否则被除数的个位指针向后移动一位
            Unit = Unit->next;
            Len_frac--;
        }
    }
    Number result(0);
    result.isNegative = isNegative ^ Num2.isNegative;//确定符号
    isNegative = Num2.isNegative = false;
    Num2.deleteZero();
    int start = Len - Len_frac - Num2.Len , i;
    for( i = start ; i >= - ( FracBit + 1 ) ; i-- ){        //不断循环直到精度达到预设值
        if(this->checkZero())               //若最新的被除数为0则结束循环
            break;
        int j;
        for( j=1; j<10; j++) {                     //遍历1-9，试商
            Number Gap((*this) - getPower(i)* Num2);       //做差法比较尝试值与被除数的大小
            if (Gap.isNegative) {            //若尝试值大于被除数
                j--;
                break;
            }else {                          //若尝试值等于被除数，余数为0
                *this = Gap;
                if (checkZero())
                    break;
            }
        }
        result.Tail = result.Tail -> insertAfter(j);//将试商得到的结果插入链表
        result.Len++ ;
        if(i < 0)
            result.Len_frac++;
        else if(i == 0)
            result.Unit=result.Tail;            //确定个位
    }
    if (i == -(FracBit+2)) {                    //若精度超过预设值
        result.Tail = result.Tail->prior;
        if (result.Tail->next->Data >= 5)       //四舍五入
            result.Tail->Data++;
        delete result.Tail->next;
        result.Tail->next = nullptr;
        result.Len--;
        result.Len_frac--;
    }else if(i>=0){                           //若被除数的整数部分位数不够
        for(;i>=0;i--) {                     //则在后补0
            result.Tail = result.Tail->insertAfter(0);
            result.Len++;
        }
        result.Unit=result.Tail;
    }
    result.deleteZero();                            //删除多余的0
    return result;                                   //返回结果
}
Number Number::operator*(const Number &num2){           //乘法
    Number result(0),&num1=*this;                  //初始化临时值
    result.isNegative = num1.isNegative ^ num2.isNegative;//确定符号
    Node* num_bit[3] = {num1.Tail, num2.Tail, result.Tail},*p=result.Tail;
    result.Len_frac = num1.Len_frac + num2.Len_frac;      //确定小数位长度
    while (num_bit[0] != nullptr) {                      //遍历被乘数链表
        while (num_bit[1] != nullptr&&num_bit[0]->Data!=0) {
            num_bit[2]->Data += num_bit[0]->Data * num_bit[1]->Data;
            num_bit[1] = num_bit[1]->prior;
            result.moveBackwards(num_bit[2]);
        }                                       //遍历乘数链表，并逐位相乘
        num_bit[0] = num_bit[0]->prior;
        num_bit[1] = num2.Tail;
        result.moveBackwards(p);                 //指向结果位的指针安全地向前移动一位
        num_bit[2] = p;
    }
    result.carry();                               //进位
    while(result.Len<=result.Len_frac) {            //若小数位长度大于现有数字总长度
        result.Unit = result.Head = result.Head->insertBefore(0); //则在前补0
        result.Len++;
    }
    result.deleteZero();                         //删除多余的0
    return result;                                //返回结果
}
Number Number::operator-(Number num2) {    //实际上是异号加法
    Number num1(*this);                       //复制被减数
    complement(&num1, &num2);                 //补齐，使两数位数相同
    Node** num_bit = new Node * [2] {num1.Tail, num2.Tail};
    while (num_bit[0] && num_bit[1]) {      //遍历链表
        if (num_bit[1] != nullptr) {
            num_bit[0]->Data -= num_bit[1]->Data;
            num_bit[1] = num_bit[1]->prior;
        }
        num1.moveBackwards(num_bit[0]);    //安全地向前移动一位
        if(num_bit[0]->next->Data<0){       //借位
            num_bit[0]->next->Data+=10;
            num_bit[0]->Data--;
        }
    }
    delete[] num_bit;
    if (num1.Head->Data == -1)    //若结果为负数（借位造成第一位为-1）
    {
        Number N1(num2),N2(num1);
        N2.Head->Data = 0;
        N1.isNegative = true;
        N1.Head = N1.Head->insertBefore(1);
        N1.Len++;
        Node* p = N1.Head->next;
        while (p != nullptr) {
            p->Data = 0;
            p = p->next;
        }
        num1=N1-N2;                  //递归调用减法
    }
    num1.deleteZero();         //删除多余的0
    return num1;                //返回结果
}
Number Number::operator+(Number num2){      //实际是同号加法
    Number num1(*this);
    complement(&num1, &num2);                  //补齐，使两数位数相同
    Node** num_bit = new Node* [2] {num1.Tail, num2.Tail};
    while (num_bit[0] && num_bit[1]) {          //遍历链表
        if (num_bit[1] != nullptr) {
            num_bit[0]->Data += num_bit[1]->Data;
            num_bit[1] = num_bit[1]->prior;
        }
        num1.moveBackwards(num_bit[0]);     //安全地向前移动一位
    }
    num1.carry();                            //进位
    delete[] num_bit;
    num1.deleteZero();                      //删除多余的0
    return num1;                             //返回结果
}
Number::Number(int goal){                     //构造有限位数的整数
    if(goal<0) {                              //判断是否为负数
        isNegative = true;
        goal = -goal;
    }
    Unit = Tail = Head= new Node(goal);
    carry();                                  //进位
}
void Number::get(STR str){               //获取输入的数据，参数为待转化的字符串
    Unit = Tail = Head = new Node;                            //分配内存
    if (str[0] == '-') {                      //判断是否为负数
        isNegative = true;
        str.erase(0, 1);
    }
    if(str.find('.') != std::string::npos){   //判断是否为小数
        Len=str.find('.');
        str.erase(str.find('.'), 1);
        Len_frac= str.size()-Len;
        Len+=Len_frac;
        for(int i=Len-Len_frac;i<Len;i++)      //将小数部分插入链表
            Tail=Tail->insertAfter(str[i]-'0');
    }
    else
        Len = str.size();
    for (int i = 0; i < Len-Len_frac; i++) {
        if (str[i] == ',') {                   //忽略逗号
            str.erase(i, 1);
            Len--;
            i--;
        }
        else
            Unit=Unit->insertAfter(str[i] - '0');//将整数部分插入链表
    }
    if(Len_frac==0)
        Tail=Unit;
    Len++;
    deleteZero();                              //删除多余的0
}
void Number::deleteZero(){                     //删除多余的0
    while(Head!=nullptr&&Head->Data == 0 && Len-Len_frac>1) {
        Head = Head->next;
        delete Head->prior;
        Head->prior = nullptr;
        Len--;
    }                                           //删除整数部分的0
    while(Tail!=nullptr&&Tail->Data == 0 && Len_frac>0) {
        Tail = Tail->prior;
        delete Tail->next;
        Tail->next = nullptr;
        Len_frac--;
        Len--;
    }                                           //删除小数部分的0
}
STR Number::show()const{                            //输出数据
    STR str="";
    if (isNegative && (Head->Data != 0 || Len_frac!=0))
        str+="-";                               //如果是负数，输出负号
    int bits = Len-Len_frac;
    Node* p = Head;
    do{                                         //遍历链表的整数部分
        if (bits % 3 == 0 && bits != Len-Len_frac)
            str+=",";                           //每三位加一个逗号
        str+=std::to_string(p->Data);           //将链表中的数据转化为字符
        bits--;
        p = p->next;
    }while (p != Unit->next);
    if (Len_frac > 0) {                         //如果是小数
        str+=".";                               //输出小数点
        while (p != nullptr) {                  //遍历链表的小数部分
            str+=std::to_string(p->Data);
            p = p->next;
        }
    }
    return str;                                 //返回字符串
}
void Number::moveBackwards(Node*& p){ //使指针安全地向前移动一位，参数为被移动的指针
    if(p->prior==nullptr){                     //如果指针指向头指针
        Head = p = p->insertBefore(0);                  //在头指针前插入一个节点
        Len++;
    }
    else                                      //否则指针直接向前移动一位
        p = p->prior;
}
void Number::carry(){                          //进位
    deleteZero();                             //删除多余的0
    Node *p = Tail;
    for(int i=0;p != nullptr;i++) {            //遍历链表
        if(i==Len_frac)                        //确定个位
            Unit = p;
        if (p->Data > 9) {                     //如果数据大于9
            moveBackwards(p);                 //指针向前移动一位
            p->Data += p->next->Data / 10;
            p->next->Data %= 10;
        }
        else
            p = p->prior;                      //否则指针向前移动一位
    }
}
Number& Number::operator=(const Number &goal){  //赋值运算符重载
    if(this==&goal)                           //防止自赋值
        return *this;
    this->copy(goal);                               //复制数据
    return *this;
}
Number::~Number(){                           //析构函数
    release();                               //释放内存
}
Number::Number(const Number &goal){//复制数据，防止指针指向同一地址，参数为被复制的数
    copy(goal);
}
void Number::copy(const Number &num2){//复制数据，防止指针指向同一地址，参数为被复制的数
    release();                                  //释放内存
    reset(num2.isNegative, num2.Len, num2.Len_frac);
    Node* p = num2.Head;
    while (p != nullptr) {                      //遍历链表
        Tail = Tail->insertAfter(p->Data);      //复制数据
        if(p==num2.Unit)                        //确定个位
            Unit=Tail;
        p = p->next;
    }
    Head = Head->next;                         //删除多余的头节点
    if(Head!=nullptr){
        delete Head->prior;
        Head->prior = nullptr;
    }
}
void Number::reset(const bool &neg, const int &len, const int &len_frac,const bool &mode){ //重置数据，参数为符号位，总长度，小数位长度，模式
    isNegative = neg;                         //重置符号位、长度等参数
    Len = len;
    Len_frac = len_frac;
    Unit = Tail = Head = (mode? new Node : nullptr);             //重新分配内存
}
void Number::release(){                 //释放内存,防止仅释放指针造成内存泄漏
    Node* p = Head;
    while (p != nullptr) {              //遍历链表，删除节点
        Node* temp = p;
        p = p->next;
        delete temp;
    }
    reset(false,1,0,false);                            //重置数据
}
bool Number::checkZero()const{              //检查是否为0
    Node* p=Head;
    while(p!=nullptr){
        if(p->Data!=0)
            return false;
        p=p->next;
    }
    return true;
}
bool Number::checkBit(){               //检查绝对值是否小于0.000001
    Number goal(*this);                 //复制数据
    goal.isNegative = false;            //取绝对值
    if((goal - getPower(-6)).isNegative)
        return true;
    return false;
}
bool Number::transformSign(Number &Num1, Number &Num2, char &Mode){
    //简化符号，便于计算，参数为两个数的指针，以及运算符
    if (Mode=='-') {                   //减法转化为加法，减少情况种数
        Num2.isNegative = !Num2.isNegative;
        Mode = '+';
    }
    if (Num1.isNegative)       //如果第一个数为负数，则交换两数，保证减数不在前
        std::swap(Num1, Num2);
    //同号相加返回真，异号相加返回假，若为异号，则负号一定在后
    return (Num1.isNegative == Num2.isNegative);
}
void Number::complement(Number *Num1, Number *Num2) {
    //补齐小数位数，使两数位数相同，便于加减法运算，参数为两个数的指针
    Number *Num[2]={Num1,Num2};
    int order[2]={0,1};
    if(Num[0]->Len_frac<Num[1]->Len_frac)
        std::swap(order[0],order[1]);       //保证Num[0]的小数位数不小于Num[1]
    for(;Num[order[1]]->Len_frac<Num[order[0]]->Len_frac;Num[order[1]]->Len_frac++){
        Num[order[1]]->Tail = Num[order[1]]->Tail->insertAfter(0);
        Num[order[1]]->Len++;
    }                                       //补齐小数位数
    *order=0,*(order+1)=1;
    if(Num[0]->Len<Num[1]->Len)
        std::swap(order[0],order[1]);       //保证Num[0]的整数长度不小于Num[1]
    for(;Num[order[1]]->Len<Num[order[0]]->Len;Num[order[1]]->Len++){
        Num[order[1]]->Head = Num[order[1]]->Head->insertBefore(0);
    }                                        //补齐整数位数
}
Number Number::getPower(int n){                        //计算10^n，返回值为Number类型
    Number result(1);                         //初始化临时值
    if(n>0){                //根据n的正负，确定链表的头尾指针及各项参数，及添加0的方式
        result.Len = n+1;
        for(int i = 1 ; i<n+1 ; i++)
            result.Tail = result.Tail -> insertAfter(0);
        result.Unit = result.Tail;
    }
    else if(n<0){
        result.Len_frac=-n;
        result.Len=result.Len_frac+1;
        for(int i = 0 ; i < -n ; i++)
            result.Head = result.Head -> insertBefore(0);
        result.Unit = result.Head;
    }

    return result;
}
std::ostream& operator<<(std::ostream& os, Number& num){  //输出运算结果
    os<<num.show();
    return os;
}
std::istream& operator>>(std::istream& is, Number& num){  //输入运算数
    STR temp;
    is>>temp;
    num.get(temp);
    return is;
}