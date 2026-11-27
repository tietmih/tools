#!/usr/bin/env python3
"""Validate HDR10+ dynamic metadata parsing and per-frame CSV output.

A synthetic ST 2094-40 T.35 prefix SEI is injected at the start of a raw
HEVC stream. bsinfo must report the same values the payload carries, and
ffprobe must recognise the SEI as SMPTE 2094-40 (HDR10+).
"""
import argparse
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_hdr10plus import t35_payload

ITM_NITS = 1000
MAXSCL = (50000, 48000, 46000)
AVG = 30000


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr[-2000:])
        raise SystemExit(1)
    return proc.stdout


def size_bytes(n):
    out = b""
    while n >= 255:
        out += b"\xff"
        n -= 255
    return out + bytes([n])


def inject(vectors, out_path):
    source = os.path.join(vectors, "h265_bf0.h265")
    if not os.path.exists(source):
        return False
    payload = t35_payload(targeted_max_luminance=ITM_NITS * 10000,
                          maxscl=MAXSCL, average_maxrgb=AVG)
    message = b"\x04" + size_bytes(len(payload)) + payload
    nal = b"\x4e\x01" + message + b"\x80"
    data = open(source, "rb").read()
    with open(out_path, "wb") as f:
        f.write(b"\x00\x00\x00\x01" + nal)
        f.write(data)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bsinfo", required=True)
    ap.add_argument("--ffprobe", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()
    work = os.path.join(args.vectors, "hdr10plus")
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, "hdr10plus.h265")
    errors = []
    if not inject(args.vectors, path):
        print("hdr10plus: vector missing")
        return 1

    hdr = run([args.bsinfo, path, "--hdr"])
    if "HDR10+" not in hdr:
        errors.append("--hdr did not report HDR10+")
    expected = f"ITM {ITM_NITS} nits maxscl 0.500/0.480/0.460"
    if expected not in hdr:
        errors.append(f"--hdr missing '{expected}'")

    csv_path = os.path.join(work, "hdr10plus.csv")
    run([args.bsinfo, path, "--csv-frames", csv_path])
    rows = []
    with open(csv_path, encoding="utf-8-sig", newline="") as f:
        import csv as csvmod
        rows = list(csvmod.DictReader(f))
    if not rows:
        errors.append("empty frame CSV")
    else:
        first = rows[0]
        if first.get("hdr10plus") != "1":
            errors.append("first frame has no HDR10+ flag")
        if abs(float(first.get("itm_nits", "0")) - ITM_NITS) > 0.5:
            errors.append(f"ITM {first.get('itm_nits')} != {ITM_NITS}")
        if abs(float(first.get("maxscl_max", "0")) - 0.5) > 1e-6:
            errors.append(f"maxscl {first.get('maxscl_max')} != 0.5")
        if abs(float(first.get("avg_maxrgb", "0")) - 0.3) > 1e-6:
            errors.append(f"avg {first.get('avg_maxrgb')} != 0.3")
        later = [r for r in rows[1:] if r.get("hdr10plus") == "1"]
        if later:
            errors.append("HDR10+ metadata leaked into frames without SEI")

    probe = subprocess.run([args.ffprobe, "-v", "error", "-show_frames",
                            "-select_streams", "v", "-read_intervals",
                            "%+#1", "-show_entries", "frame=side_data_list",
                            "-of", "json", path],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True)
    if "SMPTE2094-40" not in probe.stdout:
        errors.append("ffprobe did not recognise SMPTE 2094-40")

    print(f"hdr10plus: {'ok' if not errors else 'FAILED'}")
    for error in errors:
        print("  " + error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
