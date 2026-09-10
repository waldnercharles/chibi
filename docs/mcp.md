# Local MCP bridge

The project exposes a local, read-only MCP server backed by the native
`blueprints_ctl` controller. The controller uses the generated reflection
registry and the same asset loaders/compiler used by the editor.

Build the native controller first:

```sh
make build
```

The checked-in `.mcp.json` starts the MCP adapter from the project root. The
adapter provides these tools:

- `project_status` — project, asset, and reflection counts.
- `list_assets` — deterministic asset paths, sizes, and hashes.
- `catalog` — semantic asset metadata, dependency edges, diagnostics, and registry data.
- `search_assets` — find catalog entries by kind, name, path, or description.
- `asset_dependencies` — inspect incoming or outgoing direct and transitive dependencies.
- `inspect_graph` — inspect normalized graph nodes, pins, edges, calls, effects, and diagnostics.
- `inspect_system` — inspect a typed system query, compiled graph, inferred component effects, and diagnostics.
- `run_scenario` — run a bounded deterministic prototype scenario and return runtime evidence.
- `runtime_snapshot` — capture bounded, ordered ECS entities, reflected fields, behaviors, and timing.
- `record_scenario` — record bounded normalized inputs, events, and per-frame hashes for a prototype.
- `replay_scenario` — replay an inline recording or a safe recording artifact and report divergence.
- `read_asset` — parsed authored JSON.
- `validate_asset` — native asset loading and compiler diagnostics.
- `describe_registry` — reflected nodes, pins, types, services, operators, and components.

The stdio adapter negotiates MCP protocol versions `2025-06-18` and
`2025-11-25`, keeps notifications response-free, and advertises the dynamic
`blueprints://asset/{kind}/{name}` and
`blueprints://asset/{kind}/{name}/dependencies` and
`blueprints://asset/{kind}/{name}/graph/{graph_id}` and
`blueprints://recording/{id}` resource templates through
`resources/templates/list`. Recording IDs are bounded opaque identifiers; the
recording resource is resolved by the controller so clients never need direct
filesystem access. The static `blueprints://catalog` resource provides the
same semantic catalog as the `catalog` tool. Tool responses include both a text
rendering and structured JSON content.

The native controller also accepts newline-delimited JSON directly:

```sh
printf '%s\n' '{"contract":1,"operation":"status"}' |
  build/blueprints_ctl --project-root .
```

For example, search the catalog and inspect a graph's asset dependencies:

```sh
printf '%s\n' '{"contract":1,"operation":"search_assets","query":"Health"}' |
  build/blueprints_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"asset_dependencies","kind":"behavior","name":"HealthBehavior","direction":"both","transitive":true}' |
  build/blueprints_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"inspect_graph","kind":"library","name":"DirectionMath","graph_name":"Direction Orientation"}' |
  build/blueprints_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"inspect_system","kind":"system","name":"MovementSystem"}' |
  build/blueprints_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"run_scenario","kind":"prototype","name":"Player","frames":60,"fixed_dt":0.0166666666666667}' |
  build/blueprints_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"runtime_snapshot","kind":"prototype","name":"Player","frames":2,"entity_limit":32,"component_limit":16,"field_limit":32}' |
  build/blueprints_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"record_scenario","kind":"prototype","name":"Player","frames":60,"fixed_dt":0.0166666666666667,"inputs":[{"movement":{"x":1,"y":0},"aim":{"x":0,"y":1},"actions":[{"name":"fire","pressed":true}]}]}' |
  build/blueprints_ctl --project-root .
```

`run_scenario` currently accepts prototype targets, runs at most 600 fixed steps,
and returns the resolved asset identity, frame count, timestep, world hash, live
entity count, and engine timing fields. The fixed timestep must match the
engine's `1/60` second step. Each request creates an isolated preview runtime;
the returned world hash can be compared across repeated calls to check
deterministic behavior.

`runtime_snapshot` uses the same isolated preview runtime and returns ordered
entity and component records with reflected field values, attached behavior
state, and timing data. `entity_limit`, `component_limit`, `field_limit`, and
`behavior_limit` bound the response; `fields` can restrict reflected field
selection. The operation is read-only and does not write authored content.

`record_scenario` uses the same fixed-step runtime path while accepting at most
600 frames of normalized input. Each frame may include bounded `movement` and
`aim` vectors with `x` and `y` values in `[-1, 1]`, plus at most two named action
transitions. The result includes a versioned recording with contract and
content hashes, normalized inputs, events, fixed timing, and per-frame world
hashes. The native controller writes the recording artifact outside authored
content and returns its bounded `recording_id` and path. Clients can inspect a
recording through `blueprints://recording/{id}` without opening a filesystem
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

Set `CBLUEPRINTS_ROOT` when launching the MCP adapter outside the repository,
or set `CBLUEPRINTS_CTL` to an explicitly built controller executable.

`make test` includes the MCP protocol and native-controller smoke checks.

The adapter is intentionally local and read-only in this first slice. Typed
edits, deterministic run artifacts, and public registry metadata can build on
the same controller contract without adding arbitrary shell or file access.
