#!/usr/bin/env python3
"""Check slice/tile overlay geometry from bsinfo --overlay."""
import argparse
import json
import os
import re
import subprocess
import sys

OVERLAY = re.compile(r"^overlay (\d+) ([A-Z?]) lines=(\d+)$")


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr[-2000:])
        raise SystemExit(1)
    return proc.stdout


def lines(bsinfo, path):
    values = []
    for line in run([bsinfo, path, "--overlay"]).splitlines():
        m = OVERLAY.match(line)
        if m:
            values.append(int(m.group(3)))
    return values


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bsinfo", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()
    manifest = json.load(open(os.path.join(args.vectors, "manifest.json")))
    files = {v["file"] for v in manifest["raw"]}
    errors = []
    for filename in ("h264_slices.h264",):
        if filename not in files:
            errors.append(f"{filename}: not in manifest")
            continue
        path = os.path.join(args.vectors, filename)
        values = lines(args.bsinfo, path)
        if not values:
            errors.append(f"{filename}: no overlay rows")
        elif not any(v > 0 for v in values):
            errors.append(f"{filename}: overlay is empty")
    print(f"slice/tile overlay: {'ok' if not errors else 'FAILED'}")
    for error in errors:
        print("  " + error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
