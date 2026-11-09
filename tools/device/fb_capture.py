#!/usr/bin/env python3
"""Capture the Cardputer Zero framebuffer (/dev/fb0, 320x170 RGB565) to a PNG.

Used both for development previews and for the four store screenshots. The output
is the raw panel content: 320x170, no window chrome, exactly what the store wants.

Usage:
  fb_capture.py [out.png] [--device /dev/fb0] [--width 320] [--height 170]

Writes the PNG path to stdout. Requires read access to the framebuffer (the
`video` group is enough on Cardputer Zero).
"""
import argparse
import struct
import sys
import zlib


def read_fb(path: str, width: int, height: int) -> bytes:
    with open(path, "rb") as fh:
        data = fh.read(width * height * 2)
    if len(data) < width * height * 2:
        raise SystemExit("short read from %s: %d bytes" % (path, len(data)))
    return data


def rgb565_to_rgb888(data: bytes, width: int, height: int) -> bytes:
    out = bytearray()
    for y in range(height):
        out.append(0)  # PNG filter type: none
        row = data[y * width * 2:(y + 1) * width * 2]
        for x in range(width):
            v = row[x * 2] | (row[x * 2 + 1] << 8)
            r = ((v >> 11) & 0x1F) * 255 // 31
            g = ((v >> 5) & 0x3F) * 255 // 63
            b = (v & 0x1F) * 255 // 31
            out += bytes((r, g, b))
    return bytes(out)


def png_chunk(tag: bytes, payload: bytes) -> bytes:
    body = tag + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path: str, width: int, height: int, rgb: bytes) -> None:
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) +
           png_chunk(b"IDAT", zlib.compress(rgb, 9)) + png_chunk(b"IEND", b""))
    with open(path, "wb") as fh:
        fh.write(png)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("output", nargs="?", default="/tmp/fb.png")
    ap.add_argument("--device", default="/dev/fb0")
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=170)
    args = ap.parse_args()

    raw = read_fb(args.device, args.width, args.height)
    write_png(args.output, args.width, args.height, rgb565_to_rgb888(raw, args.width, args.height))
    print(args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
