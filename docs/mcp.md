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
- `read_asset` — parsed authored JSON.
- `validate_asset` — native asset loading and compiler diagnostics.
- `describe_registry` — reflected nodes, pins, types, services, operators, and components.

The native controller also accepts newline-delimited JSON directly:

```sh
printf '%s\n' '{"contract":1,"operation":"status"}' |
  build/blueprints_ctl --project-root .
```

Set `CBLUEPRINTS_ROOT` when launching the MCP adapter outside the repository,
or set `CBLUEPRINTS_CTL` to an explicitly built controller executable.

The adapter is intentionally local and read-only in this first slice. Typed
edits, deterministic run artifacts, and public registry metadata can build on
the same controller contract without adding arbitrary shell or file access.
