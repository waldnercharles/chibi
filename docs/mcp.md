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
started in this repository or one of its subdirectories. The launcher starts
the stdio adapter immediately, so hosts with short connect timeouts can attach
from a fresh worktree. When `build/chibi_ctl` is missing and no cached build
of the same sources exists, the adapter builds it in the background with output
on stderr, and the first tool call waits for it. A rebuilt controller replaces the running one on the
next request. Run
`make mcp-setup` to generate that machine-local registration. Restart Codex
or open a new task after adding or changing project MCP configuration.

## Asset files

Authored assets under `content/` are YAML. Documents omit what loading
reproduces: default values, pin fields that match the node type's template
pin, and `x`/`y` for nodes the editor arranges when it opens a graph. Pins are
keyed by name, and their IDs follow the node ID in order (inputs, then
outputs); an `id` appears only where the sequence breaks. Edges, locals and
event bindings name pins as `<node id>.<pin name>`, or
`<node id>.<pin>.<field>` for an exposed struct field. Wildcard pins (math
operators, reroutes, loops) omit the types their connections determine.
Expression functions store their `math_expression` source and `entry_id`;
loading rebuilds their nodes, mode and return node, and names them after their
source unless `name` says otherwise. An expression call's pins omit the groups
and flags its function implies. A call to a function in the same asset takes each pin's
`declaration_id` from the signature pin with its name. Enums are spelled by
label. A node whose pin names repeat lists its pins with explicit IDs instead. `build/chibi_ctl --project-root <root> --format`
rewrites every asset in canonical form, and `--check` lists assets that
differ. Prefer the build tools over hand edits; they keep IDs, hashes and
layout consistent.

## Default authoring workflow

For reproducible rendering measurements, `previewScene` accepts
`freeze_simulation` and an `input_timeline` spanning preparation and advancing
render frames. Results include initial/final runtime state and the active GPU
backend. Optional `metal_capture` and `capture_frame` record one warmed frame on
supported Macs. Without vendor tools, `pass_timing` waits for the GPU after each
render pass and reports average milliseconds per pass, and `capture_frames`
writes every measured frame to a raw RGBA8 file for judging motion and temporal
effects. Both serialize or read back each frame, so their frame totals are not
benchmarks. See [the GPU benchmark workflow](neon-gpu-benchmarks.md).

Use the MCP for authored content whenever it is available. Route requests by
domain:

- Graph work (behaviors, events, libraries, materials, shapes, shader
  libraries, fields, and Systems): search with `searchAssets` and
  `searchNodes`, inspect with `inspectGraph`, then use `buildGraph`. Check
  shaders with `previewShader`, fields with `previewField` (CPU evaluation, no
  GPU required), and Systems with `inspectSystem` and `getExecutionStatus`.
- Prototype or prefab work: use `describeComponents` when field details are
  unclear, then use `buildPrototype`.
- Scene or level composition: use `buildScene`.
- Studio's component tabs, prototype icons, and number ranges and units:
  edit `content/editor/Components.yaml` (kind `editor`), run `--format`, then
  call `validateAsset`.
- Game UI documents: edit `content/ui/<Name>.yaml` (kind `ui`) with `rml`
  body markup and optional `rcss` styles, run `--format`, then call
  `validateAsset`.

Call `validateAsset` after every published change. Use `previewShader`,
`previewScene`, `runScenario`, `captureRuntimeSnapshot`, or
`profileScenario` for evidence. Existing assets require the current
`expected_hash`; graph and optimizer dry runs report proposed changes without
saving them. Tool results include complete `structuredContent` and
`next_steps`.

