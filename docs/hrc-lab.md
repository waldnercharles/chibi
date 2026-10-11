# HRC experiment lab

> Script test and benchmark runners referenced below are retired. Their commands
> are historical records, not current workflows. Measurements and source hashes
> describe the recorded revisions; see [testing](testing.md) for current C tooling.

The branch contains an isolated `projects/hrc_lab` project so lighting tests do
not inherit the arena's starfield, gameplay, or material palette. The fixtures
use a neutral authored backdrop and fixed camera. The current clean-lab source
calibration is deliberately restrained:

- emitter `emission_intensity = 0.03`, `emission_multiplier = 1`
- HRC `intensity = 7.5`, `ambient = 0`, `shadow_floor = 0`, `fog = 0.2`
- lab `trace_levels = 3`, adaptive resolution disabled, edge-aware reconstruction

`emission_multiplier` remains an explicit material control. It is already one,
so removing it would not change these images or save meaningful work. Lowering
the authored emission intensity is the source-only exposure control. HRC
intensity is a later composite gain and affects received light as well.

## Lab settings

`GameHRC2D` holds what a game tunes. Color and gather detail, traced cascade
levels, debug views, Depth's near-source refinement and the horizon reuse
experiments are lab settings in `render_radiance_lab`
(`src/render/radiance.h`). Games draw with `render_radiance_lab_defaults()`:
full color and gather, three traced levels and the lit frame. Render tests set
the lab directly. Scene preview requests set only the horizon experiments,
with `horizon_reuse`; the other lab settings keep their defaults there.

## Tuning safely

The renderer fits `GameHRC2D` into what HRC supports instead of rejecting it.
Each change it makes appears as `field from -> to`: Studio shows a "Lighting
adjusted" notice in the Scene toolbar, whose tooltip lists the changes, and a
running game prints them once. The Inspector draws sliders over useful ranges,
offers dropdowns for enumerated settings, and shows each field's comment from
`src/runtime/components.h` on hover. Every combination of the resolution
settings is valid; only out-of-range values are adjusted.

Studio's Performance pane charts the scene's GPU time against a frame at
`target_fps`, and lists the resolution, color and gather detail in use,
including the step adaptive chose. GPU time needs Metal's command-buffer
timestamps.

With `adaptive` on, `resolution` is a ceiling. Turn it off while comparing
values, so the resolution you set is the one you see.

| Goal | `resolution` | lab `color_resolution` |
| --- | ---: | --- |
| Reference | 1 | Full |
| Close to reference, cheaper | 1 | Half |
| Sharp brightness, cheaper color | 1 | Quarter |
| Arena default | 0.5 | Full |

## Reconstruction

Gather reconstructs the low-resolution RGB radiance probes at canvas
resolution by bilinear interpolation blended toward the nearest probe where
absorption changes by more than 0.1 (`HRC_UPSCALE_THRESHOLD`). Earlier
revisions also offered plain bilinear, joint bilateral and a five-tap cross as
`upscale_mode` 0, 2 and 3. They were removed: the five-tap cross preserved
small emissive probes well, but in the basalt Sandbox at 1080p it matched the
edge-aware mode at resolutions 0.5 and 0.35 while costing about 0.6 ms more.

## Resolution settings

| Setting | What it controls |
| --- | --- |
| `resolution` | The finest light grid, as a fraction of the padded canvas. Brightness keeps this detail; most lighting cost scales with its area. |
| lab `color_resolution` | Color's grid relative to `resolution`: Full, Half, Quarter or Eighth. Below Full, a brightness-only transport runs at `resolution` and the RGB transport on the coarser grid. |
| lab `gather_resolution` | The received-light image: gather, temporal filtering and bounce, which turn cascade 0 into light per pixel, at the full canvas or half of it. Auto lets adaptive choose. Material raster and composite stay full resolution. |

The padded canvas is a whole multiple of 8 pixels, so resolutions of 1, 1/2,
1/4 and 1/8 have cells of whole pixels at every output size, and so does color
below them. Other resolutions have fractional cells; thin lit walls then
straddle cells and reflect less bounce light.

Merging treats even and odd probes differently, which leaves a checkerboard
two probes wide. It is invisible at resolution 1 and plain at 0.125, where a
probe spans 8 pixels. Resolve filters each quadrant's cascade-0 fluence with a
1-2-1 tent, which removes exactly that frequency. At 1080p it costs 0.2 to
0.3 ms, changes resolution 1 by a mean of 0.006/255, and lowers the share of
pixels more than 3 off the resolution-1 image from 11.4% to 8.7% at 0.125.

## Adaptive resolution

With `adaptive` on, the renderer measures the scene's GPU time every frame:
material raster, lighting, tone mapping and reflections, but not editor UI. A
frame at `target_fps` allows 1000 / `target_fps` milliseconds. Adaptive steps
between rungs that keep whole-pixel cells:

1. The authored lighting.
2. With `gather_resolution: Auto`, gather at half resolution.
3. Color one step coarser at a time, down to Quarter, at the authored
   `resolution`.
4. `resolution` halved, then halved again, down to 0.25, with the lab's
   `color_resolution`.

