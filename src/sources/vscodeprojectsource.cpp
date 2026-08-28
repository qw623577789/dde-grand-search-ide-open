// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vscodeprojectsource.h"

#include "desktopfile.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QProcess>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUrl>

Q_LOGGING_CATEGORY(logVscode, "org.deepin.grandsearch.ideproject.vscode")

static const int kOpenedPathsLimit = 20;      // 每个变体最多返回的最近打开项目数
static const int kWorkspaceStorageLimit = 40; // 每个变体最多返回的工作区项目数

// 读取 state.vscdb 的 ItemTable 中 history.recentlyOpenedPathsList（JSON entries 数组）。
// 以只读方式打开，避免与正在运行的编辑器写锁冲突；读取失败返回空
static QJsonArray readStateDbHistory(const QString &dbPath)
{
    QJsonArray entries;
    static int connCounter = 0;
    const QString connName = QStringLiteral("ideproject-sqlite-%1").arg(++connCounter);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT value FROM ItemTable WHERE key = ?"));
            query.addBindValue(QStringLiteral("history.recentlyOpenedPathsList"));
            if (query.exec() && query.next()) {
                QJsonParseError error;
                const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toByteArray(), &error);
                if (error.error == QJsonParseError::NoError) {
                    const QJsonValue value = doc.object().value(QStringLiteral("entries"));
                    if (value.isArray())
                        entries = value.toArray();
                }
            }
        } else {
            qCWarning(logVscode) << "Failed to open state db" << dbPath << ":" << db.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return entries;
}

const QList<VscodeProjectSource::Variant> VscodeProjectSource::kVariants = {
    // sharedHistoryDb 仅 VS Code 1.118+ 确认使用共享存储；其余变体共享路径未知，留空回退 globalStorage
    { QStringLiteral("Code"),            QT_TRANSLATE_NOOP("VscodeProjectSource", "VS Code"),           QStringLiteral("code"),           QStringLiteral("code"),
      QStringLiteral(".vscode-shared/sharedStorage/state.vscdb") },
    { QStringLiteral("Code - Insiders"), QT_TRANSLATE_NOOP("VscodeProjectSource", "VS Code Insiders"),  QStringLiteral("code-insiders"),  QStringLiteral("code-insiders"),
      QString() },
    { QStringLiteral("Code - OSS"),      QT_TRANSLATE_NOOP("VscodeProjectSource", "VS Code OSS"),        QStringLiteral("code-oss"),       QStringLiteral("code-oss"),
      QString() },
    { QStringLiteral("VSCodium"),        QT_TRANSLATE_NOOP("VscodeProjectSource", "VSCodium"),           QStringLiteral("codium"),         QStringLiteral("vscodium"),
      QString() },
    { QStringLiteral("Cursor"),          QT_TRANSLATE_NOOP("VscodeProjectSource", "Cursor"),             QStringLiteral("cursor"),         QStringLiteral("cursor"),
      QString() },
};

VscodeProjectSource::VscodeProjectSource()
{
}

QString VscodeProjectSource::id() const
{
    return QStringLiteral("vscode");
}

QString VscodeProjectSource::groupName() const
{
    // 各变体使用独立分组，此值仅在 item.group 为空时兜底
    return tr("VS Code");
}

bool VscodeProjectSource::matchScopePrefix(const QString &text, QString *rest) const
{
    static const QStringList aliases = {
        QStringLiteral("vsc"),
        QStringLiteral("vscode"),
    };
    return matchAliasPrefix(text, aliases, rest);
}

QList<ProjectItem> VscodeProjectSource::projects() const
{
    const QString configRoot = QDir::home().filePath(QStringLiteral(".config"));
    if (!QDir(configRoot).exists())
        return {};

    QList<ProjectItem> result;
    for (const Variant &variant : kVariants)
        result.append(projectsForVariant(variant));

    return result;
}

bool VscodeProjectSource::open(const ProjectItem &item) const
{
    if (item.launcher.isEmpty())
        return ProjectSource::open(item);

    // 优先使用 PATH 中的启动命令
    const QString command = QStandardPaths::findExecutable(item.launcher);
    if (!command.isEmpty()) {
        qCInfo(logVscode) << "Launching" << command << item.path;
        return QProcess::startDetached(command, {item.path});
    }

    // 回退到 desktop 文件的 Exec 字段
    const QStringList files = DesktopFileHelper::locate({item.launcher});
    for (const QString &file : files) {
        QStringList exec = DesktopFileHelper::parseExec(file);
        if (exec.isEmpty())
            continue;

        const QString program = exec.takeFirst();
        exec.append(item.path);
        qCInfo(logVscode) << "Launching" << program << exec;
        if (QProcess::startDetached(program, exec))
            return true;
    }

    qCWarning(logVscode) << "No launcher found for" << item.launcher << ", fallback to file manager";
    return ProjectSource::open(item);
}

