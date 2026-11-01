# Retiring the stage-zero C front end

Read-only map, written 2026-10-29 against main `f2f57ec66` (worktree `authored-strings-regressions`). Nothing was built or run; "reaches" means read from CMake, `tools/graph/engine/Targets.chibi` and call sites. Items under "Unverified" at the end could be wrong.

Terms. **Stage zero** = C front end: parser (`src/language/src/parser.c` 1.69 MB generated, `scanner.c`, `grammar.js`, `syntax.c`), semantic analysis (`src/language/semantic*.c` and helpers), lowering (`lower.c`, 399 KB). **Self-hosted compiler** = Chibi sources in `tools/compiler/*`, recovered from the checked artifact `tools/compiler/bootstrap/{bootstrap.json,compiler.chibip}` (docs/self-hosting-build.md "Bootstrap artifact").

## 1. Findings that shape the plan

1. **The self-hosted compiler does not use the ported analysis modules.** `package_compiler` imports only `source_snapshot`, `package_routes`, `package_graph`, `lexical_binding`, `syntax_diagnostics`, `prepared_writer`, `compiler_syntax`, `scalar_facts` (`tools/compiler/package_compiler/Compile.chibi:3-9`, `Behavior.chibi`, `Scalar.chibi`). Type resolution, constant folding, effects and body emission live inline in `Compile.chibi` (3085 lines) / `Body.chibi` / `Steps.chibi` (5808). `unit_analysis`, `unit_algebra`, `effect_analysis`, `signature_reader`, `callable_headers`, `union_members`, `record_members`, `type_constructors`, `type_relations`, `named_binding`, `nominal_binding`, `constant_*`, `source_tokenizer` are differential-tested against C (`test/language/*_chibi.c`) but nothing in the compile path imports them. `bounded_compiler` composes some, for one scalar function; it is a demonstration. So the `*_chibi` ports do not make any C module removable; they only make a later replacement checkable.
2. **Parsing stays native after bootstrap.** `tools/compiler/compiler_syntax/Syntax.chibi:25-39` calls native `compiler_syntax_parse`; `src/toolchain/compiler_syntax.c` implements it with tree-sitter and `tree_sitter_chibi()` from `parser.c`. `chibi-compiler-host` links `chibi_language_syntax` (Targets.chibi 3989-3999; sdk/CMakeLists.txt:677-704). `PLAN.md:1512`: "only the raw parser is native after bootstrap". `parser.c` is therefore a dependency of the artifact's target (native ABI is hashed into `bootstrap.json` `target`), not just of stage zero.
3. **Only build programs have cut over.** `chibi-build-host` loads the artifact via `compiler_bootstrap_load` (`src/host/build_host.c:571`) and never links `chibi_language`. `chibi-compiler-host`, `chibi` (launcher), `chibi-player` also avoid it. `chibi-compile` and `chibi_ctl` link the full stack (`Targets.chibi` ~4049, ~4086-4115).
4. **Windows has no build-host**; its build programs still use stage zero (docs/self-hosting-build.md:53).
5. **Artifact-serialized work** = any edit to files in the bootstrap closure (`tools/compiler/asset_compiler/CompilerSources.txt`: the 16 `package_compiler` files plus `source_snapshot`, `package_paths`, `package_syntax`, `package_routes`, `package_graph`, `lexical_binding`, `syntax_diagnostics`, `prepared_writer`, `compiler_syntax`, `scalar_facts`, core strings), or any change to the target/native ABI (`compiler_target.c`, `compiler_syntax.c` result count, prepared ABI). Those need `make bootstrap-artifact` (Makefile:386) once `minimum` rises or the target changes; changed sources otherwise take the two-compile recovery path. Edits to C in `src/language/*` or `src/authoring/*` that the closure does not depend on do not touch the artifact.

## 2. Consumers of the C front end

"S" = semantic (`script_analyze*`, `script_lower*`, catalog); "Y" = syntax only (`script_parse`, `script_edit`).

