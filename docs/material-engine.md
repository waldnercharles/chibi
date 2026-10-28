# Material engine and standalone Systems

Material algorithms operate on native grids and masks. Authored fields define
geometry; reflected components define tuning; behaviors decide gameplay outcomes.
`test/fixtures/environment-authoring/content/scenes/MaterialStudy.kdl` exercises
this boundary with two recovered procedural specimens and an authored cut.
`SeamStudy.kdl` in the same fixture uses two small slabs and continuous probes
for collision acceptance at a chunk boundary. These focused scenes omit game
movement, fluid worlds, health, projectiles, encounters and combat configuration.

## Runtime boundaries

| Module | Owns |
| --- | --- |
| `material_field` | Named compiled fields, typed overrides, atomic replacement and revisions |
| `field_program` / `field_ir` | Pure scalar compilation and bounded native bulk evaluation |
| `material_terrain` | Connectivity cleanup and 128-cell runtime chunks, including partial edges |
| `material_stream` | Tile selection, generation budget, eviction and restoration of cold masks |
| `material_checkpoint` | Optional portable terrain snapshot encoding and recipe identity |
| `material_mask` | Occupancy, accumulated damage, distance, connected components and immutable geometry views |
| `material_destruction` | Queued energy, resistance, splitting, retained chips, release and typed results |
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
material shader keep a `MaterialFrame`: where their source's frame sat when
the material was laid out. World-space material patterns stay attached to the
piece as it moves.

`MaterialDestructionWorld` bounds retained fragments. `MaterialFluidWorld`
configures particles, activation regions and fluid chunk size. Liquids fall by
the scene's `Gravity`, the gravity that pulls 2D bodies, and hot gas rises
against it; without one, neither does. Either capability
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
takes the striking entity as `Source`; its `Material` emission color and
`Igniter` heat describe the impact. `MaterialTerrain` names a field,
typed parameter object, seed, bounds, cell size, connectivity and collision filters.

## Solver-independent ownership and validation

Fog and liquid improvements must remain reusable across MPM, FLIP and other
sparse GPU solver choices. These are infrastructure constraints, not claims that
those solvers or a GPU compute backend are implemented.

- Activate simulation-space cells and their required stencil halos separately
  from rendering coverage and terrain residency.
- Keep cell/particle identity and spatial order explicit, with flat channels for
  neighbor queries and transfers. Retain CPU worker scratch under explicit
  ownership; GPU buffers need separate device ownership and synchronization.
- Declare phase inputs, outputs and joins, including write and reduction
  ownership. Keep solver-specific equations and optical rendering separate.
- Measure indexing, clearing, packing, reductions and kernel work. A GPU path
  also needs dispatch, transfer, synchronization and complete-frame costs; a
  faster isolated kernel does not establish a faster solver.
- Extract shared interfaces only when a concrete second use establishes their
  contract. A CPU cell hash or screen-space rendering bins are not themselves
  a GPU sparse-grid allocator.

Current changes must preserve native/scalar/worker semantics and replay parity.
A future GPU solver needs documented numeric tolerances, mass/momentum checks,
divergence or constitutive checks, finite-value and boundary tests, and fixed-step
trajectories. CPU hash equality alone does not validate different GPU reduction
orders. FLIP delta transfer and MPM affine/deformation/stress state remain
separate solver work rather than implied shared infrastructure.

## Procedural fields

Field documents live in `content/fields` with domain `field`. They have one
`Field Output`, scalar coordinate/noise/primitive/composition nodes, and named
float, int or uint parameters. The current field value is a float; integer
parameters convert to that scalar representation. Negative values are solid.
Translation, scale and domain warp compose coordinate arithmetic with noise;
rotation has dedicated coordinate nodes. Connectivity remains a terrain pass.

