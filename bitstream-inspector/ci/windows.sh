#!/usr/bin/env bash
# Cross-build the Windows GUI/CLI and the NSIS installer.
# Usage: ci/windows.sh [version]
# The media layer is enabled when third_party/ffmpeg-win64 is populated
# (ci/ffmpeg.sh); without it the GUI still builds but lacks containers/decode.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=${1:-}
BUILD="$ROOT/build-win"
CMAKE=${CMAKE:-cmake}
JOBS=${JOBS:-$(nproc)}

if [ ! -f "$ROOT/third_party/ffmpeg-win64/include/libavformat/avformat.h" ]; then
    echo "error: FFmpeg is missing; run ci/ffmpeg.sh first" >&2
    exit 1
fi

bash "$ROOT/ci/check-version.sh" "$VERSION"

CCACHE_ARG=()
if command -v ccache >/dev/null 2>&1; then
    CCACHE_ARG=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

"$CMAKE" -S "$ROOT" -B "$BUILD" \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
    -DCMAKE_BUILD_TYPE=Release "${CCACHE_ARG[@]}"
"$CMAKE" --build "$BUILD" --parallel "$JOBS"

if [ -n "$VERSION" ]; then
    export VERSION
fi
bash "$ROOT/installer/build_installer.sh" "$BUILD"
