# Native toolchain continuation

The November 2 handoff continues in PR #1331 on `codex/native-default-20261102`.
The earlier published checkpoint `b0f4dfa0b` has compiler/core source `776ead2f1`,
following CI merge `a76ea8005`. Further work is composed on
`codex/native-runtime-completion-20261102` and published to the same PR. The owner's checkout and
unrelated work remain untouched; existing compiler worktrees supplied the
independent driver, CLI, producer, maps, tasks, Windows and reload lanes.

## Current composition: 2ec95de3f

Source map copies now share counted backing, with canonical mapped-array
mutation and foreign-buffer loans. Nested map ownership helpers and array-held
map descriptors are integrated; the nested-map source projection and indexed
record-array field writes remain in progress. The exact source at `240d47d52`
reproduces between macOS stages three and four, SHA-256
`7b60b3ef3f556f8621b09ed85deb202091c60e022bc3aa6bc062052588e11d38`.
Its 485 compiler tests pass in 23.82 seconds, map ownership checks at O0/O2
with poisoned frees in 73.00, and character/UTF-8/callback checks in 10.41.
The directory fixture then needed its scope ID corrected from `i64` to `u64`
and an explicit working directory; the corrected suite passes in 7.75 seconds.
Evidence is in `/private/tmp/chibi-240d-proof/receipt.json`.

The intermediate seed candidate uses exact source `14f7a46a0`, including those
fixture corrections. macOS stages two and three match byte for byte after
19.09 and 21.93 seconds; all 485 compiler tests pass in 24.64. Linux's
consecutive builds with the canonical `chibi` basename match after 53.67 and
53.80 seconds, and all 485 tests pass in 71.73. Earlier Linux builds named
`stage3` and `stage4` differed in ELF source-file basename records and are not
the accepted fixed-point pair. Both platforms build with CC/CXX disabled.
`/private/tmp/chibi-native-seed-14f7-proof` holds the exact source, builders,
commands and logs. This package is being prepared; it is not published and the
approved c5 seed pins have not advanced to it.

The composition also fixes Windows synchronous process exit-code bit
preservation and the successful compiler-proof step's final PowerShell status.
A fully pinned manual CI dispatch can now schedule Windows without waiting for
another Linux bootstrap. Actual hosted acceptance of these changes, the private
generation fixture correction and the LLVM float-absolute lowering remains
required.

The newer composition adds counted map backing with canonical writable entries,
strict array ranges and lazy optional-array fallbacks, synchronous assertion
cleanup, checked directory enumeration, optional snapshot callback guards, and
true void C callback adapters. The callback source and raw-facts boundaries pass;
an actual C caller passes at O0/O2 in 1.26 seconds, the character/UTF-8 suite in
11.63, and the composed map suite with poisoned frees in 66.30. The compiler
passes 483 tests in 35.52 seconds before the final callback literal-validator
correction; focused source and malformed-facts checks pass after that correction.
Evidence is in `/private/tmp/chibi-void-callback-proof/receipt.json`.

The exact nine-family sample audit passes 455 of 456 cases with no runtime
failures and preserves all 441 original passing names. All sample source Git
blobs are unchanged. Only `collections/view_element_lending/facts.Calls` remains
uncompiled; aggregate view ownership is in progress. The inventory and original
foreign/font setup receipts are in `/private/tmp/chibi-original-nine-matrix`.
Checked directory enumeration still needs the pending GC poll's OS-error
preservation proof and hosted Windows execution. Full callback routing,
safe-point polls and managed task promises are being proved together before
integration.

### Published-seed chain at 72598f131

