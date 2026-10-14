# Authored Studio migration

Approved by the user on 2026-10-13. Implement the entire editor in RML, RCSS and `.chibi`. This is an execution plan, not an approval request.

## Candidate and branch audit

- Reuse the clean paired asset-authoring worktrees. New branch in both repositories: `cwaldner/studio-authored-migration-20261013`.
- Engine starts at `3b90a25f90c268b65abe8da4e8167c6ce77957dc`; Studio starts at `34f13325c3fb4e1888dbd64bf45d809e07c782d3`. Preserve those original branches.
- Freshly fetched engine main: `37718e5eb58a468fb4503ca956dad5aa2a426ec1`. Studio main: `78cb4d4c47242db9f7af2b9573653d9756050e99`.
- Audited 489 engine refs, 39 Studio refs, 18 checked-out worktrees and both repositories' open PRs. No full migration already in progress; no open PRs. Prior RML migration branches are merged. Unmerged historical command/intelligence prototypes do not implement this migration.
- Protect the dirty primary engine and Studio checkouts. No reset, stash or cleanup there.
- Audit details and all task scratch/evidence live under `.chibi/studio-authored-migration/`.

## Completion contract

Studio is authored application content. RML owns structure; RCSS owns styling; `.chibi` owns selection, commands, preferences, panel routing, draft values, interaction policy, workflow state and error presentation. No Studio C/C++ implementation or native application bootstrap remains. Moving a Studio controller into the engine, or binding static markup to a native controller, does not satisfy this contract.

The engine supplies reusable document, semantic, resource, text/input/render, preview, platform and transport services. Services have explicit ownership, bounded work, revision checks and typed results. They know no Studio component names, menu commands or panel selection. A generic host runs the authored application on desktop and web. CLI operations and the editor consume the same authoring implementations.

Preserve existing editing behavior, gesture transactions, cancellation, undo/redo, document navigation, unsaved state, save/conflict handling, library refresh, preview settings, CPU diagnostics and frozen evidence export. Keep shared component styling and keyboard/accessibility behavior. Seven visible dropdown choices maximum, with all choices accessible.

## Inventory

Initial Studio inventory: 156 native `.c`/`.cpp` files, 101,638 source lines excluding headers. Authored project: 10 behaviors, 81 RML files, 102 RCSS files and 60 KDL files.

| Migration group | Native files | Source lines |
| --- | ---: | ---: |
| Shell, commands, preferences, host and other panels | 19 | 15,973 |
| Graph canvas and node/member panels | 22 | 18,151 |
| Workspace, projects, runtime and scene tools | 30 | 16,788 |
| Documents and history | 17 | 11,764 |
| UI kit and text bridge | 19 | 9,752 |
| UI designer and inspector | 8 | 6,381 |
| Asset/project browser and import/export | 10 | 6,072 |
| Properties and Outliner | 4 | 4,251 |
| Text, search and refactor panels | 3 | 4,080 |
| Collaboration | 7 | 3,223 |
| Material/model previews | 3 | 2,330 |
| Graph document view/evaluation | 8 | 2,191 |
| Bootstrap, diagnostics and registry | 6 | 682 |

## Implementation

- [x] Audit existing work before creating branches.
- [x] Record the expanded initiative and inspect current contracts/history.
- [x] Integrate current main, preserving both test admission lists. Merge f04b76ef; admitted native/projects/materials/language/tools passed in 20.821/26.443/26.817/11.532/25.853 seconds. Incoming c_export test needed unistd.h on macOS.
- [ ] Add owned workspace document sessions and typed capabilities: revisioned text, transactions, cancellation, undo/redo, dirty/conflict/save and bounded path access.
- [ ] Add typed paginated resource queries with revisions and explicit refusals.
- [ ] Add document semantic inspection/edit bindings: source diagnostics/navigation/completion/refactors, graph inspection/validated mutations and UI authoring.
- [ ] Extend reusable UI input events and document-backed text control for authored interaction.
- [ ] Add owned visual authoring sessions, typed material/model inspection/editing, preview texture resources and bounded evidence jobs.
- [ ] Compose optional authoring capabilities and service lifetimes in a generic engine host. Verify with an independent authored utility, including failure/cancellation paths.
- [ ] Migrate shell, command definitions/routing, preferences, status and area layout to authored state/events.
- [ ] Migrate document tabs/history/save/conflicts and project/file workflows.
- [ ] Migrate asset browser, collections, import/export and resource selection.
- [ ] Migrate material/model Properties and diagnostics, retaining creation/edit/cancel/undo/save/refresh/sample/export parity.
- [ ] Migrate Outliner and component Properties over generic document/scene operations.
- [ ] Migrate source editor/search/refactor and graph canvas/member/evaluation interactions.
- [ ] Migrate UI designer, scene tools, cameras/gizmos, play/debug/checkpoints and collaboration.
- [ ] Remove native Studio controllers, kit reconciliation, registry/bootstrap and native source/dependency manifests. Retire application-specific engine Studio bindings.
- [ ] Package Studio as authored project content using only installed SDK tools; update distribution/web tooling.

