#!/bin/bash
# 构建 Debian 二进制包：产物收集到本项目 deb/ 目录（deb/dde-grand-search-ideproject_*.deb）
set -e
cd "$(dirname "$0")/.."

if ! command -v dpkg-buildpackage >/dev/null 2>&1; then
    echo "缺少构建工具，请先安装：sudo apt install dpkg-dev debhelper cmake qt6-base-dev qt6-tools-dev qt6-l10n-tools" >&2
    exit 1
fi

mkdir -p deb
dpkg-buildpackage -b -us -uc

# dpkg-buildpackage 默认把产物放在上级目录，收集 .deb 到 deb/
mv ../dde-grand-search-ideproject_*.deb deb/

echo "构建完成：$(ls -1 deb/dde-grand-search-ideproject_*.deb | tail -1)"
