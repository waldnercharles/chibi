# Parallel test resource audit

The runner schedules **selectors in separate processes**, not individual pico_unit
cases or C threads. A selector retains its existing cases, backend coverage and
child-process budgets. The initial parallel allowlist is deliberately narrow;
unknown selectors remain exclusive. This is not proof that every selector in the
language or compiler groups is independent. Authored `@test` discovery and inferred scheduling run within their owning
selector; they do not require a C registration per test.

Audit basis: source at `bb0ea748ecf30befda6f41853a8bd98f6151cbde`, plus the
`compiler-bootstrap` TMPDIR change described below. This branch also includes
PR #714's prerequisite at `c7ec54b940ddea7a149b94bfc356099b341d0cc1`:
its additional bootstrap bound/recovery cases retain private-root writes, and
the two new root-derived path buffers also use PATH_MAX here. Line references
in this table identify the audited baseline; function names remain the durable
anchors.

| Runner selector | Resource evidence | Child processes and isolation |
| --- | --- | --- |
| `language-package-compiler-chibi-scalar` | Retains its native-boundary cases and embeds `test/native_aot/emitter_calls/*.chibi`. `compiler_fixture_tests` discovers authored tests and schedules proven-pure spans on independent invocation storage for both prepared owners. | **Outer-exclusive:** owns the bounded authored task pool. No LLVM process or output-file path is called; native execution remains a different selector. |
| `language-package-compiler-chibi-behaviors` | `test_language_package_compiler_chibi_behaviors` (`package_compiler_chibi.c:15152`) runs `package_compiler_compiles_behaviors` (`:8318`), using embedded sources and memory-only compiler/ECS state. | No subprocesses or filesystem writes. Process isolation is necessary for the mutable project type catalog and fixture globals. |
| `language-package-compiler-chibi-behaviors-rest` | `test_language_package_compiler_chibi_behaviors_rest` (`package_compiler_chibi.c:15172`) runs the other behavior samples/rejections and per-owner text-state case (`:9317`, `:9388`). Project identities are input strings, not directories created on disk. | No subprocesses or filesystem writes; separate process-owned compiler, type catalog, ECS and pico_unit globals. |
| `language-asset-packaging-chibi` | `test/language/asset_packaging_chibi.c:597`, `test_language_asset_packaging_chibi`, exercises byte packaging and generator JSON in memory. `read_source` (`:359`) reads selected files below absolute `CHIBI_SOURCE_ROOT` using `fopen(..., "rb")`. | No subprocesses or generated-file writes. Source-root access is read-only; changing cwd does not redirect it. |
| `language-asset-compiler-host` | `test/language/asset_compiler_host.c:35`, `options`, explicitly sets `cache_directory = ""`. `persistent_directory` in `src/authoring/asset_compiler_host.c:652` treats that empty value as disabled, not an environment/HOME fallback. Compiler and library inputs are embedded; document/prototype/scene conversions remain in memory. | No subprocesses. Persistent cache read/write branches require a non-NULL directory, so these cases do not share disk cache state. Thread-local caches are process-isolated. |
| `compiler-bootstrap` | `test/tools/compiler_bootstrap.c:872`, `setup_root`, creates a unique `chibi-bootstrap-XXXXXX` directory. Every mutation, output, log and extracted source tree is below that root. `bootstrap_reuses_only_the_exact_captured_request_without_compilation` (`:586`) reads HEAD with `git -C tool_root() archive`; archive output and extraction are private. | Producer, compiler-host, git and tar commands run sequentially. `tool_run_within` in `test/tools/fixture.c:62` preserves each existing deadline and owns its child's process group. The accompanying change makes `setup_root` respect nonempty TMPDIR, falling back to `/tmp`, and expands path buffers to PATH_MAX so runner-owned scratch can contain the root. |
| `language-scalar-native` | `test/language/package_compiler_chibi.c:14721`, `native_directory`, uses unique `${TMPDIR:-/tmp}/chibi-native-scalar-XXXXXX`. `native_library` (`:11637`) writes IR, optimized IR, object and shared library beneath that root; `remove_native_files` tracks owned outputs. With `CHIBI_NATIVE_BUNDLE` set, it also writes objects and `modules.txt` under that directory, which no other selector reads or writes. | `run_native` (`:11572`) supervises sequential LLVM tools and self-executed probes. Tests intentionally time out owned children and send SIGTERM to their own parent test process (`package_compiler_native_runs_are_bounded`, `:14841`). Keep dedicated selector supervisors and existing 0.5/20/60-second child budgets. Linux x86-64 and LLVM availability gates remain unchanged. |

