# bitstream-inspector

English | [简体中文](README.zh-CN.md)

[![CI](https://github.com/tietmih/tools/actions/workflows/ci.yml/badge.svg)](https://github.com/tietmih/tools/actions/workflows/ci.yml)

A Win32 H.264/H.265 bitstream analyzer: frame list, syntax tree, hex view,
bitrate chart and decoded frame preview in one window.

Writes from scratch against the published ITU-T H.264 and H.265 syntax
specifications. It is not derived from, and contains no code or assets from,
any commercial analyzer.

```text
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

- **Containers**: MP4/MOV, MPEG-TS/M2TS, Matroska/WebM, AVI, FLV, MXF and
  MPEG-PS (demuxed when the bundled FFmpeg supports the codec), plus Annex B
  elementary streams (`.h264`/`.264`/`.avc`/`.h265`/`.265`/`.hevc`).
- **Frame list**: decode order, picture type (IDR/I/P/B, HEVC CRA/BLA), POC, QP,
  `frame_num`, size, slice/NAL counts, file offset, presentation time.
- **Syntax tree**: every field of SPS/PPS/VPS/SEI/slice headers with exact bit
  ranges. Parameter sets are snapshotted per access unit, so early frames are
  decoded against the parameter set that was active at that point.
- **Hex view**: raw NAL bytes with the selected syntax field highlighted.
- **Video tab**: decodes the selected frame on a background thread. Exact frame
  for containers and raw elementary streams (decoded from the GOP start).
  Hovering shows Y/Cb/Cr and RGB values; colour matrix and range can be
  overridden for the preview only.
- **Charts**: frame-size bars with a picture-type strip, per-AU bit allocation
  with 1/2/5-second bitrate windows, quality metrics, or CPB0 occupancy. Click
  to jump.
- **References**: a Refs tab lists the selected frame's L0/L1 POCs, DPB count
  and missing references; double-clicking a reference jumps to that frame.
- **Conformance**: a View menu report checks picture size, MB/CTB rate, DPB
  and average bitrate against the signalled H.264/H.265 level, plus HDR
  metadata consistency (MaxCLL/MaxFALL vs mastering display, HDR10+ vs PQ).
- **YUV Viewer**: File > YUV Viewer opens a standalone raw planar YUV viewer
  with I420/I422/I444/P010/P210/P012/P212, matrix/range selection and frame
  navigation; a second file can be opened for side-by-side or difference
  comparison with PSNR/SSIM.
- **Quality**: File > Load Reference YUV computes per-frame PSNR/SSIM/MSE/MSAD for the
  open stream; the quality chart and a reference difference view show the
  result, and the metrics export as CSV. The reference must be planar
  I420/I422/I444/P010/P210/P012/P212 in display order, matching the decoded
  frame size.
- **Compare**: File > Compare Streams opens two files side by side, with
  display-order/POC/time synchronization and side-by-side, subtraction,
  temperature, PSNR-clip, horizontal-split and vertical-split views; diff
  modes show per-pair PSNR/SSIM.
- **Overlays**: an optional slice/CTB boundary overlay on the video tab; H.265
  tile boundaries come from the PPS layout.
- **Thumbnails**: lazy, cached thumbnail strip with GOP boundaries; prev/next
  GOP navigation.
- **Navigation**: previous/next frame, previous/next keyframe, previous/next
  GOP, keyboard shortcuts, draggable splitters, layout persisted in
  `%APPDATA%\BitstreamInspector\settings.ini`.
- **Bilingual UI**: switch between English and Simplified Chinese at runtime
  from the Language menu. The choice is persisted. Specification terms (NAL
  types, SPS/PPS/VPS, syntax field names, codec/profile names) stay in English.
- **Export**: frame/NAL lists and the CPB model as CSV (UTF-8 with BOM); the
  current frame as PNG/BMP or native planar YUV.

## Install on Windows

Download the installer (or the portable ZIP) from the
[latest release](https://github.com/tietmih/tools/releases/latest), or run the
packaging step below (the binaries are build artifacts and are not checked into
git):

```text
dist/BitstreamInspector-0.4.0-win64-setup.exe
dist/BitstreamInspector-0.4.0-win64-portable.zip
```

The installer is per-user and needs no administrator rights or UAC prompt:

- installs to `%LOCALAPPDATA%\Programs\BitstreamInspector`
- creates Start Menu shortcuts and an optional desktop shortcut
- registers an uninstaller in **Settings → Apps → Installed apps**
- bundles the FFmpeg DLLs, the MIT license and the FFmpeg LGPL license/build info
- requires 64-bit Windows 10/11

The uninstaller removes the application, shortcuts and registry entries; it
asks whether to keep your settings (`%APPDATA%\BitstreamInspector`) or delete
them too. Silent install/uninstall is supported with `/S` (and `/D=path` for a
custom install directory).

## Releases

The tool is released independently from the rest of the repository. The current
version is **0.4.0**; tags use the form `bitstream-inspector-v0.4.0` and every
tag has a matching GitHub Release with the Windows installer, portable ZIP,
CycloneDX SBOM and `SHA256SUMS` attached. Releases are built, tested and published by the CI
workflow. Release notes are provided in English and Simplified Chinese.

See [CHANGELOG.md](CHANGELOG.md) for the change history and
[docs/releases/](docs/releases/) for the notes attached to each release.

## Requirements

- Windows 10/11 x86-64 for the GUI.
- To build from Linux: `g++-mingw-w64-x86-64` (GCC 13 or newer) and CMake 3.16+.
- Optional media layer: the Windows build uses the bundled FFmpeg 9.1 shared
  libraries (LGPL, H.264/HEVC decoders, MP4/TS/MKV demuxers). On Linux the
  media layer is enabled when the system FFmpeg development packages
  (`libavformat-dev`, `libavcodec-dev`, `libavutil-dev`, `libswscale-dev`) are
  installed. Without FFmpeg the GUI still builds and works, but container input
  and the video tab are disabled. The bundled version and its SHA-256 are
  pinned in [ci/deps.env](ci/deps.env); the repository README describes the
  weekly dependency check and upgrade policy.

## Build

### Windows GUI (from Linux, cross-compile)

```bash
# 1. Build the optional FFmpeg layer once (see scripts/build_ffmpeg_mingw.sh)
bash scripts/build_ffmpeg_mingw.sh /path/to/ffmpeg-9.1.tar.xz \
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
matching DLLs (`avcodec-63`, `avformat-63`, `avutil-61`, `swscale-10`) are
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

### Build the Windows installer

The installer uses NSIS and packages an existing `build-win` tree (GUI, CLI and
the four required FFmpeg DLLs):

```bash
# NSIS is needed on the Linux host: sudo apt install nsis
bash installer/build_installer.sh

# optional: choose another build directory
bash installer/build_installer.sh /path/to/build-win
```

Output: `dist/BitstreamInspector-<version>-win64-setup.exe`. The installer is
per-user, creates Start Menu/desktop shortcuts, adds an entry under
**Settings → Apps**, and includes `Uninstall.exe`.

The installer executable is unsigned, so SmartScreen may ask for confirmation
the first time it is downloaded and run.

## Tests and CI

The repository does not store media files; the test suite generates its own
streams with FFmpeg and compares the results with FFmpeg as the reference.

```bash
# Host build, unit tests, ffprobe, trace_headers and decode comparisons
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
bash tests/run_tests.sh build-host

# ASan/UBSan build, sanitizer pass and mutation fuzz
bash ci/ffmpeg-host.sh   # once: pinned host FFmpeg, same version as shipped
bash ci/linux.sh asan

# Windows cross-build and installer (needs mingw-w64, nasm, NSIS)
bash ci/ffmpeg.sh        # once: pinned LGPL FFmpeg 9.1, SHA-256 verified
bash ci/windows.sh
```

`tests/run_tests.sh` needs `ffmpeg` and `ffprobe` on `PATH` for encoding the
vectors. When `ci/ffmpeg-host.sh` has built the pinned FFmpeg, the reference
decode, `ffprobe` and `trace_headers` checks use that same version as `mtest`,
so the pixel comparison is exact. It checks frame counts and picture types,
syntax fields, and decoded frames pixel by pixel on MP4/TS/MKV/AVI/FLV/MXF inputs.

CI runs the same scripts on every push and pull request on Linux x64 and arm64,
adds an MSVC build and coverage, and runs native Windows checks: CLI decode,
GUI startup and language switching, portable ZIP, and an installer round trip.
Pushing a `bitstream-inspector-v<version>` tag re-runs the suite and publishes
the release with the installer, portable ZIP and `SHA256SUMS`. See the
[repository README](../README.md#continuous-integration).

## Command-line tools

`bsinfo` validates the parser without the GUI:

```bash
bsinfo stream.h264                    # NAL list with per-NAL summary
bsinfo stream.h265 --frames           # frame table
bsinfo stream.h264 --tree 4           # syntax tree of NAL 4
bsinfo stream.h264 --csv-frames f.csv --csv-nals n.csv
bsinfo stream.h264 --cpb --csv-cpb cpb.csv  # CPB occupancy model
bsinfo stream.h264 --bits --csv-bits b.csv  # per-AU bit allocation
bsinfo stream.h264 --conformance       # Annex A limit checks
bsinfo stream.h264 --hdr               # HDR metadata summary/consistency
bsinfo stream.h264 --json             # machine-readable output
bsinfo --batch vectors/               # JSON array for a directory
```

`mtest` exercises the container/demux/decode path:

```bash
mtest movie.mp4                       # container info, packet map, frame timing
mtest movie.mkv --frame 10 --ppm out.ppm
mtest stream.h264 --frame 5 --yuv frame.yuv   # native planar YUV
mtest stream.h264 --range 0:30 --yuv range.yuv  # YUV frame range
mtest capture.ts --ts                 # MPEG-TS PID/PCR/continuity scan
mtest stream.h264 --ref ref.yuv --quality  # per-frame PSNR/SSIM
```

## Verification status

Checked against independent references, not just "it compiles". The checks
below are automated in `tests/` and run by CI on every push:

| Check | Result |
|---|---|
| Frame count and I/P/B counts vs `ffprobe` (12 streams: High, Baseline, 4:2:2, 4:4:4, interlaced/MBAFF, multi-slice, long GOP, HEVC Main, Main10, Main12, tiles/WPP) | identical |
| Syntactic fields vs FFmpeg `trace_headers` (SPS/PPS/slice: `num_ref_idx_active_override_flag`, POC, `slice_qp_delta`, tiles/WPP flags) | identical |
| POC wrap-around on a 120-frame GOP | 120 unique POCs, 0..238 |
| Decoded frames vs FFmpeg reference (MP4/TS/MKV/M2TS/AVI/FLV/MXF, fragmented MP4) | 8-bit pixel-identical; 10/12-bit within rounding tolerance |
| ASan + UBSan over all test streams, mutation fuzz (200 per CI run, 3000 nightly) | no findings |
| Windows runtime: container open, frame navigation, splitter drag, CSV export, layout persistence | verified by screenshot/automation |
| CPB0 model on x264/x265 CBR and injected underflow | flat occupancy, no underflow; a 100 KB SEI is detected from frame 10 |
| Raw ES exact frame vs ffmpeg (GOP start decode) | frame 5 pixel-identical, including B pictures |
| Native YUV export vs ffmpeg rawvideo (8/10-bit) | byte-identical planes |
| DPB/reference lists | L0/L1 populated on H.264/H.265 B-frame vectors; missing-reference path covered |
| Slice/tile overlay | multi-slice H.264 geometry checked; H.265 tile vector unavailable in the installed encoder |
| MPEG-TS scan | 188/192/204-byte packets, PID/stream type, PCR samples and injected CC errors detected |
| PSNR/SSIM | per-frame values and summaries match ffmpeg psnr/ssim within rounding |
| Conformance checks | out-of-level synthetic SPS flagged; valid H.265 vector clean |
| Quality GUI | reference YUV loading, PSNR/SSIM/MSE/MSAD, quality chart/difference view and CSV export |
| Side-by-side compare | display-order/POC/time sync and subtraction/temperature/PSNR-clip/split views with PSNR/SSIM |
| Thumbnail strip and GOP navigation | window creation checked by the Windows smoke test; thumbnail loading is manual |
| Windows installer: default, custom `/D=`, `/LANG=2052`, upgrade from 0.3.0, silent uninstall | files, shortcuts, HKCU entry, version hand-off and clean removal verified |
| Runtime language switch: English ↔ 简体中文, restart persistence | verified by automation/screenshot |

Not verified: manual feel of every mouse interaction, very large files, exotic
containers.

## Known limitations

- Raw elementary streams have no timestamps; the video tab decodes from the
  GOP start to show the exact selected picture, including B pictures. Field
  pictures are shown as separate pictures.
- No DPB/GOP graph or macroblock-level entropy decoding yet.
- GUI YUV range export is current-frame only; use `mtest --range` for ranges.
- The CPB model covers CPB0 from SPS VUI HRD; sub-picture, multiple CPBs and
  concatenation are not modelled. Missing delays are marked as estimated, and
  streams without HRD get a labelled average-bitrate estimate.
- Decoding is single-threaded per request.
- For container inputs the `Offset` column reports the offset inside the
  reconstructed Annex B stream, not the container file.
- 10/12-bit preview RGB conversion can differ from the FFmpeg CLI by a few
  levels; decode correctness is checked with a bounded pixel tolerance.
- SEI details cover HDR (MDCV/CLL/ATC), HDR10+ identification, recovery point,
  T.35 and user data; HDR10+ dynamic window metadata is not decoded, and other
  payload types are listed by type.
- Multi-PPS streams that alternate between several PPS ids are parsed with the
  most recently activated state.

## Layout

```text
src/core/     portable bitstream parsing (bit reader, NAL splitter, H.264/H.265 syntax, stream model)
src/media/    optional FFmpeg demux + decode layer (no FFmpeg types leak out)
src/ui/       Win32 GUI (frame list, syntax tree, hex view, video view, chart)
tools/        bsinfo and mtest command-line utilities
scripts/      build_ffmpeg_mingw.sh and make_icon.py
installer/    NSIS script, license page and build_installer.sh
res/          manifest, icon and resources
dist/         generated installer output (ignored by git)
```

## License

MIT, see the repository [LICENSE](../LICENSE).

The optional FFmpeg build is LGPL v2.1 (no GPL components enabled). Its license
text is in [third_party/ffmpeg-win64/COPYING.LGPLv2.1](third_party/ffmpeg-win64/COPYING.LGPLv2.1),
and the exact configure flags are recorded in
[third_party/ffmpeg-win64/BUILD-INFO.txt](third_party/ffmpeg-win64/BUILD-INFO.txt).
FFmpeg is linked dynamically so the DLLs can be replaced with compatible builds.
