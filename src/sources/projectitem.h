// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROJECTITEM_H
#define PROJECTITEM_H

#include <QString>

struct ProjectItem
{
    QString id;        // 项目唯一标识，直接使用项目绝对路径
    QString name;      // 项目名称（展示用）
    QString path;      // 项目绝对路径
    QString icon;      // 图标：主题图标名或图标文件路径
    QString source;    // 来源标识，如 "vscode" / "jetbrains"
    QString group;     // 分组名（展示用），如 "VS Code" / "PyCharm"
    QString launcher;  // 打开用的启动器标识：命令名（vscode 系列）或产品名（jetbrains 系列）
    qint64 lastUsed = 0; // 最近打开时间的近似值（毫秒时间戳，0 表示未知），用于结果排序
};

#endif // PROJECTITEM_H
