# Parallel test resource audit

The runner schedules **selectors in separate processes**, not individual pico_unit
cases or C threads. A selector retains its existing cases, backend coverage and
child-process budgets. The initial parallel allowlist is deliberately narrow;
unknown selectors remain exclusive. This is not proof that every selector in the
language or compiler groups is independent. Test-case discovery and a future
Chibi-first test API are separate work, not an expansion of the C test foundation.

Audit basis: source at `bb0ea748ecf30befda6f41853a8bd98f6151cbde`, plus the
`compiler-bootstrap` TMPDIR change described below. This branch also includes
PR #714's prerequisite at `c7ec54b940ddea7a149b94bfc356099b341d0cc1`:
its additional bootstrap bound/recovery cases retain private-root writes, and
the two new root-derived path buffers also use PATH_MAX here. Line references
in this table identify the audited baseline; function names remain the durable
anchors.

| Runner selector | Resource evidence | Child processes and isolation |
| --- | --- | --- |
| `language-package-compiler-chibi-scalar` | `test/language/package_compiler_chibi.c:15191`, `test_language_package_compiler_chibi_scalar`, selects four memory-only scalar cases. `prepare_backends` (`:3213`) consumes embedded `PACKAGE_COMPILER_SOURCE_INPUTS`; `compiler_fixture_prepare_backends` in `test/language/compiler_fixture.c` supplies snapshot file texts. Scalar rows, generated IR text, compiler hosts and JIT state remain in memory. | No LLVM process or output-file path is called by these four cases. Native execution is a different selector. |
| `language-package-compiler-chibi-behaviors` | `test_language_package_compiler_chibi_behaviors` (`package_compiler_chibi.c:15152`) runs `package_compiler_compiles_behaviors` (`:8318`), using embedded sources and memory-only compiler/ECS state. | No subprocesses or filesystem writes. Process isolation is necessary for the mutable project type catalog and fixture globals. |
| `language-package-compiler-chibi-behaviors-rest` | `test_language_package_compiler_chibi_behaviors_rest` (`package_compiler_chibi.c:15172`) runs the other behavior samples/rejections and per-owner text-state case (`:9317`, `:9388`). Project identities are input strings, not directories created on disk. | No subprocesses or filesystem writes; separate process-owned compiler, type catalog, ECS and pico_unit globals. |
| `language-asset-packaging-chibi` | `test/language/asset_packaging_chibi.c:597`, `test_language_asset_packaging_chibi`, exercises byte packaging and generator JSON in memory. `read_source` (`:359`) reads selected files below absolute `CHIBI_SOURCE_ROOT` using `fopen(..., "rb")`. | No subprocesses or generated-file writes. Source-root access is read-only; changing cwd does not redirect it. |
| `language-asset-compiler-host` | `test/language/asset_compiler_host.c:35`, `options`, explicitly sets `cache_directory = ""`. `persistent_directory` in `src/authoring/asset_compiler_host.c:652` treats that empty value as disabled, not an environment/HOME fallback. Compiler and library inputs are embedded; document/prototype/scene conversions remain in memory. | No subprocesses. Persistent cache read/write branches require a non-NULL directory, so these cases do not share disk cache state. Thread-local caches are process-isolated. |
| `compiler-bootstrap` | `test/tools/compiler_bootstrap.c:872`, `setup_root`, creates a unique `chibi-bootstrap-XXXXXX` directory. Every mutation, output, log and extracted source tree is below that root. `bootstrap_reuses_only_the_exact_captured_request_without_compilation` (`:586`) reads HEAD with `git -C tool_root() archive`; archive output and extraction are private. | Producer, compiler-host, git and tar commands run sequentially. `tool_run_within` in `test/tools/fixture.c:62` preserves each existing deadline and owns its child's process group. The accompanying change makes `setup_root` respect nonempty TMPDIR, falling back to `/tmp`, and expands path buffers to PATH_MAX so runner-owned scratch can contain the root. |
| `language-scalar-native` | `test/language/package_compiler_chibi.c:14721`, `native_directory`, uses unique `${TMPDIR:-/tmp}/chibi-native-scalar-XXXXXX`. `native_library` (`:11637`) writes IR, optimized IR, object and shared library beneath that root; `remove_native_files` tracks owned outputs. | `run_native` (`:11572`) supervises sequential LLVM tools and self-executed probes. Tests intentionally time out owned children and send SIGTERM to their own parent test process (`package_compiler_native_runs_are_bounded`, `:14841`). Keep dedicated selector supervisors and existing 0.5/20/60-second child budgets. Linux x86-64 and LLVM availability gates remain unchanged. |

## Limits and scheduling policy

- Separate process ownership is essential: pico_unit, fixture arenas, published
  type catalogs, JIT state and cache statistics are not safe to share through
  concurrent C test threads.
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
