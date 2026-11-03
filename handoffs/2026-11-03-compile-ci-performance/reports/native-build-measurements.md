# Native-build measurement commands

Prepared against frozen input #1331 `13e01f9055e555caf207642df8ab477f721c38c2` and
#1330 `2fb44239a425e18b231fc44d0da0392ea31fac74`. Record the agreed integration
commit and tree before executing these commands. No timings are claimed here.
Run from the isolated clone, with no competing heavy work. Do not use another
thread's generated products as acceptance evidence.

## Fixed setup

```sh
cd /Users/cwaldner/Personal/chibi/.coordination/compile-ci-costs-20261102/chibi
task_evidence=/Users/cwaldner/Personal/chibi/.coordination/compile-ci-costs-20261102/evidence
task_llvm=/tmp/chibi-llvm22
task_libclang=/opt/homebrew/opt/llvm@22
export CHIBI_LLVM_BIN="$task_llvm"
export CC="$task_libclang/bin/clang" CXX="$task_libclang/bin/clang++"
git rev-parse HEAD 'HEAD^{tree}'
git status --porcelain
"$task_llvm/opt" --version
"$task_llvm/llc" --version
"$task_llvm/ld.lld" --version
"$task_llvm/ld64.lld" --version
```

The local LLVM and LLD 22 symlinks currently resolve to Homebrew 22.1.8.
`libclang.dylib` exists under `/opt/homebrew/opt/llvm@22/lib`. Verify tool hashes
after the candidate is frozen. Omit `--native-driver` to use the pinned seed;
`tools/build.sh` verifies/prepares `.chibi/native-seed/macos-arm64/chibi` and
reuses #1331's content-keyed build-entry cache. Do not add another bootstrap.

## Build and run the canonical inventory

The first command builds the Release launcher and private helpers required by
native-build. The second reproduces the ordinary CI Debug product/driver recipe.
For native-build alone the Debug products are not consumed. `tools/tests.tsv`
passes `build-release/sdk/chibi` as the fixture launcher.

```sh
/usr/bin/time -l -o "$task_evidence/release-build.resources" sh tools/build.sh build --release --jobs 4 --target chibi --target chibi-build-host --target chibi-compiler-host --libclang-root "$task_libclang"
/usr/bin/time -l -o "$task_evidence/debug-build.resources" sh tools/build.sh build --build build --profile debug --debug-optimization 1 --debug-frame-pointers on --jobs 4 --target chibi --target chibi-compile --target test_tools --target test_language --host "$PWD/build-release/sdk/chibi-build-host" --asset-generator-hosts build-release/sdk --libclang-root "$task_libclang"
/usr/bin/time -l -o "$task_evidence/native-build.canonical.resources" sh tools/build.sh test --build build --release-build build-release --build-host "$PWD/build-release/sdk/chibi-build-host" native-build
```

Keep the chosen job count, profile, compiler, seed, LLVM, inputs and selector
inventory identical before and after. The existing 1,500-second selector and
1,200-second worker deadlines remain in force. Scheduled sanitizer comparisons
must retain their selected sanitizer profile and instrumentation separately.

With real LLVM, Run executes `LinksAProgramAndReusesIt` first and starts 25 named
worker cases with concurrency four. macOS starts all 25 workers, but the three
Linux-only loop cases return immediately: `SwapsARunningLoopKeepingState`,
`ConvertsARetainedStateWhoseTypeChanged`, `WatchesAndSwapsARunningLoop`. Report
these three as platform skips even though the current harness prints passed.
`RunsWithLLVMAndNoCCompiler` runs O0/O2 on macOS; its extra line-table rounds
are Linux-only. Expected macOS build/link command count is 78, including negative
calls and two direct driver calls, not 78 successful compiler invocations.

## Existing host traces, mapped to cases

Run this separately from canonical latency repeats. The wrapper only chooses a
private sink from a known case name and execs macOS `/usr/bin/time -l` around the
exact Release host. It does not modify source, executable bytes, cache keys,
assertions, arguments, ordinary stdout/stderr or normal exit codes.

