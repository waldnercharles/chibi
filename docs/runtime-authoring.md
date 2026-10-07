# Author a game with Chibi

The game's `project.kdl` selects its `content` directory and starting scene.
Author scenes, prototypes, behaviors, events, shaders, and other assets there.
The complete runtime lives in the game's ignored `.chibi/runtime` directory.
Run these commands from the game root.

```sh
./.chibi/runtime/bin/chibi --play --project-root "$PWD"
./.chibi/runtime/bin/chibi_ctl --project-root "$PWD" --format --check
./.chibi/runtime/bin/chibi-mcp --setup "$PWD"
```

MCP requires Node.js 22 or newer. Setup registers the game-local launcher in
`.codex/config.toml`. Start its stdio server directly with:

```sh
./.chibi/runtime/bin/chibi-mcp "$PWD"
```

The server reads newline-delimited JSON-RPC on stdin and writes responses on
stdout. It uses the controller in this runtime package.

## Author and verify assets

Use the MCP's current tool catalog and schemas to choose operations.

1. Call `getProjectStatus` to confirm the project. Use `searchAssets` and
   `readAsset` to locate existing content; inspect graphs with `inspectGraph`.
2. Use the matching `build*` tool to author an asset. Graph edits specify nodes,
   values, and connections; automatic arrangement supplies node positions.
   Include `expected_hash` when editing existing content.
3. Call `validateAsset` after each change. Resolve loader or compiler diagnostics
   before running it.
4. Use `previewShader` or `previewScene` to inspect the result. Run a bounded
   deterministic test with `runScenario`; inspect its `behavior_failures` and
   `world_hash`. `captureRuntimeSnapshot` supplies entity and component state.

Keep game rules, scene composition, visual choices, and tuning in authored
assets. After editing code assets by hand, format and check it:

```sh
./.chibi/runtime/bin/chibi_ctl --project-root "$PWD" --format
./.chibi/runtime/bin/chibi_ctl --project-root "$PWD" --format --check
```

## Runtime identity and capability requests

`.chibi-engine.json` records the selected build ID and project format version.
The MCP checks that identity before serving the project. Runtime delivery
updates the local package and pin together; new launches use the new package.
Obtain its identity with:

```sh
./.chibi/runtime/bin/chibi_ctl --engine-info
```

When the public catalog cannot express a behavior, prepare a capability request.
Include the runtime build ID, a minimal reproducing scene or scenario, expected
and actual behavior, and validation or capture evidence. Keep that reproduction
in the game project so the delivered capability can be verified there.
