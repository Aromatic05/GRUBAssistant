#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QStandardItemModel>
#include "customtableview.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    QMap<QString, QString> grub_variables;
    QString liveEFI;
    QString efiEntryID;

public slots:
    void onbrowseButtonclicked();
    void onbrowseThemeButtonclicked();
    void onrepairButtonclicked();
    void onbiosrepairButtonclicked();
    void onChrootRepairButtonClicked();
    void onDiskSelected(int index);
    void onPartitionButtonClicked();
    void onUmountButtonClicked();
    void onLoadButtonClicked();
    void onSaveButtonClicked();
    void onUpdateButtonClicked();
    void onChrootUpdateButtonClicked();
    void refreshBootEntries();
    void onRowsMoved(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row);
    void onDelButtonClicked();
    void onSavButtonClicked();
    void onAddButtonClicked();
    void onRefreshButtonClicked();

private:
    Ui::MainWindow *ui;
    QStandardItemModel *model;
    CustomTableView *tableView;
};
#endif // MAINWINDOW_H
