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

## Build-once isolated language shards (manual proof)

`Language shard validation` is an opt-in workflow, not a replacement for ordinary
CI. Each platform builds the ASan/UBSan drivers once and restores the same-run
artifact into three separate hosted runners. Each runner executes one complete
assigned selector list serially. Linux and macOS have independent producer/test
pipelines; Linux does not wait for the macOS build. No purchased runner capacity
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

Current main `15bc4b78` has an additional
`language-migration34-generic-repro` selector (48 Linux / 47 macOS total versus
47/46 in the measured run). Its time is not measured and is not included in that
estimate. The macOS baseline spent 1,097 seconds in its serial language group;
Linux durations are not macOS measurements. PR #723 independently changes the
planner and macOS worker admission; this work does not duplicate or presume its
results. The separate O0/O1 experiment also remains independent.

The manual run must establish exact-head restored runtime success on both OSes,
complete/disjoint manifests, artifact bytes and upload/download/setup timing,
whole-run critical path and runner-seconds before enabling the default CI split.
Keep the ordinary CI fallback until that evidence and independent review pass.

### Local regression scope

The existing pico_unit `--test-runner` dispatcher checks exact membership,
disjointness, deterministic assignment, per-lane order, unknown-selector
inclusion, one-lane fallback, invalid requests, serial execution and exit-code
propagation. Artifact fixtures exercise pack/verify/restore and reject damaged
files, modes, inventory, symlinks, path escapes and identity mismatches. These
fixtures cannot prove a real hosted ASan binary is portable; the manual workflow
is the integration proof, and all existing CI remains required.
