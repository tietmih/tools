# Changelog

English | [简体中文](CHANGELOG.zh-CN.md)

All notable changes to Bitstream Inspector. The tool is versioned and released
independently from the rest of the repository. Tags use the form
`bitstream-inspector-v<version>`.

## [0.5.0] - 2026-11-27

### Added

- Quality GUI: load a reference YUV, compute per-frame PSNR/SSIM/MSE/MSAD in a
  background worker, show a quality chart and reference difference view, and
  export the per-frame metrics as CSV.
- Compare Streams: display-order/POC/time synchronization; subtraction,
  temperature, PSNR-clip, horizontal-split and vertical-split views; and up to
  four streams in a 2x2 grid, with the extra streams synchronized to the
  first.
- Standalone YUV Viewer for I420/I422/I444/P010/P210/P012/P212 with
  matrix/range controls, frame navigation, and an optional second input with
  side-by-side or difference views and PSNR/SSIM.
- `yuvcmp` CLI that compares two raw planar YUV files frame by frame.
- MSE/MSAD in the quality metrics, `mtest --quality` output and CSV.
- Selectable 1/2/5-second bitrate window in the bits chart.
- HDR10+ (SMPTE ST 2094-40) identification and full dynamic metadata parsing:
  processing windows, targeted display luminance, maxscl, average maxRGB,
  percentiles, tone mapping and saturation. Per-frame HDR10+ columns in the
  frame CSV and a status-bar summary.
- HDR metadata consistency checks (MaxCLL/MaxFALL vs the mastering display and
  HDR10+ vs PQ) in `bsinfo --hdr` and the conformance report.
- Y/Cb/Cr single-component video display.
- YUV export in the native layout or a selected I420/I422/I444/P010/P210
  format.

### Fixed

- Raw YUV export now writes >8-bit samples low-aligned, matching ffmpeg
  rawvideo and the reference YUV reader.
- HDR10+ prefix SEI is attached to the following picture and HEVC suffix SEI to
  the current picture instead of leaking onto the previous frame.

### Known limitations

- HEIC and MPEG-1 System containers are not enabled in the bundled FFmpeg
  build; VQM/NQI/VMAF metrics are not implemented.
- Difference metrics in the multi-stream compare window apply to the first two
  streams; the additional streams show normal pictures.

## [0.4.0] - 2026-11-26

### Added

- H.264/H.265 SEI parsing for MDCV, CLL, ATC, recovery point, T.35 user data
  and user-data UUIDs, with HDR10/HLG summaries, corrected payload names and
  H.265 prefix/suffix SEI support.
- H.264 VUI and H.265 VPS/SPS HRD storage, `buffering_period`/`pic_timing`
  parsing, a CPB0 occupancy model with buffering-period anchors, underflow and
  overflow reporting, a chart mode and CSV/CLI output.
- Native YUV plane decoding and export, a video Y/Cb/Cr/RGB pixel probe,
  preview-only BT.601/709/2020 and limited/full controls, WIC PNG/BMP export
  and raw planar YUV export.
- Lazy thumbnail strip with GOP boundaries, previous/next GOP navigation and
  exact raw-ES frame display, including B pictures.
- DPB/reference-list derivation for H.264 (MMCO/list modification) and H.265
  (RPS/long-term), a Refs tab, missing-reference reporting and `bsinfo --refs`.
- PSNR/SSIM comparison against a reference YUV, matching ffmpeg psnr/ssim;
  `mtest --ref/--quality` and CSV output.
- Per-AU bit allocation by NAL category, one-second window bitrate, stacked
  chart and CSV output.
- AVI, FLV, MXF and MPEG-PS demuxer support; AVI/FLV/MXF regression vectors.
- Slice/CTB/tile boundary overlay on the video tab.
- Standalone MPEG-TS scanner for 188/192/204-byte packets, PID/stream type,
  PCR jitter, continuity-counter errors and TEI/scrambling counters.
- `bsinfo --json` and `--batch` with a documented schema, plus `mtest --range`
  YUV export.
- H.264/H.265 Annex A conformance checks for picture size, MB/CTB rate, DPB
  and average bitrate.
- A basic side-by-side Compare Streams window.

### Fixed

- H.265 VPS parsing now covers ordering, layer sets and timing/HRD; NAL and VCL
  HRD CPB tables are read separately.
- MPEG-PS is treated as Annex B, and unsupported MXF vectors no longer fail
  vector generation.
- Raw ES decoding starts at the GOP and matches the selected frame, including
  B pictures; native YUV planes are byte-identical to ffmpeg for 8/10-bit.

### Known limitations

- The GUI quality chart/difference view and POC/time-bound comparison modes are
  not implemented; PSNR/SSIM and TS analysis are CLI-first.
- H.265 tile layout is implemented but the local x265 build did not produce a
  tile test vector; IVF/MPEG-PS are demuxer-only coverage.
- GUI YUV range export is current-frame only; use `mtest --range` for ranges.

## [0.3.1] - 2026-11-24

### Added

