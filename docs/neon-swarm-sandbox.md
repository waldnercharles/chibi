# Neon Swarm material sandbox

Neon Swarm lives in `projects/arena2d`. Its startup scene is
`content/scenes/Sandbox.kdl`, selected by native launches and the Make targets.
Run `make play`, or build an optimized native version:

```sh
make build-release
./build-release/chibi --project-root projects/arena2d \
  --scene content/scenes/Sandbox.kdl --play
```

Move with WASD or the left stick, boost with Space, and aim/fire with the mouse
or right stick. The ship faces the cursor even without firing and while strafing.
A neutral stick with no cursor sample retains the nearest-swarmer fallback; firing
still requires Fire. The player has finite thrust and braking. Restart the
scene to reset damage and consumed fuel; the current run retains them.

The HUD in the top-left corner shows the ship's hull as a meter, which turns
red below 30%, and its speed, whose digits roll (`<number-flow>`). Its Repair
hull button, or H, restores the hull.
The `HUD` prototype shows `ui/HUD.rml`, which reuses the `Meter` widget, and
its `HudDisplay` behavior sets both each Tick and handles the HUD's `Repair`
event.

## Authored content

The seeded cave has no edge. Terrain uses two-unit cells in 128x128-cell tiles
that stream around the camera; every visible tile loads at once and one more is
prefetched per frame. `BasaltCave` generates a warped network of chambers, side
passages and shafts joined to the original tunnel. The rock is one continuous
grid of basalt columns 18 to 47 units wide that thin, widen and lean gradually;
in scattered regions they fan up to about 25 degrees. Cross-joints divide each
column into segments, and every exposed end lies on one. Column ends are
chaotic. The column grid and joint lattice are functions of world position
shared with the `Basalt` material, so surface joints match the silhouette. Generation removes rock islands and air pockets
under 4000 cells that lie wholly inside a tile. Three
placed guardians replace the old spawn director. The alternate Arena2D scene
and its exclusive assets have been removed; the root Arena3D project remains.

Asset names describe their role within this project. `Player`, `Swarmer`,
`Projectile`, `Camera`, and `Cave` own composition and tuning. Behavior graphs
use expressions for small calculations and assignments, with explicit branches,
shot loops, spawning, and contact handling. `HullDamage` is distinct from the
`Damage` event because loaded graph assets share a name registry.

| Behavior | Visible nodes before | After | Edges before | After |
| --- | ---: | ---: | ---: | ---: |
| Movement | 23 | 7 | 27 | 6 |
| Pursuit | 20 | 7 | 27 | 7 |
| Weapon | 22 | 16 | 27 | 19 |
| CameraFollow | 6 | 4 | 5 | 3 |
| MovementEmitter | 10 | 4 | 12 | 3 |
| BoostEmitter | 3 | 3 | 2 | 2 |
| HullDamage | 12 | 10 | 14 | 12 |
| ProjectileHit | 10 | 9 | 14 | 13 |
| ProjectileLifecycle | 11 | 7 | 13 | 6 |

These counts cover the visible event graphs. Expressions retain their generated
functions for inspection. Movement speed, weapon cooldown, and shot damage read
the prototype's components. The short spawn-effect graphs remain separate.
Behavior attachment keeps these rules scoped to their entities; compatible Tick
entries can already use the runtime's compiled system loops.

`FreeMotion` demonstrates a standalone System. Its query selects visible entities
with transform and velocity, excluding physics bodies and colliders. It moves
`DriftingMarker` in both `Sandbox` and the noncombat `MaterialStudy` scene.
Combat, firing, and particle lifecycles remain attached behaviors.

Movement targets speed 120 with acceleration 800; boost targets 240. Fire emits
400-unit-per-second projectiles at 8 Hz with a 2.5-second lifetime. Shots start
16 units along the ship's rendered heading, then travel toward aim. Swept contacts carry the impact point; an authored half-unit forward offset
and four-unit damage disk cut into the contacted surface. Shots chip enemy debris
and continue to the next blocking body. Ordinary debris, intact walls, and live
enemies stop shots.

Swarmers retain their original 20x20 dark square and red emissive border. Cuts
leave permanent gaps. Removed chips and disconnected pieces become drifting
rigid bodies and retain their material. Tiny/excess pieces become a small,
non-emissive dust puff. The fragment limit is 96. Rotation remains locked.
Each projectile hit also consumes health. The third hit, or a main hull reduced
below 30% of its original area, releases the remaining hull as inert debris. Detached pieces flicker and lose their
glow over 3.5 seconds; their collision and motion remain.

Clouds, cohesive liquid blobs, fuel, smoke and engine exhaust share the optional
CPU particle solver. Movement and boost exhaust inherit ship velocity and use
connected SDF surfaces, authored shrinking, color fades and drag. Their hot particles can ignite fuel. Neither exhaust
nor fog changes enemy AI. Ordinary projectile trails and other inexpensive
visual particles remain separate.

