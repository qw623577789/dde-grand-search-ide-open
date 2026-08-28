// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktopfile.h"

#include <algorithm>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(logDesktopFile, "org.deepin.grandsearch.ideproject.desktopfile")

QStringList DesktopFileHelper::locate(const QStringList &keywords)
{
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    // snap 应用的 desktop 文件目录不在标准位置中，手动补充
    dirs.append(QStringLiteral("/var/lib/snapd/desktop/applications"));

    QStringList found;
    for (const QString &dir : dirs) {
        const QDir d(dir);
        if (!d.exists())
            continue;

        const QFileInfoList files = d.entryInfoList({QStringLiteral("*.desktop")},
                                                    QDir::Files | QDir::Readable,
                                                    QDir::Name);
        for (const QFileInfo &info : files) {
            const QString base = info.completeBaseName().toLower();
            for (const QString &keyword : keywords) {
                if (base.contains(keyword.toLower())) {
                    found.append(info.absoluteFilePath());
                    break;
                }
            }
        }
    }

    return found;
}

QStringList DesktopFileHelper::parseExec(const QString &desktopFilePath)
{
    QSettings settings(desktopFilePath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Desktop Entry"));

    const QString exec = settings.value(QStringLiteral("Exec")).toString().trimmed();
    if (exec.isEmpty())
        return {};

    QStringList args = QProcess::splitCommand(exec);
    if (args.isEmpty())
        return {};

    // 去除 %u %U %f %F %i %c %k 等字段码
    args.erase(std::remove_if(args.begin(), args.end(), [](const QString &arg) {
        return arg.startsWith('%');
    }), args.end());

    qCDebug(logDesktopFile) << "Parsed Exec of" << desktopFilePath << ":" << args;
    return args;
}