## Limits and scheduling policy

- Separate process ownership remains essential for unaudited selectors: fixture
  arenas, published type catalogs, JIT state and cache statistics must not be
  shared through concurrent C test threads. The small audited in-process scopes
  below are the exception, not a claim that compiler or runtime fixtures are safe.
- Unreviewed selectors run exclusively, including those that may mutate fixtures
  below absolute `CHIBI_SOURCE_ROOT`. TMPDIR and cwd alone are not isolation proof.
- Do not infer that the similarly named `package-compiler-chibi-bootstrap`
  selector is covered: it can write `CHIBI_BOOTSTRAP_SEED`, unlike the scalar and
  behavior selectors above.
- Workers need managed scratch and independent log streams; cleanup must target
  only that worker's owned descendants. Compiler-bootstrap and scalar-native can
  leave temporary files when killed, so runner cleanup must own the parent
  scratch directory after process cleanup.
- This audit establishes no peak-RSS values or safe memory-per-worker estimate.
  Compiler/JIT allocations can be substantial. Sequential child launches do not
  prove that LLVM/linker internals are single-threaded. Concurrency bounds must
  remain conservative and must not claim measured memory safety.
- Source inspection is not execution evidence. Report overlap/isolation,
  cancellation and manifest-preservation regressions separately from engine
  runs. Claim iteration speedups only after matched serial/parallel wall-time
  measurements; pico_unit CPU time is not elapsed time.

## Build-once isolated language shards (manual proof)

`Language shard validation` is an opt-in workflow, not a replacement for ordinary
CI. Each platform builds the ASan/UBSan drivers once and restores the same-run
artifact into three separate hosted runners. Each runner executes one complete
assigned selector list serially. Linux and macOS have independent producer/test
pipelines; Linux does not wait for the macOS build. Both producers explicitly select
`CHIBI_CI_SANITIZERS=ON` through `.github/native-profile.py`: the full sanitizer
Debug `-g -O1` profile with assertions and ASan/UBSan, distinct from ordinary
fast PR CI. A regression checks shared-helper selection on both platforms,
verbose build capture with pipeline failure propagation, and profile evidence
verification before artifact packing. The verifier checks actual compiler/link
receipts and sanitizer symbols, not just intended flags. Like the full sanitizer
lane, the producer first builds Release generation hosts and uses the Release
planner on macOS; these build-time hosts are not consumer artifacts. Include
that Release build and setup in the measured critical path and runner cost.
The transferred drivers and compiler host remain sanitized. No purchased runner capacity
or billing setting is involved. All ordinary native, material, project, language,
tooling, Studio, graph and formatting gates remain in `.github/workflows/ci.yml`.

