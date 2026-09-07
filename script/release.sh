#!/bin/bash
# 发布 Release：打 git tag、推送 main 与 tag、创建 GitHub Release 并上传
# deb 与 SHA256SUMS。前置：改动已提交、产物已由 script/build-deb.sh 构建。
set -e
cd "$(dirname "$0")/.."

if ! command -v gh >/dev/null 2>&1; then
    echo "缺少 gh CLI，请先安装并登录：sudo apt install gh && gh auth login" >&2
    exit 1
fi
if ! gh auth status >/dev/null 2>&1; then
    echo "gh 未登录，请先执行：gh auth login" >&2
    exit 1
fi

# 版本取自 debian/changelog 首行（与 deb 文件名、git tag 一致）
version=$(sed -n '1s/^.*(\([0-9][0-9.]*\)).*$/\1/p' debian/changelog)
if [ -z "$version" ]; then
    echo "无法从 debian/changelog 提取版本号" >&2
    exit 1
fi

# 发布说明：changelog 当前条目正文（"  * " 转 markdown 列表）
notes=$(awk 'NR==1{next} /^ -- /{exit} {sub(/^  \* /,"- "); print}' debian/changelog)

# 产物校验：先运行 script/build-deb.sh 生成
deb="deb/dde-grand-search-ideproject_${version}_amd64.deb"
sums="deb/SHA256SUMS"
if [ ! -f "$deb" ] || [ ! -f "$sums" ]; then
    echo "缺少发布产物（$deb / $sums），请先运行 ./script/build-deb.sh" >&2
    exit 1
fi

# 工作区须无未提交的已跟踪改动（避免 tag 与产物不一致；未跟踪文件不阻断）
if git status --porcelain | grep -qv '^??'; then
    echo "存在未提交的改动，请先提交：" >&2
    git status --short
    exit 1
fi

tag="v$version"
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    echo "本地 tag $tag 已存在" >&2
    exit 1
fi
if git ls-remote --exit-code origin "refs/tags/$tag" >/dev/null 2>&1; then
    echo "远端已存在 tag $tag" >&2
    exit 1
fi

branch=$(git rev-parse --abbrev-ref HEAD)
echo "发布版本 $version（tag $tag，分支 $branch）"

git tag -a "$tag" -m "$notes"
git push origin "$branch"
git push origin "$tag"

gh release create "$tag" "$deb" "$sums" \
    --title "dde-grand-search-ideproject $version" \
    --notes "$notes"

echo "发布完成：$(gh release view "$tag" --json url -q .url)"
