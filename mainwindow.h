/**********************************************************************
 *  mainwindow.h
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
#pragma once

#include <QButtonGroup>
#include <QCloseEvent>
#include <QMessageBox>
#include <QSettings>
#include <memory>

namespace Ui
{
class MainWindow;
}

enum class QuietMode
{
    No,
    Yes,
};

// Timeout tiers for helperProc()/cmdOut(): quick metadata/settings operations
// keep a short timeout, disk-scanning cleanup operations get a generous one
// (slow disks, huge caches), and package-manager mutations get none at all --
// killing apt-get/pacman/flatpak mid-operation can corrupt their state, so
// those must be allowed to run to completion.
constexpr int kQuickTimeoutMs = 30000;
constexpr int kDiskScanTimeoutMs = 600000;
constexpr int kNoTimeoutMs = 0;

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void pushAbout_clicked();
    void pushApply_clicked();
    void pushHelp_clicked();
    void pushKernel_clicked();
    void pushRTLremove_clicked();
    void pushUsageAnalyzer_clicked();

private:
    Ui::MainWindow *ui;
    std::unique_ptr<QSettings> settings;
    QString currentSettingsPath;
    QString shadowSettingsPath;
    QString currentUser;
    bool isArchLinux {false};
    bool manualRemovalInProgress {false};
    bool suppressUserSwitch {false};
    QString cmdOut(const QString &cmd, QuietMode quiet = QuietMode::No, bool *ok = nullptr,
                   int timeoutMs = kQuickTimeoutMs);
    QString cmdOut(const QString &program, const QStringList &args, QuietMode quiet = QuietMode::No,
                   bool *ok = nullptr, int timeoutMs = kQuickTimeoutMs);
    bool helperProc(const QStringList &helperArgs, QuietMode quiet = QuietMode::No, QString *output = nullptr,
                    const QByteArray &input = {}, QString *errorOutput = nullptr, bool *timedOut = nullptr,
                    int timeoutMs = kQuickTimeoutMs);
    QString helperOut(const QStringList &helperArgs, QuietMode quiet = QuietMode::No,
                      int timeoutMs = kQuickTimeoutMs);
    quint64 helperDuSize(const QString &sizeKey, const QString &user = {}, QuietMode quiet = QuietMode::No,
                        int timeoutMs = kDiskScanTimeoutMs);

public:
    static quint64 sumKiB(const QString &output);
    static void addGroupCheckbox(QLayout *layout, const QStringList &package, const QString &name, QStringList *list);
    static void selectRadioButton(class QGroupBox *groupbox, const QButtonGroup *group, int id);

private:
    void loadOptions(bool settingsPreloaded = false);
    void loadSchedule(bool settingsPreloaded = false);
    void loadSettings();
    // Populates groupBoxApt/checkPurge/groupBoxLogs and the "all users" side
    // of groupBoxTrash from the shared system-wide script -- always, so a
    // user with no per-user schedule of their own still sees the true shared
    // state instead of falling back to their own unrelated saved preference
    // (which could then overwrite the real shared script on their next
    // Apply). When the shared script doesn't exist yet (upgrade from a
    // pre-split release, where these commands lived inline in the user
    // script), legacyFallbackContent is parsed instead, so the old global
    // options show up checked and the next Apply migrates them into the
    // shared script rather than silently dropping them. Returns whether
    // "all users" trash is scheduled, so callers don't also apply a
    // conflicting "selected user" state on top of it.
    bool loadSystemScriptOptions(const QString &legacyFallbackContent = {});
    void removeKernelPackages(const QStringList &list);
    void removeManuals();
    [[nodiscard]] bool saveSchedule(const QStringList &scheduleOpts, const QString &period);
    [[nodiscard]] bool saveSettings();
    void setConnections();
    void setup();
    void startPreferredApp(const QStringList &apps);
    [[nodiscard]] QString homeDirForUser(const QString &user) const;
    [[nodiscard]] QString cronEntryBase(const QString &period) const;
    [[nodiscard]] QString cronEntryPath(const QString &period, bool forWrite) const;
    [[nodiscard]] QString currentUserSuffix() const;
    [[nodiscard]] QString scriptFileBase() const;
    [[nodiscard]] QString scriptFilePath(bool forWrite) const;
    // Shared system-wide script (apt/purge/logs/trash-all); see helperlib.h's
    // systemScriptPath() for why it has no cron entry of its own.
    [[nodiscard]] QString systemScriptPath() const;
    [[nodiscard]] QString settingsDirForUser(const QString &user) const;
    [[nodiscard]] QString settingsFileForUser(const QString &user) const;
    void initializeSettingsForUser(const QString &user);
    void ensureSettingsOwnership(const QString &user);
};
