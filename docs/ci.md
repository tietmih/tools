# CI/CD

English | [简体中文](ci.zh-CN.md)

GitHub Actions is the only build and release path. Workflows live in
`.github/workflows/` and are driven by `.github/tools.json`.

| Workflow | Trigger | What it does |
|---|---|---|
| `ci.yml` | push/PR touching a tool, `scripts/`, or a workflow | Discovers tools from `tools.json` and runs the Linux x64/arm64 suites, Windows cross-build, native Windows smoke test, MSVC build and coverage |
| `repo-checks.yml` | every push/PR | Bilingual doc pairing, link/anchor checks, version literals, shellcheck, Python syntax, actionlint |
| `release.yml` | tag `<tool>-v<version>`, or manual dispatch | Validates the annotated tag, version and bilingual notes, re-runs tests, builds the package, attests provenance and publishes the GitHub Release. Manual dispatch accepts `publish: false` for a dry run |
| `dependency-check.yml` | weekly/manual | Compares `ci/deps.env` with upstream FFmpeg and reports the runner toolchain; opens or updates a tracking issue |
| `nightly.yml` | daily/manual | Builds the parser with ASan/UBSan and runs 3000 mutation-fuzz iterations |

`ci.yml` ignores tag pushes (`tags-ignore`) and uses path filters, so
documentation-only changes run `repo-checks.yml` but not the build matrix.

## Tool contract

Every directory listed in `.github/tools.json` provides:

- `ci/linux.sh release|asan` — host build and regression suite (or coverage).
- `ci/windows.sh [version]` — cross-build plus installer/portable package.
- `ci/check-version.sh [expected]` — version literals consistent with CMake.
- `ci/windows-smoke.ps1` — native GUI/installer/portable checks; invoked on
  `windows-2022`.
- `ci/msvc.ps1` — MSVC compile and unit tests.
- `ci/coverage.sh` — instrumented build, suite and gcovr report.
- Optional `ci/ffmpeg.sh` and `ci/ffmpeg-host.sh` — pinned FFmpeg builds for
  Windows and Linux tests.

Adding a tool means adding those scripts and a `tools.json` entry; the
workflows pick it up automatically. Empty tool categories use a `(none)`
sentinel so the matrix never becomes empty.

## Dependencies

- `bitstream-inspector/ci/deps.env` pins the bundled FFmpeg version and
  SHA-256; both the Windows DLLs and the Linux test build use that release.
- `dependency-check.yml` opens a **Dependency updates available** issue when a
  newer FFmpeg stable tag exists; upgrades are reviewed and tested manually.
- Dependabot updates GitHub Actions; workflow actions are pinned to commit
  SHAs.
- Runner-provided tools (NSIS, mingw-w64, nasm, CMake) follow the pinned
  `ubuntu-24.04` / `windows-2022` images.

## Releasing

```bash
# 1. bump version, changelog and both release notes
# 2. run the tool's CI scripts locally
# 3. commit and push main
git tag -a <tool>-v<version> -m "<Tool> <version>"
git push origin <tool>-v<version>
# 4. verify the release workflow and assets
gh run list --workflow release.yml --limit 1
```

A manual dry run that skips publishing:

```bash
gh workflow run release.yml --ref main \
  -f tag=<tool>-v<version> -f publish=false
```

Release assets are the Windows installer, portable ZIP and `SHA256SUMS`; the
body contains both languages and the checksum block. The publish job attests
build provenance for both artifacts.

## Local reproduction

The same scripts run locally. Typical sequence for `bitstream-inspector`:

```bash
cd bitstream-inspector
bash ci/ffmpeg-host.sh      # once: pinned host FFmpeg for tests
bash ci/linux.sh release
bash ci/linux.sh asan
bash ci/ffmpeg.sh           # once: pinned Windows FFmpeg
bash ci/windows.sh
bash ci/coverage.sh         # needs gcovr
```

## Known gaps

Items that need credentials, a policy decision or a larger follow-up:

- **Code signing**: the installer and executables are not Authenticode-signed.
  A certificate, password and timestamp service are required before signing
  can be automated.
- **Branch protection**: `main` has no required checks, force-push protection
  or PR policy yet; direct pushes are allowed and CI is advisory. This is a
  repository-policy decision.
- **Windows 2025 / ARM64 Windows**: validation runs on `windows-2022` x64.
- **Reproducible builds**: no double-build hash comparison for the installer.
- **SBOM**: build provenance is attested, but no CycloneDX/SPDX SBOM is
  generated.
- **Static analysis**: clang-tidy/cppcheck, MSVC `/WX` and markdownlint are not
  wired into CI.
- **Coverage reporting**: the line threshold is enforced (60%), but the report
  is not posted as a PR comment.
- **GUI automation**: startup, language switching, clean exit, portable ZIP
  and installer scenarios are covered; splitter dragging and every dialog are
  not.
- **Installer edge cases**: uninstalling while the app is running (the
  uninstaller retry path) is not automated.
- **Parser vectors**: multi-PPS switching, dependent slice segments and PAFF
  field coding are not generated; MBAFF/interlaced is covered.
- **Efficiency**: `release.yml` rebuilds FFmpeg on tags because cache scopes
  are per branch; ccache files accumulate per commit. Centralizing the FFmpeg
  build and rolling the ccache key are follow-ups.
- **Release channels**: only tagged stable releases are published; there is no
  nightly/pre-release channel.