## Ownership and validation

Implementation checkpoint, 2026-10-13 (migration remains in progress): owned
document/resource/semantic/visual/UI/platform services and the generic host are
on disk. Source-only Studio CMake and SDK product packaging are implemented.
The new host library compiles. Authored source, package, native rendering and web
acceptance are separate outstanding gates; native Studio controllers remain.

First packaged SDK validation is complete. Its build ID is
`4ddc1bf8d52a193bfb322a60873833add20de1d8642fe72f341ffa3c2b3190ee`;
manifest SHA256 is `fd4cc3111df70ff36c87f8619c7856288e6633a9fdbe6f1c4a5896b12f99e27c`.
Strict standalone headers, exports and module dependencies passed. Studio
configures with `LANGUAGES NONE` against that installed SDK and its actual
project migrated from format 23 to 24. Individual source/editor, document,
scene, preview, browser, properties, output, performance, debugger and UI
behaviors compiled against it. Whole-app preparation found a false aggregate
memory refusal; the compiler fix and second SDK are in progress.

Fresh required groups before the subsequent source batch passed separately:
native 19.158s, materials 27.233s, language 12.210s, tools 25.818s and projects
27.912s. Focused selectors passed workspace-authoring 43 cases/1112 assertions,
workspace-services 33/967, host-services 25/292, code-control 10/53 and
authored-surface 5/113. New portability, source-preview, layout, evidence and
compiler fixes require fresh checks. These CPU results establish no rendered
editor or browser acceptance.

A material simulation regression identified the shared entity-event queue cap
as too small for an existing 4000-collider workload. Its cap is now 65536;
UI activation remains separately capped at 1024 events and 16 MiB. The bounded
event regression passed 3 cases/65557 assertions, then the full materials group
passed within its existing supervised deadline.

Render resources, project declared types and rooted image loaders have separate
owned scopes. These three globals could otherwise cross between Studio and its
target project. Catalogs stage complete replacement generations before adoption.
Project switches preflight fresh services and an application world before
retiring the old one. Source UI previews own their reader/overlay generations
and retire before borrowed target worlds; runtime acceptance is pending.

One mounted instance per panel moves between authored pane areas, preserving
control identity and avoiding duplicate fixed IDs. Home uses an inactive generic
host with no user-root scan and no temporary project. CLI model previews use the
same production viewport implementation as Studio. Source-only runtime and
model evidence jobs freeze inputs before readback; their fresh integration is
pending. Native retirement maps live in `.chibi/studio-authored-migration/native-retirement/`;
these map old files and unique assertions to replacements and open gaps.
No native Studio application code has yet been removed.

The host owns service sessions and releases jobs, GPU resources, document buffers and project worlds before shutdown. Authored behavior state owns user intent and temporary UI state. Handles are service-scoped and cannot alias closed resources. Revision checks refuse stale edits without partial mutation. Saving compares the disk baseline, preserves dirty state on conflict and writes atomically. Preview worlds are distinct from Studio's own application world.

Write meaningful pico_unit cases in existing C dispatchers for capability behavior. Use an ordinary authored fixture to prove bindings do not depend on Studio. Format changed authored assets and validate their actual capability resolution. Native/web rendered checks must exercise real controls, not only data injection. Preserve historical evidence as historical; collect fresh evidence for the migrated application with exact source, SDK, backend, dimensions, clock and artifact identities.

