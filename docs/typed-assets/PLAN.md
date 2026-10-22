# Compiled Chibi assets and configuration

## Approved completion criteria — 2026-10-21

- Remove the former declarative format from every active asset, producer,
  consumer, fixture, instruction and dependency in the engine and Studio.
- Compile asset source with the Chibi-authored Compiler.
- Migrate every tracked asset and embedded fixture, preserving its values.
- Delete the former parser, emitter, vendor library and build/package wiring.
- Put source policy, validation and writing in Chibi. Keep native code limited
  to runtime invocation, owned value transport and platform operations, following
  the repository-root self-hosting plan.

This supersedes the earlier scalar-prototype slice. That slice added native
source policy and left the other asset families and dependency in place. Its
local validation does not establish completion of this migration. Generic Bake
remains deferred. Parameter editing, computed helpers, explicit overrides, Reset,
source history and save must continue through the shared source path.

## Ownership and baseline

### Current-main integration in progress — 2026-10-22

The owner approved continuing from the local stabilization checkpoints.
Engine main is pinned at `444e5b20e60d052e9e280788eec7163ca52f8390` for this
integration. Studio main remains `97f74c7e`. The merge preserves main's
length-carrying strings, prepared format 20, prebuilt SDK dependencies and
compiler self-build workflow while keeping the retired asset format removed.

1. Resolve integration conflicts, recover the descriptor-compatible Compiler
   with its authored bootstrap contract, and regenerate both target resources.
2. Adapt the pinned-runtime self-build proof to the generic compiler-artifact
   protocol and isolate only its manifest-listed compiler/core inputs.
3. Rebuild the installed SDK and paired Studio, then address cold startup cost
   through the production compiler/asset path.
4. Independently map affected document/project/scene/graph contracts to bounded
   checks. Renew only reviewed complete workloads under the same 60-second
   ceiling, preserving holds for coverage that remains unavailable.
5. Review and commit the integrated candidate locally with exact current
   evidence. Main landing and publication remain separate actions.

Earlier results below belong to their recorded snapshots. They do not validate
this in-progress candidate or its new ABI/resources.

Engine: `codex/typed-assets-20261021`, updated to `881941162` before this rework.
Studio: `codex/typed-prototype-editor-20261021`, based on `97f74c7e`.
Both worktrees are under `.worktrees/typed-assets`. Dirty primary checkouts and
unrelated worktrees remain separate. Local recovery commits are `27382e6d8`
(engine) and `1253df1` (Studio). Stabilization integrates engine main at
`cb4cc339d`; no push, merge into main or deployment has occurred.

The initial inventory counted 238 engine and 150 Studio asset files, plus
embedded native fixtures and transport strings. It covers project manifests,
scenes, prototypes, graphs, input maps, scenarios, component presentation,
bootstrap metadata and documentation configuration. Retired standalone game
repositories remain read-only under the workspace instructions.

## Source and runtime contracts

Authored assets are ordinary Chibi packages. A public `Build() -> byte[]` entry
uses typed constructors from `game:assets`; ordinary functions, branches, loops
and imports go through the authored Compiler. Family constructors enforce stable
schema fields. Reflected component data keeps the existing catalog validation.

The shared value library and source writer are Chibi. Their JSON bytes are a
private value transport into the existing native document codec, with exact
integer widths, ordered objects, arrays, nulls and finite real values. Source is
never a JSON string passed to a renamed parser. Clipboard, checkpoints and
machine protocols use the JSON codec directly.

The native boundary loads prepared code, provides an immutable library snapshot,
invokes the authored compiler and pure asset entry, and owns the returned values.
No file, process, clock or random capability is granted to asset code. Shared
execution limits and cancellation cover nested calls. Source and evaluated values
remain distinct. Source edits use CST ranges and expected bytes, preserve helper
expressions and comments, and reject unsafe rewrites.

Bootstrap version policy is compiled into the compiler package. Early recovery
reads the existing artifact metadata, then checks the recovered compiler's
current contract. This avoids a new native source parser or a compiler-selection
cycle. Documentation schemas and validation are authored Chibi; filesystem
confinement and atomic publication remain platform operations.

## Work and deletion gates

1. Build the shared typed value library, compiler facade and authored writer.
   Prove actual recursive values, helper evaluation and compiler diagnostics.
2. Supply the portable runtime bridge and target-compatible immutable compiler
   artifact/library bundle in source, installed SDK and browser builds.
3. Migrate engine and Studio assets, embedded fixtures, discovery, creation,
   editing, saving, reference paths and protocols together. Projects become
   version 33. Retire historical format-specific migration implementations.
4. Move source mapping, reconciliation and parameter/override policy to Chibi.
   Keep the source session's validation and single undo transaction boundary.
