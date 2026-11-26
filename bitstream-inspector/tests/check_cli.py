#!/usr/bin/env python3
"""Validate bsinfo JSON, batch mode and stable exit codes."""
import argparse
import json
import os
import subprocess
import sys


def run(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)


def check_file(bsinfo, path):
    proc = run([bsinfo, path, "--json"])
    if proc.returncode != 0:
        return [f"{path}: json exit {proc.returncode}: {proc.stderr[-200:]}"]
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return [f"{path}: invalid json: {exc}"]
    errors = []
    for key in ("file", "summary", "codec", "frames", "nals"):
        if key not in data:
            errors.append(f"{path}: missing {key}")
    if not data.get("frames"):
        errors.append(f"{path}: no frames")
    else:
        frame = data["frames"][1]
        for key in ("index", "type", "poc", "l0", "l1", "dpb"):
            if key not in frame:
                errors.append(f"{path}: frame missing {key}")
    if not data.get("nals"):
        errors.append(f"{path}: no nals")
    tree = run([bsinfo, path, "--json", "--tree", "0"])
    try:
        tree_data = json.loads(tree.stdout)
        if "tree" not in tree_data or not tree_data["tree"]:
            errors.append(f"{path}: missing syntax tree")
    except json.JSONDecodeError:
        errors.append(f"{path}: tree json invalid")
    return errors


def check_batch(bsinfo, vectors):
    proc = run([bsinfo, "--batch", vectors])
    if proc.returncode != 0:
        return [f"batch exit {proc.returncode}"]
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return [f"batch invalid json: {exc}"]
    if len(data) < 10:
        return [f"batch returned {len(data)} entries"]
    if any("file" not in entry for entry in data):
        return ["batch entry missing file"]
    return []


def check_exit_codes(bsinfo):
    errors = []
    proc = run([bsinfo])
    if proc.returncode != 2:
        errors.append(f"no args exit {proc.returncode}, expected 2")
    proc = run([bsinfo, "/nonexistent/stream.h264"])
    if proc.returncode != 1:
        errors.append(f"missing file exit {proc.returncode}, expected 1")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bsinfo", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()
    path = os.path.join(args.vectors, "h264_b.h264")
    errors = check_file(args.bsinfo, path)
    errors.extend(check_batch(args.bsinfo, args.vectors))
    errors.extend(check_exit_codes(args.bsinfo))
    print(f"bsinfo json/batch: {'ok' if not errors else 'FAILED'}")
    for error in errors:
        print("  " + error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
