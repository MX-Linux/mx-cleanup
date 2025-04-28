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
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextEdit>

#include <unistd.h>

#include "about.h"

extern const QString starting_home;

MainWindow::MainWindow(QWidget *parent)
    : QDialog(parent),
      ui(new Ui::MainWindow)
{
    qDebug().noquote() << QApplication::applicationName() << "version:" << QApplication::applicationVersion();
    ui->setupUi(this);
    setConnections();
    setWindowFlags(Qt::Window); // For the close, min and max buttons
    setup();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::addGroupCheckbox(QLayout *layout, const QStringList &packages, const QString &name, QStringList *list)
{
    if (packages.isEmpty()) {
        return;
    }
    auto *grpBox = new QGroupBox(name);
    grpBox->setFlat(true);
    auto *vBox = new QVBoxLayout(grpBox);
    layout->addWidget(grpBox);

    for (const auto &item : packages) {
        auto *btn = new QCheckBox(item);
        vBox->addWidget(btn);
        connect(btn, &QCheckBox::toggled, [btn, list]() {
            if (btn->isChecked()) {
                list->append(btn->text());
            } else {
                list->removeAll(btn->text());
            }
        });
    }
    vBox->addStretch(1);
}

// Setup various items for the first run of the program
void MainWindow::setup()
{
    setWindowTitle(tr("MX Cleanup"));
    ui->tabWidget->setCurrentIndex(0);
    adjustSize();

    current_user = cmdOut("logname", false, true);

    ui->pushApply->setDisabled(true);
    ui->checkCache->setChecked(true);
    ui->checkThumbs->setChecked(true);
    ui->radioAutoClean->setChecked(true);
    ui->radioOldLogs->setChecked(true);
    ui->radioSelectedUser->setChecked(true);

    const QString users = cmdOut("lslogins --noheadings -u -o user | grep -vw root", false, true).trimmed();
    ui->comboUserClean->addItems(users.split('\n'));

    ui->comboUserClean->setCurrentIndex(ui->comboUserClean->findText(current_user));
    ui->pushApply->setEnabled(!ui->comboUserClean->currentText().isEmpty());
    loadSchedule();
}

// Check if the cleanup script exists in the cron directories
void MainWindow::loadSchedule()
{
    const QStringList cronPaths = {"/etc/cron.daily/mx-cleanup", "/etc/cron.weekly/mx-cleanup",
                                   "/etc/cron.monthly/mx-cleanup", "/etc/cron.d/mx-cleanup"};

    if (QFile::exists(cronPaths.at(0))) {
        ui->radioDaily->setChecked(true);
    } else if (QFile::exists(cronPaths.at(1))) {
        ui->radioWeekly->setChecked(true);
    } else if (QFile::exists(cronPaths.at(2))) {
        ui->radioMonthly->setChecked(true);
    } else if (QFile::exists(cronPaths.at(3))) {
        ui->radioReboot->setChecked(true);
    } else {
        ui->radioNone->setChecked(true);
    }
    loadOptions();
}

void MainWindow::loadSettings()
{
    qDebug() << "Load settings";
    int index = ui->comboUserClean->findText(settings.value("User").toString());
    ui->comboUserClean->setCurrentIndex(index == -1 ? 0 : index);

    settings.beginGroup("Folders");
    ui->checkThumbs->setChecked(settings.value("Thumbnails", true).toBool());
    ui->checkCache->setChecked(settings.value("Cache", true).toBool());
    settings.endGroup();

    settings.beginGroup("Apt");
    selectRadioButton(ui->buttonGroupApt, settings.value("AptSelection", -1).toInt());
    settings.endGroup();

    settings.beginGroup("Flatpak");
    ui->checkFlatpak->setChecked(settings.value("UninstallUnusedRuntimes", false).toBool());
    settings.endGroup();

    settings.beginGroup("Logs");
    ui->spinBoxLogs->setValue(settings.value("LogsOlderThan", 7).toInt());
    selectRadioButton(ui->buttonGroupLogs, settings.value("LogsSelection", -1).toInt());
    settings.endGroup();

    settings.beginGroup("Trash");
    ui->spinBoxTrash->setValue(settings.value("TrashOlderThan", 30).toInt());
    selectRadioButton(ui->buttonGroupTrash, settings.value("TrashSelection", -1).toInt());
    settings.endGroup();
}

void MainWindow::removeKernelPackages(const QStringList &list)
{
    if (list.isEmpty()) {
        return;
    }
    setCursor(QCursor(Qt::BusyCursor));
    QStringList headers;
    headers.reserve(list.size());
    QStringList headers_installed;
    QString rmOldVersions;

    for (const auto &item : list) {
        const QString version
            = item.section(QRegularExpression("linux-image-"), 1).remove(QRegularExpression("-unsigned$"));
        if (!version.isEmpty()) {
            headers << "linux-headers-" + version;
            if (QFile::exists("/boot/initrd.img-" + version + ".old-dkms")) {
                rmOldVersions.append("/boot/initrd.img-" + version + ".old-dkms ");
            }
        }
    }

    if (!rmOldVersions.isEmpty()) {
        rmOldVersions = rmOldVersions.trimmed().prepend("rm ").append(";");
    }

    for (const auto &item : std::as_const(headers)) {
        if (system("dpkg -s " + item.toUtf8() + "| grep -q 'Status: install ok installed'") == 0) {
            headers_installed << item;
        }
    }

    QStringList headers_depends;
    QString headers_common;
    QString image_pattern;

    for (const auto &item : std::as_const(headers_installed)) {
        headers_common = cmdOut("env LC_ALL=C.UTF-8 apt-cache depends " + item.toUtf8()
                                + "| grep 'Depends:' | grep -oE 'linux-headers-[0-9][^[:space:]]+' | sort -u");
        if (!headers_common.toUtf8().trimmed().isEmpty()) {
            image_pattern = headers_common;
            image_pattern.remove("-common");
            image_pattern.replace("headers", "image");
            if (system("dpkg -l 'linux-image-[0-9]*' | grep ^ii | cut -d ' ' -f3  | grep -v -E '"
                       + list.join('|').toUtf8() + "' | grep -q " + image_pattern.toUtf8())
                != 0) {
                headers_depends << headers_common;
            }
        }
    }

    QString common;
    if (!headers_depends.isEmpty()) {
        QString filter = "| grep -oE '" + headers_depends.join('|') + "'";
        common = cmdOut("apt-get remove -s " + headers_installed.join(' ') + " | grep '^  ' " + filter
                        + R"( | tr '\n' ' ')");
    }

    QString helper {"/usr/lib/" + QApplication::applicationName() + "/helper-terminal"};
    system("x-terminal-emulator -e pkexec " + helper.toUtf8() + " '" + rmOldVersions.toUtf8() + " apt purge "
           + headers_installed.join(' ').toUtf8() + ' ' + list.join(' ').toUtf8() + ' ' + common.toUtf8()
           + "; apt-get install -f; read -n1 -srp \"" + tr("Press any key to close").toUtf8() + "\"'");
    setCursor(QCursor(Qt::ArrowCursor));
}

// Load saved options to GUI
void MainWindow::loadOptions()
{
    QString period;
    QString file_name = "/usr/bin/mx-cleanup-script";
    if (ui->radioDaily->isChecked()) {
        period = "daily";
    } else if (ui->radioWeekly->isChecked()) {
        period = "weekly";
    } else if (ui->radioMonthly->isChecked()) {
        period = "monthly";
    } else if (ui->radioReboot->isChecked()) {
        period = "reboot";
    } else {
        loadSettings();
        return;
    }

    if (period != "reboot") {
        file_name = "/etc/cron." + period + "/mx-cleanup";
    }

    // Folders
    ui->checkThumbs->setChecked(system(R"(grep -q 'rm -r.*\.cache/thumbnails' )" + file_name.toUtf8()) == 0);
    if (system(R"(grep -qE 'find \/home\/.*\/\.cache(\s|/\*)' )" + file_name.toUtf8()) == 0) {
        ui->checkCache->setChecked(true);
        ui->radioSaferCache->setChecked(system(R"(grep -q 'find.*cache.*-atime' )" + file_name.toUtf8()) == 0);
        ui->radioAllCache->setChecked(!ui->radioSaferCache->isChecked());
    } else {
        ui->checkCache->setChecked(false);
    }

    // APT
    if (system("grep -q 'apt-get autoclean' " + file_name.toUtf8()) == 0) { // detect autoclean
        ui->radioAutoClean->setChecked(true);
    } else if (system("grep -q 'apt-get clean' " + file_name.toUtf8()) == 0) { // detect clean
        ui->radioClean->setChecked(true);
    } else {
        ui->radioNoCleanApt->setChecked(true);
    }

    // Flatpak: remove unused runtimes
    ui->checkFlatpak->setChecked(system("grep -q 'flatpak uninstall --unused' " + file_name.toUtf8()) == 0);

    // Logs
    if (system(R"(grep -q '\-exec sh \-c "echo' )" + file_name.toUtf8()) == 0) { // all logs
        ui->radioAllLogs->setChecked(true);
    } else if (system(R"(grep -q '\-type f \-delete' )" + file_name.toUtf8()) == 0) { // old logs
        ui->radioOldLogs->setChecked(true);
    } else {
        ui->radioNoCleanLogs->setChecked(true);
    }

    // Logs older than...
    QString ctime = cmdOut(
        "grep 'find /var/log' " + file_name + R"( | grep -Eo '\-ctime \+[0-9]{1,3}' | cut -f2 -d' ')", false, true);
    ui->spinBoxLogs->setValue(ctime.toInt());

    // Trash
    if (system(R"(grep -q '/home/\*/.local/share/Trash' )" + file_name.toUtf8()) == 0) { // all user trash
        ui->radioAllUsers->setChecked(true);
    } else if (system("grep -q '/.local/share/Trash' " + file_name.toUtf8()) == 0) { // selected user trash
        ui->radioSelectedUser->setChecked(true);
    } else {
        ui->radioNoCleanTrash->setChecked(true);
    }

    // Trash older than...
    ctime = cmdOut("grep 'find /home/' " + file_name + R"( | grep -Eo '\-ctime \+[0-9]{1,3}' | cut -f2 -d' ')", false,
                   true);
    ui->spinBoxTrash->setValue(ctime.toInt());
}

// Save cleanup commands to a /etc/cron.daily|weekly|monthly/mx-cleanup script
void MainWindow::saveSchedule(const QString &cmd_str, const QString &period)
{
    QString fileName = (period == "@reboot") ? "/usr/bin/mx-cleanup-script" : "/etc/cron." + period + "/mx-cleanup";

    if (period == "@reboot") {
        QString cronFile {"/etc/cron.d/mx-cleanup"};
        QTemporaryFile tempCron;
        tempCron.open();
        tempCron.write("@reboot root /usr/bin/mx-cleanup-script\n");
        tempCron.close();
        cmdOutAsRoot("mv " + tempCron.fileName() + ' ' + cronFile);
        cmdOutAsRoot("chown root: " + cronFile);
        cmdOutAsRoot("chmod +r " + cronFile);
    }

    QTemporaryFile tempFile;
    tempFile.open();
    QTextStream out(&tempFile);
    out << "#!/bin/sh\n";
    out << "#\n";
    out << "# This file was created by MX Cleanup\n";
    out << "#\n\n";
    out << cmd_str;
    tempFile.close();
    cmdOutAsRoot("mv " + tempFile.fileName() + " " + fileName);
    cmdOutAsRoot("chmod +rx " + fileName);
    cmdOutAsRoot("chown root: " + fileName);
}

void MainWindow::saveSettings()
{
    settings.setValue("User", ui->comboUserClean->currentText());

    settings.beginGroup("Folders");
    settings.setValue("Thumbnails", ui->checkThumbs->isChecked());
    settings.setValue("Cache", ui->checkCache->isChecked());
    settings.endGroup();

    settings.beginGroup("Apt");
    settings.setValue("AptSelection", ui->buttonGroupApt->checkedId());
    settings.endGroup();

    settings.beginGroup("Logs");
    settings.setValue("LogsSelection", ui->buttonGroupLogs->checkedId());
    settings.setValue("LogsOlderThan", ui->spinBoxLogs->value());
    settings.endGroup();

    settings.beginGroup("Trash");
    settings.setValue("TrashSelection", ui->buttonGroupTrash->checkedId());
    settings.setValue("TrashOlderThan", ui->spinBoxTrash->value());
    settings.endGroup();

    settings.beginGroup("Flatpak");
    settings.setValue("UninstallUnusedRuntimes", ui->checkFlatpak->isChecked());
    settings.endGroup();
}

void MainWindow::selectRadioButton(const QButtonGroup *group, int id)
{
    if (id != -1) {
        auto *selectedButton = group->button(id);
        if (selectedButton) {
            selectedButton->setChecked(true);
        }
    }
}

void MainWindow::setConnections()
{
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::pushAbout_clicked);
    connect(ui->pushApply, &QPushButton::clicked, this, &MainWindow::pushApply_clicked);
    connect(ui->pushCancel, &QPushButton::clicked, this, &MainWindow::close);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::pushHelp_clicked);
    connect(ui->pushKernel, &QPushButton::clicked, this, &MainWindow::pushKernel_clicked);
    connect(ui->pushRTLremove, &QPushButton::clicked, this, &MainWindow::pushRTLremove_clicked);
    connect(ui->pushUsageAnalyzer, &QPushButton::clicked, this, &MainWindow::pushUsageAnalyzer_clicked);
    connect(ui->radioNoCleanLogs, &QRadioButton::toggled, ui->spinBoxLogs, &QSpinBox::setDisabled);
    connect(ui->radioNoCleanTrash, &QRadioButton::toggled, ui->spinBoxTrash, &QSpinBox::setDisabled);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) {
            ui->pushApply->setDisabled(true);
        } else {
            setup();
        }
    });

    for (auto *spinBox : {ui->spinCache, ui->spinBoxLogs, ui->spinBoxTrash}) {
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [spinBox]() { spinBox->setSuffix(spinBox->value() > 1 ? tr(" days") : tr(" day")); });
    }
}

