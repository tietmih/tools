#!/usr/bin/env python3
"""Check the standalone MPEG-TS scanner on 188/192/204-byte packets."""
import argparse
import json
import os
import re
import subprocess
import sys

SUMMARY = re.compile(
    r"^ts packet_size=(\d+) packets=(\d+) pids=(\d+) "
    r"continuity_errors=(\d+) transport_errors=(\d+) scrambled=(\d+) "
    r"pcr_count=(\d+) pcr_jitter_us=(\S+)$")
PID = re.compile(r"^pid=(\d+) packets=(\d+) stream_type=(-?\d+)")


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr[-2000:])
        raise SystemExit(1)
    return proc.stdout


def scan(mtest, path):
    result = {"pids": {}}
    for line in run([mtest, path, "--ts"]).splitlines():
        m = SUMMARY.match(line)
        if m:
            result.update({
                "packet_size": int(m.group(1)),
                "packets": int(m.group(2)),
                "pids_count": int(m.group(3)),
                "cc": int(m.group(4)),
                "tei": int(m.group(5)),
                "pcr": int(m.group(7)),
                "jitter": float(m.group(8)),
            })
            continue
        p = PID.match(line)
        if p:
            result["pids"][int(p.group(1))] = {
                "packets": int(p.group(2)),
                "stream_type": int(p.group(3)),
            }
    return result


def inject_cc(data, packet_size, sync_offset):
    stride = packet_size
    first = None
    for pos in range(sync_offset, len(data) - 188, stride):
        if data[pos] != 0x47:
            continue
        pid = ((data[pos + 1] & 0x1F) << 8) | data[pos + 2]
        adaptation = (data[pos + 3] >> 4) & 0x3
        if pid == 0 or not (adaptation & 1):
            continue
        if first is None:
            first = (pid, pos + 3)
            continue
        if pid == first[0]:
            cc = data[pos + 3] & 0x0F
            data[pos + 3] = (data[pos + 3] & 0xF0) | ((cc + 2) & 0x0F)
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mtest", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()
    manifest = json.load(open(os.path.join(args.vectors, "manifest.json")))
    containers = {v["file"] for v in manifest["containers"]}
    errors = []
    for name, size in (("h264_bf0.ts", 188), ("h264_bf0.m2ts", 192)):
        if name not in containers:
            errors.append(f"{name}: vector missing")
            continue
        path = os.path.join(args.vectors, name)
        report = scan(args.mtest, path)
        if report.get("packet_size") != size:
            errors.append(f"{name}: packet size {report.get('packet_size')}")
        if report.get("packets", 0) == 0 or not report.get("pids"):
            errors.append(f"{name}: no packets/PIDs")
        if 256 not in report["pids"]:
            errors.append(f"{name}: H.264 PID 256 missing")
        if report.get("pcr", 0) == 0:
            errors.append(f"{name}: no PCR samples")

    # 204-byte packets: append 16 bytes to each 188-byte packet.
    source = os.path.join(args.vectors, "h264_bf0.ts")
    if os.path.exists(source):
        data = bytearray(open(source, "rb").read())
        out = bytearray()
        for pos in range(0, len(data), 188):
            out += data[pos:pos + 188]
            out += b"\x00" * 16
        path204 = os.path.join(args.vectors, "h264_bf0_204.ts")
        open(path204, "wb").write(out)
        report = scan(args.mtest, path204)
        if report.get("packet_size") != 204:
            errors.append("204-byte packet detection failed")

    # Inject a continuity-counter error into one PID.
    if os.path.exists(source):
        data = bytearray(open(source, "rb").read())
        if not inject_cc(data, 188, 0):
            errors.append("could not inject a CC error")
        else:
            broken = os.path.join(args.vectors, "h264_bf0_cc.ts")
            open(broken, "wb").write(data)
            report = scan(args.mtest, broken)
            if report.get("cc", 0) < 1:
                errors.append("injected CC error was not detected")

    print(f"ts scanner: {'ok' if not errors else 'FAILED'}")
    for error in errors:
        print("  " + error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
