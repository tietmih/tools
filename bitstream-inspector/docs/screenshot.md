# Screenshot

English | [简体中文](screenshot.zh-CN.md)

The READMEs reference screenshots conceptually; binary images are not checked
in by default. Generate one locally:

```bash
# Windows, with the app built and a test clip available
bsinspector.exe sample.mp4
# then capture the window, e.g. with Win+Shift+S or:
powershell -Command "Add-Type -AssemblyName System.Drawing; ..."
```

The screenshot used while developing this project showed an H.264 640x360
multi-slice clip in an MP4 container with the frame list, the SPS syntax tree,
the decoded frame and the bitrate chart visible. The UI language can be
switched from the Language menu before capturing.
