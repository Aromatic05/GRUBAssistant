#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "partitiondialog.h"
#include "customtableview.h"
#include "configure.h"
#include <QFileDialog>
#include <QDir>
#include <QProcess>
#include <QCoreApplication>
#include <QMessageBox>
#include <QDebug>
#include <QTextBrowser>
#include <QStringList>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QTableView>
#include <QHeaderView>
#include <QMessageBox>
#include <QToolTip>

bool requestRootPermissions()
{
    QProcess process;
    process.start("pkexec", QStringList() << "true");
    process.waitForFinished();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QMessageBox::critical(nullptr, QObject::tr("Error"), QObject::tr("Root privileges are required to run this application."));
        return false;
    } else {
        qDebug() << QObject::tr("Root privileges granted.");
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
        qWarning() << QObject::tr("The partition corresponding to the mount point cannot be found.！");
        return false;
    }

    // 使用 lsblk 检查文件系统类型
    QProcess Process;
    Process.start("lsblk", QStringList() << "-o" << "FSTYPE" << "-n" << device);
    Process.waitForFinished();
    QString output = Process.readAllStandardOutput();

    // 判断文件系统是否为 FAT
    if (output.contains("vfat") || output.contains("fat32")) {
        return true;
    }

    return false;
}

bool isLiveCD() {
    QFile file("/proc/mounts");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList fields = line.split(" ");

        if (fields[1] == "/" && (fields[2] == "squashfs" || fields[2] == "tmpfs")) {
            return true;
        }

        if (fields[1] == "/" && fields[3].contains("ro")) {
            return true;
        }
    }

    return false;
}

QString getPartitionTableFormat(const QString &disk) {
    QProcess process;
    QString command = "lsblk";
    QStringList arguments = {"-o", "PTTYPE", "-n", "-d", disk};

    process.start(command, arguments);
    process.waitForFinished();

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

                formattedOutput += QString("%1<font color='red'>%2 (Part)  Size: %3  FS: %4  Mount: %5</font><br>")
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
    if (ui->idlineEdit->text().isEmpty()) {
        efiEntryID = "ARCH";
    } else {
        efiEntryID = ui->idlineEdit->text();
    }

    QString efiPartition = ui->efiInput->text();  // 获取用户输入的挂载点

    if (isFATPartition(efiPartition)) {
        qDebug() << efiPartition << QObject::tr("is an EFI partition");
        ui->warningLabel->clear();
        ui->warningLabel->setStyleSheet("");

        QString command = "pkexec";
        QStringList arguments;
        arguments << "grub-install"
                  << "--target=x86_64-efi"
                  << "--efi-directory=" + efiPartition
                  << "--bootloader-id=" + efiEntryID;

        qDebug() << QObject::tr("Command:") << command << arguments.join(" ");

        // 创建QProcess来执行命令
        QProcess *process = new QProcess(this);

        // 连接错误信号
        connect(process, &QProcess::errorOccurred, [](QProcess::ProcessError error) {
            qDebug() << QObject::tr("QProcess error occurred:") << error;
        });

        // 连接信号与槽来捕获标准输出和错误输出
        connect(process, &QProcess::readyReadStandardOutput, [process]() {
            QByteArray output = process->readAllStandardOutput();
            qDebug() << QObject::tr("Stdout: ") << QString::fromUtf8(output);
        });

        connect(process, &QProcess::readyReadStandardError, [process]() {
            QByteArray errorOutput = process->readAllStandardError();
            qDebug() << QObject::tr("Stderr: ") << QString::fromUtf8(errorOutput);
        });

        // 连接完成信号
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
                        qDebug() << QObject::tr("GRUB installation failed!");
                        ui->warningLabel->setText(tr("Error: GRUB installation failed!"));
                        ui->warningLabel->setStyleSheet("QLabel { color : red; }");
                    } else {
                        qDebug() << QObject::tr("GRUB installation successful!");
                        ui->warningLabel->setText(tr("GRUB installation successful!"));
                        ui->warningLabel->setStyleSheet("QLabel { color : green; }");
                    }
                    process->deleteLater();  // 清理QProcess对象
                });

        process->start(command, arguments);

        // 等待进程启动（最多等待 5 秒）
        if (!process->waitForStarted(5000)) {
            qDebug() << "Failed to start process:" << process->errorString();
            ui->warningLabel->setText(tr("Error: Unable to start the GRUB installation process!"));
            ui->warningLabel->setStyleSheet("QLabel { color : red; }");
            process->deleteLater();  // 清理QProcess对象
            return;  // 退出函数
        }

        // 确保进程成功启动
        if (!process->waitForStarted()) {
            qDebug() << QObject::tr("QProcess failed to start: ") << process->errorString();
        }

        // 等待进程执行完成
        if (!process->waitForFinished()) {
            qDebug() << QObject::tr("QProcess execution failed: ");
        }

    } else {
        // 显示红色警告，提醒用户选择的是非EFI分区
        ui->warningLabel->setText(tr("Warning：Please select a valid EFI partition!"));
        ui->warningLabel->setStyleSheet("QLabel { color : red; }");
    }
}