| Consumer | Chain (file:function) | Kind |
|---|---|---|
| `chibi-compile <project> -o` (game build), `--check`, `--explain-costs`, `--stage-one-input` | `src/host/compile_main.c:main` -> `authoring_project_compile` (`project_compile.c:241`) -> `authoring_project_prepare` (`project_prepare.c:846`) -> `source_catalog_new` (`:589`) + `authoring_sources_prepare` (`:597`) -> `source_prepare.c` `script_analyze*`, `script_lower` | S |
| `chibi-compile --modules`, `--resources-only` | `authoring_project_compile_modules` (`project_compile.c:249`) -> `authoring_project_assemble` (`project_prepare.c:855`); still runs `source_catalog_new`, so analysis (no lowering). `--resources-only` unverified | S |
| `chibi-compile --test` / `chibi test` | `authoring_project_test` (`project_compile.c:341`) -> `source_package_prepare.c:428` `script_analyze_package`, `:356` `script_lower_package` | S |
| `chibi-compile --docs` | `authoring_project_docs` (`project_compile.c:1261`): `script_lower_bindings`, `script_parse`, `script_node_range` | S+Y |
| `chibi-compile --format`, `chibi_ctl --format` | `authoring_project_format` (`project_format.c:541`): builds a catalog (S) then `script_parse` + `authoring_source_format` | S+Y |
| `chibi_ctl --migrate`, project open | `authoring_project_migrate` (`project_open.c:341`): `source_catalog_new_for_migration`, `script_analyze_document` (`:207`); migration steps `src/assets/project_migrate*.c` use `script_parse` only | S+Y |
| `chibi_ctl` automation loop (open/render/asset cmds), Studio backend | `src/automation/controller.c` (`:1771`, `:2464`, `:2282`, `:2723`, `:5601`, `:6101`) | S+Y |
| Studio (chibi-studio, consumes installed SDK) | `authoring_sources_*`, `authoring_source_presentation_*`, `authoring_navigation_*`, `authoring_source_edit_generation`, `authoring_prototype_source_edit`, and 7 direct `script_parse`/`script_syntax_free` calls (`chibi-studio/src/editor/...`) | S+Y |
| `src/assets/new_asset.c`, `project_migrate*.c`, `project_parser*` | `script_parse` | Y |
| `chibi-build-host` | artifact via `compiler_bootstrap_load`; no `chibi_language` | none (parser native) |
| `chibi-compiler-host`, `chibi`, `chibi-player` | no `chibi_language` | none / Y |
| Tests | 113 test files reference the front end (83 use `script_analyze*`/`script_lower*`/`authoring_*_prepare`, 66 use `script_parse`); `test/language/*_chibi.c` (~17 differential drivers) compare C and authored modules | S+Y |

Studio-facing `src/authoring/source_*.c` (semantic matches / syntax matches): `source_catalog` 94/8, `source_intelligence` 42/13, `source_minimize` 36/17, `source_navigation` 16/7, `source_literal` 16/0, `source_prototype_edit` 15/3, `source_presentation` 10/0, `source_link` 8/0, `source_edit` 7/15, `source_prepare` 7/5, `source_rename` 6/2, `source_presentation_graph` 6/0, `source_references` 5/0, `source_collection` 5/0. Syntax-only: `source_format`, `source_edit_inline`, `source_asset`, `source_package_files`.

## 3. Per-module status

Include dependencies inside `src/language` (header scan) set the deletion order: `lower` -> `semantic*`/`identity` -> `unit_analysis`/`signature_reader`/`union_members`/`record_members`/`callable_headers`/`effect_analysis` -> `named_binding`/`nominal_binding`/`type_constructors`/`lexical_binding` -> `constant_proof`/`scalar_constant`/`numeric`/`unit_algebra`.

