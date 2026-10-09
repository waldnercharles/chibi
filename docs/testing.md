# Test iteration

## Fast, targeted tests

Use deterministic C pico_unit tests as the primary regression checks. Cover a
specific behavior and its failure cases through the smallest public runtime
interface that exercises it. Advance a synthetic clock for time-dependent
behavior; assert values, ownership, geometry, callback counts and work counts
rather than elapsed-time thresholds. Keep compilation separate from execution.

The goal is for every test to be fast and targeted. When replacing a smoke test,
map each meaningful assertion to its replacement first. Remove duplicate
entrypoints and measurement-only loops from regression commands. Keep unique
pixel, renderer, backend and authored-workflow assertions until a focused test
covers their contract. A short unit result does not satisfy unrelated integration
requirements or clear a PR hold. The broad suites below still need migration.

For iframe changes, after building `test_suite`, use:

```sh
make agent-test AGENT_SELECTOR=--iframe-updates
make agent-test AGENT_SELECTOR=--kit-documents
```

`--iframe-updates` checks blank-document model isolation, same-frame fitted
geometry and callback lifetimes, bound whole-document replacement with
conditional/list views, and layout refresh with exact callback counts and
synthetic animation time. `--kit-documents` covers the kit's document placement,
focus, popup and input contracts. Neither needs a window or GPU rendering.

`make agent-test AGENT_SELECTOR=--ui-tab-list` runs the existing tab-strip input
and layout checks independently of the other control fixtures. It preserves all
10 assertions for selection, focus, disabled tabs, selected-row reveal, wheel
scrolling, layout stability and middle-click close. The reviewed CPU-only
entrypoint completed in 1.743 seconds under the production 60-second supervisor
with Debug ASan+UBSan during admission. Record fresh results on each candidate;
this does not replace authored Studio or visual tab checks.

`make test-fast TEST_ARGS='--studio-shell document-tabs'` runs all 17 authored
tab checks: hover/focus geometry, contextual pinning, keyboard cycling, overflow
selection, unsaved-close safeguards, adjacent selection and the final scene
guard. Independent review confirmed one fixture with fixed frame counts and no
authored file writes. Its initial complete production-supervised run took 9.2
seconds with Debug ASan+UBSan and native Metal on macOS; configure/build time was
separate. It requires the hardware execution profile. Leave capture environment
variables unset for timing. Record fresh candidate results; full Studio suites
remain quarantined, and rendered verification still checks close/title masking.

The duplicate `--studio-panel-lifecycle` capture entrypoint and the optional
1,000-frame blank-document profiling loop have been retired. The authored panel
lifecycle assertions remain in the quarantined Studio suite; historical profiling
captures remain research evidence for their recorded revisions.

## Temporary 60-second policy

The retired frontend experiment's five source inputs are preserved as historical
syntax fixtures in `test/fixtures/language/prototype`. Its earlier parser tests
do not establish production semantic or execution coverage.

The production syntax dispatcher builds independently with `make language-build`.
`make test-language` runs seven finite C syntax/recovery/incremental snapshot cases
and the explicit `.chibi` corpus manifest under one production deadline. Corpus
input is limited to 256 files, 512 KiB per file and 8 MiB total. Independent review
approved first supervised measurement; a syntax pass does not establish semantic
or execution correctness. The dispatcher compiles against public SDK headers;
its syntax cases use allocation support and Tree-sitter's C runtime.
Default inputs are curated engine examples and fixtures. Extracted projects can
be included through semicolon-separated absolute roots in
`CHIBI_LANGUAGE_CORPUS_PROJECTS`, or an absolute
`CHIBI_LANGUAGE_CORPUS_MANIFEST` containing one existing absolute `.chibi` source
path per line. Save the generated `corpus.txt`
with evidence so the exact checked file set remains identifiable.

`python3 tools/test_policy.py tool language-semantic` selects seven independently
reviewed C cases with tiny inline sources and a fixed catalog. They check lexical
resolution, optional component guards, numeric limits, catalog operator dispatch,
transitive effects, binding cycles and invalid-source recovery. There are no files,
child processes, engine frames or GPU work. Debug Clang 23.1.1 with ASan/UBSan
passed 7 cases and 36 assertions in 0.468 seconds on 2026-10-07.

