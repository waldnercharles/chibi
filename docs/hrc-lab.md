# HRC lighting

`GameHRC2D` configures the 2D hierarchical radiance cascades (HRC) that light
2D scenes; `src/render/radiance.c` solves them. This page covers tuning, the
renderer's approximations, and the isolated `projects/hrc_lab` project.

The lab keeps lighting tests free of the arena's starfield, gameplay and
material palette: a neutral backdrop, a fixed camera, and a restrained source
calibration:

- emitter `emission_intensity = 0.03`, `emission_multiplier = 1`
- HRC `intensity = 7.5`, `ambient = 0`, `shadow_floor = 0`, `fog = 0.2`
- `trace_levels = 3`, adaptive resolution off, `upscale_mode = 1`

Lowering the authored emission intensity is the source-only exposure control.
HRC `intensity` scales emissive surfaces as drawn, not the light they cast.

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

| Goal | `resolution` | `color_resolution` |
| --- | ---: | --- |
| Reference | 1 | Full |
| Close to reference, cheaper | 1 | Half |
| Sharp brightness, cheaper color | 1 | Quarter |
| Neon Swarm `HRC` prototype | 0.5 | Full |

## Reconstruction modes

The `upscale_mode` field selects how the low-resolution RGB radiance probes are
reconstructed at canvas resolution. It is ignored when color is below Full,
where the luminance transport drives reconstruction.

| Mode | Reconstruction |
| ---: | --- |
| 0 | Plain bilinear interpolation. |
| 1 | Bilinear interpolation blended toward the nearest probe at absorption edges. |
| 2 | Joint bilateral interpolation weighted by transmittance similarity. |
| 3 | Five-tap cross: the center probe plus its four axial neighbors. |

Mode 1 is the default. Mode 3 preserves small emissive probes and helps some
shadow edges, but a lab sweep at resolution 0.5 found it no closer to the
resolution 1 image overall, and slower in volume scenes. In the basalt Sandbox
at 1080p it matched mode 1 at resolutions 0.5 and 0.35 while costing about
0.6 ms more.

## Resolution settings

| Setting | What it controls |
| --- | --- |
| `resolution` | The finest light grid, as a fraction of the padded canvas. Brightness keeps this detail; most lighting cost scales with its area. |
| `color_resolution` | Color's grid relative to `resolution`: Full, Half, Quarter or Eighth. Below Full, a brightness-only transport runs at `resolution` and the RGB transport on the coarser grid. |
| `gather_resolution` | The received-light image: gather, temporal filtering and bounce, which turn cascade 0 into light per pixel, at the full canvas or half of it. Auto lets adaptive choose. Material raster and composite stay full resolution. |

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
4. `resolution` halved, then halved again, down to `min_resolution`, with
   the authored `color_resolution`.

