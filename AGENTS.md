# Chibi

## Commands and checks

- Build with `make build` and test with `make test`, which is the gate before a change is done. For one focused check, use `make test-fast TEST_ARGS=--<selector>`. `make help` lists the other targets.
- After hand-editing asset YAML, run `build/chibi_ctl --project-root <root> --format`; `make test` fails on non-canonical assets.
- Ask before changing existing tests unless the task already covers it. Never weaken an assertion to hide a failure.

## Architecture

- Shared source implements components, nodes, functions, and systems reusable across games. Authored assets own scene composition, game rules, visual choices, and tuning.
- Name shared modules for their capability. Turn game-specific geometry, camera choreography, and outcome handling into authored data and behaviors; add native capabilities when authoring exposes a concrete gap.
- Validate an engine generalization with another scene or prototype that uses different content and omits the motivating game's mechanics.
- Keep reusable gameplay features optional so scenes can use rendering, physics, and input independently of combat, encounters, or rhythm.
- Keep debugging UI, capture/scenario tooling, and MCP adapters in clearly named modules and separate build targets where practical. Tooling consumes explicit runtime inspection/control APIs; shared runtime code stays independent of tooling and editor UI.
- When contracts change, migrate our assets directly and remove obsolete implementations; backward compatibility and legacy aliases are not required. When the change stops projects saved by earlier builds from loading, bump `CHIBI_PROJECT_VERSION` in `src/assets/project.h` and the `version` in every `project.yaml`.
- Build navigation and refactoring features (find references, rename, go to definition, call hierarchy, hover) once, outside `src/editor/`, and ship each in both Studio and the MCP. `docs/authoring-navigation.md` tracks their status.

## Authoring

- Chibi MCP is the default authoring interface for shaders, graphs, prototypes, scenes, and other authored assets; `docs/mcp.md` is its catalog.
- MCP graph edits express nodes, values, and connections, and the MCP auto-arranges the affected graphs, so agents never choose node positions.
- When authoring, reviewing, or cleaning up a behavior, shader, or field graph, follow `.claude/skills/chibi-graph-legibility/SKILL.md`.

## C style

- Write C, never C++. The only exceptions are pass-through shims to C++ APIs: `src/ui/rml.cpp` (RmlUi), `src/editor/text/zep_bridge.cpp` (Zep) and `test/draw_backend.cpp` (Cute's C++-linkage draw diagnostics). They only convert types and forward calls, so all logic stays in C.
- Follow YAGNI and prefer boring solutions. Keep code small: remove dead code and abstractions; use small functions, narrow APIs, few parameters, guards, and no `else` after `return` or `break`.
- Fail fast: `assert` invalid state; use `xmalloc`/`xrealloc`; fix pointer bugs at the source; avoid defensive `NULL` checks that mask defects.
- Types: `int` for domain values/indices; `size_t` for sizes; unsigned for bits; `bool` for booleans.
- Signatures: `f()` for no parameters; omit unused parameter names.
- Vectors: `support/array.h` (`apush`, `acount`, `afree`, `asetlen`).
- Includes: `"project.h"` for project code; `<system.h>` otherwise.
- Keep ownership and lifetimes explicit; mark file-private symbols `static`.
- Prefer direct calls and `switch` statements to function pointers.
- Follow existing names and clang-format; keep short names local; name constants and invariants with `#define`, `enum`, or `static const`.

## Editor UI

- The editor UI is RmlUi driven from C through the Studio kit (`src/editor/ui/kit.h`); its design is in `docs/prototypes/studio/DESIGN.md`. The behavior graph editor follows `docs/editor-graph-references.md`.
- Before building a UI mechanism in a panel, find the problem in `docs/editor-ui-references.md` and use the kit primitive. If it is missing, add it to the kit following the reference library's contract. The doc's "Common mismatches" section lists the usual misses: hand-built toasts, popups, motion, class strings, and long lists.
- Verify UI changes in the web build (`make web`).

## Working preferences

- Complete the requested task at its intended scope. Make routine choices yourself, and ask only when readings of the request would lead to materially different work. If the request looks mistaken or a better approach exists, say so in a sentence and continue as asked.
- Preserve unrelated changes. Read the relevant code and its callers before editing.
- Remembered third-party APIs may be out of date. Before writing code or configuration against a dependency, read the pinned version: `third_party/`, or the FetchContent sources in `build/_deps/<name>-src/` (tags in `CMakeLists.txt` and `cmake/*.cmake`). Fall back to that version's official documentation.
- Use plain language and lead with the outcome. Keep replies brief, with enough evidence to support the result and any blocker.
- Size written documents to their substance, without filler sections, repeated summaries, or boilerplate.
- For work spanning sessions, keep open items and blockers in the task list or the relevant plan in `docs/`, not in this file.