QList<ProjectItem> VscodeProjectSource::projectsForVariant(const Variant &variant) const
{
    const QString variantDir = QDir::home().filePath(
        QStringLiteral(".config/%1").arg(variant.configDir));
    const QDir userDir(variantDir + QStringLiteral("/User"));
    if (!userDir.exists())
        return {};

    QList<ProjectItem> result;

    // 历史列表优先从 state.vscdb 读取：VS Code 1.118+ 在应用共享存储（Code 变体），
    // 更早版本在编辑器自身 globalStorage；读不到时回退 storage.json
    QStringList historyDbs;
    if (!variant.sharedHistoryDb.isEmpty())
        historyDbs.append(QDir::home().filePath(variant.sharedHistoryDb));
    historyDbs.append(userDir.filePath(QStringLiteral("globalStorage/state.vscdb")));
    for (const QString &dbPath : historyDbs) {
        if (!QFileInfo::exists(dbPath))
            continue;
        const QJsonArray entries = readStateDbHistory(dbPath);
        if (entries.isEmpty())
            continue;
        const QList<ProjectItem> parsed = parseOpenedEntries(
            entries, variant, QFileInfo(dbPath).lastModified().toMSecsSinceEpoch());
        if (parsed.isEmpty())
            continue;
        result = parsed;
        qCDebug(logVscode) << variant.configDir << ": using state.vscdb history ("
                           << dbPath << ")," << result.size() << "items";
        break;
    }

    if (result.isEmpty()) {
        // 回退 storage.json：新版菜单数据优先，仅当新版无数据时才回退解析
        // 老版 openedPathsList，避免升级机器上旧格式的陈旧条目混入
        const QString storageJson = userDir.filePath(QStringLiteral("globalStorage/storage.json"));
        result.append(parseMenubarRecent(storageJson, variant));
        if (result.isEmpty()) {
            result.append(parseOpenedPaths(storageJson, variant));
            if (!result.isEmpty())
                qCDebug(logVscode) << variant.configDir << ": using legacy openedPathsList";
        } else {
            qCDebug(logVscode) << variant.configDir << ": using menubar recent list,"
                               << result.size() << "items";
        }
    }
    if (result.size() > kOpenedPathsLimit)
        result = result.mid(0, kOpenedPathsLimit);

    QSet<QString> seen;
    for (const ProjectItem &item : result)
        seen.insert(item.path);

    // 工作区目录补齐（按修改时间倒序，近似最近使用）
    const QList<ProjectItem> workspaces = parseWorkspaceStorage(variantDir, variant);
    for (const ProjectItem &item : workspaces) {
        if (result.size() >= kOpenedPathsLimit + kWorkspaceStorageLimit)
            break;
        if (seen.contains(item.path))
            continue;
        seen.insert(item.path);
        result.append(item);
    }

    return result;
}

