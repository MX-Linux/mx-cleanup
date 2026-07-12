/**********************************************************************
 *  packagemanager.cpp
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
#include "packagemanager.h"

#include <QFile>

bool isArchLinuxHost(const QString &archReleasePath, const QString &osReleasePath)
{
    if (QFile::exists(archReleasePath)) {
        return true;
    }

    QFile osRelease(osReleasePath);
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
