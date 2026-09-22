# NeonSwarm material world

## Agreed outcome

Build a playable NeonSwarm cave spanning several screens. Fly through an HRC-lit
cloud and leave a wake, disturb a floating liquid blob, shoot pieces off an
enemy, breach a cave wall, leave the area, and return to its changed state.

Sandbox becomes the Arena2D project's default scene. Remove the other
Arena2D scene and assets used exclusively by it after checking dependencies.
The separate root Arena3D project is outside this cleanup.

## Requirements

- Remove the arena walls and camera confinement. Generate a seeded cave from a
  continuous density field: a connected trunk,
  looping side passages, variable chamber width, and fractal wall noise. Store
  its destructible cells in chunks. This replaces the circular test scaffold.
- Bound active simulation to nearby chunks and a surrounding buffer. Relevant
  moving bodies and projectiles can wake nearby chunks. Retain sleeping state
  during the current run, without advancing missed time when it wakes.
- Support billowing clouds, smoke/exhaust, and cohesive zero-gravity liquid
  blobs that split and recombine. Keep ordinary cheap particles available and
  make fluid participation optional.
- Exchange translational motion between fluid and dynamic bodies. Give the
  player finite thrust and automatic braking so steering remains responsive.
- Remove material at arbitrary hit positions at a bounded cell resolution.
  Rebuild affected collision shapes; disconnected large pieces become rigid
  debris and tiny pieces become inexpensive dust. Damage persists for this run.
- Keep engine capabilities reusable and optional. Scene layout, tuning, enemy
  rules, and effects belong in authored assets, editable through Chibi MCP.
- Target 60 FPS at desktop 1920x1080 and Steam Deck 1280x800, with higher rates
  where possible. Measure the complete scene including simulation, material
  rendering, HRC, and GPU completion. Local results are not Deck verification.
- Use practical approximations wherever they improve the game. Record what
  each approximation changes, why it helps, observable limits, and measured
  savings when available.

Save-and-resume, general chemical reactions, phase changes, continuously deforming soft
bodies, and unlimited world streaming are later work. Detached pieces use
rigid physics. MPM is a fluid candidate, not a mandated implementation.

## Second iteration: fuel, exhaust, rock properties and reflections

Design confirmed September 19, 2026. Implement in Sandbox:

- Normal projectiles and hot engine exhaust ignite finite fuel pockets. Exhaust
  uses the shared fluid solver, pushes gas/liquid, and cools; it does not directly
  excavate or damage bodies. Burning fuel spreads locally, becomes smoke and
  produces bounded blasts. Solids shield bodies until breached. Blasts affect
  player and enemies without faction exemptions.
- Soft and hard rock differ in resistance to repeated hits; brittle crystals
  are especially vulnerable to blasts. Detached pieces retain physical and HRC
  properties. General acid, melting and liquid chemistry remain future work.
- Clouds deform, absorb and emit into HRC, and visually obscure objects. Enemy
  AI is unchanged. Keep the player and nearby projectiles readable.
- Attempt a localized, single-image crystal reflection pass using the scene
  already lit by HRC. Reflections may show nearby visible ships and flames;
  offscreen/hidden content and recursive reflections are unavailable. Use
  colored bounce and facet highlights as fallback if the experiment requires
  broad renderer changes or performs poorly.
- Profile before claiming improvements. Batch cuts per mask, restrict carve
  bounds, cull distant obstacles, and avoid capacity-sized hash clears. Defer
  wholesale GPU simulation and unlimited streaming.

Order: reusable reaction/damage capabilities and tests; scene authoring;
fog/reflection integration; functional captures, full tests, and matched
Release measurements at 1280x800 and 1920x1080. No actual Deck result is implied
by local native measurements.

Approximations: fuel supplies its own oxidizer; heat is a local ignition/cooling
quantity; explosions use bounded radial impulses rather than compressible gas;
reflections are stylized two-dimensional image mappings. Record additional
limitations and validation below as implementation proceeds.

