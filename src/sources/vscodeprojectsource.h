// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VSCODEPROJECTSOURCE_H
#define VSCODEPROJECTSOURCE_H

#include "projectsource.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QString>
#include <QStringList>

// VS Code 系列历史项目：VS Code / Insiders / OSS / VSCodium / Cursor
// 历史列表优先读 state.vscdb（ItemTable 的 history.recentlyOpenedPathsList，
// VS Code 1.118+ 在 ~/.vscode-shared/sharedStorage/，旧版本在各应用自身的
// User/globalStorage/），读不到时回退 storage.json（Open Recent 菜单数据 /
// 老版 openedPathsList）；另以 User/workspaceStorage/*/workspace.json 补齐。
class VscodeProjectSource : public ProjectSource
{
public:
    explicit VscodeProjectSource();

    QString id() const override;
    QString groupName() const override;
    QList<ProjectItem> projects() const override;
    bool matchScopePrefix(const QString &text, QString *rest) const override;
    bool open(const ProjectItem &item) const override;

private:
    struct Variant {
        QString configDir;   // ~/.config 下的应用配置目录名
        const char *displayName; // 分组展示名（QT_TRANSLATE_NOOP 标记，使用处经 tr() 翻译）
        QString command;     // PATH 中查找的启动命令
        QString icon;
        QString sharedHistoryDb; // 应用共享存储的 state.vscdb 相对 home 路径（VS Code 1.118+），未知则留空
    };

    QList<ProjectItem> projectsForVariant(const Variant &variant) const;
    QList<ProjectItem> parseMenubarRecent(const QString &storageJsonPath, const Variant &variant) const;
    QList<ProjectItem> parseOpenedPaths(const QString &storageJsonPath, const Variant &variant) const;
    QList<ProjectItem> parseOpenedEntries(const QJsonArray &entries, const Variant &variant,
                                          qint64 lastUsed) const;
    QList<ProjectItem> parseWorkspaceStorage(const QString &variantDir, const Variant &variant) const;
    QString projectName(const QString &path, const QString &label) const;

    static const QList<Variant> kVariants;

    // Qt6 中该宏末尾带 private:，须放在类声明最后
    Q_DECLARE_TR_FUNCTIONS(VscodeProjectSource)
};

#endif // VSCODEPROJECTSOURCE_H
