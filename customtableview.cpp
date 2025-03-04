#include "customtableview.h"

CustomTableView::CustomTableView(QWidget *parent) : QTableView(parent) {}

void CustomTableView::dropEvent(QDropEvent *event) {
    QModelIndex targetIndex = indexAt(event->position().toPoint());
    if (!targetIndex.isValid()) {
        return;
    }

    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasFormat("application/x-qabstractitemmodeldatalist")) {
        return;
    }

    QByteArray itemData = mimeData->data("application/x-qabstractitemmodeldatalist");
    QDataStream stream(&itemData, QIODevice::ReadOnly);
    int sourceRow, col;
    QMap<int, QVariant> roleDataMap;
    stream >> sourceRow >> col >> roleDataMap;

    QStandardItemModel *model = qobject_cast<QStandardItemModel *>(this->model());
    if (!model || sourceRow < 0 || sourceRow >= model->rowCount()) {
        return;
    }

    int targetRow = targetIndex.row();

    // 先备份被移动的行数据
    QList<QStandardItem*> sourceItems;
    for (int i = 0; i < model->columnCount(); ++i) {
        QStandardItem *item = model->item(sourceRow, i);
        sourceItems.append(item ? item->clone() : nullptr);
    }

    // 计算插入行的位置
    int newRow;
    if (sourceRow < targetRow) {
        // 从上向下拖动时，插入到目标行下方
        newRow = targetRow + 1;
        model->insertRow(newRow);
        for (int i = 0; i < sourceItems.size(); ++i) {
            model->setItem(newRow, i, sourceItems[i]);
        }
        model->removeRow(sourceRow);
    } else {
        // 从下向上拖动时，插入到目标行上方
        newRow = targetRow;
        model->insertRow(newRow);
        for (int i = 0; i < sourceItems.size(); ++i) {
            model->setItem(newRow, i, sourceItems[i]);
        }
        model->removeRow(sourceRow + 1);
    }

    event->acceptProposedAction();
}



