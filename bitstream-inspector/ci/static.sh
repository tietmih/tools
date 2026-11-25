#!/usr/bin/env bash
# Static analysis: cppcheck for all portable sources, clang-tidy analyzer
# checks for the core, media, CLI and unit tests.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build-static"
CMAKE=${CMAKE:-cmake}
CPPCHECK=${CPPCHECK:-cppcheck}
CLANG_TIDY=${CLANG_TIDY:-clang-tidy}

command -v "$CPPCHECK" >/dev/null 2>&1 || {
    echo "error: cppcheck not found" >&2
    exit 1
}
command -v "$CLANG_TIDY" >/dev/null 2>&1 || {
    echo "error: clang-tidy not found" >&2
    exit 1
}

echo "== cppcheck =="
"$CPPCHECK" \
    --enable=warning,performance,portability \
    --std=c++17 \
    --inline-suppr \
    --suppress=missingIncludeSystem \
    --error-exitcode=1 \
    --template=gcc \
    -I "$ROOT/src" \
    "$ROOT/src" "$ROOT/tools" "$ROOT/tests/unit_tests.cpp"

echo "== clang-tidy (clang-analyzer) =="
HOST_FFMPEG="$ROOT/build-deps/ffmpeg-host"
if [ -f "$HOST_FFMPEG/include/libavformat/avformat.h" ]; then
    export CMAKE_PREFIX_PATH="$HOST_FFMPEG${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
fi
"$CMAKE" -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBS_BUILD_TESTS=ON

GCC_VER=$(g++ -dumpversion | cut -d. -f1)
GCC_INC=$(gcc -print-file-name=include)
EXTRA=(
    --extra-arg=-I"$GCC_INC"
    --extra-arg=-I/usr/include/c++/"$GCC_VER"
    --extra-arg=-I/usr/include/"$(g++ -dumpmachine)"/c++/"$GCC_VER"
)
FILES=(
    "$ROOT"/src/core/*.cpp
    "$ROOT"/src/media/*.cpp
    "$ROOT"/tools/*.cpp
    "$ROOT"/tests/unit_tests.cpp
)
for file in "${FILES[@]}"; do
    "$CLANG_TIDY" -p "$BUILD" "$file" \
        --checks='-*,clang-analyzer-*' \
        --warnings-as-errors='clang-analyzer-*' \
        "${EXTRA[@]}"
done
