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

## Run the checks

The engine build compiles the full test drivers, `test_tools` and
`test_language`, with every native, compiler and renderer package. CI runs each
group below on every push to `main` and every pull request into it that changes
more than documentation, and `make build` links `chibi`, `chibi-compile` and
`chibi-player` in the same tree.

```sh
make test-build          # chibi_ctl and both drivers in build/ (ASan and UBSan by default)
make test-native         # every CPU test_tools selector with checked-in inputs, one 60-second deadline
make test-language-all   # every test_language selector, one 60-second deadline
make test-tools          # tooling and process integrations, separate build-tools/
make agent-test AGENT_SELECTOR=--source-edit
```

`make test-native` also builds an unsanitized Release `chibi_ctl` in
`build-release/` for `validate-project`, which starts `chibi_ctl` dozens of
times; each start builds the authoring catalog, and under ASan those starts
alone take longer than the deadline.

`python3 tools/test_policy.py tool <name>` runs one entry of that table against
the drivers `make test-build` produced and that Release `chibi_ctl`; set
`CHIBI_TOOLS_BUILD_DIR`, `CHIBI_LANGUAGE_BUILD_DIR` or `CHIBI_CTL` to use another
build.

`make language-build` builds a standalone, faster `test_language` in
`build-language/` for the `test-language*` targets. Add a case to the selector
that owns its area; add a selector to `GROUPS` in `tools/test_policy.py` so the
group, and therefore CI, runs it. Studio's checks live in `chibi-studio`.
`radiance`, `radiance-depth`, `radiance-surface`, `radiance-horizons4`,
`radiance-horizons8`, `material-pixels`, `material-sampling`, `scene2d`,
`scene3d`, `scene3d-gles`, `scene3d-gi`, `scene3d-gi-gles` and `material-ramps`
render through a hidden window, which needs a display and a GPU driver the CI
job does not set up, so they stay out of the group; run them with
`python3 tools/test_policy.py tool <name>`.

CI runs the build, both groups and `make test-tools` on Linux and macOS. On
Windows it builds the SDK tools, compiles `test/fixtures/studio` under a
one-minute step timeout (the test supervisor needs POSIX signals) and builds
Studio against the installed SDK, without running tests. The Windows jobs are
advisory until they first pass.

## Feature coverage

`test/fixtures/feature-coverage` is the all-features project: one scene per
area (2D and 3D physics, tilemaps, UI, particles, audio, save, rhythm,
materials, input and entities). `test_tools --feature-coverage <project>` compares the engine
catalog (components, native functions and events) with what the project's
scripts, prototypes and scenes use, prints each gap, and runs every scene for
four frames through `chibi_ctl`'s `run_scenario`, failing on any behavior
failure. Every prototype, behavior, library and system must be reachable from a
scene: placed or attached there, spawned or called from source that runs, and
only that source counts. A spawn reaches the prototype it names directly or
through a value, such as a local, state or library parameter that holds the
prototype's name, as find references does. A scene entity with its own behavior list
runs that list in place of its prototype's; otherwise it and a spawned
prototype run the prototype's list. A prototype's children spawn with each of
its instances, so they are reached and run as instances too. Components count on these placed and
spawned instances, after the scene's overrides, and where running source reads,
writes or adds them, directly or through a generic call such as
`add_component("GAME.Heat")`; a system query alone matches nothing without
them, so it does not count.
A system runs only while an entity's `EngineSystem` names it. Components the
project declares in `content/components` load as they do in preparation and
count as its own catalog entries. Run it on any project whose content folder is
`content` to list what that project leaves unused; it fails while any of that
is unlisted.

The project's `coverage-gaps.txt` lists the entries no scene uses yet, in groups
under a comment that gives the reason; without the file, every unused entry is a
gap. The check fails when a catalog entry is in neither the scenes nor that
list, when a listed entry has no reason comment above it, and when a listed
entry becomes used. A PR that adds a component, native function or event uses it
in a feature-coverage scene; it lists the entry as a gap only with a reason. The
`native` group, and so CI, runs it on the fixture.

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

