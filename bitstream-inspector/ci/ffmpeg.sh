#!/usr/bin/env bash
# Build the bundled Windows FFmpeg used by the media layer.
# Downloads the pinned upstream tarball, verifies its SHA-256 and installs the
# shared libraries into third_party/ffmpeg-win64 (the directory CI caches).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=/dev/null
. "$ROOT/ci/deps.env"
: "${FFMPEG_VERSION:?missing FFMPEG_VERSION in ci/deps.env}"
: "${FFMPEG_SHA256:?missing FFMPEG_SHA256 in ci/deps.env}"
VERSION=$FFMPEG_VERSION
SHA256=$FFMPEG_SHA256
PREFIX="$ROOT/third_party/ffmpeg-win64"
BUILD_ROOT="${FFMPEG_BUILD_ROOT:-$ROOT/build-deps/ffmpeg-build}"
TARBALL="${FFMPEG_TARBALL:-$ROOT/build-deps/ffmpeg-$VERSION.tar.xz}"

if [ -f "$PREFIX/include/libavformat/avformat.h" ] &&
   [ -f "$PREFIX/lib/libavformat.dll.a" ] &&
   ls "$PREFIX"/bin/avformat-*.dll >/dev/null 2>&1; then
    echo "FFmpeg is already installed in $PREFIX"
    exit 0
fi

mkdir -p "$ROOT/build-deps"
if [ ! -f "$TARBALL" ]; then
    echo "downloading FFmpeg $VERSION"
    curl -fsSL --retry 3 -o "$TARBALL" \
        "https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
fi
echo "$SHA256  $TARBALL" | sha256sum -c -

bash "$ROOT/scripts/build_ffmpeg_mingw.sh" "$TARBALL" "$BUILD_ROOT" "$PREFIX"
