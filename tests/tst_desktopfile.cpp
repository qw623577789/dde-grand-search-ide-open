// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktopfile.h"
#include "desktopfile_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <unistd.h>

// desktop 文件定位与 Exec 解析的信任校验单元测试。
// 用户属主夹具经 QTemporaryDir 构造；root 运行时 isTrustedLocation 恒为
// true（root 不受约束），「其他属主不可信」分支因此仅通过代码审查覆盖。
class TestDesktopFile : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void matchesName_data();
    void matchesName();

    void trustedLocation();

    void trustedProgram();

    void locateFiltersAndMatches();

    void parseExecValidation();

private:
    static QString writeDesktopFile(const QString &dir, const QString &name,
                                    const QString &execLine);
    static QString writePlainFile(const QString &dir, const QString &name);

    QTemporaryDir m_tempDir;
    QString m_tempProgram;   // 用户属主的可执行文件
    QString m_plainFile;     // 用户属主的不可执行文件
    QString m_appsDir;       // QStandardPaths 测试模式下的用户应用目录
};

void TestDesktopFile::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_tempProgram = m_tempDir.filePath(QStringLiteral("fake-ide"));
    {
        QFile f(m_tempProgram);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\necho fake\n");
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }

    m_plainFile = writePlainFile(m_tempDir.path(), QStringLiteral("plain"));

    // 测试模式下用户应用目录重定向到 ~/.qttest 下，由当前用户属主，可写入夹具
    QStandardPaths::setTestModeEnabled(true);
    m_appsDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    QVERIFY(QDir().mkpath(m_appsDir));
}

void TestDesktopFile::cleanupTestCase()
{
    // 清理测试模式下写入的 desktop 夹具，避免污染后续测试与本机环境
    for (const QString &name : {QStringLiteral("code.desktop"),
                                QStringLiteral("codeblocks.desktop"),
                                QStringLiteral("com.vscodium.VSCodium.desktop")}) {
        QFile::remove(m_appsDir + QLatin1Char('/') + name);
    }
}

void TestDesktopFile::matchesName_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<QString>("keyword");
    QTest::addColumn<bool>("expected");

    QTest::newRow("exact") << "code" << "code" << true;
    QTest::newRow("component prefix") << "code-url-handler" << "code" << true;
    QTest::newRow("no boundary") << "codeblocks" << "code" << false;
    QTest::newRow("no boundary ideal") << "ideal" << "idea" << false;
    QTest::newRow("flatpak code") << "com.visualstudio.code" << "code" << true;
    QTest::newRow("vs prefix") << "vscodium" << "codium" << true;
    QTest::newRow("flatpak vscodium") << "com.vscodium.VSCodium" << "codium" << true;
    QTest::newRow("vs prefix oss") << "vscode-oss" << "code-oss" << true;
    QTest::newRow("jetbrains ce") << "jetbrains-idea-ce" << "jetbrains-idea" << true;
    QTest::newRow("flatpak intellij") << "com.jetbrains.IntelliJ-IDEA" << "idea" << true;
    QTest::newRow("android studio") << "android-studio" << "android-studio" << true;
    QTest::newRow("jetbrains pycharm ce") << "jetbrains-pycharm-ce" << "pycharm" << true;
    QTest::newRow("case insensitive") << "IDEA" << "idea" << true;
}

void TestDesktopFile::matchesName()
{
    QFETCH(QString, base);
    QFETCH(QString, keyword);
    QFETCH(bool, expected);

    QCOMPARE(DesktopFileHelper::matchesName(base, keyword), expected);
}

