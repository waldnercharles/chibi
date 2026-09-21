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

## Reconstruction modes

The `upscale_mode` field selects how the low-resolution RGB radiance probes are
reconstructed at canvas resolution:

| Mode | Reconstruction |
| ---: | --- |
| 0 | Plain bilinear interpolation. |
| 1 | Bilinear interpolation blended toward the nearest probe at absorption edges. |
| 2 | Joint bilateral interpolation weighted by transmittance similarity. |
| 3 | Five-tap cross: the center probe plus its four axial neighbors. |

Mode 1 is the current default. Mode 3 is the five-tap experiment; it preserves
small emissive probes well, but its quality change depends on the fixture.

## Scale meaning

`GameHRC2D.scale` is one shared HRC work-grid scale. A `.5` run is half-resolution HRC:
the filtered emission RGB, scalar absorption, bounce input, and RGB radiance
probes all use the half-resolution work grid before full-resolution gather and
composite. A `.25` run is the same arrangement at quarter scale.

This resolution setting is distinct from the separately published Half-HRC
prototype and from reducing probe density along each plane. See the
[realtime optics comparison](research/realtime-optics-direction.md).

The current implementation does not have independent luminance and color
scales. It also does not store a separate luminance transport buffer: the
transport probe is RGB, while absorption is the scalar field. Therefore we have
not yet tested “half luminance plus quarter-resolution color” as a real split
channel configuration. That requires separate resource sizes and a new
reconstruction path; the scale sweep below measures the supported shared-scale
options first.

## Clean fixtures

| Scene | What it isolates |
| --- | --- |
| `HRCLabPinhole` | Small source, two opaque slabs, aperture and receiver. |
| `HRCLabBounce` | Two angled diffuse panels and a receiver. |
| `HRCLabSinglePixel` | Tiny source and broad receiver. |
| `HRCLabShadow` | Blocker, source, floor and receiving wall. |
| `HRCLabVolume` | Source through smoke and liquid volumes. |
| `HRCLabDenseFog` | Smoke across the fixed camera view, a source and an opaque blocker. |
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

Use `--scale 1 .75 .5 .25` to capture the supported full, three-quarter,
half, and quarter HRC work grids. Use `--upscale-mode 0`, `1`, `2`, or `3` to
compare the reconstruction modes. Every generated summary row links to its
PNG and reports its mean RGB error against that run's full-scale reference.

The runner and scene preview accept dimensions from 64 through 4096 pixels.
For desktop measurements, use `--width 2560 --height 1440 --scale .5` or
`--width 3840 --height 2160 --scale .25`. These select 1280x720 and 960x540
transport grids. They are benchmark configurations, not quality presets.
See [the first transport correction](research/hrc-transport-correction.md)
for matched baseline measurements and the dense-fog fixture.

The runner copies the project into each case directory, disables adaptive scale,
validates the edited prototype and scene, warms the renderer for 240 frames,
discards the first 16 measured frames, and writes a PNG plus JSON report. The
reported time includes GPU completion and is not a per-dispatch GPU timestamp.
Runs use a hidden window and exclude visible presentation overhead. The result's
`performance.hidden_window` field distinguishes these from older visible runs.
Each row in the generated `summary.md` links directly to its captured
`frame.png`, alongside the mean RGB error when a scale is compared with 1.0.

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
