# Native toolchain continuation

The November 2 handoff continues in PR #1331 on `codex/native-default-20261102`.
The published checkpoint `b0f4dfa0b` has compiler/core source `776ead2f1`, following
CI merge `a76ea8005`. Further work is composed
on `codex/native-runtime-completion-20261102`. The owner's checkout and
unrelated work remain untouched; existing compiler worktrees supplied the
independent driver, CLI, producer, maps, tasks, Windows and reload lanes.

## Current checkpoint, 2 November

CI run `41531103929` reproduces Linux stages three and four byte for byte
(`1cff2a7d8d021daa4639df9537ba6fb136323f0317dd6a89976885c132e70b36`).
It then fails the Windows cross-link on LLVM-generated `memset`.
`7576fe5c1` supplies freestanding memory operations and checks 1,056 cases at
O0/O2, including an explicit UCRT collision check. `4db6b2eb4` repairs a deleted
helper still called by a retained Linux C test. The completed run also exposes
native language gaps on macOS. It is not a green combined run.

The next batch includes POSIX process-group and terminal supervision, its watch
adapter, waiting scalar foreign calls, native core/portable selectors, source
inventory receipts and the Windows PowerShell 5.1 bootstrap. The original
fallible-result checker now also runs on native source; its stricter coverage
found unchecked cleanup errors in the native driver. The callers now consume
those errors, and a missing test-output file can no longer produce a false
PASS. `3736ee5b5` preserves false optional-bool payloads after a presence guard;
the seven original cases and parenthesized variants pass at O0/O2.

The compiler/core snapshot at `9306b2fe0` builds through the `776ead2f1`-derived
intermediate compiler in 110.63 seconds. Its next native generation is byte-identical,
SHA-256 `40d00b84123ae44eb8bf5453747276666338b3f298a717f42c3b1acba5c1db58`.
The original `60901f2fa` pin rejects the newer combined source, so advancing the
verified seed is required before the next normal checkout build. The existing
`60901f2fa` → `776ead2f1` proof below is the intermediate upgrade evidence.
CI run `41539102677` on `c5c0a1ac2` retains Linux native candidates and their
fixed-point log; artifact existence alone is not acceptance. Local Linux
self-build attempts were killed by Docker's memory limit, including one worker
(`OOMKilled=true`); other running services were left untouched.

All seventeen CLI survivor cases pass through the public native compiler,
together with four parent-signal and six terminal cases (43.02 seconds).
The six backend survivors pass on macOS (36.01 seconds) and Linux (151.40
seconds), including exact bounds diagnostics and child-process cleanup. Their
C harness deletions are being integrated. Fresh installed-SDK acceptance is
pending: the old `build-native-product-proof` fixture correctly rejects its
stale compiler/core resources before reaching the intended installer checks.

Remaining ownership is explicit:

- CLI lane: Windows public watch acceptance and the live compile-only asset
  facade. Replace desktop/browser C orchestration, cache and generated identity
  C with authored code and LLVM ABI adapters while preserving diagnostics,
  quotas, cancellation, thread isolation and caller-owned outputs.
- Producer and collector lanes: source pointers and precise ownership roots;
  reload lane: retained arrays and nominal retained schemas.
- Driver lane: remaining authored language samples and error-result handling.
- Tasks lane: the six remaining C native-backend cases, preserving their live
  behaviors before deleting the C harness.
- Cutover lane: all seventeen CLI survivors and signal/terminal counterparts.
- Root: graph fixtures that depend on retired C targets, final host/VM deletion,
  recovery-command and CI retirement, combined review and platform acceptance.
- Windows lane: optimized memory helpers, real Windows execution, and verified
  fixed-point seed preparation. Root owns final seed publication and pinning.

No further owner decision is pending. Archived engine and Studio services stay
archived. The public compile-only API remains a live dependency until its
replacement passes; its C wrapper is not an acceptable final state.

Published `590030513` failed before runtime testing: the pinned native seed
could not read an optional error record's fields while compiling the newer
driver. `776ead2f1` fixes that source compatibility with one plain error-result
implementation and the existing optional API adapters. It preserves failure
kind, raw tool status, cleanup and publication behavior. The exact original
SHA-verified seed now builds the complete combined compiler.

