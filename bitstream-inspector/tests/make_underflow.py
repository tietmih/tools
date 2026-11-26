#!/usr/bin/env python3
"""Insert an oversized SEI before a chosen frame to force a CPB underflow."""
import argparse
import sys


def split_annexb(data):
    starts = []
    i = 0
    while i + 3 < len(data):
        if data[i:i + 3] == b"\x00\x00\x01":
            starts.append((i, 3))
            i += 3
        elif data[i:i + 4] == b"\x00\x00\x00\x01":
            starts.append((i, 4))
            i += 4
        else:
            i += 1
    return starts


def is_vcl(codec, header):
    if codec == "h264":
        return 1 <= (header & 0x1f) <= 5
    return ((header >> 1) & 0x3f) <= 31


def size_bytes(size):
    out = b""
    while size >= 255:
        out += b"\xff"
        size -= 255
    return out + bytes([size])


def inject(input_path, output_path, codec, before_frame, payload_bytes):
    data = open(input_path, "rb").read()
    starts = split_annexb(data)
    if not starts:
        raise SystemExit("no Annex B start codes")
    insert = None
    count = 0
    for offset, start_len in starts:
        if offset + start_len >= len(data):
            continue
        if is_vcl(codec, data[offset + start_len]):
            if count == before_frame:
                insert = offset
                break
            count += 1
    if insert is None:
        raise SystemExit(f"frame {before_frame} not found")
    payload = bytes([0xAA]) * payload_bytes
    size = size_bytes(len(payload))
    if codec == "h264":
        nal = b"\x06\x05" + size + payload + b"\x80"
    else:
        nal = b"\x4e\x01\x05" + size + payload + b"\x80"
    with open(output_path, "wb") as out:
        out.write(data[:insert])
        out.write(b"\x00\x00\x00\x01")
        out.write(nal)
        out.write(data[insert:])
    return insert


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--codec", choices=("h264", "h265"), required=True)
    ap.add_argument("--before-frame", type=int, default=10)
    ap.add_argument("--payload-bytes", type=int, default=100000)
    args = ap.parse_args()
    offset = inject(args.input, args.output, args.codec, args.before_frame,
                    args.payload_bytes)
    print(f"{args.output}: inserted at byte {offset}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
