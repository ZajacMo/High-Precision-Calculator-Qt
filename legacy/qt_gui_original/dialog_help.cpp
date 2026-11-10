#include "dialog_help.h"
#include "ui_dialog_help.h"

Dialog_Help::Dialog_Help(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Dialog_Help)
{
    ui->setupUi(this);
    setWindowTitle("Help");
    setWindowIcon(QIcon("://ICON.jpg"));
}

Dialog_Help::~Dialog_Help()
{
    delete ui;
}
