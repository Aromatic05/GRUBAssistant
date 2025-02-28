#include "partitiondialog.h"
#include "mountpointdialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QRegularExpression>
#include <QMessageBox>
#include <QDateTime>

PartitionDialog::PartitionDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Select Partitions");
    QVBoxLayout *layout = new QVBoxLayout(this);

    // 添加分区选择
    layout->addWidget(new QLabel(QObject::tr("Select Root File System Partition:")));
    rootPartitionCombo = new QComboBox(this);
    layout->addWidget(rootPartitionCombo);
    layout->addWidget(new QLabel(QObject::tr("Select EFI Partition:")));
    efiPartitionCombo = new QComboBox(this);
    layout->addWidget(efiPartitionCombo);

    QPushButton *confirmButton = new QPushButton(QObject::tr("Confirm"), this);
    QPushButton *cancelButton = new QPushButton(QObject::tr("Cancel"), this);
    layout->addWidget(confirmButton);
    layout->addWidget(cancelButton);

    // 连接按钮信号和槽
    connect(confirmButton, &QPushButton::clicked, this, &PartitionDialog::onConfirm);
    connect(cancelButton, &QPushButton::clicked, this, &PartitionDialog::reject);

    // 获取分区列表并填充 ComboBox
    populatePartitions();
}

QString PartitionDialog::getEfiMountPoint() const {
    return mount;
}

QString PartitionDialog::getRootPartition() const {
    return rootPartition;
}

QString PartitionDialog::getEfiPartition() const {
    return efiPartition;
}

void PartitionDialog::populatePartitions() {
    QProcess process;
    process.start("lsblk", QStringList() << "-o" << "NAME,TYPE,FSTYPE" << "-n" << "-l");
    process.waitForFinished();

    QString output = process.readAllStandardOutput();
    QStringList partitions = output.split("\n", Qt::SkipEmptyParts);

    // 定义允许的根文件系统类型
    QStringList allowedRootFileSystems = {"ext4", "ext3", "ext2", "btrfs", "jfs", "xfs", "f2fs"};

    for (const QString &line : partitions) {
        QStringList fields = line.split(" ", Qt::SkipEmptyParts);
        if (fields.size() >= 3) {
            QString name = fields[0]; // 分区名称
            QString type = fields[1]; // 分区类型
            QString fsType = fields[2]; // 文件系统类型

            // 如果类型是 "part" 且文件系统是允许的根文件系统类型，则添加到根分区 ComboBox
            if (type == "part" && allowedRootFileSystems.contains(fsType)) {
                QString partitionPath = "/dev/" + name;
                qDebug() << QObject::tr("Adding root partition:") << partitionPath;
                rootPartitionCombo->addItem(partitionPath);
            }

            // 如果类型是 "part" 且文件系统是 "vfat"，则添加到 EFI 分区 ComboBox
            if (type == "part" && (fsType == "vfat" || fsType == "FAT")) {
                QString partitionPath = "/dev/" + name;
                qDebug() << QObject::tr("Adding EFI partition:") << partitionPath;
                efiPartitionCombo->addItem(partitionPath);
            }
        }
    }
}

