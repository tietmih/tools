#!/usr/bin/env python3
"""Generate res/app.ico (16/32/48/256 px, 32-bit BGRA) without dependencies."""
import os
import struct
import zlib

SIZE = 256
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "app.ico")


def rounded(x, y):
    x0, y0, x1, y1 = 8, 8, 248, 248
    r = 42
    if x < x0 or x >= x1 or y < y0 or y >= y1:
        return False
    cx = min(max(x, x0 + r), x1 - r - 1)
    cy = min(max(y, y0 + r), y1 - r - 1)
    dx, dy = x - cx, y - cy
    return dx * dx + dy * dy <= r * r


img = [[(0, 0, 0, 0) for _ in range(SIZE)] for _ in range(SIZE)]
for y in range(SIZE):
    for x in range(SIZE):
        if rounded(x, y):
            t = y / 255.0
            img[y][x] = (int(14 + 12 * t), int(20 + 16 * t), int(34 + 20 * t), 255)


def line(p0, p1, width, color):
    x0, y0 = p0
    x1, y1 = p1
    steps = max(1, int(max(abs(x1 - x0), abs(y1 - y0))))
    for i in range(steps + 1):
        t = i / steps
        cx = x0 + (x1 - x0) * t
        cy = y0 + (y1 - y0) * t
        for yy in range(int(cy - width), int(cy + width) + 1):
            for xx in range(int(cx - width), int(cx + width) + 1):
                if 0 <= xx < SIZE and 0 <= yy < SIZE and img[yy][xx][3]:
                    dx, dy = xx - cx, yy - cy
                    if dx * dx + dy * dy <= width * width:
                        img[yy][xx] = color


def rect(x0, y0, x1, y1, color):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if 0 <= x < SIZE and 0 <= y < SIZE and img[y][x][3]:
                img[y][x] = color


# A small bitstream trace over a frame-size chart.
line((30, 92), (62, 66), 5, (238, 246, 255, 255))
line((62, 66), (94, 96), 5, (238, 246, 255, 255))
line((94, 96), (126, 48), 5, (238, 246, 255, 255))
line((126, 48), (158, 78), 5, (238, 246, 255, 255))
line((158, 78), (190, 40), 5, (238, 246, 255, 255))
line((190, 40), (224, 70), 5, (238, 246, 255, 255))

rect(40, 126, 86, 208, (200, 56, 64, 255))
rect(103, 96, 149, 208, (216, 128, 32, 255))
rect(166, 72, 212, 208, (52, 108, 200, 255))
rect(28, 208, 228, 213, (90, 112, 145, 255))


def scaled(n):
    rows = []
    for y in range(n):
        sy = min(SIZE - 1, y * SIZE // n)
        row = []
        for x in range(n):
            sx = min(SIZE - 1, x * SIZE // n)
            row.append(img[sy][sx])
        rows.append(row)
    return rows


def png_entry(pixels):
    n = len(pixels)
    raw = bytearray()
    for y in range(n):
        raw.append(0)
        for x in range(n):
            r, g, b, a = pixels[y][x]
            raw += bytes((r, g, b, a))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def bmp_entry(pixels):
    n = len(pixels)
    xor = bytearray()
    for y in reversed(range(n)):
        for x in range(n):
            r, g, b, a = pixels[y][x]
            xor += bytes((b, g, r, a))
    mask_row = ((n + 31) // 32) * 4
    and_mask = bytearray(mask_row * n)
    for y in range(n):
        row = n - 1 - y
        for x in range(n):
            if pixels[y][x][3] == 0:
                and_mask[row * mask_row + x // 8] |= 0x80 >> (x % 8)
    header = struct.pack(
        "<IiiHHIIiiII", 40, n, n * 2, 1, 32, 0, len(xor) + len(and_mask),
        0, 0, 0, 0
    )
    return header + bytes(xor) + bytes(and_mask)


sizes = [16, 32, 48, 256]
entries = []
offset = 6 + 16 * len(sizes)
for n in sizes:
    pixels = scaled(n)
    data = png_entry(pixels) if n == 256 else bmp_entry(pixels)
    entries.append((n, len(data), offset))
    offset += len(data)

out = bytearray()
out += struct.pack("<HHH", 0, 1, len(sizes))
for n, length, off in entries:
    out += struct.pack("<BBBBHHII", 0 if n == 256 else n,
                       0 if n == 256 else n, 0, 0, 1, 32, length, off)
for n in sizes:
    pixels = scaled(n)
    out += png_entry(pixels) if n == 256 else bmp_entry(pixels)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "wb") as f:
    f.write(out)
print("wrote", OUT, len(out), "bytes")