- `buildGraph`: create or edit any graph asset; `query` replaces a System's ECS query.
- `previewField`: bake a bounded scalar field and return its PNG, range and hash.
- `buildPrototype`: create or edit reflected components, fields, and behavior attachments.
- `buildScene`: create or edit named entities, prototype instances, overrides, and attachments.
- `optimizeGraph`: analyze or publish proven graph folds and graph-local aliases.
- `previewShader`: compile and render a material or shape graph.
- `previewScene`: render a prototype or scene with its authored presentation.
- `searchAssets`: find catalog entries by kind, name, path, or description.
- `searchNodes`: list a domain's node types, or find nodes, configured component nodes, and their pins by words.
- `inspectGraph`: inspect graph content, diagnostics, and the current content hash.
- `readAsset`: read one authored asset as parsed JSON.
- `validateAsset`: run native asset loading and compiler diagnostics.
- `listAssets`: list deterministic asset paths, sizes, and hashes.
- `getAssetCatalog`: read semantic asset metadata, dependencies, diagnostics, and registry counts.
- `inspectAssetDependencies`: inspect incoming or outgoing direct and transitive dependencies.
- `getProjectStatus`: return project, asset, and reflection counts.
- `describeComponents`: inspect reflected component fields, stable keys, and native defaults.
- `inspectSystem`: inspect a typed system query, compiled graph, inferred effects, and diagnostics.
- `getExecutionStatus`: report the backend selected for a behavior or system.
- `runScenario`: run a bounded deterministic prototype or scene scenario; `behavior_failures` lists runs that stopped on an error, by node and message.
- `captureRuntimeSnapshot`: capture bounded ECS entities (32 unless `entity_limit` is set), reflected fields, behaviors, and timing.
- `profileScenario`: profile a bounded prototype or scene with warmup and timing samples.
- `recordScenario`: record bounded normalized inputs and deterministic frame hashes.
- `replayScenario`: replay a returned recording and report divergence.
- `describeRegistry`: inspect reflected types, services, and components; request `sections: ["nodes"]` for every node and pin.
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
to files outside the project data needed by the native controller. `buildGraph`
writes authored graph assets after validation and an expected-hash check. They are marked as modifying tools. `recordScenario`
writes only its bounded generated recording artifact under `/tmp`, and
`replayScenario` accepts only a returned
recording or a safe generated recording path.

For graph work, the agent should reason from node identity, pins, edges, calls,
effects, and diagnostics. Node comments (`comment`) are part of that record.
`inspectGraph` omits node coordinates, inline UI hints, hidden comment
bubbles, comment-box positions, and wire routing by default. Pass
`include_layout: true` when those editor details are needed. Placement is
presentation data; the graph arranger assigns coordinates after graph content
changes, so an agent does not need to choose node positions by hand.

Each inspected node also lists its pins as `in` and `out`, written as
`searchNodes` writes them (`"A: double"`, `"Then: exec"`), with the types
wildcard pins resolved to. The serialized node fields omit pins that match
their template, so these lists are where to find pin names to connect. Pass
`include_pins: false` to leave them out.

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
an edit. Submit graph changes through `buildGraph`. The server
assigns IDs, initializes typed pins, validates the resulting asset, and
automatically arranges the affected graphs. The edit API has no node coordinates
or layout controls.

`searchNodes` without a query or filter lists the domain's node types by
category. For behaviors it adds the host and project events and the component
and struct names that Component and Type settings take. It costs about 1,300
tokens, so read it once instead of guessing names.

A query ranks entries whose name, keywords, pins or expression function start
with every word:
- Nodes, with the expression that writes them: `clamp` returns
  `{"type": "Clamp", "expression": "clamp(Value, Min, Max)", ...}`.
- Configured component nodes: `set health current` returns
  `{"type": "Set Component", "inputs": {"Component": "GameHealth"}, "field":
  "current", "in": ["Exec: exec", "Entity: ecs.entity = Self", "current: int"],
  ...}`. Pass `type` and `inputs` to `add_node`, and name the `field` in
  `inputs` or `connect`.
- Events: `on hit` returns `{"type": "Event", "event_name": "Contact Begin",
  "out": ["Then: exec", "Other: ecs.entity", "Point: CF_V2"]}`, and a project
  event also returns its `Send Event` with `reference_name`.

To extend a graph from a pin, pass its type as `wire_from` (an output you
will wire from) or `wire_to` (an input you will wire into), written as pins
read: `exec`, `float`, `GameHealth`, `array<int>`. Results keep nodes that can
take the wire, ranked like a wire dropped in Studio (exact type, then a
conversion, then a wildcard), and name the pin to connect as `wire_input` or
`wire_output`: `{"query": "destroy", "wire_from": "ecs.entity"}` returns
Destroy Entity with `"wire_input": "Entity"`.

