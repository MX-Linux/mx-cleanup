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

#include <cstdio>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

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

struct UserInfo
{
    QString name;
    uid_t uid = 0;
    gid_t gid = 0;
};

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

// Same OS detection as the GUI, so both pick the same package manager
[[nodiscard]] bool isArchLinuxHost()
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

[[nodiscard]] bool lookupUser(const QString &user, UserInfo *info = nullptr)
{
    static const QRegularExpression safeUserPattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    if (user.isEmpty() || !safeUserPattern.match(user).hasMatch()) {
        printError(QString("Invalid username: %1").arg(user));
        return false;
    }
    const struct passwd *pwd = getpwnam(user.toUtf8().constData());
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

[[nodiscard]] bool parseDays(const QString &value, int *days)
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

[[nodiscard]] bool validPackageName(const QString &package)
{
    static const QRegularExpression packagePattern(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9.+-]*$"));
    return packagePattern.match(package).hasMatch();
}

[[nodiscard]] QString homeDirForUser(const QString &user)
{
    const struct passwd *pwd = getpwnam(user.toUtf8().constData());
    if (!pwd || pwd->pw_dir == nullptr || pwd->pw_dir[0] == '\0') {
        return {};
    }
    return QString::fromLocal8Bit(pwd->pw_dir);
}

[[nodiscard]] int openHomeDir(const QString &homeDir)
{
    return ::open(QFile::encodeName(homeDir).constData(), O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

// Open (optionally creating) ~/.config relative to a verified home directory
// fd, refusing to follow a symlink at this component. The whole chain down
// to the settings file is writable by the (untrusted) settings owner, so
// every component must be traversed this way -- a symlink anywhere in it
// must never let a root file operation land on another user's files.
[[nodiscard]] int openConfigDir(int homeFd, bool create)
{
    if (create && ::mkdirat(homeFd, ".config", 0755) < 0 && errno != EEXIST) {
        return -1;
    }
    return ::openat(homeFd, ".config", O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

// Open (optionally creating) ~/.config/MX-Linux relative to a verified
// .config fd, refusing to follow a symlink at this component. When create is
// set, ownership is (re)applied so the directory is always user-owned.
[[nodiscard]] int openMxLinuxDir(int configFd, bool create, uid_t uid, gid_t gid)
{
    if (create && ::mkdirat(configFd, "MX-Linux", 0755) < 0 && errno != EEXIST) {
        return -1;
    }
    const int fd = ::openat(configFd, "MX-Linux", O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0 && create) {
        ::fchown(fd, uid, gid);
        ::fchmod(fd, 0755);
    }
    return fd;
}

// Traverse home -> .config -> .config/MX-Linux using directory fds with
// O_NOFOLLOW at every component. Returns -1 if any component is missing
// (when create is false) or turns out to be a symlink.
[[nodiscard]] int openSettingsDirFd(const QString &homeDir, bool create, uid_t uid, gid_t gid)
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

[[nodiscard]] bool writeFileAsRoot(const QString &path, const QByteArray &content, mode_t mode)
{
    QFile::remove(path);
    const int fd = ::open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
    if (fd < 0) {
        printError(QString("Failed to create %1").arg(path));
        return false;
    }
    qint64 offset = 0;
    while (offset < content.size()) {
        const ssize_t written = ::write(fd, content.constData() + offset, static_cast<size_t>(content.size() - offset));
        if (written < 0) {
            printError(QString("Failed to write %1").arg(path));
            ::close(fd);
            return false;
        }
        offset += written;
    }
    ::fchmod(fd, mode);
    ::close(fd);
    return true;
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
    char buffer[8192];
    ssize_t bytes = 0;
    qint64 total = 0;
    while ((bytes = ::read(fd, buffer, sizeof(buffer))) > 0 && total < 1024 * 1024) {
        writeAndFlush(stdout, QByteArray(buffer, static_cast<int>(bytes)));
        total += bytes;
    }
    ::close(fd);
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
    const QByteArray content = input.read(1024 * 1024);

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
    ::fchmod(fd, 0644);
    ::fchown(fd, info.uid, info.gid);
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
    ::fchown(dirFd, info.uid, info.gid);
    const int fileFd = ::openat(dirFd, "mx-cleanup.conf", O_RDONLY | O_NOFOLLOW | O_NOCTTY);
    ::close(dirFd);
    if (fileFd >= 0) {
        ::fchown(fileFd, info.uid, info.gid);
        ::close(fileFd);
    }
    return 0;
}

[[nodiscard]] bool validPeriod(const QString &period)
{
    static const QStringList periods {"daily", "weekly", "monthly", "@reboot"};
    if (!periods.contains(period)) {
        printError(QString("Invalid period: %1").arg(period));
        return false;
    }
    return true;
}

[[nodiscard]] QString cronEntryBase(const QString &period)
{
    if (period == "@reboot") {
        return QStringLiteral("/etc/cron.d/mx-cleanup");
    }
    return "/etc/cron." + period + "/mx-cleanup";
}

[[nodiscard]] QString scriptFileBase()
{
    return QStringLiteral("/usr/bin/mx-cleanup-script");
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

[[nodiscard]] bool parseScheduleOptions(const QStringList &args, ScheduleOptions *opts)
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

[[nodiscard]] QString generateScheduleScript(const ScheduleOptions &opts)
{
    auto cacheAge = [](int days) {
        return days > 0 ? QString(" -atime +%1 -mtime +%1").arg(days) : QString();
    };
    auto trashAge = [](int days) {
        return days > 0 ? QString(" -ctime +%1 -atime +%1").arg(days) : QString();
    };

    QStringList parts;
    if (opts.cacheDays >= 0) {
        parts << QString("find /home/%1/.cache -mindepth 1 ! -path '/home/%1/.cache/thumbnails*'%2 -delete 2>/dev/null")
                     .arg(opts.user, cacheAge(opts.cacheDays));
    }
    if (opts.thumbsDays >= 0) {
        parts << QString("find /home/%1/.cache/thumbnails -type f%2 -delete 2>/dev/null")
                     .arg(opts.user, cacheAge(opts.thumbsDays));
    }
    if (opts.logsMode == "old") {
        parts << R"(find /var/log \( -name "*.gz" -o -name "*.old" -o -name "*.[0-9]" -o -name "*.[0-9].log" \))"
                     + trashAge(opts.logsDays) + " -type f -delete 2>/dev/null";
    } else if (opts.logsMode == "all") {
        parts << "find /var/log -type f" + trashAge(opts.logsDays) + R"( -exec sh -c "echo > '{}'" \;)";
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
        parts << QString("find /home/*/.local/share/Trash -mindepth 1%1 -delete").arg(trashAge(opts.trashDays));
    } else if (opts.trashMode == "user") {
        parts << QString("find /home/%1/.local/share/Trash -mindepth 1%2 -delete")
                     .arg(opts.user, trashAge(opts.trashDays));
    }
    if (opts.flatpak) {
        const QString cleanupCmd
            = QStringLiteral("pgrep -a flatpak | grep -v flatpak-s || flatpak uninstall --unused "
                             "--delete-data --noninteractive");
        parts << (opts.user.isEmpty() ? cleanupCmd
                                      : QString("runuser -u %1 -- /bin/bash -lc \"%2\"").arg(opts.user, cleanupCmd));
    }

    return "#!/bin/sh\n#\n# This file was created by MX Cleanup\n#\n\n" + parts.join('\n');
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
