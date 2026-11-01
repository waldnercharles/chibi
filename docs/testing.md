# Test iteration

## Fast, targeted tests

Prefer authored Chibi `@test` regressions whenever they can preserve the same
behavior and oracle. Their inferred purity supplies automatic parallelism; keep
C/pico_unit for native internals, ABI, ownership and failure boundaries Chibi
cannot express. Cover each behavior and failure case through the smallest
interface that exercises it. Advance a synthetic clock for time-dependent
behavior; assert values, ownership, geometry, callback counts and work counts
rather than elapsed-time thresholds. Keep compilation separate from execution.

The goal is for every test to be fast and targeted. When replacing a smoke test,
map each meaningful assertion to its replacement first. Remove duplicate
entrypoints and measurement-only loops from regression commands. A short unit
result does not satisfy unrelated integration requirements or clear a PR hold.

The scalar lane (`language-package-compiler-chibi-scalar`) runs the authored
compiler, as `compiler_bootstrap_load` recovers it, through `test/language/scalar_host.c`:
`CollectScalar` produces rows, and a closure of the consumer packages (`scalar_llvm`,
`llvm_native`, `native_abi`, the `scalar_cases` corpus) validates and emits them.
Nothing is prepared with stage zero.

### Authored package tests

`chibi test DIR` runs every `@test` in the packages below `DIR`. The end-user
command runs eligible tests on the bounded runtime thread pool;
`CHIBI_TEST_WORKERS=1` keeps it serial (see the
[package testing guide](guide/testing.md#parallel-execution)). The `chibi-test-*`
rows of `tools/tests.tsv` run it over the compiler (`tools/compiler`), `core`
(`packages/core`), `test/integer_arithmetic` and each folder of
`test/compiler/samples`, all in the `language` group.

The compiler's packages keep their tests beside them in `tools/compiler/*/tests`:
the tokenizer, parser, package paths, routes, syntax and graph, source snapshot,
type relations, unit algebra, prepared writer, shader source, asset packaging and
JSON strings. These replaced the C differentials that compared each package with
stage zero or Tree-sitter; [the archive](../archive/README.md) lists what those
covered that the `@test`s do not.

### Authored emitter regressions

`chibi-test-compiler` (`chibi test tools/compiler`) runs every `@test` in
`tools/compiler/llvm_native/tests/*.chibi` and `tools/compiler/native_abi/tests`
on the host's backend; no C selector, runtime source staging or seed change is
involved. It fails on a rejected diagnostic, a malformed module that emits, or any
failed test, and results keep source order.

The first eighteen authored tests preserve call definitions/argument order,
eleven malformed call plans, exact float and NaN payload encodings, unordered
comparison, saturating conversions and declaration uniqueness, and five rejected
float/conversion plans. Thirteen record tests additionally preserve field layout,
zero initialization, insert/extract and internal value calls, eight malformed
instructions, exported-record rejection, and overrun/nested/wrapped field tables.
All thirty-one inspect emitted LLVM text; they do not replace
native machine-code execution, ABI binding, detached ownership or C harness
anti-false-consensus checks. Correctness cases are fixed, not performance
benchmark loops.

### Authored string and formatting regressions

`test/core_strings` holds the 174 string behavior rows and the formatting
fallback rows of the retired `language-package-core` C selector as `@test`s
against the production `core:strings` package. Its README maps each test to the
C row it replaced. `chibi test test/core_strings` runs all 191; no
`tools/tests.tsv` row runs them yet. Native byte ownership, read counters,
metadata and binary/hash witnesses stay in C.

### Portable byte-library regressions

The bytes, bits, FNV, Adler-32 and CRC-32 packages carry their `@test`s in
`packages/core`, so `chibi-test-core` runs them. See the
[package decision and validation record](package-byte-libraries.md).

## CI profiles and release validation

Ordinary pull requests and pushes to `main` use **Debug O1 without ASan/UBSan**
on Linux and macOS. Symbols, assertions, frame pointers and native, project,
language and tooling correctness selections remain enabled. Performance
benchmarks are opt-in, not part of routine CI or correctness aggregates.
This is a CI-only choice: a local `chibi build` keeps its sanitizer default.

Native CI builds the Release helpers first, then the products and test drivers
in the selected profile with the Release build host, and runs the
`tools/tests.tsv` groups with `chibi build test` against those builds, one group
after another.

The same `CI` workflow runs full **ASan + UBSan** coverage daily at **08:00 UTC**
on the latest default-branch commit (GitHub may delay scheduled starts). To run
it on demand, choose **Actions → CI → Run workflow**, select the candidate
branch, and enable **sanitizers**. A manual run without that option uses the
fast profile. Run titles identify `ASan + UBSan` versus `fast Debug O1`; native
profile artifacts record the selected instrumentation and checked build evidence.
The event and profile have separate concurrency keys, so a scheduled sanitizer
run cannot cancel ordinary CI. Job/check names and 45-minute native timeouts
are unchanged. Sanitizer-only defects can now be discovered after merge rather
than blocking every PR; a scheduled workflow by itself does not automatically
triage or fix failures.

Before a major release, dispatch with **sanitizers enabled** on the exact release
candidate; require both Linux and macOS native jobs and all existing functional
checks to pass. Check the run's commit SHA and profile receipts, investigate any
sanitizer failure, and rerun on the fixed candidate. A passing fast run is not
a substitute for sanitizer evidence. Explicit holds and required repository checks
still apply. Both profiles share `.github/native-profile.sh` and the same test
steps to prevent drift. Graph frame pointers are a separate Debug-only opt-in
(`--debug-frame-pointers on`); ordinary developer Debug/O1 behavior is unchanged.

## Run the checks

The engine build compiles the test drivers, `test_tools` and `test_language`.
CI runs the groups below on every push to `main` and every pull request into it
that changes more than documentation.

```sh
# Unsanitized Release tools in build-release/
chibi build --release --target chibi --target chibi-compile --target chibi-compiler-host --target test_tools
# Both test drivers in build/ (ASan and UBSan by default)
chibi build --target chibi --target chibi-compile --target test_tools --target test_language
chibi build test --list            # every row of tools/tests.tsv
chibi build test short             # test_tools' default cases
chibi build test native            # native CPU selectors
chibi build test projects          # the Release chibi launcher and its native builds
chibi build test language          # language group, including stage-one bootstrap and recovery
chibi build test benchmarks        # opt-in benchmark selectors
chibi build bootstrap-artifact     # rewrite the checked-in recovery artifact from stage two (Release build)
chibi build bootstrap-recover      # recover the compiler from that artifact and the sources on disk
```

`chibi build test` (tools/toolchain/Driver.chibi) runs the selectors and groups
of `tools/tests.tsv` in table order, up to `--jobs N` at a time (default 1). Each
selector is a separate process. It prints `RUN` when a selector starts, then
`PASS` or `FAIL` when it ends. A short selector frees its slot while a long one
still runs. With `--jobs` above 1, each selector's output is captured and printed
when it ends, so concurrent selectors don't interleave. With `--jobs 1`, a
selector writes to the terminal as it runs.

An `exclusive NAME` row runs that selector alone: nothing else runs while it
does. Mark a selector exclusive when it starts its own worker pool
(`language-test-parallel`) or shares files with other selectors. Each selector
has a deadline, set by `--timeout SECONDS` (600 by default) or by a
`timeout NAME SECONDS` row. A selector that passes it is killed with its process
tree and reported as `FAIL NAME (timed out after Ns)`; the others still run.
CI runs the `projects language` invocation with `--jobs 3` on Linux (4 cores,
16 GB) and `--jobs 2` on macOS (3 cores, 7 GB).

The language group queues `bootstrap-recovery` immediately after the longer
compiler suite, before the short sample suites. Recovery exports to its own
temporary root and does not share the compiler cache, so it can use an earlier
free worker without changing its clean-checkout proof. Keep it in the group
exactly once: adding it as an explicit selector as well would run it twice.

The `projects` group runs `chibi-cli`, `chibi-cli-authored` and `native-build` against the Release
`chibi` launcher in `build-release/sdk/`, with its `chibi-compile` beside it.
`native-build` links real programs only when `CHIBI_LLVM_BIN` names the LLVM
tools; without it, LLVM stand-ins cover the process contract.

`chibi-cli` runs twenty-three launcher cases: `version` and `doctor`, over a
fake sibling and the built launcher; `chibi test` through the real
`chibi-compile` beside it; `build --native` argument forwarding; and programs
run from files and directories, with `--watch`, cancellation and child
processes.

`chibi-cli-authored` (`test/graph/ChibiCliTests.chibi`) runs the `chibi-cli`
cases that outlive the VM, through the same launcher, for its native
replacement.

`chibi build test <name>` runs one selector against the drivers in `build/` and
the Release tools in `build-release/`; `--build`, `--release-build` and
`--build-host` select other builds. Some selectors run separately from the
groups; `chibi build test --list` shows every selector and group.

Add a case to the selector that owns its area; add a selector to its group's
row in `tools/tests.tsv` so the group, and therefore CI, runs it. Studio's
checks live in `chibi-studio`.

The groups of `tools/tests.tsv`:

| Group | Current coverage |
| --- | --- |
| `native` | CPU `test_tools` and build-graph selectors with checked-in inputs. |
| `projects` | The Release `chibi` launcher (`chibi-cli`, `chibi-cli-authored`) and its native builds (`native-build`). |
| `language` | `test_language` selectors, `chibi test` over the compiler, `core` and the compiler samples, and stage-one bootstrap and recovery. |
| `compiler` | The scalar package-compiler route. |
| `wasm` | The browser `test_language` build under Node. No build currently produces `build-wasm-language`. |
| `benchmarks` | The ECS query, native workload and scheduler benchmarks. |

The groups list their members explicitly; they do not include every standalone
selector. Registering a selector alone does not add it to a group.

CI runs the build, the `short` selector and the `native`, `projects` and
`language` groups on Linux and macOS.
The general Windows engine and Studio jobs are paused pending desktop archives.
The independent `Compiler self-build` workflow runs nonadvisory macOS, Linux and
Windows proofs on every PR into `main` and push to `main`. Each downloads a
checksum-pinned runtime seed and uses the checked-in Chibi compiler to compile
the current compiler/core sources. It then executes that output to compile them
again. This workflow installs and invokes neither a C compiler nor CMake, and
has no native-build job dependency. Both compiler processes use an isolated
input directory and an empty tool search path. The two stages must agree locally,
and the three platforms must produce identical compiler bytes. See the
[compiler recovery proof](self-hosting-build.md#three-platform-compiler-recovery-proof)
for the exact command, isolation and uploaded evidence.
The separate manual runtime-seed workflow retains the C/CMake recovery route
for updating the explicitly prebuilt runtime dependency.

### Starting CI with labels

The `CI control` workflow starts runs for maintainers and agents whose GitHub
access cannot re-run or dispatch workflows. It acts only when the person who
added the label or retargeted the PR has write access, which GitHub requires
for re-runs and dispatches anyway. It only acts on branches of this repository
and on the head commit that the event named, and removes the label when it is
done.

- `ci-rerun` re-runs the failed jobs of the PR's own CI and compiler
  self-build runs on the head commit. It never re-runs a push or publishing
  run that shares the commit. On a PR into `main`, it dispatches a workflow
  that never ran on the head branch.
- `ci-seed` dispatches the runtime-seed workflow on the head branch with
  `publish` set. Once all three platforms recover identical compiler bytes, it
  publishes the seeds as a commit-specific prerelease and commits the new pin in
  `tools/compiler/runtime.json` to that branch, then starts CI and the compiler
  self-build on the pin commit. Pull before pushing again. If the branch moved
  after the seed run started, unrelated branch movement fails the pin step.
  Rerunning the same workflow can resume its verified one-file pin commit and
  dispatch only checks not already started on that exact pin. Existing published
  seeds are reused only after verifying their tag target, source identity,
  complete asset inventory, checksums and three-platform recovery evidence;
  a mismatch fails without replacing published assets. Runs for the same source
  SHA serialize across refs and do not cancel an active publication/pin.

A dispatched run must build the head commit the event named. If the branch
moved in between, the workflow cancels the run and fails, and the PR needs the
label again. Retargeting a stacked PR to `main` does the same as `ci-rerun`, so
it gets CI without a new push. A dispatched run checks the head branch, not the
merge with `main`.

## Focused regression coverage

`chibi build test short` (and `native-tools`) runs `test_tools`' default cases:
execution policy, type metadata, the ECS world, relationships, hierarchy and
queries, and the room service. `chibi build test unit` runs only the execution
policy cases.

`chibi build test language-execution` runs the C cases of
`test/language/execution.c` through public SDK headers, on the VM and the
compiled backend. They check results and state changes, host calls, owned
values, linked functions, malformed instruction rejection, back-edge budgets,
integer bitwise/shift behavior at all eight signed and unsigned widths, the
verifier, managed pointers and their owners, native boundaries, string bytes and
the integer reports. Native code generation is bounded to these small functions;
there are no files, processes, frames or GPU work.

The 2026-10-21 shift-contract regression adds sixteen tiny functions (eight
integer types, two directions), each run on VM and the compiled backend with
three invalid counts: width, width plus one, and all bits set. These 96 calls
check DATA failure and source metadata. Existing valid-shift cases cover zero,
one and width minus one, sign extension and discarded bits. This bounded
extension was independently reviewed before execution; its no-IO/no-process
constraints are unchanged.

The same dispatcher adds six two-frame, ten-instruction definitions to check
the optimizer's treatment of failing shifts. Twenty invocations, each capped at
100 instructions, cover both directions, variable counts and valid/invalid
constant counts on both backends. An unused invalid shift must still fail and
preserve a prior persistent write; a valid shift reaches the subsequent write.

`chibi build test language-program` runs the six managed-capture cases of
`test/language/capture.c`: whole-value shrinking with escaped and self values,
capture paths that survive root replacement and fail atomically, nested scalar
writes that retain only the current managed root, references that refuse another
type, element writes that keep the item type, and unnamed union references.

The length-carrying string migration added these fixed CPU workloads to live
selectors. The counts describe coverage and do not record a pass.

- `language-record-values`: eight fixed UTF-8 vectors, eight indexed string
  keys, and ownership/native-boundary checks. One tiny nested-record case
  checks local type resolution, a conflicting global shape, atomic NUL
  rejection, retained bytes, ranges and collectable-arena rejection; it adds no
  loops or child processes. One partial native record copy adds three
  assertions for preserved opaque bytes and owned text after mutation.
- `language-execution`: the added five-instruction function runs eight times
  across two backends with a 100-instruction budget; four iterator calls cover
  NUL and malformed UTF-8. A fixed one-field native record
  checks read ownership and rejection before reference/constructor publication.
  Seven pointer cases cover owner isolation, complete base layouts, location
  identity, native-boundary rejection, malformed aggregate storage, effect
  restrictions and cycle collection.
- `language-record-document`: five cases, adding three tiny JSON roundtrips,
  four malformed byte tags and one rejected NUL name.
- `build-process`: the helper case adds seven operations on at most seven bytes
  and no processes. Binary-output and file assertions replace text rejection.

These extensions add no rendering, GPU or network work and no additional child
process launches.

`language-ranges` runs seven interval-proof cases on hand-built execution
functions: proven intervals that remove repeated clamps and checks, unordered
and signed-zero clamp selection, IEEE rounding and overflow in interval
arithmetic, exact integer comparisons, reassigned operands, control joins that
discard path-specific intervals, and opaque calls that end a proof. Allocation
and instruction assertions establish removed work; elapsed time is not an
optimization oracle.

`asset-values` runs nine cases on KDL asset text and the native value tree:
typed scalars at the 64-bit limits, slashdash, annotated arrays and source
ranges; reals and floats that read back bit for bit and write plainly;
escaped separators; thirteen malformed documents reported with a line and
column; owned snapshots and subtrees; ordered objects; exact numeric
equality; and JSON that owns its strings. 172 assertions in 0.05 seconds with
ASan/UBSan; in memory, CPU only.

`fmt` runs two cases on the vendored fmt library: positional, typed and
aligned arguments with escaped braces, and the runtime specifier path Format
Text takes, where an out-of-range placeholder renders empty. 12 assertions in
0.05 seconds with ASan/UBSan.

The independently reviewed `text-format` selector runs two fixed public native
formatting cases, with 41 assertions covering scalar formats, owned string and
array display, rejected printf modifiers and bounded placeholder indices. It
passes in 0.799 seconds under ASan/UBSan; there is no world, process or GPU work.

`native-values` selects one independently reviewed native SDK case. It checks
canonical ABI lookup, NaN comparisons, integer saturation, nominal enums, vector
conversion and output ownership after input/table retirement. The scoped native
consumer passes 43 assertions in 0.403 seconds, without files, processes or GPU.
`box3d` selects one in-tree check of the archive's prebuilt Box3D (v0.1.0,
the copy Cute pins). A box falls onto static ground and comes to rest, then
closest raycasts hit the box, hit the ground and miss. It needs a native tools
build and passes 14 assertions in 0.052 seconds, without files, processes or GPU.
`arena` checks the support arena allocator: growth that keeps earlier
allocations intact and aligned, a reset that hands back the retained blocks in
order, a zero-initialized arena, and collectable values found by address
through the page index across sweeps and a reset. It passes 360 assertions in
0.06 seconds.

The focused room-service check is `chibi build test collab` after
`chibi build --target test_tools`. It runs the real Node server from the C
dispatcher. Health readiness must succeed before the existing 401/201/403
authority checks. Unexpected responses report the method, path, expected and
actual status, byte count, transport errno or parse error, and the server's JSON
error code when available. A failed case prints its server output and retains
the temporary `server.log` at the reported path; passing cases remove their
files. Failure cases exercise invalid server configuration, an occupied port, a
live process without health readiness, and real receive timeout/connection
refusal. They use process signals and sockets rather than replacement server
responses. These cases also run in `short`; the focused command does not
certify that selector or the groups.

Routine CI and the correctness groups exclude benchmark selectors.
`chibi build test benchmarks` runs the benchmarks, or name one:

- `native-workload-benchmark` builds the four kernels in
  `test/native_aot/workloads` with `chibi build --native` and the LLVM tools in
  `CHIBI_LLVM_BIN`, checks each program's status against the VM's, and prints the
  build time, peak memory, size and median run time at O0 and O2. Each build
  runs under a Linux subreaper, so a timeout, an interrupt or the launcher's exit
  kills every process the build started, including those in groups or sessions
  of their own; a fake-launcher case checks that no descendant survives. It runs
  on Linux x86-64, the one target the driver admits, against the Release
  launcher, and skips the builds without `CHIBI_LLVM_BIN`. It takes about
  70 seconds.

Each fixture owns its fixed trial count and reports timings alongside correctness
assertions. Setup and correctness checks stay outside timed loops; no
elapsed-time threshold decides a pass. Retain the workload output and compare
matching build configurations.

CI remains partial coverage. Report the checks that actually ran. Applicable
server-required checks, independent review, explicit holds and approval rules
remain binding.

## Compiler bootstrap checks

`chibi build test compiler-bootstrap` includes seven CLI admission cases. They
check that the compiler host's `--bootstrap` mode refuses bad input before any
compile runs:
- mixed or missing options;
- a missing or invalid `bootstrap.kdl`;
- another manifest format, or a version older than the minimum;
- a folder with no manifest, or a closure its manifest doesn't record;
- a missing source, or a package that gained a file;
- a closure recorded for another target;
- a seed folder without a seed, or a seed whose sources aren't on disk.

The SHA-256 check is pinned by four digests: FIPS 180-2's for "abc", and those
of 55, 56 and 64 a's, which sit at the padding boundaries. The cases run about
twenty host children on tiny folders. The target case records the checked-in
seed's closure for a target no host has.

The seed read also admits 16 MiB + 1 byte and exactly 64 MiB, reaching source
validation, and rejects 64 MiB + 1 byte before decoding. An API case checks
artifact reads at 16 MiB, 16 MiB + 1, 64 MiB and 64 MiB + 1 with sparse files:
accepted reads still fail the unchanged checksum check, then the original
valid artifact loads again. Ordinary byte-output and asset-source bounds remain 16 MiB;
only bootstrap closures and existing CLI file outputs use the 64 MiB artifact bound.

`chibi build test bootstrap-recovery`, in the `language` group, selects two
cases. `--validate` admits the checked-in `tools/compiler/bootstrap` for the
working tree without recovering, and refuses a closure its manifest does not
record. Then the case exports HEAD's `tools/compiler` and `packages/core` with
`git archive` and has the unsanitized Release host recover the compiler from
the artifact.

`chibi build test bootstrap-load` loads the compiler in process from a Release
`test_tools`, since one recovery takes about 70 s there on an M5 and about 30
minutes in a Debug build. CI runs it in its own Linux step. Its one case adds a
marker to an exported compiler source and a new source file. The unlisted file
is refused, naming `CompilerSources.txt`; once listed, one recovery compiles
both. The load writes no output file, and the shared cache drops the oldest of
two sparse 1 GiB entries to fit its 2 GiB budget. A second load reads the cached
compiler without recovering. Interned strings keep the compiler near 23 MB, and
package sources are capped at 8 MiB, so this case can't cross the artifact
bound; the sparse cases above cover it.

## Private build-host process checks

`chibi build test build-process` selects the C cases in the existing tooling
dispatcher; `chibi build` builds `chibi-build-host`, `test_tools` and its
`policy_worker` dependency. The route uses one disposable directory,
fixed authored sources and at most 58 controlled process launches or failed
spawn attempts. It performs no
engine configure/build, network, renderer or GPU work. Case deadlines are at
most two seconds, with shorter deliberate timeout/cancellation budgets.

Cases cover exact argv and cwd, explicit environment and parent isolation,
separate output bounds, a step's 1,023-argument bound, exits/signals, spawn
errors, timeout, active cancellation,
and descendants whose leader exits first. The private host executes both the
controlled source and unchanged recording workflow on bytecode and SLJIT, with
actual backend counters and failure-path assertions. Compiler-enabled full
native builds also verify absence from the ordinary game catalog; minimal
compiler-only builds verify rejection by a catalog without build capabilities.
Two CLI invocations cover source loading, a nonzero authored result and a missing
entry. Seven additional cases cover argument and returned-value lifetimes,
string operations, truncated or binary output rejection, regular-file and size
limits, and atomic write failure cleanup. Five more cover a step's 1 MiB output
and unreported probes, file kinds, stamps and sorted directory entries with
their limits, atomic copies, renames and removals, SHA-256 against FIPS 180-4
vectors, and environment names. Their fixed file workload is about 10 MiB and 8,193 empty
files.

`graph-engine`, `graph-driver`, `graph-discovery`, `graph-c` and `graph-build`
run the authored programs in `test/graph` under the build host: the profile's
flags, argument checks and target selection; the driver's plans; toolchain
discovery against fixture installations; `tools/graph/c`'s steps,
response-file links and dependency files; and, in `graph-build`, Debug builds
of `chibi_support` in a copy of the sources that rebuild only what changed,
retry a failed compile alone and link an archive that runs, plus a launcher
build whose failing compiler leaves the staged headers in place. `graph-build`
compiles about fifteen C files and takes about 2.5 minutes.

`chibi-cli-installed` runs an installed launcher in place, `dist/sdk/bin/chibi`,
so run `chibi build install --prefix dist/sdk` first. Its cases cover `version`
and `doctor`, `chibi test` on package trees, including one that imports the
installed `core:` collection, and programs run from files and directories, with
`--watch`, cancellation and child processes.

`build-process` and `chibi-cli-installed` are in no group; run them by name.
Windows does not build or select the private host.

## Batch validation (CUT62)

Publication uses independently reviewed validation for the affected contracts.
A change spanning several subsystems can use focused checks when review accounts
for each changed contract and their interactions. A mixed scope label or an
unavailable full suite does not automatically block landing.

1. **Review and choose checks.** Independently inspect the diff, callers, tests
   and dependency/build effects. Record which contracts changed and why the
   selected checks cover them. Include applicable backend, sanitizer,
   load and release validation. File extensions and selector names alone
   do not establish coverage. Resolve unknown impact before publication. When
   affected coverage is unavailable, provide a reviewed replacement; otherwise
   that specific coverage gap blocks landing. Report unexecuted coverage
   without claiming a pass.
2. **Freeze and validate.** Use a clean candidate based on freshly fetched main.
   For a batch, pin each reviewed source and review the combined interactions
   and conflict resolutions. Record the candidate commit/tree, configuration,
   commands, exit codes and logs. Run selected checks, formatting and affected
   builds against that candidate. Preserve assertions and failure cases.
3. **Publish the tested tree.** Recheck main and sources, explicit PR/ticket
   holds, required approvals and actual server protections. Land the complete
   tested combination atomically through the authorized workflow. A new hold
   blocks publication even when Git is unchanged. Held code stays excluded,
   including indirect dependencies and copied patches. If the base, source,
   tree, conflict resolution, dependency or validation flags change, review and
   validate the new candidate. Intermediate batch trees require their own review
   and validation.

Local execution evidence is sufficient when CI is disabled or unavailable unless
server protections actually require CI checks. Workflow configuration describes
possible jobs, not mandatory execution of every matrix row. Inspect applicable
server requirements before landing; required checks and approvals remain binding.
An absent CI job is not a pass. Preserve existing backend and configuration
requirements for each selected check, and report other coverage as unexecuted.

A server-created merge/squash commit may have a different ID. Prove identical
tested tree and unchanged base and source revisions, and satisfy current-head
server requirements. Tree equality does not transfer a required status check or
replace a metadata-dependent check. Plans and component-head results grant no
merge authority.

## Retired coverage records

Until 2026-10-31 this page also recorded the workloads and measurements of
selectors that no longer exist: the stage-zero front-end, package and compiler
differentials, the compiler fixture and artifact routes, the renderer, material,
audio, scene, model and shader GPU checks, the Wasm and browser routes, Studio
acceptance, and the Make and `tools/test_policy.py` commands that ran them.
`git show ef9bc08c0:docs/testing.md` has that text, and
[the archive](../archive/README.md) lists the coverage that left with them.
