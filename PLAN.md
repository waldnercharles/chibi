# Native toolchain continuation

The November 2 handoff continues in PR #1331 on `codex/native-default-20261102`.
The combined checkpoint includes CI merge `a76ea8005` and compiler/core source
`1d46f11a8`; its exact-revision platform CI is pending. Further work is composed
on `codex/native-runtime-completion-20261102`. The owner's checkout and
unrelated work remain untouched; existing compiler worktrees supplied the
independent driver, CLI, producer, maps, tasks, Windows and reload lanes.

## Current checkpoint, 2 November

At published `1aaf0ef3e`, no-C-compiler, compiler self-build, Linux native
self-host, build-alone and SDK relocation CI pass. Windows stages three and
four reproduce exactly with SHA-256
`f291fa77d17b5ce34d9dbc6171bbbfd3f6ca47d07c8ba9912ecfbe79ea0f5459`.
The original long Unicode-path asset compiler build also succeeds. Its next
step rejects unnamed C stub parameters under strict C11; that is fixed in
the new batch at `1a3dff070`, awaiting another Windows execution.

General CI at that published revision is not green: it exposes native
language admission, CLI diagnostics/reload and old host-test assumptions.
The complete failing selector log is retained at
`/tmp/chibi-engine-1aaf-failed.log`. These failures are migration work, not
grounds to restore automatic fallback or omit surviving behavior checks.

The combined compiler/core source at `1d46f11a8` passes all 425 compiler tests
in 21.26 seconds, the complete native CLI suite in 52.31 seconds, map ownership
and poisoned-allocation checks in 41.11 seconds, generation/reload checks in
13.06 seconds and native cache/publication checks in 6.44 seconds. Two self-builds
take 56.63 and 56.16 seconds and produce identical SHA-256
`06af8f15dca5e9c0df2e5bcc6afe2d86d45bd72c6e6515d0428c0e6f98cc9610`.
The actual Wasm compiler service, including quotas, cancellation, concurrent
pthread requests and allocation balance, passes in 168.11 seconds from an
immutable snapshot of that source. These concurrent timings are acceptance
evidence, not controlled performance comparisons.

The VM-backed AOT driver is deleted at `44ed8dbc6`. Its surviving cache and
publication cases use the native driver. All 25 native build cases pass in
26.38 seconds, preserving their original worker deadline and all three Mac/six
Windows build-program rows. Three Linux-only reload cases also execute in Linux
Docker. Native-link object/archive and Unicode command-line cases pass on Mac
(3.55 seconds) and Linux. The combined build/link bundle retains five Mac/eight
Windows program cases; all five Mac cases execute successfully. The Linux local
link/reload checks use the previously built `5065cd09d` Linux compiler with the
current test sources; full exact-revision platform validation remains required.

The single-workflow CI reconciliation at `fbf1ee054` is composed with the new
retirement selectors and Windows checks. Its 34 orchestration tests pass in
14.49 seconds, the composed native graph-driver suite passes in 14.78 seconds,
and actionlint passes with shellcheck disabled. Both VM/native
command comparisons remain until their replacement evidence permits deletion.
The native compiler proof retains all 19 previous checks and adds character/
UTF-8 coverage. Linux build timing and C graph checks remain in the `native`
group rather than being repeated individually.

The installed native CLI/SDK and optional LLVM Wasm compiler SDK are implemented;
`docs/evidence/native-wasm-browser-20261102.md` records the earlier browser lane
artifact identities and limits. Windows checkout, DLL isolation, UTF-8 and foreign
buffer checks are wired into the combined CI and still need actual Windows
execution at the new revision. Pending compiler work includes generic aggregate
lifetimes, owned pointers and collection, math admission, process supervision,
and the authored replacement for the C target runner.

The evidence below is chronological. Older counts and gates describe their
named revisions, not the latest integration. Remaining deletion gates are:

- Finish native language and ownership parity, including live pointer forms,
  array-backed records/maps and managed suspended/retained values.
- Complete the 17 authored CLI survivor cases and three live Edit-and-Continue
  behaviors from `docs/c-test-verdicts.md`; port surviving scalar/C test cases.
- Migrate old host-based build/link tests and remaining live graph consumers.
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


