#ifndef MOUNTPOINTDIALOG_H
#define MOUNTPOINTDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>

class MountPointDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MountPointDialog(QWidget *parent = nullptr);
    QString getMountPoint() const;

private:
    QString findDefaultMountPoint();
    QLineEdit *mountPointLineEdit;
    QPushButton *confirmButton;
    QPushButton *cancelButton;
};

#endif // MOUNTPOINTDIALOG_H
