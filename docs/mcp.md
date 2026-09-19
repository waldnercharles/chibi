# Chibi MCP

The project exposes a local MCP server backed by the native `chibi_ctl`
controller. The controller uses the generated reflection registry and the
same asset loaders and compiler used by the editor.

## Start the server in a terminal

Run this from the project root:

```sh
make mcp
```

The target builds `chibi_ctl`, which handles headless automation and GPU previews
through its `--render` mode, then starts the Node.js adapter in the foreground.
The adapter uses stdin and stdout for
newline-delimited JSON-RPC. It does not open a port. The readiness message is
written to stderr so it does not corrupt the protocol stream.

Paste these lines into the running terminal to perform a live MCP handshake,
list the available tools, and ask for project status:

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"manual-terminal","version":"0.1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"getProjectStatus","arguments":{}}}
```

The server prints one response for each request that has an `id`. The
notification has no response. Press Ctrl-D to send EOF and stop the server, or
Ctrl-C to interrupt it.

For a repeatable one-shot check, send the same protocol messages through a
pipe. Build diagnostics stay on stderr while stdout contains JSON-RPC:

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"manual-terminal","version":"0.1"}}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"getProjectStatus","arguments":{}}}' |
  make mcp
```

The checked-in `.mcp.json` starts the self-locating adapter for MCP hosts that
support project configuration. The public catalog and canonical workflow below
are shared by MCP hosts and Codex.

Codex can start the adapter automatically for this repository. Its project
scoped registration lives in `.codex/config.toml`; it is loaded when Codex is
started in this repository or one of its subdirectories. The launcher keeps
build output on stderr, starts the stdio adapter on stdout, and builds
`build/chibi_ctl` on first use if needed. Run
`make mcp-setup` to generate that machine-local registration. Restart Codex
or open a new task after adding or changing project MCP configuration.

## Default authoring workflow

For reproducible rendering measurements, `previewScene` accepts
`freeze_simulation` and an `input_timeline` spanning preparation and advancing
render frames. Results include initial/final runtime state and the active GPU
backend. Optional `metal_capture` and `capture_frame` record one warmed frame on
supported Macs. See [the GPU benchmark workflow](neon-gpu-benchmarks.md).

Use the MCP for authored content whenever it is available. Route requests by
domain:

- Shader, material, shape, or shader-library work: search with `searchAssets` and
  `searchNodes`, inspect with `inspectGraph`, then use `buildShader`.
- Behavior, event, or behavior-library work: search with `searchAssets` and
  `searchNodes`, inspect with `inspectGraph`, then use `buildBehavior`.
- Prototype or prefab work: use `describeComponents` when field details are
  unclear, then use `buildPrototype`.
- Scene or level composition: use `buildScene`.

Call `validateAsset` after every published change. Use `previewShader`,
`previewScene`, `runScenario`, `captureRuntimeSnapshot`, or
`profileScenario` for evidence. Existing assets require the current
`expected_hash`; graph and optimizer dry runs report proposed changes without
saving them. Tool results include complete `structuredContent` and
`next_steps`.

- `buildShader`: create or edit material, shape, and shader-library graphs.
- `buildBehavior`: create or edit behavior, event, and behavior-library graphs.
- `buildPrototype`: create or edit reflected components, fields, and behavior attachments.
- `buildScene`: create or edit named entities, prototype instances, overrides, and attachments.
- `optimizeGraph`: analyze or publish proven graph folds and graph-local aliases.
- `previewShader`: compile and render a material or shape graph.
- `previewScene`: render a prototype or scene with its authored presentation.
- `searchAssets`: find catalog entries by kind, name, path, or description.
- `searchNodes`: find reflected nodes and typed pins for graph authoring.
- `inspectGraph`: inspect graph content, diagnostics, and the current content hash.
- `readAsset`: read one authored asset as parsed JSON.
- `validateAsset`: run native asset loading and compiler diagnostics.
- `listAssets`: list deterministic asset paths, sizes, and hashes.
- `getAssetCatalog`: read semantic asset metadata, dependencies, diagnostics, and registry data.
- `inspectAssetDependencies`: inspect incoming or outgoing direct and transitive dependencies.
- `getProjectStatus`: return project, asset, and reflection counts.
- `describeComponents`: inspect reflected component fields, stable keys, and native defaults.
- `inspectSystem`: inspect a typed system query, compiled graph, inferred effects, and diagnostics.
- `getExecutionStatus`: report the backend selected for a behavior or system.
- `runScenario`: run a bounded deterministic prototype or scene scenario.
- `captureRuntimeSnapshot`: capture bounded ECS entities, reflected fields, behaviors, and timing.
- `profileScenario`: profile a bounded prototype or scene with warmup and timing samples.
- `recordScenario`: record bounded normalized inputs and deterministic frame hashes.
- `replayScenario`: replay a returned recording and report divergence.
- `describeRegistry`: inspect reflected nodes, pins, types, services, operators, and components.
- `describeContract`: inspect the private native contract and its public tool mappings.