## Browser formatter, 2026-11-02

The formatter playground will use the existing native LLVM Wasm target and the
same authored formatter, parser and highlighter. This lane is isolated from the
active self-hosting and performance checkouts at frozen parent `db6b9f6c1`.

Discovery checked local worktrees, branches and active chat snapshots. The
`native-asset-compiler` lane already implements and has browser evidence for
`wasm32-unknown-emscripten`; the earlier claim that this target was absent was
outdated. Historical VM browser work is not needed.

Changes:
- Restore `tools/compiler/source_format` and `source_highlight` from the retained
  formatter revision, using the current parser, core and collection imports.
- Extract the existing Wasm request/runtime implementation into
  `src/runtime/program_wasm.*`, retaining asset compiler API behavior. Both real
  callers share allocation, quota, cancellation and failure recovery.
- Add `tools/docs/playground/wasm/Main.chibi` and a small C/JavaScript boundary.
  Input is UTF-8 options plus source; output includes formatting and highlighting.
- Replace local APIs with a worker and relative static files. Requests coalesce,
  stale responses are ignored and runtime failure/timeout allows retry.
- Add a Chibi build program that reuses the existing Emscripten toolchain checks,
  compiles the formatter, packages repository examples and publishes a new output
  directory atomically. No deployment is part of this task.

Verification:
- Keep the existing native formatter/highlighter regression cases.
- Run the real Wasm shared-runtime and public asset compiler regressions.
- Compare exact browser output with the local formatter across options, Unicode,
  comments, invalid drafts and repeated requests.
- Verify the UI on a static-only server under a URL prefix with no formatting API.
- Check worker failure/retry, rapid edits and consistent generated output.

State remains owned by the page and each worker; no source is uploaded. The
existing local playground remains available during implementation.

## Formatter language and semantic styles, 2026-11-02

The user authorized the language and compiler support needed for optional
control-body braces and explicit/inferred type preferences. Continue in the
isolated formatter worktree, preserving the earlier browser implementation.
The current compiler/parser remains the source of truth for grammar and types.
The user also requested research into inserting blank lines between logical
sections and assessing readability; document evidence before selecting a model.

Changes:
- `source_parser/Statements.chibi`: parse one unbraced statement after `if`,
  `else` and `while`, wrapped in the existing Block node so scope and lowering
  remain shared. A dangling `else` binds to the nearest `if`; explicit empty
  bodies remain invalid.
- `source_format`: preserve and indent unbraced bodies, add opt-in brace
  placement/presence controls, and validate brace edits with normalized control
  trees while retaining scopes, comments and statement order.
- Compiler analysis and `source_type_style`: reuse declaration, specialization
  and preparation passes to expose resolved binding types without code emission.
  Validate type-style rewrites against the program's semantic representation;
  keep numeric context and report unsupported/unresolved sites explicitly.
- Playground options/entry: expose supported style controls, keep semantic
  analysis out of ordinary layout requests, refresh highlighting after token
  rewrites, and display analysis limitations. Build the same code for native
  tests and the browser worker. Source remains local to the browser.
- `docs/formatter-readability-research.md`: primary-source research on logical
  section detection, existing tools, readable-code classifiers and a deterministic
  opt-in approach suitable for the playground.

Verification:
- Parser shapes/rejections, nearest-if association, scoped declarations and real
  native execution at O0/O2 for unbraced branches and loops.
- Formatter expected outputs and idempotence across brace policies, comments,
  nested controls, semicolon policies, tabs and CRLF.
- Actual compiler analysis for inferred/explicit bindings, narrow numeric types,
  constants, aliases, call choices and unresolved imports. Recompilation alone
  is insufficient evidence for a semantics-preserving rewrite.
- Native formatter/highlighter suites, protocol/client tests and real Wasm style
  requests with recovery after rejected input.
- Rebuild the static playground and exercise the new options in the browser.

Defaults preserve source type and brace spelling. At this first checkpoint,
compiler analysis ran only for the opted-in type path and logical-section
heuristics remained a proposal. The expanded implementation below supersedes
those limits. No external model or source upload is introduced.

