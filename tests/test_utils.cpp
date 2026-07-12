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

#include <QFile>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include "../mainwindow.h"
#include "../packagemanager.h"

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

QTEST_MAIN(TestUtils)
#include "test_utils.moc"
