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

#include <csignal>

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include "about.h"
#include "packagemanager.h"

extern const QString starting_home;

namespace {
// Settings keys shared between loadSettings and saveSettings
inline const QString keyThumbnails = QStringLiteral("Folders/Thumbnails");
inline const QString keyCache = QStringLiteral("Folders/Cache");
inline const QString keyCacheOlderThan = QStringLiteral("Folders/CacheOlderThan");
inline const QString keyCacheSafer = QStringLiteral("Folders/CacheSafer");
inline const QString keyAptCleanup = QStringLiteral("Apt/AptCleanup");
inline const QString keyAptSelection = QStringLiteral("Apt/AptSelection");
inline const QString keyAptPurge = QStringLiteral("Apt/AptPurge");
inline const QString keyLogsCleanup = QStringLiteral("Logs/LogsCleanup");
inline const QString keyLogsSelection = QStringLiteral("Logs/LogsSelection");
inline const QString keyLogsOlderThan = QStringLiteral("Logs/LogsOlderThan");
inline const QString keyTrashCleanup = QStringLiteral("Trash/TrashCleanup");
inline const QString keyTrashSelection = QStringLiteral("Trash/TrashSelection");
inline const QString keyTrashOlderThan = QStringLiteral("Trash/TrashOlderThan");
inline const QString keyFlatpakCleanup = QStringLiteral("Flatpak/FlatpakCleanup");
inline const QString keyFlatpakUnused = QStringLiteral("Flatpak/UninstallUnusedRuntimes");

// Run the child in its own process group so a timeout can kill the whole
// tree (e.g. pkexec -> helper -> apt-get), not just the immediate child.
void makeNewProcessGroup(QProcess &proc)
{
    proc.setChildProcessModifier([] { ::setpgid(0, 0); });
}

void killProcessGroup(QProcess &proc)
{
    const qint64 pid = proc.processId();
    if (pid > 0) {
        ::killpg(static_cast<pid_t>(pid), SIGKILL);
    }
    proc.kill();
    proc.waitForFinished();
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

    if (getuid() != 0
        && !helperProc({"check"}, QuietMode::Yes, nullptr, {}, nullptr, nullptr, kDiskScanTimeoutMs)) {
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
        helperProc({"purge-packages", packageList.at(index)}, QuietMode::Yes, nullptr, {}, nullptr, nullptr,
                  kNoTimeoutMs);
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
            QString content;
            helperProc({"read-settings", user}, QuietMode::Yes, &content);
            if (!content.isEmpty()) {
                tempFile.write(content.toUtf8() + '\n');
            }
            tempFile.close();
            settings = std::make_unique<QSettings>(shadowSettingsPath, QSettings::IniFormat);
            settings->setFallbacksEnabled(false);
            currentSettingsPath = filePath;
            return;
        }
        qWarning().noquote() << "Failed to create temporary file for settings shadow";
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
    helperProc({"chown-settings", user}, QuietMode::Yes);
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

    ui->checkThumbs->setChecked(value(keyThumbnails, true).toBool());
    ui->checkCache->setChecked(value(keyCache, true).toBool());
    ui->spinCache->setValue(value(keyCacheOlderThan, 2).toInt());
    const bool cacheSafer = value(keyCacheSafer, true).toBool();
    ui->radioSaferCache->setChecked(cacheSafer);
    ui->radioAllCache->setChecked(!cacheSafer);

    const bool aptCleanup = value(keyAptCleanup, true).toBool();
    ui->groupBoxApt->setChecked(aptCleanup);
    const int aptSelection = aptCleanup ? value(keyAptSelection, -1).toInt() : -1;
    selectRadioButton(ui->groupBoxApt, ui->buttonGroupApt, aptSelection);
    ui->checkPurge->setChecked(value(keyAptPurge, false).toBool());

    ui->checkFlatpak->setChecked(value(keyFlatpakUnused, false).toBool());

    const bool logsCleanup = value(keyLogsCleanup, true).toBool();
    ui->groupBoxLogs->setChecked(logsCleanup);
    ui->spinBoxLogs->setValue(value(keyLogsOlderThan, 7).toInt());
    const int logsSelection = logsCleanup ? value(keyLogsSelection, -1).toInt() : -1;
    selectRadioButton(ui->groupBoxLogs, ui->buttonGroupLogs, logsSelection);

    const bool trashCleanup = value(keyTrashCleanup, true).toBool();
    ui->groupBoxTrash->setChecked(trashCleanup);
    ui->spinBoxTrash->setValue(value(keyTrashOlderThan, 30).toInt());
    const int trashSelection = trashCleanup ? value(keyTrashSelection, -1).toInt() : -1;
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

    for (const auto &item : list) {
        const QString version
            = item.section(QRegularExpression("linux-image-"), 1).remove(QRegularExpression("-unsigned$"));
        if (!version.isEmpty()) {
            headers << "linux-headers-" + version;
        }
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
            QStringList escapedPkgs;
            escapedPkgs.reserve(list.size());
            for (const auto &pkg : list)
                escapedPkgs << QRegularExpression::escape(pkg);
            // Package/pattern data is passed as bash positional parameters
            // ($1, $2) rather than interpolated into the script text, so
            // none of it is ever parsed as shell syntax.
            bool patternStillInstalled = false;
            cmdOut("/bin/bash",
                   {"-c",
                    "dpkg -l 'linux-image-[0-9]*' | grep ^ii | cut -d ' ' -f3 | grep -v -E \"$1\" | grep -q -- \"$2\"",
                    "bash", escapedPkgs.join('|'), image_pattern},
                   QuietMode::No, &patternStillInstalled);
            if (!patternStillInstalled) {
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
        // As above: the grep pattern and package list are passed as bash
        // positional parameters, not interpolated into the script text.
        QStringList commonArgs {
            "-c", R"(pattern="$1"; shift; apt-get remove -s "$@" | grep '^  ' | grep -oE "$pattern" | tr '\n' ' ')",
            "bash", escapedDepends.join('|')};
        commonArgs += headers_installed;
        common = cmdOut("/bin/bash", commonArgs);
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

    QStringList terminalArgs {"-e", "pkexec", helper, "purge-packages"};
    terminalArgs += packages;
    QProcess terminalProc;
    terminalProc.start("x-terminal-emulator", terminalArgs);
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

// Save cleanup commands to a /etc/cron.daily|weekly|monthly/mx-cleanup script.
// The helper composes and writes the script itself from the validated options.
bool MainWindow::saveSchedule(const QStringList &scheduleOpts, const QString &period)
{
    QStringList args {"write-schedule", period};
    const QString user = ui->comboUserClean->currentText();
    if (!user.isEmpty()) {
        args << "--user" << user;
    }
    args += scheduleOpts;
    if (!helperProc(args, QuietMode::Yes)) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to save the cleanup schedule"));
        return false;
    }
    return true;
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
        store.setValue(keyThumbnails, ui->checkThumbs->isChecked());
        store.setValue(keyCache, ui->checkCache->isChecked());
        store.setValue(keyCacheOlderThan, ui->spinCache->value());
        store.setValue(keyCacheSafer, ui->radioSaferCache->isChecked());

        const bool aptCleanup = ui->groupBoxApt->isChecked();
        store.setValue(keyAptCleanup, aptCleanup);
        store.setValue(keyAptSelection, aptCleanup ? ui->buttonGroupApt->checkedId() : -1);
        store.setValue(keyAptPurge, ui->checkPurge->isChecked());

        const bool logsCleanup = ui->groupBoxLogs->isChecked();
        store.setValue(keyLogsSelection, logsCleanup ? ui->buttonGroupLogs->checkedId() : -1);
        store.setValue(keyLogsOlderThan, ui->spinBoxLogs->value());
        store.setValue(keyLogsCleanup, logsCleanup);

        const bool trashCleanup = ui->groupBoxTrash->isChecked();
        store.setValue(keyTrashCleanup, trashCleanup);
        store.setValue(keyTrashSelection, trashCleanup ? ui->buttonGroupTrash->checkedId() : -1);
        store.setValue(keyTrashOlderThan, ui->spinBoxTrash->value());

        store.setValue(keyFlatpakCleanup, ui->groupBoxFlatpak->isChecked());
        store.setValue(keyFlatpakUnused, ui->checkFlatpak->isChecked());
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

        QByteArray content;
        QFile contentFile(tempFile.fileName());
        if (contentFile.open(QIODevice::ReadOnly)) {
            content = contentFile.readAll();
            contentFile.close();
        }
        helperProc({"write-settings", user}, QuietMode::Yes, nullptr, content);

        currentSettingsPath = targetPath;
        initializeSettingsForUser(user);
        return;
    }

    QDir dir;
    if (!dir.exists(dirPath)) {
        qDebug().noquote() << "Creating settings directory:" << dirPath;
        dir.mkpath(dirPath);
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
    QApplication::setOverrideCursor(Qt::BusyCursor);
    QApplication::processEvents();
    setEnabled(false);

    // Try to elevate privileges if needed
    if (getuid() != 0) {
        if (!helperProc({"check"}, QuietMode::Yes, nullptr, {}, nullptr, nullptr, kDiskScanTimeoutMs)) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to elevate privileges"));
            QApplication::restoreOverrideCursor();
            setEnabled(true);
            return;
        }
    }

    quint64 total {};
    QStringList scheduleOpts;
    const QString selectedUser = ui->comboUserClean->currentText();
    const bool elevate = (selectedUser != currentUser);

    auto addToTotal = [&](const QString &label, quint64 amount) {
        if (amount == 0) {
            return;
        }
        total += amount;
        qDebug().noquote() << "Freed" << label << amount << "KiB";
    };

    QStringList failures;
    auto runOp = [&](const QString &label, const QStringList &args, int timeoutMs = kDiskScanTimeoutMs) -> bool {
        QString errorOutput;
        if (helperProc(args, QuietMode::Yes, nullptr, {}, &errorOutput, nullptr, timeoutMs)) {
            return true;
        }
        failures << (errorOutput.isEmpty() ? label : label + ": " + errorOutput);
        return false;
    };
    if (ui->checkCache->isChecked()) {
        const int cacheDays = ui->radioSaferCache->isChecked() ? ui->spinCache->value() : 0;
        const QString cacheDaysArg = QString::number(cacheDays);
        const QString cachePath = QString("/home/%1/.cache").arg(selectedUser);
        QString period = cacheDays > 0 ? QString(" -atime +%1 -mtime +%1").arg(cacheDays) : QString();
        QString findCmd = QString("find /home/%1/.cache -mindepth 1 ! -path '/home/%1/.cache/thumbnails*'%2 -type f "
                                  "-exec du -c '{}' + | awk 'END{print $1}'")
                              .arg(selectedUser, period);
        quint64 cacheKiB {};
        if (elevate) {
            cacheKiB = sumKiB(
                helperOut({"clean-cache", "size", selectedUser, cacheDaysArg}, QuietMode::Yes, kDiskScanTimeoutMs));
        } else {
            cacheKiB = cmdOut(findCmd, QuietMode::No, nullptr, kDiskScanTimeoutMs).toULongLong();
        }

        scheduleOpts << "--cache" << cacheDaysArg;
        bool cacheOk = true;
        if (!ui->radioReboot->isChecked()) {
            if (elevate) {
                cacheOk = runOp(tr("Cache cleanup"), {"clean-cache", "delete", selectedUser, cacheDaysArg});
            } else {
                const QString output = cmdOut(QString("find /home/%1/.cache -mindepth 1 ! -path '/home/%1/.cache/thumbnails*'%2 -type f -delete")
                                                   .arg(selectedUser, period),
                                               QuietMode::No, &cacheOk, kDiskScanTimeoutMs);
                // Flatpak may leave a directory owned by its sandbox helper in
                // the user's cache.  It cannot be traversed by the user, but
                // must not make unrelated cache cleanup appear to have failed.
                if (!cacheOk && !output.isEmpty()) {
                    const QString ignoredPath = cachePath + "/.flatpak-helper";
                    bool onlyIgnoredErrors = true;
                    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
                        if (!line.contains(ignoredPath)) {
                            onlyIgnoredErrors = false;
                            break;
                        }
                    }
                    if (onlyIgnoredErrors) {
                        cacheOk = true;
                    }
                }
                if (!cacheOk) {
                    failures << tr("Cache cleanup");
                }
            }
        }
        if (cacheOk) {
            addToTotal("cache", cacheKiB);
        }
    }

    if (ui->checkThumbs->isChecked()) {
        const int thumbsDays = ui->radioSaferCache->isChecked() ? ui->spinCache->value() : 0;
        const QString thumbsDaysArg = QString::number(thumbsDays);
        const QString thumbsPath = QString("/home/%1/.cache/thumbnails").arg(selectedUser);
        QString period = thumbsDays > 0 ? QString(" -atime +%1 -mtime +%1").arg(thumbsDays) : QString();
        QString findThumbsCmd = QString("find /home/%1/.cache/thumbnails -type f%2 -exec du -c '{}' + | awk '{field = $1} END {print field}'")
                                    .arg(selectedUser, period);
        QString thumbsDeleteCmd = QString("find /home/%1/.cache/thumbnails -type f%2 -delete 2>/dev/null")
                                      .arg(selectedUser, period);

        quint64 thumbnailsKiB {};
        if (elevate) {
            thumbnailsKiB = sumKiB(helperOut({"clean-thumbnails", "size", selectedUser, thumbsDaysArg},
                                             QuietMode::Yes, kDiskScanTimeoutMs));
        } else if (QFileInfo::exists(thumbsPath)) {
            thumbnailsKiB = cmdOut(findThumbsCmd, QuietMode::No, nullptr, kDiskScanTimeoutMs).toULongLong();
        }
        scheduleOpts << "--thumbs" << thumbsDaysArg;
        bool thumbsOk = true;
        if (!ui->radioReboot->isChecked()) {
            if (elevate) {
                thumbsOk = runOp(tr("Thumbnail cleanup"), {"clean-thumbnails", "delete", selectedUser, thumbsDaysArg});
            } else if (QFileInfo::exists(thumbsPath)) {
                cmdOut(thumbsDeleteCmd, QuietMode::No, &thumbsOk, kDiskScanTimeoutMs);
                if (!thumbsOk) {
                    failures << tr("Thumbnail cleanup");
                }
            }
        }
        if (thumbsOk) {
            addToTotal("thumbnails", thumbnailsKiB);
        }
    }

    if (ui->checkFlatpak->isChecked()) {
        const QString flatpakCleanupCmd = "flatpak uninstall --unused --delete-data --noninteractive";
        const QString flatpakCleanupCheckCmd = "pgrep -a flatpak | grep -v flatpak-s || " + flatpakCleanupCmd;
        const QString userSizeCmd = QString("du -s /home/%1/.local/share/flatpak/ | cut -f1").arg(selectedUser);
        bool flatpakOk = true;
        auto execFlatpakScoped = [&](const QString &command) -> QString {
            if (!elevate) {
                if (command == flatpakCleanupCheckCmd) {
                    bool ok = true;
                    const QString result = cmdOut(command, QuietMode::No, &ok, kNoTimeoutMs);
                    if (!ok) {
                        flatpakOk = false;
                        failures << tr("Flatpak cleanup");
                    }
                    return result;
                }
                return cmdOut(command);
            }

            if (command == userSizeCmd) {
                return QString::number(helperDuSize("flatpak-user", selectedUser, QuietMode::Yes));
            }

            QString output = helperOut({"list-flatpak-procs"}, QuietMode::Yes);
            const QStringList activeFlatpak
                = output.split('\n', Qt::SkipEmptyParts).filter(QRegularExpression("^(?!.*flatpak-s).*$"));
            if (!activeFlatpak.isEmpty()) {
                return output.trimmed();
            }

            flatpakOk = runOp(tr("Flatpak cleanup"), {"flatpak-cleanup-user", selectedUser}, kNoTimeoutMs);
            return QString();
        };

        quint64 userBefore = execFlatpakScoped(userSizeCmd).toULongLong();
        quint64 systemBefore = helperDuSize("flatpak-system", {}, QuietMode::Yes);
        if (!ui->radioReboot->isChecked()) {
            execFlatpakScoped(flatpakCleanupCheckCmd);
        }
        quint64 userAfter = execFlatpakScoped(userSizeCmd).toULongLong();
        quint64 systemAfter = helperDuSize("flatpak-system", {}, QuietMode::Yes);

        quint64 userDelta = (userBefore > userAfter) ? userBefore - userAfter : 0;
        quint64 systemDelta = (systemBefore > systemAfter) ? systemBefore - systemAfter : 0;
        if (flatpakOk) {
            addToTotal("flatpak-user", userDelta);
            addToTotal("flatpak-system", systemDelta);
        }

        scheduleOpts << "--flatpak";
    }

    if (ui->groupBoxApt->isChecked()) {
        QString cleanMode;
        if (ui->radioAutoClean->isChecked()) {
            cleanMode = "auto";
        } else if (ui->radioClean->isChecked()) {
            cleanMode = "full";
        }

        if (!cleanMode.isEmpty()) {
            const QString cacheLabel = isArchLinux ? "pacman-cache" : "apt-cache";
            quint64 before_size = helperDuSize(cacheLabel, {}, QuietMode::Yes);

            bool packageCacheOk = true;
            if (!ui->radioReboot->isChecked()) {
                packageCacheOk = runOp(tr("Package cache cleanup"), {"clean-package-cache", cleanMode}, kNoTimeoutMs);
            }

            quint64 after_size = helperDuSize(cacheLabel, {}, QuietMode::Yes);
            if (packageCacheOk) {
                addToTotal(cacheLabel, before_size > after_size ? before_size - after_size : 0);
            }
            scheduleOpts << "--apt" << cleanMode;
        }
    }

    if (ui->checkPurge->isChecked()) {
        quint64 before_size = helperDuSize("dpkg-info", {}, QuietMode::Yes);
        const QStringList residualPackages
            = cmdOut("dpkg-query -W -f='${db:Status-Abbrev}\t${Package}\n'", QuietMode::Yes)
                                                 .split('\n', Qt::SkipEmptyParts);
        QStringList packagesToPurge;
        for (const QString &line : residualPackages) {
            if (line.startsWith("rc\t")) {
                packagesToPurge << line.section('\t', 1);
            }
        }

        bool purgeOk = true;
        if (!ui->radioReboot->isChecked() && !packagesToPurge.isEmpty()) {
            purgeOk = runOp(tr("Package purge"), QStringList {"purge-packages"} + packagesToPurge, kNoTimeoutMs);
        }

        quint64 after_size = helperDuSize("dpkg-info", {}, QuietMode::Yes);
        if (purgeOk) {
            addToTotal("apt-purge", before_size > after_size ? before_size - after_size : 0);
        }
        scheduleOpts << "--purge";
    }

    if (ui->groupBoxLogs->isChecked()) {
        const QString logsDaysArg = QString::number(ui->spinBoxLogs->value());
        QString logsMode;
        if (ui->radioOldLogs->isChecked()) {
            logsMode = "old";
        } else if (ui->radioAllLogs->isChecked()) {
            logsMode = "all";
        }
        if (!logsMode.isEmpty()) {
            quint64 logsKiB = sumKiB(
                helperOut({"clean-logs", logsMode, "size", logsDaysArg}, QuietMode::Yes, kDiskScanTimeoutMs));
            scheduleOpts << "--logs" << logsMode << logsDaysArg;
            bool logsOk = true;
            if (!ui->radioReboot->isChecked()) {
                logsOk = runOp(tr("Log cleanup"), {"clean-logs", logsMode, "delete", logsDaysArg});
            }
            if (logsOk) {
                addToTotal("logs-" + logsMode, logsKiB);
            }
        }
    }

    if (ui->groupBoxTrash->isChecked()) {
        const bool allUsers = ui->radioAllUsers->isChecked();
        QString user = allUsers ? "*" : ui->comboUserClean->currentText();
        const QString trashUserArg = allUsers ? QStringLiteral("@all") : user;
        const QString trashDaysArg = QString::number(ui->spinBoxTrash->value());
        const QString trashPath = QString("/home/%1/.local/share/Trash").arg(user);
        QString timeTrash = ui->spinBoxTrash->value() > 0
                                ? QString(" -ctime +%1 -atime +%1").arg(ui->spinBoxTrash->value())
                                : QString();
        QString findSizeCmd = QString("find /home/%1/.local/share/Trash -mindepth 1%2 -exec du -sc '{}' + | awk '{field = $1} END {print field}'")
                                .arg(user, timeTrash);
        QString findDeleteCmd = QString("find /home/%1/.local/share/Trash -mindepth 1%2 -delete")
                                .arg(user, timeTrash);

        quint64 trashKiB {};
        if (user != currentUser) {
            trashKiB = sumKiB(
                helperOut({"clean-trash", "size", trashUserArg, trashDaysArg}, QuietMode::Yes, kDiskScanTimeoutMs));
        } else if (QFileInfo::exists(trashPath)) {
            trashKiB = cmdOut(findSizeCmd, QuietMode::No, nullptr, kDiskScanTimeoutMs).toULongLong();
        }

        scheduleOpts << "--trash" << (allUsers ? "all" : "user") << trashDaysArg;
        bool trashOk = true;
        if (!ui->radioReboot->isChecked()) {
            if (user != currentUser) {
                trashOk = runOp(tr("Trash cleanup"), {"clean-trash", "delete", trashUserArg, trashDaysArg});
            } else if (QFileInfo::exists(trashPath)) {
                cmdOut(findDeleteCmd, QuietMode::No, &trashOk, kDiskScanTimeoutMs);
                if (!trashOk) {
                    failures << tr("Trash cleanup");
                }
            }
        }
        if (trashOk) {
            addToTotal("trash", trashKiB);
        }
    }

    // Cleanup schedule for the currently selected user only
    auto removeScheduleFiles = [&](const QString &period) {
        if (!selectedUser.isEmpty()) {
            helperProc({"remove-schedule", "cron", period, selectedUser}, QuietMode::Yes);
        }
        if (cronEntryPath(period, false) == cronEntryBase(period)) {
            helperProc({"remove-schedule", "cron", period}, QuietMode::Yes);
        }
    };

    auto removeScriptFiles = [&]() {
        if (!selectedUser.isEmpty()) {
            helperProc({"remove-schedule", "script", selectedUser}, QuietMode::Yes);
        }
        if (scriptFilePath(false) == scriptFileBase()) {
            helperProc({"remove-schedule", "script"}, QuietMode::Yes);
        }
    };

    static const QStringList allPeriods {"daily", "weekly", "monthly", "@reboot"};

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

    if (schedule.isEmpty()) {
        // Scheduling disabled: drop any existing schedule outright.
        for (const auto &period : allPeriods) {
            removeScheduleFiles(period);
        }
        removeScriptFiles();
    } else {
        // The new schedule is always written to the per-user suffixed path. If the
        // selected period is presently served by the pre-per-user legacy unsuffixed
        // file, remember that now -- once the suffixed write succeeds, that legacy
        // file must be retired too, or both would run.
        const bool legacyCronActive = cronEntryPath(schedule, false) == cronEntryBase(schedule);
        const bool legacyScriptActive = schedule == "@reboot" && scriptFilePath(false) == scriptFileBase();

        if (saveSchedule(scheduleOpts, schedule)) {
            // Only drop schedules for the other periods once the new one is safely
            // written, so a failed write above leaves the prior schedule intact.
            for (const auto &period : allPeriods) {
                if (period != schedule) {
                    removeScheduleFiles(period);
                }
            }
            if (schedule != "@reboot") {
                removeScriptFiles();
            }
            if (legacyCronActive) {
                helperProc({"remove-schedule", "cron", schedule}, QuietMode::Yes);
            }
            if (legacyScriptActive) {
                helperProc({"remove-schedule", "script"}, QuietMode::Yes);
            }
        }
    }

    saveSettings();

    QApplication::restoreOverrideCursor();
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

    if (ui->radioReboot->isChecked()) {
        QMessageBox::information(this, tr("Done"), tr("Cleanup script will run at reboot"));
    } else if (!failures.isEmpty()) {
        QMessageBox::warning(this, tr("Some cleanup steps failed"),
                             tr("%1 MiB were freed, but the following steps failed:").arg(freedText) + "\n\n"
                                 + failures.join('\n'));
    } else {
        QMessageBox::information(this, tr("Done"),
                                 tr("Cleanup command done") + '\n' + tr("%1 MiB were freed").arg(freedText));
    }
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

QString MainWindow::cmdOut(const QString &cmd, QuietMode quiet, bool *ok, int timeoutMs)
{
    if (quiet == QuietMode::No) {
        qDebug().noquote() << cmd;
    }
    QProcess proc;
    makeNewProcessGroup(proc);
    QEventLoop loop;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    }
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("/bin/bash", {"-c", cmd});
    loop.exec();

    if (proc.state() != QProcess::NotRunning) {
        killProcessGroup(proc);
        if (ok) {
            *ok = false;
        }
        return proc.readAll().trimmed();
    }
    if (ok) {
        *ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    }
    return proc.readAll().trimmed();
}

QString MainWindow::cmdOut(const QString &program, const QStringList &args, QuietMode quiet, bool *ok, int timeoutMs)
{
    if (quiet == QuietMode::No) {
        qDebug().noquote() << program << args;
    }
    QProcess proc;
    makeNewProcessGroup(proc);
    QEventLoop loop;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    }
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);
    loop.exec();

    if (proc.state() != QProcess::NotRunning) {
        killProcessGroup(proc);
        if (ok) {
            *ok = false;
        }
        return proc.readAll().trimmed();
    }
    if (ok) {
        *ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    }
    return proc.readAll().trimmed();
}

