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

#include <QDebug>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QProcess>
#include <QTextEdit>

#include "about.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

extern const QString starting_home;

MainWindow::MainWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MainWindow)
{
    qDebug().noquote() << qApp->applicationName() << "version:" << qApp->applicationVersion();
    ui->setupUi(this);
    setConnections();
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    setup();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::addGroupCheckbox(QLayout *layout, const QString &package, const QString &name, QStringList &list)
{
    if (package.isEmpty())
        return;
    auto *grpBox = new QGroupBox(name);
    grpBox->setFlat(true);
    auto *vBox = new QVBoxLayout;
    grpBox->setLayout(vBox);
    layout->addWidget(grpBox);
    for (const auto &item : package.split(QStringLiteral("\n"))) {
        auto *btn = new QCheckBox(item);
        vBox->addWidget(btn);
        connect(btn, &QCheckBox::toggled, [btn, &list]() {
            if (btn->isChecked())
                list << btn->text();
            else
                list.removeAll(btn->text());
        });
    }
    vBox->addStretch(1);
}

// Setup versious items first time program runs
void MainWindow::setup()
{
    this->setWindowTitle(tr("MX Cleanup"));
    this->adjustSize();

    user = getCmdOut(QStringLiteral("logname"));

    ui->pushApply->setDisabled(true);
    ui->checkCache->setChecked(true);
    ui->checkThumbs->setChecked(true);
    ui->radioAutoClean->setChecked(true);
    ui->radioOldLogs->setChecked(true);
    ui->radioSelectedUser->setChecked(true);

    const QString users = getCmdOut(QStringLiteral("lslogins --noheadings -u -o user | grep -vw root"));

    qDebug() << users;
    ui->comboUserClean->addItems(users.split(QStringLiteral("\n")));

    ui->comboUserClean->setCurrentIndex(ui->comboUserClean->findText(user));
    ui->pushApply->setEnabled(!ui->comboUserClean->currentText().isEmpty());
    loadSchedule();
}

// check if /etc/cron.daily|weekly|monthly/mx-cleanup script exists
void MainWindow::loadSchedule()
{
    if (QFile::exists(QStringLiteral("/etc/cron.daily/mx-cleanup")))
        ui->radioDaily->setChecked(true);
    else if (QFile::exists(QStringLiteral("/etc/cron.weekly/mx-cleanup")))
        ui->radioWeekly->setChecked(true);
    else if (QFile::exists(QStringLiteral("/etc/cron.monthly/mx-cleanup")))
        ui->radioMonthly->setChecked(true);
    else if (QFile::exists(QStringLiteral("/etc/cron.d/mx-cleanup")))
        ui->radioReboot->setChecked(true);
    else
        ui->radioNone->setChecked(true);
    loadOptions();
}

void MainWindow::loadSettings()
{
    qDebug() << "Load settings";
    int index = ui->comboUserClean->findText(settings.value(QStringLiteral("User")).toString());
    if (index == -1) index = 0;
    ui->comboUserClean->setCurrentIndex(index);

    settings.beginGroup(QStringLiteral("Folders"));
    ui->checkThumbs->setChecked(settings.value(QStringLiteral("Thumbnails"), true).toBool());
    ui->checkCache->setChecked(settings.value(QStringLiteral("Cache"), true).toBool());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Apt"));
    selectRadioButton(ui->buttonGroupApt, settings.value(QStringLiteral("AptSelection"), -1).toInt());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Flatpak"));
    ui->checkFlatpak->setChecked(settings.value(QStringLiteral("UninstallUnusedRuntimes"), false).toBool());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Logs"));
    ui->spinBoxLogs->setValue(settings.value(QStringLiteral("LogsOlderThan"), 7).toInt());
    selectRadioButton(ui->buttonGroupLogs, settings.value(QStringLiteral("LogsSelection"), -1).toInt());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Trash"));
    ui->spinBoxTrash->setValue(settings.value(QStringLiteral("TrashOlderThan"), 30).toInt());
    selectRadioButton(ui->buttonGroupTrash, settings.value(QStringLiteral("TrashSelection"), -1).toInt());
    settings.endGroup();
}

void MainWindow::removeKernelPackages(const QStringList &list)
{
    if (list.isEmpty())
        return;
    setCursor(QCursor(Qt::BusyCursor));
    QStringList headers;
    QStringList headers_installed;
    for (const auto &item : list) {
        const QString version = item.section(QRegularExpression(QStringLiteral("linux-image-")), 1)
                .remove(QRegularExpression(QStringLiteral("-unsigned$")));
        headers << "linux-headers-" + version;
        QFile::remove("/boot/initrd.img-" + version + ".old-dkms");
    }
    for (const auto &item : qAsConst(headers)) {
        if (system("dpkg -s " + item.toUtf8() + "| grep -q 'Status: install ok installed'") == 0)
            headers_installed << item;
    }
    QStringList headers_depends;
    QString headers_common;
    QString image_pattern;
    for (const auto &item : qAsConst(headers_installed)) {
        headers_common = getCmdOut("env LC_ALL=C.UTF-8 apt-cache depends " + item.toUtf8() +
                                   "| grep 'Depends:' | grep -oE 'linux-headers-[0-9][^[:space:]]+' | sort -u");
        if (!headers_common.toUtf8().trimmed().isEmpty()) {
            image_pattern = headers_common;
            image_pattern.replace(QLatin1String("-common"), QLatin1String(""));
            image_pattern.replace(QLatin1String("headers"), QLatin1String("image"));
            if (system("dpkg -l 'linux-image-[0-9]*' | grep ^ii | cut -d ' ' -f3  | grep -v -E '" +
                       list.join(QStringLiteral("|")).toUtf8() + "' | grep -q " +  image_pattern.toUtf8()) != 0)
                headers_depends << headers_common;
        }
    }
    QString filter;
    QString common;
    if (!headers_depends.isEmpty()) {
        filter = "| grep -oE '" + headers_depends.join(QStringLiteral("|")) + "'";
        common = getCmdOut("apt-get remove -s " + headers_installed.join(QStringLiteral(" ")) +
                           " | grep '^  ' " + filter + R"( | tr '\n' ' ')");
    }
    system("x-terminal-emulator -e bash -c 'apt purge " + headers_installed.join(QStringLiteral(" ")).toUtf8() + " " +
           list.join(QStringLiteral(" ")).toUtf8() +  " " + common.toUtf8() + "; apt-get install -f'");
    setCursor(QCursor(Qt::ArrowCursor));
}

// Load saved options to GUI
void MainWindow::loadOptions()
{
    QString period;
    QString file_name = QStringLiteral("/usr/bin/mx-cleanup-script");
    if (ui->radioDaily->isChecked()) {
        period = QStringLiteral("daily");
    } else if (ui->radioWeekly->isChecked()) {
        period = QStringLiteral("weekly");
    } else if (ui->radioMonthly->isChecked()) {
        period = QStringLiteral("monthly");
    } else if (ui->radioReboot->isChecked()) {
        period = QStringLiteral("reboot");
    } else {
        loadSettings();
        return;
    }

    if (period != QLatin1String("reboot"))
        file_name = "/etc/cron." + period + "/mx-cleanup";

    // Folders
    ui->checkThumbs->setChecked(system(R"(grep -q '\.thumbnails' )" + file_name.toUtf8()) == 0);
    if (system(R"(grep -q '\.cache' )" + file_name.toUtf8()) == 0) {
        ui->checkCache->setChecked(true);
        if (system("grep -q 'rm.*cache' " + file_name.toUtf8()) == 0)
            ui->radioAllCache->setChecked(true);
        else
            ui->radioSaferCache->setChecked(true);
    } else {
        ui->checkCache->setChecked(false);
    }

    // APT
    if (system("grep -q 'apt-get autoclean' " + file_name.toUtf8()) == 0)  // detect autoclean
        ui->radioAutoClean->setChecked(true);
    else if (system("grep -q 'apt-get clean' " + file_name.toUtf8()) == 0)  // detect clean
        ui->radioClean->setChecked(true);
    else
        ui->radioNoCleanApt->setChecked(true);

    // Flatpak: remove unused runtiles
    ui->checkFlatpak->setChecked(system("grep -q 'flatpak uninstall --unused' " + file_name.toUtf8()) == 0);

    // Logs
    if (system(R"(grep -q '\-exec sh \-c "echo' )" + file_name.toUtf8()) == 0) // all logs
        ui->radioAllLogs->setChecked(true);
    else if (system(R"(grep -q '\-type f \-delete' )" + file_name.toUtf8()) == 0) // old logs
        ui->radioOldLogs->setChecked(true);
    else
        ui->radioNoCleanLogs->setChecked(true);

    // Logs older than...
    QString ctime = getCmdOut("grep 'find /var/log' " + file_name +
                              R"( | grep -Eo '\-ctime \+[0-9]{1,3}' | cut -f2 -d' ')");
    ui->spinBoxLogs->setValue(ctime.toInt());

    // Trash
    if (system(R"(grep -q '/home/\*/.local/share/Trash' )" + file_name.toUtf8()) == 0)  // all user trash
        ui->radioAllUsers->setChecked(true);
    else if (system("grep -q '/.local/share/Trash' " + file_name.toUtf8()) == 0)  // selected user trash
        ui->radioSelectedUser->setChecked(true);
    else
        ui->radioNoCleanTrash->setChecked(true);

    // Trash older than...
    ctime = getCmdOut("grep 'find /home/' " + file_name +
                      R"( | grep -Eo '\-ctime \+[0-9]{1,3}' | cut -f2 -d' ')");
    ui->spinBoxTrash->setValue(ctime.toInt());
}


// Save cleanup commands to a /etc/cron.daily|weekly|monthly/mx-cleanup script
void MainWindow::saveSchedule(const QString &cmd_str, const QString &period)
{
    QFile file;
    if (period == QLatin1String("@reboot")) {
        file.setFileName(QStringLiteral("/usr/bin/mx-cleanup-script"));
        QFile cronfile(QStringLiteral("/etc/cron.d/mx-cleanup"));
        cronfile.open(QFile::WriteOnly | QFile::Truncate);
        cronfile.write("@reboot root /usr/bin/mx-cleanup-script\n");
        cronfile.close();
    } else {
        file.setFileName("/etc/cron." + period + "/mx-cleanup");
    }
    if (!file.open(QFile::WriteOnly))
        qDebug() << "Could not open file:" << file.fileName();
    QTextStream out(&file);
    out << "#!/bin/sh\n";
    out << "#\n";
    out << "# This file was created by MX Cleanup\n";
    out << "#\n\n";
    out << cmd_str;
    file.close();


    file.setPermissions(QFlag(0x755));
}

void MainWindow::saveSettings()
{
    settings.setValue(QStringLiteral("User"), ui->comboUserClean->currentText());

    settings.beginGroup(QStringLiteral("Folders"));
    settings.setValue(QStringLiteral("Thumbnails"), ui->checkThumbs->isChecked());
    settings.setValue(QStringLiteral("Cache"), ui->checkCache->isChecked());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Apt"));
    settings.setValue(QStringLiteral("AptSelection"), ui->buttonGroupApt->checkedId());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Logs"));
    settings.setValue(QStringLiteral("LogsSelection"), ui->buttonGroupLogs->checkedId());
    settings.setValue(QStringLiteral("LogsOlderThan"), ui->spinBoxLogs->value());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Trash"));
    settings.setValue(QStringLiteral("TrashSelection"), ui->buttonGroupTrash->checkedId());
    settings.setValue(QStringLiteral("TrashOlderThan"), ui->spinBoxTrash->value());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Flatpak"));
    settings.setValue(QStringLiteral("UninstallUnusedRuntimes"), ui->checkFlatpak->isChecked());
    settings.endGroup();
}

void MainWindow::selectRadioButton(const QButtonGroup *group, int id)
{
    if (id != -1) {
        for (auto *button : group->buttons()) {
            if (group->id(button) == id) {
                button->setChecked(true);
                break;
            }
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
    connect(ui->pushUsageAnalyzer, &QPushButton::clicked, this, &MainWindow::pushUsageAnalyzer_clicked);
    connect(ui->radioNoCleanLogs, &QRadioButton::toggled, ui->spinBoxLogs, &QSpinBox::setDisabled);
    connect(ui->radioNoCleanTrash, &QRadioButton::toggled, ui->spinBoxTrash, &QSpinBox::setDisabled);
    connect(ui->spinCache, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        (ui->spinCache->value() > 1) ? ui->spinCache->setSuffix(tr(" days")) :  ui->spinCache->setSuffix(tr(" day"));});
    connect(ui->spinBoxLogs, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        (ui->spinBoxLogs->value() > 1) ? ui->spinBoxLogs->setSuffix(tr(" days")) :  ui->spinBoxLogs->setSuffix(tr(" day"));});
    connect(ui->spinBoxTrash, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        (ui->spinBoxTrash->value() > 1) ? ui->spinBoxTrash->setSuffix(tr(" days")) :  ui->spinBoxTrash->setSuffix(tr(" day"));});
}

void MainWindow::pushApply_clicked()
{
    quint64 total = 0;
    setCursor(QCursor(Qt::BusyCursor));

    QString apt;
    QString cache;
    QString cmd_str;
    QString flatpak;
    QString logs;
    QString thumbnails;
    QString trash;

    if (ui->checkCache->isChecked() && ui->radioAllCache->isChecked()) {
        total += getCmdOut("du -c /home/" + ui->comboUserClean->currentText().toUtf8() +
                           "/.cache/* | tail -1 | cut -f1").toULongLong();
        cache = "rm -r /home/" + ui->comboUserClean->currentText().toUtf8() + "/.cache/* 2>/dev/null";
        system(cache.toUtf8());
    } else if (ui->checkCache->isChecked() && ui->radioSaferCache->isChecked()) {
        QString days = QString::number(ui->spinCache->value());
        total += getCmdOut("find /home/" + ui->comboUserClean->currentText().toUtf8() +
                           "/.cache/ -type f -atime +" + days + " -mtime +" + days +
                           " -exec du -sc '{}' + | tail -1 | cut -f1").toULongLong();
        cache = "find /home/" + ui->comboUserClean->currentText().toUtf8() + "/.cache/ -type f -atime +" +
                days + " -mtime +" + days + " -delete";
        system(cache.toUtf8());
    }

    if (ui->checkThumbs->isChecked()) {
        total += getCmdOut("du -c /home/" + ui->comboUserClean->currentText().toUtf8() +
                           "/.thumbnails/* | tail -1 | cut -f1").toULongLong();
        thumbnails = "rm -r /home/" + ui->comboUserClean->currentText().toUtf8() +
                "/.thumbnails/* 2>/dev/null";
        system(thumbnails.toUtf8());
    }

    if (ui->checkFlatpak->isChecked()) {
        flatpak = QStringLiteral("pgrep -a flatpak | grep -v flatpak-system-helper || flatpak uninstall --unused --noninteractive");
        QString user_size = QStringLiteral("du -s /home/$(logname)/.local/share/flatpak/ | cut -f1");
        QString system_size = QStringLiteral("du -s /var/lib/flatpak/ | cut -f1");
        total += getCmdOut(user_size).toULongLong();
        total += getCmdOut(system_size).toULongLong();
        system(flatpak.toUtf8());
        total -= getCmdOut(user_size).toULongLong();
        total -= getCmdOut(system_size).toULongLong();
    }

    if (ui->radioAutoClean->isChecked())
        apt = QStringLiteral("apt-get autoclean");
    else if (ui->radioClean->isChecked())
        apt = QStringLiteral("apt-get clean");

    total += getCmdOut(QStringLiteral("du -s /var/cache/apt/archives/ | cut -f1")).toULongLong();
    system(apt.toUtf8());
    total -= getCmdOut(QStringLiteral("du -s /var/cache/apt/archives/ | cut -f1")).toULongLong();

    QString time = ui->spinBoxLogs->value() == 0 ? QStringLiteral(" ")
                                                  : " -ctime +" + QString::number(ui->spinBoxLogs->value()) +
                                                    " -atime +" + QString::number(ui->spinBoxLogs->value()) + " ";
    if (ui->radioOldLogs->isChecked()) {
        total += getCmdOut(R"(find /var/log \( -name "*.gz" -o -name "*.old" -o -name "*.1" \) -type f)" +
                           time + "-exec du -sc '{}' + | tail -1 | cut -f1").toULongLong();
        logs = R"(find /var/log \( -name "*.gz" -o -name "*.old" -o -name "*.1" \))" +
                time + "-type f -delete 2>/dev/null";
        system(logs.toUtf8());
    } else if (ui->radioAllLogs->isChecked()){
        total += getCmdOut("find /var/log -type f" + time + "-exec du -sc '{}' + | tail -1 | cut -f1").toULongLong();
        logs = "find /var/log -type f" + time + R"(-exec sh -c "echo > '{}'" \;)";  // empty the logs
        system(logs.toUtf8());
    }

    if (ui->radioSelectedUser->isChecked() || ui->radioAllUsers->isChecked()) {
        QString user = ui->radioAllUsers->isChecked() ? QStringLiteral("*")
                                                      : ui->comboUserClean->currentText();
        QString time = ui->spinBoxTrash->value() == 0 ? QStringLiteral(" ")
                                                       : " -ctime +" + QString::number(ui->spinBoxTrash->value()) +
                                                         " -atime +" + QString::number(ui->spinBoxTrash->value()) + " ";
        total += getCmdOut("find /home/" + user + "/.local/share/Trash -type f" + time +
                           "-exec du -sc '{}' + | tail -1 | cut -f1").toULongLong();
        trash = "find /home/" + user + "/.local/share/Trash -type f" + time + "-delete";
        system(trash.toUtf8());
    }

    // cleanup schedule
    QFile::remove(QStringLiteral("/etc/cron.daily/mx-cleanup"));
    QFile::remove(QStringLiteral("/etc/cron.weekly/mx-cleanup"));
    QFile::remove(QStringLiteral("/etc/cron.monthly/mx-cleanup"));
    QFile::remove(QStringLiteral("/etc/cron.d/mx-cleanup"));
    // add schedule file
    if (!ui->radioNone->isChecked()) {
        QString period;
        cmd_str = cache + "\n" + thumbnails + "\n" + logs + "\n" + apt + "\n" + trash + "\n" + flatpak;
        qDebug() << "CMD STR" << cmd_str;
        if (ui->radioDaily->isChecked())
            period = QStringLiteral("daily");
        else if (ui->radioWeekly->isChecked())
            period = QStringLiteral("weekly");
        else if (ui->radioMonthly->isChecked())
            period = QStringLiteral("monthly");
        else if (ui->radioReboot->isChecked())
            period = QStringLiteral("@reboot");
        saveSchedule(cmd_str, period);
    }

    saveSettings();

    setCursor(QCursor(Qt::ArrowCursor));
    QMessageBox::information(this, tr("Done"), tr("Cleanup command done") + "\n" +
                             tr("%1 MiB were freed").arg(total / 1024));
}

void MainWindow::pushAbout_clicked()
{
    this->hide();
    displayAboutMsgBox(tr("About") + tr("MX Cleanup"),
                       R"(<p align="center"><b><h2>MX Cleanup</h2></b></p><p align="center">)" +
                       tr("Version: ") + qApp->applicationVersion() + "</p><p align=\"center\"><h3>" +
                       tr("Quick and safe removal of old files") +
                       R"(</h3></p><p align="center"><a href="http://mxlinux.org">http://mxlinux.org</a><br /></p><p align="center">)" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>",
                       QStringLiteral("/usr/share/doc/mx-cleanup/license.html"),
                       tr("%1 License").arg(this->windowTitle()));
    this->show();
}

void MainWindow::pushHelp_clicked()
{
    const QString url = QStringLiteral("/usr/share/doc/mx-cleanup/mx-cleanup.html");
    displayDoc(url, tr("%1 Help").arg(this->windowTitle()));
}

void MainWindow::pushUsageAnalyzer_clicked()
{
    const QString desktop = qgetenv("XDG_CURRENT_DESKTOP");
    const QString run_as_user = "runuser " + user.toUtf8() +
            " -c \"env XDG_RUNTIME_DIR=/run/user/$(id -u " + user.toUtf8() + ")";

    // try filelight, qdirstat for Qt based DEs, otherwise baobab
    if (desktop == QLatin1String("KDE") || desktop == QLatin1String("LXQt")) {
        if (system("command -v filelight") == 0)
            system(run_as_user.toUtf8() + " filelight\"&");
        else if (system("command -v qdirstat") == 0)
            system(run_as_user.toUtf8() + " qdirstat\"&");
        else   // failsafe just in case the des
            system(run_as_user.toUtf8() + " baobab\"&");
    } else {
        if (system("command -v baobab") == 0)
            system(run_as_user.toUtf8() + " baobab\"&");
        else if (system("command -v filelight") == 0)
            system(run_as_user.toUtf8() + " filelight\"&");
        else
            system(run_as_user.toUtf8() + " qdirstat\"&");
    }
}

QString MainWindow::getCmdOut(const QString &cmd)
{
    qDebug().noquote() << cmd;
    auto *proc = new QProcess(this);
    QEventLoop loop;
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->start(QStringLiteral("/bin/bash"), {QStringLiteral("-c"), cmd});
    loop.exec();
    QString out = proc->readAll().trimmed();
    delete proc;
    return out;
}

void MainWindow::pushKernel_clicked()
{
    auto current_kernel = getCmdOut(QStringLiteral("uname -r"));
    QString similar_kernels;
    QString other_kernels;
    if (system(R"(dpkg -l linux-image\* | grep ^ii)") == 0) {
        similar_kernels = getCmdOut(QStringLiteral(R"(dpkg -l linux-image-[0-9]\*.[0-9]\* | grep ^ii |
    grep $(uname -r | cut -f1 -d'-') | cut -f3 -d' ' | grep -v --extended-regexp linux-image-$(uname -r)'(-unsigned)?$')"));
        other_kernels = getCmdOut(QStringLiteral(R"(dpkg -l linux-image-[0-9]\*.[0-9]\* | grep ^ii |
    grep -v $(uname -r | cut -f1 -d'-') | cut -f3 -d' ')"));
    }
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(this->windowTitle());
    auto *layout = new QVBoxLayout;
    dialog->setLayout(layout);
    layout->addWidget(new QLabel(tr("Kernel currently in use: <b>%1</b>").arg(current_kernel)));

    auto *btnBox = new QDialogButtonBox(dialog);
    auto *pushRemove = new QPushButton(tr("Remove selected"));
    btnBox->addButton(pushRemove, QDialogButtonBox::AcceptRole);
    btnBox->addButton(tr("Close"), QDialogButtonBox::RejectRole);

    QStringList removal_list;
    addGroupCheckbox(layout, similar_kernels, tr("Similar kernels that can be removed:"), removal_list);
    addGroupCheckbox(layout, other_kernels, tr("Other kernels that can be removed:"), removal_list);
    if (layout->count() == 1) {
        layout->addWidget(new QLabel(tr("<b>Nothing to remove.</b> Cannot remove kernel in use.")));
        pushRemove->setHidden(true);
    } else {
        pushRemove->setHidden(false);
    }

    layout->addStretch(1);
    layout->addWidget(btnBox);

    connect(btnBox, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(btnBox, &QDialogButtonBox::accepted, this, [this, &removal_list] {removeKernelPackages(removal_list);});
    connect(btnBox, &QDialogButtonBox::accepted, dialog, &QDialog::close);

    dialog->exec();
}
