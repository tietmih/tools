#!/usr/bin/env bash
# Build the Windows installer from an existing build-win/ tree.
# Usage: installer/build_installer.sh [build-dir]
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PROJECT="$ROOT/bitstream-inspector"
BUILD_DIR=${1:-"$PROJECT/build-win"}
VERSION=${VERSION:-0.3.0}
STAGE="$PROJECT/installer/staging"
DIST="$ROOT/dist"
APP_ICON="$PROJECT/res/app.ico"
OUT="$DIST/BitstreamInspector-${VERSION}-win64-setup.exe"

required=(bsinspector.exe bsinfo.exe avcodec-61.dll avformat-61.dll avutil-59.dll swscale-8.dll)
for f in "${required[@]}"; do
    if [ ! -f "$BUILD_DIR/$f" ]; then
        echo "error: missing $BUILD_DIR/$f" >&2
        echo "build the GUI first: cmake --build $BUILD_DIR -j" >&2
        exit 1
    fi
done

for f in "$ROOT/LICENSE" \
         "$PROJECT/third_party/ffmpeg-win64/COPYING.LGPLv2.1" \
         "$PROJECT/third_party/ffmpeg-win64/BUILD-INFO.txt" \
         "$PROJECT/third_party/ffmpeg-win64/BUILD-INFO.zh-CN.txt" \
         "$PROJECT/README.md" \
         "$PROJECT/README.zh-CN.md"; do
    if [ ! -f "$f" ]; then
        echo "error: missing $f" >&2
        exit 1
    fi
done

if [ ! -f "$APP_ICON" ]; then
    python3 -I "$PROJECT/scripts/make_icon.py"
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/LICENSES" "$DIST"
cp "$BUILD_DIR"/{bsinspector.exe,bsinfo.exe,avcodec-61.dll,avformat-61.dll,avutil-59.dll,swscale-8.dll} "$STAGE/"
cp "$PROJECT/README.md" "$STAGE/README.md"
cp "$PROJECT/README.zh-CN.md" "$STAGE/README.zh-CN.md"
cp "$ROOT/LICENSE" "$STAGE/LICENSES/MIT.txt"
cp "$PROJECT/third_party/ffmpeg-win64/COPYING.LGPLv2.1" "$STAGE/LICENSES/FFmpeg-LGPLv2.1.txt"
cp "$PROJECT/third_party/ffmpeg-win64/BUILD-INFO.txt" "$STAGE/LICENSES/FFmpeg-BUILD-INFO.txt"
cp "$PROJECT/third_party/ffmpeg-win64/BUILD-INFO.zh-CN.txt" "$STAGE/LICENSES/FFmpeg-BUILD-INFO.zh-CN.txt"

MAKENSIS=${MAKENSIS:-}
if [ -z "$MAKENSIS" ]; then
    if command -v makensis >/dev/null 2>&1; then
        MAKENSIS=$(command -v makensis)
    elif [ -x /tmp/nsis-local/usr/bin/makensis ]; then
        MAKENSIS=/tmp/nsis-local/usr/bin/makensis
        export NSISDIR=/tmp/nsis-local/usr/share/nsis
    else
        echo "error: makensis not found." >&2
        echo "install NSIS (sudo apt install nsis) or set MAKENSIS and NSISDIR." >&2
        exit 1
    fi
fi

cd "$PROJECT/installer"
"$MAKENSIS" -V2 \
    -DAPP_VERSION="$VERSION" \
    -DOUT_FILE="$OUT" \
    -DAPP_ICON="$APP_ICON" \
    -DSTAGE_DIR="$STAGE" \
    bitstream-inspector.nsi

( cd "$DIST" && sha256sum "$(basename "$OUT")" > SHA256SUMS )
ls -lh "$OUT" "$DIST/SHA256SUMS"
echo "installer ready: $OUT"
