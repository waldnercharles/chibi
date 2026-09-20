# HRC experiment lab

The `hrc-gpu-lab` branch contains five static fixtures for comparing HRC
resolution and reconstruction behavior:

| Scene | Signal under test |
| --- | --- |
| `NeonSwarmHRCPinhole` | A small opening between opaque slabs and a bright source. |
| `NeonSwarmHRCBounce` | Two reflective panels and a receiver for indirect light. |
| `NeonSwarmHRCSinglePixel` | A 4×4 emissive source and a broad receiver. |
| `NeonSwarmHRCShadow` | A narrow source, opaque blocker and receiving wall. |
| `NeonSwarmHRCVolume` | Cloud, liquid and mist bands between source and receiver. |

They use a fixed camera, no gameplay behaviors and the authored HRC prototype.
The runner copies the project into each output directory, disables adaptive
scale, validates the edited prototype and scene, warms the renderer for 240
frames, discards the first 16 measured frames, and writes a PNG plus JSON
report for each case.

## Run the lab

```sh
make build-release
python3 tools/benchmark_hrc_lab.py \
  --scale 1 .75 .5 \
  --width 960 --height 540 \
  --render-frames 180 \
  --output /private/tmp/hrc-lab-960
```

Use one fixture when iterating on a shader:

```sh
python3 tools/benchmark_hrc_lab.py \
  --scene NeonSwarmHRCPinhole \
  --scale 1 .5 \
  --render-frames 180 \
  --output /private/tmp/hrc-pinhole
```

`--upscale-mode 1` is the current edge-aware bilinear-to-nearest path.
`--upscale-mode 3` enables the experimental five-tap cross reconstruction:
the nearest work probe plus its four axial neighbors. It is intended for
comparison at low scale, especially in the single-pixel, pinhole and shadow
fixtures. It is a reconstruction experiment; it does not reduce the HRC
trace, extension or merge work.

Each output has `summary.md`, `summary.json`, `comparisons.json`, one folder
per scene and scale, and a `frame.png` in every case folder. The comparison
metrics are 8-bit image differences against the same scene at scale 1.0.
They are useful for finding regressions and sharp-edge failures; they are not
a perceptual quality score.

One local 960×540 Release run on the five fixtures produced this directional
comparison. The full-scale reference used mode 1; the two half-scale columns
include the exact-mip prefilter path. Mean error is mean absolute RGB in 8-bit
space against the full-scale image.

| Scene | Full median | Half mode 1 | Half mode 3 | Mode 3 mean error |
| --- | ---: | ---: | ---: | ---: |
| Pinhole | 10.26 ms | 4.15 ms | 4.37 ms | 1.61 |
| Bounce | 10.22 ms | 4.34 ms | 4.28 ms | 0.46 |
| Single pixel | 10.51 ms | 4.38 ms | 4.34 ms | 0.32 |
| Shadow | 10.40 ms | 4.23 ms | 4.28 ms | 2.31 |
| Volume | 10.82 ms | 4.19 ms | 4.25 ms | 0.11 |

This is a single local run, so it establishes a useful direction rather than a
stable hardware benchmark. In this sample, mode 3 improved the full-scale
image error for every fixture, with the largest changes in pinhole and shadow.

## GPU phase attribution

The renderer already synchronizes before presenting benchmark frames, so the
reported median and percentile values include GPU completion. They are whole
frame timings. They do not identify the duration of one dispatch.

The lab can record one warmed Metal frame:

```sh
python3 tools/benchmark_hrc_lab.py \
  --scene NeonSwarmHRCShadow \
  --scale .5 \
  --metal-capture \
  --render-frames 180 \
  --output /private/tmp/hrc-shadow-metal
```

The resulting `frame.gputrace` contains labels for material tile binning,
material raster, fluid density, HRC prefilter, each quadrant's Trace/Extend/
Merge group, Gather, Filter, Bounce and Composite. Trace, Extend and Merge now
also contain nested `L0` through `L15` labels for the dispatch levels that are
actually active. Open the trace in Xcode's Metal debugger to obtain GPU counter
and pass-duration data. Capture timing is instrumented and should not be mixed
with ordinary benchmark timings.

## Angular partition audit

```sh
python3 tools/audit_hrc_angles.py --levels 5
```

The current trace shader uses `step = 2^n` and endpoint offsets
`2*k - step`. That produces evenly spaced endpoint slopes, not evenly spaced
radians. The endpoints are four 90° quadrants. Levels 0 and 1 happen to have
equal angular widths; beginning at level 2 the widths differ:

| Cascade | Smallest width | Largest width | Ratio |
| ---: | ---: | ---: | ---: |
| 0 | 90.0000° | 90.0000° | 1.0000 |
| 1 | 45.0000° | 45.0000° | 1.0000 |
| 2 | 18.4349° | 26.5651° | 1.4410 |
| 3 | 8.1301° | 14.0362° | 1.7265 |
| 4 | 3.8141° | 7.1250° | 1.8681 |
| 5 | 1.8476° | 3.5763° | 1.9357 |

The CPU merge weights already use the matching `atan2` boundaries, so the
current merge accounts for those nonuniform angular spans. That is different
from saying that the underlying frusta have equal radians.

Replacing the endpoint offsets with
`step * tan(-π/4 + k * π/(2*step))` would make the boundaries angularly
uniform. It would also break the exact additive endpoint relation used by
`extend`: two child intervals no longer land on the parent's endpoint at the
same integer probe row. The audit reports this as `chain_error_px`. A valid
equal-radian experiment therefore needs fractional-row interpolation or a
different hierarchy mapping in `extend`, followed by the same fixture matrix.
The full-circle direction helper is useful for a different layout; this
implementation's four quadrants each cover one 90° sector.

The branch currently records this geometry rather than silently changing it.
That makes the first equal-radian comparison an algorithm experiment with a
known compatibility cost instead of a trace-only change whose later levels no
longer describe the same rays.

At an exact 2× canvas-to-work reduction, prefilter now takes the generated mip
level directly. The old path sampled that mip four times per source texture for
each work pixel; the fast path uses one `texelFetch` for emission, absorption
and bounce. Non-exact fractional reductions retain the weighted path.
