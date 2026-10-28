# Core math verification

Work and checks ran on 2026-10-27 and 2026-10-28. This receipt records the original
uncommitted development snapshot on `codex/core-math-recovery-20261027`, based on
`7f35662ac53a3d6635eaac7b95af3f0cafbdc586`. SDK publication remains held. No
checked bootstrap, prepared format or ABI version was changed.

The preserved recovery refs remain `codex/core-math-main-20261024` at
`90e0936e90a06c6ed66f6a347c98923b04cd6c0b` and
`codex/core-math-inverse-lerp-20261024` at
`d039de3eeb2e66e34fc3fc5f97339f9ba6ead06e`.

The candidate contains the scalar package, 31 easing functions, the analytic
spring and their authored tests; native/graph retirement; the owner-approved
package reachability fix; and the older-seed bootstrap bridge. Thin libm calls
provide precise sine, cosine and floating remainder. Math policy remains in
Chibi. See [the contracts](../../core-math.md).

## Candidate and source evidence

[inputs.json](inputs.json) records the base and SHA-256 of every changed
code/build/test input, including new files. Documentation and this receipt are
excluded from that fingerprint. [math-source-manifest.json](math-source-manifest.json)
records the generated asset resource's six math inputs and resource/artifact
hashes. Each recorded source digest was checked against the actual source.
All six installed math files were also compared byte for byte with the source.

Native checks used macOS arm64 and Clang 22. Wasm checks used Emscripten 6.0.9
and Node 26.8.1. Builds and tests used isolated output under `.chibi/math/`.
Build times below are separate from test command wall times. Other work was
running on this host, so elapsed times are observations rather than gates.

## Focused checks

Each math run executes 29 authored tests on each selected backend, plus six C
cases at the native boundary. The C cases supply nonfinite values, signed zeros
and exact f32 values, check rejected domains and explicit f64 narrowing, and
verify removal of the old catalog names. Entry counters require actual backend
execution. Native uses BYTECODE and SLJIT; Wasm uses BYTECODE and WASM.

| Check | Result | Command wall time |
| --- | --- | ---: |
| Final native package math | 6 passed, 4,132 assertions | 1.497 s |
| Final installed-SDK package math | 6 passed, 4,132 assertions | 1.139 s |
| Final Wasm package math | 6 passed, 4,132 assertions | 1.426 s |
| ASan + UBSan package math | 6 passed, 4,132 assertions | 5.531 s |
| ASan + UBSan package imports | 18 passed, 1,530 assertions | 2.972 s |
| Native compiler imports, bytecode compiler | 2 passed, 17,109 assertions | 33.313 s |
| Native compiler imports, compiled compiler | 2 passed, 16,740 assertions | 28.623 s |
| Graph-built compiled compiler imports, launched from `/tmp` | 2 passed, 16,740 assertions | 16.690 s |
| Wasm compiler imports, bytecode compiler | 2 passed, 17,109 assertions | 56.490 s |
| Wasm compiler imports, compiled compiler | 2 passed, 16,740 assertions | 46.433 s |

Compiler import checks load the actual math source, execute imported values and
the spring, and reject implicit f64 narrowing. Reachability cases check unused
host-only/declaration/body failures, direct and transitive imports, blank
imports, independent project roots and emitted closure membership. Native
cases additionally test a collection-only first-input root and incremental
removal/readdition of an import while an old retained module still executes.

Final review found an omitted-argument mismatch: f64 `fmod` used a zero divisor
default while `number` used one. The added authored assertion failed before
the change, then passed after both defaults became one. The native, installed,
Wasm and sanitizer math rows above include that final fix. The earlier compiler
import rows precede this metadata/test-only fix; the outside-directory native
import run follows it. Compiler and production math source did not change.

The sanitizer build used actual `-fsanitize=address,undefined` compiler and link
flags, with both runtimes configured to halt on errors. It used the supported
Release generation hosts for its source-generated asset resource. No sanitizer
finding appeared in the selected checks. This does not claim leak-sanitizer or
all-runtime sanitizer coverage.

