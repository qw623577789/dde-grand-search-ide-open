// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchpluginadaptor.h"

#include <QMetaObject>
#include <QString>

SearchPluginAdaptor::SearchPluginAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
    setAutoRelaySignals(true);
}

SearchPluginAdaptor::~SearchPluginAdaptor()
{
}

QString SearchPluginAdaptor::Search(const QString &json)
{
    QString out;
    QMetaObject::invokeMethod(parent(), "search",
                              Q_RETURN_ARG(QString, out),
                              Q_ARG(QString, json));
    return out;
}

bool SearchPluginAdaptor::Stop(const QString &json)
{
    bool out = false;
    QMetaObject::invokeMethod(parent(), "stop",
                              Q_RETURN_ARG(bool, out),
                              Q_ARG(QString, json));
    return out;
}

bool SearchPluginAdaptor::Action(const QString &json)
{
    bool out = false;
    QMetaObject::invokeMethod(parent(), "action",
                              Q_RETURN_ARG(bool, out),
                              Q_ARG(QString, json));
    return out;
}
