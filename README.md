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
│   ├── installer/           # NSIS packaging
│   └── res/                 # Windows resources
├── LICENSE
└── README.md
```

Conventions used by every project in this repository:

- Language: C++17 (user-facing apps) or Python 3 (small scripts).
- No build outputs, third-party binaries or test media in git — see `.gitignore`.
- Each project README documents how to build it and what has actually been
  verified (versus what was only compiled).
- Each project is versioned independently; every released version has a tag and
  a matching GitHub Release (see below).

## Versioning and releases

There is no repository-wide version. Each tool follows its own SemVer timeline.

Rules:

- **Version** lives in the tool (for `bitstream-inspector`: `CMakeLists.txt`
  `project(... VERSION ...)`, the Windows version resource, the installer
  metadata and the application About box). Bump it in the same commit as the
  change.
- **Tag**: annotated tag named `<tool>-v<major>.<minor>.<patch>`, for example
  `bitstream-inspector-v0.2.0`. Never use a bare version tag that could collide
  between tools.
- **Release**: every pushed tag must have a matching GitHub Release with the
  same name. A tag without a release is not a finished release.
- **Assets**: attach the tool's built packages (Windows installer; optional
  portable archive) plus a `SHA256SUMS` file. Binary artifacts live only in the
  Release, never in git — `dist/` is ignored.
- **Notes**: list user-visible changes, verification performed, known
  limitations and asset SHA-256 values. Keep a per-tool `CHANGELOG.md` and a
  release note under `docs/releases/`.

Release checklist for a tool:

```bash
# 1. bump the version in the tool, update CHANGELOG.md and docs/releases/
# 2. build and verify
# 3. commit
# 4. tag and push the commit and tag together
git tag -a <tool>-v<version> -m "<Tool> <version>"
git push origin main <tool>-v<version>

# 5. create the release with assets (requires GitHub authentication)
gh release create <tool>-v<version> \
  dist/<asset> dist/SHA256SUMS \
  --title "<Tool> <version>" \
  --notes-file bitstream-inspector/docs/releases/v<version>.md
```

If `gh` is not available, create the Release from the GitHub web UI and attach
the same files; the tag and notes must match.

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
