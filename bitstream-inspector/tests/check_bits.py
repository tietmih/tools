#!/usr/bin/env python3
"""Check the per-AU bit allocation sums and window bitrate."""
import argparse
import json
import os
import re
import subprocess
import sys

BITS = re.compile(
    r"^bits (\d+) ([A-Z?]) total=(\d+) ps=(\d+) sei=(\d+) hdr=(\d+) "
    r"payload=(\d+) filler=(\d+) other=(\d+)$")
RATE = re.compile(r"^window_bitrate_bps=(\S+) frames=(\d+)$")


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr[-2000:])
        raise SystemExit(1)
    return proc.stdout


def check(bsinfo, vectors, filename):
    path = os.path.join(vectors, filename)
    if not os.path.exists(path):
        return [f"{filename}: vector missing"]
    text = run([bsinfo, path, "--bits"])
    frames = 0
    rate = None
    errors = []
    for line in text.splitlines():
        m = BITS.match(line)
        if m:
            frames += 1
            values = [int(m.group(i)) for i in range(3, 9)]
            total = values[0]
            parts = sum(values[1:])
            if total != parts:
                errors.append(f"{filename}: frame {m.group(1)} parts "
                              f"{parts} != total {total}")
            continue
        r = RATE.match(line)
        if r:
            rate = float(r.group(1))
    if frames == 0:
        errors.append(f"{filename}: no bit allocation rows")
    if rate is None or rate <= 0:
        errors.append(f"{filename}: invalid window bitrate")
    # CSV must have one row per frame and a header.
    csv_path = os.path.join(vectors, filename + ".bits.csv")
    run([bsinfo, path, "--csv-bits", csv_path])
    with open(csv_path, encoding="utf-8-sig") as f:
        rows = f.readlines()
    if len(rows) != frames + 1:
        errors.append(f"{filename}: CSV has {len(rows) - 1} rows, "
                      f"expected {frames}")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bsinfo", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()
    manifest = json.load(open(os.path.join(args.vectors, "manifest.json")))
    files = {v["file"] for v in manifest["raw"]}
    failures = []
    for filename in ("h264_b.h264", "h265_b.h265", "h264_cbr.h264"):
        if filename not in files:
            failures.append(f"{filename}: not in manifest")
            continue
        errors = check(args.bsinfo, args.vectors, filename)
        print(f"{filename}: {'ok' if not errors else 'FAILED'}")
        failures.extend(errors)
    for error in failures:
        print("  " + error)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
