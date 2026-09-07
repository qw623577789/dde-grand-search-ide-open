// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sources/jetbrainsprojectsource.h"
#include "sources/vscodeprojectsource.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

// 数据源解析测试：HOME 重定向到临时目录（每个用例独立子目录），构造
// VS Code storage.json / state.vscdb 与 JetBrains recentProjects.xml 夹具，
// 覆盖解析优先级、路径过滤、名称规则、时间排序、版本目录选择。
// 真实用户配置（~/.config 等）完全隔离，不受本机环境干扰。

namespace {

bool writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(content);
    f.close();
    return true;
}

QString folderEntry(const QString &path)
{
    return QUrl::fromLocalFile(path).toString();
}

// storage.json 新版菜单数据：File -> Open Recent 子菜单
QJsonObject menubarStorageJson(const QJsonArray &recentItems)
{
    QJsonObject recentMenu;
    recentMenu[QStringLiteral("id")] = QStringLiteral("submenuitem.MenubarRecentMenu");
    recentMenu[QStringLiteral("submenu")] = QJsonObject{{QStringLiteral("items"), recentItems}};

    QJsonObject fileMenu;
    fileMenu[QStringLiteral("items")] = QJsonArray{recentMenu};

    QJsonObject menus;
    menus[QStringLiteral("File")] = fileMenu;

    QJsonObject root;
    root[QStringLiteral("lastKnownMenubarData")] =
        QJsonObject{{QStringLiteral("menus"), menus}};
    return root;
}

QJsonObject folderRecentItem(const QString &path)
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = QStringLiteral("openRecentFolder");
    obj[QStringLiteral("uri")] = QJsonObject{{QStringLiteral("path"), path}};
    return obj;
}

QJsonObject workspaceRecentItem(const QString &path)
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = QStringLiteral("openRecentWorkspace");
    obj[QStringLiteral("uri")] =
        QJsonObject{{QStringLiteral("external"), QUrl::fromLocalFile(path).toString()}};
    return obj;
}

QByteArray jsonBytes(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// state.vscdb 夹具：ItemTable(key, value)，value 为 history.recentlyOpenedPathsList
// 的 {"entries": [...]} JSON（与 VS Code 1.118+ 共享存储格式一致）
bool writeStateDb(const QString &dbPath, const QJsonArray &entries)
{
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("ideproject-fixture"));
        db.setDatabaseName(dbPath);
        if (!db.open())
            return false;
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral("CREATE TABLE ItemTable (key TEXT PRIMARY KEY, value BLOB)")))
            return false;
        query.prepare(QStringLiteral("INSERT INTO ItemTable (key, value) VALUES (?, ?)"));
        query.addBindValue(QStringLiteral("history.recentlyOpenedPathsList"));
        query.addBindValue(QByteArray("{\"entries\":")
                           + QJsonDocument(entries).toJson(QJsonDocument::Compact)
                           + QByteArray("}"));
        if (!query.exec())
            return false;
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("ideproject-fixture"));
    return true;
}

// recentProjects.xml 2021+ 格式：additionalInfo map，逐 entry 带 projectOpenTimestamp
QByteArray jbRecentXml(const QList<QPair<QString, qint64>> &entries)
{
    QString xml = QStringLiteral(
        "<application><component name=\"RecentProjectsManager\">"
        "<option name=\"additionalInfo\"><map>");
    for (const auto &entry : entries) {
        xml += QStringLiteral(
                   "<entry key=\"%1\"><value><RecentProjectMetaInfo>"
                   "<option name=\"projectOpenTimestamp\" value=\"%2\"/>"
                   "</RecentProjectMetaInfo></value></entry>")
                   .arg(entry.first)
                   .arg(entry.second);
    }
    xml += QStringLiteral("</map></option></component></application>");
    return xml.toUtf8();
}