Every result carries a one-sentence `description` of what the node or event
does, from the node's `description`, the host event table or the event asset.

Pins read `name: type`. Behavior search offers component nodes instead of the
generated `Get_<Component>` and `Make_<Type>` nodes they lower to. Pass
`domain: "system"` for System graphs, which use the generated nodes directly and
only the nodes that need no service beyond the ECS world.

Graph metadata can be authored in the same request with `description`,
`owner_type`, and `graph_description`. The response echoes the selected graph
metadata. `owner_type` is useful for behavior assets that expose `Self`; graph
descriptions give functions and event graphs an authoring purpose that catalog
search can surface.

For example, these `buildGraph` arguments create a material:

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
`expose_fields`, `hide_fields`, `bind_input`, `clear_binding`, `create_local`, and
`expand_local`. `create_local` on an output that already has a local renames
that local; without a `name`, a new local takes the name Studio's Name Value
gives it. `expand_local` removes the local and wires its readers to the output
again.

Studio's code actions are operations too, and `ref` names what they create:
- `collapse_to_function` and `collapse_to_macro` take `nodes` (IDs or refs) and
  a `name`, and replace the selection with a call to a new graph. The call and
  the graph both take `ref`.
- `expand_node` replaces a call to one of the asset's own functions or macros
  with a copy of its body.
- `convert_to_expression` turns `nodes` that form a connected, pure calculation
  into one Behavior Expression.
- `promote_to_variable` gives an input a Get of a new variable, or an output a
  Set; `name` defaults to the pin's name.

`add_pin`, `remove_pin` and `set_pin` on a function's or macro's Entry outputs
or Return inputs edit its signature, so every call to it follows: a renamed
input keeps its wires on each call, and a new input shows its `default`.
`set_pin.order` is refused there; remove the pin and add it again.
`set_comment` gives a node a comment, drawn as a bubble above it like
Unreal's node comments: `{"op":"set_comment","node":12,"comment":"Clamp before
the dash"}`. An empty `comment` removes it, and `"hidden": true` shows the
bubble only while the node is hovered; either field alone keeps the other.
Select the owning graph before referring to one of its nodes. Graph selectors
accept IDs, names, or aliases created in the same request. Function calls can
resolve `reference_name` without a numeric graph ID.

Add a Behavior Expression with `add_node` and its `source` text, for example
`{"op":"add_node","type":"Behavior Expression","source":"vec2(x, y)","ref":"motion"}`.
Use `set_expression` with the node selector and new `source` to regenerate it;
valid edits reconcile pins and disconnect incompatible wires, while invalid edits
leave the previous generated graph in place. Inputs match by name, as in
Unreal's Math Expression: a name that stays keeps its wire or binding, a name
that disappears loses it (the result lists it under `unfed_inputs`), and a new
name starts unfed unless it matches a graph local, which it then reads. To
rename an input and keep its wire, use `set_pin` with the expression input and
the new `name`; the source is rewritten to match. Bind an assignment's leftmost
identifier pin with the normal `bind_input` operation; it does not need a special
name.

For `bind_input`, choose `kind: "variable"` with a variable ID/name/alias,
`kind: "component"` with a reflected component name or stable key and optional
`field`, `kind: "delta_time"`, or `kind: "event_output"` with the source
event's `event_node_id` and `event_pin_id`. Omitting `field` binds the whole
component. Event output bindings are scoped to the event activation; Tick's
`DeltaSeconds` payload also supplies the Delta Time context automatically.
These operations use the same graph model as editor bindings and projections.

Read and write components with `Get Component`, `Set Component` and
`Has Component`, attach and detach them with `Add Component` and
`Remove Component`, and build or split structs with `Make Struct` and
`Break Struct` (`inputs: {"Type": "GameTransform"}`). Choose the component with
the `Component` input, by name or stable key, for example
`{"op":"add_node","type":"Set Component","inputs":{"Component":"GameHealth","current":5}}`.
`add_node` applies a `Component` or `Type` input before the pins it creates, so
their order in `inputs` doesn't matter. `Entity` defaults to Self; connect it to address another entity. Set writes its
`Value`, or only the field inputs it shows: naming a settable field in `inputs` or
`connect` shows it, and `split_pin`/`recombine_pin` switch between every field and
`Value`. Get exposes fields with `expose_fields`. These nodes lower to the
generated `Get_<Component>`, `Set_<Component>_<field>`, `Add_<Component>` and
`Make_<Type>` descriptors when the asset runs; see
[component nodes](component-nodes.md).

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