`--source-presentation` is an independently admitted native selector for seven small
CPU cases: dedicated clone ownership after compiler retirement, explicit source
edits, exact-source layout metadata, invalid drafts, native palette declarations,
callable-local ownership and readable formulas. Formula coverage retains authored
call origins and effectful operations and reparses expression replacements.
These cases pass 7,971 assertions in 1.259 seconds under the production deadline.
Later document/Studio integrations are unverified.
It also runs two component presentation cases on two fixed in-memory documents:
the engine's tabs, icons and reflected units and ranges, and a document
overriding them key by key.

The four graph selectors compose the application's graph palette (core, shader,
field and native nodes over the game's components) into the registry and edit
in-memory graphs; no files, worlds, processes or GPU work run.
`--graph-document` runs fourteen cases: node comments and comment frames through
graph documents, input and EXEC edge lookups across edits, pastes, remapped IDs,
malformed fan-out and four concurrent first readers, stable node and pin IDs,
reflected Make/Break descriptors, literal defaults and edge replacement. It passes
256 assertions in 0.36 seconds with ASan/UBSan.
`--graph-presentation` runs fourteen cases: which nodes fold inline, arrangement
skipping inline nodes, presentations through asset value documents, expression
text for behavior, component, shader and field nodes, Find in graph and node
hover sentences. It passes 128 assertions in 0.51 seconds with ASan/UBSan.
`--graph-functions` runs eighteen cases: function and macro signature edits,
purity, defaults, moves and variable promotion reaching every call, Collapse to
Macro and Expand Node, and catalog problems of two linked shader libraries. It
passes 170 assertions in 0.50 seconds with ASan/UBSan.
`--vector-nodes` runs one case on the vector kernels: normalize at extremes,
zero scale and the palette's Direction To through a saved graph. It passes 42
assertions in 0.20 seconds with ASan/UBSan.

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

`--asset-refactor` admits one case: renaming a prototype that prototype
children use plans a rewrite of each child in its parent's text, and a parent
that includes another prototype keeps its includes and only its own children.
Four in-memory prototypes, no files, CPU only.

After `make test-build` and `make build-release BUILD_TARGET=chibi_ctl`,
`python3 tools/test_policy.py tool validate-project` runs its cases against
`build-release/bin/chibi_ctl`. Each scaffolds a temporary
project with `new_project` and checks the `validate_project` reply: a new
project is valid; a UI style error is listed under its asset; a start scene
outside the scene assets is parsed and checked for its prototypes; a broken
start scene inside them is reported once; a UI document named by the
manifest or a scene must exist, and markup that is a folder or a link is
refused; a scene's missing prototype or behavior, and each syntax error in a
source, is one error; a project saved by an earlier build is reported, not
upgraded, and one from a newer build needs that build; the application icon must decode as a PNG and a long project name
needs a title; a prototype that includes another validates, runs in a scene
and keeps its include through `--format`, while a missing include is
reported and the included prototype's behaviors stay its own references; a
prototype is found by the name it declares, even when another file bears that
name or the name could not name a file, `new_asset` refuses a declared name,
two declarations of one name are a conflict, and a file the search cannot read,
or one declaring a name longer than the player takes, fails it; a scene runs as
a scenario in a project that has a material; a scene requested by its file name
runs, and reloads, under its own name; a scenario's controls hold the input
map's actions; a scenario and validation accept a real
Ogg Vorbis sound, but refuse copies whose headers or audio packets players
cannot decode; sounds that fit the player's resource count alone, but not with
the project's other resources, fail validation as they fail compiling; and in a
project whose manifest names another content folder, assets are scaffolded,
validated, run, formatted, found and reported there under their `content/`
paths, and `status` names that folder even once the project is too old to open.
In a compiler build, `import_model` names a model for its file, writes files
that validate and that `--format --check` keeps, and refuses a name that is
no asset name or that a prototype already declares, and a model it cannot
read, writing nothing.
A few short `chibi_ctl` runs, CPU only; the project is removed afterwards.

