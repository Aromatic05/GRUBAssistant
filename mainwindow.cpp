#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "partitiondialog.h"
#include <QFileDialog>
#include <QProcess>
#include <QCoreApplication>
#include <QMessageBox>
#include <QDebug>
#include <QTextBrowser>
#include <QStringList>
#include <QRegularExpression>

bool requestRootPermissions()
{
    QProcess process;
    process.start("pkexec", QStringList() << "true");
    process.waitForFinished();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        // 如果未成功获得root权限，弹出提示并退出程序
        QMessageBox::critical(nullptr, "Error", "Root privileges are required to run this application.");
        return false;
    } else {
        qDebug() << "Root privileges granted.";
        return true;
    }
}

bool isFATPartition(const QString& mountPoint)
{
    // 获取挂载点对应的分区
    QProcess findmntProcess;
    findmntProcess.start("findmnt", QStringList() << "-n" << "-o" << "SOURCE" << mountPoint);
    findmntProcess.waitForFinished();
    QString device = findmntProcess.readAllStandardOutput().trimmed();

    if (device.isEmpty()) {
        qWarning() << "无法找到挂载点对应的分区！";
        return false;
    }

    // 使用 blkid 检查文件系统类型
    QProcess blkidProcess;
    blkidProcess.start("blkid", QStringList() << device);
    blkidProcess.waitForFinished();
    QString output = blkidProcess.readAllStandardOutput();

    // 判断文件系统是否为 FAT
    if (output.contains("TYPE=\"vfat\"") || output.contains("TYPE=\"fat32\"")) {
        return true;  // 是 FAT 分区
    }

    return false;  // 不是 FAT 分区
}

bool isLiveCD() {
    QFile file("/proc/mounts");  // 读取挂载信息
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList fields = line.split(" ");

        if (fields[1] == "/" && (fields[2] == "squashfs" || fields[2] == "tmpfs")) {
            return true;  // 可能是LiveCD环境
        }

        if (fields[1] == "/" && fields[3].contains("ro")) {
            return true;  // 只读挂载
        }
    }

    return false;  // 不是LiveCD环境
}

QString getPartitionTableFormat(const QString &disk) {
    QProcess process;
    QString command = "lsblk";
    QStringList arguments = {"-o", "PTTYPE", "-n", "-d", disk};

    process.start(command, arguments);
    process.waitForFinished();

    // 读取命令输出
    QString output = process.readAllStandardOutput().trimmed();
    if (output.isEmpty()) {
        return "Unknown";
    }
    return output;
}

QStringList getDiskList() {
    QStringList disks;
    QProcess process;
    process.start("lsblk", QStringList() << "-o" << "NAME,TYPE" << "-n" << "-l");
    process.waitForFinished();

    QString output = process.readAllStandardOutput();
    QStringList lines = output.split("\n", Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        QStringList parts = line.split(" ", Qt::SkipEmptyParts);
        if (parts.size() == 2 && parts[1] == "disk") {
            QString device = "/dev/" + parts[0];

            // 排除掉 zram 设备
            if (!device.startsWith("/dev/zram")) {
                disks.append(device);
            }
        }
    }

    return disks;
}