void MainWindow::onbiosrepairButtonclicked()
{
    QString biosDisk = ui->DiskcomboBox->currentText();  // 获取用户输入的挂载点

    ui->warningLabel->clear();
    ui->warningLabel->setStyleSheet("");

    QString command = "pkexec";
    QStringList arguments;
    arguments << "grub-install"
              << "--target=i386-pc"
              << biosDisk;

    qDebug() << QObject::tr("Command: ") << command << arguments.join(" ");

    // 创建QProcess来执行命令
    QProcess *process = new QProcess(this);

    // 连接错误信号
    connect(process, &QProcess::errorOccurred, [](QProcess::ProcessError error) {
        qDebug() << QObject::tr("QProcess error occurred:") << error;
    });

    // 连接信号与槽来捕获标准输出和错误输出
    connect(process, &QProcess::readyReadStandardOutput, [process]() {
        QByteArray output = process->readAllStandardOutput();
        qDebug() << QObject::tr("Stdout: ") << QString::fromUtf8(output);
    });

    connect(process, &QProcess::readyReadStandardError, [process]() {
        QByteArray errorOutput = process->readAllStandardError();
        qDebug() << QObject::tr("Stderr: ") << QString::fromUtf8(errorOutput);
    });

    // 连接完成信号
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus == QProcess::CrashExit || exitCode != 0) {
                    qDebug() << QObject::tr("GRUB installation failed!");
                    ui->warningLabel->setText(tr("Error: GRUB installation failed!"));
                    ui->warningLabel->setStyleSheet("QLabel { color : red; }");
                } else {
                    qDebug() << QObject::tr("GRUB installation successful!");
                    ui->warningLabel->setText(tr("GRUB installation successful!"));
                    ui->warningLabel->setStyleSheet("QLabel { color : green; }");
                }
                process->deleteLater();  // 清理QProcess对象
            });

    process->start(command, arguments);

    // 等待进程启动（最多等待 5 秒）
    if (!process->waitForStarted(5000)) {
        qDebug() << QObject::tr("Failed to start process:") << process->errorString();
        ui->warningLabel->setText(tr("Error: Unable to start the GRUB installation process!"));
        ui->warningLabel->setStyleSheet("QLabel { color : red; }");
        process->deleteLater();  // 清理QProcess对象
        return;  // 退出函数
    }

        // 确保进程成功启动
    if (!process->waitForStarted()) {
        qDebug() << QObject::tr("QProcess failed to start: ") << process->errorString();
    }

    // 等待进程执行完成
    if (!process->waitForFinished()) {
        qDebug() << QObject::tr("QProcess execution failed: ");
    }
}

