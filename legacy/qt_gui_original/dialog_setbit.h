#ifndef DIALOG_SETBIT_H
#define DIALOG_SETBIT_H
#include<QDialogButtonBox>
#include<QDialog>
#include<QMessageBox>
#include<QPushButton>


namespace Ui {
class Dialog_setBit;
}

class Dialog_setBit : public QDialog
{
    Q_OBJECT
    friend class MainWindow;
public:
    static int frac_bit;
    explicit Dialog_setBit(QWidget *parent = nullptr);
    ~Dialog_setBit();

private:
    Ui::Dialog_setBit *ui;
};

#endif // DIALOG_SETBIT_H