Representative commands, from the engine root:

```sh
CHIBI_LANGUAGE_BUILD_DIR=.chibi/math/language python3 tools/test_policy.py tool language-package-math
CHIBI_LANGUAGE_BUILD_DIR=.chibi/math/installed-tests python3 tools/test_policy.py tool language-package-math
CHIBI_WASM_BUILD_DIR=.chibi/math/wasm python3 tools/test_policy.py tool wasm-package-math
CHIBI_LANGUAGE_BUILD_DIR=.chibi/math/sanitizer ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 python3 tools/test_policy.py tool language-package-imports
```

Selected raw output is retained in [logs/](logs/), including the failing and
passing `fmod` default regression and the compiler run outside the source tree.

## Builds and installation

The final normal graph build passed in 212.17 s, including 66.8 s third-party
preparation and 144.8 s graph work. It built `test_tools`, `test_language`,
`chibi_ctl` and `chibi`, with normal asset generation and source-snapshot checks.

```sh
make build BUILD_DIR=.chibi/math/full BUILD_PROFILE=release \
  BUILD_HOST=.chibi/math/language/sdk/chibi-build-host \
  ASSET_GENERATOR_HOSTS=.chibi/math/language/sdk JOBS=4 \
  BUILD_TARGET='test_tools test_language chibi_ctl chibi'
```

The native standalone SDK/language build and SDK installation passed. The final
native rebuild after the default fix took 170.73 s; the final install took
1.81 s and the installed consumer relink took 6.49 s. The installed consumer
uses the installed math source path, not a checkout fallback. No public SDK
artifact was published.

The final Wasm rebuild took 48.00 s, including 16.1 s configuration. Its test
filesystem embeds exactly six math source files. Two existing test portability
issues were repaired: pointer-width-aware `pin_string` layout assertions and
conditional storage for worker settings when enkiTS is absent. There is no
global NODERAWFS access or host-catalog widening.

The initial sanitizer build's own asset-generation host reached its operation
timeout after 305.01 s. Reconfiguration to the existing Release generation
hosts passed; the resulting sanitizer driver build took 143.18 s. Sanitized
runtime checks still ran against sanitized libraries.

A six-target normal build batch reached the build host's 256 MiB value-storage
limit. The four-target batch above passed. The limit was not raised.

## Wider checks and remaining failure

[group-results.json](group-results.json) sums reported dispatcher summaries,
including child runs. These counts are not a measure of unique contracts.

| Check | Reported result | Command wall time |
| --- | --- | ---: |
| Native group, final candidate | 648 passed, 235,200 assertions | 4.805 s |
| Materials group | 104 passed, 247,602 assertions | 8.607 s |
| Projects group | 109 passed, 11,965 assertions | 124.246 s |
| Language group | 922 passed, 477,658 assertions | 777.600 s |
| Final feature coverage | 2 passed, 8 assertions | 9.316 s |
| Full tooling run | 217 passed, 3 failed | 589.076 s |
| Focused test-runner retry | 63 passed, 856 assertions | 97.869 s |

The materials, projects and language groups ran before the final f64 divisor
default/assertion change. Affected math, native catalog, installation, Wasm,
sanitizer and feature-coverage checks were rerun afterward. The group commands
used the same policy dispatchers as `make test-native`, `make test-materials`,
`make test-projects` and `make test-language-all`, with the isolated build paths
above and Release CLI/compiler products. Tests were not admitted or rejected by
a global elapsed-time threshold.

Two initial tooling failures were fixture subprocess timeouts in the runtime
seed pin and language-group recorder tests. Both passed in the focused
63-case retry, which uses the actual test-policy group and its disposable
drivers. No timeout policy was changed.

One failure remains in unchanged code. `c-export-alias` also fails in isolation
on arm64: the generated C calls `behavior_function_host_arguments`, but
`test/tools/c_export.c:1686` links only the generated file and
`src/execution/source_debug.c`. The helper is implemented in
`src/execution/host.c:256`. This candidate changes none of those files.
The isolated result is 1 passed and 1 failed in 1.683 s; its linker output is in
[c-export-alias-isolated.log](logs/c-export-alias-isolated.log). The full tooling
suite is therefore not green, and this receipt does not certify a release gate.

