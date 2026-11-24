# tools

[English](README.md) | 简体中文

个人开源工具箱。包含面向嵌入式 Linux 与多媒体工作的小型、专注工具；每个工具
一个目录，各自带构建文件和 README。

## 项目

| 项目 | 说明 | 平台 |
|---|---|---|
| [bitstream-inspector](bitstream-inspector/README.zh-CN.md) | H.264/H.265 码流分析器（Win32 图形界面）：帧列表、语法树、十六进制视图、码率图和帧画面预览。支持 Annex B 裸流以及 MP4/MOV/TS/MKV 容器。 | Windows（Linux 下用 mingw-w64 交叉编译） |

## 目录结构

```
.
├── bitstream-inspector/     # 每个工具一个目录
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
- 图形界面支持运行时英文/简体中文切换。标准中的语法术语（NAL 类型、SPS/PPS/VPS、
  slice/PPS/SPS 字段名、编码与 profile 名称）在两种界面语言下都保持英文。

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
- **附件**：附带构建产物（Windows 安装包，可选便携压缩包）和 `SHA256SUMS`。
  二进制只放在 Release，绝不进 git——`dist/` 已被忽略。校验和文件使用纯文件名，
  这样在下载目录里可以直接 `sha256sum -c SHA256SUMS`。
- **说明**：发布说明同时提供英文和简体中文，包含用户可见变化、已做验证、已知限制
  和附件 SHA-256。每个工具维护 `CHANGELOG.md` / `CHANGELOG.zh-CN.md`，发布说明
  放在 `docs/releases/`（英文与 `.zh-CN.md`）。
- **文档**：每次发布都必须同时更新两种语言；只有一种语言的发布视为不完整。

发布检查清单：

```bash
# 1. 更新版本号；同时更新 CHANGELOG(.zh-CN).md、README（两种语言）和 docs/releases/
# 2. 构建并验证
# 3. 提交
# 4. 同时推送提交和 tag
git tag -a <tool>-v<version> -m "<Tool> <version>"
git push origin main <tool>-v<version>

# 5. 创建 Release 并上传附件（需要 GitHub 认证）
gh release create <tool>-v<version> \
  dist/<asset> dist/SHA256SUMS \
  --title "<Tool> <version>" \
  --notes-file bitstream-inspector/docs/releases/v<version>.md
```

没有 `gh` 时，从 GitHub 网页创建 Release 并上传同样的附件；tag 和发布说明必须一致。

## 当前发布

| 工具 | 版本 | Tag / Release |
|---|---|---|
| bitstream-inspector | 0.3.0 | [`bitstream-inspector-v0.3.0`](https://github.com/tietmih/tools/releases/tag/bitstream-inspector-v0.3.0) |

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
