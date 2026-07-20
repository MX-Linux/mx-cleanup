/**********************************************************************
 *  test_utils.cpp
 **********************************************************************
 * Copyright (C) 2025 MX Authors
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

#include <pwd.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include "../helperlib.h"
#include "../mainwindow.h"
#include "../packagemanager.h"
#include "../usernameutils.h"

class TestUtils : public QObject
{
    Q_OBJECT

private slots:
    void testSumKiB_data();
    void testSumKiB();
    void testSumKiB_EmptyInput();
    void testSumKiB_InvalidLines();

    void testIsArchLinuxHost_ArchRelease();
    void testIsArchLinuxHost_IdArch();
    void testIsArchLinuxHost_IdLikeArch();
    void testIsArchLinuxHost_Debian();
    void testIsArchLinuxHost_MissingOsRelease();
    void testIsArchLinuxHost_DebianWithPacmanInstalled();

    void testValidPackageName_data();
    void testValidPackageName();

    void testParseDays_data();
    void testParseDays();

    void testValidPeriod_data();
    void testValidPeriod();
    void testCronEntryBase();

    void testLookupUser_Valid();
    void testLookupUser_InvalidChars();
    void testLookupUser_Unknown();
    void testLookupUser_Empty();
    void testValidUserNameSyntax_data();
    void testValidUserNameSyntax();
    void testUserScheduleFileId();

    void testHomeDirForUser_Valid();
    void testHomeDirForUser_Unknown();

    void testParseScheduleOptions_MissingUserForPerUserOption();
    void testParseScheduleOptions_UnknownUser();
    void testParseScheduleOptions_InvalidLogsMode();
    void testParseScheduleOptions_InvalidAptMode();
    void testParseScheduleOptions_InvalidTrashMode();
    void testParseScheduleOptions_UnknownOption();
    void testParseScheduleOptions_ValidCombination();

    void testGenerateUserScript_Cache();
    void testGenerateUserScript_NoAgeFilter();
    void testGenerateUserScript_CallsSystemScript();
    void testGenerateUserScript_UnicodeUser();
    void testScriptOptions_RoundTrip();
    void testScriptOptions_LegacyUnquoted();
    void testGenerateSystemScript_AllLogsUsesTruncate();
    void testGenerateSystemScript_Purge();
    void testGenerateSystemScript_TrashAll();

    void testOpenSettingsDirFd_NoSettingsYet();
    void testOpenSettingsDirFd_CreatesWithCorrectOwnership();
    void testOpenSettingsDirFd_RefusesConfigSymlink();
    void testOpenSettingsDirFd_RefusesMxLinuxSymlink();

    void testWriteFileAsRoot_WritesContentAndMode();
    void testWriteFileAsRoot_ReplacesExistingContentAtomically();
    void testWriteFileAsRoot_NoLeftoverTempFiles();
    void testWriteFileAsRoot_FailsInMissingDirectory();

    void testStagedFile_NothingVisibleUntilCommit();
    void testStagedFile_DiscardLeavesTargetAndDirUntouched();

    void testGenerateSystemScript_HostileLogFilenamesAreSafe();
    void testWriteFileAsRoot_FailedWritePreservesExistingTarget();
    void testGenerateScripts_ScopeIsolation();
    void testHelperBinary_ReportsFailureExitCodes();
};

void TestUtils::testSumKiB_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<quint64>("expected");

    QTest::newRow("single line")       << QStringLiteral("42")     << 42ULL;
    QTest::newRow("multiple lines")    << QStringLiteral("10\n20\n30") << 60ULL;
    QTest::newRow("trailing newline")  << QStringLiteral("100\n")  << 100ULL;
    QTest::newRow("real output style") << QStringLiteral("  4\n  8\n 15") << 27ULL;
}

void TestUtils::testSumKiB()
{
    QFETCH(QString, input);
    QFETCH(quint64, expected);

    QCOMPARE(MainWindow::sumKiB(input), expected);
}

void TestUtils::testSumKiB_EmptyInput()
{
    QCOMPARE(MainWindow::sumKiB(QString()), 0ULL);
}

void TestUtils::testSumKiB_InvalidLines()
{
    QString input = QStringLiteral("10\nnotanumber\n20\n   \n30\n");
    QCOMPARE(MainWindow::sumKiB(input), 60ULL);
}

namespace {
QString writeFixture(QTemporaryDir &dir, const QString &name, const QString &content)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(content.toUtf8());
    }
    return path;
}
}

void TestUtils::testIsArchLinuxHost_ArchRelease()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archRelease = writeFixture(dir, "arch-release", QString());
    const QString missingOsRelease = dir.filePath("no-such-os-release");

    QVERIFY(isArchLinuxHost(archRelease, missingOsRelease));
}

void TestUtils::testIsArchLinuxHost_IdArch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missingArchRelease = dir.filePath("no-such-arch-release");
    const QString osRelease = writeFixture(dir, "os-release", "ID=arch\nID_LIKE=\n");

    QVERIFY(isArchLinuxHost(missingArchRelease, osRelease));
}

void TestUtils::testIsArchLinuxHost_IdLikeArch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missingArchRelease = dir.filePath("no-such-arch-release");
    const QString osRelease = writeFixture(dir, "os-release", "ID=manjaro\nID_LIKE=arch\n");

    QVERIFY(isArchLinuxHost(missingArchRelease, osRelease));
}

void TestUtils::testIsArchLinuxHost_Debian()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missingArchRelease = dir.filePath("no-such-arch-release");
    const QString osRelease = writeFixture(dir, "os-release", "ID=debian\nID_LIKE=\n");

    QVERIFY(!isArchLinuxHost(missingArchRelease, osRelease));
}

void TestUtils::testIsArchLinuxHost_MissingOsRelease()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missingArchRelease = dir.filePath("no-such-arch-release");
    const QString missingOsRelease = dir.filePath("no-such-os-release");

    QVERIFY(!isArchLinuxHost(missingArchRelease, missingOsRelease));
}

void TestUtils::testIsArchLinuxHost_DebianWithPacmanInstalled()
{
    // Detection must be driven purely by /etc/arch-release and os-release
    // ID/ID_LIKE, never by which package manager binaries happen to be
    // installed -- a Debian system with pacman available must still report
    // Debian so the GUI and helper keep using apt.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missingArchRelease = dir.filePath("no-such-arch-release");
    const QString osRelease = writeFixture(dir, "os-release", "ID=debian\nID_LIKE=\n");

    // Whether pacman happens to be installed on the machine running this
    // test is irrelevant: isArchLinuxHost() never inspects PATH/binaries.
    QVERIFY(!isArchLinuxHost(missingArchRelease, osRelease));
}

void TestUtils::testValidPackageName_data()
{
    QTest::addColumn<QString>("package");
    QTest::addColumn<bool>("expected");

    QTest::newRow("simple")          << QStringLiteral("bash") << true;
    QTest::newRow("kernel image")    << QStringLiteral("linux-image-6.1.0-13-amd64") << true;
    QTest::newRow("digits+dots")     << QStringLiteral("lib32z1") << true;
    QTest::newRow("plus sign")       << QStringLiteral("g++-12") << true;
    QTest::newRow("empty")          << QString() << false;
    QTest::newRow("leading dash")    << QStringLiteral("-bash") << false;
    QTest::newRow("space")          << QStringLiteral("bash extra") << false;
    QTest::newRow("semicolon")       << QStringLiteral("bash;rm") << false;
    QTest::newRow("shell metachar") << QStringLiteral("$(bash)") << false;
}

void TestUtils::testValidPackageName()
{
    QFETCH(QString, package);
    QFETCH(bool, expected);

    QCOMPARE(validPackageName(package), expected);
}

void TestUtils::testParseDays_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("expectedOk");
    QTest::addColumn<int>("expectedDays");

    QTest::newRow("zero")           << QStringLiteral("0") << true << 0;
    QTest::newRow("typical")        << QStringLiteral("30") << true << 30;
    QTest::newRow("max allowed")    << QStringLiteral("36500") << true << 36500;
    QTest::newRow("over max")       << QStringLiteral("36501") << false << 0;
    QTest::newRow("negative")       << QStringLiteral("-1") << false << 0;
    QTest::newRow("not a number")   << QStringLiteral("abc") << false << 0;
    QTest::newRow("empty")         << QString() << false << 0;
}

void TestUtils::testParseDays()
{
    QFETCH(QString, value);
    QFETCH(bool, expectedOk);
    QFETCH(int, expectedDays);

    int days = -1;
    QCOMPARE(parseDays(value, &days), expectedOk);
    if (expectedOk) {
        QCOMPARE(days, expectedDays);
    }
}

void TestUtils::testValidPeriod_data()
{
    QTest::addColumn<QString>("period");
    QTest::addColumn<bool>("expected");

    QTest::newRow("daily")    << QStringLiteral("daily") << true;
    QTest::newRow("weekly")   << QStringLiteral("weekly") << true;
    QTest::newRow("monthly")  << QStringLiteral("monthly") << true;
    QTest::newRow("reboot")   << QStringLiteral("@reboot") << true;
    QTest::newRow("yearly")   << QStringLiteral("yearly") << false;
    QTest::newRow("empty")   << QString() << false;
}

void TestUtils::testValidPeriod()
{
    QFETCH(QString, period);
    QFETCH(bool, expected);

    QCOMPARE(validPeriod(period), expected);
}

void TestUtils::testCronEntryBase()
{
    QCOMPARE(cronEntryBase("daily"), QStringLiteral("/etc/cron.daily/mx-cleanup"));
    QCOMPARE(cronEntryBase("weekly"), QStringLiteral("/etc/cron.weekly/mx-cleanup"));
    QCOMPARE(cronEntryBase("@reboot"), QStringLiteral("/etc/cron.d/mx-cleanup"));
}

void TestUtils::testLookupUser_Valid()
{
    // root always exists and looking it up needs no privilege of our own.
    UserInfo info;
    QVERIFY(lookupUser(QStringLiteral("root"), &info));
    QCOMPARE(info.uid, static_cast<uid_t>(0));
}

void TestUtils::testLookupUser_InvalidChars()
{
    QVERIFY(!lookupUser(QStringLiteral("../etc/passwd")));
    QVERIFY(!lookupUser(QStringLiteral("user name")));
    QVERIFY(!lookupUser(QStringLiteral("$(whoami)")));
}

void TestUtils::testLookupUser_Unknown()
{
    QVERIFY(!lookupUser(QStringLiteral("no-such-user-zzz-1234")));
}

void TestUtils::testLookupUser_Empty()
{
    QVERIFY(!lookupUser(QString()));
}

void TestUtils::testValidUserNameSyntax_data()
{
    QTest::addColumn<QString>("user");
    QTest::addColumn<bool>("expected");

    QTest::newRow("ascii") << QStringLiteral("adrian") << true;
    QTest::newRow("cyrillic") << QStringLiteral("иван") << true;
    QTest::newRow("greek") << QStringLiteral("δοκιμή") << true;
    QTest::newRow("arabic") << QStringLiteral("مستخدم") << true;
    QTest::newRow("cjk") << QStringLiteral("使用者") << true;
    QTest::newRow("accented") << QStringLiteral("josé") << true;
    QTest::newRow("decomposed") << QString::fromUtf8("Jose\xCC\x81") << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("leading dash") << QStringLiteral("-user") << false;
    QTest::newRow("space") << QStringLiteral("user name") << false;
    QTest::newRow("slash") << QStringLiteral("user/name") << false;
    QTest::newRow("shell metachar") << QStringLiteral("$(whoami)") << false;
}

void TestUtils::testValidUserNameSyntax()
{
    QFETCH(QString, user);
    QFETCH(bool, expected);
    QCOMPARE(validUserNameSyntax(user), expected);
}

void TestUtils::testUserScheduleFileId()
{
    QCOMPARE(userScheduleFileId(QStringLiteral("adrian")), QStringLiteral("adrian"));

    const QString cyrillicId = userScheduleFileId(QStringLiteral("иван"));
    QCOMPARE(QString::fromLocal8Bit(encodedUserName(QStringLiteral("иван"))), QStringLiteral("иван"));
    QVERIFY(cyrillicId.startsWith(QStringLiteral("u-")));
    QCOMPARE(cyrillicId, userScheduleFileId(QStringLiteral("иван")));
    QVERIFY(QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$")).match(cyrillicId).hasMatch());
    QVERIFY(userScheduleFileId(QString()).isEmpty());
}

void TestUtils::testHomeDirForUser_Valid()
{
    const struct passwd *pwd = getpwnam("root");
    QVERIFY(pwd != nullptr);
    QCOMPARE(homeDirForUser(QStringLiteral("root")), QString::fromLocal8Bit(pwd->pw_dir));
}

void TestUtils::testHomeDirForUser_Unknown()
{
    QVERIFY(homeDirForUser(QStringLiteral("no-such-user-zzz-1234")).isEmpty());
}

void TestUtils::testParseScheduleOptions_MissingUserForPerUserOption()
{
    ScheduleOptions opts;
    QVERIFY(!parseScheduleOptions({"--cache", "5"}, &opts));
}

void TestUtils::testParseScheduleOptions_UnknownUser()
{
    ScheduleOptions opts;
    QVERIFY(!parseScheduleOptions({"--user", "no-such-user-zzz-1234", "--cache", "5"}, &opts));
}

void TestUtils::testParseScheduleOptions_InvalidLogsMode()
{
    ScheduleOptions opts;
    QVERIFY(!parseScheduleOptions({"--logs", "ancient", "5"}, &opts));
}

void TestUtils::testParseScheduleOptions_InvalidAptMode()
{
    ScheduleOptions opts;
    QVERIFY(!parseScheduleOptions({"--apt", "thorough"}, &opts));
}

void TestUtils::testParseScheduleOptions_InvalidTrashMode()
{
    ScheduleOptions opts;
    QVERIFY(!parseScheduleOptions({"--trash", "everyone", "5"}, &opts));
}

void TestUtils::testParseScheduleOptions_UnknownOption()
{
    ScheduleOptions opts;
    QVERIFY(!parseScheduleOptions({"--bogus"}, &opts));
}

void TestUtils::testParseScheduleOptions_ValidCombination()
{
    ScheduleOptions opts;
    QVERIFY(parseScheduleOptions(
        {"--user", "root", "--cache", "5", "--thumbs", "0", "--logs", "old", "7", "--apt", "auto", "--purge",
         "--trash", "user", "30", "--flatpak"},
        &opts));
    QCOMPARE(opts.user, QStringLiteral("root"));
    QCOMPARE(opts.cacheDays, 5);
    QCOMPARE(opts.thumbsDays, 0);
    QCOMPARE(opts.logsMode, QStringLiteral("old"));
    QCOMPARE(opts.logsDays, 7);
    QCOMPARE(opts.aptMode, QStringLiteral("auto"));
    QVERIFY(opts.purge);
    QCOMPARE(opts.trashMode, QStringLiteral("user"));
    QCOMPARE(opts.trashDays, 30);
    QVERIFY(opts.flatpak);
}

void TestUtils::testGenerateUserScript_Cache()
{
    ScheduleOptions opts;
    opts.user = QStringLiteral("alice");
    opts.cacheDays = 5;

    const QString script = generateUserScript(opts);
    QVERIFY(script.startsWith("#!/bin/sh\n"));
    QVERIFY(script.contains("find '/home/alice/.cache' -mindepth 1"));
    QVERIFY(script.contains("-atime +5 -mtime +5"));
    QVERIFY(script.contains("-delete"));
}

void TestUtils::testGenerateUserScript_NoAgeFilter()
{
    ScheduleOptions opts;
    opts.user = QStringLiteral("alice");
    opts.cacheDays = 0;

    const QString script = generateUserScript(opts);
    QVERIFY(script.contains("find '/home/alice/.cache' -mindepth 1"));
    QVERIFY(!script.contains("-atime"));
}

void TestUtils::testGenerateUserScript_CallsSystemScript()
{
    ScheduleOptions opts;
    opts.user = QStringLiteral("alice");
    opts.cacheDays = 5;

    const QString script = generateUserScript(opts);
    QVERIFY(script.contains(systemScriptPath()));
    // apt/purge/logs/trash-all must never be embedded directly in the
    // per-user script -- they belong solely in the shared system script.
    QVERIFY(!script.contains("apt-get"));
    QVERIFY(!script.contains("/var/log"));
}

void TestUtils::testGenerateUserScript_UnicodeUser()
{
    ScheduleOptions opts;
    opts.user = QStringLiteral("иван");
    opts.cacheDays = 5;
    opts.thumbsDays = 2;
    opts.trashMode = QStringLiteral("user");
    opts.trashDays = 3;
    opts.flatpak = true;

    const QString script = generateUserScript(opts);
    QVERIFY(script.contains(QStringLiteral("'/home/иван/.cache'")));
    QVERIFY(script.contains(QStringLiteral("'/home/иван/.cache/thumbnails'")));
    QVERIFY(script.contains(QStringLiteral("'/home/иван/.local/share/Trash'")));
    QVERIFY(script.contains(QStringLiteral("runuser -u 'иван'")));

    QProcess shell;
    shell.start(QStringLiteral("/bin/sh"), {QStringLiteral("-n")});
    QVERIFY(shell.waitForStarted());
    shell.write(script.toLocal8Bit());
    shell.closeWriteChannel();
    QVERIFY(shell.waitForFinished());
    QCOMPARE(shell.exitCode(), 0);
}

// The GUI re-reads the generated script on startup to restore checkbox and
// spinbox state; parsing must keep up with generateUserScript()'s quoting.
void TestUtils::testScriptOptions_RoundTrip()
{
    ScheduleOptions opts;
    opts.user = QStringLiteral("иван");
    opts.cacheDays = 5;
    opts.thumbsDays = 2;
    opts.trashMode = QStringLiteral("user");
    opts.trashDays = 3;

    const QString script = generateUserScript(opts);
    QVERIFY(MainWindow::scriptCleansCache(script));
    QVERIFY(MainWindow::scriptCleansThumbnails(script));
    QCOMPARE(MainWindow::scriptCacheAgeDays(script), 5);
    QCOMPARE(MainWindow::scriptTrashAgeDays(script), 3);

    ScheduleOptions allCache;
    allCache.user = QStringLiteral("alice");
    allCache.cacheDays = 0;
    allCache.trashMode = QStringLiteral("user");
    allCache.trashDays = 0;

    const QString allCacheScript = generateUserScript(allCache);
    QVERIFY(MainWindow::scriptCleansCache(allCacheScript));
    QVERIFY(!MainWindow::scriptCleansThumbnails(allCacheScript));
    QCOMPARE(MainWindow::scriptCacheAgeDays(allCacheScript), -1);
    QCOMPARE(MainWindow::scriptTrashAgeDays(allCacheScript), 0);
}

// Scripts written by releases that predate shell quoting must keep parsing.
void TestUtils::testScriptOptions_LegacyUnquoted()
{
    const QString script = QStringLiteral(
        "#!/bin/sh\n"
        "find /home/alice/.cache -mindepth 1 ! -path '/home/alice/.cache/thumbnails*' -atime +7 -mtime +7 -type f "
        "-delete 2>/dev/null\n"
        "find /home/alice/.cache/thumbnails -type f -delete 2>/dev/null\n"
        "find /home/alice/.local/share/Trash -mindepth 1 -ctime +14 -atime +14 -delete\n");

    QVERIFY(MainWindow::scriptCleansCache(script));
    QVERIFY(MainWindow::scriptCleansThumbnails(script));
    QCOMPARE(MainWindow::scriptCacheAgeDays(script), 7);
    QCOMPARE(MainWindow::scriptTrashAgeDays(script), 14);
}

void TestUtils::testGenerateSystemScript_AllLogsUsesTruncate()
{
    ScheduleOptions opts;
    opts.logsMode = QStringLiteral("all");
    opts.logsDays = 7;

    const QString script = generateSystemScript(opts);
    QVERIFY(script.contains("find /var/log -type f -ctime +7 -atime +7 -exec truncate -s 0 {} +"));
    QVERIFY(!script.contains("sh -c"));
}

void TestUtils::testGenerateSystemScript_Purge()
{
    ScheduleOptions opts;
    opts.purge = true;

    const QString script = generateSystemScript(opts);
    QVERIFY(script.contains("dpkg -l | awk '/^rc/ { print $2 }' | xargs -r apt-get purge -y"));
}

void TestUtils::testGenerateSystemScript_TrashAll()
{
    ScheduleOptions opts;
    opts.trashMode = QStringLiteral("all");
    opts.trashDays = 30;

    const QString script = generateSystemScript(opts);
    QVERIFY(script.contains("find /home/*/.local/share/Trash -mindepth 1"));
    QVERIFY(script.contains("-ctime +30 -atime +30"));
}

