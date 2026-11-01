# C test files after the VM

Goal item 4. Programs run only as native code once the native seed lands. After that,
the C VM, runtime, types and hosts are deleted (`src/execution`, `src/runtime`,
`src/types`, `src/host`), along with the C build host. Each case under `test/` has one
of two verdicts:
- **Goes with the VM.** Its subject is that C.
- **Survives.** It checks a behavior that a Chibi program, a `core:` package or the
  `chibi` CLI still has. A surviving case becomes an authored test, and its C case is
  deleted when the port lands.

`handoffs/vm-deletion.md` lists the files, `tools/tests.tsv` rows and CI steps that are
deleted with the VM.

This was checked on 2026-11-01 against `main` at 89ec4cc8f, by reading every case body.
A case's subject is what a failing assertion would blame. Case counts are `TEST_CASE`
definitions; the earlier table also counted the `RUN_TEST_CASE` lines.

## Verdicts

| File | Cases | Subject | Verdict |
| --- | ---: | --- | --- |
| `test/language/execution.c` | 54 | hand-built `behavior_function` IR on `src/execution` | goes; its 5 language-rule cases are ported |
| `test/language/execution_scope.c`, `capture.c`, `ranges.c` | 8, 6, 7 | VM scopes, budgets, intrinsics, the interval optimizer | goes |
| `test/language/record_values.c`, `record_document.c`, `record_compatibility.c`, `record_prepared.c` | 15, 5, 1, 0 | `src/types` records, value documents, legacy array callbacks | goes (`text_keys_preserve_suffix_after_nul` is ported) |
| `test/language/text_format.c` | 2 | `src/runtime/text_format.c`, `src/types/value_format.c` | goes, except the format specifiers (see "Owner decisions") |
| `test/language/host_packages_compiler.c` | 3 | `src/host/clock_package`, grants, the compiler host | goes |
| `test/language/native_census.c`, `census_tree.h` | 3 | the C census flattener over CollectScalar | goes; the census is obsolete once the compiler runs natively |
| `test/language/program_session.c` | 11 | `src/authoring/program.c` sessions | goes, except 3 Edit-and-Continue cases |
| `test/language/package_compiler_chibi.c`, `scalar_host.c` | 16, 0 | compiler packages through the C compiler host | 7 cases survive (scalar rows, `Validate`, the corpus); the rest are covered or are host or harness code |
| `test/language/parallel.c`, `main.c`, `backend.h`, `compiler_cache.h` | 9, 0 | the C test runner | goes |
| `test/runtime/whole_integer.c`, `whole_conversion.c`, `whole_math.c` | 3, 1, 2 | hand-built IR, bytecode against SLJIT | goes; the conversion, negation and comparison rules are ported, and integer `abs`/`min`/`max`/`clamp` and the math specials wait on owner decisions |
| `test/runtime/persistent_store.c` | 1 | `execution_program_set_state` | goes, except retained records across a swap |
| `test/runtime/reference_cave.c` | 0 | a helper with no live caller | goes (dead) |
| `test/support/arena.c`, `asset_value.c`, `reflected_types.c` | 5, 3, 0 | `src/support` used only by the VM | goes |
| `test/support/fmt.c` | 2 | `third_party/fmt` behind `src/runtime/text_format.c` | goes; `{}` holes are covered by `FormatsTextWithoutTheVM` |
| `test/host/process.c`, `state_file.c`, `concurrent.c`, `main.c` | 1, 1, 0, 0 | `src/host` | goes (no row runs them; `state_file.c` is dead) |
| `test/parallel.c` | 0 | the pico_unit scheduler | goes |
| `test/native_aot/targets/run.c` | 2 | LLVM-built bundles on macOS and Windows | survives, since no VM is in it; it is first-party C, so it becomes a native Chibi runner later |
| `test/tools/types.c`, `native_values.c`, `program_consumer.c` | 42, 1, 2 | `src/types`, `src/runtime`, `src/authoring/program.h` | goes |
| `test/tools/compiler_bootstrap.c` | 17 | the bytecode seed loader | goes with the bytecode seed |
| `test/tools/wasm_browser.c` | 1 | the C VM built to wasm | goes |
| `test/tools/case_timings_test.c`, `case_timings.c`, `fixture.c`, `policy_worker.c`, `main.c`, `browser.c` | 9, 0 | the C test runner and its workers | goes |
| `test/tools/box3d.c` | 2 | the third-party box3d library | goes with the C engine build |
| `test/tools/collab.c`, `web_storage.c` | 5, 19 | the Node collab server; chibi-studio's `web_storage.js` | not Chibi: these move to Node tests in their owners' repositories |
| `test/tools/build_process.c` | 53 | the C build host's natives | goes, except 16 core:os cases |
| `test/tools/chibi_cli.c` | 24 | `apps/chibi` launched by the C apphost | 17 cases survive as CLI behavior |
| `test/tools/native_workloads.c` | 2 | `chibi build --native` end to end | `native_workloads_build_and_run` survives |