// 写一个带菜单历史的 storage.json 夹具，返回其中的项目路径
QString writeMenubarFixture(const QString &home)
{
    const QString proj = home + QStringLiteral("/proj/json");
    QDir().mkpath(proj);
    writeFile(home + QStringLiteral("/.config/Code/User/globalStorage/storage.json"),
              jsonBytes(menubarStorageJson(QJsonArray{folderRecentItem(proj)})));
    return proj;
}

// 2020.2 老格式：recentPaths 列表，lastProjectLocation 是新建项目默认上级目录
QByteArray jbLegacyXml(const QStringList &paths, const QString &lastLocation)
{
    QString xml = QStringLiteral(
        "<application><component name=\"RecentProjectsManager\">"
        "<option name=\"recentPaths\"><list>");
    for (const QString &path : paths)
        xml += QStringLiteral("<option value=\"%1\"/>").arg(path);
    xml += QStringLiteral("</list></option>"
                          "<option name=\"lastProjectLocation\" value=\"%1\"/>"
                          "<option name=\"lastProjectCreationLocation\" value=\"%1\"/>"
                          "</component></application>")
               .arg(lastLocation);
    return xml.toUtf8();
}

} // namespace

class TestDataSources : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void emptyHome();

    void vscodeMenubarRecent();
    void vscodeLegacyOpenedPaths();
    void vscodeStateDbPriority();
    void vscodeStateDbFallbacks();
    void vscodeWorkspaceSupplement();
    void vscodeVariants();

    void jetbrainsAdditionalInfo();
    void jetbrainsLegacyFormat();
    void jetbrainsVersionSelection();
    void jetbrainsAndroidStudio();

private:
    QString useHome(const QString &name);

    QTemporaryDir m_home;
};

void TestDataSources::initTestCase()
{
    QVERIFY(m_home.isValid());
}

QString TestDataSources::useHome(const QString &name)
{
    const QString dir = m_home.filePath(name);
    QDir().mkpath(dir);
    qputenv("HOME", dir.toUtf8());
    return dir;
}

void TestDataSources::emptyHome()
{
    useHome(QStringLiteral("empty"));
    VscodeProjectSource vscode;
    JetbrainsProjectSource jetbrains;
    QVERIFY(vscode.projects().isEmpty());
    QVERIFY(jetbrains.projects().isEmpty());
}

void TestDataSources::vscodeMenubarRecent()
{
    const QString home = useHome(QStringLiteral("menubar"));
    const QString folderPath = home + QStringLiteral("/proj/alpha");
    QVERIFY(QDir().mkpath(folderPath));
    const QString wsPath = home + QStringLiteral("/ws/demo.code-workspace");
    QVERIFY(writeFile(wsPath, "{}"));

    QJsonArray recent;
    recent.append(folderRecentItem(folderPath));
    recent.append(workspaceRecentItem(wsPath));
    recent.append(folderRecentItem(home + QStringLiteral("/proj/gone"))); // 不存在 → 过滤

    QVERIFY(writeFile(home + QStringLiteral("/.config/Code/User/globalStorage/storage.json"),
                      jsonBytes(menubarStorageJson(recent))));

    VscodeProjectSource source;
    const QList<ProjectItem> items = source.projects();
    QCOMPARE(items.size(), 2);

    // 保持菜单内的顺序；字段完整
    const ProjectItem &folder = items.at(0);
    QCOMPARE(folder.id, folderPath);
    QCOMPARE(folder.path, folderPath);
    QCOMPARE(folder.name, QStringLiteral("alpha"));
    QCOMPARE(folder.source, QStringLiteral("vscode"));
    QCOMPARE(folder.group, QStringLiteral("VS Code"));
    QCOMPARE(folder.launcher, QStringLiteral("code"));

    // workspace 条目：名称取 .code-workspace 的完整基名
    const ProjectItem &ws = items.at(1);
    QCOMPARE(ws.path, wsPath);
    QCOMPARE(ws.name, QStringLiteral("demo"));
}

