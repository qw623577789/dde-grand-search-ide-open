// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ideprojectsearch.h"
#include "sources/jetbrainsprojectsource.h"
#include "sources/projectsource.h"
#include "sources/vscodeprojectsource.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

// 核心搜索流程测试：用 mock 数据源驱动 IdeProjectSearch，覆盖协议校验、
// 匹配规则、作用域、快捷命令配置、去重、排序截断、分组与序号前缀、Action 打开。
// HOME 重定向到临时目录，使快捷命令配置读取与真实环境隔离。

namespace {

ProjectItem makeItem(const QString &path, const QString &name, qint64 lastUsed,
                     const QString &source, const QString &group = QString())
{
    ProjectItem item;
    item.id = path;
    item.name = name;
    item.path = path;
    item.icon = QStringLiteral("icon");
    item.source = source;
    item.group = group;
    item.lastUsed = lastUsed;
    return item;
}

QJsonArray parseGroups(const QString &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json.toUtf8()).object();
    return root.value(QStringLiteral("cont")).toArray();
}

QJsonArray parseItems(const QString &json)
{
    QJsonArray items;
    const QJsonArray groups = parseGroups(json);
    for (const QJsonValue &groupValue : groups) {
        const QJsonArray groupItems = groupValue.toObject()
            .value(QStringLiteral("items")).toArray();
        for (const QJsonValue &itemValue : groupItems)
            items.append(itemValue);
    }
    return items;
}

QStringList itemIds(const QString &json)
{
    QStringList ids;
    const QJsonArray items = parseItems(json);
    for (const QJsonValue &value : items)
        ids.append(value.toObject().value(QStringLiteral("item")).toString());
    return ids;
}

QStringList itemNames(const QString &json)
{
    QStringList names;
    const QJsonArray items = parseItems(json);
    for (const QJsonValue &value : items)
        names.append(value.toObject().value(QStringLiteral("name")).toString());
    return names;
}

QString searchJson(const QString &cont)
{
    return QStringLiteral("{\"ver\":\"1.0\",\"mID\":\"m1\",\"cont\":\"%1\"}").arg(cont);
}

} // namespace

class MockSource : public ProjectSource
{
public:
    MockSource(const QString &id, const QString &group, const QStringList &aliases,
               const QList<ProjectItem> &items = {})
        : m_id(id), m_group(group), m_aliases(aliases), m_items(items)
    {
    }

    QString id() const override { return m_id; }
    QString groupName() const override { return m_group; }
    QList<ProjectItem> projects() const override { return m_items; }
    bool matchScopePrefix(const QString &text, QString *rest) const override
    {
        return matchAliasPrefix(text, m_aliases, rest);
    }
    bool open(const ProjectItem &item) const override
    {
        m_opened.append(item);
        return true;
    }

    mutable QList<ProjectItem> m_opened;

private:
    QString m_id;
    QString m_group;
    QStringList m_aliases;
    QList<ProjectItem> m_items;
};

class TestSearch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void aliasPrefix();
    void scopePrefixBuiltin();

    void invalidRequests();
    void emptyKeyword();
    void nameAndPathMatch();
    void scopeFilter();
    void shortcutConfig();
    void dedup();
    void sortAndTruncate();
    void groupingAndRankPrefix();
    void actionOpen();
    void actionOpenSamePath();
    void stopInterface();

private:
    QTemporaryDir m_home;
};

void TestSearch::initTestCase()
{
    QVERIFY(m_home.isValid());
    // 隔离快捷命令配置文件（~/.config/deepin/dde-grand-search-ideproject/...）
    qputenv("HOME", m_home.path().toUtf8());
}

void TestSearch::cleanupTestCase()
{
}

void TestSearch::aliasPrefix()
{
    QString rest;
    QVERIFY(matchAliasPrefix(QStringLiteral("vsc"), {QStringLiteral("vsc")}, &rest));
    QVERIFY(rest.isEmpty());
    QVERIFY(matchAliasPrefix(QStringLiteral("vsc dde"), {QStringLiteral("vsc")}, &rest));
    QCOMPARE(rest, QStringLiteral("dde"));
    QVERIFY(matchAliasPrefix(QStringLiteral("VSC"), {QStringLiteral("vsc")}, &rest));
    QVERIFY(!matchAliasPrefix(QStringLiteral("vscx"), {QStringLiteral("vsc")}, &rest));
    QVERIFY(!matchAliasPrefix(QStringLiteral("x vsc"), {QStringLiteral("vsc")}, &rest));
}

