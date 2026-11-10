#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "dialog_setbit.h"
#include "dialog_help.h"
#include "dialog_tip.h"
int Dialog_setBit::frac_bit=10;
int Number::FracBit=10;
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("Calculator");
    setWindowIcon(QIcon("://ICON.jpg"));
    //标记按钮——数字0~9
    NumGroup=new QPushButton*[10]{ui->pushButton_10,ui->pushButton,ui->pushButton_2,ui->pushButton_3,ui->pushButton_4,ui->pushButton_5,ui->pushButton_6,ui->pushButton_7,ui->pushButton_8,ui->pushButton_9};
    //标记文本框——操作数1、操作数2、结果、符号、等号
    TextGroup=new QTextBrowser*[5]{ui->textBrowser,ui->textBrowser_4,ui->textBrowser_3,ui->textBrowser_5,ui->textBrowser_2};
    //标记按钮——运算符 + - * / . =
    SignGroup=new QPushButton*[6]{ui->pushButton_13,ui->pushButton_14,ui->pushButton_12,ui->pushButton_15,ui->pushButton_16,ui->pushButton_11};
    //标记按钮——Back、AC
    BackGroup=new QPushButton*[2]{ui->pushButton_17,ui->pushButton_18};

    rightShowText(0,"0");                                       //初始化操作数1
    /*数字识别*/
    for(int i=0;i<10;i++){
        NumGroup[i]->setShortcut(NumKeys[i]);                   //绑定键盘和按钮——数字0~9
        connect(NumGroup[i],&QPushButton::clicked,this, [=](){
            if(DB->mode=='=')                                   //若已按下等号，则清空所有数据
                clearAll();
            DB->str += QString::fromUtf8(std::to_string(i));    //转化输入的数字为QString
            DB->input = true;                                   //标记已输入数字
            showNumber();                                       //更新文本框
            DB->ACPressed = false;                              //标记未按下AC
        });
    }
    connect(SignGroup[5],&QPushButton::clicked,this, [=](){     //响应小数点的输入
        if(DB->DotExit){                                       //防止重复输入小数点
            QMessageBox(QMessageBox::Warning,"Warning","You can only input one dot",QMessageBox::Ok).exec();
            return;
        }
        if(DB->str == "")                                   //若未输入数字，则在前面加0
            DB->str = "0";
        DB->str += ".";
        DB->DotExit = true;                                 //标记已输入小数点
        showNumber();
    });

    /*符号识别*/
    for(int i=0;i<6;i++)                                        //绑定键盘和按钮——运算符 + - * / . =
        SignGroup[i] -> setShortcut(SignKeys[i]);

    for(int i=1;i<4;i++)                                        //响应 +、*、/ 对应的运算操作
        connect(SignGroup[i+1],&QPushButton::clicked,this,[=](){ operation(Signs[i]); });
    connect(SignGroup[1],&QPushButton::clicked,this, [=](){     //响应 - ，区别负号和减号
        if(DB->str == ""){                                        //若未输入数字，则输入负号
            DB->str += "-";
            showNumber();
        }
        else
            operation(Signs[0]);                                //若已输入数字，则输入减号
    });
    connect(SignGroup[0],&QPushButton::clicked,this, [=](){     //绑定键盘和按钮—— =
        if(DB->str == "")                                       //若未输入数字，则不进行计算
            return;
        DB->Num2->get(DB->str);                                 //将输入的数字转化为操作数2
        calculate();
        DB->mode = '=';                                        //标记已按下等号
    });

    /*其他交互识别*/
    BackGroup[0]->setShortcut(BackKeys[0]);                    //绑定键盘和按钮——Back
    connect(BackGroup[0],&QPushButton::clicked,this, [=](){
        if(DB->str!=""){
            DB->str=DB->str.left(DB->str.length()-1);          //删除最后一个字符
            showNumber();
        }
    });
    BackGroup[1]->setShortcut(BackKeys[1]);                    //绑定键盘和按钮——AC
    connect(BackGroup[1],&QPushButton::clicked,this, [=](){
        if(!DB->ACPressed){
            DB->str="";                                        //清空输入的数字
            DB->ACPressed=true;                                //标记已按下AC
            showNumber();
        }
        else
            clearAll();
    });

    /*菜单交互*/
    connect(ui->action11,&QAction::triggered,this,[=](){        //绑定菜单栏——修改计算精度
        Dialog_setBit *setbit=new Dialog_setBit(this);
        connect(setbit,&QDialog::accepted,setbit,[=](){
            if(Dialog_setBit::frac_bit>=0){
                Number::FracBit=Dialog_setBit::frac_bit;
                ui->statusbar->showMessage("小数位数更改成功！！！",3000);
            }
            else
                ui->statusbar->showMessage("小数位数更改失败！！！",3000);
            setbit->close();
        });
        dealDialog(setbit);
    });
    connect(ui->action21,&QAction::triggered,this,[=](){      //绑定菜单栏——帮助
        Dialog_Tip *tip=new Dialog_Tip(this);
        dealDialog(tip);
    });
    connect(ui->action22,&QAction::triggered,this,[=](){       //绑定菜单栏——关于
        Dialog_Help *help=new Dialog_Help(this);
        dealDialog(help);
    });
}



