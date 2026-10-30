# Environment authoring roadmap

Approved direction, 2026-10-15. Implementation starts in the isolated
`codex/environment-authoring-20261015` engine worktree from `22966e126`.
This document records the environment program. The existing Studio migration
register remains authoritative for its own gates and owners.

## Goal and current state

Build an environment asset pipeline with terrain editing, complete 2D tilemap
authoring, procedural layout and tile/material generation, and professional
material/shader authoring. Ship bounded stages with usable authored examples.
Start with terrain and the shared generation services. Layout generation and
tile/material generation develop in parallel after their shared contracts exist.
The first game workflow is 2D sidescrolling destructive terrain using
`MaterialTerrain`. Heightmaps initially describe procedural texture outputs.
A 3D heightfield editor remains a separate deferred representation decision.

The current engine already has declarative materials, fields and shader libraries,
native graph preparation, source authoring services, and runtime material terrain.
Existing material terrain includes simulation and chunk behavior described in
[material-engine.md](material-engine.md). Those capabilities do not establish
heightfield editing, a tilemap editor, a professional material workspace, or the
new image-baking acceptance criteria below.

At the start of this initiative, no environment milestone below is certified
complete. Recover the MaterialStudy/Sandbox specimen as the baseline before
claiming preserved behavior. The first implementation slice is generic float
material-channel baking, a bounded controller operation, and a minimal migrated
terrain fixture. Record its candidate results
separately from merge, package, Studio, browser and deployed evidence.

## E0 implementation checkpoint

The CPU portion of E0 is implemented on this branch. It is an uncommitted
candidate, not a merged or deployed editor feature.

- `Chibi::shader_runtime` exports `runtime/shaders/bake.h`. Jobs snapshot the
  validated graph and numeric parameters, sample a configurable plane in bounded
  row steps, and publish packed float32 Surface channels only after completion.
  Failed or cancelled work cannot replace the previous result. Image pixels
  remain borrowed and immutable for the job lifetime. Nested derivative sampling
  uses job-owned scratch storage without per-sample allocation.
- [`bake_material`](material-baking.md) exposes the same evaluator through the
  controller. It resolves linked shader libraries and root-owned parameters,
  including local function parameters, and returns channel data, typed semantics,
  resolved inputs and dependency fingerprints. It bounds dimensions, evaluation
  work, distinct image keys and decoded image storage.
- `test/fixtures/environment-authoring` recovers the two-sample MaterialStudy
  scene with current KDL and `.chibi` contracts. Both source masks start with
  8,420 occupied cells. After one authored Tick and the next material boundary,
  the cut sample has 4,640 source cells, an empty cut center and retained
  fragments; the comparison sample is unchanged. This proves CPU behavior.
  It does not establish rendering, collision agreement or persistent edits.

Validation on the candidate:

| Scope | Result |
| --- | --- |
| `make agent-test AGENT_SELECTOR=--shader-bake` | Passed eight CPU cases: float/HDR values, coordinates, ownership, parameters, limits, publication failure, images and nested derivatives. |
| `make agent-test AGENT_SELECTOR=--shader-graphs` | Passed the existing CPU evaluator/GLSL generation selector after the derivative workspace change. |
| `make agent-test AGENT_SELECTOR=--material-bake` | Passed the controller integration case, including local-function overrides, changed-image provenance, invalid requests, missing images and image-key admission; final selected execution 2.794s. |
| `make agent-test AGENT_SELECTOR=--environment-authoring` | Passed two cases and 31 assertions: prepared CPU runtime behavior and project validation; final selected execution 0.492s. |
| Installed SDK | SDK packaging and a standalone controller build passed. The same material-bake selector passed against that controller in 1.406s. The bake consumer object also compiled using only exported SDK headers. |
| Native build and formatting | `chibi build` and the controller/test-tools targets passed; changed C/header files pass clang-format 22 and `git diff --check`. |