The current compiler includes optional and union map values, owned task results,
string-array views, deep inline array places, ordinary record-array resize
failures, record formatting, immutable text sequences and true void foreign
calls. It builds in 20.69 seconds and passes all 480 compiler tests in 24.45
seconds. The coupled task suite passes in 42.91 seconds. Managed-pointer/text
checks ran for 67.80 seconds before an outdated fixture asserted the former
56-byte heap size. `56af95c23` updates that assertion to 64 bytes; the remaining
native, view, optional and rejection checks pass in 49.95 seconds. Evidence is
in `/private/tmp/chibi-200b-proof`. `72598f131` also preserves the independently
verified dynamic named task-result fixture. A fresh build from the published
macOS seed takes 1194.70 seconds because that seed retains the older type-lookup
cost. The next stages take 29.34 and 20.11 seconds and reproduce byte for byte:
SHA-256 `c387cc619e0afd01d1a2f958040845f9a9782cbd8daa4da6cb2c7b4c777e0841`.
That binary also matches the compiler used for these 480-test and managed/task
proofs. `/private/tmp/chibi-published-seed-current-proof/receipt.json` records the
seed identity, source, all stage hashes and timings. This establishes the chain
for `72598f131`, not a fixed point for the later composition above.

The preceding `ce3adf842` composition reproduces between macOS stages three and
four, SHA-256 `7cdad0bf57e64ebfbab323345a731dce39b333708c43c921e7e38110d7bd0f9a`.
Its three builds took 19.12, 19.62 and 19.81 seconds; the older producer's stage
two differs. All 474 compiler tests passed in 23.34 seconds. Composed managed
values passed in 45.80 seconds, tasks including image-unload ownership in 38.63,
formatting in 10.93 and array operations in 26.73. Evidence is in
`/private/tmp/chibi-ce3a-proof`.

That source's product build passes with CC/CXX disabled in 62.61 seconds, and
full SDK installation/relocation passes in 85.10. The C consumer then exposed
missing logical-place failure adapters in the authored archive. `0cfff8230`
adds them to the desktop and Wasm runtime templates. The unchanged C consumer
passes in 7.00 seconds against the rebuilt archive; Wasm resource cleanup and
successful-call recovery pass at O0/O2 in 2.19. Hosted macOS job `125743532364`
of run `41583508859` has the same missing-symbol failure at published `6b46f9261`.
That run completed with failures. Linux reproduced its compiler, but its core
phase hit the existing deadline and its obsolete VM Driver comparison failed
before Graph parity. Windows passed native products and public watch at O0/O2;
it failed the C facade's same missing helper, a private generation fixture's
missing managed context, process exit-code interpretation, an unavailable
`fabsf` import, and the final PowerShell status after successful checks. These
are distinct failures, with fixes integrated or under focused validation.
A separate dispatch, `41589954388`, validates `72598f131` on
`codex/native-platform-validation-20261102-v2` without cancelling that run. It
reuses the byte-verified Windows bootstrap from successful Linux producer job
`125487999272` in run `41506782159`; the new Windows job must establish that
the older bootstrap builds this source. These are separate revision-specific
results, not platform acceptance.

The parity audit at `a0a7ae3c5` preserves all 255 previously passing names and
passes 263 tests in the four affected sample families. Seven cases remain
uncompiled; all are accepted by the ordinary compiler. Strict array ranges and
aggregate views have active owners. Nested map ownership, mapped foreign
buffers, managed task results and void C callback adapters are also in progress.
The collector callback foundation is composed: the heap is 64 bytes and its
family ABI is 3. Complete callback routing and safe-point polls are still in
progress; the foundation alone does not fix the reproduced collection-reentry
failure. Native instruction IDs 107/108 remain reserved for those polls.

The C/VM/recovery deletion candidate remains held until these native behavior
replacements and supported-platform products pass. Its independent Driver and
Graph command fixtures preserve the comparison obligations; removing an old
comparison is not a parity pass. PR #1331 remains open and is not ready to merge.

## Earlier checkpoint, 2 November

