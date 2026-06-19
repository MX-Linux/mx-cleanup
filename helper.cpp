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

#include <cstdio>

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
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

[[nodiscard]] const QHash<QString, QStringList> &allowedCommands()
{
    static const QHash<QString, QStringList> commands {
        {"apt-get", {"/usr/bin/apt-get"}},
        {"chmod", {"/usr/bin/chmod", "/bin/chmod"}},
        {"chown", {"/usr/bin/chown", "/bin/chown"}},
        {"du", {"/usr/bin/du", "/bin/du"}},
        {"find", {"/usr/bin/find", "/bin/find"}},
        {"install", {"/usr/bin/install", "/bin/install"}},
        {"mv", {"/usr/bin/mv", "/bin/mv"}},
        {"pacman", {"/usr/bin/pacman"}},
        {"pgrep", {"/usr/bin/pgrep", "/bin/pgrep"}},
        {"rm", {"/usr/bin/rm", "/bin/rm"}},
        {"true", {"/usr/bin/true", "/bin/true"}},
        {"truncate", {"/usr/bin/truncate", "/bin/truncate"}},
    };
    return commands;
}

[[nodiscard]] QString resolveBinary(const QStringList &candidates);

[[nodiscard]] QString resolveRunuserBinary()
{
    static const QStringList candidates {"/usr/sbin/runuser", "/sbin/runuser", "/usr/bin/runuser"};
    return resolveBinary(candidates);
}

[[nodiscard]] QString resolveFlatpakBinary()
{
    static const QStringList candidates {"/usr/bin/flatpak", "/bin/flatpak"};
    return resolveBinary(candidates);
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

[[nodiscard]] int runAllowedCommand(const QString &command, const QStringList &args)
{
    const auto commandIt = allowedCommands().constFind(command);
    if (commandIt == allowedCommands().constEnd()) {
        printError(QString("Command is not allowed: %1").arg(command));
        return 127;
    }

    const QString resolvedCommand = resolveBinary(commandIt.value());
    if (resolvedCommand.isEmpty()) {
        printError(QString("Command is not available: %1").arg(command));
        return 127;
    }

    return relayResult(runProcess(resolvedCommand, args));
}

[[nodiscard]] int runFlatpakCleanupForUser(const QString &user)
{
    static const QRegularExpression safeUserPattern(QStringLiteral("^[A-Za-z0-9._-]+$"));
    if (user.isEmpty() || !safeUserPattern.match(user).hasMatch()) {
        printError(QString("Invalid username: %1").arg(user));
        return 1;
    }

    const QString runuserBinary = resolveRunuserBinary();
    if (runuserBinary.isEmpty()) {
        printError(QStringLiteral("Command is not available: runuser"));
        return 127;
    }

    const QString flatpakBinary = resolveFlatpakBinary();
    if (flatpakBinary.isEmpty()) {
        printError(QStringLiteral("Command is not available: flatpak"));
        return 127;
    }

    return relayResult(runProcess(runuserBinary,
                                  {"-u", user, "--", flatpakBinary, "uninstall", "--unused",
                                   "--delete-data", "--noninteractive"}));
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
    if (action == "exec") {
        if (arguments.size() < 2) {
            printError(QStringLiteral("Missing helper command"));
            return 1;
        }
        return runAllowedCommand(arguments.at(1), arguments.mid(2));
    }

    if (action == "flatpak-cleanup-user") {
        if (arguments.size() != 2) {
            printError(QStringLiteral("flatpak-cleanup-user requires exactly one username"));
            return 1;
        }
        return runFlatpakCleanupForUser(arguments.at(1));
    }

    printError(QString("Unsupported helper action: %1").arg(action));
    return 1;
}