Required admitted groups: native, materials, projects, language and tools, each under the production 60-second supervisor. Configuration/build times are separate. Full native/render/Studio aggregates remain quarantined and must not be reconstructed in chunks. Run admitted relevant GPU checks only with reviewed bounds. Report source checks, package builds, native runtime and browser runtime separately. No deployment or merge is implied by local evidence.

Before each commit review ownership, failure paths, interface depth, dead code and the full affected diff. Record verified decisions and evidence in shared Hindsight. Do not mark this migration complete until every inventory group is accounted for and native Studio application code is gone.


Prepared decode budget correction (2026-10-13): the whole authored application
exposed cumulative charging of sequential verifier/optimizer peaks. LLDB observed
StudioCollaboration requesting 61,122,001 preparation bytes for 37 functions while
50,571,323 aggregate bytes remained; module closure and effects estimates were
only 1,873 and 72 bytes. Preparation now checks one function peak at a time,
charges actual retained function growth, and checks module finalization in its
own phase. The hard 64 MiB aggregate bound is unchanged. A 32-function fixture
with 1,024 scalar slots per function fails against the original decoder and
passes against the corrected decoder with the same 5 MiB budget. A budget below
actual retained storage still refuses atomically. The admitted language-prepared
producer/runtime-only-consumer check passed in 1.506s; logs are under
`.chibi/studio-authored-migration/validation/integration_checks/budget-{red,final}.log`.
Whole-app acceptance with the next installed SDK remains pending.


Authored-tool preparation profile (2026-10-14): independent 64 MiB decodes of
all 29 Studio module records measured 139,993,314 bytes of unique retained
canonical storage. A separate source-phase measurement was 162,072,442 bytes;
718 owned function pointers had no aliases. The complete tool therefore exceeds
the game budget even after the sequential scratch-accounting correction.
Trusted callers now select an explicit authored-tool profile of 256 MiB through
`chibi-compile --authoring`, CLI validation `authoring: true`, or `chibi-author`.
The default game budget, encoded input cap and individual module cap remain
64 MiB; no artifact field can select or enlarge the profile. Canonical storage
is distinct from RSS, preparation scratch, JIT code, simulation and GPU storage.
The two-module runtime-only regression retained 97,868,583 bytes from 8,483,278
encoded bytes: game refusal, authored acceptance and insufficient-budget rollback
passed under the production supervisor in 6.689s. Package and rendered acceptance
for this profile remain pending. Detailed evidence is in integration_checks
`sdk2-{decoded-modules,storage-banks}.log` and `sdk3-profile.log`.

## Installed SDK3 checkpoint — 2026-10-14

The installed native SDK3 is immutable for the current Studio acceptance pass:

- Build ID: `d862a268337dcae8d6a842d34d3f099135e89e96e1bacaa31f118815223e51a3`.
- Exact package manifest SHA256: `993ce77f18783674c240f1c01c1ad5b1da0074635d58f0670959dd4bd1351296`.
- Dirty source snapshot: `dbd402f7b584753555753149fdf2f259d8fc8fc942252c54554d2ec84013aefd`, based on `f04b76ef`.
- All five required groups passed freshly. Focused workspace-authoring 67/1619, workspace-services 50/1195, host-services 30/431, RNG isolation 4/431, generated-C alias 1/997 and host-core 4/142 passed separately.

Independent decoding measured 139,993,314 bytes of uniquely owned canonical
program storage across the then-current 29-module authored application. A
visited-pointer inspection found 718 owned functions and zero duplicates. The
explicit authored-tool preparation profile now permits 256 MiB; consumer-game
defaults remain 64 MiB, and encoded/per-module/depth limits are unchanged. An
aggregate fixture retaining 97,868,583 bytes verifies default refusal, authored
success and atomic failure with an insufficient caller budget. This bounds
canonical decoded storage, not RSS, JIT, activations or GPU memory.

