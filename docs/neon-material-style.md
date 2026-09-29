# NeonSwarm material appearance

The target is a reactive, readable world with fine moving detail. Exhaust keeps
its connected SDF shape. Clouds, combustion, liquids, and wreckage have distinct
appearances and transitions. This is a game approximation, not a thermodynamics
or chemistry simulator.

## Reference inspection, 2026-09-19

I tested [Tutti Space Program](https://tsp.grantkot.com/) with gravity and N-body
disabled, using isolated materials and its heat/cool brushes:

| Experiment | Observed result |
| --- | --- |
| Water, then heat | Blue particles spread; the cursor reported a water liquid/gas mixture at 913 K. |
| Lava, then cooling | Orange illumination disappeared; the remaining dark material reported solid rock. |
| Hydrogen beside oxygen, then heat | A brief bright cloud dispersed into faint points. The reaction products were not verified. |
| Water beside an anchored basalt strip | The strip remained; water dispersed near it. A separate drag test tore material from an unanchored strip. |

These were qualitative UI experiments, not controlled physical measurements.
Disappearance alone does not establish particle deletion.

The [served renderer](https://tsp.grantkot.com/assets/singleton-CC0WmPkn.js)
draws antialiased, deformed particle disks and interpolates previous/current
positions. Temperature changes material color, size, and emission. Reaction
flashes have separate palettes. The build includes threaded WebAssembly MPM
interfaces and WebGL lighting; it is not evidence of a GPU physics solver.

The [material definitions](https://tsp.grantkot.com/assets/mpm-material-manager-BQQNFGyQ.js)
separate phase, heat capacity, conduction, cooling, surface tension, opacity,
emission, absorption, scattering, and refraction. The UI exposes merging and
coarser gas simulation. Its population control is not verified as FPS-driven.

## Changes in this engine

- **Gas:** cold, nonemissive gas uses broader, overlapping density kernels.
  A persistent pressure-projected velocity grid carries gas around colliders
  and sustains wakes. Moving kernels stretch along velocity while preserving
  area, limited to a 2.25:1 aspect ratio. Low-density
  tails fade so isolated parcels do not remain as bright beads. The old
  four-lobe contour is removed. Density resolves at output resolution without
  a full-screen blur. Tight ellipse bounds limit the particle references per tile.
  See [gas flow](neon-gas-flow.md) for the algorithm and approximations.
- **Fuel fire:** small independent cores stretched slightly along velocity.
  They do not join through the exhaust's smooth-union operation. Overlapping
  hot cores use the strongest emission instead of accumulating unlimited light.
- **Cooling smoke:** fuel exhaustion preserves a hot afterglow. Color and light
  output fall with heat; the smoke broadens, becomes thinner, and expires after
  the authored lifetime. Sleeping chunks preserve this state.
- **Received light:** fluid coverage applies the authored HRC exposure to incoming
  light in the final composite. Faint cloud/fuel visibility floors add to that
  light instead of masking small changes. Neither exposure nor the visibility
  floor enters bounce history. Global light haze is reduced to 0.15.
- **Solid surfaces:** rock is shaded as a relief facing the camera. Light
  received in the open space around a solid reaches `GameHRC2D.surface_radius`
  world units into it, scaled by `GameMaterial.edge_light`. The HRC pass averages light over open space at a
  coarse block size and blurs it; concave corners receive less. HRC keeps the
  light arriving from each of its four quadrants, so several lights shade relief
  from their own directions instead of cancelling. Material normals brighten
  relief facing a light, and low roughness adds a highlight. Material `Height`
  casts short shadows toward the strongest light by marching the relief. Near a
  silhouette the surface turns toward the open side (`surface_bevel`) to catch
  rim light. Diffuse is Lambertian and the highlight is a GGX dielectric with
  Smith visibility and Schlick Fresnel; `GameHRC2D.specular` of one is
  physically scaled. Internal contour seams do not create edges, and no incoming
  light means no surface light. Basalt remains nonemissive.
- **HDR presentation:** with `GameHRC2D.tone_mapping`, the composite stays in
  linear HDR. A seven-level bloom (a 13-tap downsample that suppresses single
  bright pixels, then tent upsampling) is mixed in by `bloom`, then `exposure`
  and AgX tone mapping map it to the display. Highlights keep their colour
  instead of clipping to white.
- **Liquid:** the constant readable rim is removed. A faint teal body color
  helps identify liquid in shadow; fuel gas uses a stronger amber visibility
  floor, while ordinary fog remains cooler and dimmer. These are camera-only
  artistic adjustments, with zero cold-material emission into HRC.
  Near-surface normals redistribute incoming light and offset background/light
  sampling. `GameFluidVolume.refraction` controls that offset in output pixels;
  the authored liquid uses eight. Opacity is reduced to show the background.
- **Enemy damage:** three projectile hits exhaust health; falling below 30% of
  the original hull area releases the remaining body sooner. Released debris
  retains material and collision but has no pursuit behavior or health.
- **Debris penetration:** optional `GameCollider.pierce_fragments` lets continuous
  sensors report contacts from fragments authored with
  `GameDestructible.pierceable_fragments` while continuing to the first blocking
  body.
  `Contact Begin` carries the world-space Point (`CF_V2`); the projectile carves there,
  then remains alive for debris hits. Swept events are delivered in travel order,
  and fragments behind a blocking wall receive no hit. A normal shot still stops
  on live enemies, ordinary debris, and intact terrain. This is an authored
  penetration rule,
  without energy loss or a ballistics model.
- **Fragment power:** optional `GameFragmentGlow` fades emitted light over 3.5
  seconds with staggered flicker. Dark fragments keep their collision and motion.

## Cost and limits

The gas kernels share each physical parcel's mass, heat, velocity and collision.
They do not multiply neighbor searches or change gameplay with frame rate. The
simulation population remains bounded; NeonSwarm now pins HRC scale to 0.5 and
disables adaptation. Fine gas density and silhouettes render independently of HRC resolution.
Edge brightening reuses the normal texture and samples a short neighborhood only
on opted-in materials. There is no extra full-scene lighting pass.

HRC integrates emission and exponential attenuation in the 2D plane, with
approximate diffuse redistribution. It has no cloud depth or directional phase
function. Camera opacity fades sparse tails more strongly than transport
absorption. Received-light exposure is a display adjustment, not extra physical
scattering. Surface light is also a screen-space approximation: it lights rock
thinner than its radius from both sides, and near the screen edge the blur
repeats edge values. Its direction is the net direction of nearby light, so two
opposed lights cancel.

Liquid distortion bends the background and incoming light, not foreground
opaque entities. It does not solve Snell's law, Fresnel reflection, depth,
caustics or multiple refractions. Its simulation retains existing cohesion and
split/recombine behavior. Boiling/freezing, oxygen requirements, general chemical
reactions, deformable solids and independent small-scale turbulence remain
outside this pass.

Validation includes GPU checks for continuous gas density, light-dependent
terrain edges with no internal seams, colored gas/liquid lighting and wall
shadowing, refraction confined to liquid, and stable transparent-material
feedback. Noncombat slab fixtures cover the hull-area cutoff and sensor
penetration through fragments, including a blocking wall and a fragment behind
it. `tools/check_neon_swarm.py` exercises real shots through wreckage, enemy
exhaustion, glow fading, material retention and deterministic scene replay.

## Earlier adaptive measurements

These are historical results from before fixed-quality profiling. See
[the fixed-scale performance report](neon-material-performance.md) for current
measurements, Tracy findings and remaining optimization candidates.

Local release captures use 240 frozen warmup frames followed by 180 advancing
frames while moving and firing. The first 16 measured frames are discarded,
leaving 164 samples. Timings include GPU completion and exclude the preview's
60 Hz pacing wait. Adaptive HRC scale is reported with each result. These short
Mac captures do not establish sustained performance or verify Steam Deck hardware.

| Output | HRC scale at end | Median | p95 | p99 | Simulation mean | Render/presentation mean |
| --- | --- | --- | --- | --- | --- | --- |
| 1280x800 | 0.422 | 11.45 ms | 12.64 ms | 13.58 ms | 4.40 ms | 6.81 ms |
| 1920x1080 | 0.419 | 14.55 ms | 16.03 ms | 16.51 ms | 4.38 ms | 9.98 ms |

Both local p95 results fit the 16.67 ms budget. The 1080p margin is small.
The simulation/render split measures wall time around the simulation tick;
render/presentation includes window work, command submission and GPU completion,
not isolated GPU execution time. The preview now reports those means directly.
The moving/firing scene ends with 144 entities, 161 ordinary visual particles,
and 8,787 fluid particles, of which 5,296 are active.

The first measured version exceeded the budget (p95 23.47/25.13 ms with a second
live game paused). A native call-stack sample found two CPU hotspots:

- Heat and blast occlusion repeatedly transformed distant terrain pieces.
  A conservative rotation-independent bound rejects them before the exact test;
  unrotated bodies skip trigonometry.
- The contact broad phase selected a nearby terrain polygon, but narrow-phase
  code then tested every polygon in its parent chunk. It now tests the selected
  polygon and deduplicates only after finding a contact, preserving later
  candidate pieces after a miss.

An isolated 1280x800 comparison with terrain edge lighting disabled changed mean
render/presentation time only from 6.36 to 6.30 ms, while simulation remained
about 11.6 ms. The final optimizations reduced that simulation mean to 4.4 ms.
HRC scale is adaptive, so these runs do not establish an isolated shader speedup.
The complete world and material hashes at frame 420 match before and after both
optimizations. Particle count, material rules and solver substeps are unchanged.
Regression fixtures cover rotated offset occluders, carved gaps, and compound
contact deduplication in addition to the gameplay checks above.

## Starwell rendering capabilities

These optional capabilities serve [Starwell](starwell.md)'s visual direction.
A scene pays for each only while it uses it. Timings are Metal GPU pass
timestamps from the Release build at 1920x1080 on an Apple M4 Pro; the
fixture is `scenes/RenderLab.yaml`, a noncombat scene that shows every effect.

### Screen effects

Screen effects need tone mapping (`GameHRC2D.tone_mapping`): they act on the
linear frame and its presentation.

`GamePost2D` grades and lenses the presented frame. Zero is neutral for every
field, so an absent component and an all-zero one look the same:

| Field | Effect |
| --- | --- |
| `vignette` | Darkening toward the corners, 0 to 1. |
| `chromatic_aberration` | Red and blue split at the corners, in output pixels. |
| `grain` | Film grain amplitude in display units, strongest in midtones. |
| `contrast`, `saturation` | Added to one; -1 saturation is grayscale. |
| `temperature`, `tint` | Linear white balance: cool to warm, green to magenta. |
| `lift`, `gain` | Display color added to shadows; per-channel multiplier added to one. |
| `heat_haze` | Output pixels of shimmer over burning or emissive fluid. |

Behaviors add transient juice with nodes in *Visual/Effects*:

- **Flash Screen**(Color, Amount, Duration) washes the display toward Color.
- **Pulse Chromatic Aberration**(Amount, Duration) adds to the authored split.
- **Add Shockwave**(Position, Radius, Thickness, Strength, Duration) expands a
  ring that bends the frame by up to Strength output pixels. Sixteen rings can
  run at once; a seventeenth replaces the oldest.

Pulses fall off quadratically. A new flash or aberration pulse replaces the
current one when it is at least as strong now. The engine ages them on the
fixed clock (`src/runtime/visual_effects.c`); they are transient presentation,
not saved state.

`GameHeatHaze{radius, strength}` shimmers the frame within radius world
units of an entity, strongest at its center. Up to sixteen sources in view
contribute. Burning and emissive fluid writes a heat weight into the material
cover texture's blue channel, which `GamePost2D.heat_haze` scales.

Shockwaves, heat haze and solid refraction share one distortion pass that
resamples the linear frame before bloom, so bent highlights still bloom. It
runs only while one of them is on screen: 0.09 ms at 1080p. Grading, vignette,
grain, flash and chromatic aberration are part of the tone-map pass; the
pass went from 0.29 to 0.35 ms with aberration active. The distortion is
screen space: it does not reveal what is offscreen, and edges clamp.

### Hit flash

`GameFlash{color, amount, decay}` tints an entity's drawn material toward
color by amount and adds that color as emission (0.3 times the amount per
unit of coverage), so a hit lights its surroundings through HRC. Amount falls
by decay per second on the fixed clock. **Flash Entity**(Entity, Color,
Amount) sets the color and raises the amount; Entity defaults to Self and must
carry a `GameFlash`, which sets its decay. The flash applies after a graph
material's Surface Output, so plain `GameMaterial`, graph materials and
material-mask pieces all flash, borders included. Material entities grew by
one vec4 (352 GPU bytes); the raster cost is within measurement noise.

### Backdrops

`GameBackdrop2D{material, parallax, layer, intensity, lit}` fills the screen
behind the scene with a material graph, over the starfield. The graph's
Position is the offset from the camera center, UV spans the screen and World
Position is the camera position times `parallax` plus that offset: 0 pins the
layer to the camera, 1 moves it with the world. Layers draw back to front in
ascending `layer`, blending by Opacity. Base Color plus Emission, times
`intensity`, is the layer's color; `lit` multiplies Base Color by the HRC
surface light, received light that `GameHRC2D.surface_radius` has already
blurred, so nearby lamps pick silhouettes out of the dark. Without surface
light, lit layers use the unblurred received light.

Layers render at half the output resolution into one premultiplied RGBA16F
canvas that the composite lays over the starfield wherever the scene is open.
The canvas is reused while the camera, layers, parameters and shader revision
are unchanged and no layer is lit or reads Time, the beat or Surface
Influence. RenderLab's six-octave animated nebula plus a lit ridge cost
0.50 ms at 1080p; a full-resolution version cost 1.78 ms. The cost is the
graphs': keep backdrop noise cheap, and prefer a static layer where possible.

### Caustics

`GameHRC2D.caustics` (zero disables; RenderLab uses 0.4) focuses light passing
through liquid into bright moving lines. The fluid raster treats the liquid
surface as a height field: four drifting waves plus the curvature of the
particles' own density, so moving matter bends the pattern. A patch of
surface at x lands at x + depth times its slope; its brightness is
1 / |det J| for that mapping's Jacobian J = I + depth H, evaluated
analytically per pixel. Depth is the strength times each volume's
`GameFluidVolume.refraction`, so per-volume refraction scales caustics too.

The brightness above the mean is added to the liquid's albedo. The composite
multiplies albedo by received light, so caustics show only where light
reaches the liquid, and bounce feeds the same albedo times the previous
frame's light back into the transport as emission, so caustics light their
surroundings (this needs `GameHRC2D.feedback`). The cost is part of the fluid
raster: about 0.03 ms at 1080p for RenderLab's pool. Caustics appear on the
liquid body, the plane behind it; light reaching floors below arrives through
bounce as a soft, moving glow rather than a focused pattern. The waves are
procedural, not a simulated surface.

### Refraction and transmission

Surface Output has two more inputs for 2D solids; their zero defaults change
nothing:

- **Refraction** bends what lies behind the surface by this many output
  pixels along the Normal's tilt. The material raster writes the offset,
  weighted by coverage rather than opacity, to the cover texture's GB
  channels, and the screen distortion pass resamples the finished linear
  frame there, so the background, lit scene and received light behind glass
  all bend, and the bent highlights still bloom. The pass runs only while a
  material that may refract is collected.
- **Transmission** is the fraction of each channel that crosses a meter (32
  world units) of the material. HRC transports gray light, so it absorbs like
  the least transmitted channel, `-ln(min T) / 32` per world unit; the colored
  rest joins the material's albedo. Glass lit from behind then glows in its
  color, and bounce carries that color into what the light reaches next.

Plain `GameMaterial` keeps its `filter` and gray absorption. RenderLab's
`GlassSlab` uses `RenderLabGlass`: a pillowed, slightly wavy cyan pane.

The bend is screen space: it shows what is on screen, clamped at the edges,
not what a lens would really gather. Transmission is not a colored
transport; light behind colored glass is gray light plus the glass's colored
re-emission, which also spreads backward. Refractive solids do not focus
caustics. Adding the two inputs changed the Surface Output pin layout; the
shipped materials were migrated and `CHIBI_PROJECT_VERSION` is now 4.

### Reflections

`GameHRC2D.reflections` (zero disables; RenderLab uses 0.6) makes liquid
surfaces mirror what lies above them. Each liquid pixel marches up the
material normal's liquid mask, 24 steps over 60 world units, to its surface.
Where that surface faces up (its normal's upward component above about 0.35,
as a pool's does), the pixel shows the frame mirrored across the surface,
rippled by time and by the surface normal, fading over 36 world units of
depth. An object h above the surface appears h below it, so only things near
the water show; the mirror is the finished linear frame, so reflected lights
bloom.

The same pass draws `GameReflection`'s planar reflections. With tone mapping
both now run on the linear frame before distortion and bloom; without it they
run on the display frame as before. The pass runs only while liquid is in
view with nonzero strength or a reflector is collected: 0.06 ms at 1080p for
RenderLab. It shows only what is on screen, and a surface deeper than 60
world units below its pixel does not reflect there.

### Trails

`GameTrail{color, width, lifetime, emission}` now draws in 2D scenes too. The
3D arena's trail history (`src/render/arena_trail.c`) records each entity's
position on the fixed clock and fades it over `lifetime`; the 2D path
(`src/render/trail_2d.c`) turns each trail into at most eight capsules of the
material raster, so a trail tapers from `width` at its head, fades out, emits
`color` times `emission` and lights the world through HRC. The history holds
256 trails (it held 64) and records while the scene renders.

200 moving trails cost 0.19 ms of material raster at 1080p (0.31 to
0.50 ms); drawing every history segment as its own capsule cost 0.73 ms. The
eight pieces follow the curve by chords, and their overlapping round joints
show as faint beads along the ribbon. The raster now uploads material
parameters only for graph entities, which trails, particles and plain
materials are not.

### Lights

`GameLight2D{color, intensity, radius}` is an invisible disc of light: it
emits color times intensity, on the same scale as `GameMaterial`'s emission,
over `radius` world units into the HRC transport, and nothing is drawn. Use
it for muzzle flashes, lamps and fire glow without geometry; RenderLab's
brazier carries one. The disc is a material-raster circle marked as a light:
its emission joins the transport's source density but not the emission the
composite displays. Fog still shows the glow in open air around it. A light
costs as much as any small emissive circle.