The composition at `95ec7d976` includes the bounded constructor fix, authored
asset-compiler facade and archive builder, native host clock projection and
embedding, retained snapshot infrastructure, nested/reverse array iteration,
whole-array managed cells, and aggregate formatting. Its macOS stages two,
three and four are byte-identical:
`340210ff3cfa3bc32472fa3a027a32d65a6caf9691002e7d5995ef8718b33ad0`.
Build times were 23.40, 22.66 and 25.22 seconds under shared machine load;
these are verification timings, not a controlled performance comparison.
The same source passes 455 compiler tests in 26.10 seconds and the complete
native clock projection/factory/two-image suite in 27.52 seconds.

The later compiler/core source at `3d4d948e9` adds primitive pointer views,
canonical nested-array mutation, retained owned source values and numeric
compound map assignments. Its compiler builds in 18.37 seconds and passes
458 compiler tests in 27.39 seconds. The complete managed-pointer/view suite
passes in 85.77 seconds, nested arrays in 21.01, and maps with the promoted
compound fixture in 48.09, including O0/O2 poisoned allocation checks. All
213 frozen compiler/core files match that commit. This is a composed runtime
check; the newer source has not yet received its fixed-point proof.

The preceding `bba9ddaae` composition passes the generation/family/snapshot
suite in 49.92 seconds, builds native compiler/service/archive products in
53.91 seconds, and passes the actual C facade consumer in 7.71 seconds.
The SDK source check exposed a copied empty test directory whose original was
removed after the build. `7212c8c6e` makes the empty-directory rule symmetric;
missing, changed and extra source files remain rejected. The full SDK suite,
including archive receipts, failure cleanup, installation and relocation,
passes in 86.87 seconds against the frozen product source with this SDK fix.

The authored facade also passes actual Linux and plain/threaded WebAssembly
consumer proofs. Its Windows archive cross-build is verified; actual hosted
Windows discovery, Unicode paths and relocation remain open. Clock ownership
is registered in both POSIX and Windows CI. Studio commit `69ca96dbb` owns all
19 browser-storage cases plus two process supervision controls (21/21 pass);
engine `450f8f2e6` removes their C dispatcher. The host-clock C fixture and its
embedded-source header are retired in `de7a751c6` after the native selector
passes, preserving the live clock behavior and removing a private VM transport
quota. Native asset byte/depth/CHECK limits remain unchanged.

Completed CI `41563501452` Linux phases proved a fixed point for published
`d4451f6be`, SHA-256
`19333fe99140e067510ed7925c964142ed8ba5d5f5a5e6a358bf1a6b5a7819b6`.
This run failed broader acceptance. Core tests rejected an unchecked fixture file-read result, then the
existing core phase deadline expired. Native CLI validation found three more
unchecked fixture reads. `a3853c327` and `5dee31198` correct those fixtures.
The platform module bundle held only runners because its producer had stopped;
those consumer failures occurred before native module execution.

Owned values across task suspension, optional managed cells, additional view
payloads, collection equality and remaining map/foreign-buffer forms remain
active implementation lanes. The parity audit compares native restrictions
with the ordinary checker; a native rejection alone is not preservation evidence.
The original sample survey covers all nine categories: 441 passed and 14
required missing native support at `450f8f2e6`. The newer source needs a fresh
combined run of those cases after their owning changes are integrated.
The final VM/build/recovery removal is being prepared separately and will be
integrated after the replacement behavior passes. Graph C fixtures now use the three-source `chibi_graph_fixture` archive and
`graph_headerless` program; `ca08e2a35` removes the 552-line `policy_worker`.
Incremental/archive/headerless controls pass on macOS (14.49 seconds) and
Linux (36.42 seconds), preserving foreign-library build checks after VM deletion.
This is not full platform acceptance and PR #1331 is not ready to merge.

