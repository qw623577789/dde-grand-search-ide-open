# dde-grand-search-ide-open

DDE 全局搜索（dde-grand-search）搜索插件：查找并打开本机 IDE（VS Code、JetBrains 系列）的历史项目。

![效果](./images/preview.gif)

## 工作原理

全局搜索插件以「独立进程 + D-Bus 服务」形式接入 dde-grand-search：

- daemon 通过 `.conf` 注册文件发现插件，按需启动插件进程；
- daemon 通过 D-Bus 调用插件的 `Search` / `Stop` / `Action` 三个接口（V1.0 JSON 协议）；
- 插件解析各 IDE 的本地配置，返回历史项目列表；用户点击结果时，插件用对应 IDE 打开项目。

协议细节见 dde-grand-search 源码 `docs/plugin-development-guide.md`。

## 项目结构

```
├── CMakeLists.txt                 # 构建配置（Qt6）
├── ide-project-search.conf.in     # 插件注册配置模板
├── script/                        # install.sh / uninstall.sh / build-deb.sh
├── debian/                        # Debian 打包配置（control/rules/changelog/...）
├── tests/
│   ├── tst_desktopfile.cpp        # desktop 解析与信任校验单元测试
│   ├── tst_search.cpp             # 核心搜索流程测试（mock 数据源驱动）
│   └── tst_datasources.cpp        # VS Code / JetBrains 数据源解析测试
├── translations/
│   └── ide-project-search-plugin_zh_CN.ts # 组名等界面字符串翻译
└── src/
    ├── main.cpp                   # 入口：注册 D-Bus 服务、装配数据源
    ├── ideprojectsearch.*         # 核心服务：协议解析、搜索匹配、分组构建、打开动作
    ├── searchpluginadaptor.*      # D-Bus adaptor（SearchPlugin 接口）
    └── sources/
        ├── projectitem.h          # 项目条目数据结构
        ├── projectsource.h        # 历史项目数据源抽象接口
        ├── desktopfile.*          # desktop 文件定位与 Exec 解析助手
        ├── vscodeprojectsource.*  # VS Code / Insiders / OSS / VSCodium / Cursor
        └── jetbrainsprojectsource.* # JetBrains 系列 + Android Studio
```

## 数据来源

| IDE | 数据位置 |
|-----|----------|
| VS Code 系列 | 历史列表（`history.recentlyOpenedPathsList`）：1.118+ 在 `~/.vscode-shared/sharedStorage/state.vscdb`，旧版本在 `~/.config/<应用>/User/globalStorage/state.vscdb`；均读不到时回退 `storage.json` 的 Open Recent 菜单/老版 `openedPathsList`；另以 `User/workspaceStorage/*/workspace.json` 补齐 |
| JetBrains 系列 | `~/.config/JetBrains/<产品><版本>/options/recentProjects.xml`（Rider 另含 `recentSolutions.xml`），Android Studio 在 `~/.config/Google/AndroidStudio<版本>/`；同款产品只读最新版本目录（升级时历史打开记录自动迁移），最新版本无记录时才回退更旧版本 |
| 打开方式 | VS Code 系优先用 PATH 命令（code/cursor/...）；JetBrains 系优先匹配 desktop 文件 Exec，其次 JetBrains Toolbox `~/.local/share/JetBrains/Toolbox/apps/`；最后回退文件管理器。desktop 文件与 Exec 程序须通过来源与路径校验：仅信任当前用户或系统（root 且用户不可写）属主的文件，程序须为绝对路径且存在、可执行；文件名按组件边界匹配已知 IDE（`code` 不误中 `codeblocks`、`idea` 不误中 `ideal`） |

搜索匹配：项目名不区分大小写子串匹配；关键词 ≥3 字符时同时匹配完整路径（父目录名等）。同一路径被多个 IDE 记录时按来源各保留一条——作用域搜索只列出对应 IDE 的历史，全局搜索同时展示各 IDE 的记录（分组与打开行为不同，点击分别用对应 IDE 打开），仅同一来源内的重复路径保留首次出现。项目名统一附加完整路径显示（如 `demo (/project/java/demo)`）。

结果排序：按最近打开时间排序——VS Code 历史列表取所在 `state.vscdb` 的修改时间、工作区取 `workspaceStorage/*/state.vscdb` 的修改时间，JetBrains 取 `recentProjects.xml` 各条目的 `projectOpenTimestamp`（缺失时回退文件修改时间；XML 的 entry 顺序是插入序、不可当作最近使用序）；同一配置来源内保持 IDE 自身的最近使用（MRU）顺序。数量上限：每个 VS Code 变体历史最多 20 条、工作区补齐最多 40 条；JetBrains 每产品最多 20 条；最终结果整体截断 100 条。

