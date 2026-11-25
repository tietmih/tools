# 持续集成 / 持续交付

[English](ci.md) | 简体中文

GitHub Actions 是唯一的构建和发布路径。工作流位于 `.github/workflows/`，由
`.github/tools.json` 驱动。

| 工作流 | 触发条件 | 内容 |
|---|---|---|
| `ci.yml` | 涉及工具、`scripts/` 或工作流的推送/PR | 从 `tools.json` 自动发现工具，运行 Linux x64/arm64、Windows 交叉构建、原生 Windows 冒烟、MSVC 和覆盖率 |
| `repo-checks.yml` | 每次推送/PR | 双语文档配对、链接/锚点、受版本库文件卫生（禁止二进制/媒体/构建产物）、版本–CHANGELOG–README 一致性、shellcheck、markdownlint、Python 语法、actionlint |
| `static-analysis.yml` | 代码变更 | 对解析器、媒体层、命令行工具和单元测试跑 cppcheck 与 clang-tidy analyzer 检查 |
| `release.yml` | tag `<tool>-v<version>` 或手动 | 校验注释 tag、版本和双语文档，重跑测试、构建安装包、生成 provenance 并发布 Release；手动触发可传 `publish: false` 做 dry-run |
| `dependency-check.yml` | 每周/手动 | 对比 `ci/deps.env` 与上游 FFmpeg，报告 runner 工具链，创建/更新跟踪 issue |
| `nightly.yml` | 每天/手动 | 用 ASan/UBSan 构建解析器并跑 3000 轮变异 fuzz |

`ci.yml` 忽略 tag 推送（`tags-ignore`）并使用 paths 过滤，因此只改文档时只运行
`repo-checks.yml`，不会跑构建矩阵。

## 工具约定

`.github/tools.json` 中列出的每个工具目录需要提供：

- `ci/linux.sh release|asan` —— 主机构建与回归测试（或覆盖率）。
- `ci/windows.sh [version]` —— 交叉构建及安装包/便携包。
- `ci/check-version.sh [expected]` —— 校验版本字面量与 CMake 一致。
- `ci/windows-smoke.ps1` —— 原生 GUI/安装器/便携包检查，在 `windows-2022` 运行。
- `ci/msvc.ps1` —— MSVC 编译和单元测试。
- `ci/coverage.sh` —— 插桩构建、测试和 gcovr 报告。
- `ci/static.sh` —— cppcheck 和 clang-tidy analyzer 检查。
- 可选的 `ci/ffmpeg.sh`、`ci/ffmpeg-host.sh` —— 固定版本的 Windows/Linux FFmpeg。

新增工具只需添加这些脚本并在 `tools.json` 登记，工作流会自动发现。空工具分类使用
`(none)` 哨兵，避免矩阵为空报错。

## 依赖

- `bitstream-inspector/ci/deps.env` 固定内置 FFmpeg 版本和 SHA-256；Windows DLL
  与 Linux 测试构建使用同一版本。
- `dependency-check.yml` 在上游出现更新的 FFmpeg 稳定 tag 时创建
  **Dependency updates available** issue；升级需人工评审并测试。
- Dependabot 更新 GitHub Actions；工作流中的 action 固定到 commit SHA。
- runner 提供的工具（NSIS、mingw-w64、nasm、CMake）跟随固定的
  `ubuntu-24.04` / `windows-2022` 镜像。

## 发布

```bash
# 1. 更新版本号、变更日志和两种语言的发布说明
# 2. 本地运行该工具的 CI 脚本
# 3. 提交并推送 main
git tag -a <tool>-v<version> -m "<Tool> <version>"
git push origin <tool>-v<version>
# 4. 检查 release 工作流和附件
gh run list --workflow release.yml --limit 1
```

只验证、不发布的 dry-run：

```bash
gh workflow run release.yml --ref main \
  -f tag=<tool>-v<version> -f publish=false
```

Release 附件是 Windows 安装包、便携 ZIP、CycloneDX 1.6 SBOM（`*.cdx.json`）和
`SHA256SUMS`；正文包含中英两种语言和校验和块。publish job 会为三个附件生成构建
provenance。

## 安装器行为

按用户的卸载器明确处理“程序正在运行”：

- 交互模式：提示用户关闭程序，并提供“重试/取消”；
- 静默模式（`/S`）：重试一次后中止，安装保持完整，脚本可关闭程序后重试。NSIS
  会从 `%TEMP%` 重新启动卸载器，因此启动进程的退出码不可靠；应检查安装目录和
  卸载注册表项是否仍然存在；
