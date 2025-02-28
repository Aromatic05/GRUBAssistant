#include "configure.h"
#include <QFile>
#include <QTextStream>
#include <QMap>
#include <QDebug>
#include <QComboBox>
#include <QProcess>
#include <QRegularExpression>
#include <QDebug>
#include <QMessageBox>
#include <QStack>
#include <QStringList>

// 函数：读取文件并提取变量
QMap<QString, QString> parseGrubFile() {
    QString filePath = "/etc/default/grub";
    QMap<QString, QString> variables; // 用于存储变量
    QFile file(filePath);

    // 打开文件
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << QObject::tr("Unable to open file: ") << filePath;
        return variables; // 返回空的 QMap
    }

    QTextStream in(&file);

    // 逐行读取文件
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed(); // 去除空白字符

        // 忽略行中间的注释（# 后面的内容）
        int commentIndex = line.indexOf("#");
        if (commentIndex != -1) {
            line = line.left(commentIndex).trimmed();
        }

        // 跳过空行
        if (line.isEmpty()) {
            continue;
        }

        // 按等号分割键值对
        QStringList parts = line.split("=");
        if (parts.size() >= 2) { // 允许值中包含等号
            QString key = parts[0].trimmed();
            QString value = parts.mid(1).join("=").trimmed(); // 合并等号后的部分

            // 去除值中的引号（单引号或双引号）
            if ((value.startsWith("\"") && value.endsWith("\"")) ||
                (value.startsWith("'") && value.endsWith("'"))) {
                value = value.mid(1, value.length() - 2);
            }

            variables[key] = value; // 存储变量

            // 输出提取的键值对
            qDebug() << "提取的键值对:" << key << "=" << value;
        }
    }

    file.close();
    return variables;
}

void parseGrubMenu(QComboBox* defaultMenuCombo) {
    // 使用 pkexec 读取 /boot/grub/grub.cfg 文件
    QProcess process;
    process.setProgram("pkexec");
    process.setArguments({"cat", "/boot/grub/grub.cfg"});

    // 启动进程
    process.start();
    if (!process.waitForStarted()) {
        qWarning() << QObject::tr("Unable to start pkexec process");
        return;
    }

    // 等待进程完成
    if (!process.waitForFinished()) {
        qWarning() << QObject::tr("pkexec process execution failed");
        return;
    }

    // 检查退出状态
    if (process.exitCode() != 0) {
        qWarning() << QObject::tr("pkexec execution failed with exit code:") << process.exitCode();
        QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Unable to read /boot/grub/grub.cfg file, please check permissions."));
        return;
    }

    QString content = process.readAllStandardOutput();
    defaultMenuCombo->clear();
    QStack<bool> stack;

    // 逐行处理文件内容
    QTextStream stream(&content);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        QRegularExpression menuRegex(R"((menuentry|submenu)\s+['"]([^'"]+)['"])");
        QRegularExpressionMatch menuMatch = menuRegex.match(line);

        if (menuMatch.hasMatch() && stack.isEmpty()) {
            // 如果匹配到 menuentry 或 submenu，且栈为空，提取菜单名并添加到 ComboBox 中
            QString menuName = menuMatch.captured(2);
            defaultMenuCombo->addItem(menuName);
        }

        if (line.contains("{")) {
            stack.push(true);
        }

        if (line.contains("}")) {
            if (!stack.isEmpty()) {
                stack.pop();
            }
        }
    }
}

void loadGrubUi(Ui::MainWindow* ui, QMap<QString, QString>& grub_variables) {
    if (!grub_variables.contains("GRUB_DEFAULT")) {
        grub_variables.insert("GRUB_DEFAULT", "0");
    }
    bool ok;
    ui->defaultMenuCombo->setCurrentIndex(grub_variables.value("GRUB_DEFAULT").toInt(&ok));

    if (!grub_variables.contains("GRUB_THEME")) {
        grub_variables.insert("GRUB_THEME", "");
    }
    ui->themeInput->setText(grub_variables.value("GRUB_THEME"));

    if (!grub_variables.contains("GRUB_TIMEOUT")) {
        grub_variables.insert("GRUB_TIMEOUT", "0");
    }
    ui->timeoutInput->setText(grub_variables.value("GRUB_TIMEOUT"));

    if (!grub_variables.contains("GRUB_CMDLINE_LINUX_DEFAULT")) {
        grub_variables.insert("GRUB_CMDLINE_LINUX_DEFAULT", "0");
    }
    ui->kernelInput->setText(grub_variables.value("GRUB_CMDLINE_LINUX_DEFAULT"));

    if (!grub_variables.contains("GRUB_PRELOAD_MODULES")) {
        grub_variables.insert("GRUB_PRELOAD_MODULES", "");
    }
    ui->moduleslineEdit->setText(grub_variables.value("GRUB_PRELOAD_MODULES"));

    if (!grub_variables.contains("GRUB_GFXMODE")) {
        grub_variables.insert("GRUB_GFXMODE", "auto");
    }
    ui->gfxlineEdit->setText(grub_variables.value("GRUB_GFXMODE"));

    if (!grub_variables.contains("GRUB_DISABLE_OS_PROBER")) {
        grub_variables.insert("GRUB_DISABLE_OS_PROBER", "true");
    }
    if (grub_variables.value("GRUB_DISABLE_OS_PROBER") == "false") {
        ui->checkBox->setChecked(true);
    } else {
        ui->checkBox->setChecked(false);
    }

    if (!grub_variables.contains("GRUB_DISABLE_LINUX_UUID")) {
        grub_variables.insert("GRUB_DISABLE_LINUX_UUID", "false");
    }
    if (grub_variables.value("GRUB_DISABLE_LINUX_UUID") == "false") {
        ui->UUIDcheckBox->setChecked(true);
    } else {
        ui->UUIDcheckBox->setChecked(false);
    }

}

