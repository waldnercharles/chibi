- When contracts change, migrate our assets directly and remove obsolete implementations; backward compatibility and legacy aliases are not required. When the change alters what projects saved by earlier builds mean or stops them from loading, bump `CHIBI_PROJECT_VERSION` in `src/assets/project.h`, add the step that upgrades them in `src/assets/project_migrate.c`, and migrate our projects with `build/bin/chibi_ctl --project-root <root> --migrate`.
- Follow YAGNI principles, and boring solutions.
- Shared source implements components, nodes, functions, and systems reusable across games. Authored assets own scene composition, game rules, visual choices, and tuning.
- Name shared modules for their capability. Turn game-specific geometry, camera choreography, and outcome handling into authored data and behaviors; add native capabilities when authoring exposes a concrete gap.
- Keep reusable gameplay features optional so scenes can use rendering, physics, and input independently of combat, encounters, or rhythm.
- Keep debugging UI, capture/scenario tooling, in clearly named modules and separate build targets where practical. Tooling consumes explicit runtime inspection/control APIs; shared runtime code stays independent of tooling and editor UI.
- Author assets by editing their KDL and `.chibi` files directly. Read existing sources and reflected capabilities first; format and validate changed assets through `chibi_ctl`, then inspect or run a bounded scenario. See `docs/runtime-authoring.md`.
- Graph legibility: when authoring, reviewing, or cleaning up a behavior, shader, or field graph, follow `.claude/skills/chibi-graph-legibility/SKILL.md`.
- Declarative assets use KDL. Behaviors, callable libraries, and systems use readable `.chibi` as their complete executable source; graph layout and debug metadata are optional. After hand-editing assets, run `build/bin/chibi_ctl --project-root <root> --format`; asset canonicality validation is currently quarantined with the full gate, so do not claim `make test` checked it.
- Navigation and refactoring (find references, rename, go to definition, call hierarchy, hover): implement each in shared SDK authoring modules and expose it through Studio. `docs/authoring-navigation.md` tracks each feature's status.
- Validate engine generalizations with another scene or prototype that uses different content and omits the motivating game's mechanics.
- For routine agent iteration, use `make agent-list` and `make agent-test AGENT_SELECTOR=--<selector>`. For a relevant GPU assertion, explicitly use `make agent-gpu-test` with a listed selector. Inspect actual capabilities with `python3 tools/agent.py doctor`; do not infer hardware from the host name. See `docs/agent-execution.md` for preview/MP4 budgets and truthful coverage reporting. Routine previews/captures use the native hidden-window controller and exit after saving; do not launch Studio. If a display context is required, use the bounded Xvfb command and report it as described there. Headless rendering still spends GPU resources. Do not reconstruct expensive suites through repeated one-frame previews.
- One-off checks and probes go into one dispatching tool (a new `--<case>` selector in `test/tools/main.c` or `test/language/main.c`), never one linked executable per probe. Keep scratch for a task in one directory, link scratch tools with dead-code stripping (`-Wl,-dead_strip` on macOS, `-Wl,--gc-sections` on Linux) and without sanitizers unless the check needs them, copy projects as copy-on-write clones (`cp -c` on APFS, `cp --reflink=auto` on Linux), and delete generated binaries and project copies once the check has passed. Report any scratch left behind.
- Before publishing, follow [reviewed validation](docs/testing.md#batch-validation-cut62): independently review affected contracts and interactions, run the selected admitted checks and applicable builds on the exact clean candidate, and land that tested combination atomically. Mixed scope does not require a full suite by itself. Local evidence is sufficient when CI is disabled unless actual server rules require CI. Preserve current-tree evidence, explicit holds, approvals and branch protections.
- Temporary user policy: test commands, including aggregates, have a hard 60-second ceiling. Known slower and unmeasured commands are quarantined; hardware opt-in does not re-enable them. Use `make test` or `make test-short` for the six pure C execution-policy cases, and `make test-tools` for all C tooling/process integrations under one shared production supervisor deadline and `make test-list` for exact admitted native commands. Full verification is disabled, not passed. Do not run full native/render/Studio suites or CUT61's historical 2016-frame capture, or reconstruct them from chunks. See [temporary test policy](docs/testing.md#temporary-60-second-policy). Keep configuration/build timing separate. Required server checks and review/hold rules remain in force; an unavailable affected-contract check without an admitted replacement, or a server-required check, blocks landing.
- Preserve every explicit PR/ticket hold and denied-action approval requirement when choosing batch members. Approval of the batch policy does not clear those holds. Do not bring held code into an integration PR through merges, cherry-picks or dependencies. The integration owner must verify each member is reviewed and eligible under the existing approval rules, and recheck before landing; helper confirmation never supplies or replaces that approval.
- Write tests and benchmark workloads in C with pico_unit. Retain `tools/test_policy.py` only as the production deadline/quarantine supervisor; do not restore Python or Node test suites.
- Prefer `make build` when building.
- Keep code small: remove dead code/abstractions; use small functions, narrow APIs, few parameters, guards, and no `else` after `return` or `break`.
- Fail fast: `assert` invalid state; use `xmalloc`/`xrealloc`; fix pointer bugs at the source; avoid defensive `NULL` checks that mask defects.
- Types: `int` for domain values/indices; `size_t` for sizes; unsigned for bits; `bool` for booleans.
- Signatures: `f()` for no parameters; omit unused parameter names.
- Vectors: `support/array.h` (`apush`, `acount`, `afree`, `asetlen`).
- Includes: `"project.h"` for project code; `<system.h>` otherwise.
- Keep ownership/lifetimes explicit; mark file-private symbols `static`.
- Prefer direct calls and `switch` statements to function pointers.
- Write benchmarks, unit tests, and integration tests in C using pico_unit; use another third-party library when additional functionality is needed.
- Write C, never C++. The only exceptions are pass-through shims to C++ APIs: `src/ui/rml.cpp` (RmlUi), Studio's `src/editor/text/zep_bridge.cpp` (Zep) and `test/draw_backend.cpp` (Cute's C++-linkage draw diagnostics). They only convert types and forward calls, so all logic stays in C.
- Follow existing names and clang-format; keep short names local; name constants and invariants with `#define`, `enum`, or `static const`.

