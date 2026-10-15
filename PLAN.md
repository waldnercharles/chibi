# Engine SDK and Chibi language implementation

## Objective and authority

Implement the production architecture agreed in this conversation: an engine SDK
with independently built compiler, execution and headless authoring modules;
Studio consumes public SDK interfaces; games consume installed engine artifacts.
Rename executable source to `.chibi`. Remove the Chibi MCP product entirely. AI
authoring edits asset files and uses format, validation, inspection and run tools.
Any repositories published to GitHub must be private.

The user authorized planning followed by implementation. This is the complete
deliverable, not a prototype-only task. Temporary graph adapters and mixed build
targets do not satisfy the final separation requirements.

Implementation status, 2026-10-08: the SDK separation, direct source compiler,
standalone player, external Studio and game/example extraction are implemented.
The final evidence below distinguishes passed builds and bounded execution from
unverified browser execution and quarantined coverage. The initial delivery was
local and uncommitted. The architecture reference is
[`docs/architecture.md`](docs/architecture.md). The progress entries record
successive intermediate states; their open issues are superseded only by later
explicit evidence.

Private source publication was authorized on 2026-10-08. The extracted
repositories are `waldnercharles/chibi-studio`, `waldnercharles/chibi-arena3d`,
`waldnercharles/chibi-space-cadet`, `waldnercharles/chibi-voidweaver` and
`waldnercharles/chibi-examples`. The engine refactor is published on
`cwaldner/code-language-prototype` in the existing private `chibi`
repository. Branch publication does not merge the refactor into engine `main`
or change the acceptance limits below.

## Evidence and preservation

- Branch `cwaldner/code-language-prototype`, based on `1e5d6584`. Preserve the
  conversation's uncommitted prototype and build/test integration while migrating.
- `prototypes/code-language` contains Tree-sitter syntax and semantic checking,
  not executable compilation. Systems, ECS paths and bindings are incomplete.
- `src/assets/code_program*`, `code_statements*`, `code_reactive*` mix syntax,
  semantic resolution, native graph construction and authoring information.
- `node_desc` mixes execution contracts and presentation. `behavior_function*`
  contains useful typed instructions/backends but retains graph-owned origins.
- `core` contains graph authoring, source intelligence and editor flow visuals.
  Universal public include roots expose internals. Automation/controller link the
  editor. The host application loop is implemented by Studio.
- Current native packages combine player/editor/controller/MCP/Studio assets.
  Existing package identities and game pins are useful foundations.
- Project format is 9, with nine tracked manifests and 108 executable `.code`
  assets plus 105 sidecars in the baseline inventory. Re-enumerate for acceptance.
- All test commands have a hard 60-second ceiling; quarantined suites stay
  disabled. Build/configuration timing is separate from execution evidence.
- The prior plan is preserved in
  `docs/plans/2026-10-07-authoring-migrations.md` as historical evidence.

## System design

Source text is authoritative. Syntax snapshots preserve text and recovery. The
resolved program owns typed declarations, symbols, expressions, queries, events,
state, bindings and effects. The executable module owns verified instructions,
layouts, imports and origin indices. Neither resolved nor executable programs
own editor objects or graph nodes. Generated machine code, compiler scratch and
live instances have separate lifetimes from immutable definitions.

The capability catalog owns stable IDs, explicit script names, reflected types,
signatures, effects and native execution contracts. Graph presentation refers to
capabilities. Resolve names once. Persistent modules contain IDs and compatibility
requirements, never process pointers. The compiler understands typed ECS reads,
writes, queries and batch operations; SLJIT details stay in its backend.

ECS owns entity generations, component storage, query membership and structural
mutation. Execution owns state, activations, dispatch, suspension and cancellation.
Systems own registration and matched-entity lifetimes. Reactive execution owns
bindings/subscriptions. Scheduling consumes verified effects and explicit order.
Execution views expire at structural or suspension boundaries. Query exit/entity
destruction cancel work and subscriptions. Invalid code cannot replace a valid
generation. Do not reintroduce native hot reload.

Authoring owns source/graph edits, navigation, refactoring and diagnostics; Studio
owns UI. Graphs project resolved meaning. Graph semantic edits update `.chibi`,
then reanalyze before adoption. Optional `.chibi.meta` holds identity/layout/debug
presentation, never executable facts. Owned source-origin records map instructions
to source and optional graph presentation without depending on graph lifetime.

The direct alternative, renaming folders while preserving graph-mediated
compilation, would preserve the coupling. Extract existing implementation through
real interfaces instead. Track temporary bridges and remove them before completion.

## Package and repository boundaries

Each C package has explicit source ownership, scoped public headers and private
implementation headers. Target packages are types/capabilities, ECS, language,
compiler, execution/backends, runtime integration, optional native capabilities,
asset preparation, shared authoring, Studio and CLI tools. Rendering, physics,
input and game UI remain optional. A prepared-asset player needs no syntax or
authoring libraries, though native execution can include SLJIT.

Header search paths are owned per target in source and installed builds. Linking
a target makes only its declared dependency closure visible. Package validation
rejects exports of a shared include root or undeclared header owners; file include
spelling stays stable through the target's generated search paths.

Keep SDK/compiler/runtime contracts in the engine repository. Build Studio
against an installed SDK before extracting it into its own repository. Actual
games move to independent repositories. Curated labs/examples move to an examples repository;
minimal regression fixtures remain with their owning module. Verify replacement
repositories and fixtures before removing old source projects. Preserve provenance.
No remote is required for local extraction. Inspect remote visibility before any
push; create new GitHub repositories as private only.

## Implementation stages

### 1. Asset contract and MCP retirement

- Change `src/support/asset_text.h`, hardcoded catalog/editor filters, web MIME and
  preload lists, examples, tests, source tools and active docs to `.chibi` and
  `.chibi.meta`. Replace hardcoded suffix lengths with the central constant.
- Rename the full authored corpus and references directly. Do not modify unrelated
  C `.code` members, CSS classes or `.codex` paths. No legacy extension aliases.
- Bump `CHIBI_PROJECT_VERSION` and all project manifests to 10.
- Remove `tools/mcp`, MCP-only auxiliary servers/configuration, Make/setup/cache
  hooks, packaging/install payload and obsolete MCP-only tests. Preserve package
  identity pins and unrelated user configuration.
- Retain useful native format/validate/inspect/scenario/preview CLI operations.
  Remove MCP-only AI mutation entry points after checking shared callers. Shared
  Studio semantic editing stays in the authoring module.
- Retire or convert authoring scripts that launch MCP to direct asset generation;
  do not hide the old graph-mutation workflow behind a renamed transport.