void updateVariables(Ui::MainWindow* ui, QMap<QString, QString>& grub_variables) {
    grub_variables.insert("GRUB_DEFAULT", QString::number(ui->defaultMenuCombo->currentIndex()));
    grub_variables.insert("GRUB_THEME", ui->themeInput->text());
    grub_variables.insert("GRUB_TIMEOUT", ui->timeoutInput->text());
    grub_variables.insert("GRUB_PRELOAD_MODULES", ui->moduleslineEdit->text());
    grub_variables.insert("GRUB_CMDLINE_LINUX_DEFAULT", ui->kernelInput->text());
    grub_variables.insert("GRUB_GFXMODE", ui->gfxlineEdit->text());
    if (ui->checkBox->isChecked()){
        grub_variables.insert("GRUB_DISABLE_OS_PROBER", "false");
    } else {
        grub_variables.insert("GRUB_DISABLE_OS_PROBER", "true");
    }
    if (ui->UUIDcheckBox->isChecked()){
        grub_variables.insert("GRUB_DISABLE_LINUX_UUID", "false");
    } else {
        grub_variables.insert("GRUB_DISABLE_LINUX_UUID", "true");
    }
}

void saveGrubVariables(const QMap<QString, QString>& grub_variables) {
    // 需要加引号的变量列表
    QStringList quotedVariables = {
        "GRUB_THEME",
        "GRUB_CMDLINE_LINUX_DEFAULT",
        "GRUB_PRELOAD_MODULES"
    };

    // 不需要加引号的变量列表
    QStringList unquotedVariables = {
        "GRUB_TIMEOUT",
        "GRUB_GFXMODE",
        "GRUB_DEFAULT",
        "GRUB_DISABLE_OS_PROBER",
        "GRUB_DISABLE_LINUX_UUID"
    };

    // 文件路径
    QString grubFilePath = "/etc/default/grub";
    QString tempFilePath = "/tmp/grub_temp";

    // 打开原始文件进行读取
    QFile grubFile(grubFilePath);
    if (!grubFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, QObject::tr("Error"), QObject::tr("Failed to open GRUB file for reading."));
        return;
    }

    // 读取文件内容
    QTextStream in(&grubFile);
    QStringList fileLines;
    while (!in.atEnd()) {
        fileLines.append(in.readLine());
    }
    grubFile.close();

    // 遍历文件内容，更新或标记已存在的变量
    QMap<QString, bool> variableExists;
    for (auto& line : fileLines) {
        // 处理需要加引号的变量
        for (const auto& variable : quotedVariables) {
            if (line.startsWith(variable + "=")) {
                if (grub_variables.contains(variable)) {
                    line = variable + "=\"" + grub_variables.value(variable) + "\""; // 更新变量的值并加引号
                }
                variableExists[variable] = true; // 标记变量已存在
            }
        }
        // 处理不需要加引号的变量
        for (const auto& variable : unquotedVariables) {
            if (line.startsWith(variable + "=")) {
                if (grub_variables.contains(variable)) {
                    line = variable + "=" + grub_variables.value(variable); // 更新变量的值，不加引号
                }
                variableExists[variable] = true; // 标记变量已存在
            }
        }
    }

    // 追加未存在的变量到文件末尾
    for (const auto& variable : quotedVariables) {
        if (!variableExists.contains(variable) && grub_variables.contains(variable)) {
            fileLines.append(variable + "=\"" + grub_variables.value(variable) + "\""); // 追加需要加引号的变量
        }
    }
    for (const auto& variable : unquotedVariables) {
        if (!variableExists.contains(variable) && grub_variables.contains(variable)) {
            fileLines.append(variable + "=" + grub_variables.value(variable)); // 追加不需要加引号的变量
        }
    }

    // 打开临时文件进行写入
    QFile tempFile(tempFilePath);
    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, QObject::tr("Error"), QObject::tr("Failed to open temporary file for writing."));
        return;
    }

    QTextStream out(&tempFile);
    for (const auto& line : fileLines) {
        out << line << "\n"; // 写入更新后的内容
    }
    tempFile.close();

    QProcess process;
    process.start("pkexec", QStringList() << "cp" << tempFilePath << grubFilePath);
    process.waitForFinished();

    if (process.exitCode() != 0) {
        QMessageBox::critical(nullptr, QObject::tr("Error"), QObject::tr("Failed to copy temporary file to /etc/default/grub."));
        return;
    }

    QMessageBox::information(nullptr, QObject::tr("Success"), QObject::tr("GRUB variables saved successfully."));
}