void TestDesktopFile::trustedLocation()
{
    // 当前用户属主 → 可信
    QVERIFY(DesktopFileHelper::isTrustedLocation(m_tempDir.path()));
    QVERIFY(DesktopFileHelper::isTrustedLocation(m_tempProgram));

    // 系统目录按策略判定（root 属主且用户不可写 → 可信；环境异常时跳过）
    const QFileInfo sysDir(QStringLiteral("/usr/share/applications"));
    if (sysDir.exists()) {
        const bool expected = ::geteuid() == 0
            || sysDir.ownerId() == static_cast<uint>(::geteuid())
            || (sysDir.ownerId() == 0 && !sysDir.isWritable());
        QCOMPARE(DesktopFileHelper::isTrustedLocation(sysDir.absoluteFilePath()), expected);

        // 系统目录内的 desktop 文件同样按策略判定
        const QFileInfoList sysFiles = QDir(sysDir.absoluteFilePath())
            .entryInfoList({QStringLiteral("*.desktop")}, QDir::Files | QDir::Readable);
        if (!sysFiles.isEmpty()) {
            const QFileInfo &file = sysFiles.first();
            const bool expectedFile = ::geteuid() == 0
                || file.ownerId() == static_cast<uint>(::geteuid())
                || (file.ownerId() == 0 && !file.isWritable());
            QCOMPARE(DesktopFileHelper::isTrustedLocation(file.absoluteFilePath()), expectedFile);
        }
    } else {
        QSKIP("/usr/share/applications not present");
    }
}

void TestDesktopFile::trustedProgram()
{
    QVERIFY(DesktopFileHelper::isTrustedProgram(m_tempProgram));
    QVERIFY(!DesktopFileHelper::isTrustedProgram(QStringLiteral("code")));
    QVERIFY(!DesktopFileHelper::isTrustedProgram(QStringLiteral("/nonexistent/definitely-not-here")));
    QVERIFY(!DesktopFileHelper::isTrustedProgram(m_plainFile));

    // 符号链接：指向可信目标 → 可信；悬空或指向不可执行文件 → 拒绝
    const QString linkOk = m_tempDir.filePath(QStringLiteral("link-ok"));
    QVERIFY(QFile::link(m_tempProgram, linkOk));
    QVERIFY(DesktopFileHelper::isTrustedProgram(linkOk));

    const QString linkDangling = m_tempDir.filePath(QStringLiteral("link-dangling"));
    QVERIFY(QFile::link(QStringLiteral("/nonexistent/definitely-not-here"), linkDangling));
    QVERIFY(!DesktopFileHelper::isTrustedProgram(linkDangling));

    const QString linkPlain = m_tempDir.filePath(QStringLiteral("link-plain"));
    QVERIFY(QFile::link(m_plainFile, linkPlain));
    QVERIFY(!DesktopFileHelper::isTrustedProgram(linkPlain));

    // 系统二进制与其符号链接（如 /usr/bin/code -> /usr/share/code/bin/code）按策略判定
    const QFileInfo env(QStringLiteral("/usr/bin/env"));
    if (env.exists() && env.isExecutable()) {
        const bool expected = ::geteuid() == 0
            || env.ownerId() == static_cast<uint>(::geteuid())
            || (env.ownerId() == 0 && !env.isWritable());
        QCOMPARE(DesktopFileHelper::isTrustedProgram(env.absoluteFilePath()), expected);
    } else {
        QSKIP("/usr/bin/env not executable");
    }

    const QFileInfo codeBin(QStringLiteral("/usr/bin/code"));
    if (codeBin.exists() && codeBin.isExecutable()) {
        const bool expected = ::geteuid() == 0
            || codeBin.ownerId() == static_cast<uint>(::geteuid())
            || (codeBin.ownerId() == 0 && !codeBin.isWritable());
        QCOMPARE(DesktopFileHelper::isTrustedProgram(codeBin.absoluteFilePath()), expected);
    }
}

void TestDesktopFile::locateFiltersAndMatches()
{
    writeDesktopFile(m_appsDir, QStringLiteral("code.desktop"),
                     m_tempProgram + QStringLiteral(" %F"));
    writeDesktopFile(m_appsDir, QStringLiteral("codeblocks.desktop"),
                     m_tempProgram + QStringLiteral(" %F"));
    writeDesktopFile(m_appsDir, QStringLiteral("com.vscodium.VSCodium.desktop"),
                     m_tempProgram + QStringLiteral(" %F"));

    const QStringList code = DesktopFileHelper::locate({QStringLiteral("code")});
    QVERIFY(code.contains(m_appsDir + QStringLiteral("/code.desktop")));
    QVERIFY(!code.contains(m_appsDir + QStringLiteral("/codeblocks.desktop")));

    const QStringList codium = DesktopFileHelper::locate({QStringLiteral("codium")});
    QVERIFY(codium.contains(m_appsDir + QStringLiteral("/com.vscodium.VSCodium.desktop")));

    // 测试模式下系统目录仍参与扫描（root 属主走 owner==0 分支）
    const QFileInfo sysCode(QStringLiteral("/usr/share/applications/code.desktop"));
    if (sysCode.exists()
        && DesktopFileHelper::isTrustedLocation(sysCode.absoluteFilePath())) {
        QVERIFY(code.contains(sysCode.absoluteFilePath()));
    }
}