void MainWindow::onChrootRepairButtonClicked() {
    ui->warningLabel->clear();
    ui->warningLabel->setStyleSheet("");

    if (ui->idlineEdit->text().isEmpty()) {
        efiEntryID = "ARCH";
    } else {
        efiEntryID = ui->idlineEdit->text();
    }

    if (liveEFI.isEmpty()) {
        liveEFI = "/boot/efi"; // 如果为空，设置为默认值 /boot/efi
    } else if (liveEFI.startsWith("/mnt")) {
        liveEFI.remove("/mnt");
        if (!liveEFI.startsWith("/")) {
            liveEFI.prepend("/");
        }
    }
    QString command = "pkexec";
    QStringList arguments;
    arguments << "bash"
              << "-c"
              << "chroot /mnt grub-install --target=x86_64-efi --efi-directory=" + liveEFI + " --bootloader-id=" + efiEntryID;
    qDebug() << QObject::tr("执行的命令：") << command << arguments.join(" ");

    // 创建QProcess来执行命令
    QProcess *process = new QProcess(this);

    // 连接错误信号
    connect(process, &QProcess::errorOccurred, [](QProcess::ProcessError error) {
        qDebug() << QObject::tr("QProcess error occurred:") << error;
    });

    // 连接信号与槽来捕获标准输出和错误输出
    connect(process, &QProcess::readyReadStandardOutput, [process]() {
        QByteArray output = process->readAllStandardOutput();
        qDebug() << QObject::tr("Stdout: ") << QString::fromUtf8(output);
    });

    connect(process, &QProcess::readyReadStandardError, [process]() {
        QByteArray errorOutput = process->readAllStandardError();
        qDebug() << QObject::tr("Stderr: ") << QString::fromUtf8(errorOutput);
    });

    // 连接完成信号
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus == QProcess::CrashExit || exitCode != 0) {
                    qDebug() << QObject::tr("GRUB installation failed!");
                    ui->warningLabel->setText(tr("Error: GRUB installation failed!"));
                    ui->warningLabel->setStyleSheet("QLabel { color : red; }");
                } else {
                    qDebug() << QObject::tr("GRUB installation successful!");
                    ui->warningLabel->setText(tr("GRUB installation successful!"));
                    ui->warningLabel->setStyleSheet("QLabel { color : green; }");
                }
                process->deleteLater();  // 清理QProcess对象
            });

    process->start(command, arguments);

    // 等待进程启动（最多等待 5 秒）
    if (!process->waitForStarted(5000)) {
        qDebug() << QObject::tr("Failed to start process:") << process->errorString();
        ui->warningLabel->setText(tr("Error: Unable to start the GRUB installation process!"));
        ui->warningLabel->setStyleSheet("QLabel { color : red; }");
        process->deleteLater();  // 清理QProcess对象
        return;  // 退出函数
    }

    // 确保进程成功启动
    if (!process->waitForStarted()) {
        qDebug() << QObject::tr("QProcess failed to start: ") << process->errorString();
    }

    // 等待进程执行完成
    if (!process->waitForFinished()) {
        qDebug() << QObject::tr("QProcess execution failed: ");
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
        liveEFI = dialog.getEfiMountPoint();

        // 打印或处理用户选择的分区
        qDebug() << QObject::tr("Root Partition:") << rootPartition;
        qDebug() << QObject::tr("EFI Partition:") << efiPartition;

        // // 更新 UI 或其他逻辑
        // ui->rootPartitionLabel->setText("Root: " + rootPartition);
        // ui->efiPartitionLabel->setText("EFI: " + efiPartition);
    }
}

void MainWindow::onUmountButtonClicked() {
    QProcess process;
    // process.start("pkexec", QStringList() << "bash" << "-c" << "umount --lazy /mnt/dev && umount --lazy /mnt/proc && umount --lazy /mnt/sys && umount -R --lazy /mnt");
    // process.start("pkexec", QStringList() << "umount -R /mnt || umount -R /mnt");
    process.start("pkexec", QStringList() << "bash" << "-c" << "umount -R --lazy /mnt");
    process.waitForFinished();
    if (process.exitCode() != 0) {
        qDebug() << QObject::tr("Failed to umount root partition.");
        return;
    }
    qDebug() << QObject::tr("Umount Success!");

}

void MainWindow::onLoadButtonClicked() {
    grub_variables = parseGrubFile();
    qDebug() << QObject::tr("Load Success!");

    parseGrubMenu(ui->defaultMenuCombo);
    loadGrubUi(ui, grub_variables);
}

void MainWindow::onSaveButtonClicked() {
    updateVariables(ui, grub_variables);
    saveGrubVariables(grub_variables);
    qDebug() << QObject::tr("Save Success!");
}

void MainWindow::onUpdateButtonClicked() {
    QProcess process;
    process.start("pkexec", QStringList() << "grub-mkconfig" << "-o" << "/boot/grub/grub.cfg");
    process.waitForFinished();
    if (process.exitCode() != 0) {
        QMessageBox::critical(nullptr, QObject::tr("Error"), QObject::tr("Failed to update GRUB configuration"));
        return;
    }
    QMessageBox::information(nullptr, QObject::tr("Success"), QObject::tr("GRUB configuration update successfully."));
    qDebug() << QObject::tr("Update Success!");
}