`python3 tools/test_policy.py tool prototype-expansion` runs, the same way, the
two cases whose projects read past the prototype expansion budget that
preparation shares: a project's prototypes, each under its own load's limit,
and a scene's instances of one prototype tree. Each reads tens of thousands of
small prototype documents, so they have their own selector.

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

`python3 tools/test_policy.py tool tilemap` selects eight reviewed C cases for
owned grid decoding and cache invalidation, half-open point lookup, exact rotated
rectangle overlap, transformed bounds and native ECS binding lifetimes. Each
grid has at most four cells and no rendering or engine frames run. A decorative
prototype also checks, without gameplay, that its atlas and its normal and height
maps are prepared and that it owns its prepared data, and a map of another size
must fail preparation.

`python3 tools/test_policy.py tool tilemap-project` admits two project acceptance
cases when the tooling build includes asset authoring. It reads only the sibling
`chibi-celeste` checkout. A bounded local `git show` supplies the former Level
scene from commit `56ca9d486b93c15a504bf293dc722c6202cd2bf5`; the production KDL
parser compares all 1,472 cells and 230 former tile centres/frame indices, then
checks the four retained actors and one Room layer. Inputs are capped at 64 KiB
for the baseline, 16 KiB for Room and 4 KiB for Level. Three owned controller
children validate, prepare and run exactly two headless simulation frames with
five authored and fifteen live entities and no behavior failures. Each child
has the existing ten-second deadline inside the shared sixty-second supervisor.
No GPU or network is used. Logs and the baseline revision are retained under
`/tmp/chibi-publish-validation/evidence`; generated baseline, requests and prepared
artifacts are removed. This fixed workload is independently admitted for its
first supervised measurement; it does not admit arbitrary projects or frames.
The first supervised run passed both cases and 3,818 assertions in 8.270 seconds,
including controller startup, source preparation and the two simulation frames.

`python3 tools/test_policy.py tool webgpu-staging` runs a C-owned disposable
Chrome process against the browser build of the same tooling dispatcher in
`build-webgpu-test`. It draws one 64-by-64 quad for 120 animation frames, checks
staging-buffer reuse and a 64 MiB peak ceiling, then checks that destroying a
pending readback or its app releases the custom-allocator owner immediately. It
restores the default allocator before draining cancelled callbacks. With an
imported device it recreates the app, delivers a controlled old loss continuation,
and verifies that the new app stays healthy. A real validation error must reach
both CF and the page's listener; a real device destruction must mark the backend
lost. Each callback phase has at most one second, clipped to the shared 55-second
startup/execution deadline inside the production 60-second limit.
The isolated Cute source is selected with `CHIBI_TEST_CUTE_SOURCE`; the comparison
build's `CHIBI_TEST_WEBGPU_BASELINE=ON` retains CF's ASYNCIFY initialization.
Callback wall time excludes the first 30 frames and is diagnostic, not a timing
assertion. This small workload does not establish whole-game performance.

`python3 tools/test_policy.py tool source-native-numeric-queries` selects five
finite C cases on VM and SLJIT. Besides numeric outputs and nested entity query
membership, the rectangle case prepares one declared Boolean component and two
contextually typed `EntityCondition` arrays. Ten fixed transform positions check
fractional overlap, strict rectangle edge contact and half-open point inclusion.
It retains the query snapshot while disabling, removing, restoring and destroying
its single member. The case creates no windows, files, subprocesses or engine
frames and uses at most 16 bounded event dispatches per backend. Independent
review admitted the extension; its supervised run passed three cases and 722
assertions in 1.351 seconds. The room-border rules and player spawn remain
project-owned scenario coverage.
The tilemap case executes both tilemap functions on VM and SLJIT using one
two-cell map, then edits its cells and checks the next dispatch sees the change.

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

