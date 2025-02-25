#include "partitiondialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QRegularExpression>

PartitionDialog::PartitionDialog(QWidget *parent) : QDialog(parent) {
    // 设置窗口标题
    setWindowTitle("Select Partitions");
    QVBoxLayout *layout = new QVBoxLayout(this);

    // 添加分区选择
    layout->addWidget(new QLabel("Select Root File System Partition:"));
    rootPartitionCombo = new QComboBox(this);
    layout->addWidget(rootPartitionCombo);
    layout->addWidget(new QLabel("Select EFI Partition:"));
    efiPartitionCombo = new QComboBox(this);
    layout->addWidget(efiPartitionCombo);

    // 添加确认和取消按钮
    QPushButton *confirmButton = new QPushButton("Confirm", this);
    QPushButton *cancelButton = new QPushButton("Cancel", this);
    layout->addWidget(confirmButton);
    layout->addWidget(cancelButton);

    // 连接按钮信号和槽
    connect(confirmButton, &QPushButton::clicked, this, &PartitionDialog::onConfirm);
    connect(cancelButton, &QPushButton::clicked, this, &PartitionDialog::reject);

    // 获取分区列表并填充 ComboBox
    populatePartitions();
}

QString PartitionDialog::getRootPartition() const {
    return rootPartition;
}

QString PartitionDialog::getEfiPartition() const {
    return efiPartition;
}

void PartitionDialog::onConfirm() {
    rootPartition = rootPartitionCombo->currentText();
    efiPartition = efiPartitionCombo->currentText();
    mountPartitions(); // 按下确认后挂载分区
    accept(); // 关闭对话框并返回 QDialog::Accepted
}

void PartitionDialog::populatePartitions() {
    // 使用 lsblk 命令获取分区列表和文件系统信息
    QProcess process;
    process.start("lsblk", QStringList() << "-o" << "NAME,TYPE,FSTYPE" << "-n" << "-l");
    process.waitForFinished();

    // 读取命令输出
    QString output = process.readAllStandardOutput();
    QStringList partitions = output.split("\n", Qt::SkipEmptyParts);

    // 定义允许的根文件系统类型
    QStringList allowedRootFileSystems = {"ext4", "ext3", "ext2", "btrfs", "jfs", "xfs", "f2fs"};

    for (const QString &line : partitions) {
        QStringList fields = line.split(" ", Qt::SkipEmptyParts);
        if (fields.size() >= 3) {
            QString name = fields[0]; // 分区名称（如 sda1）
            QString type = fields[1]; // 分区类型（如 part）
            QString fsType = fields[2]; // 文件系统类型（如 ext4）

            // 如果类型是 "part" 且文件系统是允许的根文件系统类型，则添加到根分区 ComboBox
            if (type == "part" && allowedRootFileSystems.contains(fsType)) {
                QString partitionPath = "/dev/" + name; // 构造完整分区路径
                qDebug() << "Adding root partition:" << partitionPath;
                rootPartitionCombo->addItem(partitionPath);
            }

            // 如果类型是 "part" 且文件系统是 "vfat"，则添加到 EFI 分区 ComboBox
            if (type == "part" && (fsType == "vfat" || fsType == "FAT")) {
                QString partitionPath = "/dev/" + name; // 构造完整分区路径
                qDebug() << "Adding EFI partition:" << partitionPath;
                efiPartitionCombo->addItem(partitionPath);
            }
        }
    }
}

void PartitionDialog::mountPartitions() {
    // 获取根分区的文件系统类型
    QString rootFsType = getFsType(rootPartition);

    // 挂载根分区
    QProcess process;
    QStringList mountArgs;
    if (rootFsType == "btrfs") {
        mountArgs << "mount" << "-t" << "btrfs" << "-o" << "subvol=@,defaults" << rootPartition << "/mnt";
    } else {
        mountArgs << "mount" << rootPartition << "/mnt";
    }

    process.start("pkexec", mountArgs);
    process.waitForFinished();

    // 检查是否成功挂载根分区
    if (process.exitCode() != 0) {
        qDebug() << "Failed to mount root partition.";
        return;
    }

    process.start("pkexec", QStringList() << "bash" << "-c" << "mount --bind /dev /mnt/dev && mount --bind /proc /mnt/proc && mount --bind /sys /mnt/sys && arch-chroot /mnt mount -a");
    process.waitForFinished();

    // 获取 EFI 分区的 UUID
    QString efiUUID = getUUID(efiPartition);

    // 根据 UUID 查找挂载点并挂载 EFI 分区
    QString efiMountPoint = getMountPointFromFstab(efiUUID);
    if (!efiMountPoint.isEmpty()) {
        // 使用 pkexec 挂载 EFI 分区
        process.start("pkexec", QStringList() << "mount" << efiPartition << efiMountPoint);
        process.waitForFinished();
    }
}





QString PartitionDialog::getFsType(const QString &partition) {
    QProcess process;
    process.start("lsblk", QStringList() << "-o" << "NAME,FSTYPE" << "-n" << partition);
    process.waitForFinished();
    QString output = process.readAllStandardOutput().trimmed();
    QStringList fields = output.split(" ", Qt::SkipEmptyParts);
    if (fields.size() >= 2) {
        return fields[1]; // 返回文件系统类型
    }
    return "";
}

QString PartitionDialog::getUUID(const QString &partition) {
    QProcess process;
    process.start("lsblk", QStringList() << "-o" << "NAME,UUID" << "-n" << partition);
    process.waitForFinished();
    QString output = process.readAllStandardOutput().trimmed();  // 去掉末尾的换行符
    QStringList fields = output.split(" ", Qt::SkipEmptyParts);  // 按空格分割
    if (fields.size() >= 2) {
        return fields[1]; // 返回 UUID
    }
    return "";
}



QString PartitionDialog::getMountPointFromFstab(const QString &uuid) {
    QFile fstab("/mnt/etc/fstab");
    if (fstab.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&fstab);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.contains(uuid)) {
                // 使用 QRegularExpression 来处理空白字符
                QRegularExpression re("\\s+");
                QStringList fields = line.split(re, Qt::SkipEmptyParts);
                if (fields.size() >= 2) {
                    return "/mnt" + fields[1].simplified(); // 确保去除空白字符
                }
            }
        }
    }
    return ""; // 如果没有找到挂载点，返回空字符串
}