- Rewrite current authoring instructions around asset files and ordinary CLI
  validation. Historical evidence may describe the former product explicitly.

### 2. Build ownership and headless separation

- Refactor `CMakeLists.txt` and `cmake/sources.cmake` into explicit package source
  lists. Move flow visuals out of core and pure component document parsing out of
  editor. Remove automation's editor dependency through shared interfaces.
- Separate Studio schema/catalog declarations from UI callbacks. Studio-specific
  capabilities are an optional authoring extension, absent from player linking.
- Break actual dependency cycles before splitting libraries: shared values/arrays
  become leaves; low-level serialization must not dispatch back into source
  formatting; runtime instances must not own compiler publication.
- Separate configuration of compiler/headless/player/Studio dependencies. Compiler
  builds must not require SDL, RmlUi, renderer assets or a live world.
- Export scoped public headers and installed CMake targets. Add executable build
  dependency checks. Do not merely rename mixed `core`/`runtime_support` libraries.

### 3. Capability and executable model extraction

- Extract value/type layouts and host/query/command contracts from graph model
  headers. Adapt reflected registrations and native operations without duplicating
  signatures or using graph display labels as script names.
- Extract graph-free source origins, typed instructions, state layouts and
  executable ownership from `behavior_function*`. Move graph traversal and
  frame-input reconstruction into the graph producer bridge.
- Separate compiler scratch, immutable modules, linked/backend code and activation
  storage. Preserve existing optimization and execution implementations.
- Add a narrow builder/finalization/verifier interface. Both producers use this
  compiler; persistent modules verify imports, types and layout requirements.
- Make the VM/native backend independently linkable without graph/editor objects.

### 4. Production Chibi frontend

- Promote the prototype into the language package. Cover the production corpus:
  concrete types, ECS paths, systems/queries, events, bindings, callable libraries,
  control flow, locals/state/shared state and suspension. Inspect each current
  contract; do not quietly reduce supported behavior to the prototype subset.
- Lower the resolved program directly into the shared executable representation.
  Remove obsolete handwritten parsing and duplicated catalogs as paths migrate.
  A remaining required native-graph intermediate is incomplete work.
- Define and enforce numeric/conversion rules, payload signatures, optional
  component presence, effects, binding purity/cycles/writers and suspension.
- Add runnable system, non-Tick event and binding examples. Verify ECS outcomes,
  query entry/exit, event payloads, cleanup and actual backend selection.

### 5. Runtime, prepared assets and player

- Make runtime definitions immutable program references, with state/activation
  ownership separate. Remove authored graph lifetime from execution.
- Preserve behavior/system state lifetimes, cancellation, subscription and binding
  order. Reacquire component views after suspension/structural boundaries.
- Split editing/working/applied/unsaved state from `runtime/behaviors/live.*` into
  authoring integration. Split viewport controls out of runtime engine headers.
- Add offline executable preparation and explicit asset dependency manifests.
- Build a standalone player with its own host loop and scene loader, excluding
  Studio, source intelligence, authoring history and MCP.

### 6. Shared authoring and Studio

- Move shared diagnostics/navigation/refactoring/document transactions into the
  authoring package. Map graphs and debugging through semantic identities/origins.
- Preserve drafts/comments, source/graph synchronization and invalid-edit behavior.
- Build Studio against an installed SDK with private engine source headers absent.
  Fix needed functionality through the owning public interface.

### 7. Artifacts and project extraction

- Produce distinct runtime/player, SDK/compiler and Studio/tooling packages.
  Record checksums, build/release identity, project format, executable/runtime ABI
  and external protocol requirements. Consume copied, pinned installed artifacts.
- Extract Arena3D, Voidweaver/arena2d and SpaceCadet into game repositories after
  moving engine regression cases to focused fixtures. Preserve authored source.
- Extract labs/sampled-model examples with licenses/provenance and requirements.
- Replace implicit `projects/*` shipping/preload discovery with explicit product
  choices. Studio assets remain with Studio.
- Verify an external game against installed artifacts. Any pushed remotes private.

### 8. Review and acceptance

- Independently review ownership, include/link dependencies, lifetimes, migration,
  source/graph transactions, package contents and extracted repository completeness.
- Use admitted bounded C/pico_unit checks for syntax/semantics, VM/native agreement,
  ECS outcomes, events, bindings, suspension and cancellation. Add reviewed bounded
  cases for missing contracts. Do not reconstruct quarantined suites as chunks.
- Build native SDK/compiler/runtime/player/Studio and applicable web targets.
  Verify real Studio source/graph interaction when these paths change.
- Validate and canonicalize all retained and extracted assets with current binaries;
  inspect packages and installed external consumers. Record exact evidence.
- Apply reviewed-validation/publication rules. Missing affected coverage remains
  open, never silently treated as passed. No broad claim from a narrow check.

## Completion evidence

- [x] Detailed plan precedes implementation; old plan preserved.
- [x] `.chibi` extension and project version 10 migration verified without aliases;
  full corpus formatting remains quarantined and unverified.
- [x] MCP product, setup, dependencies, packaging and active workflows removed.
- [x] Narrow public headers and package dependency enforcement in place.
- [x] Independent compiler configuration without world/UI/render dependencies.
- [x] Resolved/executable programs free of graph/editor ownership.
- [x] Production corpus uses the new frontend and shared compiler.
- [x] System, non-Tick event and binding examples execute with checked outcomes.
- [x] Headless runtime and standalone prepared-asset player verified by focused
  VM/SLJIT and prepared reader checks; full runtime/render suites quarantined.
- [x] Shared authoring and installed-SDK Studio build and bounded edit/history
  interaction verified.
- [x] Runtime/backend and applicable native UI and web build evidence recorded;
  browser execution remains unverified.
- [x] Separate packages and pinned external-game execution verified.
- [x] Game/example repositories preserve source; regression fixtures migrated;
  implicit project shipping removed; any published repositories private.
- [x] Independent final implementation audit recorded, with explicit acceptance
  limits below. No publication or broad production certification is claimed.

## Final implementation evidence — 2026-10-08

The engine owns types, ECS, capability contracts, syntax/analysis, direct
compilation, execution/backends, runtime integration, preparation and shared
authoring. `sdk/contracts.cmake` enforces declared source/link ownership; scoped
public headers and installed consumers enforce header visibility. Compiler builds
omit desktop dependencies. Prepared-player builds omit compiler and authoring.
Studio compiles through `find_package(Chibi)` with no private engine source
include paths. Source graphs are projections and edits of `.chibi`; they are no
longer the executable compiler intermediate. MCP is absent from the products.

Five separate Git repositories retain extraction records. These paths describe
the original local extraction; their private GitHub repositories are listed above:

