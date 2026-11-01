# C-exit plan

Written 2026-10-29 for the coordinator. Basis: `stage-zero-retirement.md` (read it for the caller map), `docs/native-llvm-ledger.md`, `docs/toolchain-inventory.md` and fresh line counts on `d5435f0b9`. Owner direction applies: breaking Studio, old projects, docs, `format`, migrations and Windows is fine; the only gate is a green `main`. Anything marked (?) is unverified.

## What is left (non-test C, lines)

| Area | Lines | Fate |
| --- | ---: | --- |
| `src/language/src/parser.c` + `scanner.c` + `grammar.js` | 76k + 0.1k + 0.8k | Replace with a Chibi parser, then delete with `syntax.c` and the tree-sitter vendor |
| `src/language/*.c` (semantic, lower, analyses) | 30k | Delete once the compile path runs on the self-hosted compiler |
| `src/authoring` | 39k | About 25k is editor/Studio services (navigation, rename, intelligence, presentation, edit, minimize): delete now. The rest is project prepare/compile: delete after the cutover |
| `src/assets` | (?) | Old KDL parsers and `project_migrate*`: delete (old-formats) |
| `src/execution` (bytecode, SLJIT, prepared) | 41k | Keep until native self-hosting and a hot-reload story exist |
| `src/runtime` / `src/render` / `src/graph` / `src/ui` | 49k / 29k / 33k / 14k | Move to Chibi packages over narrow bindings (M9) |
| `src/host`, `src/toolchain`, `tools/chibi_ctl` | 12k + 0.3k | Shrink to the apphost and the narrow natives; delete with the native backend |
| `test/**/*.c` | 185k | Leaves with the C it tests; port only what an `@test` can say |

The self-hosted compiler is 24.5k lines of Chibi. It already compiles build programs, `@test` packages and behaviors; it does not run the public project path.

## Order

1. **Seeds stop gating** (#835, landed first). Compiler PRs no longer queue on the checked artifact; follow-up: the seed becomes a release asset.
2. **Land the feature queue** now: #813 (split), #823, #827, #795, #824, #826, #825.
3. **Cut `chibi test` and `chibi-compile` over** to the self-hosted compiler and delete the C front end behind them (workstream A). No compatibility shims: if the authored compiler lacks it, add it to the language or drop the feature.
4. **Assets and the game catalog become Chibi** (B), so nothing exports a C catalog any more.
5. **A Chibi parser** (C) replaces tree-sitter. It is last to delete, but it starts now because it is long and independent.
6. **Native codegen** (E) takes aggregates, modules and the native catalog, then self-hosts; the engine modules (F) move as the codegen reaches them.

Rules for everyone: delete the C in the same PR that replaces it; no feature flags; use the newest language features in new Chibi (results with `@error`, records, generics, maps, `core:math`, `core:time`); when a feature is missing, add it first as its own PR (the seed rule in `docs/self-hosting-build.md`: land the feature, refresh the seed, then use it in compiler sources).

## Workstreams

### A. Compile-path cutover and stage-zero deletion
- Owner: this session (port-lead). Depends on B for the project catalog.
- Steps: (1) `chibi test` / `chibi-compile --test` call the artifact through `compiler_bootstrap_load` + `CompileClosure` instead of `script_analyze_package` + `script_lower_package` (`source_package_prepare.c:356,428`); (2) `chibi-compile` check/modules/resources/stage-one-input go through `tools/toolchain/StageOne.chibi`'s authored route; (3) delete the Studio-facing `source_*` services and the `controller.c` commands that need them, and `chibi_ctl --format/--migrate/--docs`; (4) delete `semantic*.c`, `lower.c`, `identity.c`, `component.c`, then the leaf analyses in the include order given in `stage-zero-retirement.md` section 3.
- **First PR**: route `chibi test` through the self-hosted compiler and delete `source_package_prepare.c`'s C analyzer/lowerer path and what only it used. Gaps it will hit become language-feature PRs (see D).
- Also mine: the seed-as-release-asset follow-up to #835, and `lower.c` leak fix (branch `claude/fix-query-evaluator-leak-20261029`, to be dropped if step 4 lands first).

### B. Assets, project and catalog in Chibi
- Owner: old-formats (with lang-ux for the `chibi` CLI surface).
- Finish retiring KDL and the old formats; scenes, prototypes, input, materials, project manifest and the game catalog become typed Chibi sources (`packages/game/assets` is the start); `project_migrate*.c`, `src/assets` parsers and `new_asset.c` go.
- **First PR**: delete all project migrations (`project_migrate*.c`; there are no users) and the parsers nothing reads after the typed sources, then make the catalog a Chibi package the compiler imports.

### C. Chibi parser
- Owner: strings-ints (lead), with portable-libs (bytes/bits, `char`) and errors (`@error` diagnostics).
- Recursive descent plus precedence climbing in Chibi producing the `syntax_diagnostics.Node` rows that `compiler_syntax.Parse` returns now (kind, range, parent, children, error/missing flags, fields, extras). Strict parse first (enough for compiling); error recovery later for tools. `source_tokenizer` is a token-level start. Validate differentially against tree-sitter on `test/language/syntax_corpus` and every `.chibi` in the repo.
- **First PR**: the parser for declarations, types, statements and expressions with a differential test over the whole repo's `.chibi` files; no switch-over yet. The switch (changing `compiler_syntax_parse` callers and the artifact's native list) comes after the seed rule: a seed refresh.
- Deleting `parser.c`, `scanner.c`, `grammar.js`, `syntax.c`, `tree-sitter.json` is the closing PR.

