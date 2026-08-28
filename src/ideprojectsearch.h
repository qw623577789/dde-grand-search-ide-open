// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IDEPROJECTSEARCH_H
#define IDEPROJECTSEARCH_H

#include "sources/projectitem.h"
#include "sources/projectsource.h"

#include <QHash>
#include <QList>
#include <QObject>

class IdeProjectSearch : public QObject
{
    Q_OBJECT
public:
    explicit IdeProjectSearch(QObject *parent = nullptr);

    void registerSource(ProjectSource *source);

public slots:
    QString search(const QString &json);
    bool stop(const QString &json);
    bool action(const QString &json);

private:
    QString buildResultJson(const QString &mID, const QList<ProjectItem> &items) const;
    QString buildEmptyResult(const QString &mID) const;
    bool matches(const QString &keyword, const ProjectItem &item) const;
    QList<ProjectItem> collectProjects() const;
    void appendPathsToNames(QList<ProjectItem> &items) const; // 项目名统一附加完整路径
    ProjectSource *sourceById(const QString &id) const;
    QHash<QString, QString> loadShortcuts() const; // 配置文件中定义的快捷命令（别名 -> 作用域）

    QList<ProjectSource *> m_sources;
    QHash<QString, ProjectItem> m_itemMap; // item id（项目路径） -> 项目信息
};

#endif // IDEPROJECTSEARCH_H