## Check adapter distribution

The adapter package keeps the runtime modules and launcher in its published
file boundary. From `tools/mcp`, run `npm ci`, `npm run audit:names`, and
`npm run pack:check`. The audit uses `o200k_base` to report candidate name
token counts, token pieces, description limits, and the serialized
`tools/list` budget.

The client can also read these MCP resources:

- `chibi://project/status` and `chibi://catalog` for stable project summaries.
- `chibi://asset/{kind}/{name}` for parsed authored JSON.
- `chibi://asset/{kind}/{name}/dependencies` for dependency relationships.
- `chibi://asset/{kind}/{name}/graph/{graph_id}` for one normalized graph.
- `chibi://recording/{id}` for a generated deterministic recording.

All public tools report safety metadata through `describeContract`. They do not
provide arbitrary shell commands, network access, editor UI control, or access
to files outside the project data needed by the native controller. `buildShader`
and `buildBehavior` write authored graph assets after validation and an
expected-hash check. They are marked as modifying tools. `recordScenario`
writes only its bounded generated recording artifact under `/tmp`, and
`replayScenario` accepts only a returned
recording or a safe generated recording path.

For graph work, the agent should reason from node identity, pins, edges, calls,
effects, and diagnostics. `inspectGraph` omits node coordinates, inline UI
hints, comment-box positions, and wire routing by default. Pass
`include_layout: true` when those editor details are needed. Placement is
presentation data; the graph arranger assigns coordinates after graph content
changes, so an agent does not need to choose node positions by hand.

`buildPrototype` applies the same boundary to prototype assets. Components are
selected by reflected name or stable key, `add_component` uses native defaults,
`set_field` accepts JSON values through the prototype loader's type conversion,
and behavior attachments are resolved and loaded before publication. The
complete edit batch is serialized, loaded again for round-trip validation, and
published with the same expected-hash check as graph edits.

`buildScene` applies the same boundary to multi-entity fixtures. Entities are
addressed by name and reference a prototype; component overrides, retained
editor values, and behavior attachments are semantic JSON objects. The scene
loader validates every prototype, override, and behavior before publication.
Scene scenarios use the authored entity list, so a test can include a target,
enemy, and support entity without constructing a second JSON fixture by hand.

## Author a graph or shader

Use `searchAssets` to find an asset, `inspectGraph` to read its graph and
current hash, and `searchNodes` to fetch the few node signatures needed for
an edit. Submit shader changes through `buildShader` and behavior changes
through `buildBehavior`. The server
assigns IDs, initializes typed pins, validates the resulting asset, and
automatically arranges the affected graphs. The edit API has no node coordinates
or layout controls.

Graph metadata can be authored in the same request with `description`,
`owner_type`, and `graph_description`. The response echoes the selected graph
metadata. `owner_type` is useful for behavior assets that expose `Self`; graph
descriptions give functions and event graphs an authoring purpose that catalog
search can surface.

For example, these `buildShader` arguments create a material:

```json
{
  "kind": "material",
  "name": "WarmSurface",
  "create": true,
  "operations": [
    {"op": "add_node", "ref": "color", "type": "Shader Vector3",
     "inputs": {"X": 1, "Y": 0.25, "Z": 0}},
    {"op": "connect", "from": "color", "from_pin": "Value",
     "to": "output", "to_pin": "Base Color", "kind": "data"}
  ]
}
```

New material and shape assets have an `output` node alias. New shapes start
with 32-unit half extents; use `{"op":"set_bounds","value":[32,24]}`
to set the finite positive half extents that enclose a shape. Other aliases last
only for the current request; use the stable IDs returned in `result.nodes`
for later requests. Creating a function with `add_graph` and `ref: "math"`
also exposes `math.entry` and `math.return`, so the same batch can add its
pins and connections without guessing IDs. Pin names avoid manually maintaining
serialized pin IDs.
Graph batches also support `select_graph`, `rename_graph`, `remove_graph`,
`remove_variable`, `set_pin`, `remove_pin`, `split_pin`, `recombine_pin`,
`expose_fields`, `hide_fields`, `bind_input`, `clear_binding`, and `create_local`.
Select the owning graph before referring to one of its nodes. Graph selectors
accept IDs, names, or aliases created in the same request. Function calls can
resolve `reference_name` without a numeric graph ID.

