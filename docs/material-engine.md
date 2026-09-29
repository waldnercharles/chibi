# Material engine and standalone Systems

Material algorithms operate on native grids and masks. Authored fields define
geometry; reflected components define tuning; behaviors decide gameplay outcomes.
`projects/arena2d/content/scenes/MaterialStudy.yaml` exercises this boundary with
two procedural specimens, a cut operation, and a moving marker. It has no fluid
world, health, projectile, encounter, or combat configuration.

## Runtime boundaries

| Module | Owns |
| --- | --- |
| `material_field` | Named compiled fields, typed overrides, atomic replacement and revisions |
| `field_program` / `field_ir` | Pure scalar compilation and bounded native bulk evaluation |
| `material_terrain` | Connectivity cleanup and 128-cell runtime chunks, including partial edges |
| `material_stream` | Tile selection, generation budget, eviction and restoration of cold masks |
| `material_mask` | Occupancy, accumulated damage, distance, connected components and immutable geometry views |
| `material_destruction` | Queued energy, resistance, splitting, retained chips, release and typed results |
| `material_solid` | Signed distance, segment and cell queries over static masks placed in the world |
| `material_fluid` | Fluid solving and activation |
| `material_ecs` / `material_ecs_fluid` / `material_ecs_terrain` | Entity ownership, configuration, terrain views, fragments, particles and event delivery |
| `material_nodes` | Graph service bindings |
| `materials` | Service registration and tick orchestration |

The field, mask, terrain and destruction algorithms do not depend on gameplay
components, rendering, emitters or Box2D. `material_host` supplies source reads,
fragment ownership transfer, destruction, geometry invalidation and result
callbacks. Sources must keep their mask addresses stable during callbacks.
Mask views borrow storage until the next mutation; geometry consumers track the
mask revision. The ECS fragment adapter copies an explicit presentation/physics
bundle and omits health, projectiles and attached behaviors. Fragments with a
material shader keep a `GameMaterialFrame`: where their source's frame sat when
the material was laid out. World-space material patterns stay attached to the
piece as it moves.

`MaterialDestructionWorld` bounds retained fragments. `MaterialFluidWorld`
configures particles, activation regions and fluid chunk size. Either capability
works independently; its `debris_limit` also bounds crater rock chunks.
`MaterialBreakProfile` owns kick speeds and crater effects; missing profiles
produce no impulses or dust. Everything sprays around the crater's outward
normal, found from the open cells around it: grit, one grain per crumbled cell;
sparks in proportion to the striker's heat, starting in its light color and
cooling through `spark_color`; rock chunks; and `smoke_count` gas parcels. The
strike also shoves loose chunks already near the crater. Chunks show the source
material, glow in its spark color while a hot strike's heat cools, tumble,
bounce off terrain and get shoved by bodies and blasts. A hard knock splits a
large chunk into two or three pieces, at most twice; every chunk shrinks away
at the end of its short life and leaves a small puff of the source's smoke
color. `Damage Material`
takes the striking entity as `Source`; its `GameMaterial` emission color and
`GameIgniter` heat describe the impact. `MaterialTerrain` names a field,
typed parameter object, seed, bounds, cell size, connectivity and collision filters.

## Gravity and static solids

A scene's `physics.gravity` (`{x, y}` in units per second squared, +Y down,
default zero) is the one world acceleration. Box2D applies it to dynamic
bodies scaled by `GameBody.gravity_scale`; a ship held by a `GameDrive` uses
zero. Omitted fields read as zero, so existing bodies float; adding a GameBody
in the editor starts at one. `GameBody.free_rotation` lets a dynamic body turn
under contacts and writes its Orientation. Detached fragments fall with scale
one; terrain fragments tumble, other fragments keep their source's setting.
Crater chunks fall with the same gravity.

Static masks (terrain tiles and destructibles without a dynamic or kinematic
body) reach matter through `material_solids`, rebuilt each tick, instead of
per-rectangle obstacles. Queries bilinearly sample the masks' signed distance
samples, the contour the renderer draws, and union overlapping masks. Liquids
collide with that distance: a swept segment test stops fast particles at thin
walls, then particles are pushed to a skin outside the surface, lose their
approach speed and slide with light friction, so they pool on floors and run
down slopes and shafts. Gas flow cells near solids hold still; heat and blast
occlusion trace the same distance. Moving bodies remain box and circle
obstacles with two-way impulses.