void MainWindow::onChrootUpdateButtonClicked() {
    QProcess process;

    QStringList args;
    args << "-c" << "chroot /mnt grub-mkconfig -o /boot/grub/grub.cfg";

    process.start("pkexec", QStringList() << "bash" << args);
    process.waitForFinished();

    // 检查进程是否成功执行
    if (process.exitCode() != 0) {
        QMessageBox::critical(nullptr, QObject::tr("Error"), QObject::tr("Failed to update GRUB configuration"));
        return;
    }

    QMessageBox::information(nullptr, QObject::tr("Success"), QObject::tr("GRUB configuration update successfully."));
    qDebug() << QObject::tr("Update Success!");
}


void MainWindow::onDiskSelected(int index) {
    // 获取当前选择的磁盘名称
    QString selectedDisk = ui->DiskcomboBox->itemText(index);

    // 获取磁盘的分区表格式
    QString partitionFormat = getPartitionTableFormat(selectedDisk);

    // 更新 tableLabel
    ui->tableLabel->setText(QString("%1").arg(partitionFormat));
}

void MainWindow::onRowsMoved(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row) {
    Q_UNUSED(parent);
    Q_UNUSED(start);
    Q_UNUSED(end);
    Q_UNUSED(destination);

    // 自动向上填充空位
    for (int i = 0; i < model->rowCount(); ++i) {
        if (model->item(i, 0) == nullptr) {
            model->removeRow(i); // 移除空行
            i--; // 调整索引
        }
    }
}

void MainWindow::onDelButtonClicked() {
    // 获取当前选中的行
    QModelIndexList selectedRows = tableView->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) {
        QMessageBox::warning(this, QObject::tr("Delete"), QObject::tr("Please select a boot entry to delete"));
        return;
    }

    // 获取要删除的 Boot Number
    QString bootNumber = model->data(selectedRows.first()).toString();  // 第一列是 Boot Number

    // 执行 pkexec 和 efibootmgr 删除操作
    QProcess process;
    QStringList arguments;
    arguments << "efibootmgr" << "-b" << bootNumber << "-B"; // 删除引导项的参数

    // 使用 pkexec 以管理员身份执行 efibootmgr
    process.start("pkexec", arguments);
    process.waitForFinished();

    // 检查删除操作是否成功
    if (process.exitStatus() == QProcess::NormalExit) {
        QMessageBox::information(this, QObject::tr("Success"), QObject::tr("Boot entry deleted successfully"));
    } else {
        QMessageBox::warning(this, QObject::tr("Failure"), QObject::tr("Failed to delete boot entry"));
    }

    // 删除后刷新引导项
    refreshBootEntries();
}

void MainWindow::onSavButtonClicked() {
    // 获取 BootOrder 列表（从表格中获取）
    QStringList bootOrder;
    for (int row = 0; row < model->rowCount(); ++row) {
        QString bootNumber = model->data(model->index(row, 0)).toString(); // 第一列是 Boot Number
        bootOrder.append(bootNumber);
    }

    // 执行 pkexec 和 efibootmgr 保存 BootOrder
    QProcess process;
    QStringList arguments;
    arguments << "efibootmgr" << "-o" << bootOrder.join(","); // 保存引导顺序的参数

    // 使用 pkexec 以管理员身份执行 efibootmgr
    process.start("pkexec", arguments);
    process.waitForFinished();

    // 检查保存操作是否成功
    if (process.exitStatus() == QProcess::NormalExit) {
        QMessageBox::information(this, QObject::tr("Success"), QObject::tr("Boot order saved successfully"));
    } else {
        QMessageBox::warning(this, QObject::tr("Failure"), QObject::tr("Failed to save boot order"));
    }

    // 保存后刷新引导项
    refreshBootEntries();
}


