#!/usr/bin/env bash
# Build the pinned FFmpeg for the host so the Linux tests exercise the same
# version that ships on Windows. CI caches build-deps/ffmpeg-host.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=/dev/null
. "$ROOT/ci/deps.env"
: "${FFMPEG_VERSION:?missing FFMPEG_VERSION in ci/deps.env}"
: "${FFMPEG_SHA256:?missing FFMPEG_SHA256 in ci/deps.env}"

PREFIX="${FFMPEG_HOST_PREFIX:-$ROOT/build-deps/ffmpeg-host}"
BUILD_ROOT="${FFMPEG_HOST_BUILD_ROOT:-$ROOT/build-deps/ffmpeg-host-build}"
TARBALL="${FFMPEG_HOST_TARBALL:-$ROOT/build-deps/ffmpeg-$FFMPEG_VERSION.tar.xz}"

if [ -f "$PREFIX/include/libavformat/avformat.h" ] &&
   ls "$PREFIX"/lib/libavformat.so* >/dev/null 2>&1 &&
   [ -x "$PREFIX/bin/ffmpeg" ] && [ -x "$PREFIX/bin/ffprobe" ]; then
    echo "host FFmpeg is already installed in $PREFIX"
    exit 0
fi

mkdir -p "$ROOT/build-deps"
if [ ! -f "$TARBALL" ]; then
    echo "downloading FFmpeg $FFMPEG_VERSION"
    curl -fsSL --retry 3 -o "$TARBALL" \
        "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
fi
echo "$FFMPEG_SHA256  $TARBALL" | sha256sum -c -

bash "$ROOT/scripts/build_ffmpeg_host.sh" "$TARBALL" "$BUILD_ROOT" "$PREFIX"
