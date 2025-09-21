#ifndef DIALOG_HELP_H
#define DIALOG_HELP_H

#include <QDialog>

namespace Ui {
class Dialog_Help;
}

class Dialog_Help : public QDialog
{
    Q_OBJECT

public:
    explicit Dialog_Help(QWidget *parent = nullptr);
    ~Dialog_Help();

private:
    Ui::Dialog_Help *ui;
};

#endif // DIALOG_HELP_H