void showFormattedLsblkOutput(QTextBrowser *textBrowser) {
    QProcess process;
    process.start("lsblk", QStringList() << "-o" << "NAME,TYPE,SIZE,FSTYPE,MOUNTPOINT" << "-n" << "-l");
    process.waitForFinished();

    QString output = process.readAllStandardOutput();
    QString formattedOutput;
    QStringList lines = output.split("\n", Qt::SkipEmptyParts);

    // 存储磁盘信息
    QMap<QString, QString> diskSizes;
    QMap<QString, QString> diskFsTypes;
    QMap<QString, QString> diskMountPoints;

    // 存储分区信息
    QMap<QString, QList<QString>> devicePartitions;
    QMap<QString, QString> partitionSizes;
    QMap<QString, QString> partitionFsTypes;
    QMap<QString, QString> partitionMountPoints;

    for (const QString &line : lines) {
        QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 3) continue; // 至少需要 NAME, TYPE, SIZE

        QString name = parts[0];
        QString type = parts[1];
        QString size = parts[2];
        QString fsType = (parts.size() >= 4) ? parts[3] : "N/A";
        QString mountPoint = (parts.size() >= 5) ? parts[4] : "N/A";
        QString deviceName = "/dev/" + name;
        if (type == "disk") {
            diskSizes[deviceName] = size;
            diskFsTypes[deviceName] = fsType;
            diskMountPoints[deviceName] = mountPoint;
        } else if (type == "part") {
            // 确定父设备名称
            QString parentDevice;
            QRegularExpression diskRegex("^(.*?\\D+)\\d+$");
            QRegularExpressionMatch match = diskRegex.match(name);

            if (match.hasMatch()) {
                parentDevice = "/dev/" + match.captured(1);
            } else {
                continue; // 无法确定父设备，跳过
            }

            devicePartitions[parentDevice].append(deviceName);
            partitionSizes[deviceName] = size;
            partitionFsTypes[deviceName] = fsType;
            partitionMountPoints[deviceName] = mountPoint;
        }
    }

    // 收集所有磁盘（合并有分区和无分区的）
    QSet<QString> allDisks;
    for (const QString &disk : diskSizes.keys()) allDisks.insert(disk);
    for (const QString &disk : devicePartitions.keys()) allDisks.insert(disk);
    QStringList sortedDisks = allDisks.values();
    std::sort(sortedDisks.begin(), sortedDisks.end());

    foreach (const QString &disk, sortedDisks) {
        QString size = diskSizes.value(disk, "N/A");
        QString fsType = diskFsTypes.value(disk, "N/A");
        QString mountPoint = diskMountPoints.value(disk, "N/A");

        if (!disk.endsWith('p')) {
            formattedOutput += QString("<font color='green'>%1 (Disk)  Size: %2  FS: %3  Mount: %4</font><br>")
            .arg(disk, size, fsType, mountPoint);
        }

        if (devicePartitions.contains(disk)) {
            QStringList partitions = devicePartitions.value(disk);
            for (int i = 0; i < partitions.size(); ++i) {
                QString partition = partitions[i];
                QString indent = (i == partitions.size() - 1) ? "└─" : "├─";
                QString partSize = partitionSizes.value(partition, "N/A");
                QString partFsType = partitionFsTypes.value(partition, "N/A");
                QString partMountPoint = partitionMountPoints.value(partition, "N/A");

                formattedOutput += QString("%1<font color='white'>%2 (Part)  Size: %3  FS: %4  Mount: %5</font><br>")
                                       .arg(indent, partition, partSize, partFsType, partMountPoint);
            }
        }
    }

    textBrowser->setHtml("<pre>" + formattedOutput + "</pre>");
}

// 槽函数
void MainWindow::onbrowseButtonclicked()
{
    QString filePath = QFileDialog::getExistingDirectory(this, tr("Select EFI Partition"));
    if (!filePath.isEmpty()) {
        ui->efiInput->setText(filePath);
    }
}

void MainWindow::onbrowseThemeButtonclicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, tr("Select GRUB Theme"), "", tr("GRUB Theme Files (*)"));
    if (!filePath.isEmpty()) {
        ui->themeInput->setText(filePath);
    }
}

void MainWindow::onrepairButtonclicked()
{
    QString efiPartition = ui->efiInput->text();  // 获取用户输入的挂载点

    if (isFATPartition(efiPartition)) {
        qDebug() << "这是一个EFI分区";
        ui->warningLabel->clear();  // 清空警告标签的文本
        ui->warningLabel->setStyleSheet("");  // 恢复默认样式

        QString command = "pkexec";
        QStringList arguments;
        arguments << "grub-install"
                  << "--target=x86_64-efi"
                  << "--efi-directory=" + efiPartition
                  << "--bootloader-id=ARCH";

        qDebug() << "执行的命令：" << command << arguments.join(" ");

        // 创建QProcess来执行命令
        QProcess *process = new QProcess(this);

        // 连接错误信号
        connect(process, &QProcess::errorOccurred, [](QProcess::ProcessError error) {
            qDebug() << "QProcess error occurred:" << error;
        });

        // 连接信号与槽来捕获标准输出和错误输出
        connect(process, &QProcess::readyReadStandardOutput, [process]() {
            qDebug() << "标准输出: " << process->readAllStandardOutput();
        });

        connect(process, &QProcess::readyReadStandardError, [process]() {
            qDebug() << "错误输出: " << process->readAllStandardError();
        });

        // 连接完成信号
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
                        qDebug() << "GRUB安装失败！";
                        ui->warningLabel->setText("错误：GRUB安装失败！");
                        ui->warningLabel->setStyleSheet("QLabel { color : red; }");
                    } else {
                        qDebug() << "GRUB安装成功！";
                        ui->warningLabel->setText("GRUB安装成功！");
                        ui->warningLabel->setStyleSheet("QLabel { color : green; }");
                    }
                    process->deleteLater();  // 清理QProcess对象
                });

        process->start(command, arguments);

        // 等待进程启动（最多等待 5 秒）
        if (!process->waitForStarted(5000)) {
            qDebug() << "Failed to start process:" << process->errorString();
            ui->warningLabel->setText("错误：无法启动GRUB安装进程！");
            ui->warningLabel->setStyleSheet("QLabel { color : red; }");
            process->deleteLater();  // 清理QProcess对象
            return;  // 退出函数
        }

        // 确保进程成功启动
        if (!process->waitForStarted()) {
            qDebug() << "QProcess 启动失败: " << process->errorString();
        }

        // 等待进程执行完成
        if (!process->waitForFinished()) {
            qDebug() << "QProcess 执行失败";
        }

    } else {
        // 显示红色警告，提醒用户选择的是非EFI分区
        ui->warningLabel->setText("警告：请选择一个有效的EFI分区！");
        ui->warningLabel->setStyleSheet("QLabel { color : red; }");
    }
}

