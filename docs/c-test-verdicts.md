# C test files: what ports to Chibi, what stays

Goal item 4: every case whose subject is Chibi code (compiler packages, graph or
toolchain programs) becomes an authored `@test`; cases whose subject is the C VM,
runtime or hosts go with that C. Checked on 2026-10-31 against `main`, by reading each
file's cases (`TEST_CASE` names, includes and what the assertions exercise).

A case's subject is what a failing assertion would blame. A C test that drives a Chibi
program only to exercise a C host (a process runner, a loader, a limit) is C.

## Verdicts

| File | Cases | Verdict | Why |
| --- | --- | --- | --- |
| `test/language/package_compiler_chibi.c` | 8 | **port**, partly | The scalar producer and consumer cases (`rows_outlive_the_producer`, `rows_span_imported_packages`, `rejects_unions_and_reads_libraries`, `retains_library_states`, `corpus_rows_validate_and_emit`, `native_modules_emit_through_the_consumer`) assert on `scalar_facts` rows, `scalar_llvm` and `llvm_native`, which are Chibi. They need a Chibi-side source for the instruction names and target facts that `CollectScalar` takes from the host (see below). The Linux cases (`native_bundle`, `stack_overflow`, `runs_are_bounded`, `arithmetic_is_total`, `arrays_run`, `edit_and_continue`) run LLVM tools and child processes: keep. |
| `test/language/native_census.c` | 7 | port later | Subject is `CollectScalar` over the compiler's own functions; the harness is C only for tree-sitter-free flattening and report writing. Port once the target-facts native exists and the flattening moves to a Chibi tool. |
| `test/language/scalar_host.c` | 0 | keep | The JSON bridge the two files above use; goes when they do. |
| `test/language/host_packages_compiler.c` | 6 | keep | Subject is the `host:clock` native boundary, grants and the compiler host's compile entry (C); `core:time` contracts already run as authored tests. |
| `test/language/program_session.c` | 22 | keep | `authoring/program.c`, the C session API Studio and `--watch` share. |
| `test/language/execution.c`, `execution_scope.c` | 156, 18 | keep | The C VM: verifier, costs, scopes, intrinsics. No case compiles Chibi source any more. |
| `test/language/capture.c`, `ranges.c`, `component_write_ir.c` | 12, 14, 15 | keep | Hand-built `behavior_function`s: C VM and ECS. |
| `test/language/record_values.c`, `record_document.c`, `record_compatibility.c`, `record_prepared.c`, `text_format.c` | 32, 10, 2, 0, 4 | keep | C value, document and text formatting layers. |
| `test/language/parallel.c`, `main.c` | 19, 0 | keep | The C test runner. |
| `test/tools/compiler_bootstrap.c` | 13 | keep | The C seed loader and its manifest checks. |
| `test/tools/chibi_cli.c` | 70 | keep | Process tests of the `chibi` launcher: exit codes, signals, terminal hand-over, relocation. The Chibi programs it runs are fixtures. |
| `test/tools/build_process.c` | 116 | keep | The C build host's natives (process, file, hash, value storage) and its limits. |
| `native_workloads.c` | 4 | keep | `chibi build --native` end to end with LLVM tools and child processes. |
| `test/tools/types.c`, `ecs.c`, `ecs_hierarchy.c`, `ecs_query.c`, `ecs_relationship.c`, `ecs_world.c`, `native_values.c`, `program_consumer.c` | 84, 39, 12, 44, 22, 26, 2, 4 | keep | C types, ECS and the program consumer. |
| `test/tools/scheduler_benchmark.c`, `ecs_query_benchmark.c`, `box3d.c` | 26, 6, 4 | keep | C benchmarks and a third-party library check. |
| `test/tools/execution_policy.c`, `fixture.c`, `case_timings.c`, `case_timings_test.c`, `policy_worker.c`, `main.c` | 12, 0, 0, 19, 0, 0 | keep | The C test infrastructure and execution policy. |
| `test/tools/reflect.c` | 30 | keep | The libclang reflection tool (C). |
| `test/tools/collab.c`, `web_storage.c`, `browser.c`, `wasm_browser.c` | 10, 38, 0, 2 | keep | Node and browser servers and bridges driven from C. |

Nothing in either directory is a pure delete: no case duplicates an authored one.

## The one missing piece for the port

`CollectScalar` takes the host's description of itself: the `Target` record (21 byte
offsets and failure kinds), the instruction names, and the native catalog's `Native`,
`Port` and identity tables. `chibi test` gives a test no way to ask for them, so the
scalar cases go through the compiler host from C (`scalar_host.c`). To port them,
`chibi test`'s testing natives would add one pure native that returns that description
(a `Target` and the three tables), built from `compiler_target_json`, which already
produces exactly these arguments for `CompileWithEffects`. With it, each case is a
`@test` in `tools/compiler/package_compiler/tests/` that calls `CollectScalar` and the
consumers directly, and mutates rows in Chibi instead of through `pin_value`s.