Tests ran under the production supervisor. Native source tests used
ASan/UBSan; the packaged SDK used its separate Debug configuration. Build time
is excluded from the test timings. Independent reviews covered API ownership,
derivative nesting, parameter identities, provenance and resource bounds; the
reported findings were fixed before the final relevant checks.

The attempted full standalone installed `test_tools` build hit existing warning
errors in unrelated graph/render test sources. That aggregate did not pass;
installed-SDK evidence is limited to the successful consumer object and controller
checks above. Full native/render suites were not run. Those E0 checks did not establish Studio, GPU, browser/device or deployment
acceptance. The separate E1 renderer evidence follows below. Local logs, package identity
and remaining build artifacts are recorded in `.chibi/environment-authoring`.

Full MaterialStudy rendered acceptance and the CP0/CP1 completion gates remain
open. The next section records the narrower E1 terrain lifecycle checks. Image inputs currently remain RGBA8; high-precision image codecs,
normal-texture encoding, baked-asset publication and the professional material
workspace are follow-on work.

## E1 terrain implementation checkpoint

The engine candidate now supplies shared chunk surfaces and explicit durable
terrain checkpoints. It remains in `Chibi::material_runtime`; no game-specific
terrain implementation or separate language repository was introduced.

- Finite chunks retain empty cells and their signed distances. Owned neighbor
  samples let marching contours and material borders cross chunk edges. Masks
  share a source-level LOD limit, including partial edge chunks, and reduction
  carries their neighbor samples. Physics retains full-resolution occupied-cell
  collision rectangles.
- Successful damage disks update signed distances in adjacent resident chunks,
  including empty streamed chunks without entities. This fixes a cut that looked
  different when the same field was split into chunks. Hits below the removal
  threshold leave surface distances unchanged. Moving fragments drop terrain
  neighbor samples.
- `materials_terrain_checkpoint_write/read` export and restore one source as
  portable versioned bytes. They preserve occupied and removed cells, distance
  samples, partial damage and both resident and cold streamed tiles. Identity and
  a resolved field/parameter/seed recipe fingerprint reject incompatible data;
  corrupt or malformed input is rejected before publication. Tests restore from
  a real file in a fresh process with a different field registration order.