| Repository | Ownership |
| --- | --- |
| `/Users/cwaldner/Personal/chibi-studio` | Studio, shell/starter assets, native and browser hosts |
| `/Users/cwaldner/Personal/chibi-arena3d` | Arena3D game assets |
| `/Users/cwaldner/Personal/chibi-space-cadet` | SpaceCadet assets and copied player pin |
| `/Users/cwaldner/Personal/chibi-voidweaver` | Voidweaver game assets |
| `/Users/cwaldner/Personal/chibi-examples` | Curated labs and sampled content |

Retirement followed preservation and validation: 109 obsolete unbuilt production
files, 284 game originals (including a separately preserved historical README),
and 532 Studio/source/resource/oracle/build-wrapper files were removed. The 344
example originals were also extracted. Engine regression fixtures remain with
the engine. Historical Studio oracles are preserved under Studio's
`tests/held/engine_oracles`; they are held coverage, not passing tests. The separate
Neon Swarm checkout was outside this migration.

The final product packages are under `build-verify/sdk-migration/`:

| Product | Directory | Package identity |
| --- | --- | --- |
| Compiler/authoring SDK | `release-sdk-package` | `f9491e2a267ed8dc34ace0ae162907159e52083aba9d1061bf7fd8e58a31ba35` |
| Compiler-free runtime SDK | `player-release-runtime-sdk` | `d837f03665894e64941b31949f577e02d0efeda37fba371ba653fa71f8020ec2` |
| Player deployment | `player-release-package` | `77fda1d78d1a02c53a3ff2a7481948abd27bb9614f861024ffbce6bbb06adebd` |
| Studio Release distribution | `studio-release-package` | `0d54a83382cbba2a60ef463d29763147905367ab193b9a6e01737a9fa28c0cbb` |
| Serial Emscripten SDK | `web-sdk-package` | `b78a78611acaaaa234c3e5941cfdc16e7bf8ab5de1ee95b0da973323eb4ffef2` |

Package audits verified complete inventories and installed consumers. The Studio
distribution pins the compiler SDK above, contains 368 files and has only system
dynamic dependencies. The player deployment contains 96 inventoried files plus
its manifest, with no compiler, authoring, SDK headers or libraries. Using the
installed packager leaves the SDK inventory unchanged.

Passed acceptance is bounded as follows; every test used the production
60-second supervisor, with configuration/build time recorded separately:

| Check | Result |
| --- | --- |
| Final root products and C consumers after source retirement | Build passed |
| Final tooling/process aggregate | 36 cases, 960 assertions; 19.869s |
| Execution-policy checks | 6 cases, 61 assertions; 0.539s |
| Six unique external catalogs, 87 sources / 138,371 bytes | Semantic 6/471, VM preparation 6/483, SLJIT preparation 6/483; each under 2s |
| Source systems / execution programs | 8/314 and 18/2,011 cases/assertions; VM and SLJIT |
| Service ownership and incremental attachment | 2 cases, 104 assertions; VM and SLJIT |
| Source scheduling | 4 cases; parallel cohorts and serial barriers checked |
| Source presentation/layout | 3 cases, 7,696 assertions; 0.842s |
| External native Studio | Four hidden rendered frames, one graph edit, one undo, one redo; 1.607s |
| Copied SpaceCadet player, VM | Four frames, three simulation steps; 4.200s |
| Copied SpaceCadet player, SLJIT | Four frames, three simulation steps; 1.411s |
| Final installed SpaceCadet compilation | 1.155s; 3,256,763 bytes, eight embedded resources |
| External serial browser Studio | Installed SDK configure/build/link passed; browser not launched |

The Studio interaction checks actual production editing/history handlers, source,
typed semantics, graph values and dirty state. Undo restores the clean original;
redo restores the edit. Automatic graph coordinates do not become authored
metadata, and programmatic widget echoes do not create duplicate history entries.
The fixture and its preferences/cache are disposable and removed after the run.
This checks hidden rendering and handler integration, not pointer hit testing.
The native acceptance uses a Debug installed SDK consumer; the Release Studio
distribution has separate build/package evidence, not a launch result.

SpaceCadet consumes its copied player, pinned by `.chibi-engine.json`. The final
compiler SDK emitted the exact bytes already executed on both backends:
SHA-256 `c8826370473e3dbf86e7bc7c53db721da2e9de9aee7c2b329950c402462ce1ff`.
The compiler and runtime package IDs differ; their checked runtime headers,
formats, schemas and platform contracts agree. Recompilation did not imply a new
GPU run. Logs, inventories, preservation records and JSON acceptance reports are
retained in `.chibi/architecture/`; product/build outputs are retained in
`build-verify/sdk-migration/` for inspection and continued development.

Browser execution, threaded browser behavior, full corpus canonical formatting,
full native/render suites and the held historical Studio oracles remain
unverified. The temporary test policy keeps the full suites quarantined. These
limits prevent a full verification or release-readiness claim; they do not change
the implemented repository and package ownership. The initial implementation
delivery performed no commit, push, PR, merge or deployment. Subsequent private
source publication preserves these acceptance limits.

## Historical progress — superseded by final evidence above

2026-10-07: current tree, previous plan and owning contracts inspected. Inventories
completed for extension migration, MCP retirement and compiler/runtime extraction.
The first extraction and compatibility checks completed:

- Renamed 108 tracked executable assets, 105 sidecars and five prototype examples
  to `.chibi`; all nine project manifests and `CHIBI_PROJECT_VERSION` are 10.
  The 213 tracked asset/sidecar payloads were byte-identical after renaming.
- Removed the MCP servers, setup/cache hooks, package payload and active authoring
  instructions. Native mutation operations and their uncalled helpers are now
  removed; retained inspect/analyze test setups are being converted to file edits.
- Installed SDK exports support, types and execution contract headers. The same
  public-header contract tests build against an installed SDK without private
  engine includes. Before the next ECS expansion, native tooling passed 27 cases,
  317 assertions in 20.340 seconds; installed consumption passed in 22.067 seconds.
- Controller no longer links editor/RmlUi Studio/Zep targets. Native editor,
  controller and test-suite builds passed Debug, warnings enabled, sanitizers off.
- A bounded hidden Metal preview rendered one SpaceCadet frame at 320x180.
  Inspection verified nonblank, correctly oriented scene compositing. This does
  not establish web or full Studio coverage.
- The admitted compiled-behaviors selector passed in 10.387 seconds after six
  generation fixtures adopted identity-preserving clones instead of source-only
  save/load. All state, cancellation, ownership and update assertions remain.
- Prototype checking passed 13 cases and 137 assertions in 0.421 seconds. It is
  still only frontend checking, not proof of executable source compilation.
