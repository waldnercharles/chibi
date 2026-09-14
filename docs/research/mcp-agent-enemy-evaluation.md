# MCP agent authoring and enemy evaluation

Date: 2026-09-11

## Result

Chibi can author and validate behavior, material, and shape graphs, inspect their topology and static effects, preview shaders, author prototype and scene composition, and run bounded deterministic prototype or scene scenarios. It also exposes per-behavior timing samples, explicit execution backend status, and a shared graph optimizer.

The experiment created:

- `PhaseWardenBehavior`: finds the nearest player and moves around the player on a rhythm-driven orbit.
- `PhaseWardenMaterial`: dark blue body color with cyan emission.
- `PhaseWardenShape`: a centered ring SDF.
- `PhaseWarden.json`: a prototype that attaches the behavior and visual assets.

The first three assets were created with `edit_graph`. The prototype was added as JSON in the initial experiment because no prototype authoring operation existed then. `edit_prototype` and `edit_scene` now cover component values, entity composition, overrides, and behavior attachments. Chibi validated the prototype with zero errors and warnings, resolved all seven referenced assets, and ran a 60-frame snapshot. The isolated prototype has no player entity, so the behavior takes its no-target branch and leaves velocity at zero.

## Authoring observations

- Graph editing is safer than hand-editing graph JSON because operations validate references, preserve content hashes, and auto-arrange layout.
- A variable type repair removed its connected edges. The graph became valid only after reconnecting those edges explicitly.
- Shader wildcard connections need an explicit vector or struct type. The MCP now requires `connect.type` for wildcard shader pins and reports guidance when a shader conversion would be implicit.
- Shape preview exposed an undocumented coordinate assumption: raw UV coordinates placed the ring at a corner until a mapping node shifted the origin to the center.
- `edit_prototype` covers one prototype's components and behavior attachments, while `edit_scene` covers named multi-entity fixtures. `describe_components` provides compact field schemas and native defaults.
- `optimize_graph` shares the editor's safe folding and alias passes, reports skipped reasons, and supports dry runs before publication. It is a structural optimizer, not a replacement for runtime profiling.

## Runtime and JIT findings

`runtime_snapshot` and `run_scenario` report frame count, fixed timestep, simulation steps, simulation time, entity state, and a world hash. `profile_scenario` adds aggregated engine timing, per-behavior inclusive and exclusive CPU samples, and scoped xalloc allocation counters after optional warmup. Broader allocator coverage and native behavior compilation remain future work.

The current arena behavior path is interpreted. `behavior_asset_compile` validates graphs and effects; it does not emit a behavior program. The system backend has a separate graph-program path with SLJIT support, and `execution_status` now reports the backend selected by a temporary live system. Behavior assets still report the interpreter until they have a compiled behavior runtime.

## Useful MCP additions

Implemented follow-ups:

1. Scene authoring and deterministic multi-entity fixtures, building on `edit_prototype` for each entity's component values and behavior attachments.
2. `profile_scenario` with warmup, aggregated engine timing, behavior CPU samples, invocation counts, and backend context.
3. `execution_status` returning interpreter, bytecode, typed bytecode, or SLJIT state plus validation and runtime diagnostics.
4. `optimize_graph` as a dry-run first operation that reports removed nodes, folded values, local aliases, changed edges, and skipped reasons.
5. Semantic graph metadata and typed pin filters in `edit_graph` and `search_nodes`.

Remaining follow-ups:

1. Shader preview controls for visual UV mapping and a production geometry test grid. Named uniform overrides, camera framing, and a CPU UV sample grid are now supported.
2. Compiled behavior execution and broader allocator coverage beyond the xalloc wrappers.

## mcporter

MCPorter would be useful as a secondary client for CI and external scripts. It can discover and call a stdio server, generate a typed TypeScript client or CLI, and record/replay JSON-RPC traffic. That could make Chibi smoke tests and regression fixtures easier to run outside Codex.

It would not add prototype authoring, profiling, JIT inspection, or graph semantics. Inside Codex it would add a client boundary around an MCP server that is already directly available. It is therefore best treated as an optional development and CI harness, not a runtime dependency or a fix for Chibi's missing capabilities.

Sources:

- [Chibi MCP guide](../mcp.md)
- [Behavior execution audit](behavior-execution.md)
- [Arena development and profiling notes](../arena-development.md)
- [MCPorter](https://github.com/openclaw/mcporter)
- [MCPorter ad-hoc stdio usage](https://github.com/openclaw/mcporter/blob/main/docs/adhoc.md)
- [MCPorter record and replay](https://github.com/openclaw/mcporter/blob/main/docs/record-replay.md)