Hosted CI `41574174752` at published `7482af7be` confirms macOS native products,
CLI and compiler/core tests. Its sample failures match the known admissions.
`ac9db86ae` fixes a formatter test that expected a later diagnostic than its
fixture reached; both the union-payload and formatter restrictions retain
separate rejection/output-preservation coverage. `517dd1b0b` checks each
temporary-directory error independently, fixing the Linux graph-OS fixture.
The Linux native compiler reproduces at stages three and four with SHA-256
`6e867c8e5588509286a5709ecdca902e8aaeb6d3db51f1ea4efb1bb09b677d03`.
Its old VM driver comparison failed before graph parity, and a scalar bounds
exit-code mismatch remains under investigation. Windows execution is in progress;
these results do not establish final platform acceptance.

## Earlier integration evidence

The later composition `3e038edf1` adds explicit clock grant/context support,
ordered array-place writes, managed-record image leases, and the private
build-host test retirement. An experimental constructor-growth builder compiles
that frozen source in 17.40 seconds. The resulting compiler passes 446 compiler
tests in 24.90 seconds, the generation/family suite in 36.95, and array operations
at O0/O2 in 14.84. This is a composed runtime check, not a new fixed-point claim;
the constructor fix and public clock bridge are still being validated separately.

`decffa729` restores binary-copy, nonempty publication and file-identity controls
in the native OS suite, passing on macOS arm64 and Linux x64. Those controls
caught Darwin accepting a same-directory exclusive rename; the native Publish
path now rejects occupied destinations before its kernel-exclusive rename.
`6a88590b3` retires the 53-case private build-host harness, its authored fixtures,
selectors, implicit test seeds and test-driver dependency on private host objects.
Graph/driver, native-runner, CI orchestration and C syntax checks pass. Explicit
VM comparison and recovery remain, and `policy_worker` remains a graph fixture.

The combined compiler/core source at `813b93875` reproduces between macOS
stages three and four, SHA-256
`74af3f632114846c54327550627450ac013da8a8c7befc58ac065d753232af75`.
The older builder's stage two differs; stage times are 198.04, 181.34 and
206.10 seconds. Its stage-three compiler passes 445 tests in 25.66 seconds.
Managed pointer/record and task-root checks pass in 49.17 seconds, patterns in
13.83, arrays in 11.20, poisoned owned-value checks in 20.98, and the family
and reload suite in 36.75. These are matching-source macOS results; broad
product/platform acceptance remains open.

This composition includes execution-family heaps and code leases, flat managed
record cells, canonical array places and insert/reverse/replacement, lexical
cleanup and union condition bindings, nested math writes, and linear inline
record-layout validation. The later `3e038edf1` composition wires shared record
cells to their image counters and tests cyclic task roots across reloads.
Owned values across suspension and deep retained snapshots are still in progress.

The verified compiler permits retirement of the obsolete C compiler census,
its VM transport, eleven embedded-source templates and sixty-six source-byte
substitutions. The historical census report remains. `8b5258e3f` removes the
unused VM-host download/publishing preparation routes, and `93c881a24` runs
map-receiver coverage through the native compiler.

CI run `41557074074` uses the same tree as published `4d23cceee`. Linux stages
three and four match at `eb84095352e74dfde1851a9a0d099e7215ac5088219cc79e3b993ec77cdf6077`.
Its core Linux/macOS jobs failed on a missing embedded optional-source declaration,
now corrected and then superseded by census retirement. The native proof later
failed on an outdated imported-attribute diagnostic expectation, corrected in
`3d727ed53` with a reproduced failure and passing full attribute check. The two
platform module jobs received only cross-runners because the failed core build
never produced their manifests; the artifact paths are correct. These failures
do not constitute product acceptance. Windows self-host/product validation is
still being followed. Its Windows stages three and four do match at
`a0b1cc5ec867c125e2b3315d3486c4ef72e6f85b1e87bfede70aff4c74820cfd`
(114 and 113 seconds). The Windows asset consumer then failed SDK-relative
service discovery with an unspecified program, although explicit-service and
authored verifier checks passed. The authored facade replacement must pass that
same relocation case. The later checkout-product step was cancelled by the next
push; the job is not platform acceptance. CI `41563501452` is testing `d4451f6be`.

