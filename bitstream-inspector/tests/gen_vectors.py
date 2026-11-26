#!/usr/bin/env python3
"""Generate deterministic test vectors with the system ffmpeg.

The repository does not store media files, so CI and local runs build the
vectors from lavfi sources each time. Encoders are libx264/libx265; the
manifest written next to the files tells the other test scripts what to check.
"""
import argparse
import json
import os
import subprocess
import sys

from make_underflow import inject


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr)
        raise SystemExit(1)


def encode(ffmpeg, out_dir, name, codec, extra, frames=30, size="320x240",
           fps=25):
    path = os.path.join(out_dir, name)
    cmd = [ffmpeg, "-v", "error", "-y",
           "-f", "lavfi", "-i", f"testsrc2=size={size}:rate={fps}",
           "-frames:v", str(frames), "-c:v", "libx264" if codec == "h264"
           else "libx265"]
    cmd += extra + [path]
    run(cmd)
    return path


def remux(ffmpeg, src, dst, extra=()):
    run([ffmpeg, "-v", "error", "-y", "-i", src, "-c", "copy", *extra, dst])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    ff = args.ffmpeg

    x264 = ["-preset", "ultrafast", "-pix_fmt", "yuv420p"]
    x265 = ["-preset", "ultrafast", "-pix_fmt", "yuv420p",
            "-x265-params", "log-level=error"]

    vectors = {"gen_version": 3, "raw": [], "containers": []}

    # H.264 without B frames: exact frame-index decode comparison.
    h264_bf0 = encode(ff, out, "h264_bf0.mp4", "h264",
                      x264 + ["-g", "15", "-bf", "0"], frames=30)
    # H.264 with B frames: POC and PTS matching.
    h264_b = encode(ff, out, "h264_b.h264", "h264",
                    x264 + ["-g", "15", "-bf", "2", "-f", "h264"], frames=30)
    # Baseline with two slices per picture.
    h264_slices = encode(ff, out, "h264_slices.h264", "h264",
                         x264 + ["-profile:v", "baseline", "-bf", "0",
                                 "-x264-params", "slices=2", "-f", "h264"],
                         frames=20)
    # High 4:2:2 and a long GOP (single IDR, POC wrap-around coverage).
    h264_422 = encode(ff, out, "h264_422.h264", "h264",
                      ["-preset", "ultrafast", "-pix_fmt", "yuv422p",
                       "-profile:v", "high422", "-bf", "0", "-f", "h264"],
                      frames=20)
    h264_long = encode(ff, out, "h264_longgop.h264", "h264",
                       x264 + ["-g", "120", "-bf", "0", "-f", "h264"],
                       frames=30)
    # Interlaced/MBAFF, 4:4:4 and AUD/SEI variants.
    h264_interlaced = encode(ff, out, "h264_interlaced.h264", "h264",
                             x264 + ["-flags", "+ildct+ilme", "-x264-params",
                                     "tff=1:mbaff=1", "-f", "h264"], frames=20)
    h264_444 = encode(ff, out, "h264_444.h264", "h264",
                      ["-preset", "ultrafast", "-pix_fmt", "yuv444p",
                       "-profile:v", "high444", "-bf", "0", "-f", "h264"],
                      frames=20)
    # CBR HRD streams for the CPB0 model.
    h264_cbr = encode(
        ff, out, "h264_cbr.h264", "h264",
        ["-preset", "medium", "-pix_fmt", "yuv420p", "-b:v", "400k",
         "-maxrate", "400k", "-bufsize", "800k", "-x264-params",
         "nal-hrd=cbr:force-cfr=1", "-f", "h264"],
        frames=60)
    h265_cbr = encode(
        ff, out, "h265_cbr.h265", "h265",
        ["-preset", "ultrafast", "-pix_fmt", "yuv420p", "-b:v", "400k",
         "-maxrate", "400k", "-bufsize", "800k", "-x265-params",
         "log-level=error:hrd=1:strict-cbr=1:vbv-bufsize=800:"
         "vbv-maxrate=400:bitrate=400", "-f", "hevc"],
        frames=60)
    h264_underflow = os.path.join(out, "h264_cbr_underflow.h264")
    h265_underflow = os.path.join(out, "h265_cbr_underflow.h265")
    inject(h264_cbr, h264_underflow, "h264", before_frame=10,
           payload_bytes=100000)
    inject(h265_cbr, h265_underflow, "h265", before_frame=10,
           payload_bytes=100000)
    # HDR10 mastering display and content light level SEI.
    h264_hdr = encode(
        ff, out, "h264_hdr.h264", "h264",
        x264 + ["-g", "10", "-bf", "0", "-x264-params",
                "mastering-display=G(13250,34500)B(7500,3000)"
                "R(34000,16000)WP(15635,16450)L(10000000,1):cll=1000,400",
                "-f", "h264"],
        frames=10)

    # H.265 Main and Main10, one with B frames.
    h265_bf0 = encode(ff, out, "h265_bf0.mp4", "h265",
                      ["-preset", "ultrafast", "-pix_fmt", "yuv420p",
                       "-x265-params", "log-level=error:keyint=15:bframes=0",
                       "-tag:v", "hvc1"], frames=30)
    h265_b = encode(ff, out, "h265_b.h265", "h265",
                    x265 + ["-x265-params",
                            "log-level=error:keyint=15:bframes=2", "-f", "hevc"],
                    frames=30)
    h265_main10 = encode(ff, out, "h265_main10.h265", "h265",
                         ["-preset", "ultrafast", "-pix_fmt", "yuv420p10le",
                          "-profile:v", "main10", "-x265-params",
                          "log-level=error:keyint=10:bframes=0", "-f", "hevc"],
                         frames=20)
    h265_main12 = encode(ff, out, "h265_main12.h265", "h265",
                         ["-preset", "ultrafast", "-pix_fmt", "yuv420p12le",
                          "-profile:v", "main12", "-x265-params",
                          "log-level=error:keyint=10:bframes=0", "-f", "hevc"],
                         frames=20)
    h265_tiles = encode(ff, out, "h265_tiles.h265", "h265",
                        ["-preset", "ultrafast", "-pix_fmt", "yuv420p",
                         "-x265-params",
                         "log-level=error:keyint=10:bframes=0:wpp=1:tiles=2x2",
                         "-f", "hevc"], frames=20, size="640x480")
    # HDR10 metadata in prefix SEI.
    h265_hdr = encode(
        ff, out, "h265_hdr.h265", "h265",
        ["-preset", "ultrafast", "-pix_fmt", "yuv420p", "-x265-params",
         "log-level=error:keyint=10:bframes=0:"
         "master-display=G(13250,34500)B(7500,3000)R(34000,16000)"
         "WP(15635,16450)L(10000000,1):max-cll=1000,400",
         "-f", "hevc"],
        frames=10)

    # Annex B elementary streams extracted from the MP4.
    h264_es = os.path.join(out, "h264_bf0.h264")
    remux(ff, h264_bf0, h264_es,
          ["-bsf:v", "h264_mp4toannexb", "-f", "h264"])
    h265_es = os.path.join(out, "h265_bf0.h265")
    remux(ff, h265_bf0, h265_es,
          ["-bsf:v", "hevc_mp4toannexb", "-f", "hevc"])

    raw = [h264_es, h264_b, h264_slices, h264_422, h264_long,
           h264_interlaced, h264_444, h264_hdr, h264_cbr, h264_underflow,
           h265_es, h265_b, h265_main10, h265_main12, h265_tiles, h265_hdr,
           h265_cbr, h265_underflow]
    containers = [h264_bf0, h265_bf0]
    for src, codec, stem in ((h264_bf0, "h264", "h264_bf0"),
                             (h265_bf0, "h265", "h265_bf0")):
        for ext, fmt in (("ts", "mpegts"), ("mkv", "matroska")):
            dst = os.path.join(out, f"{stem}.{ext}")
            remux(ff, src, dst, ["-f", fmt])
            containers.append(dst)
    h264_frag = os.path.join(out, "h264_bf0-frag.mp4")
    remux(ff, h264_bf0, h264_frag, ["-movflags", "frag_keyframe+empty_moov"])
    containers.append(h264_frag)
    h264_m2ts = os.path.join(out, "h264_bf0.m2ts")
    remux(ff, h264_bf0, h264_m2ts,
          ["-f", "mpegts", "-mpegts_m2ts_mode", "1"])
    containers.append(h264_m2ts)
    h265_frag = os.path.join(out, "h265_bf0-frag.mp4")
    remux(ff, h265_bf0, h265_frag, ["-movflags", "frag_keyframe+empty_moov"])
    containers.append(h265_frag)
    for src, stem in ((h264_bf0, "h264_bf0"), (h265_bf0, "h265_bf0")):
        for ext, fmt in (("avi", "avi"), ("flv", "flv")):
            dst = os.path.join(out, f"{stem}.{ext}")
            remux(ff, src, dst, ["-f", fmt])
            containers.append(dst)
    h264_mxf = os.path.join(out, "h264_bf0.mxf")
    remux(ff, h264_bf0, h264_mxf, ["-f", "mxf"])
    containers.append(h264_mxf)
    for raw_path, stem in ((h264_interlaced, "h264_interlaced"),
                           (h264_444, "h264_444"),
                           (h265_main10, "h265_main10"),
                           (h265_main12, "h265_main12"),
                           (h265_tiles, "h265_tiles")):
        dst = os.path.join(out, f"{stem}.mkv")
        remux(ff, raw_path, dst, ["-f", "matroska"])
        containers.append(dst)

    for path in raw:
        name = os.path.basename(path)
        codec = "h264" if name.startswith("h264") else "h265"
        vectors["raw"].append({"file": name, "codec": codec})
    for path in containers:
        name = os.path.basename(path)
        codec = "h264" if name.startswith("h264") else "h265"
        vectors["containers"].append({"file": name, "codec": codec})

    manifest = os.path.join(out, "manifest.json")
    with open(manifest, "w", encoding="utf-8") as f:
        json.dump(vectors, f, indent=2)
    print(f"generated {len(raw)} raw streams and {len(containers)} containers "
          f"in {out}")


if __name__ == "__main__":
    main()
