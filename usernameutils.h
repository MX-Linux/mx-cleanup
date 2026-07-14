/**********************************************************************
 *  usernameutils.h
 **********************************************************************
 * Copyright (C) 2026 MX Authors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 **********************************************************************/
#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QString>

#include <pwd.h>

// Keep the GUI and privileged helper on the same definition of a safe login
// name. Unicode letters, combining marks, and digits support localized account
// names while excluding path separators, whitespace, and shell metacharacters.
inline bool validUserNameSyntax(const QString &user)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[\\p{L}\\p{N}][\\p{L}\\p{M}\\p{N}._-]*$"));
    return !user.isEmpty() && pattern.match(user).hasMatch();
}

// Account names are byte strings to NSS. Decode command output and encode NSS
// queries with the active locale so non-ASCII names round-trip on both UTF-8
// and legacy-locale systems. Refuse a lossy conversion.
inline QByteArray encodedUserName(const QString &user)
{
    const QByteArray encoded = user.toLocal8Bit();
    return QString::fromLocal8Bit(encoded) == user ? encoded : QByteArray();
}

inline const struct passwd *passwdForUser(const QString &user)
{
    const QByteArray encoded = encodedUserName(user);
    return encoded.isEmpty() ? nullptr : ::getpwnam(encoded.constData());
}

// Preserve existing schedule filenames for ASCII accounts. For localized
// names, use a stable ASCII identifier because cron/run-parts filenames are
// not reliably Unicode-aware across distributions.
inline QString userScheduleFileId(const QString &user)
{
    static const QRegularExpression asciiPattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    if (asciiPattern.match(user).hasMatch()) {
        return user;
    }
    if (user.isEmpty()) {
        return {};
    }
    const QByteArray digest = QCryptographicHash::hash(user.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    return QStringLiteral("u-") + QString::fromLatin1(digest);
}