5. Remove the old parser/emitter, vendor directory, exported components, sources,
   headers, defines, include paths, licenses and packaging references together.
6. Check source and relocated SDK builds, Studio bootstrap and browser packaging;
   inspect final tracked files and linked/installed inventories for leftovers.
   Use the existing production supervisor for any authorized runtime checks.

A one-time ignored migration utility may read the old format while converting
tracked data. Delete that utility and its outputs before completion. It is not a
compatibility path or an installed dependency.

## Status

The migration is checkpointed in the two worktrees. All 238 engine and 150 Studio
assets from the initial inventory, embedded fixtures, producers and consumers
use compiled Chibi modules. The retired parser/emitter, vendor library, scalar
evaluator, frozen migration implementations and dependency/export/license wiring
are deleted. One-time migration utilities and snapshots have been removed.
Tracked and untracked source/path audits found no retired-format references;
active build outputs and both installed SDK inventories are clean.

Source writing, syntax metadata, parameter changes, reconciliation, materializing
an explicit detach, value encoding, schemas and resource generation are Chibi.
Native code supplies runtime invocation, owned transport, reflection and platform
operations. The obsolete prototype-specific grammar changes were removed.

### Initial migration validation

These are production builds and compiler invocations, not runtime suite passes.
All supervised runtime commands used the unchanged 60-second ceiling.

| Work | Result | Evidence under `.chibi/` |
| --- | --- | --- |
| Engine/controller/compiler and existing fixture drivers | Native build passed | `asset-rework-final-native-build.log` |
| Native SDK and Studio | Packaged and rebuilt | `asset-native-sdk-package.log`, `asset-native-studio-build.log` |
| Browser SDK and Studio | Built, packaged and installed | `asset-rework-web-build.log` |
| Installed and relocated public SDK consumers | Configured and compiled | `asset-sdk-consumer-installed.log`, `asset-sdk-consumer-relocated.log` |
| Studio starter project | Normal compiler check passed, 9.376s | `asset-starter-project-compile.log` |
| Recursive values, helpers, exact integers, Unicode and finite reals | Native compilation and evaluation passed | `asset-schema-proof.log` |
| Whole-record prototype overrides | Compiled and evaluated, siblings preserved | `asset-overrides-proof.log` |
| Negative unsigned integer | Compiler rejection with mapped source range | `asset-negative-proof.log` |
| Wasm Compiler followed by asset Build | Native output parity, 1.844s | `asset-wasm-proof.log` |
| Largest migrated asset, 96,181 source bytes | Compiled and evaluated, 15.634s; 1,864,944 prepared bytes | `asset-largest-build.log` |
| Installed documentation command | Eight pages generated, 50.511s | `asset-installed-docs-build.log` |

Both native64 and wasm32 resource artifacts match their manifest checksums and
all 34 source fingerprints. Native SDK identity:
`77d65d89843c583cfa6a2b1a2b526192c4818525ca4c4af5e0e8b3c4ecf88ac9`.
Browser SDK identity:
`2566ee31796e298b53457797afe274f6a77c346191cbbc6e76640697bd47de6b`.
Studio's 476 extraction consumer hashes and 269 current provenance records were
verified against the migrated files and immutable origin records.

### Limits and delivery state

Full native, render and Studio suites were not run. The former scalar evaluator's
three standalone prototype admissions were withdrawn; the rewritten compiler
workloads require fresh review before execution. Existing fixtures were compiled.
Earlier scalar-slice results do not establish replacement-path runtime behavior.

Cold compilation in the native Debug configuration took about five seconds for
a small distinct asset before the stabilization integration. The thread cache
holds up to 128 prepared assets/128 MiB. The stabilization adds persistent
prepared-code caching; its focused runtime evidence is recorded below. Source,
transport, value, depth, instruction and cancellation limits are implemented;
a recoverable aggregate VM allocation quota remains separate work. Arbitrary
computed source shapes can be evaluated, but edits that cannot preserve their
source structure return a diagnostic.

Validation used macOS arm64 and Emscripten/Node. Linux and Windows were not built
in this rework. The recovery commits and subsequent stabilization are local and
unpushed; they are not merged into main or deployed. Dirty primary checkouts are
unchanged.


## Approved stabilization follow-up

The owner approved starting on 2026-10-22: checkpoint this migration, integrate
current main and its existing compiler performance work, add persistent compiled
asset caching, and establish focused runtime/editor validation. Current main now
contains result-location construction from PR 642. Shared-string and ownership
changes remain with their existing worktrees and semantic discussion.

### Plan and system impact

1. Review and checkpoint both owning worktrees locally, recording unexecuted
   runtime coverage. Integrate current engine main and reconcile new graph/test,
   SDK and license changes without restoring the retired format or dependency.
