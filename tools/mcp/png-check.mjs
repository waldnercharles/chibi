import assert from "node:assert/strict";
import { inflateSync } from "node:zlib";

// Read the non-interlaced RGBA PNGs returned by the native capture tool.
export function pngPixels(png) {
  assert(png.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])));
  const width = png.readUInt32BE(16), height = png.readUInt32BE(20);
  assert.equal(png[24], 8); assert.equal(png[25], 6); assert.equal(png[28], 0);
  const chunks = [];
  for (let at = 8; at < png.length;) {
    const length = png.readUInt32BE(at);
    if (png.toString("ascii", at + 4, at + 8) === "IDAT") chunks.push(png.subarray(at + 8, at + 8 + length));
    at += length + 12;
  }
  const raw = inflateSync(Buffer.concat(chunks)), stride = width * 4;
  assert.equal(raw.length, (stride + 1) * height);
  const pixels = Buffer.alloc(stride * height);
  function paeth(a, b, c) {
    const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
  }
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    assert(filter <= 4);
    for (let x = 0; x < stride; x++) {
      const i = y * stride + x, a = x >= 4 ? pixels[i - 4] : 0,
        b = y ? pixels[i - stride] : 0, c = y && x >= 4 ? pixels[i - stride - 4] : 0;
      const predict = [0, a, b, Math.floor((a + b) / 2), paeth(a, b, c)][filter];
      pixels[i] = (raw[y * (stride + 1) + x + 1] + predict) & 255;
    }
  }
  return { width, height, pixels, at: (x, y) => [...pixels.subarray((y * width + x) * 4, (y * width + x) * 4 + 4)] };
}