void MainWindow::pushApply_clicked()
{
    setCursor(QCursor(Qt::BusyCursor));

    quint64 total {};
    QString cache;
    if (ui->checkCache->isChecked()) {
        QString period = ui->radioSaferCache->isChecked()
                             ? QString(" -atime +%1 -mtime +%1").arg(ui->spinCache->value())
                             : QString();
        total
            = cmdOut(QString("find /home/%1/.cache -type d -name 'thumbnails' -prune -o -type f%2 -exec du -sc '{}' + "
                             "| awk '{field = $1} END {print field}'")
                         .arg(ui->comboUserClean->currentText(), period))
                  .toULongLong();
        cache = QString("find /home/%1/.cache -mindepth 1 ! -path '/home/%1/.cache/thumbnails*'%2 -delete")
                    .arg(ui->comboUserClean->currentText(), period);
        if (!ui->radioReboot->isChecked()) {
            cmdOut(cache);
        }
    }

    QString thumbnails;
    if (ui->checkThumbs->isChecked()) {
        total += cmdOut(QString("du -c /home/%1/.cache/thumbnails/* | awk '{field = $1} END {print field}'")
                            .arg(ui->comboUserClean->currentText()))
                     .toULongLong();
        thumbnails = QString("rm -r /home/%1/.cache/thumbnails/* 2>/dev/null").arg(ui->comboUserClean->currentText());
        if (!ui->radioReboot->isChecked()) {
            cmdOut(thumbnails);
        }
    }

    QString flatpak;
    if (ui->checkFlatpak->isChecked()) {
        flatpak = "pgrep -a flatpak | grep -v flatpak-s || flatpak uninstall --unused --noninteractive";
        QString user_size = "du -s /home/$(logname)/.local/share/flatpak/ | cut -f1";
        QString system_size = "du -s /var/lib/flatpak/ | cut -f1";
        total += cmdOut(user_size).toULongLong();
        bool ok {false};
        qulonglong system_size_num = cmdOutAsRoot(system_size).toULongLong(&ok);
        if (ok) {
            total += system_size_num;
        }
        if (!ui->radioReboot->isChecked()) {
            cmdOut(flatpak);
        }
        total -= cmdOut(user_size).toULongLong();
        system_size_num = cmdOutAsRoot(system_size).toULongLong(&ok);
        if (ok) {
            total -= system_size_num;
        }
    }

    QString apt;
    if (ui->radioAutoClean->isChecked()) {
        apt = "apt-get autoclean";
    } else if (ui->radioClean->isChecked()) {
        apt = "apt-get clean";
    }

    total += cmdOutAsRoot("du -s /var/cache/apt/archives/ | cut -f1").toULongLong();
    if (!ui->radioReboot->isChecked()) {
        cmdOutAsRoot(apt);
    }
    total -= cmdOutAsRoot("du -s /var/cache/apt/archives/ | cut -f1").toULongLong();

    QString time
        = ui->spinBoxLogs->value() > 0 ? QString(" -ctime +%1 -atime +%1").arg(ui->spinBoxLogs->value()) : QString();
    QString logs;
    if (ui->radioOldLogs->isChecked()) {
        total
            += cmdOutAsRoot(
                   R"(find /var/log \( -name "*.gz" -o -name "*.old" -o -name "*.[0-9]" -o -name "*.[0-9].log" \) -type f)"
                   + time + " -exec du -sc '{}' + | awk '{field = $1} END {print field}'")
                   .toULongLong();
        logs = R"(find /var/log \( -name "*.gz" -o -name "*.old" -o -name "*.[0-9]" -o -name "*.[0-9].log" \))" + time
               + " -type f -delete 2>/dev/null";
        cmdOutAsRoot(logs);
    } else if (ui->radioAllLogs->isChecked()) {
        total += cmdOutAsRoot(
                     QString("find /var/log -type f%1 -exec du -sc '{}' + | awk '{field = $1} END {print field}'")
                         .arg(time))
                     .toULongLong();
        logs = "find /var/log -type f" + time + R"( -exec sh -c "echo > '{}'" \;)"; // empty the logs
        cmdOutAsRoot(logs);
    }

    QString trash;
    if (ui->radioSelectedUser->isChecked() || ui->radioAllUsers->isChecked()) {
        QString user = ui->radioAllUsers->isChecked() ? "*" : ui->comboUserClean->currentText();
        QString timeTrash = ui->spinBoxTrash->value() > 0
                                ? QString(" -ctime +%1 -atime +%1").arg(ui->spinBoxTrash->value())
                                : QString();
        total += cmdOut(QString("find /home/%1/.local/share/Trash -mindepth 1%2 -exec du -sc '{}' + | awk '{field = "
                                "$1} END {print field}'")
                            .arg(user, timeTrash))
                     .toULongLong();
        trash = QString("find /home/%1/.local/share/Trash -mindepth 1%2 -delete").arg(user, timeTrash);
        if (!ui->radioReboot->isChecked()) {
            cmdOut(trash);
        }
    }

    // Cleanup schedule
    cmdOutAsRoot("rm /etc/cron.daily/mx-cleanup");
    cmdOutAsRoot("rm /etc/cron.weekly/mx-cleanup");
    cmdOutAsRoot("rm /etc/cron.monthly/mx-cleanup");
    cmdOutAsRoot("rm /etc/cron.d/mx-cleanup");

    // Add schedule file
    if (!ui->radioNone->isChecked()) {
        QStringList parts {cache, thumbnails, logs, apt, trash, flatpak};
        QString cmd_str = parts.join("\n");
        qDebug() << "CMD STR" << cmd_str;
        QString schedule;
        if (ui->radioDaily->isChecked()) {
            schedule = "daily";
        } else if (ui->radioWeekly->isChecked()) {
            schedule = "weekly";
        } else if (ui->radioMonthly->isChecked()) {
            schedule = "monthly";
        } else if (ui->radioReboot->isChecked()) {
            schedule = "@reboot";
        }
        saveSchedule(cmd_str, schedule);
    }

    saveSettings();

    setCursor(QCursor(Qt::ArrowCursor));
    QMessageBox::information(this, tr("Done"),
                             ui->radioReboot->isChecked()
                                 ? tr("Cleanup script will run at reboot")
                                 : tr("Cleanup command done") + '\n' + tr("%1 MiB were freed").arg(total / 1024));
}