- Full corpus formatting hit its 60-second process ceiling without completion
  evidence. Canonicality remains unverified. Full suites remain quarantined.

Evidence from this stage is in `.chibi/architecture/`. Source changes remain
uncommitted and later implementation changes require new affected checks.

Current work extracts ECS into `Chibi::ecs`, graph-specific readers into the graph
adapter, backend artifacts and graph-free origins from executable definitions,
production Tree-sitter syntax/semantics, and authoring documents from live runtime
registrations. Games and labs are preserved in four local repositories under
`/Users/cwaldner/Personal/chibi-*`, with source-file checksums and no remotes.
Original assets remain until test fixtures and explicit packaging roots replace
their dependencies. None of the incomplete acceptance items is claimed complete.

The next extraction produced independently configured `Chibi::ecs`,
`Chibi::language_syntax`, `Chibi::language`, `Chibi::execution` and
`Chibi::compiler` targets. Scoped header copies and explicit package dependency
contracts reject accidental graph/editor includes and cross-package links.
Direct execution links initially exposed two graph-owned helpers, now moved to
the execution package. Public builder tests also found and fixed comparison
result typing in both VM writes and native register allocation.

Snapshot evidence from this stage, before subsequent source/module work:

- Native tooling including ECS contracts passed 32 cases and 641 assertions in
  19.879 seconds.
- Production syntax passed 8 cases and 702 assertions over 158 files totaling
  218,889 bytes, through an installed SDK consumer, in 0.328 seconds. The exact
  manifest is `.chibi/architecture/installed-language-corpus.txt`.
- Semantic analysis passed 7 cases and 36 assertions with ASan/UBSan in 0.468
  seconds; the installed consumer passed in 0.285 seconds.
- Public builder execution passed 7 cases and 104 assertions, using the actual
  VM and SLJIT, with ASan/UBSan in 0.641 seconds. The installed consumer passed
  in 0.325 seconds without private source includes.
- Optional runtime construction and authoring publication lifetimes passed the
  admitted lifecycle selector in 1.180 seconds. Neutral host requests, including
  native Spawn's initializer ownership adapter, passed in 0.281 seconds.
- Read-only authoring navigation/code checks passed in 1.004 seconds after
  preserving synthetic event symbol initialization. Search passed in 52.728
  seconds after correcting invalid-source metadata ownership and the expected
  host-event set. Compiled behaviors passed in 13.549 seconds after repairing
  publication fixtures and integrating the source registry. These remain
  focused checks rather than full parity evidence.

The neutral source module runner and realistic system/event/binding execution
cases are being integrated. Production corpus semantics, source publication,
managed component ownership, prepared asset serialization, installed Studio,
external-game execution and full canonical formatting remain open. Four local
repositories preserve game/example source and historical acceptance assertions;
their installed-player acceptance has not run. Static optional runtime flags do
not yet prove a renderer-free runtime link target. These are required remaining
work, not deferred acceptance exceptions.

The next source-runtime stage produced these additional snapshot results:

- Managed ECS row ownership passed 34 tooling cases and 935 assertions in
  19.166 seconds, including replacement and component/entity/world cleanup.
- Three actual `.chibi` example files passed compilation and execution on VM
  and SLJIT: a typed event, query membership with cancellation, and a component
  binding. The initial three-case check passed 59 assertions in 0.526 seconds.
- `Chibi::behavior_runtime` owns the real program registry used by the game.
  Its dependencies exclude language, compiler, graph, Studio and graphics. A
  separately compiled public consumer passed 50 assertions in 0.441 seconds
  with ASan/UBSan, and in 0.454 seconds against installed SDK artifacts.
- Native prototype/source integration passed 68 additional assertions on both
  backends inside the lifecycle selector (1.628 seconds). It covers deferred
  Begin, typed events, state-preserving replacement, cancellation and system
  updates preceding bindings, including reversed registration order.
- Public execution tests now cover all integer bitwise widths and neutral
  logging context. Ten cases and 726 assertions passed in 0.577 seconds with
  ASan/UBSan. The source-level logging call also passed in the headless consumer.
- Grammar regressions for generic calls, `await`, named values and explicit
  typed arguments are fixed. Retained syntax passed 145 files/213,858 bytes,
  eight cases and 665 assertions in 0.869 seconds with ASan/UBSan.

An independently reviewed semantic aggregate now covers nine production project
catalogs and 148 sources, including extracted games/examples. It is not passing:
it exposed missing source contracts and a null outcome descriptor during error
recovery. The descriptor bug is fixed. A renewed normal-build aggregate completed
without crashing in 4.188 seconds, with two projects passing and seven still
reporting diagnostics across 148 sources and 255,417 bytes. Imported-library/managed-state
regressions, cross-module binding ordering and full source preparation still
require verification. No production loader switch or prepared-player acceptance
is inferred from the smaller passing examples.

Source preparation and import resolution now build as `Chibi::authoring`, separate
from the adapter that applies a generation to a live world. The SDK can configure
with `CHIBI_SDK_BUILD_COMPILER=OFF`; that configuration creates no parser, compiler,
language or authoring targets. The prepared behavior package loader uses only
execution/ECS/types and validates every module before returning a registry.
Prepared serialization and owned decoding have independent review, including
malformed cleanup, compatibility requirements, import fingerprints and bounded
allocation. Separate-process and installed-package validation is in progress.

The frozen prepared-artifact process check passed in 0.701 seconds: the compiler
process wrote three modules and retired all compiler products before a separate
runtime-only process loaded and executed them. It covered deterministic bytes,
state, typed events, imports, bindings, malformed input, ABI rejection and atomic
package loading on VM and SLJIT. The producer passed 31 assertions, runtime 184,
and process coordinator five. The SDK was configured without compiler packages.
The source-preparation/headless roundtrip also passed two cases and 106 assertions
with ASan/UBSan in 0.635 seconds. These prove the behavior runtime seam; they do
not establish a complete scene player. The expanded program suite currently
aborts on an anonymous aggregate in the imported-library fixture; repair and
renewed execution evidence remain required.

The imported result-bank representation and root timer continuation defects are
fixed. The expanded program suite now passes nine cases and 342 assertions on VM
and SLJIT with ASan/UBSan in 0.746 seconds. Its checks include imported component
reads after replacement, independent shared system storage, retained strings over
two resumes, sibling timers, cancellation and copied source diagnostics. A bounded
full collection preparation selector is independently admitted; it prepares every
source in the production manifests using the same collection API as authoring.