- The host owns file paths, atomic storage, save slots and save timing. The API is
  synchronous at a quiescent preparation boundary, limited to 256 tiles and
  64 MiB per checkpoint. Call `materials_prepare` after import before consumers.
  Imported terrain refuses configuration changes until explicit `materials_reset`,
  preventing implicit regeneration from discarding restored edits. See
  [material-engine.md](material-engine.md#optional-durable-terrain-checkpoints) for the API
  contract and exclusions.

Current candidate validation:

| Scope | Result |
| --- | --- |
| `--terrain-seams` | Three CPU cases compare split 128+2 chunks with a continuous field before and after a seam cut, inspect exact cell collision coverage, coarse contours, finite corners and known empty neighbors. Passed in 2.097s. |
| `--terrain-checkpoint` | Six CPU cases passed in 2.441s: finite and streamed snapshots, cold/resident precedence, partial damage, empty cells, fresh-process restore, recipe identity and malformed data refusal. Public runtime cases check that successful cuts update adjacent air samples and persist them, including a wholly empty streamed tile with no entity; subthreshold damage leaves distances unchanged. |
| `--environment-authoring` | Three cases passed in 2.205s. The new authored SeamStudy checks real swept collision on both sides of a boundary, a queued cross-boundary cut, passage afterward and an unaffected comparison. Fixture validation reports 12 assets with no errors or warnings. |
| Existing affected CPU checks | `--fields`, `--material-render` and `--material-destruction` passed after the final runtime fix. |
| `--material-terrain-seams` | Six 64x64 Metal readbacks passed in 0.998s: fine borders and coarse LOD, before/cut/restored. Both cuts visibly change their baseline; restored edited images match the cut images exactly. |
| Build and SDK | Native products, controller/test tools and SDK packaging passed. The existing `chibi_native_consumer` object target, including the new seam/checkpoint cases, compiled against the installed SDK with no engine source-header include path. This is exported-header compilation evidence; the installed aggregate test driver was not run for E1. |
| Execution policy and formatting | Six production execution-policy cases passed (61 assertions); changed C/header files pass clang-format 22 and the diff passes whitespace checks. |

All tests use the production supervisor. The GPU case records the
actual `CF_BACKEND_TYPE_METAL` backend and pixel hashes in
`.chibi/environment-authoring/terrain-seams/evidence.txt`, beside its PNGs.
The agent capability probe checks Vulkan and reports its loader unavailable on
this Mac; that result does not describe the Metal renderer used by this test.
The readbacks establish this bounded native rendering case, not browser/device
coverage or a frame-time guarantee. Astra independently reviewed persistence,
ownership, surface invalidation and the damage correction; findings were fixed.

E1 native terrain mechanisms and shared runtime/controller save commands have
passed the bounded checks below. E1 game-facing playable and visual acceptance
remains open following the 2026-10-16 reference review. Studio integration,
authored override layers, editor save/reopen UI and the professional material
workspace remain later milestones. Snapshots do not include fragments, fluids,
actors or whole-game state. Derived neighbor storage adds about 100 KiB to a full
128x128 chunk at the default 32-cell radius; wide borders can raise that radius
to 128. Stream storage and terrain-wide work still need representative performance
measurements before larger-world claims.

### SDK test example and shared save/load completion

[`test/fixtures/terrain-lab`](../test/fixtures/terrain-lab/README.md) is a
standalone native example, first written on the retired chibi-examples branch
`codex/terrain-playground-20261015` and moved into the engine on 2026-10-17. Its
`.chibi` sources own
movement, jumping, facing-based excavation, camera following, HUD messages,
terrain identity, explicit S/L controls and respawn policy. Its field library
owns the ground and cave shape. It uses Chibi's Y-down gameplay coordinates.
Startup does not edit or load terrain. The README contains installed SDK commands
and a repeatable interactive restart procedure.

The optional material-world bridge exposes `save_terrain`, `load_terrain` and
`terrain_storage_result` through the native catalog. It queues one request per
world, copies the identity, commits at the engine's end-of-step boundary, and
reports a correlated completion token. The engine supplies no game save timing
or directory policy. Generic binary attachments in `save_runtime` provide bounded,
atomic native-file replacement under the host's existing `--save` namespace.
The terrain codec remains independent of file IO. The controller operations use
this same bridge and report completion, terrain snapshot hashes and collision
readiness. See [terrain-storage.md](terrain-storage.md) for the complete contract.

| Scope | Result |
| --- | --- |
| `--terrain-storage` | Four CPU cases passed in 1.410s: query-safe requests, copied identity, busy/expired tokens, reset cancellation, material-only engine drain, exact restore and storage/validation failure preservation. |
| `--terrain-storage-commands` | Three process integration cases passed in 5.828s: explicit namespace/discovery contracts, fresh-process cut restoration, missing/corrupt/incompatible saves, failed writes and preservation of existing text saves. |
| `--terrain-playable` | Two cases passed in 4.449s, each with independent writer and reader processes. Actual `.chibi` movement, jump, dig, S/L and completion HUD run on VM and SLJIT, with backend identity asserted. Both sides of the seam lose cells; a fresh process first proves pristine generation, then restores the saved hashes and collision holes. The reference patch remains unchanged. |
| `--terrain-playable-render` | Installed SDK compiler prepared the example. Installed player passed four hidden 640x360 frames in 1.125s on `CF_BACKEND_TYPE_METAL`; PNG inspection confirms terrain below the explorer, readable HUD and seam/reference markers. This is initial presentation evidence, not interactive GPU input or performance measurement. |
| Installed public headers | `chibi_native_consumer`, including the new storage cases, compiled against the installed SDK without engine source-header includes. |
| Formatting and policy | Authored formatting and validation pass: 16 assets, zero errors/warnings. The six execution-policy cases pass with 61 assertions. |

Each test uses the production supervisor. Native CPU tests use
ASan/UBSan; installed compiler/player checks use the packaged Debug SDK. Build
time is separate. The first render check caught inverted example coordinates
and an inline HUD; both were corrected and the affected checks passed afterward.
Astra reviewed request lifetimes, scheduling, atomic writes, namespace isolation,
controller contracts and SDK dependencies. Full native/render suites were not run. Browser binary persistence, Studio UI and deployment are unverified.
Both repositories remain uncommitted candidates; no merge, push or deployment is
implied. Logs, the prepared example, PNG and source/package identities are in
`.chibi/environment-authoring` in the engine worktree.

### Ship and visual acceptance correction, 2026-10-16

The user clarified that the player is a ship and the playable result must follow
our generated screenshot. Terrain Lab's walking explorer, bright flat quarry and
simple shapes do not satisfy that direction. Its CPU and Metal checks remain
valid mechanism tests; they do not establish the intended game appearance.
The earlier unqualified E1-complete claim is withdrawn.

The game-facing reference is Voidweaver's preserved round-05 basalt study and
Cradle design, under `chibi-voidweaver/references/art-direction/round-05`.
That task owns the game scene and is already correcting its fidelity. Keep game
assets, free-flight controls, camera, terrain composition and visual tuning in
Voidweaver. Terrain Lab remains the independent SDK fixture. The user confirmed
this ownership split; reusable renderer and material gaps belong in the engine.

The consumer acceptance needs to demonstrate:

- A recognisable open C-shaped Cradle ship in free flight, with its working gap,
  hull proportions, readable orientation and appropriate scale in the chamber.
- A natural basalt passage with the reference's broad tube, diagonal flow shelf,
  pit and collapse chimney; irregular silhouettes and visibly recessed rear
  surfaces must establish depth.
- Correlated rock material channels and local lighting that preserve the dark
  cave, restrained warm lamp and focused ship worklamp. Ambient-on/off changes
  must retain camera, exposure and local source settings.
- Matched reference/runtime captures before and after a breach, plus actual ship
  passage and save/relaunch restoration through the shared terrain service.

Judge these against the saved reference and a representative runtime capture,
not a nonblank-image assertion or successful compilation. The retained first
Voidweaver runtime capture is a blockout and does not yet pass this gate.
The shared renderer work below supports that consumer gate. Terrain storage and
contour checks do not establish these lighting or depth capabilities.

### Shared shader transport checkpoint

Approved scope: shader-driven `MeshSurface2D` must preserve the entity's existing
`Material` transport coefficients, multiplying both absorption RGB and absorption
coverage by the shader graph's opacity. The previous shader branch discarded
those coefficients and used opacity as absorption. The correction is implemented
on the uncommitted engine candidate and passes the bounded native GPU check.
No new public material fields are required.

Existing coefficient semantics remain: absorption RGB is
`Material.scene.a * Material.absorption * (1 - Material.filter.rgb)`.
Absent `Material`, or zero-valued defaults, provides no absorption. Positive
absorption requires explicit scene alpha and a positive absorption scalar;
filter alpha is unused. Existing coverage is one when scene alpha times the
absorption scalar is positive, otherwise zero, then multiplied by graph opacity.
Per-model-part shaders inherit the entity's `Material`. HRC averages absorption
RGB into scalar optical depth; this change does not promise colored transmission.
Zero absorption does not remove the separate Depth-mode height occlusion.

Acceptance requires mesh and SDF shader-driven surfaces with zero and positive
absorption, filter RGB and graph-opacity variation. Per-part coefficient
inheritance is unchanged. Depth-mode rear-surface exclusion remains separate work.
Record bounded native renderer results and installed-SDK evidence separately;
Voidweaver's matched reference/runtime visual gate remains open.

`--surface-transport` first reproduced the defect on Metal: a graph mesh without
`Material` wrote absorption `(1, 1, 1)` and coverage `1` instead of zeros. After
the correction, one hidden 64x64 frame and two attachment readbacks pass all six
cases on both mesh and SDF paths: absent Material, zero absorption, positive
absorption, filter plus scene alpha, half graph opacity and zero-opacity cutout.
Visible-color assertions prevent an empty render from passing. Final native
selected execution took 2.270s; the six execution-policy cases also pass with
61 assertions in 0.339s. Astra independently reviewed the contract, fixture,
resource bounds and selector review. All checks use the production supervisor.

The regression measures raw visible and absorption buffers, not a final lit cave
or a performance guarantee. The public contract is in
[rendering-3d.md](rendering-3d.md#meshes). Logs, numeric Metal readbacks and the
separate packaged-shader verification are recorded in
`.chibi/environment-authoring`; package identity and tested shader hashes belong
in its candidate manifest. Full render suites were not run, and browser,
Studio, Depth-mode exclusion and directional HRC lighting are not covered here.

### Later shared renderer checkpoints

- **Directional HRC2D emission:** currently `SpotLight` is consumed by the 3D
  renderer; an authored HRC2D emission cone remains separate work. Prerequisites
  are an explicit source direction/cone/range contract and its integration with
  2D radiance transport. Acceptance must rotate and aim a local worklamp, check
  its lit region and occlusion, and compare ambient-on/off captures with camera,
  exposure and local sources held fixed.
- **Live-terrain shallow geometry:** choose its reusable geometry/depth contract
  and coordinate/material frames before implementation. It must consume the
  authoritative live terrain masks and revisions. Acceptance needs recessed rear
  surfaces and readable passage depth before/after a breach and after checkpoint
  restore, with coherent seams, collision and per-part material placement. This
  is not yet supplied by flat terrain contours or persistence.

## Ownership and contracts

The terrain checkpoint belongs in the engine repository as an optional reusable
module. A game selects its fields, seeds, composition, edit rules and save policy.
The module supplies masks, chunk lifetimes, collision/render geometry and portable
checkpoint data. The host chooses durable storage and commit timing; the terrain
module does not choose a save directory or start automatic writes.

The language is already separated through SDK targets: `Chibi::language` and
`Chibi::compiler` do not require the desktop renderer. Engine features enter
through capability declarations and host bindings. Execution intentionally uses
generic ECS, tasks and types for queries and systems. Keeping those capabilities
available does not require terrain or rendering in the language implementation.

Keep the repositories together for this checkpoint. A separate language
distribution should first prove a small installed consumer and remove unrelated
configuration dependencies. Moving repositories immediately would add coordinated
versioning and packaging work. Current runtime optionality is also broader than
link-time independence: `Chibi::engine` links the standard runtime modules, and
`Chibi::material_runtime` still bundles terrain with fluid/MPM simulation and ECS
adapters. Splitting those packages is a separate extraction, not a prerequisite
for these terrain fixes.

- The engine owns portable generation, validation, preparation, runtime data,
  public SDK APIs and controller operations. Studio owns the authoring application
  and consumes an installed SDK. Examples own playable acceptance content.
- Follow [code-source-roadmap.md](code-source-roadmap.md). Declarative materials,
  fields and shader libraries remain KDL with their existing domain validation and
  lowering. Executable behaviors, callable libraries and systems use `.chibi`.
  This initiative does not introduce a shader language or move GPU graphs into
  the behavior compiler.
- Put new Studio policy and interactions into the approved `.chibi` migration
  architecture. Shared native services handle computation and explicit platform
  boundaries. Do not establish a second C editor while migration gates remain.
- Native Chibi assets are authoritative. External interchange is an adapter with
  documented losses. Introduce Tiled import/export after native tilemap contracts
  work; complete parity with every reference product is not a release gate.
- Every authored operation must have a shared inspectable command or API usable
  by Studio and the controller. UI state, viewport projection and generated output
  must not become the only copy of an asset's intent.
- A generation recipe records inputs, parameters, seed and output settings.
  Preserve durable recipe/seed identity and compose generated base, authored
  overrides, then runtime edits. Store these separately. Regeneration must not
  silently overwrite durable edits. Bake outputs record dependencies and provenance.
- Share generation semantics between authoring, builds and supported runtime use.
  Runtime generation requires explicit bounds, ownership, cancellation and
  publication. Expensive rebuilds must not silently occur in an ordinary frame.
- Keep grid topology, logical coordinates, visual projection and render assets
  distinct. Heightfields, tilemaps and editable tiled meshes may share tools and
  recipes while retaining appropriate data representations.
- Keep visual materials separate from collision, physical surface properties and
  material simulation. Material simulation is deferred from this program.
- Game `.chibi` owns biome, cave, stamp and spawning policy. Shared services own
  terrain generation, chunking, editing, collision and persistence mechanisms.
  A cave generation policy does not imply a voxel or arbitrary 3D cave editor.

## Stages and parallel work

| Stage | Owner and dependencies | Deliverable and completion gate |
| --- | --- | --- |
| E0: Baseline and shared baking | Engine generation owner; starts now | Recover the MaterialStudy/Sandbox baseline. Add a narrow generic float material-channel baking API, bounded controller entry point, deterministic inputs/outputs, and one minimal migrated terrain fixture. C tests and controller readback establish semantics; applicable GPU checks establish rendering separately. |
| E1: 2D terrain lifecycle | Engine terrain owner; depends on E0 | Reuse MaterialTerrain for generated density/occupancy, runtime chunks, destructive edits, collision and persistence. Deliver a playable sidescrolling specimen with bounded generation and durable edits. Demonstrate matching rendering/collision and clean chunk seams after edits, unload/reload and replacement. |
| E2A: Procedural layout | Generation owner; depends on E0 and E1 contracts | Seeded terrain/biome masks, exclusions and object scattering; authored presets; preview, regeneration and manual overrides. Add constraint-based layouts only when a concrete example requires them. |
| E2B: Procedural tiles and materials | Baking/material owner; depends on E0 | Noise, patterns, masks, transforms, blend/filter/palette operations and reusable recipes. Generate tile borders and height, base-color, normal, roughness, metallic and AO outputs with explicit color spaces and channel conventions. |
| E3: Studio 2D terrain workflow | Studio owner; depends on E1, packaged SDK and applicable migration gates | Create/edit 2D density or occupancy terrain, paint/erase with brushes and stamps, edit masks and generated surfaces, preview scatter and destructive runtime behavior, undo/redo and save/reopen through shared services. Preserve the controller workflow as an equal authoring route. |
| E4: Complete 2D tilemaps | Engine tilemap and Studio owners; depends on shared asset/command contracts and Studio gates | Layers, atlas/tileset authoring, painting tools, stamps, terrain rules, animation, metadata and multiple projections, with native runtime playback and external interchange. Acceptance workflows below define completion. |
| E5: Professional material workspace | Engine shader/material and Studio owners; foundational services may proceed alongside E1/E2 | Reusable typed shader-library logic, material parameters/instances, previews, diagnostics and target-aware validation. Studio integrates them after its gates. Finish with authored environment materials and measured target scenes. |
| E6: 3D authoring exploration | Dedicated exploration owner; deferred | Compare Sprytile/Crocotile-style tiled mesh construction, modular prefab assembly and 3D heightfield terrain. Record the chosen source representation, commands and runtime bake contract before scheduling a full 3D modeling/editor release. |

An integration owner coordinates public SDK changes and acceptance content.
Agents may own E2A and E2B independently once E0 interfaces are reviewed. Terrain
runtime and shader/material services may also proceed independently when their
interfaces are explicit. Serialize edits to shared catalogs, codecs and build
registration. A separate reviewer checks generation semantics, data ownership,
asset publication and installed SDK consumption before integration.

The delivery checkpoints group these stages into reviewable outcomes. CP0
recovers the baseline specimen; CP1 establishes contracts and a terrain lifecycle
specimen; CP2 delivers usable 2D terrain editing after Studio gates; CP3 expands
runtime, materials and editor tools in parallel; CP4 certifies the complete 2D
release; CP5 records the deferred 3D comparison and decision. E0/E1 establish
CP0/CP1, E3 supplies CP2, E2/E5 and runtime expansion supply CP3, E4 completes
CP4, and E6 supplies CP5. None of these checkpoints is marked complete here.

## Acceptance workflows

### Shared generation and terrain

- Repeating a bake with the same recipe produces the same output. A seed or
  parameter change changes the expected output. Invalid inputs return actionable
  diagnostics without replacing valid output.
- The controller can generate and inspect results without Studio. Save/reopen
  preserves recipes, dependencies and authored overrides. Cancelled or failed
  work leaves the applied generation valid.
- Generate a 2D density/occupancy terrain with solid ground, a cave opening and
  a traversable passage. Apply a generated surface and run a sidescrolling actor
  against its collision. Destroy terrain across a chunk boundary and verify
  occupancy, rendering and collision agree without a seam.
- Persist edits, unload/reload chunks and reopen the project. Preserve generated
  base, authored overrides and runtime edits with recipe/seed identity. Test
  negative coordinates, partial chunks and deterministic neighboring chunks.
- Bake procedural height textures with explicit dimensions and value ranges.
  Future 3D heightfield generation/import/export requires a distinct geometry
  representation with explicit units, vertical scale and sufficient precision;
  it is not required by the first runtime terrain milestone.
- Prove bounded runtime generation with an authored game fixture. Report cold
  generation, steady playback and replacement costs separately. Keep scene/game
  choices in authored assets and behaviors.
- Use a second scene with different content to establish that shared services do
  not depend on the first fixture's rules or visual choices.

### Complete 2D editing

- Support paint, erase, pick, line/shape/flood fill, selections, clipboard,
  transforms, named multi-tile stamps and weighted variants. A brush stroke is
  one undoable command.
- Support grouped layers, ordering, visibility, locking, opacity, tint and
  parallax; collision/semantic grids; entity and decal placement; per-asset
  properties and animated tiles.
- Support corner/edge terrain matching and familiar 16/47 autotile templates,
  rule-generated visual layers, sparse chunked maps and fixed map bounds.
- Verify picking, transforms, ordering and save/reopen for orthogonal,
  isometric, staggered and hexagonal layouts. Add oblique projection through the
  same projection contract once its acceptance content exists.
- Author and play a platformer, a top-down level, an isometric scene and a hex
  map. Reproduce meaningful operations through the controller. Tiled round trips
  must state unsupported properties and losses.

### Professional material and shader authoring

- Separate runtime shader graphs, parameterized material assets/instances and
  offline texture recipes. Share compatible functions without treating the
  execution domains as interchangeable.
- Provide searchable typed nodes, reusable shader-library functions, useful
  defaults, node/output previews, source/graph navigation, grouped parameters,
  reset/inheritance inspection, and diagnostics tied to authored locations.
- Establish complete opaque PBR, unlit/pixel-art and alpha-clipped workflows.
  Validate texture color spaces, channel packing, tangent/normal conventions,
  atlas sampling and texel density. Base color is unlit; baked artistic lighting
  is a separate explicit output.
- Supply reusable environment functions for masks, world/object coordinates,
  vertex color, variation, triplanar sampling and surface/height blending.
  Integrate terrain weights and tile UVs through explicit inputs.
- Expose generated shader source, compilation errors/time, texture and sampler
  usage, variants and target compatibility. Static estimates do not establish
  frame performance. Measure representative native and browser/device scenes.
- Create one reusable environment material with dry/wet/moss variations, blend
  another surface using a painted or generated mask, preview it on actual
  geometry, inspect a deliberate shader error, and bake texture outputs.
- Complex artist-editable material layer stacks are a follow-on to function and
  instance reuse. Refraction, subsurface scattering, anisotropy, clear coat,
  displacement, virtual textures, decals and custom passes require separately
  scoped renderer support and target acceptance.

### Deferred 3D decision

Build a house with walls, roof, stairs and raised platform from an atlas; test
face placement, UV painting, vertex edits and extrusion. Compare that workflow
with reusable prefab placement and placement on a generated 3D heightfield.
For heightfields, explore sculpt/flatten/smooth, precise import/export and patch
boundaries as their own geometry workflow. Require
save/reopen, undo/redo, collision, controller access and orthographic/perspective
views. The decision checkpoint does not approve general-purpose modeling,
character rigging, animation timelines, movie rendering, or voxel caves.

## Validation and delivery

Use C/pico_unit checks in the owning dispatchers. Run affected-contract selectors
and groups, including `make test-native` when applicable, and the relevant
builds. Preserve backend, hardware and explicit hold requirements, and report
build/configuration time separately from test execution.

Before landing, independently review the exact candidate and record its source
tree, checks, package identity and remaining holds. Engine tests, installed SDK
consumption, Studio runtime interaction, web/device performance and deployment
are separate evidence. Do not mark a stage complete from compilation alone.
Keep the existing migration register unchanged and respect its explicit holds.

## Research references

- [GameMaker tilesets](https://manual.gamemaker.io/beta/en/The_Asset_Editors/Tile_Sets.htm)
  and [autotiles](https://manual.gamemaker.io/beta/en/The_Asset_Editors/Tile_Set_Editors/Auto_Tiles.htm)
  inform brushes, animation and 16/47 templates. These are beta manual links.
- Tiled documents [editing tools](https://doc.mapeditor.org/en/stable/manual/editing-tile-layers/),
  [layers](https://doc.mapeditor.org/en/stable/manual/layers/),
  [projections](https://doc.mapeditor.org/en/stable/manual/maps/),
  [terrain matching](https://doc.mapeditor.org/en/stable/manual/terrain/) and
  [automapping](https://doc.mapeditor.org/en/stable/manual/automapping/).
- [Ogmo](https://ogmo-editor-3.github.io/) informs entity, decal and metadata
  layers. [LDtk auto-layers](https://ldtk.io/docs/general/auto-layers/) separate
  semantic grids from generated visual layers.
- [Tilesetter generation](https://www.tilesetter.org/docs/generating_tilesets)
  informs source/edge composition. [Tilemancer by Led](https://led.itch.io/tilemancer)
  informs procedural pixel-art height/shading recipes; its author ended
  development in 2019 and identifies Tilesetter as its successor.
- [Material Maker](https://www.materialmaker.org/) and its
  [batch export](https://rodzill4.github.io/material-maker/doc/command_line.html)
  inform procedural materials and reproducible output.
- Terrain3D documents [heightmaps](https://terrain3d.readthedocs.io/en/stable/docs/heightmaps.html),
  [painting](https://terrain3d.readthedocs.io/en/stable/docs/texture_painting.html)
  and [interchange](https://terrain3d.readthedocs.io/en/stable/docs/import_export.html).
- [Sprytile](https://docs.sprytile.xyz/quick-start/) and
  [Crocotile3D](https://www.crocotile3d.com/howto.html) inform tiled mesh construction.
  [Godot GridMap](https://docs.godotengine.org/en/stable/tutorials/3d/using_gridmaps.html)
  illustrates the separate modular mesh-placement workflow.
- Unreal documents [material instances](https://dev.epicgames.com/documentation/unreal-engine/creating-and-using-material-instances-in-unreal-engine),
  [layers](https://dev.epicgames.com/documentation/unreal-engine/using-material-layers-in-unreal-engine),
  [terrain materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-materials-in-unreal-engine)
  and [editor diagnostics](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-material-editor-ui).
- Unity documents [subgraphs](https://docs.unity3d.com/Packages/com.unity.shadergraph@17.3/manual/Sub-graph.html),
  [parameters](https://docs.unity3d.com/Packages/com.unity.shadergraph@17.3/manual/Blackboard.html),
  [material variants](https://docs.unity3d.com/6000.3/Documentation/Manual/materialvariant-landingpage.html),
  [targets](https://docs.unity3d.com/Packages/com.unity.shadergraph@17.3/manual/Graph-Target.html)
  and [terrain shader authoring](https://docs.unity.com/en-us/engine/6000.3/manual/creating-environments/script-terrain/terrain-shader-graph).
