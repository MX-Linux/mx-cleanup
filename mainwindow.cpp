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
#include "version.h"

#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QTextEdit>

MainWindow::MainWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MainWindow)
{
    qDebug().noquote() << QCoreApplication::applicationName() << "version:" << VERSION;
    ui->setupUi(this);
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    setup();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// Setup versious items first time program runs
void MainWindow::setup()
{
    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::cleanup);
    this->setWindowTitle(tr("MX Cleanup"));
    this->adjustSize();

    user = getCmdOut("logname");

    ui->buttonApply->setDisabled(true);
    ui->cacheCheckBox->setChecked(true);
    ui->thumbCheckBox->setChecked(true);
    ui->autocleanRB->setChecked(true);
    ui->oldLogsRB->setChecked(true);
    ui->selectedUserCB->setChecked(true);

    QString users = getCmdOut("lslogins --noheadings -u -o user | grep -vw root");

    qDebug() << users;
    ui->userCleanCB->addItems(users.split("\n"));

    ui->userCleanCB->setCurrentIndex(ui->userCleanCB->findText(user));
    ui->buttonApply->setEnabled(ui->userCleanCB->currentText() != "");
    checkSchedule();
}

// Cleanup environment when window is closed
void MainWindow::cleanup()
{

}


// check if /etc/cron.daily|weekly|monthly/mx-cleanup script exists
void MainWindow::checkSchedule()
{
    if (QFile::exists("/etc/cron.daily/mx-cleanup")) {
        ui->rbDaily->setChecked(true);
        checkOptions();
    } else if (QFile::exists("/etc/cron.weekly/mx-cleanup")) {
        ui->rbWeekly->setChecked(true);
        checkOptions();
    } else if (QFile::exists("/etc/cron.monthly/mx-cleanup")) {
        ui->rbMonthly->setChecked(true);
    } else {
        ui->rbNone->setChecked(true);
        checkOptions();
    }
}

// Load saved options to GUI
void MainWindow::checkOptions()
{
    QString period;
    if (ui->rbDaily->isChecked()) {
        period = "daily";
    } else if (ui->rbWeekly->isChecked()) {
        period = "weekly";
    } else if (ui->rbMonthly->isChecked()) {
        period = "monthly";
    }
    QString file_name = "/etc/cron." + period + "/mx-cleanup";

    // Folders
    ui->thumbCheckBox->setChecked(system("grep -q '.thumbnails' " + file_name.toUtf8()) == 0);
    ui->cacheCheckBox->setChecked(system("grep -q '.cache' " + file_name.toUtf8()) == 0);
    // APT
    if (system("grep -q 'apt-get autoclean' " + file_name.toUtf8()) == 0) { // detect autoclean
        ui->autocleanRB->setChecked(true);
    } else if (system("grep -q 'apt-get clean' " + file_name.toUtf8()) == 0) { // detect clean
        ui->cleanRB->setChecked(true);
    } else {
        ui->noCleanAptRB->setChecked(true);
    }
    // Logs
    if (system("grep -q '\\-exec sh \\-c \"echo' " + file_name.toUtf8()) == 0) { // all logs
        ui->allLogsRB->setChecked(true);
    } else if (system("grep -q '\\-type f \\-delete' " + file_name.toUtf8()) == 0) { // old logs
        ui->oldLogsRB->setChecked(true);
    } else {
        ui->noCleanLogsRB->setChecked(true);
    }
    // Trash
    if (system("grep -q '/home/\\*/.local/share/Trash/' " + file_name.toUtf8()) == 0) { // all user trash
        ui->allUsersCB->setChecked(true);
    } else if (system("grep -q '/.local/share/Trash/' " + file_name.toUtf8()) == 0) { // selected user trash
        ui->selectedUserCB->setChecked(true);
    } else {
        ui->noCleanTrashRB->setChecked(true);
    }
}