- GitHub Actions continuous integration. Every push and pull request builds
  the tool on Linux, compares parser output and decoded frames against FFmpeg,
  runs ASan/UBSan with a mutation-fuzz pass, cross-builds the Windows GUI and
  installer, and runs the binaries plus an installer round trip on Windows.
- Unit tests for the portable parser core (`tests/unit_tests.cpp`) and a
  scripted regression suite (`tests/`) that generates its own streams with
  FFmpeg; no media files are stored in git.
- Tag-driven releases: pushing `bitstream-inspector-v<version>` runs the tests,
  verifies that the tag, source version and bilingual release notes agree, and
  publishes the installer and `SHA256SUMS` as a GitHub Release.
- Dependency tracking: the bundled FFmpeg version and SHA-256 live in
  `ci/deps.env`; a weekly workflow compares them with upstream stable releases
  and opens a tracking issue, and Dependabot keeps GitHub Actions current.
- CI/CD expansion: Linux x64/arm64 tests, MSVC build, coverage report, Windows
  GUI/installer smoke tests, portable ZIP asset and a release dry-run switch.
- GitHub Actions are pinned to commit SHAs.

### Fixed

- H.264 POC types 1 and 2: `FrameNumOffset` is now carried across `frame_num`
  wrap-arounds within one IDR period. Previously the POC stopped increasing at
  the second wrap and repeated earlier values.

### Changed

- Bundled Windows FFmpeg upgraded from 7.1.5 to 9.1 (LGPL, still no GPL
  components); the Linux test reference now uses the same pinned version.
- Version bumped to 0.3.1 across CMake, Windows resources, About box and the
  installer.
- The release checklist is now tag-driven; creating release assets by hand is
  only a fallback when Actions is unavailable.

## [0.3.0] - 2026-11-24

### Added

- Runtime English/Simplified Chinese UI switch in the Language menu; the choice
  is persisted in `%APPDATA%\BitstreamInspector\settings.ini`.
- Bilingual documentation: English and `.zh-CN.md` versions of the README,
  changelog, screenshot notes and release notes, with language-switch links.
- Installer language selection (English/Simplified Chinese) and a bilingual
  license page.
- Chinese-aware frame-list column widths.

### Changed

- Version bumped to 0.3.0 across CMake, Windows resources, About box and the
  installer.

### Fixed

- Chart and hex views fall back to direct drawing when PrintWindow or
  WM_PRINTCLIENT supplies a DC without a compatible bitmap.
- Installer retries deleting the executable when the application was just
  closed; release checksum files use bare asset names.

### Verification

- Runtime language switch and persistence across restart verified on Windows.
- Installer install/launch/uninstall and both UI languages verified.
- Existing parser/decoder regression suite (ffprobe, trace_headers, PSNR,
  ASan/UBSan, fuzz) remains clean.

## [0.2.0] - 2026-11-24

### Added

- MP4/MOV, MPEG-TS/M2TS and Matroska/WebM input through the optional FFmpeg
  (LGPL) media layer; Annex B elementary streams continue to work.
- Decoded frame preview in a Hex/Video tab, decoded on a background thread.
  Container inputs decode the exact frame selected (PTS-matched); raw streams
  without timestamps show the nearest keyframe, labelled as such.
- Presentation timestamps in the frame list and CSV export.
- NSIS per-user Windows installer, application icon and 0.2.0 version resources.
- `bsinfo` command-line tool installed as an optional component.

### Fixed

- H.264 P slices read `num_ref_idx_active_override_flag`; previously every P
  slice was shifted by one bit, corrupting reference-list and QP fields.
- H.265 `num_pic_total_curr` was shadowed inside the slice-header block, making
  reference-list modification syntax unreachable; use the spec's `> 1` guard.
- VCL NALs whose header failed to parse are no longer attributed to the next
  frame.
- Parameter sets are range-validated and only activated after a complete parse,
  avoiding undefined behaviour and oversized allocations from malformed input.
- CSV time conversion, splitter drag offset/dead zone, navigation button refresh,
  File → Exit layout save, zero-AU open handling, EnsureVisible, high-resolution
  wheel deltas and invalid settings defaults.

### Verification

- Frame counts and I/P/B types vs `ffprobe` on eight H.264/H.265 streams.
- SPS/PPS/slice fields vs FFmpeg `trace_headers`, including the P-slice flag.
- PSNR = inf (pixel-identical) vs FFmpeg reference frames for real MP4/TS/MKV
  inputs (H.264 and H.265).
- ASan + UBSan over all test streams and 300 mutation-fuzz iterations, clean.
- Windows installer: silent install, launch of the installed app, silent
  uninstall, shortcuts and uninstall-registry entry verified.

### Known limitations

- Raw elementary streams have no timestamps; the Video tab shows the nearest
  keyframe rather than the exact frame.
- No thumbnail strip, slice/CTU overlay, DPB/GOP graph, HRD/CPB analysis or
  macroblock-level entropy decoding yet.
- Decoding is single-threaded per request.
- For container inputs the `Offset` column is the offset in the reconstructed
  Annex B stream, not the container file.

## [0.1.0] - 2026-11-24

- Initial Win32 analyzer: Annex B H.264/H.265 parsing, frame list, syntax tree,
  hex view, bitrate chart and CSV export.