The artifact contains only `build/test/language/test_language`, its `corpus.txt`,
`build/test/tools/test_tools`, and `build/sdk/chibi-compiler-host`. The test
binaries embed the source path; the corpus also contains absolute paths, and
compiler-bootstrap uses `git archive HEAD`. Consumers therefore check out the
exact producer commit at the identical absolute workspace path on a separate
runner. Copying these binaries into an arbitrary worktree is not supported.
`tools/language_test_artifact.py` verifies the source commit, tracked-tree
cleanliness, source root, OS/architecture, runner image, run/attempt, compiler and
sanitizer runtime identity, selector manifest, file inventory, hashes and modes.
The fixed-member tar unpacker rejects duplicate names, links, special files,
path traversal and wrong modes before writing, then verifies the staged identity.
A mismatch fails closed, before a test runs. It does not fetch cross-run artifacts
or substitute Release binaries. Runtime libraries come from the matching runner
image/toolchain; successful restored selector execution is still required to
prove the real loader/runtime closure, especially macOS ASan dylibs.

`python3 tools/test_policy.py shard-manifest` prints the exact current manifest,
platform exclusions and all three lanes. `--shard 1/3 group language` selects the
first lane and always uses one worker; `--jobs 2` is rejected. The ordinary
unsharded invocation is unchanged, and `--shard 1/1 group language` preserves the
full original serial order. Counts outside 1–3 and out-of-range indices fail
before launching work. Each platform's current `GROUPS['language']` is the sole
coverage source. Historical integer weights only choose a lane; new/unmeasured
selectors have weight one and cannot disappear. A lane preserves the original
relative selector order. This permits overlap only across independent runner
filesystems, not across previously unaudited selectors in one checkout. Existing
supervision, failure propagation, sanitizer settings, subprocess budgets and
45-minute job timeouts remain in effect.

Closure tracing at `15bc4b78`: `tools/test_policy.py` maps every language
selector to `test_language`, except compiler-bootstrap's `test_tools` invocation
with the language and compiler-host paths. Syntax and format receive the corpus
manifest. `test/language/CMakeLists.txt:340–385` creates that absolute-path
manifest; `tools/graph/engine/Targets.chibi:3836` declares its graph equivalent.
`test/tools/compiler_bootstrap.c:635` archives source `HEAD` into private scratch,
`test/language/integer_semantics.c:744–746` reads the checked source bootstrap,
and `test/language/asset_packaging_chibi.c:362` reads source-root files. Native
scalar helpers self-execute via `/proc/self/exe`
(`test/language/package_compiler_chibi.c:12811`) and invoke the supplied LLVM
binaries. Authored generated asset code is linked into the driver, not copied
as a writable shared cache. These source checks motivate the minimal closure;
they do not replace the exact-head restored-runtime tests.

### Critical path and runner-time tradeoff

