#!/bin/bash
# 构建 Debian 二进制包：产物收集到本项目 deb/ 目录，附 sha256 校验和
set -e
cd "$(dirname "$0")/.."

if ! command -v dpkg-buildpackage >/dev/null 2>&1; then
    echo "缺少构建工具，请先安装：sudo apt install dpkg-dev debhelper cmake qt6-base-dev qt6-tools-dev qt6-l10n-tools" >&2
    exit 1
fi

# 版本同步检查：CMakeLists.txt 的 project(... VERSION) 与 debian/changelog
# 首个版本必须一致（deb 文件名由 changelog 决定）
cmake_version=$(awk '/^project\(/ { inproj = 1 } inproj && /VERSION/ {
    match($0, /[0-9][0-9.]*/); print substr($0, RSTART, RLENGTH); exit }' CMakeLists.txt)
changelog_version=$(sed -n '1s/^.*(\([0-9][0-9.]*\)).*$/\1/p' debian/changelog)
if [ -z "$cmake_version" ] || [ -z "$changelog_version" ]; then
    echo "无法提取版本号：CMakeLists.txt=$cmake_version debian/changelog=$changelog_version" >&2
    exit 1
fi
if [ "$cmake_version" != "$changelog_version" ]; then
    echo "版本不一致：CMakeLists.txt 为 $cmake_version，debian/changelog 为 $changelog_version" >&2
    exit 1
fi
echo "版本检查通过：$cmake_version"

mkdir -p deb
dpkg-buildpackage -b -us -uc

# dpkg-buildpackage 默认把产物放在上级目录，收集 .deb 到 deb/
mv ../dde-grand-search-ideproject_*.deb deb/

deb=$(ls -1 deb/dde-grand-search-ideproject_*.deb | tail -1)

# deb 内容校验：changelog 由 dh_installchangelogs 自动安装
if ! dpkg-deb -c "$deb" | grep -q "usr/share/doc/dde-grand-search-ideproject/changelog.gz"; then
    echo "错误：deb 内未找到 changelog.gz" >&2
    exit 1
fi

# 生成 sha256 校验和（相对 deb/ 目录，可用 sha256sum -c 校验）
(cd deb && sha256sum dde-grand-search-ideproject_*.deb > SHA256SUMS)

echo "构建完成：$deb"
echo "校验和："
cat deb/SHA256SUMS
