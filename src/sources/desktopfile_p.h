// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DESKTOPFILE_P_H
#define DESKTOPFILE_P_H

#include <QString>

namespace DesktopFileHelper {

// 以下为 desktopfile.cpp 的内部校验函数，声明于此以便单元测试直接验证
bool isTrustedLocation(const QString &path);
bool isTrustedProgram(const QString &program);
bool matchesName(const QString &base, const QString &keyword);

} // namespace DesktopFileHelper

#endif // DESKTOPFILE_P_H
