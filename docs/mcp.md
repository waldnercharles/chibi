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
- `read_asset` — parsed authored JSON.
- `validate_asset` — native asset loading and compiler diagnostics.
- `describe_registry` — reflected nodes, pins, types, services, operators, and components.

The stdio adapter negotiates MCP protocol versions `2025-06-18` and
`2025-11-25`, keeps notifications response-free, and advertises the dynamic
`blueprints://asset/{kind}/{name}` and
`blueprints://asset/{kind}/{name}/dependencies` and
`blueprints://asset/{kind}/{name}/graph/{graph_id}` resource templates through
`resources/templates/list`. The static `blueprints://catalog` resource provides
the same semantic catalog as the `catalog` tool. Tool responses include both a
text rendering and structured JSON content.

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
```

`run_scenario` currently accepts prototype targets, runs at most 600 fixed steps,
and returns the resolved asset identity, frame count, timestep, world hash, live
entity count, and engine timing fields. The fixed timestep must match the
engine's `1/60` second step. Each request creates an isolated preview runtime;
the returned world hash can be compared across repeated calls to check
deterministic behavior.

Catalog, search, and asset-list responses accept a numeric `limit` and `cursor`,
and return `total`, `count`, `has_more`, and `next_cursor`. Catalog and search
also accept `summary: true` to omit relationship arrays from each asset record.

Set `CBLUEPRINTS_ROOT` when launching the MCP adapter outside the repository,
or set `CBLUEPRINTS_CTL` to an explicitly built controller executable.

`make test` includes the MCP protocol and native-controller smoke checks.

The adapter is intentionally local and read-only in this first slice. Typed
edits, deterministic run artifacts, and public registry metadata can build on
the same controller contract without adding arbitrary shell or file access.