`python3 tools/test_policy.py tool language-execution` selects ten independently
reviewed C cases through public SDK headers. They check VM/native results and
state changes, host calls, owned values, linked functions, malformed instruction
rejection, a loop with a three-step budget, integer bitwise/shift behavior at
all eight signed and unsigned widths, and optional context-aware logging. Native code generation is bounded
to these small functions; there are no files, processes, frames or GPU work.
Debug Clang 23.1.1 with ASan/UBSan passed 10 cases and 726 assertions in 0.577
seconds on 2026-10-07, including actual VM and SLJIT execution.

`python3 tools/test_policy.py tool language-program` selects twelve independently
reviewed source/runtime cases on VM and SLJIT. Besides typed events, systems,
bindings, imports, cancellation and failure records, it exercises eager tuple
copies and declaration-scope lazy tuple initialization. Four tiny lazy consumers
check one producer call, sibling initialization on a field write, complete
replacement suppressing the producer, and continuation capture. The managed
payload case uses root/outlined variants, 256 supplied-time pumps and strings up
to 2,624 bytes, checking retained values and bounded live/cached storage. Invocation
budgets remain 1,000 steps. There are no engine frames, processes, sleeps or GPU
work. Debug Clang 23 with ASan/UBSan passed 12 cases and 1,638 assertions in 0.881
seconds on 2026-10-07 (`.chibi/architecture/language-program-fields.log`).

`python3 tools/test_policy.py tool language-navigation` selects eleven reviewed
in-memory cases for shadowing, capture rejection, cross-file renames, escaped
names, event aliases, reflected members, signatures, completion recovery and
owned output lifetimes. It passed 99 assertions in 0.678 seconds with ASan/UBSan.
`language-publication` selects four reviewed VM/SLJIT cases for atomic rejection,
name swaps/removal, pending-work preservation and shared root/import state after
all owners detach, and opt-in source profiling. The profiling case checks actual
entries and resumes, origin retention after removal, disable and reset. All four
cases passed 226 assertions in 0.964 seconds with ASan/UBSan after fixing selection
of the builder's unnamed source sentinel. The later grammar
and authoring integrations require their own affected validation. The expanded
13-case navigation selector now passes 130 assertions in 0.864 seconds with
ASan/UBSan, including exact KDL declaration/reference edits, local event alias
resolution, and hover/highlights over external documents.
The 14-case extension passes 144 assertions in 0.743 seconds with ASan/UBSan.
It adds exact RML event-token renaming while preserving surrounding markup,
comments and unrelated names, plus stale-span and unsafe-token rejection.
The 16-case capture passes 195 assertions in 0.614 seconds with ASan/UBSan.
It covers recovered assignment and prototype contexts, exact library completion
ranges and local shadowing. The 13-case execution capture passes 792 assertions
in 0.479 seconds on VM/SLJIT, including copied synchronous outcome identities,
nonzero outcome selection, result payloads and malformed outcome rejection.
The 13-case program selector now passes 1,689 assertions in 0.884 seconds with
ASan/UBSan on VM and SLJIT. Its new fixed case checks exact signed/unsigned 64-bit
limits, values above 2^53, contextual nominal enums and five out-of-range rejects.

`python3 tools/test_policy.py tool runtime-scene` runs two public SDK consumer
cases with four entities/two scene instances, owned component payloads, deferred
Begin, rollback and cancellation. The normal Debug runtime-only consumer passed
59 assertions in 0.656 seconds. `field-runtime` originally ran three public cases for owned
scalar artifacts, a 3-by-3 whole/sliced bake, geometry/parameter results, invalid
IR and every prefix of a prepared record smaller than 512 bytes. It passed 254
assertions in 0.828 seconds with ASan/UBSan. Neither selector loads a renderer or
runs engine frames.
`prototype-document` selects one independently reviewed case with two small
reflected types and fixed creation-default, detached-component, value snapshot,
partial KDL and malformed-size operations. It has no files, worlds, processes or
GPU work. The standalone consumer uses the public `Chibi::prototype_document`
interface; it passes 21 assertions in 0.928 seconds under ASan/UBSan.
`ui-authoring` selects two independently reviewed public SDK cases with fixed
RML/RCSS snapshots, formatting and attribute-edit reparses, owned text after
producer retirement, last-valid state after malformed RCSS, and exact event token
ranges. It has no files, processes, worlds, or GPU work.
Its independently reviewed fourth case adds typed catalog overrides, revision
replacement invalidating an existing session, new default values and a rejected
wrong parameter type on a fixed 3-by-3 bake. All four cases pass 267 assertions
in 0.650 seconds with ASan/UBSan after the typed catalog extraction.

