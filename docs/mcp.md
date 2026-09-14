# Local MCP bridge

The project exposes a local MCP server backed by the native `chibi_ctl`
controller. The controller uses the generated reflection registry and the
same asset loaders and compiler used by the editor.

## Start the server in a terminal

Run this from the project root:

```sh
make mcp
```

The target builds `chibi_ctl` and the separate GPU tool `chibi_render_ctl`, then starts the
Node.js adapter in the foreground. The adapter uses stdin and stdout for
newline-delimited JSON-RPC. It does not open a port. The readiness message is
written to stderr so it does not corrupt the protocol stream.

Paste these lines into the running terminal to perform a live MCP handshake,
list the available tools, and ask for project status:

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"manual-terminal","version":"0.1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"project_status","arguments":{}}}
```

The server prints one response for each request that has an `id`. The
notification has no response. Press Ctrl-D to send EOF and stop the server, or
Ctrl-C to interrupt it.

For a repeatable one-shot check, send the same protocol messages through a
pipe. The build output appears before the JSON-RPC responses:

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"manual-terminal","version":"0.1"}}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"project_status","arguments":{}}}' |
  make mcp
```

The checked-in `.mcp.json` starts the adapter from the project root for MCP
hosts that support project configuration. The adapter provides these tools:

- `project_status`: project, asset, and reflection counts.
- `list_assets`: deterministic asset paths, sizes, and hashes.
- `catalog`: semantic asset metadata, dependency edges, diagnostics, and registry data.
- `search_assets`: find catalog entries by kind, name, path, or description.
- `asset_dependencies`: inspect incoming or outgoing direct and transitive dependencies.
- `inspect_graph`: inspect one selected graph and a compact list of the asset's other graphs, with nodes, pins, edges, calls, effects, and diagnostics. Layout fields are omitted by default; pass `include_layout: true` only when inspecting the editor view.
- `inspect_system`: inspect a typed system query, compiled graph, inferred component effects, and diagnostics.
- `run_scenario`: run a bounded deterministic prototype scenario and return runtime evidence.
- `runtime_snapshot`: capture bounded, ordered ECS entities, reflected fields, behaviors, and timing.
- `record_scenario`: record bounded normalized inputs, events, and per-frame hashes for a prototype.
- `replay_scenario`: replay an inline recording or a safe recording artifact and report divergence.
- `read_asset`: read one authored asset as parsed JSON.
- `validate_asset`: run native asset loading and compiler diagnostics.
- `describe_registry`: inspect reflected nodes, pins, types, services, operators, and components.
- `describe_contract`: inspect supported operations, input fields, response versions, and safety annotations.
- `search_nodes`: find a bounded page of node types and typed pins by query and behavior/shader domain.
- `edit_graph`: create or update a graph asset using a validated batch of semantic operations, short node aliases, and named pins.
- `preview_shader`: compile a material or shape on the GPU and return a PNG image, diagnostics, and its source hash. Generated GLSL is opt-in.

The client can also read these MCP resources:

- `chibi://project/status` and `chibi://catalog` for stable project summaries.
- `chibi://asset/{kind}/{name}` for parsed authored JSON.
- `chibi://asset/{kind}/{name}/dependencies` for dependency relationships.
- `chibi://asset/{kind}/{name}/graph/{graph_id}` for one normalized graph.
- `chibi://recording/{id}` for a generated deterministic recording.

All tools report safety metadata through `describe_contract`. They do not
provide arbitrary shell commands, network access, editor UI control, or access
to files outside the project data needed by the native controller. `edit_graph`
writes authored graph assets after validation and an expected-hash check. It is
marked as a modifying tool. `record_scenario` writes only its bounded generated
recording artifact under `/tmp`, and `replay_scenario` accepts only a returned
recording or a safe generated recording path.

For graph work, the agent should reason from node identity, pins, edges, calls,
effects, and diagnostics. `inspect_graph` omits node coordinates, inline UI
hints, comment-box positions, and wire routing by default. Pass
`include_layout: true` when those editor details are needed. Placement is
presentation data; the graph arranger assigns coordinates after graph content
changes, so an agent does not need to choose node positions by hand.

## Author a graph or shader

Use `search_assets` to find an asset, `inspect_graph` to read its graph and
current hash, and `search_nodes` to fetch the few node signatures needed for
an edit. Submit related changes together through `edit_graph`. The server
assigns IDs, initializes typed pins, validates the resulting asset, and
automatically arranges the affected graphs. The edit API has no node coordinates
or layout controls.

For example, these `edit_graph` arguments create a material:

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
Existing assets require the exact `expected_hash` returned by inspection or
the previous edit. A stale hash returns `asset_conflict`; inspect again before
constructing a replacement batch. `create: true` refuses to overwrite an asset.

