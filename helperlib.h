/**********************************************************************
 *  helperlib.h
 **********************************************************************
 * Copyright (C) 2026 MX Authors
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

// Building blocks factored out of helper.cpp so they can be exercised by
// unit tests (argument validation, settings-path handling, schedule
// generation) without needing root or the privileged helper binary itself.

#include <cstdio>
#include <sys/types.h>

#include <QByteArray>
#include <QString>
#include <QStringList>

// Settings content is a small user config file; reject anything larger
// rather than silently truncating it (which would then get saved back,
// permanently losing whatever was beyond the limit).
constexpr qint64 kMaxSettingsBytes = 1024 * 1024;

struct UserInfo
{
    QString name;
    uid_t uid = 0;
    gid_t gid = 0;
};

struct ScheduleOptions
{
    QString user;
    int cacheDays = -1;
    int thumbsDays = -1;
    QString logsMode;
    int logsDays = 0;
    QString aptMode;
    bool purge = false;
    QString trashMode;
    int trashDays = 0;
    bool flatpak = false;
};

void writeAndFlush(FILE *stream, const QByteArray &data);
void printError(const QString &message);

[[nodiscard]] bool lookupUser(const QString &user, UserInfo *info = nullptr);
[[nodiscard]] bool parseDays(const QString &value, int *days);
[[nodiscard]] bool validPackageName(const QString &package);
[[nodiscard]] QString homeDirForUser(const QString &user);

// Traverse home -> .config -> .config/MX-Linux using directory fds with
// O_NOFOLLOW at every component, so a symlink placed anywhere in that
// (user-writable) chain can never redirect a root file operation onto
// another user's files. Each returns an open fd (caller must close it) or
// -1 if a component is missing (when create is false) or is a symlink.
[[nodiscard]] int openHomeDir(const QString &homeDir);
[[nodiscard]] int openConfigDir(int homeFd, bool create);
[[nodiscard]] int openMxLinuxDir(int configFd, bool create, uid_t uid, gid_t gid);
[[nodiscard]] int openSettingsDirFd(const QString &homeDir, bool create, uid_t uid, gid_t gid);

// Write content to a temp file in the same directory as path, set its mode,
// fsync it, then atomically rename it over path (best-effort fsync of the
// directory afterwards), so an interrupted write can never leave a
// scheduled cron job or reboot script partially written or missing.
[[nodiscard]] bool writeFileAsRoot(const QString &path, const QByteArray &content, mode_t mode);

[[nodiscard]] bool validPeriod(const QString &period);
[[nodiscard]] QString cronEntryBase(const QString &period);
[[nodiscard]] QString scriptFileBase();

[[nodiscard]] bool parseScheduleOptions(const QStringList &args, ScheduleOptions *opts);
[[nodiscard]] QString generateScheduleScript(const ScheduleOptions &opts);
