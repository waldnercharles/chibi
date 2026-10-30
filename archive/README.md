# Archive

This tree is not built. It keeps code that is being rebuilt in Chibi so it can serve as a reference
while features are replaced one at a time with Zig- and Odin-style tooling. Paths below `archive/`
match their original paths in the repository root.

Nothing here is built, formatted, tested or listed in an inventory. Bring a file back with `git mv` to its original path and re-add it to the build lists.

## Studio editor services (authoring)

Editing, rename, completion/hover/highlighting and prototype-source editing for Studio. The compiler, formatter and reference index do not need them.

- `src/authoring/source_edit*`: refactoring and source edits (the text helpers `source_minimize` needs moved to `source_refactor_text`).
- `src/authoring/source_intelligence*`: highlighting, completion and hover.
- `src/authoring/source_rename*`: rename plans.
- `src/authoring/source_prototype_edit*`: prototype source edits.
- `test/authoring/source_edit.c`: their test.

## Source formatter and documentation site (authoring)

`chibi fmt` / `chibi-compile --format` and `chibi doc` / `chibi-compile --docs` ran on the C front end. They are archived while the compiler moves to Chibi.

- `src/authoring/project_format*`, `source_format*`, `source_minimize*`, `source_refactor_text*`: canonical formatting of project assets and package sources.
- `src/authoring/project_docs.c`: the site runner, cut from `project_compile.c`.
- `tools/docs/`: the Chibi site generator (Markdown, reference, grammar, site).
- `test/language/format.c`, `test/language/docs_markdown*`, `test/tools/published_docs.c`: their tests.

## Stage-zero script tests (test/language)

These tests compiled the stage-zero script dialect (`library` blocks, scriptless top level, paren-less `if`, `and`/`or`, handler/component/system script syntax) with `script_parse`/`script_analyze`/`script_lower`, so they leave with stage zero. Re-add the behavior as authored `@test` (or package-dialect fixtures) when needed:

- **Analyzer subject tests:** `semantic.c`, `record_semantic.c`, and the `*_chibi.c` differentials (named, lexical and nominal binding, unit and effect analysis, constant proof and scalar, type constructors, callable headers, union and record members). Diagnostics, ranges, recovery and C-vs-authored equivalence.
- **Language features through the VM:** borrow, iteration, maps, operations, generic, scalarize, effects, symbol references. Borrow flags and call-scoped views, map insertion order and nesting, iteration lifetimes, operator ABI generation, generic constructed types, scalar optimization patterns, effect publication.
- **Program and query runtime:** program (313 cases: bindings, handlers, scene bindings), query program/values/boundary, publication, retention of prepared results, reductions, native char results.
- **Records:** record publication, runtime and state.
- **Execution paths:** SLJIT and wasm execution of script modules (`wasm.c`), binding schedule, activation cache, component write, runtime attribution, packed and row array layouts, outliner projection, inferred string arrays, and the script/binding/activation/borrow benchmarks.
- **Not yet archived** (they cross the stage-zero producer and the `test_tools` consumer): the prepared round trip (`prepared.c`, `record_producer.c`, `record_prepared_runtime.c`, `test/tools/prepared.c`, `c_export.c`, `record_c_export.c`) and `compositional_records.c`, whose source generators `package_compiler_chibi.c` uses.

## Game-project compile path (authoring, host)

`chibi-compile --output/--check/--explain-costs/--modules/--resources-only/--stage-one-input` and the `chibi build/check/export/web` and run-a-game paths prepared game projects through the C front end. `chibi-compile` now only runs `--test`; the CLI reports "game projects are archived".

- `src/authoring/project_compile.c` (and its full header), `project_prepare*`, `component_declarations`, `prototype_prepare`, `scene_prepare`, `material_prepare`, `sound_prepare`, `ui_prepare*`, `source_costs`, `material_attach`, `prototype_attach`.
- `src/host/export.c` and `export.h`: exported folders, apps and web sites.
- `tools/toolchain/StageOne.chibi`: the stage-one project route.
- `test/host/export.c`, `test/tools/GameEquivalence.chibi`.
