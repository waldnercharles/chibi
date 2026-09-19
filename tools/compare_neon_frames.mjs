// Compare output RGB values. This is an image-difference diagnostic, not a
// perceptual quality score or a physical ground-truth lighting comparison.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { pngPixels } from "./mcp/png-check.mjs";

const [reference, candidate] = process.argv.slice(2).map(path => pngPixels(readFileSync(path)));
assert.equal(reference.width, candidate.width);
assert.equal(reference.height, candidate.height);
const histogram = new Uint32Array(256);
let sum = 0, maximum = 0, changed = 0;
for (let i = 0; i < reference.pixels.length; i += 4) {
  let pixelMax = 0;
  for (let channel = 0; channel < 3; channel++) {
    const delta = Math.abs(reference.pixels[i + channel] - candidate.pixels[i + channel]);
    histogram[delta]++; sum += delta; maximum = Math.max(maximum, delta);
    pixelMax = Math.max(pixelMax, delta);
  }
  changed += pixelMax > 3;
}
const pixels = reference.width * reference.height;
function percentile(fraction) {
  let count = 0;
  for (let delta = 0; delta < histogram.length; delta++) {
    count += histogram[delta];
    if (count >= Math.ceil(pixels * 3 * fraction)) return delta;
  }
}
console.log(JSON.stringify({ width: reference.width, height: reference.height,
  mean_absolute_rgb_8bit: sum / (pixels * 3), max_rgb_8bit: maximum,
  p95_rgb_8bit: percentile(.95), p99_rgb_8bit: percentile(.99),
  pixels_over_3: changed, fraction_over_3: changed / pixels }));
