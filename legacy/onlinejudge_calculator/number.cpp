#include "number.h"
int FracBit=10;                               //精度位数
Number TEMP(0);                               //临时值，便于操作和计算
Number POWER(int);                            //计算10^n，返回值为Number类型
Number Number::operator/(Number& Num2){
    Num2.Delete_zero();                       //删除多余的0
    while(Num2.Len_frac>0){                   //将除数转化为整数，被除数扩大相同倍数
        Num2.Len_frac--;
        Num2.Unit=Num2.Unit->next;
        if(Unit->next==nullptr){              //若被除数位数不够，则补0
            Tail=Tail->insertAfter(0);
            Len++;
            Unit=Tail;
        }
        else{                                //否则被除数的个位指针向后移动一位
            Unit=Unit->next;
            Len_frac--;
        }
    }
    Number *p,Result(0),Trial, Gap, Times;
    Num2.Delete_zero();
    int start=Len-Len_frac-Num2.Len,i;
    for(i=start;i>=-(FracBit+1);i--){        //不断循环直到精度达到预设值
        if(this->Check_Zero())               //若最新的被除数为0则结束循环
            break;
        int j;
        for(j=1;j<FracBit+1;j++) {           //遍历1-9，试商
            p = new Number(j);
            Times.Copy(POWER(i));            //计算10^i
            Gap.Copy((*p) * Times);          //得到带单位的试商值
            Trial.Copy(Gap * Num2);          //计算试商与除数的乘积
            Gap.Copy((*this) - Trial);       //做差法比较尝试值与被除数的大小
            delete p;
            if (Gap.isNegative) {            //若尝试值大于被除数
                Trial.Copy(Times * Num2);
                Copy(Trial - Gap);           //则返回上一个尝试值，存在TEMP中
                isNegative=false;
                j--;
                break;
            } else if (Gap.Check_Zero()) {   //若尝试值等于被除数，余数为0
                Copy(Gap);                   //下一次运算的被除数也变为0
                break;
            }
        }
        Result.Tail=Result.Tail->insertAfter(j);//将试商得到的结果插入链表
        Result.Len++;
        if(i<0)
            Result.Len_frac++;
        else if(i==0)
            Result.Unit=Result.Tail;            //确定个位
    }
    if (i == -(FracBit+2)) {                    //若精度超过预设值
        Result.Tail = Result.Tail->prior;
        if (Result.Tail->next->Data >= 5)       //四舍五入
            Result.Tail->Data++;
        delete Result.Tail->next;
        Result.Tail->next = nullptr;
        Result.Len--;
        Result.Len_frac--;
    }
    else if(i>=0){                           //若被除数的整数部分位数不够
        for(;i>=0;i--) {                     //则在后补0
            Result.Tail = Result.Tail->insertAfter(0);
            Result.Len++;
        }
        Result.Unit=Result.Tail;
    }
    Times.Release();
    Gap.Release();
    Trial.Release();
    Result.isNegative = isNegative ^ Num2.isNegative;//确定符号
    Result.Delete_zero();                            //删除多余的0
    return Result;                                   //返回结果
}
Number Number::operator*(const Number &Num2){           //乘法
    TEMP.Release();                          //释放临时值的内存，防止混乱内存泄漏
    Number t(0);
    TEMP=t;                                              //初始化临时值
    TEMP.isNegative = this->isNegative ^ Num2.isNegative;//确定符号
    Node* num_bit[3] = {this->Tail, Num2.Tail,TEMP.Tail},*p=TEMP.Tail;
    TEMP.Len_frac = this->Len_frac + Num2.Len_frac;      //确定小数位长度
    while (num_bit[0] != nullptr) {                      //遍历被乘数链表
        while (num_bit[1] != nullptr&&num_bit[0]->Data!=0) {
            num_bit[2]->Data += num_bit[0]->Data * num_bit[1]->Data;
            num_bit[1] = num_bit[1]->prior;
            TEMP.Move_Backwards(num_bit[2]);
        }                                       //遍历乘数链表，并逐位相乘
        num_bit[0] = num_bit[0]->prior;
        num_bit[1] = Num2.Tail;
        TEMP.Move_Backwards(p);                 //指向结果位的指针安全地向前移动一位
        num_bit[2] = p;
    }
    TEMP.Carry();                               //进位
    while(TEMP.Len<=TEMP.Len_frac) {            //若小数位长度大于现有数字总长度
        TEMP.Head = TEMP.Head->insertBefore(0); //则在前补0
        TEMP.Len++;
        TEMP.Unit = TEMP.Head;
    }
    TEMP.Delete_zero();                         //删除多余的0
    return TEMP;                                //返回结果
}
Number Number::operator-(Number &Num2) {    //实际上是异号加法
    Complement(this,&Num2);                 //补齐，使两数位数相同
    TEMP.Copy(*this);                       //复制被减数
    Node** num_bit = new Node * [2] {TEMP.Tail, Num2.Tail};
    while (num_bit[0] && num_bit[1]) {      //遍历链表
        if (num_bit[1] != nullptr) {
            num_bit[0]->Data -= num_bit[1]->Data;
            num_bit[1] = num_bit[1]->prior;
        }
        TEMP.Move_Backwards(num_bit[0]);    //安全地向前移动一位
        if(num_bit[0]->next->Data<0){       //借位
            num_bit[0]->next->Data+=10;
            num_bit[0]->Data--;
        }
    }
    delete[] num_bit;
    if (TEMP.Head->Data == -1)    //若结果为负数（借位造成第一位为-1）
    {
        Number N1,N2;
        N1.Copy(Num2),N2.Copy(TEMP);
        N1.isNegative = 1;
        N2.Head->Data = 0;
        N1.Head = N1.Head->insertBefore(1);
        N1.Len++;
        Node* p = N1.Head->next;
        while (p != nullptr) {
            p->Data = 0;
            p = p->next;
        }
        N1-N2;                  //递归调用减法，结果由TEMP暂存
        N1.Release();
        N2.Release();
    }
    TEMP.Delete_zero();         //删除多余的0
    return TEMP;                //返回结果
}
Number Number::operator+(Number &Num2){      //实际是同号加法
    Complement(this,&Num2);                  //补齐，使两数位数相同
    TEMP.Copy(*this);
    Node** num_bit = new Node* [2] {TEMP.Tail, Num2.Tail};
    while (num_bit[0] && num_bit[1]) {          //遍历链表
        if (num_bit[1] != nullptr) {
            num_bit[0]->Data += num_bit[1]->Data;
            num_bit[1] = num_bit[1]->prior;
        }
        TEMP.Move_Backwards(num_bit[0]);     //安全地向前移动一位
    }
    TEMP.Carry();                            //进位
    delete[] num_bit;
    TEMP.Delete_zero();                      //删除多余的0
    return TEMP;                             //返回结果
}
Number::Number(int goal){                     //构造整数
    Head=new Node;
    Tail=Head;
    Unit=Tail;
    Head->Data=goal;
    Carry();                                  //进位
}
Number::Number(){                             //构造空数
    Head=nullptr;
    Tail=Head;
    Unit=Tail;
    isNegative=false;
    Len=1;
    Len_frac=0;
}
void Number::Get(std::string str){               //获取输入的数据，参数为待转化的字符串，Qt版为QString类型
    Head=new Node;                            //分配内存
    Tail=Head;
    Unit=Tail;
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
    Delete_zero();                              //删除多余的0
}
void Number::Delete_zero(){                     //删除多余的0
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
std::string Number::Show(){     //输出数据，Qt版返回值为QString类型
    std::string str="\0";
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
    return str;                 //返回字符串，Qt版返回值为QString类型
}
void Number::Move_Backwards(Node*& p){ //使指针安全地向前移动一位，参数为被移动的指针
    if(p->prior==nullptr){                     //如果指针指向头指针
        p=p->insertBefore(0);                  //在头指针前插入一个节点
        Head=p;
        Len++;
    }
    else                                      //否则指针直接向前移动一位
        p=p->prior;
}
void Number::Carry(){                          //进位
    Delete_zero();                             //删除多余的0
    Node *p = Tail;
    for(int i=0;p != nullptr;i++) {            //遍历链表
        if(i==Len_frac)                        //确定个位
            Unit = p;
        if (p->Data > 9) {                     //如果数据大于9
            Move_Backwards(p);                 //指针向前移动一位
            p->Data += p->next->Data / 10;
            p->next->Data %= 10;
        }
        else
            p = p->prior;                      //否则指针向前移动一位
    }
}
void Number::Copy(const Number &num2){//复制数据，防止指针指向同一地址，参数为被复制的数
    this->Release();                           //释放原有内存，防止内存泄漏
    isNegative = num2.isNegative;              //复制符号位、长度等参数
    Len = num2.Len;
    Len_frac = num2.Len_frac;
    Head = new Node;                           //重新分配内存
    Tail = Head;
    Unit = Tail;
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
void Number::Release(){                 //释放内存,防止仅释放指针造成内存泄漏
    Node* p = Head;
    while (p != nullptr) {              //遍历链表，删除节点
        Node* temp = p;
        p = p->next;
        delete temp;
    }
    Head = nullptr;                     //恢复默认值
    Tail = nullptr;
    Unit = nullptr;
    Len = 1;
    Len_frac = 0;
    isNegative = false;
}
bool Number::Check_Zero(){              //检查是否为0
    Node* p=Head;
    while(p!=nullptr){
        if(p->Data!=0)
            return false;
        p=p->next;
    }
    return true;
}
bool Number::Check_Bit(){               //检查绝对值是否小于0.000001
    bool Sign=isNegative;               //保存符号位
    isNegative=false;                   //转化为绝对值
    Number TEMP2,TEMP3;
    TEMP3.Copy(POWER(-6));              //计算10^-6
    TEMP2.Copy(*this-TEMP3);            //和10^-6作差
    TEMP3.Release();
    isNegative=Sign;                    //恢复符号位
    if(TEMP2.isNegative) {
        TEMP2.Release();
        return true;
    }
    TEMP2.Release();
    return false;
}
bool Transform_Sign(Number &Num1,Number &Num2,char &Mode){
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
void Complement(Number *Num1,Number *Num2) {
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
Number POWER(int n){                        //计算10^n，返回值为Number类型
    TEMP.Release();                         //释放临时值的内存，防止混乱内存泄漏
    TEMP.Head = new Node(1);                //分配内存
    TEMP.Tail = TEMP.Head;
    if(n>0){                //根据n的正负，确定链表的头尾指针及各项参数，及添加0的方式
        TEMP.Len=n+1;
        for(int i=1;i<n+1;i++)
            TEMP.Tail = TEMP.Tail->insertAfter(0);
        TEMP.Unit = TEMP.Tail;
    }
    else if(n<0){
        TEMP.Len_frac=-n;
        TEMP.Len=TEMP.Len_frac+1;
        for(int i=0;i<-n;i++)
            TEMP.Head=TEMP.Head->insertBefore(0);
        TEMP.Unit = TEMP.Head;
    }
    else{                                   //n=0时，直接返回1
        TEMP.Len=1;
        TEMP.Len_frac=0;
        TEMP.Unit=TEMP.Tail;
    }
    return TEMP;
}