First brace/type checkpoint, implemented and locally accepted:
- Optional single-statement `if`/`else`/`while` bodies preserve Block scopes and
  nearest-if association. The tokenizer recognizes inline typed constant bodies.
- `brace_placement`, `branch_braces` and `binding_types` are available in the
  playground. Unsafe omissions retain braces. Typed rewrites preserve protected
  regions and report skipped or unresolved sites in a visible notes panel.
- `AnalyzeSources` and `SameSemantics` use the compiler's prepared representation.
  `RewriteProject` accepted dependency documents; that browser checkpoint supplied
  one document. Parameter/result signatures then remained as written. Generated
  lambda/task identities can conservatively prevent a type rewrite.
- Actual optimized-driver checks exposed ignored OS errors in the frozen native
  driver. Reported failures are now checked in cache, source discovery and test
  output handling. The existing core OS unreadable-directory reporting issue is
  outside this change and is not claimed fixed.
- All 502 authored compiler tests passed, including 47 formatter and 19 semantic
  tests. The compiler run took about 49.39 seconds from log creation/final-write
  timestamps. The separate 19-test semantic run took 21.42 seconds. Seven real
  control-body checks passed at O0 in 0.72 seconds and O2 in 0.93 seconds.
- Ten JavaScript protocol/client checks and ten real Wasm checks passed. Wasm
  checks took 1.47 seconds. Browser acceptance covered combined styles, Unicode,
  exact protected text, invalid-source correction, skipped-type notes and the
  production URL prefix. No browser console errors were observed.
- The final coherent static build took 103.84 seconds. It contains the same
  3,705,668-byte Wasm module accepted above, SHA-256
  `a6bfe3e7813a0ce78d6d9a35a33a2ad808fe7082103bf632262886713987df84`.
  It is served at `http://127.0.0.1:8766/chibi/formatter/`. The previous site is
  retained at `.chibi/formatter-before-styles-1793662987`; the live path points
  to `.chibi/formatter-styles-final`. Receipt: `.chibi/formatter-styles-acceptance.json`.
- Readability research is in `docs/formatter-readability-research.md`, with direct
  blank-line segmentation precedent and the original deterministic paragraph
  design. Automatic paragraphs were not implemented at that checkpoint.

Changes remain local and uncommitted; GitHub Pages has not been deployed.

## Expanded formatter catalog and logical sections, 2026-11-02

At the checkpoint before the SEGMENT replacement, the user requested the broader
survey families and a working logical-section formatter. That source defined 174 settings in one canonical catalog,
`tools/compiler/source_format/options.json`. Generated native definitions,
validation/configuration parsing and browser controls share that catalog.
`docs/formatter-implementation.md` maps every survey family to concrete options
and records the unsupported variants.

Implemented in the current source:
- Independent foreign/when indentation; construct brace/empty/short-body
  policies; parameter/result/attribute wrapping; list packing and widths;
  binary/chain wrapping; syntax minimum gaps; comment-group gaps; per-construct
  block padding and independent unbraced-body placement.
- Optional `fn` and return-arrow spelling and checked block/`=>` function forms.
  Contextual type preferences include parameters, results, members, public APIs,
  evident types, alias spelling and four compiler analysis targets. Required
  annotations, unresolved sites and unsuitable recursive/suspending results stay
  with notes. Generic templates are not specialized from individual calls.
- Operator-family and syntax-context spacing; bounded consecutive/manual
  alignment; array/record table columns and per-column left/right preferences.
- Parentheses including mixed same-precedence operator groups, per-list trailing
  commas, comment prose/doc reflow, numeric separators and casing, exact checked
  integer base conversion and string/character escapes.
- Import sort/organize candidates with conservative unused-alias detection.
  Every import reorder or removal requires compiler semantic proof. Removal
  retains implicit namespaces, side-effect imports and uncertain, commented or
  conditional candidates.
- `statement_paragraphs=preserve|syntax|logical` and three density choices.
  The working logical mode uses bounded lexical/structural summaries and dynamic
  programming over at most 12 predecessor units. It preserves authored anchors,
  recognizes receiver runs, keeps producer/check pairs together and separates
  the tested validation/computation/output example. It does not use a classifier
  or claim resolved dataflow or measured comprehension improvements.
