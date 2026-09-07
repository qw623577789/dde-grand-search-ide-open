# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

project(ide-project-search-plugin
    VERSION 1.0.1
    DESCRIPTION "DDE Grand Search plugin for finding and opening IDE recent projects"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui DBus Sql LinguistTools)

set(SOURCES
    src/main.cpp
    src/ideprojectsearch.cpp
    src/ideprojectsearch.h
    src/searchpluginadaptor.cpp
    src/searchpluginadaptor.h
    src/sources/desktopfile.cpp
    src/sources/desktopfile.h
    src/sources/jetbrainsprojectsource.cpp
    src/sources/jetbrainsprojectsource.h
    src/sources/projectitem.h
    src/sources/projectsource.h
    src/sources/vscodeprojectsource.cpp
    src/sources/vscodeprojectsource.h
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::DBus
        Qt6::Sql
)

# 翻译：lupdate 从源码提取 tr()/QT_TRANSLATE_NOOP，lrelease 生成 .qm 随程序安装
set(TS_FILES translations/ide-project-search-plugin_zh_CN.ts)

qt_add_lrelease(${PROJECT_NAME}
    TS_FILES ${TS_FILES}
    QM_FILES_OUTPUT_VARIABLE QM_FILES
)
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_lrelease)

# 更新翻译文件：cmake --build build --target ${PROJECT_NAME}_lupdate
qt_add_lupdate(${PROJECT_NAME} TS_FILES ${TS_FILES} SOURCES ${SOURCES})

# 单元测试：ctest 运行（dpkg 打包时 dh_auto_test 同样会执行）
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 QUIET COMPONENTS Test)
    if(Qt6Test_FOUND)
        add_executable(tst_desktopfile
            tests/tst_desktopfile.cpp
            src/sources/desktopfile.cpp
        )
        target_include_directories(tst_desktopfile PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sources
        )
        target_link_libraries(tst_desktopfile PRIVATE Qt6::Core Qt6::Test)
        add_test(NAME tst_desktopfile COMMAND tst_desktopfile)

        # 核心搜索流程：mock 数据源驱动 IdeProjectSearch（协议/匹配/作用域/
        # 快捷配置/去重/排序/分组/打开），HOME 重定向隔离快捷配置文件
        add_executable(tst_search
            tests/tst_search.cpp
            src/ideprojectsearch.cpp
            src/sources/desktopfile.cpp
            src/sources/jetbrainsprojectsource.cpp
            src/sources/vscodeprojectsource.cpp
        )
        target_include_directories(tst_search PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sources
        )
        target_link_libraries(tst_search PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
        add_test(NAME tst_search COMMAND tst_search)

        # 数据源解析：HOME 重定向构造 VS Code storage.json / state.vscdb 与
        # JetBrains recentProjects.xml 夹具，覆盖解析优先级/过滤/排序/版本选择
        add_executable(tst_datasources
            tests/tst_datasources.cpp
            src/sources/desktopfile.cpp
            src/sources/jetbrainsprojectsource.cpp
            src/sources/vscodeprojectsource.cpp
        )
        target_include_directories(tst_datasources PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sources
        )
        target_link_libraries(tst_datasources PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
        add_test(NAME tst_datasources COMMAND tst_datasources)
    else()
        message(WARNING "Qt6 Test 组件未找到，跳过单元测试构建")
    endif()
endif()

include(GNUInstallDirs)

# 插件安装目录，与 dde-grand-search-daemon 的 searcher 插件目录一致
if(NOT DEFINED IDE_PROJECT_PLUGIN_DIR)
    set(IDE_PROJECT_PLUGIN_DIR
        ${CMAKE_INSTALL_FULL_LIBDIR}/dde-grand-search-daemon/plugins/searcher)
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${IDE_PROJECT_PLUGIN_DIR}
)

install(FILES ${QM_FILES}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/translations
)

# 生成插件注册配置（.conf），Exec 指向安装后的二进制
set(IDE_PROJECT_PLUGIN_EXEC "${IDE_PROJECT_PLUGIN_DIR}/${PROJECT_NAME}")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/ide-project-search.conf.in
    ${CMAKE_CURRENT_BINARY_DIR}/ide-project-search.conf
    @ONLY
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/ide-project-search.conf
    DESTINATION ${IDE_PROJECT_PLUGIN_DIR}
)
