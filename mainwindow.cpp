/**********************************************************************
 *  mainwindow.cpp
 **********************************************************************
 * Copyright (C) 2018-2025 MX Authors
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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QTemporaryFile>
#include <QTextEdit>

#include <pwd.h>
#include <unistd.h>

#include "about.h"

extern const QString starting_home;

namespace
{
QString shellQuote(const QString &path)
{
    QString escaped = path;
    escaped.replace('\\', "\\\\");
    escaped.replace('"', "\\\"");
    return '"' + escaped + '"';
}
}

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
    if (!shadowSettingsPath.isEmpty()) {
        QFile::remove(shadowSettingsPath);
    }
    delete ui;
}

void MainWindow::removeManuals()
{
    QSettings defaultlocale("/etc/default/locale", QSettings::NativeFormat);
    QString lang = defaultlocale.value("LANG", "C").toString().section('.', 0, 0);

    // Fix for pt_BR, others use base language
    if (lang == "pt_BR") {
        lang = "pt-br";
    } else {
        lang = lang.section("_", 0, 0);
    }

    if (lang.isEmpty()) {
        return;
    }

    QString exclusionPattern = QString("(mx|mxfb)-(docs|faq)-(en|common%1)")
                                   .arg(lang == "en" || lang == "C" ? "" : QString("|%1").arg(lang));
    QString listCmd = QString("dpkg-query -W -f='${Package}\n' -- 'mx-docs-*' 'mxfb-docs-*' 'mx-faq-*' 'mxfb-faq-*' "
                              "2>/dev/null | grep -vE '%1'")
                          .arg(exclusionPattern);

    QStringList packageList = cmdOut(listCmd, true).split('\n', Qt::SkipEmptyParts);

    if (packageList.isEmpty()) {
        QMessageBox::information(this, tr("Remove Manuals"), tr("No manuals to remove."));
        return;
    }

    QString purgeCmd = QString("apt-get purge -y %1").arg(packageList.join(' '));

    ui->tabWidget->setDisabled(true);
    QProgressDialog prog(tr("Removing packages, please wait"), nullptr, 0, packageList.count());

    QProcess proc;
    QEventLoop loop;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    connect(&proc, &QProcess::readyReadStandardOutput, this, [&prog]() { prog.setValue(prog.value() + 1); });
    proc.setProcessChannelMode(QProcess::MergedChannels);
    QString elevate {QFile::exists("/usr/bin/pkexec") ? "/usr/bin/pkexec" : "/usr/bin/gksu"};
    QString helper {"/usr/lib/" + QApplication::applicationName() + "/helper"};
    proc.start(elevate, {helper, purgeCmd});
    prog.show();
    loop.exec();
    ui->tabWidget->setEnabled(true);
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

    // Hide disk usage analyzer group box if none of the tools are available
    const QStringList diskUsageTools = {"baobab", "qdirstat", "filelight"};
    bool hasAnyTool = false;
    for (const auto &tool : diskUsageTools) {
        if (!QStandardPaths::findExecutable(tool).isEmpty()) {
            hasAnyTool = true;
            break;
        }
    }
    if (!hasAnyTool) {
        ui->groupBoxUsage->hide();
    }

    suppressUserSwitch = true;

    currentUser = cmdOut("logname", false, true);

    ui->pushApply->setDisabled(true);
    ui->checkCache->setChecked(true);
    ui->checkThumbs->setChecked(true);
    ui->radioAutoClean->setChecked(true);
    ui->radioOldLogs->setChecked(true);
    ui->radioSelectedUser->setChecked(true);

    const QString users = cmdOut("lslogins --noheadings -u -o user | grep -vw root", false, true).trimmed();

    {
        QSignalBlocker blocker(ui->comboUserClean);
        ui->comboUserClean->addItems(users.split('\n'));

        int targetIndex = ui->comboUserClean->findText(currentUser);
        if (targetIndex == -1 && ui->comboUserClean->count() > 0) {
            targetIndex = 0;
        }

        if (targetIndex != -1) {
            ui->comboUserClean->setCurrentIndex(targetIndex);
        }
    }

    initializeSettingsForUser(ui->comboUserClean->currentText());
    loadSettings();
    ui->pushApply->setEnabled(!ui->comboUserClean->currentText().isEmpty());
    loadSchedule(true);

    suppressUserSwitch = false;
}

QString MainWindow::homeDirForUser(const QString &user) const
{
    if (user.isEmpty()) {
        return QString();
    }

    struct passwd *pwd = getpwnam(user.toUtf8().constData());
    if (!pwd) {
        return QString();
    }

    return QString::fromUtf8(pwd->pw_dir);
}

QString MainWindow::currentUserSuffix() const
{
    const QString user = ui->comboUserClean->currentText();
    return user.isEmpty() ? QString() : '.' + user;
}

QString MainWindow::settingsDirForUser(const QString &user) const
{
    const QString homeDir = homeDirForUser(user);
    if (homeDir.isEmpty()) {
        return QString();
    }
    QString orgName = QApplication::organizationName();
    if (orgName.isEmpty()) {
        orgName = QStringLiteral("MX-Linux");
    }
    return homeDir + "/.config/" + orgName;
}

QString MainWindow::settingsFileForUser(const QString &user) const
{
    const QString dir = settingsDirForUser(user);
    if (dir.isEmpty()) {
        return QString();
    }
    QString appName = QApplication::applicationName();
    if (appName.isEmpty()) {
        appName = QStringLiteral("mx-cleanup");
    }
    return dir + '/' + appName + ".conf";
}

void MainWindow::initializeSettingsForUser(const QString &user)
{
    if (!shadowSettingsPath.isEmpty()) {
        QFile::remove(shadowSettingsPath);
        shadowSettingsPath.clear();
    }

    currentSettingsPath.clear();
    settings.reset();

    if (user.isEmpty()) {
        settings = std::make_unique<QSettings>();
        return;
    }

    const QString filePath = settingsFileForUser(user);
    if (filePath.isEmpty()) {
        settings = std::make_unique<QSettings>();
        return;
    }

    const bool needsRoot = (getuid() != 0 && user != currentUser);
    if (needsRoot) {
        if (QFile::exists(filePath)) {
            QTemporaryFile tempFile(QDir::tempPath() + "/mx-cleanup-shadowXXXXXX.conf");
            tempFile.setAutoRemove(false);
            if (tempFile.open()) {
                shadowSettingsPath = tempFile.fileName();
                tempFile.close();
                QString owner = currentUser.isEmpty() ? QStringLiteral("root") : currentUser;
                QString command = QString("install -m 600 -o %1 -g %1 %2 %3")
                                      .arg(owner, shellQuote(filePath), shellQuote(shadowSettingsPath));
                qDebug().noquote() << "Loading settings with root privileges:" << command;
                cmdOutAsRoot(command, true);
                settings = std::make_unique<QSettings>(shadowSettingsPath, QSettings::IniFormat);
                settings->setFallbacksEnabled(false);
                currentSettingsPath = filePath;
                return;
            }
            qWarning().noquote() << "Failed to create temporary file for settings shadow";
        }
        settings = std::make_unique<QSettings>();
        currentSettingsPath = filePath;
        return;
    }

    settings = std::make_unique<QSettings>(filePath, QSettings::IniFormat);
    settings->setFallbacksEnabled(false);
    currentSettingsPath = filePath;
}

void MainWindow::ensureSettingsOwnership(const QString &user)
{
    if (user.isEmpty()) {
        return;
    }

    const QString settingsDir = settingsDirForUser(user);
    if (settingsDir.isEmpty() || !QFile::exists(settingsDir)) {
        return;
    }

    const QString command = "chown -R " + user + ':' + user + ' ' + shellQuote(settingsDir);
    if (getuid() == 0) {
        cmdOut(command, false, true);
    } else {
        cmdOutAsRoot(command, true);
    }
}

QString MainWindow::cronEntryBase(const QString &period) const
{
    if (period == "@reboot") {
        return "/etc/cron.d/mx-cleanup";
    }
    return "/etc/cron." + period + "/mx-cleanup";
}

QString MainWindow::cronEntryPath(const QString &period, bool forWrite) const
{
    const QString base = cronEntryBase(period);
    const QString suffix = currentUserSuffix();

    if (suffix.isEmpty()) {
        return base;
    }

    const QString candidate = base + suffix;
    if (forWrite) {
        return candidate;
    }

    return QFile::exists(candidate) ? candidate : base;
}

QString MainWindow::scriptFileBase() const
{
    return "/usr/bin/mx-cleanup-script";
}

QString MainWindow::scriptFilePath(bool forWrite) const
{
    const QString base = scriptFileBase();
    const QString suffix = currentUserSuffix();

    if (suffix.isEmpty()) {
        return base;
    }

    const QString candidate = base + suffix;
    if (forWrite) {
        return candidate;
    }

    return QFile::exists(candidate) ? candidate : base;
}

// Check if the cleanup script exists in the cron directories
void MainWindow::loadSchedule(bool settingsPreloaded)
{
    const QString dailyPath = cronEntryPath("daily", false);
    const QString weeklyPath = cronEntryPath("weekly", false);
    const QString monthlyPath = cronEntryPath("monthly", false);
    const QString rebootPath = cronEntryPath("@reboot", false);

    if (QFile::exists(dailyPath)) {
        ui->radioDaily->setChecked(true);
    } else if (QFile::exists(weeklyPath)) {
        ui->radioWeekly->setChecked(true);
    } else if (QFile::exists(monthlyPath)) {
        ui->radioMonthly->setChecked(true);
    } else if (QFile::exists(rebootPath)) {
        ui->radioReboot->setChecked(true);
    } else {
        ui->radioNone->setChecked(true);
    }
    loadOptions(settingsPreloaded);
}

void MainWindow::loadSettings()
{
    const QString source = currentSettingsPath.isEmpty() ? QStringLiteral("<defaults>") : currentSettingsPath;
    qDebug().noquote() << "Load settings from" << source;
    auto value = [this](const QString &key, const QVariant &fallback) -> QVariant {
        return settings ? settings->value(key, fallback) : fallback;
    };

    ui->checkThumbs->setChecked(value("Folders/Thumbnails", true).toBool());
    ui->checkCache->setChecked(value("Folders/Cache", true).toBool());
    ui->spinCache->setValue(value("Folders/CacheOlderThan", 2).toInt());
    const bool cacheSafer = value("Folders/CacheSafer", true).toBool();
    ui->radioSaferCache->setChecked(cacheSafer);
    ui->radioAllCache->setChecked(!cacheSafer);

    ui->groupBoxApt->setChecked(value("Apt/AptCleanup", true).toBool());
    selectRadioButton(ui->groupBoxApt, ui->buttonGroupApt, value("Apt/AptSelection", -1).toInt());
    ui->checkPurge->setChecked(value("Apt/AptPurge", false).toBool());

    ui->checkFlatpak->setChecked(value("Flatpak/UninstallUnusedRuntimes", false).toBool());

    ui->groupBoxLogs->setChecked(value("Logs/LogsCleanup", true).toBool());
    ui->spinBoxLogs->setValue(value("Logs/LogsOlderThan", 7).toInt());
    selectRadioButton(ui->groupBoxLogs, ui->buttonGroupLogs, value("Logs/LogsSelection", -1).toInt());

    ui->groupBoxTrash->setChecked(value("Trash/TrashCleanup", true).toBool());
    ui->spinBoxTrash->setValue(value("Trash/TrashOlderThan", 30).toInt());
    selectRadioButton(ui->groupBoxTrash, ui->buttonGroupTrash, value("Trash/TrashSelection", -1).toInt());
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
        QProcess proc;
        proc.start("dpkg", {"-s", item});
        proc.waitForFinished();
        if (proc.exitCode() == 0 && proc.readAllStandardOutput().contains("Status: install ok installed")) {
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
            QProcess checkProc;
            QString checkCmd
                = QString("dpkg -l 'linux-image-[0-9]*' | grep ^ii | cut -d ' ' -f3 | grep -v -E '%1' | grep -q %2")
                      .arg(list.join('|'), image_pattern);
            checkProc.start("/bin/bash", {"-c", checkCmd});
            checkProc.waitForFinished();
            if (checkProc.exitCode() != 0) {
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
    QString terminalCmd
        = QString("%1 apt purge %2 %3 %4; apt-get install -f; read -n1 -srp \"%5\"")
              .arg(rmOldVersions, headers_installed.join(' '), list.join(' '), common, tr("Press any key to close"));
    QProcess terminalProc;
    terminalProc.start("x-terminal-emulator", {"-e", "pkexec", helper, terminalCmd});
    terminalProc.waitForFinished();
    setCursor(QCursor(Qt::ArrowCursor));
}

// Load saved options to GUI
void MainWindow::loadOptions(bool settingsPreloaded)
{
    QString period;
    if (ui->radioDaily->isChecked()) {
        period = "daily";
    } else if (ui->radioWeekly->isChecked()) {
        period = "weekly";
    } else if (ui->radioMonthly->isChecked()) {
        period = "monthly";
    } else if (ui->radioReboot->isChecked()) {
        period = "@reboot";
    } else {
        if (!settingsPreloaded) {
            loadSettings();
        }
        return;
    }

    QString file_name = (period == "@reboot") ? scriptFilePath(false) : cronEntryPath(period, false);

    if (!QFile::exists(file_name)) {
        if (!settingsPreloaded) {
            loadSettings();
        }
        return;
    }

    // Folders
    QProcess thumbsProc;
    thumbsProc.start("grep", {"-qE", R"(find /home/[^/]+/\.cache/thumbnails)", file_name});
    thumbsProc.waitForFinished();
    ui->checkThumbs->setChecked(thumbsProc.exitCode() == 0);

    QProcess cacheProc;
    cacheProc.start("grep", {"-qE", R"(find /home/[^/]+/\.cache(\s|/\*))", file_name});
    cacheProc.waitForFinished();
    ui->checkCache->setChecked(cacheProc.exitCode() == 0);

    if (cacheProc.exitCode() == 0 || thumbsProc.exitCode() == 0) {
        QProcess atimeProc;
        atimeProc.start("grep", {"-oE", "--", R"(\.cache.*-atime \+[0-9]+)", file_name});
        atimeProc.waitForFinished();
        QString atimeOutput = atimeProc.readAllStandardOutput();
        QRegularExpression regex(R"(-atime \+([0-9]+))");
        QRegularExpressionMatch match = regex.match(atimeOutput);
        if (match.hasMatch()) {
            int atimeValue = match.captured(1).toInt();
            ui->radioSaferCache->setChecked(true);
            ui->radioAllCache->setChecked(false);
            ui->spinCache->setValue(atimeValue);
        } else {
            ui->radioSaferCache->setChecked(false);
            ui->radioAllCache->setChecked(true);
        }
    }

    // APT
    QProcess autocleanProc;
    autocleanProc.start("grep", {"-q", "apt-get autoclean", file_name});
    autocleanProc.waitForFinished();
    if (autocleanProc.exitCode() == 0) { // detect autoclean
        ui->radioAutoClean->setChecked(true);
    } else {
        QProcess cleanProc;
        cleanProc.start("grep", {"-q", "apt-get clean", file_name});
        cleanProc.waitForFinished();
        if (cleanProc.exitCode() == 0) { // detect clean
            ui->radioClean->setChecked(true);
        } else {
            ui->groupBoxApt->setChecked(false);
        }
    }

    // APT purge
    QProcess purgeProc;
    purgeProc.start("grep", {"-q", "apt-get purge", file_name});
    purgeProc.waitForFinished();
    ui->checkPurge->setChecked(purgeProc.exitCode() == 0);

    // Flatpak: remove unused runtimes
    QProcess flatpakProc;
    flatpakProc.start("grep", {"-q", "flatpak uninstall --unused", file_name});
    flatpakProc.waitForFinished();
    ui->checkFlatpak->setChecked(flatpakProc.exitCode() == 0);

    // Logs
    QProcess allLogsProc;
    allLogsProc.start("grep", {"-q", "\\-exec sh \\-c \"echo", file_name});
    allLogsProc.waitForFinished();
    if (allLogsProc.exitCode() == 0) { // all logs
        ui->radioAllLogs->setChecked(true);
    } else {
        QProcess oldLogsProc;
        oldLogsProc.start("grep", {"-q", "\\-type f \\-delete", file_name});
        oldLogsProc.waitForFinished();
        if (oldLogsProc.exitCode() == 0) { // old logs
            ui->radioOldLogs->setChecked(true);
        } else {
            ui->groupBoxLogs->setChecked(false);
        }
    }

    // Logs older than...
    QString ctime = cmdOut(
        "grep 'find /var/log' " + file_name + R"( | grep -Eo '\-ctime \+[0-9]{1,3}' | cut -f2 -d' ')", false, true);
    ui->spinBoxLogs->setValue(ctime.toInt());

    // Trash
    QProcess allUsersProc;
    allUsersProc.start("grep", {"-q", "/home/\\*/.local/share/Trash", file_name});
    allUsersProc.waitForFinished();
    if (allUsersProc.exitCode() == 0) { // all user trash
        ui->radioAllUsers->setChecked(true);
    } else {
        QProcess selectedUserProc;
        selectedUserProc.start("grep", {"-q", "/.local/share/Trash", file_name});
        selectedUserProc.waitForFinished();
        if (selectedUserProc.exitCode() == 0) { // selected user trash
            ui->radioSelectedUser->setChecked(true);
        } else {
            ui->groupBoxTrash->setChecked(false);
        }
    }

    // Trash older than...
    ctime = cmdOut("grep 'find /home/' " + file_name + R"( | grep -Eo '\-ctime \+[0-9]{1,3}' | cut -f2 -d' ')", false,
                   true);
    ui->spinBoxTrash->setValue(ctime.toInt());
}

