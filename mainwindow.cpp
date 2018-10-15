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

#include <QTextEdit>
#include <QFileInfo>
#include <QDebug>


MainWindow::MainWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setup();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// Setup versious items first time program runs
void MainWindow::setup()
{
    cmd = new Cmd(this);
    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::cleanup);
    this->setWindowTitle(tr("MX Cleanup"));
    this->adjustSize();
    user = cmd->getOutput("logname", QStringList("quiet"));
    ui->buttonApply->setDisabled(true);
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
    ui->userCleanCB->setCurrentIndex(ui->userCleanCB->findText(user));
    ui->buttonApply->setEnabled(ui->userCleanCB->currentText() != "");
    checkSchedule();
}

// Cleanup environment when window is closed
void MainWindow::cleanup()
{

}


// Get version of the program
QString MainWindow::getVersion(QString name)
{
    Cmd cmd;
    return cmd.getOutput("dpkg-query -f '${Version}' -W " + name);
}

// check if /etc/cron.daily|weekly|monthly/mx-cleanup script exists
void MainWindow::checkSchedule()
{
    if (QFile::exists("/etc/cron.daily/mx-cleanup")) {
        ui->rbDaily->setChecked(true);
    } else if (QFile::exists("/etc/cron.weekly/mx-cleanup")) {
        ui->rbWeekly->setChecked(true);
    } else if (QFile::exists("/etc/cron.monthly/mx-cleanup")) {
        ui->rbMonthly->setChecked(true);
    } else {
        ui->rbNone->setChecked(true);
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
    cmd->run("chmod +x " + file.fileName());
}


void MainWindow::on_buttonApply_clicked()
{
    int total = 0;
    setCursor(QCursor(Qt::BusyCursor));

    QString cmd_str;
    QString cache, thumbnails, logs, apt, trash;

//  Too aggressive
//    if (ui->tmpCheckBox->isChecked()) {
//        total +=  cmd->getOutput("du -c /tmp/* | tail -1 | cut -f1").toInt();
//        cmd->run("rm -r /tmp/* 2>/dev/null");
//    }

    if (ui->cacheCheckBox->isChecked()) {
        total += cmd->getOutput("du -c /home/" + ui->userCleanCB->currentText().toUtf8() + "/.cache/* | tail -1 | cut -f1").toInt();
        cache = "rm -r /home/" + ui->userCleanCB->currentText().toUtf8() + "/.cache/* 2>/dev/null";
        cmd->run(cache);
    }

    if (ui->thumbCheckBox->isChecked()) {
        total += cmd->getOutput("du -c /home/" + ui->userCleanCB->currentText().toUtf8() + "/.thumbnails/* | tail -1 | cut -f1").toInt();
        thumbnails = "rm -r /home/" + ui->userCleanCB->currentText().toUtf8() + "/.thumbnails/* 2>/dev/null";
        cmd->run(thumbnails);
    }

    if (ui->autocleanRB->isChecked()) {
        apt = "apt-get autoclean";
    } else {
        apt = "apt-get clean";
    }
    total += cmd->getOutput("du -s /var/cache/apt/archives/ | cut -f1").toInt();
    cmd->run(apt);
    total -= cmd->getOutput("du -s /var/cache/apt/archives/ | cut -f1").toInt();

    if (ui->oldLogsRB->isChecked()) {
        total += cmd->getOutput("find /var/log \\( -name \"*.gz\" -o -name \"*.old\" -o -name \"*.1\" \\) -type f -exec du -sc {} + | tail -1 | cut -f1").toInt();
        logs = "find /var/log \\( -name \"*.gz\" -o -name \"*.old\" -o -name \"*.1\" \\) -type f -delete 2>/dev/null";
        cmd->run(logs);
    } else {
        total += cmd->getOutput("du -s /var/log/ | cut -f1").toInt();
        logs = "find /var/log -type f -exec sh -c \"echo > '{}'\" \\;";  // empty the logs
        cmd->run(logs);
        total -= cmd->getOutput("du -s /var/log/ | cut -f1").toInt();
    }

    if (ui->selectedUserCB->isChecked()) {
        total += cmd->getOutput("du -c /home/" + ui->userCleanCB->currentText().toUtf8() +"/.local/share/Trash/* | tail -1 | cut -f1").toInt();
        trash = "rm -r /home/" + ui->userCleanCB->currentText().toUtf8() +"/.local/share/Trash/* 2>/dev/null";
    } else {
        total += cmd->getOutput("find /home/*/.local/share/Trash/* -exec du -sc {} + | tail -1 | cut -f1").toInt();
        trash = "rm -r /home/*/.local/share/Trash/* 2>/dev/null";
    }
    cmd->run(trash);

    // cleaup schedule
    QFile::remove("/etc/cron.daily/mx-cleanup");
    QFile::remove("/etc/cron.weekly/mx-cleanup");
    QFile::remove("/etc/cron.monthly/mx-cleanup");
    // add schedule file
    if (!ui->rbNone->isChecked()) {
        QString period;
        cmd_str = cache + "\n" + thumbnails + "\n" + logs + "\n" + apt + "\n" + trash;
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
                       tr("Version: ") + getVersion("mx-cleanup") + "</p><p align=\"center\"><h3>" +
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
        if (system("command -v mx-viewer") == 0) { // use mx-viewer if available
            system("su " + user.toUtf8() + " -c \"mx-viewer " + url.toUtf8() + " " + tr("License").toUtf8() + "\"&");
        } else {
            system("su " + user.toUtf8() + " -c \"xdg-open " + url.toUtf8() + "\"&");
        }
    } else if (msgBox.clickedButton() == btnChangelog) {
        QDialog *changelog = new QDialog(this);
        changelog->resize(600, 500);

        QTextEdit *text = new QTextEdit;
        text->setReadOnly(true);
        Cmd cmd;
        text->setText(cmd.getOutput("zless /usr/share/doc/" + QFileInfo(QCoreApplication::applicationFilePath()).fileName()  + "/changelog.gz"));

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
    QString url = "https://mxlinux.org/wiki/help-files/help-mx-cleanup";
    QString exec = "xdg-open";
    if (system("command -v mx-viewer") == 0) { // use mx-viewer if available
        exec = "mx-viewer";
        url += " " + tr("MX Cleanup");
    }
    QString cmd = QString("su " + user + " -c \"" + exec + " " + url + "\"&");
    system(cmd.toUtf8());
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