These are asset edits on disk, without an editor undo entry. An open editor
reloads what they change: graph assets and systems at once, prototypes and the
open scene when its runtime stops. A document with unsaved edits in the editor
keeps them and offers Reload instead. Referenced libraries are linked for validation, while
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
512; scene previews accept 64–1024 pixels per side. `searchNodes` can filter by pin type, container, pin kind, and purity.
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
assumptions. Materials read `beat_phase`, `beat_pulse`, and `influence` (linear
RGB at the preview center, fading out at radius 50) in both the image and the
samples, so scene-driven looks can be checked without a running scene.

`optimizeGraph` uses the same optimizer as the editor's Graph Actions menu.
Start with `dry_run: true`: it reports before and after node and edge counts,
folded inputs, removed nodes, local aliases, changed edges, and each skipped
opportunity. Safe folds include component getters, variable reads, `DeltaTime`,
`Self`, and constants when their source, consumer, conversion, fan-out, and
debug state are unambiguous. The native controller validates the optimized
clone before an apply request publishes it with `expected_hash`. With
`math_expressions: true` it first turns math trees into expressions; the result
lists each new expression's source under `expressions` and the graph's local
names under `locals`, so a dry run shows the formulas to review before applying.

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

The repository contains two project roots: Arena3D at the repository root and
Arena2D at `projects/arena2d`, with its own manifest and `content` tree. The
MCP launcher defaults to Arena2D when that project is present, matching the
default Make and editor workflows. Set `CHIBI_ROOT` to select a different
project explicitly. Point the controller or renderer at the project root when
searching, authoring, running, or previewing assets; names are resolved only
within that project.

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
- `ui: [{"target": "hud", "element": "#volume", "event": "Volume", "value": 0.5}]`,
  a player activating an element of a `GameUi` document: the optional `value`
  is stored under the `element` selector for `Get UI Value`, then the
  element's `data-event` is sent to the target entity, as the rendered
  document does.

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
| Player UI | `GameUi` shows `content/ui/<document>.yaml` (RML markup and RCSS) over the game view. `Set UI Text`, `Set UI Class`, `Set UI Visible`, `Set UI Value` and `Focus UI Element` change every element their RCSS selector matches, such as `#score` or `#hull .fill`; `Click UI Element` clicks the first match, and `Move UI Focus` steps focus through the matches, wrapping. `Get UI Value` reads what was last set under the same selector, and a player-changed input under `#id`. An element's `data-event` sends that event to the entity when clicked or, for an input, changed. |
| Save progress | `Read Saved Text`, `Set Saved Text`, `Remove Saved Text`, `Commit Save`, `Discard Save Changes`. Pending changes become durable only on commit. `Commit Save.Failed` supports retry/error UI. |
| Sound | `Play Sound` synthesizes a swept tone/noise cue. Frequency and end frequency: 20–10,000 Hz; duration: 0.01–4 s; volume/noise: 0–1; pan: 0 left, 0.5 center, 1 right. Editor Play and standalone Play mix up to 64 voices. A zero volume mutes a cue. |
| Lighting | `GameHRC2D` authors fractional 2D transport resolution, trace/feedback settings, ambient, intensity, radiance fog, shadow floor, falloff, temporal blending, upscaling and debug view. `GameEnvironment` authors 3D exposure and bloom. |
| Line of sight | `Raycast` returns Found, Entity, Point, Normal and Distance against circles and rotated boxes. Category filtering is independent of collision masks. New colliders and teleports are visible to the query. |
| Fog | One enabled `GameFog` defines origin, 1–256 rows/columns, cell size, color and ambient drift. `drift: 0` removes fog animation. `GameVision` sources reveal nearby cells, blocked by `occluders`. |
| Terrain memory | `GameVision.remember` adds explored terrain. Positive duration creates a shrinking temporary clearing; age starts at zero. `Visibility At` reads Visible/Explored/Obstacle. `GameRenderState.visibility_culled` opts an actor into current-sight culling; `hidden` always hides it. |
| Sensor contacts | `GameSensorContact` stores an observed position, lifetime and color, independently of explored terrain. |