bool PartitionDialog::mountPartitions() {
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
        qDebug() << QObject::tr("Failed to mount root partition.");
        return false;
    }

    QFile fstab("/mnt/etc/fstab");
    if (!fstab.exists()) {
        QMessageBox::critical(nullptr, QObject::tr("Error"), QObject::tr("Not a valid linux root partition!"));
        return false;
    }

    /* Using chroot
       If you run chroot directly, below steps are needed before actual chroot.
      First, mount the temporary API filesystems:

      # cd /path/to/new/root
      # mount -t proc /proc proc/
      # mount -t sysfs /sys sys/
      # mount --rbind /dev dev/
      Warning: When using --rbind, some subdirectories of dev/ and sys/ will not be unmountable. Attempting to unmount with umount -l in this situation will break your session, requiring a reboot. If possible, use -o bind instead.
      And optionally:

      # mount --rbind /run run/
      If you are running a UEFI system, you will also need access to EFI variables. Otherwise, when installing GRUB, you will receive a message similar to: UEFI variables not supported on this machine:

      # mount --rbind /sys/firmware/efi/efivars sys/firmware/efi/efivars/
      Next, in order to use an internet connection in the chroot environment, copy over the DNS details:

      # cp /etc/resolv.conf etc/resolv.conf
      Finally, to change root into /path/to/new/root using a bash shell:

      # chroot /path/to/new/root /bin/bash
    */

    // process.start("pkexec", QStringList() << "bash" << "-c" << "mount --rbind /dev /mnt/dev && mount --rbind /proc /mnt/proc && mount --rbind /sys /mnt/sys && arch-chroot /mnt mount -a");
    process.start("pkexec", QStringList()
                                << "bash"
                                << "-c"
                                << "mount --bind /dev /mnt/dev && " // 用rbind会有糟糕的后果
                                   "mount -t proc /proc /mnt/proc && "
                                   "mount -t sysfs /sys /mnt/sys && "
                                   "mount --bind /run /mnt/run && " // 用rbind会有糟糕的后果
                                   "mount --rbind /sys/firmware/efi/efivars /mnt/sys/firmware/efi/efivars && "
                                   "cp /etc/resolv.conf /mnt/etc/resolv.conf && "
                                   "chroot /mnt mount -a");
    process.waitForFinished();

    rooted = true;
    // 获取 EFI 分区的 UUID
    QString efiUUID = getUUID(efiPartition);
    QString efiMountPoint = getMountPointFromFstab(efiUUID);
    mount = efiMountPoint;

    if (!efiMountPoint.isEmpty()) {
        process.start("pkexec", QStringList() << "mount" << efiPartition << efiMountPoint);
        process.waitForFinished();
        return true;
    } else {
        MountPointDialog dialog;
        if (dialog.exec() == QDialog::Accepted) {
            QString mountPoint = dialog.getMountPoint();
            qDebug() << QObject::tr("Enter the mount point:") << mountPoint;
            process.start("pkexec", QStringList() << "mount" << efiPartition << "/mnt" + mountPoint);
            QPushButton *fixButton = new QPushButton("Fix fstab", this);
            QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(this->layout());
            layout->addWidget(fixButton);
            mount = mountPoint;
            uuid = efiUUID;
            connect(fixButton, &QPushButton::clicked, this, &PartitionDialog::onFixButtonClicked);
            process.waitForFinished();
        }
        return false;
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
    QMessageBox::critical(nullptr, QObject::tr("Error"), QObject::tr("We don't find the mount point of your EFI partition!"));
    return ""; // 如果没有找到挂载点，返回空字符串
}

void PartitionDialog::onConfirm() {
    rootPartition = rootPartitionCombo->currentText();
    efiPartition = efiPartitionCombo->currentText();

    if(!rooted) {
        if (mountPartitions()){
            accept();
        }
    } else {
        accept();
    }
}

void PartitionDialog::onFixButtonClicked() {
    // 打开 /etc/fstab 文件
    QFile fstabFile("/mnt/etc/fstab");
    if (!fstabFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QObject::tr("Error"), QObject::tr("Failed to open /etc/fstab."));
        return;
    }

    // 读取文件内容并查找旧 UUID
    QTextStream in(&fstabFile);
    QStringList lines;
    QString old_uuid;
    bool mountpoint_found = false;

    while (!in.atEnd()) {
        QString line = in.readLine();
        lines.append(line);

        // 使用正则表达式匹配挂载点并提取旧 UUID
        QRegularExpression regex(QString("^UUID=([^ ]+)\\s+%1\\s+").arg(QRegularExpression::escape(mount)));
        QRegularExpressionMatch match = regex.match(line);
        if (match.hasMatch()) {
            old_uuid = match.captured(1); // 提取旧 UUID
            mountpoint_found = true;
        }
    }

    fstabFile.close();

    // 如果未找到挂载点，提示错误
    if (!mountpoint_found) {
        QMessageBox::critical(this, QObject::tr("Error"), QString("Mount point '%1' not found in /mnt/etc/fstab.").arg(mount));
        return;
    }

    // 生成备份文件名，包含当前时间戳
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString backupFile = "/mnt/etc/fstab.backup_" + timestamp;

    QString command = QString(
                          "cp /mnt/etc/fstab %1 && "
                          "sed -i 's/\\b%2\\b/%3/g' /mnt/etc/fstab"
                          ).arg(backupFile).arg(old_uuid).arg(uuid);

    // 使用 pkexec 提权执行命令
    QProcess process;
    process.start("pkexec", QStringList() << "sh" << "-c" << command);
    process.waitForFinished(); // 等待命令执行完成

    // 检查命令执行结果
    if (process.exitCode() == 0) {
        QMessageBox::information(this, QObject::tr("Success"), QString("/mnt/etc/fstab has been updated successfully. Backup file: %1.").arg(backupFile));
    } else {
        QMessageBox::critical(this, QObject::tr("Error"), "Failed to update /mnt/etc/fstab: " + process.readAllStandardError());
    }
}