2. Keep persistent cache decisions in authored Chibi. Native code provides
   bounded owned transport and explicit cache storage outside pure asset code.
   Cache prepared code only, so every load still evaluates Build with the
   current cancellation and instruction limits. The key covers source/path,
   explicit imported sources/collections, compiler bytes and target/catalog ABI.
   Treat missing, stale, malformed, truncated and incompatible entries as misses;
   publish complete entries atomically. Cache storage is optional and cannot make
   valid source fail when unavailable. Respect concurrent workers and processes.
3. Rebuild compiler/bootstrap/resource closures after compiler ABI changes and
   build the SDK, native/browser Studio and relocated public consumers. Review
   actual compiler work and cache state before restoring standalone runtime
   admissions. Check cold/warm behavior, key invalidation, corrupt entries,
   parameter/override/Reset/history/save, and actual evaluated migrated values
   under the unchanged shared 60-second runtime supervisor.
4. Record remaining work accurately. Aggregate recoverable allocation ceilings,
   automatic project-helper snapshots and broader schema/edit patterns are next
   increments; do not fold in paused ownership semantics or legacy compatibility.

Critical files: `src/authoring/asset_compiler_host.[ch]`, authored compiler/source
services and resource generator, `tools/graph/engine/{Engine,Targets}.chibi`,
`sdk/asset_documents.cmake`, SDK packaging/export code, existing engine and Studio
fixture dispatchers and test policy, plus owning project/session lifecycle calls
that select optional cache storage. Extend shared APIs rather than adding a
separate compiler or test executable. No publishing or merging into main is
implied by this local stabilization checkpoint.

Status: both migration checkpoints exist. Engine main is integrated locally,
compiler self-compilation parity passed, and native/browser SDK and Studio builds
passed. Focused runtime, browser-cache and editor-model validation passed in the
configurations below.


Pre-checkpoint review found correctness work now implemented for stabilization:
validate decoded Build output before recursive native conversion; align projected
value depth with the inclusive 1024-level authored quota; reject unsupported
decoded embedded NUL until the string representation supports it; verify complete
source inventory and hashes before packaging compiler resources; and regenerate
stale resources without assuming the destination build host already exists.

### Persistent cache contract

The authored `asset_compiler/Cache.chibi` owns key admission and eviction. SHA256
of the actual compiler artifact and exact compile request identifies prepared
code. Source, paths, explicit imports/collections, target layout and native catalog
are covered. Budgets and cache location are excluded. Every read evaluates Build
under its current instruction, cancellation and pure-execution scope; evaluated
documents are never cached. Invalid framing, checksum, prepared code or entry
contract falls back to one fresh compilation.

Native storage uses checked binary records, nonblocking publication locks and
atomic replacement. Retention is bounded to 1024 records / 256 MiB. Explicit
`asset_compiler_options.cache_directory` overrides the default; an empty string
disables persistence. Otherwise `CHIBI_ASSET_CACHE_DIR` overrides the platform
user-cache location. Missing or unavailable storage is a disposable miss.

Browser sessions use MEMFS at `/cache/chibi-assets` and an isolated IDBFS staging
mount for asynchronous persistence. Restore has a five-second deadline; late
callbacks cannot replace active session records. Cache failures do not mark the
project unsaved. Cache files stay outside project exports, collaboration,
checkpoints and undo. Separate browser tabs can replace each other's disposable
snapshots; cross-tab publication locking is not provided.

Compiler input inventories and fingerprints are checked during build and before
installation. `CHIBI_ASSET_COMPILER_RECOVERY` permits stale source fingerprints
only for rebuilding tools; installation is refused in recovery mode. Resource
generation is authored Chibi at `tools/toolchain/AssetCompiler.chibi`, with the
shared implementation under `tools/graph/asset_compiler`.

### Stabilization evidence

Each runtime command below used the unchanged production 60-second supervisor.
Configuration and build time are separate. Exact artifact, source and binary
identities are retained with the focused logs.