### D. Language features, in order of what A and C need
- Owners: generics (#823 then optionals `T?` and `comptime`), records (#827, then arrays/unions in records), errors (#813 split: add `@error`, then use it), builtin-math (#826), core-math (#824), host-clock (#825), portable-libs (#795, then `char`/byte slices).
- Gaps known to block A: optionals (`ReadType` rejects `?`), `comptime`, package-local templates, records with arrays/unions/nested records, nested maps and map value writes, first-error-only diagnostics, declared components/systems/projections in the compile path (B). Gaps that block C: `char`, byte slices, nested maps, `assert` in the compiler host.
- **First PRs**: land the queued ones in the order #813 -> #823 -> #827 -> #795 -> #824/#826 -> #825; each follows the seed rule.

### E. Native backend and the toolchain
- Owners: sdk-cmake (lead: linking, SDK, runtime SDK, native self-build, `chibi build --native`) and builtin-math (aggregate layout for vectors/matrices).
- LLVM ledger path: M4 aggregates and native ownership, M5 imports and native catalog calls (W13/W14) and VM-to-native calls (W16), M7 census and native stage one/two (W21/W22), recovery without `cmake`/`clang`/`gcc` (W23). Add C-ABI `extern` bindings for the narrow libraries (SDL, Cute, Box2D, Box3D).
- Also E: finish deleting CMake, `Makefile`, `tools/*.py` and `configure*.sh` (test supervisor, packaging) as the Chibi replacements exist; adding a narrow host primitive in C when Chibi needs one is allowed.
- **First PR**: W14 (native catalog calls) or the compiler-source census W21, whichever the owner sees as the shorter path to a native `chibi-compiler-host`.

### F. Engine modules in Chibi (M9)
- Owners: host-clock, core-math, lang-ux.
- Lean on Chibi's strengths: components, systems, queries and effects, `core:math` vectors and quaternions, `core:time`. Order by external dependence: `src/graph` first (model/optimizer with no native library; much of it is dead once B lands), `src/runtime` behaviors/scenes/prototypes/ui/materials next, then `src/render` over the Cute/SDL bindings. Keep Box2D and Box3D as vendored libraries.
- The VM (`src/execution`) stays until native code can hot-reload.
- **First PRs**: host-clock: delete the clock and time natives that `core:time` replaces and the C systems built on them; core-math: delete the C math/ease/spring copies; lang-ux: replace `src/runtime/ui` and the CLI-side C that `Main` made redundant.

## Sequencing
Week 1: A.1, B.1, C.1, the D queue and E's first PR run in parallel; they touch different files (the shared ones are `Targets.chibi`, `tools/test_policy.py` and `test/language/main.c`: one writer per PR, rebase over merges). Week 2: A.2-A.3 once B's catalog exists; C finishes the parser corpus; E reaches M4. After: A.4, F in earnest, C's switch-over and `parser.c` deletion, E's native self-hosting, then delete `src/execution` and the C host.

## Risks
- Compiler-source PRs still serialize on `main` when one raises `minimum`; keep feature PRs small and mark the seed refresh explicitly.
- Hot reload and the editor need a VM or JIT; do not delete `src/execution` before a replacement exists.
- Windows has no build host (?) and web builds stay on CMake until E reaches them; both may stay red.
- The parser switch changes the artifact's native list: it needs a runtime re-pin and a seed refresh together.
