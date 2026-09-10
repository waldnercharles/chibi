# Local MCP bridge

The project exposes a local, read-only MCP server backed by the native
`chibi_ctl` controller. The controller uses the generated reflection
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
- `read_asset` — parsed authored JSON.
- `validate_asset` — native asset loading and compiler diagnostics.
- `describe_registry` — reflected nodes, pins, types, services, operators, and components.

The stdio adapter negotiates MCP protocol versions `2025-06-18` and
`2025-11-25`, keeps notifications response-free, and advertises the dynamic
`chibi://asset/{kind}/{name}` and
`chibi://asset/{kind}/{name}/dependencies` and
`chibi://asset/{kind}/{name}/graph/{graph_id}` resource templates through
`resources/templates/list`. The static `chibi://catalog` resource provides
the same semantic catalog as the `catalog` tool. Tool responses include both a
text rendering and structured JSON content.

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
printf '%s\n' '{"contract":1,"operation":"run_scenario","kind":"prototype","name":"Player","frames":60,"fixed_dt":0.0166666666666667}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"runtime_snapshot","kind":"prototype","name":"Player","frames":2,"entity_limit":32,"component_limit":16,"field_limit":32}' |
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

Catalog, search, and asset-list responses accept a numeric `limit` and `cursor`,
and return `total`, `count`, `has_more`, and `next_cursor`. Catalog and search
also accept `summary: true` to omit relationship arrays from each asset record.

Set `CHIBI_ROOT` when launching the MCP adapter outside the repository,
or set `CHIBI_CTL` to an explicitly built controller executable.

`make test` includes the MCP protocol and native-controller smoke checks.

The adapter is intentionally local and read-only in this first slice. Typed
edits, deterministic run artifacts, and public registry metadata can build on
the same controller contract without adding arbitrary shell or file access.