Host event declarations now live in `Chibi::events`, with neutral payload fields
and a types-only dependency; graph pin construction remains in the graph adapter.
The production semantic aggregate passed all nine projects and 148 sources with
794 assertions in 4.321 seconds. Full executable preparation completed its first
admitted run in 3.644 seconds, with one project passing and eight failing. Native
capability linkage, component selectors, continuation control flow and a few typed
lowering operations are being repaired; the loader switch remains held.

The standalone runtime SDK was packaged with explicit content and compiled-input
checksums, configured header copies, object/archive freshness evidence, exported
target closure, and project/prepared format requirements. A fresh consumer built
against its installed headers and archives then passed the separate-process
prepared check in 0.728 seconds. The tested package build ID is
`66d69baf99ce9fbd5cd2f3ef17aa7417ac84c40131be7b9e4536efbceb217024`.
It exports eight runtime targets and no compiler/parser/authoring package. This
snapshot uses prepared format 1; referenced event declaration metadata now being
added changes the wire format to version 2 and requires renewed package evidence.

The next verified language snapshot passes ten program cases and 384 assertions
in 0.759 seconds with ASan/UBSan. Eager multi-result locals retain independent
scalar and managed-string copies through reassignment, loops and suspension on
both VM and SLJIT. The whitespace-only source formatter passes six cases and 612
assertions in 5.655 seconds, including unchanged syntax and idempotence for all
145 retained files (215,022 bytes). CLI formatting still uses its previous path
until optional sidecar identities can be remapped without graph construction.

Prepared format 2 now retains named event declarations and defaults. The renewed
process check passes producer/runtime/coordinator assertions of 31/184/5 in
0.627 seconds, and the headless source roundtrip passes two cases and 126
assertions in 0.705 seconds. The earlier installed package is still a format-1
snapshot and must be rebuilt before it can establish current installed coverage.

The latest normal native semantic run passes all nine projects and 148 files in
4.198 seconds. Preparation completes in 4.379 seconds with two projects passing
and seven failing. Native adapters, explicit component argument roles and event
payload retention are being repaired against those diagnostics. Activation
payload reclamation and grouped lazy tuple initialization remain open lifetime
contracts. Source collection publication, the scene player and installed Studio
are not yet complete.

Document values now have their own `Chibi::asset_values` package; the reflected
value writer is `Chibi::value_document`. Execution, ECS and types do not link
either document package. Native UI and asset callers share this writer through
the package boundary instead of reaching into graph serialization.

The next standalone snapshot builds successfully with scoped SDK headers. Its
expanded prepared check passes two producer cases (147 assertions), six runtime
cases (334 assertions), and the process coordinator (five assertions) in 0.751
seconds. It verifies transitive external imports, duplicate compatible
generations, incompatible generations/event schemas, and optional or required
event linkage. The source/headless roundtrip passes 126 assertions in 0.657
seconds; execution contracts pass 726 assertions in 0.744 seconds.

Pure source formatting and optional metadata remapping pass eight cases and 627
assertions in 5.767 seconds, including the full retained 145-file corpus. The CLI
now uses this syntax path for executable source; the updated native CLI has not
yet been built and run for all-project canonicalization. Sidecar hashes bind
presentation to exact source bytes; format remaps only unchanged syntax and
token boundaries without constructing executable graphs.

Managed payload collection now separates durable activation control from value
storage and bounds unused storage to 4 KiB per root activation. Review corrected
collection during imported execution, dead-slot growth, and fresh arena
initialization. The new varied-size suspension test exposes a remaining outlined
tuple lowering failure (`slot 2: invalid scalar type`); ten existing program cases
pass, but the eleven-case program selector is not passing. Physical address
temporaries still share the integer storage tag, so exact pointer provenance
remains a distinct open executable contract.

The next standalone snapshot resolves the outlined call and grouped tuple defects.
Program execution passes 12 cases and 1,638 assertions with ASan/UBSan in 0.881
seconds. Lazy tuple groups initialize once, preserve sibling fields on writes,
suppress their producer after whole replacement and survive continuation capture.
The zero-argument outlined invocation now allocates its owning data bank. Exact
physical-address provenance remains open; numeric slots are still conservatively
considered by parts of value retention.

Canonical navigation/refactoring/intelligence passes 11 cases and 99 assertions
in 0.678 seconds. Implicit event payloads no longer hide alias references; type
annotations come from semantic facts. Scene lifecycle passes two cases and 59
assertions in 0.656 seconds after applying entity creation before component adds.
Transactional publication passed three cases and 160 assertions in 1.025 seconds,
including root and imported shared state with every owner detached. Prepared
format 3 passed the separate-process closure check in 1.008 seconds; the older
installed format-1 package remains stale.

Scheduling now has a compiler-free SDK target with an injected worker executor.
Prepared scalar fields have a separate validated runtime/codec; their three public
cases pass 254 assertions in 0.828 seconds with ASan/UBSan. The field graph compiler
and native physics nodes are explicit graph adapters. Named field parameter text,
physics event/retirement services and the complete prepared player still need
extraction. Native scheduler tests are admitted but not yet run.

Studio source sessions, graph presentation ownership, canonical navigation and
refactoring are being migrated. Current source files pass focused syntax checks;
editor tests and source-system document consumers are still being converted. The
last native preparation snapshot passes seven of nine projects, with only Field
Add/Subtract linkage failures; those ports are implemented but await the next
native capture. The production loader has not switched, and no Studio/web or
complete player acceptance is claimed.

The SDK now owns public and private include roots per target, including installed
consumers. Configuration and the isolated types/fields/named-field catalog build
pass with these roots. Named field execution takes typed parameters; KDL parsing
is an asset-preparation adapter. Exact value provenance and prepared format 4 are
in flight, so no full SDK or installed-package acceptance is claimed yet.

Editor source sessions now preserve sidecar conflict detection and recover from
a source write that succeeds before a sidecar write fails. Four finite source
session cases have independent admission and await native execution. A reviewed
Studio edit defect is fixed: graph edits reject a stale displayed generation
instead of overwriting a newer invalid or unapplied source draft.

Explicit VALUE/ADDRESS/REFERENCE provenance now passes the expanded execution
selector: 11 cases and 758 assertions on VM/SLJIT with ASan/UBSan in 0.991 seconds.
Program execution passes 12 cases and 1,638 assertions in 0.484 seconds. Prepared
format 4 passes the separate-process checks in 0.474 seconds. Named field catalog
execution now passes all four cases and 267 assertions in 0.650 seconds.

