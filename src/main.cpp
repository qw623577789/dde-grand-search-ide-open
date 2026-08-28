// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ideprojectsearch.h"
#include "searchpluginadaptor.h"
#include "sources/jetbrainsprojectsource.h"
#include "sources/vscodeprojectsource.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QLibraryInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QTranslator>

#define DBUS_SERVICE_NAME   "org.deepin.grandsearch.ideproject"
#define DBUS_OBJECT_PATH    "/org/deepin/grandsearch/ideproject"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("ide-project-search-plugin");

    // 翻译文件 basename 与 applicationName 一致（ide-project-search-plugin_<locale>.qm），
    // 依次查找构建目录、安装目录和 Qt 自带翻译目录，找不到时回退源码文本
    QTranslator translator;
    const QLocale locale = QLocale::system();
    const QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
        QStringLiteral("/usr/share/ide-project-search-plugin/translations"),
        QLibraryInfo::path(QLibraryInfo::TranslationsPath),
    };
    bool translationLoaded = false;
    for (const QString &dir : searchPaths) {
        // locale.name() 形如 "zh_CN"，与 lrelease 输出的
        // ide-project-search-plugin_zh_CN.qm 命名一致（不用 QLocale 重载，
        // 其按 uiLanguages() 的连字符形式查找会失配）
        if (translator.load(QStringLiteral("ide-project-search-plugin_%1").arg(locale.name()), dir)) {
            app.installTranslator(&translator);
            translationLoaded = true;
            break;
        }
    }
    qInfo("IDE project search plugin locale: %s, translation loaded: %s",
          qPrintable(locale.name()), translationLoaded ? "yes" : "no");

    VscodeProjectSource vscodeSource;
    JetbrainsProjectSource jetbrainsSource;

    IdeProjectSearch searcher;
    searcher.registerSource(&vscodeSource);
    searcher.registerSource(&jetbrainsSource);

    SearchPluginAdaptor adaptor(&searcher);

    QDBusConnection connection = QDBusConnection::sessionBus();
    if (!connection.registerService(DBUS_SERVICE_NAME)) {
        qCritical("Failed to register DBus service: %s", DBUS_SERVICE_NAME);
        return 1;
    }

    if (!connection.registerObject(DBUS_OBJECT_PATH, &searcher)) {
        qCritical("Failed to register DBus object: %s", DBUS_OBJECT_PATH);
        return 1;
    }

    qInfo("IDE project search plugin started - Service: %s, Path: %s",
          DBUS_SERVICE_NAME, DBUS_OBJECT_PATH);

    return app.exec();
}
