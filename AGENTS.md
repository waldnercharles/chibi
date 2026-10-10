# Chibi

## Architecture and assets

- Shared source owns reusable engine capabilities. Authored assets own scene composition, game rules, visual choices, and tuning. Keep gameplay features optional and runtime code independent of editor and debugging tools.
- Declarative assets use KDL. Behaviors, callable libraries, and systems use readable `.chibi` as their complete executable source; graph layout and debug metadata are optional. Edit assets directly and format changed projects with `build/bin/chibi_ctl --project-root <root> --format`. For asset validation and scenarios, see [runtime authoring](docs/runtime-authoring.md).
- For behavior, shader, or field graphs, follow [graph legibility](.claude/skills/chibi-graph-legibility/SKILL.md).
- Implement navigation and refactoring in shared SDK authoring modules and expose them through Studio. See [navigation status](docs/authoring-navigation.md).
- Validate engine generalizations with another scene or prototype that omits the motivating game's mechanics.
- When saved asset contracts change, migrate our assets directly and remove obsolete implementations; compatibility aliases are unnecessary. Bump `CHIBI_PROJECT_VERSION` only when a project in this repository, `chibi-studio`, or `chibi-celeste` would load wrong or fail and a migration would rewrite it. Follow the [migration recipe](.claude/skills/chibi-recipes/SKILL.md#change-saved-projects-migration), including migrating this repository's projects in the same change. Other repositories upgrade on open; avoid PRs that only bump their project version.
- For new components, native functions, and test scenes, use [the recipes](.claude/skills/chibi-recipes/SKILL.md). New components, functions, and events belong in a [feature-coverage scene or its documented gaps](docs/testing.md#feature-coverage).

## C conventions

- Write C. C++ is limited to pass-through shims in `src/ui/rml.cpp`, Studio's `src/editor/text/zep_bridge.cpp`, and `test/draw_backend.cpp`.
- Use `int` for domain values and indices, `size_t` for sizes, unsigned types for bits, and `bool` for booleans.
- Use `f()` for no parameters and omit unused parameter names. Use `support/array.h` for vectors: `apush`, `acount`, `afree`, `asetlen`.
- Use `xmalloc`/`xrealloc` and assert invalid internal state. Prefer direct calls and `switch` to function pointers, and guards without `else` after `return` or `break`.
- Use `"project.h"` for project includes and `<system.h>` for system includes. Formatting uses clang-format 22; the pre-commit hook formats staged C sources.
- Permanent tests and benchmark workloads use C and pico_unit in the existing dispatchers. Python remains the test supervisor, not a test suite.

## Verification

- Prefer `make build`. Use `make test-list` or `make agent-list` to find checks. `make test` checks only execution-policy units.
- Test executions have a shared 60-second ceiling. For commands, new CPU checks, disposable diagnostics, and quarantine rules, see [testing](docs/testing.md). For GPU checks and previews, see [agent execution](docs/agent-execution.md).
- Before publishing, follow [reviewed validation](docs/testing.md#batch-validation-cut62) on the final candidate. Explicit PR/ticket holds, denied-action approval requirements, and server protections remain binding; held code stays out of integration changes, including through dependencies.

## Studio

Studio lives in `chibi-studio`; its UI rules are in that repository's `AGENTS.md`. Engine UI primitives follow [editor UI references](docs/editor-ui-references.md).
