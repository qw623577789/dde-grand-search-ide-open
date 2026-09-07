// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ideprojectsearch.h"

#include <algorithm>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QSet>
#include <QSettings>
#include <QUrl>

Q_LOGGING_CATEGORY(logIdeProject, "org.deepin.grandsearch.ideproject")

static const char kProtocolVersion[] = "1.0";
static const int kMaxItems = 100; // daemon 端每组/全局最多解析 100 项

// 快捷命令配置文件（格式见 README「快捷命令配置」）
static QString shortcutConfigPath()
{
    return QDir::home().filePath(
        QStringLiteral(".config/deepin/dde-grand-search-ideproject/dde-grand-search-ideproject.conf"));
}

IdeProjectSearch::IdeProjectSearch(QObject *parent)
    : QObject(parent)
{
    qCInfo(logIdeProject) << "IDE project search service initialized";
}

void IdeProjectSearch::registerSource(ProjectSource *source)
{
    if (source && !m_sources.contains(source))
        m_sources.append(source);
}

QString IdeProjectSearch::search(const QString &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(logIdeProject) << "Search: JSON parse error:" << error.errorString();
        return QString();
    }

    const QJsonObject root = doc.object();
    const QString ver = root.value("ver").toString();
    const QString mID = root.value("mID").toString();
    QString keyword = root.value("cont").toString().trimmed();

    if (ver != kProtocolVersion || mID.isEmpty()) {
        qCWarning(logIdeProject) << "Search: invalid request, ver:" << ver << "mID:" << mID;
        return QString();
    }

    // 作用域前缀（如 "vsc" 列出全部 VS Code 历史，"vsc dde" 在其内筛选）。
    // 配置文件中定义的快捷命令优先，未命中时回退程序内置默认
    QString scopeId;
    QString rest;
    const QHash<QString, QString> shortcuts = loadShortcuts();
    for (auto it = shortcuts.constBegin(); it != shortcuts.constEnd(); ++it) {
        if (matchAliasPrefix(keyword, {it.key()}, &rest) && sourceById(it.value())) {
            scopeId = it.value();
            break;
        }
    }
    if (scopeId.isEmpty()) {
        for (ProjectSource *source : m_sources) {
            if (source->matchScopePrefix(keyword, &rest)) {
                scopeId = source->id();
                break;
            }
        }
    }
    // rest 仅在命中作用域前缀时有效；未命中时整个输入就是关键词
    if (!scopeId.isEmpty())
        keyword = rest;

    qCDebug(logIdeProject) << "Search request - mID:" << mID << "scope:" << scopeId << "keyword:" << keyword;

    if (keyword.isEmpty() && scopeId.isEmpty())
        return buildEmptyResult(mID);

    QList<ProjectItem> matched;
    // 同一路径可能被多个 IDE 记录：按来源各保留一条（分组与打开行为不同，
    // 点击分别用对应 IDE 打开），仅同一来源内的重复路径保留首次出现
    QHash<QString, QSet<QString>> seenBySource;
    const QList<ProjectItem> all = collectProjects();
    for (const ProjectItem &item : all) {
        if (!scopeId.isEmpty() && item.source != scopeId)
            continue;
        if (!keyword.isEmpty() && !matches(keyword, item))
            continue;
        QSet<QString> &seen = seenBySource[item.source];
        if (seen.contains(item.path))
            continue;
        seen.insert(item.path);
        matched.append(item);
    }

    // 按最近打开时间排序（lastUsed 为各来源的最佳可用近似值）；
    // 稳定排序保证同一时间戳（同一份配置文件）内保持 IDE 自身的 MRU 顺序
    std::stable_sort(matched.begin(), matched.end(),
                     [](const ProjectItem &a, const ProjectItem &b) {
                         return a.lastUsed > b.lastUsed;
                     });
    if (matched.size() > kMaxItems)
        matched = matched.mid(0, kMaxItems);

    appendPathsToNames(matched);

    // 记录 item id -> 项目信息，供 Action 接口打开时使用
    for (const ProjectItem &item : matched)
        m_itemMap.insert(item.id, item);

    qCInfo(logIdeProject) << "Search - mID:" << mID << "matched:" << matched.size();
    return buildResultJson(mID, matched);
}

bool IdeProjectSearch::stop(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    const QString mID = doc.object().value("mID").toString();

    qCDebug(logIdeProject) << "Stop search task:" << mID;

    // 搜索为同步执行，无可中断任务，直接返回成功
    Q_UNUSED(mID)
    return true;
}