// Save cleanup commands to a /etc/cron.daily|weekly|monthly/mx-cleanup script
void MainWindow::saveSchedule(QString cmd_str, QString period)
{
    QFile file("/etc/cron." + period + "/mx-cleanup");
    if (!file.open(QFile::WriteOnly)) {
        qDebug() << "Could not open file:" << file.fileName();
    }
    QTextStream out(&file);
    out << "#!/bin/sh\n";
    out << "#\n";
    out << "# This file was created by MX Cleanup\n";
    out << "#\n\n";
    out << cmd_str;
    file.close();
    system("chmod +x " + file.fileName().toUtf8());
}


void MainWindow::on_buttonApply_clicked()
{
    int total = 0;
    setCursor(QCursor(Qt::BusyCursor));

    QString cmd_str;
    QString cache, thumbnails, logs, apt, trash;

//  Too aggressive
//    if (ui->tmpCheckBox->isChecked()) {
//        total +=  getCmdOut("du -c /tmp/* | tail -1 | cut -f1").toInt();
//        system("rm -r /tmp/* 2>/dev/null");
//    }

    if (ui->cacheCheckBox->isChecked()) {
        total += getCmdOut("du -c /home/" + ui->userCleanCB->currentText().toUtf8() + "/.cache/* | tail -1 | cut -f1").toInt();
        cache = "rm -r /home/" + ui->userCleanCB->currentText().toUtf8() + "/.cache/* 2>/dev/null";
        system(cache.toUtf8());
    }

    if (ui->thumbCheckBox->isChecked()) {
        total += getCmdOut("du -c /home/" + ui->userCleanCB->currentText().toUtf8() + "/.thumbnails/* | tail -1 | cut -f1").toInt();
        thumbnails = "rm -r /home/" + ui->userCleanCB->currentText().toUtf8() + "/.thumbnails/* 2>/dev/null";
        system(thumbnails.toUtf8());
    }

    if (ui->autocleanRB->isChecked()) {
        apt = "apt-get autoclean";
    } else if (ui->cleanRB->isChecked()) {
        apt = "apt-get clean";
    }
    total += getCmdOut("du -s /var/cache/apt/archives/ | cut -f1").toInt();
    system(apt.toUtf8());
    total -= getCmdOut("du -s /var/cache/apt/archives/ | cut -f1").toInt();

    if (ui->oldLogsRB->isChecked()) {
        total += getCmdOut("find /var/log \\( -name \"*.gz\" -o -name \"*.old\" -o -name \"*.1\" \\) -type f -exec du -sc {} + | tail -1 | cut -f1").toInt();
        logs = "find /var/log \\( -name \"*.gz\" -o -name \"*.old\" -o -name \"*.1\" \\) -type f -delete 2>/dev/null";
        system(logs.toUtf8());
    } else if (ui->allLogsRB->isChecked()){
        total += getCmdOut("du -s /var/log/ | cut -f1").toInt();
        logs = "find /var/log -type f -exec sh -c \"echo > '{}'\" \\;";  // empty the logs
        system(logs.toUtf8());
        total -= getCmdOut("du -s /var/log/ | cut -f1").toInt();
    }

    if (ui->selectedUserCB->isChecked()) {
        total += getCmdOut("du -c /home/" + ui->userCleanCB->currentText().toUtf8() +"/.local/share/Trash/* | tail -1 | cut -f1").toInt();
        trash = "rm -r /home/" + ui->userCleanCB->currentText().toUtf8() +"/.local/share/Trash/* 2>/dev/null";
    } else if (ui->allUsersCB->isChecked()){
        total += getCmdOut("find /home/*/.local/share/Trash/* -exec du -sc {} + | tail -1 | cut -f1").toInt();
        trash = "rm -r /home/*/.local/share/Trash/* 2>/dev/null";
    }
    system(trash.toUtf8());

    // cleaup schedule
    QFile::remove("/etc/cron.daily/mx-cleanup");
    QFile::remove("/etc/cron.weekly/mx-cleanup");
    QFile::remove("/etc/cron.monthly/mx-cleanup");
    // add schedule file
    if (!ui->rbNone->isChecked()) {
        QString period;
        cmd_str = cache + "\n" + thumbnails + "\n" + logs + "\n" + apt + "\n" + trash;
        qDebug() << "CMD STR" << cmd_str;
        if (ui->rbDaily->isChecked()) {
            period = "daily";
        } else if (ui->rbWeekly->isChecked()) {
            period = "weekly";
        } else {
            period = "monthly";
        }
        saveSchedule(cmd_str, period);
    }

    setCursor(QCursor(Qt::ArrowCursor));
    QMessageBox::information(this, tr("Done"),
                             tr("Cleanup command done") + "\n" +
                             tr("%1 MiB were freed").arg((total/1000)));
}