Approximations: the procedural field is not an exact Euclidean distance far
from the surface, so a push is capped at two cells per substep; particles
trapped deeper recover over several steps.

## Substances, heat and reactions

Matter is data. A prototype with `MaterialSubstance` is a substance named by
the prototype; a prototype with `MaterialReaction` is a reaction. The fluid
world discovers every registered prototype of either kind, so the same rules
hold in every scene. Substance indices only grow while the world lives, and
edited prototypes update the tables in place at the next frame boundary.

| Field | Meaning |
| --- | --- |
| `phase` | Gas, Liquid, Powder or Elastic |
| `density` | Mass per area; one matches rigid bodies |
| `viscosity`, `cohesion` | Liquid tuning |
| `buoyancy` | Gas only: fraction of gravity that lifts it, times 1 + temperature; negative sinks |
| `color`, `emission`, `absorption`, `visibility`, `refraction` | Look |
| `temperature`, `cooling` | Temperature it forms at; exponential cooling toward ambient zero per second |
| `lifetime` | Seconds it lasts, zero for persistent; gas that expires billows and fades |
| `ignite_at`, `burn_rate`, `burn_heat`, `burns_to`, `blast` | Flammability: at `ignite_at` it burns for `1 / burn_rate` seconds, held at `burn_heat`, then becomes `burns_to` (or vanishes); each ignition adds `blast` to its neighborhood's explosion |
| `boil_at`, `boils_to` | At or above, it becomes `boils_to`; empty never boils |
| `freeze_at`, `freezes_to` | At or below, it becomes `freezes_to`; empty never freezes |
| `corrosion` | Damage per second to touching solids |
| `conductive` | Carries arcs; reported by queries |

Products name another substance or a deposit target, a
`MaterialTerrain.deposit`. A particle that changes substance keeps its motion,
takes the new substance's look and lifetime, and keeps the hotter of its
temperature and the new substance's.

`MaterialReaction {a, b, rate, a_becomes, b_becomes, heat}`: where substance
`a` touches `b` (within 0.7 kernels), each pair reacts with probability
`rate` per second; both change (empty keeps them) and gain `heat`. `b` may be
`Burning`, which matches anything on fire. Each particle reacts at most once
per tick; the rarer side searches, and proposals commit serially in particle
order, so parallel runs match serial ones.

Heat: every particle carries a temperature. Touching matter exchanges
`MaterialFluidWorld.heat_conduction` of the difference per second; flammable
matter instead takes 0.85 of a hotter neighbor's heat at once unless a wall
separates them, so flames race through fuel. Bodies with `GameIgniter.heat`
ignite flammable matter they sweep past and heat or chill the rest; negative
heat chills. Hot matter glows: from half of `glow_temperature` to all of it,
its color shifts toward `glow_color` and it adds `glow_emission`; burning gas
also draws hot cores. `blast_radius`, `blast_speed` and `blast_damage` on the
fluid world shape ignition blasts.

Neon Swarm's fixtures in `projects/arena2d/content/prototypes`: Fog, FuelGas,
Smoke, Fire, Exhaust, Dust, Steam, Spores, Water, Oil, Lava, Acid, Slime,
MoltenMetal, Sand and Ash, with the reactions ReactionQuench (water and lava
to steam and Basalt), ReactionMetalQuench (water and molten metal to steam and
Slag) and ReactionDouse (water and anything burning to steam and smoke).
Lava cools into Basalt and water freezes into Ice. Fuel, oil and spores ignite
from heat. `GameFluidVolume` fills an ellipse with a substance, and its
nonzero look fields tint that fill; `GameParticleEmitter.fluid_substance`
emits the emitter's particles as a substance with the emitter's look and
`GameIgniter` heat; `MaterialBreakProfile.smoke_substance` names crater gas.

Approximations: temperature is a gameplay quantity, not kelvin; conduction
uses neighbor averages, not a conserved heat equation; reactions are
probabilistic contact rules without stoichiometry; fuel carries its own
oxidizer; blasts are bounded radial impulses.

## Matter and terrain

