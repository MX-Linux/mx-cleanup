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
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextEdit>
#include <QTimer>

#include <pwd.h>
#include <grp.h>
#include <unistd.h>

#include "about.h"

extern const QString starting_home;

namespace
{
bool isArchLinuxHost()
{
    if (QFile::exists("/etc/arch-release")) {
        return true;
    }

    QFile osRelease("/etc/os-release");
    if (!osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QString data = QString::fromUtf8(osRelease.readAll());
    QString id;
    QString idLike;
    const auto lines = data.split('\n');
    for (const auto &line : lines) {
        if (line.startsWith("ID=")) {
            id = line.mid(3).trimmed();
        } else if (line.startsWith("ID_LIKE=")) {
            idLike = line.mid(8).trimmed();
        }
    }

    auto normalize = [](QString value) {
        value.remove('"');
        return value.toLower();
    };

    id = normalize(id);
    idLike = normalize(idLike);

    return id == "arch" || idLike.split(' ').contains("arch");
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (manualRemovalInProgress) {
        event->ignore();
        return;
    }

    QDialog::closeEvent(event);
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

    QStringList packageList = cmdOut(listCmd).split('\n', Qt::SkipEmptyParts);

    if (packageList.isEmpty()) {
        QMessageBox::information(this, tr("Remove Manuals"), tr("No manuals to remove."));
        return;
    }

    if (getuid() != 0 && !helperExec("true", {}, QuietMode::Yes)) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to elevate privileges"));
        return;
    }

    manualRemovalInProgress = true;
    ui->pushCancel->setDisabled(true);
    ui->tabWidget->setDisabled(true);
    QProgressDialog prog(tr("Removing packages, please wait"), QString(), 0, packageList.size(), this);
    prog.setMinimumDuration(0);
    prog.setValue(0);
    prog.show();
    QApplication::processEvents();

    for (int index = 0; index < packageList.size(); ++index) {
        prog.setLabelText(tr("Removing packages, please wait"));
        helperExec("apt-get", {"purge", "-y", packageList.at(index)}, QuietMode::Yes);
        prog.setValue(index + 1);
        QApplication::processEvents();
    }

    ui->tabWidget->setEnabled(true);
    ui->pushCancel->setEnabled(true);
    manualRemovalInProgress = false;
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

    isArchLinux = isArchLinuxHost();
    if (isArchLinux) {
        ui->groupBoxKernel->hide();
        ui->groupBoxApt->setTitle(tr("Clean pacman cache"));
    }

    suppressUserSwitch = true;

    currentUser = cmdOut("logname", QuietMode::Yes);

    ui->pushApply->setDisabled(true);
    ui->checkCache->setChecked(true);
    ui->checkThumbs->setChecked(true);
    ui->radioAutoClean->setChecked(true);
    ui->radioOldLogs->setChecked(true);
    ui->radioSelectedUser->setChecked(true);

    QStringList users = cmdOut("lslogins --noheadings -u -o user", QuietMode::Yes)
                            .split('\n', Qt::SkipEmptyParts);
    users.removeAll(QStringLiteral("root"));

    {
        QSignalBlocker blocker(ui->comboUserClean);
        ui->comboUserClean->addItems(users);

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

QString MainWindow::primaryGroupForUser(const QString &user) const
{
    if (user.isEmpty()) {
        return QString();
    }

    struct passwd *pwd = getpwnam(user.toUtf8().constData());
    if (!pwd) {
        return QString();
    }

    struct group *grp = getgrgid(pwd->pw_gid);
    if (!grp) {
        return QString();
    }

    return QString::fromUtf8(grp->gr_name);
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
        QTemporaryFile tempFile(QDir::tempPath() + "/mx-cleanup-shadowXXXXXX.conf");
        tempFile.setAutoRemove(false);
        if (tempFile.open()) {
            shadowSettingsPath = tempFile.fileName();
            tempFile.close();
            const QString owner = currentUser.isEmpty() ? QStringLiteral("root") : currentUser;
            const QString ownerGroup = primaryGroupForUser(owner);
            const QString effectiveGroup = ownerGroup.isEmpty() ? owner : ownerGroup;
            if (QFile::exists(filePath)) {
                helperExec("install",
                           {"-m", "600", "-o", owner, "-g", effectiveGroup, filePath, shadowSettingsPath},
                           QuietMode::Yes);
            }
            if (QFile::exists(shadowSettingsPath)) {
                settings = std::make_unique<QSettings>(shadowSettingsPath, QSettings::IniFormat);
                settings->setFallbacksEnabled(false);
                currentSettingsPath = filePath;
                return;
            }
            QFile::remove(shadowSettingsPath);
        } else {
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

void MainWindow::ensureSettingsOwnership(const QString &user, const QString &targetPath)
{
    if (user.isEmpty()) {
        return;
    }

    const QString settingsDir = settingsDirForUser(user);
    if (settingsDir.isEmpty() || !QFile::exists(settingsDir)) {
        return;
    }

    const QString primaryGroup = primaryGroupForUser(user);
    const QString ownerGroup = primaryGroup.isEmpty() ? user : primaryGroup;
    helperExec("chown", {user + ":" + ownerGroup, settingsDir}, QuietMode::Yes);
    if (!targetPath.isEmpty() && QFile::exists(targetPath)) {
        helperExec("chown", {user + ":" + ownerGroup, targetPath}, QuietMode::Yes);
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

    if (QFile::exists(candidate)) {
        return candidate;
    }

    const bool selectedIsCurrent = (ui->comboUserClean->currentText() == currentUser);
    return selectedIsCurrent ? base : candidate;
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

    if (QFile::exists(candidate)) {
        return candidate;
    }

    const bool selectedIsCurrent = (ui->comboUserClean->currentText() == currentUser);
    return selectedIsCurrent ? base : candidate;
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
    auto value = [this](const QString &key, const QVariant &fallback) -> QVariant {
        return settings ? settings->value(key, fallback) : fallback;
    };

    ui->checkThumbs->setChecked(value("Folders/Thumbnails", true).toBool());
    ui->checkCache->setChecked(value("Folders/Cache", true).toBool());
    ui->spinCache->setValue(value("Folders/CacheOlderThan", 2).toInt());
    const bool cacheSafer = value("Folders/CacheSafer", true).toBool();
    ui->radioSaferCache->setChecked(cacheSafer);
    ui->radioAllCache->setChecked(!cacheSafer);

    const bool aptCleanup = value("Apt/AptCleanup", true).toBool();
    ui->groupBoxApt->setChecked(aptCleanup);
    const int aptSelection = aptCleanup ? value("Apt/AptSelection", -1).toInt() : -1;
    selectRadioButton(ui->groupBoxApt, ui->buttonGroupApt, aptSelection);
    ui->checkPurge->setChecked(value("Apt/AptPurge", false).toBool());

    ui->checkFlatpak->setChecked(value("Flatpak/UninstallUnusedRuntimes", false).toBool());

    const bool logsCleanup = value("Logs/LogsCleanup", true).toBool();
    ui->groupBoxLogs->setChecked(logsCleanup);
    ui->spinBoxLogs->setValue(value("Logs/LogsOlderThan", 7).toInt());
    const int logsSelection = logsCleanup ? value("Logs/LogsSelection", -1).toInt() : -1;
    selectRadioButton(ui->groupBoxLogs, ui->buttonGroupLogs, logsSelection);

    const bool trashCleanup = value("Trash/TrashCleanup", true).toBool();
    ui->groupBoxTrash->setChecked(trashCleanup);
    ui->spinBoxTrash->setValue(value("Trash/TrashOlderThan", 30).toInt());
    const int trashSelection = trashCleanup ? value("Trash/TrashSelection", -1).toInt() : -1;
    selectRadioButton(ui->groupBoxTrash, ui->buttonGroupTrash, trashSelection);
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
        {
            QProcess proc;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("LC_ALL", "C.UTF-8");
            proc.setProcessEnvironment(env);
            proc.start("apt-cache", {"depends", item});
            proc.waitForFinished();
            QRegularExpression reDepends("Depends:\\s+(linux-headers-\\d\\S+)");
            auto matches = reDepends.globalMatch(QString::fromUtf8(proc.readAllStandardOutput()));
            QStringList found;
            while (matches.hasNext()) {
                found << matches.next().captured(1);
            }
            found.removeDuplicates();
            found.sort();
            headers_common = found.join('\n');
        }
        if (!headers_common.toUtf8().trimmed().isEmpty()) {
            image_pattern = headers_common;
            image_pattern.remove("-common");
            image_pattern.replace("headers", "image");
            QProcess checkProc;
            QStringList escapedPkgs;
            escapedPkgs.reserve(list.size());
            for (const auto &pkg : list)
                escapedPkgs << QRegularExpression::escape(pkg);
            QString checkCmd
                = QString("dpkg -l 'linux-image-[0-9]*' | grep ^ii | cut -d ' ' -f3 | grep -v -E '%1' | grep -q %2")
                      .arg(escapedPkgs.join('|'), image_pattern);
            checkProc.start("/bin/bash", {"-c", checkCmd});
            checkProc.waitForFinished();
            if (checkProc.exitCode() != 0) {
                headers_depends << headers_common;
            }
        }
    }

    static const QRegularExpression pkgNameRe(R"(^[a-zA-Z0-9][a-zA-Z0-9.+-]*$)");
    QString common;
    if (!headers_depends.isEmpty()) {
        QStringList escapedDepends;
        escapedDepends.reserve(headers_depends.size());
        for (const auto &dep : headers_depends)
            escapedDepends << QRegularExpression::escape(dep);
        QString filter = "| grep -oE '" + escapedDepends.join('|') + "'";
        common = cmdOut("apt-get remove -s " + headers_installed.join(' ') + " | grep '^  ' " + filter
                        + R"( | tr '\n' ' ')");
    }

    QString helper {"/usr/lib/" + QApplication::applicationName() + "/helper-terminal-keep-open"};
    QStringList packages;
    for (const auto &pkg : headers_installed) {
        if (pkgNameRe.match(pkg).hasMatch())
            packages << pkg;
    }
    for (const auto &pkg : list) {
        if (pkgNameRe.match(pkg).hasMatch())
            packages << pkg;
    }
    if (!common.isEmpty()) {
        for (const auto &pkg : common.split(' ', Qt::SkipEmptyParts)) {
            if (pkgNameRe.match(pkg).hasMatch())
                packages << pkg;
        }
    }

    QString terminalCmd
        = QString("%1 apt-get purge %2; apt-get install -f")
              .arg(rmOldVersions, packages.join(' '));
    QProcess terminalProc;
    terminalProc.start("x-terminal-emulator", {"-e", "pkexec", helper, terminalCmd});
    terminalProc.waitForFinished(1800000);  // 30-minute timeout for terminal to close
    if (terminalProc.state() == QProcess::Running) {
        terminalProc.kill();
        terminalProc.waitForFinished();
    }
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
        loadSettings();
        return;
    }

    QString file_name = (period == "@reboot") ? scriptFilePath(false) : cronEntryPath(period, false);

    if (!settingsPreloaded && !QFile::exists(file_name)) {
        loadSettings();
        return;
    }

    if (!QFile::exists(file_name)) {
        return;
    }

    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QString content = QString::fromUtf8(file.readAll());

    // Folders
    bool hasThumbs = QRegularExpression(R"(find /home/[^/]+/\.cache/thumbnails)").match(content).hasMatch();
    ui->checkThumbs->setChecked(hasThumbs);

    bool hasCache = QRegularExpression(R"(find /home/[^/]+/\.cache(\s|/\*))").match(content).hasMatch();
    ui->checkCache->setChecked(hasCache);

    if (hasCache || hasThumbs) {
        QRegularExpression atimeRe(R"(\.cache.*-atime \+([0-9]+))");
        QRegularExpressionMatch match = atimeRe.match(content);
        if (match.hasMatch()) {
            ui->radioSaferCache->setChecked(true);
            ui->radioAllCache->setChecked(false);
            ui->spinCache->setValue(match.captured(1).toInt());
        } else {
            ui->radioSaferCache->setChecked(false);
            ui->radioAllCache->setChecked(true);
        }
    }

    // APT
    if (content.contains("apt-get autoclean")) {
        ui->radioAutoClean->setChecked(true);
    } else if (content.contains("apt-get clean")) {
        ui->radioClean->setChecked(true);
    } else {
        ui->groupBoxApt->setChecked(false);
    }

    // APT purge
    ui->checkPurge->setChecked(content.contains("apt-get purge"));

    // Flatpak: remove unused runtimes
    ui->checkFlatpak->setChecked(content.contains("flatpak uninstall --unused"));

    // Logs
    QRegularExpression allLogsRe(R"(\-exec sh \-c "echo)");
    if (allLogsRe.match(content).hasMatch()) {
        ui->radioAllLogs->setChecked(true);
    } else if (QRegularExpression(R"(\-type f \-delete)").match(content).hasMatch()) {
        ui->radioOldLogs->setChecked(true);
    } else {
        ui->groupBoxLogs->setChecked(false);
    }

    // Logs older than...
    QRegularExpression logCtimeRe(R"(find /var/log.*-ctime \+([0-9]{1,3}))");
    QRegularExpressionMatch logMatch = logCtimeRe.match(content);
    ui->spinBoxLogs->setValue(logMatch.hasMatch() ? logMatch.captured(1).toInt() : 0);

    // Trash
    if (content.contains("/home/*/.local/share/Trash")) {
        ui->radioAllUsers->setChecked(true);
    } else if (content.contains("/.local/share/Trash")) {
        ui->radioSelectedUser->setChecked(true);
    } else {
        ui->groupBoxTrash->setChecked(false);
    }

    // Trash older than...
    QRegularExpression trashCtimeRe(R"(find /home/.*-ctime \+([0-9]{1,3}))");
    QRegularExpressionMatch trashMatch = trashCtimeRe.match(content);
    ui->spinBoxTrash->setValue(trashMatch.hasMatch() ? trashMatch.captured(1).toInt() : 0);
}

// Save cleanup commands to a /etc/cron.daily|weekly|monthly/mx-cleanup script
void MainWindow::saveSchedule(const QString &cmd_str, const QString &period)
{
    const QString cronBase = cronEntryBase(period);
    const QString cronTarget = cronEntryPath(period, true);

    helperExec("rm", {"-f", cronBase}, QuietMode::Yes);
    if (cronTarget != cronBase) {
        helperExec("rm", {"-f", cronTarget}, QuietMode::Yes);
    }

    QString scriptTarget = cronTarget;

    if (period == "@reboot") {
        scriptTarget = scriptFilePath(true);
        const QString scriptBase = scriptFileBase();

        helperExec("rm", {"-f", scriptBase}, QuietMode::Yes);
        if (scriptTarget != scriptBase) {
            helperExec("rm", {"-f", scriptTarget}, QuietMode::Yes);
        }

        QTemporaryFile tempCron;
        if (!tempCron.open()) {
            QMessageBox::critical(this, tr("Error"),
                                  tr("Failed to create temporary cron file: %1").arg(tempCron.errorString()));
            return;
        }
        tempCron.write(QString("@reboot root %1\n").arg(scriptTarget).toUtf8());
        tempCron.close();
        helperExec("mv", {tempCron.fileName(), cronTarget}, QuietMode::Yes);
        helperExec("chown", {"root:root", cronTarget}, QuietMode::Yes);
        helperExec("chmod", {"0644", cronTarget}, QuietMode::Yes);
    }

    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed to create temporary script file: %1").arg(tempFile.errorString()));
        return;
    }
    QTextStream out(&tempFile);
    out << "#!/bin/sh\n";
    out << "#\n";
    out << "# This file was created by MX Cleanup\n";
    out << "#\n\n";
    out << cmd_str;
    tempFile.close();
    helperExec("mv", {tempFile.fileName(), scriptTarget}, QuietMode::Yes);
    helperExec("chmod", {"0755", scriptTarget}, QuietMode::Yes);
    helperExec("chown", {"root:root", scriptTarget}, QuietMode::Yes);
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
    const QString targetGroupName = primaryGroupForUser(user);

    auto writeValues = [this](QSettings &store) {
        store.setValue("Folders/Thumbnails", ui->checkThumbs->isChecked());
        store.setValue("Folders/Cache", ui->checkCache->isChecked());
        store.setValue("Folders/CacheOlderThan", ui->spinCache->value());
        store.setValue("Folders/CacheSafer", ui->radioSaferCache->isChecked());

        const bool aptCleanup = ui->groupBoxApt->isChecked();
        store.setValue("Apt/AptCleanup", aptCleanup);
        store.setValue("Apt/AptSelection", aptCleanup ? ui->buttonGroupApt->checkedId() : -1);
        store.setValue("Apt/AptPurge", ui->checkPurge->isChecked());

        const bool logsCleanup = ui->groupBoxLogs->isChecked();
        store.setValue("Logs/LogsSelection", logsCleanup ? ui->buttonGroupLogs->checkedId() : -1);
        store.setValue("Logs/LogsOlderThan", ui->spinBoxLogs->value());
        store.setValue("Logs/LogsCleanup", logsCleanup);

        const bool trashCleanup = ui->groupBoxTrash->isChecked();
        store.setValue("Trash/TrashCleanup", trashCleanup);
        store.setValue("Trash/TrashSelection", trashCleanup ? ui->buttonGroupTrash->checkedId() : -1);
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
        tempFile.flush();
        tempFile.close();

        const QString ownerGroup = targetGroupName.isEmpty() ? user : targetGroupName;
        helperExec("install", {"-d", "-m", "755", "-o", user, "-g", ownerGroup, dirPath}, QuietMode::Yes);
        helperExec("install", {"-m", "644", "-o", user, "-g", ownerGroup, tempFile.fileName(), targetPath},
                   QuietMode::Yes);

        QFile::remove(tempFile.fileName());

        currentSettingsPath = targetPath;
        initializeSettingsForUser(user);
        return;
    }

    QDir dir;
    if (!dir.exists(dirPath)) {
        if (getuid() == 0) {
            qDebug().noquote() << "Creating settings directory as root:" << dirPath;
            const QString ownerGroup = targetGroupName.isEmpty() ? user : targetGroupName;
            helperExec("install", {"-d", "-m", "755", "-o", user, "-g", ownerGroup, dirPath}, QuietMode::Yes);
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
        ensureSettingsOwnership(user, targetPath);
    }

    currentSettingsPath = targetPath;
}

void MainWindow::selectRadioButton(QGroupBox *groupbox, const QButtonGroup *group, int id)
{
    if (id != -1) {
        if (groupbox) {
            groupbox->setChecked(true);
        }
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
        if (!helperExec("true", {}, QuietMode::Yes)) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to elevate privileges"));
            setCursor(QCursor(Qt::ArrowCursor));
            setEnabled(true);
            return;
        }
    }

    quint64 total {};
    QString cache;
    const QString selectedUser = ui->comboUserClean->currentText();
    const bool elevate = (selectedUser != currentUser);

    auto addToTotal = [&](const QString &label, quint64 amount) {
        if (amount == 0) {
            return;
        }
        total += amount;
        qDebug().noquote() << "Freed" << label << amount << "KiB";
    };
    if (ui->checkCache->isChecked()) {
        QString period = ui->radioSaferCache->isChecked()
                             ? QString(" -atime +%1 -mtime +%1").arg(ui->spinCache->value())
                             : QString();
        QString findCmd = QString("find /home/%1/.cache -mindepth 1 ! -path '/home/%1/.cache/thumbnails*'%2 -type f "
                                  "-exec du -c '{}' + | awk 'END{print $1}'")
                              .arg(selectedUser, period);
        quint64 cacheKiB {};
        if (elevate) {
            const QString cachePath = QString("/home/%1/.cache").arg(selectedUser);
            const QString thumbsPath = QString("%1/thumbnails*").arg(cachePath);
            if (QFileInfo::exists(cachePath)) {
                QStringList args {cachePath, "-mindepth", "1", "!", "-path", thumbsPath};
                if (ui->radioSaferCache->isChecked()) {
                    const QString days = QString("+%1").arg(ui->spinCache->value());
                    args << "-atime" << days << "-mtime" << days;
                }
                args << "-type" << "f" << "-printf" << "%k\n";
                cacheKiB = sumKiB(helperOut("find", args, QuietMode::Yes));
            }
        } else {
            cacheKiB = cmdOut(findCmd).toULongLong();
        }
        addToTotal("cache", cacheKiB);

        cache = QString("find /home/%1/.cache -mindepth 1 ! -path '/home/%1/.cache/thumbnails*'%2 -delete 2>/dev/null")
                    .arg(selectedUser, period);
        if (!ui->radioReboot->isChecked()) {
            if (elevate) {
                const QString cachePath = QString("/home/%1/.cache").arg(selectedUser);
                const QString thumbsPath = QString("%1/thumbnails*").arg(cachePath);
                if (QFileInfo::exists(cachePath)) {
                    QStringList args {cachePath, "-mindepth", "1", "!", "-path", thumbsPath};
                    if (ui->radioSaferCache->isChecked()) {
                        const QString days = QString("+%1").arg(ui->spinCache->value());
                        args << "-atime" << days << "-mtime" << days;
                    }
                    args << "-delete";
                    helperExec("find", args, QuietMode::Yes);
                }
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

        quint64 thumbnailsKiB {};
        if (elevate) {
            const QString thumbsPath = QString("/home/%1/.cache/thumbnails").arg(selectedUser);
            if (QFileInfo::exists(thumbsPath)) {
                QStringList args {thumbsPath, "-type", "f"};
                if (ui->radioSaferCache->isChecked()) {
                    const QString days = QString("+%1").arg(ui->spinCache->value());
                    args << "-atime" << days << "-mtime" << days;
                }
                args << "-printf" << "%k\n";
                thumbnailsKiB = sumKiB(helperOut("find", args, QuietMode::Yes));
            }
        } else {
            thumbnailsKiB = cmdOut(findThumbsCmd).toULongLong();
        }
        addToTotal("thumbnails", thumbnailsKiB);
        thumbnails = thumbsDeleteCmd;
        if (!ui->radioReboot->isChecked()) {
            if (elevate) {
                const QString thumbsPath = QString("/home/%1/.cache/thumbnails").arg(selectedUser);
                if (QFileInfo::exists(thumbsPath)) {
                    QStringList args {thumbsPath, "-type", "f"};
                    if (ui->radioSaferCache->isChecked()) {
                        const QString days = QString("+%1").arg(ui->spinCache->value());
                        args << "-atime" << days << "-mtime" << days;
                    }
                    args << "-delete";
                    helperExec("find", args, QuietMode::Yes);
                }
            } else {
                cmdOut(thumbnails);
            }
        }
    }

    QString flatpak;
    if (ui->checkFlatpak->isChecked()) {
        const QString flatpakCleanupCmd = "flatpak uninstall --unused --delete-data --noninteractive";
        const QString flatpakCleanupCheckCmd = "pgrep -a flatpak | grep -v flatpak-s || " + flatpakCleanupCmd;
        const QString userSizeCmd = QString("du -s /home/%1/.local/share/flatpak/ | cut -f1").arg(selectedUser);
        auto scheduleUserShellCommand = [&](const QString &command) -> QString {
            auto quoteShellArg = [](QString value) {
                value.replace('\\', "\\\\");
                value.replace('"', "\\\"");
                return '"' + value + '"';
            };

            if (selectedUser.isEmpty()) {
                return command;
            }
            return QString("runuser -u %1 -- /bin/bash -lc %2").arg(selectedUser, quoteShellArg(command));
        };
        auto execFlatpakScoped = [&](const QString &command) -> QString {
            if (!elevate) {
                return cmdOut(command);
            }

            if (command == userSizeCmd) {
                const QString path = QString("/home/%1/.local/share/flatpak/").arg(selectedUser);
                return QString::number(helperDuSize(path, QuietMode::Yes));
            }

            QString output = helperOut("pgrep", {"-a", "flatpak"}, QuietMode::Yes);
            const QStringList activeFlatpak
                = output.split('\n', Qt::SkipEmptyParts).filter(QRegularExpression("^(?!.*flatpak-s).*$"));
            if (!activeFlatpak.isEmpty()) {
                return output.trimmed();
            }

            helperFlatpakCleanup(selectedUser, QuietMode::Yes);
            return QString();
        };

        quint64 userBefore = execFlatpakScoped(userSizeCmd).toULongLong();
        quint64 systemBefore = helperDuSize("/var/lib/flatpak/", QuietMode::Yes);
        if (!ui->radioReboot->isChecked()) {
            execFlatpakScoped(flatpakCleanupCheckCmd);
        }
        quint64 userAfter = execFlatpakScoped(userSizeCmd).toULongLong();
        quint64 systemAfter = helperDuSize("/var/lib/flatpak/", QuietMode::Yes);

        quint64 userDelta = (userBefore > userAfter) ? userBefore - userAfter : 0;
        quint64 systemDelta = (systemBefore > systemAfter) ? systemBefore - systemAfter : 0;
        addToTotal("flatpak-user", userDelta);
        addToTotal("flatpak-system", systemDelta);

        flatpak = scheduleUserShellCommand(flatpakCleanupCheckCmd);
    }

    QString apt;
    const auto addCommandToSchedule = [&](const QString &command) {
        if (command.isEmpty()) {
            return;
        }
        if (!apt.isEmpty()) {
            apt += '\n';
        }
        apt += command;
    };
    if (ui->groupBoxApt->isChecked()) {
        QString cleanCmd;
        QString cacheLabel = "apt-cache";
        if (isArchLinux) {
            cacheLabel = "pacman-cache";
            if (ui->radioAutoClean->isChecked()) {
                cleanCmd = "pacman -Sc --noconfirm";
            } else if (ui->radioClean->isChecked()) {
                cleanCmd = "pacman -Scc --noconfirm";
            }
        } else {
            if (ui->radioAutoClean->isChecked()) {
                cleanCmd = "apt-get autoclean";
            } else if (ui->radioClean->isChecked()) {
                cleanCmd = "apt-get clean";
            }
        }

        if (!cleanCmd.isEmpty()) {
            const QString cacheDir = isArchLinux ? "/var/cache/pacman/pkg/" : "/var/cache/apt/archives/";
            quint64 before_size = helperDuSize(cacheDir, QuietMode::Yes);

            if (!ui->radioReboot->isChecked()) {
                if (isArchLinux) {
                    helperExec("pacman", ui->radioAutoClean->isChecked() ? QStringList {"-Sc", "--noconfirm"}
                                                                          : QStringList {"-Scc", "--noconfirm"},
                               QuietMode::Yes);
                } else {
                    helperExec("apt-get", {ui->radioAutoClean->isChecked() ? "autoclean" : "clean"},
                               QuietMode::Yes);
                }
            }

            quint64 after_size = helperDuSize(cacheDir, QuietMode::Yes);
            addToTotal(cacheLabel, before_size > after_size ? before_size - after_size : 0);
            addCommandToSchedule(cleanCmd);
        }
    }

    if (ui->checkPurge->isChecked()) {
        const QString purgeCmd = "dpkg -l | awk '/^rc/ { print $2 }' | xargs -r apt-get purge -y";
        quint64 before_size = helperDuSize("/var/lib/dpkg/info/", QuietMode::Yes);
        const QStringList residualPackages
            = cmdOut("dpkg-query -W -f='${db:Status-Abbrev}\t${Package}\n'", QuietMode::Yes)
                                                 .split('\n', Qt::SkipEmptyParts);
        QStringList packagesToPurge;
        for (const QString &line : residualPackages) {
            if (line.startsWith("rc\t")) {
                packagesToPurge << line.section('\t', 1);
            }
        }

        if (!ui->radioReboot->isChecked() && !packagesToPurge.isEmpty()) {
            helperExec("apt-get", QStringList {"purge", "-y"} + packagesToPurge, QuietMode::Yes);
        }

        quint64 after_size = helperDuSize("/var/lib/dpkg/info/", QuietMode::Yes);
        addToTotal("apt-purge", before_size > after_size ? before_size - after_size : 0);
        addCommandToSchedule(purgeCmd);
    }

    QString logs;
    if (ui->groupBoxLogs->isChecked()) {
        QString time = ui->spinBoxLogs->value() > 0 ? QString(" -ctime +%1 -atime +%1").arg(ui->spinBoxLogs->value())
                                                    : QString();
        if (ui->radioOldLogs->isChecked()) {
            QStringList args {"/var/log", "(",
                              "-name", "*.gz", "-o", "-name", "*.old", "-o", "-name", "*.[0-9]", "-o", "-name",
                              "*.[0-9].log", ")"};
            if (ui->spinBoxLogs->value() > 0) {
                const QString days = QString("+%1").arg(ui->spinBoxLogs->value());
                args << "-ctime" << days << "-atime" << days;
            }
            QStringList sizeArgs = args;
            sizeArgs << "-type" << "f" << "-printf" << "%k\n";
            quint64 logsKiB = sumKiB(helperOut("find", sizeArgs, QuietMode::Yes));
            addToTotal("logs-old", logsKiB);
            logs = R"(find /var/log \( -name "*.gz" -o -name "*.old" -o -name "*.[0-9]" -o -name "*.[0-9].log" \))"
                   + time + " -type f -delete 2>/dev/null";
            if (!ui->radioReboot->isChecked()) {
                args << "-type" << "f" << "-delete";
                helperExec("find", args, QuietMode::Yes);
            }
        } else if (ui->radioAllLogs->isChecked()) {
            QStringList args {"/var/log", "-type", "f"};
            if (ui->spinBoxLogs->value() > 0) {
                const QString days = QString("+%1").arg(ui->spinBoxLogs->value());
                args << "-ctime" << days << "-atime" << days;
            }
            QStringList sizeArgs = args;
            sizeArgs << "-printf" << "%k\n";
            quint64 logsKiB = sumKiB(helperOut("find", sizeArgs, QuietMode::Yes));
            addToTotal("logs-all", logsKiB);
            logs = "find /var/log -type f" + time + R"( -exec sh -c "echo > '{}'" \;)"; // empty the logs
            if (!ui->radioReboot->isChecked()) {
                args << "-exec" << "truncate" << "-s" << "0" << "{}" << "+";
                helperExec("find", args, QuietMode::Yes);
            }
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

        quint64 trashKiB {};
        if (user != currentUser) {
            QStringList args;
            if (user == "*") {
                args << "/home" << "-path" << "/home/*/.local/share/Trash/*";
            } else {
                args << QString("/home/%1/.local/share/Trash").arg(user) << "-mindepth" << "1";
            }
            if (ui->spinBoxTrash->value() > 0) {
                const QString days = QString("+%1").arg(ui->spinBoxTrash->value());
                args << "-ctime" << days << "-atime" << days;
            }
            args << "-printf" << "%k\n";
            trashKiB = sumKiB(helperOut("find", args, QuietMode::Yes));
        } else {
            trashKiB = cmdOut(findSizeCmd).toULongLong();
        }
        addToTotal("trash", trashKiB);

        trash = findDeleteCmd;
        if (!ui->radioReboot->isChecked()) {
            if (user != currentUser) {
                QStringList args;
                if (user == "*") {
                    args << "/home" << "-path" << "/home/*/.local/share/Trash/*";
                } else {
                    args << QString("/home/%1/.local/share/Trash").arg(user) << "-mindepth" << "1";
                }
                if (ui->spinBoxTrash->value() > 0) {
                    const QString days = QString("+%1").arg(ui->spinBoxTrash->value());
                    args << "-ctime" << days << "-atime" << days;
                }
                args << "-delete";
                helperExec("find", args, QuietMode::Yes);
            } else {
                cmdOut(trash);
            }
        }
    }

    // Cleanup schedule for the currently selected user only
    auto removeScheduleFiles = [&](const QString &period) {
        QStringList targets;
        const QString writeTarget = cronEntryPath(period, true);
        if (!writeTarget.isEmpty()) {
            targets << writeTarget;
        }
        const QString existingTarget = cronEntryPath(period, false);
        const QString baseTarget = cronEntryBase(period);
        if (!existingTarget.isEmpty() && existingTarget == baseTarget && !targets.contains(baseTarget)) {
            targets << baseTarget;
        }
        for (const auto &path : targets) {
            helperExec("rm", {"-f", path}, QuietMode::Yes);
        }
    };

    auto removeScriptFiles = [&]() {
        QStringList targets;
        const QString writeTarget = scriptFilePath(true);
        if (!writeTarget.isEmpty()) {
            targets << writeTarget;
        }
        const QString existingTarget = scriptFilePath(false);
        const QString baseTarget = scriptFileBase();
        if (!existingTarget.isEmpty() && existingTarget == baseTarget && !targets.contains(baseTarget)) {
            targets << baseTarget;
        }
        for (const auto &path : targets) {
            helperExec("rm", {"-f", path}, QuietMode::Yes);
        }
    };

    removeScheduleFiles("daily");
    removeScheduleFiles("weekly");
    removeScheduleFiles("monthly");
    removeScheduleFiles("@reboot");
    removeScriptFiles();

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
    const double freedMiB = static_cast<double>(total) / 1024.0;
    QString freedText;
    if (freedMiB >= 0.1) {
        int precision = freedMiB < 10.0 ? 1 : 0;
        freedText = QLocale().toString(freedMiB, 'f', precision);
    } else if (total > 0) {
        freedText = QStringLiteral("<0.1");
    } else {
        freedText = QStringLiteral("0");
    }

    QMessageBox::information(this, tr("Done"),
                             ui->radioReboot->isChecked()
                                 ? tr("Cleanup script will run at reboot")
                                 : tr("Cleanup command done") + '\n'
                                       + tr("%1 MiB were freed").arg(freedText));
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
    displayHelpDoc(url, tr("%1 Help").arg(this->windowTitle()));
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

QString MainWindow::cmdOut(const QString &cmd, QuietMode quiet)
{
    if (quiet == QuietMode::No) {
        qDebug().noquote() << cmd;
    }
    QProcess proc;
    QEventLoop loop;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("/bin/bash", {"-c", cmd});
    loop.exec();
    return proc.readAll().trimmed();
}

QString MainWindow::cmdOut(const QString &program, const QStringList &args, QuietMode quiet)
{
    if (quiet == QuietMode::No) {
        qDebug().noquote() << program << args;
    }
    QProcess proc;
    QEventLoop loop;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);
    loop.exec();
    return proc.readAll().trimmed();
}

bool MainWindow::helperProc(const QStringList &helperArgs, QuietMode quiet, QString *output)
{
    if (quiet == QuietMode::No) {
        qDebug().noquote() << "helper" << helperArgs;
    }

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);

    const QString helper = "/usr/lib/" + QApplication::applicationName() + "/helper";
    QStringList programArgs = helperArgs;

    if (getuid() == 0) {
        proc.start(helper, programArgs);
    } else {
        QString elevate;
        if (QFile::exists("/usr/bin/pkexec")) {
            elevate = "/usr/bin/pkexec";
        } else if (QFile::exists("/usr/bin/gksu")) {
            elevate = "/usr/bin/gksu";
        }
        if (elevate.isEmpty()) {
            if (output) {
                *output = QString();
            }
            return false;
        }
        programArgs.prepend(helper);
        proc.start(elevate, programArgs);
    }

    if (!proc.waitForStarted()) {
        if (output) {
            *output = QString();
        }
        return false;
    }

    QEventLoop loop;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();

    if (proc.state() != QProcess::NotRunning) {
        proc.kill();
        proc.waitForFinished();
        if (output) {
            *output = QString();
        }
        return false;
    }

    const QString standardOutput = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    const QString standardError = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
    if (output) {
        *output = standardOutput;
    }
    if (quiet == QuietMode::No && !standardError.isEmpty()) {
        qWarning().noquote() << standardError;
    }
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

bool MainWindow::helperExec(const QString &cmd, const QStringList &args, QuietMode quiet, QString *output)
{
    QStringList helperArgs {"exec", cmd};
    helperArgs += args;
    return helperProc(helperArgs, quiet, output);
}

QString MainWindow::helperOut(const QString &cmd, const QStringList &args, QuietMode quiet)
{
    QString output;
    helperExec(cmd, args, quiet, &output);
    return output;
}

bool MainWindow::helperFlatpakCleanup(const QString &user, QuietMode quiet)
{
    return helperProc({"flatpak-cleanup-user", user}, quiet);
}

quint64 MainWindow::helperDuSize(const QString &path, QuietMode quiet)
{
    bool ok {false};
    const quint64 size = helperOut("du", {"-s", path}, quiet).section('\t', 0, 0).trimmed().toULongLong(&ok);
    return ok ? size : 0;
}

quint64 MainWindow::sumKiB(const QString &output)
{
    quint64 total {};
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        bool ok {false};
        const quint64 value = line.trimmed().toULongLong(&ok);
        if (ok) {
            total += value;
        }
    }
    return total;
}

void MainWindow::pushKernel_clicked()
{
    QString current_kernel = cmdOut("uname -r").trimmed();
    QStringList similar_kernels;
    QStringList other_kernels;
    QStringList installedKernels;
    for (const QString &line : cmdOut("dpkg -l 'linux-image-[0-9]*'").split('\n', Qt::SkipEmptyParts)) {
        if (line.startsWith("ii")) {
            installedKernels << line;
        }
    }

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
            if dpkg -l "${modname}-dkms" 2>/dev/null | grep -q ^ii; then
                echo -n "${modname}-dkms "
            fi
        fi
    done
    if ! lsmod | grep -q -w ^wl
        then
            if dpkg -l broadcom-sta-dkms 2>/dev/null | grep -q ^ii; then
                echo -n broadcom-sta-dkms
            fi
        else
            lspci -v  | grep -q "Kernel driver in use: wl"$ || \
            lsusb -tv | grep -q "Driver=wl, "               || \
            if dpkg -l broadcom-sta-dkms 2>/dev/null | grep -q ^ii; then
                echo -n broadcom-sta-dkms
            fi
    fi)", QuietMode::Yes);  // Run as user, quiet mode

    dumpList = dumpList.trimmed();
    if (dumpList.isEmpty()) {
        QMessageBox::information(this, tr("Info"), tr("No unused network drivers found to remove."));
        setCursor(QCursor(Qt::ArrowCursor));
        return;
    }

    QString helper {"/usr/lib/" + QApplication::applicationName() + "/helper-terminal-keep-open"};
    QStringList validPkgs;
    static const QRegularExpression pkgNameRe(R"(^[a-zA-Z0-9][a-zA-Z0-9.+-]*$)");
    for (const auto &pkg : dumpList.split(' ', Qt::SkipEmptyParts)) {
        if (pkgNameRe.match(pkg).hasMatch())
            validPkgs << pkg;
    }
    if (validPkgs.isEmpty()) {
        QMessageBox::information(this, tr("Info"), tr("No valid packages found to remove."));
        setCursor(QCursor(Qt::ArrowCursor));
        return;
    }
    QString terminalCmd = QString("apt-get purge %1; apt-get install -f").arg(validPkgs.join(' '));
    QProcess terminalProc;
    terminalProc.start("x-terminal-emulator", {"-e", "pkexec", helper, terminalCmd});
    terminalProc.waitForFinished(1800000);  // 30-minute timeout for terminal to close
    if (terminalProc.state() == QProcess::Running) {
        terminalProc.kill();
        terminalProc.waitForFinished();
    }
    setCursor(QCursor(Qt::ArrowCursor));
}