`--semantic-corpora`, `--prepare-corpora` and `--prepare-corpora-sljit` take
a manifest with one row per project: its content folder, a tab, and a file that
lists every `.chibi` source in it (at most 16 projects, 256 sources and 8 MiB in
total). They analyze, or prepare for bytecode or SLJIT, every listed source
against the engine catalog alone. They are not in a group: no manifest is checked
in, and the repository's fixtures are not complete projects for them, since some
reference events and prototypes they omit or declare their own components. Run
them on extracted game projects and keep the manifest with the evidence.

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
make test-build
make test-native
make test-list
```

| Command | Current coverage |
| --- | --- |
| `test`, `test-short` | Six pure C execution-policy unit cases under one deadline. |
| `test-tools` | Complete C tooling and process integration group under one shared deadline. |
| `test-native` | Every CPU `test_tools` selector with checked-in inputs under one shared deadline. |
| `test-language-all` | Every `test_language` selector under one shared deadline. |
| `agent-test` | One admitted selector routed by `tools/agent.py`. |
| `test-list`, `agent-list` | Admission inventory without test execution. |
| `test-fast`, `test-full`, `test-slow`, `test-render`, `test-compiler-parity` | Retired or quarantined, disabled and not passed. |

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
python3 tools/agent.py validation-plan --base origin/main --scope mixed --review-note 'Independent review: affected tooling and runtime contracts covered by selected checks' --tools --selector=--source-edit
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
`interior-border` draws a circle and a diamond with Cute's interior border into
a hidden 64 x 64 canvas, opaque and translucent, through Cute's instanced and
tiled draw paths, and compares every pixel with an analytic reference. It is a
GPU check that needs the render-enabled tools of the main build: run
`make build BUILD_TARGET=test_tools`, then
`make agent-gpu-test AGENT_SELECTOR=--interior-border`, which runs that build's
`test/tools/test_tools` after the agent's device preflight. Pass the same
`BUILD_DIR` to both to check another build. On lavapipe it passes
9 assertions in 6.1 seconds on a cold shader cache and 0.9 seconds warm. The
tiled case fails when Cute's compute renderer is unavailable. Every SDL_GPU
backend has one, so there a failure means Cute's tile shaders did not load.
`viewport-resize` creates an engine viewport at a 640 x 360 placeholder in a
hidden app. The first requested size must replace the canvas on that draw, and
a later size only once it has been requested for two more draws. Build and run
it like `interior-border` with `AGENT_SELECTOR=--viewport-resize`.
`scene2d` draws 2D scenes through the scene renderer and an engine viewport in a
hidden 64 x 64 app and reads back the app canvas. A scene without `GameHRC2D`
must show its materials unlit, `game_hrc2d_defaults()` lighting must keep them
visible, and the unlit view or removing the settings must stop lighting. A
sprite whose normal map faces a light must be lit more than flat normals, and
mirrored, less, unless a Material turns its edge light off; a map must be loaded
and the size of its image, and relief finite. Build and run it like
`interior-border` with `AGENT_SELECTOR=--scene2d`.
`radiance` runs HRC in a hidden 128 x 128 app and reads it back: an emitter
lighting open space through its material, opaque, finite, bouncing and nearly
transparent walls, reset, empty and resized grids, reused capacity against a
fresh grid, color two and four times coarser than brightness, work cells of
fractional pixels, a spark and a thin wall through split color and a
half-resolution gather, lighting that normalization fitted, every resolution,
color and gather combination, the adaptive ladder, GPU pass timing, and the
extend and filter passes alone against CPU references. The cases pin the
lighting they were calibrated with (finest grid, no ambient, intensity 7.5)
rather than the authoring defaults. On lavapipe it passes 645 assertions in
24.3 seconds, 30.5 seconds with an empty shader cache. HRC's view layout and
lighting normalization run without a GPU in `render-resources`, and so do
material borders: rectangles that cover exactly the cells near empty space,
their build scratch, and their cache's hits, replacement, eviction, limits and
a crowd of 20,000 masks.
`radiance-depth` lights Depth thickness from material inputs written by hand,
in a hidden 32 x 32 app with 128 x 128 targets for light that travels farther,
from the same pinned lighting but with color at half resolution: slabs that
shadow only the light they cross, by height whether or not they absorb, unless
they glow, small, moving, edge and mixed-color sources, falloff to the depth
radius, perspective views, the depth gain on receivers that absorb, as much at
a Material's full absorption as opaque, receivers that absorb nothing taking
HRC's light by facing and height, smoothly, gloss,
every depth quality, depth history of light and of that shading, segments and
steep planes. The surface
gather pass alone must take each quadrant's light, find crests past gaps and
stay dark without light in reach, and surface gather light must restart when
the view turns. Its cases share one HRC, since a new one compiles all of HRC's
shaders. It passes 2070 assertions in 11.5 seconds, 20.9 with an empty shader
cache.
`material-sampling` draws shapes into the material raster alone in a hidden
65 x 49 app: small circles must emit their area wherever they land on the pixel
grid and at twice the resolution, overlapping tiles must keep their draw order,
a moving ring of boxes in one tile group must keep its emission, and every
merged border must write the five targets its cells do. It passes 1330
assertions in 2.4 seconds, 3.2 with an empty shader cache.
`radiance-surface` lights solids through relief materials in its own hidden
128 x 128 app and from the pinned lighting of `radiance`:
edge light only on a silhouette's lit side, relief facing the light and below
a ceiling, a surface radius in world units, surface colors that keep the
direction they came from in every shadow mode, and surface light history. It
passes 86 assertions in 26.0 seconds, 30.8 with an empty shader cache.
`radiance-horizons4` and `radiance-horizons8` check relief shadows with four
and eight horizon directions: horizons that follow moving relief without a
reset, the horizon hierarchy against the fine scan, and the horizon cache
against fresh horizons through 15 stages of light, geometry, camera, reset,
resize, mode and fusion changes for all 8 sets of fusion, reprojection and
hierarchy. Each passes 2866 assertions in 23.3 seconds, 30.7 with an empty
shader cache. They run apart because lavapipe maps GPU memory a page at a
time: the cache case peaks between 46000 and 56000 of Linux's 65530 mappings
per process.
`material-pixels` checks what the material raster draws for HRC in a hidden
128 x 128 app: Set Material Parameter overriding one entity until its world is
cleared, a world-space material following its layout frame, a graph material's
parameter through the raster and its own fragment program inside a shape graph's
circle, mixed batches of graph boxes, plain boxes and meshes uploading only what
they draw, a sprite's normal and height maps tilting and raising each texel,
mirrored and turned with it, and fluids landing where game Y points down, gas
joining and parting, crisp unlit liquid edges, burning cores, plumes without
holes, fading parcels and wisps along their velocity. Gas and liquid must take
HRC's light and the wall's shadow with one and three traced cascades, a
refracting liquid must bend only the background behind it, and a crystal must
mirror the scene beside it. It passes 587 assertions in 11.5 seconds, 22.3 with
an empty shader cache.
These selectors need the material graphs of the main build.
`sdf-visibility` renders 14 small scenes of meshes and SDFs hiding each other
(occluders, cutouts, holes, partial cover, draw order, equal depth, perspective,
near clipping, a mixed 2D layer, and a later frame without the occluder) with
hidden-surface rejection on and off, and requires the same pixels both ways and
the expected depth snapshot copies. An indexed mesh asset of the occluding box,
split into three material groups, must draw the box's pixels and bound and
pick like it. It also checks Cute's whole-canvas depth copy
on its own: a cropped draw after the copy must not reach the snapshot. It builds
shader graphs, so it needs a compiler build, and it reads the shaders from
`src/render/shaders`. Build and run it like `interior-border`
with `AGENT_SELECTOR=--sdf-visibility`. On lavapipe it passes in 8.9 seconds
with Mesa's shader cache disabled and 2.9 seconds warm.
`sky-files` selects four in-tree Radiance `.hdr` cases: flat and run-length
decoding, fourteen malformed files, preparation from mounted content (read
once, with an identity that follows the texels) and import that never replaces
a file. It needs a compiler build, uses one temporary folder and passes 661
assertions in 0.12 seconds under ASan/UBSan with leak checks, without
processes or GPU.
`mesh-files` selects eight in-tree indexed `.mesh` cases: tangents that follow
texture directions (mirrored, rotated, tilted normals, unvarying coordinates
and a sliver beside a large triangle), preparation from mounted content (read
once, with part coordinates stored only when present), ten refused meshes,
each with its reason (bad indices, degenerate triangles, bad tangents, and
groups that split triangles or skip indices), unreadable files (retired
`.mesh.json` paths, text, truncated bytes and an older version), the largest
mesh a bake can write with one more triangle refused, a model export whose
`model.mesh`, OBJ faces and provenance agree, pick trees freed by each
publish, clear and exchange of the mesh catalog, and a pick of a quad 2e20
across. It needs a compiler build,
uses one temporary folder and passes 9,929 assertions in 9.2 seconds under
ASan/UBSan with leak checks, most of them writing and reading the largest
mesh, without processes or GPU.
`model-import` selects seventeen in-tree glTF import cases: the image, Standard
materials, mesh and prototype a `.gltf` writes, each once, byte for byte the
same from a `.glb`, and nothing for a material only a hidden node uses; names
made unique, ignoring case, and cut to 63 bytes; material names for a model
named as long as a name may be; an import that would replace a file, or fails
partway, leaves the content as it was; material and prototype names another
file in the project already declares, declaration folders it cannot read
and files it cannot parse, materials past the native graphs a project can
prepare, and content folders and folders in them that are links, which it
refuses;
refused extensions, texture transforms,
second UV sets, factors beyond float range, images and primitives; unreadable
files, names and buffers, buffers that `..` or a link leads outside the model's
folder, and buffers that together pass the size limit; unlit materials that
keep only their base color; the warnings for what it leaves out, and the
vertices only a left-out triangle used; a primitive whose accessors carry
more vertices than a `.mesh` file holds, of which its triangle uses three; a
model whose nodes copy a primitive past what a `.mesh` file holds, refused
while it bakes, and a primitive past it, refused before its vertices are
built; and tangent frames whose OpenGL green tilts up the image: generated,
authored, turned, mirrored by a node, flat at any size, flattened onto the
ground by a parent, scaled past float's volume or square, and carried past
its range. It needs a compiler build, uses one temporary folder and passes
176 assertions under ASan/UBSan with leak checks, without processes or
GPU.
`material-ramps` selects four hidden-app 64x64 cases. Nine full color ramps
fill the first 288 entries of a program's ramp table, so a last ramp's stops
lie past the 4 KB of a uniform block SDL's Vulkan backend exposes. That ramp
must show its color through a 2D draw shader and in the material raster's
scene target. The third case only compiles every program kind with the table,
which cannot catch a binding collision, and the fourth counts the ramp table
among the samplers a raster batch binds, which GLES3 limits to 16, among those
of a 3D shadow program, which binds none of the lit pass's maps, and among
those of the lit pass with global illumination's probes and without them.
Build and run it like `interior-border` with `AGENT_SELECTOR=--material-ramps`;
it needs the material graphs of the main build. It passes 43 assertions in 2.2
seconds under ASan/UBSan on Vulkan llvmpipe, whose worker threads leak 112
bytes, so it runs with `detect_leaks=0`.
`graph-model` selects two independently reviewed public consumer cases for nested
value copies, opaque artifact replacement/destruction and topology mutation
rules. They pass 25 assertions in 0.355 seconds under ASan/UBSan. The consumer
links only graph storage and its declared type/execution metadata dependencies.
The prepared-format-v5 state capture passes three compiler writer cases and 161
assertions, seven separate runtime consumer cases and 396 assertions, and one
process supervisor case with five assertions in 0.791 seconds. Its new state case
checks retained banks across publication and restart, plus retrigger deadlines.