`--source-presentation` is an independently admitted native selector for two small
CPU cases: dedicated clone ownership after compiler retirement, explicit source
edits, exact-source layout metadata and invalid drafts. Its first snapshot passed
37 assertions in 0.364 seconds. Later document/Studio integrations are unverified.
`--system-schedule` admits only `test_system_schedule`: fixed access/order cases,
62 small CPU engine frames and a serial/parallel 128-entity, 20-frame comparison.
The scheduler benchmark and broad material aggregates remain quarantined. This
selector currently fails: the migrated invalid-source case exposed a NULL program
identity in lowering. The supervised debugger located the failure; its completion
is diagnostic evidence, not a passing test.

`--new-asset` admits one case: `new_asset_create` writes a behavior and a scene
into one temporary content folder, creating the missing folders, and refuses an
existing file, a model named like a shape, an invalid name, a folder blocked
by a file, and a family folder or content folder that is a symbolic link out
of the project. Each refusal's errno tells a bad request from a taken name and
a storage failure. CPU only; the folder is removed afterwards.

After `make build`, `python3 tools/test_policy.py tool validate-project` runs
twelve cases against that `build/bin/chibi_ctl`. Each scaffolds a temporary
project with `new_project` and checks the `validate_project` reply: a new
project is valid; a UI style error is listed under its asset; a start scene
outside the scene assets is parsed and checked for its prototypes; a broken
start scene inside them is reported once; a UI document named by the
manifest or a scene must exist, and markup that is a folder or a link is
refused; a scene's missing prototype or behavior, and each syntax error in a
source, is one error; a project saved by an earlier build is reported, not
upgraded; the application icon must decode as a PNG and a long project name
needs a title; a prototype that includes another validates, runs in a scene
and keeps its include through `--format`, while a missing include is
reported and the included prototype's behaviors stay its own references;
a scene runs as a scenario in a project that has a material; a scenario and
validation accept a real Ogg Vorbis sound, but refuse copies whose headers or
audio packets players cannot decode; and in a project whose manifest names another content
folder, assets are scaffolded, validated, run, formatted, found and reported
there under their `content/` paths, and `status` names that folder even once
the project is too old to open. A few short `chibi_ctl` runs, CPU only; the
project is removed afterwards.

`--source-edit` admits nine independently reviewed cases with fixed small inline
sources and checked edit transactions. They check expression/type validation,
effectful result reuse, lexical capture rejection, statement deletion/reordering,
structural selections, typed values, optional-input reconnection, palette creation,
declaration edits, and comment-box/reroute preservation. The two added cases use
four sources smaller than 200 bytes and twelve fixed edits plus clipboard checks;
the review repairs add one tiny capture counterexample and two tuple selection
requests. No files, runtime frames, processes or GPU work run. The first three-case
snapshot passed one and failed two. Three more admitted cases cover extraction,
inline capture/default handling, signature changes and resolved binding intents
using seven tiny sources and fixed requests. The first five-case integration hit
an invalid-expression parser-node crash; its bounded debugger capture identified
the exact null parent lookup. The guarded producer repair is reviewed and the
nine-case capture is pending. The ninth case uses three small analyses and fresh
projections to check program/path rename and persisted geometry, watches, boxes
and routes. It also checks atomic rejection of a malformed origin table, wrong
expected bytes and a non-name edit. Debugger completion is diagnostic evidence only.

`--source-systems` admits seven isolated pico cases from `test/code_system_runtime.c`
on VM and SLJIT. Fixed supplied-time steps and pumps cover membership, suspension,
spawn cancellation, refreshed component reads, query formatting, reactive bindings,
movement and camera globals. Each fixture has at most 259 entities, and the cases
use no renderer, sleeps or child processes. Its exact dispatcher runs only registry
validation and these cases. The migrated selector is independently reviewed and
admitted for its first supervised capture.