- `windows-smoke.ps1` 覆盖默认安装、`/D=`、`/LANG=2052`、从 0.3.0 升级、卸载，
  以及运行中卸载路径：要么卸载完成（映像文件处于 delete-pending），要么中止且
  文件和注册表保持完整；关闭程序后重试可成功卸载。

## 发布治理

- **Tag 审计**（`release-audit.yml`）：每天检查每个 `<tool>-v*` tag 是否有
  Release、安装包/便携 ZIP/SBOM 是否齐全、`SHA256SUMS` 是否通过、发布说明是否
  双语。失败时创建或更新 `Release audit failures` issue。
- **不可变发布**：`release.yml` 在目标 tag 已有且校验通过的 Release 时拒绝发布；
  损坏的 Release 可通过重跑工作流修复。dry-run（`publish=false`）不受影响。
- **版本联动**：`ci/check-version.sh` 要求 `CMakeLists.txt` 的版本与 Windows
  资源、About、安装器、两个 CHANGELOG 以及仓库 README 的“当前发布”表一致。
- **文件卫生**：`scripts/check_repo_hygiene.py` 在二进制、媒体、`dist/`、
  `build-*` 或 `__pycache__` 被跟踪时失败。

## Runner 成本策略

- `windows-2025` 只在非 PR 运行；PR 只跑 `windows-2022`。
- Fuzz 轮数按事件缩放：PR 50 轮、分支 200 轮、`nightly.yml` 3000 轮。
- 覆盖率只在非 PR 构建；PR 运行 release 套件。

## 缓存与共享依赖

`_deps.yml` 每次运行只构建一次 FFmpeg（host x64/arm64，以及工具有 Windows
目标时的 mingw 构建），并以短期 artifact 发布。Linux、ARM64、覆盖率和 Windows
job 直接使用这些 artifact，不再各自构建 FFmpeg；缓存 key 保持不变，本地运行和
手动触发仍可复用每周缓存。

- ccache key 按周滚动（`<os>-<arch>-<tool>-<YYYY-Www>`），不再随每次提交增长。
- 覆盖率 job 在 PR 上维护一条 sticky 评论（行/分支覆盖率和 60% 阈值），同一摘要
  写入 workflow step summary，HTML/XML 报告作为 artifact 上传。

## 可复现构建

在源码和工具链相同的前提下，Windows 打包步骤可复现：

- 可执行文件链接加 `-Wl,--no-insert-timestamp`，PE 头不写入构建时间；
- `build_installer.sh` 在 NSIS 与 `zip -X` 之前把所有待打包文件的 mtime 固定为
  `SOURCE_DATE_EPOCH`（默认 `1735689600`，可覆盖）；
- SBOM 使用该时间戳且不写构建主机；
- `_build-windows.yml` 从干净构建目录重新打包，任何一个附件字节不同即失败。

只有文档记录的工具链（Ubuntu 24.04 mingw-w64、固定 FFmpeg 9.1、NSIS）预期
可复现；不同编译器或 NSIS 版本仍可能产生不同字节。

## 本地复现

同一套脚本可在本地运行。`bitstream-inspector` 的典型顺序：

```bash
cd bitstream-inspector
bash ci/ffmpeg-host.sh      # 首次：测试用固定版本 host FFmpeg
bash ci/linux.sh release
bash ci/linux.sh asan
bash ci/ffmpeg.sh           # 首次：固定版本 Windows FFmpeg
bash ci/windows.sh
bash ci/coverage.sh         # 需要 gcovr
```

## 已知缺口

以下项目需要证书、策略决定或后续较大的改动：

- **代码签名**：维护者于 2026-11-25 决定暂缓。安装包和可执行文件未做
  Authenticode 签名；自动化前需要证书或 OSS 签名服务账号。
- **分支保护**：`main` 现在拒绝 force-push 和删除分支（管理员同样受限）。
  必需检查和强制 PR 按直接推送流程有意关闭；直接推送时 CI 仍是建议性的。
- **ARM64 Windows**：原生检查在 `windows-2022` 和 `windows-2025` x64 上运行；
  Windows on ARM 尚未验证。
- **覆盖率报告**：已强制 60% 行覆盖率阈值，但不会把报告评论到 PR。
- **GUI 自动化**：覆盖启动、语言切换、正常退出、便携 ZIP 和安装器场景；不覆盖
  拖拽分栏和所有对话框。
- **解析测试向量**：multi-PPS 切换、dependent slice segment 和 PAFF 场编码尚未
  生成；MBAFF/隔行已覆盖。
- **发布通道**：2026-11-25 决定只发布带 tag 的稳定版。`nightly.yml` 只跑扩展
  fuzz，不构建或发布 nightly 安装包。
