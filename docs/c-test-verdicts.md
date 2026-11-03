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
| `test/language/text_format.c` | 2 | `src/runtime/text_format.c`, `src/types/value_format.c` | retired; authored `test/native_aot/text_format` preserves scalar specifiers, owned results and array/map count summaries |
| `test/language/host_packages_compiler.c` | 2 | `src/host/clock_package`, grants, the compiler host | the unused C program-service catalog case is retired; native formatting/target checks remain, and `base/unknown_parameter_type` preserves its missing-type rejection. Clock cases remain until authored `host:clock` behavior has a native replacement. |
| `test/language/native_census.c`, `census_tree.h` | 3 | the C census flattener over CollectScalar | retired after the combined native compiler reproduced byte for byte; the historical report remains |
| `test/language/program_session.c` | 11 | `src/authoring/program.c` sessions | goes, except 3 Edit-and-Continue cases |
| `test/language/package_compiler_chibi.c`, `scalar_host.c` | 6, 0 | native backend execution through the C compiler host | six backend cases retired through `test/native_aot/backend`, retained generations and process supervision; `scalar_host.c` is retired with the obsolete C census |
| `test/language/parallel.c`, `main.c`, `backend.h`, `compiler_cache.h` | 9, 0 | the C test runner | goes |
| `test/runtime/whole_integer.c` (retired 2026-11-02) | 3 | hand-built integer IR, bytecode against SLJIT | retired; admitted integer rules remain in the authored all-width arithmetic suite and native backend; exact integer math in this file used BF-only instructions, not exact integer source overloads |
| `test/runtime/whole_conversion.c` (retired 2026-11-02) | 1 | typed scalar selection through hand-built IR | native `math_builtin/Select.chibi` preserves bool/u8/i32/i64/f32/f64 selection, both conditions, NaN and signed zero at O0/O2; its unused conversion helpers leave with it |
| `test/runtime/whole_math.c` (retired 2026-11-02) | 2 | hand-built math IR, bytecode against SLJIT | native `math_builtin/Edges.chibi` preserves admitted source special values and live results at O0/O2; unsupported BF-only f64/log10 shapes retire without admitting source overloads |
| `test/runtime/persistent_store.c` | 1 | `execution_program_set_state` | goes, except retained records across a swap |
| `test/runtime/reference_cave.c`, `reference_cave.h` (retired 2026-11-02) | 0 | a helper with no live caller | retired with its unused test-driver source entry |
| `test/support/arena.c`, `asset_value.c` (retired 2026-11-02) | 5, 3 | C allocator internals and `av_doc` ownership, cloning and JSON representation | retired; production support and asset libraries remain for their live C consumers |
| `test/support/reflected_types.c` | 0 | C reflected type hooks used by the source-runtime tests | goes with its remaining consumers |
| `test/support/fmt.c` (retired 2026-11-02) | 2 | `third_party/fmt` behind `src/runtime/text_format.c` | retired; native `text-format` covers positional, typed and aligned arguments, escaped braces and empty/missing placeholders; the vendored C library remains for its live runtime callers |
| `test/host/process.c`, `state_file.c`, `concurrent.c`, `main.c` (retired 2026-11-02) | 1, 1, 0, 0 | `src/host` | unselected utilities retired with their graph targets; unused `src/host/state_file.c/.h` retired with its only test caller |
| `test/parallel.c` | 0 | the pico_unit scheduler | goes |
| `test/native_aot/targets/run.c` | 2 | LLVM-built bundles on macOS and Windows | survives, since no VM is in it; it is first-party C, so it becomes a native Chibi runner later |
| `test/tools/native_values.c` (retired 2026-11-02) | 1 | C native-capability lookup and `pin_value` ABI | retired; canonical source conversions remain in the authored native suites |
| `test/tools/types.c`, `program_consumer.c` | 42, 2 | `src/types`, `src/authoring/program.h` | goes |
| `test/tools/compiler_bootstrap.c` | 17 | the bytecode seed loader | goes with the bytecode seed |
| `test/tools/wasm_browser.c`, `browser.c`, `browser.h` (retired 2026-11-02) | 1 | the C VM built to wasm and its otherwise unused browser launcher | retired with the selector and graph sources; the live LLVM Wasm compile-only service has separate tests |
| `test/tools/case_timings_test.c`, `case_timings.c`, `fixture.c`, `policy_worker.c`, `main.c` | 9, 0 | the C test runner and its workers | goes |
| `test/tools/box3d.c` | 2 | the third-party box3d library | goes with the C engine build |
| `test/tools/collab.c` (retired 2026-11-02) | 5 | the Node collab server | authority, readiness, logs and transport failures moved to `tools/collab/server/server.test.mjs`; `collab`, `short` and `native-tools` select them |
| `test/tools/web_storage.c` | 19 | chibi-studio's `web_storage.js` | move to Node tests in its owning repository |
| `test/tools/build_process.c` (retired) | 53 | the C build host's natives | public behavior covered by native OS/CLI/supervisor suites; private contracts retired |
| `test/tools/chibi_cli.c` (retired 2026-11-02) | 24 | `apps/chibi` launched by the C apphost | native CLI behavior is covered by `ChibiCliTests.chibi` and `test/native_cli/foreground.py`; launcher/VM mechanics leave with the host |
| `test/tools/native_workloads.c` (retired 2026-11-02) | 2 | `chibi build --native` end to end | checksums, timings and detached-descendant cleanup run through the authored native workload benchmark |

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