界面顺序保持：dde-grand-search UI 对无权重条目按 `item` 字符串排序（`Utils::compareByWeight` 兜底分支），会破坏插件给出的排序。插件因此在 `item` 字段加等宽序号前缀（如 `000|/project/demo`，随排序后的序号），使 UI 的字符串序与插件的 MRU 序一致；用户点开时 `Action` 接口剥掉前缀再打开项目（无前缀的裸路径同样兼容）。

## 搜索语法

| 输入 | 行为 |
|------|------|
| `vsc` / `vscode` | 列出全部 VS Code 系列（含 Insiders/OSS/VSCodium/Cursor）历史项目 |
| `vsc <关键词>` | 在 VS Code 系列历史内按项目名/路径筛选 |
| `jh` / `jb` / `jetbrains` | 列出全部 JetBrains 系列（含 Android Studio）历史项目 |
| `jh <关键词>` | 在 JetBrains 系列历史内筛选 |

## 快捷命令配置

作用域快捷命令（`vsc` / `jh` 等）可在配置文件中自定义，未定义时采用程序默认值。
配置文件位于 `~/.config/deepin/dde-grand-search-ideproject/dde-grand-search-ideproject.conf`：

```ini
[Shortcut]
# 别名=作用域（作用域为插件内置数据源 id：vscode / jetbrains）
code=vscode
idea=jetbrains
```

- 配置只新增或重定义个别别名：未在配置中出现的默认快捷命令（`vsc` / `vscode` → VS Code 系列，`jh` / `jb` / `jetbrains` → JetBrains 系列）保持可用
- 仅当显式重定义同名别名时才改变其默认行为（如 `vsc=jetbrains` 把 `vsc` 重定向到 JetBrains 历史）
- 作用域值不是已注册数据源 id 时，该条配置被忽略
- 配置文件在每次搜索时读取，修改后无需重启插件

## 插件注册信息

| 字段 | 值 |
|------|-----|
| Name | org.deepin.grandsearch.ideproject |
| Mode | Auto（由 daemon 启动和守护） |
| Priority | 1（首次搜索时启动） |
| DBusService | org.deepin.grandsearch.ideproject |
| DBusAddress | /org/deepin/grandsearch/ideproject |
| DBusInterface | org.deepin.grandsearch.ideproject.SearchPlugin |
| InterfaceVersion | 1.0 |

## 构建与安装

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

或直接使用脚本：

```bash
./script/install.sh     # 构建 + 安装 + 重启搜索后端
./script/uninstall.sh   # 卸载 + 重启搜索后端
```

安装后重启搜索后端使插件生效：

```bash
killall dde-grand-search-daemon
```

## 单元测试

三个 Qt Test 测试套件覆盖核心逻辑，测试把 `HOME` 重定向到临时目录（每个用例
独立子目录），与真实用户配置完全隔离：

- **tst_desktopfile**：desktop 文件定位与 Exec 解析的信任校验（组件边界匹配、
  来源信任、程序路径校验、locate/parseExec 集成）
- **tst_search**：核心搜索流程，用 mock 数据源驱动 `IdeProjectSearch`——协议
  校验、名称/路径匹配规则、作用域前缀与快捷命令配置（含每次搜索重新读取）、
  去重语义、排序与 100 条截断、分组聚合与序号前缀、`Action` 打开
- **tst_datasources**：数据源解析，构造 VS Code storage.json（新版菜单 /
  老版 openedPathsList）/ state.vscdb（优先级、损坏与缺失回退、工作区补齐）
  与 JetBrains recentProjects.xml（additionalInfo 时间戳排序、`$USER_HOME$`
  展开、失效路径过滤、老格式、版本目录选择、Android Studio）夹具；涉及
  SQLite 的用例在缺少 QSQLITE 驱动时自动跳过

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`dpkg-buildpackage` 打包时也会自动运行测试（dh_auto_test）；不需要测试时可
`cmake -B build -DBUILD_TESTING=OFF`。

## Debian 打包

debian/ 目录已提供打包配置（debhelper 13、Qt6、按 multiarch 安装到
`/usr/lib/<multiarch>/dde-grand-search-daemon/plugins/searcher/`），构建脚本：

```bash
# 首次需安装构建依赖
sudo apt install dpkg-dev debhelper cmake qt6-base-dev qt6-tools-dev qt6-l10n-tools

./script/build-deb.sh   # 等价于 dpkg-buildpackage -b -us -uc，产物收集到 deb/ 目录
```

包名 `dde-grand-search-ideproject`，依赖 `dde-grand-search (>= 6.0.0)`；如需改名，
同步修改 `debian/control` 的 Source/Package 与 `debian/changelog` 首行。

版本与产物校验约定（`script/build-deb.sh` 自动执行，不满足即中止）：