`1a98be6cd` fixes a Linux heap overwrite in foreground process supervision.
glibc writes a 152-byte signal action into buffers previously sized at 128
bytes. The three buffers now reserve 256 bytes. The unchanged-source control
aborts on Linux; the fix passes repeated scopes and signal restoration on
Linux and macOS, plus the existing foreground/terminal suite.

`85b37934c` closes reordered callback arguments, including mixed named and
positional calls. Arguments evaluate in written order; the compiler rearranges
existing operand slots without runtime copies. Its matching compiler builds in
201.78 seconds and passes 442 compiler tests in 22.82 seconds. All six argument
permutations, mixed types and string replacement pass at O0/O2 and through the
ordinary compiler. The producer suite passes 152 corpus cases, 169 existing
malformed cases and nine new operand-exchange rejection cases in 65.80 seconds.
Independent review's positional-ordinal finding is fixed and closed.

The composition also includes lazy primitive coalescing, optional field reads,
generic bool constants, empty switch arms, array insert/reverse backend operations,
streaming SHA-256 and the pure cache format. The preceding composed compiler
passes managed pointer/task, poisoned managed-value and optional/fallible suites.
`456a1dc32` adds typed collector layouts and per-image cell counters while retaining
the 64-byte cell and 56-byte heap. `040c44bb2` adds Wasm compile-call options and
resource cleanup, with actual Node quota/cancellation/normal/failure checks.
Shared-family unload and aggregate source admission remain separate work.

The authored workload benchmark now preserves grouped and detached-descendant
cleanup through a private Linux subreaper worker. Its canonical Linux run passes
all supervision controls and produces the original eight measurement rows in
53.87 seconds. `ab1f3c06b` removes the C implementation and switches its selector
atomically. Desktop and browser compile/cache facades pass their existing ABI
consumers without first-party C implementation code; archive/SDK integration and
fresh full-source browser emission remain in progress.

CI run `41539102677` at `c5c0a1ac2` reproduces Linux stages three and four
byte for byte (`1d1d289a2429fe2ee2a75a71a959bf21590a02ff3ebf7cd8af1037a11bf1268e`).
Its Windows cross-build and actual Windows stages three and four also match
(`b069704224c389f793136e2e5a1190a5c7e83c9d1a4bbede7c91e97f4ea2b9c2`).
Downloaded Linux and Windows archives match GitHub's artifact digests and the
compiler bytes match their producer logs. This is compiler reproduction evidence,
not a green combined run: the older source fails a deleted C helper, Graph's
VM environment allowlist, Windows SDK-relative asset-service discovery and an
untyped arithmetic comparison. The helper and Graph fixes are integrated;
`0fe312983` fixes the comparison, including the original checkout smoke program.
The desktop facade replacement owns Windows service discovery.

The batch includes POSIX process-group and terminal supervision, its watch
adapter, waiting scalar foreign calls, native core/portable selectors, source
inventory receipts and the Windows PowerShell 5.1 bootstrap. The original
fallible-result checker now also runs on native source; its stricter coverage
found unchecked cleanup errors in the native driver. The callers now consume
those errors, and a missing test-output file can no longer produce a false
PASS. `3736ee5b5` preserves false optional-bool payloads after a presence guard;
the seven original cases and parenthesized variants pass at O0/O2.

