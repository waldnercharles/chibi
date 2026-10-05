## Cloud session startup

In the Debian workspace cloud environment (`/workspace/.cloud-tools`), normal
Make commands and direct MCP launch automatically run repository preflight before
project work. Use `make cloud-preflight` to report readiness and `make cloud-smoke`
for bounded compiler checks. For other tool calls use
`tools/cloud/bin/start_skill -- <command> [args...]`, adding `--modeling` before
Blender work. `CHIBI_CLOUD=0` opts Make/MCP out. Missing setup stops with recovery
instructions. This does not activate arbitrary platform shells; see
[cloud setup](docs/cloud-setup.md) for supported entry points and publication limits.

- When contracts change, migrate our assets directly and remove obsolete implementations; backward compatibility and legacy aliases are not required. When the change stops projects saved by earlier builds from loading, bump `CHIBI_PROJECT_VERSION` in `src/assets/project.h` and the `version` in every `project.kdl`.
- Follow YAGNI principles, and boring solutions.
- Shared source implements components, nodes, functions, and systems reusable across games. Authored assets own scene composition, game rules, visual choices, and tuning.
- Name shared modules for their capability. Turn game-specific geometry, camera choreography, and outcome handling into authored data and behaviors; add native capabilities when authoring exposes a concrete gap.
- Keep reusable gameplay features optional so scenes can use rendering, physics, and input independently of combat, encounters, or rhythm.
- Keep debugging UI, capture/scenario tooling, and MCP adapters in clearly named modules and separate build targets where practical. Tooling consumes explicit runtime inspection/control APIs; shared runtime code stays independent of tooling and editor UI.
- MCP graph edits express nodes, values, and connections; automatically arrange affected graphs so agents never need to choose node positions.
- When Chibi MCP is available, use it as the default authoring interface for shaders, behavior and event graphs, prototypes, scenes, and authored assets. Search or inspect first, then use the matching `build*` tool, validate, and preview or run as needed. See `docs/mcp.md` for the public catalog and workflow.
- Graph legibility: when authoring, reviewing, or cleaning up a behavior, shader, or field graph, follow `.claude/skills/chibi-graph-legibility/SKILL.md`.
- After hand-editing asset KDL, run `build/chibi_ctl --project-root <root> --format`; `make test` fails on non-canonical assets.
- Navigation and refactoring (find references, rename, go to definition, call hierarchy, hover): build each once outside `src/editor/` and ship it in both Studio and the MCP. `docs/authoring-navigation.md` tracks each feature's status.
- Validate engine generalizations with another scene or prototype that uses different content and omits the motivating game's mechanics.
- For routine agent iteration, use `make agent-list` and `make agent-test AGENT_SELECTOR=--<selector>`. For a relevant GPU assertion, explicitly use `make agent-gpu-test` with a listed selector. Inspect actual capabilities with `python3 tools/agent.py doctor`; do not infer hardware from the host name. See `docs/agent-execution.md` for preview/MP4 budgets and truthful coverage reporting. Routine previews/captures use the native hidden-window controller and exit after saving; do not launch Studio. If a display context is required, use the bounded Xvfb command and report it as described there. Headless rendering still spends GPU resources. Do not reconstruct expensive suites through repeated one-frame previews.
- CUT62 permits focused checks for PR iteration and admission to a reviewed integration batch. One named integration owner pins fresh main, every reviewed source head, and the immutable combined commit/tree, then runs full required verification once on that candidate before any batch change lands on main. Land the tested combination atomically; sequential PR merges must not expose untested intermediate trees. Unknown/mixed scope and broad contract changes require full verification. Any base/source/tree, conflict resolution, dependency or validation-flag change invalidates evidence. Current-head automation and server requirements still apply; tree equality never bypasses them. See [batch validation](docs/testing.md#batch-validation-cut62) for review, identity, landing and invalidation rules.
- At the batch gate (or before an individual merge outside a batch), on suitable hardware or CI, when full execution is authorized, opt in with `CHIBI_EXECUTION_PROFILE=hardware CHIBI_ALLOW_FULL_GPU=1 make test test-render`; `make test` remains the complete tooling and native regression gate. `make test-slow` runs the full native suite and whole-function audit, `make test-render` covers GPU/window checks, and `make test-compiler-parity` runs the focused behavior parity gate. Keep required CI configurations, native/backend/load, web/UI and release validation. See `docs/testing.md` for exact coverage and timing.
- Preserve every explicit PR/ticket hold and denied-action approval requirement when choosing batch members. Approval of the batch policy does not clear those holds. Do not bring held code into an integration PR through merges, cherry-picks or dependencies. The integration owner must verify each member is reviewed and eligible under the existing approval rules, and recheck before landing; helper confirmation never supplies or replaces that approval.
- Prefer `make build` when building.
- Keep code small: remove dead code/abstractions; use small functions, narrow APIs, few parameters, guards, and no `else` after `return` or `break`.
- Fail fast: `assert` invalid state; use `xmalloc`/`xrealloc`; fix pointer bugs at the source; avoid defensive `NULL` checks that mask defects.
- Types: `int` for domain values/indices; `size_t` for sizes; unsigned for bits; `bool` for booleans.
- Signatures: `f()` for no parameters; omit unused parameter names.
- Vectors: `support/array.h` (`apush`, `acount`, `afree`, `asetlen`).
- Includes: `"project.h"` for project code; `<system.h>` otherwise.
- Keep ownership/lifetimes explicit; mark file-private symbols `static`.
- Prefer direct calls and `switch` statements to function pointers.
- Write C, never C++. The only exceptions are pass-through shims to C++ APIs: `src/ui/rml.cpp` (RmlUi), `src/editor/text/zep_bridge.cpp` (Zep) and `test/draw_backend.cpp` (Cute's C++-linkage draw diagnostics). They only convert types and forward calls, so all logic stays in C.
- Follow existing names and clang-format; keep short names local; name constants and invariants with `#define`, `enum`, or `static const`.

## Editor UI

For UI/UX design, interaction reviews, or editor usability work, follow the
[UX laws skill](.agents/skills/ux-laws/SKILL.md).

The editor UI is RmlUi, driven from C through the Studio kit (`src/editor/ui/kit.h`).
Its design is in `docs/prototypes/studio/DESIGN.md`. Before building any UI mechanism
in a panel, read `docs/editor-ui-references.md`. It maps each reference library to the
kit primitive that solves the same problem:

- Base UI, shadcn/ui, cmdk and DialKit
- Motion, NumberFlow, Torph and Sonner
- Virtuoso, dnd-kit, Shiki, Liveline and Recharts
- zustand, next-themes, clsx and cva

The behavior graph editor follows `docs/editor-graph-references.md` (xyflow, imgui-node-editor,
Blender, Unreal, ComfyUI and others).

Use the kit primitive. If it is missing, add it to the kit following that library's
contract instead of building a one-off in the panel.
Before changing a component, inspect and reuse comparable controls already in
Studio. Before calling it finished, compare its text baselines, icon alignment,
spacing, grouping and interaction states with those controls in a rendered view.

Pane and graph fields must use controls that match their reflected type: checkboxes
for booleans, numeric editors for quantities, color pickers for colors, and asset
pickers for references. Integers that represent choices must be named enums shown
as searchable dropdowns, including every enum value. Anchor dropdowns to their
field, and keep labels, values and icons vertically aligned. Graph inputs, bindings
and folded sources must remain editable in their node and in Properties. Use one
editable field with type-aware autocomplete for graph sources: typing a literal
sets a constant, and typing a property path browses compatible fields. Do not add
a separate source-kind picker for actions the same field can handle. Group input
controls by device type and reuse the Studio icon set.

Common mismatches to catch:

- Toasts built by hand, or a modal used for a notice: use `kit_toast` (Sonner).
- Hand-written open, close, focus or outside-press logic: use kit popups, menus and the command palette (Base UI).
- A number animated by rewriting its text: use `kit_number_flow` (NumberFlow).
- 1,000 or more rows emitted directly: use `kit_list` (Virtuoso).
- Shared state threaded through parameters or copied into statics: authoring state stays in the C model, and view settings go in `studio_prefs` (zustand).
- Class strings built with ternaries: use `KIT_CX` (clsx) and `kit_variant`/`kit_size` (cva); state goes in `data-*` attributes.
- Instant show and hide, or motion interpolated by hand: use `kit_presence` and `kit_layout` (Motion).

Verify UI changes in the web build (`make web`).
