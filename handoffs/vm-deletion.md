# VM deletion map (core-math, 2026-11-01, main 1c3b8969f + this PR's dead-code deletion)

Owner's rule: delete the VM wholesale once a native seed exists. This lists what still needs it, in the order that
lets the deletion land right after the seed. Counts come from the include graph of every C/H file outside archive/
and third_party/ (script: /tmp/w22-vm/includes.py, reproducible with `git grep '#include "(execution|runtime|types|host|support)/'`),
from tools/graph/engine/Targets.chibi (targets, sources, links), the workflows, tools/tests.tsv and apps/chibi.
Verify a row before acting on it: "needs" means a source file includes a header of the directory, or a binary links it.

## 1. What "the VM" is

| Directory | Files | Lines | What | Includes (inside the VM) |
|---|---:|---:|---|---|
| src/execution | 87 (86 after this PR) | 28,876 | bytecode VM, SLJIT and wasm JIT, prepared format, program/module, borrow and range optimizers, suspend and reload, `native.c` (loads native libraries into the VM) | support, types |
| src/runtime | 58 | 8,189 | the native capability catalog and its definition packs (text, array, arithmetic, vectors, sets, conversions, comparisons, selection, math), event catalog, task pool | execution, support, types |
| src/types | 33 | 10,707 | VM value layer: types, schema, catalog, array/text/map/record/union values, value wire format, value documents | support |
| src/host | 35 | 7,209 | the C hosts: `build_host*`/`build_*` (the build natives), `application*` (the `chibi` program host), `compile_*`/`compile_tests`, `process`, `clock`, `native_*`, `chibi_main` | execution, runtime, support, types |
| src/support | 20 | 3,528 | arena, xalloc, file_io, binary_cache, sha256, asset_value/asset_json | none |

Dependencies among them: host → {execution, runtime, support, types}; runtime → {execution, support, types};
execution → {support, types}; types → support. So the deletion order inside the VM is host, runtime, execution,
types, support, and the last two have non-VM users (section 4).

## 2. Binaries: who needs the VM and what replaces it

Targets that link VM code (from `Links`/`Files`): chibi-compiler-host, chibi-build-host, chibi, chibi-compile,
test_language, test_tools, test_host, concurrent_runner, policy_worker, benchmark_trials (host/support only). Only
`reflect` links types/support/capabilities but no execution.

| Binary | Role today | VM use | Replacement | Blocker / owner |
|---|---|---|---|---|
| chibi-compiler-host | runs the VM-hosted compiler artifact (`compiler.chibip`): recovery, `Native` (tools/native_aot), asset compile, SDK validation (`--validate`), census/test harness (`compiler_bootstrap_load`) | is the VM | the native compiler executable (stage one/two, `tools/compiler/native_driver`) | the native seed (cleanup-781); the driver must also do what `Native` does that Main.chibi does not yet: `-g` line tables, shared libraries (`Libraries`), vendored archives and `ResolveLibraries`, receipts and the output cache, `--target` cross links, static/dynamic choice by context |
| chibi-build-host | runs `Build.chibi`/`Driver.chibi`/build graphs on the VM with the C `build.*` natives | is the VM (+ build natives) | the native build runner (docs/native-build-runner.md) over `core:os` | records |
| chibi (CLI) | `chibi build --native / test / run / --watch` | `application*` host runs apps/chibi (precompiled by chibi-compile) and user programs on the VM; `--watch` is VM reload with retained state | a native `chibi` built from apps/chibi by the native driver (cwaldner/embedded-cli-programs); `run` = compile natively and exec; `test` = native `@test` runner; `--watch` = native Edit and Continue (errors) | embedded-cli branch; native test runner; E&C host |
| chibi-compile | precompiles Chibi packages (apps/chibi, tools) to artifacts the VM loads; asset compiler | VM artifacts | gone with the VM; asset step needs an owner (below) | decision |
| test_language, test_tools, test_host, concurrent_runner, policy_worker, benchmark_trials | the C tests | link the VM | port or delete (old-formats, docs/c-test-verdicts.md) | old-formats |
| reflect | libclang tool that emits the engine metadata (types, capabilities, support only) | none (types/support/capabilities) | keep until foreign import replaces the metadata, then delete with src/types, src/capabilities | portable-libs / builtin-math |