That seed build and two native self-builds take 42.80, 42.39 and 37.14 seconds.
Stages three and four have identical SHA-256
`06338f062bb78e1aed010efc51921cef84a5d5bbfa976152739493bdd3355021`.
The immutable local compiler receipt is
`/private/tmp/chibi-776ead2-compiler/receipt.json`.

Combined source validation passes:

- 429 compiler tests in 24.18 seconds; all 36 unchanged core math tests in 1.41
  seconds and native O0/O2 math fixtures in 6.01 seconds.
- Native CLI in 49.37 seconds, public watch in 9.89 seconds, and generations
  including the three session survivors in 8.64 seconds.
- Map ownership in 39.42 seconds, aggregate/source-copy ownership with poisoned
  frees in 12.05 seconds, and managed collector lifetime checks in 8.19 seconds.
- Native scalar producer in 50.22 seconds: eight behavior cases, fifteen real
  source projections, 169 malformed variants and all 152 corpus cases.
- Authored target runner: two modules, 1,336 calls and five macOS programs in
  0.74 seconds; C/native rejection controls in 7.89 seconds. Both target runners
  cross-build in 3.15 seconds; actual Windows execution is still required.
- All 25 native build cases in 21.20 seconds, including exact tool failure and
  signal statuses, preserved prior outputs, worker deadlines and Windows
  no-CRT import checks.

The actual Wasm compiler quota/cancellation regression passes in 158.04 seconds
against an immutable snapshot of this source, including concurrent requests and
allocation balance. Concurrent timings are acceptance evidence, not controlled
performance comparisons.

The VM-backed AOT driver, obsolete C session test driver and migrated C scalar
producer tests are deleted. Their surviving behavior now runs natively. Native
SDK identity no longer depends on C VM format headers. Source arrays share
backing through copy on write, while direct native array-copy operations keep
their existing clone contract for raw foreign calls. The tracing collector is
implemented and tested; source pointers and their task/generation roots are
still being integrated.

CI keeps both explicit VM/native command comparisons, all previous native
checks, and new collector/character coverage. Windows checkout, DLL isolation,
UTF-8, foreign buffers, precise math and C/native target comparisons are wired
into the same workflow. The source-compatible seed fix must pass fresh CI;
local cross-linking is not Windows runtime acceptance. The native fixed-point
phase now runs after an attempted core phase even when a core test fails; the
core failure remains red. Initial/final source fences and the existing phase
budgets remain enforced. Seventeen affected orchestration checks pass in 8.91
seconds; the updated real Git source-fence and workflow checks pass in 1.91
seconds, and actionlint passes with shellcheck disabled.

The evidence below is chronological. Older counts and gates describe their
named revisions, not the latest integration. Remaining deletion gates are:

- Finish native language and ownership parity, including live pointer forms,
  array-backed records/maps and managed suspended/retained values.
- Integrate the reviewed CLI and backend harness deletions after their passing
  survivor proofs; retain the native cases in the canonical selectors.
- Finish remaining live graph/CLI consumers; native build/link and producer tests are migrated.
- Validate the combined installed compiler, CLI, asset companion and optional
  browser SDK on each supported platform, including Windows execution.
- Retire remaining prepared-runtime consumers. The obsolete C session ABI and
  already archived engine/Studio services do not need replacement adapters.
- Publish verified platform seeds only after their acceptance checks, then
  delete obsolete hosts, recovery artifacts and VM paths with no remaining users.

## Order of work

1. Make the SHA-256-verified native seed the normal build entry and make native
   compilation failures stop the build.
2. Integrate native driver caching, CLI commands, OS operations and LLVM setup.
3. Replace VM-dependent behavior with tested native maps, tasks and retained
   program generations. Keep unsupported forms explicit.
4. Move the installed product, SDK asset compiler and remaining test consumers
   to the native path, then remove their hosts and bytecode seeds.
5. Delete the VM only after the replacement passes the remaining behavior and
   platform checks. Remove obsolete aliases, flags, tests and workflows with it.

