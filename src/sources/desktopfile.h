// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DESKTOPFILE_H
#define DESKTOPFILE_H

#include <QString>
#include <QStringList>

namespace DesktopFileHelper {

// 在所有应用目录中查找文件名包含任一 keyword（忽略大小写）的 .desktop 文件，用户目录优先
QStringList locate(const QStringList &keywords);

// 解析 Exec 字段为「程序 + 参数」列表，去除 %u %U %f %F 等字段码；解析失败返回空列表
QStringList parseExec(const QString &desktopFilePath);

} // namespace DesktopFileHelper

#endif // DESKTOPFILE_H