namespace {
// A fresh QTemporaryDir used as a fake $HOME, so these filesystem tests
// never touch a real user's actual settings.
QString makeHomeDir(QTemporaryDir &dir)
{
    return dir.path();
}
}

void TestUtils::testOpenSettingsDirFd_NoSettingsYet()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());

    const int fd = openSettingsDirFd(makeHomeDir(home), false, getuid(), getgid());
    QCOMPARE(fd, -1);
}

void TestUtils::testOpenSettingsDirFd_CreatesWithCorrectOwnership()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());

    const int fd = openSettingsDirFd(makeHomeDir(home), true, getuid(), getgid());
    QVERIFY(fd >= 0);
    ::close(fd);

    const QString mxDir = home.filePath(".config/MX-Linux");
    QVERIFY(QFileInfo::exists(mxDir));
    const QFileInfo info(mxDir);
    QCOMPARE(info.ownerId(), getuid());
    QCOMPARE(info.groupId(), getgid());
}

void TestUtils::testOpenSettingsDirFd_RefusesConfigSymlink()
{
    QTemporaryDir home;
    QTemporaryDir elsewhere;
    QVERIFY(home.isValid());
    QVERIFY(elsewhere.isValid());

    QVERIFY(QFile::link(elsewhere.path(), home.filePath(".config")));

    const int fd = openSettingsDirFd(makeHomeDir(home), false, getuid(), getgid());
    QCOMPARE(fd, -1);
    // Must not have followed the symlink and created anything under it.
    QVERIFY(!QFileInfo::exists(elsewhere.filePath("MX-Linux")));
}

