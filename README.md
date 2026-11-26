# tools

English | [简体中文](README.zh-CN.md)

Personal open-source toolbox. Small, focused utilities for embedded Linux and
multimedia work — one directory per project, each with its own build files and
README.

## Projects

| Project | What it does | Platform |
|---|---|---|
| [bitstream-inspector](bitstream-inspector/) | H.264/H.265 bitstream analyzer with a Win32 GUI: frame list, syntax tree, hex view, bitrate chart and decoded frame preview. Reads Annex B elementary streams and MP4/MOV/TS/MKV containers. | Windows (cross-built from Linux with mingw-w64) |

## Layout

```text
.
├── .github/workflows/       # GitHub Actions CI and release workflows
├── scripts/                 # repository-wide CI helpers (docs pairing, release notes)
├── bitstream-inspector/     # one project per directory
│   ├── ci/                  # build/test scripts the workflows call
│   ├── tests/               # unit tests and FFmpeg-referenced regression suite
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
- Documentation is bilingual: `<name>.md` in English and `<name>.zh-CN.md` in
  Simplified Chinese, each with a language-switch link at the top. Legal license
  texts (`LICENSE`, LGPL) stay in their original language.
- Work branches are temporary. Create a branch for each change, fast-forward it
  into `main`, and delete it. `cleanup-branches.yml` removes merged `ci/*`
  branches automatically after a push to `main`.
- GUI applications provide a runtime English/Simplified-Chinese switch. Syntax
  terms from the standards (NAL types, SPS/PPS/VPS, slice/PPS/SPS field names,
  codec and profile names) remain in English in both UI languages.

## Continuous integration

GitHub Actions is the release path for every tool:

| Workflow | Trigger | What it runs |
|---|---|---|
| `ci.yml` | code changes and workflow changes | discovers tools from `.github/tools.json`; Linux x64 and arm64 regression tests, ASan/UBSan and mutation fuzz, Windows cross-build plus installer and portable ZIP, native Windows GUI/installer smoke test and MSVC build on `windows-2022` and `windows-2025`, plus coverage. FFmpeg is built once per run in a shared `deps` job. Ignores tags and documentation-only changes |
| `repo-checks.yml` | every push/PR | bilingual documentation pairs, link/anchor checks, tracked-file hygiene, version–CHANGELOG–README consistency, shellcheck, markdownlint, Python syntax and actionlint |
| `static-analysis.yml` | code changes | cppcheck and clang-tidy analyzer checks for the parser, media layer, CLI and tests |
| `release.yml` | tag `<tool>-v*`, or manual dispatch | version/notes checks, tests, package build, provenance attestation and GitHub Release; manual dispatch accepts `publish: false` for a dry run |
| `dependency-check.yml` | weekly, or manual dispatch | compares pinned dependencies with upstream releases, opens a tracking issue, reports the runner toolchain |
| `release-audit.yml` | daily, or manual dispatch | verifies every tag's Release, assets and `SHA256SUMS`; opens an issue when a release is missing or broken |
| `nightly.yml` | daily, or manual dispatch | ASan/UBSan build and 3000 mutation-fuzz iterations |

The full contract for tools, caching, releases and local reproduction is in
[docs/ci.md](docs/ci.md).

- Pushing a `<tool>-v<version>` tag publishes the release automatically. Do not
  create releases by hand unless Actions is unavailable.
- CI must be green before tagging. The tag version must match the tool's
  `CMakeLists.txt`, and the release notes must exist in both languages.
- Tool-specific steps live in `<tool>/ci/` (`linux.sh`, `windows.sh`,
  `check-version.sh`, `windows-smoke.ps1`, `msvc.ps1`, `coverage.sh`, optional
  `ffmpeg.sh`/`ffmpeg-host.sh`) so the same commands run locally and in CI.
  Adding a tool means adding those scripts and an entry in
  `.github/tools.json`; no workflow edit is required.
- The Windows build compiles a pinned LGPL FFmpeg from the upstream tarball
  (SHA-256 verified) and caches it between runs.

## Dependencies

Third-party versions are pinned and tracked:

- `bitstream-inspector/ci/deps.env` holds the bundled FFmpeg version and its
  SHA-256; `ci/ffmpeg.sh` verifies the tarball before building. The Linux test
  job builds the same version from the tarball (`ci/ffmpeg-host.sh`, cached),
  so the tests and the shipped DLLs use one FFmpeg release.
- `dependency-check.yml` runs weekly, compares the pin with upstream stable
  releases, and opens or updates a **Dependency updates available** issue with
  an upgrade checklist when FFmpeg is behind. It also reports the
  runner-provided toolchain (NSIS, mingw-w64, nasm, CMake).
- Dependabot keeps the GitHub Actions versions current.
- Runner-provided tools follow the pinned `ubuntu-24.04` image; changing them
  means changing the image or their installation step.
- Upgrades are deliberate: update the pin, DLL names, `BUILD-INFO` and docs,
  run the full local suite, then release through the normal tag flow.

## Versioning and releases

There is no repository-wide version. Each tool follows its own SemVer timeline.

Rules:

- **Version** lives in the tool (for `bitstream-inspector`: `CMakeLists.txt`
  `project(... VERSION ...)`, the Windows version resource, the installer
  metadata and the application About box). Bump it in the same commit as the
  change.
- **Tag**: annotated tag named `<tool>-v<major>.<minor>.<patch>`, for example
  `bitstream-inspector-v0.3.0`. Never use a bare version tag that could collide
  between tools.
- **Release**: every pushed tag must have a matching GitHub Release with the
  same name. A tag without a release is not a finished release.
- **Assets**: attach the tool's built packages (Windows installer; optional
  portable archive), a CycloneDX SBOM and a `SHA256SUMS` file. Binary artifacts live only in the
  Release, never in git — `dist/` is ignored. Checksums list bare file names so
  `sha256sum -c SHA256SUMS` works in the download directory.
- **Notes**: list user-visible changes, verification performed, known
  limitations and asset SHA-256 values, in both English and Simplified Chinese.
  Keep a per-tool `CHANGELOG.md` / `CHANGELOG.zh-CN.md` and release notes under
  `docs/releases/` (English and `.zh-CN.md`).
- **Docs**: every release updates both language versions; a release with only
  one language is incomplete.

Release checklist for a tool:

```bash
# 0. check open "Dependency updates available" issues; upgrade dependencies
#    in a dedicated change, not inside the release commit
# 1. bump the version; update CHANGELOG(.zh-CN).md, README(s) and docs/releases/ (both languages)
# 2. run the tool's CI scripts locally
bash bitstream-inspector/ci/ffmpeg-host.sh  # once: host FFmpeg for tests
bash bitstream-inspector/ci/linux.sh release
bash bitstream-inspector/ci/linux.sh asan
bash bitstream-inspector/ci/ffmpeg.sh       # once: Windows LGPL FFmpeg
bash bitstream-inspector/ci/windows.sh
# 3. commit and push main
git push origin main

# 4. push an annotated tag; the release workflow tests, builds and publishes
git tag -a <tool>-v<version> -m "<Tool> <version>"
git push origin <tool>-v<version>

# 5. verify the workflow run and the published assets
gh run list --workflow release.yml --limit 1
```

The release workflow fails if the tag version does not match the source, if
either language of the release notes is missing, or if any test job fails. If
Actions is unavailable, the manual fallback is `gh release create` with the
installer and `SHA256SUMS` from `dist/` and the same notes file; the tag and
notes must match.

## Current release

| Tool | Version | Tag / Release |
|---|---|---|
| bitstream-inspector | 0.4.0 | [`bitstream-inspector-v0.4.0`](https://github.com/tietmih/tools/releases/tag/bitstream-inspector-v0.4.0) |

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
