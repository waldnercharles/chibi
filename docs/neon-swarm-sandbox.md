# Neon Swarm

Neon Swarm lives in `projects/arena2d`, the default `PROJECT` for the Make
targets. Its start scene is `content/scenes/Sandbox.yaml`; the design direction
is in [world exploration](neon-world-exploration.md).

```sh
make play                                  # optimized standalone game
make play SCENE=scenes/MaterialStudy.yaml  # noncombat material study
```

For a repeatable game-only capture:

```sh
build/chibi --play --project-root projects/arena2d --scene scenes/Sandbox.yaml \
  --frames 120 --fixed-dt 0.016666667 --screenshot /tmp/neon.png
```

`--fixed-dt` feeds a fixed wall-clock step per display frame; the engine consumes
it in 1/60-second simulation steps. `--frames` alone only limits the run.
Screenshots are native-only.

## Playing

Move with WASD or the left stick, boost with Space, and aim/fire with the mouse
or right stick. The ship faces the cursor even without firing and while
strafing; a neutral stick with no cursor sample falls back to the nearest
swarmer. The player has finite thrust and braking. Restart the scene to reset
damage and consumed fuel.

The HUD shows the hull as a meter, red below 30%, and the speed, whose digits
roll (`<number-flow>`). Its Repair hull button, or H, restores the hull. The
`HUD` prototype shows `ui/HUD.rml`, which reuses the `Meter` widget; its
`HudDisplay` behavior sets both each Tick and handles the `Repair` event.

## Authored content

The seeded cave has no edge. Terrain uses two-unit cells in 128x128-cell tiles
that stream around the camera; every visible tile loads at once and one more is
prefetched per frame. `BasaltCave` generates a warped network of chambers, side
passages and shafts joined to the original tunnel. The rock is one continuous
grid of basalt columns 18 to 47 units wide that thin, widen and lean gradually;
in scattered regions they fan up to about 25 degrees. Cross-joints divide each
column into segments, and every exposed end lies on one. The column grid and
joint lattice are functions of world position shared with the `Basalt`
material, so surface joints match the silhouette. Generation removes rock
islands and air pockets under 4000 cells that lie wholly inside a tile. Three
placed swarmers guard the start.