`--source-session` admits only `test_asset_code_checkpoint`: four fixed source
session cases plus the existing KDL checkpoint and external merge checks. Each
temporary project has at most two tiny executable files. It covers invalid
drafts, undo/redo, catalog refresh, retained presentations, navigation, source and
sidecar disk conflicts, and retry after a sidecar write fails. Preview services
are initialized and freed, but no engine step, render, window or process runs.
The corrected migrated selector passes four cases and 71 assertions in 1.483
seconds. A later admitted extension verifies coalescing a continuous graph gesture
into one source/metadata undo entry, stale-text rejection and history boundaries;
that extension awaits its capture.

`python3 tools/test_policy.py tool language-headless` selects two independently
reviewed public SDK consumer cases on VM and SLJIT. Each owns one entity and
scalar component, runs five explicit steps, and checks registration ownership,
Begin, Tick, named payloads, bindings, destruction and removal. Preparation uses
`Chibi::authoring`; its source snapshot is freed before execution. The second case
round-trips prepared bytes, retires the producer module, and frees the bytes before
execution. The consumer compiles with only `Chibi::behavior_runtime`. There are no
files, processes or GPU work. Debug Clang 23.1.1 with ASan/UBSan passed two
cases and 106 assertions in 0.635 seconds on 2026-10-07.

`python3 tools/test_policy.py tool language-prepared` starts the existing C tooling
dispatcher with an exact compiler-test producer path. One producer process exports
three small modules and checks repeatable bytes, then exits. A separate runtime-only
process runs four independently reviewed cases: state/events/bindings/imports,
seven missing or incompatible linkage failures, ten malformed wire mutations,
and atomic package loading with reversed records and three rejection cases.
Both execution backends are checked. Input files are capped at 512 KiB and the
metadata file at 96 bytes. Each child has a 10-second cap within the shared
60-second production deadline; generated inputs and logs are removed by the parent.
The consumer translation unit sees only exported SDK headers and its fixture;
the executable links no parser, compiler, graph or renderer library. The frozen
candidate passed in 0.701 seconds on 2026-10-07: producer one case/31 assertions,
runtime four cases/184 assertions, and parent one case/five assertions. The producer
used ASan/UBSan; the consumer was a normal Debug runtime-only SDK build.

`--semantic-corpora .chibi/architecture/semantic-corpus/projects.txt` analyzes an
explicit exhaustive manifest for each of six unique extracted project catalogs.
Its 87 executable sources share a 256-file and 8 MiB ceiling. Earlier captures
used nine checkout rows and 148 source entries: three original game checkouts
duplicated their extracted copies. Migration verified those copies byte-for-byte
before removing the 61 repeated entries; no unique source was removed. Each declaration scan
has separate limits of 1,024 files, 4,096 directory entries, depth 16, 512 KiB per
file and 8 MiB total; the dispatcher accepts at most 16 projects. This new bounded
check has independent admission and uses one shared production deadline. It
checks semantic analysis; it does not certify executable lowering or game behavior.

`--prepare-corpora .chibi/architecture/semantic-corpus/projects.txt` uses the same
exhaustive project manifests and bounds. It validates complete, unique source
membership and the shared source/byte limits before transferring the owned catalog
snapshot into production collection preparation. Sources are analyzed once;
libraries are prepared in dependency order, then all sources lower and finalize
through the bytecode backend. It retains invalid source diagnostics and verifies
that every valid source owns a finalized module. It creates no game world and
runs no engine frames or GPU work. Independent review admitted the first supervised
measurement; semantic-only analysis remains a separate selector.

`--prepare-corpora-sljit .chibi/architecture/semantic-corpus/projects.txt` uses
the same independently reviewed runner and bounds with the native backend selected.
Every prepared module must report SLJIT. This checks production backend generation
without creating worlds or running events. The exact six-project command is
admitted for its first measurement under one shared 60-second deadline.

`--automation-authoring-readonly` selects the existing code/navigation contracts
with direct file fixture setup and seven rejected retired mutation operations.
Independent review found bounded file/analysis work and a 1,100-caller hierarchy
fixture, with no runtime frames or GPU. It uses a finite cleanup shell command.
It passed in 1.004 seconds on 2026-10-07; retired mutation transport checks are
no longer registered or admitted.

`--authoring-preview-lifecycle` covers the existing live/update/prototype contracts
with small in-memory assets, three CPU engine frames and no renderer callbacks,
project reads, sleeps or child processes. Independent source review admitted its
first supervised measurement, which passed in 1.180 seconds on 2026-10-07. An
independently reviewed extension exercises canonical source registration through
prototype attachment, deferred Begin, typed events, replacement, system/binding
ordering and pending-work cancellation on VM and SLJIT. The expanded selector
passed in 1.628 seconds, including 68 new pico assertions. It does
not cover rendered material updates or Studio interactions.

