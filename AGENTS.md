## Priority: get off C, C++ and CMake

The owner's top priority is replacing C, C++ and CMake with Chibi as fast as possible. Chibi has no end users yet, so breaking things is fine; only the end result matters, not the safety of each change.

- Delete C and CMake whenever Chibi can take over. Write new code in Chibi; write C only for what Chibi can't run yet.
- Replacements lean into Chibi's strengths and newest language features instead of transliterating C. When a feature is missing, add it to the language and compiler rather than writing C around it.
- Breaking `chibi-studio`, old project formats, public APIs, the C front end's consumers or a platform temporarily is acceptable. Fix them afterwards in Chibi, or delete what nobody needs.
- Merge first, fix forward. Merge a PR as soon as the change is done; don't wait for CI. CI runs on `main`, and whoever's merge breaks `main` fixes it right away. Merge `main` into a PR only to resolve a conflict. No evidence bundles, receipts, staged admissions, holds or paired-PR requirements.
- When a design choice is unclear, follow Zig's and Odin's prior art.
- When contracts change, migrate this repository's assets directly and remove obsolete implementations. No backward compatibility, legacy aliases or migration steps for projects outside this repository.

## Rules

- Follow YAGNI principles, and boring solutions.
- Shared source implements components, nodes, functions, and systems reusable across games. Authored assets own scene composition, game rules, visual choices, and tuning.
- Name shared modules for their capability. Turn game-specific geometry, camera choreography, and outcome handling into authored data and behaviors; add native capabilities when authoring exposes a concrete gap.
- Keep reusable gameplay features optional so scenes can use rendering, physics, and input independently of combat, encounters, or rhythm.
- Keep debugging UI, capture/scenario tooling, in clearly named modules and separate build targets where practical. Tooling consumes explicit runtime inspection/control APIs; shared runtime code stays independent of tooling and editor UI.
- Author assets by editing their KDL and `.chibi` files directly. Read existing sources and reflected capabilities first; format and validate changed assets through `chibi_ctl`, then inspect or run a bounded scenario. See `docs/runtime-authoring.md`.
- Graph legibility: when authoring, reviewing, or cleaning up a behavior, shader, or field graph, follow `.claude/skills/chibi-graph-legibility/SKILL.md`.
- Declarative assets use KDL. Behaviors, callable libraries, and systems use readable `.chibi` as their complete executable source; graph layout and debug metadata are optional. After hand-editing assets, run `build/bin/chibi_ctl --project-root <root> --format` and the relevant canonicality check; `make test` alone does not cover asset canonicality.
- Language design: before adding syntax, check whether the compiler can infer the feature or a form the language already has can express it. For example, a map keyed by a small enum is stored densely instead of adding enum-indexed array syntax. Propose new syntax only when neither works, and say why in the proposal (`docs/language-friction.md`).
- Navigation and refactoring (find references, rename, go to definition, call hierarchy, hover): implement each in shared SDK authoring modules and expose it through Studio. `docs/authoring-navigation.md` tracks each feature's status.
- Validate engine generalizations with another scene or prototype that uses different content and omits the motivating game's mechanics.
- For routine agent iteration, use `make agent-list` and `make agent-test AGENT_SELECTOR=--<selector>`. For a relevant GPU assertion, explicitly use `make agent-gpu-test` with a listed selector. Inspect actual capabilities with `python3 tools/agent.py doctor`; do not infer hardware from the host name. See `docs/agent-execution.md` for preview/MP4 budgets and truthful coverage reporting. Routine previews/captures use the native hidden-window controller and exit after saving; do not launch Studio. If a display context is required, use the bounded Xvfb command and report it as described there. Headless rendering still spends GPU resources. Do not reconstruct expensive suites through repeated one-frame previews.
- One-off checks and probes go into one dispatching tool (a new `--<case>` selector in `test/tools/main.c` or `test/language/main.c`), never one linked executable per probe. Keep scratch for a task in one directory, link scratch tools with dead-code stripping (`-Wl,-dead_strip` on macOS, `-Wl,--gc-sections` on Linux) and without sanitizers unless the check needs them, copy projects as copy-on-write clones (`cp -c` on APFS, `cp --reflink=auto` on Linux), and delete generated binaries and project copies once the check has passed. Report any scratch left behind.
- Select tests from the affected behavior. Use `make test-list` for the current groups and focused selectors, run those locally, and let CI run the full groups. See [test iteration](docs/testing.md).
- Prefer authored Chibi `@test` regressions whenever they can express the same behavior and oracle; inferred purity supplies automatic test parallelism. Keep C/pico_unit for native internals, ABI, ownership and failure boundaries Chibi cannot express. `tools/test_policy.py` dispatches groups and owns process cleanup.
- Prefer `make build` when building.
- Keep code small: remove dead code/abstractions; use small functions, narrow APIs, few parameters, guards, and no `else` after `return` or `break`.
- Fail fast: `assert` invalid state; use `xmalloc`/`xrealloc`; fix pointer bugs at the source; avoid defensive `NULL` checks that mask defects.
- Types: `int` for domain values/indices; `size_t` for sizes; unsigned for bits; `bool` for booleans.
- Signatures: `f()` for no parameters; omit unused parameter names.
- Vectors: `support/array.h` (`apush`, `acount`, `afree`, `asetlen`).
- Includes: `"project.h"` for project code; `<system.h>` otherwise.
- Keep ownership/lifetimes explicit; mark file-private symbols `static`.
- Prefer direct calls and `switch` statements to function pointers.
- Keep native-boundary tests and benchmark harnesses in C using pico_unit; use another third-party library when additional functionality is needed.
- Never write C++. The remaining pass-through shims to C++ APIs (`src/ui/rml.cpp` for RmlUi, Studio's `src/editor/text/zep_bridge.cpp` for Zep, `test/draw_backend.cpp` for Cute's draw diagnostics) only convert types and forward calls; delete them when their C callers move to Chibi.
- The C style rules above apply to C that still exists; don't write new C when Chibi can do the job.
- Follow existing names and clang-format; keep short names local; name constants and invariants with `#define`, `enum`, or `static const`.