// Save cleanup commands to a /etc/cron.daily|weekly|monthly/mx-cleanup script
void MainWindow::saveSchedule(const QString &cmd_str, const QString &period)
{
    const QString cronBase = cronEntryBase(period);
    const QString cronTarget = cronEntryPath(period, true);

    cmdOutAsRoot("rm " + cronBase, true);
    if (cronTarget != cronBase) {
        cmdOutAsRoot("rm " + cronTarget, true);
    }

    QString scriptTarget = cronTarget;

    if (period == "@reboot") {
        scriptTarget = scriptFilePath(true);
        const QString scriptBase = scriptFileBase();

        cmdOutAsRoot("rm " + scriptBase, true);
        if (scriptTarget != scriptBase) {
            cmdOutAsRoot("rm " + scriptTarget, true);
        }

        QTemporaryFile tempCron;
        tempCron.open();
        tempCron.write(QString("@reboot root %1\n").arg(scriptTarget).toUtf8());
        tempCron.close();
        cmdOutAsRoot("mv " + tempCron.fileName() + ' ' + cronTarget);
        cmdOutAsRoot("chown root: " + cronTarget);
        cmdOutAsRoot("chmod +r " + cronTarget);
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
    cmdOutAsRoot("mv " + tempFile.fileName() + ' ' + scriptTarget);
    cmdOutAsRoot("chmod +rx " + scriptTarget);
    cmdOutAsRoot("chown root: " + scriptTarget);
}

void MainWindow::saveSettings()
{
    if (!settings) {
        return;
    }

    const QString user = ui->comboUserClean->currentText();
    if (user.isEmpty()) {
        return;
    }

    const QString dirPath = settingsDirForUser(user);
    const QString targetPath = settingsFileForUser(user);

    if (dirPath.isEmpty() || targetPath.isEmpty()) {
        qWarning().noquote() << "Missing settings path for user" << user;
        return;
    }

    const bool needsRoot = (getuid() != 0 && user != currentUser);

    auto writeValues = [this](QSettings &store) {
        store.setValue("Folders/Thumbnails", ui->checkThumbs->isChecked());
        store.setValue("Folders/Cache", ui->checkCache->isChecked());
        store.setValue("Folders/CacheOlderThan", ui->spinCache->value());
        store.setValue("Folders/CacheSafer", ui->radioSaferCache->isChecked());

        store.setValue("Apt/AptCleanup", ui->groupBoxApt->isChecked());
        store.setValue("Apt/AptSelection", ui->buttonGroupApt->checkedId());
        store.setValue("Apt/AptPurge", ui->checkPurge->isChecked());

        store.setValue("Logs/LogsSelection", ui->buttonGroupLogs->checkedId());
        store.setValue("Logs/LogsOlderThan", ui->spinBoxLogs->value());
        store.setValue("Logs/LogsCleanup", ui->groupBoxLogs->isChecked());

        store.setValue("Trash/TrashCleanup", ui->groupBoxTrash->isChecked());
        store.setValue("Trash/TrashSelection", ui->buttonGroupTrash->checkedId());
        store.setValue("Trash/TrashOlderThan", ui->spinBoxTrash->value());

        store.setValue("Flatpak/FlatpakCleanup", ui->groupBoxFlatpak->isChecked());
        store.setValue("Flatpak/UninstallUnusedRuntimes", ui->checkFlatpak->isChecked());
    };

    if (needsRoot) {
        QTemporaryFile tempFile;
        if (!tempFile.open()) {
            qWarning().noquote() << "Failed to open temporary settings file for user" << user;
            return;
        }

        QSettings tempSettings(tempFile.fileName(), QSettings::IniFormat);
        tempSettings.setFallbacksEnabled(false);
        writeValues(tempSettings);
        tempSettings.sync();
        qDebug().noquote() << "Temporary settings status:" << tempSettings.status();

        tempFile.flush();
        tempFile.close();

        const QString command
            = QString("install -d -m 755 -o %1 -g %1 %2 && install -m 644 -o %1 -g %1 %3 %4")
                  .arg(user, shellQuote(dirPath), shellQuote(tempFile.fileName()), shellQuote(targetPath));
        qDebug().noquote() << "Saving settings with root privileges:" << command;
        cmdOutAsRoot(command, true);

        QFile::remove(tempFile.fileName());

        currentSettingsPath = targetPath;
        initializeSettingsForUser(user);
        return;
    }

    QDir dir;
    if (!dir.exists(dirPath)) {
        if (getuid() == 0) {
            qDebug().noquote() << "Creating settings directory as root:" << dirPath;
            cmdOut(QString("install -d -m 755 -o %1 -g %1 %2").arg(user, shellQuote(dirPath)), false, true);
        } else {
            qDebug().noquote() << "Creating settings directory:" << dirPath;
            dir.mkpath(dirPath);
        }
    }

    qDebug().noquote() << "Save settings to" << targetPath;
    writeValues(*settings);
    settings->sync();
    qDebug().noquote() << "Settings sync status:" << settings->status();
    if (getuid() == 0 && user != currentUser) {
        ensureSettingsOwnership(user);
    }

    currentSettingsPath = targetPath;
}

void MainWindow::selectRadioButton(QGroupBox *groupbox, const QButtonGroup *group, int id)
{
    if (groupbox) {
        groupbox->setChecked(id != -1);
    }
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
    connect(ui->pushRemoveManuals, &QPushButton::clicked, this, &MainWindow::removeManuals);
    connect(ui->pushRTLremove, &QPushButton::clicked, this, &MainWindow::pushRTLremove_clicked);
    connect(ui->pushUsageAnalyzer, &QPushButton::clicked, this, &MainWindow::pushUsageAnalyzer_clicked);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this,
            [this](int index) { ui->pushApply->setDisabled(index == 1); });
    connect(ui->comboUserClean, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (suppressUserSwitch) {
            return;
        }
        ui->pushApply->setEnabled(!text.isEmpty());
        initializeSettingsForUser(text);
        loadSettings();
        loadSchedule(true);
    });

    for (auto *spinBox : {ui->spinCache, ui->spinBoxLogs, ui->spinBoxTrash}) {
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [spinBox]() { spinBox->setSuffix(spinBox->value() > 1 ? tr(" days") : tr(" day")); });
    }
}