void TestDesktopFile::parseExecValidation()
{
    const QString good = writeDesktopFile(m_tempDir.path(), QStringLiteral("good.desktop"),
                                          m_tempProgram + QStringLiteral(" %u %f"));
    const QStringList exec = DesktopFileHelper::parseExec(good);
    QCOMPARE(exec.size(), 1);
    QCOMPARE(exec.first(), m_tempProgram);

    // 相对命令名（经 PATH 查找，不可信）
    QVERIFY(DesktopFileHelper::parseExec(
        writeDesktopFile(m_tempDir.path(), QStringLiteral("bad-relative.desktop"),
                         QStringLiteral("code %F"))).isEmpty());

    // 程序不存在
    QVERIFY(DesktopFileHelper::parseExec(
        writeDesktopFile(m_tempDir.path(), QStringLiteral("bad-missing.desktop"),
                         QStringLiteral("/nonexistent/xyz %F"))).isEmpty());

    // 程序不可执行
    QVERIFY(DesktopFileHelper::parseExec(
        writeDesktopFile(m_tempDir.path(), QStringLiteral("bad-plain.desktop"),
                         m_plainFile + QStringLiteral(" %F"))).isEmpty());

    // 字段码去除后其余参数原样保留
    const QString withArgs = writeDesktopFile(m_tempDir.path(), QStringLiteral("with-args.desktop"),
                                              m_tempProgram + QStringLiteral(" --new-window %F %u"));
    const QStringList argsExec = DesktopFileHelper::parseExec(withArgs);
    QCOMPARE(argsExec.size(), 2);
    QCOMPARE(argsExec.at(0), m_tempProgram);
    QCOMPARE(argsExec.at(1), QStringLiteral("--new-window"));

    // 缺少 Exec 字段 → 空
    const QString noExec = m_tempDir.filePath(QStringLiteral("no-exec.desktop"));
    {
        QFile f(noExec);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Desktop Entry]\nName=Test\n");
        f.close();
    }
    QVERIFY(DesktopFileHelper::parseExec(noExec).isEmpty());

    // 真实系统 desktop 文件：能通过校验的，其程序须为绝对路径且可信
    const QStringList sysDirs = {QStringLiteral("/usr/share/applications"),
                                 QStringLiteral("/var/lib/snapd/desktop/applications")};
    bool checked = false;
    for (const QString &dir : sysDirs) {
        const QFileInfoList files = QDir(dir)
            .entryInfoList({QStringLiteral("*.desktop")}, QDir::Files | QDir::Readable);
        for (const QFileInfo &file : files) {
            const QStringList result = DesktopFileHelper::parseExec(file.absoluteFilePath());
            if (result.isEmpty())
                continue;
            QVERIFY(QDir::isAbsolutePath(result.first()));
            QVERIFY(DesktopFileHelper::isTrustedProgram(result.first()));
            checked = true;
            break;
        }
        if (checked)
            break;
    }
    if (!checked)
        QSKIP("no parseable system desktop file");
}

QString TestDesktopFile::writeDesktopFile(const QString &dir, const QString &name,
                                          const QString &execLine)
{
    const QString path = dir + QLatin1Char('/') + name;
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("[Desktop Entry]\nName=Test\nExec=" + execLine.toUtf8() + '\n');
    f.close();
    return path;
}

QString TestDesktopFile::writePlainFile(const QString &dir, const QString &name)
{
    const QString path = dir + QLatin1Char('/') + name;
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("x");
    f.close();
    return path;
}

QTEST_GUILESS_MAIN(TestDesktopFile)

#include "tst_desktopfile.moc"
