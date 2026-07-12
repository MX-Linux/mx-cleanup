/**********************************************************************
 *  helper.cpp
 **********************************************************************
 * Copyright (C) 2026 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *          OpenAI Codex
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

// Privileged helper for mx-cleanup, run via pkexec. It only accepts a
// fixed set of named actions with validated arguments, never arbitrary
// commands, so a cached polkit authorization (auth_admin_keep) cannot
// be abused to run arbitrary code as root.

#include <cerrno>
#include <cstdio>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include "helperlib.h"
#include "packagemanager.h"

namespace
{
struct ProcessResult
{
    bool started = false;
    int exitCode = 1;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    QByteArray standardOutput;
    QByteArray standardError;
};

[[nodiscard]] QString resolveBinary(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isExecutable()) {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] QString aptGetBinary()
{
    return resolveBinary({"/usr/bin/apt-get"});
}

[[nodiscard]] QString pacmanBinary()
{
    return resolveBinary({"/usr/bin/pacman"});
}

[[nodiscard]] QString findBinary()
{
    return resolveBinary({"/usr/bin/find", "/bin/find"});
}

[[nodiscard]] QString duBinary()
{
    return resolveBinary({"/usr/bin/du", "/bin/du"});
}

[[nodiscard]] QString pgrepBinary()
{
    return resolveBinary({"/usr/bin/pgrep", "/bin/pgrep"});
}

[[nodiscard]] QString runuserBinary()
{
    return resolveBinary({"/usr/sbin/runuser", "/sbin/runuser", "/usr/bin/runuser"});
}

[[nodiscard]] QString flatpakBinary()
{
    return resolveBinary({"/usr/bin/flatpak", "/bin/flatpak"});
}

[[nodiscard]] ProcessResult runProcess(const QString &program, const QStringList &args)
{
    ProcessResult result;

    QProcess process;
    process.start(program, args, QIODevice::ReadOnly);
    if (!process.waitForStarted()) {
        result.standardError = QString("Failed to start %1").arg(program).toUtf8();
        result.exitCode = 127;
        return result;
    }

    result.started = true;
    process.waitForFinished(-1);
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    return result;
}

[[nodiscard]] int relayResult(const ProcessResult &result)
{
    writeAndFlush(stdout, result.standardOutput);
    writeAndFlush(stderr, result.standardError);
    if (!result.started) {
        return result.exitCode;
    }
    return result.exitStatus == QProcess::NormalExit ? result.exitCode : 1;
}

[[nodiscard]] int runRequiredBinary(const QString &binary, const QString &name, const QStringList &args)
{
    if (binary.isEmpty()) {
        printError(QString("Command is not available: %1").arg(name));
        return 127;
    }
    return relayResult(runProcess(binary, args));
}

// check
[[nodiscard]] int cmdCheck()
{
    return 0;
}

// purge-packages <package...>
[[nodiscard]] int cmdPurgePackages(const QStringList &packages)
{
    if (packages.isEmpty()) {
        printError(QStringLiteral("No packages specified"));
        return 1;
    }
    for (const QString &package : packages) {
        if (!validPackageName(package)) {
            printError(QString("Invalid package name: %1").arg(package));
            return 1;
        }
    }
    return runRequiredBinary(aptGetBinary(), "apt-get", QStringList {"purge", "-y", "--"} + packages);
}

// read-settings <user>: print the user's mx-cleanup settings file to stdout
[[nodiscard]] int cmdReadSettings(const QString &user)
{
    UserInfo info;
    if (!lookupUser(user, &info)) {
        return 1;
    }
    const QString homeDir = homeDirForUser(user);
    if (homeDir.isEmpty()) {
        return 1;
    }
    const int dirFd = openSettingsDirFd(homeDir, false, info.uid, info.gid);
    if (dirFd < 0) {
        return 0; // no settings yet
    }
    const int fd = ::openat(dirFd, "mx-cleanup.conf", O_RDONLY | O_NOFOLLOW | O_NOCTTY);
    ::close(dirFd);
    if (fd < 0) {
        return 0; // no settings yet
    }

    QByteArray content;
    char buffer[8192];
    for (;;) {
        const ssize_t bytes = ::read(fd, buffer, sizeof(buffer));
        if (bytes == 0) {
            break; // EOF
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            printError(QString("Failed to read settings file for %1").arg(user));
            ::close(fd);
            return 1;
        }
        if (content.size() + bytes > kMaxSettingsBytes) {
            printError(QString("Settings file for %1 exceeds the %2 byte limit").arg(user).arg(kMaxSettingsBytes));
            ::close(fd);
            return 1;
        }
        content.append(buffer, static_cast<int>(bytes));
    }
    ::close(fd);
    writeAndFlush(stdout, content);
    return 0;
}

// write-settings <user>: write stdin to the user's mx-cleanup settings file
[[nodiscard]] int cmdWriteSettings(const QString &user)
{
    UserInfo info;
    if (!lookupUser(user, &info)) {
        return 1;
    }
    const QString homeDir = homeDirForUser(user);
    if (homeDir.isEmpty()) {
        return 1;
    }

    QFile input;
    if (!input.open(0, QIODevice::ReadOnly)) {
        printError(QStringLiteral("Failed to read settings content"));
        return 1;
    }
    QByteArray content;
    char buffer[8192];
    qint64 bytesRead = 0;
    while ((bytesRead = input.read(buffer, sizeof(buffer))) > 0) {
        if (content.size() + bytesRead > kMaxSettingsBytes) {
            printError(QString("Settings content for %1 exceeds the %2 byte limit").arg(user).arg(kMaxSettingsBytes));
            return 1;
        }
        content.append(buffer, static_cast<int>(bytesRead));
    }
    if (bytesRead < 0) {
        printError(QStringLiteral("Failed to read settings content"));
        return 1;
    }

    const int dirFd = openSettingsDirFd(homeDir, true, info.uid, info.gid);
    if (dirFd < 0) {
        printError(QString("Failed to create settings directory for %1").arg(user));
        return 1;
    }

    const int fd = ::openat(dirFd, "mx-cleanup.conf", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    ::close(dirFd);
    if (fd < 0) {
        printError(QString("Failed to create settings file for %1").arg(user));
        return 1;
    }
    qint64 offset = 0;
    while (offset < content.size()) {
        const ssize_t written = ::write(fd, content.constData() + offset, static_cast<size_t>(content.size() - offset));
        if (written < 0) {
            printError(QString("Failed to write settings file for %1").arg(user));
            ::close(fd);
            return 1;
        }
        offset += written;
    }
    if (::fchmod(fd, 0644) < 0 || ::fchown(fd, info.uid, info.gid) < 0) {
        printError(QString("Failed to set ownership or permissions for settings file for %1").arg(user));
        ::close(fd);
        return 1;
    }
    ::close(fd);
    return 0;
}

// chown-settings <user>: give the user ownership of their settings dir/file
[[nodiscard]] int cmdChownSettings(const QString &user)
{
    UserInfo info;
    if (!lookupUser(user, &info)) {
        return 1;
    }
    const QString homeDir = homeDirForUser(user);
    if (homeDir.isEmpty()) {
        return 1;
    }
    const int dirFd = openSettingsDirFd(homeDir, false, info.uid, info.gid);
    if (dirFd < 0) {
        return 0; // no settings yet
    }
    if (::fchown(dirFd, info.uid, info.gid) < 0) {
        printError(QString("Failed to set ownership for settings directory for %1").arg(user));
        ::close(dirFd);
        return 1;
    }
    const int fileFd = ::openat(dirFd, "mx-cleanup.conf", O_RDONLY | O_NOFOLLOW | O_NOCTTY);
    ::close(dirFd);
    if (fileFd >= 0) {
        if (::fchown(fileFd, info.uid, info.gid) < 0) {
            printError(QString("Failed to set ownership for settings file for %1").arg(user));
            ::close(fileFd);
            return 1;
        }
        ::close(fileFd);
    }
    return 0;
}

// remove-schedule cron <period> [user] | remove-schedule script [user]
[[nodiscard]] int cmdRemoveSchedule(const QStringList &args)
{
    if (args.isEmpty()) {
        printError(QStringLiteral("Missing schedule kind"));
        return 1;
    }
    const QString kind = args.constFirst();
    QString base;
    QString user;
    if (kind == "cron") {
        if (args.size() < 2 || args.size() > 3) {
            printError(QStringLiteral("remove-schedule cron requires a period and optional user"));
            return 1;
        }
        if (!validPeriod(args.at(1))) {
            return 1;
        }
        base = cronEntryBase(args.at(1));
        user = args.value(2);
    } else if (kind == "script") {
        if (args.size() > 2) {
            printError(QStringLiteral("remove-schedule script takes an optional user"));
            return 1;
        }
        base = scriptFileBase();
        user = args.value(1);
    } else {
        printError(QString("Invalid schedule kind: %1").arg(kind));
        return 1;
    }

    if (user.isEmpty()) {
        QFile::remove(base);
        return 0;
    }
    if (!lookupUser(user)) {
        return 1;
    }
    QFile::remove(base + '.' + user);
    return 0;
}

// write-schedule <period> [--user U] [--cache N] [--thumbs N] [--logs old|all N]
//                [--apt auto|full] [--purge] [--trash user|all N] [--flatpak]
// The helper composes the cleanup script itself from the validated options.
[[nodiscard]] int cmdWriteSchedule(const QStringList &args)
{
    if (args.isEmpty()) {
        printError(QStringLiteral("Missing schedule period"));
        return 1;
    }
    const QString period = args.constFirst();
    if (!validPeriod(period)) {
        return 1;
    }
    ScheduleOptions opts;
    if (!parseScheduleOptions(args.mid(1), &opts)) {
        return 1;
    }

    const QString suffix = opts.user.isEmpty() ? QString() : '.' + opts.user;
    const QString cronBase = cronEntryBase(period);
    const QString cronTarget = cronBase + suffix;

    QFile::remove(cronBase);
    if (cronTarget != cronBase) {
        QFile::remove(cronTarget);
    }

    QString scriptTarget = cronTarget;
    if (period == "@reboot") {
        const QString scriptBase = scriptFileBase();
        scriptTarget = scriptBase + suffix;
        QFile::remove(scriptBase);
        if (scriptTarget != scriptBase) {
            QFile::remove(scriptTarget);
        }
        if (!writeFileAsRoot(cronTarget, QString("@reboot root %1\n").arg(scriptTarget).toUtf8(), 0644)) {
            return 1;
        }
    }

    return writeFileAsRoot(scriptTarget, generateScheduleScript(opts).toUtf8(), 0755) ? 0 : 1;
}

[[nodiscard]] bool parseSizeOrDelete(const QString &mode, bool *isDelete)
{
    if (mode == "size") {
        *isDelete = false;
        return true;
    }
    if (mode == "delete") {
        *isDelete = true;
        return true;
    }
    printError(QString("Invalid mode: %1").arg(mode));
    return false;
}

// clean-cache <size|delete> <user> <days>  (days 0 disables the age filter)
[[nodiscard]] int cmdCleanCache(const QStringList &args)
{
    bool isDelete = false;
    int days = 0;
    if (args.size() != 3 || !parseSizeOrDelete(args.at(0), &isDelete) || !lookupUser(args.at(1))
        || !parseDays(args.at(2), &days)) {
        return 1;
    }
    const QString cachePath = "/home/" + args.at(1) + "/.cache";
    if (!QFileInfo::exists(cachePath)) {
        return 0;
    }
    QStringList findArgs {cachePath, "-mindepth", "1", "!", "-path", cachePath + "/thumbnails*"};
    if (days > 0) {
        findArgs << "-atime" << QString("+%1").arg(days) << "-mtime" << QString("+%1").arg(days);
    }
    if (isDelete) {
        findArgs << "-delete";
    } else {
        findArgs << "-type" << "f" << "-printf" << "%k\n";
    }
    return runRequiredBinary(findBinary(), "find", findArgs);
}

// clean-thumbnails <size|delete> <user> <days>
[[nodiscard]] int cmdCleanThumbnails(const QStringList &args)
{
    bool isDelete = false;
    int days = 0;
    if (args.size() != 3 || !parseSizeOrDelete(args.at(0), &isDelete) || !lookupUser(args.at(1))
        || !parseDays(args.at(2), &days)) {
        return 1;
    }
    const QString thumbsPath = "/home/" + args.at(1) + "/.cache/thumbnails";
    if (!QFileInfo::exists(thumbsPath)) {
        return 0;
    }
    QStringList findArgs {thumbsPath, "-mindepth", "1"};
    if (days > 0) {
        findArgs << "-atime" << QString("+%1").arg(days) << "-mtime" << QString("+%1").arg(days);
    }
    findArgs << "-type" << "f" << (isDelete ? QStringList {"-delete"} : QStringList {"-printf", "%k\n"});
    return runRequiredBinary(findBinary(), "find", findArgs);
}

// clean-logs <old|all> <size|delete> <days>
[[nodiscard]] int cmdCleanLogs(const QStringList &args)
{
    bool isDelete = false;
    int days = 0;
    if (args.size() != 3 || !parseSizeOrDelete(args.at(1), &isDelete) || !parseDays(args.at(2), &days)) {
        return 1;
    }
    const QString mode = args.at(0);
    QStringList findArgs;
    if (mode == "old") {
        findArgs << "/var/log" << "(" << "-name" << "*.gz" << "-o" << "-name" << "*.old" << "-o" << "-name"
                 << "*.[0-9]" << "-o" << "-name" << "*.[0-9].log" << ")";
    } else if (mode == "all") {
        findArgs << "/var/log" << "-type" << "f";
    } else {
        printError(QString("Invalid logs mode: %1").arg(mode));
        return 1;
    }
    if (days > 0) {
        findArgs << "-ctime" << QString("+%1").arg(days) << "-atime" << QString("+%1").arg(days);
    }
    if (mode == "old") {
        findArgs << "-type" << "f" << (isDelete ? QStringList {"-delete"} : QStringList {"-printf", "%k\n"});
    } else if (isDelete) {
        findArgs << "-exec" << "truncate" << "-s" << "0" << "{}" << "+";
    } else {
        findArgs << "-printf" << "%k\n";
    }
    return runRequiredBinary(findBinary(), "find", findArgs);
}

// clean-trash <size|delete> <@all|user> <days>
[[nodiscard]] int cmdCleanTrash(const QStringList &args)
{
    bool isDelete = false;
    int days = 0;
    if (args.size() != 3 || !parseSizeOrDelete(args.at(0), &isDelete) || !parseDays(args.at(2), &days)) {
        return 1;
    }
    QStringList findArgs;
    if (args.at(1) == "@all") {
        findArgs << "/home" << "-path" << "/home/*/.local/share/Trash/*";
    } else {
        if (!lookupUser(args.at(1))) {
            return 1;
        }
        const QString trashPath = "/home/" + args.at(1) + "/.local/share/Trash";
        if (!QFileInfo::exists(trashPath)) {
            return 0;
        }
        findArgs << trashPath << "-mindepth" << "1";
    }
    if (days > 0) {
        findArgs << "-ctime" << QString("+%1").arg(days) << "-atime" << QString("+%1").arg(days);
    }
    findArgs << (isDelete ? QStringList {"-delete"} : QStringList {"-printf", "%k\n"});
    return runRequiredBinary(findBinary(), "find", findArgs);
}

// dir-size <apt-cache|pacman-cache|dpkg-info|flatpak-system> | dir-size flatpak-user <user>
[[nodiscard]] int cmdDirSize(const QStringList &args)
{
    if (args.isEmpty()) {
        printError(QStringLiteral("Missing dir-size key"));
        return 1;
    }
    const QString key = args.constFirst();
    QString path;
    if (key == "apt-cache") {
        path = QStringLiteral("/var/cache/apt/archives/");
    } else if (key == "pacman-cache") {
        path = QStringLiteral("/var/cache/pacman/pkg/");
    } else if (key == "dpkg-info") {
        path = QStringLiteral("/var/lib/dpkg/info/");
    } else if (key == "flatpak-system") {
        path = QStringLiteral("/var/lib/flatpak/");
    } else if (key == "flatpak-user") {
        if (args.size() != 2 || !lookupUser(args.at(1))) {
            return 1;
        }
        path = QString("/home/%1/.local/share/flatpak/").arg(args.at(1));
    } else {
        printError(QString("Invalid dir-size key: %1").arg(key));
        return 1;
    }
    return runRequiredBinary(duBinary(), "du", {"-s", path});
}

// list-flatpak-procs
[[nodiscard]] int cmdListFlatpakProcs()
{
    return runRequiredBinary(pgrepBinary(), "pgrep", {"-a", "flatpak"});
}

// clean-package-cache <auto|full>
[[nodiscard]] int cmdCleanPackageCache(const QString &mode)
{
    if (mode != "auto" && mode != "full") {
        printError(QString("Invalid package cache mode: %1").arg(mode));
        return 1;
    }
    if (isArchLinuxHost()) {
        return runRequiredBinary(pacmanBinary(), "pacman", {mode == "auto" ? "-Sc" : "-Scc", "--noconfirm"});
    }
    return runRequiredBinary(aptGetBinary(), "apt-get", {mode == "auto" ? "autoclean" : "clean"});
}

// flatpak-cleanup-user <user>
[[nodiscard]] int cmdFlatpakCleanupUser(const QString &user)
{
    if (!lookupUser(user)) {
        return 1;
    }
    const QString runuser = runuserBinary();
    if (runuser.isEmpty()) {
        printError(QStringLiteral("Command is not available: runuser"));
        return 127;
    }
    const QString flatpak = flatpakBinary();
    if (flatpak.isEmpty()) {
        printError(QStringLiteral("Command is not available: flatpak"));
        return 127;
    }
    return relayResult(runProcess(
        runuser, {"-u", user, "--", flatpak, "uninstall", "--unused", "--delete-data", "--noninteractive"}));
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments().mid(1);

    if (arguments.isEmpty()) {
        printError(QStringLiteral("Missing helper action"));
        return 1;
    }

    const QString action = arguments.constFirst();
    const QStringList args = arguments.mid(1);

    if (action == "check") {
        return cmdCheck();
    }
    if (action == "purge-packages") {
        return cmdPurgePackages(args);
    }
    if (action == "read-settings" && args.size() == 1) {
        return cmdReadSettings(args.constFirst());
    }
    if (action == "write-settings" && args.size() == 1) {
        return cmdWriteSettings(args.constFirst());
    }
    if (action == "chown-settings" && args.size() == 1) {
        return cmdChownSettings(args.constFirst());
    }
    if (action == "remove-schedule") {
        return cmdRemoveSchedule(args);
    }
    if (action == "write-schedule") {
        return cmdWriteSchedule(args);
    }
    if (action == "clean-cache") {
        return cmdCleanCache(args);
    }
    if (action == "clean-thumbnails") {
        return cmdCleanThumbnails(args);
    }
    if (action == "clean-logs") {
        return cmdCleanLogs(args);
    }
    if (action == "clean-trash") {
        return cmdCleanTrash(args);
    }
    if (action == "dir-size") {
        return cmdDirSize(args);
    }
    if (action == "list-flatpak-procs") {
        return cmdListFlatpakProcs();
    }
    if (action == "clean-package-cache" && args.size() == 1) {
        return cmdCleanPackageCache(args.constFirst());
    }
    if (action == "flatpak-cleanup-user" && args.size() == 1) {
        return cmdFlatpakCleanupUser(args.constFirst());
    }

    printError(QString("Unsupported helper action: %1").arg(action));
    return 1;
}