| Check | Configuration | Result | Evidence under `.chibi/` |
| --- | --- | --- | --- |
| Compiler bootstrap self-compilation | Merged sources | Exact artifact parity | `asset-stabilization-bootstrap-compile.log`, `asset-stabilization-bootstrap-export.log` |
| Engine/controller/compiler and fixture drivers | Native Debug, sanitizers off | Built after final ownership fix | `asset-stabilization-final-native-build.log` |
| Native SDK and Studio | Debug, sanitizers off | Packaged and built after final ownership fix | `asset-final-debug-sdk-package.log`, `asset-final-debug-studio-build.log` |
| Installed and relocated public SDK consumers | Current Debug SDK | Configured and compiled | `asset-final-sdk-consumer-installed.log`, `asset-final-sdk-consumer-relocated.log` |
| Browser SDK and Studio | Emscripten Release | Packaged and built after final ownership fix | `asset-final-web-build.log` |
| Native SDK and Studio acceptance | Release, sanitizers off | Packaged and built after final ownership fix | `asset-final-build-evidence.json` |
| Execution policy | Six C cases | 6 cases / 61 assertions, 1.296s | `asset-stabilization-test-short-second.log` |
| Native language semantics | Debug | 63 cases / 824 assertions, 1.508s | `asset-stabilization-language-semantic.log` |
| Complete C tooling aggregate | Debug | 127 cases / 34,870 assertions, 21.128s | `asset-stabilization-test-tools-second.log` |
| Existing browser project-storage bridge | C-owned transport fixtures | 19 cases / 97 assertions, 1.934s | `asset-stabilization-web-storage-second.log` |
| Prepared asset cache | Debug, isolated cache | 2 cases / 36 assertions, 25.669s | `asset-cache-second.log` |
| Decoded transport and writer boundaries | Current installed Release SDK, cache disabled | 3 cases / 1,039 assertions, 27.318s | `asset-transport-release-current.log` |
| Actual browser cache adapter | Chrome, serial Wasm, real IDBFS | 5 C cases / 32 assertions, 9.121s | `asset-cache-browser-four-first.log`, matching hash log |
| Registered Studio editor-model route | Current Release SDK, disk cache disabled | 1 case / 73 assertions, 12.145s | `asset-final-model-tool.log`, `asset-final-model-tool-evidence.json` |
| Recovery installation and source inventories | Complete temporary setup included | Passed, 2.670s | `asset-build-guards.log` |

Current native Debug SDK identity:
`60ca0af147227a0e71f8b3ffcd12b8fca02d16be83b1e922a3d49535680bd41b`.
Native Release SDK identity:
`c2ee63a58e93f69bfba286adec0a000ccf2a654d65a96d6cc849b702b3645325`.
Browser SDK identity:
`ade4f33742460f11730a5f538e735268d55bdf25f54d646308f08b78a90a8fba`.
Both target asset compiler resources match all 36 input fingerprints. The merged
compiler bootstrap matches all 19 compiler inputs and passed self-compilation.

The complete Studio CPU-model fixture passed 1 case / 73 assertions in 14.466s
against the current Release SDK, with an initially absent private cache: eight
cold compile attempts, 39 memory hits and no persistent hits. It exercises three
production Properties actions (UInt parameter, override and Reset), exact
Undo/Redo, formula/comment preservation, Save/Reload and external source-only
merge/conflict handling. It uses two tiny files, no actors or checkpoints, and
zero host frames or renders. Evidence is retained under the task artifact's
`compiled-asset-edit/model4/{native.log,evidence.json}`. This does not prove UI
hit testing or startup. The admitted repeatable route disables disk caching;
14.466s is the fresh-private-cache measurement. After formatting the new fixture,
the actual registered disk-disabled route passed the same complete contract in
12.145s, with all source/binary/configuration pins matching.

The model regression also found and fixed a real heap ownership error:
`asset_source_highlight` used the stretchy-array deallocator on the plain
`xmalloc` result from `authoring_source_syntax_highlights`. The adapter now uses
`free`; the unchanged source-opening regression passed after all SDK rebuilds.
The failed run's log and crash report are retained.

### Open stabilization limits

The complete Debug transport command timed out at 60.094s; its accepted/rejected
depth observations do not make that command a pass. The successful Release route
has a separate configuration-specific admission.

Cold full Studio startup exceeded the same deadline before the editor fixture
ran in both Debug (60.076s) and Release (60.057s). The runs committed 11 and 42
cache records respectively; neither reached edit assertions or produced a
screenshot. Failed-run logs, evidence and the model crash report are retained in
the task's visualization directory; disposable project/cache copies were removed.
Persistent caching reduces repeated compilation, but
this checkpoint does not establish a completed cold or warm full Studio startup.

Native, project, material, language and Wasm aggregates containing newly compiled
assets are held until their complete new workloads are reviewed and measured.
Historical parse-only timings do not admit the replacement compiler path. The
retained broad Studio routes are also held. Focused passes do not clear these
holds or the full native/render/Studio quarantines.

Recoverable aggregate VM allocation limits, automatic project-helper snapshots,
broader schema/edit shapes, exhaustive evaluation of every migrated asset, and
Linux/Windows and threaded-browser validation remain follow-up work. Generic Bake
remains deferred.
The authored compiler also rejected the focused fixture's explicit
`double(value)` call as an unknown function. The fixture uses the supported UInt
value constructor directly; general compiler feature parity is not established.