For `bind_input`, choose `kind: "variable"` with a variable ID/name/alias,
`kind: "component"` with a reflected component name or stable key and optional
`field`, or `kind: "delta_time"`. Omitting `field` binds the whole component.
These operations use the same graph model as editor bindings and projections.

`add_node` honors `event_name`. Tick, Input, and Contact Begin events acquire
their host payload pins automatically. Declare custom payloads as variables in
an `event` asset first; adding Event, Call Event, or Send Event for that name
then applies its declaration IDs and types. Payload declarations participate in
link validation. Do not recreate their pins manually.

Use `set_output` on a constant node when a shader value has no input pins;
provide `type` for a vector or struct value so the wildcard output is resolved
before validation.
Existing assets require the exact `expected_hash` returned by inspection or
the previous edit. A stale hash returns `asset_conflict`; inspect again before
constructing a replacement batch. `create: true` refuses to overwrite an asset.

For example, this prototype edit adds a movement component, sets a reflected
field, and attaches a behavior without reproducing component defaults or JSON
field ordering:

```json
{
  "kind": "prototype",
  "name": "PhaseWarden",
  "expected_hash": "0123456789abcdef",
  "operations": [
    {"op": "add_component", "component": "GameMovement", "fields": {"speed": 55}},
    {"op": "set_field", "component": "GameHealth", "field": "maximum", "value": 30},
    {"op": "add_behavior", "behavior": "PhaseWardenBehavior"}
  ]
}
```

Pass `dry_run: true` to validate and obtain predicted hashes without saving.
A failed operation or validation leaves the authored file unchanged. Successful
batches publish one complete file, and concurrent MCP writes to the same asset
are serialized. The hash is checked again just before publication; this is
not a locking protocol shared with unrelated filesystem writers.

These are offline asset edits. They do not modify an open editor document or
add an editor undo entry; save or close unsaved editor work before editing the
same asset through MCP. Referenced libraries are linked for validation, while
the saved file retains its authored library references.

Call `previewShader` with `{"kind":"material","name":"WarmSurface"}` to
inspect the result. It returns a PNG image and compact diagnostics, with image
bytes excluded from the accompanying structured JSON. Material and shape
previews use the production renderer in a separate native process. They
require a working graphics backend; on Linux, use a display or Xvfb. Shader
libraries are validated as reusable functions; preview a material or shape
that consumes the library to see its effect.

Requests are bounded: node search returns at most 50 entries, edit batches
contain at most 128 operations, scene edits contain at most 128 operations and
64 behavior attachments per entity. Shader previews are at most 512 by
512; scene previews accept 64–1024 pixels per side. `searchNodes` can filter by pin type, container, pin kind, and purity;
its text query also searches pin names and type names.
Preview time is explicit and deterministic. Pass `parameters` as an object
keyed by `Shader Parameter` names to try scalar values or exact-width vector
arrays without editing the asset. Unknown names, nonfinite values, and wrong
vector widths are rejected before drawing. Generated GLSL is available with
`include_source: true` and is limited to 64 KiB. MCP cancellation and the
preview timeout terminate the GPU child process.
Use `camera` with `center: [x, y]` and `span: [width, height]` to override
preview framing. Use `sample_grid` to request up to a 16 by 16 CPU evaluation
grid over `uv_min` and `uv_max`; each sample returns the UV coordinate and all
shader output vectors, with local positions mapped from that UV rectangle.
This separates graph behavior from production geometry or camera framing
assumptions.

`optimizeGraph` uses the same optimizer as the editor's Graph Actions menu.
Start with `dry_run: true`: it reports before and after node and edge counts,
folded inputs, removed nodes, local aliases, changed edges, and each skipped
opportunity. Safe folds include component getters, variable reads, `DeltaTime`,
`Self`, and constants when their source, consumer, conversion, fan-out, and
debug state are unambiguous. The native controller validates the optimized
clone before an apply request publishes it with `expected_hash`.

