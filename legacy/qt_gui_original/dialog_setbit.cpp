#include "dialog_setbit.h"
#include "ui_dialog_setbit.h"
Dialog_setBit::Dialog_setBit(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Dialog_setBit)
{
    ui->setupUi(this);
    setWindowTitle("set lenth");
    setWindowIcon(QIcon("://ICON.jpg"));
    ui->lineEdit->setText(QString::number(frac_bit));
    ui->lineEdit->setAlignment(Qt::AlignRight);
    ui->lineEdit->selectAll();
    connect(ui->save,&QPushButton::clicked,this,&QDialog::accepted);
    connect(ui->cancle,&QPushButton::clicked,this,&QDialog::reject);
    connect(this,&QDialog::accepted,this, [=](){
        int bit=ui->lineEdit->text().toInt();
        if(bit<0){
            QMessageBox::warning(this,"Warning","Please input a number greater than 0");
            return;
        }
        else{
            frac_bit=bit;
        }
    });
}

Dialog_setBit::~Dialog_setBit()
{

    delete ui;
}