void MainWindow::onAddButtonClicked() {
    // 创建对话框并设置布局
    QDialog *dialog = new QDialog(this);
    QVBoxLayout *layout = new QVBoxLayout(dialog);

    // 磁盘选择下拉框
    QLabel *diskLabel = new QLabel(QObject::tr("Select Disk:"), dialog);
    layout->addWidget(diskLabel);

    QComboBox *diskComboBox = new QComboBox(dialog);
    layout->addWidget(diskComboBox);

    // 分区选择下拉框
    QLabel *partitionLabel = new QLabel(QObject::tr("Select FAT Partition:"), dialog);
    layout->addWidget(partitionLabel);

    QComboBox *partitionComboBox = new QComboBox(dialog);
    layout->addWidget(partitionComboBox);

    // 文件路径输入框
    QLabel *filePathLabel = new QLabel(QObject::tr("Enter File Path:"), dialog);
    layout->addWidget(filePathLabel);

    QLineEdit *filePathLineEdit = new QLineEdit(dialog);
    layout->addWidget(filePathLineEdit);

    // ID输入框
    QLabel *idLabel = new QLabel(QObject::tr("Enter the ID:"), dialog);
    layout->addWidget(idLabel);

    QLineEdit *idLineEdit = new QLineEdit(dialog);
    layout->addWidget(idLineEdit);

    // 添加按钮
    QPushButton *addButton = new QPushButton(QObject::tr("Add"), dialog);
    layout->addWidget(addButton);

    dialog->setLayout(layout);
    dialog->setWindowTitle(QObject::tr("Add Boot Entry"));

    // 创建 QProcess 对象（指针形式）
    QProcess *process = new QProcess(this);

    // 加载磁盘和分区信息
    process->start("lsblk", QStringList() << "-o" << "NAME,TYPE,FSTYPE" << "-n" << "-l");
    process->waitForFinished();

    QString output = process->readAllStandardOutput();
    QStringList devices = output.split("\n", Qt::SkipEmptyParts);

    // 提取磁盘信息并填充磁盘 ComboBox
    for (const QString &line : devices) {
        QStringList fields = line.split(" ", Qt::SkipEmptyParts);
        if (fields.size() == 2) {
            QString name = fields[0];  // 设备名称
            QString type = fields[1];  // 类型

            // 如果是磁盘（而不是分区），将其加入磁盘选择框
            if (type == "disk") {
                QString diskPath = "/dev/" + name;
                diskComboBox->addItem(diskPath);
            }
        }
    }

    // 加载所有分区信息并填充分区 ComboBox
    for (const QString &line : devices) {
        QStringList fields = line.split(" ", Qt::SkipEmptyParts);
        if (fields.size() >= 3) {
            QString name = fields[0]; // 分区名称
            QString type = fields[1]; // 分区类型
            QString fsType = fields[2]; // 文件系统类型

            // 只选择文件系统为 FAT 或 vfat 的分区
            if (type == "part" && (fsType == "vfat" || fsType == "FAT")) {
                partitionComboBox->addItem(name);
            }
        }
    }

    // 当用户选择磁盘时，重新加载该磁盘的分区信息
    connect(diskComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        partitionComboBox->clear();  // 清空分区列表

        QString selectedDisk = diskComboBox->currentText();
        process->start("lsblk", QStringList() << "-o" << "NAME,TYPE,FSTYPE" << "-n" << "-l" << selectedDisk);
        process->waitForFinished();

        QString output = process->readAllStandardOutput();
        QStringList partitions = output.split("\n", Qt::SkipEmptyParts);

        for (const QString &line : partitions) {
            QStringList fields = line.split(" ", Qt::SkipEmptyParts);
            if (fields.size() >= 3) {
                QString name = fields[0]; // 分区名称
                QString type = fields[1]; // 分区类型
                QString fsType = fields[2]; // 文件系统类型

                // 只选择文件系统为 FAT 或 vfat 的分区
                if (type == "part" && (fsType == "vfat" || fsType == "FAT")) {
                    // 只显示分区号（例如：sda1, nvme0n1p1）
                    partitionComboBox->addItem(name);
                }
            }
        }
    });

    // 连接添加按钮的点击信号
    connect(addButton, &QPushButton::clicked, [=]() {
        QString selectedPartition = partitionComboBox->currentText();
        QString filePath = filePathLineEdit->text();

        // 判断文件路径是否为空
        if (selectedPartition.isEmpty() || filePath.isEmpty()) {
            QMessageBox::warning(dialog, QObject::tr("Input Error"), QObject::tr("Please select a partition and enter a file path."));
            return;
        }

        // 使用 lsblk -f 检查文件系统类型
        process->start("lsblk", QStringList() << "-f" << "/dev/" + selectedPartition);
        process->waitForFinished();

        QString output = process->readAllStandardOutput();
        if (!output.contains("vfat")) {
            QMessageBox::warning(dialog, QObject::tr("Error"), QObject::tr("The selected partition is not a FAT filesystem."));
            return;
        }

        // 获取磁盘和分区号
        QStringList partitionParts = selectedPartition.split("p");
        QString disk = partitionParts[0];
        QString partitionNumber = partitionParts.size() > 1 ? partitionParts[1] : "1";  // 默认分区号为 1

        QString id = idLineEdit->text();
        QString path = filePathLineEdit->text();

        QStringList args;
        args << "efibootmgr" << "-c" << "-d" << "/dev/" + disk << "-p" << partitionNumber << "-L" << id << "-l" << path;

        process->start("pkexec", args);
        process->waitForFinished();

        if (process->exitStatus() == QProcess::NormalExit) {
            QMessageBox::information(dialog, QObject::tr("Success"), QObject::tr("Boot entry added successfully."));
        } else {
            QMessageBox::warning(dialog, QObject::tr("Error"), QObject::tr("Failed to add boot entry."));
        }

        refreshBootEntries();
        dialog->accept(); // 关闭对话框
    });

    // 显示对话框
    dialog->exec();
}