The stdio adapter negotiates MCP protocol versions `2025-06-18` and
`2025-11-25`, keeps notifications response-free, and advertises the dynamic
`chibi://asset/{kind}/{name}` and
`chibi://asset/{kind}/{name}/dependencies` and
`chibi://asset/{kind}/{name}/graph/{graph_id}` and
`chibi://recording/{id}` resource templates through
`resources/templates/list`. Recording IDs are bounded opaque identifiers; the
recording resource is resolved by the controller so clients never need direct
filesystem access. The static `chibi://catalog` resource provides the
same semantic catalog as the `getAssetCatalog` tool. Tool responses include a
compact text summary for conversational clients and the complete native
response in `structuredContent`, including a `next_steps` follow-up.
The `describeContract` result labels each private native operation with the
public MCP tool or tools that expose it.

The native controller also accepts newline-delimited JSON directly:

```sh
printf '%s\n' '{"contract":1,"operation":"status"}' |
  build/chibi_ctl --project-root .
```

The repository root is the Arena3D project. Arena2D is an independent project
at `projects/arena2d`, with its own manifest and `content` tree. Point the
controller or renderer at the project root when searching, authoring, running,
or previewing assets; names are resolved only within that project.

```sh
printf '%s\n' '{"contract":1,"operation":"status"}' |
  build/chibi_ctl --project-root projects/arena2d
```

For example, search the catalog and inspect a graph's asset dependencies:

```sh
printf '%s\n' '{"contract":1,"operation":"search_assets","query":"Health"}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"asset_dependencies","kind":"behavior","name":"HealthBehavior","direction":"both","transitive":true}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"inspect_graph","kind":"library","name":"DirectionMath","graph_name":"Direction Orientation"}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"inspect_system","kind":"system","name":"MovementSystem"}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"run_scenario","kind":"prototype","name":"ArenaPlayer","frames":60,"fixed_dt":0.0166666666666667}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"runtime_snapshot","kind":"prototype","name":"ArenaPlayer","frames":2,"entity_limit":32,"component_limit":16,"field_limit":32}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"record_scenario","kind":"prototype","name":"ArenaPlayer","frames":60,"fixed_dt":0.0166666666666667,"inputs":[{"movement":{"x":1,"y":0},"aim":{"x":0,"y":1},"actions":[{"name":"Fire","pressed":true}],"events":[{"name":"Trace"}]}]}' |
  build/chibi_ctl --project-root .
```

`runScenario` accepts prototype or scene targets, runs at most 108,000 fixed steps,
and returns the resolved asset identity, frame count, timestep, world hash, live
entity count, and engine timing fields. The fixed timestep must match the
engine's `1/60` second step. Each request creates an isolated preview runtime;
the returned world hash can be compared across repeated calls to check
deterministic behavior.

`getExecutionStatus` distinguishes availability from selection. Read the reported backend and fallback details for the selected asset. A typed
system may report generic bytecode, typed bytecode, or `sljit`; `fully_jit` is true only when the actual
Tick path selected SLJIT for the live temporary system. `jit_available` alone
means the backend exists in the build and does not mean the asset is using it.

`profileScenario` runs optional warmup frames before collecting samples. It
aggregates engine frame timing and behavior invocation and CPU counters across
all sampled frames, and returns the target's final world hash. Timing is
diagnostic data from the isolated runtime; it does not alter authored assets.
Each behavior row exposes `inclusive_cpu_ms` (including nested root events) and
`cpu_ms` (exclusive time), plus an average for each.
The result also includes `allocations`, which reports calls and requested bytes
observed through the engine's `xalloc` wrappers during sampled frames. It does
not claim to count third-party or system allocator traffic.

Recorded scenario inputs use `movement` and `aim` samples plus an `actions`
array of `{ "name": string, "pressed": boolean }` records. Action names are
resolved against the attached behavior graphs, so unknown actions are rejected
instead of being silently ignored.

`captureRuntimeSnapshot` uses the same isolated preview runtime and returns ordered
entity and component records with reflected field values, attached behavior
state, and timing data. `entity_limit`, `component_limit`, `field_limit`, and
`behavior_limit` bound the response; `fields` can restrict reflected field
selection. The operation is read-only and does not write authored content.

`runScenario` and `captureRuntimeSnapshot` accept up to 600 input changes across a
run lasting up to 30 simulated minutes. Use `inputs` for consecutive frames or
`input_timeline: [{"frame": 0, "input": {...}}, ...]` for sparse changes. Timeline
frames must be unique, increasing, and less than `frames`. Movement, aim, and
physical controls remain held between samples. Named events and UI actions
occur only at their specified frame.