void TestDataSources::vscodeLegacyOpenedPaths()
{
    const QString home = useHome(QStringLiteral("legacy"));
    const QString folderPath = home + QStringLiteral("/proj/beta");
    QVERIFY(QDir().mkpath(folderPath));
    const QString wsPath = home + QStringLiteral("/ws/x.code-workspace");
    QVERIFY(writeFile(wsPath, "{}"));

    QJsonArray entries;
    QJsonObject labeled;
    labeled[QStringLiteral("folderUri")] = folderEntry(folderPath);
    labeled[QStringLiteral("label")] = QStringLiteral("Custom Label");
    entries.append(labeled);
    QJsonObject workspace;
    workspace[QStringLiteral("folderUri")] = folderEntry(wsPath);
    entries.append(workspace);
    QJsonObject gone;
    gone[QStringLiteral("folderUri")] = folderEntry(home + QStringLiteral("/proj/gone"));
    entries.append(gone);

    QJsonObject opened;
    opened[QStringLiteral("entries")] = entries;
    QJsonObject root;
    root[QStringLiteral("openedPathsList")] = opened;
    QVERIFY(writeFile(home + QStringLiteral("/.config/Code/User/globalStorage/storage.json"),
                      jsonBytes(root)));

    VscodeProjectSource source;
    const QList<ProjectItem> items = source.projects();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).path, folderPath);
    QCOMPARE(items.at(0).name, QStringLiteral("Custom Label")); // label 优先作为名称
    QCOMPARE(items.at(1).path, wsPath);
    QCOMPARE(items.at(1).name, QStringLiteral("x"));
}

void TestDataSources::vscodeStateDbPriority()
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
        QSKIP("QSQLITE driver not available");

    const QString home = useHome(QStringLiteral("statedb"));
    const QString dbProj = home + QStringLiteral("/proj/db");
    QVERIFY(QDir().mkpath(dbProj));
    QVERIFY(writeStateDb(home + QStringLiteral("/.vscode-shared/sharedStorage/state.vscdb"),
                         QJsonArray{QJsonObject{{QStringLiteral("folderUri"),
                                                 folderEntry(dbProj)}}}));

    // storage.json 有另一项目：state.vscdb 命中后不应再读取
    const QString jsonProj = home + QStringLiteral("/proj/json");
    QVERIFY(QDir().mkpath(jsonProj));
    QVERIFY(writeFile(home + QStringLiteral("/.config/Code/User/globalStorage/storage.json"),
                      jsonBytes(menubarStorageJson(QJsonArray{folderRecentItem(jsonProj)}))));

    VscodeProjectSource source;
    const QList<ProjectItem> items = source.projects();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.at(0).path, dbProj);
}

void TestDataSources::vscodeStateDbFallbacks()
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
        QSKIP("QSQLITE driver not available");

    // 历史库损坏（非 SQLite 内容）→ 回退 storage.json
    {
        const QString home = useHome(QStringLiteral("db-corrupt"));
        QVERIFY(writeFile(home + QStringLiteral("/.vscode-shared/sharedStorage/state.vscdb"),
                          "not a sqlite database"));
        const QString proj = writeMenubarFixture(home);

        VscodeProjectSource source;
        const QList<ProjectItem> items = source.projects();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).path, proj);
    }

    // 有效库但无 history 键（如从未打开过项目）→ 回退 storage.json
    {
        const QString home = useHome(QStringLiteral("db-nohistory"));
        const QString dbPath = home + QStringLiteral("/.vscode-shared/sharedStorage/state.vscdb");
        QDir().mkpath(QFileInfo(dbPath).absolutePath());
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                        QStringLiteral("ideproject-empty"));
            db.setDatabaseName(dbPath);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE ItemTable (key TEXT PRIMARY KEY, value BLOB)")));
            query.prepare(QStringLiteral("INSERT INTO ItemTable (key, value) VALUES (?, ?)"));
            query.addBindValue(QStringLiteral("some.other.key"));
            query.addBindValue(QByteArray("x"));
            QVERIFY(query.exec());
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("ideproject-empty"));
        const QString proj = writeMenubarFixture(home);

        VscodeProjectSource source;
        const QList<ProjectItem> items = source.projects();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).path, proj);
    }
}

