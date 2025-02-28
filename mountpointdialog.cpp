#include "mountpointdialog.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QRegularExpression>

MountPointDialog::MountPointDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QObject::tr("Enter the mount point:"));

    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *label = new QLabel(QObject::tr("Enter the mount point:"), this);
    layout->addWidget(label);

    mountPointLineEdit = new QLineEdit(this);
    layout->addWidget(mountPointLineEdit);

    // 查找默认挂载点并设置到 QLineEdit
    QString defaultMountPoint = findDefaultMountPoint();
    if (!defaultMountPoint.isEmpty()) {
        mountPointLineEdit->setText(defaultMountPoint);
    }

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    confirmButton = new QPushButton(QObject::tr("Confirm"), this);
    cancelButton = new QPushButton(QObject::tr("Cancel"), this);

    buttonLayout->addWidget(confirmButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);

    connect(confirmButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

QString MountPointDialog::getMountPoint() const
{
    return mountPointLineEdit->text();
}

QString MountPointDialog::findDefaultMountPoint()
{
    QString fstabPath = "/mnt/etc/fstab";
    QFile file(fstabPath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << QObject::tr("Unable to open file: ") << fstabPath;
        return QString();
    }

    QTextStream in(&file);
    QStringList searchPaths = {"/boot/EFI", "/boot/efi", "/boot"}; // 按顺序查找的路径

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith("#") || line.isEmpty()) {
            continue; // 跳过注释和空行
        }

        // 使用 QRegularExpression 分割行
        QRegularExpression regex("\\s+"); // 匹配空白字符
        QStringList fields = line.split(regex, Qt::SkipEmptyParts);

        if (fields.size() < 2) {
            continue;
        }

        QString mountPoint = fields.at(1); // 挂载点是第二个字段

        // 检查是否匹配搜索路径
        for (const QString &path : searchPaths) {
            if (mountPoint == path) {
                file.close();
                return path; // 返回第一个匹配的路径
            }
        }
    }

    file.close();
    return QString(); // 没有找到匹配的路径
}
