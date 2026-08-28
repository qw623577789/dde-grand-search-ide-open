// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef JETBRAINSPROJECTSOURCE_H
#define JETBRAINSPROJECTSOURCE_H

#include "projectsource.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

// JetBrains 系列历史项目：解析 ~/.config/JetBrains/<产品><版本>/options/recentProjects.xml
// （Rider 另含 recentSolutions.xml），以及 Android Studio 的
// ~/.config/Google/AndroidStudio<版本>/options/recentProjects.xml。
// 每个产品独立分组展示，打开时通过 desktop 文件或 JetBrains Toolbox 探测启动器。
class JetbrainsProjectSource : public ProjectSource
{
public:
    explicit JetbrainsProjectSource();

    QString id() const override;
    QString groupName() const override;
    QList<ProjectItem> projects() const override;
    bool matchScopePrefix(const QString &text, QString *rest) const override;
    bool open(const ProjectItem &item) const override;

private:
    struct Product {
        QString dirPrefix;      // 配置目录名前缀，如 "IntelliJIdea"
        const char *displayName; // 分组展示名（QT_TRANSLATE_NOOP 标记，使用处经 tr() 翻译）
        QStringList keywords;   // desktop 文件名匹配关键字，如 {"jetbrains-idea", "idea"}
        QStringList toolbox;    // Toolbox 目录匹配关键字，如 {"IDEA"}
        QString icon;
        QString key;            // 稳定标识，存入 item.launcher 用于打开时定位产品
    };

    static QList<ProjectItem> parseRecentXml(const QString &xmlPath,
                                             const Product &product,
                                             const QString &displaySuffix);

    static const QList<Product> kProducts;

    // Qt6 中该宏末尾带 private:，须放在类声明最后
    Q_DECLARE_TR_FUNCTIONS(JetbrainsProjectSource)
};

#endif // JETBRAINSPROJECTSOURCE_H