void MainWindow::pushAbout_clicked()
{
    this->hide();
    displayAboutMsgBox(
        tr("About") + tr("MX Cleanup"),
        R"(<p align="center"><b><h2>MX Cleanup</h2></b></p><p align="center">)" + tr("Version: ")
            + QApplication::applicationVersion() + "</p><p align=\"center\"><h3>"
            + tr("Quick and safe removal of old files")
            + R"(</h3></p><p align="center"><a href="http://mxlinux.org">http://mxlinux.org</a><br /></p><p align="center">)"
            + tr("Copyright (c) MX Linux") + "<br /><br /></p>",
        "/usr/share/doc/mx-cleanup/license.html", tr("%1 License").arg(this->windowTitle()));
    this->show();
}

void MainWindow::pushHelp_clicked()
{
    const QString url {"/usr/share/doc/mx-cleanup/mx-cleanup.html"};
    displayDoc(url, tr("%1 Help").arg(this->windowTitle()));
}

void MainWindow::pushUsageAnalyzer_clicked()
{
    const QString desktop = qgetenv("XDG_CURRENT_DESKTOP");
    if (desktop == "KDE" || desktop == "LXQt") {
        startPreferredApp({"filelight", "qdirstat", "baobab"});
    } else {
        startPreferredApp({"baobab", "filelight", "qdirstat"});
    }
}