Pass `dry_run: true` to validate and obtain predicted hashes without saving.
A failed operation or validation leaves the authored file unchanged. Successful
batches publish one complete file, and concurrent MCP writes to the same asset
are serialized. The hash is checked again just before publication; this is
not a locking protocol shared with unrelated filesystem writers.

These are offline asset edits. They do not modify an open editor document or
add an editor undo entry; save or close unsaved editor work before editing the
same asset through MCP. Referenced libraries are linked for validation, while
the saved file retains its authored library references.

Call `preview_shader` with `{"kind":"material","name":"WarmSurface"}` to
inspect the result. It returns a PNG image and compact diagnostics, with image
bytes excluded from the accompanying structured JSON. Material and shape
previews use the production renderer in a separate native process. They
require a working graphics backend; on Linux, use a display or Xvfb. Shader
libraries are validated as reusable functions; preview a material or shape
that consumes the library to see its effect.

Requests are bounded: node search returns at most 50 entries, edit batches
contain at most 128 operations, and preview dimensions are at most 512 by 512.
Preview time is explicit and deterministic. Generated GLSL is available with
`include_source: true` and is limited to 64 KiB. MCP cancellation and the
preview timeout terminate the GPU child process.

The editor's Graph Actions menu includes `Optimize graph`. It currently folds a
pure component getter into a `Self.Component` or `Self.Component.field` input
when that getter has one compatible consumer and no debug state or fan-out.
New connections that meet the same conditions are folded immediately; shared
getters and other ambiguous cases remain explicit.

The stdio adapter negotiates MCP protocol versions `2025-06-18` and
`2025-11-25`, keeps notifications response-free, and advertises the dynamic
`chibi://asset/{kind}/{name}` and
`chibi://asset/{kind}/{name}/dependencies` and
`chibi://asset/{kind}/{name}/graph/{graph_id}` and
`chibi://recording/{id}` resource templates through
`resources/templates/list`. Recording IDs are bounded opaque identifiers; the
recording resource is resolved by the controller so clients never need direct
filesystem access. The static `chibi://catalog` resource provides the
same semantic catalog as the `catalog` tool. Tool responses include a compact
text summary for conversational clients and the complete native response in
`structuredContent` for machine use.

The native controller also accepts newline-delimited JSON directly:

```sh
printf '%s\n' '{"contract":1,"operation":"status"}' |
  build/chibi_ctl --project-root .
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

`run_scenario` currently accepts prototype targets, runs at most 600 fixed steps,
and returns the resolved asset identity, frame count, timestep, world hash, live
entity count, and engine timing fields. The fixed timestep must match the
engine's `1/60` second step. Each request creates an isolated preview runtime;
the returned world hash can be compared across repeated calls to check
deterministic behavior.

Recorded scenario inputs use `movement` and `aim` samples plus an `actions`
array of `{ "name": string, "pressed": boolean }` records. Action names are
resolved against the attached behavior graphs, so unknown actions are rejected
instead of being silently ignored.

`runtime_snapshot` uses the same isolated preview runtime and returns ordered
entity and component records with reflected field values, attached behavior
state, and timing data. `entity_limit`, `component_limit`, `field_limit`, and
`behavior_limit` bound the response; `fields` can restrict reflected field
selection. The operation is read-only and does not write authored content.

`record_scenario` uses the same fixed-step runtime path while accepting at most
600 frames of normalized input. Each frame may include bounded `movement` and
`aim` vectors with `x` and `y` values in `[-1, 1]`, at most two named digital
action transitions, and up to 32 named zero-payload events. Aim vectors are
dispatched through the optional `Aim` behavior event; event payload values and
analog action values are rejected until the runtime exposes a stable typed
input seam. The result includes a versioned recording with contract and
content hashes, normalized inputs, events, fixed timing, and per-frame world
hashes. The native controller writes the recording artifact outside authored
content and returns its bounded `recording_id` and path. Clients can inspect a
recording through `chibi://recording/{id}` without opening a filesystem
path. Repeated recordings of unchanged content and inputs should have the same
deterministic hashes and recording payload.

`replay_scenario` accepts exactly one of the returned recording object or its
bounded `recording_path`. It validates the recording and content revisions
before running the same headless fixed-step path. A matching replay reports
deterministic frame and world hashes; a mismatch reports the first divergent
frame and selected state differences. Recording format and contract revisions
are checked explicitly so incompatible artifacts fail with a useful error.

Catalog, search, and asset-list responses accept a numeric `limit` and `cursor`,
and return `total`, `count`, `has_more`, and `next_cursor`. Catalog and search
also accept `summary: true` to omit relationship arrays from each asset record.

Set `CHIBI_ROOT` when launching the MCP adapter outside the repository,
or set `CHIBI_CTL` to an explicitly built controller executable.

`make test` includes the MCP protocol and native-controller smoke checks.

The adapter is intentionally local and read-only in this first slice. Typed
edits, deterministic run artifacts, and public registry metadata can build on
the same controller contract without adding arbitrary shell or file access.