void TestSearch::scopePrefixBuiltin()
{
    VscodeProjectSource vscode;
    QString rest;
    QVERIFY(vscode.matchScopePrefix(QStringLiteral("vsc"), &rest));
    QVERIFY(vscode.matchScopePrefix(QStringLiteral("vscode demo"), &rest));
    QCOMPARE(rest, QStringLiteral("demo"));
    QVERIFY(!vscode.matchScopePrefix(QStringLiteral("jh"), &rest));

    JetbrainsProjectSource jetbrains;
    QVERIFY(jetbrains.matchScopePrefix(QStringLiteral("jh"), &rest));
    QVERIFY(jetbrains.matchScopePrefix(QStringLiteral("jb dde"), &rest));
    QCOMPARE(rest, QStringLiteral("dde"));
    QVERIFY(jetbrains.matchScopePrefix(QStringLiteral("jetbrains"), &rest));
    QVERIFY(!jetbrains.matchScopePrefix(QStringLiteral("vsc"), &rest));
}

void TestSearch::invalidRequests()
{
    IdeProjectSearch service;
    QCOMPARE(service.search(QStringLiteral("not json")), QString());
    QCOMPARE(service.search(QStringLiteral("{\"ver\":\"9.9\",\"mID\":\"m1\",\"cont\":\"demo\"}")),
             QString());
    QCOMPARE(service.search(QStringLiteral("{\"ver\":\"1.0\",\"mID\":\"\",\"cont\":\"demo\"}")),
             QString());
}

void TestSearch::emptyKeyword()
{
    IdeProjectSearch service;
    service.registerSource(new MockSource(QStringLiteral("vscode"), QStringLiteral("VS Code"),
                                          {QStringLiteral("vsc")}));
    const QString result = service.search(searchJson(QString()));
    const QJsonObject root = QJsonDocument::fromJson(result.toUtf8()).object();
    QCOMPARE(root.value(QStringLiteral("ver")).toString(), QStringLiteral("1.0"));
    QCOMPARE(root.value(QStringLiteral("mID")).toString(), QStringLiteral("m1"));
    QVERIFY(root.value(QStringLiteral("cont")).toArray().isEmpty());
}

void TestSearch::nameAndPathMatch()
{
    IdeProjectSearch service;
    service.registerSource(new MockSource(QStringLiteral("vscode"), QStringLiteral("VS Code"),
                                          {QStringLiteral("vsc")},
                                          {makeItem(QStringLiteral("/p/alpha/AlphaDemo"),
                                                    QStringLiteral("AlphaDemo"), 1, QStringLiteral("vscode")),
                                           makeItem(QStringLiteral("/p/beta/Beta"),
                                                    QStringLiteral("Beta"), 2, QStringLiteral("vscode"))}));

    // 名称子串匹配（大小写不敏感）
    QCOMPARE(itemNames(service.search(searchJson(QStringLiteral("alph")))).size(), 1);
    QCOMPARE(itemNames(service.search(searchJson(QStringLiteral("DEMO")))).size(), 1);

    // 关键词 ≥3 字符时才匹配完整路径
    QCOMPARE(itemIds(service.search(searchJson(QStringLiteral("/p/beta")))).size(), 1);
    QVERIFY(itemIds(service.search(searchJson(QStringLiteral("/p/beta"))))
                .first().endsWith(QStringLiteral("/p/beta/Beta")));

    // 短关键词不匹配路径、且名称不包含时无结果
    QVERIFY(itemNames(service.search(searchJson(QStringLiteral("z")))).isEmpty());
}

void TestSearch::scopeFilter()
{
    IdeProjectSearch service;
    service.registerSource(new MockSource(QStringLiteral("vscode"), QStringLiteral("VS Code"),
                                          {QStringLiteral("vsc"), QStringLiteral("vscode")},
                                          {makeItem(QStringLiteral("/p/one"), QStringLiteral("One"), 1,
                                                    QStringLiteral("vscode")),
                                           makeItem(QStringLiteral("/p/two"), QStringLiteral("Two"), 2,
                                                    QStringLiteral("vscode"))}));
    service.registerSource(new MockSource(QStringLiteral("jetbrains"), QStringLiteral("JetBrains"),
                                          {QStringLiteral("jh")},
                                          {makeItem(QStringLiteral("/p/three"), QStringLiteral("Three"), 3,
                                                    QStringLiteral("jetbrains"))}));

    // 作用域前缀列出该 IDE 全部历史
    const QStringList scoped = itemIds(service.search(searchJson(QStringLiteral("vsc"))));
    QCOMPARE(scoped.size(), 2);
    for (const QString &id : scoped)
        QVERIFY(id.endsWith(QStringLiteral("/p/one")) || id.endsWith(QStringLiteral("/p/two")));

    // 作用域 + 关键词在作用域内筛选
    const QStringList filtered = itemIds(service.search(searchJson(QStringLiteral("vsc one"))));
    QCOMPARE(filtered.size(), 1);
    QVERIFY(filtered.first().endsWith(QStringLiteral("/p/one")));

    // 别名 vscode 同样生效
    QCOMPARE(itemIds(service.search(searchJson(QStringLiteral("vscode")))).size(), 2);
}

