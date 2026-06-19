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

#include <QObject>
#include <QString>
#include <QTest>
#include "../mainwindow.h"

class TestUtils : public QObject
{
    Q_OBJECT

private slots:
    void testSumKiB_data();
    void testSumKiB();
    void testSumKiB_EmptyInput();
    void testSumKiB_InvalidLines();
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

QTEST_MAIN(TestUtils)
#include "test_utils.moc"