Amber fuel floats in two uncontained clouds. Normal shots or exhaust ignite
exposed gas. Burning consumes fuel, pushes nearby matter and becomes smoke.
Gas fire uses small separate hot cores. As smoke cools, its color, light output,
and opacity diminish, with a 4.5-second lifetime in active chunks.
Shots into basalt throw hot chips out of each crater along its surface normal;
they cool as they fly. A puff of lit rock dust drifts from the crater and
thins out. Broken pieces keep the material pattern of the rock they came from.
Walls shield heat and blast forces until breached. Blasts chip solids and damage
the player's health. Hard rock needs repeated hits; brittle crystals resist
ordinary shots but break more easily under blasts. Detached material keeps
these properties.

## Rendering and approximations

Terrain retains continuous signed samples and circular SDF cuts, rendered as cached
contours; physics retains the cell grid. See [SDF terrain notes](sdf-terrain-notes.md)
for the reference research and fidelity limits. Rock, crystals, liquids and unburned
gas emit no light. The shadow floor is 0.0005, with no ambient light.
Gas has a faint authored camera visibility floor that contributes no HRC light
or bounce. Four overlapping density lobes follow each parcel and join into a
continuous cloud without visible particle disks or a full-screen blur. Fluids
receive the same display exposure as emitters. Terrain is lit only by incoming
light: the faces of rock near open space receive that space's light, fading over
64 world units, and the physically based `Basalt` material's prism faces,
cross-joints, streaks and glinting phenocrysts respond to the
directions it arrives from, with short relief shadows. The frame is tone mapped
with AgX after a light bloom. Liquid
edges brighten from light received just outside them. There is no constant outline. Liquid has no visibility floor; its near-surface normals distort the
background and incoming light by up to eight output pixels. See [material style notes](neon-material-style.md)
for reference experiments and the rendering shortcuts.

The 2D scene uses HRC emission, absorption and colored bounce, solved at half
resolution with fixed scale, the four light directions split over two frames. Fluid density
splats feed its material textures. Fog overlays visible objects as a shallow
volume and retains some ship/projectile emission for readability. This is a 2D
optical approximation, not 3D volume ray marching. Physics uses downward-positive
world Y; the renderer converts it once to upward-positive Y, including fluids.

Crystal surfaces additionally reflect the already-lit scene image across a
local plane. This provides recognizable spatial detail without a second scene
render. It cannot recover hidden/offscreen objects or recursive reflections.
Crystals also have stronger diffuse reflection and authored absorption. HRC
always attenuates reflected feedback by interaction opacity; the nearly-empty
cell emission fallback applies only to authored emission. It must not turn
transparent reflective edges into multiplying light sources.

The fluid solver uses a spatial hash, two relaxation substeps and shared workers.
Heat occlusion rejects distant pieces before exact geometry tests. Compound
contact queries test the polygon selected by Box2D rather than every polygon in
its chunk. These optimizations preserve simulation state and particle counts.
Gas is a weakly compressible tracer model, liquid uses approximate cohesion,
and solid fragments use Box2D. Crater rock chunks use a separate lightweight
solver: circles that bounce off the terrain's signed distance, are shoved by
moving bodies and blasts without pushing back, split on hard knocks and shrink
away within about two seconds. Tile generation runs in
eight-row bands on the shared task pool. Hot fuel supplies its own oxidizer. Blasts use
bounded radial impulses and first-hit damage spokes. These are gameplay
approximations, not a general chemistry or deformable-solid solver.

## Validation

`python3 tools/check_neon_swarm.py --build build-release` checks movement, free
travel, muzzle origin, expiry, enemy appearance, repeated excavation, finite fuel
burnout and deterministic scene state. Enemy checks also cover hit exhaustion,
inert debris, power fading, and real shots reaching a target behind wreckage.
`build/test_suite --materials` exercises
sleeping, liquid cohesion/splitting, two-way momentum, damage, hull-area release,
fragment penetration, chips and wall shielding independently of combat. `build/test_suite --radiance` requires native
GPU access and checks joined gas density, fluid/camera alignment, illuminated
edges, colored volume lighting and shadows, localized liquid distortion,
recognizable reflections, and long-running transparent-material feedback.

See [the implementation plan](neon-material-world-plan.md) for complete-frame
measurements, their boundaries and retained limitations. Desktop 1080p60 and
Steam Deck 1280x800 are targets; local Mac measurements do not verify Deck
hardware. The new dense fluid renderer requires native compute; no GLES fluid
fallback is implemented. Far fluid sleeps with a camera buffer, while far rigid
bodies and behavior schedules are not yet a fully streamed world scheduler.

Author assets through Chibi MCP with `CHIBI_ROOT` set to this checkout's
`projects/arena2d` and `CHIBI_CTL` set to its built controller. Native runtime
changes require rebuilding and relaunching an already-open game/editor.
