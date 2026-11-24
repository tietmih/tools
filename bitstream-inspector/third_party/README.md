# third_party

`ffmpeg-win64/` is where `scripts/build_ffmpeg_mingw.sh` installs a locally
built FFmpeg 7.1.5 (LGPL, shared libraries). Nothing in this directory is
checked in except the license text and the build description, so a fresh clone
does not contain third-party headers or binaries.

Rebuild it with:

```bash
bash scripts/build_ffmpeg_mingw.sh /path/to/ffmpeg-n7.1.5.tar.gz \
     /tmp/ffmpeg-build "$PWD/third_party/ffmpeg-win64"
```

If the directory is absent the GUI and CLI still build; container input and the
video tab report that the media layer is unavailable.
