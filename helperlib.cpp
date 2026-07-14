/**********************************************************************
 *  helperlib.cpp
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
#include "helperlib.h"

#include <cerrno>
#include <cstdlib>

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "packagemanager.h"
#include "usernameutils.h"

void writeAndFlush(FILE *stream, const QByteArray &data)
{
    if (!data.isEmpty()) {
        std::fwrite(data.constData(), 1, static_cast<size_t>(data.size()), stream);
        std::fflush(stream);
    }
}

void printError(const QString &message)
{
    writeAndFlush(stderr, message.toUtf8() + '\n');
}

bool lookupUser(const QString &user, UserInfo *info)
{
    if (!validUserNameSyntax(user)) {
        printError(QString("Invalid username: %1").arg(user));
        return false;
    }
    const struct passwd *pwd = passwdForUser(user);
    if (!pwd) {
        printError(QString("Unknown user: %1").arg(user));
        return false;
    }
    if (info) {
        info->name = user;
        info->uid = pwd->pw_uid;
        info->gid = pwd->pw_gid;
    }
    return true;
}

bool parseDays(const QString &value, int *days)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || parsed < 0 || parsed > 36500) {
        printError(QString("Invalid number of days: %1").arg(value));
        return false;
    }
    *days = parsed;
    return true;
}

bool validPackageName(const QString &package)
{
    static const QRegularExpression packagePattern(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9.+-]*$"));
    return packagePattern.match(package).hasMatch();
}

QString homeDirForUser(const QString &user)
{
    const struct passwd *pwd = passwdForUser(user);
    if (!pwd || pwd->pw_dir == nullptr || pwd->pw_dir[0] == '\0') {
        return {};
    }
    return QString::fromLocal8Bit(pwd->pw_dir);
}

int openHomeDir(const QString &homeDir)
{
    return ::open(QFile::encodeName(homeDir).constData(), O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

int openConfigDir(int homeFd, bool create)
{
    if (create && ::mkdirat(homeFd, ".config", 0755) < 0 && errno != EEXIST) {
        return -1;
    }
    return ::openat(homeFd, ".config", O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

int openMxLinuxDir(int configFd, bool create, uid_t uid, gid_t gid)
{
    if (create && ::mkdirat(configFd, "MX-Linux", 0755) < 0 && errno != EEXIST) {
        return -1;
    }
    const int fd = ::openat(configFd, "MX-Linux", O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0 && create) {
        if (::fchown(fd, uid, gid) < 0 || ::fchmod(fd, 0755) < 0) {
            ::close(fd);
            return -1;
        }
    }
    return fd;
}

int openSettingsDirFd(const QString &homeDir, bool create, uid_t uid, gid_t gid)
{
    const int homeFd = openHomeDir(homeDir);
    if (homeFd < 0) {
        return -1;
    }
    const int configFd = openConfigDir(homeFd, create);
    ::close(homeFd);
    if (configFd < 0) {
        return -1;
    }
    const int mxFd = openMxLinuxDir(configFd, create, uid, gid);
    ::close(configFd);
    return mxFd;
}

bool stageFileAsRoot(const QString &path, const QByteArray &content, mode_t mode, StagedFile *staged)
{
    const QString dir = QFileInfo(path).path();
    QByteArray tmpPath = QFile::encodeName(dir + "/.mx-cleanup.XXXXXX");
    const int fd = ::mkstemp(tmpPath.data());
    if (fd < 0) {
        printError(QString("Failed to create a temporary file in %1").arg(dir));
        return false;
    }

    qint64 offset = 0;
    while (offset < content.size()) {
        const ssize_t written = ::write(fd, content.constData() + offset, static_cast<size_t>(content.size() - offset));
        if (written < 0) {
            printError(QString("Failed to write %1").arg(path));
            ::close(fd);
            ::unlink(tmpPath.constData());
            return false;
        }
        offset += written;
    }
    if (::fchmod(fd, mode) < 0 || ::fsync(fd) < 0) {
        printError(QString("Failed to finalize %1").arg(path));
        ::close(fd);
        ::unlink(tmpPath.constData());
        return false;
    }
    ::close(fd);

    staged->tmpPath = tmpPath;
    staged->finalPath = path;
    return true;
}

bool commitStagedFile(StagedFile *staged)
{
    if (::rename(staged->tmpPath.constData(), QFile::encodeName(staged->finalPath).constData()) < 0) {
        printError(QString("Failed to replace %1").arg(staged->finalPath));
        ::unlink(staged->tmpPath.constData());
        staged->tmpPath.clear();
        return false;
    }
    staged->tmpPath.clear();

    // Also fsync the directory so the renamed entry survives a crash --
    // otherwise the rename above is not crash-durable even though the file
    // content and mode are.
    const QString dir = QFileInfo(staged->finalPath).path();
    const int dirFd = ::open(QFile::encodeName(dir).constData(), O_DIRECTORY | O_RDONLY);
    if (dirFd >= 0) {
        ::fsync(dirFd);
        ::close(dirFd);
    }
    return true;
}

void discardStagedFile(StagedFile *staged)
{
    if (!staged->tmpPath.isEmpty()) {
        ::unlink(staged->tmpPath.constData());
        staged->tmpPath.clear();
    }
}

bool writeFileAsRoot(const QString &path, const QByteArray &content, mode_t mode)
{
    StagedFile staged;
    if (!stageFileAsRoot(path, content, mode, &staged)) {
        return false;
    }
    return commitStagedFile(&staged);
}

bool validPeriod(const QString &period)
{
    static const QStringList periods {"daily", "weekly", "monthly", "@reboot"};
    if (!periods.contains(period)) {
        printError(QString("Invalid period: %1").arg(period));
        return false;
    }
    return true;
}

QString cronEntryBase(const QString &period)
{
    if (period == "@reboot") {
        return QStringLiteral("/etc/cron.d/mx-cleanup");
    }
    return "/etc/cron." + period + "/mx-cleanup";
}

QString scriptFileBase()
{
    return QStringLiteral("/usr/bin/mx-cleanup-script");
}

QString systemScriptPath()
{
    return QStringLiteral("/usr/bin/mx-cleanup-system-script");
}

bool parseScheduleOptions(const QStringList &args, ScheduleOptions *opts)
{
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == "--user" && i + 1 < args.size()) {
            opts->user = args.at(++i);
            if (!lookupUser(opts->user)) {
                return false;
            }
        } else if (arg == "--cache" && i + 1 < args.size()) {
            if (!parseDays(args.at(++i), &opts->cacheDays)) {
                return false;
            }
        } else if (arg == "--thumbs" && i + 1 < args.size()) {
            if (!parseDays(args.at(++i), &opts->thumbsDays)) {
                return false;
            }
        } else if (arg == "--logs" && i + 2 < args.size()) {
            opts->logsMode = args.at(++i);
            if (opts->logsMode != "old" && opts->logsMode != "all") {
                printError(QString("Invalid logs mode: %1").arg(opts->logsMode));
                return false;
            }
            if (!parseDays(args.at(++i), &opts->logsDays)) {
                return false;
            }
        } else if (arg == "--apt" && i + 1 < args.size()) {
            opts->aptMode = args.at(++i);
            if (opts->aptMode != "auto" && opts->aptMode != "full") {
                printError(QString("Invalid apt mode: %1").arg(opts->aptMode));
                return false;
            }
        } else if (arg == "--purge") {
            opts->purge = true;
        } else if (arg == "--trash" && i + 2 < args.size()) {
            opts->trashMode = args.at(++i);
            if (opts->trashMode != "user" && opts->trashMode != "all") {
                printError(QString("Invalid trash mode: %1").arg(opts->trashMode));
                return false;
            }
            if (!parseDays(args.at(++i), &opts->trashDays)) {
                return false;
            }
        } else if (arg == "--flatpak") {
            opts->flatpak = true;
        } else {
            printError(QString("Invalid schedule option: %1").arg(arg));
            return false;
        }
    }
    if (opts->user.isEmpty() && (opts->cacheDays >= 0 || opts->thumbsDays >= 0 || opts->trashMode == "user")) {
        printError(QStringLiteral("Missing --user for per-user cleanup options"));
        return false;
    }
    return true;
}

namespace
{
QString shellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace('\'', QStringLiteral("'\\''"));
    return '\'' + quoted + '\'';
}

QString scheduleAge(int days)
{
    return days > 0 ? QString(" -ctime +%1 -atime +%1").arg(days) : QString();
}
} // namespace

QString generateUserScript(const ScheduleOptions &opts)
{
    auto cacheAge = [](int days) {
        return days > 0 ? QString(" -atime +%1 -mtime +%1").arg(days) : QString();
    };

    QStringList parts;
    if (opts.cacheDays >= 0) {
        const QString cachePath = "/home/" + opts.user + "/.cache";
        parts << QString("find %1 -mindepth 1 ! -path %2%3 -type f -delete 2>/dev/null")
                     .arg(shellQuote(cachePath), shellQuote(cachePath + "/thumbnails*"),
                          cacheAge(opts.cacheDays));
    }
    if (opts.thumbsDays >= 0) {
        const QString thumbsPath = "/home/" + opts.user + "/.cache/thumbnails";
        parts << QString("find %1 -type f%2 -delete 2>/dev/null")
                     .arg(shellQuote(thumbsPath), cacheAge(opts.thumbsDays));
    }
    if (opts.trashMode == "user") {
        const QString trashPath = "/home/" + opts.user + "/.local/share/Trash";
        parts << QString("find %1 -mindepth 1%2 -delete")
                     .arg(shellQuote(trashPath), scheduleAge(opts.trashDays));
    }
    if (opts.flatpak) {
        const QString cleanupCmd
            = QStringLiteral("pgrep -a flatpak | grep -v flatpak-s || flatpak uninstall --unused "
                             "--delete-data --noninteractive");
        parts << (opts.user.isEmpty()
                      ? cleanupCmd
                      : QString("runuser -u %1 -- /bin/bash -lc %2")
                            .arg(shellQuote(opts.user), shellQuote(cleanupCmd)));
    }
    // Different users can schedule their own cron entry at different periods,
    // all invoking this same shared system-wide script, rather than
    // duplicating apt/purge/logs/trash-all commands into every user's file.
    parts << QString("[ -x %1 ] && %1").arg(systemScriptPath());

    return "#!/bin/sh\n#\n# This file was created by MX Cleanup\n#\n\n" + parts.join('\n');
}

QString generateSystemScript(const ScheduleOptions &opts)
{
    QStringList parts;
    if (opts.logsMode == "old") {
        parts << R"(find /var/log \( -name "*.gz" -o -name "*.old" -o -name "*.[0-9]" -o -name "*.[0-9].log" \))"
                     + scheduleAge(opts.logsDays) + " -type f -delete 2>/dev/null";
    } else if (opts.logsMode == "all") {
        parts << "find /var/log -type f" + scheduleAge(opts.logsDays) + " -exec truncate -s 0 {} + 2>/dev/null";
    }
    QString apt;
    if (!opts.aptMode.isEmpty()) {
        if (isArchLinuxHost()) {
            apt = opts.aptMode == "auto" ? QStringLiteral("pacman -Sc --noconfirm")
                                         : QStringLiteral("pacman -Scc --noconfirm");
        } else {
            apt = opts.aptMode == "auto" ? QStringLiteral("apt-get autoclean") : QStringLiteral("apt-get clean");
        }
    }
    if (opts.purge) {
        if (!apt.isEmpty()) {
            apt += '\n';
        }
        apt += QStringLiteral("dpkg -l | awk '/^rc/ { print $2 }' | xargs -r apt-get purge -y");
    }
    if (!apt.isEmpty()) {
        parts << apt;
    }
    if (opts.trashMode == "all") {
        parts << QString("find /home/*/.local/share/Trash -mindepth 1%1 -delete").arg(scheduleAge(opts.trashDays));
    }

    return "#!/bin/sh\n#\n# This file was created by MX Cleanup\n#\n\n" + parts.join('\n');
}