Fields call pure scalar functions of `.chibi` libraries. `FieldShapes.Islands`
composes a circle, box, capsule and smooth unions, and `MaterialIslands` calls it
by its library and function name. A callable function's body is one `return`
built from its parameters, constants, field calls and calls to other such
functions. Every field node but `Field Output` has a call named after it, such
as `field_circle`, `field_max`, `field_noise` or `field_cell_edge`, and each
call becomes the node it names. A function calls functions declared before or
after it in its library, and functions of other libraries by qualified name,
such as `"Shapes".Disc(X, Y)`; a field that links the caller links
those libraries too, and arguments a call omits take the callee's defaults.
Arithmetic operators and local bindings do not lower yet, so write `field_add`
rather than `+` and name shared subexpressions as functions; repeated calls
merge in the compiled program.

Function calls inline into scalar IR, so in a field each call evaluates as its
node does there: noise, fractal, hash and cell calls add the field's seed to
their `Seed Offset`, and `field_x`, `field_y`, `field_z` and `field_parameter`
read the field's sample and parameters. Outside a field program, as when a
behavior calls the function, the same calls evaluate with seed 0, so `Seed
Offset` is the whole seed, and the sample reads fail. Sampling and these calls
share one scalar evaluator; row and point bakes have their own, and a
`--field-runtime` case checks every operation agrees across them. Both fail on
any nonfinite value. Recursive functions are left out of the lowered library,
and so is a function that calls one left out; libraries cannot import each
other in a cycle. Mutable bindings, execution pins, unsupported nodes,
invalid parameters and nonfinite output fail validation.
Compilation is bounded to 4096 instructions; grids are bounded to
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

## Chunk surfaces and collision

Terrain chunks share neighboring occupancy and signed distance samples for
contour reconstruction, material borders and reduced visual masks. The exterior
sampling ring is derived from the currently resident chunks of one terrain
source; it does not become part of saved edits. Known empty samples and
unavailable neighbors remain distinct. Damage and neighborhood changes invalidate
the derived surface cache. Each successful damage disk also updates neighboring
resident distance samples, even where no local cell was removed; subthreshold
hits do not change those distances. Finite generation retains empty chunks so an empty
region remains available to neighboring surface reconstruction and checkpoints.

A terrain source supplies a common visual LOD limit, including its partial edge
chunks. The renderer clamps mask reduction to that limit and carries neighbor
samples through reduction, keeping adjacent chunks on compatible sampling support.
Collision continues to use the full-resolution surviving cell rectangles; visual
LOD does not coarsen the physics mask. These are implementation contracts, not a
claim of measured pixel parity or GPU performance.

## Optional terrain streaming

`MaterialTerrain.stream_radius` defaults to zero, preserving resident terrain
generation for existing assets. A positive radius enables streaming around
enabled cameras, including their view extents. Moving bodies retain a small
nearby area. Tiles a camera can see, and those under a moving body, prepare
immediately so visible terrain never appears late. The remaining prefetch bakes
a tile a slice at a time: `stream_budget` allows 1..64 slices of 16 tile rows
(a tile is 128) per material preparation boundary, so no step bakes a whole
prefetched tile at once. It limits rows, not elapsed milliseconds, so streaming
stays deterministic. Restoring a stored tile costs one slice.

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
This is retention within one run, not durable storage across process restarts;
it does not stream actors, inventory, or quests. Without an imported checkpoint,
changing generation settings recreates the stream and discards its old damage.

Cold storage and its in-memory index grow with visited terrain. Resident work
depends on the number and extent of active views. Generation and file I/O remain
synchronous. This capability does not establish a frame-time guarantee or a
complete infinite-world system. Runtime snapshots expose resident/stored tile
counts, load/eviction counts, and terrain storage bytes for measurement.

## Optional durable terrain checkpoints

