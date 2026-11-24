# Screenshot

The README references `screenshot.png`. It is not checked in by default because
it is a binary asset; generate it locally:

```bash
# Windows, with the app built and a test clip available
bsinspector.exe sample.mp4
# then capture the window, e.g. with Win+Shift+S or:
powershell -Command "Add-Type -AssemblyName System.Drawing; ..."
```

Save the result as `docs/screenshot.png`.

The screenshot used while developing this project showed an H.264 640x360
multi-slice clip in an MP4 container with the frame list, the SPS syntax tree,
the decoded frame and the bitrate chart visible.