SDK3 consumers have been promoted into the actual Studio project, including
typed material creation, prototype workflows, recoverable file history,
correlated document births, scene projection/presentation controls and shared
authored notifications. The first complete application compile caught a stale
eight-field event sender after the ninth InitialTexts field was added; that
sender and its isolated fixture schemas were corrected. The next attempt reached
resource preparation and caught an undefined debugger font token. Those findings
show why individual module compiles do not establish whole-application acceptance.
Full preparation, native replay and browser acceptance are still in progress.

Next SDK work includes preserved document history checkpoints, exact component
references, measured pixel ratio, additional graph operations, input forwarding
and remaining thumbnail kinds. Current editor-native source files have not yet
been retired. No candidate has been merged or deployed.

An essential downstream adapter is isolated at the paired `chibi-examples`
worktree on the same migration branch, based on `a5a86f6`. Its Tetris publisher
consumes the authored `author.json`/`files.json` contract and committed source tree;
it preserves the game pin and assets. The existing C publishing dispatcher
passed six cases and 68 assertions in 11.051 seconds under the production
60-second supervisor. This proves packaging/rollback behavior with temporary Git
fixtures; it establishes no browser, gameplay or deployment result. Its receipt
is `.chibi/studio-authored-migration/validation/examples-publishing/receipt.json`.


SDK3 package capture (2026-10-14): all five admitted required groups passed
(native, materials, language, projects and tools), together with the bounded
authoring/services/host/RNG/C-export checks. Installed SDK build
`d862a268337dcae8d6a842d34d3f099135e89e96e1bacaa31f118815223e51a3`
has package manifest SHA-256
`993ce77f18783674c240f1c01c1ad5b1da0074635d58f0670959dd4bd1351296`.
The exact whole Studio source compile passed authored syntax after correcting
Graph's ninth InitialTexts batch argument. Standalone widget resource collection
then exposed missing font-role fallbacks; the source owners repaired those.
Current preparation is blocked by dynamic preview image bindings: the collector
rejects every img data-attr-src despite generic context-bound runtime textures.
A supported prepared image contract is required in the next SDK; whole-app
artifact, native Home replay and browser acceptance remain unpassed. Logs and
stable before/after input hashes are under integration_checks/sdk3-*.

### 2026-10-14 integration: SDK4 application and SDK5 checks

SDK4 installed build `0b947518ca200db23b86fa963b9b98e67b5c5b0d1dc85d0b2b8099cdf6c66dd7`
compiled the complete authored Studio project: 727 stable input files, 32,296,938-byte
prepared artifact, SHA-256 `89ad64978dbe27b2c2ed8e11ea84c0eefc54536485ee96c3b1ab25207a250a8c`.
This supersedes the SDK3 dynamic-image preparation blocker: generic texture-view
resource contracts and static font fallbacks are implemented and checked. The
admitted 24-frame native Home flow failed before UI execution because static
service validation required workspace_resources in an inactive host. It did not
produce native UI acceptance.

SDK5 installed build `cef077585ca28f1edfb80ccc00d6344026bec1bf347104974b1303d08d210a21`
has manifest SHA-256 `4aab3609c818da3813741e160ab972068e9540e0206a3773af8253921c230baa`.
Strict standalone packaging and all five required groups pass: native 22.555s,
materials 26.832s, language 11.572s, projects 28.498s, tools 127 cases/28,524
assertions in 26.918s. Bounded owning selectors pass: workspace authoring
89/2,383, workspace services 73/1,738, host services 40/794, authored surface
7/166, code control 11/54, event payload 3/65,557, random isolation 4/431.
The inactive-service audit now checks explicit typed unavailable results without
project services. These are CPU/native and SDK closure results; quarantined
native/render/Studio aggregates remain disabled and unpassed.

The promoted SDK5 Scene consumer exposes a separate compiler failure: the
simulation declaration pack omitted its six host stub bindings. A seven-line
await/result-field reproducer reaches host tuple lowering with bank index -1.
The installed SDK5 remains unchanged; the owner is repairing the declaration
contract and adding an owning regression for SDK6. Full promoted application and
native Home acceptance wait for SDK6. No reduced composition is used to bypass
the failed simulation consumer. Browser acceptance remains pending. Exact logs,
identities and scope records are in validation/integration_checks/sdk5-validation.json
and material-scene-sdk5/minimal-timeline-backtrace.log under the migration scratch.
