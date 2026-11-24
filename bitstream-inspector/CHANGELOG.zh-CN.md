# 更新日志

[English](CHANGELOG.md) | 简体中文

记录 Bitstream Inspector 的重要变更。本工具独立于仓库中其他工具进行版本管理和
发布，tag 格式为 `bitstream-inspector-v<version>`。

## [0.3.1] - 2026-11-24

### 新增

- GitHub Actions 持续集成。每次推送和拉取请求都会在 Linux 上构建、与 FFmpeg
  对比解析结果和解码帧、运行 ASan/UBSan 和变异 fuzz、交叉构建 Windows 界面与
  安装包，并在 Windows 上运行程序及安装/卸载往返测试。
- 可移植解析核心的单元测试（`tests/unit_tests.cpp`）和脚本化回归套件
  （`tests/`），测试码流由 FFmpeg 现场生成，不向 git 存放媒体文件。
- tag 驱动发布：推送 `bitstream-inspector-v<version>` 后自动运行测试、校验
  tag、源码版本与双语文档一致，并发布安装包和 `SHA256SUMS`。
- 依赖跟踪：内置 FFmpeg 的版本和 SHA-256 记录在 `ci/deps.env`；每周工作流与
  上游稳定版比较并创建跟踪 issue，Dependabot 负责更新 GitHub Actions。
- CI/CD 扩展：Linux x64/arm64 测试、MSVC 构建、覆盖率报告、Windows GUI/安装器
  冒烟测试、便携 ZIP 和发布 dry-run 开关。
- GitHub Actions 固定到 commit SHA。

### 修复

- H.264 POC 类型 1/2：`FrameNumOffset` 现在会跨同一个 IDR 周期内的
  `frame_num` 回绕累加。此前 POC 在第二次回绕后停止增长并重复先前的值。

### 变更

- Windows 内置 FFmpeg 从 7.1.5 升级到 9.1（LGPL，仍未启用 GPL 组件）；Linux
  测试参考版本也改为同一固定版本。
- 版本号统一升级到 0.3.1：CMake、Windows 版本资源、About 对话框和安装器。
- 发布检查清单改为 tag 驱动；仅在 Actions 不可用时才手工创建发布附件。

## [0.3.0] - 2026-11-24

### 新增

- 运行时英文/简体中文界面切换（“语言”菜单），选择保存在
  `%APPDATA%\BitstreamInspector\settings.ini`。
- 双语文档：README、更新日志、截图说明和发布说明均提供英文与 `.zh-CN.md`
  两个版本，文件顶部带语言切换链接。
- 安装器支持英文/简体中文语言选择，许可证页双语。
- 帧列表列宽适配中文标题。

### 变更

- 版本号统一升级到 0.3.0：CMake、Windows 版本资源、About 对话框和安装器。

### 修复

- PrintWindow/WM_PRINTCLIENT 提供的 DC 没有兼容位图时，图表和十六进制视图改为
  直接绘制到目标 DC。
- 安装器在程序刚关闭时重试删除 exe；Release 校验和文件使用纯文件名。

### 验证

- Windows 上验证运行时语言切换及重启后保持。
- 验证安装器的安装/启动/卸载，以及两种界面语言。
- 原有解析/解码回归（ffprobe、trace_headers、PSNR、ASan/UBSan、fuzz）保持通过。

## [0.2.0] - 2026-11-24

### 新增

- 通过可选的 FFmpeg（LGPL）媒体层支持 MP4/MOV、MPEG-TS/M2TS 和
  Matroska/WebM 容器；Annex B 裸流不受影响。
- Hex/Video 标签页中的解码画面预览，在后台线程解码。容器输入按 PTS 精确解码
  选中帧；无时间戳的裸流显示最近关键帧并标注提示。
- 帧列表和 CSV 导出中的显示时间戳（PTS）。
- NSIS 按用户安装的 Windows 安装包、应用图标和 0.2.0 版本资源。
- `bsinfo` 命令行工具作为可选组件安装。

### 修复

- H.264 P slice 读取 `num_ref_idx_active_override_flag`；此前每个 P slice 都会
  错位一位，导致参考列表和 QP 字段错误。
- H.265 `num_pic_total_curr` 在 slice header 代码块内被同名变量遮蔽，导致参考
  列表修改语法永远不读取；改为规范要求的 `> 1` 条件。
- 头解析失败的 VCL NAL 不再被归入下一帧。
- 参数集做范围校验，且只在完整解析成功后启用，避免畸形输入触发的未定义行为和
  超大内存分配。
- CSV 时间转换、分栏拖拽偏移/死区、导航按钮刷新、File → Exit 保存布局、
  零 AU 打开处理、EnsureVisible、高分辨率滚轮增量、非法设置默认值。

### 验证

- 8 条 H.264/H.265 码流的帧数和 I/P/B 类型与 `ffprobe` 一致。
- SPS/PPS/slice 字段与 FFmpeg `trace_headers` 一致，包括 P slice 标志。
- 真实 MP4/TS/MKV（H.264/H.265）解码帧与 FFmpeg 参考帧 PSNR = inf（逐像素一致）。
- ASan + UBSan 跑完测试集，并做 300 轮变异 fuzz，无问题。
- Windows 安装器：静默安装、启动已安装程序、静默卸载、快捷方式和卸载注册表项
  均验证。

### 已知限制

- 裸流没有时间戳，Video 页显示最近关键帧，而不是精确目标帧。
- 还没有缩略图条、slice/CTU 覆盖层、DPB/GOP 图、HRD/CPB 分析和宏块级熵解码。
- 每次解码是单线程的。
- 容器输入的 `Offset` 列是重构后的 Annex B 流内偏移，不是容器文件偏移。

## [0.1.0] - 2026-11-24

- 初版 Win32 分析器：Annex B H.264/H.265 解析、帧列表、语法树、十六进制视图、
  码率图和 CSV 导出。