Each input can contain:

- `movement: {"x": ..., "y": ...}` with both axes in [-1, 1].
- `aim: {"x": ..., "y": ..., "point": false, "firing": false}`. Direction axes
  are in [-1, 1]; `point: true` uses world coordinates within ±1,000,000.
- `controls: [1, 3]`, the held physical control codes interpreted by authored
  `GameActionBinding` components. This also determines continuous firing.
- `actions: [{"name": "Scan", "pressed": true}]`, up to 32 normalized action
  transitions. The action must exist in attached behaviors. Presses activate
  its event on GameInput owners; release permits the next press.
- `events: [{"name": "Award", "target": "ship", "values": [{"type": "int", "value": 42}]}]`,
  up to 32 events with up to 16 payload values. Types are bool, int, uint,
  float, double, name, string, or entity. Entity values resolve scene names.
- `ui: [{"target": "slot", "action": "drop", "source": "part"}]`. Actions are
  `activate`, `change` (requires `value`), and `drop` (requires `source`). They
  use the same visibility, disabled-state, range, and drop-group validation as
  rendered widgets, then send the widget's authored event.

Aim goes through the engine's normal input path. An `Aim` event and the old
object-shaped `actions` input are not part of this contract.

Scenario preparation registers the entire runtime prototype catalog and its
behavior dependencies before launch, including prototypes spawned by name but
not placed in the scene. `buildScene` copies prototype behaviors when
`add_entity` omits `behaviors`; an explicit empty array creates an entity without
attached behaviors. Scene/world positions and player UI layout are authored
component fields. Only graph node placement is automatic.

`recordScenario` captures at most 600 fixed steps. Recording format **v2** has
one canonical set of fields: `version`, `contract_version`, `id`, `format`,
`kind`, `name`, `content_hash`, `runtime_content_hash`, `assets`, `frames`,
`fixed_dt`, `artifact_path`, `inputs`, `saved_state`, `frame_hashes`,
`live_entities`, and `world_hash`. `assets` is the runtime dependency manifest.
Per-frame hashes include reflected world state, committed saves, visibility
memory and sound cue history. Replaying rejects changed dependencies before
execution. Old v1 recordings and duplicate field aliases were removed; record
new evidence after a contract change. Artifact IDs and paths are still bounded
and stored outside authored content.

`replayScenario` accepts exactly one returned recording or its generated
`recording_path`. It reports the first divergent frame when evidence differs.
The initial `saved_state` is restored into an isolated store for recording and
replay. No scenario reads or writes the player's real save file.

## Player-facing capabilities

These optional capabilities are available as reflected components or graph
nodes in both the editor and MCP. They contain no job, loadout, reward, or
extraction policy.

| Capability | Authoring contract |
| --- | --- |
| Camera | One enabled `GameCamera` plus `GameTransform`. Positive width/height set the logical span; offset follows the entity. Disable it to release the camera. |
| Controls | `GameActionBinding` maps one physical source to an event name. Multiple entities can bind the same action; their held states combine. `firing` also sets GameAim.firing. Remove GameInput while fitting to suspend ship controls. |
| Player UI | `GameWidget` supplies label, panel, button, progress, slider, fitting slot, or map. Text, colors, geometry and values are reflected and writable from behaviors. |
| Save progress | `Read Saved Text`, `Set Saved Text`, `Remove Saved Text`, `Commit Save`, `Discard Save Changes`. Pending changes become durable only on commit. `Commit Save.Failed` supports retry/error UI. |
| Sound | `Play Sound` synthesizes a swept tone/noise cue. Frequency and end frequency: 20–10,000 Hz; duration: 0.01–4 s; volume/noise: 0–1; pan: 0 left, 0.5 center, 1 right. Editor Play and standalone Play mix up to 64 voices. A zero volume mutes a cue. |
| Lighting | `GameHRC2D` authors fractional 2D transport resolution, trace/feedback settings, ambient, intensity, radiance fog, shadow floor, falloff, temporal blending, upscaling and debug view. `GameEnvironment` authors 3D exposure and bloom. |
| Line of sight | `Raycast` returns Found, Entity, Point, Normal and Distance against circles and rotated boxes. Category filtering is independent of collision masks. New colliders and teleports are visible to the query. |
| Fog | One enabled `GameFog` defines origin, 1–256 rows/columns, cell size, color and ambient drift. `drift: 0` removes fog animation. `GameVision` sources reveal nearby cells, blocked by `occluders`. |
| Terrain memory | `GameVision.remember` adds explored terrain. Positive duration creates a shrinking temporary clearing; age starts at zero. `Visibility At` reads Visible/Explored/Obstacle. `GameRenderState.visibility_culled` opts an actor into current-sight culling; `hidden` always hides it. |
| Sensor contacts | `GameSensorContact` stores an observed position, lifetime and color. Map widgets show those stored positions independently of explored terrain. No live enemy positions enter map memory automatically. |