// About button clicked
void MainWindow::on_buttonAbout_clicked()
{
    QMessageBox msgBox(QMessageBox::NoIcon,
                       tr("About") + tr("MX Cleanup"), "<p align=\"center\"><b><h2>MX Cleanup</h2></b></p><p align=\"center\">" +
                       tr("Version: ") + VERSION + "</p><p align=\"center\"><h3>" +
                       tr("Quick and safe removal of old files") +
                       "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>");
    QPushButton *btnLicense = msgBox.addButton(tr("License"), QMessageBox::HelpRole);
    QPushButton *btnChangelog = msgBox.addButton(tr("Changelog"), QMessageBox::HelpRole);
    QPushButton *btnCancel = msgBox.addButton(tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme("window-close"));

    msgBox.exec();

    if (msgBox.clickedButton() == btnLicense) {
        QString url = "file:///usr/share/doc/mx-cleanup/license.html";
        if (system("command -v mx-viewer >/dev/null") == 0) { // use mx-viewer if available
            system("mx-viewer " + url.toUtf8() + " \"" + tr("License").toUtf8() + "\"&");
        } else {
            system("su " + user.toUtf8() + " -c \"env XDG_RUNTIME_DIR=/run/user/$(id -u " + user.toUtf8() + ") xdg-open " + url.toUtf8() + "\"&");
        }
    } else if (msgBox.clickedButton() == btnChangelog) {
        QDialog *changelog = new QDialog(this);
        changelog->setWindowTitle(tr("Changelog"));
        changelog->resize(600, 500);

        QTextEdit *text = new QTextEdit;
        text->setReadOnly(true);
        text->setText(getCmdOut("zless /usr/share/doc/" + QFileInfo(QCoreApplication::applicationFilePath()).fileName()  + "/changelog.gz"));

        QPushButton *btnClose = new QPushButton(tr("&Close"));
        btnClose->setIcon(QIcon::fromTheme("window-close"));
        connect(btnClose, &QPushButton::clicked, changelog, &QDialog::close);

        QVBoxLayout *layout = new QVBoxLayout;
        layout->addWidget(text);
        layout->addWidget(btnClose);
        changelog->setLayout(layout);
        changelog->exec();
    }
}

// Help button clicked
void MainWindow::on_buttonHelp_clicked()
{
    QString url = "/usr/share/doc/mx-cleanup/help/mx-cleanup.html";

    if (system("command -v mx-viewer >/dev/null") == 0) { // use mx-viewer if available
        system("mx-viewer " + url.toUtf8() + " \"" + tr("MX Cleanup").toUtf8() + "\"&");
    } else {
        system("su " + user.toUtf8() + " -c \"env XDG_RUNTIME_DIR=/run/user/$(id -u " + user.toUtf8() + ") xdg-open " + url.toUtf8() + "\"&");
    }
}

void MainWindow::on_buttonUsageAnalyzer_clicked()
{
    QString desktop = qgetenv("XDG_CURRENT_DESKTOP");

    if (desktop == "KDE" || desktop == "LXQt") { // try qdirstat for Qt based DEs, otherwise baobab
        if (system("command -v qdirstat") == 0) {
            system("qdirstat&");
        } else {                     // failsafe
            system("baobab&");
        }
    } else {
        if (system("command -v baobab") == 0) {
            system("baobab&");
        } else {
            system("qdirstat&");
        }
    }
}

// util function for getting bash command output
QString MainWindow::getCmdOut(const QString &cmd)
{
    qDebug().noquote() << cmd;
    QProcess *proc = new QProcess();
    QEventLoop loop;
    proc->setReadChannelMode(QProcess::MergedChannels);
    proc->start("/bin/bash", QStringList() << "-c" << cmd);
    proc->waitForFinished();
    QString out = proc->readAll().trimmed();
    delete proc;
    return out;
}