Subsequent classification reproduced the identical failure on a clean detached
`7f35662ac53a3d6635eaac7b95af3f0cafbdc586` build, with the C-export fixture enabled.
It reported the same missing symbol, 1 passed/1 failed and 755 assertions in
2.181 s. The fixture, generated-C emitter and runtime helper sources are identical
to the tested candidate. The
[baseline classification](baseline-classification/classification.json), source
hash comparisons and raw output are retained beside this receipt.

## Observed cost

The final benchmark used the installed SDK, 512 steps per invocation, 16
invocations and three trials after warmup. Invocation timing includes reusable
storage reset, binding and execution; preparation is separate. The earlier
128-step run overlapped builds and had large scheduling variation, so the
512-step run below was taken after this task's builds finished.

| Body | BYTECODE median ns/step | SLJIT median ns/step |
| --- | ---: | ---: |
| Imported Lerp | 689.575 | 101.196 |
| Inline Lerp | 288.086 | 5.859 |
| Spring value update | 11,784.302 | 2,246.826 |
| Spring fields update | 12,618.774 | 2,367.310 |

Imported calls have visible overhead. This implementation does not claim an
inlining or interpreter optimization. Neither spring form supports a universal
allocation-free claim: BYTECODE recorded 1,872 xalloc calls and 128 arena blocks
per 16-invocation trial. Warm SLJIT trials recorded no new xalloc calls or arena
blocks, while retaining 303,406/311,582 bytes used in a 585,442-byte arena and
about 60 KiB of activation storage. The counters cover xalloc wrappers and arena
blocks, not every platform allocator. Full trial data is in
[benchmark.json](benchmark.json) and [benchmark-quiet.log](logs/benchmark-quiet.log).

## Studio prerequisite, review and unexecuted coverage

The companion worktree is
`/Users/cwaldner/Personal/chibi/.worktrees/core-math-studio/chibi-studio`, branch
`codex/core-math-consumer-20261027`, based on Studio
`9afa743996a72efb491ca169e0928eb3e56429fe`.
Only `OutlinerRows.chibi` and its `EXTRACTION.json` consumer digest changed.
The old Step call becomes an exact double 0/1 offset from integer `First >= 1`;
the original `Count - 1 + offset` operation order is retained. This works with
the existing engine. The real manifest remains v28, and the companion must land
before engine retirement.

An independent agent ran the existing engine migration on a disposable APFS
clone: v28-to-v38 migration passed in 4.618 s, formatting in 1.270 s, and
canonicality checking in 0.577 s. The edited file was already canonical; only
unrelated existing clone files needed formatting. The clone was deleted. No
Studio UI build, launch or interaction was performed.
Commands, elapsed times and companion input hashes are recorded in
[studio-companion.json](studio-companion.json). Their original evidence is the
independent agent's tool transcript; the temporary migration log was deleted
with the clone, and no exact execution timestamp was captured.

Independent reviews covered analytic transitions and numerical tests, native
retirement and live Studio usage, package ownership/indexing, closure layout,
bootstrap membership, Wasm fixture isolation and SDK installation. Review
findings about collection-only roots, fixture path parity, the Studio consumer
and fmod defaults were addressed. All changed C files pass Clang-format 22;
both repositories pass `git diff --check`.

Linux x86-64 scalar-native execution was explicitly unrun on this macOS host.
Windows SDK execution, the full Wasm suite, GPU rendering and Studio interaction
coverage were not run. Public precision-polymorphic math exports remain with
the separate exported-generics work. Scratch probes and the Studio clone were
deleted; ordinary ignored build products and diagnostic logs remain under
`.chibi/math/` for review. No publication action had occurred when this snapshot
was recorded. Later commits, PRs and accepted-main validation are recorded in
the separate [publication receipt](../core-math-20261028-publication/RESULTS.md).