void TestUtils::testOpenSettingsDirFd_RefusesMxLinuxSymlink()
{
    QTemporaryDir home;
    QTemporaryDir elsewhere;
    QVERIFY(home.isValid());
    QVERIFY(elsewhere.isValid());

    QVERIFY(QDir().mkpath(home.filePath(".config")));
    QVERIFY(QFile::link(elsewhere.path(), home.filePath(".config/MX-Linux")));

    const int fd = openSettingsDirFd(makeHomeDir(home), false, getuid(), getgid());
    QCOMPARE(fd, -1);
}

void TestUtils::testWriteFileAsRoot_WritesContentAndMode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("script.sh");

    QVERIFY(writeFileAsRoot(path, "#!/bin/sh\necho hi\n", 0755));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("#!/bin/sh\necho hi\n"));
    QCOMPARE(QFileInfo(path).permissions() & QFileDevice::ExeOwner, QFileDevice::ExeOwner);
}

void TestUtils::testWriteFileAsRoot_ReplacesExistingContentAtomically()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("mx-cleanup");

    QVERIFY(writeFileAsRoot(path, "first version, much longer than the second\n", 0644));
    QVERIFY(writeFileAsRoot(path, "second\n", 0644));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("second\n"));
}

void TestUtils::testWriteFileAsRoot_NoLeftoverTempFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("mx-cleanup");

    QVERIFY(writeFileAsRoot(path, "content\n", 0644));

    const QStringList entries = QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    QCOMPARE(entries, QStringList {"mx-cleanup"});
}

