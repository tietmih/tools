# tools

[English](README.md) | 简体中文

个人开源工具箱。包含面向嵌入式 Linux 与多媒体工作的小型、专注工具；每个工具
一个目录，各自带构建文件和 README。

## 项目

| 项目 | 说明 | 平台 |
|---|---|---|
| [bitstream-inspector](bitstream-inspector/README.zh-CN.md) | H.264/H.265 码流分析器（Win32 图形界面）：帧列表、语法树、十六进制视图、码率图和帧画面预览。支持 Annex B 裸流以及 MP4/MOV/TS/MKV 容器。 | Windows（Linux 下用 mingw-w64 交叉编译） |

## 目录结构

```text
.
├── .github/workflows/       # GitHub Actions CI 与发布工作流
├── scripts/                 # 仓库级 CI 辅助脚本（文档配对、发布说明）
├── bitstream-inspector/     # 每个工具一个目录
│   ├── ci/                  # 工作流调用的构建/测试脚本
│   ├── tests/               # 单元测试与以 FFmpeg 为参考的回归套件
│   ├── src/core/            # 可移植 C++17 码流解析（不依赖 Qt/Win32）
│   ├── src/media/           # 可选 FFmpeg 解封装/解码层
│   ├── src/ui/              # Win32 图形界面
│   ├── tools/               # 命令行验证工具
│   ├── scripts/             # 构建辅助脚本
│   ├── installer/           # NSIS 安装包
│   └── res/                 # Windows 资源
├── LICENSE
└── README.md
```

所有项目遵循的约定：

- 语言：C++17（面向用户的程序）或 Python 3（小型脚本）。
- 不提交构建产物、第三方二进制和测试码流——见 `.gitignore`。
- 每个项目的 README 记录如何构建，以及哪些内容**实际验证过**、哪些只是编译通过。
- 每个工具独立版本；每个发布版本都有 tag 和对应的 GitHub Release（见下）。
- 文档双语：`<name>.md` 英文、`<name>.zh-CN.md` 简体中文，文件顶部提供语言切换
  链接。法律许可证原文（`LICENSE`、LGPL）保持原语言。
- 工作分支是临时的：每个改动单独建分支，fast-forward 合并到 `main` 后删除。
  推送 `main` 后 `cleanup-branches.yml` 会自动删除已合并的 `ci/*` 分支。
- 图形界面支持运行时英文/简体中文切换。标准中的语法术语（NAL 类型、SPS/PPS/VPS、
  slice/PPS/SPS 字段名、编码与 profile 名称）在两种界面语言下都保持英文。

## 持续集成

所有工具都用 GitHub Actions 完成构建、测试和发布：

| 工作流 | 触发条件 | 内容 |
|---|---|---|
| `ci.yml` | 代码和工作流变更 | 从 `.github/tools.json` 自动发现工具；Linux x64/arm64 回归、ASan/UBSan 与变异 fuzz、Windows 交叉构建与安装包/便携 ZIP、在 `windows-2022` 和 `windows-2025` 上的原生 Windows GUI/安装器冒烟与 MSVC 构建、覆盖率。FFmpeg 每次运行只在共享的 `deps` job 构建一次。忽略 tag 和纯文档变更 |
| `repo-checks.yml` | 每次推送/PR | 双语文档配对、链接/锚点、版本字面量、shellcheck、markdownlint、Python 语法、actionlint |
| `static-analysis.yml` | 代码变更 | 对解析器、媒体层、命令行工具和测试跑 cppcheck 与 clang-tidy analyzer 检查 |
| `release.yml` | tag `<tool>-v*` 或手动触发 | 版本/说明校验、测试、构建、provenance 证明和 GitHub Release；手动触发可传 `publish: false` 做 dry-run |
| `dependency-check.yml` | 每周或手动触发 | 比较固定依赖与上游版本，创建跟踪 issue，报告 runner 工具链 |
| `nightly.yml` | 每天或手动触发 | ASan/UBSan 构建与 3000 轮变异 fuzz |

工具约定、缓存、发布流程和本地复现详见 [docs/ci.zh-CN.md](docs/ci.zh-CN.md)。

- 推送 `<tool>-v<version>` tag 会自动发布；除非 Actions 不可用，否则不要手工创建
  Release。
- 打 tag 前 CI 必须全绿。tag 版本必须与工具的 `CMakeLists.txt` 一致，发布说明必须
  两种语言都存在。
- 工具自己的步骤放在 `<tool>/ci/`（`linux.sh`、`windows.sh`、`check-version.sh`、
  `windows-smoke.ps1`、`msvc.ps1`、`coverage.sh`，可选的 `ffmpeg.sh`/
  `ffmpeg-host.sh`），本地执行与 CI 完全相同的命令。新增工具时添加这些脚本并在
  `.github/tools.json` 中登记即可，无需改 workflow。