Baseline run [40423984810](https://github.com/waldnercharles/chibi/actions/runs/40423984810)
checked out `28059065dbf35823a7ab825a5ce0451d51da5f16`.
[Linux selector diagnostics](https://github.com/waldnercharles/chibi/actions/runs/40423984810/job/121925067563)
recorded 923 seconds of language wall time at two workers and 1,516 seconds of
summed selector lifetimes. Applying the checked-in partition to those observations
models lanes of 507/503/506 seconds, before setup, transfer and queue time: about
416 seconds less test-tail wall time, but about 1.64 times the test-phase runner
seconds. These are scheduling estimates, not a measured new CI speedup or CPU
cost. Removing CPU contention can change individual lifetimes.

The later `15bc4b78` inventory has an additional
`language-migration34-generic-repro` selector (48 Linux / 47 macOS total versus
47/46 in the measured run). Its time is not measured and is not included in that
estimate. The macOS baseline spent 1,097 seconds in its serial language group;
Linux durations are not macOS measurements. PR #723 independently changes the
planner and macOS worker admission; this work does not duplicate or presume its
results. The separate O0/O1 experiment also remains independent.

The manual run must establish exact-head restored runtime success on both OSes,
complete/disjoint manifests, artifact bytes and upload/download/setup timing,
submission-to-result wall time (including producer and shard queues), whole-run
critical path and runner-seconds before considering a default CI split. Ordinary
PR CI now selects a fast unsanitized Debug profile; this sanitizer-only proof
cannot establish that three shards improve that different lane. Compare its
full-suite wall time and queue/setup overhead separately, without purchasing
runner capacity. Keep ordinary CI unchanged until matched evidence and
independent review justify any activation.

### Local regression scope

The existing pico_unit `--test-runner` dispatcher checks exact membership,
disjointness, deterministic assignment, per-lane order, unknown-selector
inclusion, one-lane fallback, invalid requests, serial execution and exit-code
propagation. Artifact fixtures exercise pack/verify/restore and reject damaged
files, modes, inventory, symlinks, path escapes and identity mismatches. These
fixtures cannot prove a real hosted ASan binary is portable; the manual workflow
is the integration proof, and all existing CI remains required.

## Integer selector extension (source baseline `15bc4b78`)

The following three selectors are also independent at the **process** boundary.
They join the existing allowlist without changing the manifest or scheduler:

| Runner selector | Resource evidence | Isolation |
| --- | --- | --- |
| `language-integer-semantics` | `test/language/integer_semantics.c`: `test_language_integer_semantics` runs `integer_semantics_pin_every_width`; `stage_zero` creates snapshots from literal `source_package_input` strings, and `sample_agrees` compares authored and stage-zero closures. | `native`, `authored`, `host`, result JSON and all backend state are process-private. No subprocess or output file is created. |
| `language-integer-shift-counts` | `test_language_integer_shift_counts` uses `setup_native`, `stage_zero` and `compiles_alike`; it does not load the authored bootstrap. | Only memory-owned capabilities, snapshots, closures and bytecode/SLJIT calls; no filesystem access or child process. |
| `language-integer-shift-counts-authored` | `test_language_integer_shift_counts_authored` uses the same `setup` and `sample_agrees` paths as integer semantics, once per shift sample. | Read-only bootstrap/compiler inputs, process-private compiler and execution state; no subprocess or generated-file write. |

Shared dependencies and less obvious paths were included in this audit:

- `setup` passes absolute source-root `tools/compiler/bootstrap`, `tools/compiler`
  and `packages/core` paths to `compiler_bootstrap_load`. Its `validated` path
  reads the manifest, closure, compiler contract and source snapshot. If sources
  changed, `compiler_bootstrap_load` calls `recover` with **NULL output**;
  `written` only calls `write_path` for a non-NULL output. Recovery creates an
  in-memory host; it does not update the checked-in seed. Neither the bootstrap
  writer nor the output-writing check/reuse APIs are called by these selectors.
- `compiler_host_load` exposes text, array, compiler JSON and syntax capabilities,
  not file/process capabilities or a persistent disk cache. Invocation results
  and diagnostics are allocated JSON/text or stderr. `stage_zero` passes explicit
  file texts, not a disk-backed project; `/compiler`, `/project` and `/sdk/core`
  are snapshot/compile identities, not directories written by these tests.
- Native test builds use SLJIT, not external LLVM compilation. The JIT creates
  per-process SLJIT compilers and generated code. The configured
  `SLJIT_WX_EXECUTABLE_ALLOCATOR=1` uses anonymous process-owned mappings on
  Linux/macOS, not shared diagnostic files or a persistent compiler cache.

The pico_unit policy regression substitutes native rendezvous workers for each
new selector and existing `language-asset-packaging-chibi`, preserving their real
resource metadata. Both workers must be alive together and receive isolated
scratch. Missing allowlist entries therefore fail the regression, not silently
serialize it. The existing whole-language manifest, ordered output, serial
escape, memory admission, fail-fast, deadline and owned-cancellation regressions
remain applicable. These fixture checks do not constitute engine execution or a
matched wall-time benchmark; no additional peak-RSS measurement is claimed and
the default two-worker cap / conservative memory admission are unchanged.

Rechecked when integrating `959d22d4` (prepared format 22): string-table
interning scratch belongs to each `execution_wire`/module and authored local
arrays. Updated compiler/seed inputs remain read-only; the integer setup and
NULL-output bootstrap recovery paths are unchanged. This introduces no new
shared file/cache/process resources. Allocation and compiler behavior did
change, so prior-head engine results are not integration validation; rerun the
current combined candidate rather than treating the old CI as current evidence.

## Native C case pool

`test_language` runs the audited syntax (21 cases), format/minimizer (60 cases),
and focused root-state (3 cases) scopes in parallel by default when the native
SDK provides enkiTS. This is actual in-process case parallelism, not selector
subprocess scheduling. The runner owns a separate enkiTS instance; it does not
acquire the engine's process-global shared task pool. Its budget is at most eight
threads including the submitting thread, capped to enkiTS's hardware count.
`CHIBI_TEST_THREADS=1` reproduces serial case execution; values 1–8 are accepted,
and malformed values fail before execution. Builds without the native scheduler,
including current web test drivers, run the same cases serially.

Each scope registers existing pico cases and captures their setup/teardown and
display options. A bounded 64-case queue flushes synchronously. Every case has a
separate pico context and two owned temporary log streams; the controller joins
the batch, replays output and errors in registration order, and merges counters.
No worker redirects process stdout/stderr. Converted fixture owners are
thread-local and their existing teardown runs after success and failure. Syntax
corpus I/O remains serial, after the scope barrier. Every other C fixture remains
exclusive: scheduling a scope is an explicit source-audited choice, not an
inference from a selector name. Nested scopes are rejected; the audited scopes
call no runtime scheduler or process-global mutation.

The current pool-enabled selectors are **not** in `PARALLEL_RESOURCES`, so the
outer process scheduler runs them exclusively. This prevents multiplying outer
selector workers by inner case workers for this migration. Adding another scope
requires reviewing that admission rule as well as its fixture ownership, nested
work, shared catalogs/caches, native calls and output. Existing process-group
cleanup, group deadlines, assertions and full selector coverage are unchanged.
Thread failures are not retried; a crash still fails the owning selector.
An unfinished batch has not replayed its logs when its process is killed;
previous batches remain visible. Buffering changes log delivery, not timeouts.

`language-test-parallel` covers overlap, worker bounds, more than one queue batch,
fixture reuse, captured hooks, deterministic failure ordering, counters and
case-local display settings. Run it with the default and with
`CHIBI_TEST_THREADS=1`. Compare real syntax/format case and assertion totals in
both modes. The language driver supplies a monotonic wall clock for passive
case timing; the other pico drivers retain their existing clock unless they
configure one. Per-case timing excludes queue wait and hooks, just as the
previous body timing did. Use external selector wall time to measure speedup.

This first migration does **not** parallelize the expensive package compiler
fixtures or claim a material full-language CI reduction. Those require explicit
compiler fixture ownership, catalog/cache/host-probe review, and program
inspection generation state isolation; converting their file globals to TLS
alone is not a safety proof.

### Initial in-process safety audit

- Syntax allocates a fresh Tree-sitter parser/tree per case; the generated
  language tables and fixture corpus tables are immutable. Parser/scanner and
  syntax-diagnostic owners are not shared. Corpus manifest I/O is outside the
  parallel scope.
- Formatter/minimizer cases own their syntax snapshots, generated strings, edit
  map, semantic analysis and any constructed native capability catalog in TLS.
  Existing teardown frees and resets each owner before its worker is reused.
- The reached semantic/formatting implementation has invocation-owned arenas
  and read-only descriptor tables. Native capability generation allocation and
  xalloc counters are atomic; these cases do not assert process-wide allocation
  deltas. Type catalogs are only read, never installed or removed here.
- No migrated case compiles or executes SLJIT, creates an execution inspector,
  calls a runtime task pool, changes locale/environment/current directory,
  writes a shared fixture or launches a child. Fixture diagnostics use the
  current pico context's streams rather than raw process streams.
