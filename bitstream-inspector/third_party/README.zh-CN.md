# third_party

[English](README.md) | 简体中文

`ffmpeg-win64/` 是 `scripts/build_ffmpeg_mingw.sh` 安装本地构建的
FFmpeg 9.1（LGPL，共享库）的位置。除许可证文本和构建说明外，这个目录下的
内容都不进 git，因此全新克隆不会包含第三方头文件或二进制。

重新构建：

```bash
bash scripts/build_ffmpeg_mingw.sh /path/to/ffmpeg-9.1.tar.xz \
     /tmp/ffmpeg-build "$PWD/third_party/ffmpeg-win64"
```

如果该目录不存在，图形界面和命令行工具仍然可以构建；容器输入和画面页会提示
媒体层不可用。
