#!/usr/bin/env python3
"""Decode selected frames through mtest and compare them with ffmpeg.

The container vectors are encoded without B frames, so decode order equals
display order and frame N in mtest is frame N in ffmpeg's select filter. PPM
pixels must be identical; any difference means the media layer regressed.
"""
import argparse
import hashlib
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
    expected = {}
    expected_frames = frame_count(ffprobe, path)
    limited = "main10" in name or "main12" in name
    max_tolerance = 160 if limited else 0
    mean_tolerance = 5.0 if limited else 0.0
    out = run([mtest, path]).stdout.decode("utf-8", "replace")
    m = FRAMES.search(out)
    if not m:
        return [f"{name}: mtest did not report a frame count"], expected
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
        expected[str(index)] = {
            "sha256": hashlib.sha256(pix2).hexdigest(),
            "ref": os.path.basename(ref),
            "width": w2,
            "height": h2,
            "max_tolerance": max_tolerance,
            "mean_tolerance": mean_tolerance,
        }
        if (w1, h1) != (w2, h2):
            errors.append(f"{name}: frame {index} size {w1}x{h1} vs "
                          f"{w2}x{h2}")
        elif pix1 != pix2:
            diffs = [abs(a - b) for a, b in zip(pix1, pix2)]
            max_diff = max(diffs)
            mean_diff = sum(diffs) / len(diffs)
            if max_diff > max_tolerance or mean_diff > mean_tolerance:
                errors.append(
                    f"{name}: frame {index} pixels differ (max {max_diff}, "
                    f"mean {mean_diff:.3f}, "
                    f"{sum(1 for d in diffs if d)}/{len(diffs)} bytes)")
    return errors, expected


def check_raw_exact(mtest, ffmpeg, vectors):
    targets = (("h264_bf0.h264", 5), ("h264_b.h264", 4),
               ("h265_b.h265", 5))
    errors = []
    for name, ref_index in targets:
        path = os.path.join(vectors, name)
        ppm = os.path.join(vectors, name + ".exact.ppm")
        ref = os.path.join(vectors, name + ".exact.ref.ppm")
        proc = run([mtest, path, "--frame", "5", "--ppm", ppm], check=False)
        text = proc.stdout.decode("utf-8", "replace")
        if proc.returncode != 0:
            errors.append(f"{name}: raw decode failed")
            continue
        m = DECODED.search(text)
        if not m:
            errors.append(f"{name}: no decoded frame line")
            continue
        if int(m.group(4)) != 0:
            errors.append(f"{name}: raw stream should decode the exact frame")
            continue
        run([ffmpeg, "-v", "error", "-y", "-i", path, "-vf",
             f"select='eq(n\\,{ref_index})'", "-fps_mode", "passthrough",
             "-frames:v", "1", "-pix_fmt", "rgb24", ref])
        w1, h1, pix1 = read_ppm(ppm)
        w2, h2, pix2 = read_ppm(ref)
        if (w1, h1) != (w2, h2):
            errors.append(f"{name}: raw frame size {w1}x{h1} vs {w2}x{h2}")
        elif pix1 != pix2:
            errors.append(f"{name}: raw exact frame differs from ffmpeg")
    return errors

def check_yuv_export(mtest, ffmpeg, vectors):
    targets = (("h264_bf0.h264", "yuv420p"),
               ("h265_main10.h265", "yuv420p10le"))
    errors = []
    for name, pix_fmt in targets:
        path = os.path.join(vectors, name)
        ours = os.path.join(vectors, name + ".exact.yuv")
        ref = os.path.join(vectors, name + ".exact.ref.yuv")
        proc = run([mtest, path, "--frame", "5", "--yuv", ours], check=False)
        if proc.returncode != 0:
            errors.append(f"{name}: YUV export failed")
            continue
        run([ffmpeg, "-v", "error", "-y", "-i", path, "-vf",
             "select='eq(n\\,5)'", "-fps_mode", "passthrough", "-frames:v",
             "1", "-f", "rawvideo", "-pix_fmt", pix_fmt, ref])
        if open(ours, "rb").read() != open(ref, "rb").read():
            errors.append(f"{name}: raw YUV planes differ from ffmpeg")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--ffprobe", default="ffprobe")
    ap.add_argument("--mtest", required=True)
    ap.add_argument("--vectors", required=True)
    ap.add_argument("--expected-out",
                    help="write reference PPM hashes for the Windows runtime")
    args = ap.parse_args()

    manifest = json.load(open(os.path.join(args.vectors, "manifest.json")))
    failures = []
    expected_all = {}
    for vector in manifest["containers"]:
        errors, expected = check_container(
            args.mtest, args.ffmpeg, args.ffprobe, args.vectors,
            vector["file"])
        expected_all[vector["file"]] = expected
        print(f"{vector['file']}: {'ok' if not errors else 'FAILED'}")
        for error in errors:
            print("  " + error)
        failures.extend(errors)
    errors = check_raw_exact(args.mtest, args.ffmpeg, args.vectors)
    print(f"raw exact frame: {'ok' if not errors else 'FAILED'}")
    yuv_errors = check_yuv_export(args.mtest, args.ffmpeg, args.vectors)
    print(f"raw YUV export: {'ok' if not yuv_errors else 'FAILED'}")
    errors.extend(yuv_errors)
    failures.extend(errors)
    for error in errors:
        print("  " + error)
    if args.expected_out:
        with open(args.expected_out, "w", encoding="utf-8") as f:
            json.dump(expected_all, f, indent=2, sort_keys=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
