#!/usr/bin/env bash
# Verify that every version literal matches CMakeLists.txt.
# Usage: ci/check-version.sh [expected-version]
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(sed -n 's/^project(bsinspector VERSION \([0-9][0-9.]*\).*/\1/p' \
    "$ROOT/CMakeLists.txt")
if [ -z "$VERSION" ]; then
    echo "error: cannot read the project version" >&2
    exit 1
fi
if [ $# -ge 1 ] && [ -n "$1" ] && [ "$1" != "$VERSION" ]; then
    echo "error: tag/expected version $1 does not match CMake version $VERSION" >&2
    exit 1
fi

IFS=. read -r MAJOR MINOR PATCH <<< "$VERSION"
RC_VERSION="$MAJOR,$MINOR,$PATCH,0"
fail=0
check() {
    if ! grep -qF -- "$2" "$1"; then
        echo "error: $1 does not contain: $2" >&2
        fail=1
    fi
}

check "$ROOT/res/app.rc" " FILEVERSION $RC_VERSION"
check "$ROOT/res/app.rc" " PRODUCTVERSION $RC_VERSION"
check "$ROOT/res/app.manifest" "version=\"$VERSION.0\""
check "$ROOT/src/ui/main.cpp" "Bitstream Inspector $VERSION"
check "$ROOT/src/ui/main.cpp" "码流分析器 $VERSION"
check "$ROOT/installer/bitstream-inspector.nsi" \
    "!define APP_VERSION \"$VERSION\""
check "$ROOT/CHANGELOG.md" "## [$VERSION]"
check "$ROOT/CHANGELOG.zh-CN.md" "## [$VERSION]"

# The repository README keeps a "current release" table.
ROOT_README="$ROOT/../README.md"
ROOT_README_ZH="$ROOT/../README.zh-CN.md"
for readme in "$ROOT_README" "$ROOT_README_ZH"; do
    if [ -f "$readme" ] &&
       ! grep -Eq "bitstream-inspector \| $VERSION \|" "$readme"; then
        echo "error: $readme does not list $VERSION as the current release" >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "version check failed (CMake version $VERSION)" >&2
    exit 1
fi
echo "version $VERSION is consistent"