MainWindow::~MainWindow()                           //析构函数,释放内存
{
    delete ui;
    delete DB;
    delete[] TextGroup;
    delete[] SignGroup;
    delete[] NumGroup;
    delete[] BackGroup;
}
void MainWindow::calculate(){
    if(DB->calculate())
        return;
    strBuffer=DB->str;
    rightShowText(2,strBuffer);                       //显示结果
    refresh(DB);
    DB->Num1->get(strBuffer);
    DB->inSecond=true;                                //切换到第二行
}
//判断符号含义
void MainWindow::operation(const QChar &mode){
    if((DB->str==""||DB->str=="-") && !DB->inSecond){   //防止未输入数字就按下运算符
        rightShowText(0,"Please input a number first");
        refresh(DB);
        return;
    }
    if(DB->inSecond){                                   //判断是否为连续运算。若已按过运算符，则直接计算
        if(DB->input){                                          //若DB->str已有输入的数字
            DB->Num2->get(DB->str);                             //将输入的数字转化为操作数2
            calculate();
        }
        DB->mode=mode;
        rightShowText(0,strBuffer);                            //显示前一个运算的结果
        makeBlank(1,2);
        showSign(mode);
    }
    else{                                                      //常规运算
        showSign(mode);
        DB->Num1->get(DB->str);                                //将输入的数字转化为操作数1
    }
    DB->mode=mode;                                             //上传运算符到DB
    DB->DotExit=false;                                         //重置输入状态
    DB->str="";
    DB->inSecond=true;
}
void MainWindow::showNumber(){                    //更新文本框，显示输入的数字
    if(DB->inSecond){                       //更新第二行
        rightShowText(1,DB->str);
    }
    else{                                   //更新第一行
        rightShowText(0,DB->str);
        makeBlank(1,2);
    }
}
void MainWindow::clearAll(){                //AC的模式二，清空所有数据
    strBuffer="";
    makeBlank(0,5);
    refresh(DB);
}

void MainWindow::showSign(const QChar &ch){      //显示运算符
    TextGroup[3]->setText(" ");
    TextGroup[3]->append(ch);
}

void MainWindow::makeBlank(int start,int num){   //清空文本框
    for(int i=start;i<start+num;i++)
        TextGroup[i]->setText("");
}

void MainWindow::rightShowText(int i,QString qstr){ //显示右对齐的文本
    TextGroup[i]->setText(qstr);
    TextGroup[i]->setAlignment(Qt::AlignRight);
}
template<typename T>void MainWindow::dealDialog(T t){
    t->exec();
    delete t;
    t=nullptr;
}