- Versioned profiles, explicit overrides, searchable UI groups, project-context
  source loading and declaration/statement range formatting. EditorConfig
  import/export supports common properties and all `chibi_*` overrides, matching
  sections, explicit atomic Apply and diagnostics. Parent-config discovery is
  outside this single-file interchange.
- Public formatting defaults to `semantic_targets=all`. The chosen
  `analysis_target` determines initial analysis/type spelling, then
  compiler-backed edits require proof on Linux, macOS, Windows and WebAssembly.
  Failed proofs keep the affected original source with notes; layout remains
  available. `semantic_targets=selected` explicitly opts into one-target proof.
  Lower-level `source_type_style` rewrite APIs retain their selected-target
  contract and are not equivalent to the public portable-proof pipeline.

Current validation:
- The broad authored compiler checkpoint passed 718 of 718 tests in 173.26 seconds,
  including 222 formatter cases, 51 semantic-style cases, 49 parser cases and
  15 tokenizer cases. Formatter coverage includes 12 private and 210 public
  cases, all 11 logical-paragraph and 12 import-candidate cases, and five new
  all-platform/selected-target portability fixtures. Log:
  `.chibi/formatter-compiler-final-void-tests.log`.
- A Wasm case exposed conservative mixed-void function-body handling.
  Prepared-result queries now select eligible void bodies before batch proof.
  The updated focused formatter suite passed 222 of 222 cases in 56.45 seconds;
  semantic-style tests passed 51 of 51. Logs are
  `.chibi/full-pipeline-mixed-void-tests.log` and `.chibi/void-offsets-style-tests.log`.
- A final comment fixed-point case exposed reflow measuring indentation before
  layout. Comment reflow now runs after final indentation and before selection.
  The current public formatter directory passed 211 of 211 tests in 31.10 seconds,
  including the regression. Log: `.chibi/formatter-comment-final-indent-tests.log`.
  This is 211 public cases, while the earlier 222 included 210 public plus 12
  private function-style cases. The current source inventory is 223 formatter
  tests; those 12 private cases and the global 718-test suite were not rerun
  after this localized ordering change.
- Current fresh-driver native execution passed six function-form cases per
  optimization level at O0/O2 in 1.62 seconds combined, two numeric-literal cases
  per level in 1.40 seconds and seven control-body cases per level in 1.20 seconds.
  The preceding checkpoint also passed one grouped-record return case per level
  and optional/managed ownership regressions in 31.88 seconds.
- The current producer receipt verifies 87 input hashes unchanged across the
  build, including the new prepared void-call query. The 6,682,640-byte
  `.chibi/formatter-final-void-driver` built in 57.56 seconds and is SHA-256
  `ca55770532ba76ce037809149e04e7e908afd477c3245ea8473c22a2ef7c5b27`.
- The exact Host project-context probe omitted four safe annotations, kept 25
  required/contextual sites and reported no analysis error. A separate native
  paragraph parser/helper probe over 5,000 declarations completed in 0.22 seconds;
  that excludes compiler proof and is not browser latency.
- All 44 JavaScript configuration/context/catalog/client tests passed in
  0.094 seconds. Prior browser UI QA covered EditorConfig import and validation
  errors without applying invalid settings. Log:
  `.chibi/formatter-javascript-reflow-final.log`.
- The final reflow-fixed coherent Wasm artifact passed all 24 checks in
  1.497 seconds, including the width-40 indented-comment fixed point.
  `.chibi/formatter-full-final-reflow` built in 196.89 seconds and contains
  a 4,389,641-byte module, SHA-256
  `d0781fa04e3be993dd8c99656fcf4ee69b1b2f4b6300ddd003e466b329172bcd`.
  Input digest:
  `3ceeeb55cca3181e488677ac40e9beac24b2626887d55c5e6cdc0f26fb9adb5b`.
  Shipped assets match the receipt. Log: `.chibi/formatter-wasm-reflow-final.log`.
- Prior browser UI checks used the 174-setting module with all-platform validation by
  default. Bytes applied 18 type-style changes and retained 29 sites with
  `return_types=inherit`; Host applied 23 changes and retained 80 without an
  analysis error. Foreign/when indentation and function keyword/arrow controls
  worked. EditorConfig valid import and invalid atomic import checks passed.