```sh
CHIBI_BUILD_TIMINGS="$task_evidence/diagnostic-1" /usr/bin/time -l -o "$task_evidence/native-build.diagnostic.resources" sh tools/build.sh test --build build --release-build build-release --build-host "$task_evidence/measure-host.sh" native-build
```

The Run directory contains its trace and 25 case subdirectories. Each case owns
`resources.txt` and existing numbered TSVs. Trace columns are category, step,
monotonic start, observed completion, status and error. Use startup,
compiler-load, prepare, execute and invocation rows. `prepare` includes cache
lookup, artifact decoding/linking and binding; it does not establish a recompile.
The existing cap is 64 trace files per sink directory. Missing final invocation
rows mean incomplete evidence. Do not sum nested/overlapping intervals as wall
time. `/usr/bin/time -l` reports process resource usage and CPU work; its maximum
RSS is not the simultaneous RSS sum of all concurrent descendants. It may add
an abnormal-termination warning if a measured host is signaled. No signal or
cancellation experiment should use this diagnostic wrapper as equivalence proof.

`CHIBI_TEST_TIMINGS` only measures C pico cases. It cannot time these authored
BuildTests cases. The installed CLI commands use `env -i` and remove the timing
variable, so inner collector/emitter/LLVM phases remain unattributed here.

## Cold, warm and edit trials

Use fresh private compiler-cache directories for cold compiler/cache trials;
keep the same directory for unchanged warm trials. Preserve existing private
build outputs/caches by moving them to evidence between cold build trials,
rather than deleting unrelated state. A cold native-build run still intentionally
warms the shared emitter in its first case. Every full Run recreates fixture
output directories, so an unchanged full suite is warm for compiler preparation
but cold for those per-fixture program outputs. Label these states separately.

For a direct compiler rebuild use the verified pinned seed after preparation:

```sh
/usr/bin/time -l -o "$task_evidence/compiler-rebuild.resources" .chibi/native-seed/macos-arm64/chibi --compiler "$PWD/tools/compiler" --core "$PWD/packages/core" --package native_driver --entry Main --output "$task_evidence/rebuilt-compiler" --llvm "$task_llvm" --target aarch64-apple-darwin --jobs 4
```

For edit feedback, keep a separate fixed-path project under evidence with Main
calling Twice(21). Time a cold `build --native -O0`, an unchanged hit and an edit
to Twice's body with an explicitly checked changed return value. Use the same
source and expected diagnostics with both the installed Release CLI and the
verified native candidate. Do not reuse one path's output cache as the other's.
Record source bytes, program/IR/object bytes, cache state and compiler identity.
Start with three paired cold trials and five paired warm/edit trials, then add
trials only if the observed spread cannot distinguish the result. Report mean,
minimum, maximum and paired deltas, separately for wall latency and CPU work.

## Next narrow instrumentation and regression checks

Do not wrap hashed LLVM executables for canonical comparisons. Tool wrappers
change cache identities; their owned-child signal forwarding needs verification.
The first diagnostic run is sufficient to locate large host-prepare or case
intervals. If inner phases still matter, a diagnostic copy of native_aot plus
explicit timing-variable passthrough in a diagnostic harness can expose existing
build-host process intervals. Keep that copy out of acceptance source and run the
same instrumented copy on both candidates. Additional compiler-phase probes
should follow the largest unexplained interval.

Removing stand-in setup for real-tool fixtures should keep all original real
program assertions and controlled-tool failure/recovery/cancellation cases.
Cheap setup regression: in a private fixture, verify the three real-tool paths
are executable symlinks to the configured tools and do not leave stand-in action
or argv files; then use one existing real-program build/assertion. Avoid a
timing threshold test.

For the shared bundle manifest race, give concurrent cases private manifests and
merge them only after workers finish. A cheap regression can publish four small
fixture entries concurrently, with one producer delayed, then assert the exact
complete ordered inventory and absence of duplicates. It need not compile or
run target programs. Preserve the existing target executables and their runtime
assertions in full selector validation.
