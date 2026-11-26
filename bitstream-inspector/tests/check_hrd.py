#!/usr/bin/env python3
"""Compare buffering-period and picture-timing fields with trace_headers."""
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
        try:
            value = int(m.group(2).strip(), 0)
        except ValueError:
            continue
        fields.setdefault(m.group(1).strip(), []).append(value)
    return fields


def check(ffmpeg, bsinfo, path, codec):
    name = os.path.basename(path)
    summary = run([bsinfo, path]).stdout
    if codec == "h264":
        wanted = ["initial_cpb_removal_delay[0]",
                  "initial_cpb_removal_delay_offset[0]",
                  "cpb_removal_delay", "dpb_output_delay"]
        nal_types = (6,)
    else:
        wanted = ["nal_initial_cpb_removal_delay[0]",
                  "nal_initial_cpb_removal_offset[0]",
                  "au_cpb_removal_delay_minus1", "pic_dpb_output_delay"]
        nal_types = (39, 40)
    if "buffering_period" not in summary or "pic_timing" not in summary:
        return [f"{name}: missing buffering_period/pic_timing summary"]

    indices = []
    for line in summary.splitlines():
        m = BSINFO_NAL.match(line)
        if m and int(m.group(2)) in nal_types:
            indices.append(int(m.group(1)))
    fields = {}
    for index in indices:
        for key, values in tree_fields(bsinfo, path, index).items():
            fields.setdefault(key, []).extend(values)

    trace = trace_fields(ffmpeg, path)
    errors = []
    compared = 0
    for field in wanted:
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
    if compared < 4:
        errors.append(f"{name}: only {compared} HRD SEI fields compared")
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
    for filename in ("h264_cbr.h264", "h265_cbr.h265"):
        if filename not in targets:
            failures.append(f"{filename}: vector missing")
            continue
        path = os.path.join(args.vectors, filename)
        errors = check(args.ffmpeg, args.bsinfo, path, targets[filename])
        print(f"{filename}: {'ok' if not errors else 'FAILED'}")
        failures.extend(errors)
    for error in failures:
        print("  " + error)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