## Implementation sequence

1. Inspect current scene/runtime/rendering ownership and establish reproducible
   baseline captures and timings. Confirm the MCP project root before authoring.
2. Add a small material simulation module with focused tests for conservation,
   sleeping/waking, body interaction, destruction, and splitting. Profile its
   cost before increasing particle populations or choosing GPU residency.
3. Connect material state to physics, authored components, projectile damage,
   and the HRC inputs. Use a dedicated dense-material rendering path rather
   than one general renderer entity per simulated cell or particle.
4. Author the multi-screen NeonSwarm cave, dynamic player controls, fluid
   regions, destructible terrain/enemy, and activation settings. Consolidate
   scene defaults and remove obsolete exclusive content.
5. Validate the acceptance demonstration and another noncombat prototype using
   the same capabilities. Run make build, make test, appropriate rendering
   checks, and optimized complete-frame measurements. Record unresolved
   platform limits and avoid claiming unmeasured performance.

## Progress

- [x] Design agreed; implementation authorized.
- [x] Baseline and implementation design.
- [x] Material simulation and destruction.
- [x] Physics and renderer integration.
- [x] Authored cave and scene consolidation.
- [x] Functional, visual, and local performance validation; device limits recorded.

## Findings and approximation record

Before this change, the player was kinematic and the camera follow graph clamped its position,
and physics locks body rotation. Existing particles use CPU integration and
feed the general material renderer. Free travel, fluid response, and dense
material rendering therefore need changes beyond deleting boundary entities.

The initial native Release preview at 960x540, HRC scale 0.5, measured 164
samples after 240 renderer warmup frames: median 6.598 ms, p95 7.411 ms,
p99 7.952 ms (42 entities, 177 ordinary particles). This predates material
integration and does not establish performance at either target resolution.

Implemented and checked so far:

- Exact cell-mask rectangle coverage, circular carving, connected components,
  large rigid fragments, and capped dust. Masks participate in Box2D, raycasts,
  current contact queries and swept projectile tests; a noncombat slab/probe
  fixture verifies that a cut really opens a passage.
- A CPU spatial hash and two substeps of double-density relaxation. Density
  and correction passes run through the shared worker pool. Native tests check
  ten seconds of liquid cohesion, pair/body translation momentum, sleeping
  lifetimes, capacity behavior, and bit-identical serial/parallel output.
- Optional finite-acceleration velocity drive, including automatic braking.
- Compact GPU fluid splats with exact tile lists feed HRC's existing scene,
  emission, absorption, albedo and normal inputs. The initial cave preview
  rendered successfully at 1280x800; interactive validation is in progress.

Approximations:

- Liquid cohesion is a particle relaxation model, not MPM. There is no elastic
  stress model, and detached solids stay rigid with locked rotation.
- Gas particles are weakly compressible tracers. They repel when crowded and
  exchange motion locally; they do not solve a global incompressible pressure
  field. Initial opacity variation and position jitter are authored-looking
  detail carried by particles rather than another high-resolution fluid solve.
- Body response exchanges linear impulses after the rigid solve. Those impulses
  affect the next rigid integration; there is no torque feedback. Penetration
  correction includes bounded artificial separation velocity.
- Fluid surfaces use summed density splats: optical thickness for gas and an
  isodensity surface for liquid. Lighting is 2D HRC, with density-gradient edge
  normals. This is not 3D volumetric ray marching.
- Fluid emitter particles use circular splats and their initial material
  properties, with a short fade before expiry. Ordinary emitters retain their
  shape and curve implementation.
- Active fluid chunks use a camera buffer plus regions around moving bodies.
  Sleeping particles retain velocity and lifetime with no catch-up. Activation
  currently scans retained particle positions; the retained population cap
  bounds that scan. Far rigid bodies and authored behavior schedules have not
  been converted into a streamed world scheduler.

A 900-step Release movement/fire run traversed 1,756 world units through the
generated scene. A prior Debug scenario timeout was a tooling limit, not a
measurement of target-platform performance.

