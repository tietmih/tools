# third_party

English | [简体中文](README.zh-CN.md)

`ffmpeg-win64/` is where `scripts/build_ffmpeg_mingw.sh` installs a locally
built FFmpeg 9.1 (LGPL, shared libraries). Nothing in this directory is
checked in except the license text and the build description, so a fresh clone
does not contain third-party headers or binaries.

Rebuild it with:

```bash
bash scripts/build_ffmpeg_mingw.sh /path/to/ffmpeg-9.1.tar.xz \
     /tmp/ffmpeg-build "$PWD/third_party/ffmpeg-win64"
```

If the directory is absent the GUI and CLI still build; container input and the
video tab report that the media layer is unavailable.