void TestDataSources::vscodeWorkspaceSupplement()
{
    const QString home = useHome(QStringLiteral("workspace"));
    const QString historyProj = home + QStringLiteral("/proj/history");
    QVERIFY(QDir().mkpath(historyProj));
    QVERIFY(writeFile(home + QStringLiteral("/.config/Code/User/globalStorage/storage.json"),
                      jsonBytes(menubarStorageJson(QJsonArray{folderRecentItem(historyProj)}))));

    const QString wsOldPath = home + QStringLiteral("/proj/ws-old");
    const QString wsNewPath = home + QStringLiteral("/proj/ws-new");
    QVERIFY(QDir().mkpath(wsOldPath));
    QVERIFY(QDir().mkpath(wsNewPath));

    const QString wsOldJson = home + QStringLiteral(
        "/.config/Code/User/workspaceStorage/hash-old/workspace.json");
    const QString wsNewJson = home + QStringLiteral(
        "/.config/Code/User/workspaceStorage/hash-new/workspace.json");
    QVERIFY(writeFile(wsOldJson, QByteArray("{\"folder\":\"")
                                     + folderEntry(wsOldPath).toUtf8() + "\"}"));
    QVERIFY(writeFile(wsNewJson, QByteArray("{\"folder\":\"")
                                     + folderEntry(wsNewPath).toUtf8() + "\"}"));

    // 显式设定 mtime 控制排序（无 state.vscdb 时 recency 取 workspace.json 的 mtime）
    auto setMtime = [](const QString &path, qint64 ms) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.setFileTime(QDateTime::fromMSecsSinceEpoch(ms),
                      QFileDevice::FileModificationTime);
        f.close();
    };
    setMtime(wsOldJson, 1000);
    setMtime(wsNewJson, 2000);

    VscodeProjectSource source;
    const QList<ProjectItem> items = source.projects();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items.at(0).path, historyProj); // 历史列表在前
    QCOMPARE(items.at(1).path, wsNewPath);   // 工作区按 mtime 倒序
    QCOMPARE(items.at(2).path, wsOldPath);
    QCOMPARE(items.at(1).lastUsed, 2000);
    QCOMPARE(items.at(2).lastUsed, 1000);
}

void TestDataSources::vscodeVariants()
{
    // 各变体配置目录独立：Insiders 的记录分组为 VS Code Insiders
    const QString home = useHome(QStringLiteral("variants"));
    const QString insidersProj = home + QStringLiteral("/proj/insiders");
    QVERIFY(QDir().mkpath(insidersProj));
    QVERIFY(writeFile(
        home + QStringLiteral("/.config/Code - Insiders/User/globalStorage/storage.json"),
        jsonBytes(menubarStorageJson(QJsonArray{folderRecentItem(insidersProj)}))));

    VscodeProjectSource source;
    const QList<ProjectItem> items = source.projects();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.at(0).path, insidersProj);
    QCOMPARE(items.at(0).group, QStringLiteral("VS Code Insiders"));
    QCOMPARE(items.at(0).launcher, QStringLiteral("code-insiders"));
}