Corrosive and hot matter touching static solids reports merged touches per
solid and two-cell patch. Corrosion deals `corrosion × (1 −
GameSubstance.corrosion_resistance)` damage per second there (cause
`Corrosion`), spending the particle's potency by the same unresisted share,
a quarter per second; a spent particle leaves. A resistance of one, like
metal, is immune and spends nothing. Matter at least as hot as a solid's
`GameSubstance.melts_at` melts the touched cells at once (cause `Melt`) and
loses half that temperature; the removed area returns as particles of
`melts_to` at the crater. `Heat Area` melts solids the same way. Neither cause
throws crater debris.

Deposition: a product that names a deposit removes the particle and adds a
disk of its area to the streamed `MaterialTerrain` whose `deposit` matches, at
the next preparation boundary. Cells fill with no damage, the signed distance
takes the union, and collision and contours rebuild. Cold and new tiles load
to receive deposits; streaming stores and restores them like damage. A
terrain that receives deposits must stream; `Empty` is a field with no
material, for terrains that only hold deposits. Neon Swarm's
`BasaltDeposits`, `SlagDeposits` and `IceDeposits` receive Basalt, Slag and
Ice; ice melts back into water at 0.4.

`scenes/MatterLab.yaml` is a noncombat gravity scene with a station for each
rule: lava falling into water, burning oil, acid in a metal basin eating the
floor, a fuel and spore cloud beside a heater, sand, slime, molten metal cast
into water, and water frozen by a chiller.

Approximations: touches merge per two-cell patch and cap at 512 per tick;
melting removes whole cells; deposits add disks, not the particle's shape;
a mixed damage batch reports its first request's cause.

## Matter nodes

