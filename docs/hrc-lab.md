# HRC experiment lab

The branch contains an isolated `projects/hrc_lab` project so lighting tests do
not inherit the arena's starfield, gameplay, or material palette. The fixtures
use a neutral authored backdrop and fixed camera. The current clean-lab source
calibration is deliberately restrained:

- emitter `emission_intensity = 0.03`, `emission_multiplier = 1`
- HRC `intensity = 7.5`, `ambient = 0`, `shadow_floor = 0`, `fog = 0.2`
- `trace_levels = 3`, adaptive scale disabled, reconstruction mode 1

`emission_multiplier` remains an explicit material control. It is already one,
so removing it would not change these images or save meaningful work. Lowering
the authored emission intensity is the source-only exposure control. HRC
intensity is a later composite gain and affects received light as well.

## Clean fixtures

| Scene | What it isolates |
| --- | --- |
| `HRCLabPinhole` | Small source, two opaque slabs, aperture and receiver. |
| `HRCLabBounce` | Two angled diffuse panels and a receiver. |
| `HRCLabSinglePixel` | Tiny source and broad receiver. |
| `HRCLabShadow` | Blocker, source, floor and receiving wall. |
| `HRCLabVolume` | Source through smoke and liquid volumes. |
| `HRCLabTwoLights` | Warm and white circular sources around a circular blocker. |
| `HRCLabColorBoundary` | Red, green and blue sources through one aperture. |
| `HRCLabOpaqueInterior` | Three slabs with different absorption/albedo values and interior targets. |

The last fixture is a baseline for the opaque-solid question. Current HRC
materials attenuate and reflect through the existing scene representation; they
do not yet have a separate one-way transport path that illuminates an interior
while preserving a fully blocking exterior. That proposed behavior can be
judged against this scene without changing the other fixtures.

Zero ambient makes an opaque pixel with zero received light black. A gray region
around a source can still be real HRC fluence or bounce; it is no longer caused
by the unconditional `scene.rgb * ambient` term. The shadow floor is also zero.

## Run the lab

```sh
make build-release
python3 tools/benchmark_hrc_lab.py \
  --project projects/hrc_lab \
  --hrc-prototype HRCLabHRC \
  --scale 1 .5 \
  --trace-levels 3 \
  --upscale-mode 1 \
  --width 960 --height 540 \
  --render-frames 180 \
  --output /private/tmp/hrc-clean-lab
```

The runner copies the project into each case directory, disables adaptive scale,
validates the edited prototype and scene, warms the renderer for 240 frames,
discards the first 16 measured frames, and writes a PNG plus JSON report. The
reported time includes GPU completion and is not a per-dispatch GPU timestamp.

The original `projects/arena2d` fixtures remain the default when `--project`
is omitted. The same runner can therefore compare the clean lab with the
existing arena content without duplicating benchmark logic.

## Current clean-lab result

These are one local 960×540 Release sweep with trace level 3, zero ambient, and
the low source calibration above. Mean error is mean absolute RGB difference in
8-bit space against that scene's full-scale image.

| Scene | Mode 1 full | Mode 1 half | Mode 1 error | Mode 3 half | Mode 3 error |
| --- | ---: | ---: | ---: | ---: | ---: |
| Pinhole | 7.78 ms | 7.76 ms | 0.25 | 5.71 ms | 0.33 |
| Bounce | 8.08 ms | 7.75 ms | 0.07 | 5.48 ms | 0.07 |
| Single pixel | 7.49 ms | 7.66 ms | 0.09 | 5.47 ms | 0.09 |
| Shadow | 7.95 ms | 7.71 ms | 0.34 | 6.02 ms | 0.21 |
| Volume | 8.28 ms | 6.31 ms | 0.07 | 8.74 ms | 0.07 |
| Two lights | 7.18 ms | 5.70 ms | 0.28 | 5.32 ms | 0.32 |
| Color boundary | 7.79 ms | 5.40 ms | 0.18 | 5.26 ms | 0.21 |
| Opaque interior | 6.71 ms | 5.33 ms | 0.29 | 5.20 ms | 0.28 |

Mode 3 is useful for difficult shadow edges, but it is not uniformly closer to
the full-resolution reference and can cost extra work in volume scenes. Keep
mode 1 as the default until a larger matched workload shows a consistent win.

## GPU phase attribution

The renderer already synchronizes before presenting benchmark frames. GPU labels
cover material tile binning, material raster, fluid density raster, HRC
prefilter, each quadrant's Trace/Extend/Merge group, Gather, Filter, Bounce and
Composite. Trace, Extend and Merge also contain nested `L0` through `L15`
labels for active dispatch levels.

Capture one warmed Metal frame with:

```sh
python3 tools/benchmark_hrc_lab.py \
  --project projects/hrc_lab \
  --hrc-prototype HRCLabHRC \
  --scene HRCLabShadow --scale .5 \
  --trace-levels 3 --upscale-mode 1 \
  --metal-capture --render-frames 180 \
  --output /private/tmp/hrc-shadow-metal
```

Open `frame.gputrace` in a Metal debugger for per-pass GPU durations. Capture
timing should not be mixed with ordinary benchmark timing.

## Angular partition audit

```sh
python3 tools/audit_hrc_angles.py --levels 5
```

The current trace shader uses integer endpoint slopes. That makes endpoint
spacing uniform in slope, not in radians. Levels 0 and 1 happen to have equal
angular widths; beginning at level 2 the widths differ. The CPU merge weights
use matching `atan2` boundaries, so the current merge accounts for those spans.

Replacing the endpoints with `step * tan(-pi/4 + k*pi/(2*step))` makes angular
boundaries uniform but breaks the exact integer-row endpoint relation used by
`extend`. An equal-radian experiment therefore needs fractional-row
interpolation or a different hierarchy mapping.

At an exact 2× canvas-to-work reduction, prefilter reads the generated mip 1
directly. The old path sampled that mip four times per source texture for each
work pixel; the exact path uses one `texelFetch` for emission, absorption and
bounce. Fractional reductions retain the weighted path.
