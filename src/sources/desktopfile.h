// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DESKTOPFILE_H
#define DESKTOPFILE_H

#include <QString>
#include <QStringList>

namespace DesktopFileHelper {

// 在受信任（属主为当前用户或 root 且用户不可写；root 运行时为全部）的应用
// 目录中查找文件名按组件边界命中任一 keyword（忽略大小写，允许 vs 前缀如
// vscode/vscodium）的 .desktop 文件
QStringList locate(const QStringList &keywords);

// 解析 Exec 字段为「程序 + 参数」列表，去除 %u %U %f %F 等字段码；
// desktop 文件本身及 Exec 程序须通过可信校验（绝对路径、解析符号链接后
// 存在/可执行、属主为当前用户或 root），否则返回空列表
QStringList parseExec(const QString &desktopFilePath);

} // namespace DesktopFileHelper

#endif // DESKTOPFILE_H