- Windows 构建会从上游源码包编译固定版本的 LGPL FFmpeg（校验 SHA-256），并在
  多次运行之间缓存。

## 依赖

第三方版本统一固定并跟踪：

- `bitstream-inspector/ci/deps.env` 记录内置 FFmpeg 的版本和 SHA-256；
  `ci/ffmpeg.sh` 在构建前校验源码包。Linux 测试任务用同一份源码包构建相同版本
  （`ci/ffmpeg-host.sh`，走缓存），保证测试与出货 DLL 使用同一个 FFmpeg 版本。
- `dependency-check.yml` 每周运行一次，比较固定版本与上游稳定版；FFmpeg 落后时
  创建或更新 **Dependency updates available** issue，并附升级检查清单。同时报告
  runner 提供的工具链版本（NSIS、mingw-w64、nasm、CMake）。
- Dependabot 负责保持 GitHub Actions 版本最新。
- runner 提供的工具跟随固定的 `ubuntu-24.04` 镜像；升级意味着更换镜像或修改安装
  步骤。
- 升级要单独提交：更新 pin、DLL 名称、`BUILD-INFO` 和文档，跑完整本地测试，再走
  正常 tag 发布流程。

## 版本与发布

仓库没有全局版本号，每个工具按自己的 SemVer 节奏独立发布。

规则：

- **版本号**写在工具内部（`bitstream-inspector` 位于 `CMakeLists.txt` 的
  `project(... VERSION ...)`、Windows 版本资源、安装器元数据和 About 对话框）。
  版本号必须与改动在同一个提交里更新。
- **Tag**：附注标签，格式 `<tool>-v<major>.<minor>.<patch>`，例如
  `bitstream-inspector-v0.3.0`。不要使用裸版本号，避免多个工具冲突。
- **Release**：每个推送的 tag 必须有同名 GitHub Release；只有 tag 没有 Release
  视为未完成发布。
- **附件**：附带构建产物（Windows 安装包，可选便携压缩包）、CycloneDX SBOM 和
  `SHA256SUMS`。
  二进制只放在 Release，绝不进 git——`dist/` 已被忽略。校验和文件使用纯文件名，
  这样在下载目录里可以直接 `sha256sum -c SHA256SUMS`。
- **说明**：发布说明同时提供英文和简体中文，包含用户可见变化、已做验证、已知限制
  和附件 SHA-256。每个工具维护 `CHANGELOG.md` / `CHANGELOG.zh-CN.md`，发布说明
  放在 `docs/releases/`（英文与 `.zh-CN.md`）。
- **文档**：每次发布都必须同时更新两种语言；只有一种语言的发布视为不完整。

发布检查清单：

```bash
# 0. 检查未关闭的 "Dependency updates available" issue；依赖升级放在独立提交，
#    不要混进发布提交
# 1. 更新版本号；同时更新 CHANGELOG(.zh-CN).md、README（两种语言）和 docs/releases/
# 2. 在本地运行该工具的 CI 脚本
bash bitstream-inspector/ci/ffmpeg-host.sh  # 首次：测试用 host FFmpeg
bash bitstream-inspector/ci/linux.sh release
bash bitstream-inspector/ci/linux.sh asan
bash bitstream-inspector/ci/ffmpeg.sh       # 首次：Windows LGPL FFmpeg
bash bitstream-inspector/ci/windows.sh
# 3. 提交并推送 main
git push origin main

# 4. 推送附注 tag；发布工作流会运行测试、构建并发布
git tag -a <tool>-v<version> -m "<Tool> <version>"
git push origin <tool>-v<version>

# 5. 检查工作流运行结果和已发布的附件
gh run list --workflow release.yml --limit 1
```

如果 tag 版本与源码不一致、缺少任一语言的发布说明，或任何测试任务失败，发布
工作流会失败。Actions 不可用时的后备方案是手工执行 `gh release create`，使用
`dist/` 中的安装包和 `SHA256SUMS` 以及同一份发布说明；tag 与说明必须一致。

## 当前发布

| 工具 | 版本 | Tag / Release |
|---|---|---|
| bitstream-inspector | 0.3.1 | [`bitstream-inspector-v0.3.1`](https://github.com/tietmih/tools/releases/tag/bitstream-inspector-v0.3.1) |

## 构建

每个项目自包含，先读对应 README：

```bash
cd bitstream-inspector
cat README.zh-CN.md
```

## 许可证

仓库代码使用 MIT 许可证，见 [LICENSE](LICENSE)。

第三方组件保留各自许可证；例如 `bitstream-inspector` 以 LGPL v2.1 动态链接
FFmpeg（未启用 GPL 组件）——见
`bitstream-inspector/third_party/ffmpeg-win64/COPYING.LGPLv2.1`。
