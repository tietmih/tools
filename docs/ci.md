# CI/CD

English | [简体中文](ci.zh-CN.md)

GitHub Actions is the only build and release path. Workflows live in
`.github/workflows/` and are driven by `.github/tools.json`.

| Workflow | Trigger | What it does |
|---|---|---|
| `ci.yml` | push/PR touching a tool, `scripts/`, or a workflow | Discovers tools from `tools.json`, builds FFmpeg once in a `deps` job, then runs the Linux x64/arm64 suites, Windows cross-build, native Windows smoke test and MSVC build on `windows-2022` and `windows-2025`, plus coverage |
| `repo-checks.yml` | every push/PR | Bilingual doc pairing, link/anchor checks, version literals, shellcheck, markdownlint, Python syntax, actionlint |
| `static-analysis.yml` | code changes | cppcheck and clang-tidy analyzer checks for the parser, media layer, CLI and unit tests |
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
- `ci/static.sh` — cppcheck and clang-tidy analyzer checks.
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

Release assets are the Windows installer, portable ZIP, a CycloneDX 1.6 SBOM
(`*.cdx.json`) and `SHA256SUMS`; the body contains both languages and the
checksum block. The publish job attests build provenance for all three
artifacts.

## Installer behaviour

The per-user uninstaller handles a running application explicitly:

- interactive: it asks the user to close the app and offers Retry/Cancel;
- silent (`/S`): it retries once, then aborts and leaves the installation
  untouched, so scripts can close the app and retry. NSIS relaunches the
  uninstaller from `%TEMP%`, so the launcher's exit code is not a reliable
  signal; check whether the install directory and uninstall registry key still
  exist instead;
- `windows-smoke.ps1` covers default install, `/D=`, `/LANG=2052`, upgrade from
  0.3.0, uninstall, and the running-app path: either the uninstall completes
  with the image delete-pending, or it aborts with files and registry intact;
  a retry after closing the app then succeeds.

## Caching and shared dependencies

`_deps.yml` builds FFmpeg once per run (host x64/arm64 and, when the tool has a
Windows target, the mingw build) and publishes short-lived artifacts. The
Linux, ARM64, coverage and Windows jobs consume those artifacts instead of
building FFmpeg themselves; the cache keys are unchanged, so local runs and
workflow dispatches still reuse the weekly cache.

- ccache keys roll weekly (`<os>-<arch>-<tool>-<YYYY-Www>`) instead of per
  commit, so the cache stops growing with every push.
- The coverage job posts a single sticky comment on pull requests with the
  line/branch figures and the 60% threshold; the same summary is written to
  the workflow step summary and the HTML/XML report is uploaded as an
  artifact.

## Reproducible builds

The Windows packaging step is reproducible for identical sources and
toolchains:

- executable link steps pass `-Wl,--no-insert-timestamp`, so PE headers carry
  no build time;
- `build_installer.sh` pins every staged file's mtime to `SOURCE_DATE_EPOCH`
  (default `1735689600`, exportable) before NSIS and `zip -X` run;
- the SBOM uses that epoch as its timestamp and omits the build host;
- `_build-windows.yml` rebuilds the whole package from clean build directories
  and fails if any asset byte differs from the first build.

Only the documented toolchain (Ubuntu 24.04 mingw-w64, pinned FFmpeg 9.1,
NSIS) is expected to reproduce; a different compiler or NSIS version may still
produce different bytes.

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

- **Code signing**: deferred by the maintainer on 2026-11-25. The installer
  and executables are not Authenticode-signed; a certificate or an OSS signing
  service account is required before this can be automated.
- **Branch protection**: `main` now rejects force-pushes and branch deletion,
  including by administrators. Required checks and mandatory pull requests are
  intentionally disabled to keep the direct-push workflow; CI remains
  advisory for direct pushes.
- **ARM64 Windows**: native checks run on `windows-2022` and `windows-2025`
  x64; Windows on ARM is not validated.
- **Coverage reporting**: the line threshold is enforced (60%), but the report
  is not posted as a PR comment.
- **GUI automation**: startup, language switching, clean exit, portable ZIP
  and installer scenarios are covered; splitter dragging and every dialog are
  not.
- **Parser vectors**: multi-PPS switching, dependent slice segments and PAFF
  field coding are not generated; MBAFF/interlaced is covered.
- **Release channels**: by decision on 2026-11-25 only tagged stable releases
  are published. `nightly.yml` runs the extended fuzz suite; it does not build
  or publish nightly packages.