The compiler-free runtime SDK is rebuilt with isolated installed include roots
and prepared format 4. Its package build ID is
`05d564d5e47104b14410a4406200866a7dfbc06ad2b5abb756ab991daca64acd`.
A consumer built against only that installed package passes the prepared process
check in 0.404 seconds and the two scene lifecycle cases in 0.100 seconds. This
supersedes the earlier format-1 runtime package evidence. It still does not prove
the complete player or native capability pack. The compiler-enabled package,
native corpus and Studio source integrations await their current captures.

The compiler-enabled SDK package now has build ID
`5b2782adc10496a12a9f165738060930b5b0911281b6807be65cafd37f42db78`.
Its shared runtime header identity matches the installed runtime package. Both
producer and consumer built against their installed packages pass prepared-file
handoff; the installed compiler also passes all 12 program cases and 13 navigation
cases. These are normal Debug package consumers. The separate in-tree language
checks retain ASan/UBSan coverage. External event declarations and local event
aliases now remain distinct during rename, and source intelligence accepts KDL
documents without syntax/analysis records. Native integration capture is pending.

The next native capture builds the CLI, Studio and test dispatcher successfully.
Canonical preparation passes eight of nine project corpora in 4.676 seconds;
semantic analysis passes the same eight in 5.368 seconds. The remaining SDF lab
failure comes from duplicate declaration/reference traversal exhausting the scan
budget. The reviewed single-pass correction awaits execution. A shared unary
instruction description fix removed the other seven preparation failures. Its
standalone VM/SLJIT regression passes 12 cases and 773 assertions with ASan/UBSan
in 0.571 seconds; program execution passes 12 cases and 1,638 assertions.

Native presentation passes two cases and 39 assertions. Source sessions complete
without their former crash but retain two failures; source editing retains two
failures. Scheduler diagnosis found invalid source reaching lowering with no
program identity. The production loader cutover remains gated on the nine-project
preparation and corrected lifecycle checks.

Capability declarations now own a parser-independent SDK target. A separate
native binding target validates callback descriptors and their declaration ABI;
real reflected/manual native producer extraction remains in progress. Document
value reading is now a public arena-owned adapter used by graph and editor
consumers. The tooling aggregate passes all 36 cases and 955 assertions in 19.311
seconds, including two new ownership/malformed-value cases. Installed package IDs
above predate these module changes and must be renewed for final acceptance.

The corrected native capture passes all nine semantic project corpora with 794
assertions in 4.922 seconds and all nine prepared corpora with 812 assertions in
4.334 seconds. Source sessions pass four cases and 71 assertions in 1.483 seconds;
canonical lifecycle passes 104 assertions in 0.781 seconds alongside the existing
graph lifecycle checks. Canonical EngineSystem phase/removal passes before a
separate graph scheduler fixture fails its movement expectation. Production
loader cutover is now implemented and source-reviewed, pending capture.

The seven isolated source-system scenarios have their own reviewed CPU selector.
The first capture passes two and fails five. Owners identified missing source host
service bindings, optional-component condition lowering and event receiver metadata
as production gaps, plus incomplete scene/query composition in two fixtures.
Expected observable effects remain unchanged. The first source-edit capture also
found builtin scalar types incorrectly marked unresolved when a catalog has no
nominal types. That owning catalog fix awaits the next source-edit capture.

RML reference renaming now passes 14 canonical navigation cases and 144 assertions
in 0.743 seconds with ASan/UBSan. Component lookup and presentation metadata have
public owning packages; project manifest ownership is separate as well. Native
reflection output separation and checked graph gesture coverage remain active work.

Exact scalar parsing and nominal enum literals now pass the expanded 13-case
program selector with 1,689 assertions in 0.884 seconds on VM/SLJIT with ASan/UBSan.
The current SDK navigation capture passes all 14 cases and 144 assertions in 0.777
seconds. Component presentation, project manifests and the reference index compile
as independent targets against scoped public headers. Generic prototype documents
and their explicit native creation policy are extracted; the new ownership/default
case passes all 21 assertions in 0.928 seconds under ASan/UBSan after the
reviewed reflected-bounds correction.

Reflection now generates separate type metadata, capability callbacks and graph
adapters. The generated GAME type object imports only ECS storage/access commands;
the capability object imports native C functions, type metadata and owned-value
snapshot support, with no graph/eval/flow symbols. These objects compile. The first
whole native build stopped at consumer includes after the reference-index split;
this is not yet a linked player/package result. Native capability producer
composition and entity services remain in progress.

The next native capture builds the CLI, Studio and test dispatcher after the
component/entity/audio SDK split. The isolated native runtime SDK also compiles
and packages with compiler/parser packages disabled. Its build ID is
`a8293c6f767c5351bdedfe7398f639545dd9d9c9f51c7cb6dcbacc5e9b572400`;
it exports actual component layouts, entity execution/source host and audio
capabilities. Installed native consumer execution and a complete player remain
unverified. Generic scene serialization and UI source authoring also compile as
independent scoped packages. The first UI cases expose a pinned scanner callback
type mismatch under UBSan; a targeted configured-source correction awaits rerun.

The compiler-free native SDK consumer now executes both VM and SLJIT rather than
skipping the backend loop. Its installed entity/prototype case passes 84 assertions
in 0.442 seconds, including retained generations, initialization before Begin and
cancellation. The corrected pinned UI scanner callbacks pass both UI cases and 24
assertions with ASan/UBSan. These results belong to the captured packages and builds;
the package IDs above predate later capability extractions.

Optional Studio declarations and calls now compile behind their own SDK target.
Math, vector and CPU particle packages compile with scoped native SDK headers.
Set and selection kernels and the explicit host catalog builder also compile in
isolation. Runtime profiling passes four publication cases and 226 assertions in
0.964 seconds with ASan/UBSan. Canonical navigation now passes 16 cases and 195
assertions in 0.614 seconds. The new synchronous action outcome contract passes
13 execution cases and 792 assertions in 0.479 seconds on VM and SLJIT.

The latest completed native capture passes all nine SLJIT corpora, 148 files and
812 assertions in 5.246 seconds. Source editing passes all nine cases and 141
assertions in 0.374 seconds, including extraction, inline edits and metadata
preservation. Six of seven source-system scenarios pass; typed reactive field
notification remains under implementation. Later package and prepared-format
changes require a new capture before final runtime or Studio acceptance.

The next scoped SDK extraction builds comparison/conversion/text, Save, UI, input
and graph model archives using only their declared headers. Two text cases pass
41 assertions under ASan/UBSan; one native value case passes 43 assertions; two
graph model consumer cases link and pass 25 assertions under ASan/UBSan. Generated
native declarations now omit unused plain-struct value signatures, allowing strict
compilation as a native SDK owner. Prepared format v5 state records cross the
separate compiler/runtime process boundary: 161 writer, 396 reader and five
supervisor assertions pass. The complete native and desktop package captures are
still pending ongoing physics/material and renderer/authoring extraction.