| C module | Authored counterpart | What a replacement still lacks |
|---|---|---|
| `parser.c`, `scanner.c`, `grammar.js`, `syntax.c` | none (`compiler_syntax` is a native wrapper; `source_tokenizer` is a token-level port with no importers) | An authored parser producing the same raw facts (kinds, ranges, parents, errors, missing nodes, fields, extras: the 15-result ABI). Error-tolerant, incremental (`script_edit`, `source_intelligence.c:891`). Needed by the artifact, formatter, migrations, Studio |
| `lexical_binding.c` | `lexical_binding` (wired) | nothing known |
| `nominal_binding`, `named_binding`, `type_constructors`, `signature_reader`, `callable_headers`, `union_members`, `record_members`, `unit_algebra`, `unit_analysis`, `effect_analysis`, `constant_proof`, `scalar_constant`, `numeric` | ported, not wired | Wire into `package_compiler` or replace `Compile`'s inline logic; the compile path hard-codes e.g. "no type name or unit" for record fields (`Compile.chibi:2428`) |
| `semantic_{expression,calls,statement,declaration,record,union,generic,comptime,package,context,recovery,effects,units,constant}.c` | spread across `package_compiler/*` | Records limited to scalar fields (no arrays/unions/nested records), no optionals (`ReadType` rejects `?`), no `comptime`, templates package-local (docs/language-friction.md, may be stale), generics requests (`script_analyze_document_requested`), first-error-only diagnostics vs `semantic_recovery.c` partial facts |
| `lower.c` (`script_lower`, `script_lower_package`) | `prepared_writer`, `Body`/`Steps` for package functions; `scalar_lowering` is a Bool/I32 plan only | Game/project lowering: declared components, libraries, systems, projections, bindings (docs/self-hosting-build.md:177-181: `StageOne.chibi` still uses the C catalog for input export and final assembly) |
| `identity.c`, `component.c` | none | Occurrence/record IDs used by `source_presentation*.c`; `Behavior.chibi` takes "the IDs stage zero gives them" (header comment), so IDs are computed by C today |
| Editor APIs in `semantic.h`: `script_declarations*`, `script_use_at`, `script_symbol_at`, navigation, rename, completion, hover, `script_analyze_migration_document` | none | All of the Studio/LSP surface (`docs/authoring-navigation.md`); CLAUDE.md requires these in shared SDK authoring modules |

Public C API with no non-test callers outside `src/language` (candidates for immediate deletion, subject to a check of test uses): `script_declarations`, `script_declarations_package`, `script_package_private`, `script_unit_shorthand`, `script_analysis_constant`, `script_type_contains_tasks`; `script_symbol_at` only in one test.

## 4. Removal order

Each step names whether it changes the bootstrap artifact (A) or not (N).

