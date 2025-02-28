#ifndef CONFIGURE_H
#define CONFIGURE_H

#include <QFile>
#include <QTextStream>
#include <QMap>
#include <QDebug>
#include <QComboBox>
#include <QProcess>
#include <QRegularExpression>
#include <QDebug>
#include <QMessageBox>
#include "mainwindow.h"
#include "ui_mainwindow.h"

QMap<QString, QString> parseGrubFile();
void parseGrubMenu(QComboBox* defaultMenuCombo);
void loadGrubUi(Ui::MainWindow* ui, QMap<QString, QString>& grub_variables);
void updateVariables(Ui::MainWindow* ui, QMap<QString, QString>& grub_variables);
void saveGrubVariables(const QMap<QString, QString>& grub_variables);

#endif // CONFIGURE_H
