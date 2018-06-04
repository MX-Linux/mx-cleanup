/**********************************************************************
 *  mainwindow.cpp
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



#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>


MainWindow::MainWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setup();
    refresh();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// setup versious items first time program runs
void MainWindow::setup()
{
    cmd = new Cmd(this);
    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::cleanup);
    this->setWindowTitle(tr("MX Cleanup"));
    this->adjustSize();
    ui->buttonCancel->setEnabled(true);
    ui->buttonApply->setDisabled(true);
}

void MainWindow::refresh()
{
    ui->tmpCheckBox->setChecked(true);
    ui->cacheCheckBox->setChecked(true);
    ui->thumbCheckBox->setChecked(true);
    ui->autocleanRB->setChecked(true);
    ui->oldLogsRB->setChecked(true);
    ui->selectedUserCB->setChecked(true);
    char line[130];
    char line2[130];
    char *tok;
    FILE *fp;
    int i;
    ui->userCleanCB->clear();
    fp = popen("ls -1 /home", "r");
    if (fp != NULL) {
        while (fgets(line, sizeof line, fp) != NULL) {
            i = strlen(line);
            line[--i] = '\0';
            tok = strtok(line, " ");
            if (tok != NULL && strlen(tok) > 1 && strncmp(tok, "ftp", 3) != 0) {
                sprintf(line2, "grep '^%s' /etc/passwd >/dev/null", tok);
                if (system(line2) == 0) {
                    ui->userCleanCB->addItem(tok);
                }
            }
        }
        pclose(fp);
    }
    ui->userCleanCB->setCurrentIndex(ui->userCleanCB->findText(cmd->getOutput("logname")));
    ui->buttonApply->setEnabled(ui->userCleanCB->currentText() != tr("none"));
}

// cleanup environment when window is closed
void MainWindow::cleanup()
{

}


// Get version of the program
QString MainWindow::getVersion(QString name)
{
    Cmd cmd;
    return cmd.getOutput("dpkg -l "+ name + "| awk 'NR==6 {print $3}'");
}

void MainWindow::cmdStart()
{
    setCursor(QCursor(Qt::BusyCursor));
}


void MainWindow::cmdDone()
{
    setCursor(QCursor(Qt::ArrowCursor));
}

// set proc and timer connections
void MainWindow::setConnections()
{
    cmd->disconnect();
    connect(cmd, &Cmd::started, this, &MainWindow::cmdStart);
    connect(cmd, &Cmd::finished, this, &MainWindow::cmdDone);
}


void MainWindow::on_buttonApply_clicked()
{
    setCursor(QCursor(Qt::BusyCursor));
    if (ui->tmpCheckBox->isChecked()) {
        system("rm -r /tmp/* 2>/dev/null");
    }
    if (ui->cacheCheckBox->isChecked()) {
        system("rm -r /home/" + ui->userCleanCB->currentText().toUtf8() + "/.cache/* 2>/dev/null");
    }
    if (ui->thumbCheckBox->isChecked()) {
        system("rm -r /home/" + ui->userCleanCB->currentText().toUtf8() + "/.thumbnails/* 2>/dev/null");
    }
    if (ui->autocleanRB->isChecked()) {
        system("apt-get autoclean");
    } else {
        system("apt-get clean");
    }
    if (ui->oldLogsRB->isChecked()) {
        system("find /var/log -name \"*.gz\" -o -name \"*.old\" -o -name \"*.1\" -type f -delete 2>/dev/null");
    } else {
        system("find /var/log -type f -exec sh -c \"echo > '{}'\" \\;");  // empty the logs
    }
    if (ui->selectedUserCB->isChecked()) {
        system("rm -r /home/" + ui->userCleanCB->currentText().toUtf8() +"/.local/share/Trash/* 2>/dev/null");
    } else {
        system("rm -r /home/*/.local/share/Trash/* 2>/dev/null");
    }
    setCursor(QCursor(Qt::ArrowCursor));
    QMessageBox::information(this, tr("Done"),
                             tr("Cleanup command has been completed."));
    refresh();
}

// About button clicked
void MainWindow::on_buttonAbout_clicked()
{
    QMessageBox msgBox(QMessageBox::NoIcon,
                       tr("About") + tr("MX Cleanup"), "<p align=\"center\"><b><h2>MX Cleanup</h2></b></p><p align=\"center\">" +
                       tr("Version: ") + getVersion("mx-cleanup") + "</p><p align=\"center\"><h3>" +
                       tr("Description goes here") +
                       "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>");
    msgBox.addButton(tr("License"), QMessageBox::AcceptRole);
    msgBox.addButton(tr("Cancel"), QMessageBox::NoRole);
    if (msgBox.exec() == QMessageBox::AcceptRole) {
        QString url = "file:///usr/share/doc/mx-cleanup/license.html";
        Cmd c;
        QString user = c.getOutput("logname");
        if (system("command -v mx-viewer") == 0) { // use mx-viewer if available
            system("su " + user.toUtf8() + " -c \"mx-viewer '" + url.toUtf8() + " " + tr("License").toUtf8() + "'\"&");
        } else {
            system("su " + user.toUtf8() + " -c \"xdg-open " + url.toUtf8() + "\"&");
        }
    }
}

// Help button clicked
void MainWindow::on_buttonHelp_clicked()
{
    QString url = "google.com";
    QString exec = "xdg-open";
    if (system("command -v mx-viewer") == 0) { // use mx-viewer if available
        exec = "mx-viewer";
        url += " MX Cleanup";
    }
    Cmd c;
    QString user = c.getOutput("logname");
    QString cmd = QString("su " + user + " -c \"" + exec + " " + url + "\"&");
    system(cmd.toUtf8());
}

void MainWindow::on_baobabPushButton_clicked()
{
    system("baobab&");
}