2026-10-08 integration checkpoint: the complete desktop runtime archive set and
then the desktop compiler/authoring archive set built through scoped SDK headers.
The runtime configuration disables the compiler and includes real rendering, UI,
physics and simulation ownership. Installed consumer linking remains outstanding.
The first public consumer captures passed native catalog 1 case/11 assertions,
prepared scene codec 2/33, typed fields 5/285, graph authoring 1/13 and model runtime
3/47. Native prepared-player handoff passed 3/164 on VM and SLJIT after correcting
world construction to reuse exactly matching component descriptors. The model
proof's retained shader sample was corrected to its translated object-space
center; runtime behavior did not change for that correction. Logs are in
`.chibi/architecture/`.

The new field-observer persistence case initially rejected an anonymous entity
payload header. Its narrow builtin `ecs.entity` repair is independently reviewed;
a complete dynamic rerun remains outstanding while neutral debugger version 6
is integrated. Prepared player version 2 adds standalone event schemas; the first
player proof covers its empty event section only. Full source corpora, native
products, installed Studio, visual resource persistence, external project runtime
and applicable web/UI acceptance remain incomplete.

The next persistence capture passes five model/shader/mesh cases with 132
assertions in 0.664 seconds, including detached resource tables and failed-read
preservation. Five field cases pass 292 assertions in 0.820 seconds after adding
shared retained-storage budget checks. These are CPU checks with ASan/UBSan.

Prepared execution version 6 now passes the debugger and typed observer cases.
The program selector passes 18 cases/2,011 assertions in 1.022 seconds on VM and
SLJIT. The separate-process handoff passes five writer cases/187 assertions,
nine compiler-free reader cases/514 assertions and five supervisor assertions in
0.532 seconds. Fixes preserve declared nominal type identity for constant state
initializers and distinguish accepted event dispatch from a paused activation.
Pause, step, no-replay effects and retained watch ownership remain asserted.

External Studio configures against the installed desktop SDK. Compilation is
still exposing undeclared consumer dependencies; the directory watcher and app
diagnostics are now Studio-owned, while shared camera, call hierarchy, expression
presentation and Studio descriptors have explicit SDK owners. Complete linking,
runtime UI, prepared visual/UI resource staging, aggregate player budgets and
final packages remain under implementation. No final product acceptance or
landing is claimed by these intermediate captures.

The installed desktop SDK now links the actual external `chibi-studio` executable
from its extracted repository. Studio has no engine checkout/private include
paths. This proves the compile-time consumer contract only; UI and web execution
remain pending. `chibi-compile` and compiler-disabled `chibi-player` also link.
The first actual SpaceCadet offline invocation exposed a source-watch payload
validation failure; diagnostic ownership and the lowering contract are under
repair, so external project playback is not yet accepted.

The installed public consumer now passes primitive math 2 cases/32,241 assertions,
integer 4/111,636, scalar conversion 1/2,820 and Select 1/164 on VM and SLJIT.
Owned Tick snapshots pass 2/2,916; service rebinding/incremental attachment passes
2/62; native reaction/rhythm/particle source cases pass 3/122. Typed material writes
after suspension pass 1/116. These normal-Debug CPU captures each used the 60-second
production supervisor and took under one second. Initial migrated fixture errors
were repaired against actual API ownership/count/source identity contracts. The
numeric/query source cases still fail and remain open.

Renderer resource transactions additionally passed the installed consumer's
1 case/70 assertions, and installed native player/world checks passed 3/188.
Those captures predate the next UI/image ownership extraction and aggregate
execution budget repair. No final candidate validation is claimed. The prepared
UI codec passed 4/53 with ASan/UBSan; that validates its neutral envelope, not the
font backend or rendered UI.

Remaining product extraction is removing authored UI loading from the player,
compiling native assets without a live preview world or desktop dependencies,
and giving prepared decoders explicit allocation accounting. Source system
parallelism remains held until serial membership/Begin preparation and verified
module effects are integrated. Package manifests now distinguish SDK, player and
Studio ownership and are being tied to compiled format/version/hash evidence.

The offline compiler now builds with compiler/native packages enabled and desktop
packages disabled. Its linked dynamic dependencies are only libc++ and libSystem
on this host. CPU visual catalogs, image decoding, mesh loading, fonts and UI
preparation have explicit owners; prepared publication and cache ownership are
separate from renderer/authoring consumers. The desktop compiler/player also link
with these targets. A fresh empty SDK install replaced the earlier raw install so
moved headers cannot survive accidentally in consumer include paths.

New installed consumer captures pass nominal aliases 1/144 in 0.529s and source
numeric/nested-query semantics 2/568 in 0.512s on VM and SLJIT. The inner query
ordinal now resets on outer-loop reentry. The four package/installer cases pass
4/70 in 7.019s; subsequent reviewed backup-recovery changes await recapture.
The latest actual SpaceCadet compilation remains open on nominal-versus-ECS
component identity resolution after exposing and fixing field watch typing.
The shared palette composition case exposed the same ID distinction. A public
persistent-state test exposed unwanted zero materialization of explicit NULL
struct assignments; its preserved NULL oracle is being restored in the owner.

Standalone controller compilation against the installed SDK is in progress.
Its target owns CLI/automation implementation only, consumes renderer and source
services through installed public targets, and removes the dead graph executor
inspection helper. Final root build/test retirement, packaged products, external
project playback and applicable Studio/web acceptance remain incomplete. Source
parallel execution is still held pending independent review of mixed native/source
phase ordering, the verified access proof and final bounded dynamic capture.

The root build now composes SDK targets, the controller, reflection and the
admitted C consumers. Its first full compilation reached 700 of 731 actions;
the remaining inherited pedantic warning in a tooling fixture is under repair.
The standalone controller already links with installed public SDK headers only.
Offline compilation has progressed to prepared capture schema validation; a
producer count mismatch is being repaired in the prepared writer.

Installed consumers now pass persistent state 1/48, full source/native palette
composition 3/7,667, source edits 9/141 and shader parameter persistence 1/31.
The package/installer recapture passes 4/70 in 6.375s. Prepared handoff with shared
decode budgets passes five writer cases/187 assertions, ten compiler-free reader
cases/554 assertions and five supervisor assertions in 1.180s. These captures
precede the next prepared capture-format revision and are not final acceptance.

The installed nine-project semantic pass read 148 sources/255,417 bytes and
passed five projects. Four expose missing component metadata, query capability
or value-method support; full authored semantics remain required. Input observer
ordering and copied host descriptor identity in effect analysis are being fixed
against the existing failing assertions. Separate Studio native packaging and
its browser SDK consumer remain in progress. No prepared game GPU run has occurred.