- Editing a tracked file now retains its project path and marks it `, edited`.
  Manual regression verified import resolution and a filename-specific
  EditorConfig section after editing Bytes. Explicit Scratch still clears the
  path.
- The final build has a new local preview at
  `http://127.0.0.1:8768/chibi/formatter/`. Prefixed HTTP assets and Wasm hashes
  were verified. Real HTTP project-context loading and Wasm formatting handled
  edited Bytes with a two-document context, and a second edit reused cached
  context while supplying the current edited source. Receipt:
  `.chibi/formatter-final-http-context.json`.
- Final browser acceptance of the reflow-fixed build combined expression bodies,
  required `fn`, omitted return arrows, inferred bindings, logical paragraphs
  and prose wrapping. All 174 controls rendered and no browser warnings or
  errors were observed. Screenshot: `.chibi/formatter-full-browser.png`.
- The main local URL `http://127.0.0.1:8766/chibi/formatter/` now points to
  `.chibi/formatter-full-final-reflow`. HTTP bytes match the accepted build,
  and a fresh browser tab loaded Bytes successfully. The old site is retained
  at `.chibi/formatter-styles-final`; final receipt:
  `.chibi/formatter-full-acceptance.json`. No GitHub Pages deployment occurred.

Per-boundary explanation UI, learned preference/classifier models and human
readability/comprehension evaluation remain proposals. The coverage matrix also
names smaller absent variants such as shifted braces, independent tuple-layout
controls and arbitrary statement-gap selectors. Region formatter directives
provide the survey's escape hatch; ignore-next is outside this iteration.
Changes remain local and uncommitted; no GitHub Pages deployment is part of this
checkpoint.


## SEGMENT replacement, 2026-11-02

The user requested SEGMENT's algorithm in place of the custom logical-group
optimizer. The replacement is implemented in `source_format/SegmentFacts.chibi`,
`SegmentActions.chibi`, `SegmentSimilarity.chibi` and `SegmentRefine.chibi`, with
formatter boundaries in `Paragraphs.chibi`. It follows the four published phases:
recognize action/SynS units, resolve overlaps, refine uneven similarity windows,
and merge compatible singleton statements. `docs/formatter-segment-algorithm.md`
records the recovered rules and the deterministic Chibi completions where the
original description omits recognition or tie-order details. The original Java
source was not recovered, so this is not claimed to be a byte-identical port.

The old density scores, twelve-predecessor optimizer and eight-name truncation
are removed. The canonical catalog now has 173 settings and four profiles;
`paragraph_density` is removed. Logical groups resolve lexical binding identity
and index complete definitions/uses and identifier words. Analysis uses flat
arrays and sorted intervals, bounded by source references/words rather than
all statement pairs. A few thousand statements should take milliseconds to
hundreds of milliseconds in the native/Wasm formatter, rather than seconds
of graph construction; the final artifact acceptance records actual timings.

Acceptance is complete: 256 native formatter tests passed (32 private and
224 public) in 118.06 seconds while the Wasm build ran concurrently; 25 actual
Wasm tests passed in 1.598 seconds, and 44 JavaScript checks passed in 0.107
seconds. The real-project and 5,000-statement fixed-point Wasm case took 0.163
seconds including two formatting/highlighting passes. The coherent build took
283.89 seconds. Its 4,501,913-byte Wasm SHA-256 is
`e77c5cc933c99ef656c563e917ec8a213798cb060c7be4f3ce7be951a9b913b5`.

The main local URL at port 8766 and the example preview at port 8768 now point
to `.chibi/formatter-segment-final`, with matching HTTP receipts and Wasm hashes.
Browser acceptance selected Logical groups (SEGMENT) in the 173-setting catalog
and showed the two expected inserted lines in published/rule-derived examples,
with no console warnings or errors. Screenshot:
`.chibi/formatter-segment-browser.png`. Receipt:
`.chibi/formatter-segment-acceptance.json`. The preceding reflow build is retained
for rollback. Existing user tabs/drafts were left intact and need a reload for
the new module. No GitHub Pages deployment or commit was requested.
