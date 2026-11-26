#!/usr/bin/env bash
# Build with coverage instrumentation, run the regression suite and produce
# coverage.xml / HTML for the CI artifact.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build-ci-coverage"
VECTORS=${BS_TEST_VECTORS:-$ROOT/build-deps/vectors}
OUT="$ROOT/build-deps/coverage"
CMAKE=${CMAKE:-cmake}
JOBS=${JOBS:-$(nproc)}
export BS_TEST_VECTORS="$VECTORS"

HOST_FFMPEG="$ROOT/build-deps/ffmpeg-host"
if [ -f "$HOST_FFMPEG/include/libavformat/avformat.h" ]; then
    export CMAKE_PREFIX_PATH="$HOST_FFMPEG${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    export LD_LIBRARY_PATH="$HOST_FFMPEG/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

"$CMAKE" -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="--coverage -O0 -g" \
    -DCMAKE_EXE_LINKER_FLAGS="--coverage"
"$CMAKE" --build "$BUILD" --parallel "$JOBS"
( cd "$BUILD" && ctest --output-on-failure )

need_vectors=0
if [ ! -f "$VECTORS/manifest.json" ]; then
    need_vectors=1
elif ! python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if d.get("gen_version", 1) >= 3 else 1)' "$VECTORS/manifest.json"; then
    need_vectors=1
fi
if [ "$need_vectors" -eq 1 ]; then
    python3 "$ROOT/tests/gen_vectors.py" --out "$VECTORS"
fi
FFMPEG=${FFMPEG:-ffmpeg}
FFPROBE=${FFPROBE:-ffprobe}
REF_FFMPEG=${REF_FFMPEG:-$FFMPEG}
if [ -x "$HOST_FFMPEG/bin/ffmpeg" ] && [ -x "$HOST_FFMPEG/bin/ffprobe" ]; then
    REF_FFMPEG="$HOST_FFMPEG/bin/ffmpeg"
    FFPROBE="$HOST_FFMPEG/bin/ffprobe"
fi
python3 "$ROOT/tests/check_parse.py" --bsinfo "$BUILD/bsinfo" \
    --ffprobe "$FFPROBE" --vectors "$VECTORS"
python3 "$ROOT/tests/check_headers.py" --bsinfo "$BUILD/bsinfo" \
    --ffmpeg "$FFMPEG" --vectors "$VECTORS"
python3 "$ROOT/tests/check_sei.py" --bsinfo "$BUILD/bsinfo" \
    --ffmpeg "$REF_FFMPEG" --vectors "$VECTORS"
python3 "$ROOT/tests/check_hrd.py" --bsinfo "$BUILD/bsinfo" \
    --ffmpeg "$REF_FFMPEG" --vectors "$VECTORS"
python3 "$ROOT/tests/check_cli.py" --bsinfo "$BUILD/bsinfo" \
    --vectors "$VECTORS"
python3 "$ROOT/tests/check_bits.py" --bsinfo "$BUILD/bsinfo" \
    --vectors "$VECTORS"
python3 "$ROOT/tests/check_overlay.py" --bsinfo "$BUILD/bsinfo" \
    --vectors "$VECTORS"
python3 "$ROOT/tests/check_refs.py" --bsinfo "$BUILD/bsinfo" \
    --vectors "$VECTORS"
python3 "$ROOT/tests/check_cpb.py" --bsinfo "$BUILD/bsinfo" \
    --vectors "$VECTORS"
if [ -x "$BUILD/mtest" ]; then
    python3 "$ROOT/tests/check_decode.py" --mtest "$BUILD/mtest" \
        --ffmpeg "$REF_FFMPEG" --ffprobe "$FFPROBE" --vectors "$VECTORS" \
        --expected-out "$VECTORS/expected.json"
    python3 "$ROOT/tests/check_ts.py" --mtest "$BUILD/mtest" \
        --vectors "$VECTORS"
    python3 "$ROOT/tests/check_quality.py" --mtest "$BUILD/mtest" \
        --ffmpeg "$FFMPEG" --vectors "$VECTORS"
    python3 "$ROOT/tests/check_yuvcmp.py" --yuvcmp "$BUILD/yuvcmp" \
        --ffmpeg "$FFMPEG" --vectors "$VECTORS"
    python3 "$ROOT/tests/check_yuv_convert.py" --mtest "$BUILD/mtest" \
        --yuvcmp "$BUILD/yuvcmp" --ffmpeg "$FFMPEG" --vectors "$VECTORS"
fi

if ! command -v gcovr >/dev/null 2>&1; then
    echo "error: gcovr is required for the coverage report" >&2
    exit 1
fi
mkdir -p "$OUT"
MIN_COVERAGE=${MIN_COVERAGE:-60}
gcovr --root "$ROOT" --filter "$ROOT/src" --object-directory "$BUILD" \
    --exclude '.*/CMakeFiles/.*' \
    --gcov-ignore-errors=no_working_dir_found \
    --fail-under-line "$MIN_COVERAGE" \
    --xml-pretty --output "$OUT/coverage.xml" \
    --html-details "$OUT/index.html"
echo "coverage report: $OUT/index.html (minimum line coverage: $MIN_COVERAGE%)"
