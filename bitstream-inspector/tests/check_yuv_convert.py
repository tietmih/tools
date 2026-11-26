#!/usr/bin/env python3
"""Check mtest --yuv-format output against ffmpeg rawvideo conversions."""
import argparse
import os
import subprocess
import sys


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr[-2000:])
        raise SystemExit(1)
    return proc.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mtest", required=True)
    ap.add_argument("--yuvcmp", required=True)
    ap.add_argument("--ffmpeg", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()
    src = os.path.join(args.vectors, "h264_bf0.h264")
    if not os.path.exists(src):
        print("yuv conversion: vector missing")
        return 1
    work = os.path.join(args.vectors, "yuv_convert")
    os.makedirs(work, exist_ok=True)
    errors = []
    for name, fmt, ff_fmt, exact in (
            ("i420", "i420", "yuv420p", True),
            ("i422", "i422", "yuv422p", False),
            ("p010", "p010", "yuv420p10le", True)):
        out = os.path.join(work, name + "_mtest.yuv")
        ref = os.path.join(work, name + "_ffmpeg.yuv")
        run([args.mtest, src, "--frame", "5", "--yuv", out, "--yuv-format",
             fmt])
        run([args.ffmpeg, "-v", "error", "-y", "-i", src, "-vf",
             "select='eq(n\\,5)'", "-vsync", "0", "-frames:v", "1",
             "-pix_fmt", ff_fmt, "-f", "rawvideo", ref])
        compare = run([args.yuvcmp, out, ref, "--format", fmt, "--width",
                       "320", "--height", "240", "--limit", "1"])
        psnr = None
        for line in compare.splitlines():
            if line.startswith("summary"):
                for token in line.split():
                    if token.startswith("psnr_avg="):
                        psnr = float(token.split("=", 1)[1])
        if psnr is None:
            errors.append(f"{name}: no PSNR summary")
        elif exact and psnr < 99.0:
            errors.append(f"{name}: conversion differs from ffmpeg (PSNR {psnr})")
        elif not exact and psnr < 40.0:
            errors.append(f"{name}: conversion differs too much (PSNR {psnr})")
    print(f"yuv conversion: {'ok' if not errors else 'FAILED'}")
    for error in errors:
        print("  " + error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