## Working autonomously

- Work autonomously. Ask only when a choice changes the goal, an output the owner will notice, or something that can't be undone; otherwise pick the reasonable default, say which, and keep going.
- This is production-grade software: no hacks or shortcuts. Fix root causes, and leave every commit building, formatted and tested.
- Recipes for adding a component, a native script function, a test scene and a project migration are in `.claude/skills/chibi-recipes/SKILL.md`.

## Before every commit

1. Review the whole diff as a reviewer would: correctness, ownership and lifetimes, failure paths, the style rules above, and dead code left behind. In Claude Code run `/code-review`; otherwise review it yourself. Fix what you find before committing.
2. Add or update a pico_unit case for every behavior change, in the dispatcher that owns the area (`test/tools` or `test/language`). A fix needs a case that failed before it.
3. Build what you touched (`make build`; `make language-build` for language and authoring code) and run the admitted checks that cover it: `make test-tools`, the `make test-language` targets for the area (`test-language`, `-semantic`, `-execution`, `-program`, `-headless`, `-format`, `-prepared`), and the tool selectors `make test-list` prints. Say which ran; quarantined coverage is not passed.
4. Format. The pre-commit hook (`.githooks/pre-commit`, installed by configure) runs clang-format 22 on staged C sources; `make check-format` checks everything. Format hand-edited assets with `chibi_ctl --format`.
5. Commit messages say what changed and why, in the imperative.

## Editor UI

Studio lives in the separate `chibi-studio` repository. Its UI rules and the UX laws skill live in that repository's `AGENTS.md`. Engine UI primitives in `src/ui` follow `docs/editor-ui-references.md`.