bool IdeProjectSearch::action(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    const QJsonObject root = doc.object();
    const QString action = root.value("action").toString();
    QString itemId = root.value("item").toString();

    qCDebug(logIdeProject) << "Action:" << action << "item:" << itemId;

    if (action != "openitem" || itemId.isEmpty())
        return false;

    // UI 会原样回传 item 字段；优先按带序号前缀的完整 item 精确匹配——同一路径
    // 可能被多个 IDE 记录，须打开对应 IDE 的那条；未命中再剥前缀按裸路径回退
    ProjectItem item = m_itemMap.value(itemId);
    QString bareId = itemId;
    if (item.path.isEmpty()) {
        if (bareId.size() > 4 && bareId.at(3) == QLatin1Char('|')
            && bareId.at(0).isDigit() && bareId.at(1).isDigit() && bareId.at(2).isDigit()) {
            bareId = bareId.mid(4);
        }
        item = m_itemMap.value(bareId);
    }

    if (item.path.isEmpty()) {
        // 进程重启等场景下 item 不在缓存中，尝试重新扫描定位
        const QList<ProjectItem> all = collectProjects();
        for (const ProjectItem &candidate : all) {
            if (candidate.id == bareId) {
                item = candidate;
                break;
            }
        }
    }

    if (item.path.isEmpty()) {
        qCWarning(logIdeProject) << "Action: unknown item:" << itemId;
        return false;
    }

    if (ProjectSource *source = sourceById(item.source)) {
        qCInfo(logIdeProject) << "Opening project:" << item.path << "via" << item.source;
        return source->open(item);
    }

    // 来源未知时回退到文件管理器打开
    qCInfo(logIdeProject) << "Opening project via fallback:" << item.path;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(item.path));
}

QString IdeProjectSearch::buildResultJson(const QString &mID, const QList<ProjectItem> &items)
{
    QJsonObject root;
    root["ver"] = kProtocolVersion;
    root["mID"] = mID;

    // 按分组名聚合（item.group 为空时回退到来源默认组名），保持项目原有顺序
    QList<QPair<QString, QJsonArray>> groups;
    QHash<QString, int> groupIndex;
    int rank = 0;
    for (const ProjectItem &item : items) {
        QString groupName = item.group;
        if (groupName.isEmpty()) {
            if (ProjectSource *source = sourceById(item.source))
                groupName = source->groupName();
        }
        if (groupName.isEmpty())
            continue;

        int index = groupIndex.value(groupName, -1);
        if (index < 0) {
            index = groups.size();
            groupIndex.insert(groupName, index);
            groups.append({groupName, QJsonArray()});
        }

        QJsonObject jsonItem;
        // dde-grand-search UI 对无权重条目按 item 字符串排序（compareByWeight 兜底），
        // 会破坏插件给出的 MRU 顺序；加等宽序号前缀使 UI 的字符串序等于我们的排序。
        // 打开时 action() 会剥掉该前缀
        const QString prefixedId =
            QStringLiteral("%1|%2").arg(rank++, 3, 10, QLatin1Char('0')).arg(item.id);
        jsonItem["item"] = prefixedId;
        // 同路径条目（不同 IDE）各有唯一前缀，Action 按前缀定位到对应 IDE 的记录
        m_itemMap.insert(prefixedId, item);
        jsonItem["name"] = item.name;
        jsonItem["icon"] = item.icon;
        jsonItem["type"] = "ide/recent-project";
        groups[index].second.append(jsonItem);
    }

    QJsonArray contents;
    for (const auto &group : groups) {
        QJsonObject jsonGroup;
        jsonGroup["group"] = group.first;
        jsonGroup["items"] = group.second;
        contents.append(jsonGroup);
    }
    root["cont"] = contents;

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString IdeProjectSearch::buildEmptyResult(const QString &mID) const
{
    QJsonObject root;
    root["ver"] = kProtocolVersion;
    root["mID"] = mID;
    root["cont"] = QJsonArray();
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool IdeProjectSearch::matches(const QString &keyword, const ProjectItem &item) const
{
    if (item.name.contains(keyword, Qt::CaseInsensitive))
        return true;

    // 关键词较长时同时匹配完整路径（父目录名等）；避免过短关键词产生大量噪声结果
    if (keyword.size() >= 3 && item.path.contains(keyword, Qt::CaseInsensitive))
        return true;

    return false;
}

QList<ProjectItem> IdeProjectSearch::collectProjects() const
{
    QList<ProjectItem> all;

    for (ProjectSource *source : m_sources) {
        const QList<ProjectItem> items = source->projects();
        for (const ProjectItem &item : items) {
            if (item.id.isEmpty() || item.name.isEmpty() || item.path.isEmpty())
                continue;
            all.append(item);
        }
    }

    return all;
}

void IdeProjectSearch::appendPathsToNames(QList<ProjectItem> &items) const
{
    for (ProjectItem &item : items)
        item.name = tr("%1 (%2)").arg(item.name, item.path);
}

ProjectSource *IdeProjectSearch::sourceById(const QString &id) const
{
    for (ProjectSource *source : m_sources) {
        if (source->id() == id)
            return source;
    }
    return nullptr;
}

QHash<QString, QString> IdeProjectSearch::loadShortcuts() const
{
    QHash<QString, QString> shortcuts;

    QSettings settings(shortcutConfigPath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Shortcut"));
    const QStringList keys = settings.childKeys();
    for (const QString &key : keys) {
        const QString alias = key.trimmed();
        const QString scope = settings.value(key).toString().trimmed();
        if (!alias.isEmpty() && !scope.isEmpty())
            shortcuts.insert(alias, scope);
    }

    if (!shortcuts.isEmpty())
        qCDebug(logIdeProject) << "Configured shortcuts:" << shortcuts;
    return shortcuts;
}