void TestDataSources::jetbrainsAdditionalInfo()
{
    const QString home = useHome(QStringLiteral("jb-info"));
    const QString projA = home + QStringLiteral("/jb/alpha"); // ts 3000
    const QString projB = home + QStringLiteral("/jb/beta");  // ts 1000
    const QString projC = home + QStringLiteral("/jb/gamma"); // ts 2000
    QVERIFY(QDir().mkpath(projA));
    QVERIFY(QDir().mkpath(projB));
    QVERIFY(QDir().mkpath(projC));

    QVERIFY(writeFile(
        home + QStringLiteral("/.config/JetBrains/IntelliJIdea2026.1/options/recentProjects.xml"),
        jbRecentXml({{QStringLiteral("$USER_HOME$/jb/alpha"), 3000},
                     {QStringLiteral("$USER_HOME$/jb/beta"), 1000},
                     {QStringLiteral("$USER_HOME$/jb/gamma"), 2000},
                     {QStringLiteral("/nonexistent/xyz"), 4000}}))); // 不存在 → 过滤

    JetbrainsProjectSource source;
    const QList<ProjectItem> items = source.projects();
    QCOMPARE(items.size(), 3);

    // 按 projectOpenTimestamp 降序，而非 XML 插入序
    QCOMPARE(items.at(0).path, projA);
    QCOMPARE(items.at(0).lastUsed, 3000);
    QCOMPARE(items.at(0).name, QStringLiteral("alpha"));
    QCOMPARE(items.at(0).source, QStringLiteral("jetbrains"));
    QCOMPARE(items.at(0).group, QStringLiteral("IntelliJ IDEA"));
    QCOMPARE(items.at(0).launcher, QStringLiteral("idea"));
    QCOMPARE(items.at(1).path, projC);
    QCOMPARE(items.at(1).lastUsed, 2000);
    QCOMPARE(items.at(2).path, projB);
    QCOMPARE(items.at(2).lastUsed, 1000);
}

void TestDataSources::jetbrainsLegacyFormat()
{
    // 2020.2 老格式：recentPaths 列表；lastProjectLocation 是上级目录、不是项目本身
    const QString home = useHome(QStringLiteral("jb-legacy"));
    const QString projOld = home + QStringLiteral("/jb/old");
    QVERIFY(QDir().mkpath(projOld));

    QVERIFY(writeFile(
        home + QStringLiteral("/.config/JetBrains/IntelliJIdea2020.2/options/recentProjects.xml"),
        jbLegacyXml({QStringLiteral("$USER_HOME$/jb/old")},
                    QStringLiteral("$USER_HOME$/jb"))));

    JetbrainsProjectSource source;
    const QList<ProjectItem> items = source.projects();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.at(0).path, projOld);
}

void TestDataSources::jetbrainsVersionSelection()
{
    // 同款产品只读最新版本目录；最新版本无记录时才回退更旧版本
    const QString home = useHome(QStringLiteral("jb-versions"));
    const QString projNew = home + QStringLiteral("/jb/new");
    const QString projOld = home + QStringLiteral("/jb/old2");
    QVERIFY(QDir().mkpath(projNew));
    QVERIFY(QDir().mkpath(projOld));

    const QString newXml = home + QStringLiteral(
        "/.config/JetBrains/IntelliJIdea2026.1/options/recentProjects.xml");
    const QString oldXml = home + QStringLiteral(
        "/.config/JetBrains/IntelliJIdea2025.2/options/recentProjects.xml");
    QVERIFY(writeFile(newXml, jbRecentXml({{projNew, 5000}})));
    QVERIFY(writeFile(oldXml, jbRecentXml({{projOld, 9000}}))); // 旧版本条目时间更新也不读

    JetbrainsProjectSource source;
    QList<ProjectItem> items = source.projects();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.at(0).path, projNew);

    // 最新版本无记录（xml 缺失）→ 回退更旧版本
    QVERIFY(QFile::remove(newXml));
    items = source.projects();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.at(0).path, projOld);
}

void TestDataSources::jetbrainsAndroidStudio()
{
    // Android Studio 配置目录在 ~/.config/Google/ 下
    const QString home = useHome(QStringLiteral("jb-as"));
    const QString proj = home + QStringLiteral("/as/app");
    QVERIFY(QDir().mkpath(proj));
    QVERIFY(writeFile(
        home + QStringLiteral("/.config/Google/AndroidStudio2025.1/options/recentProjects.xml"),
        jbRecentXml({{proj, 1234}})));

    JetbrainsProjectSource source;
    const QList<ProjectItem> items = source.projects();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.at(0).path, proj);
    QCOMPARE(items.at(0).group, QStringLiteral("Android Studio"));
    QCOMPARE(items.at(0).launcher, QStringLiteral("androidstudio"));
}

QTEST_GUILESS_MAIN(TestDataSources)

#include "tst_datasources.moc"