`Player`, `Swarmer`, `Projectile`, `Camera`, and `Cave` own composition and
tuning. Behavior graphs use expressions for small calculations and explicit
branches for shot loops, spawning and contact handling. Movement speed, weapon
cooldown and shot damage read the prototype's components. `HullDamage` is
distinct from the `Damage` event because loaded graph assets share a name
registry. The `FreeMotion` System moves `DriftingMarker` in both `Sandbox` and
`MaterialStudy`; see [the material engine](material-engine.md#standalone-systems).

Movement targets speed 120 with acceleration 800; boost targets 240. Fire emits
400-unit-per-second projectiles at 8 Hz with a 2.5-second lifetime. Shots start
16 units along the ship's rendered heading, then travel toward aim. Swept
contacts carry the impact point; an authored half-unit forward offset and
four-unit damage disk cut into the contacted surface.

Projectiles set `GameCollider.pierce_fragments`, so they chip fragments marked
`GameDestructible.pierceable_fragments` and continue to the next blocking body.
Swept contacts arrive in travel order, and fragments behind a blocking wall
receive no hit. Ordinary debris, intact walls and live enemies stop shots. This
is an authored penetration rule without energy loss.

Swarmers are 20x20 dark squares with red emissive borders. Cuts leave permanent
gaps. Removed chips and disconnected pieces become drifting rigid bodies that
keep their material; tiny or excess pieces become a small non-emissive dust
puff. Rotation stays locked. The third hit, or a hull reduced below 30% of its
original area, releases the remaining hull as inert debris with no pursuit or
health. `GameFragmentGlow` fades detached pieces' light over 3.5 seconds with
staggered flicker; their collision and motion remain.

Clouds, cohesive liquid blobs, fuel, smoke and engine exhaust share the optional
CPU particle solver; ordinary projectile trails and other cheap visual particles
stay separate. Movement and boost exhaust inherit ship velocity and use connected
SDF surfaces, authored shrinking, color fades and drag. Hot exhaust particles
can ignite fuel. Neither exhaust nor fog changes enemy AI.

Amber fuel floats in two uncontained clouds. Shots or exhaust ignite exposed
gas. Burning consumes fuel, pushes nearby matter and becomes smoke. Walls shield
heat and blast forces until breached. Blasts chip solids and damage every body,
including the player. Hard rock needs repeated hits; brittle crystals resist
ordinary shots but break more easily under blasts. Detached material keeps these
properties.

Shots into basalt throw grit, sparks in the weapon's light color and hot rock
chunks out of each crater along its surface normal. Chunks cool as they fly,
bounce off terrain, get shoved by bodies and blasts without pushing back, split
on hard knocks and shrink away within about two seconds. A puff of lit rock
dust drifts from the crater and thins out.

## Appearance

These are game approximations, not a thermodynamics or chemistry simulation.

- **Rock:** terrain keeps continuous signed samples and circular SDF cuts,
  rendered as cached contours; physics keeps the cell grid. See
  [SDF terrain notes](sdf-terrain-notes.md). Rock, crystals, liquid and unburned
  gas emit no light, and there is no ambient light. Rock near open space receives
  that space's light over 64 world units, and the physically based `Basalt`
  material's prism faces, cross-joints, streaks and phenocrysts respond to its
  direction with short relief shadows; see
  [HRC surface light](hrc-lab.md#surface-light-and-presentation).
- **Gas:** cold gas follows the [gas flow](material-engine.md#gas-flow) field.
  Its render kernels are broad and overlapping, stretch along velocity up to a
  2.25:1 aspect ratio while preserving area, and fade in low-density tails so
  isolated parcels do not show as beads. Density resolves at output resolution
  without a full-screen blur. Gas has a faint camera-only visibility floor that
  adds no HRC light or bounce; fuel's is amber and stronger.
- **Fire and smoke:** burning fuel uses small separate hot cores stretched along
  velocity; overlapping cores take the strongest emission instead of adding up.
  Exhaust uses smooth unions of circles. Smoke keeps a hot afterglow, then its
  color, light and opacity fall as it cools and broadens, over a 4.5-second
  lifetime in active chunks.
- **Liquid:** an isodensity surface with a faint teal body color and no constant
  rim. Edges brighten from light received just outside them. Near-surface normals
  bend the background and incoming light by `GameFluidVolume.refraction` output
  pixels (8 here). This does not model Snell's law, Fresnel reflection, depth,
  caustics or foreground refraction.
- **Crystals:** reflect the already-lit scene image across a local plane,
  without a second scene render. Hidden, offscreen and recursive reflections are
  unavailable.

Sandbox overrides its `HRC` prototype to run adaptive lighting from resolution 1
down to 0.5, with HDR tone mapping, bloom 0.2 and fog 0.66. Fluids receive the
same display exposure as emitters. Physics uses downward-positive world Y; the
renderer converts it once to upward-positive Y, including fluids. The dense
fluid renderer requires native compute; there is no GLES fluid fallback.

## Validation

- `python3 tools/check_neon_swarm.py --build build-release` checks movement,
  free travel, muzzle origin, expiry, enemy appearance, repeated excavation,
  finite fuel burnout and deterministic scene state, plus hit exhaustion, inert
  debris, power fading and real shots reaching a target behind wreckage.
- `build/test_suite --materials` exercises sleeping, liquid cohesion and
  splitting, two-way momentum, damage, hull-area release, fragment penetration,
  chips and wall shielding independently of combat.
- `build/test_suite --radiance` needs native GPU access and checks joined gas
  density, fluid/camera alignment, illuminated edges, colored volume lighting and
  shadows, localized liquid distortion, reflections and long-running
  transparent-material feedback.

[GPU benchmarks](neon-gpu-benchmarks.md) covers fixed-quality performance
measurement. Desktop 1080p60 and Steam Deck 1280x800 are targets; Mac
measurements do not verify Deck hardware.

Author assets through Chibi MCP with `CHIBI_ROOT` set to `projects/arena2d` and
`CHIBI_CTL` set to the built controller. Native runtime changes require
rebuilding and relaunching an already-open game or editor.
