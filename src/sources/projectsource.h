// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROJECTSOURCE_H
#define PROJECTSOURCE_H

#include "projectitem.h"

#include <QDesktopServices>
#include <QList>
#include <QStringList>
#include <QUrl>

// 匹配 "vsc" / "jh" 形式的作用域前缀：首个空白分隔的 token 命中别名即匹配，
// rest 返回剩余关键词（无剩余时为空串）。区分「匹配但无剩余」与「不匹配」。
inline bool matchAliasPrefix(const QString &text, const QStringList &aliases, QString *rest)
{
    const int space = text.indexOf(QLatin1Char(' '));
    const QString first = text.left(space < 0 ? text.size() : space);

    for (const QString &alias : aliases) {
        if (first.compare(alias, Qt::CaseInsensitive) == 0) {
            if (rest)
                *rest = space < 0 ? QString() : text.mid(space + 1).trimmed();
            return true;
        }
    }
    return false;
}

// 历史项目数据源抽象接口。每个 IDE（VS Code、JetBrains、Qt Creator 等）实现一个子类：
// 负责从该 IDE 的配置文件中解析历史项目列表，并负责用该 IDE 打开项目。
class ProjectSource
{
public:
    virtual ~ProjectSource() = default;

    virtual QString id() const = 0;
    virtual QString groupName() const = 0;
    virtual QList<ProjectItem> projects() const = 0;

    // 作用域前缀匹配：如 "vsc" 列出全部 VS Code 历史，"vsc dde" 在其内筛选
    virtual bool matchScopePrefix(const QString &text, QString *rest) const
    {
        Q_UNUSED(text)
        Q_UNUSED(rest)
        return false;
    }

    // 默认回退：用文件管理器打开项目目录
    virtual bool open(const ProjectItem &item) const
    {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(item.path));
    }
};

#endif // PROJECTSOURCE_H
