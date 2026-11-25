# bitstream-inspector

[English](README.md) | 简体中文

[![CI](https://github.com/tietmih/tools/actions/workflows/ci.yml/badge.svg)](https://github.com/tietmih/tools/actions/workflows/ci.yml)

Win32 平台的 H.264/H.265 码流分析器：在同一窗口里提供帧列表、语法树、十六进制
视图、码率图和解码画面预览。

按公开的 ITU-T H.264/H.265 语法规范从零实现，不包含任何商业分析器的代码或资源，
也没有对 Elecard 等产品做反汇编。

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ 文件  视图  语言  帮助                                                   │
│ [打开...] [< 上一帧] [下一帧 >] [上个关键帧] [下个关键帧]  MP4  H.264 …   │
├────────────────────────────┬─────────────────────────────────────────────┤
│  序号 类型 POC QP 帧号 …   │  ▼ [0] SPS（序列参数集）                    │
│   0  IDR   0 27 8312       │      nal_unit_type = 7   [0..4]             │
│   1  P     6 27 4387       │      ▼ seq_parameter_set_rbsp               │
│   2  B     2 34 2619       │          profile_idc = 100  [0..7]          │
│   3  B     4 34 2437       │          level_idc = 30     [16..23]        │
│   …                        ├─────────────────────────────────────────────┤
│                            │ [十六进制] [画面]                           │
│                            │   ┌─────────────────────────────────────┐   │
│                            │   │          解码画面                   │   │
├────────────────────────────┴───┴─────────────────────────────────────┴───┤
│ 帧大小图：▁▄▂▁▃▅▂▁▄▃ …（点击可跳帧）                                    │
├──────────────────────────────────────────────────────────────────────────┤
│ proper_h264.mp4 | MP4 1s | H.264/AVC | 640x360 | 25 fps | 25 帧          │
└──────────────────────────────────────────────────────────────────────────┘
```

## 功能

- **容器**：MP4/MOV、MPEG-TS/M2TS、Matroska/WebM，以及 Annex B 裸流
  （`.h264`/`.264`/`.avc`/`.h265`/`.265`/`.hevc`）。
- **帧列表**：解码顺序、图像类型（IDR/I/P/B，HEVC 的 CRA/BLA）、POC、QP、
  `frame_num`、大小、slice/NAL 数量、文件偏移、显示时间。
- **语法树**：SPS/PPS/VPS/SEI/slice header 的每个字段及精确位范围。参数集按
  access unit 快照，因此早期帧会用当时有效的参数集重建语法树。
- **十六进制视图**：显示 NAL 原始字节，选中语法字段时高亮对应字节。
- **画面页**：后台线程解码选中的帧。容器输入按 PTS 精确取帧；无时间戳的裸流
  显示最近关键帧并给出提示。
- **图表**：逐帧大小柱状图 + 帧类型色带，点击跳帧。
- **导航**：上一帧/下一帧、上一个/下一个关键帧、键盘快捷键、可拖拽分栏；
  布局保存在 `%APPDATA%\BitstreamInspector\settings.ini`。
- **双语界面**：通过“语言”菜单在运行时切换英文/简体中文，选择会被持久化。
  标准术语（NAL 类型、SPS/PPS/VPS、语法字段名、编码/profile 名称）保持英文。
- **导出**：帧列表和 NAL 列表导出为 CSV（UTF-8 带 BOM）。

## Windows 安装

从[最新 Release](https://github.com/tietmih/tools/releases/latest) 下载安装包
或便携 ZIP，或运行下面的打包步骤（二进制是构建产物，不进 git）：

```text
dist/BitstreamInspector-0.3.1-win64-setup.exe
dist/BitstreamInspector-0.3.1-win64-portable.zip
```

安装器按用户安装，不需要管理员权限，也不会弹 UAC：

- 安装到 `%LOCALAPPDATA%\Programs\BitstreamInspector`
- 创建开始菜单快捷方式，可选创建桌面快捷方式
- 在 **设置 → 应用 → 已安装的应用** 中注册卸载项
- 自带 FFmpeg DLL、MIT 许可证、FFmpeg LGPL 许可证和构建信息
- 安装界面支持英文/简体中文选择
- 要求 64 位 Windows 10/11

卸载器会删除程序、快捷方式和注册表项，并询问是否保留
`%APPDATA%\BitstreamInspector` 中的设置。支持静默安装/卸载（`/S`，
自定义目录用 `/D=路径`）。

## 发布

本工具独立于仓库中的其他工具发布。当前版本 **0.3.1**，tag 格式为
`bitstream-inspector-v0.3.1`；每个 tag 都有对应的 GitHub Release，附件包含
Windows 安装包、便携 ZIP 和 `SHA256SUMS`，发布说明提供英文和简体中文两个版本。
发布包由 CI 工作流构建、测试并自动发布。

变更历史见 [CHANGELOG.zh-CN.md](CHANGELOG.zh-CN.md)，各版本发布说明见
[docs/releases/](docs/releases/)。

## 环境要求

- 运行图形界面：Windows 10/11 x86-64。
- 从 Linux 构建：`g++-mingw-w64-x86-64`（GCC 13+）和 CMake 3.16+。
- 可选媒体层：Windows 构建使用仓库内置的 FFmpeg 9.1 共享库（LGPL，含
  H.264/HEVC 解码器，MP4/TS/MKV 解封装器）。Linux 上安装了系统 FFmpeg 开发包
  （`libavformat-dev`、`libavcodec-dev`、`libavutil-dev`、`libswscale-dev`）时
  也会启用媒体层。没有 FFmpeg 时程序仍可构建和运行，但容器输入和画面页不可用。
  内置版本和 SHA-256 固定在 [ci/deps.env](ci/deps.env)；仓库 README 说明每周
  依赖检查和升级规则。

## 构建

### Windows 图形界面（Linux 交叉编译）

```bash
# 1. 先构建可选的 FFmpeg 层（只需一次）
bash scripts/build_ffmpeg_mingw.sh /path/to/ffmpeg-9.1.tar.xz \
     /tmp/ffmpeg-build "$PWD/third_party/ffmpeg-win64"

# 2. 配置并构建
cmake -S . -B build-win \
      -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
      -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j
```

`build-win/bsinspector.exe` 是图形界面。如果 FFmpeg 层存在，构建会自动把
`avcodec-63`、`avformat-63`、`avutil-61`、`swscale-10` 四个 DLL 复制到 exe 旁边；
分发时必须一起带上。

### Linux 原生构建（核心库 + 命令行工具）

```bash
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host -j
```

核心解析器和 `bsinfo` 命令行工具与平台无关。Win32 图形界面和 FFmpeg 媒体层只在
Windows 目标下构建。

### MSVC

同一份 CMake 工程也可用 MSVC 构建
（`cmake -B build-msvc -G "Visual Studio 17 2022"`）；FFmpeg 层需要匹配的
MSVC import library。

### 构建 Windows 安装包

安装器使用 NSIS，打包现有的 `build-win` 目录（GUI、CLI 和四个必需的
FFmpeg DLL）：

```bash
# Linux 主机需要 NSIS：sudo apt install nsis
bash installer/build_installer.sh

# 可选：指定其他构建目录
bash installer/build_installer.sh /path/to/build-win
```

输出：`dist/BitstreamInspector-<version>-win64-setup.exe`。安装器按用户安装，
创建开始菜单/桌面快捷方式，在“设置 → 应用”中注册卸载项，并包含 `Uninstall.exe`。

安装包没有代码签名，首次从网上下载运行时 Windows SmartScreen 可能会提示一次。

## 测试与 CI

仓库不存放媒体文件；测试套件用 FFmpeg 现场生成码流，并以 FFmpeg 结果作为参考。

```bash
# 主机构建、单元测试、ffprobe/trace_headers/解码对比
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
bash tests/run_tests.sh build-host

# ASan/UBSan 构建、检查与变异 fuzz
bash ci/ffmpeg-host.sh   # 首次：与出货版本一致的 host FFmpeg
bash ci/linux.sh asan

# Windows 交叉构建和安装包（需要 mingw-w64、nasm、NSIS）
bash ci/ffmpeg.sh        # 首次：固定版本 LGPL FFmpeg 9.1，校验 SHA-256
bash ci/windows.sh
```

`tests/run_tests.sh` 需要 `PATH` 中有 `ffmpeg` 和 `ffprobe` 来生成码流。执行过
`ci/ffmpeg-host.sh` 后，参考解码、`ffprobe` 和 `trace_headers` 检查会使用与
`mtest` 相同的固定 FFmpeg 版本，从而保持逐像素严格比较。检查内容包括帧数/图像
类型、语法字段，以及 MP4/TS/MKV 解码帧的逐像素对比。

CI 在每次推送和拉取请求时于 Linux x64/arm64 运行同样的脚本，并增加 MSVC 构建、
覆盖率，以及原生 Windows 检查：CLI 解码、GUI 启动与语言切换、便携 ZIP、安装/
卸载往返。推送 `bitstream-inspector-v<version>` tag 会重新运行整套测试，并发布
带安装包、便携 ZIP 和 `SHA256SUMS` 的 Release。详见
[仓库 README](../README.zh-CN.md#持续集成)。

## 命令行工具

`bsinfo` 用于不启动 GUI 验证解析器：

```bash
bsinfo stream.h264                    # NAL 列表和每个 NAL 的摘要
bsinfo stream.h265 --frames           # 帧表
bsinfo stream.h264 --tree 4           # NAL 4 的语法树
bsinfo stream.h264 --csv-frames f.csv --csv-nals n.csv
```

`mtest` 用于验证容器解封装/解码链路：

```bash
mtest movie.mp4                       # 容器信息、packet 映射、帧时间
mtest movie.mkv --frame 10 --ppm out.ppm
```

## 验证状态

以下结果用独立工具交叉验证，不只是“能编译”。这些检查已写入 `tests/`，由 CI
在每次推送时自动运行：

| 检查项 | 结果 |
|---|---|
| 帧数和 I/P/B 计数 vs `ffprobe`（12 条流：High、Baseline、4:2:2、4:4:4、隔行/MBAFF、多 slice、长 GOP、HEVC Main、Main10、Main12、tiles/WPP） | 完全一致 |
| 语法字段 vs FFmpeg `trace_headers`（SPS/PPS/slice：`num_ref_idx_active_override_flag`、POC、`slice_qp_delta`、tiles/WPP 标志） | 完全一致 |
| 120 帧长 GOP 的 POC 回绕 | 120 个唯一 POC，0..238 |
| 解码画面 vs FFmpeg 参考帧（MP4/TS/MKV/M2TS、分片 MP4） | 8-bit 逐像素一致；10/12-bit 在舍入容差内 |
| 全部测试流跑 ASan + UBSan，变异 fuzz（CI 每轮 200、夜间 3000） | 无问题 |
| Windows 运行时：容器打开、帧导航、分栏拖拽、CSV 导出、布局持久化 | 截图/自动化验证 |
| Windows 安装器：默认安装、自定义 `/D=`、`/LANG=2052`、从 0.3.0 升级、静默卸载 | 文件、快捷方式、HKCU 卸载项、版本交接和清理均验证 |
| 运行时中英文切换及重启后保持 | 自动化/截图验证 |

未验证：每个鼠标交互的手感、超大文件、特殊容器。

## 已知限制

- 裸流没有时间戳，画面页显示最近的关键帧（带提示），不是精确目标帧；容器输入
  是精确的。
- 还没有缩略图条、slice/CTU 覆盖层、DPB/GOP 图、HRD/CPB 分析和宏块级熵解码。
- 每次解码是单线程的。
- 容器输入的 `偏移` 列是重构后的 Annex B 流内偏移，不是容器文件偏移。
- 10/12-bit 预览的 RGB 转换可能与 FFmpeg CLI 相差几个等级；解码正确性用有界
  像素容差检查。
- H.265 SEI 只列出类型，未深入解析内容。
- 多 PPS 流在多个 PPS id 之间切换时，使用最近一次激活的参数集解析。

## 目录结构

```text
src/core/     可移植码流解析（bit reader、NAL 分割、H.264/H.265 语法、流模型）
src/media/    可选 FFmpeg 解封装 + 解码层（不向外部暴露 FFmpeg 类型）
src/ui/       Win32 图形界面（帧列表、语法树、十六进制视图、画面、图表）
tools/        bsinfo 和 mtest 命令行工具
scripts/      build_ffmpeg_mingw.sh、make_icon.py
installer/    NSIS 脚本、许可证页和 build_installer.sh
res/          manifest、图标和资源
dist/         生成的安装包（被 git 忽略）
```

## 许可证

MIT，见仓库根目录 [LICENSE](../LICENSE)。

可选的 FFmpeg 构建使用 LGPL v2.1（未启用 GPL 组件）。许可证原文在
[third_party/ffmpeg-win64/COPYING.LGPLv2.1](third_party/ffmpeg-win64/COPYING.LGPLv2.1)，
精确 configure 参数在
[third_party/ffmpeg-win64/BUILD-INFO.txt](third_party/ffmpeg-win64/BUILD-INFO.txt)。
FFmpeg 通过动态链接使用，DLL 可替换为兼容的构建。