The user approved program-owned tasks with parent-child cancellation, suspended
tasks finishing in their original code generation after reload, and LLVM 22 as
the minimum. Task deadlines and lifetime after the last handle drops remain
part of the parity requirement.

## Implemented in the integration branch

- The native seed and native authored build programs are the default. Their
  caches preserve previous outputs on rejection; the automatic VM fallback is
  removed. Native recovery and the vendor sample need no C compiler or VM host.
- The native CLI supports build, run, test, version and doctor, plus Linux
  `run --watch` with retained-state migration and rejected-edit recovery. Source discovery,
  cache receipts, content invalidation, thin-archive rejection and interrupted
  publication have real compiler regressions.
- LLVM/LLD 22 or newer is required before compilation writes outputs. Verified
  cache hits avoid repeating version subprocesses.
- Native maps support scalar/string values, owned copies, updates, deletion,
  parameters/results and references. Native tasks use reusable generational
  handles, ordered scheduling and explicit cancellation/shutdown cleanup.
- Native Linux shared generations support numeric/bool retained records and
  standalone strings, schema checks, runtime failures and process context.
  The host advances active tasks and keeps retired tasks in their original code.
- Windows has filesystem/cache operations, a cross-build route and verified
  LLVM installation that extracts only the tools and DLLs it uses.
- Legacy compiler-resource staging, host test selectors and explicit bytecode
  recovery prepare their seed at their own boundary. Native-only routes skip
  that seed; host-only SDK builds prepare it before copying compiler resources.

## Verification recorded so far

Final local checks used LLVM/LLD 22.1.8 from `/tmp/chibi-llvm22`. Earlier
checks using a mixed LLVM 23 / LLD 22 directory are not minimum-version evidence.

- At `9f9e66c7a`, macOS native stages two and three are byte-identical:
  `789bdf44015f6c8e80fe854383e3334112b9f53798543f79e78998de6772ce19`.
  They took 40 s and 36 s; the full proof, including native driver/graph checks,
  took 80.50 s.
- Linux CI at `9dae90af7` passed the complete native suite, no-C-compiler proof,
  default full build, SDK relocation, and legacy compiler self-build with its
  strict scalar regression. Its native stage-three/four fixed point was
  `fddf00d2ebdfcb62e6cfb3cd058e44e84dd6820cffddbf71b65628bccf76633a`.
- Windows CI at that revision passed filesystem/object checks and native
  self-reproduction. Stages three and four took 325 s and 321 s and both produced
  `b1bc3c1e4dac87472fdb624e471f21b368ff9d76f97facc54aa4d722db8a5490`.
  Compilation remains serial until asynchronous processes land. Verified
  selective LLVM extraction also passed on the real Windows runner.
- Final native generation and watch suites passed in 37.46 s and 11.92 s.
  They cover rejected edits, retained records/strings, active and retired tasks,
  task failure, Main once, empty PATH and missing-import recovery. The legacy
  bulk-state regression preserves `1,2,3,113,123`, including a new binding's
  initializer; bool and float defaults retain their encoded values.
- All 408 authored compiler tests passed in 84.62 s after moving four scalar
  facts tests into their proper test layer. The moved tests also passed through
  the strict bytecode/SLJIT entry with identical artifacts and zero fallback.
  All 160 original emitter tests separately passed natively in 2.85 s.
- The final C scalar selector passed 10/10 tests and 2,456 assertions in 13.26 s.
  Target metadata checks accept LLVM 22's canonical macOS layout and still
  reject incompatible, missing and duplicate metadata.
- Real SDK installation/relocation passed locally in 26.83 s after a 5.11 s
  incremental host/resource build. Graph planner tests passed through their
  native fixture in 14.10 s and exact legacy-host CI route in 20.16 s.
- Map/task O0/O2 tests check allocation balance; map tests poison freed memory.
  The warm native build-entry census took 0.56 s.

The broader engine CI at `9dae90af7` exposed the test-layer dependency, LLVM
layout fixture and legacy retained-default defect described above. All three
have focused passing checks in the final source tree. The next CI run verifies
them together; its status is recorded in the PR. Cross-linking alone is not
runtime validation.

