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

`GameHRC2D.scale` is one shared HRC work-grid scale. A `.5` run is half-HRC:
the filtered emission RGB, scalar absorption, bounce input, and RGB radiance
probes all use the half-resolution work grid before full-resolution gather and
composite. A `.25` run is the same arrangement at quarter scale.

`GameHRC2D.luminance_scale` enables an experimental split path. The RGB HRC
uses `scale`; a second monochrome HRC uses `luminance_scale`. At composite time,
the monochrome result supplies brightness while the RGB result supplies local
color ratios:

```glsl
float Y = texture(luminance_tex, uv).r;
float coarseY = dot(fluence, vec3(0.2126, 0.7152, 0.0722));
vec3 reconstructed = coarseY > 1e-8
    ? fluence * (Y / coarseY)
    : vec3(Y);
```

Set `luminance_scale` to zero to retain the single RGB HRC. A useful first
experiment is `--scale .25 --luminance-scale .5`. In the scalar-luminance
variant, the filtered emission and bounce, canvas bounce, gathered radiance,
and radiance history textures use `R16F`; the hierarchy interval and frustum
buffers still use the shared packed RGB layouts. Gather, filter, and bounce
still run at canvas resolution for each transport pass, so the split path has
two transport pipelines and its value depends on the RGB work scale. The
benchmark matrix measures that tradeoff directly.

`luminance_mode` selects how the scalar half of a split run is produced:
It is independent of `upscale_mode`, which controls RGB reconstruction.

| Mode | Behavior |
| ---: | --- |
| 0 | Independent monochrome HRC transport. The child builds its own hierarchy, traces, gathers, bounces, and filters at `luminance_scale`. |
| 1 | Transport reuse experiment. The RGB HRC runs once; the child derives scalar luminance from the parent's final full-resolution gathered radiance and runs only the scalar temporal/edge filter. The child hierarchy and transport dispatches are skipped. |

Mode 1 is the performance experiment requested here. It removes the duplicated
prefilter, trace, extend, merge, gather, and bounce work, but its luminance comes
from the same low-resolution RGB transport. In the matched opaque-interior run,
that made its image error track the color-only result. `luminance_scale` remains
in the configuration for comparison and future derived-filter sizing, but mode
1 does not create an independent higher-resolution luminance hierarchy.

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

```sh
make build-release
python3 tools/benchmark_hrc_lab.py \
  --project projects/hrc_lab \
  --hrc-prototype HRCLabHRC \
  --scale 1 .5 \
  --luminance-mode 0 \
  --trace-levels 3 \
  --upscale-mode 1 \
  --width 960 --height 540 \
  --render-frames 180 \
  --output /private/tmp/hrc-clean-lab
```

Use `--scale 1 .75 .5 .25` to capture the supported full, three-quarter,
half, and quarter HRC work grids. Add `--luminance-scale .5` to run the split
experiment with a half-resolution monochrome transport pass. Use
`--luminance-mode 1` to skip the independent monochrome transport and derive the
scalar signal from the RGB pass. Use `--upscale-mode 0`, `1`, `2`, or `3` to
compare the reconstruction modes. Every generated summary row links to its PNG
and reports its mean RGB error against that run's full-scale reference.

Each run also writes `gallery.md`, which embeds every captured PNG using an
absolute local path so the Markdown can be opened directly in the desktop app.
It also writes `compare.html`, a local before-and-after slider that compares
the full-scale row with each lower-scale row. Markdown can embed the PNGs, but
the draggable comparison requires the HTML page.
The current opaque-interior comparison is also available as the self-contained
[`hrc-lab-compare.html`](hrc-lab-compare.html) snapshot in this directory.

The runner copies the project into each case directory, disables adaptive scale,
validates the edited prototype and scene, warms the renderer for 240 frames,
discards the first 16 measured frames, and writes a PNG plus JSON report. The
reported time includes GPU completion and is not a per-dispatch GPU timestamp.
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

## Split-scale result

The split matrix at 960×540 used `luminance_scale = .5`, color scales `1`, `.5`
and `.25`, trace level 3, and mode 1. The quality comparison here uses the
existing unified full-resolution captures as the reference; it is separate from
the within-split reference numbers in the generated summary.

| Color scale | Mean frame median | Mean RGB error vs unified full | Interpretation |
| ---: | ---: | ---: | --- |
| 1.0 | 7.83 ms | 0.25 | Split luminance adds cost even when color remains full resolution. |
| 0.5 | 5.99 ms | 0.26 | Preserves most scene structure while reducing RGB transport work. |
| 0.25 | 5.46 ms | 0.32 | Lowest tested split cost; color-boundary and opaque-interior scenes show the largest error. |

For comparison, repeating the lower-color-scale runs with full-resolution
luminance produced 8.00 ms / 0.15 error at color `.5`, and 7.88 ms / 0.24
error at color `.25`. Full luminance recovers some brightness edge accuracy, but
the extra full-resolution gather and filter pass removes most of the performance
benefit.

### Post-fix colored-bounce validation

The matrix above predates the colored-bounce correction. Its bounce-active
opaque-interior error values are historical. The monochrome bounce path now
computes `dot(albedo * rgb_bounce, LUMA)` and uses the parent RGB raw bounce
field, preserving the channel interaction before luminance reduction.

On the same 960×540 opaque-interior fixture after that correction, full RGB and
full luminance matched the unified reference to a maximum of 1 sRGB byte, with
zero pixels above a 3-byte difference. The corrected color `.25` / luminance
`.5` run measured 4.68 ms versus 6.78 ms for unified full resolution, with mean
RGB error 0.61, maximum error 21, and 3.23% of pixels above 3. The old near-black
slab came from the invalid luminance approximation; the remaining low-resolution
error is the expected color-scale tradeoff.

### 1080p split transport follow-up

The matched `HRCLabOpaqueInterior` run used 1920×1080, trace level 3, mode 1,
180 measured frames, and the same Metal backend for every row. Error is mean
absolute RGB difference in 8-bit space against the unified full-resolution
image.

| Configuration | Median | p95 | p99 | Mean RGB error | Maximum error | Pixels above 3 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Unified color 1.0 / mode 0 | 24.74 ms | 25.40 ms | 26.37 ms | — | — | — |
| Color 0.25 / no split | 6.16 ms | 8.84 ms | 9.81 ms | 0.921 | 29 | 6.65% |
| Independent split color 0.25 / luminance 0.5 / mode 0 | 10.89 ms | 11.31 ms | 11.49 ms | 0.706 | 28 | 5.07% |
| Reuse mode 1, color 0.25 / luminance setting 0.5 | 7.11 ms | 11.02 ms | 12.58 ms | 0.920 | 29 | 6.69% |

The reuse mode is about 71% faster than unified HRC at the median and 35%
faster than the independent split. It does not retain the quality advantage of
the independent `.5` luminance transport in this fixture: deriving luminance
from the quarter-resolution RGB gather makes its error essentially the same as
color-only. The independent `.25/.5` split is the better quality/performance
point here when the extra 3.78 ms median is acceptable.

The captures are in the matched benchmark directories under
`/private/tmp/hrc-*-1080-20260920-rerun2/`. The self-contained slider with the
same images and measurements is [`hrc-lab-compare-1080.html`](hrc-lab-compare-1080.html).

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