## References

- [Tutti Space Program](https://www.youtube.com/watch?v=y403LmyHtfc): particle
  simulation using MPM on a tiled sparse grid and radiance-cascade lighting.
- [Noita developer interview](https://80.lv/articles/noita-a-game-based-on-falling-sand-simulation):
  local material rules, dirty rectangles, parallel chunk scheduling, and rigid
  fragments reconstructed after pixel destruction.
- [MLS-MPM](https://yuanming.taichi.graphics/publication/2018-mlsmpm/): a candidate
  particle/grid approach; its research results are not a performance claim for
  this game.
- [Particle-based Viscoelastic Fluid Simulation](https://diglib.eg.org/items/7cad7994-b781-40ce-ad04-0bf61dc94279):
  double-density relaxation informed the simplified liquid solver.

### Procedural terrain revision

The initial circular collision props were a scaffold, not an acceptable cave.
The replacement samples a seeded density field at two world units per cell,
then builds 128x128-cell chunks. A varying trunk and looping side passages define
connectivity; three noise frequencies shape the walls. A flood fill removes
isolated air pockets. Rendering and collision consume the same occupancy masks.

Damage disks cross adjacent terrain chunks. Components touching chunk borders
remain anchored; internal islands detach. This conservative local connectivity
rule avoids a world-sized flood fill after every shot, but cross-chunk rock
islands cannot detach as a single body yet. Generation and static chunk creation
happen once; subsequent cuts rebuild only affected masks.

Noise composition reference: [Red Blob Games](https://www.redblobgames.com/maps/terrain-from-noise/).

A 900-step Release movement/fire run completed. An earlier 120-frame CPU-only
profile averaged 2.757 ms including physics/material simulation, excluding
rendering. It does not establish full-frame performance or Deck performance.

## First iteration validation and local measurements

`make test` passed, including generation/reflection, native reload/watch, MCP,
behavior backends, the main suite, scene documents, whole-function audit, and
the headless engine launch. The native watcher required macOS event access
outside the filesystem sandbox. Focused material tests cover seeded cave
repeatability, connected passages, exact mask coverage, cross-chunk cuts,
retained damage, source lifecycle, liquid splitting/recombination, two-way
translation momentum, finite ship drive, sleeping lifetimes, and worker parity.
The noncombat slab/probe fixture exercises destruction independently of weapons
and enemies. The authored NeonSwarm scene validates without diagnostics.

Release native previews included simulation, material rasterization, HRC, GPU
completion and adaptive lighting resolution. Each used 60 initial simulation
frames, continuous rightward flight and fire, 240 render warmup frames, and
164 reported samples from 180 measured render frames. Results on this Mac:

| Output | Median | p95 | p99 | Final HRC scale |
| --- | ---: | ---: | ---: | ---: |
| 1280x800 | 11.529 ms | 16.640 ms | 17.217 ms | 0.415 |
| 1920x1080 | 13.689 ms | 15.118 ms | 17.090 ms | 0.412 |

At the matching 240-step snapshot the scene held 8,527 fluid particles,
3,345 active fluid particles, 170 ordinary particles and 95 solid masks. The
retained fluid capacity is 32,768; these measurements do not prove 100,000
active particles. The final source-lifecycle fix passed the material and
physics suites after the full test run.

These runs measure different camera aspect ratios and adapt lighting quality
during execution; they are not a fixed-quality resolution scaling comparison.
The p99 values still exceed the 16.67 ms budget. Steam Deck and a representative
low-end integrated GPU remain unverified. Fluid rendering currently requires
the native compute renderer; there is no GLES fallback for the new fluid pass.

Launch with `make play`. Move with the existing movement controls, aim/fire with
mouse or stick, and use the existing boost action. Change `MaterialTerrain` on
`Cave` through authoring to choose the seed, extent, tunnel radius,
roughness, or damage cell size; restart the scene to regenerate it. The current
map is 4096x1536 world units at two units per cell, stored as 128x128-cell chunks.
The cloud silhouette, feathering and grain are seeded particle appearance; rock
grain is a continuous world-space material. The original readability emission
was removed in the dark cave pass below.

## Second iteration validation and corrections

Completed September 19, 2026. NeonSwarm now contains two finite sealed fuel
pockets, stronger rock, brittle reflective crystals, hot shared-solver exhaust,
and gas that burns into smoke. Native noncombat fixtures check heat shielding,
synchronous ignition, consumption, sleeping, repeated-hit resistance, inherited
fragment properties and outward debris motion. The scene acceptance script
checks real graph execution, excavation and deterministic fuel burnout.

Corrections from gameplay feedback:

- Convert downward-positive fluid Y to render Y once. A GPU regression checks
  both the correct and mirrored pixels, with and without vertical camera offset.
- Place bullets 16 units along the rendered ship heading, independently of aim.
  Keep the existing physical ship collider as the fluid obstacle. Increase
  movement/boost exhaust rate, size and lifetime, with full velocity inheritance.
- Restore enemies to 20x20 with emissive borders on their surviving mask edges.
  Retain removed chips and eject them outward from the hull. Tiny/excess debris
  produces a small, non-emissive dust puff rather than a bright explosion.
- Reduce projectile cuts from radius 13 to radius 4. Advance the authored hit
  point by 2.5 units along the bullet direction to reach the surface past its
  radius 2 collider. This is a cheap bullet-specific contact approximation.
- Clamp swept projectile poses before fluid coupling. A native regression first
  reproduced ignition through an intact wall, then verified shielding and
  ignition after the wall is breached.
- Fix runaway HRC feedback at nearly transparent material edges. The original
  near-empty-cell fallback applied to emission plus reflected light. Repeated
  reflection therefore gained energy and could overflow into black output.
  Reflected light now always uses interaction opacity in trace and gather;
  the existing emissive fallback applies only to emission. Crystal absorption
  is also authored at 1, while its high albedo and image reflection remain.
  In the GPU regression, a 0.0005-absorption, 0.9-albedo wall changed from a
  first-frame receiver sum of 95.602 to 0 after 120 frames before the fix. After
  the fix it settles at 105.893. Existing opaque bounce and absorption tests pass.

Optimizations in this iteration clear only occupied particle hash buckets,
reject distant fluid obstacles against active bounds, skip sleeping-only solves,
restrict damage scans to their local bounds, and batch cuts before rebuilding a
mask. These are implemented savings in work; isolated speedup percentages have
not been measured. Mask splitting still allocates scratch space, and large
retained worlds still scan particle/chunk records. Those are candidates for
future profiling, not evidence of 100,000 active particles at 60 FPS.

`make build`, `make build-release`, and `make test` passed. The macOS watcher
requires native filesystem event access outside the sandbox. The GPU suite
passed, including transparent feedback, fluid/camera Y alignment, spatially
recognizable reflections, HRC transport and existing renderer checks.
`tools/check_neon_swarm.py` passed, and the scene validates without diagnostics.

Final local Release measurements used the same input timeline at both outputs:
60 initial simulation steps, continuous rightward flight and fire, 240 frozen
simulation/render warmup frames, then 180 advancing frames with 164 reported
samples. Timing includes simulation, fluid/material rasterization, HRC,
reflection composition and GPU completion. The 60 Hz pacing sleep is excluded.

| Output | Median | p95 | p99 | Final HRC scale |
| --- | ---: | ---: | ---: | ---: |
|1280x800|11.067 ms|11.702 ms|11.871 ms|0.344|
|1920x1080|13.278 ms|13.892 ms|14.259 ms|0.349|

These sampled runs fit the 16.67 ms budget on this Mac with adaptive lighting.
The lower HRC scale means this is not a fixed-quality comparison to the first
iteration. Steam Deck and low-end integrated GPU performance remain unverified.
The run includes breaching and burning the nearby fuel pocket. A separate
combat scenario confirms small red fragments leave the damaged enemy while
preserving its outline and glow. Vertical-flight captures show attached exhaust
and nose-fired projectiles without mirrored fluid interactions.

Additional approximations: fog is a shallow visual overlay and preserves some
emission for readability; reflections sample the visible lit scene only;
fragment ejection is a bounded gameplay kick without rotational dynamics;
blast damage uses 24 first-hit spokes with two-unit sampling and a bounded
number of ignition regions per tick. No general chemistry, continuous soft-body
deformation, full rigid-body streaming or new GLES fluid fallback is implied.

Evidence lives in `/tmp/neon-material-world`: `enemy-chips.png`,
`vertical-fixed.png`, `final-performance-1280.json`,
`final-performance-1920.json`, `hrc-feedback-red2.log`,
`hrc-feedback-green.log`, `materials-green.log`, `acceptance2.log`, and
`final-tests-native2.log`.

## Dark cave and SDF surface pass

Completed September 19, 2026. This supersedes the readability emission and sealed
fuel containers described above. Ordinary rock, crystals, liquid and unburned
gas now have zero emission; the rock material graph's emission branch was removed.
HRC ambient remains zero and the shadow floor changed from 0.015 to 0.0005.
Fuel clouds are uncontained and still ignite through ordinary projectiles or exhaust.

Cave generation now retains its signed scalar field instead of discarding the
values after occupancy classification. Circular damage subtracts a local SDF.
Contours interpolate zero crossings, retain disconnected islands, merge solid
interiors, and rebuild only on edits. The SDF raster evaluates their boundaries
at material resolution. Box2D collision, damage thresholds and fragment
connectivity retain the coarse cells. Contour fidelity and chunk-edge limitations
are documented in [SDF terrain notes](sdf-terrain-notes.md), along with the
video's primary references and deferred sparse-atlas/clipmap options.

Fluid conversion retains emitter size/color/emission curves, drag and blend
radius. Exhaust and fire use smooth unions of circles; liquid uses a narrow,
pixel-sized isodensity transition. Gas retains its soft volume appearance.
Movement/boost emitters were retuned toward the original connected trail.
Cursor samples now remain manual aim without firing, and the authored movement
graph sets orientation from aim every tick, including idle and strafing.

Terrain outside the padded HRC view is culled before preparing draw records.
Material graph parameters are resolved once per visible mask instead of once
per contour piece. This reduces the cost of the extra surface detail without
raising simulation resolution.

Validation: `make build`, `make build-release`, native `make test`, material
regressions, native GPU radiance regressions and the release NeonSwarm acceptance
script passed. Scene, movement behavior and rock shader validation reported zero
errors or warnings. New regressions cover subcell boundaries, circular cuts,
diagonal island separation, mouse-up aiming, crisp liquid, connected flames and
lifetime shrinkage. Existing HRC feedback stability remains covered.

Release measurements on this Mac: 60 initial simulation frames, 240 frozen
render warmup frames, then 180 advancing frames (164 reported). Continuous right
movement and firing; CPU work and GPU completion included, 60 Hz pacing excluded.

| Pixels | Median | p95 | p99 | Final adaptive HRC scale |
| --- | ---: | ---: | ---: | ---: |
| 1280x800 | 12.196 ms | 13.060 ms | 13.324 ms | 0.4210 |
| 1920x1080 | 14.738 ms | 16.169 ms | 17.004 ms | 0.4189 |

The p95 fits the 60 Hz budget; occasional 1080p frames exceed it. These are local
Mac results, not Steam Deck validation. Adaptive HRC quality and the uncontained
fuel behavior differ from earlier runs, so this is not a fixed-quality speedup
claim. Evidence is under `/tmp/neon-material-world`: `dark-tests.log`,
`dark-materials.log`, `dark-radiance.log`, `dark-acceptance2.log`,
`dark-performance-1280.json` and `dark-performance-1920.json`, plus matching images.
