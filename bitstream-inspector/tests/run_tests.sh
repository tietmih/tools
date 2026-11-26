#!/usr/bin/env bash
# Build the given build directory and run the ffmpeg-referenced regression
# suite (unit tests, parser comparison, syntax headers, frame decode).
# Usage: tests/run_tests.sh <build-dir>
# Environment:
#   BS_TEST_VECTORS   vector directory (created when missing)
#   FFMPEG/FFPROBE    ffmpeg tools (default: ffmpeg/ffprobe)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-"$ROOT/build-host"}
VECTORS=${BS_TEST_VECTORS:-"$ROOT/build-deps/vectors"}
FFMPEG=${FFMPEG:-ffmpeg}
FFPROBE=${FFPROBE:-ffprobe}
REF_FFMPEG=${REF_FFMPEG:-$FFMPEG}
HOST_FFMPEG="$ROOT/build-deps/ffmpeg-host"
if [ -x "$HOST_FFMPEG/bin/ffmpeg" ] && [ -x "$HOST_FFMPEG/bin/ffprobe" ]; then
    REF_FFMPEG="$HOST_FFMPEG/bin/ffmpeg"
    FFPROBE="$HOST_FFMPEG/bin/ffprobe"
    export LD_LIBRARY_PATH="$HOST_FFMPEG/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
CMAKE=${CMAKE:-cmake}

"$CMAKE" --build "$BUILD" --parallel "${JOBS:-$(nproc)}"
( cd "$BUILD" && ctest --output-on-failure )

need_vectors=0
if [ ! -f "$VECTORS/manifest.json" ]; then
    need_vectors=1
elif ! python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if d.get("gen_version", 1) >= 3 else 1)' "$VECTORS/manifest.json"; then
    need_vectors=1
fi
if [ "$need_vectors" -eq 1 ]; then
    python3 "$ROOT/tests/gen_vectors.py" --ffmpeg "$FFMPEG" --out "$VECTORS"
fi

python3 "$ROOT/tests/check_parse.py" --ffprobe "$FFPROBE" \
    --bsinfo "$BUILD/bsinfo" --vectors "$VECTORS"
python3 "$ROOT/tests/check_headers.py" --ffmpeg "$REF_FFMPEG" \
    --bsinfo "$BUILD/bsinfo" --vectors "$VECTORS"
python3 "$ROOT/tests/check_sei.py" --ffmpeg "$REF_FFMPEG" \
    --bsinfo "$BUILD/bsinfo" --vectors "$VECTORS"
python3 "$ROOT/tests/check_hrd.py" --ffmpeg "$REF_FFMPEG" \
    --bsinfo "$BUILD/bsinfo" --vectors "$VECTORS"
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
    python3 "$ROOT/tests/check_decode.py" --ffmpeg "$REF_FFMPEG" \
        --ffprobe "$FFPROBE" --mtest "$BUILD/mtest" --vectors "$VECTORS" \
        --expected-out "$VECTORS/expected.json"
    python3 "$ROOT/tests/check_ts.py" --mtest "$BUILD/mtest" \
        --vectors "$VECTORS"
    python3 "$ROOT/tests/check_quality.py" --mtest "$BUILD/mtest" \
        --ffmpeg "$FFMPEG" --vectors "$VECTORS"
    python3 "$ROOT/tests/check_yuvcmp.py" --yuvcmp "$BUILD/yuvcmp" \
        --ffmpeg "$FFMPEG" --vectors "$VECTORS"
else
    echo "mtest not built (media layer disabled); decode checks skipped"
fi
