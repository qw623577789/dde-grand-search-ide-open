// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktopfile.h"
#include "desktopfile_p.h"

#include <algorithm>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <unistd.h>

Q_LOGGING_CATEGORY(logDesktopFile, "org.deepin.grandsearch.ideproject.desktopfile")

// 打开动作只信任当前用户自己的文件（与项目历史数据同属用户数据）或
// 系统控制的文件（root 属主且用户不可写）；其他属主（如通过环境变量
// 注入的应用目录）一律排除。符号链接按目标判定。root 进程不受此约束。
bool DesktopFileHelper::isTrustedLocation(const QString &path)
{
    if (::geteuid() == 0)
        return true;

    const QFileInfo info(path);
    const uint owner = info.ownerId();
    if (owner == static_cast<uint>(::geteuid()))
        return true;
    return owner == 0 && !info.isWritable();
}

// Exec 程序校验：必须为绝对路径（裸命令名会经 PATH 查找，PATH 受用户控制，
// 不可信）；解析符号链接后须为存在、可执行的常规文件，且属主可信
bool DesktopFileHelper::isTrustedProgram(const QString &program)
{
    if (!QDir::isAbsolutePath(program))
        return false;

    const QString resolved = QFileInfo(program).canonicalFilePath();
    const QFileInfo info(resolved);
    if (resolved.isEmpty() || !info.isFile() || !info.isExecutable())
        return false;

    return isTrustedLocation(program) && isTrustedLocation(resolved);
}

// 文件名按组件（'.' '-' '_' 分隔）匹配关键字，关键字两侧须为组件边界，
// 避免 "code" 误中 codeblocks 等无关桌面文件；同时允许 "vs" 前缀
// （vscode / vscodium 等桌面入口名，边界为 pos==2 或前一字符是分隔符）
bool DesktopFileHelper::matchesName(const QString &base, const QString &keyword)
{
    const QString baseLower = base.toLower();
    const QString keywordLower = keyword.toLower();
    const auto isSep = [](QChar c) {
        return c == QLatin1Char('.') || c == QLatin1Char('-') || c == QLatin1Char('_');
    };

    int from = 0;
    while (true) {
        const int pos = baseLower.indexOf(keywordLower, from);
        if (pos < 0)
            return false;

        const bool vsPrefix = pos >= 2
            && baseLower.mid(pos - 2, 2) == QLatin1String("vs")
            && (pos == 2 || isSep(baseLower.at(pos - 3)));
        const bool beforeOk = pos == 0 || vsPrefix || isSep(baseLower.at(pos - 1));
        const int end = pos + keywordLower.size();
        const bool afterOk = end == baseLower.size() || isSep(baseLower.at(end));
        if (beforeOk && afterOk)
            return true;

        from = pos + 1;
    }
}

QStringList DesktopFileHelper::locate(const QStringList &keywords)
{
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    // snap / flatpak 系统安装的 desktop 文件目录不在标准位置中，手动补充
    dirs.append(QStringLiteral("/var/lib/snapd/desktop/applications"));
    dirs.append(QStringLiteral("/var/lib/flatpak/exports/share/applications"));

    QStringList found;
    for (const QString &dir : dirs) {
        const QDir d(dir);
        if (!d.exists())
            continue;

        // 来源校验：仅扫描当前用户或系统控制的目录，排除其他属主目录
        // （如被注入的 XDG_DATA_DIRS）
        if (!isTrustedLocation(dir)) {
            qCDebug(logDesktopFile) << "Ignoring untrusted applications dir:" << dir;
            continue;
        }

        const QFileInfoList files = d.entryInfoList({QStringLiteral("*.desktop")},
                                                    QDir::Files | QDir::Readable,
                                                    QDir::Name);
        for (const QFileInfo &info : files) {
            if (!isTrustedLocation(info.absoluteFilePath()))
                continue;

            const QString base = info.completeBaseName();
            for (const QString &keyword : keywords) {
                if (matchesName(base, keyword)) {
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
    if (!isTrustedLocation(desktopFilePath)) {
        qCWarning(logDesktopFile) << "Ignoring untrusted desktop file:" << desktopFilePath;
        return {};
    }

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
    if (args.isEmpty())
        return {};

    // 路径校验通过后仍以原 program 路径启动（snap 等依赖符号链接名作为 argv[0]）
    const QString program = args.first();
    if (!isTrustedProgram(program)) {
        qCWarning(logDesktopFile) << "Ignoring" << desktopFilePath
                                  << ": untrusted Exec program:" << program;
        return {};
    }

    qCDebug(logDesktopFile) << "Parsed Exec of" << desktopFilePath << ":" << args;
    return args;
}
