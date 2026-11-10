#ifndef DIALOG_TIP_H
#define DIALOG_TIP_H

#include <QDialog>

namespace Ui {
class Dialog_Tip;
}

class Dialog_Tip : public QDialog
{
    Q_OBJECT

public:
    explicit Dialog_Tip(QWidget *parent = nullptr);
    ~Dialog_Tip();

private:
    Ui::Dialog_Tip *ui;
};

#endif // DIALOG_TIP_H
