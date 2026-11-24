#!/usr/bin/env python3
"""Decode selected frames through mtest and compare them with ffmpeg.

The container vectors are encoded without B frames, so decode order equals
display order and frame N in mtest is frame N in ffmpeg's select filter. PPM
pixels must be identical; any difference means the media layer regressed.
"""
import argparse
import json
import os
import re
import subprocess
import sys

PPM_HEADER = re.compile(rb"^P6\s+(\d+)\s+(\d+)\s+(\d+)\s")
SUMMARY = re.compile(r"^format=\S+ codec=\S+ (\d+)x(\d+)")
DECODED = re.compile(r"^decoded frame=(\d+) (\d+)x(\d+) keyframe_only=(\d)", re.MULTILINE)
FRAMES = re.compile(r"^frames=(\d+)", re.MULTILINE)


def run(cmd, check=True):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr.decode("utf-8", "replace")[-2000:])
        raise SystemExit(1)
    return proc


def read_ppm(path):
    data = open(path, "rb").read()
    m = PPM_HEADER.match(data)
    if not m:
        raise SystemExit(f"{path}: not a P6 PPM file")
    return int(m.group(1)), int(m.group(2)), data[m.end():]


def frame_count(ffprobe, path):
    proc = run([ffprobe, "-v", "error", "-select_streams", "v:0",
                "-count_frames", "-show_entries", "stream=nb_read_frames",
                "-of", "default=nw=1:nk=1", path])
    return int(proc.stdout.split()[0])


def check_container(mtest, ffmpeg, ffprobe, vectors, name):
    path = os.path.join(vectors, name)
    errors = []
    expected_frames = frame_count(ffprobe, path)
    out = run([mtest, path]).stdout.decode("utf-8", "replace")
    m = FRAMES.search(out)
    if not m:
        return [f"{name}: mtest did not report a frame count"]
    if int(m.group(1)) != expected_frames:
        errors.append(f"{name}: mtest frames {m.group(1)}, ffprobe "
                      f"{expected_frames}")

    for index in sorted({0, expected_frames // 2, expected_frames - 1}):
        ours = os.path.join(vectors, f"{name}.{index}.ppm")
        ref = os.path.join(vectors, f"{name}.{index}.ref.ppm")
        proc = run([mtest, path, "--frame", str(index), "--ppm", ours],
                   check=False)
        text = proc.stdout.decode("utf-8", "replace")
        d = DECODED.search(text)
        if proc.returncode != 0 or not d:
            errors.append(f"{name}: decode frame {index} failed: "
                          f"{proc.stderr.decode('utf-8', 'replace')[-200:]}")
            continue
        if int(d.group(4)) != 0:
            errors.append(f"{name}: frame {index} decoded as a keyframe "
                          f"fallback")
        run([ffmpeg, "-v", "error", "-y", "-i", path, "-vf",
             f"select='eq(n\\,{index})'", "-fps_mode", "passthrough", "-frames:v", "1",
             "-pix_fmt", "rgb24", ref])
        w1, h1, pix1 = read_ppm(ours)
        w2, h2, pix2 = read_ppm(ref)
        if (w1, h1) != (w2, h2):
            errors.append(f"{name}: frame {index} size {w1}x{h1} vs "
                          f"{w2}x{h2}")
        elif pix1 != pix2:
            diffs = [abs(a - b) for a, b in zip(pix1, pix2)]
            errors.append(
                f"{name}: frame {index} pixels differ (max {max(diffs)}, "
                f"mean {sum(diffs) / len(diffs):.3f}, "
                f"{sum(1 for d in diffs if d)}/{len(diffs)} bytes)")
    return errors


def check_raw_fallback(mtest, vectors):
    name = "h264_bf0.h264"
    path = os.path.join(vectors, name)
    ppm = os.path.join(vectors, name + ".fallback.ppm")
    proc = run([mtest, path, "--frame", "5", "--ppm", ppm], check=False)
    text = proc.stdout.decode("utf-8", "replace")
    if proc.returncode != 0:
        return [f"{name}: raw decode failed"]
    m = DECODED.search(text)
    if not m:
        return [f"{name}: no decoded frame line"]
    if int(m.group(4)) != 1:
        return [f"{name}: raw stream should report keyframe_only=1"]
    return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--ffprobe", default="ffprobe")
    ap.add_argument("--mtest", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()

    manifest = json.load(open(os.path.join(args.vectors, "manifest.json")))
    failures = []
    for vector in manifest["containers"]:
        errors = check_container(args.mtest, args.ffmpeg, args.ffprobe,
                                 args.vectors, vector["file"])
        print(f"{vector['file']}: {'ok' if not errors else 'FAILED'}")
        for error in errors:
            print("  " + error)
        failures.extend(errors)
    errors = check_raw_fallback(args.mtest, args.vectors)
    print(f"raw keyframe fallback: {'ok' if not errors else 'FAILED'}")
    failures.extend(errors)
    for error in errors:
        print("  " + error)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