bool MainWindow::helperProc(const QStringList &helperArgs, QuietMode quiet, QString *output, const QByteArray &input,
                            QString *errorOutput, bool *timedOut, int timeoutMs)
{
    if (quiet == QuietMode::No) {
        qDebug().noquote() << "helper" << helperArgs;
    }
    if (timedOut) {
        *timedOut = false;
    }

    QProcess proc;
    makeNewProcessGroup(proc);
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
            if (errorOutput) {
                *errorOutput = tr("No privilege elevation tool is available");
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
        if (errorOutput) {
            *errorOutput = tr("Failed to start the helper process");
        }
        return false;
    }

    if (!input.isEmpty()) {
        proc.write(input);
    }
    proc.closeWriteChannel();

    QEventLoop loop;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    }
    loop.exec();

    if (proc.state() != QProcess::NotRunning) {
        killProcessGroup(proc);
        if (output) {
            *output = QString();
        }
        if (errorOutput) {
            *errorOutput = tr("Operation timed out");
        }
        if (timedOut) {
            *timedOut = true;
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
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (!ok && errorOutput) {
        *errorOutput = standardError;
    }
    return ok;
}

QString MainWindow::helperOut(const QStringList &helperArgs, QuietMode quiet, int timeoutMs)
{
    QString output;
    helperProc(helperArgs, quiet, &output, {}, nullptr, nullptr, timeoutMs);
    return output;
}

quint64 MainWindow::helperDuSize(const QString &sizeKey, const QString &user, QuietMode quiet, int timeoutMs)
{
    QStringList args {"dir-size", sizeKey};
    if (!user.isEmpty()) {
        args << user;
    }
    bool ok {false};
    const quint64 size = helperOut(args, quiet, timeoutMs).section('\t', 0, 0).trimmed().toULongLong(&ok);
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
    QStringList terminalArgs {"-e", "pkexec", helper, "purge-packages"};
    terminalArgs += validPkgs;
    QProcess terminalProc;
    terminalProc.start("x-terminal-emulator", terminalArgs);
    terminalProc.waitForFinished(1800000);  // 30-minute timeout for terminal to close
    if (terminalProc.state() == QProcess::Running) {
        terminalProc.kill();
        terminalProc.waitForFinished();
    }
    setCursor(QCursor(Qt::ArrowCursor));
}
