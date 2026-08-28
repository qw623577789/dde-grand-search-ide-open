// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jetbrainsprojectsource.h"

#include "desktopfile.h"

#include <algorithm>
#include <functional>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QSet>
#include <QXmlStreamReader>

Q_LOGGING_CATEGORY(logJetbrains, "org.deepin.grandsearch.ideproject.jetbrains")

static const int kPerProductLimit = 20; // 每个产品最多返回的项目数

const QList<JetbrainsProjectSource::Product> JetbrainsProjectSource::kProducts = {
    { QStringLiteral("IntelliJIdea"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "IntelliJ IDEA"),
      {QStringLiteral("jetbrains-idea"), QStringLiteral("idea")},
      {QStringLiteral("IDEA")}, QStringLiteral("idea"), QStringLiteral("idea") },
    // 社区版配置目录前缀为 IdeaIC，与旗舰版合并展示
    { QStringLiteral("IdeaIC"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "IntelliJ IDEA"),
      {QStringLiteral("jetbrains-idea"), QStringLiteral("idea")},
      {QStringLiteral("IDEA")}, QStringLiteral("idea"), QStringLiteral("idea") },
    { QStringLiteral("PyCharm"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "PyCharm"),
      {QStringLiteral("jetbrains-pycharm"), QStringLiteral("pycharm")},
      {QStringLiteral("PyCharm")}, QStringLiteral("pycharm"), QStringLiteral("pycharm") },
    { QStringLiteral("CLion"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "CLion"),
      {QStringLiteral("jetbrains-clion"), QStringLiteral("clion")},
      {QStringLiteral("CLion")}, QStringLiteral("clion"), QStringLiteral("clion") },
    { QStringLiteral("GoLand"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "GoLand"),
      {QStringLiteral("jetbrains-goland"), QStringLiteral("goland")},
      {QStringLiteral("GoLand")}, QStringLiteral("goland"), QStringLiteral("goland") },
    { QStringLiteral("WebStorm"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "WebStorm"),
      {QStringLiteral("jetbrains-webstorm"), QStringLiteral("webstorm")},
      {QStringLiteral("WebStorm")}, QStringLiteral("webstorm"), QStringLiteral("webstorm") },
    { QStringLiteral("PhpStorm"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "PhpStorm"),
      {QStringLiteral("jetbrains-phpstorm"), QStringLiteral("phpstorm")},
      {QStringLiteral("PhpStorm")}, QStringLiteral("phpstorm"), QStringLiteral("phpstorm") },
    { QStringLiteral("Rider"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "Rider"),
      {QStringLiteral("jetbrains-rider"), QStringLiteral("rider")},
      {QStringLiteral("Rider")}, QStringLiteral("rider"), QStringLiteral("rider") },
    { QStringLiteral("DataGrip"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "DataGrip"),
      {QStringLiteral("jetbrains-datagrip"), QStringLiteral("datagrip")},
      {QStringLiteral("DataGrip")}, QStringLiteral("datagrip"), QStringLiteral("datagrip") },
    { QStringLiteral("DataSpell"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "DataSpell"),
      {QStringLiteral("jetbrains-dataspell"), QStringLiteral("dataspell")},
      {QStringLiteral("DataSpell")}, QStringLiteral("dataspell"), QStringLiteral("dataspell") },
    { QStringLiteral("RubyMine"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "RubyMine"),
      {QStringLiteral("jetbrains-rubymine"), QStringLiteral("rubymine")},
      {QStringLiteral("RubyMine")}, QStringLiteral("rubymine"), QStringLiteral("rubymine") },
    { QStringLiteral("RustRover"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "RustRover"),
      {QStringLiteral("jetbrains-rustrover"), QStringLiteral("rustrover")},
      {QStringLiteral("RustRover")}, QStringLiteral("rustrover"), QStringLiteral("rustrover") },
    { QStringLiteral("Aqua"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "Aqua"),
      {QStringLiteral("jetbrains-aqua"), QStringLiteral("aqua")},
      {QStringLiteral("Aqua")}, QStringLiteral("aqua"), QStringLiteral("aqua") },
    // Android Studio 配置目录位于 ~/.config/Google/ 下，单独处理（见 projects()）
    { QStringLiteral("AndroidStudio"), QT_TRANSLATE_NOOP("JetbrainsProjectSource", "Android Studio"),
      {QStringLiteral("android-studio"), QStringLiteral("androidstudio")},
      {QStringLiteral("android-studio")}, QStringLiteral("android-studio"),
      QStringLiteral("androidstudio") },
};