- **版本同步**：`CMakeLists.txt` 的 `project(... VERSION x.y.z)` 与
  `debian/changelog` 首行版本必须一致（deb 文件名由 changelog 决定）；
- **deb 内附 changelog**：变更记录由 dh_installchangelogs 自动装入
  `/usr/share/doc/dde-grand-search-ideproject/changelog.gz`，构建后校验其存在；
- **sha256 校验和**：构建完成后生成 `deb/SHA256SUMS`，下载/分发 deb 时校验：

  ```bash
  cd deb && sha256sum -c SHA256SUMS
  ```

## 调试

启动插件并手动调用接口：

```bash
./build/ide-project-search-plugin &

dbus-send --session --print-reply \
  --dest=org.deepin.grandsearch.ideproject \
  /org/deepin/grandsearch/ideproject \
  org.deepin.grandsearch.ideproject.SearchPlugin.Search \
  'string:{"ver":"1.0","mID":"test001","cont":"dde"}'

dbus-send --session --print-reply \
  --dest=org.deepin.grandsearch.ideproject \
  /org/deepin/grandsearch/ideproject \
  org.deepin.grandsearch.ideproject.SearchPlugin.Action \
  'string:{"ver":"1.0","action":"openitem","item":"/path/to/project"}'
```

打开插件调试日志：

```bash
QT_LOGGING_RULES="org.deepin.grandsearch.ideproject.debug=true" ./build/ide-project-search-plugin
```

## 国际化

组名等界面字符串用 `tr()` / `QT_TRANSLATE_NOOP` 标记，翻译文件为 `translations/ide-project-search-plugin_zh_CN.ts`，构建时 lrelease 自动生成 .qm 并安装到 `/usr/share/ide-project-search-plugin/translations/`。品牌名（VS Code、IntelliJ IDEA 等）按惯例保留原文；新增字符串后执行 `cmake --build build --target ide-project-search-plugin_lupdate` 更新 .ts。项目名来自用户数据，不参与翻译；按系统语言自动加载对应 .qm，缺失时回退源码英文。

## 主体依赖需求（供 dde-grand-search 开发者）

以下两个需求依赖 dde-grand-search 主体（UI/daemon）支持，插件侧无法自行实现，
暂以 workaround 绕过；主体改进落地后应移除对应 workaround。

### 1. 结果顺序尊重插件输出（协议 weight 字段 / 免重排）

**现象**：UI 对无权重条目按 `item` 字符串排序（`src/grand-search/utils/utils.cpp`
的 `Utils::compareByWeight` 兜底分支，比较 `node1.item` 而非 `name`），会覆盖插件
返回的 MRU（最近打开）顺序。

**根因**：插件协议 V1.0 无 weight 字段；daemon convertor
（`src/dde-grand-search-daemon/searchplugin/convertors/convertorv1_0.cpp`）只映射
item/name/type/icon；插件结果全部无权重，落入 UI 的字符串排序兜底。

**插件 workaround**：`item` 字段加 3 位等宽序号前缀（`000|/path`），使 UI 字符串序
等于插件 MRU 序；`Action` 收到后剥掉前缀（无前缀裸路径兼容）。


### 2. 插件分组置顶显示

**现象**：插件分组（「VS Code」「JetBrains」等）按字母序排在所有内置分组之后。

**根因**：`src/grand-search/gui/exhibition/matchresult/matchwidget.cpp` 的
`m_groupHashShowOrder` 硬编码分组显示顺序，未知组按字母序排末尾；最佳匹配
白名单仅内置 searcher（file/ocr/fulltext/semantic/app/setting），第三方
searcher 无法置顶。


## 路线图

- [x] 项目骨架：D-Bus 服务、V1.0 协议（Search/Stop/Action）、搜索匹配与分组
- [x] VS Code / Insiders / OSS / VSCodium / Cursor 历史项目（menubar 数据 + openedPathsList + workspaceStorage）
- [x] JetBrains 系列历史项目（recentProjects.xml / recentSolutions.xml，含社区版 IdeaIC 与 Android Studio）
- [x] 打开动作：PATH 命令、desktop 文件 Exec、JetBrains Toolbox、文件管理器回退
- [x] desktop Exec 信任边界收紧：来源与路径校验（当前用户/系统属主、绝对路径可执行）
- [x] 组名与项目名国际化
- [x] Debian 打包（含版本同步检查、changelog 附包、sha256 校验和）
- [x] desktop 解析单元测试（Qt Test，构建与打包时运行）
- [x] 基础功能测试：核心搜索流程（mock 数据源）与数据源解析（临时 HOME 夹具）
- [ ] 主体支持 weight/免重排后移除 item 序号前缀（见「主体依赖需求」第 1 条）
- [ ] 主体支持分组置顶后移除相关 workaround（见「主体依赖需求」第 2 条）

## 许可
本项目采用 GPL-3.0 许可证发布。