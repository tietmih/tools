# 截图说明

[English](screenshot.md) | 简体中文

README 中对界面截图只做文字说明；默认不把二进制图片提交进 git。可以本地生成：

```bash
# 在 Windows 上，程序已构建并准备好测试码流
bsinspector.exe sample.mp4
# 然后截取窗口，例如 Win+Shift+S，或:
powershell -Command "Add-Type -AssemblyName System.Drawing; ..."
```

开发本项目时使用的截图是一条 MP4 封装的 H.264 640x360 多 slice 码流，界面上
可以看到帧列表、SPS 语法树、解码画面和码率图。截图前可以在“语言”菜单里切换
界面语言。
