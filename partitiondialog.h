#ifndef PARTITIONDIALOG_H
#define PARTITIONDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QString>
#include <QLineEdit>  // 引入 QLineEdit

class PartitionDialog : public QDialog {
    Q_OBJECT

public:
    explicit PartitionDialog(QWidget *parent = nullptr);

    // 获取用户选择的根文件系统分区
    QString getRootPartition() const;

    // 获取用户选择的 EFI 分区
    QString getEfiPartition() const;

private slots:
    void onConfirm(); // 确认按钮点击事件

private:
    void populatePartitions(); // 获取分区列表并填充 ComboBox
    QString findEFIMountPoint(const QString &efiPartition); // 查找 EFI 分区挂载点
    QString getFsType(const QString &partition); // 获取分区文件系统类型
    QString getUUID(const QString &partition); // 获取分区 UUID
    QString getMountPointFromFstab(const QString &uuid); // 从 fstab 查找挂载点
    void mountPartitions(); // 挂载分区

    QComboBox *rootPartitionCombo; // 根文件系统分区选择
    QComboBox *efiPartitionCombo;  // EFI 分区选择
    QLineEdit *efiMountPointEdit;  // 用户输入的 EFI 挂载点
    QString rootPartition;         // 用户选择的根文件系统分区
    QString efiPartition;          // 用户选择的 EFI 分区
};

#endif // PARTITIONDIALOG_H
