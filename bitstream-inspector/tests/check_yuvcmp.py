#!/usr/bin/env python3
"""Check yuvcmp against ffmpeg psnr/ssim for identical and differing inputs."""
import argparse
import os
import re
import subprocess
import sys

SUMMARY = re.compile(r"^summary frames=(\d+) psnr_avg=(\S+) ssim=(\S+)$",
                     re.MULTILINE)
PSNR = re.compile(r"PSNR y:(\S+) u:(\S+) v:(\S+) average:(\S+)")
SSIM = re.compile(r"SSIM Y:(\S+)")


def run(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)


def value(text):
    return float(text) if text != "inf" else float("inf")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--yuvcmp", required=True)
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()
    vectors = args.vectors
    a = os.path.join(vectors, "yuvcmp_a.yuv")
    b = os.path.join(vectors, "yuvcmp_b.yuv")
    errors = []
    for name, path in (("a", a), ("b", b)):
        if os.path.exists(path):
            continue
        src = os.path.join(vectors, f"h264_{'bf0' if name == 'a' else 'b'}.h264")
        proc = run([args.ffmpeg, "-v", "error", "-y", "-i", src, "-f",
                    "rawvideo", "-pix_fmt", "yuv420p", path])
        if proc.returncode != 0:
            errors.append(f"cannot create {path}")
    if errors:
        for error in errors:
            print("  " + error)
        return 1
    identical = run([args.yuvcmp, a, a, "--format", "i420", "--width", "320",
                     "--height", "240", "--limit", "3"])
    if identical.returncode != 0:
        errors.append("yuvcmp failed on identical input")
    else:
        m = SUMMARY.search(identical.stdout)
        if not m or value(m.group(3)) < 0.999999:
            errors.append("identical input is not SSIM 1.0")
    differing = run([args.yuvcmp, a, b, "--format", "i420", "--width", "320",
                     "--height", "240", "--limit", "30"])
    if differing.returncode != 0:
        errors.append("yuvcmp failed on differing input")
    else:
        m = SUMMARY.search(differing.stdout)
        if not m:
            errors.append("missing summary on differing input")
        else:
            our_psnr = value(m.group(2))
            our_ssim = value(m.group(3))
            ff = run([args.ffmpeg, "-hide_banner", "-s", "320x240",
                      "-pix_fmt", "yuv420p", "-f", "rawvideo", "-i", a,
                      "-s", "320x240", "-pix_fmt", "yuv420p", "-f",
                      "rawvideo", "-i", b, "-lavfi", "psnr", "-f", "null",
                      "-"])
            ss = run([args.ffmpeg, "-hide_banner", "-s", "320x240",
                      "-pix_fmt", "yuv420p", "-f", "rawvideo", "-i", a,
                      "-s", "320x240", "-pix_fmt", "yuv420p", "-f",
                      "rawvideo", "-i", b, "-lavfi", "ssim", "-f", "null",
                      "-"])
            p = PSNR.search(ff.stderr)
            s = SSIM.search(ss.stderr)
            if not p or not s:
                errors.append("ffmpeg did not report psnr/ssim")
            else:
                if abs(our_psnr - value(p.group(4))) > 0.01:
                    errors.append(f"psnr {our_psnr} vs {p.group(4)}")
                if abs(our_ssim - value(s.group(1))) > 1e-4:
                    errors.append(f"ssim {our_ssim} vs {s.group(1)}")
    print(f"yuvcmp: {'ok' if not errors else 'FAILED'}")
    for error in errors:
        print("  " + error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