void MainWindow::refreshBootEntries() {
    QProcess process;
    process.start("efibootmgr");
    process.waitForFinished();

    QString output = process.readAllStandardOutput();
    QStringList lines = output.split("\n");

    // 清空模型
    model->removeRows(0, model->rowCount());

    // 提取 BootOrder 行
    QString bootOrderLine;
    for (const QString &line : lines) {
        if (line.startsWith("BootOrder:")) {
            bootOrderLine = line;
            break;
        }
    }

    // 获取 BootOrder 中的 Boot Number 顺序
    QStringList bootOrder = bootOrderLine.section(":", 1).trimmed().split(",");
    QRegularExpression bootEntryRegex("^Boot([0-9a-fA-F]{4})\\*?\\s+([^\t]+)\t([^\t\\\\]+)(\\\\[^ \t\n\r]*\\.(efi|EFI))?");
    QMap<QString, QList<QStandardItem *>> bootEntries;

    const int maxLabelLength = 30; // Label 最大长度
    const int maxDeviceLength = 50; // Device 最大长度
    for (const QString &line : lines) {
        // 跳过 BootCurrent 和 BootOrder 行
        if (line.startsWith("BootCurrent:") || line.startsWith("BootOrder:")) {
            continue;
        }

        // 使用正则表达式匹配 BootXXXX 行
        QRegularExpressionMatch match = bootEntryRegex.match(line);
        if (match.hasMatch()) {
            QString bootNumber = match.captured(1); // 提取 Boot Number
            QString label = match.captured(2);      // 提取 Label
            QString device = match.captured(3);     // 提取 Device
            QString path = match.captured(4);       // 提取 Path

            // 限制 Label 和 Device 的最大长度
            if (label.length() > maxLabelLength) {
                label = label.left(maxLabelLength) + "..."; // 超过最大长度则添加省略号
            }
            if (device.length() > maxDeviceLength) {
                device = device.left(maxDeviceLength) + "..."; // 超过最大长度则添加省略号
            }

            // 创建不可编辑的 QStandardItem
            QList<QStandardItem *> rowItems;
            rowItems << new QStandardItem(bootNumber);  // 第一列: Boot Number
            rowItems << new QStandardItem(label);       // 第二列: Label
            rowItems << new QStandardItem(device);      // 第三列: Device
            rowItems << new QStandardItem(path);        // 第四列: Path

            // 设置不可编辑
            for (QStandardItem *item : rowItems) {
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            }

            // 存储每个引导项，按 BootNumber 进行索引
            bootEntries[bootNumber] = rowItems;
        }
    }

    // 根据 BootOrder 中的顺序排序并添加到模型中
    for (const QString &bootNumber : bootOrder) {
        if (bootEntries.contains(bootNumber)) {
            model->appendRow(bootEntries[bootNumber]);
        }
    }

    tableView->setColumnWidth(1, 200); // 设置 Label 列最大宽度（例如：200像素）
    tableView->setColumnWidth(2, 300); // 设置 Device 列最大宽度（例如：300像素）
    tableView->resizeColumnsToContents(); // 调整列宽
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
    } else {
        this->setWindowTitle("GRUB Assistant -- Local mode");
    }

    showFormattedLsblkOutput(ui->LogBrowser);

    QDir efi1("/boot/efi");
    QDir efi2("/boot/EFI");
    if (efi1.exists()) {
        ui->efiInput->setText("/boot/efi");
    } else if (efi2.exists()) {
        ui->efiInput->setText("/boot/EFI");
    } else {
        ui->efiInput->setText("/boot");
    }

    // ui->themeInput->setText("/usr/share/grub/themes/vimix-color-1080p/theme.txt");
    // ui->timeoutInput->setText("5");

    // 将磁盘信息添加到 ComboBox 中，并获取分区表类型
    QStringList diskList = getDiskList();
    ui->DiskcomboBox->addItems(diskList);
    connect(ui->DiskcomboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onDiskSelected);
    int defaultIndex = 0;
    ui->DiskcomboBox->setCurrentIndex(defaultIndex);
    onDiskSelected(defaultIndex);

    if (ui->idlineEdit->text().isEmpty()) {
        efiEntryID = "ARCH";
    } else {
        efiEntryID = ui->idlineEdit->text();
    }

    // 处理浏览按钮
    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::onbrowseButtonclicked);
    connect(ui->browseThemeButton, &QPushButton::clicked, this, &MainWindow::onbrowseThemeButtonclicked);

    // 处理repair按钮
    connect(ui->repair1Button, &QPushButton::clicked, this, &MainWindow::onrepairButtonclicked); // 执行修复UEFI GRUB代码
    connect(ui->repair2Button, &QPushButton::clicked, this, &MainWindow::onbiosrepairButtonclicked); // 执行修复BIOS GRUB代码

    connect(ui->live1Button, &QPushButton::clicked, this, &MainWindow::onChrootRepairButtonClicked);

    // 处理Mount Disk按钮
    connect(ui->mountButton, &QPushButton::clicked, this, &MainWindow::onPartitionButtonClicked);

    // 处理Unmount按钮
    connect(ui->umountButton, &QPushButton::clicked, this, &MainWindow::onUmountButtonClicked);

    // GRUB相关设置
    connect(ui->loadButton, &QPushButton::clicked, this, &MainWindow::onLoadButtonClicked);

    // 保存GRUB设置
    connect(ui->saveButton, &QPushButton::clicked, this, &MainWindow::onSaveButtonClicked);

    // 更新GRUB菜单
    connect(ui->updateButton, &QPushButton::clicked, this, &MainWindow::onUpdateButtonClicked);
    connect(ui->reg1Button, &QPushButton::clicked, this, &MainWindow::onUpdateButtonClicked);
    connect(ui->reg2Button, &QPushButton::clicked, this, &MainWindow::onUpdateButtonClicked);

    connect(ui->reg3Button, &QPushButton::clicked, this, &MainWindow::onChrootUpdateButtonClicked);

    tableView = new CustomTableView(this);
    ui->uefiverticalLayout->addWidget(tableView); // 假设界面中有一个 QVBoxLayout

    // 初始化模型
    model = new QStandardItemModel(this);
    model->setColumnCount(4);
    model->setHeaderData(0, Qt::Horizontal, "Boot Number");
    model->setHeaderData(1, Qt::Horizontal, "Label");
    model->setHeaderData(2, Qt::Horizontal, "Device");
    model->setHeaderData(3, Qt::Horizontal, "Path");

    // 设置模型到 TableView
    tableView->setModel(model);
    tableView->resizeColumnsToContents();

    // 启用整行选择和拖放功能
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView->setDragEnabled(true);
    tableView->setAcceptDrops(true);
    tableView->setDragDropMode(QAbstractItemView::InternalMove);
    tableView->setDropIndicatorShown(true);

    // 连接拖放完成信号
    connect(model, &QStandardItemModel::rowsMoved, this, &MainWindow::onRowsMoved);

    // 刷新启动项
    refreshBootEntries();

    connect(ui->addButton, &QPushButton::clicked, this, &MainWindow::onAddButtonClicked);
    connect(ui->delButton, &QPushButton::clicked, this, &MainWindow::onDelButtonClicked);
    connect(ui->savButton, &QPushButton::clicked, this, &MainWindow::onSavButtonClicked);

}

MainWindow::~MainWindow()
{
    delete ui;
}