void MainWindow::pushApply_clicked()
{
    setCursor(QCursor(Qt::BusyCursor));
    setEnabled(false);

    // Try to elevate privileges if needed
    if (getuid() != 0) {
        QString elevate {QFile::exists("/usr/bin/pkexec") ? "/usr/bin/pkexec" : "/usr/bin/gksu"};
        QString helper {"/usr/lib/" + QApplication::applicationName() + "/helper"};
        QProcess proc;
        proc.start(elevate, {helper, "true"});
        proc.waitForFinished();
        if (proc.exitCode() != 0) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to elevate privileges"));
            setCursor(QCursor(Qt::ArrowCursor));
            return;
        }
    }

    quint64 total {};
    QString cache;
    const QString selectedUser = ui->comboUserClean->currentText();
    const bool elevate = (selectedUser != currentUser);
    if (ui->checkCache->isChecked()) {
        QString period = ui->radioSaferCache->isChecked()
                             ? QString(" -atime +%1 -mtime +%1").arg(ui->spinCache->value())
                             : QString();
        QString findCmd = QString("find /home/%1/.cache -mindepth 1 ! -path '/home/%1/.cache/thumbnails*'%2 -type f "
                                  "-exec du -c '{}' + | awk 'END{print $1}'")
                              .arg(selectedUser, period);
        total += elevate ? cmdOutAsRoot(findCmd).toULongLong() : cmdOut(findCmd).toULongLong();

        cache = QString("find /home/%1/.cache -mindepth 1 ! -path '/home/%1/.cache/thumbnails*'%2 -delete 2>/dev/null")
                    .arg(selectedUser, period);
        if (!ui->radioReboot->isChecked()) {
            if (elevate) {
                cmdOutAsRoot(cache);
            } else {
                cmdOut(cache);
            }
        }
    }

    QString thumbnails;
    if (ui->checkThumbs->isChecked()) {
        QString period = ui->radioSaferCache->isChecked()
                             ? QString(" -atime +%1 -mtime +%1").arg(ui->spinCache->value())
                             : QString();
        QString findThumbsCmd = QString("find /home/%1/.cache/thumbnails -type f%2 -exec du -c '{}' + | awk '{field = $1} END {print field}'")
                                    .arg(selectedUser, period);
        QString thumbsDeleteCmd = QString("find /home/%1/.cache/thumbnails -type f%2 -delete 2>/dev/null")
                                      .arg(selectedUser, period);

        total += elevate ? cmdOutAsRoot(findThumbsCmd).toULongLong() : cmdOut(findThumbsCmd).toULongLong();
        thumbnails = thumbsDeleteCmd;
        if (!ui->radioReboot->isChecked()) {
            if (elevate) {
                cmdOutAsRoot(thumbnails);
            } else {
                cmdOut(thumbnails);
            }
        }
    }

    QString flatpak;
    if (ui->checkFlatpak->isChecked()) {
        flatpak = "pgrep -a flatpak | grep -v flatpak-s || flatpak uninstall --unused --delete-data --noninteractive";
        const QString userSizeCmd = QString("du -s /home/%1/.local/share/flatpak/ | cut -f1").arg(selectedUser);
        QString system_size = "du -s /var/lib/flatpak/ | cut -f1";
        quint64 userBefore = elevate ? cmdOutAsRoot(userSizeCmd).toULongLong() : cmdOut(userSizeCmd).toULongLong();
        total += userBefore;
        bool ok {false};
        quint64 system_size_num = cmdOutAsRoot(system_size).toULongLong(&ok);
        if (ok) {
            total += system_size_num;
        }
        if (!ui->radioReboot->isChecked()) {
            cmdOut(flatpak);
        }
        quint64 userAfter = elevate ? cmdOutAsRoot(userSizeCmd).toULongLong() : cmdOut(userSizeCmd).toULongLong();
        total -= userAfter;
        ok = false;
        system_size_num = cmdOutAsRoot(system_size).toULongLong(&ok);
        if (ok) {
            total -= system_size_num;
        }
    }

    QString apt;
    if (ui->groupBoxApt->isChecked()) {
        if (ui->radioAutoClean->isChecked()) {
            apt = "apt-get autoclean";
        } else if (ui->radioClean->isChecked()) {
            apt = "apt-get clean";
        }

        if (!apt.isEmpty()) {
            const QString size_cmd = "du -s /var/cache/apt/archives/ | cut -f1";
            quint64 before_size = cmdOutAsRoot(size_cmd).toULongLong();

            if (!ui->radioReboot->isChecked()) {
                cmdOutAsRoot(apt);
            }

            quint64 after_size = cmdOutAsRoot(size_cmd).toULongLong();
            total += (before_size - after_size);
        }
    }

    if (ui->checkPurge->isChecked()) {
        if (!apt.isEmpty()) {
            apt += '\n';
        }
        apt += "dpkg -l | awk '/^rc/ { print $2 }' | xargs -r apt-get purge -y";

        const QString size_cmd = "du -s /var/lib/dpkg/info/ | cut -f1";
        quint64 before_size = cmdOutAsRoot(size_cmd).toULongLong();

        if (!ui->radioReboot->isChecked()) {
            cmdOutAsRoot(apt);
        }

        quint64 after_size = cmdOutAsRoot(size_cmd).toULongLong();
        total += (before_size - after_size);
    }

    QString logs;
    if (ui->groupBoxLogs->isChecked()) {
        QString time = ui->spinBoxLogs->value() > 0 ? QString(" -ctime +%1 -atime +%1").arg(ui->spinBoxLogs->value())
                                                    : QString();
        if (ui->radioOldLogs->isChecked()) {
            total
                += cmdOutAsRoot(
                       R"(find /var/log \( -name "*.gz" -o -name "*.old" -o -name "*.[0-9]" -o -name "*.[0-9].log" \) -type f)"
                       + time + " -exec du -sc '{}' + | awk '{field = $1} END {print field}'")
                       .toULongLong();
            logs = R"(find /var/log \( -name "*.gz" -o -name "*.old" -o -name "*.[0-9]" -o -name "*.[0-9].log" \))"
                   + time + " -type f -delete 2>/dev/null";
            cmdOutAsRoot(logs);
        } else if (ui->radioAllLogs->isChecked()) {
            total += cmdOutAsRoot(
                         QString("find /var/log -type f%1 -exec du -sc '{}' + | awk '{field = $1} END {print field}'")
                             .arg(time))
                         .toULongLong();
            logs = "find /var/log -type f" + time + R"( -exec sh -c "echo > '{}'" \;)"; // empty the logs
            cmdOutAsRoot(logs);
        }
    }

    QString trash;
    if (ui->groupBoxTrash->isChecked()) {
        QString user = ui->radioAllUsers->isChecked() ? "*" : ui->comboUserClean->currentText();
        QString timeTrash = ui->spinBoxTrash->value() > 0
                                ? QString(" -ctime +%1 -atime +%1").arg(ui->spinBoxTrash->value())
                                : QString();
        QString findSizeCmd = QString("find /home/%1/.local/share/Trash -mindepth 1%2 -exec du -sc '{}' + | awk '{field = $1} END {print field}'")
                                .arg(user, timeTrash);
        QString findDeleteCmd = QString("find /home/%1/.local/share/Trash -mindepth 1%2 -delete")
                                .arg(user, timeTrash);

        // If cleaning another user, run as root
        if (user != currentUser) {
            total += cmdOutAsRoot(findSizeCmd).toULongLong();
            trash = findDeleteCmd;
            if (!ui->radioReboot->isChecked()) {
                cmdOutAsRoot(trash);
            }
        } else {
            total += cmdOut(findSizeCmd).toULongLong();
            trash = findDeleteCmd;
            if (!ui->radioReboot->isChecked()) {
                cmdOut(trash);
            }
        }
    }

    // Cleanup schedule
    const QString suffix = currentUserSuffix();
    cmdOutAsRoot("rm " + cronEntryBase("daily"), true);
    cmdOutAsRoot("rm " + cronEntryBase("weekly"), true);
    cmdOutAsRoot("rm " + cronEntryBase("monthly"), true);
    cmdOutAsRoot("rm " + cronEntryBase("@reboot"), true);
    cmdOutAsRoot("rm " + scriptFileBase(), true);

    if (!suffix.isEmpty()) {
        cmdOutAsRoot("rm " + cronEntryBase("daily") + suffix, true);
        cmdOutAsRoot("rm " + cronEntryBase("weekly") + suffix, true);
        cmdOutAsRoot("rm " + cronEntryBase("monthly") + suffix, true);
        cmdOutAsRoot("rm " + cronEntryBase("@reboot") + suffix, true);
        cmdOutAsRoot("rm " + scriptFileBase() + suffix, true);
    }

    // Add schedule file
    if (!ui->radioNone->isChecked()) {
        QStringList parts;
        if (!cache.isEmpty()) {
            parts << cache;
        }
        if (!thumbnails.isEmpty()) {
            parts << thumbnails;
        }
        if (!logs.isEmpty()) {
            parts << logs;
        }
        if (!apt.isEmpty()) {
            parts << apt;
        }
        if (!trash.isEmpty()) {
            parts << trash;
        }
        if (!flatpak.isEmpty()) {
            parts << flatpak;
        }
        QString cmd_str = parts.join('\n');
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
    setEnabled(true);
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
    if ! lsmod | grep -q -w ^wl
        then
            echo -n broadcom-sta-dkms
        else
            lspci -v  | grep -q "Kernel driver in use: wl"$ || \
            lsusb -tv | grep -q "Driver=wl, "               || \
            echo -n broadcom-sta-dkms
    fi)");

    QString helper {"/usr/lib/" + QApplication::applicationName() + "/helper-terminal"};
    QString terminalCmd
        = QString("apt purge %1; apt-get install -f; read -n1 -srp \"%2\"").arg(dumpList, tr("Press any key to close"));
    QProcess terminalProc;
    terminalProc.start("x-terminal-emulator", {"-e", "pkexec", helper, terminalCmd});
    terminalProc.waitForFinished();
    setCursor(QCursor(Qt::ArrowCursor));
}