Widget kind codes are label=0, panel=1, button=2, progress=3, slider=4, slot=5,
map=6. `anchor` and `pivot` are viewport fractions. `offset` and `size` use
logical pixels at a viewport height of 720; `order` controls stacking. Colors
are packed RGBA. An interactive widget sends its named event with
`Source: ecs.entity` and `Value: float`. Create that event definition first.
Matching nonempty `drop_group` values permit transfers from `draggable` widgets.
Behavior graphs own compatibility checks, recipe changes, summary text and
undo history. Buttons support keyboard activation; authors can supply move and
undo buttons alongside dragging. Pointer and keyboard widget interactions
capture gameplay input while active.

Control codes: mouse left=1, mouse right=2, Space=3, Shift=4, Tab=5, Enter=6,
A–Z=7–32, pad A/B/X/Y=33–36, shoulders=37/38, triggers=39/40, aim stick=41.
The editor shows named choices. Existing Arena controls are migrated to
components in its player prototype and scene.

Save stores allow 1,024 namespaced keys of 1–256 bytes, values up to 65,536
bytes, and 1 MiB total text. Standalone native Play uses the platform's
`chibi/game/profiles.json` user-data path and atomic file replacement; Web Play
uses browser localStorage. Namespace keys for each game. Editor previews keep
committed data only for that live world; scenarios accept and return
`saved_state` for explicit cross-run tests. Export contains committed values
only. Restart discards pending values. Store unlock choices at extraction and
leave run inventory in ordinary variables/components. Saved settings can drive
sound volume, effect intensity, camera displacement and animation amounts.

The web 2D renderer uses the authored ambient and emission gains directly; indirect lighting, light haze and shadow-floor tuning require the native radiance renderer.

Use `previewScene` with a scene/prototype name, optional frames, inputs and
saved state to inspect a running result. It uses the same 2D/3D renderer,
authored camera and widgets, with pixel dimensions independent of display DPI.
This tool needs GPU/display access. Returned images include the UI; structured
JSON omits the base64 bytes. For visual fixtures, explicitly author material
emission rather than relying on the emissive appearance fallback.

Catalog, search, and asset-list responses accept a numeric `limit` and `cursor`,
and return `total`, `count`, `has_more`, and `next_cursor`. Catalog and search
also accept `summary: true` to omit relationship arrays from each asset record.

The launcher sets `CHIBI_ROOT` and controller paths from its own location.
When launching `server.mjs` directly outside the repository, set
`CHIBI_ROOT` or `CHIBI_CTL` to an explicitly built controller executable.

`make test` includes the MCP protocol and native-controller smoke checks,
including `design-smoke.mjs`, which builds independent fixtures entirely through
MCP. `CHIBI_KEEP_DESIGN_FIXTURE=1` retains them; `CHIBI_MCP_GPU=1` also renders
2D/3D fog, fitting and dock previews. `--game-capabilities` runs native save,
visibility, sound and actual ImGui interaction checks. Set
`CHIBI_VISIBILITY_BENCH=1` for the moving-observer visibility benchmark.
Custom build checks use that build's controller and render-controller binaries.

The adapter is intentionally local and bounded. Semantic asset edits, runtime
inspection, deterministic recordings, and shader previews share the native
controller contract without adding arbitrary shell or file access.

### Graph optimization

`optimizeGraph` reports proven binding folds, graph locals, cleanup, and skipped
candidates. It defaults to a dry run. Apply uses `dry_run: false` and the current
`expected_hash`; graph selection uses `graph_id` or `graph_name`. The fanout
threshold defaults to three. Locals preserve source evaluation and never add
persistent behavior variables. See [graph optimizer](graph-optimizer.md) for
representation, safety rules, and validation.

`GameMapMarker` adds named landmarks and objectives to map widgets. Its stored
`position` remains hidden until explored unless `always_visible` is set; behaviors
control updates and `disabled`. Map widgets use their background, color and accent
for unseen terrain, obstacles and current visibility. Sensor contacts remain
separate and expire without updating terrain memory.