The native build runner now owns ordinary builds and test selectors. The 53-case
private `build_process.c` harness and its three selectors are retired. Its live
process and file contracts are covered by these native suites, the CLI foreground
tests, and the Linux workload supervisor controls. The original cases include:
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

The file suite also checks binary Copy over an existing file, its old inode and
permission bits, failure preservation and temporary cleanup; nonempty Publish
preserves payload and destination identities on rejection. It rejects the same
directory under both identical and alternate path spellings. Stamp follows links,
rejects dangling links and distinguishes files, and List includes hidden names.

Private build-host implementation contracts retire with this harness: pipe wait
order, its job/result tables, bounded buffers, environment allowlists, VM quotas,
recovery deadlines and private namespace are not native API requirements. The
native process and CLI tests cover the surviving cancellation and child lifetime
behavior. `policy_worker` remains only as a graph executable-staging fixture.

`core:os.WriteText` and `WriteBytes` are truncating writes; they do not inherit the
private `build.write_file` atomic replacement contract. Accepted compiler outputs
use a partial file and Rename; compiler and build caches use a reserved stage and
Publish; fresh SDK publication uses a reserved stage and Publish. SDK overlays
copy each file through Copy's atomic replacement. Mutable private build metadata
and `.chibi-record` receipts are written directly: failed writes stop the build,
and incomplete receipts cause a rebuild. This does not make an SDK overlay a
transaction over the whole prefix. Explicit VM comparison and recovery still
retain their host implementation until the final VM retirement gate.

### The CLI: native public commands (ported)

`chibi-cli` runs `test/graph/ChibiCliTests.chibi` through the native compiler, then
`test/native_cli/foreground.py`. `chibi-cli-installed` runs the same suite against an
installed SDK; `chibi-cli-process` runs only the POSIX process cases. The C CLI harness,
its tooling-executable echo mode and its source registration are retired.

`ChibiCliTests.chibi` drives the built or installed native `chibi`:

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

  Native test workers preserve PASS/FAIL reporting and physical assertion locations.
- `chibi run --watch`:
  - `program_watch_tracks_siblings_and_keeps_imported_state`;
  - `program_watch_patches_a_running_frame_and_keeps_state`, including entry removal,
    rejected edits and retained-state reset on restoration;
  - from `program_session.c`: a rejected source or entry keeps the accepted program, a
    broken edit keeps state, and an unreachable package's error is never reported.
- POSIX child ownership and terminals (`test/native_cli/foreground.py`):
  - the four parent-only INT/TERM cases, including a child and descendant that ignore signals;
  - the six ordinary/watch terminal cases, typed input, Ctrl-Z and resume, Ctrl-C,
    redirected stdin with `/dev/tty`, and a prior exited child;
  - signal exit statuses, watch cancellation status 0, and child/root group cleanup.

The removed C cases that checked launcher compilation reuse, native-driver wrapper argv,
VM-only package filtering/core admission, independent C program sessions and retired SDK
companions described the retired launcher/host. Relocation still runs a native program
from a copied SDK; editing `apps/chibi` no longer overrides the native executable.

- `chibi build --native` of `test/native_aot/workloads` at O0 and O2
  (`native_workloads_build_and_run`): ported as `BuildTests.chibi`'s
  `BuildsTheWorkloads`, which checks each kernel's checksum. The authored
  `test/native_aot/workloads/Benchmark.chibi` also preserves all eight timing
  rows, per-child RSS, executable size and median-of-three runs. Its isolated
  Linux worker preserves grouped and `setsid`-detached cleanup after timeouts,
  early leader exit and interruption. The C file, dispatcher and source-list
  entry are retired; the selector keeps its name and Linux-only platform contract.

### The scalar producer: `test/native_aot/scalar_producer`

The eight producer behavior cases now run through the native compiler with its
current private target, instruction names and native catalog source. The runner
covers fifteen real source projections, twelve row-kind sequences, all 169
malformed edit sequences and their precise diagnostics, package/collection
boundaries, unsorted scoped-reference files, library IDs and retained scalar
manifests. It also checks every authored scalar corpus case, including deep
operands. The C producer selector and its unused adapters are removed; the same
`language-package-compiler-chibi-scalar` tool selector invokes the native runner.

One stale negative input changed: conversion selector `2` is now the supported
`CheckedChar` selector, so unknown-mode rejection uses `3`. The separate
character suite proves selector `2` acceptance and runtime failures.

The native emitter's C transport test was already covered by authored call and
invalid-callee tests; its transport wrapper and the C oracle-helper self-test
were harness code. The six backend execution cases are now retired through
[the native backend fixture](../test/native_aot/backend/README.md), retained
generations and process supervision. The native bounds callback preserves the
original index/length diagnostic and status134 without allocating. The obsolete C census and `scalar_host.c` are now retired. The complete native
compiler reproduced at stages three and four from the combined `813b93875` source;
445 compiler tests and managed-record, pattern, array and generation proofs pass.

## Owner decisions

- **Integer `abs`, `min`, `max` and `clamp`.** The retired `whole_integer.c`
  asserted exact integer BF instructions. Source math converts integer operands
  to `number`; retiring this C test does not admit exact integer overloads.
- **Math special values.** Native `math_builtin/Edges.chibi` pins classification
  and zero signs for admitted f32 builtins and f32/f64 exp/pow. Finite angles
  use tolerances. Unsupported BF-only f64/log10 shapes in the retired
  `whole_math.c` do not admit source overloads. Its clamp expression and the
  current source clamp keep their distinct operand order and edge results.
