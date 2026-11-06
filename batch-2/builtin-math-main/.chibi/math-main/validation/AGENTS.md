- When contracts change, migrate our assets directly and remove obsolete implementations; backward compatibility and legacy aliases are not required. Bump `CHIBI_PROJECT_VERSION` in `src/assets/project.h` only when an existing project would load wrong or fail to load and a migration step would rewrite it: a project in this repository or `chibi-studio` uses the changed contract. Don't bump for additive changes, for a contract no project uses yet, for a change to what the engine does with a value, or when loading already treats the old data correctly; say in the PR how to fix a hand-made project instead. When you do bump, add the step that upgrades them in `src/assets/project_migrate.c` and migrate this repository's projects with `build/bin/chibi_ctl --project-root <root> --migrate` in the same PR. Projects in other repositories (Studio, games) upgrade when opened; never open a PR whose only change is their version number (see `.claude/skills/chibi-recipes/SKILL.md`).
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
- Before publishing, follow [reviewed validation](docs/testing.md#batch-validation-cut62): independently review affected contracts and interactions, run the selected checks and applicable builds on the exact clean candidate, and land that tested combination atomically. Mixed scope does not require a full suite by itself. Local evidence is sufficient when CI is disabled unless actual server rules require CI. Preserve current-tree evidence, explicit holds, approvals and branch protections.
- Select tests from the affected behavior and required coverage. Use `make test-list` for the current groups and focused selectors; renderer and Studio checks need their documented hardware and environment. Keep configuration/build timing separate, and report unexecuted coverage accurately. See [test iteration](docs/testing.md). Required server checks and review/hold rules remain in force.
- Preserve every explicit PR/ticket hold and denied-action approval requirement when choosing batch members. Approval of the batch policy does not clear those holds. Do not bring held code into an integration PR through merges, cherry-picks or dependencies. The integration owner must verify each member is reviewed and eligible under the existing approval rules, and recheck before landing; helper confirmation never supplies or replaces that approval.
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
- Write C, never C++. The only exceptions are pass-through shims to C++ APIs: `src/ui/rml.cpp` (RmlUi), Studio's `src/editor/text/zep_bridge.cpp` (Zep) and `test/draw_backend.cpp` (Cute's C++-linkage draw diagnostics). They only convert types and forward calls, so all logic stays in C.
- Follow existing names and clang-format; keep short names local; name constants and invariants with `#define`, `enum`, or `static const`.

## Working autonomously

- Work autonomously. Ask only when a choice changes the goal, an output the owner will notice, or something that can't be undone; otherwise pick the reasonable default, say which, and keep going.
- This is production-grade software: no hacks or shortcuts. Fix root causes, and leave every commit building, formatted and tested.
- When checks need a re-run (for example after a dependency merged), label the PR `ci-rerun`; label it `ci-seed` to build, publish and pin new compiler runtime seeds on its branch (pull before pushing again). See [starting CI with labels](docs/testing.md#starting-ci-with-labels).
- Recipes for adding a component, a native script function, a test scene and a project migration are in `.claude/skills/chibi-recipes/SKILL.md`.

## Before every commit

1. Review the whole diff as a reviewer would: correctness, ownership and lifetimes, failure paths, the style rules above, and dead code left behind. In Claude Code run `/code-review`; otherwise review it yourself. Fix what you find before committing.
2. Add or update an authored `@test` for every expressible behavior change, or a pico_unit case for a native boundary, through the dispatcher that owns the area (`test/tools` or `test/language`). A fix needs a case that failed before it.
   A new component, native function or event is also used in a `test/fixtures/feature-coverage` scene, or listed in its `coverage-gaps.txt` with a reason (see [feature coverage](docs/testing.md#feature-coverage)).
3. Build and run the checks CI runs: `make test-native`, `make test-materials`, `make test-projects` and `make test-language-all` (each builds the drivers with `make test-build`) and `make test-tools`. Say which ran and identify unexecuted coverage.
4. Format. The pre-commit hook (`.githooks/pre-commit`, installed by configure) runs clang-format 22 on staged C sources; `make check-format` checks everything. Format hand-edited assets with `chibi_ctl --format`.
5. Commit messages say what changed and why, in the imperative.

## Review guidelines

- Ask for a project version bump and migration only when you can name a project in this repository or `chibi-studio` that the change makes load wrong or fail, and the step would rewrite it (see the first rule). A hypothetical project holding a value no project stores is not enough, and neither is a contract that reached `main` without any project using it. The owner has declined those bumps.

## Editor UI

Studio lives in the separate `chibi-studio` repository. Its UI rules and the UX laws skill live in that repository's `AGENTS.md`. Engine UI primitives in `src/ui` follow `docs/editor-ui-references.md`.