RCSS `dp` units scale with the view height, 720 pixels being one pixel per
`dp`; `GameUi.order` stacks documents. UI events carry no payload, like action
events: name each button's `data-event`, and read an input's value with
`Get UI Value`. Behavior graphs own rules, text and history. While the pointer
is over an interactive element, it does not reach gameplay.

A ui asset reuses another as a widget with `<use ui="Meter" id="hull"/>`: a
div with the use element's attributes (`ui` becomes `data-ui`) around the
widget's markup, whose `<slot/>` holds the use element's children. Widget
RCSS comes before the host's, so the host can restyle it; its `data-event`s
reach the host's entity, and `#hull .fill` selects inside one instance.
Editing a widget reloads every document that uses it.

A project's `content/ui/Theme.rcss` styles all of its UI: unscoped, before
each asset's RCSS, so it holds tokens (`--fg`, `--accent`) and shared control
classes that widgets use. It can `@import "theme/tokens.rcss";` files beside
it, in order and once each, and an edit to any of them restyles. All RCSS,
in themes and ui assets alike, takes Sass-style nested rules with `&` (a
nested `@media` wraps its rules) and `//` comments, and colors take CSS
`color-mix(in srgb | oklab, a [p%], b [p%])`, so shades derive from tokens:
`color-mix(in oklab, var(--accent) 20%, transparent)`. The Studio project's theme is the editor's own, so
its panels look the same in the editor, in the Scene and in a prototype
preview. Prototype previews show a `GameUi` even while it is hidden.

Each asset's RCSS applies only inside it: rules are scoped to the element
showing the asset (`data-ui="<name>"`), and `body` in a selector names that
element, so `body.paused #title` styles a state class set on it. Game UI lets
the pointer through to the game except over `data-event` elements and inputs;
set `pointer-events: auto` where a document should block it, such as a menu's
backdrop.

`data-keys="Enter:Choose Down:Next"` sends the named event when that key is
pressed while focus is on the element or inside it, and stops the key there.
Keys are A-Z, 0-9, Space, Enter, Escape, Tab, Backspace, Delete, Up, Down,
Left, Right, Home, End, PageUp and PageDown. A document focuses its first
`autofocus` element when it is shown. RmlUi gives `:focus` to every element on
the way to the focused one, so pick the focused row with a class, such as
`.row:focus`.

An element with `data-setting="key"` shows and edits that saved key (the
same store as `Read Saved Text`): a checkbox holds `true` or `false`, an
element with a `value` attribute is an option that is checked while the key
holds that value, and other inputs hold their value. Changes stay pending
until a behavior commits the save.

The player shows `content/ui/PauseMenu.yaml` while paused. Its `body` has the
class `paused`, `cleared` or `defeated`, and its `Resume`, `Restart` and `Quit`
events control the session.

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
authored camera and UI documents, with pixel dimensions independent of display DPI.
This tool needs GPU/display access. Returned images include the UI; structured
JSON omits the base64 bytes. For visual fixtures, explicitly author material
emission rather than relying on the emissive appearance fallback.

Catalog, search, and asset-list responses accept a numeric `limit` and `cursor`,
and return `total`, `count`, `has_more`, and `next_cursor`. Catalog and search
also accept `summary: true` to omit relationship arrays from each asset record.

The launcher selects the Arena2D project by default when it is present and
honors an explicit `CHIBI_ROOT`. It sets the controller path from its own
location. When launching `server.mjs` directly outside the repository, set
`CHIBI_ROOT` or `CHIBI_CTL` to an explicitly built controller executable.

`make test` includes the MCP protocol and native-controller smoke checks,
including `design-smoke.mjs`, which builds independent fixtures entirely through
MCP. `CHIBI_KEEP_DESIGN_FIXTURE=1` retains them; `CHIBI_MCP_GPU=1` also renders
2D/3D fog, fitting and dock previews. `--game-capabilities` runs native save,
visibility and sound checks. Set
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