## Working autonomously

- Work autonomously. Ask only when a choice changes the goal, an output the owner will notice, or something that can't be undone; otherwise pick the reasonable default, say which, and keep going.
- Fix root causes. Speed toward the C exit beats preserving existing behavior; fix a red `main` before starting new work.
- When checks need a re-run (for example after a dependency merged), label the PR `ci-rerun`; label it `ci-seed` to build, publish and pin new compiler runtime seeds on its branch (pull before pushing again). See [starting CI with labels](docs/testing.md#starting-ci-with-labels).
- Recipes for adding a component, a native script function, a test scene and a project migration are in `.claude/skills/chibi-recipes/SKILL.md`.

## Before every commit

1. Review the whole diff as a reviewer would: correctness, ownership and lifetimes, failure paths, the style rules above, and dead code left behind. In Claude Code run `/code-review`; otherwise review it yourself. Fix what you find before committing.
2. Add or update an authored `@test` for every expressible behavior change, or a pico_unit case for a native boundary, through the dispatcher that owns the area (`test/tools` or `test/language`). A fix needs a case that failed before it.
   A new component, native function or event is also used in a `test/fixtures/feature-coverage` scene, or listed in its `coverage-gaps.txt` with a reason (see [feature coverage](docs/testing.md#feature-coverage)).
3. Build what changed and run a focused selector if it's quick; CI on `main` runs the full groups.
4. Format. The pre-commit hook (`.githooks/pre-commit`, installed by configure) runs clang-format 22 on staged C sources; `make check-format` checks everything. Format hand-edited assets with `chibi_ctl --format`.
5. Commit messages say what changed and why, in the imperative.

## Review guidelines

- Don't ask for project version bumps, migration steps or compatibility shims; see the priority section.
- Flag new C or C++ that Chibi could have expressed, and C left behind after a Chibi replacement.

## Editor UI

Studio lives in the separate `chibi-studio` repository. Its UI rules and the UX laws skill live in that repository's `AGENTS.md`. Engine UI primitives in `src/ui` follow `docs/editor-ui-references.md`.
