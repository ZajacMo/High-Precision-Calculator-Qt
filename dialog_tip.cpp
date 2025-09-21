#include "dialog_tip.h"
#include "ui_dialog_tip.h"

Dialog_Tip::Dialog_Tip(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Dialog_Tip)
{
    ui->setupUi(this);
    setWindowTitle("Tip");
    setWindowIcon(QIcon("://ICON.jpg"));
}

Dialog_Tip::~Dialog_Tip()
{
    delete ui;
}
