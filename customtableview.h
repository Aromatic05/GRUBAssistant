#ifndef CUSTOMTABLEVIEW_H
#define CUSTOMTABLEVIEW_H

#include <QTableView>
#include <QDropEvent>
#include <QMimeData>
#include <QStandardItemModel>
#include <QDataStream>

class CustomTableView : public QTableView {
    Q_OBJECT
public:
    explicit CustomTableView(QWidget *parent = nullptr);

protected:
    void dropEvent(QDropEvent *event) override; // 重写 dropEvent
};

#endif // CUSTOMTABLEVIEW_H
