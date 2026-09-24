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
