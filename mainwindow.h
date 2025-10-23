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
#include <QMessageBox>
#include <QSettings>
#include <memory>

namespace Ui
{
class MainWindow;
}

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

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
    bool suppressUserSwitch {false};
    QString cmdOut(const QString &cmd, bool asRoot = false, bool quiet = false);
    QString cmdOutAsRoot(const QString &cmd, bool quiet = false);

    static void addGroupCheckbox(QLayout *layout, const QStringList &package, const QString &name, QStringList *list);
    static void selectRadioButton(class QGroupBox *groupbox, const QButtonGroup *group, int id);
    void loadOptions(bool settingsPreloaded = false);
    void loadSchedule(bool settingsPreloaded = false);
    void loadSettings();
    void removeKernelPackages(const QStringList &list);
    void removeManuals();
    void saveSchedule(const QString &cmd_str, const QString &period);
    void saveSettings();
    void setConnections();
    void setup();
    void startPreferredApp(const QStringList &apps);
    QString homeDirForUser(const QString &user) const;
    QString primaryGroupForUser(const QString &user) const;
    QString cronEntryBase(const QString &period) const;
    QString cronEntryPath(const QString &period, bool forWrite) const;
    QString currentUserSuffix() const;
    QString scriptFileBase() const;
    QString scriptFilePath(bool forWrite) const;
    QString settingsDirForUser(const QString &user) const;
    QString settingsFileForUser(const QString &user) const;
    void initializeSettingsForUser(const QString &user);
    void ensureSettingsOwnership(const QString &user);
};