void MainWindow::startPreferredApp(const QStringList &apps)
{
    for (const auto &app : apps) {
        if (!QStandardPaths::findExecutable(app).isEmpty()) {
            QProcess::startDetached(app, {});
            break;
        }
    }
}

QString MainWindow::cmdOut(const QString &cmd, bool asRoot, bool quiet)
{
    if (!quiet) {
        qDebug().noquote() << cmd;
    }
    QProcess proc;
    QEventLoop loop;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    if (asRoot && getuid() != 0) {
        QString elevate {QFile::exists("/usr/bin/pkexec") ? "/usr/bin/pkexec" : "/usr/bin/gksu"};
        QString helper {"/usr/lib/" + QApplication::applicationName() + "/helper"};
        proc.start(elevate, {helper, cmd});
    } else {
        proc.start("/bin/bash", {"-c", cmd});
    }
    loop.exec();
    return proc.readAll().trimmed();
}

QString MainWindow::cmdOutAsRoot(const QString &cmd, bool quiet)
{
    return cmdOut(cmd, true, quiet);
}

void MainWindow::pushKernel_clicked()
{
    QString current_kernel = cmdOut("uname -r").trimmed();
    QStringList similar_kernels;
    QStringList other_kernels;
    QStringList installedKernels = cmdOut("dpkg -l 'linux-image-[0-9]*' | grep ^ii").split('\n', Qt::SkipEmptyParts);

    if (!installedKernels.isEmpty()) {
        QRegularExpression regex_version("^[0-9]+[.][0-9]+([.][0-9]+)?");
        QRegularExpressionMatch match = regex_version.match(current_kernel);
        QString current_kernel_version = match.hasMatch() ? match.captured(0) : QString();

        QString similarKernelsCmd = R"STR(dpkg-query -f '${db:Status-Abbrev}${Package}\n' -W 'linux-image-[0-9]*' |
    grep ^ii | cut -c4- | grep '^linux-image-)STR"
                                    + current_kernel_version + R"STR(' |
    grep -v '^linux-image-)STR" + current_kernel
                                    + R"STR(-unsigned$' |
    grep -v '^linux-image-)STR" + current_kernel
                                    + R"STR(' |
    sort -rV
    )STR";

        QString otherKernelsCmd = R"STR(dpkg-query -f '${db:Status-Abbrev}${Package}\n' -W 'linux-image-[0-9]*' |
    grep ^ii | cut -c4- | grep -v '^linux-image-)STR"
                                  + current_kernel_version + R"STR(' |
    sort -rV
    )STR";

        similar_kernels = cmdOut(similarKernelsCmd).split('\n', Qt::SkipEmptyParts);
        other_kernels = cmdOut(otherKernelsCmd).split('\n', Qt::SkipEmptyParts);
    }

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(this->windowTitle());
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(tr("Kernel currently in use: <b>%1</b>").arg(current_kernel)));

    auto *btnBox = new QDialogButtonBox(dialog);
    auto *pushRemove = new QPushButton(tr("Remove selected"));
    btnBox->addButton(pushRemove, QDialogButtonBox::AcceptRole);
    btnBox->addButton(tr("Close"), QDialogButtonBox::RejectRole);

    QStringList removal_list;
    addGroupCheckbox(layout, similar_kernels, tr("Similar kernels that can be removed:"), &removal_list);
    addGroupCheckbox(layout, other_kernels, tr("Other kernels that can be removed:"), &removal_list);

    if (layout->count() == 1) {
        layout->addWidget(new QLabel(tr("<b>Nothing to remove.</b> Cannot remove kernel in use.")));
        pushRemove->setHidden(true);
    } else {
        pushRemove->setHidden(false);
    }

    layout->addStretch(1);
    layout->addWidget(btnBox);

    connect(btnBox, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(btnBox, &QDialogButtonBox::accepted, this, [this, &removal_list] { removeKernelPackages(removal_list); });
    connect(btnBox, &QDialogButtonBox::accepted, dialog, &QDialog::close);

    dialog->exec();
}

void MainWindow::pushRTLremove_clicked()
{
    setCursor(QCursor(Qt::BusyCursor));
    QString dumpList = cmdOut(R"(
    for module in 8812au 8814au 8821au 8821cu rtl8821ce; do
        if ! lsmod | grep -q $module; then
            modname="${module}"
            [[ "${module}" != "rtl"* ]] && modname="rtl${module}"
            echo -n "${modname}-dkms "
        fi
    done
    if ! lsmod | grep -q -w ^wl; then
        echo -n broadcom-sta-dkms
    fi)");

    QString helper {"/usr/lib/" + QApplication::applicationName() + "/helper-terminal"};
    system("x-terminal-emulator -e pkexec " + helper.toUtf8() + " 'apt purge " + dumpList.toUtf8()
           + "; apt-get install -f; read -n1 -srp \"" + tr("Press any key to close").toUtf8() + "\"'");
    setCursor(QCursor(Qt::ArrowCursor));
}
