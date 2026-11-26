#!/usr/bin/env python3
"""Compare parsed HDR SEI fields with ffmpeg trace_headers."""
import argparse
import json
import os
import re
import subprocess
import sys

TRACE_LINE = re.compile(
    r"^\[trace_headers[^\]]*\]\s+(\d+)\s+(\S+)\s+[01]+\s+=\s+(-?\d+)\s*$")
TREE_LEAF = re.compile(
    r"^\s*([^=]+?)\s*=\s*(.*?)\s+\[(\d+)\+(\d+)\]\s*$")
BSINFO_NAL = re.compile(
    r"^\[\s*(\d+)\]\s+off=\d+\s+size=\d+\s+type=(\d+)")

FIELDS = [
    "display_primaries_x[0]", "display_primaries_y[0]",
    "display_primaries_x[1]", "display_primaries_y[1]",
    "display_primaries_x[2]", "display_primaries_y[2]",
    "white_point_x", "white_point_y",
    "max_display_mastering_luminance", "min_display_mastering_luminance",
    "max_content_light_level", "max_pic_average_light_level",
]


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr[-2000:])
        raise SystemExit(1)
    return proc


def trace_fields(ffmpeg, path):
    text = run([ffmpeg, "-hide_banner", "-i", path, "-c:v", "copy",
                "-bsf:v", "trace_headers", "-f", "null", "-"]).stderr
    fields = {}
    for line in text.splitlines():
        m = TRACE_LINE.match(line)
        if m:
            fields.setdefault(m.group(2), []).append(int(m.group(3)))
    return fields


def tree_fields(bsinfo, path, index):
    text = run([bsinfo, path, "--tree", str(index)]).stdout
    fields = {}
    for line in text.splitlines():
        m = TREE_LEAF.match(line)
        if not m:
            continue
        name = m.group(1).strip()
        try:
            value = int(m.group(2).strip(), 0)
        except ValueError:
            continue
        fields.setdefault(name, []).append(value)
    return fields


def sei_nals(bsinfo, path, codec):
    text = run([bsinfo, path]).stdout
    indices = []
    for line in text.splitlines():
        m = BSINFO_NAL.match(line)
        if not m:
            continue
        nal_type = int(m.group(2))
        if codec == "h264" and nal_type == 6:
            indices.append(int(m.group(1)))
        elif codec == "h265" and nal_type in (39, 40):
            indices.append(int(m.group(1)))
    return text, indices


def check_stream(ffmpeg, bsinfo, path, codec):
    name = os.path.basename(path)
    summary, indices = sei_nals(bsinfo, path, codec)
    errors = []
    if not indices:
        return [f"{name}: no SEI NALs found"]
    hdr = run([bsinfo, path, "--hdr"]).stdout
    if "consistent=1" not in hdr:
        errors.append(f"{name}: HDR metadata consistency check failed")
    if "HDR10" not in summary:
        errors.append(f"{name}: stream summary has no HDR label")
    if "mastering_display_colour_volume" not in summary:
        errors.append(f"{name}: mastering display SEI not summarized")
    if "content_light_level_info" not in summary:
        errors.append(f"{name}: content light level SEI not summarized")

    trace = trace_fields(ffmpeg, path)
    fields = {}
    for index in indices:
        for key, values in tree_fields(bsinfo, path, index).items():
            fields.setdefault(key, []).extend(values)

    compared = 0
    for field in FIELDS:
        if field not in trace:
            continue
        if field not in fields:
            errors.append(f"{name}: missing {field}")
            continue
        if fields[field][0] != trace[field][0]:
            errors.append(f"{name}: {field}: trace {trace[field][0]}, "
                          f"bsinfo {fields[field][0]}")
        else:
            compared += 1
    if compared < 8:
        errors.append(f"{name}: only {compared} SEI fields compared")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--bsinfo", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()

    manifest = json.load(open(os.path.join(args.vectors, "manifest.json")))
    targets = {v["file"]: v["codec"] for v in manifest["raw"]}
    failures = []
    for filename in ("h264_hdr.h264", "h265_hdr.h265"):
        if filename not in targets:
            failures.append(f"{filename}: vector missing")
            continue
        path = os.path.join(args.vectors, filename)
        errors = check_stream(args.ffmpeg, args.bsinfo, path,
                              targets[filename])
        print(f"{filename}: {'ok' if not errors else 'FAILED'}")
        failures.extend(errors)
    for error in failures:
        print("  " + error)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