## 3. Non-test C outside the five directories that includes them

| Files | Needs | Why | Fate |
|---|---|---|---|
| src/toolchain (9 files, 3,711 lines): compiler_bootstrap, compiler_host, compiler_main, compiler_artifact, compiler_target | execution, runtime, support, types | the compiler host and the seed loader | delete with the VM; `compiler_target.c` (the catalog JSON) is replaced by `tools/compiler/native_driver/Catalog.chibi` (data, generated from `--target-json`; a test that keeps it equal to the host's is still owed, and once the host is gone it becomes the source of truth) |
| src/authoring (12 files, 3,345 lines): program, program_service, package_tests, package_compile, asset_compiler_host, source_package_files | execution, runtime, support, types | the C session API Studio and `--watch` share, the test runner, the package compile entry, the asset compiler host | delete with the VM, except what the native test runner and the asset step still need (below) |
| src/capabilities (9 files, 2,399 lines) | types, support | the native-function catalog the VM and the compiler host read | the native lane does not use it (natives are declared with `foreign`); delete after toolchain/runtime |
| tools/reflect (8 files) | types, support | emits metadata | see reflect above |
| tools/benchmark.c | host | process benchmark | delete |

## 4. Non-C users

- **CI** (.github/workflows): `compiler-recovery.yml` (VM self-build on 3 platforms; replaced by the native seed recovery),
  `compiler-runtime-seed.yml` (publishes the VM runtime seeds, `chibi-compiler-host` per platform; deleted with the VM),
  `bootstrap-host.yml` (builds and pins the C build host `hosts.tsv`; replaced by the native build runner),
  `ci.yml` jobs "Engine build and native tests" (+ macOS), "Native scalar modules", "Graph SDK packaging", "chibi build alone"
  (all build and run the C binaries), `linux-graph.yml`, `native-self-host.yml` (keeps; it proves item 1 and needs the
  VM-hosted stage-one build only until the seed exists).
- **Seeds and pins**: `tools/bootstrap/seed.sh`, `seed.tsv`, `hosts.tsv`, `tools/build.sh` (bootstraps a first build host), the
  bytecode seed `compiler.chibip` and bootstrap.json (lists every compiler source). All of this is the VM's.
- **Chibi code that calls VM-only natives**: `build.*` natives are used by core:os's VM bodies (packages/core/os) and by
  tools/toolchain/*.chibi, tools/native_aot (Driver.chibi, the `Native` entry), tools/graph/**. core:os already picks posix
  bodies natively (`when target.Native == 1`), so what remains is the programs, not the library.
- **Compile-time execution** is implemented in Chibi (tools/compiler/package_compiler/Comptime.chibi, generics) and runs
  wherever the compiler runs; it needs no VM once the compiler is native.
- **Docs**: docs/self-hosting-build.md, testing.md, ci-initial-build-latency.md, toolchain-inventory.md and others name the VM
  tools; they change in the deletion PR.

## 5. Tests (old-formats, 2026-11-01; the per-case verdicts are in docs/c-test-verdicts.md)

### C tests deleted with the VM

These are deleted whole, in the same change as the VM. Their subject is the C being
deleted, and no case needs a port first.

- test/language:
  - execution_scope.c, capture.c, ranges.c;
  - record_document.c, record_compatibility.c, record_prepared.c;
  - host_packages_compiler.c, native_census.c, census_tree.h;
  - parallel.c, main.c, backend.h, compiler_cache.h.
- test/runtime: reference_cave.c/.h, which is dead now.
- test/support: arena.c, asset_value.c, reflected_types.c/.h, fmt.c.
- test/host: process.c, state_file.c, concurrent.c, main.c. No row runs them, and
  state_file.c is dead now.
- test/parallel.c/.h.
- test/tools:
  - types.c, native_values.c, program_consumer.c, wasm_browser.c, browser.c/.h;
  - compiler_bootstrap.c/.h, which goes with the bytecode seed;
  - compiler_artifact.h, an orphan whose .c is archived;
  - case_timings.c, case_timings_test.c, fixture.c/.h, policy_worker.c, main.c;
  - box3d.c, which goes with the C engine build.
- test/tools/collab.c and web_storage.c: not VM code. The Node collab server and
  chibi-studio's web_storage.js need Node tests in their owners' repositories, or the
  C is deleted with them.

### C tests deleted once their surviving cases are ported (old-formats, in progress)

All of these are VM-only apart from the cases listed in docs/c-test-verdicts.md, "What
survives". Each one is deleted when its port lands. Until then, deleting the VM deletes
them anyway, and the surviving cases are lost.

- test/language:
  - execution.c: 5 language-rule cases;
  - record_values.c: map keys with a NUL;
  - text_format.c: format specifiers, an owner decision;
  - program_session.c: 3 Edit-and-Continue cases;
  - package_compiler_chibi.c + scalar_host.c/.h + package_compiler_chibi.h: the 7 scalar
    producer cases;
  - float_conversion_cases.h.
- test/runtime: whole_integer.c, whole_conversion.c, whole_math.c, persistent_store.c.
- test/tools:
  - build_process.c: its core:os cases are now ported to test/native_aot/os, which
    native-build runs. Delete the whole file with the C build host. The cases that
    don't port are listed in docs/c-test-verdicts.md.
  - chibi_cli.c: its surviving cases are now ported to test/graph/ChibiCliTests.chibi
    (row chibi-cli-authored). Delete the whole file with the C apphost, along with the
    chibi-cli* rows.
  - native_workloads.c: its checksum case is ported (native-build's
    BuildsTheWorkloads). The file and native-workload-benchmark go when a Chibi benchmark
    measures the timings.
- Kept: test/native_aot/targets/run.c. No VM is in it; CI compiles it with clang to run
  native bundles on macOS and Windows.

### Rows and CI steps that go with them

- tools/tests.tsv rows: every `@build@/test/language/test_language` and
  `@build@/test/tools/test_tools` row:
  - types, execution-scope, arena, persistent-store, program-sdk;
  - chibi-cli-build, chibi-cli-doctor, chibi-cli-process, chibi-cli, chibi-cli-installed;
  - native-workload-benchmark;
  - build-process, build-timings, build-process-files;
  - compiler-bootstrap, bootstrap-recovery, bootstrap-load;
  - collab, whole-integer, whole-conversion, whole-select, whole-math;
  - native-values, text-format, box3d, asset-values, fmt;
  - language-test-parallel, language-ranges;
  - language-package-compiler-chibi-scalar, language-scalar-native;
  - language-native-census, language-native-census-flattening;
  - language-host-packages-compiler;
  - language-record-compatibility, language-record-document, language-record-values;
  - language-program-session, language-program, language-execution;
  - wasm-browser, web-storage, native-tools, short.

  Also update the groups that name them (native, projects, language, compiler,
  benchmarks).
- CI:
  - ci.yml builds `--target test_tools` and checks that the test_tools and test_language
    drivers exist (lines ~108, 158, 333).
  - ci.yml runs `build/test/tools/test_tools --case-timings` (~141, 323) and the `short`
    and `build-timings` rows.
  - linux-graph.yml's driver checks (~102).
  - The CHIBI_NATIVE_BUNDLE writer in language-scalar-native feeds native-targets. It
    needs a replacement writer, because native-build and native-link also write to the
    bundle.

## 6. Order of landing, starting at the native seed

Gate 0, before any VM deletion:
1. The native seed is a Release asset pinned by SHA-256 for linux-x64 and macos-arm64 and recovery starts from it
   (cleanup-781). Windows follows (strings-ints).
2. `chibi build --native` is the native driver, not the VM-side `Native` script. Features still only in
   tools/native_aot/driver (list in section 2). Without them nothing can delete `chibi-compiler-host`.
3. The native build runner builds the engine (records) so `chibi-build-host` and `bootstrap-host.yml` go.
4. A native `chibi` (embedded CLI) with native `run`, `test` and `--watch` (errors' native Edit and Continue).
5. The asset compile path has a native owner or is archived (`asset_compiler_host.c`, `package_compile.c`,
   `asset_compiler_packaged.c`, `chibi_asset_compiler_host`); I did not find an owner. Decision needed.
6. The C tests are ported or listed for deletion (old-formats).

Then one deletion series, each step compiling and green:
- A. Delete the CI jobs and workflows that build the VM-hosted compiler (`compiler-recovery.yml`, `compiler-runtime-seed.yml`,
  `bootstrap-host.yml`, the VM build/test steps of `ci.yml`), `tools/bootstrap/{seed,hosts}*`, the bytecode seed.
- B. Delete the C tests that need the VM, then test_language/test_tools/test_host/concurrent_runner/policy_worker/benchmark_trials.
- C. Delete the binaries chibi-build-host, chibi-compiler-host, chibi-compile and src/host + src/toolchain + src/authoring
  (with their Targets entries; update the SDK file list in tools/toolchain/SDK.chibi).
- D. Delete src/runtime, then src/execution (nothing outside C includes them after C).
- E. Delete src/capabilities and src/types, and src/support except what `reflect` needs (types, support, capabilities, if it stays).
- F. Delete reflect with the last of them if foreign import replaces the metadata.
- G. docs.
Because bootstrap.json lists every compiler source, any compiler source rename or deletion in this series needs a seed
refresh in the same change (seed rules in sessions.md).

## 7. Already dead (deleted in the accompanying PR)

- src/execution/request.c and request.h (165 lines): 15 unreferenced functions and an unused header; module.c's include and
  the Targets entries went with them. Build verified (`chibi_execution`, `test_language`, release, macOS).
- Not deleted but unreferenced by name anywhere (macro-built tables may hide a use, so check before deleting): 143 other
  functions across 36 files, mostly in src/runtime (native_array_*, native_arithmetic_*, vectors, sets, conversions),
  src/execution/function_builder.c (14), src/support/asset_value.c (12), src/host/build_values.c (5), src/runtime/event_catalog.c (5).
  They die with their files at step C/D, so no piecemeal deletion.
- No other C file is unlisted by the graph; the only C file no target names is test/native_aot/targets/run.c, which the CI
  bundle runner compiles.

## 8. Not verified

- Which of the 143 unreferenced functions are used through token-pasted names.
- Whether Studio (chibi-studio) links src/authoring (it consumes the SDK; the SDK installs headers listed in Targets.chibi
  `PublicHeaders`). Deleting those headers breaks Studio, which is allowed by the rules, but name it in the PR.
- The `chibi` CLI's `--watch` tests (test/tools/chibi_cli.c, 70 cases) depend on the VM reload path; the native E&C host must
  cover them before they are dropped.

## 9. Native gaps that gate deletion (coordinator, 2026-11-01, from old-formats' C test port)

Programs that work on the VM today break if the VM goes before these close:
- Unary minus on a float, and `&` of a local's field, are rejected by the producer. Owner: sdk-cmake.
- Maps are not native. No owner yet; the compiler uses no maps, but games and tools do.
- Native Edit and Continue retains only `int` state, so records and strings in retained state are reset. Owner:
  errors, the Edit and Continue owner.
- Native `format_text` has only `{}`, while the VM also has `{0}`, `{:.2f}` and `{:x}`. Whether to keep them is the
  owner's decision.
- The asset compiler path (chibi_asset_compiler_host, asset_compiler_host*, package_compile.c, asset_compiler_packaged.c)
  is the packaged package compiler that `chibi run` (src/authoring/program.c) and `chibi test` (src/host/compile_tests.c)
  compile through. It is not dead. It goes with gate 4, the native `chibi`: old-formats deletes it there or right after.
