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

#include <QMessageBox>

#include "cmd.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    QString getVersion(QString name);
    void setup();
    void refresh();

public slots:

private slots:
    void cleanup();
    void cmdStart();
    void cmdDone();
    void setConnections();
    void on_buttonApply_clicked();
    void on_buttonAbout_clicked();
    void on_buttonHelp_clicked();
    void on_baobabPushButton_clicked();

private:
    Ui::MainWindow *ui;
    Cmd *cmd;

};


#endif