`--execution-request-lifecycle` selects registry validation and the existing host
wait contracts. Independent review found one 1,024-iteration tiny graph loop and
one three-event scheduler case, with supplied timestamps and no sleeps, files,
processes or GPU work. Assertions cover completion, cancellation, release, stale
requests, retained observations and FIFO resumption. It passed in 0.281 seconds
on 2026-10-07 after the neutral native Spawn adapter. Broader execution suites
remain quarantined.

Managed test executions, including aggregates, have one hard 60-second ceiling.
Known slow and unmeasured commands are quarantined; hardware opt-in cannot clear
it. See [the inventory](test-quarantine.md) for preserved native coverage,
retired script coverage and exact admitted native tuples.

`make test` and `make test-short` run six pure C execution-policy cases through
`tools/test_policy.py`. This default checks execution-policy decisions only.
`make test-tools` runs the complete C tooling group: execution policy,
package/install and configure preservation, room service, publication,
supervisor cleanup/deadlines and validation-planner regressions. Packaging,
publication and room service implementations remain external programs; C tests
own their assertions. Once `make build` has produced `build/reflect`, the
tooling build also runs it against component manifest identity, unchanged and
failed generation, handle pins and JIT identity; without it those cases are
absent, not passed. Select this complete group for affected tooling contracts.
CI runs `make test-tools`; the planner's `--tools` also selects that complete group.
Both modes retain one shared 60-second supervisor deadline. Retired script suites
have not been reproduced completely.

Configure/build separately and report that time separately from test execution:

```sh
make tools-build
make test
make build BUILD_TARGET=test_suite
make test-studio-selectors
make test-list
make test-fast TEST_ARGS=--graph-optimize
```

| Command | Current coverage |
| --- | --- |
| `test`, `test-short` | Six pure C execution-policy unit cases under one deadline. |
| `test-tools` | Complete C tooling and process integration group under one shared deadline. |
| `test-studio-selectors` | Bounded C process checks against a separately built native suite, CPU-only discovery and argument handling. |
| `test-fast` | One exact admitted native argument tuple, with complete existing assertions. |
| `test-list`, `agent-list` | Admission inventory without test execution. |
| `test-full`, `test-slow`, `test-render`, `test-compiler-parity` | Quarantined, disabled and not passed. |

Timeout is failure. Nested managed wrappers inherit the deadline; no child gets
a fresh aggregate budget. Multi-goal test execution is unadmitted. Direct native
binaries, raw CMake checks and controller captures do not universally enforce
the ceiling; do not use them to bypass it or rebuild slow suites from chunks.
The supervisor verifies cleanup discovery before test work. Linux subreaping
contains escaped sessions; arbitrary already-detached descendants are not proven
contained on macOS. Platform validation must report what actually ran.

For repeated admitted C workloads, `benchmark_trials` accepts three or five trials
and a total 3–60-second timeout including cleanup. It preserves workload output
and reports elapsed time. The Make benchmark aggregate remains quarantined.
Retired Python drivers' generated-C comparisons and report formats were not
migrated individually. Research/review command transcripts, measurements and
hashes are historical records of their revisions, not current reproduction
instructions or passes for this cleanup.

CI remains partial coverage. Report the checks that actually ran. Applicable
server-required checks, independent review, explicit holds and approval rules
remain binding.

## Batch validation (CUT62)

Publication uses independently reviewed validation for the affected contracts.
A change spanning several subsystems can use focused checks when review accounts
for each changed contract and their interactions. A mixed scope label or an
unavailable full suite does not automatically block landing.

1. **Review and choose checks.** Independently inspect the diff, callers, tests
   and dependency/build effects. Record which contracts changed and why the
   selected admitted checks cover them. Include applicable backend, sanitizer,
   load, web/UI and release validation. File extensions and selector names alone
   do not establish coverage. Resolve unknown impact before publication. When
   affected coverage is quarantined, provide a reviewed admitted replacement;
   otherwise that specific coverage gap blocks landing. Unaffected quarantined
   suites remain disabled and not passed, without blocking the change.