void TestUtils::testWriteFileAsRoot_FailsInMissingDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("no-such-subdir/mx-cleanup");

    QVERIFY(!writeFileAsRoot(path, "content\n", 0644));
}

void TestUtils::testStagedFile_NothingVisibleUntilCommit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("mx-cleanup");

    StagedFile staged;
    QVERIFY(stageFileAsRoot(path, "content\n", 0644, &staged));
    QVERIFY(!QFile::exists(path));

    QVERIFY(commitStagedFile(&staged));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("content\n"));
}

void TestUtils::testStagedFile_DiscardLeavesTargetAndDirUntouched()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("mx-cleanup");
    QVERIFY(writeFileAsRoot(path, "original\n", 0644));

    StagedFile staged;
    QVERIFY(stageFileAsRoot(path, "replacement\n", 0644, &staged));
    discardStagedFile(&staged);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("original\n"));
    const QStringList entries = QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    QCOMPARE(entries, QStringList {"mx-cleanup"});

    // Discarding again (or a never-staged handle) must be harmless.
    discardStagedFile(&staged);
    StagedFile neverStaged;
    discardStagedFile(&neverStaged);
}

// Regression test for the "all logs" shell-injection fix: actually run the
// generated cleanup command against files whose names contain shell syntax,
// and verify they are truncated without any of it ever being executed.
void TestUtils::testGenerateSystemScript_HostileLogFilenamesAreSafe()
{
    QTemporaryDir logDir;
    QVERIFY(logDir.isValid());

    const QStringList hostileNames {
        QStringLiteral("normal.log"),
        QStringLiteral("evil'; touch injected;'.log"),
        QStringLiteral("$(touch injected).log"),
        QStringLiteral("`touch injected`.log"),
    };
    for (const QString &name : hostileNames) {
        QFile file(logDir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("log content\n");
    }

    ScheduleOptions opts;
    opts.logsMode = QStringLiteral("all");
    opts.logsDays = 0;
    QString script = generateSystemScript(opts);
    script.replace(QLatin1String("/var/log"), logDir.path());

    QProcess sh;
    sh.setWorkingDirectory(logDir.path());
    sh.start("/bin/sh", {"-c", script});
    QVERIFY(sh.waitForFinished(10000));

    QVERIFY(!QFile::exists(logDir.filePath("injected")));
    for (const QString &name : hostileNames) {
        QCOMPARE(QFileInfo(logDir.filePath(name)).size(), qint64(0));
    }
}

// A failed schedule write must leave the previously working file untouched.
void TestUtils::testWriteFileAsRoot_FailedWritePreservesExistingTarget()
{
    if (getuid() == 0) {
        QSKIP("A read-only directory does not block root");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("mx-cleanup");
    QVERIFY(writeFileAsRoot(path, "original schedule\n", 0644));

    QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const bool wrote = writeFileAsRoot(path, "replacement\n", 0644);
    QVERIFY(QFile::setPermissions(dir.path(),
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QVERIFY(!wrote);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("original schedule\n"));
}

// The per-user script must stay confined to the submitting user's own files,
// and the shared system script must never be tied to any particular user.
void TestUtils::testGenerateScripts_ScopeIsolation()
{
    ScheduleOptions opts;
    opts.user = QStringLiteral("alice");
    opts.cacheDays = 3;
    opts.thumbsDays = 3;
    opts.flatpak = true;
    opts.aptMode = QStringLiteral("auto");
    opts.purge = true;
    opts.logsMode = QStringLiteral("all");
    opts.logsDays = 2;
    opts.trashMode = QStringLiteral("all");
    opts.trashDays = 5;

    const QString userScript = generateUserScript(opts);
    QVERIFY(!userScript.contains("/home/*"));
    QVERIFY(!userScript.contains("/var/log"));
    QVERIFY(!userScript.contains("apt-get"));
    QVERIFY(!userScript.contains("pacman"));

    const QString systemScript = generateSystemScript(opts);
    QVERIFY(!systemScript.contains("alice"));
}

// The UI treats a non-zero helper exit code as failure (item 3); verify the
// helper binary actually reports its argument-validation failures that way.
void TestUtils::testHelperBinary_ReportsFailureExitCodes()
{
    const QString helper = QCoreApplication::applicationDirPath() + "/helper";
    if (!QFile::exists(helper)) {
        QSKIP("helper binary not found next to the test binary");
    }

    auto helperFails = [&helper](const QStringList &args) {
        QProcess proc;
        proc.start(helper, args);
        if (!proc.waitForFinished(10000)) {
            return false;
        }
        return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() != 0;
    };

    QVERIFY(helperFails({}));
    QVERIFY(helperFails({"not-a-verb"}));
    QVERIFY(helperFails({"write-settings", "no.such.user.xyz"}));
    QVERIFY(helperFails({"write-schedule", "hourly"}));
    QVERIFY(helperFails({"write-schedule", "daily", "--cache", "5"})); // per-user option without --user
    QVERIFY(helperFails({"remove-schedule", "cron", "daily", "no.such.user.xyz"}));
    QVERIFY(helperFails({"write-system-script", "--cache", "5"})); // per-user option not allowed for this verb
    QVERIFY(helperFails({"write-system-script", "--user", "root", "--purge"})); // --user not allowed either
}

QTEST_MAIN(TestUtils)
#include "test_utils.moc"