| Node | Behavior |
| --- | --- |
| `Emit Fluid` (Substance, Position, Velocity, Count, Radius, Temperature) | Adds Count parcels of Substance spread evenly within Radius of Position, moving at Velocity, with Temperature added to the substance's own. Fails without a fluid world or for an unknown substance; stops quietly at the population limit. |
| `Heat Area` (Point, Radius, Delta) | Adds Delta to the temperature of matter within Radius, awake or asleep; a positive Delta at least a static solid's `melts_at` melts its cells there. Incendiary and cryo weapons use it. |
| `Fluid At` (Point, Radius) → Substance, Density, Temperature, Conductive | The substance with the most mass among awake matter within Radius (empty when none), mass per area relative to rigid bodies (a full pool reads its substance's density), mean temperature, and whether any of it conducts. |
| `Material At` (Point) → Entity, Solid, Hardness | The entity whose solid cells cover Point, including fragments, and its `GameSubstance.hardness` (one without one). |

Fluid queries read the positions from the end of the last simulation step.

## Procedural fields

Field documents live in `content/fields` with domain `field`. They have one
`Field Output`, scalar coordinate/noise/primitive/composition nodes, and named
float, int or uint parameters. The current field value is a float; integer
parameters convert to that scalar representation. Negative values are solid.
Translation, scale and domain warp compose coordinate arithmetic with noise;
rotation has dedicated coordinate nodes. Connectivity remains a terrain pass.

Pure scalar functions use the existing library format. `FieldShapes.Islands`
composes a circle, box, capsule and smooth unions, and `MaterialIslands` calls it.
Function calls inline into scalar IR. Recursive calls, cycles, mutable bindings,
execution pins, unsupported nodes, invalid parameters and nonfinite output fail
validation. Compilation is bounded to 4096 instructions; grids are bounded to
4096 cells per axis. No behavior instance or general behavior VM runs per cell.
The bulk evaluator retains constants and X-only instructions across rows.

`CaveField` replaces the native cave generator. The old implementation is retained
only in the test target as a parity oracle. A changed field, dependent library,
terrain transform or generation parameter causes a rebake at the next pre-physics
material boundary. The new grid is validated before old generated entities are
removed. Rebaking resets runtime damage and removes that source's fragments.
Invalid library or field edits retain the previous compiled program.

`Damage Material` accepts entity, world point, radius, energy and cause. It can
target an authored terrain source or one of its chunks; the adapter resolves the
whole intersecting disk after terrain preparation. This includes requests queued
by Begin before chunks exist. `Release Material`, fragment and pierceability
queries remain native graph capabilities.

Destruction publishes `Material Result` with energy, cause, world point, result
kind, removed/dust cell counts and the original target. Generated terrain results
route to their authored source. Combustion emits `Material Impact` for unmasked
bodies with energy, cause and point. `MaterialImpactHealth` owns the Arena health
response, rounds impact energy explicitly, and destroys depleted entities.
Native blast handling does not modify health.

## Optional terrain streaming

`MaterialTerrain.stream_radius` defaults to zero, preserving resident terrain
generation for existing assets. A positive radius enables streaming around
enabled cameras, including their view extents. Moving bodies retain a small
nearby area. Tiles a camera can see, and those under a moving body, prepare
immediately so visible terrain never appears late. `stream_budget` limits the
remaining prefetch to 1..64 tiles per material preparation boundary. It limits
tile count, not elapsed milliseconds.

Streamed terrain requires `connectivity = 0` and `border_cells = 0`. Positive
width and height define finite bounds of up to 65536 cells per axis. Setting both
dimensions to zero uses signed tile coordinates without an authored rectangular
edge. Fields receive consistent coordinates across tiles. The field and its
parameters still determine whether the resulting spaces connect.

`MaterialTerrain.island_cells` removes solid islands and fills air pockets
smaller than that many cells that lie wholly inside a tile or bounded grid.
Such components are enclosed by the tile, so streaming never removes rock
connected across a tile edge.

Evicted tiles retain occupancy, distance samples, and partial damage in a
temporary file for the current run. Returning restores the same mask, including
fully removed tiles. Resetting the material runtime deletes that stored state.
This is terrain persistence; it does not stream actors, inventory, or quests.
Changing generation settings recreates the stream and discards its old damage.

Cold storage and its in-memory index grow with visited terrain. Resident work
depends on the number and extent of active views. Generation and file I/O remain
synchronous. This capability does not establish a frame-time guarantee or a
complete infinite-world system. Runtime snapshots expose resident/stored tile
counts, load/eviction counts, and terrain storage bytes for measurement.

## Standalone Systems

A Scene binds a System document using `EngineSystem.asset`, `phase` and `order`.
The phases are 0 `PrePhysics`, 1 `PostPhysics`, 2 `PostEvents`, and 3
`Presentation`. `FreeMotion` runs in both shipped Sandbox and MaterialStudy,
selecting visible Transform/Velocity entities without Body or Collider. The
marker moves 12 units per second independently of physics and combat behaviors.

Native and graph systems register through `engine_register_system` with
`engine_system_desc`. Descriptors copy their names, query, component/resource
access and explicit `after` dependencies; graph storage is borrowed. Re-register
native graph descriptors after editing their graph. Scene System documents do
this automatically at the next safe frame boundary, including query edits.

A stable topological order respects phases, authored order, explicit dependencies
and component/resource conflicts. Cycles and missing dependencies fail with a
diagnostic. Unannotated native callbacks emit a warning and run serially. Native
callbacks own their query execution; a callback declared parallel-safe must use
stable prebound data and must not open queries or change ECS membership from a
worker. Graph systems snapshot queries and bind component columns on the main
thread before workers run. Graph effects come from compiler metadata, not author
claims. Structural effects, host sinks, unknown effects, world reads, resource
writers, material preparation and physics remain barriers.

Independent compiled systems share the existing task pool. The portable runtime
uses the same ordering with serial workers. Generic bytecode is a valid native
System backend when typed bytecode or SLJIT cannot handle the graph; backend
inspection reports the actual selection. `FreeMotion` currently uses generic
bytecode for its vector expression.

## Authoring and checks

Use `buildGraph`, `inspectGraph`, `inspectSystem`, `validateAsset`
and `previewField` through Chibi MCP. Field previews return a deterministic CPU
PNG, range and hash without a graphics context. Graph edits remain atomic,
hash-checked and automatically arranged. The editor lists fields, filters their
palette, exposes pure scalar library calls, and provides seed/cell-size controls
and a field preview in Details.

Run the noncombat scene with `make play SCENE=scenes/MaterialStudy.yaml`.

Useful checks:

```sh
CCACHE_DISABLE=1 make build
CCACHE_DISABLE=1 make test
build/test_suite --materials
build/test_suite --material-engine
build-release/test_suite --bench-fields
build-release/test_suite --bench-system-schedule
node tools/mcp/material-smoke.mjs .
```

See [validation results](material-engine-validation.md) for measured parity,
runtime evidence and the performance tradeoff of the reusable evaluator.
