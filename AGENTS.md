- When contracts change, migrate our assets directly and remove obsolete implementations. We are the only consumer; backward compatibility and legacy aliases are not required.
- Follow YAGNI principles, and boring solutions.
- Shared source implements components, nodes, functions, and systems reusable across games. Authored assets own scene composition, game rules, visual choices, and tuning.
- Name shared modules for their capability. Turn game-specific geometry, camera choreography, and outcome handling into authored data and behaviors; add native capabilities when authoring exposes a concrete gap.
- Keep reusable gameplay features optional so scenes can use rendering, physics, and input independently of combat, encounters, or rhythm.
- Keep debugging UI, capture/scenario tooling, and MCP adapters in clearly named modules and separate build targets where practical. Tooling consumes explicit runtime inspection/control APIs; shared runtime code stays independent of tooling and editor UI.
- MCP graph edits express nodes, values, and connections; automatically arrange affected graphs so agents never need to choose node positions.
- When Chibi MCP is available, use it as the default authoring interface for shaders, behavior and event graphs, prototypes, scenes, and authored assets. Search or inspect first, then use the matching `build*` tool, validate, and preview or run as needed. See `docs/mcp.md` for the public catalog and workflow.
- After hand-editing asset YAML, run `build/chibi_ctl --project-root <root> --format`; `make test` fails on non-canonical assets.
- Keep app UI free of tutorial and explanatory text. Use concise labels and values; do not add descriptive copy to menus, controls, graphs, or legends.
- Validate engine generalizations with another scene or prototype that uses different content and omits the motivating game's mechanics.
- Prefer `make build` when building, and `make test` to test.
- Keep code small: remove dead code/abstractions; use small functions, narrow APIs, few parameters, guards, and no `else` after `return` or `break`.
- Fail fast: `assert` invalid state; use `xmalloc`/`xrealloc`; fix pointer bugs at the source; avoid defensive `NULL` checks that mask defects.
- Types: `int` for domain values/indices; `size_t` for sizes; unsigned for bits; `bool` for booleans.
- Signatures: `f()` for no parameters; omit unused parameter names.
- Vectors: `support/array.h` (`apush`, `acount`, `afree`, `asetlen`).
- Includes: `"project.h"` for project code; `<system.h>` otherwise.
- Keep ownership/lifetimes explicit; mark file-private symbols `static`.
- Prefer direct calls and `switch` statements to function pointers.
- Follow existing names and clang-format; keep short names local; name constants and invariants with `#define`, `enum`, or `static const`.

Prefer Behavior Expression nodes for small calculations, assignments, and
boolean or branch conditions when their generated pins keep dependencies and
effects clear.

Use explicit graph structure for larger control-flow blocks, sequencing, and
stateful logic that benefits from step-by-step inspection.