## What survives, and where it goes

Each group lands as authored tests, and its C cases are deleted in the same change.

### Language rules: `test/compiler/samples` (ported)

`chibi test` runs these samples on the VM today and will run them natively once the VM
is gone, so they pin the rules both have to keep (`docs/code-language.md`, "Numbers").
`native-build`'s `RunsTheForeignSamples` already runs the four `types` samples natively.

| Sample | Ported from | Native today |
| --- | --- | --- |
| `types/float_to_integer_conversions_truncate_clamp_and_map_nan_to_zero` | `execution_numeric_casts_apply_exact_wrap_and_clamp`, `public_integer_saturating_casts_and_exact_identity`, `public_numeric_conversions_preserve_edges_and_real_widths` | passes |
| `types/integer_to_float_conversions_keep_magnitude_and_sign` | the same three | passes |
| `types/signed_negation_and_comparisons_at_the_edges` | `public_integer_wrapping_division_and_comparisons` | passes |
| `types/bitwise_operators_keep_each_width` | `execution_integer_bitwise_preserves_width_and_valid_shifts` | passes |
| `collections/pointer_equality_is_identity_of_location` | `execution_pointer_comparisons_use_location_identity` | not yet: the scalar proof rejects `&` of a local's field |
| `collections/map_keys_differ_after_a_nul_byte` | `text_keys_preserve_suffix_after_nul` | not yet: maps are not native |
| 19 `math` samples on f64-to-`number` narrowing (`narrowing_f64_to_number_*`, `number_*`, `constant_number_*`, `explicit_number_*`, `float_literals_*`, `narrowing_an_f64_constant_*`, `returning_an_f64_constant_*`) | the 42 rows of `float_conversion_cases.h` | not run natively yet |

Native gap the port found: unary minus on a float (`-x` for an `f64` or `number`) is
rejected with "scalar arithmetic differs from its number operands". The samples write
`x * -1.0` so that they run natively today.

The C cases above, `execution_integer_shift_past_the_width_shifts_every_bit_out` (already
covered by `types/shift_counts_clamp`), `execution_string_equality_and_iteration_use_full_bytes`
(covered by `base/byte_preserving_strings`; the string cursor is a VM intrinsic),
`float_conversion_cases.h` and the `whole-conversion` row are deleted.

### core:os: `test/native_aot/os` (ported; `native-build` runs it)

`test/native_aot/os/processes` and `test/native_aot/os/files` test the native core:os,
because the VM one goes. `RunsTheForeignSamples` runs them natively on Linux and macOS.
The two core:os implementations already differ in three ways:
- a native write replaces a file in place;
- an unknown handle returns -1;
- a program that cannot start gives 127.

What they cover:
- **processes:**
  - exit statuses, signals as negative numbers, and 127;
  - arguments passed unchanged;
  - separate stdout and stderr;
  - NUL bytes and a whole 1 MiB of output in `Capture`;
  - `Start`, `Wait` and `StillRunning`;
  - `Kill`, including the processes the program started;
  - two programs at once;
  - `FindExecutable`, `Root` and `Platform`.
- **files:**
  - `Kind` of a folder, file, link, dangling link and FIFO, a missing path, and a path
    below a file;
  - `List` in byte order;
  - `Stamp`;
  - `Copy` keeping the mode and following links;
  - `Rename`;
  - `Remove` of a full folder, a link, or nothing;
  - `Reserve` and `Publish` refusing any existing path;
  - `MakeDirectory` and `WriteText` failures;
  - reads of a folder, FIFO or missing path;
  - `RealPath`, `HashText`, and `HashFile` of a million `a`s.

