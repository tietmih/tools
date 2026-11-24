# Changelog

All notable changes to Bitstream Inspector. The tool is versioned and released
independently from the rest of the repository. Tags use the form
`bitstream-inspector-v<version>`.

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
