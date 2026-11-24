# tools

Personal open-source toolbox. Small, focused utilities for embedded Linux and
multimedia work — one directory per project, each with its own build files and
README.

## Projects

| Project | What it does | Platform |
|---|---|---|
| [bitstream-inspector](bitstream-inspector/) | H.264/H.265 bitstream analyzer with a Win32 GUI: frame list, syntax tree, hex view, bitrate chart and decoded frame preview. Reads Annex B elementary streams and MP4/MOV/TS/MKV containers. | Windows (cross-built from Linux with mingw-w64) |

## Layout

```
.
├── bitstream-inspector/     # one project per directory
│   ├── src/core/            # portable C++17 bitstream parsing (no Qt, no Win32)
│   ├── src/media/           # optional FFmpeg demux/decode layer
│   ├── src/ui/              # Win32 GUI
│   ├── tools/               # command-line validation utilities
│   ├── scripts/             # build helper scripts
│   └── res/                 # Windows resources
├── LICENSE
└── README.md
```

Conventions used by every project in this repository:

- Language: C++17 (user-facing apps) or Python 3 (small scripts).
- No build outputs, third-party binaries or test media in git — see `.gitignore`.
- Each project README documents how to build it and what has actually been
  verified (versus what was only compiled).

## Building

Each project is self-contained. Start with its README:

```bash
cd bitstream-inspector
cat README.md
```

## License

Repository code is MIT licensed, see [LICENSE](LICENSE).

Third-party components keep their own licenses; for example
`bitstream-inspector` links against FFmpeg under the LGPL v2.1 (built without
GPL components) — see `bitstream-inspector/third_party/ffmpeg-win64/COPYING.LGPLv2.1`.
