#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

extern bool isOnLiveCD;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void onbrowseButtonclicked();
    void onbrowseThemeButtonclicked();
    void onrepairButtonclicked();
    void onDiskSelected(int index);
    void onPartitionButtonClicked();
    void onUmountButtonClicked();

private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
