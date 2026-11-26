#!/usr/bin/env python3
"""Check the CPB0 model on CBR and forced-underflow vectors."""
import argparse
import json
import os
import re
import subprocess
import sys

CPB_LINE = re.compile(
    r"^cpb valid=(\d+) source=(\S+) cpb_index=(\d+) cbr=(\d+) "
    r"bit_rate_bps=(\d+) cpb_size_bits=(\d+) initial_occupancy_bits=(\S+) "
    r"samples=(\d+) underflows=(\d+) first_underflow=(-?\d+) "
    r"overflows=(\d+) estimated=(\d+) error=(.*)$")


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr[-2000:])
        raise SystemExit(1)
    return proc.stdout


def cpb_line(bsinfo, path):
    for line in run([bsinfo, path, "--cpb"]).splitlines():
        m = CPB_LINE.match(line)
        if m:
            return {
                "valid": int(m.group(1)),
                "source": m.group(2),
                "cbr": int(m.group(4)),
                "bit_rate": int(m.group(5)),
                "cpb_size": int(m.group(6)),
                "samples": int(m.group(8)),
                "underflows": int(m.group(9)),
                "first_underflow": int(m.group(10)),
                "overflows": int(m.group(11)),
                "estimated": int(m.group(12)),
                "error": m.group(13),
            }
    raise SystemExit(f"{path}: no cpb line")


def check(bsinfo, vectors, filename, underflow):
    path = os.path.join(vectors, filename)
    if not os.path.exists(path):
        return [f"{filename}: vector missing"]
    info = cpb_line(bsinfo, path)
    errors = []
    if info["valid"] != 1:
        errors.append(f"{filename}: model invalid: {info['error']}")
        return errors
    if info["samples"] != 60:
        errors.append(f"{filename}: {info['samples']} samples, expected 60")
    if info["bit_rate"] != 400000 or info["cpb_size"] != 800000:
        errors.append(f"{filename}: unexpected HRD parameters")
    if info["cbr"] != 1:
        errors.append(f"{filename}: CBR flag not set")
    if underflow:
        if info["first_underflow"] != 10:
            errors.append(f"{filename}: first underflow "
                          f"{info['first_underflow']}, expected 10")
        if info["underflows"] < 1:
            errors.append(f"{filename}: no underflow detected")
    else:
        if info["underflows"] != 0:
            errors.append(f"{filename}: {info['underflows']} underflows")
        if info["first_underflow"] != -1:
            errors.append(f"{filename}: unexpected first underflow")
    if info["overflows"] != 0:
        errors.append(f"{filename}: {info['overflows']} overflows")

    csv_path = os.path.join(vectors, filename + ".cpb.csv")
    run([bsinfo, path, "--csv-cpb", csv_path])
    with open(csv_path, encoding="utf-8-sig") as f:
        rows = f.readlines()
    if len(rows) != 61:
        errors.append(f"{filename}: CSV has {len(rows) - 1} rows")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bsinfo", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()

    manifest = json.load(open(os.path.join(args.vectors, "manifest.json")))
    files = {v["file"] for v in manifest["raw"]}
    failures = []
    for filename, underflow in (("h264_cbr.h264", False),
                                ("h265_cbr.h265", False),
                                ("h264_cbr_underflow.h264", True),
                                ("h265_cbr_underflow.h265", True)):
        if filename not in files:
            failures.append(f"{filename}: not in manifest")
            continue
        errors = check(args.bsinfo, args.vectors, filename, underflow)
        print(f"{filename}: {'ok' if not errors else 'FAILED'}")
        failures.extend(errors)
    for error in failures:
        print("  " + error)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
