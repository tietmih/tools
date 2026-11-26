#!/usr/bin/env bash
# Check or apply clang-format over this tool's C/C++ sources.
# Usage: ci/format.sh [--fix]
# CI pins clang-format 21.1.8; other versions may format slightly differently.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
REPO=$(cd "$ROOT/.." && pwd)
TOOL=${ROOT#"$REPO"/}
CLANG_FORMAT=${CLANG_FORMAT:-clang-format}
MODE=check

case "${1:-}" in
    ""|--check) ;;
    --fix) MODE=fix ;;
    *)
        echo "usage: $0 [--fix]" >&2
        exit 2
        ;;
esac

command -v "$CLANG_FORMAT" >/dev/null 2>&1 || {
    echo "error: clang-format not found (expected 21.1.8)" >&2
    exit 1
}

cd "$REPO"
mapfile -t files < <(git ls-files -- "$TOOL" | grep -E '\.(c|cc|cpp|h|hpp)$' || true)
if [ ${#files[@]} -eq 0 ]; then
    echo "no C/C++ sources under $TOOL"
    exit 0
fi

if [ "$MODE" = fix ]; then
    "$CLANG_FORMAT" -i --style=file "${files[@]}"
else
    "$CLANG_FORMAT" --dry-run --Werror --style=file "${files[@]}"
fi