It averages 30 frames per decision. It steps down after two averages in a row
exceed the frame's time, and ignores the next 30 frames while history refills.
It steps back up after four windows in a row in which the current cost, times
the cost ratio it measured when it stepped down, fits in 80% of the frame's
time.

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
[the measurements](archive/neon-hrc-optimization.md#pairing-opposite-directions-in-luminance).

## Temporal history and lighting margin

`radiance_hysteresis` and `bounce_hysteresis` are current-frame weights: 1
disables temporal smoothing, smaller values keep more history. Radiance history
is reprojected for camera translation, rejected when the view size changes or
sampling leaves the old view, and clamped to the current neighborhood. Bounce
reprojects its previous input, and its history resets when the camera moves.
Moving objects have no motion vectors; neighborhood clamping limits their trails
but does not track disocclusion.

The padded view follows a world-aligned grid at the color grid's scale, so camera
motion does not resize buffers. A fixed reserve for the coarsest color grid
adaptive can pick keeps adaptive from changing the full-resolution raster size.
The final crop follows the exact camera rectangle.

`lighting_margin` extends the raster beyond the camera so offscreen sources and
occluders contribute. Emission fades through the outer half of the margin;
occlusion stays intact. The margin shrinks per axis at the 4096-pixel raster
limit. It enlarges every canvas-sized pass: in Sandbox at 1080p and resolution
1, a 64-unit margin raised scene GPU time from 14.0 to 23.4 ms, so Sandbox keeps
the default of zero.

Solid, unbordered circular emitters use a 1.5-pixel lighting filter with power
normalization, including radii below one pixel, so small moving lights do not
flicker. Graph shapes, strokes, borders, deformed shapes and metaballs keep
their ordinary coverage.

## Surface light and presentation

Solids are shaded as relief facing the camera. Light received in the open space
around a solid reaches `surface_radius` world units into it, scaled by the
material's `GameMaterial.edge_light`. HRC averages light over open space at a
coarse block size and blurs it; concave corners receive less. It keeps the
light arriving from each of its four quadrants, so several lights shade relief
from their own directions. Material normals brighten relief facing a light,
`surface_relief` sets how far sideways nearby light reaches, and material
`Height` casts short shadows toward the strongest light. Near a silhouette the
surface turns toward the open side (`surface_bevel`) to catch rim light.
Diffuse is Lambertian; the highlight is a GGX dielectric with Smith visibility
and Schlick Fresnel, and `specular` of one is physically scaled. Internal
contour seams do not create edges, and no incoming light means no surface light.

With `tone_mapping: 1` the composite stays in linear HDR. A seven-level bloom
(a 13-tap downsample that suppresses single bright pixels, then tent upsampling)
is mixed in by `bloom`, then `exposure` and AgX tone mapping map it to the
display, so highlights keep their color instead of clipping to white.

Reflected light is always attenuated by interaction opacity; the nearly-empty
cell fallback applies only to authored emission. Otherwise transparent,
reflective edges gain energy every frame and overflow.

## Limits

HRC integrates emission and exponential attenuation in the 2D plane with
approximate diffuse redistribution. Gas is a 2D participating medium lit from
received light, which shows shafts behind occluders, but it has no depth, phase
function or colored transmission. Received-light exposure on fluids is a display
adjustment, not extra scattering. Surface light is screen-space: it lights rock
thinner than `surface_radius` from both sides, and near the screen edge the blur
repeats edge values.

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
| `HRCLabManyLights` | 120 small colored emitters, a thin wall, a tilted slab, a pinhole and smoke. The benchmark runner does not include it. |

`HRCLabOpaqueInterior` is the baseline for opaque solids. Materials attenuate
and reflect through the one scene representation; there is no one-way transport
that lights an interior while its exterior blocks fully. No caustics fixture or
path-traced reference exists yet.

Zero ambient makes an opaque pixel with no received light black, so gray around
a source is real fluence or bounce.

## Run the lab

```sh
make build-release
python3 tools/benchmark_hrc_lab.py \
  --project projects/hrc_lab \
  --hrc-prototype HRCLabHRC \
  --resolution 1 .5 \
  --trace-levels 3 \
  --upscale-mode 1 \
  --width 960 --height 540 \
  --render-frames 180 \
  --output /tmp/hrc-clean-lab
```

Use `--resolution 1 .75 .5 .25` to capture full, three-quarter, half and
quarter light grids. `--color-resolution Half` solves color at half of each
resolution, and `--gather-resolution Half` halves the received-light image. Use `--upscale-mode 0`, `1`, `2`, or `3` to
compare the reconstruction modes. Every generated summary row links to its PNG,
reports lighting's GPU time from per-pass timestamps, and reports its mean RGB
error against that run's resolution 1 reference.

Each run also writes `gallery.md`, which embeds every PNG by absolute path, and
`compare.html`, a before-and-after slider between the resolution 1 row and each
lower-resolution row.

The runner copies the project into each case directory, disables adaptive
resolution, validates the edited prototype and scene, warms the renderer for
240 frames, discards the first 16 measured frames, and writes a PNG plus JSON
report. Frame time includes GPU completion; lighting GPU time sums the HRC,
Surface light, Emission mipmaps and Composite passes. Without `--project` the
runner uses the `HRC*` fixtures in `projects/arena2d`.

## GPU phase attribution

GPU labels cover the material raster, fluid density raster, HRC prefilter, the
trace, extend and merge groups (with nested `L0` through `L15` labels for
active levels, and `HRC luminance` variants under split color), gather, filter,
bounce and composite. For timings without a debugger, use per-pass timestamps;
see [GPU benchmarks](neon-gpu-benchmarks.md#time-individual-passes).

Capture one warmed Metal frame with:

```sh
python3 tools/benchmark_hrc_lab.py \
  --project projects/hrc_lab \
  --hrc-prototype HRCLabHRC \
  --scene HRCLabShadow --resolution .5 \
  --trace-levels 3 --upscale-mode 1 \
  --metal-capture --render-frames 180 \
  --output /tmp/hrc-shadow-metal
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
