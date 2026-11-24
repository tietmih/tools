#!/usr/bin/env python3
"""Compare bsinfo's frame table with ffprobe for the generated raw streams."""
import argparse
import csv
import json
import os
import re
import subprocess
import sys
from collections import Counter

SUMMARY_DIMS = re.compile(r"\|\s*(\d+)x(\d+)\s*\|")
TYPE_MAP = {"IDR": "I", "CRA": "I", "BLA": "I", "I": "I", "P": "P", "B": "B"}


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr[-2000:])
        raise SystemExit(1)
    return proc.stdout


def probe(ffprobe, path):
    data = json.loads(run([ffprobe, "-v", "error", "-select_streams", "v:0",
                           "-count_frames", "-show_streams", "-of", "json",
                           path]))
    stream = data["streams"][0]
    frames = json.loads(run([ffprobe, "-v", "error", "-select_streams", "v:0",
                             "-show_frames", "-show_entries",
                             "frame=pict_type", "-of", "json", path]))
    types = [f.get("pict_type", "?") for f in frames["frames"]]
    return {
        "width": stream.get("width", 0),
        "height": stream.get("height", 0),
        "frames": int(stream.get("nb_read_frames", len(types))),
        "types": types,
    }


def bsinfo_frames(bsinfo, path, csv_path):
    out = run([bsinfo, path, "--csv-frames", csv_path])
    m = SUMMARY_DIMS.search(out)
    dims = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
    rows = []
    with open(csv_path, encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            rows.append(row)
    return dims, out, rows


def compare(bsinfo, ffprobe, vectors, name):
    path = os.path.join(vectors, name)
    csv_path = os.path.join(vectors, name + ".frames.csv")
    dims, out, rows = bsinfo_frames(bsinfo, path, csv_path)
    probe_result = probe(ffprobe, path)
    errors = []

    if len(rows) != probe_result["frames"]:
        errors.append(f"frames: bsinfo {len(rows)}, ffprobe "
                      f"{probe_result['frames']}")
    if dims != (probe_result["width"], probe_result["height"]):
        errors.append(f"dimensions: bsinfo {dims[0]}x{dims[1]}, ffprobe "
                      f"{probe_result['width']}x{probe_result['height']}")

    our_types = [TYPE_MAP.get(row["type"], "?") for row in rows]
    their_types = [TYPE_MAP.get(t, t) for t in probe_result["types"]]
    if Counter(our_types) != Counter(their_types):
        errors.append(f"type counts: bsinfo {dict(Counter(our_types))}, "
                      f"ffprobe {dict(Counter(their_types))}")
    if "B" not in their_types and our_types != their_types:
        errors.append("picture type sequence differs (no B frames in stream)")

    # POC restarts at every IDR picture, so uniqueness is checked per epoch.
    segment = []
    for row in rows:
        if row.get("idr") == "1" and segment:
            if len(set(segment)) != len(segment):
                errors.append("POC values repeat inside one IDR period")
                break
            segment = []
        segment.append(int(row["poc"]))
    if len(set(segment)) != len(segment):
        errors.append("POC values repeat inside one IDR period")

    if "ERROR:" in out:
        errors.append("bsinfo reported NAL errors")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffprobe", default="ffprobe")
    ap.add_argument("--bsinfo", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()

    manifest = json.load(open(os.path.join(args.vectors, "manifest.json")))
    failures = []
    for vector in manifest["raw"]:
        errors = compare(args.bsinfo, args.ffprobe, args.vectors,
                         vector["file"])
        print(f"{vector['file']}: {'ok' if not errors else 'FAILED'}")
        for error in errors:
            print("  " + error)
        failures.extend(errors)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
