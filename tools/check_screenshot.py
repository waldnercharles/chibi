#!/usr/bin/env python3
"""Verify that a headless editor screenshot contains rendered pixels."""
from pathlib import Path
import struct
import sys
import zlib

path = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/c-blueprints-screenshot.png")
data = path.read_bytes()
assert data[:8] == b"\x89PNG\r\n\x1a\n"
pos = 8
width = height = color_type = None
idat = bytearray()
while pos < len(data):
    size = struct.unpack(">I", data[pos : pos + 4])[0]
    kind = data[pos + 4 : pos + 8]
    chunk = data[pos + 8 : pos + 8 + size]
    pos += size + 12
    if kind == b"IHDR":
        width, height, depth, color_type = struct.unpack(">IIBB", chunk[:10])
        assert depth == 8 and color_type == 6, "expected 8-bit RGBA PNG"
    elif kind == b"IDAT":
        idat.extend(chunk)
    elif kind == b"IEND":
        break

raw = zlib.decompress(idat)
channels = 4
stride = width * channels
previous = bytearray(stride)
non_black = 0
for row in range(height):
    start = row * (stride + 1)
    filter_type = raw[start]
    encoded = raw[start + 1 : start + stride + 1]
    pixels = bytearray(encoded)
    for i in range(stride):
        left = pixels[i - channels] if i >= channels else 0
        above = previous[i]
        upper_left = previous[i - channels] if i >= channels else 0
        if filter_type == 1:
            pixels[i] = (pixels[i] + left) & 255
        elif filter_type == 2:
            pixels[i] = (pixels[i] + above) & 255
        elif filter_type == 3:
            pixels[i] = (pixels[i] + (left + above) // 2) & 255
        elif filter_type == 4:
            estimate = left + above - upper_left
            distances = (abs(estimate - left), abs(estimate - above), abs(estimate - upper_left))
            predictor = (left, above, upper_left)[distances.index(min(distances))]
            pixels[i] = (pixels[i] + predictor) & 255
        elif filter_type != 0:
            raise AssertionError(f"unsupported PNG filter {filter_type}")
    for i in range(0, stride, channels):
        non_black += pixels[i] != 0 or pixels[i + 1] != 0 or pixels[i + 2] != 0
    previous = pixels

print(f"screenshot={width}x{height} non_black_pixels={non_black}")
assert non_black > 0, "headless screenshot is entirely black"