0. **Prune dead API** (N). Delete the unused entry points above and the C tests that only exercise them. Small PR, no behavior change.
1. **Windows build-host** (N for artifact). Port the process-tree host so build programs stop needing stage zero on Windows. Prerequisite for removing stage zero from the build path.
2. **Package compile cutover: `chibi test`, `--test`, `authoring_packages_prepare`** (N if `package_compiler` is unchanged; A for each language feature the compiler lacks). Route `source_package_prepare.c` through the artifact like `build_host.c` does. Blocked by language gaps in section 3 (arrays in records, optionals, `comptime`, generics); each gap is a `tools/compiler` edit and so an A step. This is the main driver of artifact churn; serialize those PRs on `bootstrap.json`.
3. **Wire the ported analyses into `package_compiler`** (A). Replace inline logic with `unit_analysis`, `effect_analysis`, `signature_reader`, `type_constructors`, etc., one module per PR, differential tests proving equality. Then delete the matching C module together with its `*_chibi.c` driver, keeping the authored tests with expected values recorded from the C oracle (the plan agreed with the coordinator).
4. **Game/project compile** (A). Move declared-component, library, system and projection preparation into `Behavior.chibi`/new closure packages; retire the C catalog use in `StageOne.chibi`. Then `chibi-compile` default, `--modules`, `--resources-only`, `--stage-one-input` no longer need `authoring_project_prepare`'s C path. Delete `lower.c` and `semantic_package.c`, `semantic_generic.c`, `semantic_comptime.c`.
5. **Editor/Studio surface** (N if added as a separate closure; A if added to the compiler closure). Reimplement declarations, symbol/use-at, navigation, rename, completion, hover, occurrences/IDs (`identity.c`, `component.c`), migration analysis and recovery diagnostics as authored SDK modules; switch `chibi_ctl`, `controller.c`, Studio (separate repo, consumes an installed SDK, so this needs a paired Studio PR) to them. Then delete `semantic*.c`, `semantic_recovery.c`, `identity.c`, `component.c`, and the C halves of `src/authoring/source_*`.
6. **Retire the C leaf helpers** (N). `lexical_binding`, `numeric`, `constant_proof`, `scalar_constant`, `unit_algebra`, `named_binding`, ... once nothing links `chibi_language`. Remove `chibi_language`/`chibi_compiler` from `sdk/CMakeLists.txt`, `sdk/contracts.cmake`, `Targets.chibi` (~484-502), `tools/package_sdk.py`.
7. **Authored parser, last** (A, largest). Replace `compiler_syntax_parse` and `script_parse`: new native-free path changes the target's native list, so the artifact must be regenerated and `target` in `bootstrap.json` changes (hash of natives/ABI). Then delete `parser.c`, `scanner.c`, `grammar.js`, `syntax.c`, `tree-sitter.json`, `chibi_language_syntax`, `chibi_syntax_runtime`, and migrate the `script_parse` users in `src/assets/*` (migrations, `new_asset.c`), `source_format.c`, Studio. Needs a bootstrap that can parse its own sources with the authored parser, i.e. the usual stage-1/stage-2 reproduction proof (`make test-bootstrap`, `make test-recovery`).
8. **Drop stage zero from the bootstrap tests** (A). `make test-bootstrap` currently "prepares stage one with stage zero" (docs/self-hosting-build.md "Bootstrap artifact", step 1); rebase it on the checked artifact (`make bootstrap-recover`), remove `make stage-zero-build` and the CMake recovery path (docs lines 235, 312, 343).

Safe to parallelize: step 0, step 1, and step 3 across different modules (distinct C and Chibi files; each A step still serializes on the artifact). Step 5 can proceed independently of 2-4 only if its modules live outside the compiler closure.

## 5. Risks and costs

- 113 test files; ~17 differential drivers must be converted to authored tests or deleted with their module; order them with section 4.
- The 1.7 MB `parser.c` is generated from `grammar.js`; any grammar change is itself an artifact-affecting step until step 7.
- The ported modules are unproven on the production path; wiring them (step 3) is where regressions in diagnostics and recovery appear. Differential equality must run on both prepared backends.
- Native AOT census (docs/native-compiler-census.md): only 131 of 700 compiler functions pass the scalar native producer; irrelevant to retirement but affects any "compile the compiler natively" claim.
- Studio is outside this repo; steps 5 and 7 need paired PRs and an SDK repackage.

## 6. Unverified

- Whether `--resources-only` skips analysis; which asset kinds use the self-hosted asset compiler (only `source_costs.c:114` confirmed).
- Linux/Windows link lists in `Targets.chibi` (`LinuxLinks`) and wasm builds; `src/host/serve.c`, `export.c` consumers.
- That the language-gap list (arrays in records, optionals, `comptime`) is current; it comes from `docs/language-friction.md` and a skim of `Compile.chibi`/`Steps.chibi`, and later PRs (#808 records, #810 generics) may have changed it.
- Whether the bootstrap closure edit rules require `make bootstrap-artifact` for every `tools/compiler` edit or only when `minimum`/target change (docs say the latter; recent practice says PRs touching the closure are serialized on the artifact).
- Full call-site list inside `package_tests.c` and `source_package_prepare.c` beyond the cited lines.
