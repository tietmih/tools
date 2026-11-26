#!/usr/bin/env python3
"""Sanity-check derived reference lists and DPB counts."""
import argparse
import json
import os
import re
import subprocess
import sys

REF_LINE = re.compile(
    r"^refs (\d+) ([A-Z?]) poc=(-?\d+) dpb=(\d+) l0=([\d,-]*) "
    r"l1=([\d,-]*) missing=([\d,-]*)$")


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr[-2000:])
        raise SystemExit(1)
    return proc.stdout


def parse_refs(bsinfo, path):
    rows = {}
    for line in run([bsinfo, path, "--refs"]).splitlines():
        m = REF_LINE.match(line)
        if not m:
            continue
        rows[int(m.group(1))] = {
            "type": m.group(2),
            "poc": int(m.group(3)),
            "dpb": int(m.group(4)),
            "l0": [int(v) for v in m.group(5).split(",") if v],
            "l1": [int(v) for v in m.group(6).split(",") if v],
            "missing": [int(v) for v in m.group(7).split(",") if v],
        }
    return rows


def check(bsinfo, vectors, filename):
    path = os.path.join(vectors, filename)
    if not os.path.exists(path):
        return [f"{filename}: vector missing"]
    rows = parse_refs(bsinfo, path)
    errors = []
    if 0 not in rows:
        return [f"{filename}: no reference rows"]
    if rows[0]["l0"] or rows[0]["l1"]:
        errors.append(f"{filename}: IDR has reference lists")
    p = next((r for r in rows.values() if r["type"] == "P"), None)
    b = next((r for r in rows.values() if r["type"] == "B"), None)
    if p is None:
        errors.append(f"{filename}: no P frame")
    elif not p["l0"]:
        errors.append(f"{filename}: P frame has empty L0")
    if b is None:
        errors.append(f"{filename}: no B frame")
    else:
        if not b["l0"]:
            errors.append(f"{filename}: B frame has empty L0")
        if not b["l1"]:
            errors.append(f"{filename}: B frame has empty L1")
    if not any(r["dpb"] > 0 for r in rows.values()):
        errors.append(f"{filename}: DPB never populated")
    for r in rows.values():
        if r["missing"]:
            errors.append(f"{filename}: frame {r['poc']} missing references "
                          f"{r['missing']}")
            break
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bsinfo", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()

    manifest = json.load(open(os.path.join(args.vectors, "manifest.json")))
    files = {v["file"] for v in manifest["raw"]}
    failures = []
    for filename in ("h264_b.h264", "h265_b.h265"):
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