void MainWindow::onPartitionButtonClicked() {
    // 创建对话框
    PartitionDialog dialog(this);

    // 显示对话框并等待用户操作
    if (dialog.exec() == QDialog::Accepted) {
        // 获取用户选择的分区
        QString rootPartition = dialog.getRootPartition();
        QString efiPartition = dialog.getEfiPartition();

        // 打印或处理用户选择的分区
        qDebug() << "Root Partition:" << rootPartition;
        qDebug() << "EFI Partition:" << efiPartition;

        // // 更新 UI 或其他逻辑
        // ui->rootPartitionLabel->setText("Root: " + rootPartition);
        // ui->efiPartitionLabel->setText("EFI: " + efiPartition);
    }
}

void MainWindow::onUmountButtonClicked() {
    QProcess process;
    process.start("pkexec", QStringList() << "umount" << "-R" << "/mnt");
    process.waitForFinished();
    if (process.exitCode() != 0) {
        qDebug() << "Failed to umount root partition.";
        return;
    }
    qDebug() << "Umount Success!";

}

void MainWindow::onDiskSelected(int index) {
    // 获取当前选择的磁盘名称
    QString selectedDisk = ui->DiskcomboBox->itemText(index);

    // 获取磁盘的分区表格式
    QString partitionFormat = getPartitionTableFormat(selectedDisk);

    // 更新 tableLabel
    ui->tableLabel->setText(QString("%1").arg(partitionFormat));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    // 设置界面
    ui->setupUi(this);

    // 设置窗口不可调整大小
    // setFixedSize(this->size());

    bool isOnLiveCD = false;
    if (isLiveCD()) {
        this->setWindowTitle("GRUB Assistant -- LiveCD mode");
        isOnLiveCD = true;
    } else {
        this->setWindowTitle("GRUB Assistant -- Local mode");
    }

    showFormattedLsblkOutput(ui->LogBrowser);
    ui->efiInput->setText("/boot/efi");
    ui->themeInput->setText("/usr/share/grub/themes/vimix-color-1080p/theme.txt");
    ui->timeoutInput->setText("5");

    // 将磁盘信息添加到 ComboBox 中，并获取分区表类型
    QStringList diskList = getDiskList();
    ui->DiskcomboBox->addItems(diskList);
    connect(ui->DiskcomboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onDiskSelected);
    int defaultIndex = 0;
    ui->DiskcomboBox->setCurrentIndex(defaultIndex);
    onDiskSelected(defaultIndex);


    // 处理浏览按钮
    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::onbrowseButtonclicked);
    connect(ui->browseThemeButton, &QPushButton::clicked, this, &MainWindow::onbrowseThemeButtonclicked);

    // 处理repair按钮
    connect(ui->repair1Button, &QPushButton::clicked, this, &MainWindow::onrepairButtonclicked); // 执行修复UEFI GRUB代码

    // 处理Mount Disk按钮
    connect(ui->mountButton, &QPushButton::clicked, this, &MainWindow::onPartitionButtonClicked);

    // 处理Unmount按钮
    connect(ui->umountButton, &QPushButton::clicked, this, &MainWindow::onUmountButtonClicked);


}

MainWindow::~MainWindow()
{
    delete ui;
}