Root composition now builds all selected products and C consumers successfully.
The input boundary repair passes all eight source-system cases/314 assertions
on VM and SLJIT in 0.704s. The complete semantic corpus passes nine projects,
148 sources/255,417 bytes and 794 assertions in 1.994s. BYTECODE preparation
passes eight of nine projects; two Studio event handlers expose invalid debugger
watch metadata and remain under repair.

Prepared execution wire version 7 fixes lexical capture serialization. A coherent
producer/runtime consumer recapture passes 5/187 writer, 10/554 reader and 1/5
supervisor cases/assertions in 0.799s. The earlier version mismatch run was an
outdated consumer binary and is not acceptance evidence. Six execution-policy
unit cases pass 61 assertions in 0.548s.

An independent ownership audit removed 109 unbuilt obsolete production files,
including the old graph behavioral compiler/executor and runtime facades. Public
source/package includes have no dangling references to that closure. Existing
unported test oracles stay documented. Studio source retirement remains held
until its extracted application has functional acceptance. Browser dependency
packages configure/build, and the installed browser SDK build is in progress.
Release validation also found and repaired two host-completion calls hidden
inside assertions; actual Release runtime recapture remains required.

All nine authored project corpora now pass semantic analysis (9/794), VM
preparation (9/812) and SLJIT preparation (9/812). Offline SpaceCadet compilation
produces a 3,256,763-byte prepared project with eight embedded resource records.
The compiler-free reader process passes its wire-version-7 handoff; the installed
scene consumer passes three cases/51 assertions including canonical markers and
rejection of unknown empty structs in 0.962s. The full admitted tooling process
aggregate passes 36 cases/960 assertions in 19.745s after restoring environment
validation on the retired test-fast entry point.

The Emscripten Release SDK compiles and packages with SLJIT explicitly disabled.
Extracted native Studio builds against installed headers (148 actions), with no
private engine source include paths. These are build/package proofs; browser and
Studio runtime acceptance remain open. First prepared player execution exposed
duplicate shader-directory initialization, now repaired. The next bounded run
loaded fonts but rejected an unregistered prepared component; no frame pass is
claimed. Source scheduling also remains open after the newly enabled native
effect proof exposed a mixed-phase outcome failure. These failures are being
fixed against the preserved assertions, not removed from acceptance.

Prepared SpaceCadet playback now passes both VM and SLJIT through the standalone
installed player, each reporting exactly four frames and three simulation steps
(1.567s and 1.270s). Dynamic prototype tags are reconstructed only from their
validated canonical marker schema. The source registry retains service defaults
and supplies them to prepared-module publication. The expanded scene codec cases pass 3/63
in 0.754s; source systems pass 8/314 and execution programs 18/2,011 after the
activation ownership repair. Scheduling still requires its final parallel proof.

The serial external Studio browser consumer builds against the packaged web SDK.
Its native startup exposed a legacy executable placeholder and repeated component
registration; both are being replaced through the existing source presentation
and world ownership APIs. Historical Studio oracles are preserved as 321 files
with checksums under the extracted repository's tests/held/engine_oracles.
Corpus manifests now reference six distinct external catalogs and 87 sources;
61 byte-identical duplicate entries from the earlier nine-checkout comparison
were removed without dropping any unique authored source.

The deduplicated six-catalog corpus passes semantic analysis (6/471), VM
preparation (6/483) and SLJIT preparation (6/483), over 87 sources/138,371 bytes
in 1.720s, 1.748s and 1.728s. All four admitted scheduling cases pass in 0.520s,
including actual parallel source cohorts, conflicting writes, explicit order,
profiling/inspection barriers, generation replacement and native/source phase
membership. The parallel fixture now gives its independent systems equal order;
the scheduler continues enforcing distinct explicit orders as serial barriers.

Studio startup now reaches the authored UI. It still fails to prepare that UI's
editor_state capability, so UI acceptance and old Studio source removal remain
held. The default scene and source collection initialize; the obsolete scratch
presentation apply has been removed. Global source publication on save/reload
remains independent of the active document.

Corpus input discovery now defaults to curated engine examples/fixtures and
accepts explicit external project lists or exact source manifests. The current
semantic/preparation manifest has six unique extracted catalogs and 87 sources;
prior nine-checkout/148-entry captures included 61 byte-identical game duplicates.
Every migrated source was compared byte-for-byte before deduplication. No unique
source was removed; the six-catalog captures reported above use this exact manifest.

The source registry now owns copied service names and borrowed host values.
New publications inherit defaults; replacements preserve existing overrides,
then apply explicit replacements. Rebinding preflights every program before
mutation. The expanded two-case service/attachment check passes 104 assertions
on VM and SLJIT in 0.737s. Current source systems pass 8/314, scheduling 4/4
cases, and language programs 18/2,011 after this ownership change.

External native Studio startup passes its exact two-frame hidden-window check
in 1.616s against the installed SDK. Source/graph interaction remains pending.
The current prepared player recapture passes four frames and three simulation
steps on both VM and SLJIT. These are bounded execution results, not full render
or browser acceptance. Game extraction retired 283 byte-identical originals
after checking their replacement repositories; the differing SpaceCadet README,
Studio originals and held historical test oracles remain preserved.

## Remove component and runtime name prefixes, 2026-10-11

The runtime component declarations and reflection generator remain the source of
truth. Remove GAME., GAME_, Game and game_ prefixes from active engine code,
authored assets and SDK consumers. Preserve numeric component IDs, use bare
canonical names, and resolve real C identifier collisions with capability names.
Project version 22 migrates saved component keys and source references; older
migration inputs and archival evidence retain the spelling they describe.

Update native headers/callers, reflection output and manifest names, authoring
catalogs and test expectations together. Regenerate bindings, migrate and format
engine fixtures and active consumer projects, then verify engine products and
the installed-SDK Studio consumer. Run admitted native, language and tooling
checks under their production 60-second deadlines. Preserve concurrent edits.

Completed the rename and version 22 migration while preserving all 75 numeric
component IDs. Engine build, SDK packaging, installed-SDK Studio build,
check-format, test-tools, test-native and test-language-all passed. Native tests
also emitted Box3D dependency alignment warnings. Supported fixture and consumer
projects migrated; formatting passed except Tetris's existing input-map fields,
which the current parser rejects. Neon Swarm remains version 5, below the current
migrator's supported range; its names were updated but migration was rejected.
Full/render/Studio runtime suites remain quarantined. Changes are uncommitted;
validation logs and the captured pre-change patch are in
`.chibi/remove-game-prefix/`.
