/**********************************************************************
 *  main.cpp
 **********************************************************************
 * Copyright (C) 2018 MX Authors
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

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QTranslator>

#include "mainwindow.h"
#include <unistd.h>
#include <version.h>

QString starting_home = qEnvironmentVariable("HOME");

int main(int argc, char *argv[])
{
    qputenv("XDG_RUNTIME_DIR", "/run/user/0");
    QApplication app(argc, argv);
    qputenv("HOME", "/root");
    app.setApplicationVersion(VERSION);
    app.setOrganizationName(QStringLiteral("MX-Linux"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Quick safe removal of old files"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    app.setWindowIcon(QIcon::fromTheme(app.applicationName()));

    QTranslator qtTran;
    if (qtTran.load(QLocale(), QStringLiteral("qt"), QStringLiteral("_"), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTran);

    QTranslator qtBaseTran;
    if (qtBaseTran.load("qtbase_" + QLocale().name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtBaseTran);

    QTranslator appTran;
    if (appTran.load(app.applicationName() + "_" + QLocale().name(), "/usr/share/" + app.applicationName() + "/locale"))
        app.installTranslator(&appTran);

    // root guard
    if (QProcess::execute(QStringLiteral("/bin/bash"), {"-c", "logname |grep -q ^root$"}) == 0) {
        QMessageBox::critical(nullptr, QObject::tr("Error"),
                              QObject::tr("You seem to be logged in as root, please log out and log in as normal user to use this program."));
        exit(EXIT_FAILURE);
    }

    if (getuid() == 0) {
        MainWindow w;
        w.show();
        return app.exec();
    } else {
        QProcess::startDetached(QStringLiteral("/usr/bin/mx-cleanup-launcher"), {});
    }
}
