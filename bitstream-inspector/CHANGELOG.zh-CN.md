# 更新日志

[English](CHANGELOG.md) | 简体中文

记录 Bitstream Inspector 的重要变更。本工具独立于仓库中其他工具进行版本管理和
发布，tag 格式为 `bitstream-inspector-v<version>`。

## [0.5.0] - 2026-11-27

### 新增

- 质量 GUI：加载参考 YUV，在后台逐帧计算 PSNR/SSIM/MSE/MSAD，显示质量图表和
  参考差异视图，并可将逐帧指标导出为 CSV。
- 码流对比：支持按显示顺序/POC/时间同步；相减、温度图、PSNR 裁剪、水平分割、
  垂直分割视图；最多四路码流按 2x2 网格对比，额外码流与第一路同步。
- 独立 YUV 查看器：I420/I422/I444/P010/P210/P012/P212、矩阵/范围选择、逐帧
  导航；可打开第二个文件做并排或差异比较并显示 PSNR/SSIM。
- `yuvcmp` CLI：逐帧比较两个 raw planar YUV 文件。
- 质量指标、`mtest --quality` 输出和 CSV 增加 MSE/MSAD。
- 码率图表支持 1/2/5 秒窗口切换。
- HDR10+（SMPTE ST 2094-40）识别与完整动态元数据解析：处理窗口、目标显示
  亮度、maxscl、平均 maxRGB、分位数、tone mapping、饱和度；帧 CSV 增加逐帧
  HDR10+ 列，状态栏显示摘要。
- `bsinfo --hdr` 和合规报告增加 HDR 元数据一致性检查（MaxCLL/MaxFALL 与主控
  显示、HDR10+ 与 PQ）。
- 画面支持 Y/Cb/Cr 单分量显示。
- YUV 导出支持原生格式以及 I420/I422/I444/P010/P210。

### 修复

- >8-bit raw YUV 导出改为低位对齐，与 ffmpeg rawvideo 和参考 YUV 读取器一致。
- HDR10+ prefix SEI 归属下一帧、HEVC suffix SEI 归属当前帧，不再错误污染前一帧。

### 已知限制

- 内置 FFmpeg 构建未启用 HEIC 和 MPEG-1 System 容器；未实现 VQM/NQI/VMAF。
- 多路对比窗口中差异指标只作用于前两路，附加码流显示正常画面。

## [0.4.0] - 2026-11-26

### 新增

- H.264/H.265 SEI 解析：MDCV、CLL、ATC、recovery point、T.35 用户数据和
  user-data UUID；HDR10/HLG 摘要、修正 payload 名称、H.265 prefix/suffix SEI。
- H.264 VUI 与 H.265 VPS/SPS HRD 入库，解析 `buffering_period`/`pic_timing`，
  实现带 buffering period 锚点的 CPB0 占用模型、underflow/overflow、图表和
  CSV/CLI 输出。
- 原生 YUV 平面解码与导出、画面 Y/Cb/Cr/RGB 探针、仅预览生效的
  BT.601/709/2020 与 limited/full 控制、WIC PNG/BMP 和 raw planar YUV 导出。
- 懒加载缩略图条、GOP 边界、上一个/下一个 GOP，以及裸流精确帧（含 B 帧）。
- H.264（MMCO/列表修改）与 H.265（RPS/长期参考）的 DPB/参考列表推导、Refs
  标签页、缺失参考报告和 `bsinfo --refs`。
- 参考 YUV 的 PSNR/SSIM 比较（与 ffmpeg psnr/ssim 一致）；
  `mtest --ref/--quality` 和 CSV 输出。
- 按 NAL 类别的逐 AU 比特分配、1 秒窗口码率、堆叠图表和 CSV。
- AVI、FLV、MXF、MPEG-PS 解封装；AVI/FLV/MXF 回归向量。
- 画面页 slice/CTB/tile 边界叠加。
- 独立 MPEG-TS 扫描器：188/192/204 字节包、PID/stream type、PCR 抖动、
  continuity counter 错误和 TEI/scrambling 统计。
- `bsinfo --json`/`--batch` 及 schema 文档；`mtest --range` YUV 范围导出。
- H.264/H.265 Annex A 合规检查：图像大小、MB/CTB 速率、DPB 和平均码率。
- 基础并排 Compare Streams 窗口。

### 修复

- H.265 VPS 现在解析 ordering、layer sets、timing/HRD；NAL/VCL HRD 的 CPB
  表分别读取。
- MPEG-PS 按 Annex B 处理；不支持的 MXF 向量不再导致向量生成失败。
- 裸流从 GOP 起点解码，精确匹配选中帧（含 B 帧）；8/10-bit 原生 YUV 平面与
  ffmpeg 逐字节一致。

### 已知限制

- GUI 质量曲线/差异视图和 POC/时间绑定对比尚未实现；PSNR/SSIM 和 TS 分析
  目前以 CLI 为主。
- H.265 tile 布局已实现，但当前 x265 构建未生成 tile 测试向量；IVF/MPEG-PS
  只有 demuxer 覆盖。
- GUI YUV 范围导出只支持当前帧；范围导出请用 `mtest --range`。

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
