# bitstream-inspector

A Win32 H.264/H.265 bitstream analyzer: frame list, syntax tree, hex view,
bitrate chart and decoded frame preview in one window.

Writes from scratch against the published ITU-T H.264 and H.265 syntax
specifications. It is not derived from, and contains no code or assets from,
any commercial analyzer.

```
┌──────────────────────────────────────────────────────────────────────────┐
│ File  View  Help                                                         │
│ [Open...] [< Prev] [Next >] [Prev Key] [Next Key]  MP4  H.264 640x360 …  │
├────────────────────────────┬─────────────────────────────────────────────┤
│  #  Type POC QP Size Time  │  ▼ [0] SPS (Sequence Parameter Set)         │
│  0  IDR   0 27 8312    0   │      nal_unit_type = 7   [0..4]             │
│  1  P     6 27 4387  120   │      ▼ seq_parameter_set_rbsp               │
│  2  B     2 34 2619   40   │          profile_idc = 100  [0..7]          │
│  3  B     4 34 2437   80   │          level_idc = 30     [16..23]        │
│  4  P    12 27 4112  240   │                                             │
│  …                         ├─────────────────────────────────────────────┤
│                            │ [Hex] [Video]                               │
│                            │   ┌─────────────────────────────────────┐   │
│                            │   │        decoded frame                │   │
├────────────────────────────┴───┴─────────────────────────────────────┴───┤
│ frame size chart: ▁▄▂▁▃▅▂▁▄▃ … (click to jump)                          │
├──────────────────────────────────────────────────────────────────────────┤
│ proper_h264.mp4 | MP4 1s | H.264/AVC | 640x360 | 25 fps | 25 frames      │
└──────────────────────────────────────────────────────────────────────────┘
```

See [docs/screenshot.md](docs/screenshot.md) for how to capture a real screenshot.

## Features

- **Containers**: MP4/MOV, MPEG-TS/M2TS, Matroska/WebM, and Annex B elementary
  streams (`.h264`/`.264`/`.avc`/`.h265`/`.265`/`.hevc`).
- **Frame list**: decode order, picture type (IDR/I/P/B, HEVC CRA/BLA), POC, QP,
  `frame_num`, size, slice/NAL counts, file offset, presentation time.
- **Syntax tree**: every field of SPS/PPS/VPS/SEI/slice headers with exact bit
  ranges. Parameter sets are snapshotted per access unit, so early frames are
  decoded against the parameter set that was active at that point.
- **Hex view**: raw NAL bytes with the selected syntax field highlighted.
- **Video tab**: decodes the selected frame on a background thread. Exact frame
  for container inputs (PTS-matched); nearest keyframe for raw streams that
  carry no timestamps.
- **Charts**: per-frame size bar chart with picture-type strip; click to jump.
- **Navigation**: previous/next frame, previous/next keyframe, keyboard
  shortcuts, draggable splitters, layout persisted in
  `%APPDATA%\BitstreamInspector\settings.ini`.
- **Export**: frame list and NAL list as CSV (UTF-8 with BOM).

## Requirements

- Windows 10/11 x86-64 for the GUI.
- To build from Linux: `g++-mingw-w64-x86-64` (GCC 13 or newer) and CMake 3.16+.
- Optional media layer: FFmpeg 7.1 shared libraries (LGPL build, H.264/HEVC
  decoders, MP4/TS/MKV demuxers). Without them the GUI still builds and works,
  but container input and the video tab are disabled.

## Build

### Windows GUI (from Linux, cross-compile)

```bash
# 1. Build the optional FFmpeg layer once (see scripts/build_ffmpeg_mingw.sh)
bash scripts/build_ffmpeg_mingw.sh /path/to/ffmpeg-n7.1.5.tar.gz \
     /tmp/ffmpeg-build "$PWD/third_party/ffmpeg-win64"

# 2. Configure and build
cmake -S . -B build-win \
      -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
      -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j
```

`build-win/bsinspector.exe` is the GUI. When the FFmpeg layer is present, the
matching DLLs (`avcodec-61`, `avformat-61`, `avutil-59`, `swscale-8`) are
copied next to the executable automatically; ship them together.

### Native build (Linux, core + CLI only)

```bash
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host -j
```

The core parser and the `bsinfo` CLI are platform-independent. The Win32 GUI
and the FFmpeg media layer are only built on Windows targets.

### MSVC

The same CMake project builds with MSVC (`cmake -B build-msvc -G "Visual
Studio 17 2022"`); the FFmpeg layer requires matching MSVC import libraries.

## Command-line tools

`bsinfo` validates the parser without the GUI:

```bash
bsinfo stream.h264                    # NAL list with per-NAL summary
bsinfo stream.h265 --frames           # frame table
bsinfo stream.h264 --tree 4           # syntax tree of NAL 4
bsinfo stream.h264 --csv-frames f.csv --csv-nals n.csv
```

`mtest` exercises the container/demux/decode path:

```bash
mtest movie.mp4                       # container info, packet map, frame timing
mtest movie.mkv --frame 10 --ppm out.ppm
```

## Verification status

Checked against independent references, not just "it compiles":

| Check | Result |
|---|---|
| Frame count and I/P/B counts vs `ffprobe` (8 streams: High, Baseline, 4:2:2, multi-slice, long GOP, HEVC Main, Main10, WPP) | identical |
| Syntactic fields vs FFmpeg `trace_headers` (SPS/PPS/slice: `num_ref_idx_active_override_flag`, POC, `slice_qp_delta`, tiles/WPP flags) | identical |
| POC wrap-around on a 120-frame GOP | 120 unique POCs, 0..238 |
| Decoded frames vs FFmpeg reference, real encoder output (H.264/H.265 in MP4/TS/MKV) | PSNR = inf (pixel-identical) |
| ASan + UBSan over all test streams, then 300 mutation-fuzz iterations | no findings |
| Windows runtime: container open, frame navigation, splitter drag, CSV export, layout persistence | verified by screenshot/automation |

Not verified: manual feel of every mouse interaction, very large files, exotic
containers.

## Known limitations

- Raw elementary streams have no timestamps, so the video tab shows the nearest
  keyframe (labelled) instead of the exact frame. Container inputs are exact.
- No thumbnail strip, slice/CTU overlay, DPB/GOP graph, HRD/CPB analysis, or
  macroblock-level entropy decoding yet.
- Decoding is single-threaded per request.
- For container inputs the `Offset` column reports the offset inside the
  reconstructed Annex B stream, not the container file.
- H.265 SEI payloads are listed by type but not parsed in detail.
- Multi-PPS streams that alternate between several PPS ids are parsed with the
  most recently activated state.

## Layout

```
src/core/     portable bitstream parsing (bit reader, NAL splitter, H.264/H.265 syntax, stream model)
src/media/    optional FFmpeg demux + decode layer (no FFmpeg types leak out)
src/ui/       Win32 GUI (frame list, syntax tree, hex view, video view, chart)
tools/        bsinfo and mtest command-line utilities
scripts/      build_ffmpeg_mingw.sh
res/          manifest and resources
```

## License

MIT, see the repository [LICENSE](../LICENSE).

The optional FFmpeg build is LGPL v2.1 (no GPL components enabled). Its license
text is in [third_party/ffmpeg-win64/COPYING.LGPLv2.1](third_party/ffmpeg-win64/COPYING.LGPLv2.1),
and the exact configure flags are recorded in
[third_party/ffmpeg-win64/BUILD-INFO.txt](third_party/ffmpeg-win64/BUILD-INFO.txt).
FFmpeg is linked dynamically so the DLLs can be replaced with compatible builds.