## Retirement scope

The current deletion gates are listed at the top. The authoritative survivor
inventory is `docs/c-test-verdicts.md`, together with the workspace's
`handoffs/goal.md` and `handoffs/vm-deletion.md`; inspect current consumers
before each deletion. Archived engine, renderer, game and Studio services
stay archived. The old C program-session API goes with the VM; preserve the
three live reload behaviors without recreating that API.

Native typed expected failures, scheduling and foreign array loans are
implemented. Standalone runtime diagnostics, live managed value forms and
reload/watch platform coverage still need completion. Automatic build-entry
fallback is removed; remaining explicit recovery assets and VM dependencies
are not yet retired. Completion means their deletion, verified platform seeds
and passing combined behavior checks on the final source revision.

## Parallel performance work

Current ownership: root owns native build/test migration, including
`test/native_aot/native_build/BuildTests.chibi`; the performance chat
`01a19cba-8db8-7041-9a3e-cb5293f1603b` owns measured compiler iteration work and
reviewed the single-workflow CI reconciliation. The following entries record
its earlier baselines and coordination history.

Its original frozen native baseline is `13e01f9055e555caf207642df8ab477f721c38c2`,
tree `0455761e0b537dba872c7078b80def510ddd3edc`, whose combined CI is green.
The proposed CI-consolidation parent is
`2fb44239a425e18b231fc44d0da0392ea31fac74`, tree
`e62a39c31d25c85ea9a8b41fd0affeb81955d715`. I agree to combining these frozen
parents before performance changes, preserving the verified native seed,
native build cache, driver and graph parity, package test isolation and LLVM 22.
The performance chat owns that merge and must record its resolved commit/tree
before using it as a measured baseline. A dry merge found workflow
modify/delete conflicts plus `ci.yml` and `tools/toolchain/Driver.chibi`
conflicts; its conflict-marked tree is not an executable baseline.

Current migration lanes own installed CLI/SDK graph construction and native
driver resource discovery, asset compilation, native failures/quotas,
maps/retained ownership, Windows process operations and native test selectors.
The performance chat should coordinate before editing those files. No held
fixtures or additional access restrictions apply; unrelated worktrees remain
untouched. This integration has committed task timing work beyond the frozen
baseline, so performance work must not copy the live working tree.

The combined performance baseline must retain both driver and graph VM/native comparisons. Ordinary CI passes --compare-vm together with --skip-vm-comparison; the latter skips only the historical vm-hosted-self-build block. Full CI passes --compare-vm without that skip. Native LLVM setup uses .github/native-llvm.sh and CHIBI_LLVM_BIN, independent of C/libclang18 setup.

The performance chat reports its resolved frozen merge as
`9934af8db17daf554993bba0bd5dbc6ef37c28ca`, tree
`48534e78286e0442e4b2d54f0d86603c78b1b676`, with both parents above. Its
graph-driver check passed in 23.89 s after repairing the comparison fixture.
This is the performance baseline, separate from ongoing migration commits.

Quiet Mac measurement window: 2026-11-02 15:34:00–16:04:00 UTC, including one
ten-minute extension for controlled CLI samples and compiler profiling.
At 15:33:30 UTC every migration lane reported its heavy jobs drained and new
heavy launches held. No `/tmp/chibi-heavy-slot*` owner remained. A process
snapshot showed one existing `chibi` process, PID 44888, at 0.0% CPU; no active
`opt`, `llc`, `clang` or `chibi-compile` process appeared. Source edits/reviews
and remote CI continued. The performance chat released the machine at 16:04 UTC,
and every migration lane resumed heavy checks through the shared slot wrapper.

## Continued implementation after the first CI checkpoint

The first combined checkpoint, `13e01f905`, passed all required CI, including
Linux/Windows compiler reproduction, no-C-compiler recovery, graph SDK
relocation and the default build. This is evidence for that revision only.
The following local changes extend it; combined validation remains required.