QList<ProjectItem> VscodeProjectSource::parseMenubarRecent(const QString &storageJsonPath, const Variant &variant) const
{
    QFile file(storageJsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
        return {};

    // 新版 VS Code 将最近打开列表存放在菜单数据 File -> Open Recent 子菜单中
    const QJsonObject menus = doc.object()
        .value(QStringLiteral("lastKnownMenubarData")).toObject()
        .value(QStringLiteral("menus")).toObject();

    QJsonArray entries;
    for (auto it = menus.begin(); it != menus.end(); ++it) {
        const QJsonArray items = it.value().toObject().value(QStringLiteral("items")).toArray();
        for (const QJsonValue &itemValue : items) {
            const QJsonObject itemObj = itemValue.toObject();
            if (!itemObj.value(QStringLiteral("id")).toString().contains(QStringLiteral("RecentMenu")))
                continue;
            entries = itemObj.value(QStringLiteral("submenu")).toObject()
                .value(QStringLiteral("items")).toArray();
        }
    }

    // storage.json 每次会话结束时写入，其修改时间近似整个最近列表的打开时间
    const qint64 storageMtime = QFileInfo(storageJsonPath).lastModified().toMSecsSinceEpoch();

    QList<ProjectItem> result;
    for (const QJsonValue &value : entries) {
        if (!value.isObject())
            continue;

        const QJsonObject entry = value.toObject();
        const QString entryId = entry.value(QStringLiteral("id")).toString();
        const bool isFolder = entryId == QLatin1String("openRecentFolder");
        const bool isWorkspace = entryId == QLatin1String("openRecentWorkspace");
        if (!isFolder && !isWorkspace)
            continue;

        const QJsonObject uri = entry.value(QStringLiteral("uri")).toObject();
        QString path = uri.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            path = QUrl(uri.value(QStringLiteral("external")).toString()).toLocalFile();
        if (path.isEmpty())
            path = entry.value(QStringLiteral("label")).toString();
        if (!path.startsWith(QLatin1Char('/')))
            continue;

        const QFileInfo info(path);
        if (!info.exists())
            continue;
        if (isFolder && !info.isDir())
            continue;
        if (isWorkspace && !info.isFile())
            continue;

        ProjectItem item;
        item.id = path;
        item.path = path;
        item.name = projectName(path, QString());
        item.icon = variant.icon;
        item.source = id();
        item.group = tr(variant.displayName);
        item.launcher = variant.command;
        item.lastUsed = storageMtime;
        result.append(item);
    }

    return result;
}

QList<ProjectItem> VscodeProjectSource::parseOpenedPaths(const QString &storageJsonPath, const Variant &variant) const
{
    QFile file(storageJsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(logVscode) << "Failed to parse" << storageJsonPath << ":" << error.errorString();
        return {};
    }

    // openedPathsList 在不同版本中可能是 {"entries": [...]} 或直接是数组
    QJsonArray entries;
    const QJsonValue opened = doc.object().value(QStringLiteral("openedPathsList"));
    if (opened.isObject())
        entries = opened.toObject().value(QStringLiteral("entries")).toArray();
    else if (opened.isArray())
        entries = opened.toArray();

    const qint64 storageMtime = QFileInfo(storageJsonPath).lastModified().toMSecsSinceEpoch();
    return parseOpenedEntries(entries, variant, storageMtime);
}

QList<ProjectItem> VscodeProjectSource::parseOpenedEntries(const QJsonArray &entries,
                                                           const Variant &variant,
                                                           qint64 lastUsed) const
{
    QList<ProjectItem> result;
    for (const QJsonValue &value : entries) {
        if (!value.isObject())
            continue;

        const QJsonObject entry = value.toObject();
        QString uri = entry.value(QStringLiteral("folderUri")).toString();
        if (uri.isEmpty())
            uri = entry.value(QStringLiteral("fileUri")).toString();
        if (uri.isEmpty())
            uri = entry.value(QStringLiteral("uri")).toString();

        const QUrl url(uri);
        if (url.scheme() != QLatin1String("file"))
            continue;

        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (!info.exists())
            continue;
        if (!info.isDir() && info.suffix() != QLatin1String("code-workspace"))
            continue;

        ProjectItem item;
        item.id = path;
        item.path = path;
        item.name = projectName(path, entry.value(QStringLiteral("label")).toString());
        item.icon = variant.icon;
        item.source = id();
        item.group = tr(variant.displayName);
        item.launcher = variant.command;
        item.lastUsed = lastUsed;
        result.append(item);
    }

    return result;
}

QList<ProjectItem> VscodeProjectSource::parseWorkspaceStorage(const QString &variantDir, const Variant &variant) const
{
    const QDir storageDir(variantDir + QStringLiteral("/User/workspaceStorage"));
    if (!storageDir.exists())
        return {};

    struct Candidate {
        QString path;
        qint64 mtime;
    };
    QList<Candidate> candidates;

    const QFileInfoList subDirs = storageDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &subDir : subDirs) {
        QFile workspaceFile(subDir.absoluteFilePath() + QStringLiteral("/workspace.json"));
        if (!workspaceFile.open(QIODevice::ReadOnly))
            continue;

        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(workspaceFile.readAll(), &error);
        if (error.error != QJsonParseError::NoError)
            continue;

        const QJsonObject root = doc.object();
        QString uri = root.value(QStringLiteral("folder")).toString();
        const bool isFolderKey = !uri.isEmpty();
        if (uri.isEmpty())
            uri = root.value(QStringLiteral("workspace")).toString();

        const QUrl url(uri);
        if (url.scheme() != QLatin1String("file"))
            continue;

        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (!info.exists())
            continue;
        // folder 键指向项目目录；workspace 键指向 .code-workspace 文件。
        // 自动保存的 untitled workspace 定义（Code/Workspaces/*/workspace.json）跳过
        if (isFolderKey) {
            if (!info.isDir())
                continue;
        } else if (!info.isFile() || info.suffix() != QLatin1String("code-workspace")) {
            continue;
        }

        // workspace.json 在首次打开时写入、mtime 常年不变；state.vscdb 每次会话
        // 都会更新，其 mtime 才是工作区最近打开时间的可靠近似
        QString recencyFile = subDir.absoluteFilePath() + QStringLiteral("/state.vscdb");
        if (!QFileInfo::exists(recencyFile))
            recencyFile = subDir.absoluteFilePath() + QStringLiteral("/workspace.json");
        candidates.append({path, QFileInfo(recencyFile).lastModified().toMSecsSinceEpoch()});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.mtime > b.mtime; });

    QList<ProjectItem> result;
    for (const Candidate &candidate : candidates) {
        ProjectItem item;
        item.id = candidate.path;
        item.path = candidate.path;
        item.name = projectName(candidate.path, QString());
        item.icon = variant.icon;
        item.source = id();
        item.group = tr(variant.displayName);
        item.launcher = variant.command;
        item.lastUsed = candidate.mtime;
        result.append(item);
    }

    return result;
}

QString VscodeProjectSource::projectName(const QString &path, const QString &label) const
{
    if (!label.trimmed().isEmpty())
        return label.trimmed();

    const QFileInfo info(path);
    if (info.suffix() == QLatin1String("code-workspace"))
        return info.completeBaseName();

    return info.fileName();
}
