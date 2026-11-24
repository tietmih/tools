#!/usr/bin/env bash
# Cross-build a minimal LGPL FFmpeg (shared) for Windows x86-64 with mingw-w64.
# Usage: build_ffmpeg_mingw.sh <source-tarball> <build-root> <install-prefix>
set -euo pipefail

TARBALL=$1
BUILD_ROOT=$2
PREFIX=$3
# Resolve before the script changes directory into the source tree.
case "$BUILD_ROOT" in /*) ;; *) BUILD_ROOT="$PWD/$BUILD_ROOT" ;; esac
case "$PREFIX" in /*) ;; *) PREFIX="$PWD/$PREFIX" ;; esac

mkdir -p "$BUILD_ROOT" "$PREFIX"
# Accept the upstream tarball (ffmpeg-7.1.5/) and mirror tarballs
# (ffmpeg-n7.1.5/): derive the source directory from the archive itself.
TOP=$( { tar -tf "$TARBALL" || true; } | head -n 1 | cut -d/ -f1 )
if [ -z "$TOP" ]; then
    echo "error: cannot list $TARBALL" >&2
    exit 1
fi
SRC_DIR="$BUILD_ROOT/$TOP"
if [ ! -d "$SRC_DIR" ]; then
    tar -xf "$TARBALL" -C "$BUILD_ROOT"
fi

cd "$SRC_DIR"
if [ ! -f ffbuild/config.mak ]; then
    ./configure \
        --prefix="$PREFIX" \
        --arch=x86_64 --target-os=mingw32 \
        --cross-prefix=x86_64-w64-mingw32- \
        --enable-cross-compile \
        --enable-shared --disable-static \
        --disable-programs --disable-doc --disable-debug \
        --disable-everything \
        --enable-avformat --enable-avcodec --enable-avutil --enable-swscale \
        --enable-demuxer=mov,matroska,mpegts,h264,hevc \
        --enable-decoder=h264,hevc \
        --enable-parser=h264,hevc \
        --enable-protocol=file \
        --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb \
        --disable-network --disable-autodetect \
        --disable-iconv --disable-zlib --disable-bzlib --disable-lzma \
        --disable-sdl2 --disable-xlib \
        --extra-cflags="-O2" \
        --extra-ldflags="-static-libgcc"
fi
make -j"$(nproc)"
make install