- `83ada23b1`: authored native asset compiler service. Prepared output and first
  diagnostics match the existing compiler. Relocation with an unusable C
  compiler passes. Desktop engine and Studio consumers are being migrated.
- `68672f1b3`: native named entries, roots and collections use the verified
  program cache. CLI and cache checks passed in 4.52 s and 6.00 s.
- `c23667760`: typed expected assertion/index failures are scoped to generated
  test roots. O0/O2 source differentials match the existing compiler.
- `93a1d70e7`: task delays and deadlines use program time, preserve cancellation
  ancestry and support explicit host time. Standalone programs sleep when all
  tasks wait. The final O0/O2 allocation-checked suite passed in 11.07 s;
  Windows cross-linking passed. Full platform runtime checks remain required.
- `e3d8d76b0`: authored map iteration preserves insertion order and snapshots
  when mutation/addressing requires it. O0/O2 poison and allocation checks pass.
- `6ca0cf900`: retained string records and native aggregate ownership passed
  Linux reload/watch and compiler reproduction. The expression alias lifetime
  regression is fixed by `b979ddc4f` below. Existing mixed array-backed records
  still retain six allocations;
  generic copy-on-write backing ownership is in progress before VM retirement.
- `67efc3a3e`, `e029cb4ef`, `f3d70e879`, `636ed0928`: Windows asynchronous
  processes, content fingerprints and verified reuse of pinned cross-build
  artifacts. Real Windows run `41472306534`, source `61bf5fffc`, passed cache,
  async, graph-content and object tests. Stages three and four took 339 s and
  334 s and produced the same SHA-256:
  `fd4a9dc245aef01df5aa8c243f4ecee7e744ff14fcf5f12ccc14530a032c4432`.

The performance baseline's direct native compiler test invocation passed 380
tests, with 28 tests absent because `math_constants/tests` and
`unit_algebra/tests` failed native compilation. The older 408-test result above
used the authored compiler's existing test route; it does not establish direct
native coverage for this inventory. Both rejection sites are assigned to the
producer lane; comparison acceptance requires all 408 tests. The scalar square
root fix below restores the 23 math-constant tests; five unit-algebra tests
remain blocked by array payload handling.

Further integrated changes and evidence:

- `3171ea32b`, `2cc8f7d89`: shared native quota scopes and numeric-record maps.
  Combined task and map suites passed in 11.22 s and 16.28 s.
- `673de5755`: scalar f32 square root. Real O0/O2 rounding, signed-zero, NaN,
  infinity, subnormal and name-shadowing cases passed in 1.85 s and fail before
  the change. The direct compiler inventory now passes 403 tests (18.75 s).
- `51d6c6438`: native formatter preserves the existing scalar grammar, raw NUL
  bytes, numeric widths/modifiers and float formatting. The original 192 string
  tests pass. Combined O0/O2 formatter regression passed in 1.73 s.
- `b979ddc4f`: managed record expression lifetimes now survive a later argument
  replacing their source record. The old binary fails the regression; combined
  O0/O2 ownership checks pass with zero outstanding allocations in 1.38 s.
- `116ecabe6`, `52c39f4ad`, `f422a8bb2`: installed native products, portable SDK
  paths and native test-selector dispatch. The combined product compiler built
  in 47.88 s, the actual product graph in 144.26 s, and SDK installation and
  relocation checks passed in 15.34 s. These are ambient-load elapsed times.
  The separate product lane also passed the full install command with CC/CXX
  disabled and no legacy hosts (95.16 s), then its installed SDK test selector
  (19.03 s). The combined compiler's next fixed-point check remains pending.
- `962e420bd`, `baf6c3450`: private temporary directories use native OS APIs,
  and Windows now uses the shared parallel LLVM object pipeline. Real Windows
  execution of these additions remains pending.
- `87ad35942`: desktop C consumers use the native asset companion with quota,
  cancellation, cache and relocated-path behavior. The standalone C consumer
  regression passed in 4.924 s without compiler-host or VM objects. Studio
  companion packaging is committed separately as `94379f6`; its actual CMake
  copy/install/relocation check passed in 1.905 s.