The public `materials_terrain_checkpoint_write` and
`materials_terrain_checkpoint_read` functions snapshot one prepared authored
terrain source. Call them outside ECS queries, after `materials_prepare` has
applied queued edits. The caller supplies a nonempty stable identity, unique per
terrain within its own save namespace; an ECS entity handle is not a durable
identity. Write returns malloc-owned bytes and their length, which the caller
releases with `free`. The host owns durable storage, commit timing and storage
failure reporting; the material runtime opens no save path and chooses no slot
or automatic-save policy. The existing small text save store does not
automatically capture these binary terrain snapshots. The optional world storage
service exposes `save_terrain`, `load_terrain` and `terrain_storage_result` to
`.chibi`: requests enqueue work, and positive request tokens must be polled for
completion. It permits one pending request per world and uses native binary
attachments in the host's save namespace. Browser attachments are unavailable.
See [Terrain storage](terrain-storage.md) for player `--save`, controller
`save_path`, completion/failure behavior and the Terrain Lab example.

The versioned portable format stores canonical tile addresses, grids, occupancy,
signed distances and accumulated partial damage. Finite snapshots cover every
chunk, including empty chunks. Streamed snapshots combine cold and resident
state, with current resident masks taking precedence over older cold copies.
Unvisited streamed tiles remain generated from the matching recipe. Snapshots
exclude derived contours, collision shapes, neighbor rings, ECS handles, detached
fragments, fluids and actors. Limits are 256 tiles and 64 MiB per source snapshot,
with caller identities limited to 1024 bytes. This bounded checkpoint facility is
not an unbounded-world database.

Recipe identity includes the portable resolved field program and its inlined
dependencies, effective typed parameters, seed, grid and cell size, connectivity,
border/island cleanup, stream mode and chunk layout. In-process field revisions
and unrelated fields do not determine durable identity. Read rejects malformed,
oversized, mismatched-identity or mismatched-recipe data before replacing live
terrain; fallible stream staging also precedes publication. Allocation follows
the runtime's existing fail-fast contract. Finite masks rebuild derived geometry;
streamed imports stage their records cold. After successful read, call
`materials_prepare` again before physics or rendering so current views and
neighbor surfaces synchronize, then let the normal physics boundary synchronize
its shapes.

Imported terrain is deliberately conservative about later edits to generation,
placement or other terrain configuration. Such a change is rejected until the
caller explicitly calls `materials_reset`; it must not silently discard imported
edits. Reset clears terrain for the material service, so authored application
policy decides when to reset, regenerate and optionally import a compatible
checkpoint again. Host storage survives runtime reset only because the host
retains it independently.

## Standalone Systems

A Scene binds a System document using `EngineSystem.asset`, `phase` and `order`.
The phases are 0 `PrePhysics`, 1 `PostPhysics`, 2 `PostEvents`, and 3
`Presentation`. The historical Sandbox and MaterialStudy examples use
`FreeMotion` to select visible Transform/Velocity entities without Body or
Collider. Their marker moves 12 units per second independently of physics and
combat behaviors. The focused environment-authoring fixture omits that marker
and system.

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

Edit field KDL and system/library `.chibi` files directly, or use Studio. Use
native `inspect_graph`, `inspect_system` and `validate_asset` requests for saved
content; request examples are in the [asset workflow](runtime-authoring.md).
Native `preview_field` produces a deterministic CPU PNG, range and hash without
a graphics context. For example:

```sh
printf '%s\n' '{"contract":1,"operation":"preview_field","kind":"field","name":"MaterialIslands","width":256,"height":256}' | \
  build/bin/chibi_ctl --project-root test/fixtures/environment-authoring
```

The editor lists fields, filters their palette, exposes pure scalar library
calls, and provides seed/cell-size controls and a field preview in Details.
The fixture start scene is `scenes/MaterialStudy.kdl`; `scenes/SeamStudy.kdl`
is its separate collision specimen.

Build with `make build`. Select affected-contract checks using `make agent-list` and
`make agent-test AGENT_SELECTOR=--<selector>`; see the
[testing instructions](testing.md).
