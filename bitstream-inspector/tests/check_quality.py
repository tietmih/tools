#!/usr/bin/env python3
"""Compare mtest PSNR/SSIM with ffmpeg's psnr/ssim filters."""
import argparse
import os
import re
import subprocess
import sys

SUMMARY = re.compile(
    r"^quality_summary frames=(\d+) psnr_y=(\S+) psnr_u=(\S+) "
    r"psnr_v=(\S+) psnr_avg=(\S+) ssim=(\S+)$", re.MULTILINE)
PSNR = re.compile(r"PSNR y:(\S+) u:(\S+) v:(\S+) average:(\S+)")
SSIM = re.compile(r"SSIM Y:(\S+)")


def run(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)


def value(text):
    return float(text) if text != "inf" else float("inf")


def check(mtest, ffmpeg, vectors):
    test = os.path.join(vectors, "h264_bf0.h264")
    ref_stream = os.path.join(vectors, "h264_b.h264")
    ref = os.path.join(vectors, "h264_bf0.quality.ref.yuv")
    proc = run([ffmpeg, "-v", "error", "-y", "-i", ref_stream, "-f",
                "rawvideo", "-pix_fmt", "yuv420p", ref])
    if proc.returncode != 0:
        return ["cannot generate reference YUV"]
    ours = run([mtest, test, "--ref", ref, "--quality"])
    if ours.returncode != 0:
        return ["mtest quality failed: " + ours.stderr[-200:]]
    m = SUMMARY.search(ours.stdout)
    if not m:
        return ["missing quality summary"]
    frames = int(m.group(1))
    if frames < 10:
        return [f"only {frames} quality frames"]
    ff_psnr = run([ffmpeg, "-hide_banner", "-i", test, "-s", "320x240",
                   "-pix_fmt", "yuv420p", "-r", "25", "-f", "rawvideo",
                   "-i", ref, "-lavfi", "psnr", "-f", "null", "-"])
    ff_ssim = run([ffmpeg, "-hide_banner", "-i", test, "-s", "320x240",
                   "-pix_fmt", "yuv420p", "-r", "25", "-f", "rawvideo",
                   "-i", ref, "-lavfi", "ssim", "-f", "null", "-"])
    p = PSNR.search(ff_psnr.stderr)
    s = SSIM.search(ff_ssim.stderr)
    errors = []
    if not p or not s:
        return ["ffmpeg did not report psnr/ssim"]
    for index, key in ((2, "psnr_y"), (3, "psnr_u"), (4, "psnr_v"),
                       (5, "psnr_avg")):
        theirs = value(p.group(index - 1))
        mine = value(m.group(index))
        if abs(mine - theirs) > 0.01:
            errors.append(f"{key}: mtest {mine}, ffmpeg {theirs}")
    theirs_ssim = value(s.group(1))
    mine_ssim = value(m.group(6))
    if abs(mine_ssim - theirs_ssim) > 1e-4:
        errors.append(f"ssim: mtest {mine_ssim}, ffmpeg {theirs_ssim}")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mtest", required=True)
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()
    errors = check(args.mtest, args.ffmpeg, args.vectors)
    print(f"quality metrics: {'ok' if not errors else 'FAILED'}")
    for error in errors:
        print("  " + error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