JetbrainsProjectSource::JetbrainsProjectSource()
{
}

QString JetbrainsProjectSource::id() const
{
    return QStringLiteral("jetbrains");
}

QString JetbrainsProjectSource::groupName() const
{
    // 各产品使用独立分组，此值仅在 item.group 为空时兜底
    return tr("JetBrains IDEs");
}

bool JetbrainsProjectSource::matchScopePrefix(const QString &text, QString *rest) const
{
    static const QStringList aliases = {
        QStringLiteral("jh"),
        QStringLiteral("jb"),
        QStringLiteral("jetbrains"),
    };
    return matchAliasPrefix(text, aliases, rest);
}

QList<ProjectItem> JetbrainsProjectSource::projects() const
{
    QList<ProjectItem> result;

    const QString configRoot = QDir::home().filePath(QStringLiteral(".config"));

    // 按配置目录名（含版本号）自然降序，新版本优先
    QCollator collator;
    collator.setNumericMode(true);

    const QString jetbrainsRoot = configRoot + QStringLiteral("/JetBrains");
    const QString googleRoot = configRoot + QStringLiteral("/Google");

    const QStringList jetbrainsDirs = QDir(jetbrainsRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const QStringList googleDirs = QDir(googleRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const Product &product : kProducts) {
        const bool isAndroidStudio = product.key == QLatin1String("androidstudio");
        const QString root = isAndroidStudio ? googleRoot : jetbrainsRoot;
        const QStringList &allDirs = isAndroidStudio ? googleDirs : jetbrainsDirs;

        // 该产品的所有版本目录，按版本号降序
        QStringList productDirs;
        for (const QString &dir : allDirs) {
            if (dir.startsWith(product.dirPrefix))
                productDirs.append(dir);
        }
        std::sort(productDirs.begin(), productDirs.end(),
                  [&collator](const QString &a, const QString &b) {
                      return collator.compare(a, b) > 0;
                  });

        QSet<QString> seen;
        int count = 0;
        for (const QString &dir : productDirs) {
            const QString productDir = root + QLatin1Char('/') + dir;

            QStringList xmlFiles;
            xmlFiles.append(productDir + QStringLiteral("/options/recentProjects.xml"));
            if (product.key == QLatin1String("rider"))
                xmlFiles.append(productDir + QStringLiteral("/options/recentSolutions.xml"));

            const int before = count;
            QList<ProjectItem> versionItems;
            for (const QString &xmlFile : xmlFiles) {
                const QString suffix = xmlFile.endsWith(QStringLiteral("recentSolutions.xml"))
                    ? tr("Solutions") : QString();
                versionItems.append(parseRecentXml(xmlFile, product, suffix));
            }

            // 按逐项打开时间降序（同时间戳保持 XML 内顺序），再截取每产品上限
            std::stable_sort(versionItems.begin(), versionItems.end(),
                             [](const ProjectItem &a, const ProjectItem &b) {
                                 return a.lastUsed > b.lastUsed;
                             });
            for (const ProjectItem &item : versionItems) {
                if (count >= kPerProductLimit)
                    break;
                if (seen.contains(item.path))
                    continue;
                seen.insert(item.path);
                result.append(item);
                count++;
            }

            // 升级时历史打开记录自动迁移到新版本，同款软件只读最新版本目录；
            // 仅当最新版本尚无记录（如安装后未启动过）时才继续读更旧版本
            if (count > before) {
                qCDebug(logJetbrains) << product.displayName << ": using" << productDir
                                      << count - before << "items";
                break;
            }
        }
    }

    return result;
}

bool JetbrainsProjectSource::open(const ProjectItem &item) const
{
    const Product *product = nullptr;
    for (const Product &p : kProducts) {
        if (p.key == item.launcher) {
            product = &p;
            break;
        }
    }
    if (!product)
        return ProjectSource::open(item);

    // 1) desktop 文件（覆盖 Toolbox / 官方安装包 / flatpak 创建的入口）
    const QStringList files = DesktopFileHelper::locate(product->keywords);
    for (const QString &file : files) {
        QStringList exec = DesktopFileHelper::parseExec(file);
        if (exec.isEmpty())
            continue;

        const QString program = exec.takeFirst();
        exec.append(item.path);
        qCInfo(logJetbrains) << "Launching" << program << exec;
        if (QProcess::startDetached(program, exec))
            return true;
    }

    // 2) JetBrains Toolbox 安装目录
    const QString toolboxRoot = QDir::home().filePath(
        QStringLiteral(".local/share/JetBrains/Toolbox/apps"));
    const QStringList appDirs = QDir(toolboxRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QString matchedApp;
    for (const QString &dir : appDirs) {
        for (const QString &keyword : product->toolbox) {
            if (dir.contains(keyword, Qt::CaseInsensitive)) {
                matchedApp = dir;
                break;
            }
        }
        if (!matchedApp.isEmpty())
            break;
    }

    if (!matchedApp.isEmpty()) {
        const QString chDir = toolboxRoot + QLatin1Char('/') + matchedApp + QStringLiteral("/ch-0");
        const QStringList versions = QDir(chDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        // 取最大版本号（构建号，字符串降序即可）
        QStringList sorted = versions;
        std::sort(sorted.begin(), sorted.end(), std::greater<QString>());
        for (const QString &version : sorted) {
            const QDir binDir(chDir + QLatin1Char('/') + version + QStringLiteral("/bin"));
            const QStringList scripts = binDir.entryList({QStringLiteral("*.sh")},
                                                         QDir::Files | QDir::Executable);
            if (scripts.isEmpty())
                continue;

            const QString launcher = binDir.absoluteFilePath(scripts.first());
            qCInfo(logJetbrains) << "Launching via Toolbox:" << launcher << item.path;
            if (QProcess::startDetached(launcher, {item.path}))
                return true;
            break;
        }
    }

    qCWarning(logJetbrains) << "No launcher found for" << item.launcher << ", fallback to file manager";
    return ProjectSource::open(item);
}

QList<ProjectItem> JetbrainsProjectSource::parseRecentXml(const QString &xmlPath,
                                                         const Product &product,
                                                         const QString &displaySuffix)
{
    QFile file(xmlPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QXmlStreamReader reader(&file);

    // 文件在每次打开/关闭项目时重写，mtime 作为无逐项时间戳时的回退值
    const qint64 xmlMtime = QFileInfo(xmlPath).lastModified().toMSecsSinceEpoch();

    QList<ProjectItem> result;
    auto appendItem = [&](const QString &rawPath, qint64 lastUsed) {
        if (rawPath.isEmpty())
            return;

        QString path = rawPath;
        if (path.startsWith(QLatin1String("$USER_HOME$")))
            path = QDir::homePath() + path.mid(QLatin1String("$USER_HOME$").size());
        if (!path.startsWith(QLatin1Char('/')))
            return;

        const QFileInfo info(path);
        if (!info.exists())
            return;

        ProjectItem item;
        item.id = path;
        item.path = path;
        item.name = info.isFile() ? info.completeBaseName() : info.fileName();
        item.icon = product.icon;
        item.source = QStringLiteral("jetbrains");
        item.group = displaySuffix.isEmpty()
            ? tr(product.displayName)
            : tr("%1 - %2").arg(tr(product.displayName), displaySuffix);
        item.launcher = product.key;
        item.lastUsed = lastUsed;
        result.append(item);
    };

    // 2021+ 格式的 additionalInfo 按 entry 插入序排列（并非 MRU），逐项的最近
    // 打开时间在其子节点 RecentProjectMetaInfo 的 projectOpenTimestamp 中
    QString pendingKey;
    qint64 pendingTs = xmlMtime;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("entry")) {
            pendingKey = reader.attributes().value(QStringLiteral("key")).toString();
            pendingTs = xmlMtime;
        } else if (reader.isStartElement() && reader.name() == QLatin1String("option")) {
            const QString name = reader.attributes().value(QStringLiteral("name")).toString();
            if (name == QLatin1String("projectOpenTimestamp") && !pendingKey.isEmpty()) {
                pendingTs = reader.attributes().value(QStringLiteral("value")).toLongLong();
                continue;
            }
            // entry 内部其余 option 是元数据（frameTitle 等），忽略
            if (!pendingKey.isEmpty())
                continue;
            // 2020.2 老格式：recentPaths 列表的 option 即项目路径；
            // lastProjectLocation 是新建项目的默认上级目录，不是项目本身
            if (name == QLatin1String("lastProjectLocation")
                || name == QLatin1String("lastProjectCreationLocation"))
                continue;
            appendItem(reader.attributes().value(QStringLiteral("value")).toString(), xmlMtime);
        } else if (reader.isEndElement() && reader.name() == QLatin1String("entry")) {
            if (!pendingKey.isEmpty()) {
                appendItem(pendingKey, pendingTs);
                pendingKey.clear();
            }
        }
    }

    return result;
}