2. **Freeze and validate.** Use a clean candidate based on freshly fetched main.
   For a batch, pin each reviewed source and review the combined interactions
   and conflict resolutions. Record the candidate commit/tree, configuration,
   commands, exit codes and logs. Run selected checks, formatting and affected
   builds against that candidate. Use `make web` and web verification for UI
   changes. Preserve assertions and failure cases; previews do not replace them.
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
Full native/render suites remain quarantined under the 60-second policy.

A server-created merge/squash commit may have a different ID. Prove identical
tested tree and unchanged base and source revisions, and satisfy current-head
server requirements. Tree equality does not transfer a required status check or
replace a metadata-dependent check. Plans and component-head results grant no
merge authority.

### Planning helper

```sh
python3 tools/agent.py validation-plan --base origin/main --scope mixed --review-note 'Independent review: affected tooling and runtime contracts covered by selected checks' --tools --selector=--graph-optimize
```

The helper records Git identity and admitted commands; it does not execute them
or certify coverage. Both `focused` and `mixed` require a review note and selected
checks. `unknown` records unresolved scope. Use `--tools` (or
`--tool=native-tools`) for the C tooling group and repeat `--selector=--name` for
admitted CPU selectors. For batches, repeat `--source` and confirm eligibility
only after checking every source's review, holds and approvals.
`--verify <saved-plan>` checks local identity, not remote freshness or execution.
Keep saved plans outside the worktree.

## Historical timing evidence

The original exact native admissions are recorded in
[test quarantine](test-quarantine.md#retained-short-coverage). The former tooling
suite's 24.4-second parallel run and longer native/Studio/render aggregates
belong to their recorded revisions and removed runners. They are not measurements
of the new C group. PR186 retains the original process-cleanup reproduction and
startup-margin evidence; current results must identify the actual reviewed head.

The independently reviewed `text-format` tool selector runs two fixed public native
formatting cases, with 41 assertions covering scalar formats, owned string and
array display, rejected printf modifiers and bounded placeholder indices. It
passes in 0.799 seconds under ASan/UBSan; there is no world, process or GPU work.

`native-values` selects one independently reviewed native SDK case. It checks
canonical ABI lookup, NaN comparisons, integer saturation, nominal enums, vector
conversion and output ownership after input/table retirement. The scoped native
consumer passes 43 assertions in 0.403 seconds, without files, processes or GPU.
`box3d` selects one in-tree check of the `chibi_box3d` package (Box3D v0.1.0,
the copy Cute pins). A box falls onto static ground and comes to rest, then
closest raycasts hit the box, hit the ground and miss. It needs a native tools
build and passes 14 assertions in 0.052 seconds, without files, processes or GPU.
`sky-files` selects four in-tree Radiance `.hdr` cases: flat and run-length
decoding, fourteen malformed files, preparation from mounted content (read
once, with an identity that follows the texels) and import that never replaces
a file. It needs a compiler build, uses one temporary folder and passes 661
assertions in 0.12 seconds under ASan/UBSan with leak checks, without
processes or GPU.
`material-ramps` selects four hidden-app 64x64 cases. Nine full color ramps
fill the first 288 entries of a program's ramp table, so a last ramp's stops
lie past the 4 KB of a uniform block SDL's Vulkan backend exposes. That ramp
must show its color through a 2D draw shader and in the material raster's
scene target. The third case only compiles every program kind with the table,
which cannot catch a binding collision, and the fourth counts the ramp table
among the samplers a raster batch binds, which GLES3 limits to 16. It needs the
desktop build with material graphs (`CHIBI_TOOLS_BUILD_DIR=build/test/tools`)
and passes 32 assertions in 2.5 seconds under ASan/UBSan on Vulkan llvmpipe,
whose worker threads leak 112 bytes, so it runs with `detect_leaks=0`.
`graph-model` selects two independently reviewed public consumer cases for nested
value copies, opaque artifact replacement/destruction and topology mutation
rules. They pass 25 assertions in 0.355 seconds under ASan/UBSan. The consumer
links only graph storage and its declared type/execution metadata dependencies.
The prepared-format-v5 state capture passes three compiler writer cases and 161
assertions, seven separate runtime consumer cases and 396 assertions, and one
process supervisor case with five assertions in 0.791 seconds. Its new state case
checks retained banks across publication and restart, plus retrigger deadlines.