void TestSearch::shortcutConfig()
{
    IdeProjectSearch service;
    service.registerSource(new MockSource(QStringLiteral("vscode"), QStringLiteral("VS Code"),
                                          {QStringLiteral("vsc")},
                                          {makeItem(QStringLiteral("/p/one"), QStringLiteral("One"), 1,
                                                    QStringLiteral("vscode"))}));
    service.registerSource(new MockSource(QStringLiteral("jetbrains"), QStringLiteral("JetBrains"),
                                          {QStringLiteral("jh")},
                                          {makeItem(QStringLiteral("/p/three"), QStringLiteral("Three"), 2,
                                                    QStringLiteral("jetbrains"))}));

    const QString configDir = m_home.path()
        + QStringLiteral("/.config/deepin/dde-grand-search-ideproject");
    QVERIFY(QDir().mkpath(configDir));
    const QString configPath = configDir + QStringLiteral("/dde-grand-search-ideproject.conf");

    auto writeConfig = [&](const QByteArray &content) {
        QFile f(configPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(content);
        f.close();
    };

    // 配置定义的别名指向已注册作用域 → 生效
    writeConfig("[Shortcut]\nmyvsc=vscode\n");
    const QStringList viaAlias = itemIds(service.search(searchJson(QStringLiteral("myvsc"))));
    QCOMPARE(viaAlias.size(), 1);
    QVERIFY(viaAlias.first().endsWith(QStringLiteral("/p/one")));

    // 显式重定义内置别名 → 覆盖默认行为
    writeConfig("[Shortcut]\nvsc=jetbrains\n");
    const QStringList redirected = itemIds(service.search(searchJson(QStringLiteral("vsc"))));
    QCOMPARE(redirected.size(), 1);
    QVERIFY(redirected.first().endsWith(QStringLiteral("/p/three")));

    // 指向未注册作用域的别名被忽略，回退内置前缀
    writeConfig("[Shortcut]\nvsc=unknown\n");
    const QStringList fallback = itemIds(service.search(searchJson(QStringLiteral("vsc"))));
    QCOMPARE(fallback.size(), 1);
    QVERIFY(fallback.first().endsWith(QStringLiteral("/p/one")));

    // 配置每次搜索时读取：删除配置后立即恢复默认
    writeConfig("");
    const QStringList afterClear = itemIds(service.search(searchJson(QStringLiteral("vsc"))));
    QCOMPARE(afterClear.size(), 1);
    QVERIFY(afterClear.first().endsWith(QStringLiteral("/p/one")));
}

void TestSearch::dedup()
{
    IdeProjectSearch service;
    service.registerSource(new MockSource(QStringLiteral("vscode"), QStringLiteral("VS Code"),
                                          {QStringLiteral("vsc")},
                                          {makeItem(QStringLiteral("/p/one"), QStringLiteral("One"), 1,
                                                    QStringLiteral("vscode")),
                                           makeItem(QStringLiteral("/p/two"), QStringLiteral("Two"), 2,
                                                    QStringLiteral("vscode"))}));
    service.registerSource(new MockSource(QStringLiteral("jetbrains"), QStringLiteral("JetBrains"),
                                          {QStringLiteral("jh")},
                                          {makeItem(QStringLiteral("/p/one"), QStringLiteral("OneJB"), 3,
                                                    QStringLiteral("jetbrains")),
                                           makeItem(QStringLiteral("/p/three"), QStringLiteral("Three"), 4,
                                                    QStringLiteral("jetbrains"))}));

    // 全局搜索按来源各保留一条：同一路径的 VS Code 与 JetBrains 记录都展示
    const QJsonArray globalGroups = parseGroups(service.search(searchJson(QStringLiteral("one"))));
    QCOMPARE(globalGroups.size(), 2);
    // 按 lastUsed 排序，JetBrains 条目（3）在 VS Code 条目（1）之前
    QCOMPARE(globalGroups.at(0).toObject().value(QStringLiteral("group")).toString(),
             QStringLiteral("JetBrains"));
    QCOMPARE(globalGroups.at(1).toObject().value(QStringLiteral("group")).toString(),
             QStringLiteral("VS Code"));

    // 作用域搜索仅列出对应 IDE 的记录
    const QStringList vsc = itemIds(service.search(searchJson(QStringLiteral("vsc one"))));
    QCOMPARE(vsc.size(), 1);
    QVERIFY(vsc.first().endsWith(QStringLiteral("/p/one")));
    const QStringList jb = itemIds(service.search(searchJson(QStringLiteral("jh one"))));
    QCOMPARE(jb.size(), 1);
    QVERIFY(jb.first().endsWith(QStringLiteral("/p/one")));
}

void TestSearch::sortAndTruncate()
{
    // lastUsed 降序；相同时间戳保持来源注册（MRU）顺序
    QList<ProjectItem> items = {
        makeItem(QStringLiteral("/p/a"), QStringLiteral("A"), 5, QStringLiteral("vscode")),
        makeItem(QStringLiteral("/p/b"), QStringLiteral("B"), 3, QStringLiteral("vscode")),
        makeItem(QStringLiteral("/p/c"), QStringLiteral("C"), 3, QStringLiteral("vscode")),
        makeItem(QStringLiteral("/p/d"), QStringLiteral("D"), 1, QStringLiteral("vscode")),
    };
    IdeProjectSearch service;
    service.registerSource(new MockSource(QStringLiteral("vscode"), QStringLiteral("VS Code"),
                                          {QStringLiteral("vsc")}, items));
    const QStringList sorted = itemIds(service.search(searchJson(QStringLiteral("vsc"))));
    QCOMPARE(sorted.size(), 4);
    QVERIFY(sorted.at(0).endsWith(QStringLiteral("/p/a")));
    QVERIFY(sorted.at(1).endsWith(QStringLiteral("/p/b")));
    QVERIFY(sorted.at(2).endsWith(QStringLiteral("/p/c")));
    QVERIFY(sorted.at(3).endsWith(QStringLiteral("/p/d")));

    // 超过 100 条截断
    QList<ProjectItem> many;
    for (int i = 0; i < 150; ++i)
        many.append(makeItem(QStringLiteral("/p/proj%1").arg(i, 3, 10, QLatin1Char('0')),
                             QStringLiteral("P%1").arg(i), 150 - i, QStringLiteral("vscode")));
    IdeProjectSearch service2;
    service2.registerSource(new MockSource(QStringLiteral("vscode"), QStringLiteral("VS Code"),
                                           {QStringLiteral("vsc")}, many));
    QCOMPARE(itemIds(service2.search(searchJson(QStringLiteral("vsc")))).size(), 100);
    // 截断前按 lastUsed 降序，最新的 100 条保留
    QVERIFY(itemIds(service2.search(searchJson(QStringLiteral("vsc"))))
                .last().endsWith(QStringLiteral("/p/proj099")));
}

void TestSearch::groupingAndRankPrefix()
{
    IdeProjectSearch service;
    service.registerSource(new MockSource(
        QStringLiteral("vscode"), QStringLiteral("MockGroup"), {QStringLiteral("vsc")},
        {makeItem(QStringLiteral("/p/a"), QStringLiteral("Alpha"), 4, QStringLiteral("vscode"),
                  QStringLiteral("G1")),
         makeItem(QStringLiteral("/p/b"), QStringLiteral("Beta"), 3, QStringLiteral("vscode"),
                  QStringLiteral("G2")),
         makeItem(QStringLiteral("/p/c"), QStringLiteral("Gamma"), 2, QStringLiteral("vscode"),
                  QStringLiteral("G1")),
         makeItem(QStringLiteral("/p/d"), QStringLiteral("Delta"), 1, QStringLiteral("vscode"))}));

    const QJsonArray groups = parseGroups(service.search(searchJson(QStringLiteral("vsc"))));
    QCOMPARE(groups.size(), 3);
    QCOMPARE(groups.at(0).toObject().value(QStringLiteral("group")).toString(),
             QStringLiteral("G1"));
    QCOMPARE(groups.at(1).toObject().value(QStringLiteral("group")).toString(),
             QStringLiteral("G2"));
    // item.group 为空时回退来源默认组名
    QCOMPARE(groups.at(2).toObject().value(QStringLiteral("group")).toString(),
             QStringLiteral("MockGroup"));

    // 组内保持排序后的顺序；item 带 3 位等宽序号前缀；name 附加完整路径
    const QJsonArray g1Items = groups.at(0).toObject().value(QStringLiteral("items")).toArray();
    QCOMPARE(g1Items.at(0).toObject().value(QStringLiteral("item")).toString(),
             QStringLiteral("000|/p/a"));
    QCOMPARE(g1Items.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Alpha (/p/a)"));
    QCOMPARE(g1Items.at(1).toObject().value(QStringLiteral("item")).toString(),
             QStringLiteral("002|/p/c"));
    QCOMPARE(groups.at(1).toObject().value(QStringLiteral("items")).toArray()
                 .at(0).toObject().value(QStringLiteral("item")).toString(),
             QStringLiteral("001|/p/b"));
    QCOMPARE(groups.at(2).toObject().value(QStringLiteral("items")).toArray()
                 .at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Delta (/p/d)"));
}

void TestSearch::actionOpen()
{
    IdeProjectSearch service;
    MockSource *mock = new MockSource(
        QStringLiteral("vscode"), QStringLiteral("VS Code"), {QStringLiteral("vsc")},
        {makeItem(QStringLiteral("/p/a"), QStringLiteral("Alpha"), 1, QStringLiteral("vscode"))});
    service.registerSource(mock);

    // 先搜索使 m_itemMap 有缓存
    service.search(searchJson(QStringLiteral("vsc")));

    // 带序号前缀的 item 原样回传 → 剥前缀后打开
    QVERIFY(service.action(QStringLiteral(
        "{\"ver\":\"1.0\",\"action\":\"openitem\",\"item\":\"000|/p/a\"}")));
    QCOMPARE(mock->m_opened.size(), 1);
    QCOMPARE(mock->m_opened.first().path, QStringLiteral("/p/a"));

    // 无前缀裸路径同样兼容
    QVERIFY(service.action(QStringLiteral(
        "{\"ver\":\"1.0\",\"action\":\"openitem\",\"item\":\"/p/a\"}")));
    QCOMPARE(mock->m_opened.size(), 2);

    // 未知条目 / 非法动作
    QVERIFY(!service.action(QStringLiteral(
        "{\"ver\":\"1.0\",\"action\":\"openitem\",\"item\":\"000|/p/unknown\"}")));
    QVERIFY(!service.action(QStringLiteral(
        "{\"ver\":\"1.0\",\"action\":\"other\",\"item\":\"000|/p/a\"}")));
    QVERIFY(!service.action(QStringLiteral(
        "{\"ver\":\"1.0\",\"action\":\"openitem\",\"item\":\"\"}")));
}

void TestSearch::actionOpenSamePath()
{
    // 同一路径被两个 IDE 记录：Action 按带前缀的 item 精确定位，各自用对应 IDE 打开
    IdeProjectSearch service;
    MockSource *vsc = new MockSource(
        QStringLiteral("vscode"), QStringLiteral("VS Code"), {QStringLiteral("vsc")},
        {makeItem(QStringLiteral("/p/one"), QStringLiteral("One"), 1, QStringLiteral("vscode"))});
    MockSource *jb = new MockSource(
        QStringLiteral("jetbrains"), QStringLiteral("JetBrains"), {QStringLiteral("jh")},
        {makeItem(QStringLiteral("/p/one"), QStringLiteral("One"), 2, QStringLiteral("jetbrains"))});
    service.registerSource(vsc);
    service.registerSource(jb);

    const QJsonArray items = parseItems(service.search(searchJson(QStringLiteral("one"))));
    QCOMPARE(items.size(), 2);
    for (const QJsonValue &value : items) {
        const QString itemId = value.toObject().value(QStringLiteral("item")).toString();
        QVERIFY(service.action(QStringLiteral(
            "{\"ver\":\"1.0\",\"action\":\"openitem\",\"item\":\"%1\"}").arg(itemId)));
    }
    QCOMPARE(vsc->m_opened.size(), 1);
    QCOMPARE(jb->m_opened.size(), 1);
    QCOMPARE(vsc->m_opened.first().source, QStringLiteral("vscode"));
    QCOMPARE(jb->m_opened.first().source, QStringLiteral("jetbrains"));
}

void TestSearch::stopInterface()
{
    IdeProjectSearch service;
    QVERIFY(service.stop(QStringLiteral("{\"ver\":\"1.0\",\"mID\":\"m1\"}")));
}

QTEST_GUILESS_MAIN(TestSearch)

#include "tst_search.moc"