It averages 30 frames per decision. It steps down after two averages in a row
exceed the frame's time, and ignores the next 30 frames while history refills.
It steps back up after four windows in a row in which the current cost, times
the cost ratio it measured when it stepped down, fits in 80% of the frame's
time. At 1080p with `target_fps: 60`, Sandbox settles at resolution 0.5.

At resolution 1, Half and Quarter color cost less than Full and change the
image far less than halving the resolution; see [Color detail](#color-detail).
Below resolution 1 they save little, so halved rungs return to the authored
color. When the authored `resolution` is already low, the color steps still
run first and save little.

Steps are rare, because each one reallocates the grids and resets temporal
history. Adaptive needs GPU timestamps; without them it keeps the authored
lighting. It measures GPU time rather than the wall-clock frame, so frames
limited by the CPU or vsync keep their lighting.

## Color detail

Color below Full adds a monochrome transport beside the RGB one, following
[jason.today/hrc](https://jason.today/hrc). Its levels hold half-float
luminance and optical depth: half the RGB transport's bytes per interval and a
quarter per fluence direction. Like the demo, it solves each direction together
with its opposite: one texel holds both directions' values, so every fetch and
every fragment serves two directions.

Color takes the luminance transport's prefiltered inputs, box-averaged. One
gather, filter and bounce serve both: per quadrant, gather rescales the RGB
fluence to the luminance, then reconstructs on the luminance grid:

```glsl
vec3 with_luminance(vec3 color, float Y)
{
    float coarse = dot(color, LUMA);
    return coarse > 1e-8 ? color * (Y / coarse) : vec3(Y);
}
```

At 1080p, resolution 1.0, the whole scene's GPU time and
the RMS difference from Full color, in 8-bit levels:

| Color | Sandbox | Swarmers in view |
| --- | --- | --- |
| Full | 13.8 ms | 14.5 ms |
| Half | 12.7 ms, 0.11 | 12.8 ms, 0.16 |
| Quarter | 10.9 ms, 0.16 | 11.2 ms, 0.25 |

Full color at resolution 0.5 costs about 6.3 ms and differs by 0.30 and 0.36.
At resolution 0.5 the transport is a small part of the frame, and coarser
color saves at most 0.5 ms. See
[the measurements](neon-hrc-optimization.md#pairing-opposite-directions-in-luminance).

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

The supplied Discord references were used as qualitative targets for the clean
fixtures: `HRCLabColorBoundary` checks colored transport through a constrained
opening, `HRCLabVolume` checks attenuation through participating media, and
`HRCLabOpaqueInterior` checks the current solid and interior behavior. We have
not yet run a dedicated caustics fixture or compared against a path-traced
ground truth. In particular, the proposed one-way opaque-solid transport is
still an experiment to design, not an implemented feature.

Zero ambient makes an opaque pixel with zero received light black. A gray region
around a source can still be real HRC fluence or bounce; it is no longer caused
by the unconditional `scene.rgb * ambient` term. The shadow floor is also zero.

## Run the lab

```text
make build-release
python3 tools/benchmark_hrc_lab.py \
  --project projects/hrc_lab \
  --hrc-prototype HRCLabHRC \
  --resolution 1 .5 \
  --trace-levels 3 \
  --upscale-mode 1 \
  --width 960 --height 540 \
  --render-frames 180 \
  --output /private/tmp/hrc-clean-lab
```

Use `--resolution 1 .75 .5 .25` to capture full, three-quarter, half and
quarter light grids. `--color-resolution Half` solves color at half of each
resolution, and `--gather-resolution Half` halves the received-light image. Use `--upscale-mode 0`, `1`, `2`, or `3` to
compare the reconstruction modes. Every generated summary row links to its PNG,
reports lighting's GPU time from per-pass timestamps, and reports its mean RGB
error against that run's resolution 1 reference.

Each run also writes `gallery.md`, which embeds every captured PNG using an
absolute local path so the Markdown can be opened directly in the desktop app.
It also writes `compare.html`, a local before-and-after slider that compares
the resolution 1 row with each lower-resolution row. Markdown can embed the PNGs, but
the draggable comparison requires the HTML page.
The current opaque-interior comparison is also available as the self-contained
[`hrc-lab-compare.html`](hrc-lab-compare.html) snapshot in this directory.

The runner copies the project into each case directory, disables adaptive resolution,
validates the edited prototype and scene, warms the renderer for 240 frames,
discards the first 16 measured frames, and writes a PNG plus JSON report. The
reported frame time includes GPU completion; lighting GPU time sums the HRC,
surface light and composite passes. Each row in the generated `summary.md`
links directly to its captured `frame.png`, alongside the mean RGB error when
a resolution is compared with 1.0.

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

```text
python3 tools/benchmark_hrc_lab.py \
  --project projects/hrc_lab \
  --hrc-prototype HRCLabHRC \
  --scene HRCLabShadow --resolution .5 \
  --trace-levels 3 --upscale-mode 1 \
  --metal-capture --render-frames 180 \
  --output /private/tmp/hrc-shadow-metal
```

Open `frame.gputrace` in a Metal debugger for per-pass GPU durations. Capture
timing should not be mixed with ordinary benchmark timing.

## Angular partition audit

The former Python angular audit is retired; its recorded conclusions follow.

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
