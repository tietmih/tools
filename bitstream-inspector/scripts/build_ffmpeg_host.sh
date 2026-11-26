#!/usr/bin/env bash
# Build a minimal LGPL FFmpeg (shared) for the host, used by the Linux tests.
# Usage: build_ffmpeg_host.sh <source-tarball> <build-root> <install-prefix>
set -euo pipefail

TARBALL=$1
BUILD_ROOT=$2
PREFIX=$3
# Resolve before the script changes directory into the source tree.
case "$BUILD_ROOT" in /*) ;; *) BUILD_ROOT="$PWD/$BUILD_ROOT" ;; esac
case "$PREFIX" in /*) ;; *) PREFIX="$PWD/$PREFIX" ;; esac

mkdir -p "$BUILD_ROOT" "$PREFIX"
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
        --enable-shared --disable-static \
        --disable-doc --disable-debug \
        --disable-everything \
        --enable-ffmpeg --enable-ffprobe \
        --enable-avformat --enable-avcodec --enable-avutil --enable-swscale \
        --enable-avfilter \
        --enable-demuxer=mov,matroska,mpegts,h264,hevc,avi,flv,ivf,mxf,mpeg \
        --enable-decoder=h264,hevc \
        --enable-parser=h264,hevc \
        --enable-protocol=file \
        --enable-filter=select,scale,format,null \
        --enable-muxer=image2,null,rawvideo,avi,flv,mxf,mpeg \
        --enable-encoder=ppm,rawvideo \
        --enable-bsf=trace_headers,h264_metadata,hevc_metadata,h264_mp4toannexb,hevc_mp4toannexb \
        --disable-network --disable-autodetect \
        --disable-iconv --disable-zlib --disable-bzlib --disable-lzma \
        --disable-sdl2 --disable-xlib \
        --extra-cflags="-O2 -fPIC"
fi
make -j"$(nproc)"
make install
