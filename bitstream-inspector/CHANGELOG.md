# Changelog

English | [简体中文](CHANGELOG.zh-CN.md)

All notable changes to Bitstream Inspector. The tool is versioned and released
independently from the rest of the repository. Tags use the form
`bitstream-inspector-v<version>`.

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
