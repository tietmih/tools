#!/usr/bin/env bash
# Host build and regression tests on Linux.
# Usage: ci/linux.sh [release|asan]
#   release  optimized build, unit tests, parser/decode comparison with FFmpeg
#   asan     ASan/UBSan build, unit tests, sanitizer pass and mutation fuzz
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODE=${1:-release}
CMAKE=${CMAKE:-cmake}
JOBS=${JOBS:-$(nproc)}
VECTORS=${BS_TEST_VECTORS:-$ROOT/build-deps/vectors}
export BS_TEST_VECTORS="$VECTORS"

# Prefer the pinned host FFmpeg (built by ci/ffmpeg-host.sh); fall back to a
# system installation when it is not present.
HOST_FFMPEG="$ROOT/build-deps/ffmpeg-host"
if [ -f "$HOST_FFMPEG/include/libavformat/avformat.h" ]; then
    export CMAKE_PREFIX_PATH="$HOST_FFMPEG${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    export LD_LIBRARY_PATH="$HOST_FFMPEG/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
CCACHE_ARG=()
if command -v ccache >/dev/null 2>&1; then
    CCACHE_ARG=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

case "$MODE" in
release)
    BUILD="$ROOT/build-ci-release"
    "$CMAKE" -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-Werror" "${CCACHE_ARG[@]}"
    "$CMAKE" --build "$BUILD" --parallel "$JOBS"
    bash "$ROOT/tests/run_tests.sh" "$BUILD"
    ;;
asan)
    BUILD="$ROOT/build-ci-asan"
    "$CMAKE" -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" "${CCACHE_ARG[@]}"
    "$CMAKE" --build "$BUILD" --parallel "$JOBS"
    ( cd "$BUILD" && ASAN_OPTIONS=detect_leaks=0 ctest --output-on-failure )
    if [ ! -f "$VECTORS/manifest.json" ]; then
        python3 "$ROOT/tests/gen_vectors.py" --out "$VECTORS"
    fi
    for stream in "$VECTORS"/*.h264 "$VECTORS"/*.h265; do
        [ -f "$stream" ] || continue
        ASAN_OPTIONS=detect_leaks=0 "$BUILD/bsinfo" "$stream" > /dev/null
    done
    python3 "$ROOT/tests/fuzz.py" --bsinfo "$BUILD/bsinfo" \
        --vectors "$VECTORS" --iterations "${BS_FUZZ_ITERATIONS:-200}"
    ;;
*)
    echo "usage: $0 [release|asan]" >&2
    exit 2
    ;;
esac
