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
| `material_fluid` | Fluid solving and activation |
| `material_flow` | Persistent gas velocity field; see [Gas flow](#gas-flow) |
| `material_debris` | Crater rock chunks: bounce, shove, split and shrink |
| `material_ecs` / `material_ecs_fluid` / `material_ecs_terrain` / `material_ecs_debris` | Entity ownership, configuration, terrain views, fragments, particles, debris and event delivery |
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

`MaterialDestructionWorld` bounds retained fragments (`fragment_limit`) and
crater rock chunks (`debris_limit`). `MaterialFluidWorld` configures particles,
activation regions, fluid chunk size and [gas flow](#gas-flow). Either
capability works independently.
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

Neon Swarm's `Cave` streams `BasaltCave`; `CaveField` reproduces the original
native cave generator, which survives only in the test target
(`test/runtime/reference_cave.c`) as a parity oracle. A changed field, dependent library,
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

## Gas flow

`material_flow` gives gas a persistent sparse 2D velocity grid, independent of
combat, rendering and Box2D; `material_fluid` supplies particles, active indices
and collider data. A moving body redirects nearby gas before contact, and its
wake keeps moving afterward. This is a CPU grid-and-tracer solver, not MPM.
Liquids keep the particle density-relaxation solver. `MaterialFluidWorld`
enables the field:

| Field | Meaning |
| --- | --- |
| `gas_cell_size` | World units per cell; zero disables the field. |
| `gas_vorticity` | Strength of curl reinforcement. |
| `gas_drag` | Exponential velocity damping per second. |

Each step builds cells around active gas particles with a two-cell halo. Face
velocities live on a staggered grid. Particle velocities seed the field and feed
momentum into it; the previous field is advected with semi-Lagrangian sampling.
Collider velocities, including angular motion, constrain solid faces. Curl
reinforcement precedes 32 red/black Gauss-Seidel pressure iterations, then
particles sample the result. The ordinary particle collision step still
resolves geometry and body impulses. With the field enabled, gas skips gas-gas
density relaxation; liquid cohesion and gas-liquid contacts remain. Engine
parcels inject momentum but keep their authored ballistic velocity, drag and
visual lifetime curves.

`MaterialFluidWorld` also sets transient contact particles per fluid kind
(`gas_wake_*`, `liquid_splash_*`): minimum normal closing speed, strength,
particles per contact patch and lifetime. Zero count disables a response.
Contacts aggregate by body, body-local patch and fluid kind, with at most 128
events and 256 secondary particles per tick. Relative surface speed and impulse
set the strength; the participating fluid supplies color and emission. Fast
colliders use swept checks. These render particles do not join the simulated
population.

## Approximations

- The gas field covers active gas neighborhoods with open outer boundaries.
  Cells that leave the active region are discarded and rebuilt from particles
  on return. The fixed pressure iteration count reduces divergence without
  guaranteeing incompressibility or exact conservation.
- Collider masks expand by half a cell to cover thin geometry; particle
  collisions still use the real geometry. Grid pressure applies no force back
  to Box2D; particle collision impulses provide the return force.
- There is no unconditional buoyancy in zero gravity. Bodies, exhaust,
  combustion and particle motion supply disturbances.
- Liquid cohesion is double-density relaxation, not MPM, with no elastic
  stress. Fluid-body exchange applies linear impulses after the rigid solve,
  with no torque feedback.
- Sleeping fluid particles keep velocity and lifetime with no catch-up when
  they wake. Far rigid bodies and behavior schedules are not streamed.
- Detached solids are rigid Box2D bodies. Terrain components touching a chunk
  edge stay anchored, so rock connected across chunks cannot detach as one body.
- When the fragment budget is full, surviving detached components stay in the
  source mask and retry detachment when a slot frees. Larger components get
  slots before cut chips; chips can become dust but are never restored.
- Fuel supplies its own oxidizer; blasts use bounded radial impulses and
  first-hit damage spokes. This is not a chemistry or deformable-solid solver.

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
build/test_suite --materials
build/test_suite --material-engine
build-release/test_suite --bench-fields
build-release/test_suite --bench-system-schedule
node tools/mcp/material-smoke.mjs .
```

`--materials` covers gas flow (collider-driven motion, persistence, sleep/wake,
serial/parallel parity including field state), liquid cohesion, destruction and
combustion. The [validation record](archive/material-engine-validation.md)
measures cave parity with the original generator and the reusable field
evaluator's cost: about 4.4 times the specialized generator, paid only when a
field bakes.