- `3378845ca`, `d7d288047`: shared quota failures match the ordinary runner's
  exact category, and native test selection reads argv without core imports.
- `8b3be1d9d`: void entries use the existing zero-status ABI adapter. CLI,
  waiting void Main at O0/O2, selected void tests and a Linux shared generation
  execute successfully. Attribute checks passed in 1.63 s.

Remaining runtime consumers include `application.*`, `program.open/update/call`
and browser compilation/execution. The current WebAssembly JIT still delegates
some operations to VM stepping. These need working replacements before their
VM support, APIs or recovery assets can be deleted.

At the next checkpoint, `975dbd72d`, the optional-value fix and scoped map-alias
fix are integrated. Its compiler source reproduced identical macOS arm64
executables: SHA-256
`efd11e1e9fd61aa03f5c02ce711c1202ba73500dc23721cd68d238bfe94369bd`.
The two builds took 78.23 s and 61.52 s under ambient load. Subsequent local
commits through `3520eaef0` changed graph/docs/CI and asset test fixtures, not
the compiler inputs used for that proof.

CI at `975dbd72d` found the native asset product was still compiled by the old
pinned seed, which cannot emit the new quota-scope helpers. Local graph proofs
had supplied a current compiler and therefore missed this dependency. The
fix `b1f484167` makes the freshly built compiler a prerequisite of the asset
service and records that compiler in its receipt. The exact old-seed product
graph passed in 144.25 seconds and its warm repeat in 0.15 seconds; combined
CI is checking the corrected graph.

The second quiet performance window ran from 17:00:40 to 17:05:14 UTC. All
migration lanes drained before it and resumed afterward. Four balanced pairs
measured removal of `StatementStep` type-table snapshots: mean wall time fell
from 35.02 s to 31.49 s and mean CPU time from 63.93 s to 60.69 s; all eight
outputs matched the frozen compiler. The focused commit is `7d798d802` in
PR #1333, composed as `aa2affbea` with the task type-compatibility fix. These are
compiler measurements, not complete installed-toolchain equivalence results.

## Native integration checkpoint: 921c56bd0

The combined compiler built on macOS arm64 in 56.35 seconds. Its exact-source
checks passed: all 414 compiler tests (14.09 s), CLI workers, waiting tests and
assertion diagnostics (13.33 s), native maps and string lifetimes at O0/O2 with
poisoned frees and balanced allocations (24.58 s), and C/native product receipt
checks (2.08 s). Five Wasm foreign pointer-width rejection cases preserve the
existing output (0.12 s). These are ambient-load elapsed times.

This checkpoint also integrates counted Windows UTF-8 command conversion,
cleanup after failed LLVM object production, portable float-format fixtures,
macOS shared generations/watch, owned Windows library handles, complete native
product source receipts, and the native graph-test runner. The graph runner's
eight selectors passed from the actual pinned seed in 62.73 seconds at
`7cd5cfbd5`; the subsequent evidence changes passed the receipt checks above.
Managed arrays and map payload preparation passed together in 12.85 and
24.57 seconds at `8f15d4457`. Source-level array map payload admission remains
closed pending projected writeback support.

The earlier compiler source at `1a7a1002f` reproduced identical macOS stage-two
and stage-three executables, SHA-256
`76a2d9e55610ce9f5f50bd73a69c1408b0167752e32b01c833f9dfbcc500f4a7`.
That fixed-point result does not certify this newer checkpoint. Hosted Linux,
Windows and SDK checks must validate the new combined revision. The Windows
long-path old-versus-fixed regression is also still running.

The integration owner carries PR #1331 through independent review, exact-head
validation and merge, and then reconciles #1330/#1334 against the settled
source. The fixes in #1332/#1333/#1335/#1336 are incorporated here; their drafts
will be closed after complete supersession is verified. The owners of
#1315/#1319–#1325 continue their remaining implementation under this integration;
each older draft needs a final diff audit before closure. None is waiting on a
new owner decision. Ordinary CI does not replace full sanitizer and clean
bootstrap acceptance. Further cache work is stopped; performance work targets
measured compiler-code costs. The latest matched hosted comparison showed no
overall CI speedup.