The C build host is still in use, so its cases stay until the native build runner replaces
it. These are the ported `build_process.c` cases, which go with it:
- `exits_and_spawn_errors`;
- `argv_cwd_and_explicit_environment`;
- `descendants_stop_on_timeout_and_leader_exit`;
- `private_waits_have_limits_and_steps_can_be_killed`;
- `private_started_steps_run_together`;
- `private_last_output_preserves_bytes_and_refuses_truncation`;
- `private_step_output_fills_one_text_value_and_probes_stay_unreported`;
- `private_values_and_process_snapshots_survive_later_calls`;
- `private_metadata_reports_kinds_stamps_and_sorted_entries`;
- `private_copies_renames_and_removals_leave_whole_files`;
- `private_directory_publication_refuses_existing_paths`;
- `private_file_reads_preserve_bytes_and_reject_invalid_files`;
- `private_hashes_match_sha256_vectors`.

They don't port:
- `private_waits_take_output_in_wait_order`: a native piped child writes to a file, so
  it has no pipe to fill.
- `private_atomic_writes_preserve_target_on_rename_failure`: the native write is not
  atomic.
- `Getenv` of an empty variable: core:os cannot set a variable for a child.
- `private_waits_refuse_unknown_and_repeated_handles` and
  `private_unwaited_and_late_steps_stop`: the two implementations disagree, so these
  wait on an owner decision.

### The CLI: a Chibi suite that drives the built `chibi`, like `BuildTests.chibi`

- `version` and `doctor`, apart from the backend line:
  - `version_reads_the_build_identity`;
  - `version_of_the_built_launcher`;
  - `doctor_reports_each_capability`;
  - `doctor_of_the_built_launcher`;
  - `installed_identity_and_doctor`.
- `chibi run`:
  - `run_dispatches_standalone_programs`;
  - `program_main_runs_from_files_and_directories`;
  - `program_sources_link_packages_and_select_the_root_entry`;
  - `program_failures_report_entry_and_source_errors`;
  - the relocation part of `public_cli_is_a_relocatable_ordinary_application`;
  - `--help`;
  - `program_cancellation_stops_busy_main_and_tests`.
- `chibi test`:
  - `test_runs_shared_package_tests`;
  - `installed_package_commands`;
  - the reporting part of `compiler_test_reports_each_test`.

  Its PASS/FAIL reporting is C today (`src/host/compile_tests.c`), so these tests are the
  specification for its replacement.
- `chibi run --watch`:
  - `program_watch_tracks_siblings_and_keeps_imported_state`;
  - the uncovered parts of `program_watch_patches_a_running_frame_and_keeps_state`;
  - from `program_session.c`: a rejected source or entry keeps the accepted program, a
    broken edit keeps state, and an unreachable package's error is never reported.
- `chibi build --native` of `test/native_aot/workloads` at O0 and O2
  (`native_workloads_build_and_run`): ported as `BuildTests.chibi`'s
  `BuildsTheWorkloads`, which checks each kernel's checksum. The timings stay with
  `native-workload-benchmark` until a Chibi benchmark replaces it.

### The scalar producer: `tools/compiler/package_compiler/tests`

These are the rows and `Validate` rejections, the library rows and rejections, the corpus
in `test/scalar_facts/cases`, and the producer half of retained library states (7
`package_compiler_chibi.c` cases). `CollectScalar` takes the host's description of itself:
- the `Target` record;
- the instruction names;
- the native catalog's `Native`, `Port` and identity tables.

An `@test` has no way to ask for these. Natively, the compiler has the same description as
constants for its destination. These cases port once a `chibi test` program can read it.

## Owner decisions

- **`format_text` specifiers.** The VM's `format_text` takes `{0}`, `{1}`, `{:.2f}`,
  `{:x}` and `{:<5.3s}` (`fmt.c`, `text_format.c`). The native one takes only `{}`. Either
  native code gains the specifiers and they become samples, or they leave the language
  along with these tests.
- **Integer `abs`, `min`, `max` and `clamp`.** `whole_integer.c` asserts them on graph
  nodes, but the source language rejects math functions on integers.
- **IEEE special values in `core:math`.** `whole_math.c` asserts tan, asin, acos, atan,
  exp, log, log10, pow, atan2 and clamp at NaN, ±inf, ±0, subnormals and 1e20. The samples
  pin only finite values. Should the language guarantee libm's results at the specials?