The compiler/core snapshot at `9306b2fe0` builds through the `776ead2f1`-derived
intermediate compiler in 110.63 seconds. Stages two, three and four are byte-identical,
SHA-256 `40d00b84123ae44eb8bf5453747276666338b3f298a717f42c3b1acba5c1db58`.
The original `60901f2fa` pin rejects the newer combined source, so advancing the
verified seed is required before the next normal checkout build. The existing
`60901f2fa` → `776ead2f1` proof below is intermediate upgrade evidence; the exact
`c5c0a1ac2` source now also reproduces on macOS: stages three and four match
`0a19d74d70c5f406bb08768a9167ab099e3cb6b034c7454d225f6142149b3cfc`.
Its 429 compiler tests pass on Linux (40.58 seconds in Docker) and macOS
(13.25 seconds). The owner approved publication of these exact Linux/macOS
assets, corresponding source and two pins. The
[native seed release](https://github.com/waldnercharles/chibi/releases/tag/native-seed-c5c0a1ac2e065b5e57feec0de6dd453c12fa267d)
is published. Its tag resolves to that commit; all 19 published files were
downloaded into a new directory and checked against the reviewed package and
checksums. `7ab1c84ac` advances the two pins. Windows remains unpinned.
The published macOS compiler builds the composed `143655b19` compiler directly
in 105.07 seconds; its 436 compiler tests pass in 24.08 seconds. The normal
checkout build entry also fetches and uses the new pin successfully.
Local Linux self-build attempts were killed by Docker's memory limit, including
one worker (`OOMKilled=true`); other running services were left untouched.

The earlier composed `2ed86e078` compiler reproduces between stages two and
three: `d5c343e4f68b6ba02c47c320be0ef8df337b5565b22702c8b0729be7753f98d3`.
Those stages take 109.40 and 69.30 seconds. Its 436 compiler tests, nullable
managed-pointer tests and nested-array tests pass. Its generation suite exposes
a retained-record scoped-reference reentry regression (exit 2 instead of 3).
The cause was stale publication of a readonly reference after a nested call
updates retained state. `4d23cceee` integrates the fix, preserving parameter
write classification and testing both alias orders. Its matching generation
suites pass as recorded above; broader runtime migration remains incomplete.

The next composition includes primitive optional payload reads, source-ordered
named text calls, persistent collector roots, and the VM decoder ownership split.
At `143655b19`, optional/fallible tests plus ordinary comparisons pass in 28.68
seconds, text arguments in 8.62 seconds and the managed-pointer suite in 28.54
seconds. The obsolete C CLI, orphan host test utilities, Wasm C wrapper and
unused subprocess benchmark are deleted. The shared Windows generation now
cross-links a real watch package, and CI includes actual public watch execution;
Windows runtime acceptance remains pending.

All seventeen CLI survivor cases pass through the public native compiler,
together with four parent-signal and six terminal cases (43.02 seconds).
Their obsolete C harness is deleted in `a501e0467`. The six backend survivors
pass on macOS (36.01 seconds) and Linux (151.40 seconds), including exact bounds
diagnostics and child-process cleanup. `d2359070d` deletes their C harness and
sole stack-overflow handler and switches the existing selector to the native suite.
The merged selector's dry run resolves the new command.

Fresh SDK installation/relocation and its rejection checks pass in 18.70 seconds.
`05bb540e1` compares authored compiler/core files without requiring source-free
build-directory shapes and rejects directory/symlink substitutions for excluded
files. This corrects an earlier false stale-resource rejection; the authored
files in that fixture were identical. `034988efb` shares one correctly typed
POSIX clock binding; all 25 build cases pass in 21.91 seconds and O0/O2 ownership
checks pass in 17.63 seconds, including 1,025 clock samples without allocation.

`0c0c9bb0d` and `0fe312983` preserve floating literal promotion and infer the
spelled type of deferred constant-expression comparison operands. The expanded
fixture passes O0/O2 and the installed-CLI arithmetic smoke compiles and runs.
All 432 compiler tests pass in 12.98 seconds. The full types category retains
four then-known admission gaps (payload patterns, set_array_elem, nullable
pointers and optional descriptor growth); its arithmetic/shift/range diagnostics
pass. Nullable pointers and primitive optional descriptor growth are now covered
by the newer composition above; the other two remain in their implementation lanes.

`c50650f50` integrates scoped-reference alias preservation, native scalar managed
cells and pointer arrays. Its source/runtime proof passes in 26.66 seconds:
normal and forced collection, poisoned frees, bounded peak allocation, zero
final live allocations, malformed descriptors, cross-owner rejection and shared
array tracing once per collection epoch. Nullable pointers and retained primitive
arrays are integrated. Standalone suspended collector roots and failure cleanup
are now covered. Aggregate pointers, shared execution-family heaps and deep retained
snapshots remain active work. Each new
composition still needs its matching runtime and platform checks.

Remaining ownership is explicit:

- CLI lane: Windows public watch acceptance and the live compile-only asset
  facade. Replace desktop/browser C orchestration, cache and generated identity
  C with authored code and LLVM ABI adapters while preserving diagnostics,
  quotas, cancellation, thread isolation and caller-owned outputs.
- Producer and collector lanes: remaining array operations, aggregate pointers
  and precise ownership roots; reload lane: deep retained snapshots and schemas.
- Driver lane: remaining authored language samples and error-result handling.
- Tasks lane: shared execution-family ownership and generation code lifetime;
  the browser facade already uses LLVM, and its final authored compile/cache
  integration shares the CLI lane's request and resource contracts.
- Cutover lane: final host/VM consumer inventory and proven obsolete target deletion.
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


## Native integration checkpoint: 41ff027d9

The native compiler now supports owned task frames across suspension, plain
optional managed cells, primitive views in authored unions, retained nested
array mutation, array-backed record map values and generic owned array map
payloads. String-array managed cells use the existing typed array copy/drop
helpers. Inline record fields preserve their canonical place through foreign
buffer calls. The Linux logical-place failure path now keeps program ownership
separate from an armed instruction-budget scope.

The exact compiler source at `16ccee98e` reproduces identical macOS stages two,
three and four, SHA-256
`07a5532a09e669a0cda04029343375053687274f8a6bc407215e7e1e9e6706af`.
Builds took 20.56, 21.61 and 24.23 seconds under shared load. All 463 compiler
tests passed in 22.93 seconds. Its combined managed pointer/view source,
collector and boundary proofs passed in 89.57 seconds; owned task lifetime
proofs passed in 33.41 seconds. Full generation, retained snapshot/source and
image-unload proofs passed at O0 in 51.64 seconds and O2 in 52.18 seconds.
Evidence is in `/private/tmp/chibi-16cc-proof`.

The subsequent `41ff027d9` compiler builds in 19.45 seconds and passes all 466
compiler tests in 21.93 seconds (`/private/tmp/chibi-41ff-proof`). This includes
inline record foreign places and the Windows linker mapping of explicit
`system:c` imports to UCRT. Windows SDK-relative compiler discovery resolves the
loader's original executable spelling before adding parent segments; the C
consumer now runs with an explicit extended-length path. Actual Windows
execution of these fixes remains required. The preceding hosted run
`41574174752` failed SDK-relative discovery, the Host's unresolved `memchr`,
clock source path handling, public watch readiness and the O0 math edge case.
The clock path is fixed; math now reports the actual signed and hexadecimal
exit status. No failing assertion or product check was removed.

The target jobs' missing program manifests were downstream of Linux's graph-os
failure: that stopped before the projects/language selectors that publish the
native-build/native-link manifests. The exit trap still published arithmetic
modules. The graph-os fixture's ignored fallible result is corrected. Successful
acceptance must produce and execute all existing module and program lists.

Remaining work includes owned task results, deeper projected collection places,
string/composite views, deep composite comparison/removal and exact ordinary
record-array resize failures. A stress test has also reproduced collection
reentry during pointer-array backing allocation; its root/lifetime fix is a
required retirement gate. The final C/VM/recovery deletion candidate stays held
until these behaviors and supported-platform products pass. Driver and Graph
command equivalence must survive removal of their obsolete VM oracle through
independent expected fixture data, including complete command inventories.
Neither deletion of that comparison nor the current fixed point completes the
full self-hosting goal.
