# Compact HRC radiance buffers

The first retained HRC optimization packs angular radiance into four bytes per
sample and removes unused terminal buffers. NeonSwarm keeps its authored HRC
scale, ray count, tracing levels, optical depth, temporal feedback and material
settings. This changes GPU storage, not simulation or scene composition.

## Representation and tradeoff

The merge ping-pong buffers and four final directional buffers previously stored
three half-float color channels plus an unused half-float channel. They now use
RGB9_E5 in a 32-bit storage-buffer element. Merge and gather share the codec in
`src/render/shaders/hrc_common.shd`. The encoding follows the finite-value rules
in the [Khronos shared-exponent specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_shared_exponent.txt).

RGB9_E5 uses nine mantissa bits per channel and one shared exponent. It preserves
HDR range, but a weak channel beside a much brighter channel has less precision.
Values saturate at 65,408. This is a deliberate precision tradeoff, not lossless
compression. Optical intervals retain their original RGB16F radiance and half-float
optical depth, so interpolating opaque and transparent paths retains its original
behavior. No additional sampling or backend-specific feature is required.

The terminal `T_N` allocation was never read. The terminal `R_N` was a large zero
buffer; its zero boundary is now explicit in the merge shader. Existing valid
buffers satisfy the bindings, and the terminal branches return before loading
them. Scratch allocation sizes now cover only cascades that execute.

## Controlled timing

Apple M4 Pro with 20 GPU cores, Metal, Release, 1920×1080. The travel endpoint is
frozen, with 240 warmup frames followed by 180 rendered frames and the first 16
discarded. Three baseline/candidate pairs ran sequentially in alternating order.
The table shows the median of the three per-run statistics. Both variants replay
the same saved project and must match its complete endpoint state.

| Fixed HRC scale | Before median / p95 | After median / p95 | Median improvement |
| --- | --- | --- | --- |
| 1.0 | 30.283 / 31.050 ms | 25.270 / 25.935 ms | 16.6% |
| 0.5 | 10.958 / 11.356 ms | 10.512 / 10.879 ms | 4.1% |

These are complete rendering-work measurements including GPU completion, not
per-pass GPU timestamps. They establish a local improvement; full-scale rendering
still exceeds the 16.67 ms budget before simulation. This is not a Steam Deck
measurement.

Requested hierarchy buffer storage, including intervals, merge ping-pong and
directional outputs, changes as follows. These totals exclude images, small
angular-weight buffers and driver allocation overhead.

| Work dimensions, including view padding | Before | After | Saved |
| --- | --- | --- | --- |
| 1952×1112, scale 1 | 428.0 MiB | 303.4 MiB | 124.6 MiB |
| 976×556, scale 0.5 | 102.9 MiB | 71.7 MiB | 31.2 MiB |

These are allocation-size calculations, not measured device residency or total
engine GPU memory. Packing halves radiance storage; removing the terminal buffers
accounts for the rest of the reduction.

## Experiments rejected

- A 64×4 thread group instead of 16×16 preserved the image but did not produce a
  reliable speedup.
- Storing transmission instead of optical depth was much faster but darkened
  broad areas. Half-float transmission underflows to zero before later angular
  averages can recover partially visible paths. This version was removed.
- Hoisting constant attenuation, using base-two exponentials, and an exact
  half-float optical-depth lookup table did not improve timing.
- Packing interval color to cache transmission while retaining optical depth
  was slightly slower than packing angular radiance alone. It was removed.

A temporary probe serialized pass completion and attributed roughly 8.8 ms to
extension and 14.4 ms to merging before optimization. Other HRC groups were below
1 ms each except the four trace groups together. Forced synchronization changes
scheduling, so this probe only guided experiments. It was removed from the code.
Native GPU counter inspection remains pending.

## Validation and artifacts

The GPU regression exercises the production RGB9_E5 codec against reference
encodings for zero, very dim light, unequal channels, HDR saturation and exponent
rollover. Existing independent rendering fixtures cover occlusion, translucent
media, colored fog/liquid illumination, indirect feedback stability, reflections,
reset and resource reuse after resizing.

`make build`, `make test` and the native `test_suite --radiance` GPU regression
all pass. The full suite includes runtime, authored assets, MCP, backend parity,
scene documents and headless startup checks.

All 20 NeonSwarm cases completed: travel, dense fog, ignition, destruction and
camera turns, each live/frozen at fixed scales 0.5 and 1.0. Complete endpoint
states match the previous suite. Comparing the final images, the largest RGB
channel difference is 1/255; mean absolute channel differences range from
0.000040 to 0.003655 on an 8-bit scale. This checks those final frames, not every
animation frame or all possible HDR inputs.

All experiment artifacts are under `/tmp/neon-hrc-opt`. `alternating/summary.json`
contains the controlled timings. `scenarios/` contains the full NeonSwarm suite,
with fixed half/full scales, live/frozen runs, endpoint checks and final frames.
`image-comparisons.json` contains the paired final-image measurements.
The original suite is under `/tmp/neon-benchmark-suite/1080p`.

Use [the benchmark workflow](neon-gpu-benchmarks.md) to prepare another comparison.
Rebuilds change executable fingerprints, so intentional renderer comparisons need
`--allow-renderer-changes` when replaying earlier bundles.
