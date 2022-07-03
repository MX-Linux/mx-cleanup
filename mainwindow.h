/**********************************************************************
 *  mainwindow.h
 **********************************************************************
 * Copyright (C) 2018 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/


#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QButtonGroup>
#include <QMessageBox>
#include <QSettings>

namespace Ui {
class MainWindow;
}

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    static void addGroupCheckbox(QLayout *layout, const QString &package, const QString &name, QStringList &list);
    static void saveSchedule(const QString &cmd_str, const QString &period);
    static void selectRadioButton(const QButtonGroup *group, int id);
    void loadOptions();
    void loadSchedule();
    void loadSettings();
    void removeKernelPackages(const QStringList &list);
    void saveSettings();
    void setConnections();
    void setup();

private slots:
    void pushAbout_clicked();
    void pushApply_clicked();
    void pushHelp_clicked();
    void pushKernel_clicked();
    void pushUsageAnalyzer_clicked();

private:
    Ui::MainWindow *ui;
    QSettings settings;
    QString user;
    QString getCmdOut(const QString &cmd);

};


#endif

